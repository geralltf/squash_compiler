/* RSA key generation and RSASSA-PKCS1-v1_5 SHA-256 signing, built on
 * android_bignum.c. Public exponent is fixed at 65537 (0x10001) -- the
 * universal standard choice, small enough for fast verification while
 * satisfying every practical primality/coprimality constraint. */
#include "android_rsa.h"
#include "android_der.h"
#include "android_sha256.h"
#include <string.h>
#include <stdlib.h>

void android_rsa_generate(android_rsa_key *key, int bits) {
    bignum p, q, one, phi_p, phi_q, phi, n, d, e;
    int half = bits / 2;

    bn_set_u32(&e, 65537);
    bn_set_u32(&one, 1);

    for (;;) {
        do { bn_random_odd_topbit(&p, half); } while (!bn_is_probable_prime(&p, 20));
        do { bn_random_odd_topbit(&q, half); } while (!bn_is_probable_prime(&q, 20) || bn_cmp(&q, &p) == 0);

        bn_sub(&phi_p, &p, &one);
        bn_sub(&phi_q, &q, &one);
        bn_mul(&phi, &phi_p, &phi_q);

        if (bn_modinv(&d, &e, &phi) == 0) break; /* gcd(e, phi) == 1 -- success */
        /* else e isn't invertible mod phi (exceedingly rare with random
         * primes and e=65537) -- draw a fresh p,q and retry. */
    }

    bn_mul(&n, &p, &q);

    bn_copy(&key->n, &n);
    bn_copy(&key->e, &e);
    bn_copy(&key->d, &d);
    key->bits = bits;
}

/* Well-known, fixed DER prefix for a SHA-256 DigestInfo (RFC 8017 / common
 * across every RSASSA-PKCS1-v1_5 implementation): identifies
 * id-sha256 (2.16.840.1.101.3.4.2.1) with a NULL parameter, followed by an
 * OCTET STRING tag+length for the 32-byte digest that gets appended after
 * this fixed prefix. */
static const unsigned char SHA256_DIGESTINFO_PREFIX[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65, 0x03,
    0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20
};

void android_rsa_sign_sha256(const android_rsa_key *key, const void *data, size_t len,
                              unsigned char *out_sig) {
    unsigned char digest[32];
    unsigned char t[sizeof SHA256_DIGESTINFO_PREFIX + 32];
    int k = key->bits / 8;
    int tlen = (int)sizeof t;
    int ps_len = k - 3 - tlen;
    unsigned char *em;
    bignum em_bn, sig_bn;
    int i;

    android_sha256(data, len, digest);
    memcpy(t, SHA256_DIGESTINFO_PREFIX, sizeof SHA256_DIGESTINFO_PREFIX);
    memcpy(t + sizeof SHA256_DIGESTINFO_PREFIX, digest, 32);

    em = (unsigned char *)malloc((size_t)k);
    em[0] = 0x00;
    em[1] = 0x01;
    for (i = 0; i < ps_len; i++) em[2 + i] = 0xFF;
    em[2 + ps_len] = 0x00;
    memcpy(em + 3 + ps_len, t, (size_t)tlen);

    bn_from_bytes_be(&em_bn, em, k);
    bn_modexp(&sig_bn, &em_bn, &key->d, &key->n);
    bn_to_bytes_be(&sig_bn, out_sig, k);

    free(em);
}

/* rsaEncryption OID: 1.2.840.113549.1.1.1 */
static const unsigned char RSA_ENCRYPTION_OID[] = {
    0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01
};

int android_rsa_export_spki_der(const android_rsa_key *key,
                                 unsigned char **out_data, size_t *out_len) {
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

    { der_buf spki;
      der_buf_init(&spki);
      der_sequence(&spki, &spki_inner);
      der_buf_free(&spki_inner);
      *out_data = spki.data;
      *out_len = spki.len;
    }
    return 0;
}
