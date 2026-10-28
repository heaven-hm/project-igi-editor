/******************************************************************************
 * @file    source/menu/menu_ui_draw.cpp
 * @brief   OpenGL presentation for the in-app menu model.
 *
 * Draws with the primitives the pause menu already uses and the fonts it already
 * loads, so the menu keeps the look it has:
 *   source/runtime/pause_menu_font.*     the two bitmap fonts and their metrics
 *   source/renderer/renderer_draw.cpp    the panel/border/text idiom this follows
 *
 * ORACLE (IGI-RE) for the retail parts:
 *   src/widget/menuwindow.c:366+  MenuWindow_DrawEnd HOOK 0x424980 -- focused
 *                                 windows outline with r=b=0.5, g=1.0, unfocused
 *                                 with r=b=0, g=1.
 *   src/menu/typewriterbox.c:616-617  gMenuColors (DATA 0x53B778): the text
 *                                 colour table.  Its first five entries are
 *                                 (0,0,0), (128,128,128), (0,128,0), (128,95,0),
 *                                 (64,128,64); the registered names cover the first
 *                                 four (TEXT_BLACK..TEXT_ORANGE,
 *                                 src/menuscreen/menutext.c:301-304).
 *   src/menuscreen/menutext.c:362 a text whose colour index has bit 0x10 set is
 *                                 drawn dim (alpha halved).
 *   src/menuscreen/menuscreen.h:141-142  SCREEN_X / SCREEN_Y, applied by
 *                                 MenuUiModel::ToViewport.
 *
 * What is NOT reproduced here, on purpose, and why:
 *   * MenuFrame's eight sprite resources and the MenuScreen background picture
 *     (src/widget/menuframe.c:130-190).  The editor does not resolve retail
 *     sprites, so containers contribute layout only and the panel below is the
 *     editor's own chrome.
 *   * The colour table beyond the five entries whose values are verified above;
 *     an unverified index draws white rather than a guessed colour.
 *   * The bespoke per-row widgets of the editor's own pause menu (the font/level/
 *     autosave/log-level/terrain-intensity spinners and the stepper boxes).  Those
 *     stay in source/renderer/renderer_draw.cpp, which task-14 does not touch, and
 *     this layer draws their row label only.
 *****************************************************************************/

#include "pch.h"

#include "menu/menu_ui_draw.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "renderer/gl_helper.h"
#include "runtime/pause_menu_font.h"

namespace igi {
namespace {

// Font atlases.  Both are uploaded lazily and kept for the process lifetime; the
// retail one is re-uploaded when the archive behind it changes.
GLuint g_editor_font_tex = 0;
GLuint g_retail_font_tex = 0;
uint64_t g_retail_font_stamp = 0;
bool g_editor_font_uploaded = false;

float FontScale(bool use_editor_font, int system_font_size) {
    if (!use_editor_font) {
        return 1.0f;  // the retail font is drawn at its native size
    }
    return std::max(0.5f, static_cast<float>(system_font_size) / 12.0f);
}

GLuint UploadFont(const FntFont& font) {
    if (!font.valid || font.rgba.empty()) {
        return 0;
    }
    pic_s pic;
    pic.width_ = font.texWidth;
    pic.height_ = font.texHeight;
    pic.pixels_ = const_cast<uint8_t*>(font.rgba.data());
    // CLAMP + NEAREST keeps the small pixel font crisp, as the existing pause font
    // upload does (renderer_draw.cpp EnsureRetailPauseFontTexture).
    return GL_RegisterTexture(&pic, GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST, false);
}

GLuint AtlasFor(bool use_editor_font) {
    return use_editor_font ? g_editor_font_tex : g_retail_font_tex;
}

const FntFont* FontFor(bool use_editor_font) {
    return use_editor_font ? &GetEditorPauseFont() : &GetRetailPauseFont();
}

// One line of bitmap text in the editor's HUD ortho space (y = 0 at the bottom).
// `y_gl` is the text baseline, matching the convention of the pause menu's own
// text helpers (they lift the FNT line origin by 11 because the public HUD
// coordinate is a bitmap-font baseline).
void DrawText(int x, int y_gl, const std::string& text, float r, float g, float b,
              float alpha, float scale, bool use_editor_font) {
    const FntFont* font = FontFor(use_editor_font);
    const GLuint atlas = AtlasFor(use_editor_font);
    if (font == nullptr || !font->valid || atlas == 0 || text.empty()) {
        return;
    }

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, atlas);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, alpha);

