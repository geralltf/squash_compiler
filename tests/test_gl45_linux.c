/*
 * test_gl45_linux.c — OpenGL 4.5 core context on Linux via GLX, windowed
 *
 * Demonstrates:
 *   - GLX FBConfig selection with glXChooseFBConfig (window-capable, double-buffered)
 *   - A real, visible X11 window (XCreateWindow / XMapWindow)
 *   - Load glXCreateContextAttribsARB and create a 4.5 core context on that window
 *   - Load all OpenGL 4.5 function pointers with gl_load_funcs()
 *   - Correctness checks: draw a triangle to an offscreen FBO with a
 *     vertex/fragment shader using DSA VAO/VBO, read pixels back to verify colour
 *   - A short windowed render loop: draws a rotating triangle straight to
 *     the window's default framebuffer and swaps buffers each frame, so the
 *     triangle is actually visible on screen when a display is attached
 *
 * Build (squash):
 *   squash -linux -64 test_gl45_linux.c -lGL -lX11 -o gl45_linux
 *
 * Build (GCC):
 *   gcc test_gl45_linux.c -lGL -lX11 -o gl45_linux
 */

#define GL_FUNCS_IMPLEMENTATION
#include "../include/GL/gl_funcs.h"
#include "../include/GL/glx.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"

/* X11 prototypes squash headers omit */
Display *XOpenDisplay(const char *display_name);
void     XCloseDisplay(Display *dpy);
Window   XDefaultRootWindow(Display *dpy);
int      XDefaultScreen(Display *dpy);
Window   XCreateWindow(Display *dpy, Window parent, int x, int y,
                       unsigned int width, unsigned int height,
                       unsigned int border_width, int depth, unsigned int cls,
                       void *visual, unsigned long valuemask, void *attributes);
void     XDestroyWindow(Display *dpy, Window w);
void     XSync(Display *dpy, int discard);
Colormap XCreateColormap(Display *dpy, Window w, void *visual, int alloc);
int      XFreeColormap(Display *dpy, Colormap cmap);
int      XMapWindow(Display *dpy, Window w);
int      XStoreName(Display *dpy, Window w, const char *name);
int      XPending(Display *dpy);
int      XFlush(Display *dpy);
void     XFree(void *data);

/* Real Xlib XEvent is a big union (192 bytes on x86-64). We never inspect
 * its contents — just drain the queue so a real window manager doesn't
 * consider this window unresponsive — so an opaquely-sized buffer is all
 * that's needed rather than reproducing the whole union. */
typedef struct { long pad[24]; } XEvent;
int XNextEvent(Display *dpy, XEvent *event_return);

/* Xlib's window-attribute struct — field order/types must match the real
 * ABI exactly since this is passed straight into libX11.so's XCreateWindow. */
typedef struct {
    unsigned long background_pixmap;
    unsigned long background_pixel;
    unsigned long border_pixmap;
    unsigned long border_pixel;
    int  bit_gravity;
    int  win_gravity;
    int  backing_store;
    unsigned long backing_planes;
    unsigned long backing_pixel;
    int  save_under;
    long event_mask;
    long do_not_propagate_mask;
    int  override_redirect;
    Colormap colormap;
    unsigned long cursor;
} XSetWindowAttributes;

/* unistd.h prototype (real libc) — used to pace the render loop */
int usleep(unsigned int usec);

/* X11 constants */
#define InputOutput      1
#define CWColormap       0x2000
#define CWEventMask      0x0800
#define ExposureMask     0x00008000L
#define StructureNotifyMask 0x00020000L
#define AllocNone        0

/* ---- test infrastructure ---- */
static int g_pass = 0, g_fail = 0;
#define CHECK(label, cond) do { \
    if (cond) { printf("[PASS] %s\n", label); g_pass++; } \
    else       { printf("[FAIL] %s\n", label); g_fail++; } \
} while(0)

/* ---- shader sources ----
 * uAngle rotates the triangle for the live windowed render loop; it's left
 * at its default value of 0.0 for the offscreen FBO correctness test below,
 * which reproduces the exact same static triangle as before. */
static const char *vert_src =
    "#version 450 core\n"
    "layout(location = 0) in vec2 aPos;\n"
    "layout(location = 1) in vec3 aColor;\n"
    "out vec3 vColor;\n"
    "uniform float uAngle;\n"
    "void main() {\n"
    "    float s = sin(uAngle), c = cos(uAngle);\n"
    "    vec2 p = vec2(aPos.x * c - aPos.y * s, aPos.x * s + aPos.y * c);\n"
    "    gl_Position = vec4(p, 0.0, 1.0);\n"
    "    vColor = aColor;\n"
    "}\n";

