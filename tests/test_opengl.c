#include "include/stdio.h"
#include "include/stdlib.h"
#include "include/GL/gl.h"
#include "include/GL/osmesa.h"

static int pass_count = 0;
static int fail_count = 0;

static void pass(const char *name) {
    printf("  [PASS] %s\n", name);
    pass_count++;
}
static void fail(const char *name, const char *reason) {
    printf("  [FAIL] %s: %s\n", name, reason);
    fail_count++;
}

#define WIDTH  64
#define HEIGHT 64

static unsigned char buf[16384]; /* WIDTH * HEIGHT * 4 = 64*64*4 */

static unsigned char *pixel(int x, int y) {
    return &buf[(y * WIDTH + x) * 4];
}

/* ---- Test 1: context creation ---- */
static OSMesaContext ctx = 0;

static int test_context_create(void) {
    ctx = OSMesaCreateContext(GL_RGBA, 0);
    if (!ctx) {
        fail("OSMesa context create", "returned NULL");
        return 0;
    }
    pass("OSMesa context create");
    return 1;
}

/* ---- Test 2: make current ---- */
static int test_make_current(void) {
    if (!ctx) return 0;
    int ok = OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, WIDTH, HEIGHT);
    if (!ok) { fail("OSMesaMakeCurrent", "returned false"); return 0; }
    pass("OSMesaMakeCurrent");
    return 1;
}

/* ---- Test 3: glClear and read back ---- */
static void test_clear_color(void) {
    glClearColor(0.0f, 0.0f, 1.0f, 1.0f); /* blue */
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();

    /* OSMesa with Y_UP=0 stores row 0 at bottom; with default (Y_UP=1) top-left is [0, HEIGHT-1].
       Check corner pixel for blue (R=0,G=0,B~255,A=255). */
    unsigned char *p = pixel(0, 0);
    int r = (int)p[0];
    int g = (int)p[1];
    int b = (int)p[2];
    if (r == 0 && g == 0 && b == 255) pass("glClear blue -> pixel (0,0) is blue");
    else {
        printf("    pixel(0,0) = (%d,%d,%d) expected (0,0,255)\n", r, g, b);
        fail("glClear blue", "wrong color");
    }
}

/* ---- Test 4: draw a filled red rectangle using immediate mode ---- */
static void test_draw_rect(void) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    /* Set up a simple 2D ortho projection */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)WIDTH, 0.0, (double)HEIGHT, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* Draw a red rectangle covering bottom-left quadrant */
    glColor3f(1.0f, 0.0f, 0.0f);
    glBegin(GL_QUADS);
        glVertex2f(0.0f,            0.0f);
        glVertex2f((float)(WIDTH/2), 0.0f);
        glVertex2f((float)(WIDTH/2), (float)(HEIGHT/2));
        glVertex2f(0.0f,             (float)(HEIGHT/2));
    glEnd();
    glFinish();

    /* Center of bottom-left quadrant should be red */
    int cx = WIDTH / 4;
    int cy = HEIGHT / 4;
    unsigned char *p = pixel(cx, cy);
    int r = (int)p[0];
    int g = (int)p[1];
    int b = (int)p[2];
    if (r > 200 && g < 50 && b < 50) pass("draw red rect, sample interior pixel");
    else {
        printf("    pixel(%d,%d) = (%d,%d,%d) expected red\n", cx, cy, r, g, b);
        fail("draw red rect", "interior not red");
    }

    /* Top-right quadrant center should still be black */
    int tx = WIDTH * 3 / 4;
    int ty = HEIGHT * 3 / 4;
    p = pixel(tx, ty);
    r = (int)p[0]; g = (int)p[1]; b = (int)p[2];
    if (r < 10 && g < 10 && b < 10) pass("top-right outside rect is black");
    else {
        printf("    pixel(%d,%d) = (%d,%d,%d) expected black\n", tx, ty, r, g, b);
        fail("top-right outside rect", "not black");
    }
}

/* ---- Test 5: draw a triangle and verify a known pixel ---- */
static void test_draw_triangle(void) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)WIDTH, 0.0, (double)HEIGHT, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    /* Green triangle: bottom-left, bottom-right, top-center */
    glColor3f(0.0f, 1.0f, 0.0f);
    glBegin(GL_TRIANGLES);
        glVertex2f(0.0f, 0.0f);
        glVertex2f((float)WIDTH, 0.0f);
        glVertex2f((float)(WIDTH/2), (float)HEIGHT);
    glEnd();
    glFinish();

    /* Center of framebuffer should be inside the triangle */
    int cx = WIDTH / 2;
    int cy = HEIGHT / 3;
    unsigned char *p = pixel(cx, cy);
    int r = (int)p[0];
    int g = (int)p[1];
    int b = (int)p[2];
    if (g > 200 && r < 50 && b < 50) pass("draw green triangle, interior pixel is green");
    else {
        printf("    pixel(%d,%d) = (%d,%d,%d) expected green\n", cx, cy, r, g, b);
        fail("draw green triangle", "interior not green");
    }
}

/* ---- Test 6: glEnable / glGetError ---- */
static void test_state_errors(void) {
    glEnable(GL_DEPTH_TEST);
    GLenum err = glGetError();
    if (err == GL_NO_ERROR) pass("glEnable GL_DEPTH_TEST no error");
    else {
        printf("    glGetError()=0x%x\n", (unsigned int)err);
        fail("glEnable GL_DEPTH_TEST", "unexpected error");
    }
    glDisable(GL_DEPTH_TEST);
    err = glGetError();
    if (err == GL_NO_ERROR) pass("glDisable GL_DEPTH_TEST no error");
    else fail("glDisable GL_DEPTH_TEST", "unexpected error");
}

/* ---- Test 7: texture creation and deletion ---- */
static void test_textures(void) {
    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (tex == 0) { fail("glGenTextures", "returned tex=0"); return; }
    pass("glGenTextures returned non-zero ID");

    glBindTexture(GL_TEXTURE_2D, tex);
    unsigned char pixels[4] = {255, 0, 0, 255}; /* 1x1 red */
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    GLenum err = glGetError();
    if (err == GL_NO_ERROR) pass("glTexImage2D 1x1 red no error");
    else {
        printf("    glGetError()=0x%x\n", (unsigned int)err);
        fail("glTexImage2D", "unexpected error");
    }
    glDeleteTextures(1, &tex);
    pass("glDeleteTextures no crash");
}

int main(void) {
    printf("=== OpenGL (OSMesa offscreen) test suite ===\n");

    if (!test_context_create()) {
        printf("Cannot create OSMesa context — skipping render tests\n");
        return 1;
    }
    if (!test_make_current()) {
        OSMesaDestroyContext(ctx);
        return 1;
    }

    test_clear_color();
    test_draw_rect();
    test_draw_triangle();
    test_state_errors();
    test_textures();

    OSMesaDestroyContext(ctx);

    printf("---\n");
    printf("pass=%d  fail=%d\n", pass_count, fail_count);
    return fail_count == 0 ? 0 : 1;
}
