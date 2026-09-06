#include "web_seed.h"
#include "net_utils.h"
#include "app_log.h"
#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int append_url_path(char *out, size_t out_size, const char *base, const char *path)
{
    size_t pos = 0;
    int n = snprintf(out, out_size, "%s%s", base, (base[strlen(base) - 1] == '/') ? "" : "/");
    if (n < 0 || (size_t)n >= out_size) return -1;
    pos = (size_t)n;
    for (const unsigned char *p = (const unsigned char *)path; *p; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '/' || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            if (pos + 1 >= out_size) return -1;
            out[pos++] = (char)c;
        } else {
            if (pos + 3 >= out_size) return -1;
            static const char hex[] = "0123456789ABCDEF";
            out[pos++] = '%';
            out[pos++] = hex[c >> 4];
            out[pos++] = hex[c & 15];
        }
    }
    out[pos] = '\0';
    return 0;
}

static int parse_http_url(const char *url, char *host, size_t host_size,
                          int *port, const char **path)
{
    if (!url || strncmp(url, "http://", 7)) return -1;
    const char *start = url + 7;
    const char *slash = strchr(start, '/');
    if (!slash) return -1;
    const char *colon = memchr(start, ':', (size_t)(slash - start));
    size_t host_len = colon ? (size_t)(colon - start) : (size_t)(slash - start);
    if (host_len == 0 || host_len >= host_size) return -1;
    memcpy(host, start, host_len);
    host[host_len] = '\0';
    *port = 80;
    if (colon) {
        int value = 0;
        for (const char *p = colon + 1; p < slash; p++) {
            if (*p < '0' || *p > '9') return -1;
            value = value * 10 + (*p - '0');
        }
        if (value <= 0 || value > 65535) return -1;
        *port = value;
    }
    *path = slash;
    return 0;
}

static int http_fetch_range(const char *url, uint64_t start, size_t length, unsigned char *out)
{
    char host[256];
    int port = 80;
    const char *path = NULL;
    if (!url || !out || !length || parse_http_url(url, host, sizeof(host), &port, &path) < 0)
        return -1;

    uint32_t addr;
    if (net_resolve(host, &addr) < 0) {
        app_log_write("WARN", "Web seed DNS resolution failed");
        return -1;
    }
    int sock = net_tcp_connect(addr, (uint16_t)port);
    if (sock < 0) {
        app_log_write("WARN", "Web seed TCP connection failed");
        return -1;
    }
    if (net_set_timeout(sock, 30) < 0) {
        net_close(sock);
        return -1;
    }

    char request[1024];
    int req_len = snprintf(request, sizeof(request),
        "GET %s HTTP/1.0\r\n"
        "Host: %s\r\n"
        "User-Agent: PS5Torrent/2.0.4\r\n"
        "Range: bytes=%llu-%llu\r\n"
        "Connection: close\r\n"
        "\r\n",
        path, host, (unsigned long long)start,
        (unsigned long long)(start + (uint64_t)length - 1));
    if (req_len < 0 || (size_t)req_len >= sizeof(request) ||
        net_send_all(sock, request, (size_t)req_len) < 0) {
        net_close(sock);
        return -1;
    }

    char header[8192];
    size_t header_len = 0;
    int header_done = 0;
    size_t copied = 0;
    while (!header_done && header_len < sizeof(header)) {
        char c;
        int n = net_recv_some(sock, &c, 1);
        if (n <= 0) { net_close(sock); return -1; }
        header[header_len++] = c;
        if (header_len >= 4 &&
            header[header_len - 4] == '\r' && header[header_len - 3] == '\n' &&
            header[header_len - 2] == '\r' && header[header_len - 1] == '\n')
            header_done = 1;
    }
    if (!header_done || header_len >= sizeof(header)) { net_close(sock); return -1; }
    header[header_len] = '\0';

    int status = 0;
    if (sscanf(header, "HTTP/%*s %d", &status) != 1 || (status != 206 && status != 200)) {
        net_close(sock);
        app_log_write("WARN", "Web seed HTTP status was not usable");
        return -1;
    }
    if (status == 200 && start != 0) {
        net_close(sock);
        app_log_write("WARN", "Web seed ignored range request");
        return -1;
    }

    while (copied < length) {
        int n = net_recv_some(sock, out + copied, length - copied);
        if (n <= 0) break;
        copied += (size_t)n;
    }
    net_close(sock);
    return copied == length ? 0 : -1;
}

static int fetch_single_file_piece(const torrent_t *torrent, const char *seed,
                                   size_t piece_index, unsigned char *buffer,
                                   size_t length)
{
    char url[2048];
    const char *file_name = torrent->file_name ? torrent->file_name : torrent->name;
    if (seed[strlen(seed) - 1] == '/') {
        if (append_url_path(url, sizeof(url), seed, file_name) < 0) return -1;
    } else {
        snprintf(url, sizeof(url), "%s", seed);
    }
    uint64_t offset = (uint64_t)piece_index * (uint64_t)torrent->piece_length;
    return http_fetch_range(url, offset, length, buffer);
}

static int fetch_multi_file_piece(const torrent_t *torrent, const char *seed,
                                  size_t piece_index, unsigned char *buffer,
                                  size_t length)
{
    uint64_t piece_start = (uint64_t)piece_index * (uint64_t)torrent->piece_length;
    uint64_t piece_end = piece_start + length;
    uint64_t file_offset = 0;
    size_t out_pos = 0;

    for (size_t i = 0; i < torrent->num_files && out_pos < length; i++) {
        uint64_t file_start = file_offset;
        uint64_t file_end = file_start + (uint64_t)torrent->files[i].length;
        if (piece_start < file_end && piece_end > file_start) {
            uint64_t from = piece_start > file_start ? piece_start - file_start : 0;
            uint64_t to = piece_end < file_end ? piece_end - file_start : file_end - file_start;
            size_t part_len = (size_t)(to - from);
            char relative[1024];
            char url[2048];
            int n = snprintf(relative, sizeof(relative), "%s/%s", torrent->name, torrent->files[i].path);
            if (n < 0 || (size_t)n >= sizeof(relative)) return -1;
            if (append_url_path(url, sizeof(url), seed, relative) < 0) return -1;
            if (http_fetch_range(url, from, part_len, buffer + out_pos) < 0) return -1;
            out_pos += part_len;
        }
        file_offset = file_end;
    }
    return out_pos == length ? 0 : -1;
}

int web_seed_fetch_piece(const torrent_t *torrent, size_t piece_index,
                         unsigned char *buffer, size_t length)
{
    if (!torrent || !buffer || !length || !torrent->web_seed_count) return -1;
    for (size_t i = 0; i < torrent->web_seed_count; i++) {
        const char *seed = torrent->web_seeds[i];
        if (!seed || strncmp(seed, "http://", 7)) continue;
        char msg[192];
        snprintf(msg, sizeof(msg), "Web seed request: index=%zu, bytes=%zu", piece_index, length);
        app_log_write("INFO", msg);
        int ok = torrent->is_multi_file
               ? fetch_multi_file_piece(torrent, seed, piece_index, buffer, length)
               : fetch_single_file_piece(torrent, seed, piece_index, buffer, length);
        if (ok == 0) {
            app_log_write("INFO", "Web seed delivered a complete piece");
            return 0;
        }
        app_log_write("WARN", "Web seed request failed; trying next source");
    }
    return -1;
}
