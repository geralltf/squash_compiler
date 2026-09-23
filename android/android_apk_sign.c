/* Assembles and inserts an APK Signature Scheme v2 block. Format per
 * source.android.com/docs/security/features/apksigning/v2 (confirmed
 * against the real apksig reference source for the exact per-chunk/
 * top-level digest prefix bytes -- see android_apk_digest.c) and
 * cross-checked end-to-end against real `apksigner verify`. */
#include "android_apk_sign.h"
#include "android_apk_digest.h"
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned char *data; size_t len, cap; } bb;

static void bb_init(bb *b) { b->data = NULL; b->len = 0; b->cap = 0; }
static void bb_free(bb *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }
static void bb_reserve(bb *b, size_t extra) {
    if (b->len + extra <= b->cap) return;
    { size_t nc = b->cap ? b->cap * 2 : 512;
      while (nc < b->len + extra) nc *= 2;
      b->data = (unsigned char *)realloc(b->data, nc);
      b->cap = nc; }
}
static void bb_append(bb *b, const void *p, size_t n) { bb_reserve(b, n); memcpy(b->data + b->len, p, n); b->len += n; }
static void bb_u32(bb *b, uint32_t v) {
    unsigned char le[4] = { (unsigned char)v, (unsigned char)(v>>8), (unsigned char)(v>>16), (unsigned char)(v>>24) };
    bb_append(b, le, 4);
}
static void bb_u64(bb *b, uint64_t v) {
    unsigned char le[8]; int i; for (i = 0; i < 8; i++) le[i] = (unsigned char)(v >> (8*i));
    bb_append(b, le, 8);
}

static size_t find_eocd(const unsigned char *data, size_t len) {
    /* EOCD is at least 22 bytes; search backward for its signature. Our
     * own android_zip.c never writes a zip comment, so it's always the
     * last 22 bytes, but search generally in case this is fed a
     * differently-produced (but still comment-less) zip. */
    size_t i;
    if (len < 22) return (size_t)-1;
    for (i = len - 4; ; i--) {
        if (data[i] == 0x50 && data[i+1] == 0x4b && data[i+2] == 0x05 && data[i+3] == 0x06) return i;
        if (i == 0) break;
    }
    return (size_t)-1;
}

static uint32_t read_u32le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

