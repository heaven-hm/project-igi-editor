/******************************************************************************
 * @file    common.h
 * @brief   macros, constants, structures & functions
 *****************************************************************************/

#pragma once

/*
================================================================================
 macros
================================================================================
*/
#define FLAG_BIT(X)			(1 << X)
#define count_of(a)			(sizeof(a) / sizeof(a[0]))
#define SQUARE(x)			((x) * (x))

#define MAKE_FOURCC(c1, c2, c3, c4)	((uint32_t)c1 + ((uint32_t)c2 << 8) + ((uint32_t)c3 << 16) + ((uint32_t)c4 << 24))

#define SAFE_FREE(x)   do { if (x) { Mem_Free(x, __FILE__, __LINE__); x = nullptr; } } while (0)

#if defined(_MSC_VER)
# define	STRCASECMP(s1, s2)		_stricmp(s1, s2)
#endif

#if defined(__GNUC__)
# define	STRCASECMP(s1, s2)		strcasecmp(s1, s2)
#endif

/*
================================================================================
 constants
================================================================================
*/

constexpr float		WORLD_UNITS_PER_METER = 4096.0f;

constexpr int		MAX_FLAT_SKY_LAYERS = 2;
constexpr int		NUM_VERTEX_PER_FLAT_SKY_LAYER = 108;

constexpr int		MIN_LEVEL_NO = 1;
constexpr int		MAX_LEVEL_NO = 13;

// renderer
constexpr float		RENDER_Z_NEAR = 0.4096f;
constexpr float		RENDER_Z_FAR  = 409600.03125f;
constexpr float		RENDER_DEPTH_MIN = 0.06f;
constexpr float		RENDER_DEPTH_MAX = 1.0f;
constexpr float		FOVY_IN_DEGREE = 70.0f;
constexpr float		RENDERER_MODEL_SCALE_DOWN = 0.001f;
constexpr float		WORLD_Z_NEAR = RENDER_Z_NEAR / RENDERER_MODEL_SCALE_DOWN;

constexpr int		MAX_TERRAIN_VERTICES = 65536;	// tune this
constexpr int		MAX_TERRAIN_INDICES = 1 << 20;	// tune this
constexpr int		MAX_TERRAIN_RENDER_CHUNK = 1024;	// tune this

extern const glm::vec3	VEC3_ORIGIN;
extern const glm::vec3	VEC3_X_DIR;
extern const glm::vec3	VEC3_Y_DIR;
extern const glm::vec3	VEC3_Z_DIR;

/*
================================================================================
 memory
================================================================================
*/
void *			Mem_Alloc(size_t sz, const char * file, int line);
void			Mem_Free(void * ptr, const char * file, int line);
void			Mem_FreeAll();
void			Mem_Print();

#define			MEM_ALLOC(sz)	Mem_Alloc(sz, __FILE__, __LINE__)
#define			MEM_FREE_(ptr)	Mem_Free(ptr, __FILE__, __LINE__)

/*
================================================================================
 quit_exception
================================================================================
*/
class quit_exception : public std::exception {
public:

	quit_exception() = delete;
	quit_exception(const char* msg);

	const char*				what() const noexcept override;

private:

	char					msg_[1024];
};


/*
================================================================================
 read command line arguments
================================================================================
*/

int		Arg_OptionIdx(int argc, char** argv, const char* arg_name);
int		Arg_ReadInt(int argc, char** argv, const char* arg_name, int default_value);
float	Arg_ReadFloat(int argc, char** argv, const char* arg_name, float default_value);
const char* Arg_ReadStr(int argc, char** argv, const char* arg_name, const char * default_value);

/*
================================================================================
 log
================================================================================
*/
enum class log_type_t {
	LOG_INFOR,
	LOG_ERROR,
	LOG_FATAL
};

void	Log(log_type_t tp, const char * file, int line, const char * fmt, ...);

/*
================================================================================
 time
================================================================================
*/
int64_t	Sys_Milliseconds();

/*
================================================================================
 string
================================================================================
*/
void	Str_Copy(char* dst, int dst_cap, const char* src);
void	Str_Cat(char* dst, int dst_cap, const char* src);
void	Str_VSPrintf(char* dst, int dst_cap, const char* fmt, va_list argptr);
void	Str_SPrintf(char* dst, int dst_cap, const char* fmt, ...);

int		Str_UTF8ToUTF16(const char* utf8, char16_t* utf16, int utf16_chars);
int		Str_UTF16ToUTF8(const char16_t* utf16, char* utf8, int utf8_bytes);

bool	Str_ExtractFileName(const char* path, char* filename, int filename_cap);
bool	Str_ExtractFileDirSelf(char* path);
void	Str_EraseDoubleDotsInPath(char* path);

int		Str_ToInt(const char* s);
float	Str_ToFloat(const char *s);

/*
================================================================================
 file
================================================================================
*/
bool	File_Exists(const char* filename);
FILE *	File_Open(const char* filename, const char* mod);
bool	File_LoadText(const char* filename, char *& buf);
bool	File_LoadBinary(const char* filename, void*& buf, int32_t& len);
void	File_FreeBuf(void * buf);

