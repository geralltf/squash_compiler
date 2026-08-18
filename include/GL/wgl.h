#ifndef WGL_H
#define WGL_H

/*
 * WGL — Windows OpenGL interface
 *
 * Covers:
 *   - Core WGL functions (exported by opengl32.dll)
 *   - WGL_ARB_pixel_format       (choose pixel formats programmatically)
 *   - WGL_ARB_create_context     (create OpenGL 3.0-4.5 core contexts)
 *   - WGL_ARB_create_context_profile
 *   - WGL_ARB_multisample        (MSAA pixel formats)
 *   - WGL_ARB_extensions_string  (query WGL extension string)
 *   - WGL_EXT_swap_control       (vsync)
 *   - WGL_ARB_pbuffer            (off-screen rendering surfaces)
 *
 * Usage pattern for an OpenGL 4.5 core context:
 *   1. Create a dummy window + dummy OpenGL 1.1 context with wglCreateContext
 *   2. Call wglGetProcAddress to load wglCreateContextAttribsARB +
 *      wglChoosePixelFormatARB
 *   3. Destroy the dummy context and window
 *   4. Re-create a real window with wglChoosePixelFormatARB pixel format
 *   5. Call wglCreateContextAttribsARB with WGL_CONTEXT_VERSION 4,5 +
 *      WGL_CONTEXT_PROFILE_MASK_ARB = WGL_CONTEXT_CORE_PROFILE_BIT_ARB
 *   6. Make it current with wglMakeCurrent, then load GL 4.5 function
 *      pointers via gl_funcs.h.
 *
 * Link: -lopengl32  (squash: -l opengl32 or -lopengl32)
 */

/* =========================================================================
 * Minimal Windows types — avoids pulling in the full windows.h
 * ========================================================================= */
#ifndef _WINDOWS_   /* skip if windows.h already included */

typedef void*          HANDLE;
typedef HANDLE         HWND;
typedef HANDLE         HDC;
typedef HANDLE         HGLRC;
typedef HANDLE         HMODULE;
typedef int            BOOL;
typedef unsigned int   UINT;
typedef unsigned long  DWORD;
typedef long           LONG;
typedef unsigned short WORD;
typedef unsigned char  BYTE;
typedef const char*    LPCSTR;
typedef float          FLOAT;

typedef struct tagPOINT { LONG x, y; } POINT;
typedef struct tagRECT  { LONG left, top, right, bottom; } RECT;

#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif

#endif /* _WINDOWS_ */

/* WINAPI calling convention — ignored in x64 (single ABI), needed for 32-bit */
#ifndef WINAPI
#ifdef _WIN32
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#endif

/* =========================================================================
 * PIXELFORMATDESCRIPTOR — used by ChoosePixelFormat / SetPixelFormat
 * (from gdi32.dll; included here for completeness)
 * ========================================================================= */
#define PFD_TYPE_RGBA            0
#define PFD_TYPE_COLORINDEX      1

#define PFD_MAIN_PLANE           0
#define PFD_OVERLAY_PLANE        1
#define PFD_UNDERLAY_PLANE       (-1)

#define PFD_DOUBLEBUFFER         0x00000001
#define PFD_STEREO               0x00000002
#define PFD_DRAW_TO_WINDOW       0x00000004
#define PFD_DRAW_TO_BITMAP       0x00000008
#define PFD_SUPPORT_GDI          0x00000010
#define PFD_SUPPORT_OPENGL       0x00000020
#define PFD_GENERIC_FORMAT       0x00000040
#define PFD_NEED_PALETTE         0x00000080
#define PFD_NEED_SYSTEM_PALETTE  0x00000100
#define PFD_SWAP_EXCHANGE        0x00000200
#define PFD_SWAP_COPY            0x00000400
#define PFD_SWAP_LAYER_BUFFERS   0x00000800
#define PFD_GENERIC_ACCELERATED  0x00001000
#define PFD_SUPPORT_DIRECTDRAW   0x00002000
#define PFD_DIRECT3D_ACCELERATED 0x00004000
#define PFD_SUPPORT_COMPOSITION  0x00008000
#define PFD_DEPTH_DONTCARE       0x20000000
#define PFD_DOUBLEBUFFER_DONTCARE 0x40000000
#define PFD_STEREO_DONTCARE      0x80000000

