#ifndef GLX_H
#define GLX_H

/*
 * GLX — OpenGL Extension for the X Window System (Linux / BSD)
 *
 * Covers:
 *   - Core GLX 1.4 functions (in libGL.so)
 *   - GLX_ARB_create_context     (OpenGL 3.0-4.5 core contexts)
 *   - GLX_ARB_create_context_profile
 *   - GLX_EXT_swap_control       (vsync)
 *   - GLX_EXT_swap_control_tear
 *   - GLX_ARB_multisample        (MSAA)
 *   - GLX_ARB_framebuffer_sRGB
 *
 * Usage pattern for an OpenGL 4.5 core context:
 *   1. Open Display with XOpenDisplay
 *   2. Call glXChooseFBConfig with desired attributes
 *   3. Load glXCreateContextAttribsARB via glXGetProcAddressARB
 *   4. Call glXCreateContextAttribsARB with version 4,5 +
 *      GLX_CONTEXT_CORE_PROFILE_BIT_ARB
 *   5. Create XWindow from the chosen FBConfig visual
 *   6. glXMakeContextCurrent, then load GL 4.5 pointers via gl_funcs.h
 *
 * Link: -lGL -lX11  (squash: -lGL -lX11)
 */

/* =========================================================================
 * X11 / GLX types — forward declarations so X11/Xlib.h is optional
 * ========================================================================= */
#ifndef __XLIB_H    /* skip if Xlib.h already included */
typedef void*          Display;
typedef unsigned long  Window;
typedef unsigned long  Drawable;
typedef unsigned long  Pixmap;
typedef unsigned long  Font;
typedef unsigned long  Colormap;
typedef unsigned long  XID;
typedef unsigned long  VisualID;
typedef unsigned int   Bool;   /* X11 Bool */
typedef int            Status;

typedef struct {
    unsigned long pixel;
    unsigned char red, green, blue;
    char flags;
    char pad;
} XColor;

typedef struct _XVisualInfo {
    void       *visual;
    VisualID    visualid;
    int         screen;
    int         depth;
    int         c_class;
    unsigned long red_mask, green_mask, blue_mask;
    int         colormap_size;
    int         bits_per_rgb;
} XVisualInfo;
#endif /* __XLIB_H */

/* GLX-specific types */
typedef void* GLXContext;
typedef void* GLXDrawable;
typedef void* GLXFBConfig;
typedef void* GLXPbuffer;
typedef void* GLXWindow;
typedef void* GLXPixmap;

/* =========================================================================
 * GLX 1.2 / 1.3 base functions (libGL.so)
 * ========================================================================= */
typedef void (*__GLXextFuncPtr)(void);

GLXContext   glXCreateContext      (Display *dpy, XVisualInfo *vis,
                                    GLXContext shareList, Bool direct);
void         glXDestroyContext     (Display *dpy, GLXContext ctx);
Bool         glXMakeCurrent        (Display *dpy, GLXDrawable drawable, GLXContext ctx);
Bool         glXMakeContextCurrent (Display *dpy, GLXDrawable draw,
                                    GLXDrawable read, GLXContext ctx);
void         glXSwapBuffers        (Display *dpy, GLXDrawable drawable);
GLXContext   glXGetCurrentContext  (void);
Display*     glXGetCurrentDisplay  (void);
GLXDrawable  glXGetCurrentDrawable (void);
GLXDrawable  glXGetCurrentReadDrawable(void);
void         glXWaitGL             (void);
void         glXWaitX              (void);
void         glXUseXFont           (Font font, int first, int count, int listBase);
const char*  glXQueryExtensionsString(Display *dpy, int screen);
const char*  glXGetClientString    (Display *dpy, int name);
const char*  glXQueryServerString  (Display *dpy, int screen, int name);
Bool         glXQueryExtension     (Display *dpy, int *errorBase, int *eventBase);
Bool         glXQueryVersion       (Display *dpy, int *major, int *minor);
int          glXGetConfig          (Display *dpy, XVisualInfo *vis, int attrib, int *value);
XVisualInfo* glXChooseVisual       (Display *dpy, int screen, int *attribList);

/* GLX 1.3 */
GLXFBConfig* glXChooseFBConfig     (Display *dpy, int screen,
                                    const int *attribList, int *nitems);
GLXFBConfig* glXGetFBConfigs       (Display *dpy, int screen, int *nelements);
int          glXGetFBConfigAttrib  (Display *dpy, GLXFBConfig config,
                                    int attribute, int *value);
XVisualInfo* glXGetVisualFromFBConfig(Display *dpy, GLXFBConfig config);
GLXWindow    glXCreateWindow       (Display *dpy, GLXFBConfig config,
                                    Window win, const int *attribList);
void         glXDestroyWindow      (Display *dpy, GLXWindow win);
GLXPixmap    glXCreatePixmap       (Display *dpy, GLXFBConfig config,
                                    Pixmap pixmap, const int *attribList);
void         glXDestroyPixmap      (Display *dpy, GLXPixmap pixmap);
GLXPbuffer   glXCreatePbuffer      (Display *dpy, GLXFBConfig config,
                                    const int *attribList);
