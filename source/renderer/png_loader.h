#pragma once
#include "../pch.h"
#include <string>

// Loads a PNG file from disk and uploads it as an OpenGL RGBA texture.
// Returns 0 on failure (file missing / decode error), with `err` set.
GLuint LoadPngAsTexture(const std::string& pngPath, std::string& err);