typedef struct tagPIXELFORMATDESCRIPTOR {
    WORD  nSize;
    WORD  nVersion;
    DWORD dwFlags;
    BYTE  iPixelType;
    BYTE  cColorBits;
    BYTE  cRedBits;     BYTE  cRedShift;
    BYTE  cGreenBits;   BYTE  cGreenShift;
    BYTE  cBlueBits;    BYTE  cBlueShift;
    BYTE  cAlphaBits;   BYTE  cAlphaShift;
    BYTE  cAccumBits;
    BYTE  cAccumRedBits; BYTE cAccumGreenBits;
    BYTE  cAccumBlueBits; BYTE cAccumAlphaBits;
    BYTE  cDepthBits;
    BYTE  cStencilBits;
    BYTE  cAuxBuffers;
    BYTE  iLayerType;
    BYTE  bReserved;
    DWORD dwLayerMask;
    DWORD dwVisibleMask;
    DWORD dwDamageMask;
} PIXELFORMATDESCRIPTOR;

/* GDI pixel format functions (gdi32.dll) */
int  WINAPI ChoosePixelFormat(HDC hDC, const PIXELFORMATDESCRIPTOR *pfd);
BOOL WINAPI SetPixelFormat   (HDC hDC, int format, const PIXELFORMATDESCRIPTOR *pfd);
BOOL WINAPI SwapBuffers      (HDC hDC);
int  WINAPI DescribePixelFormat(HDC hDC, int iPixelFormat, UINT nBytes,
                                PIXELFORMATDESCRIPTOR *pfd);

/* =========================================================================
 * Core WGL functions — exported directly from opengl32.dll
 * ========================================================================= */
HGLRC WINAPI wglCreateContext    (HDC hDC);
BOOL  WINAPI wglDeleteContext    (HGLRC hglrc);
BOOL  WINAPI wglMakeCurrent      (HDC hDC, HGLRC hglrc);
HGLRC WINAPI wglGetCurrentContext(void);
HDC   WINAPI wglGetCurrentDC     (void);
void* WINAPI wglGetProcAddress   (LPCSTR lpszProc);
BOOL  WINAPI wglShareLists       (HGLRC hglrc1, HGLRC hglrc2);
BOOL  WINAPI wglCopyContext      (HGLRC src, HGLRC dst, UINT mask);
BOOL  WINAPI wglSwapLayerBuffers (HDC hDC, UINT fuPlanes);

/* =========================================================================
 * WGL_ARB_extensions_string
 * ========================================================================= */
#define WGL_ARB_extensions_string 1
typedef const char* (WINAPI *PFNWGLGETEXTENSIONSSTRINGARBPROC)(HDC hDC);

/* =========================================================================
 * WGL_ARB_pixel_format
 * Attribute keys for wglChoosePixelFormatARB
 * ========================================================================= */
#define WGL_ARB_pixel_format 1

#define WGL_NUMBER_PIXEL_FORMATS_ARB    0x2000
#define WGL_DRAW_TO_WINDOW_ARB          0x2001
#define WGL_DRAW_TO_BITMAP_ARB          0x2002
#define WGL_ACCELERATION_ARB            0x2003
#define WGL_NEED_PALETTE_ARB            0x2004
#define WGL_NEED_SYSTEM_PALETTE_ARB     0x2005
#define WGL_SWAP_LAYER_BUFFERS_ARB      0x2006
#define WGL_SWAP_METHOD_ARB             0x2007
#define WGL_NUMBER_OVERLAYS_ARB         0x2008
#define WGL_NUMBER_UNDERLAYS_ARB        0x2009
#define WGL_TRANSPARENT_ARB             0x200A
#define WGL_SHARE_DEPTH_ARB             0x200C
#define WGL_SHARE_STENCIL_ARB           0x200D
#define WGL_SHARE_ACCUM_ARB             0x200E
#define WGL_SUPPORT_GDI_ARB             0x200F
#define WGL_SUPPORT_OPENGL_ARB          0x2010
#define WGL_DOUBLE_BUFFER_ARB           0x2011
#define WGL_STEREO_ARB                  0x2012
#define WGL_PIXEL_TYPE_ARB              0x2013
#define WGL_COLOR_BITS_ARB              0x2014
#define WGL_RED_BITS_ARB                0x2015
#define WGL_RED_SHIFT_ARB               0x2016
#define WGL_GREEN_BITS_ARB              0x2017
#define WGL_GREEN_SHIFT_ARB             0x2018
#define WGL_BLUE_BITS_ARB               0x2019
#define WGL_BLUE_SHIFT_ARB              0x201A
#define WGL_ALPHA_BITS_ARB              0x201B
#define WGL_ALPHA_SHIFT_ARB             0x201C
#define WGL_ACCUM_BITS_ARB              0x201D
#define WGL_ACCUM_RED_BITS_ARB          0x201E
#define WGL_ACCUM_GREEN_BITS_ARB        0x201F
#define WGL_ACCUM_BLUE_BITS_ARB         0x2020
#define WGL_ACCUM_ALPHA_BITS_ARB        0x2021
#define WGL_DEPTH_BITS_ARB              0x2022
#define WGL_STENCIL_BITS_ARB           0x2023
#define WGL_AUX_BUFFERS_ARB             0x2024
#define WGL_NO_ACCELERATION_ARB         0x2025
#define WGL_GENERIC_ACCELERATION_ARB    0x2026
#define WGL_FULL_ACCELERATION_ARB       0x2027
#define WGL_SWAP_EXCHANGE_ARB           0x2028
#define WGL_SWAP_COPY_ARB               0x2029
#define WGL_SWAP_UNDEFINED_ARB          0x202A
#define WGL_TYPE_RGBA_ARB               0x202B
#define WGL_TYPE_COLORINDEX_ARB         0x202C
#define WGL_SAMPLE_BUFFERS_ARB          0x2041
#define WGL_SAMPLES_ARB                 0x2042

