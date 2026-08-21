#include "../img_decode_png.c"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "SQW/testpages/test.png";
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    fread(buf, 1, (size_t)sz, fp);
    fclose(fp);

    unsigned char *rgba = NULL;
    int w = 0, h = 0;
    int ok = sqw_png_decode(buf, sz, &rgba, &w, &h);
    printf("ok=%d w=%d h=%d\n", ok, w, h);
    if (ok) {
        printf("pixel(0,0) = %d,%d,%d,%d\n", rgba[0], rgba[1], rgba[2], rgba[3]);
        int lastidx = (w * (h - 1) + (w - 1)) * 4;
        printf("pixel(w-1,h-1) = %d,%d,%d,%d\n", rgba[lastidx], rgba[lastidx+1], rgba[lastidx+2], rgba[lastidx+3]);
    }
    return ok ? 0 : 1;
}
