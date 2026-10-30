/******************************************************************************
 * @file    source/runtime/editor_pointer_sprite.cpp
 * @brief   The editor's own pointer (`editor/qed/pointer.anm`): LOOP v9 reader.
 *
 * The oracle for every field read here is in the header
 * (source/runtime/editor_pointer_sprite.h): `LoopHeader` / `LoopFrame9` and the
 * v9 branch of `Sprite_Load` HOOK 0x4B6720 (src/res/sprite.c:113-146, :246-260),
 * and the per-format converters the engine's `D3DTex_DownloadTexture`
 * HOOK 0x50AD10 selects (src/sysmisc/d3dtex.c:411-432 -> src/terrdraw/pixel.c).
 *
 * Deliberately free of Logger and Config: this is the half the host lane links.
 *****************************************************************************/

#include "editor_pointer_sprite.h"

#include <fstream>
#include <iterator>

namespace igi {
namespace {

uint32_t ReadU32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t ReadU16(const uint8_t* p) {
    return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                                 (static_cast<uint16_t>(p[1]) << 8));
}

// The engine's own converter table (src/sysmisc/d3dtex.c:411-432).  Bytes per
// pixel, or 0 for a format the engine refuses with "Unsupported texture format".
uint32_t DepthForFormat(uint32_t format) {
    switch (format) {
        case kFormat1555:      return 2;  // Pixel_1555To32 HOOK 0x517C90
        case kFormat565:       return 2;  // Pixel_565To32  HOOK 0x517DD0
        case kFormat8888:      return 4;  // Pixel_Copy32   HOOK 0x517F00
        case kFormat8888Alt:   return 4;
        case kFormat8888B:     return 4;
        default:               return 0;
    }
}

// One source pixel -> the four RGBA bytes, using the engine's own arithmetic:
//   Pixel_1555To32  src/terrdraw/pixel.c:59-83
//       B = (p & 0x1F) << 3, G = (p >> 2) & 0xF8, R = (p >> 7) & 0xF8,
//       A = (p & 0x8000) ? 0xFF : 0
//   Pixel_565To32   src/terrdraw/pixel.c:85-109
//       B = (p & 0x1F) << 3, G = (p >> 3) & 0xFC, R = (p >> 8) & 0xF8, A = 0xFF
//   Pixel_Copy32    src/terrdraw/pixel.c:112-124 -- the bytes are copied
//       verbatim, i.e. the file's B,G,R,A order.
// The converters write B,G,R,A (D3D's ARGB8888 memory order); this writes the
// same channel values in R,G,B,A, which is what `pic_s` and GL take.
void DecodePixel(uint32_t format, const uint8_t* src, uint8_t* dst) {
    switch (format) {
        case kFormat1555: {
            const uint32_t p = ReadU16(src);
            // R = (p >> 10 & 0x1F) << 3 == (p >> 7) & 0xF8, and likewise for G.
            dst[0] = static_cast<uint8_t>((p >> 7) & 0xF8u);          // R
            dst[1] = static_cast<uint8_t>((p >> 2) & 0xF8u);          // G
            dst[2] = static_cast<uint8_t>((p & 0x1Fu) << 3);          // B
            dst[3] = (p & 0x8000u) != 0 ? 0xFFu : 0x00u;              // A
            return;
        }
        case kFormat565: {
            const uint32_t p = ReadU16(src);
            dst[0] = static_cast<uint8_t>((p >> 8) & 0xF8u);          // R
            dst[1] = static_cast<uint8_t>((p >> 3) & 0xFCu);          // G
            dst[2] = static_cast<uint8_t>((p & 0x1Fu) << 3);          // B
            dst[3] = 0xFF;
            return;
        }
        default: {
            // Pixel_Copy32: B,G,R,A in the file.
            dst[0] = src[2];
            dst[1] = src[1];
            dst[2] = src[0];
            dst[3] = src[3];
            return;
        }
    }
}

}  // namespace

