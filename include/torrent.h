#ifndef PS5TORRENT_TORRENT_H
#define PS5TORRENT_TORRENT_H

#include <stdint.h>
#include <stddef.h>
#include "sha1.h"

/**
 * Maximum number of files in a multi-file torrent.
 */
#define MAX_TORRENT_FILES 1024

/**
 * Represents a file entry in a multi-file torrent.
 */
typedef struct {
    char    *path;       // Full path relative to download dir
    size_t   path_len;   // Length of path
    int64_t  length;     // File length in bytes
} torrent_file_t;

/**
 * Represents a parsed .torrent metainfo.
 */
typedef struct {
    // Announce URL
    char    *announce;
    size_t   announce_len;

    // List of announce URLs (for trackers)
    char   **announce_list;
    size_t   announce_list_count;

    // Info dictionary
    unsigned char *info_dict_raw;  // Raw bencoded info dict (for infohash)
    size_t         info_dict_len;

    // Info hash (SHA-1 of bencoded info dict)
    unsigned char info_hash[SHA1_DIGEST_SIZE];

    // Torrent name
    char    *name;
    size_t   name_len;

    // Piece length
    int64_t  piece_length;

    // Pieces: concatenation of SHA-1 hashes
    unsigned char *pieces;
    size_t         pieces_len;   // Total length of pieces data
    size_t         num_pieces;   // Number of pieces (pieces_len / 20)

    // File info
    int      is_multi_file;       // 0 = single file, 1 = multi file
    int64_t  total_size;          // Total download size in bytes

    // Single file mode
    int64_t  length;              // File length (single file)
    char    *file_name;           // File name (single file)

    // Multi file mode
    torrent_file_t *files;
    size_t          num_files;
} torrent_t;

/**
 * Parse a .torrent file from memory.
 * @param data     Contents of the .torrent file
 * @param data_len Length of the data
 * @return Parsed torrent, or NULL on error. Free with torrent_free().
 */
torrent_t *torrent_parse(const unsigned char *data, size_t data_len);

/**
 * Parse a .torrent file from disk.
 * @param path Path to the .torrent file
 * @return Parsed torrent, or NULL on error.
 */
torrent_t *torrent_parse_file(const char *path);

/**
 * Get a hexadecimal string of the info hash (for display).
 * @param torrent  Parsed torrent
 * @param buf      Output buffer (must be at least 41 bytes)
 */
void torrent_info_hash_str(const torrent_t *torrent, char *buf);

/**
 * Free a parsed torrent structure.
 */
void torrent_free(torrent_t *torrent);

#endif /* PS5TORRENT_TORRENT_H */
