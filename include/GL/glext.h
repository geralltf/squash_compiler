#ifndef GLEXT_H
#define GLEXT_H

/* OpenGL Extension Header — covering OpenGL 4.5 core + common extensions */
/* Requires GL/gl.h to be included first for base types */

#ifndef GL_H
#include "gl.h"
#endif

/* =========================================================================
 * Additional OpenGL types (4.x)
 * (also declared unconditionally in gl.h; skip if already defined so
 * GL_SKIP_GLEXT callers still get these while this file's function
 * prototypes are skipped) */
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

/* =========================================================================
 * OpenGL 2.0 — Shaders
 * ========================================================================= */
#define GL_FRAGMENT_SHADER               0x8B30
#define GL_VERTEX_SHADER                 0x8B31
#define GL_GEOMETRY_SHADER               0x8DD9
#define GL_TESS_CONTROL_SHADER           0x8E88
#define GL_TESS_EVALUATION_SHADER        0x8E87
#define GL_COMPUTE_SHADER                0x91B9
#define GL_DELETE_STATUS                 0x8B80
#define GL_COMPILE_STATUS                0x8B81
#define GL_LINK_STATUS                   0x8B82
#define GL_VALIDATE_STATUS               0x8B83
#define GL_INFO_LOG_LENGTH               0x8B84
#define GL_SHADING_LANGUAGE_VERSION      0x8B8C
#define GL_MAJOR_VERSION                 0x821B
#define GL_MINOR_VERSION                 0x821C
#define GL_CURRENT_PROGRAM               0x8B8D
#define GL_ATTACHED_SHADERS              0x8B85
#define GL_ACTIVE_UNIFORMS               0x8B86
#define GL_ACTIVE_ATTRIBUTES             0x8B89
#define GL_SHADER_SOURCE_LENGTH          0x8B88

GLuint  glCreateShader   (GLenum type);
void    glShaderSource   (GLuint shader, GLsizei count,
                          const GLchar **string, const GLint *length);
void    glCompileShader  (GLuint shader);
void    glGetShaderiv    (GLuint shader, GLenum pname, GLint *params);
void    glGetShaderInfoLog(GLuint shader, GLsizei maxLen, GLsizei *length, GLchar *infoLog);
void    glDeleteShader   (GLuint shader);

GLuint  glCreateProgram  (void);
void    glAttachShader   (GLuint program, GLuint shader);
void    glDetachShader   (GLuint program, GLuint shader);
void    glLinkProgram    (GLuint program);
void    glUseProgram     (GLuint program);
void    glGetProgramiv   (GLuint program, GLenum pname, GLint *params);
void    glGetProgramInfoLog(GLuint program, GLsizei maxLen, GLsizei *length, GLchar *infoLog);
void    glDeleteProgram  (GLuint program);
void    glValidateProgram(GLuint program);

GLint   glGetUniformLocation  (GLuint program, const GLchar *name);
GLint   glGetAttribLocation   (GLuint program, const GLchar *name);
GLuint  glGetUniformBlockIndex(GLuint program, const GLchar *uniformBlockName);

void glUniform1i (GLint loc, GLint v0);
void glUniform2i (GLint loc, GLint v0, GLint v1);
void glUniform3i (GLint loc, GLint v0, GLint v1, GLint v2);
void glUniform4i (GLint loc, GLint v0, GLint v1, GLint v2, GLint v3);
void glUniform1f (GLint loc, GLfloat v0);
void glUniform2f (GLint loc, GLfloat v0, GLfloat v1);
void glUniform3f (GLint loc, GLfloat v0, GLfloat v1, GLfloat v2);
void glUniform4f (GLint loc, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
void glUniform1iv(GLint loc, GLsizei count, const GLint *value);
void glUniform1fv(GLint loc, GLsizei count, const GLfloat *value);
void glUniform2fv(GLint loc, GLsizei count, const GLfloat *value);
void glUniform3fv(GLint loc, GLsizei count, const GLfloat *value);
void glUniform4fv(GLint loc, GLsizei count, const GLfloat *value);
void glUniformMatrix2fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUniformMatrix3fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUniformMatrix4fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUniformMatrix3x4fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat *value);
void glUniformMatrix4x3fv(GLint loc, GLsizei count, GLboolean transpose, const GLfloat *value);

