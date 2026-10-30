/******************************************************************************
 * @file    tests/test_editor_pointer.cpp
 * @brief   The EDITOR's own pointer (`editor/qed/pointer.anm`), byte for byte.
 *
 * The pause menu draws the editor's cursor art, not the game's
 * (`menusystem/pointer.spr`).  This suite is the host-portable proof of what that
 * file IS, so the drawn result can be checked without a GL context:
 *
 *   * the container: LOOP v9, five frames, each with its OWN size, format and
 *     bitmap offset -- `LoopHeader` / `LoopFrame9` and the v9 branch of
 *     `Sprite_Load` HOOK 0x4B6720 (src/res/sprite.c:113-146, :246-260);
 *   * the decode: the engine's per-format converters, selected by
 *     `D3DTex_DownloadTexture` HOOK 0x50AD10 (src/sysmisc/d3dtex.c:411-432):
 *     format 2 -> `Pixel_1555To32` HOOK 0x517C90 (src/terrdraw/pixel.c:59-83),
 *     format 3/4/0x43 -> `Pixel_Copy32` HOOK 0x517F00 (:112-124),
 *     format 5 -> `Pixel_565To32` HOOK 0x517DD0 (:85-109);
 *   * the hotspot: frame 0's arrow tip is its first opaque pixel, at (0, 0), and
 *     the editor blits a cursor sprite with the bitmap's top-left at the pointer
 *     (`App::DrawCustomCursor`, source/app_ui.cpp), so (0, 0) puts the tip on the
 *     pointer.  QED's own binary is not part of IGI-RE and not in the install, and
 *     `editor/qed/QED.doc` documents the mouse but not the pointer's art or
 *     anchor, so this is an inference from the art plus the editor's own
 *     convention -- recorded as such in source/runtime/editor_pointer_sprite.h.
 *
 * Corpus: $IGI_GAME_PATH (scripts/run-host-tests.sh exports it, default
 * /Users/haseeb-mir/Downloads/IGI), falling back to this repository's own
 * `assets/editor/qed/pointer.anm` -- the two files are byte-identical (md5
 * 5ae04c4b0c02f5da31e7b633d543db27).  The suite skips when neither is present;
 * the refusal tests always run.
 *****************************************************************************/

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "runtime/editor_pointer_sprite.h"

namespace {

std::vector<std::filesystem::path> PointerCandidates() {
    std::vector<std::filesystem::path> candidates;
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0') {
        candidates.emplace_back(std::filesystem::path(env) / "editor" / "qed" /
                                igi::kEditorPointerFileName);
    }
    // The host lane and the Windows suite both run from the repository root.
    candidates.emplace_back(std::filesystem::path(igi::kEditorPointerFolder) /
                            igi::kEditorPointerFileName);
    candidates.emplace_back(std::filesystem::path("..") / igi::kEditorPointerFolder /
                            igi::kEditorPointerFileName);
    return candidates;
}

std::filesystem::path PointerPath() {
    std::error_code error;
    for (const std::filesystem::path& candidate : PointerCandidates()) {
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
    }
    return {};
}

std::vector<uint8_t> ReadAll(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
}

const uint8_t* PixelAt(const igi::EditorPointerSprite& sprite, int x, int y) {
    return sprite.rgba.data() +
           (static_cast<std::size_t>(y) * static_cast<std::size_t>(sprite.width) +
            static_cast<std::size_t>(x)) * 4u;
}

