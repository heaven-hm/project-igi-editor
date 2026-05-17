/******************************************************************************
 * @file    level_common.cpp
 * @brief   allocator, *.tex, *.qsc
 *****************************************************************************/

#include "pch.h"
#include "level_common.h"
#include "logger.h"
#include <string>

static void CleanBloatedBackslashesInPlace(char* str) {
    if (!str) return;
    char* src = str;
    char* dst = str;
    while (*src) {
        if (*src == '\\') {
            int count = 0;
            while (src[count] == '\\') {
                count++;
            }
            if (count >= 4) {
                *dst++ = '\\';
                *dst++ = '\\';
                src += count;
            } else {
                for (int i = 0; i < count; ++i) {
                    *dst++ = '\\';
                }
                src += count;
            }
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}


/*
================================================================================
allocator
================================================================================
*/
fixed_size_item_pool_s::fixed_size_item_pool_s() :
	item_pool_(nullptr),
	item_indices_(nullptr),
	allocated_item_count_(0),
	pool_capacity_(0),
	item_aligned_size_(0),
	item_original_size_(0)
{
	//
}

fixed_size_item_pool_s::~fixed_size_item_pool_s() {
	Shutdown();
}

void fixed_size_item_pool_s::Init(uint32_t item_size, uint32_t capacity, uint32_t alignment) {
	if (alignment < 4) {
		alignment = 4;
	}

	int item_aligned_size = ~(alignment - 1) & (item_size + alignment + 4);

	item_pool_ = (char*)MEM_ALLOC(capacity * (item_aligned_size + 4));
	item_indices_ = (uint32_t*)(item_pool_ + capacity * item_aligned_size);
	allocated_item_count_ = 0;
	pool_capacity_ = capacity;
	item_aligned_size_ = item_aligned_size;
	item_original_size_ = item_size;

	/*
	  item_indices_:
		[ -- allocated item indices (size: allocated_item_count_) -- | -- free item indices -- ]

	  item:
		[ -- real data (size: item_original_size_) -- | -- idx of item_indices_ (size: 4 bytes) -- ]

	 */
	for (uint32_t i = 0; i < capacity; ++i) {
		// init free item indices
		item_indices_[i] = i;
	}
}

void fixed_size_item_pool_s::Shutdown() {
	if (item_pool_) {
		MEM_FREE_(item_pool_);
		item_pool_ = nullptr;
	}

	item_indices_ = nullptr;
	allocated_item_count_ = 0;
	pool_capacity_ = 0;
	item_aligned_size_ = 0;
	item_original_size_ = 0;
}

void* fixed_size_item_pool_s::Alloc() {
	if (allocated_item_count_ < pool_capacity_) {
		// get the index right after the last allocated item
		int free_item_idx = item_indices_[allocated_item_count_];

		char* item = item_pool_ + item_aligned_size_ * free_item_idx;

		// link which item indices slot
		*(uint32_t*)(item + item_original_size_) = allocated_item_count_;

		allocated_item_count_++;

		return item;
	}
	else {
		// overflow
		return nullptr;
	}
}

void fixed_size_item_pool_s::Free(void* item) {
	int curr_item_indices_slot = *(uint32_t*)((char*)item + item_original_size_);

	if (curr_item_indices_slot != (allocated_item_count_ - 1)) {
		// if not the last item

		// update the last item's indices slot
		int curr_item_idx = item_indices_[curr_item_indices_slot];

		// get last item
		int last_item_idx = item_indices_[allocated_item_count_ - 1];

		char* last_item = item_pool_ + item_aligned_size_ * last_item_idx;

		// curr item indices point to last item in the pool
		item_indices_[curr_item_indices_slot] = last_item_idx;

		// update last_item indices_slot
		*(uint32_t*)(last_item + item_original_size_) = curr_item_indices_slot;

		// -- update item_indices for newly free item --
		item_indices_[allocated_item_count_ - 1] = curr_item_idx;	// mark current item idx as free
	}

	// if remove the last one, just decrease the item_count_
	allocated_item_count_--;
}

 /*
 ================================================================================
  *.tex
 ================================================================================
 */
static void ConvertTex16ToPic(
	int tex_line_width, int tex_width, int tex_height, const uint8_t* tex_pixels,
	pic_s* pic)
{
	static const float _5BITS_TO_8BITS = 255.0f / 31.0f;

	pic->width_ = tex_width;
	pic->height_ = tex_height;
	uint8_t* pic_pixels = pic->pixels_;

	for (int y = 0; y < tex_height; ++y) {
		// reverse y: GL texture orgin is lower-left corner
		const uint16_t* src_line = (const uint16_t*)(tex_pixels + (tex_height - y - 1) * tex_line_width);
		uint8_t* dst_line = pic_pixels + y * tex_width * 4;

		for (int x = 0; x < tex_width; ++x) {
			uint16_t src_pixel = src_line[x];
			uint8_t* dst_pixel = dst_line + x * 4;

			uint8_t src_b = (uint8_t)((src_pixel & 0x1F) * _5BITS_TO_8BITS);
			uint8_t src_g = (uint8_t)(((src_pixel >> 5) & 0x1F) * _5BITS_TO_8BITS);
			uint8_t src_r = (uint8_t)(((src_pixel >> 10) & 0x1F) * _5BITS_TO_8BITS);

			dst_pixel[0] = src_r;
			dst_pixel[1] = src_g;
			dst_pixel[2] = src_b;
			dst_pixel[3] = 255;
		}
	}
}

static void ConvertTex24ToPic(
	int tex_line_width, int tex_width, int tex_height, const uint8_t* tex_pixels,
	pic_s* pic)
{
	pic->width_ = tex_width;
	pic->height_ = tex_height;
	uint8_t* pic_pixels = pic->pixels_;

	for (int y = 0; y < tex_height; ++y) {
		// reverse y: GL texture orgin is lower-left corner
		const uint8_t* src_line = tex_pixels + (tex_height - y - 1) * tex_line_width;
		uint8_t* dst_line = pic_pixels + y * tex_width * 4;

		for (int x = 0; x < tex_width; ++x) {
			const uint8_t* src_pixel = src_line + x * 3;
			uint8_t* dst_pixel = dst_line + x * 4;

			dst_pixel[0] = src_pixel[2];
			dst_pixel[1] = src_pixel[0];
			dst_pixel[2] = src_pixel[1];
			dst_pixel[3] = 255;
		}
	}
}

static void ConvertTex32ToPic(
	int tex_line_width, int tex_width, int tex_height, const uint8_t* tex_pixels,
	pic_s* pic)
{
	pic->width_ = tex_width;
	pic->height_ = tex_height;
	uint8_t* pic_pixels = pic->pixels_;

	for (int y = 0; y < tex_height; ++y) {
		// reverse y: GL texture orgin is lower-left corner
		const uint8_t* src_line = tex_pixels + (tex_height - y - 1) * tex_line_width;
		uint8_t* dst_line = pic_pixels + y * tex_width * 4;

		for (int x = 0; x < tex_width; ++x) {
			const uint8_t* src_pixel = src_line + x * 4;
			uint8_t* dst_pixel = dst_line + x * 4;

			dst_pixel[0] = src_pixel[2];
			dst_pixel[1] = src_pixel[0];
			dst_pixel[2] = src_pixel[1];
			dst_pixel[3] = src_pixel[3];
		}
	}
}

static bool Tex_Loadv2(tex_head_v2_s* head, pics_s& pics) {
	if ((head->image_line_width_ / head->image_width_) == 2) {

		uint32_t pixel_size = head->image_width_ * head->image_height_ * 2;
		uint32_t total_size = sizeof(tex_head_v2_s) + pixel_size;

		pics.pics_ = (pic_s*)MEM_ALLOC(sizeof(pic_s));
		if (!pics.pics_) {
			return false;
		}
		pics.pics_->pixels_ = (uint8_t*)MEM_ALLOC(head->image_width_ * head->image_height_ * 4);
		if (!pics.pics_->pixels_) {
			return false;
		}

		pics.num_pic_ = 1;

		ConvertTex16ToPic(head->image_line_width_, head->image_width_, head->image_height_,
			(const uint8_t*)(head + 1), pics.pics_);

		return true;
	}
	else {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "unknown processed line width\n");
		return false;
	}
}

static bool Tex_Loadv9(tex_head_v9_s* head, pics_s& pics) {
	tex_footer_v9_s* footer = (tex_footer_v9_s*)((char*)head + head->footer_offset_);
	if (footer->ident_ != TEX_IDENT) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "bad tex format\n");
		return false;
	}

	pics.pics_ = (pic_s*)MEM_ALLOC(sizeof(pic_s) * head->layer_count_);
	if (!pics.pics_) {
		return false;
	}

	memset(pics.pics_, 0, sizeof(pic_s) * head->layer_count_);
	pics.num_pic_ = head->layer_count_;

	tex_layer_s* layers = (tex_layer_s*)(head + 1);
	for (int i = 0; i < head->layer_count_; ++i) {
		tex_layer_s* cur_layer = layers + i;

		uint8_t* src_pixels = (uint8_t*)head + cur_layer->image_offset_;
		pic_s* dst_pic = pics.pics_ + i;
		dst_pic->pixels_ = (uint8_t*)MEM_ALLOC(cur_layer->image_width_ * cur_layer->image_height_ * 4);
		if (!dst_pic->pixels_) {
			return false;
		}

		if (cur_layer->image_mode_ == IMAGE_MODE_2) {
			ConvertTex16ToPic(cur_layer->image_line_width_, cur_layer->image_width_,
				cur_layer->image_height_, src_pixels, dst_pic);
		}
		else if (cur_layer->image_mode_ == IMAGE_MODE_3) {
			if (cur_layer->image_line_width_ / cur_layer->image_width_ == 4) {
				ConvertTex32ToPic(cur_layer->image_line_width_, cur_layer->image_width_,
					cur_layer->image_height_, src_pixels, dst_pic);
			}
			else {
				ConvertTex24ToPic(cur_layer->image_line_width_, cur_layer->image_width_,
					cur_layer->image_height_, src_pixels, dst_pic);
			}
		}
	}

	return true;
}

