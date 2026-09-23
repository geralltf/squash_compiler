/* Minimal self-signed X.509v3 certificate builder (RSA / sha256WithRSA),
 * built on the DER helpers in android_der.c and signed via
 * android_rsa_sign_sha256. Only the fields Android's package manager
 * actually looks at are included (no extensions, no CA chain -- a debug/
 * release APK signing cert is always self-signed and its own trust
 * anchor: the platform only checks that the SAME cert signs every update
 * to a given package, not that it chains to anything). */
#include "android_x509.h"
#include "android_der.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

/* sha256WithRSAEncryption: 1.2.840.113549.1.1.11 */
static const unsigned char SHA256_WITH_RSA_OID[] = {
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x0b
};
/* commonName: 2.5.4.3 */
static const unsigned char COMMON_NAME_OID[] = { 0x55, 0x04, 0x03 };
/* rsaEncryption: 1.2.840.113549.1.1.1 (subjectPublicKeyInfo's algorithm) */
static const unsigned char RSA_ENCRYPTION_OID[] = {
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01
};

static void build_sig_alg_id(der_buf *out) {
    der_buf inner; der_buf_init(&inner);
    der_oid(&inner, SHA256_WITH_RSA_OID, sizeof SHA256_WITH_RSA_OID);
    der_null(&inner);
    der_sequence(out, &inner);
    der_buf_free(&inner);
}

static void build_name(der_buf *out, const char *common_name) {
    /* RDNSequence { RelativeDistinguishedName { AttributeTypeAndValue { CN, value } } } */
    der_buf atv_inner, atv_seq, rdn_inner, rdn_set, name_inner;
    der_buf_init(&atv_inner);
    der_oid(&atv_inner, COMMON_NAME_OID, sizeof COMMON_NAME_OID);
    der_string(&atv_inner, 0x0c, common_name); /* UTF8String */
    der_buf_init(&atv_seq);
    der_sequence(&atv_seq, &atv_inner);
    der_buf_free(&atv_inner);

    der_buf_init(&rdn_inner);
    der_buf_append(&rdn_inner, atv_seq.data, atv_seq.len);
    der_buf_free(&atv_seq);
    der_buf_init(&rdn_set);
    der_tlv(&rdn_set, 0x31, rdn_inner.data, rdn_inner.len); /* SET OF */
    der_buf_free(&rdn_inner);

    der_buf_init(&name_inner);
    der_buf_append(&name_inner, rdn_set.data, rdn_set.len);
    der_buf_free(&rdn_set);
    der_sequence(out, &name_inner);
    der_buf_free(&name_inner);
}

/* RFC 5280 section 4.1.2.5: dates in [1950,2049] MUST use UTCTime (0x17,
 * 2-digit year -- ambiguous outside that range, which is exactly why it's
 * restricted to it); dates in 2050+ MUST use GeneralizedTime (0x18,
 * 4-digit year). A 10000-day validity period (the common default -- it's
 * literally what Android Studio's own debug keystore uses) started today
 * lands in 2054, so both branches are load-bearing, not a hypothetical. */
