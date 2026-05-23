/******************************************************************************
 * @file    common.cpp
 * @brief   common stuff
 *****************************************************************************/

#include "pch.h"
#include <mutex>
#include <stdarg.h>
#include "logger.h"
#include "config.h"
#include "utils.h"

#include <string>
#include <filesystem>
#include <vector>
#include <utility>


#if defined(_WIN32)
# define WIN32_LEAN_AND_MEAN
# include <windows.h>
# include <shlobj.h>
#endif

#if defined(__GNUC__)
# include <string>
# include <uchar.h>
# include <locale>
# include <codecvt>
#endif

#if defined(__linux__)
# include <unistd.h>
#endif

const glm::vec3 VEC3_ORIGIN(0.0f);
const glm::vec3 VEC3_X_DIR(1.0f, 0.0f, 0.0f);
const glm::vec3 VEC3_Y_DIR(0.0f, 1.0f, 0.0f);
const glm::vec3 VEC3_Z_DIR(0.0f, 0.0f, 1.0f);

/*
================================================================================
 memory
================================================================================
*/

#if defined(_MSC_VER)
# define ALIGNED_ALLOC(sz, align)		_aligned_malloc(sz, align)
# define ALIGNED_FREE(ptr)				_aligned_free(ptr)
#endif

#if defined(__GNUC__)
# define ALIGNED_ALLOC(sz, align)		aligned_alloc(static_cast<std::size_t>(align), sz)
# define ALIGNED_FREE(ptr)				free(ptr)
#endif

struct alignas(16) mem_alloc_head_s {
	uint32_t			magic_;
	int					line_;
	size_t				size_;
	const char*			file_;
	mem_alloc_head_s *	prior_;
	mem_alloc_head_s *	next_;
};

const uint32_t	MEM_ALLOC_MAGIC = 0xABCDEFAB;

static std::mutex g_mtx_mem;
static mem_alloc_head_s * g_mem_alloc_chain;

void* Mem_Alloc(size_t sz, const char* file, int line) {
	std::lock_guard<std::mutex> lock(g_mtx_mem);

	size_t alloc_sz = sizeof(mem_alloc_head_s) + sz;

	mem_alloc_head_s * m = (mem_alloc_head_s*)ALIGNED_ALLOC(alloc_sz, 16);
	if (!m) {
		return nullptr;
	}

	m->magic_ = MEM_ALLOC_MAGIC;
	m->line_ = line;
	m->size_ = sz;
	m->file_ = file;
	m->prior_ = nullptr;
	m->next_ = nullptr;

	// add to head
	if (g_mem_alloc_chain) {
		g_mem_alloc_chain->prior_ = m;
		m->next_ = g_mem_alloc_chain;
	}

	g_mem_alloc_chain = m;

	return m + 1;
}

void Mem_Free(void* ptr, const char* file, int line) {
	if (ptr) {
		std::lock_guard<std::mutex> lock(g_mtx_mem);

		mem_alloc_head_s* m = (mem_alloc_head_s*)((size_t)ptr - sizeof(mem_alloc_head_s));
		if (m->magic_ != MEM_ALLOC_MAGIC) {
			Log(log_type_t::LOG_FATAL, file, line, "bad ptr");
		}
		else {

			// remove from chain
			if (g_mem_alloc_chain == m) {
				g_mem_alloc_chain = m->next_;
			}
			else {
				m->prior_->next_ = m->next_;
				if (m->next_) {
					m->next_->prior_ = m->prior_;
				}
			}

			m->magic_ = 0;
			ALIGNED_FREE(m);
		}
	}
}

void Mem_FreeAll() {
	std::lock_guard<std::mutex> lock(g_mtx_mem);

	mem_alloc_head_s* p = g_mem_alloc_chain;
	while (p) {
		mem_alloc_head_s* n = p->next_;
		ALIGNED_FREE(p);
		p = n;
	}

	g_mem_alloc_chain = nullptr;
}

