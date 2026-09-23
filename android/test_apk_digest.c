#include <stdio.h>
#include <stdlib.h>
#include "android_apk_digest.h"

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
    char p1[512], p2[512], p3[512];
    long l1, l2, l3;
    unsigned char *contents, *cd, *eocd;
    unsigned char digest[32];
    int i;

    snprintf(p1, sizeof p1, "%s/dig_contents.bin", base);
    snprintf(p2, sizeof p2, "%s/dig_central_dir.bin", base);
    snprintf(p3, sizeof p3, "%s/dig_eocd.bin", base);

    contents = read_file(p1, &l1);
    cd = read_file(p2, &l2);
    eocd = read_file(p3, &l3);

    android_apk_v2_content_digest(contents, (size_t)l1, cd, (size_t)l2, eocd, (size_t)l3, digest);

    printf("our digest = ");
    for (i = 0; i < 32; i++) printf("%02x", digest[i]);
    printf("\n");
    return 0;
}