static bool Tex_Loadv11(tex_head_v11_s* head, pics_s& pics) {
	pics.pics_ = (pic_s*)MEM_ALLOC(sizeof(pic_s));
	if (!pics.pics_) {
		return false;
	}
	pics.pics_->pixels_ = (uint8_t*)MEM_ALLOC(head->image_width_ * head->image_height_ * 4);
	if (!pics.pics_->pixels_) {
		return false;
	}

	pics.num_pic_ = 1;

	uint32_t image_line_width = head->unk3_ * head->bytes_per_pixels_;
	const uint8_t* src_pixels = (uint8_t*)(head + 1);

	if (head->image_mode_ == IMAGE_MODE_2) {
		ConvertTex16ToPic(image_line_width, head->image_width_,
			head->image_height_, src_pixels, pics.pics_);
		return true;
	}
	else if (head->image_mode_ == IMAGE_MODE_3) {
		ConvertTex32ToPic(image_line_width, head->image_width_,
			head->image_height_, src_pixels, pics.pics_);
		return true;
	}
	else {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "unknown image mode %d\n",
			head->image_mode_);
		return false;
	}
}

bool Tex_Load(const char* filename, pics_s& pics) {
	memset(&pics, 0, sizeof(pics));

	void* buf = nullptr;
	int32_t sz = 0;
	if (!File_LoadBinary(filename, buf, sz)) {
		return false;
	}

	tex_head_s* head = (tex_head_s*)buf;
	if (head->ident_ != TEX_IDENT) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "not a tex file\n");
		File_FreeBuf(buf);
		return false;
	}

	bool load_ok = false;

	if (head->version_ == 2) {
		load_ok = Tex_Loadv2((tex_head_v2_s*)head, pics);
	}
	else if (head->version_ == 9) {
		load_ok = Tex_Loadv9((tex_head_v9_s*)head, pics);
	}
	else if (head->version_ == 11) {
		load_ok = Tex_Loadv11((tex_head_v11_s*)head, pics);
	}
	else {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "unknown version %d\n", head->version_);
	}

	File_FreeBuf(buf);

	if (!load_ok) {
		Pic_FreePics(pics);
	}

	return load_ok;
}