static const char *frag_src =
    "#version 450 core\n"
    "in vec3 vColor;\n"
    "out vec4 fragColor;\n"
    "void main() {\n"
    "    fragColor = vec4(vColor, 1.0);\n"
    "}\n";

#define WIN_W 640
#define WIN_H 480

/* ---- state ---- */
static Display    *s_dpy    = 0;
static GLXContext  s_ctx    = 0;
static Window      s_win    = 0;
static GLXWindow   s_glx_win = 0;
static Colormap    s_cmap   = 0;

/* ---- compile shader helper ---- */
static GLuint compile_shader(GLenum type, const char *src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, 0);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, 512, 0, log);
        printf("  shader error: %s\n", log);
    }
    return sh;
}

static GLuint link_program(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, 512, 0, log);
        printf("  link error: %s\n", log);
    }
    return prog;
}

/* =========================================================================
 * Bootstrap: choose a window-capable FBConfig, create a real X11 window,
 * create a 4.5 core context bound to that window, make it current.
 * ========================================================================= */
static int bootstrap_glx(GLXExtFuncs *gf) {
    s_dpy = XOpenDisplay(0);
    if (!s_dpy) { printf("XOpenDisplay failed\n"); return 0; }

    int scr = XDefaultScreen(s_dpy);

    /* Verify GLX 1.3+ */
    int glx_maj = 0, glx_min = 0;
    glXQueryVersion(s_dpy, &glx_maj, &glx_min);
    printf("  GLX version: %d.%d\n", glx_maj, glx_min);
    if (glx_maj < 1 || (glx_maj == 1 && glx_min < 3)) {
        printf("Need GLX 1.3+\n"); return 0;
    }

    /* Choose a window-capable, double-buffered FBConfig */
    int fb_attribs[] = {
        GLX_DOUBLEBUFFER,  1,
        GLX_RED_SIZE,      8,
        GLX_GREEN_SIZE,    8,
        GLX_BLUE_SIZE,     8,
        GLX_ALPHA_SIZE,    8,
        GLX_DEPTH_SIZE,    24,
        GLX_RENDER_TYPE,   GLX_RGBA_BIT,
        GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
        0
    };
    int n_cfg = 0;
    GLXFBConfig *cfgs = glXChooseFBConfig(s_dpy, scr, fb_attribs, &n_cfg);
    if (!cfgs || n_cfg == 0) { printf("glXChooseFBConfig failed\n"); return 0; }
    GLXFBConfig cfg = cfgs[0];

    XVisualInfo *vi = glXGetVisualFromFBConfig(s_dpy, cfg);
    if (!vi) { printf("glXGetVisualFromFBConfig failed\n"); return 0; }

    /* Create a real, visible top-level window using the chosen visual */
    Window root = XDefaultRootWindow(s_dpy);
    s_cmap = XCreateColormap(s_dpy, root, vi->visual, AllocNone);

    XSetWindowAttributes swa;
    int zi; for (zi = 0; zi < (int)sizeof(swa); zi++) ((char*)&swa)[zi] = 0;
    swa.colormap   = s_cmap;
    swa.event_mask = ExposureMask | StructureNotifyMask;

    s_win = XCreateWindow(s_dpy, root, 0, 0, WIN_W, WIN_H, 0, vi->depth,
                          InputOutput, vi->visual, CWColormap | CWEventMask, &swa);
    if (!s_win) { printf("XCreateWindow failed\n"); return 0; }
    XStoreName(s_dpy, s_win, "squash OpenGL 4.5 (GLX)");
    XMapWindow(s_dpy, s_win);
    XFlush(s_dpy);
    XFree(vi);

    s_glx_win = glXCreateWindow(s_dpy, cfg, s_win, 0);
    if (!s_glx_win) { printf("glXCreateWindow failed\n"); return 0; }

    /* Load glXCreateContextAttribsARB */
    glx_load_ext_funcs(gf);
    if (!gf->CreateContextAttribsARB) {
        printf("glXCreateContextAttribsARB not available\n"); return 0;
    }

    /* Create 4.5 core context */
    int ctx_attribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 4,
        GLX_CONTEXT_MINOR_VERSION_ARB, 5,
        GLX_CONTEXT_PROFILE_MASK_ARB,  GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
        GLX_CONTEXT_FLAGS_ARB,         GLX_CONTEXT_DEBUG_BIT_ARB,
        0
    };
    s_ctx = gf->CreateContextAttribsARB(s_dpy, cfg, 0, 1, ctx_attribs);
    XSync(s_dpy, 0);
    if (!s_ctx) { printf("glXCreateContextAttribsARB (4.5) failed\n"); return 0; }

    if (!glXMakeContextCurrent(s_dpy, s_glx_win, s_glx_win, s_ctx)) {
        printf("glXMakeContextCurrent failed\n"); return 0;
    }
    return 1;
}

