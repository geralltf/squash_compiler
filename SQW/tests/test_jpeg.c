#include "../img_decode_jpeg.c"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "SQW/testpages/test.jpg";
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
    int ok = sqw_jpeg_decode(buf, sz, &rgba, &w, &h);
    printf("ok=%d w=%d h=%d\n", ok, w, h);
    if (ok) {
        int bx, by;
        for (by = 0; by < h/8; by++) {
            for (bx = 0; bx < w/8; bx++) {
                unsigned char *p = rgba + ((by*8+4)*w + (bx*8+4))*4;
                printf("(%3d,%3d,%3d) ", p[0], p[1], p[2]);
            }
            printf("\n");
        }
    }
    return ok ? 0 : 1;
}