/*
================================================================================
 QSC
================================================================================
*/
QSC::QSC() :
	scripts_(nullptr),
	pristine_scripts_(nullptr),
	pc_(nullptr),
	line_(0),
	root_func_count_(0),
	allocated_funcs_(0),
	allocated_args_(0),
	parse_func_stack_size_(0)
{
	//
}

QSC::~QSC() {
	Unload();
}

void QSC::Load(const char* filename) {
	Unload();

	Logger::Get().Log(LogLevel::INFO, "[QSC] Loading script: " + std::string(filename));
	if (!File_LoadText(filename, scripts_)) {
		Logger::Get().Log(LogLevel::ERR, "[QSC] FAILED to load script file: " + std::string(filename));
		return;
	}

	CleanBloatedBackslashesInPlace(scripts_);

	// Make a pristine copy for raw line extraction because Parse() is destructive
	size_t len = strlen(scripts_);
	pristine_scripts_ = (char*)MEM_ALLOC(len + 1);
	if (pristine_scripts_) {
		memcpy(pristine_scripts_, scripts_, len + 1);
	}


	pc_ = scripts_;

	root_func_count_ = 0;
	allocated_funcs_ = 0;
	allocated_args_ = 0;

	// start parse
	Parse();
}

void QSC::Unload() {
	if (scripts_) {
		File_FreeBuf(scripts_);
		scripts_ = nullptr;
	}
	if (pristine_scripts_) {
		MEM_FREE_(pristine_scripts_);
		pristine_scripts_ = nullptr;
	}

	pc_ = nullptr;
	line_ = 0;
	root_func_count_ = 0;
	allocated_funcs_ = 0;
	allocated_args_ = 0;
	parse_func_stack_size_ = 0;
}

