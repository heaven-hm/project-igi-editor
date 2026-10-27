#pragma once

/******************************************************************************
 * @file    res_chunks.h
 * @brief   ILFF chunk layer and resource-name identity, transcribed from IGI-RE.
 *
 * Every function here is a direct C++ transcription of the retail engine's own
 * code. The oracle is /Users/haseeb-mir/Documents/Code/IGI-RE (the decompiled
 * retail IGI.exe); the HOOK address of each engine function is given at its
 * transcription site. No behaviour is added that the engine does not have, and
 * no fallback is invented for a case the engine treats as fatal.
 *
 * Engine sources:
 *   src/res/chunk.c     Chunk_Next 0x4B7470, Chunk_FirstChild 0x4B7490,
 *                       Chunk_Data 0x4B74D0, Chunk_GetInfo 0x4B7540,
 *                       Chunk_FormType 0x4B7570, Chunk_Id 0x4B7580,
 *                       Chunk_Find 0x4B7590, gChunkError (BSS A43E5C)
 *   src/res/resource.h  Chunk layout (id, size, align, next, form) and CHUNK_ID
 *   src/res/resource.c  Resource_Create 0x4B59F0, String_ToUpper 0x4B5AF0,
 *                       Path_FixSlashes (:56-60), Resource_Hash 0x4B5B30,
 *                       Resource_IsLoaded 0x4B5B90, Resource_Add 0x4B5F80,
 *                       Resource_AddChunks 0x4B60F0, Resource_ForEach 0x4B5D80
 *   src/core/file.c     File_ResolvePath 0x4B1020 (alias expansion)
 *   src/model/model.c   ModelFile_Setup 0x4CF080 (:261, :267, :364),
 *                       ModelFile_LoadTextures 0x4CF790 (:467),
 *                       CollMesh_Load 0x4CF690 (:432)
 *
 * On-disk layout (verified against the 95 retail .res archives):
 *   root form: "ILFF" | uint32 size(TOTAL file size) | uint32 align |
 *              uint32 next | "IRES"
 *   child chunk: uint32 id | uint32 size | uint32 align | uint32 next
 *   first child of a form  = round_up(0x14, form.align)   (chunk.c:27)
 *   data of a child chunk  = parent + round_up(off + 0x10, chunk.align)
 *                            (chunk.c:47)
 *   next sibling           = chunk + chunk.next, NULL when next == 0
 *                            (chunk.c:9-15)
 *****************************************************************************/

#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>

namespace igi_res {

// ---------------------------------------------------------------------------
// Chunk identifiers — CHUNK_ID(a,b,c,d), src/res/resource.h:40
// ---------------------------------------------------------------------------
constexpr uint32_t ChunkId(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<uint8_t>(a)) |
           (static_cast<uint32_t>(static_cast<uint8_t>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<uint8_t>(c)) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(d)) << 24);
}

constexpr uint32_t kChunkIlff = ChunkId('I', 'L', 'F', 'F');
constexpr uint32_t kChunkIres = ChunkId('I', 'R', 'E', 'S');
constexpr uint32_t kChunkName = ChunkId('N', 'A', 'M', 'E');
constexpr uint32_t kChunkBody = ChunkId('B', 'O', 'D', 'Y');
constexpr uint32_t kChunkCstr = ChunkId('C', 'S', 'T', 'R');
constexpr uint32_t kChunkPath = ChunkId('P', 'A', 'T', 'H');
constexpr uint32_t kChunkDbg  = ChunkId('D', 'B', 'U', 'G');

// A chunk header as the engine sees it: src/res/resource.h:32-38. Only the
// first four words exist for a data chunk; the fifth word (form) is the form
// type of an ILFF form (root or nested) and overlaps the first four data bytes
// of a data chunk, exactly as in the engine.
struct ChunkHeader {
    uint32_t id = 0;
    uint32_t size = 0;
    uint32_t align = 0;
    uint32_t next = 0;
    uint32_t form = 0;
};

// "No such chunk" sentinel for the offset-returning helpers below. The engine
// uses NULL pointers; offsets are used here so the same rules serve both an
// in-memory buffer and a streaming reader.
constexpr uint64_t kNoChunk = ~static_cast<uint64_t>(0);

// IS_POW2 — src/res/chunk.c:7
constexpr bool IsPow2(uint32_t x) {
    return ((~((x) - 1) & (x)) == (x));
}

// gChunkError — src/res/chunk.c:5,84 (BSS A43E5C). The engine raises it when a
// chunk header fails a validity test and then returns NULL.
inline int& ChunkError() {
    static int gChunkError = 0;
    return gChunkError;
}

inline void ResetChunkError() { ChunkError() = 0; }

// Chunk_GetInfo — src/res/chunk.c:51-60, HOOK(0x4B7540)
inline void ChunkGetInfo(const ChunkHeader& c, uint32_t* size, uint32_t* align) {
    if (size) *size = c.size;
    if (align) *align = c.align;
}

