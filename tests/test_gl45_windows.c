/*
 * test_gl45_windows.c — OpenGL 4.5 core context on Windows via WGL, windowed
 *
 * Demonstrates:
 *   - A real, visible Win32 window (RegisterClassExA / CreateWindowExA)
 *   - WGL dummy context on that window -> load wglCreateContextAttribsARB
 *   - Destroy the dummy context, create a real 4.5 core context on the
 *     same window
 *   - Load all OpenGL 4.5 function pointers with gl_load_funcs()
 *   - Correctness checks: draw a triangle to an offscreen FBO with a
 *     vertex/fragment shader using DSA VAO/VBO, read back pixels to verify
 *     colour
 *   - A short windowed render loop: draws a rotating triangle straight to
 *     the window's default framebuffer and calls SwapBuffers each frame, so
 *     the triangle is actually visible on screen when a display is attached
 *
 * Build (squash):
 *   squash -windows -64 test_gl45_windows.c -o gl45_win.exe
 *
 * Build (MSVC / GCC):
 *   cl test_gl45_windows.c /link opengl32.lib gdi32.lib user32.lib
 *   gcc test_gl45_windows.c -lopengl32 -lgdi32 -luser32 -o gl45_win
 */

#define GL_FUNCS_IMPLEMENTATION
#include "../include/GL/gl_funcs.h"
#include "../include/GL/wgl.h"
#include "../include/stdio.h"
#include "../include/stdlib.h"
#include "../include/string.h"

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

/* ---- helper: compile one shader ---- */
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

/* ---- helper: link a program ---- */
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
 * Minimal Win32 windowing — squash headers deliberately omit the full
 * windows.h, so declare just enough of it here to create and pump a real
 * top-level window. Struct field order/types below match the real Win32
 * ABI exactly (checked against the Windows SDK headers), since these get
 * passed straight into user32.dll/gdi32.dll.
 * ========================================================================= */
typedef void*          HINSTANCE;
typedef void*          HICON;
typedef void*          HCURSOR;
typedef void*          HBRUSH;
typedef void*          HMENU;
typedef unsigned long long WPARAM;
typedef long long      LPARAM;
typedef long long      LRESULT;
typedef unsigned short ATOM;

typedef LRESULT (WINAPI *WNDPROC)(HWND, UINT, WPARAM, LPARAM);

typedef struct tagWNDCLASSEXA {
    UINT      cbSize;
    UINT      style;
    WNDPROC   lpfnWndProc;
    int       cbClsExtra;
    int       cbWndExtra;
    HINSTANCE hInstance;
    HICON     hIcon;
    HCURSOR   hCursor;
    HBRUSH    hbrBackground;
    LPCSTR    lpszMenuName;
    LPCSTR    lpszClassName;
    HICON     hIconSm;
} WNDCLASSEXA;

typedef struct tagPOINT2 { LONG x, y; } POINT2;
typedef struct tagMSG {
    HWND   hwnd;
    UINT   message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD  time;
    POINT2 pt;
} MSG;

ATOM      RegisterClassExA(const WNDCLASSEXA *wc);          /* user32 */
HWND      CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR windowName,
                          DWORD style, int x, int y, int w, int h,
                          HWND parent, HMENU menu, HINSTANCE hInst, void *param); /* user32 */
LRESULT   DefWindowProcA(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);    /* user32 */
BOOL      DestroyWindow(HWND hwnd);                                     /* user32 */
BOOL      ShowWindow(HWND hwnd, int cmdShow);                           /* user32 */
BOOL      UpdateWindow(HWND hwnd);                                      /* user32 */
BOOL      PeekMessageA(MSG *msg, HWND hwnd, UINT msgMin, UINT msgMax, UINT remove); /* user32 */
BOOL      TranslateMessage(const MSG *msg);                             /* user32 */
LRESULT   DispatchMessageA(const MSG *msg);                             /* user32 */
void      PostQuitMessage(int code);                                    /* user32 */
HINSTANCE GetModuleHandleA(LPCSTR moduleName);                          /* kernel32 */
HDC       GetDC(HWND hwnd);                                             /* gdi32 */
int       ReleaseDC(HWND hwnd, HDC hdc);                                /* gdi32 */

#define WS_OVERLAPPEDWINDOW 0x00CF0000
#define WS_VISIBLE          0x10000000
#define CW_USEDEFAULT       ((int)0x80000000)
#define SW_SHOW             5
#define WM_DESTROY          0x0002
#define WM_CLOSE            0x0010
#define PM_REMOVE           0x0001
#define CS_VREDRAW          0x0001
#define CS_HREDRAW          0x0002
#define CS_OWNDC            0x0020   /* important for GL windows: one persistent DC */

