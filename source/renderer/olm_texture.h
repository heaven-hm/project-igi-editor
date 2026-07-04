#pragma once
#include "../pch.h"
#include <string>

// Reads an IGI1 .olm lightmap straight from disk and uploads it as an RGBA GL
// texture, returning the texture id (0 on failure, with `err` set).
//
// This is the fast load path for applying baked lightmaps: it avoids spawning
// `igi1conv olm to-png` once per .olm (which was hundreds of subprocesses per
// building). The igi1conv converter remains the authoritative tool for WRITING
// / recalculating .olm files; this only decodes the documented IGI1 layout
// (88-byte header + 16-byte layer descriptor + RGBA pixels, with the same R/B
// channel swap igi1conv applies on export) for GPU upload.
GLuint LoadOlmAsTexture(const std::string& olmPath, std::string& err);
