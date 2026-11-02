/******************************************************************************
 * @file    glew.h  (macOS shim)
 * @brief   Minimal GLEW-compatible surface over Apple's <OpenGL/gl.h>.
 *
 * The editor links real GLEW on Windows.  macOS ships an OpenGL framework but
 * no GLEW, and its <OpenGL/gl.h> declares the legacy (2.1) API plus the
 * extension set -- not the handful of *core-named* entry points that GLEW
 * declares for GL 3.0+:
 *
 *   glGenVertexArrays / glBindVertexArray / glDeleteVertexArrays
 *   glBindBufferBase / glGetUniformBlockIndex / glUniformBlockBinding
 *   glDepthRangef / glBindTextureUnit
 *
 * Those are declared here as GLEW declares them: as function *pointers*.  That
 * is load-bearing, not stylistic -- source/renderer/gl_helper.cpp assigns to
 * them (so the 2.1 fallback can substitute the APPLE variants and legacy
 * shims), which is illegal for a real function but exactly how GLEW works.
 *
 * glewInit() resolves them through dlsym() and leaves a pointer NULL when the
 * current context does not advertise the version that provides it.  That
 * mirrors GLEW and is what makes the existing 2.1 path activate on Apple's
 * legacy profile: a legacy context reports GL 2.1, so glGenVertexArrays stays
 * NULL and gl_helper.cpp's GL_APPLE_vertex_array_object fallback runs.
 *****************************************************************************/

#pragma once

#include <OpenGL/gl.h>
#include <stddef.h>

/* GLEW defines these calling-convention macros; Apple's headers do not, and
 * source/renderer/gl_helper.cpp uses GLAPIENTRY in its fallback shims. */
#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif
#ifndef GLAPI
#define GLAPI
#endif
#ifndef APIENTRY
#define APIENTRY
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* --- GLEW status codes / entry points ------------------------------------ */
#define GLEW_OK 0
#define GLEW_ERROR_NO_GL_VERSION 1
#define GLEW_ERROR_GL_VERSION_10_ONLY 2
#define GLEW_ERROR_GLX_VERSION_11_ONLY 3
#define GLEW_ERROR_NO_GLX_DISPLAY 4
#define GLEW_VERSION 1
#define GLEW_VERSION_MAJOR 2
#define GLEW_VERSION_MINOR 0


const GLubyte *glewGetErrorString(GLenum error);
GLenum glewInit(void);

/* GLEW answers "is this supported" from the advertised GL version.  The editor
 * only asks about core versions: GL_VERSION_3_0, GL_VERSION_4_5. */
GLboolean glewIsSupported(const char *name);

/* Set by glewInit() when the context exposes GL_APPLE_vertex_array_object.
 * gl_helper.cpp tests it before substituting the APPLE VAO entry points. */
extern GLboolean GLEW_APPLE_vertex_array_object;

/* --- Function-pointer types for the entry points Apple's headers omit ---- */
typedef void (*PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint *arrays);
typedef void (*PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void (*PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint *arrays);
typedef void (*PFNGLBINDBUFFERBASEPROC)(GLenum target, GLuint index,
                                        GLuint buffer);
typedef GLuint (*PFNGLGETUNIFORMBLOCKINDEXPROC)(GLuint program,
                                                const GLchar *uniformBlockName);
typedef void (*PFNGLUNIFORMBLOCKBINDINGPROC)(GLuint program,
                                             GLuint uniformBlockIndex,
                                             GLuint uniformBlockBinding);
typedef void (*PFNGLDEPTHRANGEFPROC)(GLfloat n, GLfloat f);
typedef void (*PFNGLBINDTEXTUREUNITPROC)(GLuint unit, GLuint texture);

/* --- The entry points themselves (NULL until glewInit resolves them) ----- */
extern PFNGLGENVERTEXARRAYSPROC glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC glBindVertexArray;
extern PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays;
extern PFNGLBINDBUFFERBASEPROC glBindBufferBase;
extern PFNGLGETUNIFORMBLOCKINDEXPROC glGetUniformBlockIndex;
extern PFNGLUNIFORMBLOCKBINDINGPROC glUniformBlockBinding;
extern PFNGLDEPTHRANGEFPROC glDepthRangef;
extern PFNGLBINDTEXTUREUNITPROC glBindTextureUnit;

#ifdef __cplusplus
}
#endif

/* --- Core constants Apple's legacy <OpenGL/gl.h> does not define ----------
 * Values are from the OpenGL registry.  They exist so tier-selection code
 * compiles; on a 2.1 context the renderer's fallback never issues the
 * corresponding state, it emulates it (see source/renderer/gl_helper.cpp).
 */
#ifndef GL_UNIFORM_BUFFER
#define GL_UNIFORM_BUFFER 0x8A11
#endif
#ifndef GL_GEOMETRY_SHADER
#define GL_GEOMETRY_SHADER 0x8DD9
#endif
#ifndef GL_TESS_EVALUATION_SHADER
#define GL_TESS_EVALUATION_SHADER 0x8E87
#endif
#ifndef GL_TESS_CONTROL_SHADER
#define GL_TESS_CONTROL_SHADER 0x8E88
#endif
#ifndef GL_INVALID_INDEX
#define GL_INVALID_INDEX 0xFFFFFFFFu
#endif
