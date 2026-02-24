/******************************************************************************
 * @file    gl_helper.cpp
 * @brief   gl wrapper
 *****************************************************************************/

#include "pch.h"

#if defined(_WIN32)
# include <wglext.h>
#endif

#if defined(__linux__)
# include <GL/glx.h>
# include <glxext.h>
# include <X11/Xlib.h>
#endif

/*
================================================================================
GL Helper
================================================================================
*/

#if defined(_WIN32)
static PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT;
#endif

#if defined(__linux__)
static PFNGLXSWAPINTERVALEXTPROC glXSwapIntervalEXT;
#endif

gl_info_s g_gl_info;

bool GL_Init() {
	if (glewInit() != GLEW_OK) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "glewInit error\n");
		return false;
	}

	printf("GL_VENDOR:                   %s\n", glGetString(GL_VENDOR));
	printf("GL_RENDERER:                 %s\n", glGetString(GL_RENDERER));
	printf("GL_VERSION:                  %s\n", glGetString(GL_VERSION));
	printf("GL_SHADING_LANGUAGE_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

	// requires OpenGL 4.5 and above or ARB_direct_state_access to support glBindTextureUnit
	g_gl_info.support_version_45_ = glewIsSupported("GL_VERSION_4_5") == GL_TRUE;

#if defined(_WIN32)
	// Find the extension function
	wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
#endif

#if defined(__linux__)
	glXSwapIntervalEXT = (PFNGLXSWAPINTERVALEXTPROC)glXGetProcAddress((const GLubyte*)"glXSwapIntervalEXT");
#endif

	return true;
}

void GL_TryEnableVSync() {
#if defined(_WIN32)
	if (wglSwapIntervalEXT) {
		wglSwapIntervalEXT(1);	// enable v-sync
		printf("VSync enabled\n");
	}
#endif

#if defined(__linux__)
	if (glXSwapIntervalEXT) {
		Display * display = glXGetCurrentDisplay();
		GLXDrawable drawable = glXGetCurrentDrawable();
		if (drawable) {
			glXSwapIntervalEXT(display, drawable, 1);
			printf("VSync enabled\n");
		}
	}
#endif
}

// buf
GLuint GL_CreateBuffer(GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
	GLuint obj = 0;
	glGenBuffers(1, &obj);
	GL_BufferData(obj, target, size, data, usage);
	return obj;
}

void GL_BufferData(GLuint obj, GLenum target, GLsizeiptr size, const void* data, GLenum usage) {
	glBindBuffer(target, obj);
	glBufferData(target, size, data, usage);
	glBindBuffer(target, 0);
}

void GL_DeleteBuffer(GLuint& obj) {
	if (obj) {
		glDeleteBuffers(1, &obj);
		obj = 0;
	}
}

// vao
void GL_DeleteVertexArray(GLuint& obj) {
	if (obj) {
		glDeleteVertexArrays(1, &obj);
		obj = 0;
	}
}

// texture
GLuint GL_RegisterTexture(const pic_s* pic, GLint wrap_mode, GLint min_filter, GLint mag_filter, bool gen_mipmap)
{
	GLuint t = 0;
	glGenTextures(1, &t);
	glBindTexture(GL_TEXTURE_2D, t);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_mode);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap_mode);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, min_filter);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, mag_filter);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
		pic->width_, pic->height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, pic->pixels_);

	if (gen_mipmap) {
		glGenerateMipmap(GL_TEXTURE_2D);
	}

	return t;
}

void GL_DeleteTexture(GLuint& obj) {
	if (obj) {
		glDeleteTextures(1, &obj);
		obj = 0;
	}
}

void GL_BindTexture2D(GLuint unit, GLuint texture) {
	if (g_gl_info.support_version_45_) {
		glBindTextureUnit(unit, texture);
	}
	else {
		glActiveTexture(GL_TEXTURE0 + unit);
		glBindTexture(GL_TEXTURE_2D, texture);
	}
}

// shader
static GLuint LoadShaderFromFile(const char* filename, GLenum shader_type) {
	GLuint shader = glCreateShader(shader_type);
	if (!shader) {
		return 0;
	}

	char* shader_src = nullptr;
	if (!File_LoadText(filename, shader_src)) {
		glDeleteShader(shader);
		return 0;
	}

	glShaderSource(shader, 1, &shader_src, 0);
	glCompileShader(shader);

	File_FreeBuf(shader_src);

	// check compile status
	GLint compile_status = 0;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);

	// check compile result
	if (compile_status == 1) {
		return shader;
	}
	else {
		GLint log_len = 0;
		glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
		if (log_len) {
			char* buf = (char*)MEM_ALLOC(log_len);
			memset(buf, 0, log_len);

			glGetShaderInfoLog(shader, log_len, &log_len, buf);
			Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "shader \"%s\" compile error: %s\n", filename, buf);
		
			MEM_FREE_(buf);
		}

		glDeleteShader(shader);

		return 0;
	}
}

