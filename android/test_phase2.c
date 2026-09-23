#include <stdio.h>
#include "android_manifest.h"

int main(void) {
    android_manifest_spec spec;
    unsigned char *data;
    size_t len;
    FILE *f;

    spec.package_name = "com.squash.app";
    spec.app_label = "Squash App";
    spec.lib_name = "app";
    spec.min_sdk_version = 21;
    spec.target_sdk_version = 33;
    spec.version_code = 1;
    spec.version_name = "1.0";

    if (android_manifest_build(&spec, &data, &len) != 0) {
        fprintf(stderr, "manifest build failed\n");
        return 1;
    }

    f = fopen("/tmp/claude-1000/-home-squash-projects-squash-compler-squash-compiler/3a439974-83aa-407c-b935-d7a0d8fca460/scratchpad/AndroidManifest.xml.bin", "wb");
    if (!f) { fprintf(stderr, "could not open output\n"); return 1; }
    fwrite(data, 1, len, f);
    fclose(f);
    printf("wrote %zu bytes\n", len);
    return 0;
}