#define WIN_W 640
#define WIN_H 480

static HWND s_hwnd = 0;

/* Minimal window procedure: let WM_DESTROY end the message loop via
 * PostQuitMessage, hand everything else to the default handler. This demo
 * doesn't support interactive resize/move — it just runs a fixed number of
 * frames (see run_windowed_demo) and exits on its own either way. */
static LRESULT WINAPI wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static int create_window(void) {
    HINSTANCE hinst = GetModuleHandleA(0);

    WNDCLASSEXA wc;
    int zi; for (zi = 0; zi < (int)sizeof(wc); zi++) ((char*)&wc)[zi] = 0;
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc   = wnd_proc;
    wc.hInstance     = hinst;
    wc.lpszClassName = "squashGL45WindowClass";

    if (!RegisterClassExA(&wc)) { printf("RegisterClassExA failed\n"); return 0; }

    s_hwnd = CreateWindowExA(0, "squashGL45WindowClass", "squash OpenGL 4.5 (WGL)",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             CW_USEDEFAULT, CW_USEDEFAULT, WIN_W, WIN_H,
                             0, 0, hinst, 0);
    if (!s_hwnd) { printf("CreateWindowExA failed\n"); return 0; }

    ShowWindow(s_hwnd, SW_SHOW);
    UpdateWindow(s_hwnd);
    return 1;
}

static void pump_messages(void) {
    MSG msg;
    while (PeekMessageA(&msg, 0, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

/* =========================================================================
 * Bootstrap: create a real window, a temporary WGL context on it to load
 * WGL extensions, then a proper 4.5 core context on the same window.
 * ========================================================================= */
static HDC   s_dc  = 0;
static HGLRC s_ctx = 0;

static int bootstrap_wgl(WGLExtFuncs *wf) {
    if (!create_window()) return 0;
    s_dc = GetDC(s_hwnd);
    if (!s_dc) { printf("GetDC failed\n"); return 0; }

    PIXELFORMATDESCRIPTOR pfd;
    int i; for (i = 0; i < (int)sizeof(pfd); i++) ((char*)&pfd)[i] = 0;
    pfd.nSize      = sizeof(pfd);
    pfd.nVersion   = 1;
    pfd.dwFlags    = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pf = ChoosePixelFormat(s_dc, &pfd);
    if (!pf) { printf("ChoosePixelFormat failed\n"); return 0; }
    if (!SetPixelFormat(s_dc, pf, &pfd)) {
        printf("SetPixelFormat failed\n"); return 0;
    }

    /* Create a legacy context just to call wglGetProcAddress */
    HGLRC dummy = wglCreateContext(s_dc);
    if (!dummy) { printf("wglCreateContext (dummy) failed\n"); return 0; }
    if (!wglMakeCurrent(s_dc, dummy)) {
        printf("wglMakeCurrent (dummy) failed\n"); return 0;
    }

    /* Load WGL extension functions */
    wgl_load_ext_funcs(wf);
    if (!wf->CreateContextAttribsARB) {
        printf("wglCreateContextAttribsARB not found\n");
        return 0;
    }

    /* Create a 4.5 core context on the same window DC */
    int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
        WGL_CONTEXT_MINOR_VERSION_ARB, 5,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
        WGL_CONTEXT_FLAGS_ARB,         WGL_CONTEXT_DEBUG_BIT_ARB,
        0
    };
    s_ctx = wf->CreateContextAttribsARB(s_dc, 0, attribs);
    wglMakeCurrent(0, 0);
    wglDeleteContext(dummy);

    if (!s_ctx) { printf("wglCreateContextAttribsARB (4.5) failed\n"); return 0; }
    if (!wglMakeCurrent(s_dc, s_ctx)) {
        printf("wglMakeCurrent (4.5 core) failed\n"); return 0;
    }
    return 1;
}

/* =========================================================================
 * Test: create FBO, draw a red triangle, read pixel
 * ========================================================================= */
#define FBO_W 128
#define FBO_H 128

static void test_draw_triangle_gl45(void) {
    printf("\n--- OpenGL 4.5 triangle draw (DSA, offscreen FBO) ---\n");

    /* triangle covering most of the screen, red (uAngle defaults to 0) */
    float verts[] = {
    /*   x      y     r    g    b  */
        -0.9f, -0.9f, 1.0f, 0.0f, 0.0f,
         0.9f, -0.9f, 1.0f, 0.0f, 0.0f,
         0.0f,  0.9f, 1.0f, 0.0f, 0.0f,
    };

    /* DSA: create VAO + VBO without any bind */
    GLuint vao, vbo;
    glCreateVertexArrays(1, &vao);
    glCreateBuffers(1, &vbo);
    glNamedBufferData(vbo, sizeof(verts), verts, GL_STATIC_DRAW);

    /* Attach VBO to VAO binding 0, stride = 5 floats */
    glVertexArrayVertexBuffer(vao, 0, vbo, 0, 5 * sizeof(float));

    /* aPos  : location 0, 2 floats, offset 0 */
    glEnableVertexArrayAttrib(vao, 0);
    glVertexArrayAttribFormat(vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
    glVertexArrayAttribBinding(vao, 0, 0);

    /* aColor: location 1, 3 floats, offset 2*sizeof(float) */
    glEnableVertexArrayAttrib(vao, 1);
    glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, 2 * sizeof(float));
    glVertexArrayAttribBinding(vao, 1, 0);

    /* Compile + link shaders */
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

    /* Offscreen FBO */
    GLuint fbo, rbo;
    glCreateFramebuffers(1, &fbo);
    glCreateRenderbuffers(1, &rbo);
    glNamedRenderbufferStorage(rbo, GL_RGBA8, FBO_W, FBO_H);
    glNamedFramebufferRenderbuffer(fbo, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
    GLenum fbo_status = glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER);
    CHECK("FBO complete", fbo_status == GL_FRAMEBUFFER_COMPLETE);

    /* Render */
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0, 0, FBO_W, FBO_H);
    float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    glClearNamedFramebufferfv(fbo, GL_COLOR, 0, clear);
    glUseProgram(prog);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();

    /* Read pixel at triangle centre */
    unsigned char pixel[4] = {0, 0, 0, 0};
    glReadPixels(FBO_W/2, FBO_H/2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    CHECK("centre pixel is red (R>200)", pixel[0] > 200);
    CHECK("centre pixel G < 50",         pixel[1] < 50);
    CHECK("centre pixel B < 50",         pixel[2] < 50);

    /* Corner pixel should remain black (outside triangle) */
    unsigned char corner[4] = {0, 0, 0, 0};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, corner);
    CHECK("corner pixel is black R<50", corner[0] < 50);

    /* Cleanup */
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rbo);
    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
}

