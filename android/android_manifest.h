#ifndef ANDROID_MANIFEST_H
#define ANDROID_MANIFEST_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    const char *package_name;   /* e.g. "com.squash.app" */
    const char *app_label;      /* plain string label, no resources.arsc needed */
    const char *lib_name;       /* bare .so name without "lib"/".so", e.g. "app"
                                    -> System.loadLibrary("app") -> libapp.so */
    int32_t min_sdk_version;
    int32_t target_sdk_version;
    int32_t version_code;
    const char *version_name;
} android_manifest_spec;

/* Builds the binary AndroidManifest.xml for a minimal NativeActivity app
 * (no Java/Kotlin/DEX): a single activity backed by android.app.NativeActivity
 * that loads `lib_name` via the android.app.lib_name meta-data, with a
 * MAIN/LAUNCHER intent filter. Returns a malloc'd buffer via *out_data
 * (caller frees) and its length via *out_len. Returns 0 on success. */
int android_manifest_build(const android_manifest_spec *spec,
                            unsigned char **out_data, size_t *out_len);

#endif
