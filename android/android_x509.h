#ifndef ANDROID_X509_H
#define ANDROID_X509_H
#include "android_rsa.h"
#include <stddef.h>
#include <stdint.h>

/* Builds a minimal self-signed X.509v3 certificate (RSA/sha256WithRSA)
 * around `key`'s public parameters, signed by that same key (a debug/
 * release signing cert for an APK is self-signed -- Android doesn't
 * validate it against any CA chain, only that the same cert signs every
 * update to a given app). Returns a malloc'd DER buffer via *out_der
 * (caller frees) and its length via *out_len. */
int android_x509_self_signed(const android_rsa_key *key, const char *common_name,
                              uint32_t serial, int days_valid,
                              unsigned char **out_der, size_t *out_len);

#endif