/* =========================================================================
 * Tests
 * ========================================================================= */
#define FBO_W 128
#define FBO_H 128

static void test_gl_version(void) {
    printf("\n--- OpenGL version ---\n");
    const GLubyte *ver = glGetString(GL_VERSION);
    const GLubyte *ren = glGetString(GL_RENDERER);
    printf("  Version  : %s\n", (const char*)ver);
    printf("  Renderer : %s\n", (const char*)ren);
    GLint maj = 0, min = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &maj);
    glGetIntegerv(GL_MINOR_VERSION, &min);
    CHECK("OpenGL major >= 4", maj >= 4);
    CHECK("OpenGL minor >= 5 (if major == 4)", maj > 4 || min >= 5);
}

static void test_draw_triangle_gl45(void) {
    printf("\n--- OpenGL 4.5 triangle draw (DSA, offscreen FBO) ---\n");

    float verts[] = {
    /*   x      y     r    g    b  */
        -0.9f, -0.9f, 0.0f, 1.0f, 0.0f,   /* green triangle */
         0.9f, -0.9f, 0.0f, 1.0f, 0.0f,
         0.0f,  0.9f, 0.0f, 1.0f, 0.0f,
    };

    /* DSA VAO + VBO */
    GLuint vao, vbo;
    glCreateVertexArrays(1, &vao);
    glCreateBuffers(1, &vbo);
    glNamedBufferData(vbo, sizeof(verts), verts, GL_STATIC_DRAW);

    glVertexArrayVertexBuffer(vao, 0, vbo, 0, 5 * sizeof(float));

    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(vao, 0, 0);

    glEnableVertexArrayAttrib(vao, 1);
    glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, 2 * sizeof(float));
    glVertexArrayAttribBinding(vao, 1, 0);

    /* Shaders */
    GLuint vs   = compile_shader(GL_VERTEX_SHADER,   vert_src);
    GLuint fs   = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    GLuint prog = link_program(vs, fs);
    GLint vs_ok = 0, fs_ok = 0, prog_ok = 0;
    glGetShaderiv(vs,   GL_COMPILE_STATUS, &vs_ok);
    glGetShaderiv(fs,   GL_COMPILE_STATUS, &fs_ok);
    glGetProgramiv(prog, GL_LINK_STATUS,   &prog_ok);
    CHECK("vertex shader compiled",   vs_ok);
    CHECK("fragment shader compiled", fs_ok);
    CHECK("program linked",           prog_ok);

    /* Offscreen FBO via DSA */
    GLuint fbo, rbo;
    glCreateFramebuffers(1, &fbo);
    glCreateRenderbuffers(1, &rbo);
    glNamedRenderbufferStorage(rbo, GL_RGBA8, FBO_W, FBO_H);
    glNamedFramebufferRenderbuffer(fbo, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
    GLenum status = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER);
    CHECK("FBO complete", status == GL_FRAMEBUFFER_COMPLETE);

    /* Render (uAngle defaults to 0 — same static triangle as always) */
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, FBO_W, FBO_H);
    float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    glClearNamedFramebufferfv(fbo, GL_COLOR, 0, clear);
    glUseProgram(prog);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();

    /* Sample centre — should be green */
    unsigned char pix[4] = {0};
    glReadPixels(FBO_W/2, FBO_H/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pix);
    CHECK("centre pixel G>200 (green triangle)", pix[1] > 200);
    CHECK("centre pixel R<50",                   pix[0] < 50);
    CHECK("centre pixel B<50",                   pix[2] < 50);

    /* Bottom-left corner of FBO should be black (clear colour) */
    unsigned char corner[4] = {0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
    CHECK("corner pixel is black R<50", corner[0] < 50);
    CHECK("corner pixel is black G<50", corner[1] < 50);

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rbo);
    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
}

static void test_query_timer(void) {
    printf("\n--- Timer query ---\n");
    GLuint q = 0;
    glGenQueries(1, &q);
    CHECK("glGenQueries returned non-zero", q != 0);
    glBeginQuery(GL_TIME_ELAPSED, q);
    /* dummy work */
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glEndQuery(GL_TIME_ELAPSED);
    glFinish();
    GLuint64 ns = 0;
    glGetQueryObjectui64v(q, GL_QUERY_RESULT, &ns);
    printf("  GL_TIME_ELAPSED: %llu ns\n", ns);
    CHECK("timer query returns non-negative ns", (long long)ns >= 0);
    glDeleteQueries(1, &q);
}