void Mem_Print() {
	std::lock_guard<std::mutex> lock(g_mtx_mem);

	mem_alloc_head_s* p = g_mem_alloc_chain;
	while (p) {
		printf("alloc_size: %d, [%s:%d]\n", (int)p->size_, p->file_, p->line_);
		p = p->next_;
	}
}

/*
================================================================================
 quit_exception
================================================================================
*/
quit_exception::quit_exception(const char* msg) {
	Str_Copy(msg_, 1024, msg);
}

const char* quit_exception::what() const noexcept {
	return msg_;
}

/*
================================================================================
 arg
================================================================================
*/

int	Arg_OptionIdx(int argc, char** argv, const char* arg_name) {
	for (int i = 1; i < argc; ++i) {
		if (STRCASECMP(argv[i], arg_name) == 0) {
			return i;
		}
	}
	return -1;
}

int	Arg_ReadInt(int argc, char** argv, const char* arg_name, int default_value) {
	int opt_idx = Arg_OptionIdx(argc, argv, arg_name);
	if (opt_idx > 0 && opt_idx < (argc - 1)) {
		return Str_ToInt(argv[opt_idx + 1]);
	}
	else {
		return default_value;
	}
}

float Arg_ReadFloat(int argc, char** argv, const char* arg_name, float default_value) {
	int opt_idx = Arg_OptionIdx(argc, argv, arg_name);
	if (opt_idx > 0 && opt_idx < (argc - 1)) {
		return Str_ToFloat(argv[opt_idx + 1]);
	}
	else {
		return default_value;
	}
}

const char* Arg_ReadStr(int argc, char** argv, const char* arg_name, const char* default_value) {
	int opt_idx = Arg_OptionIdx(argc, argv, arg_name);
	if (opt_idx > 0 && opt_idx < (argc - 1)) {
		return argv[opt_idx + 1];
	}
	else {
		return default_value;
	}
}

/*
================================================================================
 log
================================================================================
*/
void Log(log_type_t tp, const char* file, int line, const char* fmt, ...) {
	char buf[1024];

	va_list argptr;
	va_start(argptr, fmt);
	Str_VSPrintf(buf, 1024, fmt, argptr);
	va_end(argptr);

	LogLevel level = LogLevel::INFO;
	if (tp == log_type_t::LOG_FATAL) level = LogLevel::FATAL;
	else if (tp == log_type_t::LOG_ERROR) level = LogLevel::ERR;

	Logger::Get().Log(level, buf);

	if (tp == log_type_t::LOG_FATAL) {
		char buf2[1024];
		Str_SPrintf(buf2, 1024, "%s [%s:%d]", buf, file, line);
		throw quit_exception(buf2);
	}

}

/*
================================================================================
 time
================================================================================
*/
int64_t Sys_Milliseconds() {
	auto time_now = std::chrono::system_clock::now();
	auto duration_in_ms = std::chrono::duration_cast<std::chrono::milliseconds>(time_now.time_since_epoch());
	return duration_in_ms.count();
}

/*
================================================================================
 string
================================================================================
*/
void Str_Copy(char* dst, int dst_cap, const char* src) {
	if (dst == src) {
		return;
	}

#if defined(_MSC_VER)
	errno_t e = strncpy_s(dst, dst_cap, src, _TRUNCATE);
	if (e) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "strncpy_s error: %d\n", (int)e);
	}
#endif

#if defined(__GNUC__)
	strncpy(dst, src, dst_cap - 1);
	size_t l = strlen(dst);
	dst[l] = 0;
#endif
}

void Str_Cat(char* dst, int dst_cap, const char* src) {
#if defined(_MSC_VER)
	errno_t e = strncat_s(dst, dst_cap, src, _TRUNCATE);
	if (e) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "strncat_s error: %d\n", (int)e);
	}
#endif

#if defined(__GNUC__)
	int l = (int)strlen(dst);
	if (dst_cap > (l + 1)) {
		strncat(dst, src, dst_cap - l - 1);
	}
