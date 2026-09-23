/* Classic JAR ("v1") APK signing: MANIFEST.MF + CERT.SF + a PKCS#7
 * SignedData CERT.RSA. Required for real installability on API < 24
 * devices, which have no concept of APK Signature Scheme v2/v3 at all.
 * Format: https://docs.oracle.com/javase/8/docs/technotes/guides/jar/jar.html
 * (manifest/signature file sections) + RFC 2315 (PKCS#7). */
#include "android_apk_sign_v1.h"
#include "android_der.h"
#include "android_base64.h"
#include "android_sha256.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

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
static void bb_str(bb *b, const char *s) { bb_append(b, s, strlen(s)); }

static void sha256_b64(const unsigned char *data, size_t len, char *out_b64) {
    unsigned char digest[32];
    android_sha256(data, len, digest);
    android_base64_encode(digest, 32, out_b64);
}

/* rsaEncryption: 1.2.840.113549.1.1.1 */
static const unsigned char RSA_ENCRYPTION_OID[] = { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01 };
/* id-sha256: 2.16.840.1.101.3.4.2.1 */
static const unsigned char SHA256_OID[] = { 0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01 };
/* signedData: 1.2.840.113549.1.7.2 */
static const unsigned char SIGNED_DATA_OID[] = { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x07, 0x02 };
/* data (plain, no content type): 1.2.840.113549.1.7.1 */
static const unsigned char PKCS7_DATA_OID[] = { 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x07, 0x01 };
/* commonName: 2.5.4.3 (must match android_x509.c's cert Name construction exactly) */
static const unsigned char COMMON_NAME_OID[] = { 0x55, 0x04, 0x03 };

static void build_name(der_buf *out, const char *common_name) {
    der_buf atv_inner, atv_seq, rdn_inner, rdn_set, name_inner;
    der_buf_init(&atv_inner);
    der_oid(&atv_inner, COMMON_NAME_OID, sizeof COMMON_NAME_OID);
    der_string(&atv_inner, 0x0c, common_name);
    der_buf_init(&atv_seq);
    der_sequence(&atv_seq, &atv_inner);
    der_buf_free(&atv_inner);

    der_buf_init(&rdn_inner);
    der_buf_append(&rdn_inner, atv_seq.data, atv_seq.len);
    der_buf_free(&atv_seq);
    der_buf_init(&rdn_set);
    der_tlv(&rdn_set, 0x31, rdn_inner.data, rdn_inner.len);
    der_buf_free(&rdn_inner);

    der_buf_init(&name_inner);
    der_buf_append(&name_inner, rdn_set.data, rdn_set.len);
    der_buf_free(&rdn_set);
    der_sequence(out, &name_inner);
    der_buf_free(&name_inner);
}

static void build_alg_id(der_buf *out, const unsigned char *oid, size_t oid_len) {
    der_buf inner;
    der_buf_init(&inner);
    der_oid(&inner, oid, oid_len);
    der_null(&inner);
    der_sequence(out, &inner);
    der_buf_free(&inner);
}

