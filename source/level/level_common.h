/******************************************************************************
 * @file    level_common.h
 * @brief   allocator, *.tex, *.qsc
 *****************************************************************************/

#pragma once

/*
================================================================================
 constants
================================================================================
*/

constexpr uint32_t	INVALID_FRAME = -1;


// cube dimension
constexpr int		ROOT_CUBE_HALF_SIZE = 1 << 30;
constexpr int		LEVEL1_CUBE_HALF_SIZE = ROOT_CUBE_HALF_SIZE >> 1;

 /*
 ================================================================================
  allocator
 ================================================================================
 */
struct fixed_size_item_pool_s {
	char*					item_pool_;
	uint32_t*				item_indices_;
	uint32_t				allocated_item_count_;
	uint32_t				pool_capacity_;
	uint32_t				item_aligned_size_;
	uint32_t				item_original_size_;

	fixed_size_item_pool_s();
	~fixed_size_item_pool_s();

	void					Init(uint32_t item_size, uint32_t capacity, uint32_t alignment);
	void					Shutdown();

	void*					Alloc();
	void					Free(void* item);
};


/*
================================================================================
 *.tex
================================================================================
*/

// identity of *.tex file
static constexpr uint32_t	TEX_IDENT = MAKE_FOURCC('L', 'O', 'O', 'P');

static constexpr int32_t	IMAGE_MODE_2 = 2;
static constexpr int32_t	IMAGE_MODE_3 = 3;
static constexpr int32_t	IMAGE_MODE_67 = 67;

#pragma pack(push, 1)

struct tex_head_s {
	uint32_t				ident_;
	int32_t					version_;
};

struct tex_head_v2_s {
	uint32_t				ident_;
	int32_t					version_;
	int32_t					image_mode_;
	int32_t					unk0_;
	int16_t					image_line_width_;
	int16_t					image_width_;
	int16_t					image_height_;
	int16_t					bytes_per_pixel_;
};

struct tex_head_v9_s {
	uint32_t				ident_;
	int32_t					version_;
	int32_t					unk0_;
	int32_t					unk1_;
	int32_t					unk2_;
	int32_t					unk3_;
	int32_t					unk4_;
	int32_t					footer_offset_;
	int32_t					layer_count_;
	int32_t					unk5_;
	int32_t					image_width_;
	int32_t					image_height_;
	int32_t					image_mode_;
};

static_assert(sizeof(tex_head_v9_s) == 52, "bad size of tex_head_v9_s");

struct tex_head_v7_s {
	uint32_t				ident_;
	int32_t					version_;
	int32_t					unk0_;
	int32_t					unk1_;
	int32_t					unk2_;
	int32_t					unk3_;
	int32_t					unk4_;
	int32_t					footer_offset_;
	int32_t					layer_count_;
	int32_t					unk5_;
	int32_t					image_width_;
	int32_t					image_height_;
	int32_t					image_mode_;
};

static_assert(sizeof(tex_head_v7_s) == 52, "bad size of tex_head_v7_s");

struct tex_layer_v7_s {
	int32_t					image_offset_;
	int32_t					image_line_width_;
	int16_t					image_width_;
	int16_t					unk0_;
	int16_t					image_height_;
	int8_t					reserved[26];
};

static_assert(sizeof(tex_layer_v7_s) == 40, "bad size of tex_layer_v7_s");

struct tex_footer_v9_s {
	uint32_t				ident_;
	int32_t					version_;
	int16_t					unk0_;
	int16_t					unk1_;
	int16_t					unk2_;
	int16_t					unk3_;
	int32_t					count_x_;
	int32_t					count_y_;
};

struct tex_layer_s {
	int32_t					image_offset_;
	int32_t					image_mode_;
	int16_t					image_line_width_;
	int16_t					image_width_;
	int16_t					image_height_;
	int16_t					unk0_;
	int32_t					reserved0_;
	int32_t					reserved1_;
	int32_t					reserved2_;
	int32_t					reserved3_;
};

static_assert(sizeof(tex_layer_s) == 32, "bad size of tex_layer_s");

