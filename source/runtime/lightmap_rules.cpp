// lightmap_rules.cpp - the retail object-lightmap rules of issue #72.
//
// Every branch below carries the IGI-RE file:line it mirrors. Where this port
// deviates (a guard the engine does not need because it is the engine, or a
// bound on a loop the engine leaves unbounded) the comment says so and names the
// deviation. See source/runtime/lightmap_rules.h for the full citation block.

#include "runtime/lightmap_rules.h"

#include <cstdio>
#include <cstring>

namespace igi {
namespace lightmap_rules {
namespace {

// The engine's fixed buffers: `char gLightmapDir[260]` (lightmap.c:369) and
// `char gLightmapResPath[256]` (lightmap.c:374). The `.olm` path is a
// `char path[0x200]` stack buffer (lightmapbuild.c:50).
constexpr std::size_t kPathBufferSize = 0x200;

// crt_strstr (lightmap.c:14, 0x4A5BB0) is a plain substring search; the engine
// uses it in both halves of the MakeName test (lightmap.c:188, :247).
bool ContainsSubstring(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// The engine's `%05d` is a MINIMUM width. The index is always non-negative here
// (it is a ref index), so the formatted field is 5 digits plus anything beyond.
bool IsAllDigits(const std::string& s) {
    if (s.empty()) return false;
    for (const char ch : s) {
        if (ch < '0' || ch > '9') return false;
    }
    return true;
}

// Little-endian readers, because `Lightmaps` is read straight out of the file the
// resource loader returned (litmaptypes.h:36-48 has no padding or endianness
// marker of its own; the shipping files are little-endian, as the exe is).
std::uint32_t ReadU32LE(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

std::int32_t ReadI32LE(const std::uint8_t* p) {
    return static_cast<std::int32_t>(ReadU32LE(p));
}

float ReadF32LE(const std::uint8_t* p) {
    const std::uint32_t bits = ReadU32LE(p);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

std::int16_t ReadI16LE(const std::uint8_t* p) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0]) |
                                     (static_cast<std::uint16_t>(p[1]) << 8));
}

// The engine's MakeName loop is `for (n = 0;; n++)` with no bound
// (lightmap.c:236). A port must not spin forever when every index is taken, so
// the search stops here and reports failure. DEVIATION (port guard): the engine
// has no such bound; retail data needs at most 121 names (corpus).
constexpr int kMaxObjectNameSearch = 1000000;

}  // namespace

// ---------------------------------------------------------------------------
// Rule 1 - the resource names (lightmap.c:89-101)
// ---------------------------------------------------------------------------

const char* LightmapDir() {
    // lightmap.c:91: crt_sprintf(gLightmapDir, "MISSION:%s", "lightmaps").
    return "MISSION:lightmaps";
}

const char* LightmapResPath() {
    // lightmap.c:98: crt_sprintf(gLightmapResPath, "MISSION:%s/%s", "lightmaps",
    // "lightmaps.res").
    return "MISSION:lightmaps/lightmaps.res";
}

// ---------------------------------------------------------------------------
// Rule 2 - the `.olm` name (lightmapbuild.c:59, lightmap.c:240/:255)
// ---------------------------------------------------------------------------

std::string OlmPath(const std::string& dir, const std::string& name, int index) {
    // lightmapbuild.c:59: crt_sprintf(path, "%s/%s_%05d.olm", Lightmap_GetDir(),
    // name, index). Same format string, same argument order; `%05d` keeps its
    // minimum-width semantics, so an index of 100000 widens the field.
    char buffer[kPathBufferSize];
    std::snprintf(buffer, sizeof(buffer), "%s/%s_%05d.olm", dir.c_str(), name.c_str(), index);
    return std::string(buffer);
}

std::string OlmFileName(const std::string& name, int index) {
    // The same name with the directory dropped -- which is the form the editor's
    // loader keys by after stripping any prefix
    // (source/renderer/object_lightmap.cpp:111-114).
    char buffer[kPathBufferSize];
    std::snprintf(buffer, sizeof(buffer), "%s_%05d.olm", name.c_str(), index);
    return std::string(buffer);
}

std::string ObjectLightmapName(int n) {
    // lightmap.c:240 and :255: crt_sprintf(name, "%s%05d", "obj", n).
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%s%05d", "obj", n);
    return std::string(buffer);
}

OlmName ParseOlmFileName(const std::string& file_name) {
    // The inverse of OlmFileName. A `%05d` field is a minimum width, so accept
    // five or more digits; anything else is not a name this rule can produce.
    OlmName out;
    constexpr const char* kSuffix = ".olm";
    constexpr std::size_t kSuffixLen = 4;
    if (file_name.size() <= kSuffixLen) return out;
    if (file_name.compare(file_name.size() - kSuffixLen, kSuffixLen, kSuffix) != 0) return out;
    const std::size_t digit_end = file_name.size() - kSuffixLen;
    const std::size_t underscore = file_name.rfind('_', digit_end);
    if (underscore == std::string::npos || underscore == 0) return out;
    const std::string digits = file_name.substr(underscore + 1, digit_end - underscore - 1);
    if (digits.size() < 5 || !IsAllDigits(digits)) return out;
    long value = 0;
    for (const char ch : digits) {
        value = value * 10 + (ch - '0');
        if (value > 0x7FFFFFFF) return out;  // the field is an int (lightmapbuild.c:44)
    }
    out.object_name = file_name.substr(0, underscore);
    out.index = static_cast<int>(value);
    out.ok = true;
    return out;
}

bool ScanNameMatches(const ScanEntry& entry, const std::string& scan_name) {
    // lightmap.c:188: `if (!e->isDir && crt_strstr(e->name, gLightmapScanName))`
    // -> a directory never matches, and the test is a substring test.
    if (entry.is_dir) return false;
    return ContainsSubstring(entry.name, scan_name);
}

int FindFreeObjectLightmapIndex(const std::vector<std::string>& existing_info_names,
                                const std::vector<ScanEntry>& dir_entries) {
    // lightmap.c:236-254:
    //
    //     for (n = 0;; n++) {
    //         used = 0;
    //         crt_sprintf(name, "%s%05d", "obj", n);
    //         gLightmapFileFound = 0; gLightmapScanName = name;
    //         File_ScanDir(Lightmap_GetDir(), LightmapInfo_ScanFile, NULL);
    //         for (i = 0; i < p->used; i++) { ... if (crt_strstr(o->name, name)) { used = 1; break; } }
    //         if (!gLightmapFileFound && !used) break;
    //     }
    //
    // DEVIATION (port shape, not result): the engine re-lists the directory on
    // every iteration (lightmap.c:243); this port lists it once, because the
    // listing cannot change inside the call. The matching predicate is identical.
    //
    // NOTE: at the point the engine calls this, the new LightmapInfo's own name is
    // the empty string (lightmap.c:210), so its own pool slot never matches.
    for (int n = 0; n < kMaxObjectNameSearch; ++n) {
        const std::string scan_name = ObjectLightmapName(n);
        bool file_found = false;
        for (const ScanEntry& entry : dir_entries) {
            if (ScanNameMatches(entry, scan_name)) {
                file_found = true;
                break;
            }
        }
        if (file_found) continue;
        bool used = false;
        for (const std::string& existing : existing_info_names) {
            // lightmap.c:247: crt_strstr(o->name, name) -- substring, not equality.
            if (ContainsSubstring(existing, scan_name)) {
                used = true;
                break;
            }
        }
        if (!used) return n;  // lightmap.c:252-253
    }
    return -1;  // DEVIATION (port guard): the engine would keep looping.
}

// ---------------------------------------------------------------------------
// The timestamp (terredit/datetime.c:50-104)
// ---------------------------------------------------------------------------

std::string FormatDateTime(const DateTime& dt) {
    // DateTime_Format(dt, buf, size, date=1, time=1, ms=1) (datetime.c:50-73)
    // composed of DateTime_FormatDate ":78" %04d-%02d-%02d,
    // DateTime_FormatTime ":84" %02d-%02d-%02d and DateTime_FormatMs ":90" %04d,
    // joined by the two "-" separators (:59, :65). One snprintf produces the same
    // text as the three-step concatenation.
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d-%02d-%02d-%02d-%04d", dt.year, dt.month,
                  dt.day, dt.hour, dt.minute, dt.second, dt.ms);
    return std::string(buffer);
}

