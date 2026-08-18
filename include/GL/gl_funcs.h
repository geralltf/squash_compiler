#ifndef GL_FUNCS_H
#define GL_FUNCS_H

/*
 * gl_funcs.h — Cross-platform OpenGL 1.2 – 4.5 function-pointer loader
 *
 * WHY THIS EXISTS
 *   On Windows, opengl32.dll only exports OpenGL 1.0/1.1 symbols directly.
 *   Every function from 1.2 onward must be fetched at runtime with
 *   wglGetProcAddress().  On Linux (libGL.so) the symbols are exported
 *   directly, but glXGetProcAddress() also works and is the portable path.
 *
 * USAGE
 *   1. Include this header INSTEAD OF (or BEFORE) <GL/glext.h>.
 *      It sets GL_SKIP_GLEXT so gl.h won't re-declare the same functions.
 *   2. In exactly ONE .c file define GL_FUNCS_IMPLEMENTATION before the include:
 *        #define GL_FUNCS_IMPLEMENTATION
 *        #include <GL/gl_funcs.h>
 *      This emits the global function-pointer definitions + the loader.
 *   3. After making your OpenGL context current, call:
 *        int missing = gl_load_funcs();
 *      Returns the number of functions that could not be loaded (0 = all good).
 *
 * FUNCTION NAMES
 *   All OpenGL 4.5 functions remain accessible under their standard names
 *   (glCreateShader, glDrawArrays, etc.) via #define macros that redirect
 *   to the underlying function pointers.  Code written against the standard
 *   API compiles unchanged.
 *
 * PLATFORM SELECTION
 *   Windows : wglGetProcAddress (link -lopengl32)
 *   Linux   : glXGetProcAddressARB (link -lGL)
 *   Auto-detected via _WIN32 / else.
 */

/* Tell gl.h / glext.h to skip their direct-declaration sections */
#define GL_SKIP_GLEXT

/* Base types from gl.h */
#ifndef GL_H
#include "gl.h"
#endif

/* =========================================================================
 * PFNGL* typedefs for every OpenGL 1.2 – 4.5 function
 * ========================================================================= */

/* --- OpenGL 1.2 --- */
typedef void (*PFNGLDRAWRANGEELEMENTSPROC)(GLenum, GLuint, GLuint, GLsizei, GLenum, const GLvoid*);
typedef void (*PFNGLTEXIMAGE3DPROC)(GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const GLvoid*);
typedef void (*PFNGLTEXSUBIMAGE3DPROC)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*);
typedef void (*PFNGLCOPYTEXSUBIMAGE3DPROC)(GLenum, GLint, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei);

/* --- OpenGL 1.3 --- */
typedef void (*PFNGLACTIVETEXTUREPROC)(GLenum);
typedef void (*PFNGLCLIENTACTIVETEXTUREPROC)(GLenum);
typedef void (*PFNGLCOMPRESSEDTEXIMAGE3DPROC)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLsizei, GLint, GLsizei, const GLvoid*);
typedef void (*PFNGLCOMPRESSEDTEXIMAGE2DPROC)(GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const GLvoid*);
typedef void (*PFNGLCOMPRESSEDTEXIMAGE1DPROC)(GLenum, GLint, GLenum, GLsizei, GLint, GLsizei, const GLvoid*);
typedef void (*PFNGLCOMPRESSEDTEXSUBIMAGE3DPROC)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid*);
typedef void (*PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const GLvoid*);
typedef void (*PFNGLCOMPRESSEDTEXSUBIMAGE1DPROC)(GLenum, GLint, GLint, GLsizei, GLenum, GLsizei, const GLvoid*);
typedef void (*PFNGLGETCOMPRESSEDTEXIMAGEPROC)(GLenum, GLint, GLvoid*);

/* --- OpenGL 1.4 --- */
typedef void (*PFNGLBLENDFUNCSEPARATEPROC)(GLenum, GLenum, GLenum, GLenum);
typedef void (*PFNGLMULTIDRAWARRAYSPROC)(GLenum, const GLint*, const GLsizei*, GLsizei);
typedef void (*PFNGLMULTIDRAWELEMENTSPROC)(GLenum, const GLsizei*, GLenum, const GLvoid**, GLsizei);
typedef void (*PFNGLPOINTPARAMETERFPROC)(GLenum, GLfloat);
typedef void (*PFNGLPOINTPARAMETERFVPROC)(GLenum, const GLfloat*);
typedef void (*PFNGLBLENDCOLORPROC)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (*PFNGLBLENDEQUATIONPROC)(GLenum);

/* --- OpenGL 1.5 --- */
typedef void     (*PFNGLGENBUFFERSPROC)(GLsizei, GLuint*);
typedef void     (*PFNGLDELETEBUFFERSPROC)(GLsizei, const GLuint*);
typedef void     (*PFNGLBINDBUFFERPROC)(GLenum, GLuint);
typedef void     (*PFNGLBUFFERDATAPROC)(GLenum, long long, const GLvoid*, GLenum);
typedef void     (*PFNGLBUFFERSUBDATAPROC)(GLenum, long long, long long, const GLvoid*);
typedef GLvoid*  (*PFNGLMAPBUFFERPROC)(GLenum, GLenum);
typedef GLboolean(*PFNGLUNMAPBUFFERPROC)(GLenum);
typedef void     (*PFNGLGETBUFFERSUBDATAPROC)(GLenum, long long, long long, GLvoid*);
typedef void     (*PFNGLGETBUFFERPARAMETERIVPROC)(GLenum, GLenum, GLint*);
typedef void     (*PFNGLGENQUERIESPROC)(GLsizei, GLuint*);
typedef void     (*PFNGLDELETEQUERIESPROC)(GLsizei, const GLuint*);
typedef void     (*PFNGLBEGINQUERYPROC)(GLenum, GLuint);
typedef void     (*PFNGLENDQUERYPROC)(GLenum);
typedef void     (*PFNGLGETQUERYOBJECTIVPROC)(GLuint, GLenum, GLint*);
typedef void     (*PFNGLGETQUERYOBJECTUIVPROC)(GLuint, GLenum, GLuint*);