    const float space = (font->defaultAdvance > 0 ? font->defaultAdvance : 4.0f) * scale;
    glBegin(GL_QUADS);
    float pen_x = static_cast<float>(x);
    float pen_y = static_cast<float>(y_gl) + 11.0f;
    for (char c : text) {
        if (c == '\n') {
            pen_x = static_cast<float>(x);
            pen_y -= font->lineHeight * scale;
            continue;
        }
        const auto it = font->glyphs.find(static_cast<unsigned char>(c));
        if (it == font->glyphs.end()) {
            pen_x += space;
            continue;
        }
        const FntGlyph& glyph = it->second;
        const float x0 = pen_x + glyph.offsetX * scale;
        const float x1 = x0 + glyph.width * scale;
        const float y0 = pen_y - glyph.offsetY * scale;
        const float y1 = y0 - glyph.height * scale;
        glTexCoord2f(glyph.u0, glyph.v0);
        glVertex2f(x0, y0);
        glTexCoord2f(glyph.u1, glyph.v0);
        glVertex2f(x1, y0);
        glTexCoord2f(glyph.u1, glyph.v1);
        glVertex2f(x1, y1);
        glTexCoord2f(glyph.u0, glyph.v1);
        glVertex2f(x0, y1);
        pen_x += glyph.advance * scale;
    }
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
}

int TextWidth(const std::string& text, bool use_editor_font, int system_font_size) {
    const float scale = FontScale(use_editor_font, system_font_size);
    return use_editor_font ? EditorPauseTextWidth(text.c_str(), scale)
                           : RetailPauseTextWidth(text.c_str(), scale);
}

// `gMenuColors`, src/menuscreen/menutext.c:616.  Only the five entries whose
// values are verified are mapped; anything else draws white instead of a guess.
struct MenuTextColour {
    float r;
    float g;
    float b;
    bool dim;
};

MenuTextColour ColourForIndex(int colour_index) {
    const bool dim = (colour_index & 0x10) != 0;  // menutext.c:362
    switch (colour_index & 0x0F) {
        case 0:  // TEXT_BLACK
            return {0.0f, 0.0f, 0.0f, dim};
        case 1:  // TEXT_WHITE
            return {128.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f, dim};
        case 2:  // TEXT_GREEN
            return {0.0f, 128.0f / 255.0f, 0.0f, dim};
        case 3:  // TEXT_ORANGE
            return {128.0f / 255.0f, 95.0f / 255.0f, 0.0f, dim};
        case 4:  // unnamed fifth entry
            return {64.0f / 255.0f, 128.0f / 255.0f, 64.0f / 255.0f, dim};
        default:
            return {1.0f, 1.0f, 1.0f, dim};
    }
}

void FillRect(const MenuUiRect& rect, float r, float g, float b, float a, int viewport_height) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(r, g, b, a);
    const int y0 = viewport_height - (rect.y + rect.h);
    glBegin(GL_QUADS);
    glVertex2i(rect.x, y0);
    glVertex2i(rect.x + rect.w, y0);
    glVertex2i(rect.x + rect.w, y0 + rect.h);
    glVertex2i(rect.x, y0 + rect.h);
    glEnd();
    glDisable(GL_BLEND);
}

void OutlineRect(const MenuUiRect& rect, float r, float g, float b, float width,
                 int viewport_height) {
    const int y0 = viewport_height - (rect.y + rect.h);
    glLineWidth(width);
    glColor3f(r, g, b);
    glBegin(GL_LINE_LOOP);
    glVertex2i(rect.x, y0);
    glVertex2i(rect.x + rect.w, y0);
    glVertex2i(rect.x + rect.w, y0 + rect.h);
    glVertex2i(rect.x, y0 + rect.h);
    glEnd();
    glLineWidth(1.0f);
}

// The editor's own pause panel: the deep emerald panel with a sharp green border
// that source/renderer/renderer_draw.cpp has always drawn.
void DrawPanel(const MenuUiRect& panel, int viewport_height) {
    FillRect(panel, 0.02f, 0.15f, 0.02f, 0.94f, viewport_height);
    OutlineRect(panel, 0.0f, 1.0f, 0.0f, 2.5f, viewport_height);
}

}  // namespace

MenuUiMetrics MenuUiMetricsForFonts(bool use_editor_font, int system_font_size) {
    MenuUiMetrics metrics;
    const float scale = FontScale(use_editor_font, system_font_size);
    metrics.text_width = [use_editor_font, scale](const std::string& text) {
        return use_editor_font ? EditorPauseTextWidth(text.c_str(), scale)
                               : RetailPauseTextWidth(text.c_str(), scale);
    };
    metrics.line_height = GetPauseMenuFontMetrics(use_editor_font, system_font_size).lineHeight;
    return metrics;
}

void MenuUiDrawPrepareFonts(bool use_editor_font) {
    if (use_editor_font) {
        if (!g_editor_font_uploaded) {
            const FntFont& font = GetEditorPauseFont();
            if (font.valid) {
                g_editor_font_tex = UploadFont(font);
                g_editor_font_uploaded = g_editor_font_tex != 0;
            }
        }
        return;
    }

    const FntFont& font = GetRetailPauseFont();
    if (!font.valid) {
        return;
    }
    const uint64_t stamp = RetailPauseFontSourceStamp();
    if (g_retail_font_tex != 0 && g_retail_font_stamp == stamp) {
        return;
    }
    const GLuint texture = UploadFont(font);
    if (texture != 0) {
        g_retail_font_tex = texture;
        g_retail_font_stamp = stamp;
    }
}

