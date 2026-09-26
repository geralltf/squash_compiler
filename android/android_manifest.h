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
    /* 1 = use the DEX-based com.squash.runtime.SquashActivity shim
     * (hasCode=true, no android.app.lib_name meta-data needed -- the shim
     * calls System.loadLibrary(lib_name) itself, baked into its own
     * classes.dex) instead of the legacy raw android.app.NativeActivity
     * (hasCode=false). See android_dex.h -- added because raw
     * NativeActivity's onNativeWindowCreated/onResume dispatch was found
     * broken on real Android 17 hardware; NativeActivity still works fine
     * on older devices/the emulator so this is opt-in, not a replacement. */
    int use_activity_shim;
} android_manifest_spec;

/* Builds the binary AndroidManifest.xml for a minimal NativeActivity app
 * (no Java/Kotlin/DEX): a single activity backed by android.app.NativeActivity
 * that loads `lib_name` via the android.app.lib_name meta-data, with a
 * MAIN/LAUNCHER intent filter. Returns a malloc'd buffer via *out_data
 * (caller frees) and its length via *out_len. Returns 0 on success. */
int android_manifest_build(const android_manifest_spec *spec,
                            unsigned char **out_data, size_t *out_len);

#endif