// A LOOP v9 container built in memory, so the refusal paths can be exercised
// without touching the shipped file.
std::vector<uint8_t> MakeLoopV9(uint32_t version, uint32_t frame_count,
                                uint32_t format, uint32_t pixels, uint16_t stride,
                                uint16_t width, uint16_t height,
                                std::size_t payload) {
    std::vector<uint8_t> bytes(igi::kLoopHeaderSize +
                                   static_cast<std::size_t>(frame_count) * igi::kLoopFrame9Size +
                                   payload,
                               0);
    bytes[0] = 'L'; bytes[1] = 'O'; bytes[2] = 'O'; bytes[3] = 'P';
    auto put32 = [&bytes](std::size_t offset, uint32_t value) {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xFF);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
        bytes[offset + 2] = static_cast<uint8_t>((value >> 16) & 0xFF);
        bytes[offset + 3] = static_cast<uint8_t>((value >> 24) & 0xFF);
    };
    auto put16 = [&bytes](std::size_t offset, uint16_t value) {
        bytes[offset + 0] = static_cast<uint8_t>(value & 0xFF);
        bytes[offset + 1] = static_cast<uint8_t>((value >> 8) & 0xFF);
    };
    put32(4, version);
    put32(32, frame_count);   // LoopHeader::nframes
    put32(40, width);
    put32(44, height);
    put32(48, format);
    for (uint32_t i = 0; i < frame_count; ++i) {
        const std::size_t record = igi::kLoopHeaderSize +
                                   static_cast<std::size_t>(i) * igi::kLoopFrame9Size;
        put32(record + 0, pixels);   // LoopFrame9::pixels
        put32(record + 4, format);   // LoopFrame9::format
        put16(record + 8, stride);   // LoopFrame9::a
        put16(record + 10, width);
        put16(record + 12, height);
    }
    return bytes;
}

}  // namespace

// ─── the shipped container ───────────────────────────────────────────────────

TEST(EditorPointer, ShippedFileIsALoopV9AnimationOfFiveFrames) {
    const std::filesystem::path path = PointerPath();
    if (path.empty()) {
        GTEST_SKIP() << "no editor/qed/pointer.anm corpus";
    }
    const std::vector<uint8_t> bytes = ReadAll(path);
    ASSERT_FALSE(bytes.empty());
    ASSERT_GE(bytes.size(), igi::kLoopHeaderSize);
    EXPECT_EQ(bytes[0], 'L');
    EXPECT_EQ(bytes[1], 'O');
    EXPECT_EQ(bytes[2], 'O');
    EXPECT_EQ(bytes[3], 'P');

    std::vector<igi::EditorPointerFrame> frames;
    std::string error;
    ASSERT_TRUE(igi::ReadLoopV9Frames(bytes.data(), bytes.size(), &frames, &error)) << error;
    ASSERT_EQ(frames.size(), 5u);

    // Every frame carries its own record -- the reader must NOT take the
    // header's w/h/format for all of them (see the uniform-reader test below).
    const struct { uint32_t offset, format, stride; int width, height; } expected[5] = {
        {212, 2, 128, 64, 64},
        {8404, 2, 32, 16, 16},
        {8916, 2, 32, 16, 16},
        {9428, 2, 32, 16, 16},
        {9940, 2, 32, 16, 16},
    };
    for (std::size_t i = 0; i < frames.size(); ++i) {
        EXPECT_EQ(frames[i].offset, expected[i].offset) << "frame " << i;
        EXPECT_EQ(frames[i].format, expected[i].format) << "frame " << i;
        EXPECT_EQ(frames[i].stride, expected[i].stride) << "frame " << i;
        EXPECT_EQ(frames[i].width, expected[i].width) << "frame " << i;
        EXPECT_EQ(frames[i].height, expected[i].height) << "frame " << i;
        // The last frame's bitmap ends exactly at the end of the file.
        const std::size_t depth = frames[i].format == 2 || frames[i].format == 5 ? 2u : 4u;
        const std::size_t end = frames[i].offset +
                                static_cast<std::size_t>(frames[i].height - 1) * frames[i].stride +
                                static_cast<std::size_t>(frames[i].width) * depth;
        if (i + 1 < frames.size()) {
            EXPECT_EQ(end, frames[i + 1].offset) << "frame " << i << " is contiguous";
        } else {
            EXPECT_EQ(end, bytes.size()) << "the last frame ends at end of file";
        }
    }
}

