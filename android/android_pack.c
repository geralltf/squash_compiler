#include "android_pack.h"
#include "android_manifest.h"
#include "android_zip.h"
#include "android_rsa.h"
#include "android_rsa_keyfile.h"
#include "android_x509.h"
#include "android_apk_sign.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static unsigned char *read_file(const char *path, long *out_len) {
    FILE *f = fopen(path, "rb");
    unsigned char *buf;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *out_len = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (unsigned char *)malloc(*out_len ? (size_t)*out_len : 1);
    if (fread(buf, 1, (size_t)*out_len, f) != (size_t)*out_len) { free(buf); fclose(f); return NULL; }
    fclose(f);
    return buf;
}

static int file_exists(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

/* Loads the cached debug signing key + cert, generating and caching them
 * on first use. `base_path` gets ".key"/".cert.der" suffixes appended. */
static int load_or_create_debug_key(const char *base_path, android_rsa_key *key,
                                     unsigned char **cert_der, size_t *cert_len) {
    char key_path[1024], cert_path[1024];
    snprintf(key_path, sizeof key_path, "%s.key", base_path);
    snprintf(cert_path, sizeof cert_path, "%s.cert.der", base_path);

    if (file_exists(key_path) && file_exists(cert_path)) {
        long clen;
        if (android_rsa_load(key, key_path) == 0) {
            *cert_der = read_file(cert_path, &clen);
            if (*cert_der) { *cert_len = (size_t)clen; return 0; }
        }
    }

    fprintf(stderr, "squash: generating a new Android debug signing key (RSA-2048; this happens "
                     "once and is cached at %s)...\n", key_path);
    android_rsa_generate(key, 2048);
    android_x509_self_signed(key, "squash debug", 1, 10000, cert_der, cert_len);

    android_rsa_save(key, key_path);
    { FILE *f = fopen(cert_path, "wb");
      if (f) { fwrite(*cert_der, 1, *cert_len, f); fclose(f); } }
    return 0;
}

int android_pack_apk(const char *so_path, const char *out_apk_path,
                      const android_pack_spec *spec) {
    long so_len;
    unsigned char *so_data;
    unsigned char *manifest_data; size_t manifest_len;
    android_zip_writer *zw;
    unsigned char *unsigned_apk; size_t unsigned_apk_len;
    android_rsa_key key;
    unsigned char *cert_der; size_t cert_len;
    unsigned char *signed_apk; size_t signed_apk_len;
    char entry_name[256];
    char keystore_base[1024];
    android_manifest_spec mspec;
    FILE *outf;

    so_data = read_file(so_path, &so_len);
    if (!so_data) { fprintf(stderr, "squash: cannot read %s\n", so_path); return -1; }

    mspec.package_name = spec->package_name;
    mspec.app_label = spec->app_label;
    mspec.lib_name = spec->lib_name;
    mspec.min_sdk_version = spec->min_sdk_version;
    mspec.target_sdk_version = spec->target_sdk_version;
    mspec.version_code = spec->version_code;
    mspec.version_name = spec->version_name;
    if (android_manifest_build(&mspec, &manifest_data, &manifest_len) != 0) {
        free(so_data);
        return -1;
    }

    snprintf(entry_name, sizeof entry_name, "lib/arm64-v8a/lib%s.so", spec->lib_name);

    zw = android_zip_new();
    android_zip_add_file(zw, "AndroidManifest.xml", manifest_data, manifest_len);
    android_zip_add_file(zw, entry_name, so_data, (size_t)so_len);
    android_zip_finish(zw, &unsigned_apk, &unsigned_apk_len);
    android_zip_free(zw);
    free(manifest_data);
    free(so_data);

    if (spec->keystore_base_path && spec->keystore_base_path[0]) {
        snprintf(keystore_base, sizeof keystore_base, "%s", spec->keystore_base_path);
    } else {
        const char *home = getenv("HOME");
        snprintf(keystore_base, sizeof keystore_base, "%s/.squash/android_debug", home ? home : ".");
        { char dir[1024]; snprintf(dir, sizeof dir, "%s/.squash", home ? home : "."); mkdir(dir, 0700); }
    }

    if (load_or_create_debug_key(keystore_base, &key, &cert_der, &cert_len) != 0) {
        free(unsigned_apk);
        return -1;
    }

    if (android_apk_sign_v2(unsigned_apk, unsigned_apk_len, &key, cert_der, cert_len,
                             &signed_apk, &signed_apk_len) != 0) {
        free(unsigned_apk); free(cert_der);
        return -1;
    }
    free(unsigned_apk);
    free(cert_der);

    outf = fopen(out_apk_path, "wb");
    if (!outf) { free(signed_apk); return -1; }
    fwrite(signed_apk, 1, signed_apk_len, outf);
    fclose(outf);
    free(signed_apk);

    printf("squash: wrote Android APK %s (%zu bytes, self-signed debug key)\n",
           out_apk_path, signed_apk_len);
    return 0;
}
