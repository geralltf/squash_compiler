#include "../img_decode_gif.c"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "SQW/testpages/test.gif";
    FILE *fp = fopen(path, "rb");
    if (!fp) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    unsigned char *buf = (unsigned char *)malloc((size_t)sz);
    fread(buf, 1, (size_t)sz, fp);
    fclose(fp);

    SqwGifFrame *frames = NULL;
    int frame_count = 0, w = 0, h = 0;
    int ok = sqw_gif_decode(buf, sz, &frames, &frame_count, &w, &h);
    printf("ok=%d w=%d h=%d frame_count=%d\n", ok, w, h, frame_count);
    if (ok) {
        int i;
        for (i = 0; i < frame_count; i++) {
            unsigned char *p = frames[i].rgba;
            printf("frame %d: delay_cs=%d center_pixel=%d,%d,%d,%d\n",
                i, frames[i].delay_cs,
                p[((h/2)*w + w/2)*4+0], p[((h/2)*w + w/2)*4+1],
                p[((h/2)*w + w/2)*4+2], p[((h/2)*w + w/2)*4+3]);
        }
        sqw_gif_frames_free(frames, frame_count);
    }
    return ok ? 0 : 1;
}