#endif
}

void Str_VSPrintf(char* dst, int dst_cap, const char* fmt, va_list argptr) {
#if defined(_MSC_VER)
	int count = vsnprintf_s(dst, (size_t)dst_cap, _TRUNCATE, fmt, argptr);
	if (count < 0) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "vsnprintf_s error\n");
	}
#endif

#if defined(__GNUC__)
	vsnprintf(dst, dst_cap, fmt, argptr);
#endif
}

void Str_SPrintf(char* dst, int dst_cap, const char* fmt, ...) {
	va_list argptr;
	va_start(argptr, fmt);
	Str_VSPrintf(dst, dst_cap, fmt, argptr);
	va_end(argptr);
}

int	Str_UTF8ToUTF16(const char* utf8, char16_t* utf16, int utf16_chars) {
#if defined(_WIN32)
	if (!utf8) {
		if (utf16 && utf16_chars > 0) { // treat as null string
			utf16[0] = 0;
		}
		return 0;
	}

	int utf8_bytes = (int)strlen(utf8);
	if (utf8_bytes < 1) {
		if (utf16 && utf16_chars > 0) {
			utf16[0] = 0;
		}
		return 0;
	}

	int need_chars = MultiByteToWideChar(CP_UTF8, 0 /* do not using MB_PRECOMPOSED */, utf8, utf8_bytes, nullptr, 0); // to UTF-16, returns the number of characters
	if (!need_chars) {
		DWORD e = GetLastError();
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "MultiByteToWideChar error: %u\n", e);
		return 0;
	}

	if (!utf16) {
		return need_chars + 1;
	}

	assert(utf16);
	assert(utf16_chars > 0);

	if (utf16_chars < need_chars + 1) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "utf16 buffer too small\n");
		utf16[0] = 0;
		return 0;
	}

	int translen = MultiByteToWideChar(CP_UTF8, 0 /* do not using MB_PRECOMPOSED */, utf8, utf8_bytes, (LPWSTR)utf16, utf16_chars);
	utf16[translen] = 0;

	return translen;
#elif defined(__linux__)
	std::u16string u16_conv = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.from_bytes(utf8);
	if (u16_conv.size() >= utf16_chars) {
		Log(log_type_t::LOG_ERROR,  __FILE__, __LINE__,  "utf16 buffer too small\n");
		utf16[0] = 0;
		return 0;
	}
	else {
		memcpy(utf16, u16_conv.c_str(), sizeof(char16_t) * u16_conv.size());
		return (int)u16_conv.size();
	}
#else
# error "unsupported platform"
#endif
}

int Str_UTF16ToUTF8(const char16_t* utf16, char* utf8, int utf8_bytes) {
#if defined(_WIN32)
	if (!utf16) {
		if (utf8 && utf8_bytes > 0) { // treat as null string
			utf8[0] = 0;
		}
		return 0;
	}

	int utf16_chars = (int)wcslen((const wchar_t*)utf16);
	if (utf16_chars < 1) {
		if (utf8 && utf8_bytes > 0) {
			utf8[0] = 0;
		}
		return 0;
	}

	int need_bytes = WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)utf16, utf16_chars, nullptr, 0, nullptr, nullptr); // return bytes count
	if (!utf8) {
		return need_bytes + 1;
	}

	assert(utf8);
	assert(utf8_bytes > 0);

	if (utf8_bytes < need_bytes + 1) {
		utf8[0] = 0;
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "utf8 buffer too small\n");
		return 0;
	}

	int translen = WideCharToMultiByte(CP_UTF8, 0, (LPCWSTR)utf16, utf16_chars, utf8, utf8_bytes, nullptr, nullptr);
	utf8[translen] = 0;

	return translen;
