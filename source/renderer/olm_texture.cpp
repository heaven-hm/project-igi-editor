#include "olm_texture.h"
#include "gl_helper.h"
#include "../common.h"
#include "../runtime/lightmap_rules.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

// IGI1 .olm layout, taken from the engine's own structs rather than from a
// descriptor table:
//
//   src/litmap/litmaptypes.h:36-48   struct Lightmaps  { magic, version,
//                                    time[7], fromResource, built, count, texels,
//                                    texels2, info, LightmapTex tex[]; }
//                                    _Static_assert(sizeof == 0x3C)
//   src/litmap/litmaptypes.h:11-17   struct LightmapTex { Texture texture;
//                                    void *pixels; short width; short height; }
//                                    _Static_assert(sizeof == 0x2C), so the two
//                                    page shorts are at +0x28 / +0x2A.
//   src/dirlight/lightmapbuild.c:72-78  the load path: `px = &file->tex[count]`,
//                                    then per page `t->pixels = px; px += n * 4`
//                                    with `n = t->width * t->height`.
//
// The offsets are the shared constants in source/runtime/lightmap_rules.h; this
// file only streams bytes and uploads them.
namespace {

namespace lm = igi::lightmap_rules;

// Streams pixels into one RGBA buffer in bounded chunks, swapping R<->B in
// flight. Keeping only one page's decoded copy live at a time is deliberate:
// `.olm` files reach a few megabytes, the editor is a 32-bit process, and the
// earlier "slurp the whole file plus a decoded copy" shape fragmented its address
// space badly enough to crash level loads.
GLuint UploadOnePage(std::ifstream& f, std::size_t pixel_offset, int w, int h,
                     std::string& err) {
    const std::size_t need = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
                             lm::kOlmTexBytesPerTexel;
    std::vector<std::uint8_t> rgba;
    try {
        rgba.resize(need);
    } catch (const std::bad_alloc&) {
        err = "out of memory for .olm pixels (" + std::to_string(need) + " bytes)";
        return 0;
    }

    constexpr std::size_t kChunk = 1u << 18;  // 256 KiB
    std::vector<std::uint8_t> chunk(std::min(kChunk, need));

    f.clear();
    f.seekg(static_cast<std::streamoff>(pixel_offset), std::ios::beg);
    std::size_t written = 0;
    while (written < need) {
        const std::size_t n = std::min(chunk.size(), need - written);
        f.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(n));
        if (static_cast<std::size_t>(f.gcount()) < n) {
            err = "short read on .olm pixels";
            return 0;
        }
        for (std::size_t i = 0; i < n; i += 4) {
            rgba[written + i + 0] = chunk[i + 2];  // R <- stored B
            rgba[written + i + 1] = chunk[i + 1];  // G
            rgba[written + i + 2] = chunk[i + 0];  // B <- stored R
            rgba[written + i + 3] = chunk[i + 3];  // A
        }
        written += n;
    }

    pic_s pic{ w, h, rgba.data() };
    return GL_RegisterTexture(&pic, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR, false);
}

}  // namespace