static bool QSC_FuncMatchStr(const QSC::func_s* func, const char* name) {
	if (func->args_ && func->args_->next_ && func->args_->next_->type_ == QSC::arg_s::type_t::STR) {
		return strcmp(func->args_->next_->str_, name) == 0;
	}
	return false;
}

static void QSC_RecursiveFindFuncByStr(
	const QSC::func_s* func, const char* s,
	const QSC::func_s* funcs[1024], int& found_count) {
	if (QSC_FuncMatchStr(func, s)) {
		if (found_count < 1024) {
			funcs[found_count++] = func;
		}
		else {
			return;
		}
	}

	QSC::arg_s* arg = func->args_;
	while (arg) {
		if (arg->type_ == QSC::arg_s::type_t::FUNC) {
			QSC_RecursiveFindFuncByStr(arg->func_, s, funcs, found_count);
		}
		arg = arg->next_;
	}
}

int	QSC::FindFuncByStr(const char* s, const func_s* funcs[1024]) const {
	int found_count = 0;

	for (int i = 0; i < root_func_count_; ++i) {
		const func_s* func = root_funcs_[i];
		QSC_RecursiveFindFuncByStr(func, s, funcs, found_count);
	}

	return found_count;
}

static void QSC_RecursiveFindFuncByName(
	const QSC::func_s* func, const char* name,
	const QSC::func_s* funcs[1024], int& found_count)
{
	if (!strcmp(func->func_name_, name)) {
		if (found_count < 1024) {
			funcs[found_count++] = func;
		}
		else {
			return;
		}
	}

	QSC::arg_s* arg = func->args_;
	while (arg) {
		if (arg->type_ == QSC::arg_s::type_t::FUNC) {
			QSC_RecursiveFindFuncByName(arg->func_, name, funcs, found_count);
		}
		arg = arg->next_;
	}
}

int QSC::FindFuncByName(const char* name, const func_s* funcs[1024]) const {
	int found_count = 0;

	for (int i = 0; i < root_func_count_; ++i) {
		const func_s* func = root_funcs_[i];
		QSC_RecursiveFindFuncByName(func, name, funcs, found_count);
	}

	return found_count;
}

// debug

static void PrintFunc(const QSC::func_s* func, int level) {
	char indent[1024];

	for (int i = 0; i < level; ++i) {
		indent[0 + i * 2] = ' ';
		indent[1 + i * 2] = ' ';
	}

	indent[level * 2] = '\0';

	printf("%sfunc: \"%s\"\n", indent, func->func_name_);

	const QSC::arg_s* arg = func->args_;
	while (arg) {
		switch (arg->type_) {
		case QSC::arg_s::type_t::FUNC:
			PrintFunc(arg->func_, level + 1);
			break;
		case QSC::arg_s::type_t::BOOL:
			printf("%s  %s\n", indent, arg->bool_ ? "TRUE" : "FALSE");
			break;
		case QSC::arg_s::type_t::DBL:
			printf("%s  %lf\n", indent, arg->dbl_);
			break;
		case QSC::arg_s::type_t::STR:
			printf("%s  \"%s\"\n", indent, arg->str_);
			break;
		}

		arg = arg->next_;
	}
}