#elif defined(__linux__)
	std::string u8_conv = std::wstring_convert<std::codecvt_utf8_utf16<char16_t>, char16_t>{}.to_bytes(utf16);
	if (u8_conv.size() >= utf8_bytes) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "utf8 buffer too small\n");
		utf8[0] = 0;
		return 0;
	}
	else {
		memcpy(utf8, u8_conv.c_str(), sizeof(char) * u8_conv.size());
		return (int)u8_conv.size();
	}
#else
# error "unsupported platform"
#endif
}

static bool IsDirectorySeparator(char c) {
	return c == '\\' || c == '/';
}

bool Str_ExtractFileName(const char* path, char* filename, int filename_cap) {
	filename[0] = 0;
	int l = (int)strlen(path);
	for (int i = l - 1; i >= 0; --i) {
		if (IsDirectorySeparator(path[i])) {
			int filename_element_count = l - i - 1;
			if (filename_cap <= filename_element_count) {
				Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "filename buffer overflow\n");
				return false;
			}
			memcpy(filename, path + i + 1, filename_element_count);
			filename[filename_element_count] = 0;
			return true;
		}
	}

	// no \ or / character been found
	if (filename_cap <= l) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "filename buffer overflow\n");
		return false;
	}

	Str_Copy(filename, filename_cap, path);
	return true;
}

bool Str_ExtractFileDirSelf(char* path) {
	int l = (int)strlen(path);
	for (int i = l - 1; i >= 0; --i) {
		if (IsDirectorySeparator(path[i])) {
			path[i] = 0;
			return true;
		}
	}
	path[0] = 0;	// return empty string
	return false;
}

static char* FindDoubleDots(char* path) {
	char* pc = strstr(path, "..\\");
	if (!pc) {
		pc = strstr(path, "../");
	}

	return pc;
}

static char* RFindSlash(char* path) {
	int len = (int)strlen(path);

	for (int i = len - 1; i >= 0; --i) {
		char c = path[i];
		if (c == '\\' || c == '/') {
			return path + i;
		}
	}

	return nullptr;
}

void Str_EraseDoubleDotsInPath(char* path) {
	char sl[2][1024];

	bool end_with_double_dots = false;
	int l = (int)strlen(path);
	if (l >= 2 && path[l - 1] == '.' && path[l - 2] == '.') {
		end_with_double_dots = true;
		path[l] = '/';	// add a '/
		path[l + 1] = '\0';
	}

	Str_Copy(sl[0], MAX_PATH, path);
	Str_Copy(sl[1], MAX_PATH, path);

	int srcidx = 0;
	int dstidx = 1 - srcidx;

	char * pc = FindDoubleDots(sl[srcidx]);
	while (pc) {
		*pc = 0;

		char * pc1 = RFindSlash(sl[srcidx]);
		if (pc1) {
			*pc1 = 0;

			char * pc2 = RFindSlash(sl[srcidx]);
			if (pc2) {
				pc2[1] = 0;
				Str_Copy(sl[dstidx], MAX_PATH, sl[srcidx]);
				Str_Cat(sl[dstidx], MAX_PATH, pc + 3);

				srcidx = 1 - srcidx;
				dstidx = 1 - dstidx;

				pc = FindDoubleDots(sl[srcidx]);
			}
			else {
				break;
			}

		}
		else {
			break;
		}
	}

	Str_Copy(path, MAX_PATH, sl[srcidx]);

	if (end_with_double_dots) {
		l = (int)strlen(path);
		if (l && (path[l - 1] == '/' || path[l - 1] == '\\')) {
			path[l - 1] = '\0';
		}
	}
}

int Str_ToInt(const char* s) {
	int i = 0;

	try {
		i = std::stoi(s);
	}
	catch (const std::invalid_argument&) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "std::stoi raise exception std::invalid_argument: \"%s\"\n", s);
	}
	catch (const std::out_of_range&) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "std::stoi raise exception std::out_of_range: \"%s\"\n", s);
	}

	return i;
}

