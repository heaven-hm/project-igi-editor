#include "olm_texture.h"
#include "gl_helper.h"
#include "../common.h"
#include <fstream>
#include <vector>
#include <cstring>
#include <algorithm>

// IGI1 .olm binary layout (see project-igi-conv Lightmap_docs.md §2.3-2.6):
//   [88]   main header
//   [16]   layer descriptor (pixel_width at +12, pixel_height at +14, uint16 LE)
//   [w*h*4] RGBA pixel data
static constexpr size_t kMainHeaderSize = 88;
static constexpr size_t kLayerDescSize  = 16;
static constexpr size_t kPixelStart     = kMainHeaderSize + kLayerDescSize; // 104
static constexpr size_t kPwOffset       = kMainHeaderSize + 12;             // 100
static constexpr size_t kPhOffset       = kMainHeaderSize + 14;             // 102

GLuint LoadOlmAsTexture(const std::string& olmPath, std::string& err) {
    // Stream the .olm instead of slurping the whole file into one contiguous
    // vector. Lightmap archives contain many .olm files; the old code kept BOTH
    // the full file bytes AND a second w*h*4 RGBA copy live at once, which
    // fragments the 32-bit address space and crashes level loads once it
    // fragments (level 3+ with lightmaps on). Reading the header, then the
    // pixels in bounded chunks into a single RGBA buffer keeps peak memory to
    // one texture plus a small scratch — the same per-entry strategy RES_ForEachEntry
    // already uses for the large .res archives (res_writer.cpp).
    std::ifstream f(olmPath, std::ios::binary | std::ios::ate);
    if (!f) { err = "cannot open .olm: " + olmPath; return 0; }
    const std::streamoff fileSize = f.tellg();
    if (fileSize < (std::streamoff)kPixelStart) { err = "truncated .olm header: " + olmPath; return 0; }

    uint8_t header[kPixelStart];
    f.seekg(0, std::ios::beg);
    f.read(reinterpret_cast<char*>(header), sizeof(header));
    if (static_cast<size_t>(f.gcount()) < sizeof(header)) {
        err = "cannot read .olm header: " + olmPath; return 0;
    }

    uint16_t w = 0, h = 0;
    std::memcpy(&w, &header[kPwOffset], 2);
    std::memcpy(&h, &header[kPhOffset], 2);
    if (w == 0 || h == 0) { err = "zero-sized .olm: " + olmPath; return 0; }

    const size_t need = static_cast<size_t>(w) * h * 4;
    if (static_cast<size_t>(fileSize) < kPixelStart + need) {
        err = "truncated .olm pixels: " + olmPath; return 0;
    }

    std::vector<uint8_t> rgba;
    try {
        rgba.resize(need);
    } catch (const std::bad_alloc&) {
        err = "out of memory for .olm pixels (" + std::to_string(need) + " bytes): " + olmPath;
        return 0;
    }

    // Copy pixels from the file in bounded chunks, swapping R<->B in flight. The
    // .olm stores pixels in a BGRA-ish order; swapping yields RGBA for sampling.
    constexpr size_t kChunk = 1u << 18; // 256 KiB
    std::vector<uint8_t> chunk;
    chunk.resize(std::min(kChunk, need));

    f.seekg(static_cast<std::streamoff>(kPixelStart), std::ios::beg);
    size_t written = 0;
    while (written < need) {
        const size_t n = std::min(chunk.size(), need - written);
        f.read(reinterpret_cast<char*>(chunk.data()), static_cast<std::streamsize>(n));
        if (static_cast<size_t>(f.gcount()) < n) {
            err = "short read on .olm pixels: " + olmPath; return 0;
        }
        for (size_t i = 0; i < n; i += 4) {
            rgba[written + i + 0] = chunk[i + 2]; // R <- stored B
            rgba[written + i + 1] = chunk[i + 1]; // G
            rgba[written + i + 2] = chunk[i + 0]; // B <- stored R
            rgba[written + i + 3] = chunk[i + 3]; // A
        }
        written += n;
    }

    pic_s pic{ static_cast<int>(w), static_cast<int>(h), rgba.data() };
    return GL_RegisterTexture(&pic, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR, false);
}