void glVertexAttrib1f (GLuint index, GLfloat x);
void glVertexAttrib2f (GLuint index, GLfloat x, GLfloat y);
void glVertexAttrib3f (GLuint index, GLfloat x, GLfloat y, GLfloat z);
void glVertexAttrib4f (GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
void glEnableVertexAttribArray (GLuint index);
void glDisableVertexAttribArray(GLuint index);
void glVertexAttribPointer     (GLuint index, GLint size, GLenum type, GLboolean normalized,
                                GLsizei stride, const GLvoid *pointer);
void glVertexAttribIPointer    (GLuint index, GLint size, GLenum type,
                                GLsizei stride, const GLvoid *pointer);
void glVertexAttribDivisor     (GLuint index, GLuint divisor);

/* =========================================================================
 * OpenGL 3.0 — Buffer Objects (VBO / IBO / UBO)
 * ========================================================================= */
#define GL_ARRAY_BUFFER                  0x8892
#define GL_ELEMENT_ARRAY_BUFFER          0x8893
#define GL_PIXEL_PACK_BUFFER             0x88EB
#define GL_PIXEL_UNPACK_BUFFER           0x88EC
#define GL_COPY_READ_BUFFER              0x8F36
#define GL_COPY_WRITE_BUFFER             0x8F37
#define GL_UNIFORM_BUFFER                0x8A11
#define GL_SHADER_STORAGE_BUFFER         0x90D2
#define GL_ATOMIC_COUNTER_BUFFER         0x92C0
#define GL_TRANSFORM_FEEDBACK_BUFFER     0x8C8E
#define GL_DRAW_INDIRECT_BUFFER          0x8F3F
#define GL_DISPATCH_INDIRECT_BUFFER      0x90EE
#define GL_QUERY_BUFFER                  0x9192

#define GL_STREAM_DRAW                   0x88E0
#define GL_STREAM_READ                   0x88E1
#define GL_STREAM_COPY                   0x88E2
#define GL_STATIC_DRAW                   0x88E4
#define GL_STATIC_READ                   0x88E5
#define GL_STATIC_COPY                   0x88E6
#define GL_DYNAMIC_DRAW                  0x88E8
#define GL_DYNAMIC_READ                  0x88E9
#define GL_DYNAMIC_COPY                  0x88EA

#define GL_MAP_READ_BIT                  0x0001
#define GL_MAP_WRITE_BIT                 0x0002
#define GL_MAP_INVALIDATE_RANGE_BIT      0x0004
#define GL_MAP_INVALIDATE_BUFFER_BIT     0x0008
#define GL_MAP_FLUSH_EXPLICIT_BIT        0x0010
#define GL_MAP_UNSYNCHRONIZED_BIT        0x0020
#define GL_MAP_PERSISTENT_BIT            0x0040
#define GL_MAP_COHERENT_BIT              0x0080
#define GL_DYNAMIC_STORAGE_BIT           0x0100
#define GL_CLIENT_STORAGE_BIT            0x0200

void    glGenBuffers    (GLsizei n, GLuint *buffers);
void    glDeleteBuffers (GLsizei n, const GLuint *buffers);
void    glBindBuffer    (GLenum target, GLuint buffer);
void    glBufferData    (GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage);
void    glBufferSubData (GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data);
void   *glMapBuffer     (GLenum target, GLenum access);
void   *glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
GLboolean glUnmapBuffer (GLenum target);
void    glBindBufferBase(GLenum target, GLuint index, GLuint buffer);
void    glBindBufferRange(GLenum target, GLuint index, GLuint buffer,
                          GLintptr offset, GLsizeiptr size);
void    glCopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                            GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size);
void    glGetBufferSubData (GLenum target, GLintptr offset, GLsizeiptr size, GLvoid *data);
void    glGetBufferParameteriv(GLenum target, GLenum pname, GLint *params);
void    glUniformBlockBinding(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding);

/* =========================================================================
 * OpenGL 3.0 — Vertex Array Objects (VAO)
 * ========================================================================= */
void  glGenVertexArrays   (GLsizei n, GLuint *arrays);
void  glDeleteVertexArrays(GLsizei n, const GLuint *arrays);
void  glBindVertexArray   (GLuint array);
GLboolean glIsVertexArray (GLuint array);

/* =========================================================================
 * OpenGL 3.0 — Framebuffer Objects (FBO)
 * ========================================================================= */
#define GL_COLOR                         0x1800
#define GL_FRAMEBUFFER                   0x8D40
#define GL_READ_FRAMEBUFFER              0x8CA8
#define GL_DRAW_FRAMEBUFFER              0x8CA9
#define GL_RENDERBUFFER                  0x8D41
#define GL_DEPTH_ATTACHMENT              0x8D00
#define GL_STENCIL_ATTACHMENT            0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT      0x821A
#define GL_COLOR_ATTACHMENT0             0x8CE0
#define GL_COLOR_ATTACHMENT1             0x8CE1
#define GL_COLOR_ATTACHMENT2             0x8CE2
#define GL_COLOR_ATTACHMENT3             0x8CE3
#define GL_COLOR_ATTACHMENT4             0x8CE4
#define GL_COLOR_ATTACHMENT5             0x8CE5
#define GL_COLOR_ATTACHMENT6             0x8CE6
#define GL_COLOR_ATTACHMENT7             0x8CE7
#define GL_FRAMEBUFFER_COMPLETE          0x8CD5
#define GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT         0x8CD6
#define GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT 0x8CD7
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER        0x8CDB
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER        0x8CDC
#define GL_FRAMEBUFFER_UNSUPPORTED       0x8CDD
#define GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE 0x8D56
#define GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS 0x8DA8

void    glGenFramebuffers      (GLsizei n, GLuint *framebuffers);
void    glDeleteFramebuffers   (GLsizei n, const GLuint *framebuffers);
void    glBindFramebuffer      (GLenum target, GLuint framebuffer);
GLenum  glCheckFramebufferStatus(GLenum target);
void    glFramebufferTexture2D (GLenum target, GLenum attachment, GLenum textarget,
                                GLuint texture, GLint level);
void    glFramebufferTexture   (GLenum target, GLenum attachment, GLuint texture, GLint level);
void    glFramebufferRenderbuffer(GLenum target, GLenum attachment,
                                  GLenum renderbuffertarget, GLuint renderbuffer);
void    glBlitFramebuffer      (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                                GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                                GLbitfield mask, GLenum filter);
void    glDrawBuffers          (GLsizei n, const GLenum *bufs);
void    glReadBuffer           (GLenum mode);

void    glGenRenderbuffers     (GLsizei n, GLuint *renderbuffers);
void    glDeleteRenderbuffers  (GLsizei n, const GLuint *renderbuffers);
void    glBindRenderbuffer     (GLenum target, GLuint renderbuffer);
void    glRenderbufferStorage  (GLenum target, GLenum internalformat,
                                GLsizei width, GLsizei height);
void    glRenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                         GLenum internalformat, GLsizei width, GLsizei height);

/* =========================================================================
 * OpenGL 3.0 — Texture formats and storage
 * ========================================================================= */
#define GL_TEXTURE_1D                    0x0DE0
#define GL_TEXTURE_3D                    0x806F
#define GL_TEXTURE_1D_ARRAY              0x8C18
#define GL_TEXTURE_2D_ARRAY              0x8C1A
#define GL_TEXTURE_RECTANGLE             0x84F5
#define GL_TEXTURE_CUBE_MAP              0x8513
#define GL_TEXTURE_CUBE_MAP_ARRAY        0x9009
#define GL_TEXTURE_BUFFER                0x8C2A
#define GL_TEXTURE_2D_MULTISAMPLE        0x9100
#define GL_TEXTURE_2D_MULTISAMPLE_ARRAY  0x9102
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X   0x8515
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_X   0x8516
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Y   0x8517
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Y   0x8518
#define GL_TEXTURE_CUBE_MAP_POSITIVE_Z   0x8519
#define GL_TEXTURE_CUBE_MAP_NEGATIVE_Z   0x851A
#define GL_SEAMLESS_CUBE_MAP             0x884F

