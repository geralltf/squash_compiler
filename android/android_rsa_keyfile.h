#ifndef ANDROID_RSA_KEYFILE_H
#define ANDROID_RSA_KEYFILE_H
#include "android_rsa.h"

/* Squash-private key storage (not PKCS#8/PEM -- this key never needs to
 * round-trip through any other tool, only through squash's own signer
 * across builds, so a minimal custom format avoids implementing PKCS#8
 * DER for no practical benefit). Format: bits(4) then, for each of n/e/d
 * in that order, a byte-length(4) + big-endian bytes. Returns 0 on
 * success. */
int android_rsa_save(const android_rsa_key *key, const char *path);
int android_rsa_load(android_rsa_key *key, const char *path);

#endif
