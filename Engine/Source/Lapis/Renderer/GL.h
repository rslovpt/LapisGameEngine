#pragma once
// Lapis' own OpenGL loader (no GLAD/GLEW). Declares the GL types, constants
// and function pointers the engine uses, inside namespace Lapis so they never
// collide with a system libGL. Call LoadOpenGL() once a context is current.

#include <cstddef>

#if defined(_WIN32) && !defined(_WIN64)
    #define LAPIS_APIENTRY __stdcall
#else
    #define LAPIS_APIENTRY
#endif

namespace Lapis {

using GLenum     = unsigned int;
using GLboolean  = unsigned char;
using GLbitfield = unsigned int;
using GLvoid     = void;
using GLbyte     = signed char;
using GLubyte    = unsigned char;
using GLshort    = short;
using GLushort   = unsigned short;
using GLint      = int;
using GLuint     = unsigned int;
using GLsizei    = int;
using GLfloat    = float;
using GLdouble   = double;
using GLchar     = char;
using GLintptr   = std::ptrdiff_t;
using GLsizeiptr = std::ptrdiff_t;

using GLDEBUGPROC = void (LAPIS_APIENTRY*)(GLenum source, GLenum type, GLuint id, GLenum severity,
                                           GLsizei length, const GLchar* message, const void* userParam);

// --- constants -------------------------------------------------------------
constexpr GLenum GL_FALSE = 0, GL_TRUE = 1, GL_NONE = 0, GL_ZERO = 0, GL_ONE = 1, GL_NO_ERROR = 0;
constexpr GLbitfield GL_DEPTH_BUFFER_BIT = 0x0100, GL_STENCIL_BUFFER_BIT = 0x0400, GL_COLOR_BUFFER_BIT = 0x4000;
constexpr GLenum GL_POINTS = 0x0000, GL_LINES = 0x0001, GL_LINE_STRIP = 0x0003, GL_TRIANGLES = 0x0004, GL_TRIANGLE_STRIP = 0x0005;
constexpr GLenum GL_BYTE = 0x1400, GL_UNSIGNED_BYTE = 0x1401, GL_SHORT = 0x1402, GL_UNSIGNED_SHORT = 0x1403,
                 GL_INT = 0x1404, GL_UNSIGNED_INT = 0x1405, GL_FLOAT = 0x1406, GL_HALF_FLOAT = 0x140B;
constexpr GLenum GL_NEVER = 0x0200, GL_LESS = 0x0201, GL_EQUAL = 0x0202, GL_LEQUAL = 0x0203, GL_GREATER = 0x0204,
                 GL_ALWAYS = 0x0207;
constexpr GLenum GL_SRC_ALPHA = 0x0302, GL_ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum GL_FRONT = 0x0404, GL_BACK = 0x0405, GL_FRONT_AND_BACK = 0x0408, GL_CW = 0x0900, GL_CCW = 0x0901;
constexpr GLenum GL_CULL_FACE = 0x0B44, GL_DEPTH_TEST = 0x0B71, GL_BLEND = 0x0BE2, GL_LINE_SMOOTH = 0x0B20,
                 GL_POLYGON_OFFSET_FILL = 0x8037, GL_MULTISAMPLE = 0x809D, GL_FRAMEBUFFER_SRGB = 0x8DB9,
                 GL_TEXTURE_CUBE_MAP_SEAMLESS = 0x884F, GL_SCISSOR_TEST = 0x0C11;
constexpr GLenum GL_LINE = 0x1B01, GL_FILL = 0x1B02;
constexpr GLenum GL_VENDOR = 0x1F00, GL_RENDERER = 0x1F01, GL_VERSION = 0x1F02, GL_EXTENSIONS = 0x1F03,
                 GL_SHADING_LANGUAGE_VERSION = 0x8B8C, GL_MAJOR_VERSION = 0x821B, GL_MINOR_VERSION = 0x821C,
                 GL_NUM_EXTENSIONS = 0x821D, GL_MAX_TEXTURE_SIZE = 0x0D33, GL_MAX_SAMPLES = 0x8D57;
constexpr GLenum GL_UNPACK_ALIGNMENT = 0x0CF5, GL_PACK_ALIGNMENT = 0x0D05;
constexpr GLenum GL_ARRAY_BUFFER = 0x8892, GL_ELEMENT_ARRAY_BUFFER = 0x8893, GL_UNIFORM_BUFFER = 0x8A11;
constexpr GLenum GL_STREAM_DRAW = 0x88E0, GL_STATIC_DRAW = 0x88E4, GL_DYNAMIC_DRAW = 0x88E8;
constexpr GLenum GL_FRAGMENT_SHADER = 0x8B30, GL_VERTEX_SHADER = 0x8B31, GL_GEOMETRY_SHADER = 0x8DD9,
                 GL_COMPILE_STATUS = 0x8B81, GL_LINK_STATUS = 0x8B82, GL_INFO_LOG_LENGTH = 0x8B84;
constexpr GLenum GL_TEXTURE_2D = 0x0DE1, GL_TEXTURE_CUBE_MAP = 0x8513, GL_TEXTURE_CUBE_MAP_POSITIVE_X = 0x8515,
                 GL_TEXTURE0 = 0x84C0;
constexpr GLenum GL_TEXTURE_MAG_FILTER = 0x2800, GL_TEXTURE_MIN_FILTER = 0x2801, GL_TEXTURE_WRAP_S = 0x2802,
                 GL_TEXTURE_WRAP_T = 0x2803, GL_TEXTURE_WRAP_R = 0x8072, GL_TEXTURE_BORDER_COLOR = 0x1004,
                 GL_TEXTURE_COMPARE_MODE = 0x884C, GL_TEXTURE_COMPARE_FUNC = 0x884D,
                 GL_COMPARE_REF_TO_TEXTURE = 0x884E, GL_TEXTURE_MAX_ANISOTROPY = 0x84FE;
constexpr GLenum GL_NEAREST = 0x2600, GL_LINEAR = 0x2601, GL_NEAREST_MIPMAP_NEAREST = 0x2700,
                 GL_LINEAR_MIPMAP_NEAREST = 0x2701, GL_NEAREST_MIPMAP_LINEAR = 0x2702,
                 GL_LINEAR_MIPMAP_LINEAR = 0x2703;
constexpr GLenum GL_REPEAT = 0x2901, GL_CLAMP_TO_EDGE = 0x812F, GL_CLAMP_TO_BORDER = 0x812D, GL_MIRRORED_REPEAT = 0x8370;
constexpr GLenum GL_RED = 0x1903, GL_RG = 0x8227, GL_RGB = 0x1907, GL_RGBA = 0x1908, GL_BGR = 0x80E0, GL_BGRA = 0x80E1,
                 GL_DEPTH_COMPONENT = 0x1902, GL_DEPTH_STENCIL = 0x84F9;
constexpr GLenum GL_R8 = 0x8229, GL_RG8 = 0x822B, GL_RGB8 = 0x8051, GL_RGBA8 = 0x8058, GL_SRGB8 = 0x8C41,
                 GL_SRGB8_ALPHA8 = 0x8C43, GL_R16F = 0x822D, GL_RG16F = 0x822F, GL_RGB16F = 0x881B,
                 GL_RGBA16F = 0x881A, GL_RGBA32F = 0x8814, GL_DEPTH_COMPONENT24 = 0x81A6,
                 GL_DEPTH_COMPONENT32F = 0x8CAC, GL_DEPTH24_STENCIL8 = 0x88F0;
constexpr GLenum GL_FRAMEBUFFER = 0x8D40, GL_READ_FRAMEBUFFER = 0x8CA8, GL_DRAW_FRAMEBUFFER = 0x8CA9,
                 GL_RENDERBUFFER = 0x8D41, GL_COLOR_ATTACHMENT0 = 0x8CE0, GL_DEPTH_ATTACHMENT = 0x8D00,
                 GL_DEPTH_STENCIL_ATTACHMENT = 0x821A, GL_FRAMEBUFFER_COMPLETE = 0x8CD5;
constexpr GLenum GL_DEBUG_OUTPUT = 0x92E0, GL_DEBUG_OUTPUT_SYNCHRONOUS = 0x8242,
                 GL_DEBUG_SEVERITY_HIGH = 0x9146, GL_DEBUG_SEVERITY_MEDIUM = 0x9147,
                 GL_DEBUG_SEVERITY_LOW = 0x9148, GL_DEBUG_SEVERITY_NOTIFICATION = 0x826B;

// --- functions ---------------------------------------------------------------
// X(return type, name, parameter list)
#define LAPIS_GL_FUNCTIONS(X)                                                                          \
    X(const GLubyte*, glGetString, (GLenum name))                                                      \
    X(const GLubyte*, glGetStringi, (GLenum name, GLuint index))                                        \
    X(void, glGetIntegerv, (GLenum pname, GLint* data))                                                \
    X(GLenum, glGetError, ())                                                                          \
    X(void, glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))                                      \
    X(void, glScissor, (GLint x, GLint y, GLsizei w, GLsizei h))                                       \
    X(void, glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a))                                \
    X(void, glClearDepth, (GLdouble d))                                                                \
    X(void, glClear, (GLbitfield mask))                                                                \
    X(void, glEnable, (GLenum cap))                                                                    \
    X(void, glDisable, (GLenum cap))                                                                   \
    X(void, glDepthFunc, (GLenum func))                                                                \
    X(void, glDepthMask, (GLboolean flag))                                                             \
    X(void, glColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a))                         \
    X(void, glCullFace, (GLenum mode))                                                                 \
    X(void, glFrontFace, (GLenum mode))                                                                \
    X(void, glBlendFunc, (GLenum s, GLenum d))                                                         \
    X(void, glPolygonOffset, (GLfloat factor, GLfloat units))                                          \
    X(void, glPolygonMode, (GLenum face, GLenum mode))                                                 \
    X(void, glLineWidth, (GLfloat width))                                                              \
    X(void, glPixelStorei, (GLenum pname, GLint param))                                                \
    X(void, glReadPixels, (GLint x, GLint y, GLsizei w, GLsizei h, GLenum fmt, GLenum type, void* px)) \
    X(void, glFinish, ())                                                                              \
    X(void, glDrawArrays, (GLenum mode, GLint first, GLsizei count))                                   \
    X(void, glDrawElements, (GLenum mode, GLsizei count, GLenum type, const void* indices))            \
    X(void, glGenBuffers, (GLsizei n, GLuint* buffers))                                                \
    X(void, glDeleteBuffers, (GLsizei n, const GLuint* buffers))                                       \
    X(void, glBindBuffer, (GLenum target, GLuint buffer))                                              \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage))            \
    X(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data))      \
    X(void, glGenVertexArrays, (GLsizei n, GLuint* arrays))                                            \
    X(void, glDeleteVertexArrays, (GLsizei n, const GLuint* arrays))                                   \
    X(void, glBindVertexArray, (GLuint array))                                                         \
    X(void, glEnableVertexAttribArray, (GLuint index))                                                 \
    X(void, glVertexAttribPointer, (GLuint i, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void* ptr)) \
    X(void, glVertexAttribIPointer, (GLuint i, GLint size, GLenum type, GLsizei stride, const void* ptr)) \
    X(GLuint, glCreateShader, (GLenum type))                                                           \
    X(void, glDeleteShader, (GLuint shader))                                                           \
    X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar* const* str, const GLint* len)) \
    X(void, glCompileShader, (GLuint shader))                                                          \
    X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint* params))                               \
    X(void, glGetShaderInfoLog, (GLuint shader, GLsizei max, GLsizei* len, GLchar* log))               \
    X(GLuint, glCreateProgram, ())                                                                     \
    X(void, glDeleteProgram, (GLuint program))                                                         \
    X(void, glAttachShader, (GLuint program, GLuint shader))                                           \
    X(void, glDetachShader, (GLuint program, GLuint shader))                                           \
    X(void, glLinkProgram, (GLuint program))                                                           \
    X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint* params))                             \
    X(void, glGetProgramInfoLog, (GLuint program, GLsizei max, GLsizei* len, GLchar* log))             \
    X(void, glUseProgram, (GLuint program))                                                            \
    X(GLint, glGetUniformLocation, (GLuint program, const GLchar* name))                               \
    X(void, glUniform1i, (GLint loc, GLint v))                                                         \
    X(void, glUniform1f, (GLint loc, GLfloat v))                                                       \
    X(void, glUniform2f, (GLint loc, GLfloat x, GLfloat y))                                            \
    X(void, glUniform3f, (GLint loc, GLfloat x, GLfloat y, GLfloat z))                                 \
    X(void, glUniform4f, (GLint loc, GLfloat x, GLfloat y, GLfloat z, GLfloat w))                      \
    X(void, glUniformMatrix4fv, (GLint loc, GLsizei count, GLboolean transpose, const GLfloat* v))     \
    X(void, glGenTextures, (GLsizei n, GLuint* textures))                                              \
    X(void, glDeleteTextures, (GLsizei n, const GLuint* textures))                                     \
    X(void, glBindTexture, (GLenum target, GLuint texture))                                            \
    X(void, glActiveTexture, (GLenum unit))                                                            \
    X(void, glTexImage2D, (GLenum target, GLint level, GLint ifmt, GLsizei w, GLsizei h, GLint border, GLenum fmt, GLenum type, const void* px)) \
    X(void, glTexParameteri, (GLenum target, GLenum pname, GLint param))                               \
    X(void, glTexParameterf, (GLenum target, GLenum pname, GLfloat param))                             \
    X(void, glTexParameterfv, (GLenum target, GLenum pname, const GLfloat* params))                    \
    X(void, glGenerateMipmap, (GLenum target))                                                         \
    X(void, glGenFramebuffers, (GLsizei n, GLuint* fbos))                                              \
    X(void, glDeleteFramebuffers, (GLsizei n, const GLuint* fbos))                                     \
    X(void, glBindFramebuffer, (GLenum target, GLuint fbo))                                            \
    X(void, glFramebufferTexture2D, (GLenum target, GLenum attach, GLenum textarget, GLuint tex, GLint level)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum target))                                               \
    X(void, glDrawBuffer, (GLenum buf))                                                                \
    X(void, glReadBuffer, (GLenum buf))                                                                \
    X(void, glGenRenderbuffers, (GLsizei n, GLuint* rbos))                                             \
    X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint* rbos))                                    \
    X(void, glBindRenderbuffer, (GLenum target, GLuint rbo))                                           \
    X(void, glRenderbufferStorage, (GLenum target, GLenum fmt, GLsizei w, GLsizei h))                  \
    X(void, glRenderbufferStorageMultisample, (GLenum target, GLsizei samples, GLenum fmt, GLsizei w, GLsizei h)) \
    X(void, glFramebufferRenderbuffer, (GLenum target, GLenum attach, GLenum rbtarget, GLuint rbo))   \
    X(void, glBlitFramebuffer, (GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter))

// Optional (may be null on older drivers).
#define LAPIS_GL_OPTIONAL_FUNCTIONS(X)                                                                 \
    X(void, glDebugMessageCallback, (GLDEBUGPROC callback, const void* userParam))

#define LAPIS_GL_DECLARE(ret, name, params)                                                            \
    using PFN_##name = ret (LAPIS_APIENTRY*) params;                                                   \
    extern PFN_##name name;
LAPIS_GL_FUNCTIONS(LAPIS_GL_DECLARE)
LAPIS_GL_OPTIONAL_FUNCTIONS(LAPIS_GL_DECLARE)
#undef LAPIS_GL_DECLARE

using GLProcLoader = void* (*)(const char* name);

// Resolves every function through `loader`. Returns false (and logs which
// functions are missing) if a required function could not be found.
bool LoadOpenGL(GLProcLoader loader);

// Logs vendor/renderer/version and installs the debug callback if available.
void LogOpenGLInfo();
void EnableOpenGLDebugOutput();

} // namespace Lapis
