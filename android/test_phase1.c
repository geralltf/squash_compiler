#include <stdio.h>
#include <string.h>
#include "android_sha256.h"
#include "android_crc32.h"
#include "android_zip.h"

static int hex_eq(const unsigned char *got, size_t n, const char *hex) {
    char buf[128];
    size_t i;
    for (i = 0; i < n; i++) sprintf(buf + i*2, "%02x", got[i]);
    buf[n*2] = 0;
    return strcmp(buf, hex) == 0;
}

int main(void) {
    int failed = 0;
    unsigned char h[32];

    android_sha256("", 0, h);
    if (!hex_eq(h, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")) {
        printf("FAIL sha256('')\n"); failed = 1;
    } else printf("OK sha256('')\n");

    android_sha256("abc", 3, h);
    if (!hex_eq(h, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")) {
        printf("FAIL sha256('abc')\n"); failed = 1;
    } else printf("OK sha256('abc')\n");

    {
        const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        android_sha256(msg, strlen(msg), h);
        if (!hex_eq(h, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1")) {
            printf("FAIL sha256(56-byte msg)\n"); failed = 1;
        } else printf("OK sha256(56-byte msg)\n");
    }

    {
        /* 1,000,000 'a' characters -> known vector, exercises multi-block update path */
        static char big[1000000];
        memset(big, 'a', sizeof(big));
        android_sha256(big, sizeof(big), h);
        if (!hex_eq(h, 32, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0")) {
            printf("FAIL sha256(1M 'a')\n"); failed = 1;
        } else printf("OK sha256(1M 'a')\n");
    }

    {
        uint32_t c = android_crc32(0, "123456789", 9);
        if (c != 0xCBF43926u) { printf("FAIL crc32('123456789') = %08x\n", c); failed = 1; }
        else printf("OK crc32('123456789')\n");
    }

    {
        android_zip_writer *zw = android_zip_new();
        unsigned char *out; size_t outlen;
        const char *a = "hello world\n";
        const char *b = "second entry contents";
        android_zip_add_file(zw, "AndroidManifest.xml", a, strlen(a));
        android_zip_add_file(zw, "lib/arm64-v8a/libapp.so", b, strlen(b));
        android_zip_finish(zw, &out, &outlen);
        {
            FILE *f = fopen("/tmp/claude-1000/-home-squash-projects-squash-compler-squash-compiler/3a439974-83aa-407c-b935-d7a0d8fca460/scratchpad/phase1_test.apk", "wb");
            if (!f) { printf("FAIL could not open output path\n"); failed = 1; }
            else { fwrite(out, 1, outlen, f); fclose(f); printf("OK wrote %zu byte test apk\n", outlen); }
        }
        android_zip_free(zw);
    }

    if (failed) { printf("SOME TESTS FAILED\n"); return 1; }
    printf("ALL PHASE1 TESTS PASSED\n");
    return 0;
}
