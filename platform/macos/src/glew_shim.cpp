/******************************************************************************
 * @file    glew_shim.cpp
 * @brief   glewInit()/glewIsSupported() over Apple's OpenGL framework.
 *
 * Resolution rules, chosen to preserve the renderer's existing behaviour:
 *
 *   * A GL 3.0+ context gets the core entry points.  A GL 4.5 context also gets
 *     glBindTextureUnit (4.5 / ARB_direct_state_access).
 *   * Anything older -- Apple's legacy profile reports 2.1 -- leaves them NULL,
 *     which is exactly what GLEW does and what activates the GL21 fallback in
 *     source/renderer/gl_helper.cpp (APPLE VAO entry points + emulated blocks).
 *
 * Symbols are looked up with dlsym() rather than by taking their address so the
 * binary links on an SDK that does not declare them (see include/glew.h).
 *****************************************************************************/

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <OpenGL/gl.h>

#include "glew.h"

PFNGLGENVERTEXARRAYSPROC glGenVertexArrays = NULL;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray = NULL;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays = NULL;
PFNGLBINDBUFFERBASEPROC glBindBufferBase = NULL;
PFNGLGETUNIFORMBLOCKINDEXPROC glGetUniformBlockIndex = NULL;
PFNGLUNIFORMBLOCKBINDINGPROC glUniformBlockBinding = NULL;
PFNGLDEPTHRANGEFPROC glDepthRangef = NULL;
PFNGLBINDTEXTUREUNITPROC glBindTextureUnit = NULL;

GLboolean GLEW_APPLE_vertex_array_object = GL_FALSE;

static int g_gl_major = 0;
static int g_gl_minor = 0;
static int g_resolved = 0;

static void *lookup(const char *name) {
    void *p = dlsym(RTLD_DEFAULT, name);
    if (!p) {
        p = dlsym(RTLD_NEXT, name);
    }
    return p;
}

static void parse_version(void) {
    const GLubyte *v = glGetString(GL_VERSION);
    g_gl_major = 0;
    g_gl_minor = 0;
    if (!v) {
        return;
    }
    /* "4.1 Metal - 89.3" / "2.1 Metal - 89.3" / "4.1 ATI-..." */
    int n = sscanf((const char *)v, "%d.%d", &g_gl_major, &g_gl_minor);
    if (n < 1) {
        g_gl_major = 0;
        g_gl_minor = 0;
    }
}

static GLboolean at_least(int major, int minor) {
    if (g_gl_major != major) {
        return g_gl_major > major ? GL_TRUE : GL_FALSE;
    }
    return g_gl_minor >= minor ? GL_TRUE : GL_FALSE;
}

GLenum glewInit(void) {
    if (g_resolved) {
        return GLEW_OK;
    }
    parse_version();
    if (g_gl_major == 0) {
        return GLEW_ERROR_NO_GL_VERSION;
    }
    if (g_gl_major == 1 && g_gl_minor < 2) {
        return GLEW_ERROR_GL_VERSION_10_ONLY;
    }

    GLEW_APPLE_vertex_array_object =
        lookup("glGenVertexArraysAPPLE") ? GL_TRUE : GL_FALSE;

    if (at_least(3, 0)) {
        glGenVertexArrays = (PFNGLGENVERTEXARRAYSPROC)lookup("glGenVertexArrays");
        glBindVertexArray = (PFNGLBINDVERTEXARRAYPROC)lookup("glBindVertexArray");
        glDeleteVertexArrays =
            (PFNGLDELETEVERTEXARRAYSPROC)lookup("glDeleteVertexArrays");
        glBindBufferBase = (PFNGLBINDBUFFERBASEPROC)lookup("glBindBufferBase");
        glGetUniformBlockIndex =
            (PFNGLGETUNIFORMBLOCKINDEXPROC)lookup("glGetUniformBlockIndex");
        glUniformBlockBinding =
            (PFNGLUNIFORMBLOCKBINDINGPROC)lookup("glUniformBlockBinding");
        glDepthRangef = (PFNGLDEPTHRANGEFPROC)lookup("glDepthRangef");
    }
    if (at_least(4, 5)) {
        glBindTextureUnit =
            (PFNGLBINDTEXTUREUNITPROC)lookup("glBindTextureUnit");
    }

    g_resolved = 1;
    return GLEW_OK;
}

GLboolean glewIsSupported(const char *name) {
    /* The editor only asks about core versions ("GL_VERSION_3_0",
     * "GL_VERSION_4_5").  Anything else is reported unsupported rather than
     * guessed, so a caller cannot silently take a path this shim did not
     * verify. */
    if (!name || strncmp(name, "GL_VERSION_", 11) != 0) {
        return GL_FALSE;
    }
    int major = 0;
    int minor = 0;
    if (sscanf(name + 11, "%d_%d", &major, &minor) != 2) {
        return GL_FALSE;
    }
    return at_least(major, minor);
}

const GLubyte *glewGetErrorString(GLenum error) {
    switch (error) {
    case GLEW_OK:
        return (const GLubyte *)"No error";
    case GLEW_ERROR_NO_GL_VERSION:
        return (const GLubyte *)"Missing GL version";
    case GLEW_ERROR_GL_VERSION_10_ONLY:
        return (const GLubyte *)"Need at least OpenGL 1.2";
    default:
        return (const GLubyte *)"Unknown error";
    }
}