static void format_time_tlv(der_buf *out, time_t t) {
    struct tm tmv;
    char buf[24];
    int full_year;
    gmtime_r(&t, &tmv);
    full_year = tmv.tm_year + 1900;
    if (full_year >= 1950 && full_year <= 2049) {
        snprintf(buf, sizeof buf, "%02d%02d%02d%02d%02d%02dZ",
                 full_year % 100, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        der_tlv(out, 0x17, buf, strlen(buf));
    } else {
        snprintf(buf, sizeof buf, "%04d%02d%02d%02d%02d%02dZ",
                 full_year, tmv.tm_mon + 1, tmv.tm_mday,
                 tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
        der_tlv(out, 0x18, buf, strlen(buf));
    }
}

static void build_validity(der_buf *out, int days_valid) {
    time_t now = time(NULL);
    der_buf inner;
    der_buf_init(&inner);
    format_time_tlv(&inner, now);
    format_time_tlv(&inner, now + (time_t)days_valid * 24 * 3600);
    der_sequence(out, &inner);
    der_buf_free(&inner);
}

static void build_spki(der_buf *out, const android_rsa_key *key) {
    der_buf pubkey_seq_inner, pubkey_seq, alg_inner, alg_seq, spki_inner;

    der_buf_init(&pubkey_seq_inner);
    der_integer_from_bignum(&pubkey_seq_inner, &key->n);
    der_integer_from_bignum(&pubkey_seq_inner, &key->e);
    der_buf_init(&pubkey_seq);
    der_sequence(&pubkey_seq, &pubkey_seq_inner);
    der_buf_free(&pubkey_seq_inner);

    der_buf_init(&alg_inner);
    der_oid(&alg_inner, RSA_ENCRYPTION_OID, sizeof RSA_ENCRYPTION_OID);
    der_null(&alg_inner);
    der_buf_init(&alg_seq);
    der_sequence(&alg_seq, &alg_inner);
    der_buf_free(&alg_inner);

    der_buf_init(&spki_inner);
    der_buf_append(&spki_inner, alg_seq.data, alg_seq.len);
    der_bitstring(&spki_inner, pubkey_seq.data, pubkey_seq.len);
    der_buf_free(&alg_seq);
    der_buf_free(&pubkey_seq);

    der_sequence(out, &spki_inner);
    der_buf_free(&spki_inner);
}

int android_x509_self_signed(const android_rsa_key *key, const char *common_name,
                              uint32_t serial, int days_valid,
                              unsigned char **out_der, size_t *out_len) {
    der_buf tbs_inner, tbs_seq, sig_alg, name, validity, spki;
    der_buf cert_inner, cert;
    unsigned char *sig;
    int k = key->bits / 8;

    der_buf_init(&sig_alg);
    build_sig_alg_id(&sig_alg);
    der_buf_init(&name);
    build_name(&name, common_name);
    der_buf_init(&validity);
    build_validity(&validity, days_valid);
    der_buf_init(&spki);
    build_spki(&spki, key);

    der_buf_init(&tbs_inner);
    { unsigned char version_body[3] = {0x02, 0x01, 0x02}; /* INTEGER 2 (v3) */
      der_tlv(&tbs_inner, 0xa0, version_body, sizeof version_body); /* [0] EXPLICIT */
    }
    der_integer_u32(&tbs_inner, serial);
    der_buf_append(&tbs_inner, sig_alg.data, sig_alg.len);
    der_buf_append(&tbs_inner, name.data, name.len);   /* issuer */
    der_buf_append(&tbs_inner, validity.data, validity.len);
    der_buf_append(&tbs_inner, name.data, name.len);   /* subject == issuer (self-signed) */
    der_buf_append(&tbs_inner, spki.data, spki.len);
    der_buf_init(&tbs_seq);
    der_sequence(&tbs_seq, &tbs_inner);
    der_buf_free(&tbs_inner);

    sig = (unsigned char *)malloc((size_t)k);
    android_rsa_sign_sha256(key, tbs_seq.data, tbs_seq.len, sig);

    der_buf_init(&cert_inner);
    der_buf_append(&cert_inner, tbs_seq.data, tbs_seq.len);
    der_buf_append(&cert_inner, sig_alg.data, sig_alg.len);
    der_bitstring(&cert_inner, sig, (size_t)k);

    der_buf_init(&cert);
    der_sequence(&cert, &cert_inner);

    der_buf_free(&cert_inner);
    der_buf_free(&tbs_seq);
    der_buf_free(&sig_alg);
    der_buf_free(&name);
    der_buf_free(&validity);
    der_buf_free(&spki);
    free(sig);

    *out_der = cert.data;
    *out_len = cert.len;
    return 0;
}
