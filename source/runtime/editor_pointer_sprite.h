#pragma once

/******************************************************************************
 * @file    source/runtime/editor_pointer_sprite.h
 * @brief   The EDITOR's own pointer: `editor/qed/pointer.anm`.
 *
 * The pause menu draws the editor's own cursor art, not the game's.  The file
 * QED's pointer is, `editor/qed/pointer.anm`, is a LOOP v9 sprite container --
 * the same container `.spr` uses, with the extension QED gave an animation --
 * and it carries FIVE frames of different sizes.  That is what this header
 * reads; the parse is pure (no file, no GL, no Logger), so the host lane can
 * assert it against the shipped bytes.
 *
 * WHAT THE CONTAINER IS (IGI-RE, the decompiled retail IGI.exe)
 *
 *   src/res/sprite.c:113-127   `LoopHeader`: magic(0) version(4) unk8(8) unkC(12)
 *                              unk10(H@16) unk12(H@18) unk14(20) palette(24)
 *                              charmap(28) nframes(I@32) unk24(36) w(I@40)
 *                              h(I@44) format(I@48) -- 0x34 bytes.
 *   src/res/sprite.c:129-146   `LoopFrame9`, one per frame, 0x20 bytes:
 *                              pixels(I@0) format(I@4) a(H@8) w(H@10) h(H@12)
 *                              advance(h@14) unk10[3](16..21) x(h@22) y(h@24)
 *                              pad(h@26) unk1C(I@28).
 *   src/res/sprite.c:246-260   `Sprite_Load` HOOK 0x4B6720, the v9 branch:
 *                              `LoopFrame9 *f = (LoopFrame9 *)(d + sizeof(LoopHeader));`
 *                              then, per frame,
 *                              `Sprite_LoadTexture(s, i, d + f->pixels, f->a,
 *                                                  f->w, f->h, f->format,
 *                                                  h->unk12, f->unk1C);`
 *                              and `Sprite_SetFrame(s, i, i, f->w, f->h, ...)`.
 *                              The per-frame record is therefore authoritative:
 *                              the header's `w`/`h`/`format` are the FIRST
 *                              frame's only in the files QED wrote, and a reader
 *                              that uses them for every frame (the editor's
 *                              shared v7/v9 reader does -- see below) cannot
 *                              read this file at all.
 *
 * THE PIXEL FORMATS (the engine's own switch, per frame)
 *
 *   src/sysmisc/d3dtex.c:411-432  `D3DTex_DownloadTexture` HOOK 0x50AD10 picks
 *                              the converter by the texture's format:
 *                                case 2 -> `Pixel_1555To32`
 *                                case 3, 4, 0x43 -> `Pixel_Copy32`
 *                                case 5 -> `Pixel_565To32`
 *                              (`case 1` is palettised, `default` is the
 *                              engine's fatal "Unsupported texture format").
 *   src/terrdraw/pixel.c:59-83    `Pixel_1555To32` HOOK 0x517C90:
 *                                B = (p & 0x1F) << 3, G = (p >> 2) & 0xF8,
 *                                R = (p >> 7) & 0xF8, A = (p & 0x8000) ? 0xFF : 0
 *   src/terrdraw/pixel.c:85-109   `Pixel_565To32` HOOK 0x517DD0:
 *                                B = (p & 0x1F) << 3, G = (p >> 3) & 0xFC,
 *                                R = (p >> 8) & 0xF8, A = 0xFF
 *   src/terrdraw/pixel.c:112-124  `Pixel_Copy32` HOOK 0x517F00: the four bytes
 *                                are copied verbatim.
 *
 *   Those converters write B,G,R,A in memory (D3D's ARGB8888 surface order);
 *   `pic_s` (source/common.h) and GL want R,G,B,A, so the decode below writes
 *   the engine's channel VALUES in R,G,B,A order.  This is the same swizzle the
 *   game's own `menusystem/pointer.spr` needed, and the same one the editor's
 *   `LoadOneSpr` applies to a format-3 sprite.
 *
 * WHAT THE SHIPPED FILE IS (assets/editor/qed/pointer.anm, md5
 * 5ae04c4b0c02f5da31e7b633d543db27, 10556 bytes; byte-identical to
 * <IGI>/editor/qed/pointer.anm):
 *
 *   header   LOOP v9, nframes 5, header w 64 h 64 format 2, charmap 10452
 *   frame 0  pixels 212,  format 2, stride 128, 64 x 64  -- the arrow cursor
 *   frame 1  pixels 8404, format 2, stride 32,  16 x 16
 *   frame 2  pixels 8916, format 2, stride 32,  16 x 16
 *   frame 3  pixels 9428, format 2, stride 32,  16 x 16
 *   frame 4  pixels 9940, format 2, stride 32,  16 x 16
 *
 *   Frame 0 is the pointer: an arrow whose tip is its first opaque pixel, at
 *   (0, 0), with 125 opaque pixels inside a bounding box of (0,0)-(11,20).
 *   Frames 1..4 are the other pointer shapes the .anm carries (16 x 16 hand /
 *   resize / move cursors); the pause menu draws frame 0.
 *
 * THE HOTSPOT
 *
 *   There is no oracle for QED's own hotspot: QED's binary is not part of
 *   IGI-RE (that is IGI.exe) and is not in the install, and `editor/qed/QED.doc`
 *   documents the mouse buttons, the camera control and the text dialog but says
 *   nothing about the pointer's art or its anchor.  What fixes the anchor here is
 *   the editor's own draw convention plus the art: `App::DrawCustomCursor`
 *   (source/app_ui.cpp) blits a cursor sprite with the BITMAP'S TOP-LEFT AT THE
 *   POINTER -- every one of the editor's tool cursors is drawn that way -- and
 *   frame 0's arrow tip is its first opaque pixel at (0, 0), so (0, 0) is the
 *   hotspot that puts the tip exactly on the pointer.
 *****************************************************************************/

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace igi {

// The file, in the editor's own folder -- the same folder the editor's tool
// cursors (`editor/qed/*.spr`) are read from (`App::LoadAllCursors`).
inline constexpr const char* kEditorPointerFileName = "pointer.anm";
inline constexpr const char* kEditorPointerFolder = "editor/qed";

// The frame the pointer is: the arrow.  Frames 1..4 are the .anm's other shapes.
inline constexpr int kEditorPointerFrame = 0;

// The editor's own convention (see the header comment): the bitmap's top-left is
// the pointer, and this frame's arrow tip is at (0, 0).
inline constexpr int kEditorPointerHotspotX = 0;
inline constexpr int kEditorPointerHotspotY = 0;

// `LoopHeader` (src/res/sprite.c:113-127).
inline constexpr std::size_t kLoopHeaderSize = 0x34;
// `LoopFrame9` (src/res/sprite.c:129-146).
inline constexpr std::size_t kLoopFrame9Size = 0x20;
inline constexpr uint32_t kLoopVersion9 = 9;

// The engine's texture formats (src/sysmisc/d3dtex.c:411-432).
inline constexpr uint32_t kFormat1555 = 2;
inline constexpr uint32_t kFormat8888 = 3;
inline constexpr uint32_t kFormat8888Alt = 4;
inline constexpr uint32_t kFormat8888B = 0x43;
inline constexpr uint32_t kFormat565 = 5;

// One `LoopFrame9` record, as the engine reads it.
struct EditorPointerFrame {
    uint32_t offset = 0;  // `pixels`: byte offset of this frame's bitmap
    uint32_t format = 0;  // `format`
    uint32_t stride = 0;  // `a`: bytes per row
    int width = 0;
    int height = 0;
};

// The decoded frame, ready to upload: RGBA8, row-major, `width * height * 4`.
struct EditorPointerSprite {
    bool ok = false;
    std::string error;
    int width = 0;
    int height = 0;
    int hotspot_x = 0;
    int hotspot_y = 0;
    int frame_index = 0;
    int frame_count = 0;
    uint32_t format = 0;
    std::vector<uint8_t> rgba;
};

// Parse a LOOP v9 container and decode frame `frame_index` (default: the arrow).
// `bytes` is the whole file.  Returns false and sets `error` on anything the
// engine would refuse (bad magic, a version that is not 9, a frame whose record
// or bitmap runs past the end, a format with no converter, a zero stride).
bool ParseEditorPointerAnm(const uint8_t* bytes, std::size_t size,
                           EditorPointerSprite* out, std::string* error,
                           int frame_index = kEditorPointerFrame);

// The frame records of a LOOP v9 container, in file order.  Exposed so a test can
// pin the shipped file's own geometry without decoding pixels.
bool ReadLoopV9Frames(const uint8_t* bytes, std::size_t size,
                      std::vector<EditorPointerFrame>* frames, std::string* error);

// Read the file and decode it.  `path` is the .anm itself.
bool LoadEditorPointerSprite(const std::string& path, EditorPointerSprite* out,
                             std::string* error);

}  // namespace igi
