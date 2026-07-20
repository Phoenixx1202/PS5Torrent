#ifndef PS5TORRENT_PIECE_MGR_H
#define PS5TORRENT_PIECE_MGR_H

#include <stdint.h>
#include <stddef.h>
#include "sha1.h"
#include "tracker.h"

/**
 * State of a piece in the download process.
 */
typedef enum {
    PIECE_FREE,         // Not yet requested
    PIECE_REQUESTED,    // Currently being downloaded
    PIECE_COMPLETE,     // Downloaded and verified
    PIECE_FAILED        // Download failed
} piece_state_t;

/**
 * A single piece metadata.
 */
typedef struct {
    uint32_t      index;       // Piece index
    piece_state_t state;       // Current state
    unsigned char hash[SHA1_DIGEST_SIZE]; // Expected SHA-1 hash
    int           num_blocks;  // Total blocks in this piece (usually = 1)
    int           blocks_done; // Blocks completed
} piece_t;

/**
 * Piece manager context.
 */
typedef struct {
    piece_t     *pieces;
    size_t       num_pieces;
    int64_t      piece_length;
    int64_t      total_size;
    size_t       num_complete;
    size_t       num_failed;

    // Bitfield of pieces we have (for HAVE messages)
    unsigned char *bitfield;
    size_t         bitfield_len;

    // Verification
    const unsigned char *expected_hashes; // From .torrent pieces field
} piece_mgr_t;

/**
 * Initialize piece manager.
 * @param pm             Piece manager to initialize
 * @param num_pieces     Total number of pieces
 * @param piece_length   Size of each piece (last may be smaller)
 * @param total_size     Total download size
 * @param expected_hashes Array of piece hashes (num_pieces * 20 bytes)
 */
void piece_mgr_init(piece_mgr_t *pm, size_t num_pieces,
                    int64_t piece_length, int64_t total_size,
                    const unsigned char *expected_hashes);

/**
 * Get the next piece to download (rarest/random first strategy).
 * @param pm       Piece manager
 * @param bitfield Peer's bitfield (NULL if unknown)
 * @param bf_len   Bitfield length
 * @return Piece index, or -1 if nothing to request
 */
int piece_mgr_get_next(piece_mgr_t *pm,
                       const unsigned char *bitfield,
                       size_t bf_len);

/**
 * Mark a piece as being requested.
 */
void piece_mgr_request(piece_mgr_t *pm, size_t index);

/**
 * Mark a piece as complete and verify its hash.
 * @param data     Downloaded piece data
 * @param data_len Length of piece data
 * @return 0 if hash matches, -1 if mismatch
 */
int piece_mgr_complete(piece_mgr_t *pm, size_t index,
                       const unsigned char *data, size_t data_len);

/**
 * Mark a piece as failed (can be retried).
 */
void piece_mgr_failed(piece_mgr_t *pm, size_t index);

/**
 * Check if all pieces are downloaded.
 */
int piece_mgr_is_done(const piece_mgr_t *pm);

/**
 * Get download progress (0.0 to 1.0).
 */
float piece_mgr_progress(const piece_mgr_t *pm);

/**
 * Clean up piece manager resources.
 */
void piece_mgr_destroy(piece_mgr_t *pm);

#endif /* PS5TORRENT_PIECE_MGR_H */
