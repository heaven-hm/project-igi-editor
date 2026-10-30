#include "retail_pointer_sprite.h"

#include "../renderer/res_writer.h"
#include "../renderer/tex_writer.h"

#include <filesystem>

namespace igi {

RetailPointerImage LoadRetailPointerSprite(const std::string& igi_root) {
    RetailPointerImage image;
    if (igi_root.empty()) {
        image.reason = "no IGI root (IGI_GAME_PATH is unset)";
        return image;
    }

    const std::filesystem::path archive =
        std::filesystem::path(igi_root) / kRetailPointerArchive;
    std::error_code error;
    if (!std::filesystem::is_regular_file(archive, error)) {
        image.reason = "archive not found: " + archive.string();
        return image;
    }

    // The editor's own archive reader, over the engine's resource list.  The
    // lookup key is the NAME chunk text normalised the way `Resource_Create`
    // 0x4B59F0 does it (src/res/resource.c:65-104), which `RES_Extract` applies.
    const std::vector<uint8_t> bytes =
        RES_Extract(archive.string(), kRetailPointerResourceName);
    if (bytes.empty()) {
        image.reason = std::string("member not found: ") + kRetailPointerResourceName +
                       " in " + archive.string();
        return image;
    }

    // The same reader the editor uses for every other LOOP image, over the
    // member's bytes instead of a file - the engine has no file here either
    // (`Sprite_Load` HOOK 0x4B6720 reads through `Resource_Load`).
    const TEXFile tex =
        TEX_ParseBytes(bytes.data(), bytes.size(), kRetailPointerResourceName);
    if (!tex.valid || tex.images.empty()) {
        image.reason = std::string("could not parse ") + kRetailPointerResourceName +
                       ": " + (tex.error.empty() ? "no image" : tex.error);
        return image;
    }

    const TEXImage& frame = tex.images.front();
    if (frame.width == 0 || frame.height == 0 || frame.pixels.empty()) {
        image.reason = std::string("empty frame in ") + kRetailPointerResourceName;
        return image;
    }
    if (frame.pixels.size() <
        static_cast<std::size_t>(frame.width) * frame.height * 4u) {
        image.reason = std::string("short frame in ") + kRetailPointerResourceName;
        return image;
    }

    image.rgba = RetailPointerToRgba(frame.pixels.data(), frame.pixels.size());
    image.width = static_cast<int>(frame.width);
    image.height = static_cast<int>(frame.height);
    image.ok = true;
    return image;
}

}  // namespace igi
