#include "android_sha1.h"
#include <string.h>

static uint32_t rotl32(uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }

void android_sha1_init(android_sha1_ctx *ctx) {
    ctx->state[0] = 0x67452301u;
    ctx->state[1] = 0xEFCDAB89u;
    ctx->state[2] = 0x98BADCFEu;
    ctx->state[3] = 0x10325476u;
    ctx->state[4] = 0xC3D2E1F0u;
    ctx->bitlen = 0;
    ctx->buflen = 0;
}

static void sha1_process_block(android_sha1_ctx *ctx, const unsigned char block[64]) {
    uint32_t w[80];
    uint32_t a, b, c, d, e;
    int i;

    for (i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
    }
    for (i = 16; i < 80; i++) {
        w[i] = rotl32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    }

    a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2];
    d = ctx->state[3]; e = ctx->state[4];

    for (i = 0; i < 80; i++) {
        uint32_t f, k, temp;
        if (i < 20)      { f = (b & c) | ((~b) & d);        k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
        temp = rotl32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rotl32(b, 30); b = a; a = temp;
    }

    ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c;
    ctx->state[3] += d; ctx->state[4] += e;
}

void android_sha1_update(android_sha1_ctx *ctx, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    ctx->bitlen += (uint64_t)len * 8;
    while (len > 0) {
        size_t take = 64 - ctx->buflen;
        if (take > len) take = len;
        memcpy(ctx->buf + ctx->buflen, p, take);
        ctx->buflen += take;
        p += take;
        len -= take;
        if (ctx->buflen == 64) {
            sha1_process_block(ctx, ctx->buf);
            ctx->buflen = 0;
        }
    }
}

void android_sha1_final(android_sha1_ctx *ctx, unsigned char out[20]) {
    unsigned char pad[64];
    uint64_t bitlen = ctx->bitlen;
    size_t padlen;
    int i;

    pad[0] = 0x80;
    memset(pad + 1, 0, 63);
    if (ctx->buflen < 56) {
        padlen = 56 - ctx->buflen;
    } else {
        padlen = 120 - ctx->buflen;
    }
    android_sha1_update(ctx, pad, padlen);
    ctx->bitlen = bitlen;

    {
        unsigned char lenbuf[8];
        for (i = 0; i < 8; i++) lenbuf[i] = (unsigned char)(bitlen >> (56 - 8*i));
        /* Direct block process for the length field: update() would re-add
         * to bitlen, which we don't want after padding is already sized. */
        memcpy(ctx->buf + ctx->buflen, lenbuf, 8);
        ctx->buflen += 8;
        sha1_process_block(ctx, ctx->buf);
        ctx->buflen = 0;
    }

    for (i = 0; i < 5; i++) {
        out[i*4]   = (unsigned char)(ctx->state[i] >> 24);
        out[i*4+1] = (unsigned char)(ctx->state[i] >> 16);
        out[i*4+2] = (unsigned char)(ctx->state[i] >> 8);
        out[i*4+3] = (unsigned char)(ctx->state[i]);
    }
}

void android_sha1(const void *data, size_t len, unsigned char out[20]) {
    android_sha1_ctx ctx;
    android_sha1_init(&ctx);
    android_sha1_update(&ctx, data, len);
    android_sha1_final(&ctx, out);
}
