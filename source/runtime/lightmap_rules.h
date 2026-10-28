// lightmap_rules.h - the retail object-lightmap rules the editor's bake button
// needs: the resource name, the `.olm` file name, the accept/reject test for an
// `.olm` already on disk, and the parameters that decide a bake.
//
// Issue #72 "Lightmap bake button". Scope is deliberately narrow: this is NOT the
// game's runtime lighting. It is the three things an editor cannot get wrong
// without writing the wrong file name or binding a stale bake:
//
//   1. the lightmap resource name          Lightmap_GetDir      0x5210C0
//                                          Lightmap_GetResPath  0x5210E0
//   2. the `.olm` file name                Lightmap_BuildLod    0x523D40
//                                          LightmapInfo_MakeName 0x5235A0
//   3. the accept/reject test for an `.olm` already on disk, and the parameters
//      that decide a bake                  Lightmap_BuildLod + LightmapInfo_Changed 0x523930
//
// Everything here is cited to IGI-RE (`/Users/haseeb-mir/Documents/Code/IGI-RE`),
// the verified C decompilation of retail `IGI.exe`. No web source and no other
// port is used as authority. Files used, all under IGI-RE/src:
//
//   litmap/lightmap.c:66-101     Lightmap_FreeBuffers 0x521040, Lightmap_GetDir
//                                0x5210C0, Lightmap_GetResPath 0x5210E0
//   litmap/lightmap.c:186-257    LightmapInfo_ScanFile 0x523420, LightmapInfo_Create
//                                0x523450, LightmapInfo_MakeName 0x5235A0
//   litmap/lightmap.c:277-318    LightmapInfo_Delete 0x5236C0 (the resource unload)
//   litmap/lightmap.c:362-378    the DATA/BSS block: the literal format strings
//                                and the global paths, at their IGI.exe addresses
//   litmap/litmaptypes.h:11-48   LightmapTex / Lightmaps: the exact on-disk layout
//   dirlight/lightmapbuild.c:26-100   LIGHTMAP_MAGIC, the name format, the accept
//                                test, and the builder's own header initialisation
//   dirlight/lightmapinfo.c:75-200    the bake parameters and the rebuild decision
//   dirlight/lightmapinfo.c:433  the DATA block holding kScaleRange {0.5f, 10.0f}
//   dirlight/dirlight.h:20-23    feq(): the x87 "unordered counts as equal" compare
//   terredit/datetime.c:50-104   DateTime_Format / DateTime_Compare: the timestamp
//                                is compared as the TEXT "YYYY-MM-DD-hh-mm-ss-mmmm"
//   model/modeltypes.h:9-27      MeshHeader: version at 0x00, time[7] at 0x04,
//                                format at 0x20 (0 rigid, 1 bone, 3 lightmapped)
//
// ---------------------------------------------------------------------------
// Rule 1 - the resource names are LITERALS, not the `.olm` name
// ---------------------------------------------------------------------------
//
//   Lightmap_GetDir      lightmap.c:89-93   crt_sprintf(gLightmapDir, "MISSION:%s", "lightmaps")
//   Lightmap_GetResPath  lightmap.c:96-101  crt_sprintf(gLightmapResPath, "MISSION:%s/%s",
//                                                       "lightmaps", "lightmaps.res")
//
// so the directory is the literal `MISSION:lightmaps` and the archive is the
// literal `MISSION:lightmaps/lightmaps.res`. Both format strings AND both
// arguments are present as bytes in the exe: lightmap.c:433 stores
// "MISSION:%s", "lightmaps", "MISSION:%s/%s", "lightmaps.res" at 0x54F050+8..+0x3D.
//
// `Lightmap_GetResPath` is the ARCHIVE path. It is not the per-object name, and
// it carries no index and no extension derived from the object. The editor's
// `lightmaps.res` lookup (source/renderer/object_lightmap.cpp:67) matches it.
//
// ---------------------------------------------------------------------------
// Rule 2 - the `.olm` name is "%s/%s_%05d.olm"
// ---------------------------------------------------------------------------
//
//   lightmapbuild.c:59  crt_sprintf(path, "%s/%s_%05d.olm", Lightmap_GetDir(), name, index)
//
// with `Lightmap_GetDir()` == "MISSION:lightmaps" from rule 1, so the full string
// is `MISSION:lightmaps/<name>_<index>.olm`:
//
//   * separator between name and index: `_` (one underscore, literal)
//   * the index field: `%05d` -- width 5, zero padded, and it is a MINIMUM width,
//     so an index of 100000 or more prints six digits (never seen in retail; the
//     largest ref index in the shipping corpus is 542)
//   * extension: `.olm` (lower case, four characters, no compression suffix)
//
// `name` is NOT the model name. It is the LightmapInfo task's `name` field, which
// LightmapInfo_MakeName allocates (lightmap.c:231-257):
//
//   lightmap.c:240  crt_sprintf(name, "%s%05d", "obj", n)
//   lightmap.c:255  crt_sprintf(li->name, "%s%05d", "obj", n)
//
// i.e. the literal `obj` plus the same five-digit zero-padded field. `n` is the
// first index for which NEITHER of these holds:
//
//   lightmap.c:236-243  any non-directory entry of Lightmap_GetDir() whose name
//                       CONTAINS `obj%05d` as a SUBSTRING (strstr, lightmap.c:188)
//   lightmap.c:244-251  any live LightmapInfo whose name contains it as a substring
//
// The retail corpus confirms both halves: all 25,337 `.olm` entries shipped in the
// 14 level bundles of `MISSION:lightmaps/lightmaps.res` are named
// `obj<5 digits>_<5 digits>.olm`, e.g. `obj00006_00052.olm` (level 4).
//
// ---------------------------------------------------------------------------
// Rule 3 - the accept/reject test for an `.olm` already on disk
// ---------------------------------------------------------------------------
//
//   lightmapbuild.c:53-54  if (mh->format != 3) return NULL;      // format 3 only
//   lightmapbuild.c:56-57  if (!mesh->unk8 || !mesh) return NULL;
//   lightmapbuild.c:59-88  the file path, then:
//
//       if (Resource_Exists(path)) {
//           file = Resource_Load(path, 0);
//           if (file->magic == 0x3DF5C28F          /* :26, :63 */
//               && feq(mh->version, file->version) /* :63 */
//               && DateTime_Compare(mh->time, file->time) < 0 /* :64 */
//               && mesh->texCount == file->count)  /* :64 */
//           {
//               if (!full) { ...return file; }     /* :65-84 */
//           } else {
//               Resource_Flush(file);              /* :86 */
//           }
//       }
//
// Four conjuncts, all mandatory:
//
//   magic      == 0x3DF5C28F. LIGHTMAP_MAGIC (lightmapbuild.c:26); a float whose
//                 bit pattern is 0.12f, written to a new bake at :99.
//   version    feq(mesh_header.version, file.version) -- EQUAL, not "at least".
//                 `feq(a,b) = !__builtin_islessgreater(a,b)` (dirlight.h:20-23),
//                 so an unordered (NaN) compare counts as EQUAL. The builder
//                 writes 0.1f (bit pattern 0x3DCCCCCD, lightmapbuild.c:98).
//   time       DateTime_Compare(mesh.time, file.time) < 0 -- the FILE must be
//                 STRICTLY LATER than the mesh. DateTime_Compare (datetime.c:94-104)
//                 renders both as "YYYY-MM-DD-hh-mm-ss-mmmm" and compares the text,
//                 so this is a lexicographic string compare of 7 ints, not a
//                 numeric timestamp compare.
//   count      mesh->texCount == file->count -- the mesh page count must equal the
//                 file page count exactly. `lm->count = mesh->texCount` (:91) is
//                 what a fresh bake writes.
//
// On ANY mismatch the loaded file is flushed (:86) and a new Lightmaps is built:
// there is no partial acceptance, no page-count clamp, and no fallback that keeps
// a mismatched file.
//
// `full` is the fifth input and it is NOT part of the accept test: when the caller
// asks for a full rebuild (`full != 0`) the accept branch does not return, so a
// perfectly valid file is ignored and rebuilt anyway (:65, :83-84). See
// DecideOlmFile().
//
// ---------------------------------------------------------------------------
// Rule 4 - the parameters that decide a bake
// ---------------------------------------------------------------------------
//
// LightmapInfo_Changed (lightmapinfo.c:97-200) is the only caller of
// Lightmap_BuildLod in the decompilation (:150). Per ref `i`:
//
//   scale  = (1 << ref.lodIndex) * info->scale * kTexelScale      /* :150 */
//            kTexelScale = 4096.0f                                /* :75  */
//   full   = stale || info->unkDC || edited                       /* :149 */
//   flags  = edited                                               /* :151 */
//   unk    = (int)float_bits(&info->unk94)                        /* :151 */
//
//   * `info->scale` is FORCED to 1.0f at the top of the function (:105) before the
//     scale is used, so the authored "Scale" parameter (range 0.5 .. 10, see
//     kLightmapScaleRangeMin/Max) never reaches the bake through this path.
//   * `edited` is set when any of the three build push-buttons is pressed
//     (:108-110): `unk70 || unk6F || unk71`. Which authored button label maps to
//     which field is NOT recoverable from the decompilation (the ParamTable
//     registers ten nameless push-buttons, lightmapinfo.c:374-383) - unverified.
//   * `stale` comes from two places: the scale clause (:106-107, only reachable
//     while `unkCC == 0`, i.e. never after the first call, because :173 then sets
//     `lastScale = 1.0f`) and `missing` (:111-114: any ref whose loaded lightmaps
//     have `info == NULL`). The latter is why an `.olm` that was accepted from disk
//     is not reused across a parameter change: Lightmap_BuildLod's load path sets
//     `file->info = NULL` (lightmapbuild.c:69), so the next Changed() marks the set
//     missing and rebuilds in full.
//   * the early out (:120) skips the whole rebuild only when the model matched,
//     the scale is unchanged, nothing is stale and `unkDC == 0`.
//   * the hemicube size (the bake "quality") is `info->unkA4`, default 150
//     (lightmap.c:212), passed to Hemicube_New (lightmapinfo.c:180). Hemicube_New
//     allocates a `size x size` top and four `(size>>1) x size` sides
//     (lightmap.c:110-121).
//   * the vertex-light pass runs only when `unk6F || unk70` (:177) and
//     Task_Nothing2(info, info->unkB4) runs only when `unk71` (:197).
//   * AppContext_GetLightmapsUsed() (:137) gates only the REF-SET RELOAD, not the
//     BuildLod loop: with lightmaps off the previous set is reused and still baked.
//
// What is NOT in the decompilation: the `.olm` WRITER. `Lightmap_BuildLod` only
// builds or loads; `gLightmapBuilt` is set (:95) and never read anywhere in IGI-RE,
// so the serialisation of a new bake to disk is outside the oracle's coverage.
// That is expected: baking is a tool feature, not a retail code path (see
// docs/igi-re-parity/TRIAGE_REMAINING_ISSUES.md row 72).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace igi {
namespace lightmap_rules {

// ---------------------------------------------------------------------------
// Constants. Every one is IGI-RE DATA or a literal from a cited line.
// ---------------------------------------------------------------------------

// lightmapbuild.c:26 `#define LIGHTMAP_MAGIC 0x3DF5C28F` (a float, 0.12f), written
// into a new bake at lightmapbuild.c:99 and required at :63.
constexpr std::uint32_t kOlmMagic = 0x3DF5C28Fu;

// The version a fresh bake carries: `0.1f`, bit pattern 0x3DCCCCCD
// (lightmapbuild.c:98). Required to compare EQUAL, not "at least" (:63).
constexpr float kOlmVersion = 0.1f;
constexpr std::uint32_t kOlmVersionBits = 0x3DCCCCCDu;

// lightmapinfo.c:75 `static const float kTexelScale = 4096.0f;` -- the scale
// multiplier of the bake (:150).
constexpr float kTexelScale = 4096.0f;

// The LightmapInfo "Scale" parameter's registered range, a RangeF {min, max}
// (param/paramtypes.h:18-20) whose two floats are the first 8 bytes of the DATA
// block at 0x54F050: 0x3F000000 = 0.5f and 0x41200000 = 10.0f
// (lightmapinfo.c:433 bytes 0..7, aliased as kScaleRange at :434).
constexpr float kScaleRangeMin = 0.5f;
constexpr float kScaleRangeMax = 10.0f;

// LightmapInfo_Create's defaults (lightmap.c:211-223). `unk94` is the float whose
// BIT PATTERN is passed to the builder as an int (lightmapinfo.c:151).
constexpr float kDefaultScale = 1.0f;        // lightmap.c:211 scale/lastScale/unk90/unk9C
constexpr float kDefaultUnk94 = 100.0f;      // lightmap.c:215, kInfoUnk94 at :64
constexpr int kDefaultHemicubeSize = 150;    // lightmap.c:212 unkA4 -> Hemicube_New (:180)
constexpr int kDefaultUnkA0 = 1000;          // lightmap.c:213 unkA0
constexpr int kDefaultUnkB4 = 1;             // lightmap.c:216 unkB4 -> Task_Nothing2 (:198)
constexpr int kDefaultUnkCC = 1;             // lightmap.c:222 unkCC
constexpr int kDefaultUnkDC = 0;             // lightmap.c:223 unkDC

// The only mesh format a lightmap LOD can have: `mh->format != 3 -> NULL`
// (lightmapbuild.c:53-54; modeltypes.h:12 "0 rigid, 1 bone, 3 lightmapped").
constexpr int kMeshFormatLightmapped = 3;

// `Lightmaps` / `LightmapTex` on-disk geometry (litmaptypes.h:11-48). The
// Lightmaps header is 0x3C bytes and each LightmapTex record 0x2C, with the
// texture's `short width` at +0x28 and `short height` at +0x2A
// (litmaptypes.h:14-15). Pixels are `width * height * 4` bytes per page and start
// at `0x3C + count * 0x2C` (lightmapbuild.c:72-78).
constexpr std::size_t kOlmHeaderSize = 0x3C;
constexpr std::size_t kOlmTexRecordSize = 0x2C;
constexpr std::size_t kOlmTexSizeOffset = 0x28;
// Four bytes per texel: the builder walks the pixel block with `px += n * 4`
// (lightmapbuild.c:78) and allocates `t->height * t->width * 4` (:109), and the
// texture is allocated with `pitch = t->width * 4` and format id 3
// (lightmapinfo.c:241). The format ID's enum is not stated by the oracle.
constexpr std::size_t kOlmTexBytesPerTexel = 4;
constexpr std::size_t kOlmMagicOffset = 0x00;
constexpr std::size_t kOlmVersionOffset = 0x04;
constexpr std::size_t kOlmTimeOffset = 0x08;
constexpr std::size_t kOlmTimeCount = 7;  // MeshHeader.time[7] (modeltypes.h:11)
constexpr std::size_t kOlmFromResourceOffset = 0x24;
constexpr std::size_t kOlmBuiltOffset = 0x28;
constexpr std::size_t kOlmCountOffset = 0x2C;
constexpr std::size_t kOlmTexelsOffset = 0x30;
constexpr std::size_t kOlmTexels2Offset = 0x34;

// VirModel's LOD cap, VIRMODEL_LODS = 5 (src/model/model.h:11), the upper bound
// of the `1 << lodIndex` factor in the bake scale (lightmapinfo.c:150).
constexpr int kMaxLods = 5;

// ---------------------------------------------------------------------------
// Rule 1 - the resource names (lightmap.c:89-101)
// ---------------------------------------------------------------------------

// "MISSION:lightmaps" (lightmap.c:91).
const char* LightmapDir();
// "MISSION:lightmaps/lightmaps.res" (lightmap.c:98).
const char* LightmapResPath();

// ---------------------------------------------------------------------------
// Rule 2 - the `.olm` name (lightmapbuild.c:59, lightmap.c:240/:255)
// ---------------------------------------------------------------------------

// `"%s/%s_%05d.olm"` (lightmapbuild.c:59). `dir` is normally LightmapDir().
std::string OlmPath(const std::string& dir, const std::string& name, int index);
// The same name without the directory, which is the key the editor's loader
// stores after stripping any prefix (source/renderer/object_lightmap.cpp:111-114).
std::string OlmFileName(const std::string& name, int index);
// `"obj%05d"` (lightmap.c:240, :255).
std::string ObjectLightmapName(int n);
// The inverse of OlmFileName: splits `name` and `index` out of `<name>_<5
// digits>.olm`. `ok` is false when the string is not of that shape.
struct OlmName {
    std::string object_name;
    int index = -1;
    bool ok = false;
};
OlmName ParseOlmFileName(const std::string& file_name);

// One entry as File_ScanDir hands it to LightmapInfo_ScanFile
// (lightmap.c:46-50 ScanEntry, :186-190 the callback).
struct ScanEntry {
    std::string name;
    bool is_dir = false;
};

// LightmapInfo_ScanFile (lightmap.c:186-190, HOOK 0x523420): a DIRECTORY never
// matches, and a file matches when `scan_name` is a SUBSTRING of its name
// (crt_strstr, :188).
bool ScanNameMatches(const ScanEntry& entry, const std::string& scan_name);

// LightmapInfo_MakeName (lightmap.c:231-257, HOOK 0x5235A0): the first n >= 0
// whose `obj%05d` appears as a substring in NO directory entry and in NO existing
// LightmapInfo name. `existing_info_names` is the pool's live names in pool order
// (lightmap.c:244-251); `dir_entries` is LightmapDir()'s listing (:243).
int FindFreeObjectLightmapIndex(const std::vector<std::string>& existing_info_names,
                                const std::vector<ScanEntry>& dir_entries);

// ---------------------------------------------------------------------------
// The timestamp (terredit/datetime.c:16-104)
// ---------------------------------------------------------------------------

// DateTime_Get (datetime.c:16-26) reads GetLocalTime into exactly these seven
// ints, in this order; the same seven ints are `MeshHeader.time[7]`
// (modeltypes.h:11) and `Lightmaps.time[7]` (litmaptypes.h:39).
struct DateTime {
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    int ms = 0;
};

// DateTime_Format(dt, buf, size, 1, 1, 1) (datetime.c:50-73 with :78/:84/:90):
// "%04d-%02d-%02d-%02d-%02d-%02d-%04d", i.e. "YYYY-MM-DD-hh-mm-ss-mmmm".
std::string FormatDateTime(const DateTime& dt);

// DateTime_Compare (datetime.c:94-104): formats both and compares the TEXT.
// Returns -1, 0 or 1. This is NOT a numeric field-by-field compare.
int DateTimeCompare(const DateTime& a, const DateTime& b);

// ---------------------------------------------------------------------------
// Rule 3 - the accept/reject test (lightmapbuild.c:53-88)
// ---------------------------------------------------------------------------

// The `Lightmaps` header of an `.olm`, in the order litmaptypes.h:36-48 declares
// it. Only the fields the accept test reads are kept, plus the two flags a
// reader must not confuse with the pixel layout.
struct OlmFileHeader {
    std::uint32_t magic = 0;
    float version = 0.0f;
    DateTime time;
    int from_resource = 0;  // 0x24, not part of the accept test
    int built = 0;          // 0x28, not part of the accept test
    int count = 0;          // 0x2C, the page count
    int texels = 0;         // 0x30, LIT texels, not the pixel count
    int texels2 = 0;        // 0x34, flagged texels (lightmapbuild.c:370-372)
};

// The mesh side of the test: the MeshHeader fields (modeltypes.h:9-27) plus the
// caller's `full` flag.
struct OlmAcceptInput {
    float mesh_version = kOlmVersion;      // mh->version
    DateTime mesh_time;                    // mh->time
    int mesh_tex_count = 0;                // mesh->texCount
    bool full = false;                     // the `full` argument
};

// What Lightmap_BuildLod does with a file that exists on disk.
enum class OlmDecision {
    kRebuild,       // flush the file (or never load it) and build a new Lightmaps
    kLoadPage0Only, // `full == 0`: use the file as-is (lightmapbuild.c:65-84)
};

// The four-conjunct test (lightmapbuild.c:63-64) WITHOUT the `full` flag: true
// only when magic, version, timestamp and page count all pass.
bool OlmHeaderAccepted(const OlmFileHeader& file, const OlmAcceptInput& mesh);

// The full decision, including the `full` flag: a file that passes the four
// conjuncts is still rebuilt when `mesh.full` is set, because the accept branch
// only returns inside `if (!full)` (lightmapbuild.c:65, :83-84).
OlmDecision DecideOlmFile(const OlmFileHeader& file, const OlmAcceptInput& mesh);

// feq() (dirlight.h:20-23): `!__builtin_islessgreater(a, b)`, i.e. `!(a < b ||
// a > b)`, so an unordered (NaN) comparison counts as EQUAL. Exposed because the
// version conjunct is a feq and a naive `==` differs on NaN.
bool Feq(float a, float b);

// --- reading the bytes -------------------------------------------------------

// One page record of the Lightmaps array (litmaptypes.h:11-17): the two shorts at
// +0x28 of the 0x2C-byte record.
struct OlmPage {
    int width = 0;
    int height = 0;
};

// Parse the fixed header of an `.olm` buffer. False when `size` is smaller than
// the header, or smaller than the header plus `count` page records. Every field
// is read little-endian, as the structs are (litmaptypes.h:36-48).
bool ParseOlmHeader(const std::uint8_t* data, std::size_t size, OlmFileHeader* out);

// The byte offset of page 0's pixels: `0x3C + count * 0x2C`
// (lightmapbuild.c:72 `px = (char *)&file->tex[file->count]`).
std::size_t OlmPixelOffset(int count);

// `0x3C + count * 0x2C + sum(4 * w * h)` -- the exact size a file with these
// pages must have, from the same arithmetic as lightmapbuild.c:72-78.
std::size_t OlmExpectedSize(const std::vector<OlmPage>& pages);

// Read header + all page records, and check the buffer is exactly as large as the
// pages it declares. False on a short buffer or a size mismatch. This is the check
// an editor must make before trusting an `.olm`; it is NOT part of the engine's
// accept test (the engine trusts the resource loader).
bool ReadOlmPages(const std::uint8_t* data, std::size_t size, OlmFileHeader* header,
                  std::vector<OlmPage>* pages);

// Parse ONLY the `count` LightmapTex records, without requiring the pixel block to
// be present. This is what a STREAMING reader needs, because a shipped `.olm` is
// up to a few megabytes and holding the whole file plus a decoded copy at once is
// what fragmented the editor's 32-bit address space: the reader takes the 0x3C
// header (`count` is at 0x2C), then `count` records of 0x2C bytes, then each
// page's pixels at OlmPagePixelOffset() -- the engine's own cursor arithmetic
// (lightmapbuild.c:72-78) without the whole file in memory. `size` must cover the
// header plus `count` records. False on a short buffer, a negative count, or a
// non-positive page dimension (see ReadOlmPages' deviation note).
bool ParseOlmPageRecords(const std::uint8_t* data, std::size_t size, int count,
                         std::vector<OlmPage>* pages);

// The byte offset of page `index`'s pixels inside the file: the end of the record
// array plus the pixel bytes of every earlier page -- exactly the `px` cursor the
// engine walks at lightmapbuild.c:72-78 (`px = (char *)&file->tex[file->count]`,
// then `px += n * 4` per page). Returns 0 when `index` is out of range.
std::size_t OlmPagePixelOffset(const std::vector<OlmPage>& pages, int index);

// Where one page's pixels live in the file: `4 * width * height` bytes at `offset`
// (lightmapbuild.c:76-78, :109).
struct OlmPageSpan {
    int width = 0;
    int height = 0;
    std::size_t offset = 0;
    std::size_t bytes = 0;
};

// The plan a streaming reader needs, from the header+records PREFIX of a file of
// `file_size` bytes: `prefix` must hold the 0x3C header followed by `count` 0x2C
// records (i.e. `OlmPixelOffset(count)` bytes), and `file_size` is the real size of
// the file on disk, which may be larger. Produces one OlmPageSpan per page, in page
// order, and is the single place the loader's arithmetic lives, so it can be tested
// without a GL context.
//
// False when the prefix is short, the header is not parseable, a page dimension is
// non-positive, or any page's pixel block falls outside `file_size`. The engine
// makes none of these checks (`Resource_Load` hands it a whole file and it trusts
// `t->width`/`t->height`); they are the port's guards, named as deviations where
// the underlying parser is.
bool PlanOlmPages(const std::uint8_t* prefix, std::size_t prefix_size,
                  std::uint64_t file_size, std::vector<OlmPageSpan>* spans);

// ---------------------------------------------------------------------------
// Rule 4 - the bake parameters (lightmapinfo.c:97-200)
// ---------------------------------------------------------------------------

// `scale = (1 << lodIndex) * info->scale * kTexelScale` (lightmapinfo.c:150).
// `info->scale` is 1.0f by the time this is evaluated (:105), so this is the
// literal expression, not a corrected one.
float BakeScale(int lod_index, float info_scale);

// `full = stale || info->unkDC || edited` (lightmapinfo.c:149).
bool BakeFullRebuild(bool stale, int unk_dc, bool edited);

// `flags = edited` (lightmapinfo.c:151).
int BakeFlags(bool edited);

// `(int)float_bits(&info->unk94)` (lightmapinfo.c:151): the FLOAT's bit pattern
// reinterpreted as an int, i.e. a float smuggled through an int parameter. The
// default 100.0f becomes 0x42C80000 = 1120403456.
int BakeUnkFromFloatBits(float unk94);

// `edited = unk70 || unk6F || unk71` (lightmapinfo.c:108-110): any of the three
// build push-buttons. Which authored label maps to which field is unverified.
bool BakeEdited(bool button_6f, bool button_70, bool button_71);

// `stale |= (any ref whose lightmaps loaded with info == NULL)`
// (lightmapinfo.c:111-114, and lightmapbuild.c:69 which is what clears `info`).
bool BakeRefsMissing(const std::vector<bool>& ref_has_lightmaps,
                     const std::vector<bool>& ref_has_info);

// The scale clause (lightmapinfo.c:106-107): `!unkCC && float_bits(lastScale) !=
// 0x3F800000`. Bit-exact, not `lastScale != 1.0f`, so -0.0f and NaN behave as the
// engine's compare does.
bool BakeStaleFromScale(int unk_cc, float last_scale);

// The early out (lightmapinfo.c:120): skip the rebuild entirely when the model
// matched, the scale is unchanged (feq), nothing is stale and `unkDC == 0`.
bool BakeEarlyOut(bool model_matched, float last_scale, float scale, bool stale, int unk_dc);

// `AppContext_GetLightmapsUsed()` gates only the ref-set reload
// (lightmapinfo.c:137-138), NOT the BuildLod loop.
bool BakeReloadsRefSet(bool lightmaps_used);

// The hemicube size passed to Hemicube_New (lightmapinfo.c:180), which allocates
// a `size x size` top and four `(size >> 1) x size` sides (lightmap.c:110-121).
int HemicubeSize(int unk_a4);
std::size_t HemicubeTopTexels(int size);
std::size_t HemicubeSideTexels(int size);

// The two post-rebuild branches: the vertex-light pass when `unk6F || unk70`
// (lightmapinfo.c:177) and `Task_Nothing2(info, unk_B4)` when `unk71` (:197).
bool BakeComputesVertexLights(bool button_6f, bool button_70);
bool BakeCallsTaskNothing2(bool button_71);

// ---------------------------------------------------------------------------
// The `.res` container rules (lightmap.c:195-201, :277-317)
// ---------------------------------------------------------------------------

// `if (!p->used && Resource_Exists(Lightmap_GetResPath())) Resource_Load(...)`
// (lightmap.c:200-201): the archive is loaded lazily, once, when the first
// LightmapInfo is created.
bool ShouldLoadLightmapResource(bool pool_empty, bool resource_exists);

// `if (!p->used && Resource_Find(Lightmap_GetResPath())) Resource_Unload(...)`
// (lightmap.c:315-316): and unloaded when the last one is deleted.
bool ShouldUnloadLightmapResource(int pool_used, bool resource_found);

}  // namespace lightmap_rules
}  // namespace igi