static void test_gl_version(void) {
    printf("\n--- OpenGL version ---\n");
    const GLubyte *ver = glGetString(GL_VERSION);
    const GLubyte *ren = glGetString(GL_RENDERER);
    printf("  Version  : %s\n", (const char*)ver);
    printf("  Renderer : %s\n", (const char*)ren);
    /* Must be 4.5 or higher */
    GLint maj = 0, min = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &maj);
    glGetIntegerv(GL_MINOR_VERSION, &min);
    CHECK("OpenGL major >= 4", maj >= 4);
    CHECK("OpenGL minor >= 5 (if major == 4)", maj > 4 || min >= 5);
}

/* =========================================================================
 * Windowed render loop: draws a rotating triangle straight to the window's
 * default framebuffer and presents it with SwapBuffers each frame, for a
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
        pump_messages(); /* keep the window responsive; WM_DESTROY not expected here */

        glViewport(0, 0, WIN_W, WIN_H);
        glClearColor(0.08f, 0.08f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(prog);
        glUniform1f(angle_loc, angle);
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        SwapBuffers(s_dc);

        angle += 0.03f;
        Sleep(16); /* ~60 FPS */
    }
    printf("  Windowed render loop finished (%d frames).\n", frame);

    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
}

int main(void) {
    printf("=== Windows OpenGL 4.5 Tests (WGL, windowed) ===\n");

    WGLExtFuncs wf;
    int i;
    for (i = 0; i < (int)sizeof(wf); i++) ((char*)&wf)[i] = 0;

    if (!bootstrap_wgl(&wf)) {
        printf("SKIP: could not create OpenGL 4.5 context on this machine.\n");
        printf("(Requires a GPU driver that supports OpenGL 4.5)\n");
        return 0;
    }

    /* Load all GL 4.5 function pointers */
    int missing = gl_load_funcs();
    printf("gl_load_funcs(): %d functions unavailable\n", missing);
    CHECK("all core GL 4.5 functions loaded (missing==0)", missing == 0);

    test_gl_version();
    test_draw_triangle_gl45();
    run_windowed_demo();

    /* Teardown */
    wglMakeCurrent(0, 0);
    wglDeleteContext(s_ctx);
    if (s_dc) ReleaseDC(s_hwnd, s_dc);
    if (s_hwnd) DestroyWindow(s_hwnd);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