/* Internal formats */
#define GL_R8                            0x8229
#define GL_R16                           0x822A
#define GL_RG8                           0x822B
#define GL_RG16                          0x822C
#define GL_RGB8                          0x8051
#define GL_RGBA8                         0x8058
#define GL_RGB16                         0x8054
#define GL_RGBA16                        0x805B
#define GL_R32F                          0x822E
#define GL_RG32F                         0x8230
#define GL_RGB32F                        0x8815
#define GL_RGBA32F                       0x8814
#define GL_R16F                          0x822D
#define GL_RG16F                         0x822F
#define GL_RGB16F                        0x881B
#define GL_RGBA16F                       0x881A
#define GL_R8I                           0x8231
#define GL_R8UI                          0x8232
#define GL_R16I                          0x8233
#define GL_R16UI                         0x8234
#define GL_R32I                          0x8235
#define GL_R32UI                         0x8236
#define GL_RG8I                          0x8237
#define GL_RG8UI                         0x8238
#define GL_RG16I                         0x8239
#define GL_RG16UI                        0x823A
#define GL_RG32I                         0x823B
#define GL_RG32UI                        0x823C
#define GL_RGBA8UI                       0x8D7C
#define GL_RGBA32UI                      0x8D70
#define GL_RGBA16UI                      0x8D76
#define GL_RGBA8I                        0x8D8E
#define GL_RGBA32I                       0x8D82
#define GL_RGBA16I                       0x8D88
#define GL_DEPTH_COMPONENT16             0x81A5
#define GL_DEPTH_COMPONENT24             0x81A6
#define GL_DEPTH_COMPONENT32F            0x8CAC
#define GL_DEPTH24_STENCIL8              0x88F0
#define GL_DEPTH32F_STENCIL8             0x8CAD
#define GL_COMPRESSED_RGB                0x84ED
#define GL_COMPRESSED_RGBA               0x84EE
#define GL_COMPRESSED_SRGB               0x8C48
#define GL_COMPRESSED_SRGB_ALPHA         0x8C49

/* Texture parameter names */
#define GL_TEXTURE_BASE_LEVEL            0x813C
#define GL_TEXTURE_MAX_LEVEL             0x813D
#define GL_TEXTURE_WRAP_R                0x8072
#define GL_CLAMP_TO_EDGE                 0x812F
#define GL_CLAMP_TO_BORDER               0x812D
#define GL_MIRRORED_REPEAT               0x8370
#define GL_LINEAR_MIPMAP_NEAREST         0x2701
#define GL_LINEAR_MIPMAP_LINEAR          0x2703
#define GL_NEAREST_MIPMAP_NEAREST        0x2700
#define GL_NEAREST_MIPMAP_LINEAR         0x2702
#define GL_TEXTURE_LOD_BIAS              0x8501
#define GL_TEXTURE_COMPARE_MODE          0x884C
#define GL_TEXTURE_COMPARE_FUNC          0x884D
#define GL_COMPARE_REF_TO_TEXTURE        0x884E
#define GL_TEXTURE_SWIZZLE_R             0x8E42
#define GL_TEXTURE_SWIZZLE_G             0x8E43
#define GL_TEXTURE_SWIZZLE_B             0x8E44
#define GL_TEXTURE_SWIZZLE_A             0x8E45
#define GL_TEXTURE_BORDER_COLOR          0x1004
#define GL_TEXTURE_MAX_ANISOTROPY        0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY    0x84FF

void    glActiveTexture         (GLenum texture);
void    glGenerateMipmap        (GLenum target);
void    glTexImage3D            (GLenum target, GLint level, GLint internalformat,
                                 GLsizei width, GLsizei height, GLsizei depth, GLint border,
                                 GLenum format, GLenum type, const GLvoid *pixels);
void    glTexSubImage2D         (GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                 GLsizei width, GLsizei height, GLenum format, GLenum type,
                                 const GLvoid *pixels);
void    glTexSubImage3D         (GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                 GLint zoffset, GLsizei width, GLsizei height, GLsizei depth,
                                 GLenum format, GLenum type, const GLvoid *pixels);
void    glTexStorage2D          (GLenum target, GLsizei levels, GLenum internalformat,
                                 GLsizei width, GLsizei height);
void    glTexStorage3D          (GLenum target, GLsizei levels, GLenum internalformat,
                                 GLsizei width, GLsizei height, GLsizei depth);
void    glTexParameterf         (GLenum target, GLenum pname, GLfloat param);
void    glTexParameterfv        (GLenum target, GLenum pname, const GLfloat *params);
void    glTexParameteriv        (GLenum target, GLenum pname, const GLint *params);

#define GL_TEXTURE0  0x84C0
#define GL_TEXTURE1  0x84C1
#define GL_TEXTURE2  0x84C2
#define GL_TEXTURE3  0x84C3
#define GL_TEXTURE4  0x84C4
#define GL_TEXTURE5  0x84C5
#define GL_TEXTURE6  0x84C6
#define GL_TEXTURE7  0x84C7
#define GL_TEXTURE8  0x84C8
#define GL_TEXTURE9  0x84C9
#define GL_TEXTURE10 0x84CA
#define GL_TEXTURE11 0x84CB
#define GL_TEXTURE12 0x84CC
#define GL_TEXTURE13 0x84CD
#define GL_TEXTURE14 0x84CE
#define GL_TEXTURE15 0x84CF
#define GL_TEXTURE16 0x84D0
#define GL_TEXTURE17 0x84D1
#define GL_TEXTURE18 0x84D2
#define GL_TEXTURE19 0x84D3
#define GL_TEXTURE20 0x84D4
#define GL_TEXTURE21 0x84D5
#define GL_TEXTURE22 0x84D6
#define GL_TEXTURE23 0x84D7
#define GL_TEXTURE24 0x84D8
#define GL_TEXTURE25 0x84D9
#define GL_TEXTURE26 0x84DA
#define GL_TEXTURE27 0x84DB
#define GL_TEXTURE28 0x84DC
#define GL_TEXTURE29 0x84DD
#define GL_TEXTURE30 0x84DE
#define GL_TEXTURE31 0x84DF