void DrawMenuUiScreen(const MenuUiModel& model, const MenuUiScreen& screen,
                      const MenuUiDrawStyle& style) {
    const int viewport_height = style.viewport_height;
    if (viewport_height <= 0) {
        return;
    }

    const MenuUiRect panel = model.ToViewport(screen.frame);
    DrawPanel(panel, viewport_height);

    const float scale = FontScale(style.use_editor_font, style.system_font_size);

    if (style.title != nullptr) {
        const int title_w = TextWidth(style.title, style.use_editor_font, style.system_font_size);
        DrawText(panel.x + (panel.w - title_w) / 2, viewport_height - (panel.y + 22),
                 style.title, 0.0f, 1.0f, 0.0f, 1.0f, scale, style.use_editor_font);
        if (style.subtitle != nullptr) {
            const int subtitle_w =
                TextWidth(style.subtitle, style.use_editor_font, style.system_font_size);
            DrawText(panel.x + (panel.w - subtitle_w) / 2, viewport_height - (panel.y + 46),
                     style.subtitle, 0.8f, 0.8f, 0.8f, 1.0f, scale, style.use_editor_font);
        }
        // Header separator, at the same offset the existing pause menu uses.
        const int separator_y = viewport_height - (panel.y + panel.h - 45);
        glColor3f(0.0f, 1.0f, 0.0f);
        glBegin(GL_LINES);
        glVertex2i(panel.x + 10, separator_y);
        glVertex2i(panel.x + panel.w - 10, separator_y);
        glEnd();
    }

    const int focused = model.FocusedEntryIndex();
    for (size_t i = 0; i < screen.entries.size(); ++i) {
        const MenuUiEntry& entry = screen.entries[i];
        if (!entry.visible || !entry.has_text) {
            continue;
        }
        if (entry.is_container && entry.label.empty()) {
            continue;  // retail draws container sprites; see the file comment
        }
        // The editor draws its OWN text for a retail row, never the game's:
        // `editor_label` is set by `app_menu::EditorLabelForRetailEntry`
        // (source/app_menu.cpp) on the live path.  There is deliberately NO
        // fallback to the model's own text here: `label` / `display_label` are the
        // GAME's words (the script key and the language archive's answer), and the
        // pause menu must show editor text only.  An entry with no editor label is
        // one the editor has nothing to draw for -- a container, or a row with no
        // text -- and it is skipped, exactly like the live draw layer
        // (source/renderer/renderer_draw.cpp, which draws `entry.editor_label` and
        // nothing else).
        const std::string& text = entry.editor_label;
        if (text.empty()) {
            continue;
        }

        const MenuUiRect rect = model.ToViewport(entry.rect);
        const bool is_focused = static_cast<int>(i) == focused;
        const bool is_fallback_row = entry.widget_type == "EditorRow";

        if (is_fallback_row) {
            // The editor's own rows: centred in the panel, green normally and
            // yellow when hovered, exactly like the existing pause menu.
            const int text_w = TextWidth(text, style.use_editor_font, style.system_font_size);
            const int text_x = panel.x + (panel.w - text_w) / 2;
            const int row_centre = rect.y + rect.h / 2;
            if (is_focused) {
                DrawText(text_x, viewport_height - row_centre, text, 1.0f, 1.0f,
                         0.0f, 1.0f, scale, style.use_editor_font);
            } else {
                DrawText(text_x, viewport_height - row_centre, text, 0.0f, 0.85f,
                         0.0f, 1.0f, scale, style.use_editor_font);
            }
            continue;
        }

        // Retail text: the model's rect is the text window, so the label starts at
        // its top-left, and the window draws its own focus outline.
        const MenuTextColour colour = ColourForIndex(entry.colour_index);
        // A dim text (colour index bit 0x10) is drawn at half alpha, menutext.c:362.
        DrawText(rect.x, viewport_height - (rect.y + rect.h), text, colour.r, colour.g,
                 colour.b, colour.dim ? 0.5f : 1.0f, scale, style.use_editor_font);

        if (is_focused) {
            // MenuWindow_DrawEnd HOOK 0x424980: focused -> r = b = 0.5, g = 1.0;
            // unfocused -> r = b = 0, g = 1.
            const MenuUiRect box{rect.x - 2, rect.y - 2, rect.w + 4, rect.h + 4};
            OutlineRect(box, 0.5f, 1.0f, 0.5f, 1.5f, viewport_height);
        } else if (entry.collected) {
            const MenuUiRect box{rect.x - 2, rect.y - 2, rect.w + 4, rect.h + 4};
            OutlineRect(box, 0.0f, 1.0f, 0.0f, 1.0f, viewport_height);
        }
    }
}

void DrawMenuUi(const MenuUiModel& model, const MenuUiDrawStyle& style) {
    const MenuUiScreen* screen = model.CurrentScreen();
    if (screen == nullptr) {
        return;
    }
    DrawMenuUiScreen(model, *screen, style);
}

}  // namespace igi