bool GL_CreateProgram(const char* shader_full_filenames[SUPPORT_SHADER_COUNT], gl_program_s& gl_prog) {
	static const GLenum GL_SHADERS[] = {
		GL_VERTEX_SHADER,
		GL_TESS_CONTROL_SHADER,
		GL_TESS_EVALUATION_SHADER,
		GL_GEOMETRY_SHADER,
		GL_FRAGMENT_SHADER
	};

	memset(&gl_prog, 0, sizeof(gl_prog));

	GLuint program = glCreateProgram();

	GLuint shaders[SUPPORT_SHADER_COUNT] = {};

	for (int i = 0; i < SUPPORT_SHADER_COUNT; ++i) {
		if (shader_full_filenames[i]) {
			shaders[i] = LoadShaderFromFile(shader_full_filenames[i], GL_SHADERS[i]);

			if (!shaders[i]) {
				for (int j = 0; j < i; ++j) {
					if (shaders[j]) {
						glDeleteShader(shaders[j]);
					}
				}

				glDeleteProgram(program);
				return false;
			}
		}

	}

	for (int i = 0; i < SUPPORT_SHADER_COUNT; ++i) {
		if (shaders[i]) {
			glAttachShader(program, shaders[i]);
		}
	}

	glLinkProgram(program);

	// check link status
	GLint link_status = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &link_status);

	if (link_status == 1) {

		gl_prog.program_ = program;

		for (int i = 0; i < SUPPORT_SHADER_COUNT; ++i) {
			gl_prog.shaders_[i] = shaders[i];
		}

		return true;
	}
	else {
		GLint log_len = 0;
		glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
		if (log_len) {
			char* buf = (char*)MEM_ALLOC(log_len);
			memset(buf, 0, log_len);

			glGetProgramInfoLog(program, log_len, &log_len, buf);
			Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "program link error: %s,\n", buf);

			MEM_FREE_(buf);

			for (int i = 0; i < SUPPORT_SHADER_COUNT; ++i) {
				if (shader_full_filenames[i]) {
					Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "shader: %s\n", shader_full_filenames[i]);
				}
			}
		}

		for (int i = 0; i < SUPPORT_SHADER_COUNT; ++i) {
			if (shaders[i]) {
				glDetachShader(program, shaders[i]);
				glDeleteShader(shaders[i]);
			}
		}

		glDeleteProgram(program);

		return false;
	}
}

void GL_DeleteProgram(gl_program_s& gl_prog) {
	if (gl_prog.program_) {
		for (int i = 0; i < SUPPORT_SHADER_COUNT; ++i) {
			if (gl_prog.shaders_[i]) {
				glDetachShader(gl_prog.program_, gl_prog.shaders_[i]);
				glDeleteShader(gl_prog.shaders_[i]);
				gl_prog.shaders_[i] = 0;
			}
		}

		glDeleteProgram(gl_prog.program_);
		gl_prog.program_ = 0;
	}
}

bool GL_CreateProgramVSFS(const char* vs, const char* fs, gl_program_s& gl_prog)
{
	char full_vs_filename[1024];
	char full_fs_filename[1024];

	const char* shader_full_filenames[SUPPORT_SHADER_COUNT] = {};

	Str_SPrintf(full_vs_filename, 1024, "%s/%s", g_folders.shader_folder_, vs);
	Str_SPrintf(full_fs_filename, 1024, "%s/%s", g_folders.shader_folder_, fs);

	shader_full_filenames[VERTEX_SHADER] = full_vs_filename;
	shader_full_filenames[FRAGMENT_SHADER] = full_fs_filename;

	return GL_CreateProgram(shader_full_filenames, gl_prog);
}

bool GL_SetUniformBlockBinding(gl_program_s& gl_prog, const char* uniform_block_name, GLuint binding_point) {
	GLuint idx = glGetUniformBlockIndex(gl_prog.program_, uniform_block_name);
	if (idx == GL_INVALID_INDEX) {
		return false;
	}

	glUniformBlockBinding(gl_prog.program_, idx, binding_point);
	return true;
}

// error
static const char* GLErrToStr(GLenum e) {
	switch (e) {
	case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
	case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
	case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
	case GL_INVALID_FRAMEBUFFER_OPERATION:return "GL_INVALID_FRAMEBUFFER_OPERATION";
	case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
	case GL_STACK_UNDERFLOW: return "GL_STACK_UNDERFLOW";
	case GL_STACK_OVERFLOW: return "GL_STACK_OVERFLOW";
	default:
		return "unknown gl error";
	}
}

int GL_CheckError(const char* file, int line) {
	int error_count = 0;
	GLenum r = GL_NO_ERROR;
	do {
		r = glGetError();
		if (r != GL_NO_ERROR) {
			error_count++;
			const char* err = GLErrToStr(r);
			printf("GLError: %s [%s:%d]\n",
				err, file, line);
		}

	} while (r != GL_NO_ERROR);

	return error_count;
}
