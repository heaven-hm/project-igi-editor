/******************************************************************************
 * @file    res_writer.cpp
 * @brief   RES archive reader and writer.
 *
 * Reader: a transcription of the engine's own resource layer. Oracle:
 * /Users/haseeb-mir/Documents/Code/IGI-RE (decompiled retail IGI.exe).
 *
 *   Resource_Add        0x4B5F80  src/res/resource.c:281-317
 *   Resource_AddChunks  0x4B60F0  src/res/resource.c:328-375
 *   Resource_Create     0x4B59F0  src/res/resource.c:65-95
 *   String_ToUpper      0x4B5AF0  src/res/resource.c:98-104
 *   Resource_Hash       0x4B5B30  src/res/resource.c:106-114
 *   Resource_IsLoaded   0x4B5B90  src/res/resource.c:124-147
 *   Resource_ForEach    0x4B5D80  src/res/resource.c:185-217
 *   Chunk_Next          0x4B7470  src/res/chunk.c:9-15
 *   Chunk_FirstChild    0x4B7490  src/res/chunk.c:17-32
 *   Chunk_Data          0x4B74D0  src/res/chunk.c:34-49
 *   Chunk_GetInfo       0x4B7540  src/res/chunk.c:51-60
 *   Chunk_FormType      0x4B7570  src/res/chunk.c:62-66
 *   Chunk_Id            0x4B7580  src/res/chunk.c:68-72
 *   Chunk_Find          0x4B7590  src/res/chunk.c:74-81
 *
 * Structure (src/res/resource.c:281-317, verified against the 95 shipped .res):
 *   "ILFF" | uint32 size (TOTAL file size) | uint32 align | uint32 next | "IRES"
 *   then repeated: NAME chunk, then one of BODY / CSTR / PATH / nested IRES,
 *   optionally followed by an ILFF/DBUG form whose CSTR child carries the info
 *   text. A "RESOURCE" pack is the second pack form the engine accepts: an
 *   8-byte magic, a member count at +8, and 32-byte-aligned member records
 *   starting at +0x20.
 *
 * The reader streams chunk headers instead of mapping the whole archive: the
 * archives are 200 MB+ and the editor is a 32-bit process. That is a memory
 * strategy, not a semantic one — every offset, validity test and branch is the
 * engine's.
 *
 * Writer: the editor's own repack path. It emits the same layout the engine's
 * Chunk_* walkers consume (root align 4 => first child at 0x14; every data
 * chunk 16-byte header + 4-byte padded body; last BODY next == 0).
 *****************************************************************************/

#include "utils.h"   // Utils::ToNativePath
#include "res_writer.h"
#include "../logger.h"
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <cstring>
#include <functional>

using igi_res::ChunkHeader;
using igi_res::kNoChunk;

static const uint32_t FOURCC_ILFF = igi_res::kChunkIlff;
static const uint32_t FOURCC_IRES = igi_res::kChunkIres;
static const uint32_t FOURCC_NAME = igi_res::kChunkName;
static const uint32_t FOURCC_BODY = igi_res::kChunkBody;
static const uint32_t FOURCC_CSTR = igi_res::kChunkCstr;
static const uint32_t FOURCC_PATH = igi_res::kChunkPath;
static const uint32_t FOURCC_DBUG = igi_res::kChunkDbg;

