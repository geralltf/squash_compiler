#ifndef ANDROID_APK_SIGN_V1_H
#define ANDROID_APK_SIGN_V1_H
#include "android_rsa.h"
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const char *name; /* zip entry path, e.g. "AndroidManifest.xml" */
    const unsigned char *data;
    size_t len;
} android_v1_entry;

/* Builds the three classic JAR-signing files (META-INF/MANIFEST.MF,
 * META-INF/CERT.SF, META-INF/CERT.RSA) covering `entries` (the APK's real
 * content entries -- NOT including these three, which get added
 * separately). This must run BEFORE the ZIP is finalized: the caller adds
 * these three outputs as ordinary entries into the same archive, which is
 * then signed with APK Signature Scheme v2 as usual (v2's content digest
 * naturally covers whatever is in the archive, including these). Required
 * for real installability on API < 24 devices, where the platform has no
 * concept of v2/v3 and only understands classic JAR (v1) signing.
 *
 * `cn`/`serial` must be the exact values used to build `cert_der` (see
 * android_x509_self_signed) -- SignerInfo's issuerAndSerialNumber must
 * byte-for-byte match the certificate's own issuer/serial for a verifier
 * to look up the right certificate. Returns 0 on success, writing three
 * malloc'd buffers (caller frees each) via the manifest_mf_*, cert_sf_*,
 * cert_rsa_* out-params. */
int android_apk_sign_v1(const android_v1_entry *entries, int n_entries,
                         const android_rsa_key *key,
                         const unsigned char *cert_der, size_t cert_len,
                         const char *cn, uint32_t serial,
                         unsigned char **manifest_mf, size_t *manifest_mf_len,
                         unsigned char **cert_sf, size_t *cert_sf_len,
                         unsigned char **cert_rsa, size_t *cert_rsa_len);

#endif