static void test_dsa_texture(void) {
    printf("\n--- DSA texture (glCreateTextures / glTextureStorage2D) ---\n");
    GLuint tex = 0;
    glCreateTextures(GL_TEXTURE_2D, 1, &tex);
    CHECK("glCreateTextures non-zero", tex != 0);
    glTextureStorage2D(tex, 1, GL_RGBA8, 4, 4);
    unsigned char data[4 * 4 * 4];
    int i;
    for (i = 0; i < 64; i++) data[i] = (unsigned char)(i * 4);
    glTextureSubImage2D(tex, 0, 0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, data);
    GLenum err = glGetError();
    CHECK("glTextureSubImage2D no error", err == GL_NO_ERROR);
    glDeleteTextures(1, &tex);
    CHECK("glDeleteTextures no crash", 1);
}

/* =========================================================================
 * Windowed render loop: draws a rotating triangle straight to the window's
 * default framebuffer and presents it with glXSwapBuffers each frame, for a
 * fixed ~2-second run. Bounded rather than an interactive "run until the
 * user closes the window" loop, so this still terminates on its own under
 * CI / headless test runs — but if a real display is attached, the window
 * is visible and the triangle actually spins on screen the whole time.
 * ========================================================================= */
static void run_windowed_demo(void) {
    printf("\n--- Windowed render loop ---\n");
    printf("  Spinning a triangle in a %dx%d window for ~2 seconds...\n", WIN_W, WIN_H);

    float verts[] = {
    /*   x      y     r    g    b  */
        -0.8f, -0.6f, 1.0f, 0.35f, 0.1f,   /* orange triangle */
         0.8f, -0.6f, 0.1f, 0.6f,  1.0f,
         0.0f,  0.9f, 1.0f, 1.0f,  0.2f,
    };

    GLuint vao, vbo;
    glCreateVertexArrays(1, &vao);
    glCreateBuffers(1, &vbo);
    glNamedBufferData(vbo, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, 5 * sizeof(float));
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(vao, 0, 0);
    glEnableVertexArrayAttrib(vao, 1);
    glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, 2 * sizeof(float));
    glVertexArrayAttribBinding(vao, 1, 0);

    GLuint vs   = compile_shader(GL_VERTEX_SHADER,   vert_src);
    GLuint fs   = compile_shader(GL_FRAGMENT_SHADER, frag_src);
    GLuint prog = link_program(vs, fs);
    GLint angle_loc = glGetUniformLocation(prog, "uAngle");

    float angle = 0.0f;
    int frame;
    for (frame = 0; frame < 120; frame++) {
        /* Drain pending X events so a real window manager doesn't decide
         * we're unresponsive. This demo doesn't act on any of them
         * (no resize/close handling) — it just runs a fixed frame count. */
        while (XPending(s_dpy)) {
            XEvent ev;
            XNextEvent(s_dpy, &ev);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, WIN_W, WIN_H);
        glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(prog);
        glUniform1f(angle_loc, angle);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glXSwapBuffers(s_dpy, s_glx_win);

        angle += 0.03f;
        usleep(16000); /* ~60 FPS */
    }
    printf("  Windowed render loop finished (%d frames).\n", frame);

    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
}

int main(void) {
    printf("=== Linux OpenGL 4.5 Tests (GLX, windowed) ===\n");

    GLXExtFuncs gf;
    int i;
    for (i = 0; i < (int)sizeof(gf); i++) ((char*)&gf)[i] = 0;

    if (!bootstrap_glx(&gf)) {
        printf("SKIP: could not create OpenGL 4.5 context.\n");
        printf("(Requires a GPU driver with OpenGL 4.5 support)\n");
        return 0;
    }

    int missing = gl_load_funcs();
    printf("gl_load_funcs(): %d functions unavailable\n", missing);
    CHECK("all core GL 4.5 functions loaded (missing==0)", missing == 0);

    test_gl_version();
    test_draw_triangle_gl45();
    test_query_timer();
    test_dsa_texture();
    run_windowed_demo();

    /* Cleanup */
    glXMakeContextCurrent(s_dpy, 0, 0, 0);
    glXDestroyContext(s_dpy, s_ctx);
    glXDestroyWindow(s_dpy, s_glx_win);
    XDestroyWindow(s_dpy, s_win);
    XFreeColormap(s_dpy, s_cmap);
    XCloseDisplay(s_dpy);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
