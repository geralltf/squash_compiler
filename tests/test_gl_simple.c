#include "include/stdio.h"
#include "include/GL/gl.h"
#include "include/GL/osmesa.h"

static unsigned char buf[16384];

int main(void) {
    OSMesaContext ctx = OSMesaCreateContext(GL_RGBA, 0);
    if (!ctx) return 1;
    
    OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, 64, 64);
    
    /* Set blue clear color with explicit double precision (to test basic functionality) */
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    
    printf("pixel=%d,%d,%d\n", (int)buf[0], (int)buf[1], (int)buf[2]);
    
    /* Try with hardcoded bit pattern to bypass float arg issues */
    /* 0x3F800000 = 1.0f */
    
    OSMesaDestroyContext(ctx);
    return 0;
}