struct tex_head_v11_s {
	uint32_t				ident_;
	int32_t					version_;
	int32_t					image_mode_;
	int32_t					unk0_;
	int32_t					unk1_;
	int16_t					unk2_;
	int16_t					image_width_;
	int16_t					image_height_;
	int16_t					unk3_;
	int16_t					unk4_;
	int16_t					bytes_per_pixels_;
};

static_assert(sizeof(tex_head_v11_s) == 32, "bad size of tex_head_v11_s");

#pragma pack(pop)

bool	Tex_Load(const char* filename, pics_s& pics);

/*
================================================================================
 QSC - *.qsc file
================================================================================
*/

class QSC {
public:

	struct func_s;

	struct arg_s {

		arg_s* next_;

		enum class type_t {
			STR,
			DBL,
			BOOL,
			FUNC
		} type_;

		union {
			const char*		str_;
			double			dbl_;
			int				bool_;
			QSC::func_s*	func_;
		};
	};

	struct func_s {
		const char*			func_name_;
		int					line_;
		int					start_offset_;
		int					end_offset_;
		arg_s*				args_;
	};

	QSC();
	~QSC();

	// load a qsc file
	void					Load(const char* filename);
	// load from a binary qvm file
	void					LoadFromQVM(const char* filename);
	void					Unload();

	int						FindFuncByStr(const char* s, const func_s* funcs[1024]) const;
	int						FindFuncByName(const char* name, const func_s* funcs[1024]) const;

	int						GetRootFuncCount() const { return root_func_count_; }
	const func_s*			GetRootFunc(int idx) const { return root_funcs_[idx]; }
	const char*				GetScripts() const { return pristine_scripts_ ? pristine_scripts_ : scripts_; }
	const char*				GetParsedScripts() const { return scripts_; }

	// debug
	void					Print() const;

private:

	static constexpr int	MAX_QSC_FUNCS = 4096;
	static constexpr int	MAX_QSC_ARGS = 65536;

	char*					scripts_;
	char*					pristine_scripts_;
	char*					pc_;
	int						line_;

	func_s*					root_funcs_[MAX_QSC_FUNCS];

	int						root_func_count_;
	int						allocated_funcs_;
	int						allocated_args_;

	func_s*					parse_func_stack_[1024];
	int						parse_func_stack_size_;

	func_s					func_pool_[MAX_QSC_FUNCS];
	arg_s					arg_pool_[MAX_QSC_ARGS];

	func_s*					AllocFunc();	// alloc func_s from pool
	arg_s*					AllocArg();		// alloc arg_s from pool

	void					Parse();

	bool					Parser_SkipWhiteChar();
	bool					Parser_SkipNumberChar();
	bool					Parser_SkipFuncName();
	bool					Parser_ForwardToChar(char c);
	bool					Parser_ForwardToDoubleQuoto();

	func_s*					ParseFunc();
	bool					ParseArgs(func_s* func);

	void					AddFunc(func_s* func);
	void					AddArgToFunc(func_s* func, arg_s* arg);
};

/*
================================================================================
 qtask_s
================================================================================
*/
struct qtask_s {
	int32_t					task_type_;
	qtask_s*				prior_;
	qtask_s*				next_;
};

/*
================================================================================
 dyn_cube_s
================================================================================
*/
#define CUBE_FLAG_DISCARD_TERRAIN	0x80

struct dyn_cube_s {
	uint32_t				cube_half_size_;
	uint32_t				idx_in_parent_children_array_;
	dyn_cube_s*				parent_;
	dyn_cube_s*				children_[8];
	qtask_s*				qtask_link_chain_;
	uint8_t					children_mask_;
	uint8_t					flags_;
};

/*
================================================================================
 ILevelDynCube - level dynamic cube interface
================================================================================
*/
class ILevelDynCube {
public:

	virtual ~ILevelDynCube() {}

	virtual dyn_cube_s*		GetDynCube(const double pos[3], int cube_lod_level, glm::ivec3& cube_ctr) = 0;
	virtual void			AddQTaskToDynCube(dyn_cube_s* dyn_cube, qtask_s* qtask) = 0;

};