int DateTimeCompare(const DateTime& a, const DateTime& b) {
    // datetime.c:94-104: format both, then
    //     for (i = 0; sa[i] == sb[i]; i++) if (!sa[i]) return 0;
    //     return sa[i] < sb[i] ? -1 : 1;
    // The loop is reproduced over the formatted text rather than replaced by a
    // std::string compare, so the "equal up to the terminator" case is explicit.
    const std::string sa = FormatDateTime(a);
    const std::string sb = FormatDateTime(b);
    std::size_t i = 0;
    while (i < sa.size() && i < sb.size() && sa[i] == sb[i]) ++i;
    if (i == sa.size() && i == sb.size()) return 0;
    const unsigned char ca = i < sa.size() ? static_cast<unsigned char>(sa[i]) : 0;
    const unsigned char cb = i < sb.size() ? static_cast<unsigned char>(sb[i]) : 0;
    if (ca == cb) return 0;
    return ca < cb ? -1 : 1;
}

// ---------------------------------------------------------------------------
// Rule 3 - the accept/reject test (lightmapbuild.c:53-88)
// ---------------------------------------------------------------------------

bool Feq(float a, float b) {
    // dirlight.h:20-23: `return !__builtin_islessgreater(a, b);` -- an unordered
    // compare (either side NaN) counts as EQUAL, exactly like fcomp + test ah,0x40.
    return !(a < b || a > b);
}