int android_apk_sign_v2(const unsigned char *unsigned_apk, size_t unsigned_apk_len,
                         const android_rsa_key *key,
                         const unsigned char *cert_der, size_t cert_len,
                         unsigned char **out_data, size_t *out_len) {
    size_t eocd_off, cd_offset, cd_size, contents_len;
    unsigned char *spki_der; size_t spki_len;
    unsigned char digest[32];
    unsigned char *signature;
    int siglen = key->bits / 8;
    bb digests_seq, certs_seq, signed_data, signer, signatures_seq, pair, block;
    unsigned char new_eocd[64];
    size_t new_eocd_len;
    uint32_t new_cd_offset;

    eocd_off = find_eocd(unsigned_apk, unsigned_apk_len);
    if (eocd_off == (size_t)-1) return -1;

    cd_offset = read_u32le(unsigned_apk + eocd_off + 16);
    cd_size = read_u32le(unsigned_apk + eocd_off + 12);
    contents_len = cd_offset;
    new_eocd_len = unsigned_apk_len - eocd_off;
    if (new_eocd_len > sizeof new_eocd) return -1; /* no zip comment expected */
    memcpy(new_eocd, unsigned_apk + eocd_off, new_eocd_len);

    android_rsa_export_spki_der(key, &spki_der, &spki_len);

    /* ---- digests sequence: one record, algo 0x0103 (RSASSA-PKCS1-v1_5 + SHA-256) ---- */
    bb_init(&digests_seq);
    { bb rec; bb_init(&rec);
      bb_u32(&rec, 0x0103);
      bb_u32(&rec, 32);
      bb_append(&rec, digest, 32); /* placeholder; filled in below once we know new_cd_offset */
      bb_u32(&digests_seq, (uint32_t)rec.len);
      bb_append(&digests_seq, rec.data, rec.len);
      bb_free(&rec);
    }

    /* ---- certificates sequence: one X.509 DER cert ---- */
    bb_init(&certs_seq);
    bb_u32(&certs_seq, (uint32_t)cert_len);
    bb_append(&certs_seq, cert_der, cert_len);

    /* signed_data = digests_seq(len-prefixed) + certs_seq(len-prefixed) + additional attrs (empty, len=0) */
    bb_init(&signed_data);
    bb_u32(&signed_data, (uint32_t)digests_seq.len);
    bb_append(&signed_data, digests_seq.data, digests_seq.len);
    bb_u32(&signed_data, (uint32_t)certs_seq.len);
    bb_append(&signed_data, certs_seq.data, certs_seq.len);
    bb_u32(&signed_data, 0); /* additional attributes: none */

    /* signatures sequence: one record, algo 0x0103 + signature bytes */
    signature = (unsigned char *)malloc((size_t)siglen);
    /* Signed over signed_data's bytes (with the real digest already
     * written in -- see the digest fixup below, done before this). */

    /* ---- fixed sizes are now known; compute the final layout, patch
     * cd_offset in the EOCD copy, then compute the REAL content digest
     * against that final layout before actually signing. ---- */
    {
        bb signer_probe, signatures_probe;
        size_t signer_len_probe, block_value_len_probe, pair_len_probe, block_len_probe;

        bb_init(&signatures_probe);
        { bb rec; bb_init(&rec); bb_u32(&rec, 0x0103); bb_u32(&rec, (uint32_t)siglen);
          { unsigned char *zeros = (unsigned char *)calloc((size_t)siglen, 1); bb_append(&rec, zeros, (size_t)siglen); free(zeros); }
          bb_u32(&signatures_probe, (uint32_t)rec.len); bb_append(&signatures_probe, rec.data, rec.len); bb_free(&rec); }

        bb_init(&signer_probe);
        bb_u32(&signer_probe, (uint32_t)signed_data.len); bb_append(&signer_probe, signed_data.data, signed_data.len);
        bb_u32(&signer_probe, (uint32_t)signatures_probe.len); bb_append(&signer_probe, signatures_probe.data, signatures_probe.len);
        bb_u32(&signer_probe, (uint32_t)spki_len); bb_append(&signer_probe, spki_der, spki_len);
        signer_len_probe = signer_probe.len;

        block_value_len_probe = 4 + (4 + signer_len_probe); /* signers-seq len + (signer len-prefix + signer) */
        pair_len_probe = 4 + block_value_len_probe;         /* id(4) + value */
        /* size1(8) + [pair's own uint64 length prefix(8) + pair.len ID+value] + size2(8) + magic(16) */
        block_len_probe = 8 + (8 + pair_len_probe) + 8 + 16;

        bb_free(&signer_probe);
        bb_free(&signatures_probe);

        /* The REAL, final on-disk EOCD needs the true offset (contents +
         * the inserted signing block) so tools/the platform can actually
         * find the central directory. */
        new_cd_offset = (uint32_t)(contents_len + block_len_probe);
        { unsigned char le[4] = { (unsigned char)new_cd_offset, (unsigned char)(new_cd_offset>>8),
                                   (unsigned char)(new_cd_offset>>16), (unsigned char)(new_cd_offset>>24) };
          memcpy(new_eocd + 16, le, 4); }
    }

    /* The DIGEST, however, is computed with the signing block "virtually
     * removed" -- its EOCD copy must have cd_offset == contents_len, as
     * if the block never existed. See android_apk_digest.h's doc comment;
     * found empirically, not obvious from the public spec summary. */
    { unsigned char eocd_for_digest[64];
      uint32_t virtual_cd_offset = (uint32_t)contents_len;
      unsigned char le[4] = { (unsigned char)virtual_cd_offset, (unsigned char)(virtual_cd_offset>>8),
                               (unsigned char)(virtual_cd_offset>>16), (unsigned char)(virtual_cd_offset>>24) };
      memcpy(eocd_for_digest, new_eocd, new_eocd_len);
      memcpy(eocd_for_digest + 16, le, 4);

      android_apk_v2_content_digest(unsigned_apk, contents_len,
                                     unsigned_apk + cd_offset, cd_size,
                                     eocd_for_digest, new_eocd_len,
                                     digest);
    }

    /* patch the real digest into signed_data's digests_seq copy. Offset
     * within signed_data.data: digests_seq_len field(4) + rec_len field(4)
     * + algo field(4) + diglen field(4) = 16 bytes in total to reach the
     * digest value itself (NOT 4+16=20 -- an earlier off-by-4 here wrote
     * the digest 4 bytes too far right, spilling into and corrupting the
     * certs_seq_len field immediately following the digests sequence;
     * caught via apksigner's "Malformed signer block" + manual parsing). */
    memcpy(signed_data.data + 16, digest, 32);

    android_rsa_sign_sha256(key, signed_data.data, signed_data.len, signature);

    bb_init(&signatures_seq);
    { bb rec; bb_init(&rec); bb_u32(&rec, 0x0103); bb_u32(&rec, (uint32_t)siglen);
      bb_append(&rec, signature, (size_t)siglen);
      bb_u32(&signatures_seq, (uint32_t)rec.len); bb_append(&signatures_seq, rec.data, rec.len); bb_free(&rec); }

    bb_init(&signer);
    bb_u32(&signer, (uint32_t)signed_data.len); bb_append(&signer, signed_data.data, signed_data.len);
    bb_u32(&signer, (uint32_t)signatures_seq.len); bb_append(&signer, signatures_seq.data, signatures_seq.len);
    bb_u32(&signer, (uint32_t)spki_len); bb_append(&signer, spki_der, spki_len);

    bb_init(&pair);
    bb_u32(&pair, 0x7109871a);
    { bb signers_seq; bb_init(&signers_seq);
      bb_u32(&signers_seq, (uint32_t)signer.len); bb_append(&signers_seq, signer.data, signer.len);
      bb_u32(&pair, (uint32_t)signers_seq.len); bb_append(&pair, signers_seq.data, signers_seq.len);
      bb_free(&signers_seq);
    }

    bb_init(&block);
    /* size1/size2 = everything AFTER the first size field: the pair's own
     * uint64 length prefix (8) + pair.len (ID+value) + size2 (8) + magic (16). */
    bb_u64(&block, (uint64_t)(8 + pair.len + 8 + 16)); /* size1 */
    bb_u64(&block, (uint64_t)pair.len);                /* pair length prefix (uint64) */
    bb_append(&block, pair.data, pair.len);
    bb_u64(&block, (uint64_t)(8 + pair.len + 8 + 16)); /* size2, repeated */
    bb_append(&block, "APK Sig Block 42", 16);

    { bb out;
      bb_init(&out);
      bb_append(&out, unsigned_apk, contents_len);
      bb_append(&out, block.data, block.len);
      bb_append(&out, unsigned_apk + cd_offset, cd_size);
      bb_append(&out, new_eocd, new_eocd_len);
      *out_data = out.data;
      *out_len = out.len;
    }

    bb_free(&digests_seq); bb_free(&certs_seq); bb_free(&signed_data);
    bb_free(&signer); bb_free(&signatures_seq); bb_free(&pair); bb_free(&block);
    free(signature); free(spki_der);
    return 0;
}