static uint32_t ReadU32LE(const uint8_t* p) {
    return (uint32_t)p[0]
        | ((uint32_t)p[1] << 8)
        | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

namespace {

// One member registered by Resource_AddChunks / the "RESOURCE" walk. `type` is
// the engine's resource type: RES_MEMBER or RES_PATH (src/res/resource.h:7-13).
struct MemberRecord {
    std::string name;      // NAME chunk text, exactly as stored
    std::string info;      // DBUG/CSTR text, empty when absent
    int type = 1;          // 1 = RES_MEMBER, 4 = RES_PATH
    uint64_t dataOffset = 0;
    uint64_t size = 0;
};

enum { RES_MEMBER = 1, RES_PATH = 4 };

// Random-access reader over an archive file. The engine maps the whole file
// (File_Load 0x4B18B0); the offsets it computes are reproduced here against a
// stream so a 200 MB archive never has to be resident.
class ResFileSource {
public:
    bool Open(const std::string& path, std::string& error) {
        // Paths arrive with Windows separators from the level/res code.
        file_.open(Utils::ToNativePath(path), std::ios::binary | std::ios::ate);
        if (!file_.is_open()) {
            error = "Could not open file: " + path;
            return false;
        }
        const std::streampos pos = file_.tellg();
        if (pos == std::streampos(-1)) {
            error = "Could not determine file size (tellg failed): " + path;
            return false;
        }
        size_ = static_cast<uint64_t>(pos);
        return true;
    }

    uint64_t Size() const { return size_; }

    bool ReadAt(uint64_t at, void* dst, size_t n) const {
        if (at > size_ || n > size_ - at) return false;
        file_.clear();
        file_.seekg(static_cast<std::streamoff>(at), std::ios::beg);
        if (n == 0) return true;
        file_.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(n));
        return static_cast<size_t>(file_.gcount()) == n;
    }

    // Reads a chunk header. A data chunk header is 16 bytes (id, size, align,
    // next); an ILFF form header is 20 bytes and its fifth word is the form
    // type (src/res/resource.h:32-38).
    bool ReadHeader(uint64_t at, ChunkHeader& out) const {
        out = ChunkHeader{};
        if (at > size_ || size_ - at < 16) return false;
        const size_t n = (size_ - at >= 20) ? 20u : 16u;
        uint8_t buf[20] = {0};
        if (!ReadAt(at, buf, n)) return false;
        out.id = ReadU32LE(buf);
        out.size = ReadU32LE(buf + 4);
        out.align = ReadU32LE(buf + 8);
        out.next = ReadU32LE(buf + 12);
        out.form = (n >= 20) ? ReadU32LE(buf + 16) : 0;
        return true;
    }

private:
    mutable std::ifstream file_;
    uint64_t size_ = 0;
};

// The engine keeps the name in a NUL-terminated buffer (Resource::name[0x200],
// src/res/resource.h:26); the NAME/CSTR chunk text is copied up to its NUL.
std::string ReadChunkString(const ResFileSource& src, uint64_t offset, uint32_t size) {
    if (offset == kNoChunk || size == 0) return std::string();
    std::vector<uint8_t> buf(size);
    if (!src.ReadAt(offset, buf.data(), size)) return std::string();
    size_t len = 0;
    while (len < size && buf[len] != '\0') len++;
    return std::string(reinterpret_cast<const char*>(buf.data()), len);
}

// Resource_AddChunks — src/res/resource.c:328-375, HOOK(0x4B60F0).
// Walks the children of one ILFF/IRES form, emitting one MemberRecord per
// member. Any structure the engine rejects takes the engine's error path
// (resource.c:370-373: "Error in resource file: '%s'" then halt); here it is
// reported as a failed load.
bool WalkFormMembers(const ResFileSource& src, uint64_t formOffset, const ChunkHeader& form,
                     const std::function<bool(const MemberRecord&)>& emit,
                     const std::string& path, std::string& error);

bool WalkFormMembers(const ResFileSource& src, uint64_t formOffset, const ChunkHeader& form,
                     const std::function<bool(const MemberRecord&)>& emit,
                     const std::string& path, std::string& error) {
    const auto fail = [&](const char* what) {
        error = std::string("Error in resource file: '") + path + "' (" + what + ")";
        Logger::Get().Log(LogLevel::ERR, "[RES] " + error);
        return false;
    };

    uint64_t c = igi_res::ChunkFirstChildOffset(formOffset, form);
    while (c != kNoChunk) {
        ChunkHeader ch;
        if (!src.ReadHeader(c, ch)) return fail("truncated chunk header");

        // The for-loop's increment is `c = Chunk_Next(c)`; a NAME branch
        // consumes the following chunk first (resource.c:339).
        uint64_t advanceFrom = c;
        ChunkHeader advanceHeader = ch;

        if (ch.id == FOURCC_NAME) {
            const uint64_t nameOffset = igi_res::ChunkDataOffset(c, ch, formOffset, form);
            if (nameOffset == kNoChunk) return fail("NAME chunk has no data");
            std::string name = ReadChunkString(src, nameOffset, ch.size);

            std::string info;
            const uint64_t bodyOffset = igi_res::ChunkNextOffset(c, ch);
            if (bodyOffset == kNoChunk) return fail("NAME chunk is not followed by a chunk");

            ChunkHeader body;
            if (!src.ReadHeader(bodyOffset, body)) return fail("truncated chunk header");

            // An ILFF/DBUG form right after the body carries the info text in
            // its CSTR child (resource.c:342-347).
            const uint64_t dbgOffset = igi_res::ChunkNextOffset(bodyOffset, body);
            if (dbgOffset != kNoChunk) {
                ChunkHeader dbg;
                if (src.ReadHeader(dbgOffset, dbg) &&
                    dbg.id == FOURCC_ILFF && dbg.form == FOURCC_DBUG) {
                    uint64_t s = igi_res::ChunkFirstChildOffset(dbgOffset, dbg);
                    while (s != kNoChunk) {
                        ChunkHeader sh;
                        if (!src.ReadHeader(s, sh)) break;
                        if (sh.id == FOURCC_CSTR) {
                            info = ReadChunkString(src,
                                igi_res::ChunkDataOffset(s, sh, dbgOffset, dbg), sh.size);
                        }
                        s = igi_res::ChunkNextOffset(s, sh);
                    }
                }
            }

            if (body.id == FOURCC_BODY) {
                uint32_t size = 0, align = 0;
                igi_res::ChunkGetInfo(body, &size, &align);
                MemberRecord m;
                m.name = std::move(name);
                m.info = std::move(info);
                m.type = RES_MEMBER;
                m.dataOffset = igi_res::ChunkDataOffset(bodyOffset, body, formOffset, form);
                m.size = size;
                if (m.dataOffset == kNoChunk) return fail("BODY chunk has no data");
                if (!emit(m)) return false;
            } else if (body.id == FOURCC_ILFF && body.form == FOURCC_IRES) {
                // A nested resource form: registered as a member with size 0
                // (resource.c:353) and then walked into the same pack.
                MemberRecord m;
                m.name = std::move(name);
                m.info = std::move(info);
                m.type = RES_MEMBER;
                m.dataOffset = kNoChunk;
                m.size = 0;
                if (!emit(m)) return false;
                if (!WalkFormMembers(src, bodyOffset, body, emit, path, error)) return false;
            } else if (body.id == FOURCC_CSTR) {
                uint32_t size = 0, align = 0;
                igi_res::ChunkGetInfo(body, &size, &align);
                MemberRecord m;
                m.name = std::move(name);
                m.type = RES_MEMBER;
                m.dataOffset = igi_res::ChunkDataOffset(bodyOffset, body, formOffset, form);
                m.size = size;
                if (m.dataOffset == kNoChunk) return fail("CSTR chunk has no data");
                if (!emit(m)) return false;
            } else if (body.id == FOURCC_PATH) {
                uint32_t size = 0, align = 0;
                igi_res::ChunkGetInfo(body, &size, &align);
                MemberRecord m;
                m.name = std::move(name);
                m.info = std::move(info);
                m.type = RES_PATH;
                m.dataOffset = igi_res::ChunkDataOffset(bodyOffset, body, formOffset, form);
                m.size = size;
                if (m.dataOffset == kNoChunk) return fail("PATH chunk has no data");
                if (!emit(m)) return false;
            } else {
                return fail("chunk after NAME is not BODY/CSTR/PATH/IRES");
            }

            advanceFrom = bodyOffset;
            advanceHeader = body;
        } else if (ch.id != FOURCC_ILFF || ch.form != FOURCC_DBUG) {
            return fail("unexpected chunk");
        }

        c = igi_res::ChunkNextOffset(advanceFrom, advanceHeader);
    }
    return true;
}

// Resource_Add — src/res/resource.c:281-317, HOOK(0x4B5F80).
// Dispatches on the container: an ILFF/IRES form, a "RESOURCE" pack, or (in
// the engine) a plain file, which has no members.
bool WalkArchiveMembers(const ResFileSource& src, const std::string& path,
                        const std::function<bool(const MemberRecord&)>& emit,
                        std::string& error) {
    // The first 0x20 bytes cover both pack headers: the 20-byte ILFF form
    // header and the RESOURCE magic + member count (resource.c:287, :293).
    std::vector<uint8_t> head(0x20);
    if (src.Size() < head.size() || !src.ReadAt(0, head.data(), head.size())) {
        error = "File too small for an archive header: " + path;
        Logger::Get().Log(LogLevel::ERR, "[RES] " + error);
        return false;
    }

    if (igi_res::IsIlffIres(head.data(), head.size())) {
        ChunkHeader root;
        std::memcpy(&root, head.data(), 20);
        return WalkFormMembers(src, 0, root, emit, path, error);
    }

    // "RESOURCE" pack: count at +8, member records from +0x20, each record a
    // 12-byte header + name, body at align32(12 + namelen), next record at
    // align32(body + size) (resource.c:293-313).
    if (!igi_res::IsResourcePack(head.data(), head.size())) {
        error = "Not an ILFF/IRES archive or a RESOURCE pack: " + path;
        Logger::Get().Log(LogLevel::ERR, "[RES] " + error);
        return false;
    }
    const uint32_t count = ReadU32LE(head.data() + 8);
    size_t p = 0x20;
    for (uint32_t i = 0; i < count; i++) {
        std::vector<uint8_t> hdr(12);
        if (!src.ReadAt(p, hdr.data(), hdr.size())) {
            error = "Truncated RESOURCE pack entry header: " + path;
            Logger::Get().Log(LogLevel::ERR, "[RES] " + error);
            return false;
        }
        const int32_t memberSize = static_cast<int32_t>(ReadU32LE(hdr.data() + 4));
        const int32_t nameLen = static_cast<int32_t>(ReadU32LE(hdr.data() + 8));
        if (nameLen < 0 || memberSize < 0) {
            error = "Malformed RESOURCE pack entry: " + path;
            Logger::Get().Log(LogLevel::ERR, "[RES] " + error);
            return false;
        }
        MemberRecord m;
        m.type = RES_MEMBER;
        m.name = ReadChunkString(src, p + 12, static_cast<uint32_t>(nameLen));
        m.size = static_cast<uint64_t>(memberSize);
        m.dataOffset = p + ((static_cast<size_t>(nameLen) + 0x2B) & ~static_cast<size_t>(0x1F));
        if (m.dataOffset > src.Size() || m.size > src.Size() - m.dataOffset) {
            error = "RESOURCE pack member lies outside the file: " + path;
            Logger::Get().Log(LogLevel::ERR, "[RES] " + error);
            return false;
        }
        if (!emit(m)) return false;
        p = static_cast<size_t>(m.dataOffset) +
            ((static_cast<size_t>(memberSize) + 0x1F) & ~static_cast<size_t>(0x1F));
    }
    return true;
}

}  // namespace