int android_apk_sign_v1(const android_v1_entry *entries, int n_entries,
                         const android_rsa_key *key,
                         const unsigned char *cert_der, size_t cert_len,
                         const char *cn, uint32_t serial,
                         unsigned char **manifest_mf, size_t *manifest_mf_len,
                         unsigned char **cert_sf, size_t *cert_sf_len,
                         unsigned char **cert_rsa, size_t *cert_rsa_len) {
    bb mf, sf;
    bb *section_bytes; /* one manifest section per entry, for CERT.SF's per-entry digest */
    int i;
    char b64[64];
    unsigned char sig[512];
    int siglen = key->bits / 8;

    bb_init(&mf);
    bb_str(&mf, "Manifest-Version: 1.0\r\nCreated-By: 1.0 (squash)\r\n\r\n");

    section_bytes = (bb *)calloc((size_t)n_entries, sizeof(bb));
    for (i = 0; i < n_entries; i++) {
        bb *sec = &section_bytes[i];
        bb_init(sec);
        bb_str(sec, "Name: "); bb_str(sec, entries[i].name); bb_str(sec, "\r\n");
        sha256_b64(entries[i].data, entries[i].len, b64);
        bb_str(sec, "SHA-256-Digest: "); bb_str(sec, b64); bb_str(sec, "\r\n\r\n");
        bb_append(&mf, sec->data, sec->len);
    }

    bb_init(&sf);
    bb_str(&sf, "Signature-Version: 1.0\r\n");
    sha256_b64(mf.data, mf.len, b64);
    bb_str(&sf, "SHA-256-Digest-Manifest: "); bb_str(&sf, b64); bb_str(&sf, "\r\n");
    bb_str(&sf, "Created-By: 1.0 (squash)\r\n\r\n");
    for (i = 0; i < n_entries; i++) {
        bb_str(&sf, "Name: "); bb_str(&sf, entries[i].name); bb_str(&sf, "\r\n");
        sha256_b64(section_bytes[i].data, section_bytes[i].len, b64);
        bb_str(&sf, "SHA-256-Digest: "); bb_str(&sf, b64); bb_str(&sf, "\r\n\r\n");
        bb_free(&section_bytes[i]);
    }
    free(section_bytes);

    android_rsa_sign_sha256(key, sf.data, sf.len, sig);

    /* ---- PKCS#7 SignedData (detached, one signer, no authenticated attrs) ---- */
    {
        der_buf digest_alg, rsa_alg, name_der;
        der_buf digest_algs_set, content_info_inner, content_info;
        der_buf issuer_and_serial_inner, issuer_and_serial;
        der_buf signer_info_inner, signer_info, signer_infos_set;
        der_buf certs_wrapped, signed_data_inner, signed_data;
        der_buf outer_inner, outer;

        der_buf_init(&digest_alg);
        build_alg_id(&digest_alg, SHA256_OID, sizeof SHA256_OID);
        der_buf_init(&rsa_alg);
        build_alg_id(&rsa_alg, RSA_ENCRYPTION_OID, sizeof RSA_ENCRYPTION_OID);
        der_buf_init(&name_der);
        build_name(&name_der, cn);

        der_buf_init(&digest_algs_set);
        der_tlv(&digest_algs_set, 0x31, digest_alg.data, digest_alg.len); /* SET OF, one alg id */

        der_buf_init(&content_info_inner);
        der_oid(&content_info_inner, PKCS7_DATA_OID, sizeof PKCS7_DATA_OID);
        der_buf_init(&content_info);
        der_sequence(&content_info, &content_info_inner);
        der_buf_free(&content_info_inner);

        der_buf_init(&certs_wrapped);
        der_tlv(&certs_wrapped, 0xa0, cert_der, cert_len); /* [0] IMPLICIT SET OF Certificate */

        der_buf_init(&issuer_and_serial_inner);
        der_buf_append(&issuer_and_serial_inner, name_der.data, name_der.len);
        der_integer_u32(&issuer_and_serial_inner, serial);
        der_buf_init(&issuer_and_serial);
        der_sequence(&issuer_and_serial, &issuer_and_serial_inner);
        der_buf_free(&issuer_and_serial_inner);

        der_buf_init(&signer_info_inner);
        der_integer_u32(&signer_info_inner, 1); /* version */
        der_buf_append(&signer_info_inner, issuer_and_serial.data, issuer_and_serial.len);
        der_buf_append(&signer_info_inner, digest_alg.data, digest_alg.len);
        der_buf_append(&signer_info_inner, rsa_alg.data, rsa_alg.len);
        der_tlv(&signer_info_inner, 0x04, sig, (size_t)siglen); /* encryptedDigest OCTET STRING */
        der_buf_init(&signer_info);
        der_sequence(&signer_info, &signer_info_inner);
        der_buf_free(&signer_info_inner);

        der_buf_init(&signer_infos_set);
        der_tlv(&signer_infos_set, 0x31, signer_info.data, signer_info.len); /* SET OF, one SignerInfo */

        der_buf_init(&signed_data_inner);
        der_integer_u32(&signed_data_inner, 1); /* version */
        der_buf_append(&signed_data_inner, digest_algs_set.data, digest_algs_set.len);
        der_buf_append(&signed_data_inner, content_info.data, content_info.len);
        der_buf_append(&signed_data_inner, certs_wrapped.data, certs_wrapped.len);
        der_buf_append(&signed_data_inner, signer_infos_set.data, signer_infos_set.len);
        der_buf_init(&signed_data);
        der_sequence(&signed_data, &signed_data_inner);
        der_buf_free(&signed_data_inner);

        der_buf_init(&outer_inner);
        der_oid(&outer_inner, SIGNED_DATA_OID, sizeof SIGNED_DATA_OID);
        der_tlv(&outer_inner, 0xa0, signed_data.data, signed_data.len); /* [0] EXPLICIT SignedData */
        der_buf_init(&outer);
        der_sequence(&outer, &outer_inner);
        der_buf_free(&outer_inner);

        *cert_rsa = outer.data;
        *cert_rsa_len = outer.len;

        der_buf_free(&digest_alg); der_buf_free(&rsa_alg); der_buf_free(&name_der);
        der_buf_free(&digest_algs_set); der_buf_free(&content_info);
        der_buf_free(&certs_wrapped); der_buf_free(&issuer_and_serial);
        der_buf_free(&signer_info); der_buf_free(&signer_infos_set);
        der_buf_free(&signed_data);
    }

    *manifest_mf = mf.data; *manifest_mf_len = mf.len;
    *cert_sf = sf.data; *cert_sf_len = sf.len;
    return 0;
}