bool OlmHeaderAccepted(const OlmFileHeader& file, const OlmAcceptInput& mesh) {
    // lightmapbuild.c:63-64, the four conjuncts in the engine's order:
    //   file->magic == LIGHTMAP_MAGIC                  (:63, magic from :26)
    //   && feq(mh->version, file->version)             (:63)
    //   && DateTime_Compare(mh->time, file->time) < 0  (:64)
    //   && mesh->texCount == file->count               (:64)
    if (file.magic != kOlmMagic) return false;
    if (!Feq(mesh.mesh_version, file.version)) return false;
    if (!(DateTimeCompare(mesh.mesh_time, file.time) < 0)) return false;
    if (mesh.mesh_tex_count != file.count) return false;
    return true;
}

OlmDecision DecideOlmFile(const OlmFileHeader& file, const OlmAcceptInput& mesh) {
    // lightmapbuild.c:60-88. There is no third outcome: a file either passes the
    // four conjuncts and is used (only when `full == 0`), or it is flushed
    // (`Resource_Flush(file)`, :86) and a new Lightmaps is built.
    if (!OlmHeaderAccepted(file, mesh)) return OlmDecision::kRebuild;
    // lightmapbuild.c:65 `if (!full) { ... return file; }` -- and :83 `return file;`
    // is INSIDE that block, so `full != 0` falls through to the builder even though
    // the file was accepted.
    return mesh.full ? OlmDecision::kRebuild : OlmDecision::kLoadPage0Only;
}

// ---------------------------------------------------------------------------
// Reading the bytes (litmaptypes.h:11-48, lightmapbuild.c:72-78)
// ---------------------------------------------------------------------------