typedef BOOL (WINAPI *PFNWGLCHOOSEPIXELFORMATARBPROC)(HDC hDC,
    const int *piAttribIList, const FLOAT *pfAttribFList,
    UINT nMaxFormats, int *piFormats, UINT *nNumFormats);
typedef BOOL (WINAPI *PFNWGLGETPIXELFORMATATTRIBIVARBPROC)(HDC hDC,
    int iPixelFormat, int iLayerPlane, UINT nAttributes,
    const int *piAttributes, int *piValues);

/* =========================================================================
 * WGL_ARB_create_context / WGL_ARB_create_context_profile
 * Used to create an OpenGL 3.2 - 4.5 core context
 * ========================================================================= */
#define WGL_ARB_create_context         1
#define WGL_ARB_create_context_profile 1

#define WGL_CONTEXT_MAJOR_VERSION_ARB           0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB           0x2092
#define WGL_CONTEXT_LAYER_PLANE_ARB             0x2093
#define WGL_CONTEXT_FLAGS_ARB                   0x2094
#define WGL_CONTEXT_PROFILE_MASK_ARB            0x9126

#define WGL_CONTEXT_DEBUG_BIT_ARB               0x0001
#define WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB  0x0002
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB        0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002

#define ERROR_INVALID_VERSION_ARB               0x2095
#define ERROR_INVALID_PROFILE_ARB               0x2096

typedef HGLRC (WINAPI *PFNWGLCREATECONTEXTATTRIBSARBPROC)(
    HDC hDC, HGLRC hShareContext, const int *attribList);

/* =========================================================================
 * WGL_ARB_create_context_no_error (4.6)
 * ========================================================================= */
#define WGL_CONTEXT_OPENGL_NO_ERROR_ARB         0x31B3

/* =========================================================================
 * WGL_EXT_swap_control  (vsync control)
 * ========================================================================= */
#define WGL_EXT_swap_control 1

typedef BOOL (WINAPI *PFNWGLSWAPINTERVALEXTPROC)  (int interval);
typedef int  (WINAPI *PFNWGLGETSWAPINTERVALEXTPROC)(void);

/* =========================================================================
 * WGL_ARB_pbuffer  (off-screen rendering)
 * ========================================================================= */
#define WGL_ARB_pbuffer 1
typedef void* HPBUFFERARB;

#define WGL_DRAW_TO_PBUFFER_ARB        0x202D
#define WGL_MAX_PBUFFER_PIXELS_ARB     0x202E
#define WGL_MAX_PBUFFER_WIDTH_ARB      0x202F
#define WGL_MAX_PBUFFER_HEIGHT_ARB     0x2030
#define WGL_PBUFFER_LARGEST_ARB        0x2033
#define WGL_PBUFFER_WIDTH_ARB          0x2034
#define WGL_PBUFFER_HEIGHT_ARB         0x2035
#define WGL_PBUFFER_LOST_ARB           0x2036

typedef HPBUFFERARB (WINAPI *PFNWGLCREATEPBUFFERARBPROC)(
    HDC hDC, int iPixelFormat, int iWidth, int iHeight, const int *piAttribList);