void QSC::Print() const {
	printf("----- qsc -----\n");

	for (int i = 0; i < root_func_count_; ++i) {
		PrintFunc(root_funcs_[i], 0);
	}
}

QSC::func_s* QSC::AllocFunc() {
	if (allocated_funcs_ < MAX_QSC_FUNCS) {
		func_s* new_func = func_pool_ + allocated_funcs_++;

		new_func->func_name_ = "";
		new_func->line_ = 0;
		new_func->start_offset_ = 0;
		new_func->end_offset_ = 0;
		new_func->args_ = nullptr;

		return new_func;
	}
	else {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "QSC::AllocFunc buf overflow\n");
	}

	return nullptr;
}

QSC::arg_s* QSC::AllocArg() {
	if (allocated_args_ < MAX_QSC_ARGS) {
		arg_s* new_arg = arg_pool_ + allocated_args_++;

		new_arg->next_ = nullptr;
		new_arg->type_ = arg_s::type_t::STR;
		new_arg->str_ = "";

		return new_arg;
	}
	else {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "QSC::AllocArg buf overflow\n");
	}

	return nullptr;
}

void QSC::Parse() {
	while (1) {
		func_s* func = ParseFunc();
		if (!func) {
			break;
		}

		AddFunc(func);

		if (!Parser_SkipWhiteChar()) {
			break;
		}

		if (*pc_ == ';') {
			pc_++;
		}
		else {
			Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "QSC::Parse\n");
			break;
		}
	}
}

bool QSC::Parser_SkipWhiteChar() {
	while (*pc_ != 0 && (*pc_ == 32 || *pc_ == '\t' || *pc_ == '\r' || *pc_ == '\n')) {
		if (*pc_ == '\n') {
			line_++;
		}

		pc_++;
	}

	if (!*pc_) {
		// Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "eof\n");
	}


	return *pc_ != '\0';
}

static bool IsValidNameChar(char c) {
	return  c == '_'
		|| (c >= '0' && c <= '9')
		|| (c >= 'A' && c <= 'Z')
		|| (c >= 'a' && c <= 'z');
}

bool QSC::Parser_SkipNumberChar() {
	while (*pc_ != 0 && ((*pc_ >= '0' && *pc_ <= '9')) || (*pc_ == '-') || (*pc_ == '.') || (*pc_ == 'e')) {
		pc_++;
	}

	if (!*pc_) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "eof\n");
	}

	return *pc_ != '\0';
}

bool QSC::Parser_SkipFuncName() {
	while (*pc_ != 0 && IsValidNameChar(*pc_)) {
		pc_++;
	}

	if (!*pc_) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "eof\n");
	}

	return *pc_ != '\0';
}

bool QSC::Parser_ForwardToChar(char c) {
	while (*pc_ != 0 && *pc_ != c) {
		pc_++;
	}

	if (!*pc_) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "eof\n");
	}

	return *pc_ != '\0';
}

bool QSC::Parser_ForwardToDoubleQuoto() {
	while (*pc_) {

		if (*pc_ == '\\') {
			pc_ += 2;	// skip escape character
		}
		else if (*pc_ == '"') {
			break;
		}
		else {
			pc_++;
		}
	}

	if (!*pc_) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "eof\n");
	}

	return *pc_ != '\0';
}

QSC::func_s* QSC::ParseFunc() {
	if (!Parser_SkipWhiteChar()) {
		return nullptr;
	}

	char* func_name = pc_;

	if (!Parser_SkipFuncName()) {
		Logger::Get().Log(LogLevel::WARNING, "[QSC] Failed to parse function name at line " + std::to_string(line_));
		return nullptr;
	}


	char* func_name_end = pc_;

	if (!Parser_ForwardToChar('(')) {
		return nullptr;
	}

	pc_++;

	*func_name_end = '\0';

	func_s* new_func = AllocFunc();
	if (!new_func) {
		return nullptr;
	}

	new_func->func_name_ = func_name;
	new_func->line_ = line_;
	new_func->start_offset_ = (int)(func_name - scripts_);
	new_func->end_offset_ = new_func->start_offset_;
	new_func->args_ = nullptr;

	parse_func_stack_[parse_func_stack_size_++] = new_func;

	if (!ParseArgs(new_func)) {
		allocated_funcs_--;
		return nullptr;
	}

	if (!Parser_ForwardToChar(')')) {
		allocated_funcs_--;	// parse current function failed
		return nullptr;
	}

	pc_++;

	new_func->end_offset_ = (int)(pc_ - scripts_);
	parse_func_stack_size_--;

	return new_func;
}