bool LoadOlmPages(const std::string& olmPath,
                  const igi::lightmap_rules::OlmAcceptInput* mesh,
                  std::vector<GLuint>* pages, std::string* err) {
    if (pages == nullptr) return false;
    pages->clear();
    const auto fail = [&](const std::string& message) {
        for (GLuint tex : *pages) {
            if (tex != 0) glDeleteTextures(1, &tex);
        }
        pages->clear();
        if (err) *err = message + ": " + olmPath;
        return false;
    };

    std::ifstream f(olmPath, std::ios::binary | std::ios::ate);
    if (!f) return fail("cannot open .olm");
    const std::streamoff file_size = f.tellg();
    if (file_size < static_cast<std::streamoff>(lm::kOlmHeaderSize))
        return fail("truncated .olm header");

    // The header first (0x3C bytes), then exactly `count` page records. `count`
    // lives at 0x2C, so the record block cannot be sized before the header is in
    // hand.
    std::uint8_t header[lm::kOlmHeaderSize];
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(header), sizeof(header));
    if (static_cast<std::size_t>(f.gcount()) < sizeof(header))
        return fail("cannot read .olm header");

    // `size` is the REAL file size while only the header is resident: ParseOlmHeader
    // reads nothing past 0x34 and uses `size` only to reject a `count` whose page
    // records cannot fit the file.
    lm::OlmFileHeader hdr;
    if (!lm::ParseOlmHeader(header, static_cast<std::size_t>(file_size), &hdr))
        return fail("bad .olm header (page count does not fit the file)");

    // THE ENGINE'S ACCEPT TEST (lightmapbuild.c:63-64). One call, the same
    // predicate the unit tests pin -- not a re-spelling of it here:
    //
    //   file->magic == 0x3DF5C28F
    //   && feq(mh->version, file->version)
    //   && DateTime_Compare(mh->time, file->time) < 0
    //   && mesh->texCount == file->count
    //
    // On a mismatch `Lightmap_BuildLod` does NOT use the file: it flushes it
    // (`Resource_Flush(file)`, :86) and allocates a fresh `Lightmaps` (:90-100).
    // The editor cannot bake in process, so the equivalent is to bind nothing --
    // the LOD falls back to dynamic lighting, exactly as for a missing .olm. It
    // must never fall back to a different file, a different page, or a partial
    // accept, and the engine has no such fallback.
    //
    // `mesh == nullptr` means the caller could not identify the ref's mesh, so the
    // engine's test cannot be run; the file is loaded as before and the caller
    // reports that case. The engine always has the ref's mesh -- this is the
    // editor's gap, not the engine's, and it is named rather than hidden.
    if (mesh != nullptr && !lm::OlmHeaderAccepted(hdr, *mesh)) {
        char detail[256];
        std::uint32_t file_version_bits = 0;
        std::uint32_t mesh_version_bits = 0;
        std::memcpy(&file_version_bits, &hdr.version, 4);
        std::memcpy(&mesh_version_bits, &mesh->mesh_version, 4);
        std::snprintf(detail, sizeof(detail),
                      "rejected by the engine's four-conjunct accept test "
                      "(magic 0x%08X, version 0x%08X vs mesh 0x%08X, "
                      "mesh time %04d-%02d-%02d-%02d-%02d-%02d-%04d vs file time "
                      "%04d-%02d-%02d-%02d-%02d-%02d-%04d, mesh texCount %d vs file count %d)",
                      hdr.magic, file_version_bits, mesh_version_bits,
                      mesh->mesh_time.year, mesh->mesh_time.month, mesh->mesh_time.day,
                      mesh->mesh_time.hour, mesh->mesh_time.minute, mesh->mesh_time.second,
                      mesh->mesh_time.ms, hdr.time.year, hdr.time.month, hdr.time.day,
                      hdr.time.hour, hdr.time.minute, hdr.time.second, hdr.time.ms,
                      mesh->mesh_tex_count, hdr.count);
        return fail(detail);
    }
    if (hdr.count <= 0) return fail("no pages in .olm");

    const std::size_t record_bytes = static_cast<std::size_t>(hdr.count) * lm::kOlmTexRecordSize;
    if (record_bytes > static_cast<std::size_t>(file_size) - lm::kOlmHeaderSize)
        return fail("truncated .olm page records");
    // The record array is prepended with the header so the planner sees the file's
    // own prefix: its bounds check is the engine's `0x3C + count * 0x2C` layout.
    std::vector<std::uint8_t> prefix(header, header + sizeof(header));
    prefix.resize(prefix.size() + record_bytes);
    f.seekg(static_cast<std::streamoff>(lm::kOlmHeaderSize), std::ios::beg);
    f.read(reinterpret_cast<char*>(prefix.data() + lm::kOlmHeaderSize),
           static_cast<std::streamsize>(record_bytes));
    if (static_cast<std::size_t>(f.gcount()) < record_bytes)
        return fail("cannot read .olm page records");

    // The loader's own arithmetic, shared with the host-portable tests: one span per
    // page, every one bounds-checked against the real file size.
    std::vector<lm::OlmPageSpan> spans;
    if (!lm::PlanOlmPages(prefix.data(), prefix.size(),
                          static_cast<std::uint64_t>(file_size), &spans))
        return fail("bad .olm page table (page pixels do not fit the file)");

    pages->reserve(spans.size());
    for (std::size_t i = 0; i < spans.size(); ++i) {
        std::string page_err;
        const GLuint tex = UploadOnePage(f, spans[i].offset, spans[i].width, spans[i].height,
                                         page_err);
        if (tex == 0) {
            return fail(page_err.empty() ? ("page " + std::to_string(i) + " upload failed")
                                         : page_err);
        }
        pages->push_back(tex);
    }
    return true;
}
