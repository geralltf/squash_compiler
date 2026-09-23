#include <stdio.h>
#include "android_manifest.h"
#include "android_zip.h"

int main(void) {
    android_manifest_spec spec;
    unsigned char *manifest_data;
    size_t manifest_len;
    android_zip_writer *zw;
    unsigned char *apk_data;
    size_t apk_len;
    FILE *f;

    spec.package_name = "com.squash.app";
    spec.app_label = "Squash App";
    spec.lib_name = "app";
    spec.min_sdk_version = 21;
    spec.target_sdk_version = 33;
    spec.version_code = 1;
    spec.version_name = "1.0";

    if (android_manifest_build(&spec, &manifest_data, &manifest_len) != 0) return 1;

    zw = android_zip_new();
    android_zip_add_file(zw, "AndroidManifest.xml", manifest_data, manifest_len);
    android_zip_finish(zw, &apk_data, &apk_len);
    android_zip_free(zw);

    f = fopen("/tmp/claude-1000/-home-squash-projects-squash-compler-squash-compiler/3a439974-83aa-407c-b935-d7a0d8fca460/scratchpad/manifest_only.apk", "wb");
    fwrite(apk_data, 1, apk_len, f);
    fclose(f);
    printf("wrote %zu byte apk\n", apk_len);
    return 0;
}