float Str_ToFloat(const char* s) {
	float f = 0.0f;

	try {
		f = std::stof(s);
	}
	catch (const std::invalid_argument&) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "std::stof raise exception std::invalid_argument: \"%s\"\n", s);
	}
	catch (const std::out_of_range&) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "std::stof raise exception std::out_of_range: \"%s\"\n", s);
	}

	return f;
}

/*
================================================================================
 file
================================================================================
*/
bool File_Exists(const char* filename) {
#if defined(_WIN32)
	char16_t utf16[1024];
	Str_UTF8ToUTF16(filename, utf16, 1024);

	DWORD code = GetFileAttributes((LPCWSTR)utf16);

	if (INVALID_FILE_ATTRIBUTES == code) {
		return false;
	}
	else if (code & FILE_ATTRIBUTE_ARCHIVE) {
		return true;
	}
	else if (code == FILE_ATTRIBUTE_NORMAL) {
		// A file that does not have other attributes set. 
		// This attribute is valid only when used alone.
		return true;
	}
	else {
		return false;
	}
#elif defined(__linux__)
	return access(filename, F_OK) == 0;
#else
# error "unsupported platform"
#endif
}

FILE* File_Open(const char* filename, const char* mod) {
	// SECURITY: Strictly prohibit WRITING to the QFiles directory
	bool isWriteMode = (strpbrk(mod, "wa+") != nullptr);
	if (isWriteMode && filename) {
		std::string lowerPath = filename;
		std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
		if (lowerPath.find("qfiles") != std::string::npos) {
			Logger::Get().Log(LogLevel::ERR, "[File_Open] CRITICAL ERROR: Blocked attempt to WRITE to READ-ONLY QFiles: " + std::string(filename));
			return nullptr;
		}
	}

#if defined(_MSC_VER)
	char16_t char16_filename[1024], char16_mod[64];
	Str_UTF8ToUTF16(filename, char16_filename, 1024);
	Str_UTF8ToUTF16(mod, char16_mod, 64);

	FILE* f = nullptr;
	errno_t e = _wfopen_s(&f, (const wchar_t*)char16_filename, (const wchar_t*)char16_mod);

	if (!f) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "open file \"%s\" in mode \"%s\" failed, error no: %d\n", filename, mod, (int)e);
	}

	return f;
#endif

#if defined(__GNUC__)
	FILE* f = fopen(filename, mod);
	return f;
#endif
}

bool File_LoadText(const char* filename, char *& buf) {
	buf = nullptr;

	FILE* f = File_Open(filename, "rb");

	if (!f) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "could not read file \"%s\"\n", filename);
		return false;
	}

	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);

	buf = (char*)MEM_ALLOC(sz + 1);

	if (!buf) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "memory overflow\n");
		fclose(f);
		return false;
	}

	int read_size = (int)fread(buf, 1, sz, f);
	fclose(f);

	if (read_size != sz) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "read file \"%s\" error\n", filename);
		MEM_FREE_(buf);
		return false;
	}
	else {
		buf[sz] = 0;
		return true;
	}
}

bool File_LoadBinary(const char* filename, void*& buf, int32_t& len) {
	buf = nullptr;
	len = 0;

	FILE* f = File_Open(filename, "rb");

	if (!f) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "could not read file \"%s\"\n", filename);
		return false;
	}

	fseek(f, 0, SEEK_END);
	long sz = ftell(f);
	fseek(f, 0, SEEK_SET);

	buf = (char*)MEM_ALLOC(sz);
	if (!buf) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "memory overflow\n");
		fclose(f);
		return false;
	}

	int read_size = (int)fread(buf, 1, sz, f);
	fclose(f);

	if (read_size != sz) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "read file \"%s\" error\n", filename);
		MEM_FREE_(buf);
		return false;
	}
	else {
		len = sz;
		return true;
	}
}

