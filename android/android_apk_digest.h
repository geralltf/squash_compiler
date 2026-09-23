#ifndef ANDROID_APK_DIGEST_H
#define ANDROID_APK_DIGEST_H
#include <stddef.h>

/* Computes the APK Signature Scheme v2/v3 "content digest" (SHA-256
 * variant) over the APK's three logical sections: zip entry contents,
 * central directory, and end-of-central-directory record.
 *
 * CRITICAL, non-obvious wrinkle (found empirically by comparing against a
 * real apksigner-signed APK -- not stated plainly in the public docs
 * summary): the `eocd` bytes passed in here must have their "offset of
 * start of central directory" field set as if the APK Signing Block did
 * not exist, i.e. equal to `contents_len` -- NOT the real, final on-disk
 * offset that actually accounts for the inserted signing block. The
 * signing block is conceptually "virtually removed" before digesting. The
 * ACTUAL file written to disk still needs the real, final offset in its
 * own EOCD (or tools/the platform can't find the central directory at
 * all) -- so the caller must pass a throwaway copy with the field patched
 * to `contents_len` here, while separately writing a copy with the true
 * final offset into the actual output file. See android_apk_sign.c for
 * exactly this two-copy pattern.
 *
 * Per-section: split into 1 MiB (2^20-byte) chunks -- independently per
 * section, never spanning a section boundary, confirmed against the
 * AOSP apksig reference implementation (ApkSigningBlockUtils.java's
 * computeOneMbChunkContentDigests: each DataSource/section resets its own
 * chunk offset to 0) -- and for each chunk compute
 * SHA256(0xa5 || chunk_len_le32 || chunk_bytes). Then the final digest is
 * SHA256(0x5a || total_chunk_count_le32 || all chunk digests concatenated,
 * in section order). */
void android_apk_v2_content_digest(const unsigned char *contents, size_t contents_len,
                                    const unsigned char *central_dir, size_t cd_len,
                                    const unsigned char *eocd, size_t eocd_len,
                                    unsigned char out_digest[32]);

#endif
