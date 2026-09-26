#ifndef ANDROID_PACK_H
#define ANDROID_PACK_H

typedef struct {
    const char *package_name;   /* e.g. "com.squash.myapp" */
    const char *app_label;      /* display name */
    const char *lib_name;       /* bare .so name, e.g. "myapp" -> libmyapp.so */
    int min_sdk_version;
    int target_sdk_version;
    int version_code;
    const char *version_name;
    /* Debug signing key cache path (a persistent RSA-2048 key + self-signed
     * cert are generated once and reused across builds -- regenerating a
     * 2048-bit key takes tens of seconds via Miller-Rabin, so paying that
     * cost on every single compile would make the -android target
     * impractical to iterate with). NULL uses "$HOME/.squash/android_debug".
     */
    const char *keystore_base_path;
    /* 1 = package the DEX-based com.squash.runtime.SquashActivity shim
     * (embeds a classes.dex, built on the fly via android_dex.h) instead
     * of the legacy raw android.app.NativeActivity path. See
     * android_manifest.h's identically-named field for why this exists. */
    int use_activity_shim;
} android_pack_spec;

/* Reads the ELF shared object at `so_path`, wraps it in a minimal
 * NativeActivity AndroidManifest.xml, packages both into a ZIP, signs it
 * with APK Signature Scheme v2 (generating/reusing a cached debug RSA key
 * + self-signed cert), and writes the finished, installable .apk to
 * `out_apk_path`. Returns 0 on success. */
int android_pack_apk(const char *so_path, const char *out_apk_path,
                      const android_pack_spec *spec);

#endif
