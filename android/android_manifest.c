/* Builds the AndroidManifest.xml tree for a minimal NativeActivity app and
 * hands it to android_axml.c for binary serialization.
 *
 * Resource ids below are the well-known android:* attribute ids from AOSP's
 * frameworks/base/core/res/res/values/public.xml. These have been stable
 * since API level 1 (an assigned attr id is never reused/changed), and are
 * what the platform's native manifest parser dispatches on -- the string
 * name alone is not sufficient without the resource map pointing these
 * strings at the ids the parser actually checks for. */
#include "android_manifest.h"
#include "android_axml.h"
#include <stdio.h>

#define ANDROID_NS "http://schemas.android.com/apk/res/android"

#define RESID_theme            0x01010000u
#define RESID_label            0x01010001u
#define RESID_icon             0x01010002u
#define RESID_name             0x01010003u
#define RESID_hasCode          0x0101000cu
#define RESID_debuggable       0x0101000fu
#define RESID_exported         0x01010010u
#define RESID_value            0x01010024u
#define RESID_resource         0x01010025u
#define RESID_minSdkVersion    0x0101020cu
#define RESID_targetSdkVersion 0x01010270u
#define RESID_versionCode      0x0101021bu
#define RESID_versionName      0x0101021cu
#define RESID_allowBackup      0x01010280u

int android_manifest_build(const android_manifest_spec *spec,
                            unsigned char **out_data, size_t *out_len) {
    axml_node *manifest, *uses_sdk, *application, *activity, *meta_data;
    axml_node *intent_filter, *action, *category;
    char libname_buf[256];
    int rc;

    manifest = axml_new_element(NULL, "manifest");
    axml_add_attr_string(manifest, NULL, "package", 0, spec->package_name);
    axml_add_attr_int(manifest, ANDROID_NS, "versionCode", RESID_versionCode,
                       spec->version_code, AXML_ATTR_INT_DEC);
    axml_add_attr_string(manifest, ANDROID_NS, "versionName", RESID_versionName,
                          spec->version_name);

    uses_sdk = axml_new_element(NULL, "uses-sdk");
    axml_add_attr_int(uses_sdk, ANDROID_NS, "minSdkVersion", RESID_minSdkVersion,
                       spec->min_sdk_version, AXML_ATTR_INT_DEC);
    axml_add_attr_int(uses_sdk, ANDROID_NS, "targetSdkVersion", RESID_targetSdkVersion,
                       spec->target_sdk_version, AXML_ATTR_INT_DEC);
    axml_add_child(manifest, uses_sdk);

    application = axml_new_element(NULL, "application");
    axml_add_attr_string(application, ANDROID_NS, "label", RESID_label, spec->app_label);
    axml_add_attr_int(application, ANDROID_NS, "hasCode", RESID_hasCode,
                       spec->use_activity_shim ? 1 : 0, AXML_ATTR_INT_BOOLEAN);
    axml_add_attr_int(application, ANDROID_NS, "allowBackup", RESID_allowBackup, 1, AXML_ATTR_INT_BOOLEAN);

    activity = axml_new_element(NULL, "activity");
    axml_add_attr_string(activity, ANDROID_NS, "name", RESID_name,
                          spec->use_activity_shim ? "com.squash.runtime.SquashActivity" : "android.app.NativeActivity");
    axml_add_attr_string(activity, ANDROID_NS, "label", RESID_label, spec->app_label);
    axml_add_attr_int(activity, ANDROID_NS, "exported", RESID_exported, 1, AXML_ATTR_INT_BOOLEAN);

    if (!spec->use_activity_shim) {
        /* The DEX shim calls System.loadLibrary(lib_name) itself (baked
         * into its own classes.dex at build time); only the legacy raw
         * NativeActivity path needs this meta-data convention. */
        meta_data = axml_new_element(NULL, "meta-data");
        axml_add_attr_string(meta_data, ANDROID_NS, "name", RESID_name, "android.app.lib_name");
        snprintf(libname_buf, sizeof(libname_buf), "%s", spec->lib_name);
        axml_add_attr_string(meta_data, ANDROID_NS, "value", RESID_value, libname_buf);
        axml_add_child(activity, meta_data);
    }

    intent_filter = axml_new_element(NULL, "intent-filter");
    action = axml_new_element(NULL, "action");
    axml_add_attr_string(action, ANDROID_NS, "name", RESID_name, "android.intent.action.MAIN");
    axml_add_child(intent_filter, action);
    category = axml_new_element(NULL, "category");
    axml_add_attr_string(category, ANDROID_NS, "name", RESID_name, "android.intent.category.LAUNCHER");
    axml_add_child(intent_filter, category);
    axml_add_child(activity, intent_filter);

    axml_add_child(application, activity);
    axml_add_child(manifest, application);

    rc = axml_serialize(manifest, "android", ANDROID_NS, out_data, out_len);
    axml_free(manifest);
    return rc;
}
