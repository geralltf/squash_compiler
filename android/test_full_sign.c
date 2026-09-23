#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "android_manifest.h"
#include "android_zip.h"
#include "android_rsa.h"
#include "android_x509.h"
#include "android_apk_sign.h"

static unsigned char *read_file(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc(*len ? (size_t)*len : 1);
    if (fread(buf, 1, (size_t)*len, f) != (size_t)*len) { fprintf(stderr, "short read\n"); exit(1); }
    fclose(f);
    return buf;
}

int main(void) {
    const char *base = "/tmp/claude-1000/-home-squash-projects-squash-compler-squash-compiler/3a439974-83aa-407c-b935-d7a0d8fca460/scratchpad";
    android_manifest_spec spec;
    unsigned char *manifest_data; size_t manifest_len;
    long so_len;
    unsigned char *so_data;
    android_zip_writer *zw;
    unsigned char *unsigned_apk; size_t unsigned_apk_len;
    android_rsa_key key;
    unsigned char *cert_der; size_t cert_len;
    unsigned char *signed_apk; size_t signed_apk_len;
    char p1[512];
    FILE *f;
    clock_t t0;

    spec.package_name = "com.squash.app";
    spec.app_label = "Squash App";
    spec.lib_name = "app";
    spec.min_sdk_version = 21;
    spec.target_sdk_version = 33;
    spec.version_code = 1;
    spec.version_name = "1.0";
    android_manifest_build(&spec, &manifest_data, &manifest_len);

    snprintf(p1, sizeof p1, "%s/libapp.so", base);
    so_data = read_file(p1, &so_len);

    zw = android_zip_new();
    android_zip_add_file(zw, "AndroidManifest.xml", manifest_data, manifest_len);
    android_zip_add_file(zw, "lib/arm64-v8a/libapp.so", so_data, (size_t)so_len);
    android_zip_finish(zw, &unsigned_apk, &unsigned_apk_len);
    android_zip_free(zw);

    printf("generating signing key...\n");
    t0 = clock();
    android_rsa_generate(&key, 2048);
    printf("keygen took %.1fs\n", (double)(clock()-t0)/CLOCKS_PER_SEC);

    android_x509_self_signed(&key, "squash", 1, 10000, &cert_der, &cert_len);

    if (android_apk_sign_v2(unsigned_apk, unsigned_apk_len, &key, cert_der, cert_len,
                             &signed_apk, &signed_apk_len) != 0) {
        fprintf(stderr, "signing failed\n");
        return 1;
    }

    snprintf(p1, sizeof p1, "%s/squash_own_signed.apk", base);
    f = fopen(p1, "wb");
    fwrite(signed_apk, 1, signed_apk_len, f);
    fclose(f);
    printf("wrote %zu byte squash-signed APK to %s\n", signed_apk_len, p1);
    return 0;
}
