#include "pause_menu_font.h"

#include "pause_menu_state.h"
#include "../renderer/res_writer.h"
#include "../utils.h"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <string>
#include <algorithm>
#include <cmath>

namespace {

uint64_t SourceStamp(const std::filesystem::path& archive) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(archive, error)) return 0;
    const auto modified = std::filesystem::last_write_time(archive, error);
    if (error) return 0;
    const auto size = std::filesystem::file_size(archive, error);
    if (error) return 0;
    return static_cast<uint64_t>(modified.time_since_epoch().count()) ^
           (static_cast<uint64_t>(size) << 1);
}

} // namespace

FntFont& GetRetailPauseFont() {
    static FntFont font;
    static igi::PauseMenuFontLoadState state;

    const std::filesystem::path archive =
        std::filesystem::path(Utils::GetIGIRootPath()) / "MENUSYSTEM" / "ingamemenu.res";
    const uint64_t stamp = SourceStamp(archive);
    if (!state.ShouldAttempt(stamp)) return font;

    font = {};
    bool success = false;
    if (stamp != 0) {
        const auto bytes = RES_Extract(archive.string(), "LOCAL:menusystem/font3.fnt");
        if (!bytes.empty()) {
            const auto staged = std::filesystem::temp_directory_path() /
                "igi1ed-ingamemenu-font3.fnt";
            std::ofstream output(staged, std::ios::binary | std::ios::trunc);
            if (output.is_open()) {
                output.write(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
                output.close();
                if (output) {
                    font = FNT_Parse(staged.string());
                    success = font.valid;
                }
            }
            std::error_code error;
            std::filesystem::remove(staged, error);
        }
    }
    state.RecordAttempt(stamp, success);
    return font;
}

uint64_t RetailPauseFontSourceStamp() {
    return SourceStamp(std::filesystem::path(Utils::GetIGIRootPath()) /
                       "MENUSYSTEM" / "ingamemenu.res");
}

FntFont& GetEditorPauseFont() {
    static FntFont font;
    static igi::PauseMenuFontLoadState state;
    const std::filesystem::path path =
        std::filesystem::path(Utils::GetExeDirectory()) / "editor" / "qed" / "editor.fnt";
    const uint64_t stamp = SourceStamp(path);
    if (!state.ShouldAttempt(stamp)) return font;
    font = {};
    bool success = false;
    if (stamp != 0) {
        font = FNT_Parse(path.string());
        success = font.valid;
    }
    state.RecordAttempt(stamp, success);
    return font;
}

static int FontTextWidth(const FntFont& font, const char* text, float scale) {
    if (!text) return 0;
    if (!font.valid) return static_cast<int>(std::strlen(text) * scale * 8.0f);
    const float fallback = font.defaultAdvance > 0 ?
        static_cast<float>(font.defaultAdvance) : 4.0f;
    float width = 0.0f;
    for (const char* cursor = text; *cursor; ++cursor) {
        const auto iterator = font.glyphs.find(static_cast<unsigned char>(*cursor));
        width += (iterator == font.glyphs.end() ? fallback :
                  static_cast<float>(iterator->second.advance)) * scale;
    }
    return static_cast<int>(width);
}

int RetailPauseTextWidth(const char* text, float scale) {
    return FontTextWidth(GetRetailPauseFont(), text, scale);
}

int EditorPauseTextWidth(const char* text, float scale) {
    return FontTextWidth(GetEditorPauseFont(), text, scale);
}

int PauseMenuTextWidth(const char* text, bool useEditorFont,
                       int systemFontSize, float layoutScale) {
    const float scale = layoutScale *
        (static_cast<float>((std::max)(1, systemFontSize)) / 12.0f);
    const FntFont& font = useEditorFont ? GetEditorPauseFont()
                                        : GetRetailPauseFont();
    if (font.valid) {
        return FontTextWidth(font, text, scale);
    }
    const int fallbackAdvance = systemFontSize <= 11 ? 6
        : systemFontSize >= 15 ? 9 : 7;
    return text ? static_cast<int>(std::strlen(text) * fallbackAdvance * layoutScale) : 0;
}

PauseMenuFontMetrics GetPauseMenuFontMetrics(bool useEditorFont,
                                             int systemFontSize) {
    const int size = (std::max)(1, systemFontSize);
    const float scale = static_cast<float>(size) / 12.0f;
    const FntFont& font = useEditorFont ? GetEditorPauseFont()
                                       : GetRetailPauseFont();
    if (font.valid && font.lineHeight > 0) {
        const int lineHeight = (std::max)(1, static_cast<int>(std::ceil(
            static_cast<float>(font.lineHeight) * scale)));
        return {
            lineHeight,
            (std::max)(16, lineHeight + 2),
            (std::max)(18, lineHeight + 4),
            (std::max)(1, static_cast<int>(std::ceil(
                static_cast<float>(font.defaultAdvance > 0 ? font.defaultAdvance : 4) * scale))),
            scale,
            true,
        };
    }

    const int lineHeight = size <= 11 ? 13 : size >= 15 ? 22 : 15;
    const int charWidth = size <= 11 ? 6 : size >= 15 ? 9 : 7;
    return {lineHeight, lineHeight + 2, lineHeight + 4, charWidth, 1.0f, false};
}