std::size_t OlmPixelOffset(int count) {
    // lightmapbuild.c:72: `px = (char *)&file->tex[file->count];` -- the pixel
    // block starts right after the `count` LightmapTex records, i.e. at
    // 0x3C + count * 0x2C. A negative count is not a legal file; the engine never
    // produces one (`lm->count = mesh->texCount`, :91).
    if (count <= 0) return kOlmHeaderSize;
    return kOlmHeaderSize + static_cast<std::size_t>(count) * kOlmTexRecordSize;
}

bool ParseOlmHeader(const std::uint8_t* data, std::size_t size, OlmFileHeader* out) {
    if (data == nullptr || out == nullptr) return false;
    // The header is the `Lightmaps` prefix up to and including `info`
    // (litmaptypes.h:36-48): magic 0x00, version 0x04, time[7] 0x08, fromResource
    // 0x24, built 0x28, count 0x2C, texels 0x30, texels2 0x34.
    if (size < kOlmHeaderSize) return false;
    OlmFileHeader header;
    header.magic = ReadU32LE(data + kOlmMagicOffset);
    header.version = ReadF32LE(data + kOlmVersionOffset);
    header.time.year = ReadI32LE(data + kOlmTimeOffset + 0);
    header.time.month = ReadI32LE(data + kOlmTimeOffset + 4);
    header.time.day = ReadI32LE(data + kOlmTimeOffset + 8);
    header.time.hour = ReadI32LE(data + kOlmTimeOffset + 12);
    header.time.minute = ReadI32LE(data + kOlmTimeOffset + 16);
    header.time.second = ReadI32LE(data + kOlmTimeOffset + 20);
    header.time.ms = ReadI32LE(data + kOlmTimeOffset + 24);
    header.from_resource = ReadI32LE(data + kOlmFromResourceOffset);
    header.built = ReadI32LE(data + kOlmBuiltOffset);
    header.count = ReadI32LE(data + kOlmCountOffset);
    header.texels = ReadI32LE(data + kOlmTexelsOffset);
    header.texels2 = ReadI32LE(data + kOlmTexels2Offset);
    // The engine never validates this: it trusts the resource loader
    // (`Resource_Load(path, 0)`, lightmapbuild.c:61) to hand back a whole file.
    // A port that reads bytes off disk must, or a corrupt `count` walks off the
    // buffer. DEVIATION (port guard): reject a count whose page records do not fit.
    if (header.count < 0) return false;
    if (OlmPixelOffset(header.count) > size) return false;
    *out = header;
    return true;
}

std::size_t OlmExpectedSize(const std::vector<OlmPage>& pages) {
    // The same arithmetic the builder uses to walk the pixel block
    // (lightmapbuild.c:72-78): the header, then one 0x2C record per page, then
    // `t->width * t->height * 4` bytes per page -- note `n = t->width * t->height`
    // and `px += n * 4` at :76-78, and the allocation `t->height * t->width * 4`
    // at :109.
    std::size_t total = OlmPixelOffset(static_cast<int>(pages.size()));
    for (const OlmPage& page : pages) {
        if (page.width <= 0 || page.height <= 0) continue;  // no bytes to add
        total += static_cast<std::size_t>(page.width) * static_cast<std::size_t>(page.height) *
                 kOlmTexBytesPerTexel;
    }
    return total;
}

bool ReadOlmPages(const std::uint8_t* data, std::size_t size, OlmFileHeader* header,
                  std::vector<OlmPage>* pages) {
    if (pages == nullptr) return false;
    OlmFileHeader parsed;
    if (!ParseOlmHeader(data, size, &parsed)) return false;
    std::vector<OlmPage> read;
    read.reserve(static_cast<std::size_t>(parsed.count));
    for (int i = 0; i < parsed.count; ++i) {
        // litmaptypes.h:14-15: `short width` at +0x28 and `short height` at +0x2A
        // of the 0x2C-byte LightmapTex record.
        const std::size_t record = kOlmHeaderSize + static_cast<std::size_t>(i) * kOlmTexRecordSize;
        OlmPage page;
        page.width = ReadI16LE(data + record + kOlmTexSizeOffset);
        page.height = ReadI16LE(data + record + kOlmTexSizeOffset + 2);
        read.push_back(page);
    }
    // DEVIATION (port guard): a non-positive page dimension is not rejected by the
    // engine -- `t->width = size[0]` (lightmapbuild.c:107) is taken as given and a
    // zero-size page simply allocates nothing (:109). A reader that multiplies must
    // not accept it. The exact-size check below is the real content of this
    // function: the shipping corpus satisfies it for all 25,337 files.
    for (const OlmPage& page : read) {
        if (page.width <= 0 || page.height <= 0) return false;
    }
    if (OlmExpectedSize(read) != size) return false;
    if (header != nullptr) *header = parsed;
    *pages = std::move(read);
    return true;
}

