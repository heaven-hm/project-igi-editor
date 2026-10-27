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

/*
================================================================================
GL 2.1 compatibility

Apple's legacy profile is the only OpenGL context Wine offers on macOS.  It
reports GL_VERSION "2.1 Metal - 89.3" and GLSL 1.20, and Wine returns NULL for
both a 4.1 core and a 4.1 compatibility context request, so the shaders/41 and
shaders/45 tiers can never compile there.  A probe of that context under Wine
11.0 (docs/igi-re-parity/GL21_FALLBACK.md) reported:

    glGenVertexArrays / glBindVertexArray / glDeleteVertexArrays   NULL
    GL_APPLE_vertex_array_object                                   YES
    glUniformBlockBinding / glGetUniformBlockIndex                 NULL
    glBindBufferBase                                               NULL
    GL_ARB_uniform_buffer_object                                   no
    glGenFramebuffers / glGenerateMipmap / glActiveTexture         OK

Exactly two things therefore need repairing, and both are repaired here rather
than at the call sites (there are ~103 VAO calls and 26 glBindBufferBase calls):

  1. Vertex array objects.  GLEW leaves the core entry points NULL, but the
     APPLE variants are present and are the same API for our usage.  GL_Init()
     points the NULL core pointers at them, which fixes every call site at once.

  2. Uniform buffer objects.  No emulation preserves the GL semantics, so the
     matrices and fog parameters are re-uploaded as the plain uniforms that the
     shaders/21 tier declares.  GL_BufferData() keeps the bytes a UBO upload
     would have written, and the patched glBindBufferBase() pushes them to
     whichever program is current.
================================================================================
*/

#include <unordered_map>
#include <vector>

struct legacy_ubo_s {
	GLsizeiptr				size_;
	std::vector<uint8_t>	data_;
};

static std::unordered_map<GLuint, legacy_ubo_s> g_legacy_ubos;

static void LegacyUploadMat4(GLuint prog, const char* name, const uint8_t* src) {
	const GLint loc = glGetUniformLocation(prog, name);
	if (loc >= 0) {
		glUniformMatrix4fv(loc, 1, GL_FALSE, reinterpret_cast<const GLfloat*>(src));
	}
}

static void LegacyUploadVec4(GLuint prog, const char* name, const uint8_t* src) {
	const GLint loc = glGetUniformLocation(prog, name);
	if (loc >= 0) {
		glUniform4fv(loc, 1, reinterpret_cast<const GLfloat*>(src));
	}
}

static void LegacyUploadFloat(GLuint prog, const char* name, const uint8_t* src) {
	const GLint loc = glGetUniformLocation(prog, name);
	if (loc >= 0) {
		glUniform1fv(loc, 1, reinterpret_cast<const GLfloat*>(src));
	}
}

// Pushes a cached uniform block into the current program under the plain
// uniform names the shaders/21 tier declares.  The block layouts come from
// renderer.h: ubo_mats_s is three mat4 in declaration order
// (mvp_mat_follow_view_, mvp_flat_sky_layer_, mvp_objects_) and ubo_fog_s is a
// vec4 followed by a float.  Both tests are size-guarded so a program is only
// given the block it actually declares.
static void LegacyUploadUboToCurrentProgram(const legacy_ubo_s& ubo) {
	GLint prog = 0;
	glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
	if (prog == 0) {
		return;
	}

	const uint8_t* base = ubo.data_.data();
	const size_t size = ubo.data_.size();

	if (size >= 3 * 64) {
		LegacyUploadMat4(static_cast<GLuint>(prog), "g_mvp_mat_follow_view", base + 0);
		LegacyUploadMat4(static_cast<GLuint>(prog), "g_mvp_flat_sky_layer", base + 64);
		LegacyUploadMat4(static_cast<GLuint>(prog), "g_mvp_objects", base + 128);

		// The inline object/rain shaders name the same third matrix `u_mvp`
		// (`uniform Matrices { mat4 u_unused1; mat4 u_unused2; mat4 u_mvp; }`),
		// so the emulation has to answer to both names.
		LegacyUploadMat4(static_cast<GLuint>(prog), "u_mvp", base + 128);
	}

	if (size >= 20) {
		LegacyUploadVec4(static_cast<GLuint>(prog), "g_fog_color", base + 0);
		LegacyUploadFloat(static_cast<GLuint>(prog), "g_fog_far", base + 16);
	}
}

