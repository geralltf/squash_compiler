#ifndef ANDROID_SHA1_H
#define ANDROID_SHA1_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[5];
    uint64_t bitlen;
    unsigned char buf[64];
    size_t buflen;
} android_sha1_ctx;

void android_sha1_init(android_sha1_ctx *ctx);
void android_sha1_update(android_sha1_ctx *ctx, const void *data, size_t len);
void android_sha1_final(android_sha1_ctx *ctx, unsigned char out[20]);

/* Convenience one-shot. Needed for the DEX file format's header `signature`
 * field (SHA-1 over everything after it) -- unrelated to APK signing, which
 * uses SHA-256 (android_sha256.c). */
void android_sha1(const void *data, size_t len, unsigned char out[20]);

#endif
