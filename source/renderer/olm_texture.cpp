#include "olm_texture.h"
#include "gl_helper.h"
#include "../common.h"
#include <fstream>
#include <vector>
#include <cstring>

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
    std::ifstream f(olmPath, std::ios::binary);
    if (!f) { err = "cannot open .olm: " + olmPath; return 0; }
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() < kPixelStart) { err = "truncated .olm header: " + olmPath; return 0; }

    uint16_t w = 0, h = 0;
    std::memcpy(&w, &bytes[kPwOffset], 2);
    std::memcpy(&h, &bytes[kPhOffset], 2);
    if (w == 0 || h == 0) { err = "zero-sized .olm: " + olmPath; return 0; }

    const size_t need = static_cast<size_t>(w) * h * 4;
    if (bytes.size() < kPixelStart + need) { err = "truncated .olm pixels: " + olmPath; return 0; }

    // Copy pixels, swapping R<->B to match igi1conv's export (the .olm stores
    // pixels in a BGRA-ish order; swapping yields correct RGBA for sampling).
    std::vector<uint8_t> rgba(need);
    const uint8_t* src = bytes.data() + kPixelStart;
    for (size_t i = 0; i < need; i += 4) {
        rgba[i + 0] = src[i + 2]; // R <- stored B
        rgba[i + 1] = src[i + 1]; // G
        rgba[i + 2] = src[i + 0]; // B <- stored R
        rgba[i + 3] = src[i + 3]; // A
    }

    pic_s pic{ static_cast<int>(w), static_cast<int>(h), rgba.data() };
    return GL_RegisterTexture(&pic, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR, false);
}