// Stands in for glDepthRangef(), which is GL 4.1 (or ARB_ES2_compatibility) and
// is NULL on a 2.1 context; the GL 1.0 glDepthRange takes the same arguments as
// doubles.  A probe of every GL entry point this source calls found this to be
// the only NULL one not already guarded by support_version_45_ or emulated
// above.  See docs/igi-re-parity/GL21_FALLBACK.md.
static void GLAPIENTRY LegacyDepthRangef(GLfloat n, GLfloat f) {
	glDepthRange(static_cast<GLdouble>(n), static_cast<GLdouble>(f));
}

// glGetUniformBlockIndex() is NULL on a 2.1 context and is called directly --
// not only through GL_SetUniformBlockBinding() -- in renderer_objects.cpp,
// renderer_objects_visual.cpp and renderer_rain.cpp.  Returning GL_INVALID_INDEX
// is the truthful answer ("this program has no such block") and it makes every
// existing `if (blk != GL_INVALID_INDEX) glUniformBlockBinding(...)` guard skip
// the bind, which is what the emulation needs.  Repairing it here rather than at
// the four call sites keeps one policy in one place.
static GLuint GLAPIENTRY LegacyGetUniformBlockIndex(GLuint program, const GLchar* name) {
	(void)program;
	(void)name;
	return GL_INVALID_INDEX;
}

// Unreachable while the shim above returns GL_INVALID_INDEX, but GLEW leaves it
// NULL and a NULL call would crash if any caller ever binds unconditionally.
static void GLAPIENTRY LegacyUniformBlockBinding(GLuint program, GLuint index, GLuint binding) {
	(void)program;
	(void)index;
	(void)binding;
}

// Stands in for glBindBufferBase(), which is NULL on a 2.1 context.  Only the
// uniform-buffer case is emulated; nothing else in this renderer calls it.
static void GLAPIENTRY LegacyBindBufferBase(GLenum target, GLuint index, GLuint buffer) {
	(void)index;
	if (target != GL_UNIFORM_BUFFER) {
		return;
	}

	const auto it = g_legacy_ubos.find(buffer);
	if (it == g_legacy_ubos.end()) {
		return;
	}

	LegacyUploadUboToCurrentProgram(it->second);
}

// GLSL 1.20 has no layout(location = N) qualifier, so the attribute indices the
// shaders/41 tier declares must be re-bound by name before linking.  These are
// exactly the indices the 410 originals use; a name the program does not
// declare is ignored by glBindAttribLocation, so binding a superset is safe.
// vs_in_clr is the one name whose index differs between families (1 in the
// skydome pair, 2 in the flat-sky pair), which is why the vertex filename picks
// the set.
static void LegacyBindAttribLocations(const char* vs_filename, GLuint program) {
	const char* vs = vs_filename ? vs_filename : "";

	// The inline object / preview / rain shaders (renderer_objects.cpp,
	// renderer_objects_visual.cpp, renderer_rain.cpp) each declare their
	// attributes with layout(location = N); these are those indices, and no
	// name appears with two different indices, so binding the whole set is
	// safe for every program.
	glBindAttribLocation(program, 0, "a_pos");
	glBindAttribLocation(program, 1, "a_normal");
	glBindAttribLocation(program, 2, "a_uv");
	glBindAttribLocation(program, 3, "a_uv2");
	glBindAttribLocation(program, 0, "a_seed");
	glBindAttribLocation(program, 1, "a_corner");
	// The weather particles' D3DFVF_DIFFUSE word (renderer_rain.cpp), which the
	// 330 originals declare at location 1 alongside a_pos and a_uv.
	glBindAttribLocation(program, 1, "a_diffuse");

	// The file-based tier, dispatched by vertex filename because vs_in_clr is
	// index 1 in the skydome pair and index 2 in the flat-sky pair.
	if (strstr(vs, "flat_sky_layer")) {
		glBindAttribLocation(program, 0, "vs_in_pos");
		glBindAttribLocation(program, 1, "vs_in_texcoord");
		glBindAttribLocation(program, 2, "vs_in_clr");
	} else if (strstr(vs, "skydome")) {
		glBindAttribLocation(program, 0, "vs_in_pos");
		glBindAttribLocation(program, 1, "vs_in_clr");
	} else if (strstr(vs, "terrain_passing")) {
		glBindAttribLocation(program, 0, "vs_in_pos_and_alpha");
		glBindAttribLocation(program, 1, "vs_in_uv");
	} else if (strstr(vs, "terrain_fog")) {
		glBindAttribLocation(program, 0, "vs_in_pos_local");
	}
}