/* --- OpenGL 2.0 --- */
typedef GLuint   (*PFNGLCREATESHADERPROC)(GLenum);
typedef void     (*PFNGLSHADERSOURCEPROC)(GLuint, GLsizei, const char**, const GLint*);
typedef void     (*PFNGLCOMPILESHADERPROC)(GLuint);
typedef void     (*PFNGLGETSHADERIVPROC)(GLuint, GLenum, GLint*);
typedef void     (*PFNGLGETSHADERINFOLOGPROC)(GLuint, GLsizei, GLsizei*, char*);
typedef void     (*PFNGLDELETESHADERPROC)(GLuint);
typedef GLuint   (*PFNGLCREATEPROGRAMPROC)(void);
typedef void     (*PFNGLATTACHSHADERPROC)(GLuint, GLuint);
typedef void     (*PFNGLDETACHSHADERPROC)(GLuint, GLuint);
typedef void     (*PFNGLLINKPROGRAMPROC)(GLuint);
typedef void     (*PFNGLUSEPROGRAMPROC)(GLuint);
typedef void     (*PFNGLGETPROGRAMIVPROC)(GLuint, GLenum, GLint*);
typedef void     (*PFNGLGETPROGRAMINFOLOGPROC)(GLuint, GLsizei, GLsizei*, char*);
typedef void     (*PFNGLDELETEPROGRAMPROC)(GLuint);
typedef void     (*PFNGLVALIDATEPROGRAMPROC)(GLuint);
typedef GLint    (*PFNGLGETUNIFORMLOCATIONPROC)(GLuint, const char*);
typedef GLint    (*PFNGLGETATTRIBLOCATIONPROC)(GLuint, const char*);
typedef void     (*PFNGLUNIFORM1IPROC)(GLint, GLint);
typedef void     (*PFNGLUNIFORM2IPROC)(GLint, GLint, GLint);
typedef void     (*PFNGLUNIFORM3IPROC)(GLint, GLint, GLint, GLint);
typedef void     (*PFNGLUNIFORM4IPROC)(GLint, GLint, GLint, GLint, GLint);
typedef void     (*PFNGLUNIFORM1FPROC)(GLint, GLfloat);
typedef void     (*PFNGLUNIFORM2FPROC)(GLint, GLfloat, GLfloat);
typedef void     (*PFNGLUNIFORM3FPROC)(GLint, GLfloat, GLfloat, GLfloat);
typedef void     (*PFNGLUNIFORM4FPROC)(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void     (*PFNGLUNIFORM1IVPROC)(GLint, GLsizei, const GLint*);
typedef void     (*PFNGLUNIFORM1FVPROC)(GLint, GLsizei, const GLfloat*);
typedef void     (*PFNGLUNIFORM2FVPROC)(GLint, GLsizei, const GLfloat*);
typedef void     (*PFNGLUNIFORM3FVPROC)(GLint, GLsizei, const GLfloat*);
typedef void     (*PFNGLUNIFORM4FVPROC)(GLint, GLsizei, const GLfloat*);
typedef void     (*PFNGLUNIFORMMATRIX2FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void     (*PFNGLUNIFORMMATRIX3FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void     (*PFNGLUNIFORMMATRIX4FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void     (*PFNGLUNIFORMMATRIX3X4FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void     (*PFNGLUNIFORMMATRIX4X3FVPROC)(GLint, GLsizei, GLboolean, const GLfloat*);
typedef void     (*PFNGLVERTEXATTRIB1FPROC)(GLuint, GLfloat);
typedef void     (*PFNGLVERTEXATTRIB2FPROC)(GLuint, GLfloat, GLfloat);
typedef void     (*PFNGLVERTEXATTRIB3FPROC)(GLuint, GLfloat, GLfloat, GLfloat);
typedef void     (*PFNGLVERTEXATTRIB4FPROC)(GLuint, GLfloat, GLfloat, GLfloat, GLfloat);
typedef void     (*PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void     (*PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint);
typedef void     (*PFNGLVERTEXATTRIBPOINTERPROC)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid*);
typedef void     (*PFNGLVERTEXATTRIBIPOINTERPROC)(GLuint, GLint, GLenum, GLsizei, const GLvoid*);
typedef void     (*PFNGLVERTEXATTRIBDIVISORPROC)(GLuint, GLuint);
typedef void     (*PFNGLBLENDFUNCSEPARATEPROC2)(GLenum, GLenum, GLenum, GLenum);
typedef void     (*PFNGLBLENDEQUATIONSEPARATEPROC)(GLenum, GLenum);
typedef void     (*PFNGLDRAWBUFFERSPROC)(GLsizei, const GLenum*);
typedef void     (*PFNGLSTENCILFUNCSEPARATEPROC)(GLenum, GLenum, GLint, GLuint);
typedef void     (*PFNGLSTENCILOPSEPARATEPROC)(GLenum, GLenum, GLenum, GLenum);
typedef void     (*PFNGLSTENCILMASKSEPARATEPROC)(GLenum, GLuint);
typedef void     (*PFNGLBINDATTRIBLOCATIONPROC)(GLuint, GLuint, const char*);
typedef void     (*PFNGLGETACTIVEATTRIBPROC)(GLuint, GLuint, GLsizei, GLsizei*, GLint*, GLenum*, char*);
typedef void     (*PFNGLGETACTIVEUNIFORMPROC)(GLuint, GLuint, GLsizei, GLsizei*, GLint*, GLenum*, char*);

/* --- OpenGL 3.0 --- */
typedef void     (*PFNGLGENVERTEXARRAYSPROC)(GLsizei, GLuint*);
typedef void     (*PFNGLDELETEVERTEXARRAYSPROC)(GLsizei, const GLuint*);
typedef void     (*PFNGLBINDVERTEXARRAYPROC)(GLuint);
typedef GLboolean(*PFNGLISVERTEXARRAYPROC)(GLuint);
typedef void     (*PFNGLGENFRAMEBUFFERSPROC)(GLsizei, GLuint*);
typedef void     (*PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei, const GLuint*);
typedef void     (*PFNGLBINDFRAMEBUFFERPROC)(GLenum, GLuint);
typedef GLenum   (*PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum);
typedef void     (*PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef void     (*PFNGLFRAMEBUFFERTEXTUREPROC)(GLenum, GLenum, GLuint, GLint);
typedef void     (*PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum, GLenum, GLenum, GLuint);
typedef void     (*PFNGLBLITFRAMEBUFFERPROC)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef void     (*PFNGLREADBUFFERPROC)(GLenum);
typedef void     (*PFNGLGENRENDERBUFFERSPROC)(GLsizei, GLuint*);
typedef void     (*PFNGLDELETERENDERBUFFERSPROC)(GLsizei, const GLuint*);
typedef void     (*PFNGLBINDRENDERBUFFERPROC)(GLenum, GLuint);
typedef void     (*PFNGLRENDERBUFFERSTORAGEPROC)(GLenum, GLenum, GLsizei, GLsizei);
typedef void     (*PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
typedef void     (*PFNGLGENERATEMIPMAPPROC)(GLenum);
typedef void     (*PFNGLBINDBUFFERBASEPROC)(GLenum, GLuint, GLuint);
typedef void     (*PFNGLBINDBUFFERRANGEPROC)(GLenum, GLuint, GLuint, long long, long long);
typedef void     (*PFNGLCOPYBUFFERSUBDATAPROC)(GLenum, GLenum, long long, long long, long long);
typedef GLvoid*  (*PFNGLMAPBUFFERRANGEPROC)(GLenum, long long, long long, GLbitfield);
typedef void     (*PFNGLGETINTEGER64VPROC)(GLenum, long long*);
typedef void     (*PFNGLGETBOOLEANI_VPROC)(GLenum, GLuint, GLboolean*);
typedef void     (*PFNGLGETINTEGERI_VPROC)(GLenum, GLuint, GLint*);
typedef void     (*PFNGLTRANSFORMFEEDBACKVARYINGSPROC)(GLuint, GLsizei, const char**, GLenum);
typedef void     (*PFNGLBEGINTRANSFORMFEEDBACKPROC)(GLenum);
typedef void     (*PFNGLENDTRANSFORMFEEDBACKPROC)(void);
typedef void     (*PFNGLCLEARBUFFERIVPROC)(GLenum, GLint, const GLint*);
typedef void     (*PFNGLCLEARBUFFERUIVPROC)(GLenum, GLint, const GLuint*);
typedef void     (*PFNGLCLEARBUFFERFVPROC)(GLenum, GLint, const GLfloat*);
typedef void     (*PFNGLCLEARBUFFERFIPROC)(GLenum, GLint, GLfloat, GLint);
typedef void     (*PFNGLGETUNIFORMBLOCKINDEXPROC_T)(GLuint, const char*);
typedef GLuint   (*PFNGLGETUNIFORMBLOCKINDEXPROC)(GLuint, const char*);
typedef void     (*PFNGLUNIFORMBLOCKBINDINGPROC)(GLuint, GLuint, GLuint);
typedef GLuint   (*PFNGLCREATESHADERNAMESDRIVERPROC)(GLuint);

/* --- OpenGL 3.2 --- */
typedef void*    (*PFNGLFENCESYNCPROC)(GLenum, GLbitfield);
typedef GLenum   (*PFNGLCLIENTWAITSYNCPROC)(void*, GLbitfield, unsigned long long);
typedef void     (*PFNGLWAITSYNCPROC)(void*, GLbitfield, unsigned long long);
typedef void     (*PFNGLDELETESYNCPROC)(void*);
typedef GLboolean(*PFNGLISSYNCPROC)(void*);
typedef void     (*PFNGLDRAWELEMENTSBASEVERTEXPROC)(GLenum, GLsizei, GLenum, const GLvoid*, GLint);
typedef void     (*PFNGLDRAWRANGEELEMSBASEVERTEXPROC)(GLenum, GLuint, GLuint, GLsizei, GLenum, const GLvoid*, GLint);
typedef void     (*PFNGLDRAWELEMENTSINSTANCEDBASEVERTEXPROC)(GLenum, GLsizei, GLenum, const GLvoid*, GLsizei, GLint);
typedef void     (*PFNGLPROVOKINGVERTEXPROC)(GLenum);

/* --- OpenGL 3.3 --- */
typedef void     (*PFNGLGENSAMPLERSPROC)(GLsizei, GLuint*);
typedef void     (*PFNGLDELETESAMPLERSPROC)(GLsizei, const GLuint*);
typedef void     (*PFNGLBINDSAMPLERPROC)(GLuint, GLuint);
typedef void     (*PFNGLSAMPLERPARAMETERIPROC)(GLuint, GLenum, GLint);
typedef void     (*PFNGLSAMPLERPARAMETERFPROC)(GLuint, GLenum, GLfloat);
typedef void     (*PFNGLSAMPLERPARAMETERIVPROC)(GLuint, GLenum, const GLint*);
typedef void     (*PFNGLSAMPLERPARAMETERFVPROC)(GLuint, GLenum, const GLfloat*);
typedef void     (*PFNGLGETQUERYOBJECTI64VPROC)(GLuint, GLenum, long long*);
typedef void     (*PFNGLGETQUERYOBJECTUI64VPROC)(GLuint, GLenum, unsigned long long*);
typedef void     (*PFNGLQUERYCOUNTERPROC)(GLuint, GLenum);

/* --- OpenGL 4.0 --- */
typedef void     (*PFNGLPATCHPARAMETERIPROC)(GLenum, GLint);
typedef void     (*PFNGLPATCHPARAMETERFVPROC)(GLenum, const GLfloat*);
typedef void     (*PFNGLDRAWARRAYSINDIRECTPROC)(GLenum, const GLvoid*);
typedef void     (*PFNGLDRAWELEMENTSINDIRECTPROC)(GLenum, GLenum, const GLvoid*);
typedef void     (*PFNGLMINSAMPLESHADINGPROC)(GLfloat);

/* --- OpenGL 4.2 --- */
typedef void     (*PFNGLDISPATCHCOMPUTEPROC)(GLuint, GLuint, GLuint);
typedef void     (*PFNGLDISPATCHCOMPUTEINDIRECTPROC)(long long);
typedef void     (*PFNGLMEMORYBARRIERPROC)(GLbitfield);
typedef void     (*PFNGLBINDIMAGETEXTUREPROC)(GLuint, GLuint, GLint, GLboolean, GLint, GLenum, GLenum);
typedef void     (*PFNGLTEXSTORAGE2DPROC)(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
typedef void     (*PFNGLTEXSTORAGE3DPROC)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLsizei);

/* --- OpenGL 4.3 --- */
typedef void     (*PFNGLDEBUGMESSAGECONTROLPROC)(GLenum, GLenum, GLenum, GLsizei, const GLuint*, GLboolean);
typedef void     (*PFNGLDEBUGMESSAGECALLBACKPROC)(void*, const void*);
typedef void     (*PFNGLDEBUGMESSAGEINSERTPROC)(GLenum, GLenum, GLuint, GLenum, GLsizei, const char*);
typedef void     (*PFNGLPUSHDEBUGGROUPPROC)(GLenum, GLuint, GLsizei, const char*);
typedef void     (*PFNGLPOPDEBUGGROUPPROC)(void);
typedef void     (*PFNGLOBJECTLABELPROC)(GLenum, GLuint, GLsizei, const char*);
typedef void     (*PFNGLSHADERSTORAGEBLOCKBINDINGPROC)(GLuint, GLuint, GLuint);
typedef void     (*PFNGLGETPROGRAMINTERFACEIVPROC)(GLuint, GLenum, GLenum, GLint*);
typedef GLuint   (*PFNGLGETPROGRAMRESOURCEINDEXPROC)(GLuint, GLenum, const char*);
typedef void     (*PFNGLGETPROGRAMRESOURCENAMEPROC)(GLuint, GLenum, GLuint, GLsizei, GLsizei*, char*);
typedef void     (*PFNGLGETPROGRAMRESOURCEIVPROC)(GLuint, GLenum, GLuint, GLsizei, const GLenum*, GLsizei, GLsizei*, GLint*);
typedef GLint    (*PFNGLGETPROGRAMRESOURCELOCATIONPROC)(GLuint, GLenum, const char*);
typedef void     (*PFNGLMULTIDRAWARRAYSINDIRECTPROC)(GLenum, const GLvoid*, GLsizei, GLsizei);
typedef void     (*PFNGLMULTIDRAWELEMENTSINDIRECTPROC)(GLenum, GLenum, const GLvoid*, GLsizei, GLsizei);
typedef void     (*PFNGLINVALIDATEFRAMEBUFFERPROC)(GLenum, GLsizei, const GLenum*);
typedef void     (*PFNGLDRAWARRAYSINSTANCEDBASEINSTANCEPROC)(GLenum, GLint, GLsizei, GLsizei, GLuint);

/* --- OpenGL 4.4 --- */
typedef void     (*PFNGLBUFFERSTORAGEPROC)(GLenum, long long, const GLvoid*, GLbitfield);
typedef void     (*PFNGLBINDBUFFERSBASEPROC)(GLenum, GLuint, GLsizei, const GLuint*);
typedef void     (*PFNGLBINDBUFFERSRANGEPROC)(GLenum, GLuint, GLsizei, const GLuint*, const long long*, const long long*);
typedef void     (*PFNGLBINDTEXTURESPROC)(GLsizei, GLsizei, const GLuint*);  /* note: first arg is 'first' (GLuint) */
typedef void     (*PFNGLBINDSAMPLERSPROC)(GLuint, GLsizei, const GLuint*);
typedef void     (*PFNGLBINDIMAGETEXTURESPROC)(GLuint, GLsizei, const GLuint*);
typedef void     (*PFNGLBINDVERTEXBUFFERSPROC)(GLuint, GLsizei, const GLuint*, const long long*, const GLsizei*);

/* --- OpenGL 4.5 DSA --- */
typedef void     (*PFNGLCREATETEXTURESPROC)(GLenum, GLsizei, GLuint*);
typedef void     (*PFNGLTEXTURESTORAGE2DPROC)(GLuint, GLsizei, GLenum, GLsizei, GLsizei);
typedef void     (*PFNGLTEXTURESTORAGE3DPROC)(GLuint, GLsizei, GLenum, GLsizei, GLsizei, GLsizei);
typedef void     (*PFNGLTEXTURESUBIMAGE2DPROC)(GLuint, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*);
typedef void     (*PFNGLTEXTURESUBIMAGE3DPROC)(GLuint, GLint, GLint, GLint, GLint, GLsizei, GLsizei, GLsizei, GLenum, GLenum, const GLvoid*);
typedef void     (*PFNGLTEXTUREPARAMETERIPROC)(GLuint, GLenum, GLint);
typedef void     (*PFNGLTEXTUREPARAMETERFPROC)(GLuint, GLenum, GLfloat);
typedef void     (*PFNGLTEXTUREPARAMETERIVPROC)(GLuint, GLenum, const GLint*);
typedef void     (*PFNGLTEXTUREPARAMETERFVPROC)(GLuint, GLenum, const GLfloat*);
typedef void     (*PFNGLGENERATETEXTUREMIPMAPPROC)(GLuint);
typedef void     (*PFNGLBINDTEXTUREUNITPROC)(GLuint, GLuint);
typedef void     (*PFNGLCREATEBUFFERSPROC)(GLsizei, GLuint*);
typedef void     (*PFNGLNAMEDBUFFERDATAPROC)(GLuint, long long, const GLvoid*, GLenum);
typedef void     (*PFNGLNAMEDBUFFERSUBDATAPROC)(GLuint, long long, long long, const GLvoid*);
typedef void     (*PFNGLNAMEDBUFFERSTORAGEPROC)(GLuint, long long, const GLvoid*, GLbitfield);
typedef GLvoid*  (*PFNGLMAPNAMEDBUFFERPROC)(GLuint, GLenum);
typedef GLvoid*  (*PFNGLMAPNAMEDBUFFERRANGEPROC)(GLuint, long long, long long, GLbitfield);
typedef GLboolean(*PFNGLUNMAPNAMEDBUFFERPROC)(GLuint);
typedef void     (*PFNGLCOPYNAMEDBUFFERSUBDATAPROC)(GLuint, GLuint, long long, long long, long long);
typedef void     (*PFNGLGETNAMEDBUFFERSUBDATAPROC)(GLuint, long long, long long, GLvoid*);
typedef void     (*PFNGLCREATEVERTEXARRAYSPROC)(GLsizei, GLuint*);
typedef void     (*PFNGLENABLEVERTEXARRAYATTRIBPROC)(GLuint, GLuint);
typedef void     (*PFNGLDISABLEVERTEXARRAYATTRIBPROC)(GLuint, GLuint);
typedef void     (*PFNGLVERTEXARRAYVERTEXBUFFERPROC)(GLuint, GLuint, GLuint, long long, GLsizei);
typedef void     (*PFNGLVERTEXARRAYELEMENTBUFFERPROC)(GLuint, GLuint);
typedef void     (*PFNGLVERTEXARRAYATTRIBFORMATPROC)(GLuint, GLuint, GLint, GLenum, GLboolean, GLuint);
typedef void     (*PFNGLVERTEXARRAYATTRIBIFORMATPROC)(GLuint, GLuint, GLint, GLenum, GLuint);
typedef void     (*PFNGLVERTEXARRAYATTRIBBINDINGPROC)(GLuint, GLuint, GLuint);
typedef void     (*PFNGLVERTEXARRAYBINDINGDIVISORPROC)(GLuint, GLuint, GLuint);
typedef void     (*PFNGLCREATEFRAMEBUFFERSPROC)(GLsizei, GLuint*);
typedef void     (*PFNGLNAMEDFRAMEBUFFERTEXTUREPROC)(GLuint, GLenum, GLuint, GLint);
typedef void     (*PFNGLNAMEDFRAMEBUFFERRENDERBUFFERPROC)(GLuint, GLenum, GLenum, GLuint);
typedef void     (*PFNGLNAMEDFRAMEBUFFERDRAWBUFFERSPROC)(GLuint, GLsizei, const GLenum*);
typedef void     (*PFNGLNAMEDFRAMEBUFFERREADBUFFERPROC)(GLuint, GLenum);
typedef GLenum   (*PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC)(GLuint, GLenum);
typedef void     (*PFNGLBLITNAMEDFRAMEBUFFERPROC)(GLuint, GLuint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
typedef void     (*PFNGLCREATERENDERBUFFERSPROC)(GLsizei, GLuint*);
typedef void     (*PFNGLNAMEDRENDERBUFFERSTORAGEPROC)(GLuint, GLenum, GLsizei, GLsizei);
typedef void     (*PFNGLNAMEDRENDERBUFFERSTORAGEMULTISAMPLEPROC)(GLuint, GLsizei, GLenum, GLsizei, GLsizei);
typedef void     (*PFNGLCLIPCONTROLPROC)(GLenum, GLenum);
typedef void     (*PFNGLCLEARNAMEDFRAMEBUFFERIVPROC)(GLuint, GLenum, GLint, const GLint*);
typedef void     (*PFNGLCLEARNAMEDFRAMEBUFFERFVPROC)(GLuint, GLenum, GLint, const GLfloat*);
typedef void     (*PFNGLCLEARNAMEDFRAMEBUFFERFIPROC)(GLuint, GLenum, GLint, GLfloat, GLint);
typedef void     (*PFNGLDRAWARRAYSINSTANCEDPROC)(GLenum, GLint, GLsizei, GLsizei);
typedef void     (*PFNGLDRAWELEMENTSINSTANCEDPROC)(GLenum, GLsizei, GLenum, const GLvoid*, GLsizei);
typedef void     (*PFNGLVERTEXBINDDIVISORPROC)(GLuint, GLuint);
typedef void     (*PFNGLVERTEXATTRIBBINDINGPROC)(GLuint, GLuint);
typedef void     (*PFNGLVERTEXATTRIBFORMATPROC)(GLuint, GLint, GLenum, GLboolean, GLuint);
typedef void     (*PFNGLVERTEXATTRIBIFORMATPROC)(GLuint, GLint, GLenum, GLuint);
typedef void     (*PFNGLBINDVERTEXBUFFERPROC)(GLuint, GLuint, long long, GLsizei);

/* =========================================================================
 * Extern declarations of global function pointer variables
 * ========================================================================= */
extern PFNGLDRAWRANGEELEMENTSPROC        pfnGlDrawRangeElements;
extern PFNGLTEXIMAGE3DPROC               pfnGlTexImage3D;
extern PFNGLTEXSUBIMAGE3DPROC            pfnGlTexSubImage3D;
extern PFNGLACTIVETEXTUREPROC            pfnGlActiveTexture;
extern PFNGLCOMPRESSEDTEXIMAGE2DPROC     pfnGlCompressedTexImage2D;
extern PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC  pfnGlCompressedTexSubImage2D;
extern PFNGLGETCOMPRESSEDTEXIMAGEPROC    pfnGlGetCompressedTexImage;
extern PFNGLBLENDFUNCSEPARATEPROC        pfnGlBlendFuncSeparate;
extern PFNGLBLENDCOLORPROC               pfnGlBlendColor;
extern PFNGLBLENDEQUATIONPROC            pfnGlBlendEquation;
extern PFNGLGENBUFFERSPROC               pfnGlGenBuffers;
extern PFNGLDELETEBUFFERSPROC            pfnGlDeleteBuffers;
extern PFNGLBINDBUFFERPROC               pfnGlBindBuffer;
extern PFNGLBUFFERDATAPROC               pfnGlBufferData;
extern PFNGLBUFFERSUBDATAPROC            pfnGlBufferSubData;
extern PFNGLMAPBUFFERPROC                pfnGlMapBuffer;
extern PFNGLUNMAPBUFFERPROC              pfnGlUnmapBuffer;
extern PFNGLGETBUFFERSUBDATAPROC         pfnGlGetBufferSubData;
extern PFNGLGETBUFFERPARAMETERIVPROC     pfnGlGetBufferParameteriv;
extern PFNGLGENQUERIESPROC               pfnGlGenQueries;
extern PFNGLDELETEQUERIESPROC            pfnGlDeleteQueries;
extern PFNGLBEGINQUERYPROC               pfnGlBeginQuery;
extern PFNGLENDQUERYPROC                 pfnGlEndQuery;
extern PFNGLGETQUERYOBJECTIVPROC         pfnGlGetQueryObjectiv;
extern PFNGLGETQUERYOBJECTUIVPROC        pfnGlGetQueryObjectuiv;
extern PFNGLCREATESHADERPROC             pfnGlCreateShader;
extern PFNGLSHADERSOURCEPROC             pfnGlShaderSource;
extern PFNGLCOMPILESHADERPROC            pfnGlCompileShader;
extern PFNGLGETSHADERIVPROC              pfnGlGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC         pfnGlGetShaderInfoLog;
extern PFNGLDELETESHADERPROC             pfnGlDeleteShader;
extern PFNGLCREATEPROGRAMPROC            pfnGlCreateProgram;
extern PFNGLATTACHSHADERPROC             pfnGlAttachShader;
extern PFNGLDETACHSHADERPROC             pfnGlDetachShader;
extern PFNGLLINKPROGRAMPROC              pfnGlLinkProgram;
extern PFNGLUSEPROGRAMPROC               pfnGlUseProgram;
extern PFNGLGETPROGRAMIVPROC             pfnGlGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC        pfnGlGetProgramInfoLog;
extern PFNGLDELETEPROGRAMPROC            pfnGlDeleteProgram;
extern PFNGLVALIDATEPROGRAMPROC          pfnGlValidateProgram;
extern PFNGLGETUNIFORMLOCATIONPROC       pfnGlGetUniformLocation;
extern PFNGLGETATTRIBLOCATIONPROC        pfnGlGetAttribLocation;
extern PFNGLGETUNIFORMBLOCKINDEXPROC     pfnGlGetUniformBlockIndex;
extern PFNGLUNIFORM1IPROC                pfnGlUniform1i;
extern PFNGLUNIFORM2IPROC                pfnGlUniform2i;
extern PFNGLUNIFORM3IPROC                pfnGlUniform3i;
extern PFNGLUNIFORM4IPROC                pfnGlUniform4i;
extern PFNGLUNIFORM1FPROC                pfnGlUniform1f;
extern PFNGLUNIFORM2FPROC                pfnGlUniform2f;
extern PFNGLUNIFORM3FPROC                pfnGlUniform3f;
extern PFNGLUNIFORM4FPROC                pfnGlUniform4f;
extern PFNGLUNIFORM1IVPROC               pfnGlUniform1iv;
extern PFNGLUNIFORM1FVPROC               pfnGlUniform1fv;
extern PFNGLUNIFORM2FVPROC               pfnGlUniform2fv;
extern PFNGLUNIFORM3FVPROC               pfnGlUniform3fv;
extern PFNGLUNIFORM4FVPROC               pfnGlUniform4fv;
extern PFNGLUNIFORMMATRIX2FVPROC         pfnGlUniformMatrix2fv;
extern PFNGLUNIFORMMATRIX3FVPROC         pfnGlUniformMatrix3fv;
extern PFNGLUNIFORMMATRIX4FVPROC         pfnGlUniformMatrix4fv;
extern PFNGLUNIFORMMATRIX3X4FVPROC       pfnGlUniformMatrix3x4fv;
extern PFNGLUNIFORMMATRIX4X3FVPROC       pfnGlUniformMatrix4x3fv;
extern PFNGLVERTEXATTRIB1FPROC           pfnGlVertexAttrib1f;
extern PFNGLVERTEXATTRIB2FPROC           pfnGlVertexAttrib2f;
extern PFNGLVERTEXATTRIB3FPROC           pfnGlVertexAttrib3f;
extern PFNGLVERTEXATTRIB4FPROC           pfnGlVertexAttrib4f;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC  pfnGlEnableVertexAttribArray;
extern PFNGLDISABLEVERTEXATTRIBARRAYPROC pfnGlDisableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC      pfnGlVertexAttribPointer;
extern PFNGLVERTEXATTRIBIPOINTERPROC     pfnGlVertexAttribIPointer;
extern PFNGLVERTEXATTRIBDIVISORPROC      pfnGlVertexAttribDivisor;
extern PFNGLBLENDEQUATIONSEPARATEPROC    pfnGlBlendEquationSeparate;
extern PFNGLDRAWBUFFERSPROC              pfnGlDrawBuffers;
extern PFNGLUNIFORMBLOCKBINDINGPROC      pfnGlUniformBlockBinding;
extern PFNGLGENVERTEXARRAYSPROC          pfnGlGenVertexArrays;
extern PFNGLDELETEVERTEXARRAYSPROC       pfnGlDeleteVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC          pfnGlBindVertexArray;
extern PFNGLISVERTEXARRAYPROC            pfnGlIsVertexArray;
extern PFNGLGENFRAMEBUFFERSPROC          pfnGlGenFramebuffers;
extern PFNGLDELETEFRAMEBUFFERSPROC       pfnGlDeleteFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC          pfnGlBindFramebuffer;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC   pfnGlCheckFramebufferStatus;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC     pfnGlFramebufferTexture2D;
extern PFNGLFRAMEBUFFERTEXTUREPROC       pfnGlFramebufferTexture;
extern PFNGLFRAMEBUFFERRENDERBUFFERPROC  pfnGlFramebufferRenderbuffer;
extern PFNGLBLITFRAMEBUFFERPROC          pfnGlBlitFramebuffer;
extern PFNGLREADBUFFERPROC               pfnGlReadBuffer;
extern PFNGLGENRENDERBUFFERSPROC         pfnGlGenRenderbuffers;
extern PFNGLDELETERENDERBUFFERSPROC      pfnGlDeleteRenderbuffers;
extern PFNGLBINDRENDERBUFFERPROC         pfnGlBindRenderbuffer;
extern PFNGLRENDERBUFFERSTORAGEPROC      pfnGlRenderbufferStorage;
extern PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC pfnGlRenderbufferStorageMultisample;
extern PFNGLGENERATEMIPMAPPROC           pfnGlGenerateMipmap;
extern PFNGLBINDBUFFERBASEPROC           pfnGlBindBufferBase;
extern PFNGLBINDBUFFERRANGEPROC          pfnGlBindBufferRange;
extern PFNGLCOPYBUFFERSUBDATAPROC        pfnGlCopyBufferSubData;
extern PFNGLMAPBUFFERRANGEPROC           pfnGlMapBufferRange;
extern PFNGLGETINTEGER64VPROC            pfnGlGetInteger64v;
extern PFNGLGETBOOLEANI_VPROC            pfnGlGetBooleani_v;
extern PFNGLGETINTEGERI_VPROC            pfnGlGetIntegeri_v;
extern PFNGLTRANSFORMFEEDBACKVARYINGSPROC pfnGlTransformFeedbackVaryings;
extern PFNGLBEGINTRANSFORMFEEDBACKPROC   pfnGlBeginTransformFeedback;
extern PFNGLENDTRANSFORMFEEDBACKPROC     pfnGlEndTransformFeedback;
extern PFNGLCLEARBUFFERIVPROC            pfnGlClearBufferiv;
extern PFNGLCLEARBUFFERUIVPROC           pfnGlClearBufferuiv;
extern PFNGLCLEARBUFFERFVPROC            pfnGlClearBufferfv;
extern PFNGLCLEARBUFFERFIPROC            pfnGlClearBufferfi;
extern PFNGLFENCESYNCPROC                pfnGlFenceSync;
extern PFNGLCLIENTWAITSYNCPROC           pfnGlClientWaitSync;
extern PFNGLWAITSYNCPROC                 pfnGlWaitSync;
extern PFNGLDELETESYNCPROC               pfnGlDeleteSync;
extern PFNGLISSYNCPROC                   pfnGlIsSync;
extern PFNGLDRAWELEMENTSBASEVERTEXPROC   pfnGlDrawElementsBaseVertex;
extern PFNGLGENSAMPLERSPROC              pfnGlGenSamplers;
extern PFNGLDELETESAMPLERSPROC           pfnGlDeleteSamplers;
extern PFNGLBINDSAMPLERPROC              pfnGlBindSampler;
extern PFNGLSAMPLERPARAMETERIPROC        pfnGlSamplerParameteri;
extern PFNGLSAMPLERPARAMETERFPROC        pfnGlSamplerParameterf;
extern PFNGLSAMPLERPARAMETERIVPROC       pfnGlSamplerParameteriv;
extern PFNGLSAMPLERPARAMETERFVPROC       pfnGlSamplerParameterfv;
extern PFNGLGETQUERYOBJECTI64VPROC       pfnGlGetQueryObjecti64v;
extern PFNGLGETQUERYOBJECTUI64VPROC      pfnGlGetQueryObjectui64v;
extern PFNGLQUERYCOUNTERPROC             pfnGlQueryCounter;
extern PFNGLPATCHPARAMETERIPROC          pfnGlPatchParameteri;
extern PFNGLPATCHPARAMETERFVPROC         pfnGlPatchParameterfv;
extern PFNGLDRAWARRAYSINDIRECTPROC       pfnGlDrawArraysIndirect;
extern PFNGLDRAWELEMENTSINDIRECTPROC     pfnGlDrawElementsIndirect;
extern PFNGLMINSAMPLESHADINGPROC         pfnGlMinSampleShading;
extern PFNGLDISPATCHCOMPUTEPROC          pfnGlDispatchCompute;
extern PFNGLDISPATCHCOMPUTEINDIRECTPROC  pfnGlDispatchComputeIndirect;
extern PFNGLMEMORYBARRIERPROC            pfnGlMemoryBarrier;
extern PFNGLBINDIMAGETEXTUREPROC         pfnGlBindImageTexture;
extern PFNGLTEXSTORAGE2DPROC             pfnGlTexStorage2D;
extern PFNGLTEXSTORAGE3DPROC             pfnGlTexStorage3D;
extern PFNGLDEBUGMESSAGECONTROLPROC      pfnGlDebugMessageControl;
extern PFNGLDEBUGMESSAGECALLBACKPROC     pfnGlDebugMessageCallback;
extern PFNGLDEBUGMESSAGEINSERTPROC       pfnGlDebugMessageInsert;
extern PFNGLPUSHDEBUGGROUPPROC           pfnGlPushDebugGroup;
extern PFNGLPOPDEBUGGROUPPROC            pfnGlPopDebugGroup;
extern PFNGLOBJECTLABELPROC              pfnGlObjectLabel;
extern PFNGLSHADERSTORAGEBLOCKBINDINGPROC pfnGlShaderStorageBlockBinding;
extern PFNGLGETPROGRAMINTERFACEIVPROC    pfnGlGetProgramInterfaceiv;
extern PFNGLGETPROGRAMRESOURCEINDEXPROC  pfnGlGetProgramResourceIndex;
extern PFNGLGETPROGRAMRESOURCENAMEPROC   pfnGlGetProgramResourceName;
extern PFNGLGETPROGRAMRESOURCEIVPROC     pfnGlGetProgramResourceiv;
extern PFNGLGETPROGRAMRESOURCELOCATIONPROC pfnGlGetProgramResourceLocation;
extern PFNGLMULTIDRAWARRAYSINDIRECTPROC  pfnGlMultiDrawArraysIndirect;
extern PFNGLMULTIDRAWELEMENTSINDIRECTPROC pfnGlMultiDrawElementsIndirect;
extern PFNGLBUFFERSTORAGEPROC            pfnGlBufferStorage;
extern PFNGLBINDBUFFERSBASEPROC          pfnGlBindBuffersBase;
extern PFNGLBINDBUFFERSRANGEPROC         pfnGlBindBuffersRange;
extern PFNGLBINDTEXTURESPROC             pfnGlBindTextures;
extern PFNGLBINDSAMPLERSPROC             pfnGlBindSamplers;
extern PFNGLBINDIMAGETEXTURESPROC        pfnGlBindImageTextures;
extern PFNGLBINDVERTEXBUFFERSPROC        pfnGlBindVertexBuffers;
extern PFNGLCREATETEXTURESPROC           pfnGlCreateTextures;
extern PFNGLTEXTURESTORAGE2DPROC         pfnGlTextureStorage2D;
extern PFNGLTEXTURESTORAGE3DPROC         pfnGlTextureStorage3D;
extern PFNGLTEXTURESUBIMAGE2DPROC        pfnGlTextureSubImage2D;
extern PFNGLTEXTURESUBIMAGE3DPROC        pfnGlTextureSubImage3D;
extern PFNGLTEXTUREPARAMETERIPROC        pfnGlTextureParameteri;
extern PFNGLTEXTUREPARAMETERFPROC        pfnGlTextureParameterf;
extern PFNGLTEXTUREPARAMETERIVPROC       pfnGlTextureParameteriv;
extern PFNGLTEXTUREPARAMETERFVPROC       pfnGlTextureParameterfv;
extern PFNGLGENERATETEXTUREMIPMAPPROC    pfnGlGenerateTextureMipmap;
extern PFNGLBINDTEXTUREUNITPROC          pfnGlBindTextureUnit;
extern PFNGLCREATEBUFFERSPROC            pfnGlCreateBuffers;
extern PFNGLNAMEDBUFFERDATAPROC          pfnGlNamedBufferData;
extern PFNGLNAMEDBUFFERSUBDATAPROC       pfnGlNamedBufferSubData;
extern PFNGLNAMEDBUFFERSTORAGEPROC       pfnGlNamedBufferStorage;
extern PFNGLMAPNAMEDBUFFERPROC           pfnGlMapNamedBuffer;
extern PFNGLMAPNAMEDBUFFERRANGEPROC      pfnGlMapNamedBufferRange;
extern PFNGLUNMAPNAMEDBUFFERPROC         pfnGlUnmapNamedBuffer;
extern PFNGLCOPYNAMEDBUFFERSUBDATAPROC   pfnGlCopyNamedBufferSubData;
extern PFNGLGETNAMEDBUFFERSUBDATAPROC    pfnGlGetNamedBufferSubData;
extern PFNGLCREATEVERTEXARRAYSPROC       pfnGlCreateVertexArrays;
extern PFNGLENABLEVERTEXARRAYATTRIBPROC  pfnGlEnableVertexArrayAttrib;
extern PFNGLDISABLEVERTEXARRAYATTRIBPROC pfnGlDisableVertexArrayAttrib;
extern PFNGLVERTEXARRAYVERTEXBUFFERPROC  pfnGlVertexArrayVertexBuffer;
extern PFNGLVERTEXARRAYELEMENTBUFFERPROC pfnGlVertexArrayElementBuffer;
extern PFNGLVERTEXARRAYATTRIBFORMATPROC  pfnGlVertexArrayAttribFormat;
extern PFNGLVERTEXARRAYATTRIBIFORMATPROC pfnGlVertexArrayAttribIFormat;
extern PFNGLVERTEXARRAYATTRIBBINDINGPROC pfnGlVertexArrayAttribBinding;
extern PFNGLVERTEXARRAYBINDINGDIVISORPROC pfnGlVertexArrayBindingDivisor;
extern PFNGLCREATEFRAMEBUFFERSPROC       pfnGlCreateFramebuffers;
extern PFNGLNAMEDFRAMEBUFFERTEXTUREPROC  pfnGlNamedFramebufferTexture;
extern PFNGLNAMEDFRAMEBUFFERRENDERBUFFERPROC pfnGlNamedFramebufferRenderbuffer;
extern PFNGLNAMEDFRAMEBUFFERDRAWBUFFERSPROC  pfnGlNamedFramebufferDrawBuffers;
extern PFNGLNAMEDFRAMEBUFFERREADBUFFERPROC   pfnGlNamedFramebufferReadBuffer;
extern PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC  pfnGlCheckNamedFramebufferStatus;
extern PFNGLBLITNAMEDFRAMEBUFFERPROC         pfnGlBlitNamedFramebuffer;
extern PFNGLCREATERENDERBUFFERSPROC          pfnGlCreateRenderbuffers;
extern PFNGLNAMEDRENDERBUFFERSTORAGEPROC     pfnGlNamedRenderbufferStorage;
extern PFNGLNAMEDRENDERBUFFERSTORAGEMULTISAMPLEPROC pfnGlNamedRenderbufferStorageMultisample;
extern PFNGLCLIPCONTROLPROC                  pfnGlClipControl;
extern PFNGLCLEARNAMEDFRAMEBUFFERIVPROC      pfnGlClearNamedFramebufferiv;
extern PFNGLCLEARNAMEDFRAMEBUFFERFVPROC      pfnGlClearNamedFramebufferfv;
extern PFNGLCLEARNAMEDFRAMEBUFFERFIPROC      pfnGlClearNamedFramebufferfi;
extern PFNGLDRAWARRAYSINSTANCEDPROC          pfnGlDrawArraysInstanced;
extern PFNGLDRAWELEMENTSINSTANCEDPROC        pfnGlDrawElementsInstanced;
extern PFNGLVERTEXBINDDIVISORPROC            pfnGlVertexBindingDivisor;
extern PFNGLVERTEXATTRIBBINDINGPROC          pfnGlVertexAttribBinding;
extern PFNGLVERTEXATTRIBFORMATPROC           pfnGlVertexAttribFormat;
extern PFNGLVERTEXATTRIBIFORMATPROC          pfnGlVertexAttribIFormat;
extern PFNGLBINDVERTEXBUFFERPROC             pfnGlBindVertexBuffer;

/* =========================================================================
 * Name-shadowing macros — code written against the standard API just works
 * ========================================================================= */
#define glDrawRangeElements        pfnGlDrawRangeElements
#define glTexImage3D               pfnGlTexImage3D
#define glTexSubImage3D            pfnGlTexSubImage3D
#define glActiveTexture            pfnGlActiveTexture
#define glCompressedTexImage2D     pfnGlCompressedTexImage2D
#define glCompressedTexSubImage2D  pfnGlCompressedTexSubImage2D
#define glGetCompressedTexImage    pfnGlGetCompressedTexImage
#define glBlendFuncSeparate        pfnGlBlendFuncSeparate
#define glBlendColor               pfnGlBlendColor
#define glBlendEquation            pfnGlBlendEquation
#define glGenBuffers               pfnGlGenBuffers
#define glDeleteBuffers            pfnGlDeleteBuffers
#define glBindBuffer               pfnGlBindBuffer
#define glBufferData               pfnGlBufferData
#define glBufferSubData            pfnGlBufferSubData
#define glMapBuffer                pfnGlMapBuffer
#define glUnmapBuffer              pfnGlUnmapBuffer
#define glGetBufferSubData         pfnGlGetBufferSubData
#define glGetBufferParameteriv     pfnGlGetBufferParameteriv
#define glGenQueries               pfnGlGenQueries
#define glDeleteQueries            pfnGlDeleteQueries
#define glBeginQuery               pfnGlBeginQuery
#define glEndQuery                 pfnGlEndQuery
#define glGetQueryObjectiv         pfnGlGetQueryObjectiv
#define glGetQueryObjectuiv        pfnGlGetQueryObjectuiv
#define glCreateShader             pfnGlCreateShader
#define glShaderSource             pfnGlShaderSource
#define glCompileShader            pfnGlCompileShader
#define glGetShaderiv              pfnGlGetShaderiv
#define glGetShaderInfoLog         pfnGlGetShaderInfoLog
#define glDeleteShader             pfnGlDeleteShader
#define glCreateProgram            pfnGlCreateProgram
#define glAttachShader             pfnGlAttachShader
#define glDetachShader             pfnGlDetachShader
#define glLinkProgram              pfnGlLinkProgram
#define glUseProgram               pfnGlUseProgram
#define glGetProgramiv             pfnGlGetProgramiv
#define glGetProgramInfoLog        pfnGlGetProgramInfoLog
#define glDeleteProgram            pfnGlDeleteProgram
#define glValidateProgram          pfnGlValidateProgram
#define glGetUniformLocation       pfnGlGetUniformLocation
#define glGetAttribLocation        pfnGlGetAttribLocation
#define glGetUniformBlockIndex     pfnGlGetUniformBlockIndex
#define glUniform1i                pfnGlUniform1i
#define glUniform2i                pfnGlUniform2i
#define glUniform3i                pfnGlUniform3i
#define glUniform4i                pfnGlUniform4i
#define glUniform1f                pfnGlUniform1f
#define glUniform2f                pfnGlUniform2f
#define glUniform3f                pfnGlUniform3f
#define glUniform4f                pfnGlUniform4f
#define glUniform1iv               pfnGlUniform1iv
#define glUniform1fv               pfnGlUniform1fv
#define glUniform2fv               pfnGlUniform2fv
#define glUniform3fv               pfnGlUniform3fv
#define glUniform4fv               pfnGlUniform4fv
#define glUniformMatrix2fv         pfnGlUniformMatrix2fv
#define glUniformMatrix3fv         pfnGlUniformMatrix3fv
#define glUniformMatrix4fv         pfnGlUniformMatrix4fv
#define glUniformMatrix3x4fv       pfnGlUniformMatrix3x4fv
#define glUniformMatrix4x3fv       pfnGlUniformMatrix4x3fv
#define glVertexAttrib1f           pfnGlVertexAttrib1f
#define glVertexAttrib2f           pfnGlVertexAttrib2f
#define glVertexAttrib3f           pfnGlVertexAttrib3f
#define glVertexAttrib4f           pfnGlVertexAttrib4f
#define glEnableVertexAttribArray  pfnGlEnableVertexAttribArray
#define glDisableVertexAttribArray pfnGlDisableVertexAttribArray
#define glVertexAttribPointer      pfnGlVertexAttribPointer
#define glVertexAttribIPointer     pfnGlVertexAttribIPointer
#define glVertexAttribDivisor      pfnGlVertexAttribDivisor
#define glBlendEquationSeparate    pfnGlBlendEquationSeparate
#define glDrawBuffers              pfnGlDrawBuffers
#define glUniformBlockBinding      pfnGlUniformBlockBinding
#define glGenVertexArrays          pfnGlGenVertexArrays
#define glDeleteVertexArrays       pfnGlDeleteVertexArrays
#define glBindVertexArray          pfnGlBindVertexArray
#define glIsVertexArray            pfnGlIsVertexArray
#define glGenFramebuffers          pfnGlGenFramebuffers
#define glDeleteFramebuffers       pfnGlDeleteFramebuffers
#define glBindFramebuffer          pfnGlBindFramebuffer
#define glCheckFramebufferStatus   pfnGlCheckFramebufferStatus
#define glFramebufferTexture2D     pfnGlFramebufferTexture2D
#define glFramebufferTexture       pfnGlFramebufferTexture
#define glFramebufferRenderbuffer  pfnGlFramebufferRenderbuffer
#define glBlitFramebuffer          pfnGlBlitFramebuffer
#define glReadBuffer               pfnGlReadBuffer
#define glGenRenderbuffers         pfnGlGenRenderbuffers
#define glDeleteRenderbuffers      pfnGlDeleteRenderbuffers
#define glBindRenderbuffer         pfnGlBindRenderbuffer
#define glRenderbufferStorage      pfnGlRenderbufferStorage
#define glRenderbufferStorageMultisample pfnGlRenderbufferStorageMultisample
#define glGenerateMipmap           pfnGlGenerateMipmap
#define glBindBufferBase           pfnGlBindBufferBase
#define glBindBufferRange          pfnGlBindBufferRange
#define glCopyBufferSubData        pfnGlCopyBufferSubData
#define glMapBufferRange           pfnGlMapBufferRange
#define glGetInteger64v            pfnGlGetInteger64v
#define glGetBooleani_v            pfnGlGetBooleani_v
#define glGetIntegeri_v            pfnGlGetIntegeri_v
#define glTransformFeedbackVaryings pfnGlTransformFeedbackVaryings
#define glBeginTransformFeedback   pfnGlBeginTransformFeedback
#define glEndTransformFeedback     pfnGlEndTransformFeedback
#define glClearBufferiv            pfnGlClearBufferiv
#define glClearBufferuiv           pfnGlClearBufferuiv
#define glClearBufferfv            pfnGlClearBufferfv
#define glClearBufferfi            pfnGlClearBufferfi
#define glFenceSync                pfnGlFenceSync
#define glClientWaitSync           pfnGlClientWaitSync
#define glWaitSync                 pfnGlWaitSync
#define glDeleteSync               pfnGlDeleteSync
#define glIsSync                   pfnGlIsSync
#define glDrawElementsBaseVertex   pfnGlDrawElementsBaseVertex
#define glGenSamplers              pfnGlGenSamplers
#define glDeleteSamplers           pfnGlDeleteSamplers
#define glBindSampler              pfnGlBindSampler
#define glSamplerParameteri        pfnGlSamplerParameteri
#define glSamplerParameterf        pfnGlSamplerParameterf
#define glSamplerParameteriv       pfnGlSamplerParameteriv
#define glSamplerParameterfv       pfnGlSamplerParameterfv
#define glGetQueryObjecti64v       pfnGlGetQueryObjecti64v
#define glGetQueryObjectui64v      pfnGlGetQueryObjectui64v
#define glQueryCounter             pfnGlQueryCounter
#define glPatchParameteri          pfnGlPatchParameteri
#define glPatchParameterfv         pfnGlPatchParameterfv
#define glDrawArraysIndirect       pfnGlDrawArraysIndirect
#define glDrawElementsIndirect     pfnGlDrawElementsIndirect
#define glMinSampleShading         pfnGlMinSampleShading
#define glDispatchCompute          pfnGlDispatchCompute
#define glDispatchComputeIndirect  pfnGlDispatchComputeIndirect
#define glMemoryBarrier            pfnGlMemoryBarrier
#define glBindImageTexture         pfnGlBindImageTexture
#define glTexStorage2D             pfnGlTexStorage2D
#define glTexStorage3D             pfnGlTexStorage3D
#define glDebugMessageControl      pfnGlDebugMessageControl
#define glDebugMessageCallback     pfnGlDebugMessageCallback
#define glDebugMessageInsert       pfnGlDebugMessageInsert
#define glPushDebugGroup           pfnGlPushDebugGroup
#define glPopDebugGroup            pfnGlPopDebugGroup
#define glObjectLabel              pfnGlObjectLabel
#define glShaderStorageBlockBinding pfnGlShaderStorageBlockBinding
#define glGetProgramInterfaceiv    pfnGlGetProgramInterfaceiv
#define glGetProgramResourceIndex  pfnGlGetProgramResourceIndex
#define glGetProgramResourceName   pfnGlGetProgramResourceName
#define glGetProgramResourceiv     pfnGlGetProgramResourceiv
#define glGetProgramResourceLocation pfnGlGetProgramResourceLocation
#define glMultiDrawArraysIndirect  pfnGlMultiDrawArraysIndirect
#define glMultiDrawElementsIndirect pfnGlMultiDrawElementsIndirect
#define glBufferStorage            pfnGlBufferStorage
#define glBindBuffersBase          pfnGlBindBuffersBase
#define glBindBuffersRange         pfnGlBindBuffersRange
#define glBindTextures             pfnGlBindTextures
#define glBindSamplers             pfnGlBindSamplers
#define glBindImageTextures        pfnGlBindImageTextures
#define glBindVertexBuffers        pfnGlBindVertexBuffers
#define glCreateTextures           pfnGlCreateTextures
#define glTextureStorage2D         pfnGlTextureStorage2D
#define glTextureStorage3D         pfnGlTextureStorage3D
#define glTextureSubImage2D        pfnGlTextureSubImage2D
#define glTextureSubImage3D        pfnGlTextureSubImage3D
#define glTextureParameteri        pfnGlTextureParameteri
#define glTextureParameterf        pfnGlTextureParameterf
#define glTextureParameteriv       pfnGlTextureParameteriv
#define glTextureParameterfv       pfnGlTextureParameterfv
#define glGenerateTextureMipmap    pfnGlGenerateTextureMipmap
#define glBindTextureUnit          pfnGlBindTextureUnit
#define glCreateBuffers            pfnGlCreateBuffers
#define glNamedBufferData          pfnGlNamedBufferData
#define glNamedBufferSubData       pfnGlNamedBufferSubData
#define glNamedBufferStorage       pfnGlNamedBufferStorage
#define glMapNamedBuffer           pfnGlMapNamedBuffer
#define glMapNamedBufferRange      pfnGlMapNamedBufferRange
#define glUnmapNamedBuffer         pfnGlUnmapNamedBuffer
#define glCopyNamedBufferSubData   pfnGlCopyNamedBufferSubData
#define glGetNamedBufferSubData    pfnGlGetNamedBufferSubData
#define glCreateVertexArrays       pfnGlCreateVertexArrays
#define glEnableVertexArrayAttrib  pfnGlEnableVertexArrayAttrib
#define glDisableVertexArrayAttrib pfnGlDisableVertexArrayAttrib
#define glVertexArrayVertexBuffer  pfnGlVertexArrayVertexBuffer
#define glVertexArrayElementBuffer pfnGlVertexArrayElementBuffer
#define glVertexArrayAttribFormat  pfnGlVertexArrayAttribFormat
#define glVertexArrayAttribIFormat pfnGlVertexArrayAttribIFormat
#define glVertexArrayAttribBinding pfnGlVertexArrayAttribBinding
#define glVertexArrayBindingDivisor pfnGlVertexArrayBindingDivisor
#define glCreateFramebuffers       pfnGlCreateFramebuffers
#define glNamedFramebufferTexture  pfnGlNamedFramebufferTexture
#define glNamedFramebufferRenderbuffer pfnGlNamedFramebufferRenderbuffer
#define glNamedFramebufferDrawBuffers pfnGlNamedFramebufferDrawBuffers
#define glNamedFramebufferReadBuffer  pfnGlNamedFramebufferReadBuffer
#define glCheckNamedFramebufferStatus pfnGlCheckNamedFramebufferStatus
#define glBlitNamedFramebuffer     pfnGlBlitNamedFramebuffer
#define glCreateRenderbuffers      pfnGlCreateRenderbuffers
#define glNamedRenderbufferStorage pfnGlNamedRenderbufferStorage
#define glNamedRenderbufferStorageMultisample pfnGlNamedRenderbufferStorageMultisample
#define glClipControl              pfnGlClipControl
#define glClearNamedFramebufferiv  pfnGlClearNamedFramebufferiv
#define glClearNamedFramebufferfv  pfnGlClearNamedFramebufferfv
#define glClearNamedFramebufferfi  pfnGlClearNamedFramebufferfi
#define glDrawArraysInstanced      pfnGlDrawArraysInstanced
#define glDrawElementsInstanced    pfnGlDrawElementsInstanced
#define glVertexBindingDivisor     pfnGlVertexBindingDivisor
#define glVertexAttribBinding      pfnGlVertexAttribBinding
#define glVertexAttribFormat       pfnGlVertexAttribFormat
#define glVertexAttribIFormat      pfnGlVertexAttribIFormat
#define glBindVertexBuffer         pfnGlBindVertexBuffer

/* Declaration of the loader — implemented in GL_FUNCS_IMPLEMENTATION section */
int gl_load_funcs(void);  /* returns number of functions that failed to load (0 = all OK) */

/* =========================================================================
 * GL_FUNCS_IMPLEMENTATION — define in exactly one .c file before this include
 * ========================================================================= */
#ifdef GL_FUNCS_IMPLEMENTATION

/* Platform proc-address fetcher */
#ifdef _WIN32
#include "wgl.h"
static void *gl_get_proc(const char *name) {
    void *p = (void*)wglGetProcAddress(name);
    /* wglGetProcAddress may return NULL or 1/2/3/-1 for core 1.1 functions */
    if (!p || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1) {
        /* Fall back to GetProcAddress on opengl32.dll for 1.0/1.1 symbols */
        HMODULE h = (HMODULE)0;  /* caller must ensure opengl32 is loaded */
        (void)h;
        p = 0;
    }
    return p;
}
#else
#include "glx.h"
static void *gl_get_proc(const char *name) {
    return (void*)glXGetProcAddressARB((const unsigned char*)name);
}
#endif

/* Global function pointer definitions */
PFNGLDRAWRANGEELEMENTSPROC        pfnGlDrawRangeElements;
PFNGLTEXIMAGE3DPROC               pfnGlTexImage3D;
PFNGLTEXSUBIMAGE3DPROC            pfnGlTexSubImage3D;
PFNGLACTIVETEXTUREPROC            pfnGlActiveTexture;
PFNGLCOMPRESSEDTEXIMAGE2DPROC     pfnGlCompressedTexImage2D;
PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC  pfnGlCompressedTexSubImage2D;
PFNGLGETCOMPRESSEDTEXIMAGEPROC    pfnGlGetCompressedTexImage;
PFNGLBLENDFUNCSEPARATEPROC        pfnGlBlendFuncSeparate;
PFNGLBLENDCOLORPROC               pfnGlBlendColor;
PFNGLBLENDEQUATIONPROC            pfnGlBlendEquation;
PFNGLGENBUFFERSPROC               pfnGlGenBuffers;
PFNGLDELETEBUFFERSPROC            pfnGlDeleteBuffers;
PFNGLBINDBUFFERPROC               pfnGlBindBuffer;
PFNGLBUFFERDATAPROC               pfnGlBufferData;
PFNGLBUFFERSUBDATAPROC            pfnGlBufferSubData;
PFNGLMAPBUFFERPROC                pfnGlMapBuffer;
PFNGLUNMAPBUFFERPROC              pfnGlUnmapBuffer;
PFNGLGETBUFFERSUBDATAPROC         pfnGlGetBufferSubData;
PFNGLGETBUFFERPARAMETERIVPROC     pfnGlGetBufferParameteriv;
PFNGLGENQUERIESPROC               pfnGlGenQueries;
PFNGLDELETEQUERIESPROC            pfnGlDeleteQueries;
PFNGLBEGINQUERYPROC               pfnGlBeginQuery;
PFNGLENDQUERYPROC                 pfnGlEndQuery;
PFNGLGETQUERYOBJECTIVPROC         pfnGlGetQueryObjectiv;
PFNGLGETQUERYOBJECTUIVPROC        pfnGlGetQueryObjectuiv;
PFNGLCREATESHADERPROC             pfnGlCreateShader;
PFNGLSHADERSOURCEPROC             pfnGlShaderSource;
PFNGLCOMPILESHADERPROC            pfnGlCompileShader;
PFNGLGETSHADERIVPROC              pfnGlGetShaderiv;
PFNGLGETSHADERINFOLOGPROC         pfnGlGetShaderInfoLog;
PFNGLDELETESHADERPROC             pfnGlDeleteShader;
PFNGLCREATEPROGRAMPROC            pfnGlCreateProgram;
PFNGLATTACHSHADERPROC             pfnGlAttachShader;
PFNGLDETACHSHADERPROC             pfnGlDetachShader;
PFNGLLINKPROGRAMPROC              pfnGlLinkProgram;
PFNGLUSEPROGRAMPROC               pfnGlUseProgram;
PFNGLGETPROGRAMIVPROC             pfnGlGetProgramiv;
PFNGLGETPROGRAMINFOLOGPROC        pfnGlGetProgramInfoLog;
PFNGLDELETEPROGRAMPROC            pfnGlDeleteProgram;
PFNGLVALIDATEPROGRAMPROC          pfnGlValidateProgram;
PFNGLGETUNIFORMLOCATIONPROC       pfnGlGetUniformLocation;
PFNGLGETATTRIBLOCATIONPROC        pfnGlGetAttribLocation;
PFNGLGETUNIFORMBLOCKINDEXPROC     pfnGlGetUniformBlockIndex;
PFNGLUNIFORM1IPROC                pfnGlUniform1i;
PFNGLUNIFORM2IPROC                pfnGlUniform2i;
PFNGLUNIFORM3IPROC                pfnGlUniform3i;
PFNGLUNIFORM4IPROC                pfnGlUniform4i;
PFNGLUNIFORM1FPROC                pfnGlUniform1f;
PFNGLUNIFORM2FPROC                pfnGlUniform2f;
PFNGLUNIFORM3FPROC                pfnGlUniform3f;
PFNGLUNIFORM4FPROC                pfnGlUniform4f;
PFNGLUNIFORM1IVPROC               pfnGlUniform1iv;
PFNGLUNIFORM1FVPROC               pfnGlUniform1fv;
PFNGLUNIFORM2FVPROC               pfnGlUniform2fv;
PFNGLUNIFORM3FVPROC               pfnGlUniform3fv;
PFNGLUNIFORM4FVPROC               pfnGlUniform4fv;
PFNGLUNIFORMMATRIX2FVPROC         pfnGlUniformMatrix2fv;
PFNGLUNIFORMMATRIX3FVPROC         pfnGlUniformMatrix3fv;
PFNGLUNIFORMMATRIX4FVPROC         pfnGlUniformMatrix4fv;
PFNGLUNIFORMMATRIX3X4FVPROC       pfnGlUniformMatrix3x4fv;
PFNGLUNIFORMMATRIX4X3FVPROC       pfnGlUniformMatrix4x3fv;
PFNGLVERTEXATTRIB1FPROC           pfnGlVertexAttrib1f;
PFNGLVERTEXATTRIB2FPROC           pfnGlVertexAttrib2f;
PFNGLVERTEXATTRIB3FPROC           pfnGlVertexAttrib3f;
PFNGLVERTEXATTRIB4FPROC           pfnGlVertexAttrib4f;
PFNGLENABLEVERTEXATTRIBARRAYPROC  pfnGlEnableVertexAttribArray;
PFNGLDISABLEVERTEXATTRIBARRAYPROC pfnGlDisableVertexAttribArray;
PFNGLVERTEXATTRIBPOINTERPROC      pfnGlVertexAttribPointer;
PFNGLVERTEXATTRIBIPOINTERPROC     pfnGlVertexAttribIPointer;
PFNGLVERTEXATTRIBDIVISORPROC      pfnGlVertexAttribDivisor;
PFNGLBLENDEQUATIONSEPARATEPROC    pfnGlBlendEquationSeparate;
PFNGLDRAWBUFFERSPROC              pfnGlDrawBuffers;
PFNGLUNIFORMBLOCKBINDINGPROC      pfnGlUniformBlockBinding;
PFNGLGENVERTEXARRAYSPROC          pfnGlGenVertexArrays;
PFNGLDELETEVERTEXARRAYSPROC       pfnGlDeleteVertexArrays;
PFNGLBINDVERTEXARRAYPROC          pfnGlBindVertexArray;
PFNGLISVERTEXARRAYPROC            pfnGlIsVertexArray;
PFNGLGENFRAMEBUFFERSPROC          pfnGlGenFramebuffers;
PFNGLDELETEFRAMEBUFFERSPROC       pfnGlDeleteFramebuffers;
PFNGLBINDFRAMEBUFFERPROC          pfnGlBindFramebuffer;
PFNGLCHECKFRAMEBUFFERSTATUSPROC   pfnGlCheckFramebufferStatus;
PFNGLFRAMEBUFFERTEXTURE2DPROC     pfnGlFramebufferTexture2D;
PFNGLFRAMEBUFFERTEXTUREPROC       pfnGlFramebufferTexture;
PFNGLFRAMEBUFFERRENDERBUFFERPROC  pfnGlFramebufferRenderbuffer;
PFNGLBLITFRAMEBUFFERPROC          pfnGlBlitFramebuffer;
PFNGLREADBUFFERPROC               pfnGlReadBuffer;
PFNGLGENRENDERBUFFERSPROC         pfnGlGenRenderbuffers;
PFNGLDELETERENDERBUFFERSPROC      pfnGlDeleteRenderbuffers;
PFNGLBINDRENDERBUFFERPROC         pfnGlBindRenderbuffer;
PFNGLRENDERBUFFERSTORAGEPROC      pfnGlRenderbufferStorage;
PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC pfnGlRenderbufferStorageMultisample;
PFNGLGENERATEMIPMAPPROC           pfnGlGenerateMipmap;
PFNGLBINDBUFFERBASEPROC           pfnGlBindBufferBase;
PFNGLBINDBUFFERRANGEPROC          pfnGlBindBufferRange;
PFNGLCOPYBUFFERSUBDATAPROC        pfnGlCopyBufferSubData;
PFNGLMAPBUFFERRANGEPROC           pfnGlMapBufferRange;
PFNGLGETINTEGER64VPROC            pfnGlGetInteger64v;
PFNGLGETBOOLEANI_VPROC            pfnGlGetBooleani_v;
PFNGLGETINTEGERI_VPROC            pfnGlGetIntegeri_v;
PFNGLTRANSFORMFEEDBACKVARYINGSPROC pfnGlTransformFeedbackVaryings;
PFNGLBEGINTRANSFORMFEEDBACKPROC   pfnGlBeginTransformFeedback;
PFNGLENDTRANSFORMFEEDBACKPROC     pfnGlEndTransformFeedback;
PFNGLCLEARBUFFERIVPROC            pfnGlClearBufferiv;
PFNGLCLEARBUFFERUIVPROC           pfnGlClearBufferuiv;
PFNGLCLEARBUFFERFVPROC            pfnGlClearBufferfv;
PFNGLCLEARBUFFERFIPROC            pfnGlClearBufferfi;
PFNGLFENCESYNCPROC                pfnGlFenceSync;
PFNGLCLIENTWAITSYNCPROC           pfnGlClientWaitSync;
PFNGLWAITSYNCPROC                 pfnGlWaitSync;
PFNGLDELETESYNCPROC               pfnGlDeleteSync;
PFNGLISSYNCPROC                   pfnGlIsSync;
PFNGLDRAWELEMENTSBASEVERTEXPROC   pfnGlDrawElementsBaseVertex;
PFNGLGENSAMPLERSPROC              pfnGlGenSamplers;
PFNGLDELETESAMPLERSPROC           pfnGlDeleteSamplers;
PFNGLBINDSAMPLERPROC              pfnGlBindSampler;
PFNGLSAMPLERPARAMETERIPROC        pfnGlSamplerParameteri;
PFNGLSAMPLERPARAMETERFPROC        pfnGlSamplerParameterf;
PFNGLSAMPLERPARAMETERIVPROC       pfnGlSamplerParameteriv;
PFNGLSAMPLERPARAMETERFVPROC       pfnGlSamplerParameterfv;
PFNGLGETQUERYOBJECTI64VPROC       pfnGlGetQueryObjecti64v;
PFNGLGETQUERYOBJECTUI64VPROC      pfnGlGetQueryObjectui64v;
PFNGLQUERYCOUNTERPROC             pfnGlQueryCounter;
PFNGLPATCHPARAMETERIPROC          pfnGlPatchParameteri;
PFNGLPATCHPARAMETERFVPROC         pfnGlPatchParameterfv;
PFNGLDRAWARRAYSINDIRECTPROC       pfnGlDrawArraysIndirect;
PFNGLDRAWELEMENTSINDIRECTPROC     pfnGlDrawElementsIndirect;
PFNGLMINSAMPLESHADINGPROC         pfnGlMinSampleShading;
PFNGLDISPATCHCOMPUTEPROC          pfnGlDispatchCompute;
PFNGLDISPATCHCOMPUTEINDIRECTPROC  pfnGlDispatchComputeIndirect;
PFNGLMEMORYBARRIERPROC            pfnGlMemoryBarrier;
PFNGLBINDIMAGETEXTUREPROC         pfnGlBindImageTexture;
PFNGLTEXSTORAGE2DPROC             pfnGlTexStorage2D;
PFNGLTEXSTORAGE3DPROC             pfnGlTexStorage3D;
PFNGLDEBUGMESSAGECONTROLPROC      pfnGlDebugMessageControl;
PFNGLDEBUGMESSAGECALLBACKPROC     pfnGlDebugMessageCallback;
PFNGLDEBUGMESSAGEINSERTPROC       pfnGlDebugMessageInsert;
PFNGLPUSHDEBUGGROUPPROC           pfnGlPushDebugGroup;
PFNGLPOPDEBUGGROUPPROC            pfnGlPopDebugGroup;
PFNGLOBJECTLABELPROC              pfnGlObjectLabel;
PFNGLSHADERSTORAGEBLOCKBINDINGPROC pfnGlShaderStorageBlockBinding;
PFNGLGETPROGRAMINTERFACEIVPROC    pfnGlGetProgramInterfaceiv;
PFNGLGETPROGRAMRESOURCEINDEXPROC  pfnGlGetProgramResourceIndex;
PFNGLGETPROGRAMRESOURCENAMEPROC   pfnGlGetProgramResourceName;
PFNGLGETPROGRAMRESOURCEIVPROC     pfnGlGetProgramResourceiv;
PFNGLGETPROGRAMRESOURCELOCATIONPROC pfnGlGetProgramResourceLocation;
PFNGLMULTIDRAWARRAYSINDIRECTPROC  pfnGlMultiDrawArraysIndirect;
PFNGLMULTIDRAWELEMENTSINDIRECTPROC pfnGlMultiDrawElementsIndirect;
PFNGLBUFFERSTORAGEPROC            pfnGlBufferStorage;
PFNGLBINDBUFFERSBASEPROC          pfnGlBindBuffersBase;
PFNGLBINDBUFFERSRANGEPROC         pfnGlBindBuffersRange;
PFNGLBINDTEXTURESPROC             pfnGlBindTextures;
PFNGLBINDSAMPLERSPROC             pfnGlBindSamplers;
PFNGLBINDIMAGETEXTURESPROC        pfnGlBindImageTextures;
PFNGLBINDVERTEXBUFFERSPROC        pfnGlBindVertexBuffers;
PFNGLCREATETEXTURESPROC           pfnGlCreateTextures;
PFNGLTEXTURESTORAGE2DPROC         pfnGlTextureStorage2D;
PFNGLTEXTURESTORAGE3DPROC         pfnGlTextureStorage3D;
PFNGLTEXTURESUBIMAGE2DPROC        pfnGlTextureSubImage2D;
PFNGLTEXTURESUBIMAGE3DPROC        pfnGlTextureSubImage3D;
PFNGLTEXTUREPARAMETERIPROC        pfnGlTextureParameteri;
PFNGLTEXTUREPARAMETERFPROC        pfnGlTextureParameterf;
PFNGLTEXTUREPARAMETERIVPROC       pfnGlTextureParameteriv;
PFNGLTEXTUREPARAMETERFVPROC       pfnGlTextureParameterfv;
PFNGLGENERATETEXTUREMIPMAPPROC    pfnGlGenerateTextureMipmap;
PFNGLBINDTEXTUREUNITPROC          pfnGlBindTextureUnit;
PFNGLCREATEBUFFERSPROC            pfnGlCreateBuffers;
PFNGLNAMEDBUFFERDATAPROC          pfnGlNamedBufferData;
PFNGLNAMEDBUFFERSUBDATAPROC       pfnGlNamedBufferSubData;
PFNGLNAMEDBUFFERSTORAGEPROC       pfnGlNamedBufferStorage;
PFNGLMAPNAMEDBUFFERPROC           pfnGlMapNamedBuffer;
PFNGLMAPNAMEDBUFFERRANGEPROC      pfnGlMapNamedBufferRange;
PFNGLUNMAPNAMEDBUFFERPROC         pfnGlUnmapNamedBuffer;
PFNGLCOPYNAMEDBUFFERSUBDATAPROC   pfnGlCopyNamedBufferSubData;
PFNGLGETNAMEDBUFFERSUBDATAPROC    pfnGlGetNamedBufferSubData;
PFNGLCREATEVERTEXARRAYSPROC       pfnGlCreateVertexArrays;
PFNGLENABLEVERTEXARRAYATTRIBPROC  pfnGlEnableVertexArrayAttrib;
PFNGLDISABLEVERTEXARRAYATTRIBPROC pfnGlDisableVertexArrayAttrib;
PFNGLVERTEXARRAYVERTEXBUFFERPROC  pfnGlVertexArrayVertexBuffer;
PFNGLVERTEXARRAYELEMENTBUFFERPROC pfnGlVertexArrayElementBuffer;
PFNGLVERTEXARRAYATTRIBFORMATPROC  pfnGlVertexArrayAttribFormat;
PFNGLVERTEXARRAYATTRIBIFORMATPROC pfnGlVertexArrayAttribIFormat;
PFNGLVERTEXARRAYATTRIBBINDINGPROC pfnGlVertexArrayAttribBinding;
PFNGLVERTEXARRAYBINDINGDIVISORPROC pfnGlVertexArrayBindingDivisor;
PFNGLCREATEFRAMEBUFFERSPROC       pfnGlCreateFramebuffers;
PFNGLNAMEDFRAMEBUFFERTEXTUREPROC  pfnGlNamedFramebufferTexture;
PFNGLNAMEDFRAMEBUFFERRENDERBUFFERPROC pfnGlNamedFramebufferRenderbuffer;
PFNGLNAMEDFRAMEBUFFERDRAWBUFFERSPROC  pfnGlNamedFramebufferDrawBuffers;
PFNGLNAMEDFRAMEBUFFERREADBUFFERPROC   pfnGlNamedFramebufferReadBuffer;
PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC  pfnGlCheckNamedFramebufferStatus;
PFNGLBLITNAMEDFRAMEBUFFERPROC         pfnGlBlitNamedFramebuffer;
PFNGLCREATERENDERBUFFERSPROC          pfnGlCreateRenderbuffers;
PFNGLNAMEDRENDERBUFFERSTORAGEPROC     pfnGlNamedRenderbufferStorage;
PFNGLNAMEDRENDERBUFFERSTORAGEMULTISAMPLEPROC pfnGlNamedRenderbufferStorageMultisample;
PFNGLCLIPCONTROLPROC                  pfnGlClipControl;
PFNGLCLEARNAMEDFRAMEBUFFERIVPROC      pfnGlClearNamedFramebufferiv;
PFNGLCLEARNAMEDFRAMEBUFFERFVPROC      pfnGlClearNamedFramebufferfv;
PFNGLCLEARNAMEDFRAMEBUFFERFIPROC      pfnGlClearNamedFramebufferfi;
PFNGLDRAWARRAYSINSTANCEDPROC          pfnGlDrawArraysInstanced;
PFNGLDRAWELEMENTSINSTANCEDPROC        pfnGlDrawElementsInstanced;
PFNGLVERTEXBINDDIVISORPROC            pfnGlVertexBindingDivisor;
PFNGLVERTEXATTRIBBINDINGPROC          pfnGlVertexAttribBinding;
PFNGLVERTEXATTRIBFORMATPROC           pfnGlVertexAttribFormat;
PFNGLVERTEXATTRIBIFORMATPROC          pfnGlVertexAttribIFormat;
PFNGLBINDVERTEXBUFFERPROC             pfnGlBindVertexBuffer;

/* The loader function: calls gl_get_proc for every entry point */
int gl_load_funcs(void) {
    int miss = 0;
#define GL_LOAD(T, ptr, name) ptr = (T)gl_get_proc(name); if (!ptr) miss++
    GL_LOAD(PFNGLDRAWRANGEELEMENTSPROC,        pfnGlDrawRangeElements,         "glDrawRangeElements");
    GL_LOAD(PFNGLTEXIMAGE3DPROC,               pfnGlTexImage3D,                "glTexImage3D");
    GL_LOAD(PFNGLTEXSUBIMAGE3DPROC,            pfnGlTexSubImage3D,             "glTexSubImage3D");
    GL_LOAD(PFNGLACTIVETEXTUREPROC,            pfnGlActiveTexture,             "glActiveTexture");
    GL_LOAD(PFNGLCOMPRESSEDTEXIMAGE2DPROC,     pfnGlCompressedTexImage2D,      "glCompressedTexImage2D");
    GL_LOAD(PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC,  pfnGlCompressedTexSubImage2D,   "glCompressedTexSubImage2D");
    GL_LOAD(PFNGLGETCOMPRESSEDTEXIMAGEPROC,    pfnGlGetCompressedTexImage,     "glGetCompressedTexImage");
    GL_LOAD(PFNGLBLENDFUNCSEPARATEPROC,        pfnGlBlendFuncSeparate,         "glBlendFuncSeparate");
    GL_LOAD(PFNGLBLENDCOLORPROC,               pfnGlBlendColor,                "glBlendColor");
    GL_LOAD(PFNGLBLENDEQUATIONPROC,            pfnGlBlendEquation,             "glBlendEquation");
    GL_LOAD(PFNGLGENBUFFERSPROC,               pfnGlGenBuffers,                "glGenBuffers");
    GL_LOAD(PFNGLDELETEBUFFERSPROC,            pfnGlDeleteBuffers,             "glDeleteBuffers");
    GL_LOAD(PFNGLBINDBUFFERPROC,               pfnGlBindBuffer,                "glBindBuffer");
    GL_LOAD(PFNGLBUFFERDATAPROC,               pfnGlBufferData,                "glBufferData");
    GL_LOAD(PFNGLBUFFERSUBDATAPROC,            pfnGlBufferSubData,             "glBufferSubData");
    GL_LOAD(PFNGLMAPBUFFERPROC,                pfnGlMapBuffer,                 "glMapBuffer");
    GL_LOAD(PFNGLUNMAPBUFFERPROC,              pfnGlUnmapBuffer,               "glUnmapBuffer");
    GL_LOAD(PFNGLGETBUFFERSUBDATAPROC,         pfnGlGetBufferSubData,          "glGetBufferSubData");
    GL_LOAD(PFNGLGETBUFFERPARAMETERIVPROC,     pfnGlGetBufferParameteriv,      "glGetBufferParameteriv");
    GL_LOAD(PFNGLGENQUERIESPROC,               pfnGlGenQueries,                "glGenQueries");
    GL_LOAD(PFNGLDELETEQUERIESPROC,            pfnGlDeleteQueries,             "glDeleteQueries");
    GL_LOAD(PFNGLBEGINQUERYPROC,               pfnGlBeginQuery,                "glBeginQuery");
    GL_LOAD(PFNGLENDQUERYPROC,                 pfnGlEndQuery,                  "glEndQuery");
    GL_LOAD(PFNGLGETQUERYOBJECTIVPROC,         pfnGlGetQueryObjectiv,          "glGetQueryObjectiv");
    GL_LOAD(PFNGLGETQUERYOBJECTUIVPROC,        pfnGlGetQueryObjectuiv,         "glGetQueryObjectuiv");
    GL_LOAD(PFNGLCREATESHADERPROC,             pfnGlCreateShader,              "glCreateShader");
    GL_LOAD(PFNGLSHADERSOURCEPROC,             pfnGlShaderSource,              "glShaderSource");
    GL_LOAD(PFNGLCOMPILESHADERPROC,            pfnGlCompileShader,             "glCompileShader");
    GL_LOAD(PFNGLGETSHADERIVPROC,              pfnGlGetShaderiv,               "glGetShaderiv");
    GL_LOAD(PFNGLGETSHADERINFOLOGPROC,         pfnGlGetShaderInfoLog,          "glGetShaderInfoLog");
    GL_LOAD(PFNGLDELETESHADERPROC,             pfnGlDeleteShader,              "glDeleteShader");
    GL_LOAD(PFNGLCREATEPROGRAMPROC,            pfnGlCreateProgram,             "glCreateProgram");
    GL_LOAD(PFNGLATTACHSHADERPROC,             pfnGlAttachShader,              "glAttachShader");
    GL_LOAD(PFNGLDETACHSHADERPROC,             pfnGlDetachShader,              "glDetachShader");
    GL_LOAD(PFNGLLINKPROGRAMPROC,              pfnGlLinkProgram,               "glLinkProgram");
    GL_LOAD(PFNGLUSEPROGRAMPROC,               pfnGlUseProgram,                "glUseProgram");
    GL_LOAD(PFNGLGETPROGRAMIVPROC,             pfnGlGetProgramiv,              "glGetProgramiv");
    GL_LOAD(PFNGLGETPROGRAMINFOLOGPROC,        pfnGlGetProgramInfoLog,         "glGetProgramInfoLog");
    GL_LOAD(PFNGLDELETEPROGRAMPROC,            pfnGlDeleteProgram,             "glDeleteProgram");
    GL_LOAD(PFNGLVALIDATEPROGRAMPROC,          pfnGlValidateProgram,           "glValidateProgram");
    GL_LOAD(PFNGLGETUNIFORMLOCATIONPROC,       pfnGlGetUniformLocation,        "glGetUniformLocation");
    GL_LOAD(PFNGLGETATTRIBLOCATIONPROC,        pfnGlGetAttribLocation,         "glGetAttribLocation");
    GL_LOAD(PFNGLGETUNIFORMBLOCKINDEXPROC,     pfnGlGetUniformBlockIndex,      "glGetUniformBlockIndex");
    GL_LOAD(PFNGLUNIFORM1IPROC,                pfnGlUniform1i,                 "glUniform1i");
    GL_LOAD(PFNGLUNIFORM2IPROC,                pfnGlUniform2i,                 "glUniform2i");
    GL_LOAD(PFNGLUNIFORM3IPROC,                pfnGlUniform3i,                 "glUniform3i");
    GL_LOAD(PFNGLUNIFORM4IPROC,                pfnGlUniform4i,                 "glUniform4i");
    GL_LOAD(PFNGLUNIFORM1FPROC,                pfnGlUniform1f,                 "glUniform1f");
    GL_LOAD(PFNGLUNIFORM2FPROC,                pfnGlUniform2f,                 "glUniform2f");
    GL_LOAD(PFNGLUNIFORM3FPROC,                pfnGlUniform3f,                 "glUniform3f");
    GL_LOAD(PFNGLUNIFORM4FPROC,                pfnGlUniform4f,                 "glUniform4f");
    GL_LOAD(PFNGLUNIFORM1IVPROC,               pfnGlUniform1iv,                "glUniform1iv");
    GL_LOAD(PFNGLUNIFORM1FVPROC,               pfnGlUniform1fv,                "glUniform1fv");
    GL_LOAD(PFNGLUNIFORM2FVPROC,               pfnGlUniform2fv,                "glUniform2fv");
    GL_LOAD(PFNGLUNIFORM3FVPROC,               pfnGlUniform3fv,                "glUniform3fv");
    GL_LOAD(PFNGLUNIFORM4FVPROC,               pfnGlUniform4fv,                "glUniform4fv");
    GL_LOAD(PFNGLUNIFORMMATRIX2FVPROC,         pfnGlUniformMatrix2fv,          "glUniformMatrix2fv");
    GL_LOAD(PFNGLUNIFORMMATRIX3FVPROC,         pfnGlUniformMatrix3fv,          "glUniformMatrix3fv");
    GL_LOAD(PFNGLUNIFORMMATRIX4FVPROC,         pfnGlUniformMatrix4fv,          "glUniformMatrix4fv");
    GL_LOAD(PFNGLUNIFORMMATRIX3X4FVPROC,       pfnGlUniformMatrix3x4fv,        "glUniformMatrix3x4fv");
    GL_LOAD(PFNGLUNIFORMMATRIX4X3FVPROC,       pfnGlUniformMatrix4x3fv,        "glUniformMatrix4x3fv");
    GL_LOAD(PFNGLVERTEXATTRIB1FPROC,           pfnGlVertexAttrib1f,            "glVertexAttrib1f");
    GL_LOAD(PFNGLVERTEXATTRIB2FPROC,           pfnGlVertexAttrib2f,            "glVertexAttrib2f");
    GL_LOAD(PFNGLVERTEXATTRIB3FPROC,           pfnGlVertexAttrib3f,            "glVertexAttrib3f");
    GL_LOAD(PFNGLVERTEXATTRIB4FPROC,           pfnGlVertexAttrib4f,            "glVertexAttrib4f");
    GL_LOAD(PFNGLENABLEVERTEXATTRIBARRAYPROC,  pfnGlEnableVertexAttribArray,   "glEnableVertexAttribArray");
    GL_LOAD(PFNGLDISABLEVERTEXATTRIBARRAYPROC, pfnGlDisableVertexAttribArray,  "glDisableVertexAttribArray");
    GL_LOAD(PFNGLVERTEXATTRIBPOINTERPROC,      pfnGlVertexAttribPointer,       "glVertexAttribPointer");
    GL_LOAD(PFNGLVERTEXATTRIBIPOINTERPROC,     pfnGlVertexAttribIPointer,      "glVertexAttribIPointer");
    GL_LOAD(PFNGLVERTEXATTRIBDIVISORPROC,      pfnGlVertexAttribDivisor,       "glVertexAttribDivisor");
    GL_LOAD(PFNGLBLENDEQUATIONSEPARATEPROC,    pfnGlBlendEquationSeparate,     "glBlendEquationSeparate");
    GL_LOAD(PFNGLDRAWBUFFERSPROC,              pfnGlDrawBuffers,               "glDrawBuffers");
    GL_LOAD(PFNGLUNIFORMBLOCKBINDINGPROC,      pfnGlUniformBlockBinding,       "glUniformBlockBinding");
    GL_LOAD(PFNGLGENVERTEXARRAYSPROC,          pfnGlGenVertexArrays,           "glGenVertexArrays");
    GL_LOAD(PFNGLDELETEVERTEXARRAYSPROC,       pfnGlDeleteVertexArrays,        "glDeleteVertexArrays");
    GL_LOAD(PFNGLBINDVERTEXARRAYPROC,          pfnGlBindVertexArray,           "glBindVertexArray");
    GL_LOAD(PFNGLISVERTEXARRAYPROC,            pfnGlIsVertexArray,             "glIsVertexArray");
    GL_LOAD(PFNGLGENFRAMEBUFFERSPROC,          pfnGlGenFramebuffers,           "glGenFramebuffers");
    GL_LOAD(PFNGLDELETEFRAMEBUFFERSPROC,       pfnGlDeleteFramebuffers,        "glDeleteFramebuffers");
    GL_LOAD(PFNGLBINDFRAMEBUFFERPROC,          pfnGlBindFramebuffer,           "glBindFramebuffer");
    GL_LOAD(PFNGLCHECKFRAMEBUFFERSTATUSPROC,   pfnGlCheckFramebufferStatus,    "glCheckFramebufferStatus");
    GL_LOAD(PFNGLFRAMEBUFFERTEXTURE2DPROC,     pfnGlFramebufferTexture2D,      "glFramebufferTexture2D");
    GL_LOAD(PFNGLFRAMEBUFFERTEXTUREPROC,       pfnGlFramebufferTexture,        "glFramebufferTexture");
    GL_LOAD(PFNGLFRAMEBUFFERRENDERBUFFERPROC,  pfnGlFramebufferRenderbuffer,   "glFramebufferRenderbuffer");
    GL_LOAD(PFNGLBLITFRAMEBUFFERPROC,          pfnGlBlitFramebuffer,           "glBlitFramebuffer");
    GL_LOAD(PFNGLREADBUFFERPROC,               pfnGlReadBuffer,                "glReadBuffer");
    GL_LOAD(PFNGLGENRENDERBUFFERSPROC,         pfnGlGenRenderbuffers,          "glGenRenderbuffers");
    GL_LOAD(PFNGLDELETERENDERBUFFERSPROC,      pfnGlDeleteRenderbuffers,       "glDeleteRenderbuffers");
    GL_LOAD(PFNGLBINDRENDERBUFFERPROC,         pfnGlBindRenderbuffer,          "glBindRenderbuffer");
    GL_LOAD(PFNGLRENDERBUFFERSTORAGEPROC,      pfnGlRenderbufferStorage,       "glRenderbufferStorage");
    GL_LOAD(PFNGLRENDERBUFFERSTORAGEMULTISAMPLEPROC, pfnGlRenderbufferStorageMultisample, "glRenderbufferStorageMultisample");
    GL_LOAD(PFNGLGENERATEMIPMAPPROC,           pfnGlGenerateMipmap,            "glGenerateMipmap");
    GL_LOAD(PFNGLBINDBUFFERBASEPROC,           pfnGlBindBufferBase,            "glBindBufferBase");
    GL_LOAD(PFNGLBINDBUFFERRANGEPROC,          pfnGlBindBufferRange,           "glBindBufferRange");
    GL_LOAD(PFNGLCOPYBUFFERSUBDATAPROC,        pfnGlCopyBufferSubData,         "glCopyBufferSubData");
    GL_LOAD(PFNGLMAPBUFFERRANGEPROC,           pfnGlMapBufferRange,            "glMapBufferRange");
    GL_LOAD(PFNGLGETINTEGER64VPROC,            pfnGlGetInteger64v,             "glGetInteger64v");
    GL_LOAD(PFNGLGETBOOLEANI_VPROC,            pfnGlGetBooleani_v,             "glGetBooleani_v");
    GL_LOAD(PFNGLGETINTEGERI_VPROC,            pfnGlGetIntegeri_v,             "glGetIntegeri_v");
    GL_LOAD(PFNGLTRANSFORMFEEDBACKVARYINGSPROC, pfnGlTransformFeedbackVaryings, "glTransformFeedbackVaryings");
    GL_LOAD(PFNGLBEGINTRANSFORMFEEDBACKPROC,   pfnGlBeginTransformFeedback,    "glBeginTransformFeedback");
    GL_LOAD(PFNGLENDTRANSFORMFEEDBACKPROC,     pfnGlEndTransformFeedback,      "glEndTransformFeedback");
    GL_LOAD(PFNGLCLEARBUFFERIVPROC,            pfnGlClearBufferiv,             "glClearBufferiv");
    GL_LOAD(PFNGLCLEARBUFFERUIVPROC,           pfnGlClearBufferuiv,            "glClearBufferuiv");
    GL_LOAD(PFNGLCLEARBUFFERFVPROC,            pfnGlClearBufferfv,             "glClearBufferfv");
    GL_LOAD(PFNGLCLEARBUFFERFIPROC,            pfnGlClearBufferfi,             "glClearBufferfi");
    GL_LOAD(PFNGLFENCESYNCPROC,                pfnGlFenceSync,                 "glFenceSync");
    GL_LOAD(PFNGLCLIENTWAITSYNCPROC,           pfnGlClientWaitSync,            "glClientWaitSync");
    GL_LOAD(PFNGLWAITSYNCPROC,                 pfnGlWaitSync,                  "glWaitSync");
    GL_LOAD(PFNGLDELETESYNCPROC,               pfnGlDeleteSync,                "glDeleteSync");
    GL_LOAD(PFNGLISSYNCPROC,                   pfnGlIsSync,                    "glIsSync");
    GL_LOAD(PFNGLDRAWELEMENTSBASEVERTEXPROC,   pfnGlDrawElementsBaseVertex,    "glDrawElementsBaseVertex");
    GL_LOAD(PFNGLGENSAMPLERSPROC,              pfnGlGenSamplers,               "glGenSamplers");
    GL_LOAD(PFNGLDELETESAMPLERSPROC,           pfnGlDeleteSamplers,            "glDeleteSamplers");
    GL_LOAD(PFNGLBINDSAMPLERPROC,              pfnGlBindSampler,               "glBindSampler");
    GL_LOAD(PFNGLSAMPLERPARAMETERIPROC,        pfnGlSamplerParameteri,         "glSamplerParameteri");
    GL_LOAD(PFNGLSAMPLERPARAMETERFPROC,        pfnGlSamplerParameterf,         "glSamplerParameterf");
    GL_LOAD(PFNGLSAMPLERPARAMETERIVPROC,       pfnGlSamplerParameteriv,        "glSamplerParameteriv");
    GL_LOAD(PFNGLSAMPLERPARAMETERFVPROC,       pfnGlSamplerParameterfv,        "glSamplerParameterfv");
    GL_LOAD(PFNGLGETQUERYOBJECTI64VPROC,       pfnGlGetQueryObjecti64v,        "glGetQueryObjecti64v");
    GL_LOAD(PFNGLGETQUERYOBJECTUI64VPROC,      pfnGlGetQueryObjectui64v,       "glGetQueryObjectui64v");
    GL_LOAD(PFNGLQUERYCOUNTERPROC,             pfnGlQueryCounter,              "glQueryCounter");
    GL_LOAD(PFNGLPATCHPARAMETERIPROC,          pfnGlPatchParameteri,           "glPatchParameteri");
    GL_LOAD(PFNGLPATCHPARAMETERFVPROC,         pfnGlPatchParameterfv,          "glPatchParameterfv");
    GL_LOAD(PFNGLDRAWARRAYSINDIRECTPROC,       pfnGlDrawArraysIndirect,        "glDrawArraysIndirect");
    GL_LOAD(PFNGLDRAWELEMENTSINDIRECTPROC,     pfnGlDrawElementsIndirect,      "glDrawElementsIndirect");
    GL_LOAD(PFNGLMINSAMPLESHADINGPROC,         pfnGlMinSampleShading,          "glMinSampleShading");
    GL_LOAD(PFNGLDISPATCHCOMPUTEPROC,          pfnGlDispatchCompute,           "glDispatchCompute");
    GL_LOAD(PFNGLDISPATCHCOMPUTEINDIRECTPROC,  pfnGlDispatchComputeIndirect,   "glDispatchComputeIndirect");
    GL_LOAD(PFNGLMEMORYBARRIERPROC,            pfnGlMemoryBarrier,             "glMemoryBarrier");
    GL_LOAD(PFNGLBINDIMAGETEXTUREPROC,         pfnGlBindImageTexture,          "glBindImageTexture");
    GL_LOAD(PFNGLTEXSTORAGE2DPROC,             pfnGlTexStorage2D,              "glTexStorage2D");
    GL_LOAD(PFNGLTEXSTORAGE3DPROC,             pfnGlTexStorage3D,              "glTexStorage3D");
    GL_LOAD(PFNGLDEBUGMESSAGECONTROLPROC,      pfnGlDebugMessageControl,       "glDebugMessageControl");
    GL_LOAD(PFNGLDEBUGMESSAGECALLBACKPROC,     pfnGlDebugMessageCallback,      "glDebugMessageCallback");
    GL_LOAD(PFNGLDEBUGMESSAGEINSERTPROC,       pfnGlDebugMessageInsert,        "glDebugMessageInsert");
    GL_LOAD(PFNGLPUSHDEBUGGROUPPROC,           pfnGlPushDebugGroup,            "glPushDebugGroup");
    GL_LOAD(PFNGLPOPDEBUGGROUPPROC,            pfnGlPopDebugGroup,             "glPopDebugGroup");
    GL_LOAD(PFNGLOBJECTLABELPROC,              pfnGlObjectLabel,               "glObjectLabel");
    GL_LOAD(PFNGLSHADERSTORAGEBLOCKBINDINGPROC, pfnGlShaderStorageBlockBinding, "glShaderStorageBlockBinding");
    GL_LOAD(PFNGLGETPROGRAMINTERFACEIVPROC,    pfnGlGetProgramInterfaceiv,     "glGetProgramInterfaceiv");
    GL_LOAD(PFNGLGETPROGRAMRESOURCEINDEXPROC,  pfnGlGetProgramResourceIndex,   "glGetProgramResourceIndex");
    GL_LOAD(PFNGLGETPROGRAMRESOURCENAMEPROC,   pfnGlGetProgramResourceName,    "glGetProgramResourceName");
    GL_LOAD(PFNGLGETPROGRAMRESOURCEIVPROC,     pfnGlGetProgramResourceiv,      "glGetProgramResourceiv");
    GL_LOAD(PFNGLGETPROGRAMRESOURCELOCATIONPROC, pfnGlGetProgramResourceLocation, "glGetProgramResourceLocation");
    GL_LOAD(PFNGLMULTIDRAWARRAYSINDIRECTPROC,  pfnGlMultiDrawArraysIndirect,   "glMultiDrawArraysIndirect");
    GL_LOAD(PFNGLMULTIDRAWELEMENTSINDIRECTPROC, pfnGlMultiDrawElementsIndirect, "glMultiDrawElementsIndirect");
    GL_LOAD(PFNGLBUFFERSTORAGEPROC,            pfnGlBufferStorage,             "glBufferStorage");
    GL_LOAD(PFNGLBINDBUFFERSBASEPROC,          pfnGlBindBuffersBase,           "glBindBuffersBase");
    GL_LOAD(PFNGLBINDBUFFERSRANGEPROC,         pfnGlBindBuffersRange,          "glBindBuffersRange");
    GL_LOAD(PFNGLBINDTEXTURESPROC,             pfnGlBindTextures,              "glBindTextures");
    GL_LOAD(PFNGLBINDSAMPLERSPROC,             pfnGlBindSamplers,              "glBindSamplers");
    GL_LOAD(PFNGLBINDIMAGETEXTURESPROC,        pfnGlBindImageTextures,         "glBindImageTextures");
    GL_LOAD(PFNGLBINDVERTEXBUFFERSPROC,        pfnGlBindVertexBuffers,         "glBindVertexBuffers");
    GL_LOAD(PFNGLCREATETEXTURESPROC,           pfnGlCreateTextures,            "glCreateTextures");
    GL_LOAD(PFNGLTEXTURESTORAGE2DPROC,         pfnGlTextureStorage2D,          "glTextureStorage2D");
    GL_LOAD(PFNGLTEXTURESTORAGE3DPROC,         pfnGlTextureStorage3D,          "glTextureStorage3D");
    GL_LOAD(PFNGLTEXTURESUBIMAGE2DPROC,        pfnGlTextureSubImage2D,         "glTextureSubImage2D");
    GL_LOAD(PFNGLTEXTURESUBIMAGE3DPROC,        pfnGlTextureSubImage3D,         "glTextureSubImage3D");
    GL_LOAD(PFNGLTEXTUREPARAMETERIPROC,        pfnGlTextureParameteri,         "glTextureParameteri");
    GL_LOAD(PFNGLTEXTUREPARAMETERFPROC,        pfnGlTextureParameterf,         "glTextureParameterf");
    GL_LOAD(PFNGLTEXTUREPARAMETERIVPROC,       pfnGlTextureParameteriv,        "glTextureParameteriv");
    GL_LOAD(PFNGLTEXTUREPARAMETERFVPROC,       pfnGlTextureParameterfv,        "glTextureParameterfv");
    GL_LOAD(PFNGLGENERATETEXTUREMIPMAPPROC,    pfnGlGenerateTextureMipmap,     "glGenerateTextureMipmap");
    GL_LOAD(PFNGLBINDTEXTUREUNITPROC,          pfnGlBindTextureUnit,           "glBindTextureUnit");
    GL_LOAD(PFNGLCREATEBUFFERSPROC,            pfnGlCreateBuffers,             "glCreateBuffers");
    GL_LOAD(PFNGLNAMEDBUFFERDATAPROC,          pfnGlNamedBufferData,           "glNamedBufferData");
    GL_LOAD(PFNGLNAMEDBUFFERSUBDATAPROC,       pfnGlNamedBufferSubData,        "glNamedBufferSubData");
    GL_LOAD(PFNGLNAMEDBUFFERSTORAGEPROC,       pfnGlNamedBufferStorage,        "glNamedBufferStorage");
    GL_LOAD(PFNGLMAPNAMEDBUFFERPROC,           pfnGlMapNamedBuffer,            "glMapNamedBuffer");
    GL_LOAD(PFNGLMAPNAMEDBUFFERRANGEPROC,      pfnGlMapNamedBufferRange,       "glMapNamedBufferRange");
    GL_LOAD(PFNGLUNMAPNAMEDBUFFERPROC,         pfnGlUnmapNamedBuffer,          "glUnmapNamedBuffer");
    GL_LOAD(PFNGLCOPYNAMEDBUFFERSUBDATAPROC,   pfnGlCopyNamedBufferSubData,    "glCopyNamedBufferSubData");
    GL_LOAD(PFNGLGETNAMEDBUFFERSUBDATAPROC,    pfnGlGetNamedBufferSubData,     "glGetNamedBufferSubData");
    GL_LOAD(PFNGLCREATEVERTEXARRAYSPROC,       pfnGlCreateVertexArrays,        "glCreateVertexArrays");
    GL_LOAD(PFNGLENABLEVERTEXARRAYATTRIBPROC,  pfnGlEnableVertexArrayAttrib,   "glEnableVertexArrayAttrib");
    GL_LOAD(PFNGLDISABLEVERTEXARRAYATTRIBPROC, pfnGlDisableVertexArrayAttrib,  "glDisableVertexArrayAttrib");
    GL_LOAD(PFNGLVERTEXARRAYVERTEXBUFFERPROC,  pfnGlVertexArrayVertexBuffer,   "glVertexArrayVertexBuffer");
    GL_LOAD(PFNGLVERTEXARRAYELEMENTBUFFERPROC, pfnGlVertexArrayElementBuffer,  "glVertexArrayElementBuffer");
    GL_LOAD(PFNGLVERTEXARRAYATTRIBFORMATPROC,  pfnGlVertexArrayAttribFormat,   "glVertexArrayAttribFormat");
    GL_LOAD(PFNGLVERTEXARRAYATTRIBIFORMATPROC, pfnGlVertexArrayAttribIFormat,  "glVertexArrayAttribIFormat");
    GL_LOAD(PFNGLVERTEXARRAYATTRIBBINDINGPROC, pfnGlVertexArrayAttribBinding,  "glVertexArrayAttribBinding");
    GL_LOAD(PFNGLVERTEXARRAYBINDINGDIVISORPROC, pfnGlVertexArrayBindingDivisor, "glVertexArrayBindingDivisor");
    GL_LOAD(PFNGLCREATEFRAMEBUFFERSPROC,       pfnGlCreateFramebuffers,        "glCreateFramebuffers");
    GL_LOAD(PFNGLNAMEDFRAMEBUFFERTEXTUREPROC,  pfnGlNamedFramebufferTexture,   "glNamedFramebufferTexture");
    GL_LOAD(PFNGLNAMEDFRAMEBUFFERRENDERBUFFERPROC, pfnGlNamedFramebufferRenderbuffer, "glNamedFramebufferRenderbuffer");
    GL_LOAD(PFNGLNAMEDFRAMEBUFFERDRAWBUFFERSPROC,  pfnGlNamedFramebufferDrawBuffers,  "glNamedFramebufferDrawBuffers");
    GL_LOAD(PFNGLNAMEDFRAMEBUFFERREADBUFFERPROC,   pfnGlNamedFramebufferReadBuffer,   "glNamedFramebufferReadBuffer");
    GL_LOAD(PFNGLCHECKNAMEDFRAMEBUFFERSTATUSPROC,  pfnGlCheckNamedFramebufferStatus,  "glCheckNamedFramebufferStatus");
    GL_LOAD(PFNGLBLITNAMEDFRAMEBUFFERPROC,         pfnGlBlitNamedFramebuffer,         "glBlitNamedFramebuffer");
    GL_LOAD(PFNGLCREATERENDERBUFFERSPROC,          pfnGlCreateRenderbuffers,          "glCreateRenderbuffers");
    GL_LOAD(PFNGLNAMEDRENDERBUFFERSTORAGEPROC,     pfnGlNamedRenderbufferStorage,     "glNamedRenderbufferStorage");
    GL_LOAD(PFNGLNAMEDRENDERBUFFERSTORAGEMULTISAMPLEPROC, pfnGlNamedRenderbufferStorageMultisample, "glNamedRenderbufferStorageMultisample");
    GL_LOAD(PFNGLCLIPCONTROLPROC,                  pfnGlClipControl,                  "glClipControl");
    GL_LOAD(PFNGLCLEARNAMEDFRAMEBUFFERIVPROC,      pfnGlClearNamedFramebufferiv,      "glClearNamedFramebufferiv");
    GL_LOAD(PFNGLCLEARNAMEDFRAMEBUFFERFVPROC,      pfnGlClearNamedFramebufferfv,      "glClearNamedFramebufferfv");
    GL_LOAD(PFNGLCLEARNAMEDFRAMEBUFFERFIPROC,      pfnGlClearNamedFramebufferfi,      "glClearNamedFramebufferfi");
    GL_LOAD(PFNGLDRAWARRAYSINSTANCEDPROC,          pfnGlDrawArraysInstanced,          "glDrawArraysInstanced");
    GL_LOAD(PFNGLDRAWELEMENTSINSTANCEDPROC,        pfnGlDrawElementsInstanced,        "glDrawElementsInstanced");
    GL_LOAD(PFNGLVERTEXBINDDIVISORPROC,            pfnGlVertexBindingDivisor,         "glVertexBindingDivisor");
    GL_LOAD(PFNGLVERTEXATTRIBBINDINGPROC,          pfnGlVertexAttribBinding,          "glVertexAttribBinding");
    GL_LOAD(PFNGLVERTEXATTRIBFORMATPROC,           pfnGlVertexAttribFormat,           "glVertexAttribFormat");
    GL_LOAD(PFNGLVERTEXATTRIBIFORMATPROC,          pfnGlVertexAttribIFormat,          "glVertexAttribIFormat");
    GL_LOAD(PFNGLBINDVERTEXBUFFERPROC,             pfnGlBindVertexBuffer,             "glBindVertexBuffer");
#undef GL_LOAD
    return miss;
}

#endif /* GL_FUNCS_IMPLEMENTATION */
#endif /* GL_FUNCS_H */