bool ReadLoopV9Frames(const uint8_t* bytes, std::size_t size,
                      std::vector<EditorPointerFrame>* frames, std::string* error) {
    const auto fail = [error](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (frames == nullptr) {
        return fail("no output");
    }
    frames->clear();
    if (bytes == nullptr || size < kLoopHeaderSize) {
        return fail("short LOOP header");
    }
    // `Loop_Is` (src/res/sprite.c:148-151): the magic is checked from index 1 and
    // the version word must be the one asked for.
    if (bytes[1] != 'O' || bytes[2] != 'O' || bytes[3] != 'P') {
        return fail("not a LOOP container");
    }
    if (ReadU32(bytes + 4) != kLoopVersion9) {
        return fail("LOOP version " + std::to_string(ReadU32(bytes + 4)) +
                    ", not 9");
    }
    // `LoopHeader::nframes` is at 32 (src/res/sprite.c:113-127).
    const uint32_t count = ReadU32(bytes + 32);
    if (count == 0) {
        return fail("LOOP v9 declares 0 frames");
    }
    if (kLoopHeaderSize + static_cast<std::size_t>(count) * kLoopFrame9Size > size) {
        return fail("LOOP v9 frame table runs past the end");
    }
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* record = bytes + kLoopHeaderSize + static_cast<std::size_t>(i) * kLoopFrame9Size;
        EditorPointerFrame frame;
        frame.offset = ReadU32(record + 0);       // `pixels`
        frame.format = ReadU32(record + 4);       // `format`
        frame.stride = ReadU16(record + 8);       // `a`, bytes per row
        frame.width = ReadU16(record + 10);       // `w`
        frame.height = ReadU16(record + 12);      // `h`
        if (frame.width <= 0 || frame.height <= 0) {
            return fail("frame " + std::to_string(i) + " has a zero dimension");
        }
        if (frame.stride == 0) {
            return fail("frame " + std::to_string(i) + " has a zero stride");
        }
        frames->push_back(frame);
    }
    return true;
}

bool ParseEditorPointerAnm(const uint8_t* bytes, std::size_t size,
                           EditorPointerSprite* out, std::string* error,
                           int frame_index) {
    const auto fail = [error](const std::string& message) {
        if (error != nullptr) {
            *error = message;
        }
        return false;
    };
    if (out == nullptr) {
        return fail("no output");
    }
    *out = EditorPointerSprite{};

    std::vector<EditorPointerFrame> frames;
    if (!ReadLoopV9Frames(bytes, size, &frames, error)) {
        return false;
    }
    if (frame_index < 0 || static_cast<std::size_t>(frame_index) >= frames.size()) {
        return fail("frame " + std::to_string(frame_index) + " of " +
                    std::to_string(frames.size()));
    }
    const EditorPointerFrame& frame = frames[static_cast<std::size_t>(frame_index)];
    const uint32_t depth = DepthForFormat(frame.format);
    if (depth == 0) {
        // The engine's own fatal string (src/sysmisc/d3dtex.c:430-432).
        return fail("Unsupported texture format " + std::to_string(frame.format));
    }
    const std::size_t row_bytes = static_cast<std::size_t>(frame.width) * depth;
    if (frame.stride < row_bytes) {
        // Not a case the engine guards, but a stride narrower than a row would
        // make frames overlap; refuse rather than decode a shifted image.
        return fail("frame " + std::to_string(frame_index) + " stride " +
                    std::to_string(frame.stride) + " is narrower than a row (" +
                    std::to_string(row_bytes) + ")");
    }
    const std::size_t end = static_cast<std::size_t>(frame.offset) +
                            static_cast<std::size_t>(frame.height - 1) * frame.stride +
                            row_bytes;
    if (frame.offset > size || end > size) {
        return fail("frame " + std::to_string(frame_index) + " bitmap runs past the end");
    }

    out->rgba.assign(static_cast<std::size_t>(frame.width) *
                         static_cast<std::size_t>(frame.height) * 4u,
                     0);
    for (int y = 0; y < frame.height; ++y) {
        const uint8_t* row = bytes + frame.offset + static_cast<std::size_t>(y) * frame.stride;
        uint8_t* dst = out->rgba.data() + static_cast<std::size_t>(y) *
                                              static_cast<std::size_t>(frame.width) * 4u;
        for (int x = 0; x < frame.width; ++x) {
            DecodePixel(frame.format, row + static_cast<std::size_t>(x) * depth,
                        dst + static_cast<std::size_t>(x) * 4u);
        }
    }

    out->ok = true;
    out->width = frame.width;
    out->height = frame.height;
    out->hotspot_x = kEditorPointerHotspotX;
    out->hotspot_y = kEditorPointerHotspotY;
    out->frame_index = frame_index;
    out->frame_count = static_cast<int>(frames.size());
    out->format = frame.format;
    return true;
}

bool LoadEditorPointerSprite(const std::string& path, EditorPointerSprite* out,
                             std::string* error) {
    if (out == nullptr) {
        if (error != nullptr) {
            *error = "no output";
        }
        return false;
    }
    *out = EditorPointerSprite{};
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        if (error != nullptr) {
            *error = "cannot open " + path;
        }
        return false;
    }
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());
    if (bytes.empty()) {
        if (error != nullptr) {
            *error = "empty file " + path;
        }
        return false;
    }
    return ParseEditorPointerAnm(bytes.data(), bytes.size(), out, error);
}

}  // namespace igi