void         glXDestroyPbuffer     (Display *dpy, GLXPbuffer pbuf);
void         glXQueryDrawable      (Display *dpy, GLXDrawable draw,
                                    int attribute, unsigned int *value);
GLXContext   glXCreateNewContext   (Display *dpy, GLXFBConfig config, int renderType,
                                    GLXContext shareList, Bool direct);
Bool         glXIsDirect           (Display *dpy, GLXContext ctx);
int          glXQueryContext       (Display *dpy, GLXContext ctx, int attribute, int *value);
void         glXSelectEvent        (Display *dpy, GLXDrawable draw, unsigned long mask);
void         glXGetSelectedEvent   (Display *dpy, GLXDrawable draw, unsigned long *mask);

/* GLX 1.4 */
__GLXextFuncPtr glXGetProcAddress   (const unsigned char *procName);
__GLXextFuncPtr glXGetProcAddressARB(const unsigned char *procName);

/* =========================================================================
 * GLX attribute constants (for glXChooseVisual / glXChooseFBConfig)
 * ========================================================================= */
#define GLX_USE_GL               1
#define GLX_BUFFER_SIZE          2
#define GLX_LEVEL                3
#define GLX_RGBA                 4
#define GLX_DOUBLEBUFFER         5
#define GLX_STEREO               6
#define GLX_AUX_BUFFERS          7
#define GLX_RED_SIZE             8
#define GLX_GREEN_SIZE           9
#define GLX_BLUE_SIZE            10
#define GLX_ALPHA_SIZE           11
#define GLX_DEPTH_SIZE           12
#define GLX_STENCIL_SIZE         13
#define GLX_ACCUM_RED_SIZE       14
#define GLX_ACCUM_GREEN_SIZE     15
#define GLX_ACCUM_BLUE_SIZE      16
#define GLX_ACCUM_ALPHA_SIZE     17

/* Error codes */
#define GLX_BAD_SCREEN           1
#define GLX_BAD_ATTRIBUTE        2
#define GLX_NO_EXTENSION         3
#define GLX_BAD_VISUAL           4
#define GLX_BAD_CONTEXT          5
#define GLX_BAD_VALUE            6
#define GLX_BAD_ENUM             7

/* GLX 1.3 FBConfig attributes */
#define GLX_WINDOW_BIT           0x00000001
#define GLX_PIXMAP_BIT           0x00000002
#define GLX_PBUFFER_BIT          0x00000004
#define GLX_RGBA_BIT             0x00000001
#define GLX_COLOR_INDEX_BIT      0x00000002

#define GLX_NONE                 0x8000
#define GLX_SLOW_CONFIG          0x8001
#define GLX_TRUE_COLOR           0x8002
#define GLX_DIRECT_COLOR         0x8003
#define GLX_PSEUDO_COLOR         0x8004
#define GLX_STATIC_COLOR         0x8005
#define GLX_GRAY_SCALE           0x8006
#define GLX_STATIC_GRAY          0x8007
#define GLX_TRANSPARENT_RGB      0x8008
#define GLX_TRANSPARENT_INDEX    0x8009
#define GLX_VISUAL_ID            0x800B
#define GLX_SCREEN               0x800C
#define GLX_NON_CONFORMANT_CONFIG 0x800D
#define GLX_DRAWABLE_TYPE        0x8010
#define GLX_RENDER_TYPE          0x8011
#define GLX_X_RENDERABLE         0x8012
#define GLX_FBCONFIG_ID          0x8013
#define GLX_X_VISUAL_TYPE        0x8014
#define GLX_CONFIG_CAVEAT        0x20
#define GLX_TRANSPARENT_TYPE     0x23
#define GLX_TRANSPARENT_INDEX_VALUE 0x24
#define GLX_TRANSPARENT_RED_VALUE   0x25
#define GLX_TRANSPARENT_GREEN_VALUE 0x26
#define GLX_TRANSPARENT_BLUE_VALUE  0x27
#define GLX_TRANSPARENT_ALPHA_VALUE 0x28
#define GLX_MAX_PBUFFER_WIDTH    0x8016
#define GLX_MAX_PBUFFER_HEIGHT   0x8017
#define GLX_MAX_PBUFFER_PIXELS   0x8018
#define GLX_PRESERVED_CONTENTS  0x801B
#define GLX_LARGEST_PBUFFER      0x801C
#define GLX_WIDTH                0x801D
#define GLX_HEIGHT               0x801E
#define GLX_EVENT_MASK           0x801F
#define GLX_DAMAGED              0x8020
#define GLX_SAVED                0x8021
#define GLX_WINDOW               0x8022
#define GLX_PBUFFER              0x8023
#define GLX_PBUFFER_HEIGHT       0x8040
#define GLX_PBUFFER_WIDTH        0x8041

/* GLX client string names */
#define GLX_VENDOR               1
#define GLX_VERSION              2
#define GLX_EXTENSIONS           3

/* Render type */
#define GLX_RGBA_TYPE            0x8014
#define GLX_COLOR_INDEX_TYPE     0x8015