/* =========================================================================
 * OpenGL 3.2 — Sync objects / fences
 * ========================================================================= */
#define GL_SYNC_GPU_COMMANDS_COMPLETE    0x9117
#define GL_SYNC_FLUSH_COMMANDS_BIT       0x00000001
#define GL_TIMEOUT_IGNORED               0xFFFFFFFFFFFFFFFFull
#define GL_ALREADY_SIGNALED              0x911A
#define GL_TIMEOUT_EXPIRED               0x911B
#define GL_CONDITION_SATISFIED           0x911C
#define GL_WAIT_FAILED                   0x911D

GLsync  glFenceSync      (GLenum condition, GLbitfield flags);
GLenum  glClientWaitSync (GLsync sync, GLbitfield flags, GLuint64 timeout);
void    glWaitSync       (GLsync sync, GLbitfield flags, GLuint64 timeout);
void    glDeleteSync     (GLsync sync);
GLboolean glIsSync       (GLsync sync);

/* =========================================================================
 * OpenGL 4.0 — Tessellation shaders
 * ========================================================================= */
#define GL_PATCHES                       0x000E
#define GL_PATCH_VERTICES                0x8E72
#define GL_TESS_CONTROL_OUTPUT_VERTICES  0x8E75
#define GL_TESS_GEN_MODE                 0x8E76
#define GL_TESS_GEN_SPACING              0x8E77
#define GL_TESS_GEN_VERTEX_ORDER         0x8E78
#define GL_TESS_GEN_POINT_MODE           0x8E79
#define GL_TRIANGLES_ADJACENCY           0x000C
#define GL_TRIANGLE_STRIP_ADJACENCY      0x000D
#define GL_LINES_ADJACENCY               0x000A
#define GL_LINE_STRIP_ADJACENCY          0x000B
#define GL_ISOLINES                      0x8E7A
#define GL_EQUAL                         0x8E7F
#define GL_FRACTIONAL_ODD                0x8E7B
#define GL_FRACTIONAL_EVEN               0x8E7C
#define GL_MAX_TESS_CONTROL_INPUT_COMPONENTS  0x886C
#define GL_MAX_TESS_EVALUATION_INPUT_COMPONENTS 0x886D

void    glPatchParameteri  (GLenum pname, GLint value);
void    glPatchParameterfv (GLenum pname, const GLfloat *values);

/* =========================================================================
 * OpenGL 4.2 — Compute / image access
 * ========================================================================= */
#define GL_COMPUTE_LOCAL_WORK_SIZE       0x8267

void    glDispatchCompute    (GLuint num_groups_x, GLuint num_groups_y, GLuint num_groups_z);
void    glDispatchComputeIndirect(GLintptr indirect);
void    glMemoryBarrier      (GLbitfield barriers);

#define GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT      0x00000001
#define GL_ELEMENT_ARRAY_BARRIER_BIT            0x00000002
#define GL_UNIFORM_BARRIER_BIT                  0x00000004
#define GL_TEXTURE_FETCH_BARRIER_BIT            0x00000008
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT      0x00000020
#define GL_COMMAND_DRAW_BARRIER_BIT             0x00000040
#define GL_PIXEL_BUFFER_BARRIER_BIT             0x00000080
#define GL_TEXTURE_UPDATE_BARRIER_BIT           0x00000100
#define GL_BUFFER_UPDATE_BARRIER_BIT            0x00000200
#define GL_FRAMEBUFFER_BARRIER_BIT              0x00000400
#define GL_TRANSFORM_FEEDBACK_BARRIER_BIT       0x00000800
#define GL_ATOMIC_COUNTER_BARRIER_BIT           0x00001000
#define GL_SHADER_STORAGE_BARRIER_BIT           0x00002000
#define GL_ALL_BARRIER_BITS                     0xFFFFFFFF

void    glBindImageTexture   (GLuint unit, GLuint texture, GLint level, GLboolean layered,
                              GLint layer, GLenum access, GLenum format);

/* =========================================================================
 * OpenGL 4.3 — Debug output, SSBO, compute
 * ========================================================================= */
#define GL_DEBUG_OUTPUT                  0x92E0
#define GL_DEBUG_OUTPUT_SYNCHRONOUS      0x8242
#define GL_DEBUG_SEVERITY_HIGH           0x9146
#define GL_DEBUG_SEVERITY_MEDIUM         0x9147
#define GL_DEBUG_SEVERITY_LOW            0x9148
#define GL_DEBUG_SEVERITY_NOTIFICATION   0x826B
#define GL_DEBUG_SOURCE_API              0x8246
#define GL_DEBUG_SOURCE_WINDOW_SYSTEM    0x8247
#define GL_DEBUG_SOURCE_SHADER_COMPILER  0x8248
#define GL_DEBUG_SOURCE_THIRD_PARTY      0x8249
#define GL_DEBUG_SOURCE_APPLICATION      0x824A
#define GL_DEBUG_SOURCE_OTHER            0x824B
#define GL_DEBUG_TYPE_ERROR              0x824C
#define GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR 0x824D
#define GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR  0x824E
#define GL_DEBUG_TYPE_PORTABILITY        0x824F
#define GL_DEBUG_TYPE_PERFORMANCE        0x8250
#define GL_DEBUG_TYPE_OTHER              0x8251
#define GL_DEBUG_TYPE_MARKER             0x8268
#define GL_DEBUG_TYPE_PUSH_GROUP         0x8269
#define GL_DEBUG_TYPE_POP_GROUP          0x826A
#define GL_MAX_DEBUG_MESSAGE_LENGTH      0x9143
#define GL_MAX_DEBUG_LOGGED_MESSAGES     0x9144
#define GL_DEBUG_LOGGED_MESSAGES         0x9145
#define GL_DEBUG_NEXT_LOGGED_MESSAGE_LENGTH 0x8243
#define GL_MAX_DEBUG_GROUP_STACK_DEPTH   0x826C
#define GL_DEBUG_GROUP_STACK_DEPTH       0x826D
#define GL_MAX_LABEL_LENGTH              0x82E8
#define GL_SHADER_STORAGE_BUFFER_BINDING 0x90D3

