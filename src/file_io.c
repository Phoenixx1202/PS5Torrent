#include "file_io.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

static int safe_relative_path(const char *path)
{
    if (!path || !path[0] || path[0] == '/') return 0;
    for (const char *p = path; *p; p++) {
        if ((unsigned char)*p < 0x20 ||
            (*p == '.' && (p == path || p[-1] == '/') &&
             (p[1] == '/' || p[1] == '\0' ||
              (p[1] == '.' && (p[2] == '/' || p[2] == '\0')))))
            return 0;
    }
    return 1;
}

static int create_parent_directories(char *full_path)
{
    for (char *p = full_path + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(full_path, 0755) < 0 && errno != EEXIST) {
            *p = '/';
            return -1;
        }
        *p = '/';
    }
    return 0;
}

int file_writer_init(file_writer_t *fw, torrent_t *torrent,
                     const char *base_path)
{
    if (!fw || !torrent || !base_path) return -1;

    memset(fw, 0, sizeof(file_writer_t));

    fw->base_path = strdup(base_path);
    if (!fw->base_path) return -1;

    fw->torrent = torrent;
    fw->multi_file = torrent->is_multi_file;
    fw->fd = -1;

    if (!fw->multi_file) {
        /* Single file - open immediately */
        char full_path[1024];
        const char *relative_path = torrent->file_name
                                  ? torrent->file_name : torrent->name;
        if (!safe_relative_path(relative_path)) {
            free(fw->base_path);
            fw->base_path = NULL;
            errno = EINVAL;
            return -1;
        }
        int n = snprintf(full_path, sizeof(full_path), "%s/%s",
                         base_path, relative_path);
        if (n < 0 || (size_t)n >= sizeof(full_path)) {
            free(fw->base_path);
            fw->base_path = NULL;
            errno = ENAMETOOLONG;
            return -1;
        }

        fw->fd = open(full_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fw->fd < 0) {
            int saved_errno = errno;
            free(fw->base_path);
            fw->base_path = NULL;
            errno = saved_errno;
            return -1;
        }
    }

    return 0;
}

int file_writer_write_piece(file_writer_t *fw,
                            const unsigned char *data, size_t len,
                            size_t piece_index)
{
    if (!fw || !data || !fw->torrent) return -1;

    if (!fw->multi_file) {
        /* Single file: seek to piece position and write */
        off_t offset = (off_t)piece_index * fw->torrent->piece_length;
        if (lseek(fw->fd, offset, SEEK_SET) < 0) return -1;

        ssize_t written = write(fw->fd, data, len);
        if (written < 0 || (size_t)written != len) return -1;

        fw->written += len;
        return 0;
    }

    /* Multi-file: pieces may span file boundaries */
    off_t piece_start = (off_t)piece_index * fw->torrent->piece_length;
    off_t piece_end = piece_start + (off_t)len;
    size_t data_offset = 0;

    /* Find which file(s) this piece spans */
    off_t file_offset = 0;

    for (size_t i = 0; i < fw->torrent->num_files && data_offset < len; i++) {
        off_t file_start = file_offset;
        off_t file_end = file_offset + fw->torrent->files[i].length;

        if (piece_start < file_end && piece_end > file_start) {
            /* This piece (partially) belongs to this file */
            off_t write_start = (piece_start > file_start) ? piece_start - file_start : 0;
            off_t write_end = (piece_end < file_end) ? piece_end - file_start : fw->torrent->files[i].length;
            size_t write_len = (size_t)(write_end - write_start);

            if (write_len > 0) {
                char full_path[1024];
                if (!safe_relative_path(fw->torrent->files[i].path)) {
                    errno = EINVAL;
                    return -1;
                }
                int n = snprintf(full_path, sizeof(full_path), "%s/%s",
                                 fw->base_path,
                                 fw->torrent->files[i].path);
                if (n < 0 || (size_t)n >= sizeof(full_path)) return -1;

                if (create_parent_directories(full_path) < 0) return -1;

                /* Open the file and seek to this piece's exact offset. */
                int fd = open(full_path, O_WRONLY | O_CREAT, 0644);
                if (fd < 0) return -1;

                /* Seek to the right position within the file */
                if (lseek(fd, write_start, SEEK_SET) < 0) {
                    close(fd);
                    return -1;
                }

                ssize_t written = write(fd, data + data_offset, write_len);
                close(fd);

                if (written < 0 || (size_t)written != write_len)
                    return -1;

                data_offset += write_len;
                fw->written += write_len;
            }
        }

        file_offset = file_end;
    }

    return 0;
}

void file_writer_finish(file_writer_t *fw)
{
    if (!fw) return;

    if (fw->fd >= 0) {
        close(fw->fd);
        fw->fd = -1;
    }
}

void file_writer_destroy(file_writer_t *fw)
{
    if (!fw) return;
    file_writer_finish(fw);
    free(fw->base_path);
    memset(fw, 0, sizeof(file_writer_t));
    fw->fd = -1;
}