std::string RES_NormalizeResourceName(const std::string& name) {
    return igi_res::NormalizeResourceName(name);
}

bool RES_FindIndexEntry(const std::unordered_map<std::string, ResEntryInfo>& index,
                        const std::string& name,
                        ResEntryInfo& out) {
    const auto found = index.find(igi_res::NormalizeResourceName(name));
    if (found == index.end()) return false;
    out = found->second;
    return true;
}

// Stream the archive chunk-by-chunk, invoking onEntry for each resource WITHOUT
// holding the whole file (or all entries) in memory at once. The .res archives are
// large (200+ MB); allocating that much contiguously fails in the 32-bit process once
// the address space is fragmented (e.g. after switching levels in-session), which is
// what silently dropped cross-level textures. Reading per-chunk keeps the peak
// allocation to a single resource (a few MB). Returns true if the header was valid.
bool RES_ForEachEntry(const std::string& filepath,
                      const std::function<void(const std::string&, const uint8_t*, size_t)>& onEntry,
                      std::string& error) {
    ResFileSource src;
    if (!src.Open(filepath, error)) {
        Logger::Get().Log(LogLevel::ERR, "[RES] " + error);
        return false;
    }
    if (src.Size() < 20) {
        error = "File too small for ILFF header: " + filepath;
        Logger::Get().Log(LogLevel::ERR, "[RES] " + error);
        return false;
    }
    if (src.Size() > 1024ull * 1024 * 1024) {
        error = "File is unreasonably large or size read failed (" +
                std::to_string(src.Size()) + " bytes): " + filepath;
        Logger::Get().Log(LogLevel::ERR, "[RES] " + error);
        return false;
    }

    std::vector<uint8_t> chunkBuf;  // reused scratch for the current member only
    const uint8_t empty = 0;
    return WalkArchiveMembers(src, filepath,
        [&](const MemberRecord& m) -> bool {
            const uint8_t* data = &empty;
            if (m.dataOffset != kNoChunk && m.size > 0) {
                try {
                    chunkBuf.resize(static_cast<size_t>(m.size));
                } catch (const std::bad_alloc&) {
                    // The engine's Mem_Alloc raises Error_Show and halts
                    // (src/core/mem.c:90); the editor reports the failure.
                    error = "Memory allocation failed for chunk '" + m.name + "' of size " +
                            std::to_string(m.size);
                    Logger::Get().Log(LogLevel::ERR, "[RES] " + error);
                    return false;
                }
                if (!src.ReadAt(m.dataOffset, chunkBuf.data(), static_cast<size_t>(m.size))) {
                    error = "Short read for chunk '" + m.name + "' in " + filepath;
                    Logger::Get().Log(LogLevel::ERR, "[RES] " + error);
                    return false;
                }
                data = chunkBuf.data();
            }
            onEntry(m.name, data, static_cast<size_t>(m.size));
            return true;
        },
        error);
}

