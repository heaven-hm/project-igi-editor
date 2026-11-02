/******************************************************************************
 * @file    source/renderer/tex_parser.h
 * @brief   LOOP texture/sprite parser core — no logging, no files.
 *
 * `TEX_Parse` / `TEX_ParseBytes` (source/renderer/tex_writer.h) are the editor's
 * entry points; they add the log lines.  This header is the half that links
 * without `Logger` (whose `Log` reads `Config::Get()`, and `source/config.cpp`
 * is not host-portable), which is what lets the host test lane parse a retail
 * archive member — see tests/test_pause_menu_input.cpp.
 *
 * The engine oracle lives with the code that uses it:
 *   Texture_LoadFile 0x4B24C0  src/file/texture.c:231-252   (v2, v11)
 *   Sprite_Load      0x4B6720  src/res/sprite.c:162-207     (v7, v9)
 *****************************************************************************/

#pragma once

#include "tex_writer.h"

#include <cstdint>
#include <istream>

namespace igi_tex {

// Bytes per pixel of the engine's level buffer, per the format switch in
// `D3DTex_DownloadTexture` 0x50AD10 (src/sysmisc/d3dtex.c:411-432).  0 means the
// engine has no conversion for that format and reports "Unsupported texture
// format".
uint32_t DepthForMode(uint32_t mode);

// Parse a LOOP stream into `out`.  Returns `out.valid`; `out.error` carries the
// reason when it returns false.  Sets `out.version` to the version word the
// engine accepted (2, 7, 9 or 11).
bool TEX_ParseStreamCore(std::istream& f, TEXFile& out);

}  // namespace igi_tex
