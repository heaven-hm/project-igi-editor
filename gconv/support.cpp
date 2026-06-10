// gconv/support.cpp — minimal standalone implementations of symbols used by
// the parsers (Logger::Log, Utils::Trim, Log, Mem_Alloc, File_LoadBinary,
// File_FreeBuf, and helpers they depend on). No GL/app dependencies.

#include "pch.h"
#include "logger.h"   // resolves to source/logger.h via source/ include dir
#include "utils.h"    // resolves to source/utils.h
#include "config.h"   // resolves to source/config.h
#include <cstdarg>
#include <cstdio>
#include <mutex>

// ---------------------------------------------------------------------------
// glm constants declared in common.h
// ---------------------------------------------------------------------------
const glm::vec3 VEC3_ORIGIN(0.0f, 0.0f, 0.0f);
const glm::vec3 VEC3_X_DIR (1.0f, 0.0f, 0.0f);
const glm::vec3 VEC3_Y_DIR (0.0f, 1.0f, 0.0f);
const glm::vec3 VEC3_Z_DIR (0.0f, 0.0f, 1.0f);

float RENDER_Z_NEAR = 10.0f;
float WORLD_Z_NEAR  = 10.0f;

// ---------------------------------------------------------------------------
// quit_exception (declared in common.h, defined in common.cpp)
// ---------------------------------------------------------------------------
quit_exception::quit_exception(const char* msg) {
    strncpy_s(msg_, sizeof(msg_), msg, sizeof(msg_) - 1);
}
const char* quit_exception::what() const noexcept { return msg_; }

// ---------------------------------------------------------------------------
// Str helpers
// ---------------------------------------------------------------------------
void Str_Copy(char* dst, int dst_cap, const char* src) {
    strncpy_s(dst, (size_t)dst_cap, src, (size_t)(dst_cap - 1));
}
void Str_VSPrintf(char* dst, int dst_cap, const char* fmt, va_list argptr) {
    vsnprintf(dst, (size_t)dst_cap, fmt, argptr);
}
void Str_SPrintf(char* dst, int dst_cap, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, (size_t)dst_cap, fmt, ap);
    va_end(ap);
}

// ---------------------------------------------------------------------------
// Arg helpers (parsers may call these indirectly)
// ---------------------------------------------------------------------------
int Arg_OptionIdx(int argc, char** argv, const char* arg_name) {
    for (int i = 1; i < argc; ++i)
        if (strcmp(argv[i], arg_name) == 0) return i;
    return -1;
}
int Arg_ReadInt(int argc, char** argv, const char* arg_name, int default_value) {
    int idx = Arg_OptionIdx(argc, argv, arg_name);
    if (idx > 0 && idx < argc - 1) return atoi(argv[idx + 1]);
    return default_value;
}
float Arg_ReadFloat(int argc, char** argv, const char* arg_name, float default_value) {
    int idx = Arg_OptionIdx(argc, argv, arg_name);
    if (idx > 0 && idx < argc - 1) return (float)atof(argv[idx + 1]);
    return default_value;
}
const char* Arg_ReadStr(int argc, char** argv, const char* arg_name, const char* default_value) {
    int idx = Arg_OptionIdx(argc, argv, arg_name);
    if (idx > 0 && idx < argc - 1) return argv[idx + 1];
    return default_value;
}

// ---------------------------------------------------------------------------
// Logger (minimal: print to stderr, no config/file dependency)
// ---------------------------------------------------------------------------
void Logger::Init(const std::string& /*logFile*/) {}

void Logger::Log(LogLevel level, const std::string& message) {
    const char* tag = "[INFO]";
    if      (level == LogLevel::DEBUG)   tag = "[DEBUG]";
    else if (level == LogLevel::WARNING) tag = "[WARN]";
    else if (level == LogLevel::ERR)     tag = "[ERROR]";
    else if (level == LogLevel::FATAL)   tag = "[FATAL]";

    auto& out = (level == LogLevel::ERR || level == LogLevel::FATAL) ? std::cerr : std::cout;
    out << tag << " " << message << "\n";

    std::lock_guard<std::mutex> lock(mutex_);
    if (entries_.size() < 1000)
        entries_.push_back({level, message, ""});
}

// ---------------------------------------------------------------------------
// Log (old-style C variadic, declared in common.h)
// ---------------------------------------------------------------------------
void Log(log_type_t tp, const char* file, int line, const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    LogLevel level = LogLevel::INFO;
    if      (tp == log_type_t::LOG_FATAL) level = LogLevel::FATAL;
    else if (tp == log_type_t::LOG_ERROR) level = LogLevel::ERR;

    Logger::Get().Log(level, buf);

    if (tp == log_type_t::LOG_FATAL) {
        char buf2[1024];
        snprintf(buf2, sizeof(buf2), "%s [%s:%d]", buf, file, line);
        throw quit_exception(buf2);
    }
}

