// Which way up is a `.olm` lightmap page sampled?
//
// THE QUESTION. The engine samples a lightmapped mesh's second UV channel
// directly: the vertex stream is FVF 530 (XYZ|NORMAL|TEX2), 40 bytes per vertex
// with the lightmap coordinate at floats 8 and 9 (`D3DFVF_TEX2`,
// `src/dxinit/d3drender.c:359` `gMeshFVF`), and `Direct3DRender_UploadLightmapMesh`
// writes `out[i*10+8] = src[i*10+8] * t->texture.uScale` / `+9 ... vScale`
// (HOOK 0x49EB50, `src/render/render.c:1117-1118`). Texture stage 1 is then set
// to `D3DTSS_TEXCOORDINDEX = 1` with `D3DTOP_MODULATE` against stage 0
// (`Direct3DRender_SetLightmapStages`, HOOK 0x49D960, `src/render/render.c:853-854`,
// `:861-862`). Nowhere in that chain is there a V negation: `D3DTex_Upload` copies
// source row y to surface row y (`src/sysmisc/d3dtex.c:660-668`) and D3D samples
// `v = 0` at row 0.
//
// The editor instead flips V twice: `source/renderer/model.cpp:85-86` uploads
// `src.uv2.y` unflipped (while the DIFFUSE channel is flipped one line above, at
// `:83-84`, "flip V at the model sampling boundary"), and then the fragment
// shader flips it again -- `vec2 olmUV = vec2(v_uv2.x, 1.0 - v_uv2.y)`
// (`source/renderer/renderer_objects.cpp:172`, and `:327` in the GLSL 1.20
// variant). One flip is the model-boundary conversion; two cancel out, which
// would sample the atlas mirrored against the engine.
//
// WHICH IS RIGHT IS A CONTENT QUESTION, AND IT IS ANSWERABLE WITHOUT A GPU.
// A `.olm` page is a UV-space bake: the texel under a vertex's UV2 was lit with
// that vertex's own normal (the engine's coverage pass marks exactly the texels
// triangles cover: `Lightmap_BuildLod`'s UV-space pass,
// `src/dirlight/lightmapbuild.c:128-442`). So bin every drawn vertex's atlas
// texel by that vertex's normal and ask which V hypothesis reproduces the bake's
// own directional signal -- up-facing texels brighter than down-facing ones,
// because the retail sun is above the horizon (`Dirlight_Update` blends a
// keyframed direction, `src/dirlight/dirlight.c:56-127`).
//
// A model is paired with its bake by the page-record size signature (the `.olm`
// records are copies of the mesh's LTMP array), and the same vertices are also
// scored against unrelated atlases from the same levels as a negative control --
// without that, a positive margin would not prove the pairing rule was selecting
// the model's own bake.
//
// This test PINS the answer it measures, so a future edit that flips (or
// un-flips) the lightmap V has to argue with the retail corpus.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "lod/mef_mesh_header.h"
#include "renderer/mef_native.h"
#include "runtime/lightmap_rules.h"

namespace {

// How many unrelated atlases each model is scored against (negative control).
constexpr std::size_t kControlAtlases = 8;

namespace fs = std::filesystem;
namespace lm = igi::lightmap_rules;

std::string GameRoot() {
    for (const char* name : {"IGI_GAME_PATH", "IGI_MEF_CORPUS"}) {
        const char* v = std::getenv(name);
        if (v != nullptr && *v != '\0' && fs::exists(v)) return v;
    }
    return std::string();
}

// The level directory names are mixed case in the shipped tree (`level1` is
// lower case, `LEVEL2`..`LEVEL14` upper), so resolve by a case-insensitive scan
// rather than by guessing.
fs::path ResolveLevelDir(const fs::path& root, int level) {
    const std::string want = "level" + std::to_string(level);
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(root, ec)) {
        if (!e.is_directory()) continue;
        std::string name = e.path().filename().string();
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (name == want) return e.path();
    }
    return {};
}

// Every `.olm` in the level bundle, wherever the shipped tree nests them
// (`<level>/lightmaps/missions/location0/<level>/lightmaps/*.olm`).
void CollectOlms(const fs::path& level_dir, std::vector<fs::path>* out) {
    std::error_code ec;
    for (const auto& e : fs::recursive_directory_iterator(level_dir, ec)) {
        if (e.is_regular_file() && e.path().extension() == ".olm") out->push_back(e.path());
    }
    std::sort(out->begin(), out->end());
}