typedef HDC         (WINAPI *PFNWGLGETPBUFFERDCARBPROC)  (HPBUFFERARB hPbuffer);
typedef int         (WINAPI *PFNWGLRELEASEPBUFFERDCARBPROC)(HPBUFFERARB hPbuffer, HDC hDC);
typedef BOOL        (WINAPI *PFNWGLDESTROYPBUFFERARBPROC)(HPBUFFERARB hPbuffer);
typedef BOOL        (WINAPI *PFNWGLQUERYPBUFFERARBPROC)  (HPBUFFERARB hPbuffer,
                                                           int iAttribute, int *piValue);

/* =========================================================================
 * WGL_ARB_render_texture
 * ========================================================================= */
#define WGL_ARB_render_texture 1
#define WGL_BIND_TO_TEXTURE_RGBA_ARB   0x2071
#define WGL_BIND_TO_TEXTURE_RGB_ARB    0x2070
#define WGL_TEXTURE_FORMAT_ARB         0x2072
#define WGL_TEXTURE_TARGET_ARB         0x2073
#define WGL_MIPMAP_TEXTURE_ARB         0x2074
#define WGL_TEXTURE_RGBA_ARB           0x2075
#define WGL_TEXTURE_RGB_ARB            0x2076
#define WGL_TEXTURE_2D_ARB             0x20AD

typedef BOOL (WINAPI *PFNWGLBINDTEXIMAGEARBPROC)    (HPBUFFERARB hPbuffer, int iBuffer);
typedef BOOL (WINAPI *PFNWGLRELEASETEXIMAGEARBPROC)  (HPBUFFERARB hPbuffer, int iBuffer);
typedef BOOL (WINAPI *PFNWGLSETPBUFFERATTRIBARBPROC) (HPBUFFERARB hPbuffer,
                                                       const int *piAttribList);

/* =========================================================================
 * Convenience: a struct that holds the loaded WGL extension function pointers
 * ========================================================================= */
typedef struct WGLExtFuncs_s {
    PFNWGLGETEXTENSIONSSTRINGARBPROC  GetExtensionsStringARB;
    PFNWGLCHOOSEPIXELFORMATARBPROC    ChoosePixelFormatARB;
    PFNWGLGETPIXELFORMATATTRIBIVARBPROC GetPixelFormatAttribivARB;
    PFNWGLCREATECONTEXTATTRIBSARBPROC CreateContextAttribsARB;
    PFNWGLSWAPINTERVALEXTPROC         SwapIntervalEXT;
    PFNWGLGETSWAPINTERVALEXTPROC      GetSwapIntervalEXT;
    PFNWGLCREATEPBUFFERARBPROC        CreatePbufferARB;
    PFNWGLGETPBUFFERDCARBPROC         GetPbufferDCARB;
    PFNWGLRELEASEPBUFFERDCARBPROC     ReleasePbufferDCARB;
    PFNWGLDESTROYPBUFFERARBPROC       DestroyPbufferARB;
    PFNWGLQUERYPBUFFERARBPROC         QueryPbufferARB;
} WGLExtFuncs;

/* Load all WGL extension function pointers (call after any WGL context is current) */
static int wgl_load_ext_funcs(WGLExtFuncs *wf) {
    int ok = 1;
#define WGL_LOAD(T, name) \
    wf->name = (T)wglGetProcAddress("wgl" #name); \
    if (!wf->name) ok = 0
    WGL_LOAD(PFNWGLGETEXTENSIONSSTRINGARBPROC,   GetExtensionsStringARB);
    WGL_LOAD(PFNWGLCHOOSEPIXELFORMATARBPROC,     ChoosePixelFormatARB);
    WGL_LOAD(PFNWGLGETPIXELFORMATATTRIBIVARBPROC, GetPixelFormatAttribivARB);
    WGL_LOAD(PFNWGLCREATECONTEXTATTRIBSARBPROC,  CreateContextAttribsARB);
    WGL_LOAD(PFNWGLSWAPINTERVALEXTPROC,          SwapIntervalEXT);
    WGL_LOAD(PFNWGLGETSWAPINTERVALEXTPROC,       GetSwapIntervalEXT);
    WGL_LOAD(PFNWGLCREATEPBUFFERARBPROC,         CreatePbufferARB);
    WGL_LOAD(PFNWGLGETPBUFFERDCARBPROC,          GetPbufferDCARB);
    WGL_LOAD(PFNWGLRELEASEPBUFFERDCARBPROC,      ReleasePbufferDCARB);
    WGL_LOAD(PFNWGLDESTROYPBUFFERARBPROC,        DestroyPbufferARB);
    WGL_LOAD(PFNWGLQUERYPBUFFERARBPROC,          QueryPbufferARB);
#undef WGL_LOAD
    return ok;
}

#endif /* WGL_H */