bool QSC::ParseArgs(func_s* func) {
	while (1) {
		if (!Parser_SkipWhiteChar()) {
			return false;
		}

		if (*pc_ == '"') {
			pc_++;

			char* str_start = pc_;

			if (!Parser_ForwardToDoubleQuoto()) {
				return false;
			}

			*pc_ = '\0';
			pc_++;

			arg_s* new_arg = AllocArg();
			if (!new_arg) {
				return false;
			}

			new_arg->next_ = nullptr;
			new_arg->type_ = arg_s::type_t::STR;
			new_arg->str_ = str_start;

			AddArgToFunc(func, new_arg);

			if (!Parser_SkipWhiteChar()) {
				return false;
			}

			if (*pc_ == ',') {
				pc_++;	// continue parse
			}
			else if (*pc_ == ')') {
				// function end
				return true;
			}
		}
		else if ((*pc_ >= '0' && *pc_ <= '9') || *pc_ == '-') {
			char* num_start = pc_;

			if (!Parser_SkipNumberChar()) {
				return false;
			}

			char backup_c = *pc_;
			*pc_ = '\0';

			double num = atof(num_start);

			*pc_ = backup_c;

			arg_s* new_arg = AllocArg();
			if (!new_arg) {
				return false;
			}

			new_arg->next_ = nullptr;
			new_arg->type_ = arg_s::type_t::DBL;
			new_arg->dbl_ = num;

			AddArgToFunc(func, new_arg);

			if (!Parser_SkipWhiteChar()) {
				return false;
			}

			if (*pc_ == ',') {
				pc_++;	// continue parse
			}
			else if (*pc_ == ')') {
				// function end
				return true;
			}
		}
		else if (!strncmp(pc_, "TRUE", 4)) {

			pc_ += 4;	// strlen("TRUE")

			arg_s* new_arg = AllocArg();
			if (!new_arg) {
				return false;
			}

			new_arg->next_ = nullptr;
			new_arg->type_ = arg_s::type_t::BOOL;
			new_arg->bool_ = 1;

			AddArgToFunc(func, new_arg);

			if (!Parser_SkipWhiteChar()) {
				return false;
			}

			if (*pc_ == ',') {
				pc_++;	// continue parse
			}
			else if (*pc_ == ')') {
				// function end
				return true;
			}
		}
		else if (!strncmp(pc_, "FALSE", 5)) {
			pc_ += 5;	// strlen("FALSE")

			arg_s* new_arg = AllocArg();
			if (!new_arg) {
				return false;
			}

			new_arg->next_ = nullptr;
			new_arg->type_ = arg_s::type_t::BOOL;
			new_arg->bool_ = 0;

			AddArgToFunc(func, new_arg);

			if (!Parser_SkipWhiteChar()) {
				return false;
			}

			if (*pc_ == ',') {
				pc_++;	// continue parse
			}
			else if (*pc_ == ')') {
				// function end
				return true;
			}
		}
		else {
			func_s* nested_func = ParseFunc();
			if (!nested_func) {
				return false;
			}

			arg_s* new_arg = AllocArg();
			if (!new_arg) {
				return false;
			}

			new_arg->next_ = nullptr;
			new_arg->type_ = arg_s::type_t::FUNC;
			new_arg->func_ = nested_func;

			AddArgToFunc(func, new_arg);

			if (!Parser_SkipWhiteChar()) {
				return false;
			}

			if (*pc_ == ',') {
				pc_++;	// continue parse
			}
			else if (*pc_ == ')') {
				// function end
				return true;
			}
		}

	}
}

void QSC::AddFunc(func_s* func) {
	root_funcs_[root_func_count_++] = func;
}

void QSC::AddArgToFunc(func_s* func, arg_s* arg) {
	if (func->args_) {
		arg_s* tail = func->args_;
		while (tail->next_) {
			tail = tail->next_;
		}

		tail->next_ = arg;
	}
	else {
		func->args_ = arg;
	}
}