RESFile RES_Parse(const std::string& filepath) {
    RESFile result;
    result.valid = RES_ForEachEntry(filepath,
        [&](const std::string& name, const uint8_t* data, size_t size) {
            RESEntry entry;
            entry.name = name;
            try {
                entry.data.assign(data, data + size);
            } catch (const std::bad_alloc&) {
                Logger::Get().Log(LogLevel::ERR, "[RES] Memory allocation failed while buffering chunk '" + name + "' of size " + std::to_string(size));
                return;
            }
            result.entries.push_back(std::move(entry));
        },
        result.error);

    if (result.valid) {
        Logger::Get().Log(LogLevel::INFO, "[RES] Parsed " + filepath + " | Entries: " +
            std::to_string(result.entries.size()));
    }
    return result;
}

std::vector<uint8_t> RES_Extract(const std::string& filepath, const std::string& resourceName) {
    RESFile res = RES_Parse(filepath);
    if (!res.valid) {
        return {};
    }

    // Resource_IsLoaded compares the upper-cased, '/'-separated name
    // (src/res/resource.c:124-147); so does this lookup.
    const std::string wanted = RES_NormalizeResourceName(resourceName);
    for (const auto& entry : res.entries) {
        if (RES_NormalizeResourceName(entry.name) == wanted) {
            return entry.data;
        }
    }

    Logger::Get().Log(LogLevel::WARNING, "[RES] Resource not found: " + resourceName + " in " + filepath);
    return {};
}