// ---------------------------------------------------------------------------
// Rule 4 - the bake parameters (lightmapinfo.c:97-200)
// ---------------------------------------------------------------------------

float BakeScale(int lod_index, float info_scale) {
    // lightmapinfo.c:150: `(1 << r->lodIndex) * info->scale * kTexelScale`, with
    // kTexelScale = 4096.0f (:75). The int factor converts to float, so the whole
    // product is float arithmetic. `info->scale` is 1.0f by the time this runs
    // (:105) -- the authored Scale parameter never reaches here.
    //
    // DEVIATION (port guard): `1 << lod_index` is undefined for lod_index outside
    // 0..30. The engine only ever passes a ref's own lodIndex, which
    // SectorObject_LoadLightmaps fills from 0..lod_count-1 (sector.c:557,
    // VirModel_InitLods caps at VIRMODEL_LODS = 5). Out-of-range returns 0.0f.
    if (lod_index < 0 || lod_index > 30) return 0.0f;
    const float lod_factor = static_cast<float>(1u << lod_index);
    return lod_factor * info_scale * kTexelScale;
}

bool BakeFullRebuild(bool stale, int unk_dc, bool edited) {
    // lightmapinfo.c:149: `int full = stale || info->unkDC || edited;`
    return stale || unk_dc != 0 || edited;
}

int BakeFlags(bool edited) {
    // lightmapinfo.c:151, the `flags` argument: `edited` itself.
    return edited ? 1 : 0;
}

int BakeUnkFromFloatBits(float unk94) {
    // lightmapinfo.c:151: `(int)float_bits(&info->unk94)` -- the FLOAT's bit
    // pattern reinterpreted as an int (float_bits at :80-86 memcpys 4 bytes into a
    // uint32_t). The default 100.0f is 0x42C80000 = 1120403456.
    std::uint32_t bits = 0;
    std::memcpy(&bits, &unk94, sizeof(bits));
    return static_cast<int>(bits);
}

bool BakeEdited(bool button_6f, bool button_70, bool button_71) {
    // lightmapinfo.c:108: `if (info->unk70 || info->unk6F || info->unk71) { ...
    // edited = 1; ... }`. The engine's order is unk70 first; the disjunction is
    // order-independent.
    return button_70 || button_6f || button_71;
}

bool BakeRefsMissing(const std::vector<bool>& ref_has_lightmaps,
                     const std::vector<bool>& ref_has_info) {
    // lightmapinfo.c:111-114:
    //     for (i = 0; i < set->count; i++)
    //         if (set->refs[i].lightmaps && !set->refs[i].lightmaps->info)
    //             missing = 1;
    // `lightmaps->info == NULL` is what Lightmap_BuildLod's load path sets
    // (lightmapbuild.c:69 `file->info = NULL;`), so a ref satisfied from an `.olm`
    // on disk is exactly what marks the set stale.
    const std::size_t count = ref_has_lightmaps.size() < ref_has_info.size()
                                  ? ref_has_lightmaps.size()
                                  : ref_has_info.size();
    for (std::size_t i = 0; i < count; ++i) {
        if (ref_has_lightmaps[i] && !ref_has_info[i]) return true;
    }
    return false;
}