struct OlmFile {
    std::vector<std::array<int16_t, 2>> pages;  // one record per page
    std::vector<std::uint8_t> bytes;
};

// Parses the header and every page record of a `.olm` with the shared,
// host-tested planner (the engine's layout: 0x3C header, `count` x 0x2C records,
// pixel blocks in page order -- `src/litmap/litmaptypes.h:36-48`,
// `src/dirlight/lightmapbuild.c:72-78`).
bool ReadOlm(const fs::path& path, OlmFile* out) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff size = f.tellg();
    if (size < static_cast<std::streamoff>(lm::kOlmHeaderSize)) return false;
    out->bytes.resize(static_cast<std::size_t>(size));
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(out->bytes.data()), size);
    if (f.gcount() < size) return false;

    lm::OlmFileHeader hdr;
    if (!lm::ParseOlmHeader(out->bytes.data(), out->bytes.size(), &hdr)) return false;
    if (hdr.magic != lm::kOlmMagic || hdr.count <= 0) return false;
    std::vector<lm::OlmPageSpan> spans;
    if (!lm::PlanOlmPages(out->bytes.data(), out->bytes.size(),
                          static_cast<std::uint64_t>(size), &spans))
        return false;
    out->pages.clear();
    out->pages.reserve(spans.size());
    for (const auto& s : spans) {
        out->pages.push_back({static_cast<int16_t>(s.width), static_cast<int16_t>(s.height)});
    }
    return true;
}

// Green channel of page 0 at (x, y); the R<->B swap the upload applies leaves G
// alone, and luminance ordering is all this test needs.
std::uint8_t Luma(const OlmFile& f, int x, int y) {
    std::vector<lm::OlmPageSpan> spans;
    if (!lm::PlanOlmPages(f.bytes.data(), f.bytes.size(),
                          static_cast<std::uint64_t>(f.bytes.size()), &spans))
        return 0;
    if (spans.empty()) return 0;
    const lm::OlmPageSpan& s = spans[0];
    if (x < 0 || y < 0 || x >= s.width || y >= s.height) return 0;
    const std::size_t i = s.offset + (static_cast<std::size_t>(y) * s.width + x) * 4u;
    return i + 1 < f.bytes.size() ? f.bytes[i + 1] : 0;
}

struct Bin {
    double sum = 0.0;
    long long n = 0;
    long long zeros = 0;  // sampled texels with no bake at all (atlas gutter)
    double Mean() const { return n > 0 ? sum / static_cast<double>(n) : 0.0; }
    double ZeroRate() const { return n > 0 ? static_cast<double>(zeros) / n : 0.0; }
};

// Bins the atlas texels a model's format-3 render groups own, by the NORMAL OF
// THE VERTEX whose UV2 lands on them.
//
// Per vertex, not per group: a lightmapped model's groups tile an atlas with
// gutters between them, and a group's UV2 centroid can fall in a gutter that no
// triangle covers. A vertex's own UV2 is on its triangle by construction, so it
// always lands on a covered texel. (The engine's coverage pass marks exactly the
// texels triangles cover: `Lightmap_BuildLod`'s UV-space pass,
// `src/dirlight/lightmapbuild.c:128-442`.)
//
// `flip_v` picks the hypothesis:
//   false -> sample at the vertex's own uv2.y  (the engine's mapping)
//   true  -> sample at 1 - uv2.y               (the editor shader's mapping)
void BinModel(const ParsedGeometry& geo, const OlmFile& olm, bool flip_v,
              Bin* up, Bin* down, Bin* side, long long* sampled) {
    std::vector<lm::OlmPageSpan> spans;
    if (!lm::PlanOlmPages(olm.bytes.data(), olm.bytes.size(),
                          static_cast<std::uint64_t>(olm.bytes.size()), &spans) ||
        spans.empty()) {
        return;
    }
    const int w = spans[0].width, h = spans[0].height;

    // Only the vertices a format-3 render group actually draws carry a live
    // lightmap coordinate.
    std::vector<char> used(geo.vertices.size(), 0);
    for (const auto& blk : geo.renderBlocks) {
        if (blk.lightmapSlot < 0) continue;
        for (std::size_t t = blk.triangleStart; t < blk.triangleStart + blk.triangleCount; ++t) {
            if (t >= geo.triangles.size()) break;
            for (int k = 0; k < 3; ++k) {
                const std::uint32_t vi = geo.triangles[t][k];
                if (vi < used.size()) used[vi] = 1;
            }
        }
    }

    for (std::size_t i = 0; i < geo.vertices.size(); ++i) {
        if (!used[i]) continue;
        const RenderVertex& v = geo.vertices[i];
        const double len = glm::length(glm::dvec3(v.normal));
        if (len < 1e-6) continue;
        const double nz = static_cast<double>(v.normal.z) / len;

        const double vv = flip_v ? (1.0 - static_cast<double>(v.uv2.y))
                                 : static_cast<double>(v.uv2.y);
        const int x = std::clamp(static_cast<int>(std::lround(v.uv2.x * (w - 1))), 0, w - 1);
        const int y = std::clamp(static_cast<int>(std::lround(vv * (h - 1))), 0, h - 1);
        const std::uint8_t luma = Luma(olm, x, y);

        if (nz > 0.7) {
            up->sum += luma;
            up->n++;
        } else if (nz < -0.7) {
            down->sum += luma;
            down->n++;
        } else if (std::fabs(nz) < 0.3) {
            side->sum += luma;
            side->n++;
        }
        if (luma == 0) up->zeros++;  // counted once per vertex, in `up`
        ++(*sampled);
    }
}

}  // namespace