typedef void (*GLDEBUGPROC)(GLenum source, GLenum type, GLuint id, GLenum severity,
                             GLsizei length, const GLchar *message, const void *userParam);
void    glDebugMessageControl   (GLenum source, GLenum type, GLenum severity,
                                 GLsizei count, const GLuint *ids, GLboolean enabled);
void    glDebugMessageCallback  (GLDEBUGPROC callback, const void *userParam);
void    glDebugMessageInsert    (GLenum source, GLenum type, GLuint id, GLenum severity,
                                 GLsizei length, const GLchar *buf);
void    glPushDebugGroup        (GLenum source, GLuint id, GLsizei length, const GLchar *message);
void    glPopDebugGroup         (void);
void    glObjectLabel           (GLenum identifier, GLuint name, GLsizei length, const GLchar *label);
void    glGetObjectLabel        (GLenum identifier, GLuint name, GLsizei bufSize,
                                 GLsizei *length, GLchar *label);
GLuint  glGetDebugMessageLog    (GLuint count, GLsizei bufSize, GLenum *sources, GLenum *types,
                                 GLuint *ids, GLenum *severities, GLsizei *lengths, GLchar *messageLog);

#define GL_MAX_COMPUTE_WORK_GROUP_COUNT         0x91BE
#define GL_MAX_COMPUTE_WORK_GROUP_SIZE          0x91BF
#define GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS   0x90EB
#define GL_MAX_COMPUTE_SHARED_MEMORY_SIZE       0x8262
#define GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS   0x90DD
#define GL_MAX_SHADER_STORAGE_BLOCK_SIZE        0x90DE
#define GL_MAX_COMBINED_SHADER_STORAGE_BLOCKS   0x90DC

void    glShaderStorageBlockBinding(GLuint program, GLuint storageBlockIndex, GLuint storageBlockBinding);

/* =========================================================================
 * OpenGL 4.4 — Buffer storage, multi-bind
 * ========================================================================= */
void    glBufferStorage    (GLenum target, GLsizeiptr size, const GLvoid *data, GLbitfield flags);
void    glBindBuffersBase  (GLenum target, GLuint first, GLsizei count, const GLuint *buffers);
void    glBindBuffersRange (GLenum target, GLuint first, GLsizei count, const GLuint *buffers,
                            const GLintptr *offsets, const GLsizeiptr *sizes);
void    glBindTextures     (GLuint first, GLsizei count, const GLuint *textures);
void    glBindSamplers     (GLuint first, GLsizei count, const GLuint *samplers);
void    glBindImageTextures(GLuint first, GLsizei count, const GLuint *textures);
void    glBindVertexBuffers(GLuint first, GLsizei count, const GLuint *buffers,
                            const GLintptr *offsets, const GLsizei *strides);

/* =========================================================================
 * OpenGL 4.5 — Direct State Access (DSA)
 * ========================================================================= */
/* DSA Textures */
void    glCreateTextures              (GLenum target, GLsizei n, GLuint *textures);
void    glTextureStorage2D            (GLuint texture, GLsizei levels, GLenum internalformat,
                                       GLsizei width, GLsizei height);
void    glTextureStorage3D            (GLuint texture, GLsizei levels, GLenum internalformat,
                                       GLsizei width, GLsizei height, GLsizei depth);
void    glTextureSubImage2D           (GLuint texture, GLint level, GLint xoffset, GLint yoffset,
                                       GLsizei width, GLsizei height, GLenum format, GLenum type,
                                       const GLvoid *pixels);
void    glTextureSubImage3D           (GLuint texture, GLint level, GLint xoffset, GLint yoffset,
                                       GLint zoffset, GLsizei width, GLsizei height, GLsizei depth,
                                       GLenum format, GLenum type, const GLvoid *pixels);
void    glTextureParameteri           (GLuint texture, GLenum pname, GLint param);
void    glTextureParameterf           (GLuint texture, GLenum pname, GLfloat param);
void    glTextureParameteriv          (GLuint texture, GLenum pname, const GLint *params);
void    glTextureParameterfv          (GLuint texture, GLenum pname, const GLfloat *params);
void    glGenerateTextureMipmap       (GLuint texture);
void    glBindTextureUnit             (GLuint unit, GLuint texture);

/* DSA Buffers */
void    glCreateBuffers               (GLsizei n, GLuint *buffers);
void    glNamedBufferData             (GLuint buffer, GLsizeiptr size, const GLvoid *data, GLenum usage);
void    glNamedBufferSubData          (GLuint buffer, GLintptr offset, GLsizeiptr size, const GLvoid *data);
void    glNamedBufferStorage          (GLuint buffer, GLsizeiptr size, const GLvoid *data, GLbitfield flags);
void   *glMapNamedBuffer              (GLuint buffer, GLenum access);
void   *glMapNamedBufferRange         (GLuint buffer, GLintptr offset, GLsizeiptr length, GLbitfield access);
GLboolean glUnmapNamedBuffer          (GLuint buffer);
void    glCopyNamedBufferSubData      (GLuint readBuffer, GLuint writeBuffer, GLintptr readOffset,
                                       GLintptr writeOffset, GLsizeiptr size);