bool BakeStaleFromScale(int unk_cc, float last_scale) {
    // lightmapinfo.c:106-107:
    //     if (!info->unkCC && float_bits(&info->lastScale) != 0x3F800000)
    //         stale = 1;
    // The comparison is on the BIT PATTERN of 1.0f, so -0.0f and NaN are "not 1.0"
    // while a NaN lastScale is not equal to 0x3F800000 either.
    std::uint32_t bits = 0;
    std::memcpy(&bits, &last_scale, sizeof(bits));
    return unk_cc == 0 && bits != 0x3F800000u;
}

bool BakeEarlyOut(bool model_matched, float last_scale, float scale, bool stale, int unk_dc) {
    // lightmapinfo.c:120:
    //     if (match[1] && feq(info->lastScale, info->scale) && !stale && !info->unkDC)
    //         goto done;
    // `match[1]` is written by the Mesh3D_GetEvent3 handler (:119); feq is
    // dirlight.h:20-23.
    return model_matched && Feq(last_scale, scale) && !stale && unk_dc == 0;
}

bool BakeReloadsRefSet(bool lightmaps_used) {
    // lightmapinfo.c:137-138: `if (AppContext_GetLightmapsUsed())
    // TASK_HANDLER(Mesh3D_GetEvent1(), parent->type)(parent, &loaded);` -- this
    // gates the ref-set reload ONLY. The BuildLod loop at :147-162 runs either way,
    // over whatever `loaded` holds.
    return lightmaps_used;
}

int HemicubeSize(int unk_a4) {
    // lightmapinfo.c:180: `info->unkBC = Hemicube_New(info->unkA4);` with the
    // default 150 from lightmap.c:212.
    return unk_a4;
}

std::size_t HemicubeTopTexels(int size) {
    // lightmap.c:110-115: `bytes = size * size * 4;` for the top, top2 and
    // topFactor buffers. Returned in TEXELS; the allocation is four times this.
    if (size <= 0) return 0;
    return static_cast<std::size_t>(size) * static_cast<std::size_t>(size);
}

std::size_t HemicubeSideTexels(int size) {
    // lightmap.c:116-121: `bytes = (size >> 1) * size * 4;` for sideFactor and for
    // each of the four sides (and their four `side2` copies).
    if (size <= 0) return 0;
    return static_cast<std::size_t>(size >> 1) * static_cast<std::size_t>(size);
}

bool BakeComputesVertexLights(bool button_6f, bool button_70) {
    // lightmapinfo.c:177: `if (info->unk6F || info->unk70) { ... Hemicube_New ...
    // Qvm_GetArgLine ... }`.
    return button_6f || button_70;
}

bool BakeCallsTaskNothing2(bool button_71) {
    // lightmapinfo.c:197-198: `if (info->unk71) Task_Nothing2(info, info->unkB4);`
    // with the default unkB4 = 1 (lightmap.c:216).
    return button_71;
}

// ---------------------------------------------------------------------------
// The `.res` container rules (lightmap.c:195-201, :277-317)
// ---------------------------------------------------------------------------

bool ShouldLoadLightmapResource(bool pool_empty, bool resource_exists) {
    // lightmap.c:200-201: `if (!p->used && Resource_Exists(Lightmap_GetResPath()))
    // Resource_Load(Lightmap_GetResPath(), NULL);` -- `!p->used` is "no live
    // LightmapInfo", so the archive is loaded on the first one only.
    return pool_empty && resource_exists;
}

bool ShouldUnloadLightmapResource(int pool_used, bool resource_found) {
    // lightmap.c:315-316: `if (!p->used && Resource_Find(Lightmap_GetResPath()))
    // Resource_Unload(Lightmap_GetResPath());` -- and only after the last
    // LightmapInfo is deleted (:304-306 decrements `p->used` first).
    return pool_used == 0 && resource_found;
}

}  // namespace lightmap_rules
}  // namespace igi
