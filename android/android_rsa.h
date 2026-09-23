#ifndef ANDROID_RSA_H
#define ANDROID_RSA_H
#include "android_bignum.h"
#include <stddef.h>

typedef struct {
    bignum n; /* modulus */
    bignum e; /* public exponent, fixed at 65537 */
    bignum d; /* private exponent */
    int bits; /* modulus bit width, e.g. 2048 */
} android_rsa_key;

/* Generates a new RSA keypair (public exponent fixed at 65537). `bits`
 * should be 2048 for a normal APK signing key. This calls the Miller-Rabin
 * prime search, which is the slow part (build-time cost, not a runtime
 * one) -- expect low single-digit seconds for 2048 bits on typical
 * hardware. */
void android_rsa_generate(android_rsa_key *key, int bits);

/* RSASSA-PKCS1-v1_5 signature over the SHA-256 digest of `data` (`len`
 * bytes). Writes exactly key->bits/8 bytes to `out_sig` (caller-allocated,
 * must be at least that large). */
void android_rsa_sign_sha256(const android_rsa_key *key, const void *data, size_t len,
                              unsigned char *out_sig);

/* Encodes `key`'s public parameters as a DER SubjectPublicKeyInfo
 * (rsaEncryption OID + BIT STRING wrapping the RSAPublicKey SEQUENCE).
 * Returns a malloc'd buffer via *out_data (caller frees) and its length
 * via *out_len. */
int android_rsa_export_spki_der(const android_rsa_key *key,
                                 unsigned char **out_data, size_t *out_len);

#endif
