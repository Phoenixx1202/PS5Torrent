#ifndef PS5TORRENT_SHA1_H
#define PS5TORRENT_SHA1_H

#include <stdint.h>
#include <stddef.h>

#define SHA1_DIGEST_SIZE 20

/**
 * SHA-1 context structure.
 */
typedef struct {
    uint32_t state[5];      // Intermediate hash state
    uint64_t count;         // Number of bits processed
    unsigned char buffer[64]; // Data block being processed
} sha1_ctx_t;

/**
 * Initialize a new SHA-1 computation.
 */
void sha1_init(sha1_ctx_t *ctx);

/**
 * Feed data into the SHA-1 computation.
 */
void sha1_update(sha1_ctx_t *ctx, const unsigned char *data, size_t len);

/**
 * Finalize the SHA-1 computation and write the digest.
 * @param digest  Output buffer (must be at least SHA1_DIGEST_SIZE bytes)
 */
void sha1_final(sha1_ctx_t *ctx, unsigned char digest[SHA1_DIGEST_SIZE]);

/**
 * Convenience: compute SHA-1 of a buffer in one call.
 */
void sha1_hash(const unsigned char *data, size_t len,
               unsigned char digest[SHA1_DIGEST_SIZE]);

#endif /* PS5TORRENT_SHA1_H */