bool File_SaveBinary(const char* filename, const void* buf, int32_t len) {
	FILE* f = File_Open(filename, "wb");

	if (!f) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "could not write file \"%s\"\n", filename);
		return false;
	}

	size_t write_size = fwrite(buf, 1, len, f);
	fclose(f);

	if (write_size != (size_t)len) {
		Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "incomplete write to file \"%s\"\n", filename);
		return false;
	}

	return true;
}

void File_FreeBuf(void * buf) {
	if (buf) {
		MEM_FREE_(buf);
	}
}

/*
================================================================================
  converted rgba picture
================================================================================
*/
void Pic_SaveToBMP(const pic_s* pic, const char* filename) {
#pragma pack(push, 1)

	struct bmpfilehead_s {
		uint16_t				bfType;
		uint32_t				bfSize;
		uint16_t				bfReserved1;
		uint16_t				bfReserved2;
		uint32_t				bfOffBits;
	};

	struct bmpinfohead_s {
		uint32_t				biSize;
		int						biWidth;
		int						biHeight;
		uint16_t				biPlanes;
		uint16_t				biBitCount;
		uint32_t				biCompression;
		uint32_t				biSizeImage;
		int						biXPelsPerMeter;
		int						biYPelsPerMeter;
		uint32_t				biClrUsed;
		uint32_t				biClrImportant;
	};

#pragma pack(pop)

	uint8_t* dst_line_buf = (uint8_t*)MEM_ALLOC(pic->width_ * 4);
	if (!dst_line_buf) {
		return;
	}

	bmpfilehead_s head = {};

	head.bfType = 0x4d42;
	head.bfOffBits = (uint32_t)(sizeof(bmpfilehead_s) + sizeof(bmpinfohead_s));
	head.bfSize = head.bfOffBits + pic->width_ * pic->height_ * 4;
	head.bfReserved1 = 0;
	head.bfReserved2 = 0;

	bmpinfohead_s info = {};

	info.biBitCount = 32;
	info.biClrImportant = 0;
	info.biClrUsed = 0;
	info.biCompression = 0;
	info.biHeight = pic->height_;
	info.biPlanes = 1;
	info.biSize = (uint32_t)sizeof(bmpinfohead_s);
	info.biSizeImage = pic->width_ * pic->height_ * 4;
	info.biWidth = pic->width_;
	info.biXPelsPerMeter = 0;
	info.biYPelsPerMeter = 0;

	FILE* f = File_Open(filename, "wb");
	if (!f) {
		MEM_FREE_(dst_line_buf);
		printf("Could not create file \"%s\"\n", filename);
		return;
	}

	fwrite(&head, sizeof(head), 1, f);
	fwrite(&info, sizeof(info), 1, f);

	for (int i = 0; i < pic->height_; ++i) {
		const uint8_t* src_line = pic->pixels_ + pic->width_ * 4 * i;
		uint8_t* dst_line = dst_line_buf;

		for (int j = 0; j < pic->width_; ++j) {
			const uint8_t* src_pixel = src_line + j * 4;
			uint8_t* dst_pixel = dst_line + j * 4;

			dst_pixel[0] = src_pixel[2];	// b
			dst_pixel[1] = src_pixel[1];	// g
			dst_pixel[2] = src_pixel[0];	// r
			dst_pixel[3] = src_pixel[3];	// a
		}

		fwrite(dst_line_buf, 4, pic->width_, f);
	}

	fclose(f);
	f = nullptr;

	MEM_FREE_(dst_line_buf);
}

void Pic_FreePics(pics_s& pics) {
	if (pics.pics_) {
		for (int i = 0; i < pics.num_pic_; ++i) {
			pic_s* p = pics.pics_ + i;
			if (p->pixels_) {
				MEM_FREE_(p->pixels_);
			}
		}

		MEM_FREE_(pics.pics_);
		pics.pics_ = nullptr;
	}

	pics.num_pic_ = 0;
}


/*
================================================================================
 folders_s
================================================================================
*/
folders_s g_folders;
bool g_isCLIMode = false;