void GL_LegacyBindInlineAttribLocations(GLuint program) {
	LegacyBindAttribLocations(nullptr, program);
}

gl_info_s g_gl_info;

bool GL_Init() {
	const GLenum glew_status = glewInit();
	if (glew_status != GLEW_OK) {
		// LogStartup, not Log: this runs before the renderer's logging path is
		// usable, and the specific code is the whole diagnosis.  A bare
		// "glewInit error" cannot distinguish a driver that reports no GL
		// version from one that reports GL 1.0/1.1 only from a WGL-level
		// failure -- all three have completely different fixes.  See
		// docs/igi-re-parity/WINE_SMOKE.md for the observed values.
		LogStartup(log_type_t::LOG_ERROR, __FILE__, __LINE__,
		           "glewInit error %u: %s\n",
		           (unsigned)glew_status, (const char *)glewGetErrorString(glew_status));
		return false;
	}

	printf("GL_VENDOR:                   %s\n", glGetString(GL_VENDOR));
	printf("GL_RENDERER:                 %s\n", glGetString(GL_RENDERER));
	printf("GL_VERSION:                  %s\n", glGetString(GL_VERSION));
	printf("GL_SHADING_LANGUAGE_VERSION: %s\n", glGetString(GL_SHADING_LANGUAGE_VERSION));

	// requires OpenGL 4.5 and above or ARB_direct_state_access to support glBindTextureUnit
	g_gl_info.support_version_45_ = glewIsSupported("GL_VERSION_4_5") == GL_TRUE;

	// A context without uniform buffer objects cannot run the shaders/41 or
	// shaders/45 tiers at all, so treat "no GL 3.0" as "use the 2.1 fallback".
	g_gl_info.legacy_gl21_ = glewIsSupported("GL_VERSION_3_0") != GL_TRUE;

	if (g_gl_info.legacy_gl21_) {
		// Point GLEW's NULL core VAO entry points at the APPLE equivalents.
		// This is the whole reason the ~103 VAO call sites need no edits.
		// The casts are only for the APPLE prototypes' `const GLuint*` on
		// glGenVertexArrays/glDeleteVertexArrays; the calls are identical.
		if (!glGenVertexArrays && GLEW_APPLE_vertex_array_object) {
			glGenVertexArrays = reinterpret_cast<PFNGLGENVERTEXARRAYSPROC>(glGenVertexArraysAPPLE);
		}
		if (!glBindVertexArray && GLEW_APPLE_vertex_array_object) {
			glBindVertexArray = reinterpret_cast<PFNGLBINDVERTEXARRAYPROC>(glBindVertexArrayAPPLE);
		}
		if (!glDeleteVertexArrays && GLEW_APPLE_vertex_array_object) {
			glDeleteVertexArrays = reinterpret_cast<PFNGLDELETEVERTEXARRAYSPROC>(glDeleteVertexArraysAPPLE);
		}
		// Unconditional in legacy mode, not only when GLEW left the pointer NULL:
		// GL_BufferData()/GL_BufferSubData() below swallow every GL_UNIFORM_BUFFER
		// upload whenever legacy_gl21_ is set, so this shim is the only path that
		// can ever deliver those bytes.  On a 2.1 context that still advertises
		// GL_ARB_uniform_buffer_object, GLEW would otherwise install the real
		// entry point, the data would be dropped, and every emulated block would
		// read back as zero matrices.
		glBindBufferBase = LegacyBindBufferBase;
		if (!glDepthRangef) {
			glDepthRangef = LegacyDepthRangef;
		}
		if (!glGetUniformBlockIndex) {
			glGetUniformBlockIndex = LegacyGetUniformBlockIndex;
		}
		if (!glUniformBlockBinding) {
			glUniformBlockBinding = LegacyUniformBlockBinding;
		}

		printf("GL 2.1 fallback active: shaders/21 tier, emulated uniform blocks\n");

		if (!glGenVertexArrays) {
			LogStartup(log_type_t::LOG_ERROR, __FILE__, __LINE__,
			           "no vertex array object support (neither core nor APPLE)\n");
			return false;
		}
	}

#if defined(_WIN32)
	// Find the extension function
	wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
#endif

#if defined(__linux__)
	glXSwapIntervalEXT = (PFNGLXSWAPINTERVALEXTPROC)glXGetProcAddress((const GLubyte*)"glXSwapIntervalEXT");
#endif

	return true;
}