TEST(EditorPointer, FrameZeroIsTheArrowAndItsTipIsTheHotspot) {
    const std::filesystem::path path = PointerPath();
    if (path.empty()) {
        GTEST_SKIP() << "no editor/qed/pointer.anm corpus";
    }
    igi::EditorPointerSprite sprite;
    std::string error;
    ASSERT_TRUE(igi::LoadEditorPointerSprite(path.string(), &sprite, &error)) << error;
    EXPECT_EQ(sprite.width, 64);
    EXPECT_EQ(sprite.height, 64);
    EXPECT_EQ(sprite.frame_count, 5);
    EXPECT_EQ(sprite.frame_index, igi::kEditorPointerFrame);
    EXPECT_EQ(sprite.rgba.size(), 64u * 64u * 4u);

    int opaque = 0;
    int tip_x = -1;
    int tip_y = -1;
    int min_x = sprite.width;
    int min_y = sprite.height;
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < sprite.height; ++y) {
        for (int x = 0; x < sprite.width; ++x) {
            if (PixelAt(sprite, x, y)[3] == 0) {
                continue;
            }
            ++opaque;
            if (tip_x < 0) {
                tip_x = x;
                tip_y = y;
            }
            min_x = (std::min)(min_x, x);
            min_y = (std::min)(min_y, y);
            max_x = (std::max)(max_x, x);
            max_y = (std::max)(max_y, y);
        }
    }
    EXPECT_EQ(opaque, 125) << "the arrow's own pixel count";
    EXPECT_EQ(tip_x, igi::kEditorPointerHotspotX);
    EXPECT_EQ(tip_y, igi::kEditorPointerHotspotY);
    EXPECT_EQ(sprite.hotspot_x, igi::kEditorPointerHotspotX);
    EXPECT_EQ(sprite.hotspot_y, igi::kEditorPointerHotspotY);
    EXPECT_EQ(min_x, 0);
    EXPECT_EQ(min_y, 0);
    EXPECT_EQ(max_x, 11);
    EXPECT_EQ(max_y, 20);
    // Everything outside the arrow is fully transparent, so the editor can blit
    // the whole 64x64 frame with its top-left on the pointer.
    EXPECT_EQ(PixelAt(sprite, 63, 63)[3], 0);
    EXPECT_EQ(PixelAt(sprite, 20, 5)[3], 0);
}

TEST(EditorPointer, TheDecodeIsTheEngines1555Converter) {
    const std::filesystem::path path = PointerPath();
    if (path.empty()) {
        GTEST_SKIP() << "no editor/qed/pointer.anm corpus";
    }
    igi::EditorPointerSprite sprite;
    std::string error;
    ASSERT_TRUE(igi::LoadEditorPointerSprite(path.string(), &sprite, &error)) << error;
    EXPECT_EQ(sprite.format, igi::kFormat1555);

    // The tip pixel is the arrow's black outline, fully opaque (bit 15 set).
    const uint8_t* tip = PixelAt(sprite, 0, 0);
    EXPECT_EQ(tip[0], 0);
    EXPECT_EQ(tip[1], 0);
    EXPECT_EQ(tip[2], 0);
    EXPECT_EQ(tip[3], 255);
    // The arrow's fill is 0x7FFF through `Pixel_1555To32`: the five-bit channels
    // are shifted into the top of each byte, so the result is 248 -- 255 would
    // mean the 8-bit path, and a green of 252 would mean the 565 one.
    const uint8_t* fill = PixelAt(sprite, 2, 5);
    EXPECT_EQ(fill[0], 248);
    EXPECT_EQ(fill[1], 248);
    EXPECT_EQ(fill[2], 248);
    EXPECT_EQ(fill[3], 255);
}

TEST(EditorPointer, FrameSelectionDefaultsToTheArrow) {
    const std::filesystem::path path = PointerPath();
    if (path.empty()) {
        GTEST_SKIP() << "no editor/qed/pointer.anm corpus";
    }
    const std::vector<uint8_t> bytes = ReadAll(path);
    igi::EditorPointerSprite frame_one;
    std::string error;
    ASSERT_TRUE(igi::ParseEditorPointerAnm(bytes.data(), bytes.size(), &frame_one, &error, 1))
        << error;
    EXPECT_EQ(frame_one.width, 16);
    EXPECT_EQ(frame_one.height, 16);
    EXPECT_EQ(frame_one.rgba.size(), 16u * 16u * 4u);

    igi::EditorPointerSprite frame_four;
    ASSERT_TRUE(igi::ParseEditorPointerAnm(bytes.data(), bytes.size(), &frame_four, &error, 4))
        << error;
    EXPECT_EQ(frame_four.width, 16);
    EXPECT_FALSE(igi::ParseEditorPointerAnm(bytes.data(), bytes.size(), &frame_four, &error, 5));
    EXPECT_FALSE(error.empty());
}