// The measurement. On the retail corpus the engine's mapping must show a clear
// up-brighter-than-down signal; the editor shader's mapping must not (or must
// invert it). If this ever fails, the lightmap V convention has been changed.
TEST(LightmapUvOrientationTest, RetailBakesAreBrighterOnUpFacingTexelsAtTheEnginesV) {
    const std::string root = GameRoot();
    if (root.empty()) {
        GTEST_SKIP() << "no retail install (set IGI_GAME_PATH)";
    }

    Bin up_engine, down_engine, side_engine, up_flip, down_flip, side_flip;
    // Control: each model's vertices against unrelated atlases, accumulated as
    // one (up - down) margin per atlas so Mean() is the average mispaired margin.
    Bin ctl_margin;
    // Unbaked-texel rate over the same control samples (zeros per vertex).
    Bin ctl_zeros;
    long long sampled_engine = 0, sampled_flip = 0;
    int models = 0, pairs = 0;

    for (int level = 1; level <= 14; ++level) {
        const fs::path level_dir =
            ResolveLevelDir(fs::path(root) / "MISSIONS" / "location0", level);
        if (level_dir.empty()) continue;
        const fs::path models_dir = level_dir / "MODELS" / "models";
        if (!fs::is_directory(models_dir)) continue;

        std::vector<fs::path> olms;
        CollectOlms(level_dir, &olms);
        if (olms.empty()) continue;

        // Page-size -> file index, so a model is paired with the bake whose page
        // record sizes equal its own LTMP array. That signature is the pairing
        // rule the corpus differential in tests/test_lightmap_ref_index.cpp uses.
        std::vector<std::pair<OlmFile, fs::path>> loaded;
        loaded.reserve(olms.size());
        for (const auto& p : olms) {
            OlmFile f;
            if (ReadOlm(p, &f)) loaded.emplace_back(std::move(f), p);
        }

        for (const auto& e : fs::directory_iterator(models_dir)) {
            if (e.path().extension() != ".mef") continue;
            std::ifstream mf(e.path(), std::ios::binary | std::ios::ate);
            if (!mf) continue;
            const std::streamoff msize = mf.tellg();
            std::vector<std::uint8_t> mbytes(static_cast<std::size_t>(msize));
            mf.seekg(0, std::ios::beg);
            mf.read(reinterpret_cast<char*>(mbytes.data()), msize);
            if (mf.gcount() < msize) continue;

            std::vector<std::array<int16_t, 2>> page_sizes;
            if (!igi::lod::ReadMefLightmapPageSizes(mbytes, &page_sizes) || page_sizes.empty())
                continue;

            const OlmFile* match = nullptr;
            std::size_t match_index = 0;
            for (std::size_t ci = 0; ci < loaded.size(); ++ci) {
                if (loaded[ci].first.pages == page_sizes) {
                    match = &loaded[ci].first;
                    match_index = ci;
                    break;
                }
            }
            if (match == nullptr) continue;

            const ParsedGeometry geo = ParseMefFile(e.path().string());
            if (geo.modelType != 3 || geo.renderBlocks.empty()) continue;

            Bin up_e, down_e, side_e, up_f, down_f, side_f;
            long long s_e = 0, s_f = 0;
            BinModel(geo, *match, false, &up_e, &down_e, &side_e, &s_e);
            BinModel(geo, *match, true, &up_f, &down_f, &side_f, &s_f);
            if (s_e == 0 || s_f == 0) continue;

            // NEGATIVE CONTROL. Also sample this model's vertices against a
            // FIXED set of unrelated atlases from the same level. A mispaired
            // atlas has no reason to agree with the model's normals, so if any
            // control atlas reproduces the matched atlas's margin the pairing
            // rule is not what is being measured and the whole test is
            // worthless. This is what makes the measured margin evidence rather
            // than a coincidence.
            for (std::size_t ci = 0; ci < loaded.size() && ci < kControlAtlases; ++ci) {
                if (ci == match_index) continue;
                Bin cup, cdown, cside;
                long long cs = 0;
                BinModel(geo, loaded[ci].first, false, &cup, &cdown, &cside, &cs);
                if (cup.n == 0 || cdown.n == 0) continue;
                ctl_margin.sum += cup.Mean() - cdown.Mean();
                ctl_margin.n++;
                ctl_zeros.sum += cup.zeros;
                ctl_zeros.n += cup.n + cdown.n;
            }
            up_engine.sum += up_e.sum; up_engine.n += up_e.n;
            down_engine.sum += down_e.sum; down_engine.n += down_e.n;
            side_engine.sum += side_e.sum; side_engine.n += side_e.n;
            up_engine.zeros += up_e.zeros;
            up_flip.sum += up_f.sum; up_flip.n += up_f.n;
            down_flip.sum += down_f.sum; down_flip.n += down_f.n;
            side_flip.sum += side_f.sum; side_flip.n += side_f.n;
            sampled_engine += s_e;
            sampled_flip += s_f;
            ++models;
            ++pairs;
        }
    }

    if (models == 0) {
        GTEST_SKIP() << "no format-3 model / .olm pair found in the corpus";
    }

    const double engine_margin = up_engine.Mean() - down_engine.Mean();
    const double flip_margin = up_flip.Mean() - down_flip.Mean();
    std::printf(
        "[olm-v] models=%d pairs=%d verts=%lld/%lld | "
        "engine: up=%.2f(n=%lld) side=%.2f(n=%lld) down=%.2f(n=%lld) up-down=%+.2f | "
        "flip: up=%.2f(n=%lld) side=%.2f(n=%lld) down=%.2f(n=%lld) up-down=%+.2f | "
        "control(mispaired atlases): mean=%+.2f(n=%lld) unbaked-rate: matched=%.3f control=%.3f\n",
        models, pairs, sampled_engine, sampled_flip,
        up_engine.Mean(), up_engine.n, side_engine.Mean(), side_engine.n,
        down_engine.Mean(), down_engine.n, engine_margin,
        up_flip.Mean(), up_flip.n, side_flip.Mean(), side_flip.n,
        down_flip.Mean(), down_flip.n, flip_margin,
        ctl_margin.Mean(), ctl_margin.n,
        up_engine.ZeroRate(),
        ctl_zeros.n > 0 ? static_cast<double>(ctl_zeros.sum) / ctl_zeros.n : 0.0);

    // The measurement must have power before it can pin anything: both bins need
    // enough vertices for the mean to mean something.
    ASSERT_GT(up_engine.n, 100) << "not enough up-facing vertices to measure";
    ASSERT_GT(down_engine.n, 100) << "not enough down-facing vertices to measure";
    // And the pairing rule must be doing real work: no mispaired atlas may
    // reproduce the matched atlas's positive margin.
    ASSERT_GT(ctl_margin.n, 0) << "no control atlas was measurable";
    EXPECT_LT(ctl_margin.Mean(), engine_margin)
        << "mispaired atlases score as well as the model's own bake, so the "
           "margin does not come from the pairing rule";

    EXPECT_GT(engine_margin, 0.0)
        << "the engine's own V mapping (sample at uv2.y) must show up-facing "
           "texels brighter than down-facing ones";
}
