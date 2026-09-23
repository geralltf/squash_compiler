/* Builds a full (unsigned) test APK using squash's own manifest encoder +
 * ZIP writer, packaging the real ARM64 .so from test_phase3. This is the
 * first end-to-end assembly of Phases 1-3's output; apksigner (SDK
 * build-tools) is used afterwards purely as an external verification
 * oracle to confirm real Android accepts the result, since squash's own
 * signing engine hasn't been built yet. */
#include <stdio.h>
#include <stdlib.h>
#include "android_manifest.h"
#include "android_zip.h"

static unsigned char *read_file(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    *out_len = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc(*out_len);
    fread(buf, 1, *out_len, f);
    fclose(f);
    return buf;
}

int main(void) {
    android_manifest_spec spec;
    unsigned char *manifest_data;
    size_t manifest_len;
    long so_len;
    unsigned char *so_data;
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

    if (android_manifest_build(&spec, &manifest_data, &manifest_len) != 0) {
        fprintf(stderr, "manifest build failed\n"); return 1;
    }

    so_data = read_file("/tmp/claude-1000/-home-squash-projects-squash-compler-squash-compiler/3a439974-83aa-407c-b935-d7a0d8fca460/scratchpad/libapp.so", &so_len);

    zw = android_zip_new();
    android_zip_add_file(zw, "AndroidManifest.xml", manifest_data, manifest_len);
    android_zip_add_file(zw, "lib/arm64-v8a/libapp.so", so_data, (size_t)so_len);
    android_zip_finish(zw, &apk_data, &apk_len);
    android_zip_free(zw);

    f = fopen("/tmp/claude-1000/-home-squash-projects-squash-compler-squash-compiler/3a439974-83aa-407c-b935-d7a0d8fca460/scratchpad/squash_test_unsigned.apk", "wb");
    fwrite(apk_data, 1, apk_len, f);
    fclose(f);
    printf("wrote %zu byte unsigned apk\n", apk_len);
    return 0;
}
