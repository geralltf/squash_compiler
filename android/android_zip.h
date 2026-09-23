#ifndef ANDROID_ZIP_H
#define ANDROID_ZIP_H
#include <stdint.h>
#include <stddef.h>

/* Minimal ZIP container writer sufficient to build a valid APK: STORE
 * (uncompressed) entries only, no encryption, no zip64 (fine for the small
 * native-app APKs this backend targets). APK signing schemes v1 (JAR
 * signing) and v2/v3 both expect entries to be readable via the standard
 * ZIP central directory, and v2/v3 additionally require the central
 * directory + EOCD to sit immediately after the (optionally present)
 * "APK Signing Block" — this writer produces plain, spec-correct ZIP bytes
 * and leaves signing-block insertion to a later layer. */

typedef struct android_zip_writer android_zip_writer;

android_zip_writer *android_zip_new(void);
void android_zip_free(android_zip_writer *zw);

/* Adds one stored (uncompressed) file entry. `name` is the path inside the
 * archive (e.g. "AndroidManifest.xml", "classes.dex", "lib/arm64-v8a/libapp.so"). */
int android_zip_add_file(android_zip_writer *zw, const char *name,
                          const void *data, size_t len);

/* Serializes the archive. Returns a malloc'd buffer via *out_data (caller
 * frees) and its length via *out_len. Returns 0 on success. */
int android_zip_finish(android_zip_writer *zw, unsigned char **out_data, size_t *out_len);

#endif