void Folders_Init() {
	if (g_isCLIMode) {
		return;
	}

	std::string exeDir = Utils::GetExeDirectory();
	std::string toolsPath = exeDir + "\\content\\tools";

	namespace fs = std::filesystem;
	try {
		fs::create_directories(toolsPath + "\\restore");
	} catch (...) {}

	Str_SPrintf(g_folders.res_folder_, 1024, "%s/restore", toolsPath.c_str());
	Str_SPrintf(g_folders.objects_folder_, 1024, "%s/objects", toolsPath.c_str());
	Str_SPrintf(g_folders.buildings_folder_, 1024, "%s/buildings", toolsPath.c_str());
	Str_SPrintf(g_folders.ai_folder_, 1024, "%s/ai", toolsPath.c_str());

	Str_SPrintf(g_folders.shader_folder_, 1024, "%s/content/shaders", exeDir.c_str());

	// debug
	/*
	printf("res_folder:    \"%s\"\n", g_folders.res_folder_);
	printf("shader_folder: \"%s\"\n", g_folders.shader_folder_);
	//*/
}

/*
================================================================================
 math
================================================================================
*/
int get_highest_bit(int value) {
	uint32_t uvalue = *(uint32_t*)&value;
	uint32_t mask = 0x80000000;
	for (int i = 31; i >= 0; --i) {
		if (uvalue & mask) {
			return i;
		}
		mask >>= 1;
	}
	return -1;	// not found
}

void TransformDoubleVec3(const glm::mat3& mat, double v[3], double out[3]) {
	out[0] = mat[0][0] * v[0] + mat[1][0] * v[1] + mat[2][0] * v[2];
	out[1] = mat[0][1] * v[0] + mat[1][1] * v[1] + mat[2][1] * v[2];
	out[2] = mat[0][2] * v[0] + mat[1][2] * v[1] + mat[2][2] * v[2];
}

void AngleToVectors(float yaw, float pitch, float roll, glm::vec3& forward, glm::vec3& right, glm::vec3& up) {
	float yaw_in_rad = glm::radians(yaw);
	float pitch_in_rad = glm::radians(pitch);
	float roll_in_rad = glm::radians(roll);

	float cos_y = (float)std::cos(yaw_in_rad);
	float sin_y = (float)std::sin(yaw_in_rad);

	float cos_p = (float)std::cos(pitch_in_rad);
	float sin_p = (float)std::sin(pitch_in_rad);

	float cos_r = (float)std::cos(roll_in_rad);
	float sin_r = (float)std::sin(roll_in_rad);

	forward.x = -sin_y * cos_p;
	forward.y = cos_y * cos_p;
	forward.z = sin_p;

	right.x = cos_y * cos_r - sin_p * sin_y * sin_r;
	right.y = sin_y * cos_r + sin_p * cos_y * sin_r;
	right.z = -cos_p * sin_r;

	up.x = sin_y * sin_p * cos_r + cos_y * sin_r;
	up.y = sin_y * sin_r - cos_y * sin_p * cos_r;
	up.z = cos_p * cos_r;
}

void PrintMatrix3x3(const char * name, const glm::mat3& m) {
	printf("-- matrix: %s --\n", name);

	printf("[%12.6f, %12.6f, %12.6f]\n", m[0].x == -0.0f ? 0.0f : m[0].x, m[1].x == -0.0f ? 0.0f : m[1].x, m[2].x == -0.0f ? 0.0f : m[2].x);
	printf("[%12.6f, %12.6f, %12.6f]\n", m[0].y == -0.0f ? 0.0f : m[0].y, m[1].y == -0.0f ? 0.0f : m[1].y, m[2].y == -0.0f ? 0.0f : m[2].y);
	printf("[%12.6f, %12.6f, %12.6f]\n", m[0].z == -0.0f ? 0.0f : m[0].z, m[1].z == -0.0f ? 0.0f : m[1].z, m[2].z == -0.0f ? 0.0f : m[2].z);

}
