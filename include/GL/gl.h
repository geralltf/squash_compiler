#ifndef GL_H
#define GL_H

typedef unsigned int   GLenum;
typedef unsigned int   GLbitfield;
typedef unsigned int   GLuint;
typedef int            GLint;
typedef int            GLsizei;
typedef unsigned char  GLboolean;
typedef signed char    GLbyte;
typedef short          GLshort;
typedef unsigned char  GLubyte;
typedef unsigned short GLushort;
typedef unsigned long  GLulong;
typedef float          GLfloat;
typedef float          GLclampf;
typedef double         GLdouble;
typedef double         GLclampd;
typedef void           GLvoid;

/* Boolean */
#define GL_FALSE   0
#define GL_TRUE    1

/* Primitives */
#define GL_POINTS         0x0000
#define GL_LINES          0x0001
#define GL_LINE_LOOP      0x0002
#define GL_LINE_STRIP     0x0003
#define GL_TRIANGLES      0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN   0x0006
#define GL_QUADS          0x0007
#define GL_QUAD_STRIP     0x0008
#define GL_POLYGON        0x0009

/* Clear bits */
#define GL_COLOR_BUFFER_BIT   0x00004000
#define GL_DEPTH_BUFFER_BIT   0x00000100
#define GL_STENCIL_BUFFER_BIT 0x00000400

/* Enable caps */
#define GL_DEPTH_TEST   0x0B71
#define GL_BLEND        0x0BE2
#define GL_CULL_FACE    0x0B44
#define GL_LIGHTING     0x0B50
#define GL_TEXTURE_2D   0x0DE1
#define GL_COLOR_MATERIAL 0x0B57

/* Matrix modes */
#define GL_MODELVIEW  0x1700
#define GL_PROJECTION 0x1701
#define GL_TEXTURE    0x1702

/* Blend factors */
#define GL_ZERO                0
#define GL_ONE                 1
#define GL_SRC_COLOR           0x0300
#define GL_SRC_ALPHA           0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA           0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305

/* Texture parameters */
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_LINEAR             0x2601
#define GL_NEAREST            0x2600
#define GL_REPEAT             0x2901
#define GL_CLAMP              0x2900
#define GL_TEXTURE_WRAP_S     0x2802
#define GL_TEXTURE_WRAP_T     0x2803

/* Pixel formats */
#define GL_RGB    0x1907
#define GL_RGBA   0x1908
#define GL_ALPHA  0x1906
#define GL_LUMINANCE 0x1909

/* Data types */
#define GL_UNSIGNED_BYTE  0x1401
#define GL_BYTE           0x1400
#define GL_SHORT          0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT            0x1404
#define GL_UNSIGNED_INT   0x1405
#define GL_FLOAT          0x1406
#define GL_DOUBLE         0x140A

/* Errors */
#define GL_NO_ERROR          0
#define GL_INVALID_ENUM      0x0500
#define GL_INVALID_VALUE     0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY     0x0505

/* Immediate mode */
void glBegin   (GLenum mode);
void glEnd     (void);
void glVertex2f(GLfloat x, GLfloat y);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glVertex2i(GLint x, GLint y);
void glVertex3i(GLint x, GLint y, GLint z);
void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz);
void glColor3f (GLfloat r, GLfloat g, GLfloat b);
void glColor4f (GLfloat r, GLfloat g, GLfloat b, GLfloat a);
void glColor3ub(GLubyte r, GLubyte g, GLubyte b);
void glTexCoord2f(GLfloat s, GLfloat t);

/* Viewport / matrices */
void glViewport       (GLint x, GLint y, GLsizei w, GLsizei h);
void glMatrixMode     (GLenum mode);
void glLoadIdentity   (void);
void glOrtho          (GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void glFrustum        (GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
void glPushMatrix     (void);
void glPopMatrix      (void);
void glTranslatef     (GLfloat x, GLfloat y, GLfloat z);
void glRotatef        (GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glScalef         (GLfloat x, GLfloat y, GLfloat z);

/* Clear / draw */
void glClear     (GLbitfield mask);
void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a);
void glClearDepth(GLclampd depth);
void glFlush     (void);
void glFinish    (void);

/* State / query */
void            glEnable     (GLenum cap);
void            glDisable    (GLenum cap);
void            glBlendFunc  (GLenum sfactor, GLenum dfactor);
GLenum          glGetError   (void);
const GLubyte  *glGetString  (GLenum name);
void            glGetIntegerv(GLenum pname, GLint *params);

#define GL_VENDOR     0x1F00
#define GL_RENDERER   0x1F01
#define GL_VERSION    0x1F02
#define GL_EXTENSIONS 0x1F03

/* Textures */
void glGenTextures  (GLsizei n, GLuint *textures);
void glBindTexture  (GLenum target, GLuint texture);
void glDeleteTextures(GLsizei n, const GLuint *textures);
void glTexImage2D   (GLenum target, GLint level, GLint internalformat,
                     GLsizei width, GLsizei height, GLint border,
                     GLenum format, GLenum type, const GLvoid *pixels);
void glTexParameteri(GLenum target, GLenum pname, GLint param);

/* Pixel ops */
void glReadPixels   (GLint x, GLint y, GLsizei width, GLsizei height,
                     GLenum format, GLenum type, GLvoid *pixels);

/* Vertex arrays */
void glEnableClientState (GLenum array);
void glDisableClientState(GLenum array);
void glVertexPointer     (GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void glNormalPointer     (GLenum type, GLsizei stride, const GLvoid *ptr);
void glColorPointer      (GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void glTexCoordPointer   (GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);
void glDrawArrays        (GLenum mode, GLint first, GLsizei count);
void glDrawElements      (GLenum mode, GLsizei count, GLenum type, const GLvoid *indices);

/* Always pull in glext.h: it holds the GL_* enum #defines and 4.x type
 * typedefs that callers need whether or not they also use gl_funcs.h's
 * function-pointer loader. Its function prototypes are declared but never
 * required to be linked unless actually called, so there's no conflict with
 * callers that only invoke the extension functions through pfnGl* pointers. */
#ifndef GLEXT_BASE_TYPES_H
#define GLEXT_BASE_TYPES_H
typedef long long          GLint64;
typedef unsigned long long GLuint64;
typedef long long          GLint64EXT;
typedef unsigned long long GLuint64EXT;
typedef void*              GLsync;
typedef char               GLchar;
typedef unsigned short     GLhalf;
typedef signed char        GLbyte_t;
typedef float              GLfixed;
typedef long long          GLsizeiptr;
typedef long long          GLintptr;
#endif

#include "glext.h"

#endif /* GL_H */
