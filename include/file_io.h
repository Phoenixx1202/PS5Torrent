#ifndef PS5TORRENT_FILE_IO_H
#define PS5TORRENT_FILE_IO_H

#include <stdint.h>
#include <stddef.h>
#include "torrent.h"

/**
 * Output mode for multi-file torrents.
 */
typedef enum {
    OUTPUT_FLAT,    // All files in same directory
    OUTPUT_TREE     // Preserve directory structure
} output_mode_t;

/**
 * File writer context for a torrent download.
 */
typedef struct {
    char      *base_path;       // Base download directory
    torrent_t *torrent;         // Parsed torrent info
    int        multi_file;      // Is multi-file?
    int        fd;              // Current file descriptor for single-file mode
    int64_t    written;         // Bytes written so far

    // Multi-file: current file tracking
    int        current_file_idx;
    int64_t    current_file_offset; // Offset within current file
} file_writer_t;

/**
 * Initialize file writer for a torrent.
 * @param fw        File writer to initialize
 * @param torrent   Parsed torrent
 * @param base_path Directory to write files into (must exist)
 * @return 0 on success, -1 on error
 */
int file_writer_init(file_writer_t *fw, torrent_t *torrent,
                     const char *base_path);

/**
 * Write a piece of data to the output files.
 * Pieces may span file boundaries in multi-file torrents.
 * @param fw    File writer
 * @param data  Piece data
 * @param len   Length of piece data
 * @param piece_index  Piece index (for offset calculation)
 * @return 0 on success, -1 on error
 */
int file_writer_write_piece(file_writer_t *fw,
                            const unsigned char *data, size_t len,
                            size_t piece_index);

/**
 * Finalize and close all files.
 */
void file_writer_finish(file_writer_t *fw);

/**
 * Clean up file writer resources.
 */
void file_writer_destroy(file_writer_t *fw);

#endif /* PS5TORRENT_FILE_IO_H */