// Chunk_Id — src/res/chunk.c:68-72, HOOK(0x4B7580)
inline uint32_t ChunkId(const ChunkHeader& c) { return c.id; }

// Chunk_FormType — src/res/chunk.c:62-66, HOOK(0x4B7570)
inline uint32_t ChunkFormType(const ChunkHeader& c) { return c.form; }

// ---------------------------------------------------------------------------
// Offset arithmetic — the engine's pointer arithmetic on Chunk*.
// ---------------------------------------------------------------------------

// Chunk_Next — src/res/chunk.c:9-15, HOOK(0x4B7470)
//   return c && c->next ? (char *)c + c->next : NULL;
inline uint64_t ChunkNextOffset(uint64_t chunkOffset, const ChunkHeader& c) {
    if (c.next) return chunkOffset + c.next;
    return kNoChunk;
}

// Chunk_FirstChild — src/res/chunk.c:17-32, HOOK(0x4B7490)
//   if (!c || !c->size) return NULL;
//   if (!IS_POW2(c->align)) { gChunkError = 1; return NULL; }
//   off = (c->align + 0x13) & ~(c->align - 1);
//   return off < c->size ? (char *)c + off : NULL;
inline uint64_t ChunkFirstChildOffset(uint64_t formOffset, const ChunkHeader& form) {
    if (!form.size) return kNoChunk;
    if (!IsPow2(form.align)) {
        ChunkError() = 1;
        return kNoChunk;
    }
    const uint32_t off = (form.align + 0x13) & ~(form.align - 1);
    if (off < form.size) return formOffset + off;
    return kNoChunk;
}

// Chunk_Data — src/res/chunk.c:34-49, HOOK(0x4B74D0)
//   if (!parent || !c) return NULL;
//   off = (char *)c - (char *)parent;
//   if (!IS_POW2(parent->align) || off >= parent->size ||
//       !IS_POW2(c->align) || c->align > parent->align) { gChunkError = 1; return NULL; }
//   if (!c->size) return NULL;
//   return (char *)parent + ((c->align + off + 0xF) & ~(c->align - 1));
inline uint64_t ChunkDataOffset(uint64_t chunkOffset, const ChunkHeader& c,
                               uint64_t parentOffset, const ChunkHeader& parent) {
    const uint64_t off = chunkOffset - parentOffset;
    if (!IsPow2(parent.align) || off >= parent.size ||
        !IsPow2(c.align) || c.align > parent.align) {
        ChunkError() = 1;
        return kNoChunk;
    }
    if (!c.size) return kNoChunk;
    return parentOffset + ((c.align + off + 0xF) & ~(static_cast<uint64_t>(c.align) - 1));
}

// ---------------------------------------------------------------------------
// Resource name identity — src/res/resource.c
// ---------------------------------------------------------------------------

// Path_FixSlashes — src/res/resource.c:56-60
inline void PathFixSlashes(char* s) {
    for (; *s; s++)
        *s = (*s == '\\') ? '/' : *s;
}

// String_ToUpper — src/res/resource.c:98-104, HOOK(0x4B5AF0)
// The engine calls the CRT toupper (crt_toupper, ORIG 0x4AD73D), i.e. the
// default "C" locale mapping.
inline void StringToUpper(char* dst, const char* src) {
    for (; *src; src++) {
        unsigned char c = static_cast<unsigned char>(*src);
        *dst++ = static_cast<char>((c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c);
    }
    *dst = 0;
}

// Resource_Create — src/res/resource.c:65-95, HOOK(0x4B59F0): the stored name is
// the upper-cased, '/'-separated form of the name the caller supplied.
inline std::string NormalizeResourceName(const std::string& name) {
    std::string out(name.size() + 1, '\0');
    StringToUpper(out.data(), name.c_str());
    out.resize(std::strlen(out.c_str()));
    PathFixSlashes(out.data());
    return out;
}

// Resource_Hash — src/res/resource.c:106-114, HOOK(0x4B5B30)
//   unsigned int h = 1;
//   for (; *name; name++) h = (h + 1) * *name;
//   return h % 0x3037;
// *name is a signed char in the engine, so the sign extension is reproduced.
inline uint32_t ResourceHash(const char* name) {
    uint32_t h = 1;
    for (; *name; name++)
        h = (h + 1) * static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(*name)));
    return h % 0x3037;
}

// Resource_IsLoaded name test — src/res/resource.c:124-147, HOOK(0x4B5B90):
// a match requires the same hash AND strcmp on the upper-cased, '/'-separated
// name. The engine iterates the loaded-resource list newest-first
// (gResourceList.tailpred backwards) and, for each candidate, the alias
// expansions of the requested name in File_ResolvePath order
// (src/core/file.c:24-61, HOOK(0x4B1020)). The name is compared in full: the
// engine has no basename/flattened form and no name-based fallback.
inline bool ResourceNamesEqual(const std::string& a, const std::string& b) {
    const std::string na = NormalizeResourceName(a);
    const std::string nb = NormalizeResourceName(b);
    if (ResourceHash(na.c_str()) != ResourceHash(nb.c_str())) return false;
    return na == nb;
}

