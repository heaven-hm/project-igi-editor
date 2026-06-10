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
	bool	support_version_41_;	// OpenGL 4.1+ (Core Profile — shaders/41)
	bool	legacy_glsl_;			// true when only OpenGL 2.1 / GLSL 1.20 is available (Wine/Mac)
} ;

extern gl_info_s g_gl_info;

bool	GL_Init();
void	GL_TryEnableVSync();

// buf
GLuint	GL_CreateBuffer(GLenum target, GLsizeiptr size, const void * data, GLenum usage);
void	GL_BufferData(GLuint obj, GLenum target, GLsizeiptr size, const void* data, GLenum usage);
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

// check gl error
int		GL_CheckError(const char * file, int line);

#define GL_CHECK_ERROR	GL_CheckError(__FILE__, __LINE__)
