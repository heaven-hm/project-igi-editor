/******************************************************************************
 * @file    gl_helper.h
 * @brief   gl wrapper
 *****************************************************************************/

#pragma once

/*
================================================================================
GL Helper
================================================================================
*/

struct gl_info_s {
	bool	support_version_45_;

	// True when the context is too old for this renderer's native shader tier
	// (GLSL 1.50 / GL 3.0 and up): no uniform buffer objects, no core vertex
	// array objects.  Apple's legacy profile -- the only context Wine offers
	// on macOS -- reports GL_VERSION 2.1 / GLSL 1.20 and lands here.  When
	// set, GL_Init() repairs GLEW's NULL entry points and the uniform blocks
	// are emulated with plain uniforms; see the compatibility block in
	// gl_helper.cpp and docs/igi-re-parity/GL21_FALLBACK.md.
	bool	legacy_gl21_;
} ;

extern gl_info_s g_gl_info;

bool	GL_Init();
void	GL_TryEnableVSync();

// AMD atioglxx.dll dereferences NULL+0x444 if glBegin runs with a VBO/VAO still
// bound. Call this immediately before any fixed-function immediate-mode draw,
// and after any mesh upload that may have rebound ARRAY_BUFFER.
void	GL_UnbindForImmediateMode();

// buf
GLuint	GL_CreateBuffer(GLenum target, GLsizeiptr size, const void * data, GLenum usage);
void	GL_BufferData(GLuint obj, GLenum target, GLsizeiptr size, const void* data, GLenum usage);
// Partial update.  For GL_UNIFORM_BUFFER on a 2.1 context this writes into the
// emulated block's cache instead of the (non-existent) buffer object.
void	GL_BufferSubData(GLuint obj, GLenum target, GLintptr offset, GLsizeiptr size, const void* data);
void	GL_DeleteBuffer(GLuint& obj);

// vao
void	GL_DeleteVertexArray(GLuint& obj);

// texture
GLuint	GL_RegisterTexture(const pic_s* pic, GLint wrap_mode, GLint min_filter, GLint mag_filter, bool gen_mipmap);
void	GL_DeleteTexture(GLuint& obj);
void	GL_BindTexture2D(GLuint unit, GLuint texture);

// shader
enum gl_shader_t {
	VERTEX_SHADER,
	TESSELLATION_CONTROL_SHADER,
	TESSELLATION_EVALUATION_SHADER,
	GEOMETRY_SHADER,
	FRAGMENT_SHADER,

	SUPPORT_SHADER_COUNT
};

struct gl_program_s {
	GLuint					program_;
	GLuint					shaders_[SUPPORT_SHADER_COUNT];

	gl_program_s() :program_(0) {
		memset(shaders_, 0, sizeof(shaders_));
	}
};

bool	GL_CreateProgram(const char* shader_full_filenames[SUPPORT_SHADER_COUNT], gl_program_s& gl_prog);
void	GL_DeleteProgram(gl_program_s& gl_prog);
bool	GL_CreateProgramVSFS(const char* vs, const char* fs, gl_program_s& gl_prog);
bool	GL_SetUniformBlockBinding(gl_program_s& gl_prog, const char * uniform_block_name, GLuint binding_point);

// GLSL 1.20 has no layout(location = N).  Programs built from the inline
// shader strings in the renderer must call this with the program id BEFORE
// glLinkProgram when g_gl_info.legacy_gl21_ is set.  No-op otherwise.
void	GL_LegacyBindInlineAttribLocations(GLuint program);

// check gl error
int		GL_CheckError(const char * file, int line);

#define GL_CHECK_ERROR	GL_CheckError(__FILE__, __LINE__)