// ---------------------------------------------------------------------------
// "RESOURCE" packs — Resource_Add, src/res/resource.c:281-317, HOOK(0x4B5F80)
//
// A "RESOURCE" pack is an 8-byte magic, a member count at +8, and 32-byte
// aligned member records starting at +0x20. Each record is
//   { int unk0; int size; int namelen; char name[]; }   (resource.c:273-279)
// with the body at p + ((namelen + 0x2B) & ~0x1F) and the next record at
// body + ((size + 0x1F) & ~0x1F)                        (resource.c:307-308).
// ---------------------------------------------------------------------------

// A "RESOURCE" pack is recognised by its 8-byte magic — resource.c:293.
inline bool IsResourcePack(const uint8_t* data, size_t size) {
    return data && size >= 8 && std::memcmp(data, "RESOURCE", 8) == 0;
}

// An "IRES" pack is an ILFF form whose form type is IRES — resource.c:287.
inline bool IsIlffIres(const uint8_t* data, size_t size) {
    if (!data || size < 20) return false;
    ChunkHeader h;
    std::memcpy(&h, data, 20);
    return h.id == kChunkIlff && h.form == kChunkIres;
}

// ---------------------------------------------------------------------------
// Member names the engine builds for models — the only names a model loader
// ever looks up. There is no search: the archive is chosen by the caller and
// the member name is built from these two templates.
// ---------------------------------------------------------------------------

// ModelFile_LoadTextures — src/model/model.c:467, HOOK(0x4CF790):
//   crt_sprintf(gModelPath, "LOCAL:textures/%s.tex", s);
inline std::string TextureMemberName(const std::string& textureId) {
    return "LOCAL:textures/" + textureId + ".tex";
}

// Mesh_Load — src/model/model.c:364, HOOK(0x4CF390):
//   crt_sprintf(gModelPath, "LOCAL:models/%s.mef", name);
inline std::string ModelMemberName(const std::string& modelName) {
    return "LOCAL:models/" + modelName + ".mef";
}

// ---------------------------------------------------------------------------
// Member names the engine builds for the game's SHARED textures — the ones
// Game_AddTexture interns. A different template from TextureMemberName above:
// a model's texture is "LOCAL:textures/%s.tex" (model.c:467) while these are
// "LOCAL:common/textures/%s". "snow.tex" is the weather system's
// (RainEffect_Init, src/fxobj/rain.c:82).
// ---------------------------------------------------------------------------

// Game_LoadTextures — src/game/game.c:524-534, HOOK(0x416130):
//   char path[0x100] = "LOCAL:common/textures/";
//   strcat(path, gGameTextures[i].name);
//   Texture_LoadFile(gGameTextures[i].tex, NULL, path);
// Texture_LoadFile is Resource_Load (src/file/texture.c:231-252), so the name
// below is a RESOURCE identity: it is matched upper-cased with '\' folded to
// '/' against the resource list (Resource_IsLoaded 0x4B5B90,
// src/res/resource.c:124-147). There is no basename fallback.
inline std::string GameTextureMemberName(const std::string& name) {
    return "LOCAL:common/textures/" + name;
}

// The archive that supplies the names above. The game mounts it once, before
// Game_LoadTextures walks the registered names — src/game/game.c:430:
//   gGame->textures = Resource_Load("LOCAL:common/textures/textures.res", NULL);
// In the retail install that resolves to <IGI>/common/textures/textures.res
// (measured: it carries the member "LOCAL:common/textures/snow.tex").
inline constexpr const char* kGameTexturePackName = "LOCAL:common/textures/textures.res";

// The LOCAL: device strips its own prefix and hands the remainder to the CRT
// (LocalDevice_Open, src/sysmisc/localdev.c:162-205: crt_fopen(path, how)), so a
// "LOCAL:" resource name is an on-disk path relative to the process's working
// directory — the install root when the game runs.
inline std::string LocalDeviceRelativePath(const std::string& resourceName) {
    static const char kPrefix[] = "LOCAL:";
    const std::size_t kPrefixLen = 6;
    if (resourceName.size() >= kPrefixLen) {
        bool match = true;
        for (std::size_t i = 0; i < kPrefixLen; ++i) {
            char c = resourceName[i];
            if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            if (c != kPrefix[i]) { match = false; break; }
        }
        if (match) return resourceName.substr(kPrefixLen);
    }
    return resourceName;
}

// The on-disk path of a "LOCAL:" resource under an install root. std::filesystem
// supplies the platform separator, so this is the engine's own path on Windows
// (root\common\textures\textures.res) and still a real path on a POSIX host,
// which is what makes it checkable outside the Windows build.
inline std::string LocalDevicePath(const std::string& root, const std::string& resourceName) {
    const std::filesystem::path relative(LocalDeviceRelativePath(resourceName));
    return (std::filesystem::path(root) / relative).string();
}

}  // namespace igi_res
