#ifndef ANDROID_APK_SIGN_H
#define ANDROID_APK_SIGN_H
#include "android_rsa.h"
#include <stddef.h>

/* Takes an unsigned APK's bytes (as produced by android_zip.c: entries
 * then central directory then EOCD, no signing block yet) and returns a
 * new buffer with an APK Signature Scheme v2 block inserted between the
 * entries and the central directory, with the EOCD's central-directory
 * offset patched accordingly -- i.e. the finished, installable, signed
 * APK. `key` signs it; `cert_der`/`cert_len` is the X.509 certificate
 * (DER) whose public key matches `key`, embedded in the signing block
 * exactly as apksigner does. Returns a malloc'd buffer via *out_data
 * (caller frees) and its length via *out_len. Returns 0 on success. */
int android_apk_sign_v2(const unsigned char *unsigned_apk, size_t unsigned_apk_len,
                         const android_rsa_key *key,
                         const unsigned char *cert_der, size_t cert_len,
                         unsigned char **out_data, size_t *out_len);

#endif