/* =========================================================================
 * GLX_ARB_create_context / GLX_ARB_create_context_profile
 * For creating OpenGL 3.2 - 4.5 core contexts
 * ========================================================================= */
#define GLX_ARB_create_context         1
#define GLX_ARB_create_context_profile 1

#define GLX_CONTEXT_MAJOR_VERSION_ARB        0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB        0x2092
#define GLX_CONTEXT_FLAGS_ARB                0x2094
#define GLX_CONTEXT_PROFILE_MASK_ARB         0x9126

#define GLX_CONTEXT_DEBUG_BIT_ARB            0x0001
#define GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB 0x0002
#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB     0x00000001
#define GLX_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002

typedef GLXContext (*PFNGLXCREATECONTEXTATTRIBSARBPROC)(
    Display *dpy, GLXFBConfig config, GLXContext share_context,
    Bool direct, const int *attrib_list);

/* =========================================================================
 * GLX_EXT_swap_control / GLX_EXT_swap_control_tear
 * ========================================================================= */
#define GLX_EXT_swap_control      1
#define GLX_EXT_swap_control_tear 1

#define GLX_SWAP_INTERVAL_EXT     0x20F1
#define GLX_MAX_SWAP_INTERVAL_EXT 0x20F2
#define GLX_LATE_SWAPS_TEAR_EXT   0x20F3

typedef void (*PFNGLXSWAPINTERVALEXTPROC)(Display *dpy, GLXDrawable drawable, int interval);
typedef int  (*PFNGLXGETSWAPINTERVALEXTPROC)(Display *dpy, GLXDrawable drawable);

/* GLX_MESA_swap_control (alternative, common on Mesa) */
typedef int  (*PFNGLXSWAPINTERVALMESAPROC)(unsigned int interval);
typedef int  (*PFNGLXGETSWAPINTERVALMESAPROC)(void);

/* =========================================================================
 * GLX_ARB_multisample  (MSAA)
 * ========================================================================= */
#define GLX_ARB_multisample      1
#define GLX_SAMPLE_BUFFERS_ARB   100000
#define GLX_SAMPLES_ARB          100001

/* =========================================================================
 * GLX_ARB_framebuffer_sRGB
 * ========================================================================= */
#define GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB 0x20B2

/* =========================================================================
 * GLX_EXT_texture_from_pixmap
 * ========================================================================= */
#define GLX_TEXTURE_2D_BIT_EXT           0x00000002
#define GLX_TEXTURE_RECTANGLE_BIT_EXT    0x00000004
#define GLX_BIND_TO_TEXTURE_RGB_EXT      0x20D0
#define GLX_BIND_TO_TEXTURE_RGBA_EXT     0x20D1
#define GLX_BIND_TO_MIPMAP_TEXTURE_EXT   0x20D2
#define GLX_BIND_TO_TEXTURE_TARGETS_EXT  0x20D3
#define GLX_Y_INVERTED_EXT               0x20D4
#define GLX_TEXTURE_FORMAT_EXT           0x20D5
#define GLX_TEXTURE_TARGET_EXT           0x20D6
#define GLX_MIPMAP_TEXTURE_EXT           0x20D7
#define GLX_TEXTURE_FORMAT_NONE_EXT      0x20D8
#define GLX_TEXTURE_FORMAT_RGB_EXT       0x20D9
#define GLX_TEXTURE_FORMAT_RGBA_EXT      0x20DA
#define GLX_TEXTURE_2D_EXT               0x20DC
#define GLX_TEXTURE_RECTANGLE_EXT        0x20DD
#define GLX_FRONT_LEFT_EXT               0x20DE

/* =========================================================================
 * Convenience: struct holding loaded GLX extension function pointers
 * ========================================================================= */
typedef struct GLXExtFuncs_s {
    PFNGLXCREATECONTEXTATTRIBSARBPROC CreateContextAttribsARB;
    PFNGLXSWAPINTERVALEXTPROC         SwapIntervalEXT;
    PFNGLXGETSWAPINTERVALEXTPROC      GetSwapIntervalEXT;
    PFNGLXSWAPINTERVALMESAPROC        SwapIntervalMESA;
    PFNGLXGETSWAPINTERVALMESAPROC     GetSwapIntervalMESA;
} GLXExtFuncs;

static int glx_load_ext_funcs(GLXExtFuncs *gf) {
    int ok = 1;
#define GLX_LOAD(T, sym) \
    gf->sym = (T)glXGetProcAddressARB((const unsigned char*)"glX" #sym); \
    if (!gf->sym) ok = 0
    GLX_LOAD(PFNGLXCREATECONTEXTATTRIBSARBPROC, CreateContextAttribsARB);
    GLX_LOAD(PFNGLXSWAPINTERVALEXTPROC,          SwapIntervalEXT);
    GLX_LOAD(PFNGLXGETSWAPINTERVALEXTPROC,       GetSwapIntervalEXT);
    GLX_LOAD(PFNGLXSWAPINTERVALMESAPROC,         SwapIntervalMESA);
    GLX_LOAD(PFNGLXGETSWAPINTERVALMESAPROC,      GetSwapIntervalMESA);
#undef GLX_LOAD
    return ok;
}

#endif /* GLX_H */
