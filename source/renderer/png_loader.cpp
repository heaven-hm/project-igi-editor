#include "png_loader.h"
#include "../pch.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

GLuint LoadPngAsTexture(const std::string& pngPath, std::string& err) {
    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load(pngPath.c_str(), &w, &h, &channels, 4);
    if (!data) {
        err = "failed to decode PNG: " + pngPath;
        return 0;
    }
    pic_s pic{ w, h, data };
    GLuint texture = GL_RegisterTexture(&pic, GL_CLAMP_TO_EDGE, GL_LINEAR, GL_LINEAR, false);
    stbi_image_free(data);
    return texture;
}