/*
================================================================================
  rgba picture
================================================================================
*/

struct pic_s {
	int						width_;
	int						height_;
	uint8_t*				pixels_;	// rgba
};

struct pics_s {
	pic_s*					pics_;
	int						num_pic_;
};

void	Pic_SaveToBMP(const pic_s* pic, const char* filename);
void	Pic_FreePics(pics_s & pics);

/*
================================================================================
 folders_s
================================================================================
*/
struct folders_s {
	char					res_folder_[1024];
	char					shader_folder_[1024];
};

extern folders_s	g_folders;

void		Folders_Init();

/*
================================================================================
 math
================================================================================
*/

// return highest bit index which is not zero
int			get_highest_bit(int value);

void		TransformDoubleVec3(const glm::mat3& mat, double v[3], double out[3]);

/*

	Z   Y
	|  /
	| /
	|/________X

  when: yaw == 0.0, pitch == 0.0, roll = 0.0
  forward: +y
  right:   +x
  up:      +z

 */
void		AngleToVectors(float yaw, float pitch, float roll, glm::vec3 & forward, glm::vec3& right, glm::vec3& up);

// debug
void		PrintMatrix3x3(const char * name, const glm::mat3 & m);

/*
================================================================================
 view_define_s
================================================================================
*/
struct view_define_s {
	glm::vec3				pos_;
	glm::vec3				forward_;
	glm::vec3				right_;
	glm::vec3				up_;
	float					fovx_;	// in radian
	float					fovy_;	// in radian
	float					render_z_near_;
	float					render_z_far_;
	float					render_min_depth_;
	float					render_max_depth_;
	int						viewport_width_;
	int						viewport_height_;
	float					tan_half_fovx_;
	float					tan_half_fovy_;
	float					half_viewport_width_div_tan_half_fovx_;
	float					half_viewport_height_div_tan_half_fovy_;
	glm::mat3				mat_rot_;
};

/*
================================================================================
 vertex format
================================================================================
*/

// vertex format of skydome
struct vert_pos_rgb_s {
	glm::vec3				pos_;
	glm::vec3				color_;
};

// vertex format of flat sky layer
struct vert_flat_sky_layer_s {
	glm::vec4				pos_;	// screen_x, screen_y, z (in rot coordinate), RHW
	glm::vec2				uv_;	// in world space
	glm::vec4				clr_;
};

// vertex format of terrain
struct vert_pos_a_uv_s {
	glm::vec3				pos_;
	float					tex_alpha_;	// * tex color
	glm::vec2				uv_;
};

/*
================================================================================
 skydome_define_s
================================================================================
*/
struct skydome_define_s {
	float					top_color1_[3];
	float					top_color2_[3];
	float					middle_color1_[3];
	float					middle_color2_[3];
	float					bottom_color1_[3];
	float					bottom_color2_[3];
	float					angle_;		// in radian
};

struct render_triangles_s {
	int32_t					first_vert_idx_;	// or index idx
	int32_t					num_triangle_;
};

static const int			MAX_TRIANGLE_LIST_PER_CHUNK = 1024;

struct render_chunk_s {
	uint32_t				tex_idx_;
	uint32_t				render_layer_;
	render_triangles_s		render_triangles_[MAX_TRIANGLE_LIST_PER_CHUNK];
	int32_t					num_render_triangle_;
};

/*
================================================================================
 update_params_s
================================================================================
*/

// terrain modification options
constexpr int	TERRAIN_TEXTURE_MOD = FLAG_BIT(0);
constexpr int	TERRAIN_HEIGHT_MOD	= FLAG_BIT(1);
constexpr int	TERRAIN_DISCARD_MOD = FLAG_BIT(2);

struct update_params_s {
	uint32_t				frame_;			// frame number
	float					delta_seconds_;	// frame time
	const view_define_s*	view_define_;

	// flat sky layer
	vert_flat_sky_layer_s*	flat_sky_layer_vb_;			// write to
	bool					flat_sky_layer_is_visible_;	// write to

	// terrain
	int						terrain_mod_options_;
	vert_pos_a_uv_s*		terrain_vb_;				// write to
	uint32_t*				terrain_ib_;				// write to
	render_chunk_s*			terrain_render_chunks_;		// write to
	int						num_terrain_vert_;			// write to
	int						num_terrain_idx_;			// write to
	int						num_terrain_render_chunk_;	// write to
};

/*
================================================================================
 IRenderResLoader
================================================================================
*/
class IRenderResLoader {
public:

	virtual ~IRenderResLoader() {}

	virtual void			SetupClearColor(const glm::vec4& color) = 0;
	virtual void			SetupFog(const glm::vec4 & color, float fog_far) = 0;
	virtual void			SetupSkydome(const skydome_define_s& d) = 0;

	virtual void			LoadFlatSkyLayerTex(int layer_no, const pic_s* pic) = 0;
	virtual void			LoadTerrainMatTex(const pic_s* pic) = 0;
	virtual void			LoadTerrainLMPTex(const pic_s* pic) = 0;

};