TEST(EditorPointer, TheSharedUniformFrameReaderCannotReadThisFile) {
    const std::filesystem::path path = PointerPath();
    if (path.empty()) {
        GTEST_SKIP() << "no editor/qed/pointer.anm corpus";
    }
    const std::vector<uint8_t> bytes = ReadAll(path);
    ASSERT_GE(bytes.size(), igi::kLoopHeaderSize);

    // Why this reader exists: the editor's shared v7/v9 reader
    // (source/renderer/tex_parser.cpp at HEAD, `ParseV7or9`) sizes EVERY frame
    // from the header's w/h/format and skips a fixed 32-byte record per frame.
    // For this file that means five 64x64 format-2 bitmaps from offset
    // 0x34 + 5 * 0x20 = 212, i.e. 40960 bytes -- far past the file's end.  The
    // arithmetic is reproduced here from the file's own header so the reason is
    // pinned by a test rather than by a comment.
    auto u32 = [&bytes](std::size_t offset) {
        return static_cast<uint32_t>(bytes[offset]) |
               (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
               (static_cast<uint32_t>(bytes[offset + 3]) << 24);
    };
    const uint32_t count = u32(32);
    const uint32_t header_width = u32(40);
    const uint32_t header_height = u32(44);
    const uint32_t header_format = u32(48);
    ASSERT_EQ(count, 5u);
    ASSERT_EQ(header_width, 64u);
    ASSERT_EQ(header_height, 64u);
    ASSERT_EQ(header_format, igi::kFormat1555);

    const std::size_t uniform_frame_bytes =
        static_cast<std::size_t>(header_width) * 2u * header_height;
    const std::size_t uniform_start =
        igi::kLoopHeaderSize + static_cast<std::size_t>(count) * igi::kLoopFrame9Size;
    EXPECT_EQ(uniform_start, 212u);
    EXPECT_EQ(uniform_frame_bytes, 8192u);
    EXPECT_GT(uniform_start + static_cast<std::size_t>(count) * uniform_frame_bytes,
              bytes.size())
        << "the uniform reader must run off the end: this file needs the per-frame "
           "records (LoopFrame9) the shared reader ignores";
}

// ─── refusals, all of them the engine's own ──────────────────────────────────

TEST(EditorPointer, RefusesWhatTheEngineWouldRefuse) {
    igi::EditorPointerSprite sprite;
    std::string error;

    // Not a LOOP container: `Loop_Is` (src/res/sprite.c:148-151).
    std::vector<uint8_t> not_loop = MakeLoopV9(9, 1, 2, 84, 32, 16, 16, 512);
    not_loop[1] = 'X';
    EXPECT_FALSE(igi::ParseEditorPointerAnm(not_loop.data(), not_loop.size(), &sprite, &error));
    EXPECT_NE(error.find("LOOP"), std::string::npos);

    // Version 7 is a different record layout, so a v9 reader must refuse it.
    std::vector<uint8_t> v7 = MakeLoopV9(7, 1, 2, 84, 32, 16, 16, 512);
    EXPECT_FALSE(igi::ParseEditorPointerAnm(v7.data(), v7.size(), &sprite, &error));
    EXPECT_NE(error.find("version"), std::string::npos);

    // A frame table that does not fit.
    std::vector<uint8_t> short_table = MakeLoopV9(9, 4, 2, 0, 32, 16, 16, 0);
    short_table.resize(igi::kLoopHeaderSize + igi::kLoopFrame9Size);
    EXPECT_FALSE(igi::ReadLoopV9Frames(short_table.data(), short_table.size(), nullptr, &error));

    // A bitmap that runs past the end: the record is honest but the file is cut.
    std::vector<uint8_t> cut = MakeLoopV9(9, 1, 2, 84, 32, 16, 16, 512);
    cut.resize(84 + 512 - 1);
    EXPECT_FALSE(igi::ParseEditorPointerAnm(cut.data(), cut.size(), &sprite, &error));
    EXPECT_NE(error.find("past the end"), std::string::npos);

    // A format the engine has no converter for: `D3DTex_DownloadTexture`'s
    // default branch is the fatal "Unsupported texture format"
    // (src/sysmisc/d3dtex.c:430-432).
    std::vector<uint8_t> bad_format = MakeLoopV9(9, 1, 77, 84, 32, 16, 16, 512);
    EXPECT_FALSE(igi::ParseEditorPointerAnm(bad_format.data(), bad_format.size(), &sprite, &error));
    EXPECT_NE(error.find("Unsupported texture format"), std::string::npos);

    // A stride narrower than a row would decode a shifted image.
    std::vector<uint8_t> bad_stride = MakeLoopV9(9, 1, 2, 84, 8, 16, 16, 512);
    EXPECT_FALSE(igi::ParseEditorPointerAnm(bad_stride.data(), bad_stride.size(), &sprite, &error));
    EXPECT_NE(error.find("stride"), std::string::npos);

    // A zero-frame container.
    std::vector<uint8_t> no_frames = MakeLoopV9(9, 0, 2, 0, 0, 0, 0, 0);
    EXPECT_FALSE(igi::ReadLoopV9Frames(no_frames.data(), no_frames.size(), nullptr, &error));
    EXPECT_NE(error.find("0 frames"), std::string::npos);

    // Nothing at all.
    EXPECT_FALSE(igi::ParseEditorPointerAnm(nullptr, 0, &sprite, &error));
}

TEST(EditorPointer, TheLoaderReportsAMissingFile) {
    igi::EditorPointerSprite sprite;
    std::string error;
    EXPECT_FALSE(igi::LoadEditorPointerSprite("no/such/pointer.anm", &sprite, &error));
    EXPECT_NE(error.find("no/such/pointer.anm"), std::string::npos);
    EXPECT_FALSE(sprite.ok);
}

TEST(EditorPointer, TheFormatTableIsTheEnginesOwn) {
    // src/sysmisc/d3dtex.c:411-432 -> the converter per format.
    EXPECT_EQ(igi::kFormat1555, 2u);      // Pixel_1555To32 HOOK 0x517C90
    EXPECT_EQ(igi::kFormat565, 5u);       // Pixel_565To32  HOOK 0x517DD0
    EXPECT_EQ(igi::kFormat8888, 3u);      // Pixel_Copy32   HOOK 0x517F00
    EXPECT_EQ(igi::kFormat8888Alt, 4u);
    EXPECT_EQ(igi::kFormat8888B, 0x43u);
    // The editor's tool cursors are format 3, so the reader must take 8888 too.
    std::vector<uint8_t> argb = MakeLoopV9(9, 1, 3, 84, 8, 2, 1, 8);
    // B,G,R,A in the file -> R,G,B,A out.
    argb[84 + 0] = 10; argb[84 + 1] = 20; argb[84 + 2] = 30; argb[84 + 3] = 40;
    argb[84 + 4] = 50; argb[84 + 5] = 60; argb[84 + 6] = 70; argb[84 + 7] = 80;
    igi::EditorPointerSprite sprite;
    std::string error;
    ASSERT_TRUE(igi::ParseEditorPointerAnm(argb.data(), argb.size(), &sprite, &error)) << error;
    ASSERT_EQ(sprite.rgba.size(), 8u);
    EXPECT_EQ(sprite.rgba[0], 30);
    EXPECT_EQ(sprite.rgba[1], 20);
    EXPECT_EQ(sprite.rgba[2], 10);
    EXPECT_EQ(sprite.rgba[3], 40);
    EXPECT_EQ(sprite.rgba[4], 70);
    EXPECT_EQ(sprite.rgba[5], 60);
    EXPECT_EQ(sprite.rgba[6], 50);
    EXPECT_EQ(sprite.rgba[7], 80);
}