// ---------------------------------------------------------------------------
// RES_BuildIndex — read only chunk headers + NAME chunks, build name→offset map
// ---------------------------------------------------------------------------
bool RES_BuildIndex(const std::string& filepath,
                    std::unordered_map<std::string, ResEntryInfo>& out_index,
                    std::string& error) {
    ResFileSource src;
    if (!src.Open(filepath, error)) return false;
    if (src.Size() < 20) { error = "File too small: " + filepath; return false; }

    return WalkArchiveMembers(src, filepath,
        [&](const MemberRecord& m) -> bool {
            // Key = the engine's resource name (Resource_Create 0x4B59F0,
            // src/res/resource.c:65-104). No basename flattening: the engine
            // never compares anything but the full name, and the same name can
            // exist in several archives — the archive is chosen by the caller.
            ResEntryInfo info;
            info.data_offset = static_cast<size_t>(m.dataOffset == kNoChunk ? 0 : m.dataOffset);
            info.data_size = static_cast<size_t>(m.size);
            out_index[igi_res::NormalizeResourceName(m.name)] = info;
            return true;
        },
        error);
}

// ---------------------------------------------------------------------------
// RES_ReadEntry — seek to a known offset, read exactly info.data_size bytes
// ---------------------------------------------------------------------------
namespace {
// IGI_PROFILE-only: count of archive-entry reads (see res_writer.h).  Gated on
// the same flag the rest of the instrumentation uses, so an ordinary run is
// unchanged: no counter, no atomic, no branch taken.
bool ResReadCountingEnabled() {
    static const bool enabled = std::getenv("IGI_PROFILE") != nullptr;
    return enabled;
}
std::atomic<uint64_t> g_res_entry_reads{0};
}  // namespace

uint64_t RES_EntryReadCount() {
    return g_res_entry_reads.load(std::memory_order_relaxed);
}

std::vector<uint8_t> RES_ReadEntry(const std::string& filepath, const ResEntryInfo& info) {
    std::vector<uint8_t> buf;
    if (info.data_size == 0) return buf;
    if (ResReadCountingEnabled()) {
        g_res_entry_reads.fetch_add(1, std::memory_order_relaxed);
    }
    // `filepath` is the archive path the caller indexed, which the level and res
    // code builds with the engine's "\\" separators (ResFileSource::Open above
    // converts for the same reason).  On POSIX a backslash is an ordinary
    // filename character, so an unconverted path opens nothing and this read
    // returned empty for every model and texture member — the viewport loaded
    // terrain and no objects.  Identity on Windows.
    std::ifstream file(Utils::ToNativePath(filepath), std::ios::binary);
    if (!file.is_open()) return buf;
    file.seekg(static_cast<std::streamoff>(info.data_offset), std::ios::beg);
    buf.resize(info.data_size);
    file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(info.data_size));
    if (static_cast<size_t>(file.gcount()) != info.data_size) buf.clear();
    return buf;
}