// ---------------------------------------------------------------------------
// Memory allocator
// ---------------------------------------------------------------------------
#ifdef _WIN32
# define GCONV_ALIGNED_ALLOC(sz, align)  _aligned_malloc(sz, align)
# define GCONV_ALIGNED_FREE(ptr)         _aligned_free(ptr)
#else
# define GCONV_ALIGNED_ALLOC(sz, align)  aligned_alloc(static_cast<std::size_t>(align), sz)
# define GCONV_ALIGNED_FREE(ptr)         free(ptr)
#endif

struct alignas(16) mem_alloc_head_s {
    uint32_t           magic_;
    int                line_;
    size_t             size_;
    const char*        file_;
    mem_alloc_head_s*  prior_;
    mem_alloc_head_s*  next_;
};
static const uint32_t MEM_ALLOC_MAGIC = 0xABCDEFAB;
static std::mutex     g_mtx_mem;
static mem_alloc_head_s* g_mem_alloc_chain = nullptr;

void* Mem_Alloc(size_t sz, const char* file, int line) {
    std::lock_guard<std::mutex> lock(g_mtx_mem);
    size_t alloc_sz = sizeof(mem_alloc_head_s) + sz;
    auto* m = (mem_alloc_head_s*)GCONV_ALIGNED_ALLOC(alloc_sz, 16);
    if (!m) return nullptr;
    m->magic_ = MEM_ALLOC_MAGIC;
    m->line_  = line;
    m->size_  = sz;
    m->file_  = file;
    m->prior_ = nullptr;
    m->next_  = g_mem_alloc_chain;
    if (g_mem_alloc_chain) g_mem_alloc_chain->prior_ = m;
    g_mem_alloc_chain = m;
    return m + 1;
}
void Mem_Free(void* ptr, const char* /*file*/, int /*line*/) {
    if (!ptr) return;
    std::lock_guard<std::mutex> lock(g_mtx_mem);
    auto* m = (mem_alloc_head_s*)((size_t)ptr - sizeof(mem_alloc_head_s));
    if (m->magic_ != MEM_ALLOC_MAGIC) return;
    if (g_mem_alloc_chain == m) g_mem_alloc_chain = m->next_;
    else { m->prior_->next_ = m->next_; if (m->next_) m->next_->prior_ = m->prior_; }
    m->magic_ = 0;
    GCONV_ALIGNED_FREE(m);
}
void Mem_FreeAll() {
    std::lock_guard<std::mutex> lock(g_mtx_mem);
    auto* p = g_mem_alloc_chain;
    while (p) { auto* n = p->next_; GCONV_ALIGNED_FREE(p); p = n; }
    g_mem_alloc_chain = nullptr;
}
void Mem_Print() {}

// ---------------------------------------------------------------------------
// File I/O
// ---------------------------------------------------------------------------
bool File_LoadBinary(const char* filename, void*& buf, int32_t& len) {
    buf = nullptr; len = 0;
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, filename, "rb");
#else
    f = fopen(filename, "rb");
#endif
    if (!f) { Log(log_type_t::LOG_ERROR, __FILE__, __LINE__, "could not read \"%s\"\n", filename); return false; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = Mem_Alloc((size_t)sz, __FILE__, __LINE__);
    if (!buf) { fclose(f); return false; }
    int rd = (int)fread(buf, 1, sz, f);
    fclose(f);
    if (rd != sz) { Mem_Free(buf, __FILE__, __LINE__); buf = nullptr; return false; }
    len = sz;
    return true;
}
bool File_SaveBinary(const char* filename, const void* buf, int32_t len) {
    FILE* f = nullptr;
#ifdef _WIN32
    fopen_s(&f, filename, "wb");
#else
    f = fopen(filename, "wb");
#endif
    if (!f) return false;
    size_t wr = fwrite(buf, 1, (size_t)len, f);
    fclose(f);
    return (int)wr == len;
}
void File_FreeBuf(void* buf) {
    if (buf) Mem_Free(buf, __FILE__, __LINE__);
}

// ---------------------------------------------------------------------------
// Config::Get() stub (Logger::Log references it in the real logger.cpp;
// gconv provides its own Logger::Log above so this is only needed if other
// parsers call Config::Get directly)
// ---------------------------------------------------------------------------
static ConfigData g_config_data{};
ConfigData& Config::Get() {
    return g_config_data;
}

// ---------------------------------------------------------------------------
// Pic_FreePics (used by cmd_terrain)
// ---------------------------------------------------------------------------
void Pic_FreePics(pics_s& pics) {
    if (pics.pics_) {
        for (int i = 0; i < pics.num_pic_; ++i) {
            pic_s* p = pics.pics_ + i;
            if (p->pixels_) {
                Mem_Free(p->pixels_, __FILE__, __LINE__);
            }
        }
        Mem_Free(pics.pics_, __FILE__, __LINE__);
        pics.pics_ = nullptr;
    }
    pics.num_pic_ = 0;
}

// ---------------------------------------------------------------------------
// Utils::Trim (used by dat_parser)
// ---------------------------------------------------------------------------
std::string Utils::Trim(const std::string& str) {
    size_t start = str.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    size_t end = str.find_last_not_of(" \t\r\n");
    return str.substr(start, end - start + 1);
}