void GL_UnbindForImmediateMode() {
	glUseProgram(0);
	glBindVertexArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	for (int i = 0; i < 4; ++i) glDisableVertexAttribArray(static_cast<GLuint>(i));
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
	// A uniform buffer cannot exist on a 2.1 context.  Keep its bytes instead,
	// so the patched glBindBufferBase() can upload them as plain uniforms.
	if (g_gl_info.legacy_gl21_ && target == GL_UNIFORM_BUFFER) {
		legacy_ubo_s& ubo = g_legacy_ubos[obj];
		ubo.size_ = size;
		ubo.data_.assign(static_cast<size_t>(size), 0);
		if (data) {
			memcpy(ubo.data_.data(), data, static_cast<size_t>(size));
		}
		return;
	}

	glBindBuffer(target, obj);
	glBufferData(target, size, data, usage);
	glBindBuffer(target, 0);
}

void GL_BufferSubData(GLuint obj, GLenum target, GLintptr offset, GLsizeiptr size, const void* data) {
	// A uniform buffer cannot exist on a 2.1 context; patch the emulated
	// block's cache so the next bind uploads the updated values.
	if (g_gl_info.legacy_gl21_ && target == GL_UNIFORM_BUFFER) {
		legacy_ubo_s& ubo = g_legacy_ubos[obj];
		const size_t end = static_cast<size_t>(offset) + static_cast<size_t>(size);
		if (ubo.data_.size() < end) {
			ubo.data_.resize(end, 0);
			ubo.size_ = static_cast<GLsizeiptr>(end);
		}
		if (data) {
			memcpy(ubo.data_.data() + offset, data, static_cast<size_t>(size));
		}
		return;
	}

	glBindBuffer(target, obj);
	glBufferSubData(target, offset, size, data);
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
			LogStartup(log_type_t::LOG_ERROR, __FILE__, __LINE__, "shader \"%s\" compile error: %s\n", filename, buf);
		
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

	// GLSL 1.20 has no layout(location = N), so the attribute indices have to be
	// bound by name before linking; see LegacyBindAttribLocations().
	if (g_gl_info.legacy_gl21_) {
		LegacyBindAttribLocations(shader_full_filenames[VERTEX_SHADER], program);
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
			LogStartup(log_type_t::LOG_ERROR, __FILE__, __LINE__, "program link error: %s,\n", buf);

			MEM_FREE_(buf);

			for (int i = 0; i < SUPPORT_SHADER_COUNT; ++i) {
				if (shader_full_filenames[i]) {
					LogStartup(log_type_t::LOG_ERROR, __FILE__, __LINE__, "shader: %s\n", shader_full_filenames[i]);
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
	// On a 2.1 context there is no uniform block to bind -- the shaders/21 tier
	// declares plain uniforms and the patched glBindBufferBase() uploads them.
	// Report success so callers' "if (!...) return false" stays inert.
	if (g_gl_info.legacy_gl21_) {
		(void)uniform_block_name;
		(void)binding_point;
		return true;
	}

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
