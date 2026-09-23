#include "android_apk_digest.h"
#include "android_sha256.h"
#include <stdlib.h>
#include <string.h>

#define CHUNK_MAX (1024 * 1024) /* 1 MiB, per APK Signature Scheme v2/v3 */

static size_t chunk_count(size_t len) {
    if (len == 0) return 0;
    return (len + CHUNK_MAX - 1) / CHUNK_MAX;
}

/* Appends this section's per-chunk digests (32 bytes each) to *digests,
 * growing the buffer; returns the updated chunk count so far. */
static size_t append_section_chunk_digests(const unsigned char *data, size_t len,
                                            unsigned char **digests, size_t *cap_chunks,
                                            size_t n_so_far) {
    size_t off = 0;
    while (off < len) {
        size_t clen = len - off < CHUNK_MAX ? len - off : CHUNK_MAX;
        unsigned char lenprefix[5];
        android_sha256_ctx ctx;

        if (n_so_far == *cap_chunks) {
            size_t nc = *cap_chunks ? *cap_chunks * 2 : 16;
            *digests = (unsigned char *)realloc(*digests, nc * 32);
            *cap_chunks = nc;
        }

        lenprefix[0] = 0xa5;
        lenprefix[1] = (unsigned char)(clen & 0xFF);
        lenprefix[2] = (unsigned char)((clen >> 8) & 0xFF);
        lenprefix[3] = (unsigned char)((clen >> 16) & 0xFF);
        lenprefix[4] = (unsigned char)((clen >> 24) & 0xFF);

        android_sha256_init(&ctx);
        android_sha256_update(&ctx, lenprefix, 5);
        android_sha256_update(&ctx, data + off, clen);
        android_sha256_final(&ctx, (*digests) + n_so_far * 32);

        n_so_far++;
        off += clen;
    }
    return n_so_far;
}

void android_apk_v2_content_digest(const unsigned char *contents, size_t contents_len,
                                    const unsigned char *central_dir, size_t cd_len,
                                    const unsigned char *eocd, size_t eocd_len,
                                    unsigned char out_digest[32]) {
    unsigned char *digests = NULL;
    size_t cap_chunks = 0, n = 0;
    android_sha256_ctx ctx;
    unsigned char top_prefix[5];
    size_t expected_total;

    n = append_section_chunk_digests(contents, contents_len, &digests, &cap_chunks, n);
    n = append_section_chunk_digests(central_dir, cd_len, &digests, &cap_chunks, n);
    n = append_section_chunk_digests(eocd, eocd_len, &digests, &cap_chunks, n);

    expected_total = chunk_count(contents_len) + chunk_count(cd_len) + chunk_count(eocd_len);
    (void)expected_total; /* == n by construction; kept only as a readable invariant note */

    top_prefix[0] = 0x5a;
    top_prefix[1] = (unsigned char)(n & 0xFF);
    top_prefix[2] = (unsigned char)((n >> 8) & 0xFF);
    top_prefix[3] = (unsigned char)((n >> 16) & 0xFF);
    top_prefix[4] = (unsigned char)((n >> 24) & 0xFF);

    android_sha256_init(&ctx);
    android_sha256_update(&ctx, top_prefix, 5);
    android_sha256_update(&ctx, digests, n * 32);
    android_sha256_final(&ctx, out_digest);

    free(digests);
}