void    glGetNamedBufferSubData       (GLuint buffer, GLintptr offset, GLsizeiptr size, GLvoid *data);
void    glFlushMappedNamedBufferRange (GLuint buffer, GLintptr offset, GLsizeiptr length);

/* DSA Vertex Arrays */
void    glCreateVertexArrays          (GLsizei n, GLuint *arrays);
void    glEnableVertexArrayAttrib     (GLuint vaobj, GLuint index);
void    glDisableVertexArrayAttrib    (GLuint vaobj, GLuint index);
void    glVertexArrayVertexBuffer     (GLuint vaobj, GLuint bindingindex, GLuint buffer,
                                       GLintptr offset, GLsizei stride);
void    glVertexArrayElementBuffer    (GLuint vaobj, GLuint buffer);
void    glVertexArrayAttribFormat     (GLuint vaobj, GLuint attribindex, GLint size,
                                       GLenum type, GLboolean normalized, GLuint relativeoffset);
void    glVertexArrayAttribIFormat    (GLuint vaobj, GLuint attribindex, GLint size,
                                       GLenum type, GLuint relativeoffset);
void    glVertexArrayAttribLFormat    (GLuint vaobj, GLuint attribindex, GLint size,
                                       GLenum type, GLuint relativeoffset);
void    glVertexArrayAttribBinding    (GLuint vaobj, GLuint attribindex, GLuint bindingindex);
void    glVertexArrayBindingDivisor   (GLuint vaobj, GLuint bindingindex, GLuint divisor);

/* DSA Framebuffers */
void    glCreateFramebuffers          (GLsizei n, GLuint *framebuffers);
void    glNamedFramebufferTexture     (GLuint framebuffer, GLenum attachment,
                                       GLuint texture, GLint level);
void    glNamedFramebufferRenderbuffer(GLuint framebuffer, GLenum attachment,
                                       GLenum renderbuffertarget, GLuint renderbuffer);
void    glNamedFramebufferDrawBuffers (GLuint framebuffer, GLsizei n, const GLenum *bufs);
void    glNamedFramebufferReadBuffer  (GLuint framebuffer, GLenum mode);
GLenum  glCheckNamedFramebufferStatus (GLuint framebuffer, GLenum target);
void    glBlitNamedFramebuffer        (GLuint readFramebuffer, GLuint drawFramebuffer,
                                       GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                                       GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                                       GLbitfield mask, GLenum filter);

/* DSA Renderbuffers */
void    glCreateRenderbuffers         (GLsizei n, GLuint *renderbuffers);
void    glNamedRenderbufferStorage    (GLuint renderbuffer, GLenum internalformat,
                                       GLsizei width, GLsizei height);
void    glNamedRenderbufferStorageMultisample(GLuint renderbuffer, GLsizei samples,
                                              GLenum internalformat, GLsizei width, GLsizei height);

/* Clip control (4.5) */
#define GL_LOWER_LEFT                    0x8CA1
#define GL_UPPER_LEFT                    0x8CA2
#define GL_NEGATIVE_ONE_TO_ONE           0x935E
#define GL_ZERO_TO_ONE                   0x935F
void    glClipControl                 (GLenum origin, GLenum depth);

/* Transform feedback (3.0) */
#define GL_TRANSFORM_FEEDBACK            0x8E22
#define GL_INTERLEAVED_ATTRIBS           0x8C8C
#define GL_SEPARATE_ATTRIBS              0x8C8D
void    glGenTransformFeedbacks        (GLsizei n, GLuint *ids);
void    glDeleteTransformFeedbacks     (GLsizei n, const GLuint *ids);
void    glBindTransformFeedback        (GLenum target, GLuint id);
void    glBeginTransformFeedback       (GLenum primitiveMode);
void    glEndTransformFeedback         (void);
void    glTransformFeedbackVaryings    (GLuint program, GLsizei count,
                                        const GLchar **varyings, GLenum bufferMode);
void    glPauseTransformFeedback       (void);
void    glResumeTransformFeedback      (void);

/* Queries */
#define GL_SAMPLES_PASSED                0x8914
#define GL_ANY_SAMPLES_PASSED            0x8C2F
#define GL_PRIMITIVES_GENERATED          0x8C87
#define GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN 0x8C88
#define GL_TIME_ELAPSED                  0x88BF
#define GL_TIMESTAMP                     0x8E28
#define GL_QUERY_RESULT                  0x8866
#define GL_QUERY_RESULT_AVAILABLE        0x8867
#define GL_QUERY_RESULT_NO_WAIT          0x9194
void    glGenQueries             (GLsizei n, GLuint *ids);
void    glDeleteQueries          (GLsizei n, const GLuint *ids);
void    glBeginQuery             (GLenum target, GLuint id);
void    glEndQuery               (GLenum target);
void    glGetQueryObjectiv       (GLuint id, GLenum pname, GLint *params);
void    glGetQueryObjectuiv      (GLuint id, GLenum pname, GLuint *params);
void    glGetQueryObjecti64v     (GLuint id, GLenum pname, GLint64 *params);
void    glGetQueryObjectui64v    (GLuint id, GLenum pname, GLuint64 *params);
void    glQueryCounter           (GLuint id, GLenum target);

/* Sampler objects */
void    glGenSamplers            (GLsizei n, GLuint *samplers);
void    glDeleteSamplers         (GLsizei n, const GLuint *samplers);
void    glBindSampler            (GLuint unit, GLuint sampler);
void    glSamplerParameteri      (GLuint sampler, GLenum pname, GLint param);
void    glSamplerParameterf      (GLuint sampler, GLenum pname, GLfloat param);
void    glSamplerParameteriv     (GLuint sampler, GLenum pname, const GLint *params);
void    glSamplerParameterfv     (GLuint sampler, GLenum pname, const GLfloat *params);

/* Indirect / multi-draw */
void    glDrawArraysIndirect     (GLenum mode, const GLvoid *indirect);
void    glDrawElementsIndirect   (GLenum mode, GLenum type, const GLvoid *indirect);
void    glMultiDrawArraysIndirect (GLenum mode, const GLvoid *indirect,
                                  GLsizei drawcount, GLsizei stride);
