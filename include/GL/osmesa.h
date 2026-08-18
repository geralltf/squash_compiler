#ifndef OSMESA_H
#define OSMESA_H

#include "include/GL/gl.h"

typedef void *OSMesaContext;

#define OSMESA_COLOR_INDEX    GL_COLOR_INDEX
#define OSMESA_RGBA           GL_RGBA
#define OSMESA_BGRA           0x1
#define OSMESA_ARGB           0x2
#define OSMESA_RGB            GL_RGB
#define OSMESA_BGR            0x3
#define OSMESA_RGB_565        0x4
#define OSMESA_ROW_LENGTH     0x10
#define OSMESA_Y_UP           0x11
#define OSMESA_WIDTH          0x20
#define OSMESA_HEIGHT         0x21
#define OSMESA_FORMAT         0x22
#define OSMESA_TYPE           0x23
#define OSMESA_MAX_WIDTH      0x24
#define OSMESA_MAX_HEIGHT     0x25

OSMesaContext OSMesaCreateContext(GLenum format, OSMesaContext sharelist);
OSMesaContext OSMesaCreateContextExt(GLenum format, GLint depthBits, GLint stencilBits, GLint accumBits, OSMesaContext sharelist);
void          OSMesaDestroyContext(OSMesaContext ctx);
GLboolean     OSMesaMakeCurrent(OSMesaContext ctx, void *buffer, GLenum type, GLsizei width, GLsizei height);
OSMesaContext OSMesaGetCurrentContext(void);
void          OSMesaPixelStore(GLint pname, GLint value);
void          OSMesaGetIntegerv(GLint pname, GLint *value);
GLboolean     OSMesaGetColorBuffer(OSMesaContext ctx, GLint *width, GLint *height, GLint *bytesPerPixel, void **buffer);
GLboolean     OSMesaGetDepthBuffer(OSMesaContext ctx, GLint *width, GLint *height, GLint *bytesPerPixel, void **buffer);

#endif
