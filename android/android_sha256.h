#ifndef ANDROID_SHA256_H
#define ANDROID_SHA256_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    unsigned char buf[64];
    size_t buflen;
} android_sha256_ctx;

void android_sha256_init(android_sha256_ctx *ctx);
void android_sha256_update(android_sha256_ctx *ctx, const void *data, size_t len);
void android_sha256_final(android_sha256_ctx *ctx, unsigned char out[32]);

/* Convenience one-shot. */
void android_sha256(const void *data, size_t len, unsigned char out[32]);

#endif