void    glMultiDrawElementsIndirect(GLenum mode, GLenum type, const GLvoid *indirect,
                                    GLsizei drawcount, GLsizei stride);
void    glDrawArraysInstanced    (GLenum mode, GLint first, GLsizei count, GLsizei primcount);
void    glDrawElementsInstanced  (GLenum mode, GLsizei count, GLenum type,
                                  const GLvoid *indices, GLsizei primcount);
void    glDrawElementsBaseVertex (GLenum mode, GLsizei count, GLenum type,
                                  const GLvoid *indices, GLint basevertex);
void    glDrawRangeElements      (GLenum mode, GLuint start, GLuint end, GLsizei count,
                                  GLenum type, const GLvoid *indices);

/* Misc 4.x */
void    glGetInteger64v          (GLenum pname, GLint64 *data);
void    glGetBooleani_v          (GLenum target, GLuint index, GLboolean *data);
void    glGetIntegeri_v          (GLenum target, GLuint index, GLint *data);
void    glGetInteger64i_v        (GLenum target, GLuint index, GLint64 *data);
void    glGetFloatv              (GLenum pname, GLfloat *data);
void    glGetDoublev             (GLenum pname, GLdouble *data);
void    glPointSize              (GLfloat size);
void    glLineWidth              (GLfloat width);
void    glPolygonMode            (GLenum face, GLenum mode);
void    glDepthMask              (GLboolean flag);
void    glDepthFunc              (GLenum func);
void    glDepthRange             (GLdouble near_, GLdouble far_);
void    glDepthRangef            (GLfloat near_, GLfloat far_);
void    glStencilFunc            (GLenum func, GLint ref, GLuint mask);
void    glStencilOp              (GLenum sfail, GLenum dpfail, GLenum dppass);
void    glStencilMask            (GLuint mask);
void    glColorMask              (GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
void    glScissor                (GLint x, GLint y, GLsizei width, GLsizei height);
void    glPolygonOffset          (GLfloat factor, GLfloat units);
void    glSampleCoverage         (GLfloat value, GLboolean invert);
void    glFrontFace              (GLenum mode);
void    glCullFace               (GLenum mode);
void    glLogicOp                (GLenum opcode);
void    glPrimitiveRestartIndex  (GLuint index);

#define GL_FILL                          0x1B02
#define GL_LINE                          0x1B01
#define GL_POINT                         0x1B00
#define GL_FRONT                         0x0404
#define GL_BACK                          0x0405
#define GL_FRONT_AND_BACK                0x0408
#define GL_CW                            0x0900
#define GL_CCW                           0x0901
#define GL_NEVER                         0x0200
#define GL_LESS                          0x0201
#define GL_LEQUAL                        0x0203
#define GL_GREATER                       0x0204
#define GL_NOTEQUAL                      0x0205
#define GL_GEQUAL                        0x0206
#define GL_ALWAYS                        0x0207
#define GL_KEEP                          0x1E00
#define GL_REPLACE                       0x1E01
#define GL_INCR                          0x1E02
#define GL_DECR                          0x1E03
#define GL_INCR_WRAP                     0x8507
#define GL_DECR_WRAP                     0x8508
#define GL_INVERT                        0x150A
#define GL_FUNC_ADD                      0x8006
#define GL_FUNC_SUBTRACT                 0x800A
#define GL_FUNC_REVERSE_SUBTRACT         0x800B
#define GL_MIN                           0x8007
#define GL_MAX                           0x8008
#define GL_BLEND_EQUATION                0x8009
#define GL_BLEND_COLOR                   0x8005
#define GL_CONSTANT_COLOR                0x8001
#define GL_ONE_MINUS_CONSTANT_COLOR      0x8002
#define GL_CONSTANT_ALPHA                0x8003
#define GL_ONE_MINUS_CONSTANT_ALPHA      0x8004
#define GL_SRC_ALPHA_SATURATE            0x0308
#define GL_SRC1_ALPHA                    0x8589
#define GL_SRC1_COLOR                    0x88F9
#define GL_ONE_MINUS_SRC1_COLOR          0x88FA
#define GL_ONE_MINUS_SRC1_ALPHA          0x88FB

void    glBlendEquation          (GLenum mode);
void    glBlendEquationSeparate  (GLenum modeRGB, GLenum modeAlpha);
void    glBlendFuncSeparate      (GLenum sfactorRGB, GLenum dfactorRGB,
                                  GLenum sfactorAlpha, GLenum dfactorAlpha);
void    glBlendColor             (GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);

/* Maximum constants */
#define GL_MAX_VERTEX_ATTRIBS                     0x8869
#define GL_MAX_VERTEX_UNIFORM_COMPONENTS          0x8B4A
#define GL_MAX_VERTEX_UNIFORM_BLOCKS              0x8A2B
#define GL_MAX_VERTEX_OUTPUT_COMPONENTS           0x9122
#define GL_MAX_FRAGMENT_UNIFORM_COMPONENTS        0x8B49
#define GL_MAX_FRAGMENT_UNIFORM_BLOCKS            0x8A2D
#define GL_MAX_FRAGMENT_INPUT_COMPONENTS          0x9125
#define GL_MAX_TEXTURE_IMAGE_UNITS                0x8872
#define GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS       0x8B4D
#define GL_MAX_UNIFORM_BUFFER_BINDINGS            0x8A2F
#define GL_MAX_UNIFORM_BLOCK_SIZE                 0x8A30
#define GL_MAX_VARYING_FLOATS                     0x8B4B
#define GL_MAX_COLOR_ATTACHMENTS                  0x8CDF
#define GL_MAX_SAMPLES                            0x8D57
#define GL_MAX_VIEWPORT_DIMS                      0x0D3A
#define GL_MAX_TEXTURE_SIZE                       0x0D33
#define GL_MAX_3D_TEXTURE_SIZE                    0x8073
#define GL_MAX_ARRAY_TEXTURE_LAYERS               0x88FF
#define GL_MAX_CUBE_MAP_TEXTURE_SIZE              0x851C
#define GL_MAX_RENDERBUFFER_SIZE                  0x84E8

/* Program interface query (GL 4.3) */
#define GL_PROGRAM_INPUT                 0x92E3
#define GL_PROGRAM_OUTPUT                0x92E4
#define GL_UNIFORM                       0x92E1
#define GL_UNIFORM_BLOCK                 0x92E2
#define GL_SHADER_STORAGE_BLOCK          0x92E6
#define GL_ACTIVE_RESOURCES              0x92F5
#define GL_NAME_LENGTH                   0x92F9
#define GL_TYPE                          0x92FA
#define GL_ARRAY_SIZE                    0x92FB
#define GL_LOCATION                      0x930E
#define GL_OFFSET                        0x92FC
#define GL_BLOCK_INDEX                   0x92FD
#define GL_ARRAY_STRIDE                  0x92FE
#define GL_MATRIX_STRIDE                 0x92FF
#define GL_IS_ROW_MAJOR                  0x9300
#define GL_ATOMIC_COUNTER_BUFFER_INDEX   0x9301
void    glGetProgramInterfaceiv         (GLuint program, GLenum programInterface,
                                         GLenum pname, GLint *params);
GLuint  glGetProgramResourceIndex       (GLuint program, GLenum programInterface, const GLchar *name);
void    glGetProgramResourceName        (GLuint program, GLenum programInterface,
                                         GLuint index, GLsizei bufSize, GLsizei *length, GLchar *name);
void    glGetProgramResourceiv          (GLuint program, GLenum programInterface, GLuint index,
                                         GLsizei propCount, const GLenum *props, GLsizei bufSize,
                                         GLsizei *length, GLint *params);
GLint   glGetProgramResourceLocation   (GLuint program, GLenum programInterface, const GLchar *name);

/* Separate shader programs (4.1) */
GLuint  glCreateShaderProgramv         (GLenum type, GLsizei count, const GLchar **strings);
void    glGenProgramPipelines          (GLsizei n, GLuint *pipelines);
void    glDeleteProgramPipelines       (GLsizei n, const GLuint *pipelines);
void    glBindProgramPipeline          (GLuint pipeline);
void    glUseProgramStages             (GLuint pipeline, GLbitfield stages, GLuint program);

#define GL_VERTEX_SHADER_BIT             0x00000001
#define GL_FRAGMENT_SHADER_BIT           0x00000002
#define GL_GEOMETRY_SHADER_BIT           0x00000004
#define GL_TESS_CONTROL_SHADER_BIT       0x00000008
#define GL_TESS_EVALUATION_SHADER_BIT    0x00000010
#define GL_COMPUTE_SHADER_BIT            0x00000020
#define GL_ALL_SHADER_BITS               0xFFFFFFFF

/* Precision */
void    glMinSampleShading             (GLfloat value);

/* Clear functions (4.4) */
void    glClearBufferiv   (GLenum buffer, GLint drawbuffer, const GLint *value);
void    glClearBufferuiv  (GLenum buffer, GLint drawbuffer, const GLuint *value);
void    glClearBufferfv   (GLenum buffer, GLint drawbuffer, const GLfloat *value);
void    glClearBufferfi   (GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil);
void    glClearDepthf     (GLfloat depth);
void    glClearStencil    (GLint s);

/* DSA clear (4.5) */
void    glClearNamedFramebufferiv  (GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLint *value);
void    glClearNamedFramebufferfv  (GLuint framebuffer, GLenum buffer, GLint drawbuffer, const GLfloat *value);
void    glClearNamedFramebufferfi  (GLuint framebuffer, GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil);

/* Pixel transfer */
void    glPixelStorei  (GLenum pname, GLint param);
void    glPixelStoref  (GLenum pname, GLfloat param);

#define GL_PACK_SWAP_BYTES               0x0D00
#define GL_PACK_LSB_FIRST                0x0D01
#define GL_PACK_ROW_LENGTH               0x0D02
#define GL_PACK_IMAGE_HEIGHT             0x806C
#define GL_PACK_SKIP_IMAGES              0x806B
#define GL_PACK_SKIP_ROWS                0x0D03
#define GL_PACK_SKIP_PIXELS              0x0D04
#define GL_PACK_ALIGNMENT                0x0D05
#define GL_UNPACK_SWAP_BYTES             0x0CF0
#define GL_UNPACK_LSB_FIRST              0x0CF1
#define GL_UNPACK_ROW_LENGTH             0x0CF2
#define GL_UNPACK_IMAGE_HEIGHT           0x806E
#define GL_UNPACK_SKIP_IMAGES            0x806D
#define GL_UNPACK_SKIP_ROWS              0x0CF3
#define GL_UNPACK_SKIP_PIXELS            0x0CF4
#define GL_UNPACK_ALIGNMENT              0x0CF5

/* Indexed state queries */
void    glGetFloati_v   (GLenum target, GLuint index, GLfloat *data);
void    glGetDoublei_v  (GLenum target, GLuint index, GLdouble *data);

/* Vertex attrib format (4.3 / ARB_vertex_attrib_binding) */
void    glVertexBindingDivisor  (GLuint bindingindex, GLuint divisor);
void    glVertexAttribBinding   (GLuint attribindex, GLuint bindingindex);
void    glVertexAttribFormat    (GLuint attribindex, GLint size, GLenum type,
                                 GLboolean normalized, GLuint relativeoffset);
void    glVertexAttribIFormat   (GLuint attribindex, GLint size, GLenum type,
                                 GLuint relativeoffset);
void    glBindVertexBuffer      (GLuint bindingindex, GLuint buffer, GLintptr offset, GLsizei stride);

/* Miscellaneous limits */
#define GL_MAX_VERTEX_STREAMS            0x8E71
#define GL_MAX_PATCH_VERTICES            0x8E7D
#define GL_MAX_TESS_GEN_LEVEL            0x8E7E

#endif /* GLEXT_H */
