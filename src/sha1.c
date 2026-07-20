#include "sha1.h"
#include <string.h>

/**
 * SHA-1 implementation (FIPS 180-4 compliant).
 * Clean-room implementation, no external dependencies.
 */

/* Left rotation macro */
#define ROTLEFT(a, b) (((a) << (b)) | ((a) >> (32 - (b))))

/* SHA-1 round functions */
#define F0(b, c, d) (((b) & (c)) | ((~(b)) & (d)))
#define F1(b, c, d) ((b) ^ (c) ^ (d))
#define F2(b, c, d) (((b) & (c)) | ((b) & (d)) | ((c) & (d)))
#define F3(b, c, d) ((b) ^ (c) ^ (d))

/* Round constants */
#define K0 0x5A827999
#define K1 0x6ED9EBA1
#define K2 0x8F1BBCDC
#define K3 0xCA62C1D6

/* SHA-1 round macros */
#define SHA1_ROUND_F0(a, b, c, d, e, i) do { \
    e = ROTLEFT(a, 5) + F0(b, c, d) + e + w[i] + K0; \
    b = ROTLEFT(b, 30); \
} while(0)

#define SHA1_ROUND_F1(a, b, c, d, e, i) do { \
    e = ROTLEFT(a, 5) + F1(b, c, d) + e + w[i] + K1; \
    b = ROTLEFT(b, 30); \
} while(0)

#define SHA1_ROUND_F2(a, b, c, d, e, i) do { \
    e = ROTLEFT(a, 5) + F2(b, c, d) + e + w[i] + K2; \
    b = ROTLEFT(b, 30); \
} while(0)

#define SHA1_ROUND_F3(a, b, c, d, e, i) do { \
    e = ROTLEFT(a, 5) + F3(b, c, d) + e + w[i] + K3; \
    b = ROTLEFT(b, 30); \
} while(0)

static void sha1_transform(sha1_ctx_t *ctx, const unsigned char block[64])
{
    uint32_t a, b, c, d, e;
    uint32_t w[80];
    int i;

    /* Copy block into w[0..15] as big-endian 32-bit words */
    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i * 4]) << 24;
        w[i] |= ((uint32_t)block[i * 4 + 1]) << 16;
        w[i] |= ((uint32_t)block[i * 4 + 2]) << 8;
        w[i] |= ((uint32_t)block[i * 4 + 3]);
    }

    /* Extend to 80 words */
    for (i = 16; i < 80; i++) {
        w[i] = ROTLEFT(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    a = ctx->state[0];
    b = ctx->state[1];
    c = ctx->state[2];
    d = ctx->state[3];
    e = ctx->state[4];

    /* Round 0-19 */
    for (i = 0; i < 20; i += 5) {
        SHA1_ROUND_F0(a, b, c, d, e, i);
        SHA1_ROUND_F0(e, a, b, c, d, i + 1);
        SHA1_ROUND_F0(d, e, a, b, c, i + 2);
        SHA1_ROUND_F0(c, d, e, a, b, i + 3);
        SHA1_ROUND_F0(b, c, d, e, a, i + 4);
    }

    /* Round 20-39 */
    for (i = 20; i < 40; i += 5) {
        SHA1_ROUND_F1(a, b, c, d, e, i);
        SHA1_ROUND_F1(e, a, b, c, d, i + 1);
        SHA1_ROUND_F1(d, e, a, b, c, i + 2);
        SHA1_ROUND_F1(c, d, e, a, b, i + 3);
        SHA1_ROUND_F1(b, c, d, e, a, i + 4);
    }

    /* Round 40-59 */
    for (i = 40; i < 60; i += 5) {
        SHA1_ROUND_F2(a, b, c, d, e, i);
        SHA1_ROUND_F2(e, a, b, c, d, i + 1);
        SHA1_ROUND_F2(d, e, a, b, c, i + 2);
        SHA1_ROUND_F2(c, d, e, a, b, i + 3);
        SHA1_ROUND_F2(b, c, d, e, a, i + 4);
    }

    /* Round 60-79 */
    for (i = 60; i < 80; i += 5) {
        SHA1_ROUND_F3(a, b, c, d, e, i);
        SHA1_ROUND_F3(e, a, b, c, d, i + 1);
        SHA1_ROUND_F3(d, e, a, b, c, i + 2);
        SHA1_ROUND_F3(c, d, e, a, b, i + 3);
        SHA1_ROUND_F3(b, c, d, e, a, i + 4);
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
}

void sha1_init(sha1_ctx_t *ctx)
{
    ctx->count = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xEFCDAB89;
    ctx->state[2] = 0x98BADCFE;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xC3D2E1F0;
}

void sha1_update(sha1_ctx_t *ctx, const unsigned char *data, size_t len)
{
    size_t i;
    size_t idx = (size_t)(ctx->count >> 3) & 0x3F;

    ctx->count += (uint64_t)len << 3;

    if (idx > 0) {
        size_t fill = 64 - idx;
        if (len < fill) {
            for (i = 0; i < len; i++)
                ctx->buffer[idx + i] = data[i];
            return;
        }
        for (i = 0; i < fill; i++)
            ctx->buffer[idx + i] = data[i];
        sha1_transform(ctx, ctx->buffer);
        data += fill;
        len -= fill;
        idx = 0;
    }

    while (len >= 64) {
        sha1_transform(ctx, data);
        data += 64;
        len -= 64;
    }

    if (len > 0) {
        for (i = 0; i < len; i++)
            ctx->buffer[idx + i] = data[i];
    }
}

void sha1_final(sha1_ctx_t *ctx, unsigned char digest[SHA1_DIGEST_SIZE])
{
    uint64_t bits = ctx->count;
    size_t idx = (size_t)(bits >> 3) & 0x3F;

    /* Append 0x80 pad byte */
    ctx->buffer[idx] = 0x80;
    idx++;

    /* If we filled the buffer exactly at the last byte, transform now */
    if (idx == 64) {
        sha1_transform(ctx, ctx->buffer);
        idx = 0;
    }

    /* If we're past position 56, finish this block and start a new one */
    if (idx > 56) {
        memset(ctx->buffer + idx, 0, 64 - idx);
        sha1_transform(ctx, ctx->buffer);
        memset(ctx->buffer, 0, 56);
        idx = 56;
    } else if (idx < 56) {
        /* Pad from current position to position 56 */
        memset(ctx->buffer + idx, 0, 56 - idx);
        idx = 56;
    }
    /* Now idx == 56 (either naturally or after memset above) */

    /* Append length in bits as big-endian 64-bit value at positions 56-63 */
    ctx->buffer[56] = (unsigned char)(bits >> 56);
    ctx->buffer[57] = (unsigned char)(bits >> 48);
    ctx->buffer[58] = (unsigned char)(bits >> 40);
    ctx->buffer[59] = (unsigned char)(bits >> 32);
    ctx->buffer[60] = (unsigned char)(bits >> 24);
    ctx->buffer[61] = (unsigned char)(bits >> 16);
    ctx->buffer[62] = (unsigned char)(bits >> 8);
    ctx->buffer[63] = (unsigned char)(bits);

    sha1_transform(ctx, ctx->buffer);

    /* Output hash as big-endian bytes */
    for (int i = 0; i < 5; i++) {
        digest[i * 4]     = (unsigned char)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (unsigned char)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (unsigned char)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (unsigned char)(ctx->state[i]);
    }
}

void sha1_hash(const unsigned char *data, size_t len,
               unsigned char digest[SHA1_DIGEST_SIZE])
{
    sha1_ctx_t ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, digest);
}
