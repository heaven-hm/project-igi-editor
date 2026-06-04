/******************************************************************************
 * @file    renderer.cpp
 * @brief   main renderer
 *   GL 4.1: need manually set binding point of uniform blocks
 *                             texture unit of sample object
 *   GL 4.5: binding point, texture unit can specified in shaders
 *****************************************************************************/

#include "config.h"
#include "logger.h"
#include "utils.h"
#include "pch.h"
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <cstdio>

#include <freeglut.h>
#include "../level/task_schema.h"
#include "../parsers/fnt_parser.h"
using namespace TaskSchemaNS;

/*
================================================================================
 Editor bitmap font (content/qed/editor.fnt) for HUD text
================================================================================
*/
static FntFont g_editorFont;
static GLuint  g_editorFontTex = 0;
static bool    g_editorFontTried = false;

static FntFont g_editorSmFont;
static GLuint  g_editorSmFontTex = 0;
static bool    g_editorSmFontTried = false;

// Lazily load + upload the editor font atlas on first HUD draw.
static void EnsureEditorFont() {
  if (g_editorFontTried) {
    return;
  }
  g_editorFontTried = true;

  g_editorFont = FNT_Parse("content\\qed\\editor.fnt");
  if (!g_editorFont.valid) {
    return;
  }

  pic_s pic;
  pic.width_  = g_editorFont.texWidth;
  pic.height_ = g_editorFont.texHeight;
  pic.pixels_ = g_editorFont.rgba.data();
  // CLAMP + NEAREST so the small pixel font stays crisp (no blur/scaling).
  g_editorFontTex = GL_RegisterTexture(&pic, GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST, false);
}

// Lazily load + upload the small editor font atlas (editorsm.fnt) for tooltips.
static void EnsureEditorSmFont() {
  if (g_editorSmFontTried) {
    return;
  }
  g_editorSmFontTried = true;

  g_editorSmFont = FNT_Parse("content\\qed\\editorsm.fnt");
  if (!g_editorSmFont.valid) {
    return;
  }

  pic_s pic;
  pic.width_  = g_editorSmFont.texWidth;
  pic.height_ = g_editorSmFont.texHeight;
  pic.pixels_ = g_editorSmFont.rgba.data();
  g_editorSmFontTex = GL_RegisterTexture(&pic, GL_CLAMP_TO_EDGE, GL_NEAREST, GL_NEAREST, false);
}

// Draw a string with the editor bitmap font in the HUD ortho space (y=0 bottom).
// y_gl is the gl-space y of the top of the first text line; glyphs extend
// downward from there. '\n' starts a new line.
static void DrawFontText(int x, int y_gl, const char* str, float r, float g, float b, float scale = 1.0f) {
  if (!g_editorFont.valid || !g_editorFontTex) {
    return;
  }

  const float texW = (float)g_editorFont.texWidth;
  (void)texW;
  const float spaceAdvance = (g_editorFont.lineHeight > 0 ? g_editorFont.lineHeight / 2 : 4) * scale;

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, g_editorFontTex);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColor4f(r, g, b, 1.0f);

  // Float pen so fractional per-glyph scaling accumulates smoothly — every 1-pt
  // size step visibly changes width instead of rounding to the same integer.
  float pen_x = (float)x;
  float pen_y = (float)y_gl; // top of current line in gl space (y up)

  glBegin(GL_QUADS);
  for (const char* p = str; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if (c == '\n') {
      pen_x = (float)x;
      pen_y -= g_editorFont.lineHeight * scale;
      continue;
    }

    auto it = g_editorFont.glyphs.find((int)c);
    if (it == g_editorFont.glyphs.end()) {
      pen_x += spaceAdvance; // unknown char -> advance like a space
      continue;
    }

    const FntGlyph& gl = it->second;
    float x0 = pen_x;
    float x1 = pen_x + gl.width * scale;
    float yTop = pen_y;                    // y up: top of glyph
    float yBot = pen_y - gl.height * scale;

    // Atlas V grows downward; gl V grows upward -> top of glyph uses v0.
    glTexCoord2f(gl.u0, gl.v0); glVertex2f(x0, yTop);
    glTexCoord2f(gl.u1, gl.v0); glVertex2f(x1, yTop);
    glTexCoord2f(gl.u1, gl.v1); glVertex2f(x1, yBot);
    glTexCoord2f(gl.u0, gl.v1); glVertex2f(x0, yBot);

    pen_x += gl.advance * scale;
  }
  glEnd();

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
}

// Draw a string with the small editor bitmap font (editorsm.fnt) for tooltips.
static void DrawFontTextSm(int x, int y_gl, const char* str, float r, float g, float b) {
  if (!g_editorSmFont.valid || !g_editorSmFontTex) {
    return;
  }

  const int spaceAdvance = g_editorSmFont.lineHeight > 0 ? g_editorSmFont.lineHeight / 2 : 4;

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, g_editorSmFontTex);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColor4f(r, g, b, 1.0f); // Use glColor4f to ensure alpha is 1

  int pen_x = x;
  int pen_y = y_gl;

  glBegin(GL_QUADS);
  for (const char* p = str; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if (c == '\n') {
      pen_x = x;
      pen_y -= g_editorSmFont.lineHeight;
      continue;
    }

    auto it = g_editorSmFont.glyphs.find((int)c);
    if (it == g_editorSmFont.glyphs.end()) {
      pen_x += spaceAdvance;
      continue;
    }

    const FntGlyph& gl = it->second;
    float x0 = (float)pen_x;
    float x1 = (float)(pen_x + gl.width);
    float yTop = (float)pen_y;
    float yBot = (float)(pen_y - gl.height);

    glTexCoord2f(gl.u0, gl.v0); glVertex2f(x0, yTop);
    glTexCoord2f(gl.u1, gl.v0); glVertex2f(x1, yTop);
    glTexCoord2f(gl.u1, gl.v1); glVertex2f(x1, yBot);
    glTexCoord2f(gl.u0, gl.v1); glVertex2f(x0, yBot);

    pen_x += gl.advance;
  }
  glEnd();

  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
}

/*
================================================================================
 Renderer
================================================================================
*/
Renderer::Renderer() : ubo_mats_(0), ubo_fog_(0), splines_(objects_) {
}

Renderer::~Renderer() { Shutdown(); }

#include <sstream>
#include <iomanip>




bool Renderer::Init() {
  ubo_mats_ = GL_CreateBuffer(GL_UNIFORM_BUFFER, sizeof(ubo_mats_s), nullptr,
                              GL_DYNAMIC_DRAW);
  ubo_fog_ = GL_CreateBuffer(GL_UNIFORM_BUFFER, sizeof(ubo_fog_s), nullptr,
                             GL_STATIC_DRAW);

  if (!skydome_.Init()) {
    return false;
  }

  if (!flat_sky_layers_.Init()) {
    return false;
  }

  if (!terrain_.Init()) {
    return false;
  }

  if (!objects_.Init()) {
    return false;
  }

  // init default state
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);

  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glDepthMask(GL_TRUE);
  glDepthRangef(RENDER_DEPTH_MIN, RENDER_DEPTH_MAX);

  glEnable(GL_CULL_FACE);
  glFrontFace(GL_CCW);
  glCullFace(GL_BACK);

#if defined(_WIN32)
  glPolygonOffset(-1.0f, -1.0f);
#else
  glPolygonOffset(-64.0f, -64.0f); // tune this
#endif
  glDisable(GL_POLYGON_OFFSET_LINE);

  GL_TryEnableVSync();

  return true;
}

void Renderer::Shutdown() {
  terrain_.Shutdown();
  objects_.Shutdown();
  flat_sky_layers_.Shutdown();

  skydome_.Shutdown();

  GL_DeleteBuffer(ubo_fog_);
  GL_DeleteBuffer(ubo_mats_);
}

void Renderer::BeginLoadLevel() {
  flat_sky_layers_.UnloadAllTexs();
  terrain_.UnloadAllTexs();
  objects_.ClearCaches();
}

void Renderer::SetupClearColor(const glm::vec4 &color) {
  glClearColor(color.r, color.g, color.b, 1.0f);
}

void Renderer::SetupFog(const glm::vec4 &color, float fog_far) {
  ubo_fog_s ubo_fog;

  ubo_fog.color_ = color;
  ubo_fog.far_ = fog_far;

  GL_BufferData(ubo_fog_, GL_UNIFORM_BUFFER, sizeof(ubo_fog), &ubo_fog,
                GL_STATIC_DRAW);
}

void Renderer::SetupSkydome(const skydome_define_s &d) {
  skydome_.UpdateVertices(d);
}

void Renderer::LoadFlatSkyLayerTex(int layer_no, const pic_s *pic) {
  flat_sky_layers_.LoadLayerTex(layer_no, pic);
}

void Renderer::LoadTerrainMatTex(const pic_s *pic) { terrain_.LoadMatTex(pic); }

void Renderer::LoadTerrainLMPTex(const pic_s *pic) { terrain_.LoadLMPTex(pic); }

vert_flat_sky_layer_s *Renderer::MapFlatSkyLayersVB() {
  return flat_sky_layers_.MapVB();
}

void Renderer::UnmapFlatSkyLayersVB() { flat_sky_layers_.UnmapVB(); }

vert_pos_a_uv_s *Renderer::MapTerrainVB() { return terrain_.MapVB(); }

void Renderer::UnmapTerrainVB() { terrain_.UnmapVB(); }

uint32_t *Renderer::MapTerrainIB() { return terrain_.MapIB(); }

void Renderer::UnmapTerrainIB() { terrain_.UnmapIB(); }

render_chunk_s *Renderer::GetTerrainRenderChunckBuffer() {
  return terrain_.GetRenderChunckBuffer();
}

void Renderer::Draw(const draw_params_s &params,
                    const task_tree_view_params_s &task_tree_view) {
  static bool logged_params = false;
  if (!logged_params) {
    Logger::Get().Log(
        LogLevel::INFO,
        "[Renderer] Draw Params: draw_parts=" +
            std::to_string(params.draw_parts_) +
            " level_objects=" + (params.level_objects_ ? "VALID" : "NULL"));
    logged_params = true;
  }
  SetupUBOMats(*params.view_define_);

  // start draw
  glDepthMask(GL_TRUE); // insure clear depth buffer
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
  glViewport(0, 0, params.view_define_->viewport_width_,
             params.view_define_->viewport_height_);

  if (params.draw_parts_ & DRAW_SKYDOME) {
    skydome_.Draw(ubo_mats_, params.overlay_wireframe_);
  }

  if ((params.draw_parts_ & DRAW_FLAT_SKY_LAYER) &&
      params.flat_sky_layer_is_visible_) {
    flat_sky_layers_.Draw(ubo_mats_, params.overlay_wireframe_);
  }

  if (params.draw_parts_ & DRAW_TERRAIN) {
    terrain_.Draw(ubo_mats_, ubo_fog_, params.overlay_wireframe_,
                  params.draw_terrain_options_,
                  params.num_terrain_render_chunk_);
  }

  if ((params.draw_parts_ & (DRAW_OBJECTS | DRAW_BUILDINGS | DRAW_PROPS)) &&
      params.level_objects_) {
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(glm::value_ptr(mat_proj_));
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(glm::value_ptr(mat_view_));
    objects_.Draw(ubo_mats_, params.overlay_wireframe_,
                  params.level_objects_->GetObjects(),
                  params.selected_object_index_, task_tree_view.hover_object_index_,
                  params.draw_parts_, params.view_define_->pos_,
                  params.show_magic_obj_spheres_);
    splines_.Draw(params.level_objects_->GetObjects(), ubo_mats_,
                  objects_.GetShaderProgram());
  }

  // 2D HUD overlay — always active so tooltip/pause/debug show even when TreeView is hidden
  {
    glUseProgram(0);      // Disable any active shaders for fixed-function HUD
    glBindVertexArray(0); // UNBIND VAO to prevent state leak
    glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0); // UNBIND UBO
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
    GL_CHECK_ERROR;

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_LIGHTING);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0, params.view_define_->viewport_width_, 0,
            params.view_define_->viewport_height_, -1, 1);

    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    const bool useEditorFont = Config::Get().useEditorFont;
    if (useEditorFont) {
      EnsureEditorFont();
      EnsureEditorSmFont();
    }

    // UI font scale derived from the configured size so the task tree, picker,
    // and property editor (all routed through draw_text) honor the Font Size
    // chosen in the pause menu. 12 is the baseline; 10/18 scale the editor
    // bitmap glyphs and pick the matching GLUT bitmap for the fallback path.
    const int uiFontSize = Config::Get().systemFontSize;
    // Continuous scale: 12 is the baseline; size changes in 1-pt steps. The editor
    // bitmap font scales smoothly; the GLUT fallback picks the nearest bitmap.
    const float uiFontScale = (float)uiFontSize / 12.0f;
    void *uiGlutFont = (uiFontSize <= 11) ? GLUT_BITMAP_HELVETICA_10
                     : (uiFontSize >= 15) ? GLUT_BITMAP_HELVETICA_18
                                          : GLUT_BITMAP_HELVETICA_12;
    const int uiLineH = std::max(10, (int)(15 * uiFontScale));

    // Pixel width of the first `count` chars of `str` in the active HUD font.
    // Mirrors DrawFontText's per-glyph advance exactly so callers (e.g. the text
    // caret) land on the true character boundary instead of a fixed-width guess.
    auto measure_text_width = [&](const char *str, int count) -> int {
      if (count <= 0) return 0;
      if (useEditorFont && g_editorFont.valid && g_editorFontTex) {
        const int spaceAdvance =
            g_editorFont.lineHeight > 0 ? g_editorFont.lineHeight / 2 : 4;
        int w = 0;
        for (int i = 0; i < count && str[i]; ++i) {
          auto it = g_editorFont.glyphs.find((int)(unsigned char)str[i]);
          w += (it != g_editorFont.glyphs.end()) ? it->second.advance : spaceAdvance;
        }
        return (int)(w * uiFontScale);
      }
      int w = 0;
      for (int i = 0; i < count && str[i]; ++i)
        w += glutBitmapWidth(uiGlutFont, (unsigned char)str[i]);
      return w;
    };

    auto draw_text = [&](int x, int y, const char *str, float r, float g,
                         float b) {
      if (useEditorFont && g_editorFont.valid && g_editorFontTex) {
        // Editor bitmap font path. y is top-down; convert each line's top to
        // gl space (y=0 bottom) and let glyphs extend downward.
        std::stringstream ss(str);
        std::string line;
        int line_y = y;
        while (std::getline(ss, line)) {
          int y_gl = params.view_define_->viewport_height_ - line_y;
          // 1px black shadow for readability, then the requested color.
          DrawFontText(x + 1, y_gl - 1, line.c_str(), 0.0f, 0.0f, 0.0f, uiFontScale);
          DrawFontText(x, y_gl, line.c_str(), r, g, b, uiFontScale);
          line_y += uiLineH; // Vertical spacing
        }
        return;
      }

      // Fallback: GLUT bitmap font (size chosen from config).
      std::stringstream ss(str);
      std::string line;
      int line_y = y;
      while (std::getline(ss, line)) {
        // Draw shadow
        glColor3f(0.0f, 0.0f, 0.0f);
        glRasterPos2i(x + 1,
                      params.view_define_->viewport_height_ - line_y - 1);
        for (char c : line)
          glutBitmapCharacter(uiGlutFont, c);

        // Draw main text
        glColor3f(r, g, b);
        glRasterPos2i(x, params.view_define_->viewport_height_ - line_y);
        for (char c : line)
          glutBitmapCharacter(uiGlutFont, c);

        line_y += uiLineH; // Vertical spacing
      }
    };

    auto draw_text_mono = [&](int x, int y, const char *str, float r, float g,
                              float b) {
      std::stringstream ss(str);
      std::string line;
      int line_y = y;
      while (std::getline(ss, line)) {
        // Draw shadow
        glColor3f(0.0f, 0.0f, 0.0f);
        glRasterPos2i(x + 1,
                      params.view_define_->viewport_height_ - line_y - 1);
        for (char c : line)
          glutBitmapCharacter(GLUT_BITMAP_9_BY_15, c);

        // Draw main text
        glColor3f(r, g, b);
        glRasterPos2i(x, params.view_define_->viewport_height_ - line_y);
        for (char c : line)
          glutBitmapCharacter(GLUT_BITMAP_9_BY_15, c);

        line_y += 18; // Vertical spacing for 9x15
      }
    };

    // draw_text_sm: uses editorsm.fnt bitmap font for tooltip text.
    auto draw_text_sm = [&](int x, int y, const char *str, float r, float g,
                            float b) {
      if (useEditorFont && g_editorSmFont.valid && g_editorSmFontTex) {
        std::stringstream ss(str);
        std::string line;
        int line_y = y;
        while (std::getline(ss, line)) {
          int y_gl = params.view_define_->viewport_height_ - line_y;
          DrawFontTextSm(x + 1, y_gl - 1, line.c_str(), 0.0f, 0.0f, 0.0f);
          DrawFontTextSm(x, y_gl, line.c_str(), r, g, b);
          line_y += g_editorSmFont.lineHeight > 0 ? g_editorSmFont.lineHeight + 2 : 15;
        }
        return;
      }
      // Fallback: GLUT bitmap font (smaller).
      std::stringstream ss(str);
      std::string line;
      int line_y = y;
      while (std::getline(ss, line)) {
        glColor3f(0.0f, 0.0f, 0.0f);
        glRasterPos2i(x + 1, params.view_define_->viewport_height_ - line_y - 1);
        for (char c : line) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, c);
        glColor3f(r, g, b);
        glRasterPos2i(x, params.view_define_->viewport_height_ - line_y);
        for (char c : line) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_10, c);
        line_y += 13;
      }
    };

    // draw_text_sys: always uses GLUT system font (for Escape menu, Help, Debug).
    // Font size is chosen via Config::Get().systemFontSize (10, 12, or 18).
    const int sysFontSize = Config::Get().systemFontSize;
    void* sysFontPtr = (sysFontSize <= 11) ? GLUT_BITMAP_HELVETICA_10
                     : (sysFontSize >= 15) ? GLUT_BITMAP_HELVETICA_18
                                           : GLUT_BITMAP_HELVETICA_12;
    const int sysLineH = (sysFontSize <= 11) ? 13 : (sysFontSize >= 15) ? 22 : 15;
    auto draw_text_sys = [&](int x, int y, const char *str, float r, float g,
                             float b) {
      std::stringstream ss(str);
      std::string line;
      int line_y = y;
      while (std::getline(ss, line)) {
        glColor3f(0.0f, 0.0f, 0.0f);
        glRasterPos2i(x + 1, params.view_define_->viewport_height_ - line_y - 1);
        for (char c : line) glutBitmapCharacter(sysFontPtr, c);
        glColor3f(r, g, b);
        glRasterPos2i(x, params.view_define_->viewport_height_ - line_y);
        for (char c : line) glutBitmapCharacter(sysFontPtr, c);
        line_y += sysLineH;
      }
    };

    // Helper to convert KeyBinding to display string
    auto keybinding_to_string = [&](const KeyBinding &kb) -> std::string {
      std::string result;
      if (kb.ctrl)
        result += "CTRL+";
      if (kb.shift)
        result += "SHIFT+";
      if (kb.alt)
        result += "ALT+";

      // Convert vkCode to key name
      if (kb.vkCode >= VK_F1 && kb.vkCode <= VK_F12) {
        result += "F" + std::to_string(kb.vkCode - VK_F1 + 1);
      } else {
        // For regular keys, use the character
        char key = MapVirtualKeyA(kb.vkCode, MAPVK_VK_TO_CHAR) & 0xFF;
        if (key) {
          result += std::string(1, toupper(key));
        } else {
          result += "?";
        }
      }
      return result;
    };

    // Get font color from config
    ConfigData &cfg = Config::Get();
    float font_r = cfg.fontColorR / 255.0f;
    float font_g = cfg.fontColorG / 255.0f;
    float font_b = cfg.fontColorB / 255.0f;

    int line_y = 30;

    // --- TreeView HUD Implementation (only when TaskTree is visible) ---
    if (task_tree_view.show_hud_) {
    if (task_tree_view.level_objects_ && !task_tree_view.prop_editor_open_ && !task_tree_view.task_picker_open_) {
      const auto &objects = task_tree_view.level_objects_->GetObjects();
      int tree_x = 20;
      int tree_y = 30; // Starting Y for tree
      int row_h = 16;
      int start_y = 30;
      int current_row = 0;
      int scroll_offset = task_tree_view.tree_scroll_offset;
      int viewport_h = params.view_define_->viewport_height_;

      // Recursive helper to draw tree nodes
      current_row = 0;
      std::function<void(int, int)> draw_node = [&](int idx, int depth) {
        if (idx < 0 || idx >= (int)objects.size())
          return;
        const auto &obj = objects[idx];
        if (obj.deleted)
          return;

        int x = tree_x + (depth * 18);
        int y =
            start_y + (current_row - task_tree_view.tree_scroll_offset) * row_h;
        current_row++;

        if (y >= start_y && y < params.view_define_->viewport_height_ - 50) {
          // Highlight if selected or hovered
          if (idx == task_tree_view.selected_object_index_) {
            glEnable(GL_BLEND);
            glColor4f(0.0f, 0.5f, 0.0f, 0.4f);
            glBegin(GL_QUADS);
            glVertex2i(tree_x - 5,
                       params.view_define_->viewport_height_ - (y - 2));
            glVertex2i(tree_x + 300,
                       params.view_define_->viewport_height_ - (y - 2));
            glVertex2i(tree_x + 300,
                       params.view_define_->viewport_height_ - (y + row_h - 2));
            glVertex2i(tree_x - 5,
                       params.view_define_->viewport_height_ - (y + row_h - 2));
            glEnd();
          } else if (idx == task_tree_view.hover_tree_index_) {
            glEnable(GL_BLEND);
            glColor4f(0.3f, 0.3f, 0.3f, 0.3f);
            glBegin(GL_QUADS);
            glVertex2i(tree_x - 5,
                       params.view_define_->viewport_height_ - (y - 2));
            glVertex2i(tree_x + 300,
                       params.view_define_->viewport_height_ - (y - 2));
            glVertex2i(tree_x + 300,
                       params.view_define_->viewport_height_ - (y + row_h - 2));
            glVertex2i(tree_x - 5,
                       params.view_define_->viewport_height_ - (y + row_h - 2));
            glEnd();
          }

          // Draw Hierarchy Line (dotted vertical)
          if (depth > 0) {
            glLineStipple(1, 0xAAAA);
            glEnable(GL_LINE_STIPPLE);
            glColor3f(0.5f, 0.5f, 0.5f);
            glBegin(GL_LINES);
            // Vertical line from parent down to this node
            glVertex2i(x - 9, viewport_h - (y - row_h / 2));
            glVertex2i(x - 9, viewport_h - (y + row_h / 2));
            // Horizontal line to the node
            glVertex2i(x - 9, viewport_h - (y + row_h / 2));
            glVertex2i(x - 2, viewport_h - (y + row_h / 2));
            glEnd();
            glDisable(GL_LINE_STIPPLE);
          }

          // Expansion Toggle [+] or [-] box
          if (obj.isContainer && !obj.childrenIndices.empty()) {
            glColor3f(1.0f, 1.0f, 1.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2i(x - 14, viewport_h - (y + 4));
            glVertex2i(x - 6, viewport_h - (y + 4));
            glVertex2i(x - 6, viewport_h - (y + 12));
            glVertex2i(x - 14, viewport_h - (y + 12));
            glEnd();
            // Draw minus
            glBegin(GL_LINES);
            glVertex2i(x - 12, viewport_h - (y + 8));
            glVertex2i(x - 8, viewport_h - (y + 8));
            // Draw plus vertical
            if (!obj.expanded) {
              glVertex2i(x - 10, viewport_h - (y + 6));
              glVertex2i(x - 10, viewport_h - (y + 10));
            }
            glEnd();
          } else if (depth > 0) {
            // Just a horizontal dash if no children
            glColor3f(0.5f, 0.5f, 0.5f);
            glBegin(GL_LINES);
            glVertex2i(x - 14, viewport_h - (y + 8));
            glVertex2i(x - 6, viewport_h - (y + 8));
            glEnd();
          }

          // Draw Yellow Folder Icon
          glColor3f(1.0f, 0.9f, 0.2f);
          glBegin(GL_QUADS);
          glVertex2i(x, viewport_h - (y + 2));
          glVertex2i(x + 12, viewport_h - (y + 2));
          glVertex2i(x + 12, viewport_h - (y + 12));
          glVertex2i(x, viewport_h - (y + 12));
          glEnd();
          // Folder tab
          glBegin(GL_QUADS);
          glVertex2i(x, viewport_h - y);
          glVertex2i(x + 5, viewport_h - y);
          glVertex2i(x + 5, viewport_h - (y + 2));
          glVertex2i(x, viewport_h - (y + 2));
          glEnd();
          // Folder outline
          glColor3f(0.0f, 0.0f, 0.0f);
          glBegin(GL_LINE_LOOP);
          glVertex2i(x, viewport_h - (y + 2));
          glVertex2i(x + 12, viewport_h - (y + 2));
          glVertex2i(x + 12, viewport_h - (y + 12));
          glVertex2i(x, viewport_h - (y + 12));
          glEnd();

          // Format label: Type (ID, "Name")
          std::string label = obj.type;
          if (!obj.taskId.empty() && obj.taskId != "-1") {
            label += " (" + obj.taskId;
            if (!obj.name.empty())
              label += ", \"" + obj.name + "\"";
            label += ")";
          } else if (!obj.name.empty()) {
            label += " (\"" + obj.name + "\")";
          }

          float tr = font_r, tg = font_g, tb = font_b;
          if (idx == task_tree_view.selected_object_index_) {
            tr = 1.0f;
            tg = 1.0f;
            tb = 0.0f;
          } // Selected = Yellow
          else if (idx == task_tree_view.hover_object_index_) {
            tr = 0.5f;
            tg = 0.8f;
            tb = 1.0f;
          } // Hover = Blue

          // Note: We use y + 11 for draw_text so the baseline aligns correctly
          // with the hitbox
          draw_text(x + 16, y + 11, label.c_str(), tr, tg, tb);
        }

        if (obj.expanded) {
          for (int childIdx : obj.childrenIndices) {
            draw_node(childIdx, depth + 1);
          }
        }
      };

      // To keep the root clean, group all Task_DeclareParameters into a virtual
      // "Mission Declarations" folder
      std::vector<int> root_decls;
      std::vector<int> root_others;

      for (int i = 0; i < (int)objects.size(); ++i) {
        if (objects[i].parentIndex == -1 && !objects[i].deleted) {
          if (objects[i].type == "Task_DeclareParameters")
            root_decls.push_back(i);
          else
            root_others.push_back(i);
        }
      }

      if (!root_decls.empty()) {
        int y = start_y + (current_row - scroll_offset) * row_h;
        current_row++;
        if (y >= start_y && y < viewport_h - 50) {
          int vx = tree_x;
          // Draw folder icon
          glColor3f(1.0f, 0.9f, 0.2f);
          glBegin(GL_QUADS);
          glVertex2i(vx, viewport_h - (y + 2));
          glVertex2i(vx + 12, viewport_h - (y + 2));
          glVertex2i(vx + 12, viewport_h - (y + 12));
          glVertex2i(vx, viewport_h - (y + 12));
          glEnd();
          // Folder tab
          glBegin(GL_QUADS);
          glVertex2i(vx, viewport_h - y);
          glVertex2i(vx + 5, viewport_h - y);
          glVertex2i(vx + 5, viewport_h - (y + 2));
          glVertex2i(vx, viewport_h - (y + 2));
          glEnd();
          // Folder outline
          glColor3f(0.0f, 0.0f, 0.0f);
          glBegin(GL_LINE_LOOP);
          glVertex2i(vx, viewport_h - (y + 2));
          glVertex2i(vx + 12, viewport_h - (y + 2));
          glVertex2i(vx + 12, viewport_h - (y + 12));
          glVertex2i(vx, viewport_h - (y + 12));
          glEnd();

          // Draw toggle box
          glColor3f(1.0f, 1.0f, 1.0f);
          glBegin(GL_LINE_LOOP);
          glVertex2i(vx - 14, viewport_h - (y + 4));
          glVertex2i(vx - 6, viewport_h - (y + 4));
          glVertex2i(vx - 6, viewport_h - (y + 12));
          glVertex2i(vx - 14, viewport_h - (y + 12));
          glEnd();
          // Draw minus
          glBegin(GL_LINES);
          glVertex2i(vx - 12, viewport_h - (y + 8));
          glVertex2i(vx - 8, viewport_h - (y + 8));
          // Draw plus vertical
          if (!task_tree_view.tree_decl_expanded) {
            glVertex2i(vx - 10, viewport_h - (y + 6));
            glVertex2i(vx - 10, viewport_h - (y + 10));
          }
          glEnd();

          if (task_tree_view.selected_object_index_ == -2) {

            glEnable(GL_BLEND);

            glColor4f(0.0f, 0.5f, 0.0f, 0.4f);

            glBegin(GL_QUADS);

            glVertex2i(vx - 5, viewport_h - (y - 2));

            glVertex2i(vx + 300, viewport_h - (y - 2));

            glVertex2i(vx + 300, viewport_h - (y + row_h - 2));

            glVertex2i(vx - 5, viewport_h - (y + row_h - 2));

            glEnd();

          } else if (task_tree_view.hover_object_index_ == -2) {

            glEnable(GL_BLEND);

            glColor4f(0.3f, 0.3f, 0.3f, 0.3f);

            glBegin(GL_QUADS);

            glVertex2i(vx - 5, viewport_h - (y - 2));

            glVertex2i(vx + 300, viewport_h - (y - 2));

            glVertex2i(vx + 300, viewport_h - (y + row_h - 2));

            glVertex2i(vx - 5, viewport_h - (y + row_h - 2));

            glEnd();
          }

          float dtr = 0.7f, dtg = 0.7f, dtb = 0.7f;

          if (task_tree_view.selected_object_index_ == -2) {
            dtr = 1.0f;
            dtg = 1.0f;
            dtb = 0.0f;
          }

          else if (task_tree_view.hover_object_index_ == -2) {
            dtr = 0.5f;
            dtg = 0.8f;
            dtb = 1.0f;
          }

          draw_text(vx + 16, y + 11, "Mission Declarations", dtr, dtg, dtb);
        }
        if (task_tree_view.tree_decl_expanded) {
          for (int idx : root_decls)
            draw_node(idx, 1);
        }
      }

      for (int idx : root_others) {
        draw_node(idx, 0);
      }

      line_y = start_y + (current_row - scroll_offset) * row_h + 20;
    }
    } // end show_hud_ tree panel

    // Display object info at mouse position
    int info_object_index = task_tree_view.hover_object_index_;
    if (!task_tree_view.status_msg_.empty() &&
        task_tree_view.selected_object_index_ >= 0) {
      info_object_index = task_tree_view.selected_object_index_;
    }

    // Suppress tooltip when: camera orbit mode, over TaskTree, or over property panel
    {
        bool over_tree  = task_tree_view.show_hud_ && task_tree_view.mouse_x_ < 350;
        bool over_panel = task_tree_view.prop_editor_open_ &&
                          task_tree_view.mouse_x_ < (PropPanel::kLeft + PropPanel::kWidth);
        if (over_tree || over_panel || task_tree_view.enable_camera_mode_) info_object_index = -1;
    }

    if (info_object_index >= 0 && task_tree_view.level_objects_) {
      const auto &objects = task_tree_view.level_objects_->GetObjects();
      if (info_object_index < (int)objects.size()) {
        const auto &obj = objects[info_object_index];

        // Skip labels for skipped models (Poles, Wires, etc.)
        if (Renderer_Objects::IsSkippedModelId(obj.modelId)) {
          info_object_index = -1; // Reset to hide label
        }
      }
    }

    int tooltip_x = task_tree_view.mouse_x_ + 20;  // right of cursor
    int tooltip_y = task_tree_view.mouse_y_;



    if (tooltip_x < 10)
      tooltip_x = 10;
    if (tooltip_x > params.view_define_->viewport_width_ - 260)
      tooltip_x = params.view_define_->viewport_width_ - 260;
    if (tooltip_y < 10)
      tooltip_y = 10;
    if (tooltip_y > params.view_define_->viewport_height_ - 100)
      tooltip_y = params.view_define_->viewport_height_ - 100;

    if (info_object_index >= 0 && task_tree_view.level_objects_) {
      const auto &objects = task_tree_view.level_objects_->GetObjects();
      if (info_object_index < (int)objects.size()) {
        const auto &obj = objects[info_object_index];

        char buf[512];
        // Look up model name from IGIModels.json; fall back to task note or type
        std::string display_name;
        if (task_tree_view.level_objects_) {
            display_name = task_tree_view.level_objects_->GetModelName(obj.modelId);
        }
        if (display_name.empty()) display_name = obj.type;
        if (display_name.empty()) display_name = obj.modelId;

        std::string task_id = obj.taskId.empty() ? "-1" : obj.taskId;

        int text_x = tooltip_x;
        int text_y = tooltip_y;

        bool isAI = !obj.aiId.empty() || obj.type.find("AITYPE") == 0 ||
                    obj.type == "HumanSoldier" || obj.type == "HumanAI" || obj.type == "HumanSoldierFemale" || obj.type == "HumanPlayer";
        if (isAI) {
          std::string aiName = obj.name.empty() ? "AI Soldier" : obj.name;
          snprintf(buf, sizeof(buf), "Name: %s (Type: %s)", aiName.c_str(), obj.type.c_str());
          draw_text_sm(text_x, text_y, buf, 0.0f, 0.8f, 1.0f); // Sky blue title
          text_y += 15;

          snprintf(buf, sizeof(buf), "Soldier ID: %s | AI ID: %s",
                   task_id.c_str(), obj.aiId.empty() ? "-1" : obj.aiId.c_str());
          draw_text_sm(text_x, text_y, buf, 1.0f, 1.0f, 1.0f);
          text_y += 15;

          // Read Team from argTokens via schema (more accurate than obj.team)
          int teamVal = obj.team;
          {
            const TaskSchema* sc = GetSchema(obj.type);
            if (sc) {
              for (auto& fd : *sc) {
                if (fd.name == "Team" && fd.argOffset < (int)obj.argTokens.size()) {
                  try { teamVal = std::stoi(obj.argTokens[fd.argOffset]); } catch (...) {}
                  break;
                }
              }
            }
          }
          std::string teamStr = (teamVal == 0) ? "Friendly" : "Enemy";
          float tr = (teamVal == 0) ? 0.2f : 1.0f;
          float tg = (teamVal == 0) ? 1.0f : 0.2f;
          float tb = 0.2f;
          snprintf(buf, sizeof(buf), "Team: %s (%d)", teamStr.c_str(), teamVal);
          draw_text_sm(text_x, text_y, buf, tr, tg, tb);
          text_y += 15;

          if (!obj.primaryWeapon.empty()) {
            snprintf(buf, sizeof(buf), "Pri: %s (%s Ammo)",
                     obj.primaryWeapon.c_str(), obj.primaryAmmo.c_str());
            draw_text_sm(text_x, text_y, buf, 0.9f, 0.9f, 0.9f);
            text_y += 15;
          }

          if (!obj.secondaryWeapon.empty()) {
            snprintf(buf, sizeof(buf), "Sec: %s (%s Ammo)",
                     obj.secondaryWeapon.c_str(), obj.secondaryAmmo.c_str());
            draw_text_sm(text_x, text_y, buf, 0.8f, 0.8f, 0.8f);
            text_y += 15;
          }

          if (!obj.graphName.empty() || !obj.graphId.empty()) {
            snprintf(buf, sizeof(buf), "Graph: %s (ID: %s)",
                     obj.graphName.c_str(), obj.graphId.c_str());
            draw_text_sm(text_x, text_y, buf, 0.9f, 0.6f, 0.9f);
            text_y += 15;
          }
        } else {
          snprintf(buf, sizeof(buf), "%s ID: %s", display_name.c_str(),
                   task_id.c_str());
          draw_text_sm(text_x, text_y, buf, 1.0f, 1.0f, 1.0f);
          text_y += 15;

          snprintf(buf, sizeof(buf), "%s", obj.modelId.c_str());
          draw_text_sm(text_x, text_y, buf, 0.0f, 1.0f, 0.0f);  // always green
          text_y += 15;
        }

        if (!task_tree_view.status_msg_.empty() &&
            info_object_index == task_tree_view.selected_object_index_) {
          draw_text_sm(text_x, text_y, task_tree_view.status_msg_.c_str(), 0.85f,
                    1.0f, 0.6f);
        }
      }
    } else if (!task_tree_view.pause_mode_ && (!task_tree_view.show_hud_ || task_tree_view.mouse_x_ >= 350)) {
      draw_text_sm(tooltip_x, tooltip_y, "Terrain ID: -1", 1.0f, 1.0f, 1.0f);
    }

    // Watermark — centered, versioned
    {
      static std::string s_ver_watermark;
      if (s_ver_watermark.empty())
        s_ver_watermark = "IGI Editor v" + Utils::GetVersionString() + " - HeavenHM";
      int wm_w = glutBitmapLength(GLUT_BITMAP_HELVETICA_12,
                                  (const unsigned char *)s_ver_watermark.c_str());
      int wm_x = (params.view_define_->viewport_width_ - wm_w) / 2;
      draw_text(wm_x, params.view_define_->viewport_height_ - 20, s_ver_watermark.c_str(), 0.7f, 0.7f, 0.7f);
    }

    if (task_tree_view.task_picker_open_ && task_tree_view.level_objects_) {
      int picker_x = 20;
      int picker_w = 520;
      int viewport_h = params.view_define_->viewport_height_;

      // Proportional card layout:
      // Header: top-down 20 to 50 (bottom-up viewport_h - 50 to viewport_h -
      // 20) Items List: top-down 50 to viewport_h - 50 (bottom-up 50 to
      // viewport_h - 50) Footer: top-down viewport_h - 50 to viewport_h - 20
      // (bottom-up 20 to 50)
      int card_top_y = viewport_h - 20; // bottom-up
      int card_bottom_y = 20;           // bottom-up
      int picker_h = viewport_h - 100;  // items list area height

      // Translucent white & yellow background (white and yellow transparent)
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(0.0f, 0.0f, 0.0f, 0.45f);
      glBegin(GL_QUADS);
      glVertex2i(picker_x, card_bottom_y);
      glVertex2i(picker_x + picker_w, card_bottom_y);
      glVertex2i(picker_x + picker_w, card_top_y);
      glVertex2i(picker_x, card_top_y);
      glEnd();

      // Warm Yellow/Gold transparent border
      glColor4f(1.0f, 0.85f, 0.0f, 0.7f); // Warm yellow/gold border
      glLineWidth(2.0f);
      glBegin(GL_LINE_LOOP);
      glVertex2i(picker_x, card_bottom_y);
      glVertex2i(picker_x + picker_w, card_bottom_y);
      glVertex2i(picker_x + picker_w, card_top_y);
      glVertex2i(picker_x, card_top_y);
      glEnd();
      glLineWidth(1.0f);

      // Header separator line (at bottom-up viewport_h - 50)
      glColor4f(1.0f, 0.85f, 0.0f, 0.7f);
      glBegin(GL_LINES);
      glVertex2i(picker_x, viewport_h - 50);
      glVertex2i(picker_x + picker_w, viewport_h - 50);
      glEnd();

      // Footer separator line (at bottom-up 50)
      glColor4f(1.0f, 0.85f, 0.0f, 0.7f);
      glBegin(GL_LINES);
      glVertex2i(picker_x, 50);
      glVertex2i(picker_x + picker_w, 50);
      glEnd();

      // Title
      draw_text(picker_x + 15, 38, "Select Task", 1.0f, 0.9f, 0.1f); // Vibrant yellow/gold

      // Search box at top right
      int box_left = picker_x + picker_w - 180;
      int box_right = picker_x + picker_w - 15;

      // Draw Search Box background (semi-transparent warm white)
      glColor4f(1.0f, 1.0f, 1.0f, 0.2f);
      glBegin(GL_QUADS);
      glVertex2i(box_left, viewport_h - 44);
      glVertex2i(box_right, viewport_h - 44);
      glVertex2i(box_right, viewport_h - 26);
      glVertex2i(box_left, viewport_h - 26);
      glEnd();

      // Draw Search Box border (transparent yellow/gold)
      glColor4f(1.0f, 0.85f, 0.0f, 0.7f);
      glBegin(GL_LINE_LOOP);
      glVertex2i(box_left, viewport_h - 44);
      glVertex2i(box_right, viewport_h - 44);
      glVertex2i(box_right, viewport_h - 26);
      glVertex2i(box_left, viewport_h - 26);
      glEnd();

      // Draw Search query or placeholder
      if (task_tree_view.task_picker_search_.empty()) {
        draw_text(box_left + 8, 38, "Search...", 0.5f, 0.5f, 0.5f);
      } else {
        draw_text(box_left + 8, 38, task_tree_view.task_picker_search_.c_str(),
                  1.0f, 1.0f, 1.0f);
      }

      // Footer guidelines
      draw_text(picker_x + 15, viewport_h - 38,
                "[Enter] Insert  [ESC] Cancel  [Type] Search", 0.7f, 0.7f, 0.7f);

      // Build task list mapping exactly like app.cpp
      const auto &objects = task_tree_view.level_objects_->GetObjects();
      std::vector<int> picker_to_objects;

      std::string search_lower = task_tree_view.task_picker_search_;
      std::transform(search_lower.begin(), search_lower.end(),
                     search_lower.begin(),
                     [](unsigned char c) { return std::tolower(c); });

      std::set<std::string> seen_types;
      for (int i = 0; i < (int)objects.size(); ++i) {
        if (!objects[i].deleted) {
          const auto &obj = objects[i];
          if (obj.type == "Task_DeclareParameters") continue; // picker excludes declare params
          std::string type_lower = obj.type;
          std::transform(type_lower.begin(), type_lower.end(), type_lower.begin(),
                         [](unsigned char c) { return std::tolower(c); });
          if (seen_types.count(type_lower) > 0) continue;
          seen_types.insert(type_lower);
          std::string label = obj.type + "()";

          std::string label_lower = label;
          std::transform(label_lower.begin(), label_lower.end(),
                         label_lower.begin(),
                         [](unsigned char c) { return std::tolower(c); });

          if (search_lower.empty() ||
              label_lower.find(search_lower) != std::string::npos) {
            picker_to_objects.push_back(i);
          }
        }
      }

      int row_h = 16;
      int count = (int)picker_to_objects.size();
      int max_visible = std::max(1, picker_h / row_h);

      int start_idx = task_tree_view.task_picker_scroll_offset_;
      int end_idx = std::min(count, start_idx + max_visible);

      if (count == 0) {
        draw_text(picker_x + 22, 70, "No matching tasks found.", 0.6f, 0.6f,
                  0.6f);
      } else {
        for (int i = start_idx; i < end_idx; ++i) {
          int obj_idx = picker_to_objects[i];
          const auto &obj = objects[obj_idx];
          int item_y = 50 + (i - start_idx) * row_h;

          // Background highlight for selected item (transparent yellow/gold)
          if (i == task_tree_view.task_picker_selected_idx_) {
            glEnable(GL_BLEND);
            glColor4f(1.0f, 0.85f, 0.0f,
                      0.35f); // semi-transparent vibrant yellow/gold
            glBegin(GL_QUADS);
            glVertex2i(picker_x + 4, viewport_h - (item_y + row_h));
            glVertex2i(picker_x + picker_w - 4, viewport_h - (item_y + row_h));
            glVertex2i(picker_x + picker_w - 4, viewport_h - item_y);
            glVertex2i(picker_x + 4, viewport_h - item_y);
            glEnd();

            // Arrow highlight pointing to selected line
            draw_text(picker_x + 8, item_y + 11, ">", 1.0f, 0.9f, 0.0f);
          }

          // Format label standard to HUD: Type()
          std::string label = obj.type + "()";

          // Truncate to prevent text overflow
          if (label.size() > 43) {
            label = label.substr(0, 40) + "...";
          }

          // Draw Golden/Yellow Folder Icon
          int folder_x = picker_x + 22;
          int folder_y = item_y;

          glColor3f(1.0f, 0.9f, 0.2f); // Golden/yellow folder color
          glBegin(GL_QUADS);
          glVertex2i(folder_x, viewport_h - (folder_y + 2));
          glVertex2i(folder_x + 12, viewport_h - (folder_y + 2));
          glVertex2i(folder_x + 12, viewport_h - (folder_y + 12));
          glVertex2i(folder_x, viewport_h - (folder_y + 12));
          glEnd();

          // Folder tab
          glBegin(GL_QUADS);
          glVertex2i(folder_x, viewport_h - folder_y);
          glVertex2i(folder_x + 5, viewport_h - folder_y);
          glVertex2i(folder_x + 5, viewport_h - (folder_y + 2));
          glVertex2i(folder_x, viewport_h - (folder_y + 2));
          glEnd();

          // Folder outline
          glColor3f(0.0f, 0.0f, 0.0f);
          glBegin(GL_LINE_LOOP);
          glVertex2i(folder_x, viewport_h - (folder_y + 2));
          glVertex2i(folder_x + 12, viewport_h - (folder_y + 2));
          glVertex2i(folder_x + 12, viewport_h - (folder_y + 12));
          glVertex2i(folder_x, viewport_h - (folder_y + 12));
          glEnd();

          // Select color
          float tr = 1.0f, tg = 1.0f,
                tb = 1.0f; // White text color as requested
          if (i == task_tree_view.task_picker_selected_idx_) {
            tr = 1.0f;
            tg = 0.9f;
            tb = 0.1f; // Yellow highlight for selected text
          }

          draw_text(picker_x + 38, item_y + 11, label.c_str(), tr, tg, tb);
        }
      }

      // Scrollbar on the right edge if tasks count exceeds viewport capacity
      if (count > max_visible) {
        int track_x = picker_x + picker_w - 8;
        int track_h = picker_h - 10;
        int track_y_top = viewport_h - 55;
        int track_y_bottom = track_y_top - track_h;

        // Draw track (translucent white)
        glColor4f(1.0f, 1.0f, 1.0f, 0.15f);
        glBegin(GL_LINES);
        glVertex2i(track_x, track_y_bottom);
        glVertex2i(track_x, track_y_top);
        glEnd();

        // Calculate thumb size and position
        float visible_ratio = (float)max_visible / (float)count;
        int thumb_h = std::max(20, (int)(track_h * visible_ratio));

        float scroll_ratio = 0.0f;
        if (count > max_visible) {
          scroll_ratio = (float)task_tree_view.task_picker_scroll_offset_ /
                         (float)(count - max_visible);
        }
        int thumb_y_top =
            track_y_top - (int)((track_h - thumb_h) * scroll_ratio);
        int thumb_y_bottom = thumb_y_top - thumb_h;

        // Draw thumb (match gold theme border color)
        glColor4f(1.0f, 0.85f, 0.0f, 0.7f);
        glLineWidth(3.0f);
        glBegin(GL_LINES);
        glVertex2i(track_x, thumb_y_bottom);
        glVertex2i(track_x, thumb_y_top);
        glEnd();
        glLineWidth(1.0f);
      }
    }


    // SPR sprite cursors are drawn by App::DrawCustomCursor() — no GLUT overlays here

    if (task_tree_view.pause_mode_) {
      const int menu_w = 380;
      const int menu_h = 280;
      const int menu_x = (params.view_define_->viewport_width_ - menu_w) / 2;
      const int menu_y = (params.view_define_->viewport_height_ - menu_h) / 2;
      const int viewport_h = params.view_define_->viewport_height_;

      // Glassmorphism-style background
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(0.02f, 0.15f, 0.02f, 0.94f); // Deep emerald
      glBegin(GL_QUADS);
      glVertex2i(menu_x, menu_y);
      glVertex2i(menu_x + menu_w, menu_y);
      glVertex2i(menu_x + menu_w, menu_y + menu_h);
      glVertex2i(menu_x, menu_y + menu_h);
      glEnd();
      glDisable(GL_BLEND);

      // Sharp green border
      glLineWidth(2.5f);
      glColor3f(0.0f, 1.0f, 0.0f);
      glBegin(GL_LINE_LOOP);
      glVertex2i(menu_x, menu_y);
      glVertex2i(menu_x + menu_w, menu_y);
      glVertex2i(menu_x + menu_w, menu_y + menu_h);
      glVertex2i(menu_x, menu_y + menu_h);
      glEnd();

      // Header separator
      glBegin(GL_LINES);
      glVertex2i(menu_x + 10, menu_y + menu_h - 45);
      glVertex2i(menu_x + menu_w - 10, menu_y + menu_h - 45);
      glEnd();
      glLineWidth(1.0f);

      int screen_menu_top = (viewport_h - menu_h) / 2;
      draw_text_sys(menu_x + menu_w / 2 - 45, screen_menu_top + 30, "IGI EDITOR",
                0.0f, 1.0f, 0.0f);
      draw_text_sys(menu_x + menu_w / 2 - 35, screen_menu_top + 44, "PAUSED", 0.8f,
                0.8f, 0.8f);

      // Font row is rendered specially (index 1): a "Font: <type>" toggle on the
      // left plus a [-] [size] [+] size control on the right, all on one line.
      char font_btn_label[32];
      snprintf(font_btn_label, sizeof(font_btn_label), "Font: %s",
               Config::Get().useEditorFont ? "Editor" : "System");
      const char *btn_labels[] = {"Resume", font_btn_label,
                                  "Reset Level", "Save Level", "Quit"};
      const int NUM_BTNS = 5;
      const int FONT_ROW = 1;

      for (int i = 0; i < NUM_BTNS; ++i) {
        int screen_btn_y = screen_menu_top + 85 + i * 35;
        int gl_btn_y = viewport_h - screen_btn_y;

        bool hovered = (task_tree_view.mouse_x_ >= menu_x &&
                        task_tree_view.mouse_x_ <= menu_x + menu_w &&
                        task_tree_view.mouse_y_ >= screen_btn_y - 15 &&
                        task_tree_view.mouse_y_ <= screen_btn_y + 15);

        if (i == FONT_ROW) {
          // Whole row "Font: <type>  [-] <n> [+]" is centered as one group.
          // Layout constants MUST match the click handler in app.cpp.
          const int sz_box_w = 34, btn_w = 22, gap = 6;
          const int label_w  = 96;   // "Font: Editor"/"Font: System" (~8px/char)
          const int label_gap = 16;
          const int controls_w = btn_w + gap + sz_box_w + gap + btn_w;
          const int group_w = label_w + label_gap + controls_w;
          int gx = menu_x + (menu_w - group_w) / 2;

          // Left: font-type toggle label.
          draw_text_sys(gx, screen_btn_y, font_btn_label,
                        hovered ? 1.0f : 0.0f, hovered ? 1.0f : 0.85f, 0.0f);

          // Right: [-] [size] [+] size control.
          int minus_x = gx + label_w + label_gap;
          int box_x   = minus_x + btn_w + gap;
          int plus_x  = box_x + sz_box_w + gap;
          int row_top = gl_btn_y - 14, row_bot = gl_btn_y + 10;

          auto sbox = [&](int x1, int w, const char *txt) {
            glColor3f(0.0f, 0.7f, 0.0f);
            glBegin(GL_LINE_LOOP);
            glVertex2i(x1, row_bot); glVertex2i(x1 + w, row_bot);
            glVertex2i(x1 + w, row_top); glVertex2i(x1, row_top);
            glEnd();
            int tw = (int)strlen(txt) * 6;
            draw_text_sys(x1 + (w - tw) / 2, screen_btn_y, txt, 0.0f, 0.9f, 0.0f);
          };
          char szbuf[8];
          snprintf(szbuf, sizeof(szbuf), "%d", Config::Get().systemFontSize);
          sbox(minus_x, btn_w, "-");
          sbox(box_x,   sz_box_w, szbuf);
          sbox(plus_x,  btn_w, "+");
        } else {
          if (hovered) {
            glEnable(GL_BLEND);
            glColor4f(0.0f, 0.8f, 0.0f, 0.35f);
            glBegin(GL_QUADS);
            glVertex2i(menu_x + 20, gl_btn_y - 16);
            glVertex2i(menu_x + menu_w - 20, gl_btn_y - 16);
            glVertex2i(menu_x + menu_w - 20, gl_btn_y + 12);
            glVertex2i(menu_x + 20, gl_btn_y + 12);
            glEnd();
            glDisable(GL_BLEND);
            draw_text_sys(menu_x + menu_w / 2 - (int)(strlen(btn_labels[i]) * 4),
                      screen_btn_y, btn_labels[i], 1.0f, 1.0f, 1.0f);
          } else {
            draw_text_sys(menu_x + menu_w / 2 - (int)(strlen(btn_labels[i]) * 4),
                      screen_btn_y, btn_labels[i], 0.0f, 0.85f, 0.0f);
          }
        }
      }
    }

    if (task_tree_view.show_debug_) {
      const auto &entries = Logger::Get().GetEntries();
      // Top-left, fully transparent (no panel, no border). Text uses the editor's
      // yellow/white theme: white for info, yellow for warnings, red for errors.
      const int debug_x = 10;
      const int startY  = 10;     // draw_text_sys uses top-left origin
      const int line_height = 14;
      const int max_lines = 30;
      const int max_chars = 110;

      // Show the last max_lines entries oldest→newest (newest at the bottom).
      int total = (int)entries.size();
      int first = std::max(0, total - max_lines);
      int row = 0;
      for (int i = first; i < total; ++i, ++row) {
        const auto &e = entries[i];
        float r = 1.0f, g = 1.0f, b = 1.0f;   // INFO / default: white
        if (e.level == LogLevel::ERR)          { r = 1.0f; g = 0.3f; b = 0.3f; }
        else if (e.level == LogLevel::FATAL)   { r = 1.0f; g = 0.0f; b = 0.0f; }
        else if (e.level == LogLevel::WARNING) { r = 1.0f; g = 0.85f; b = 0.1f; } // yellow
        else if (e.level == LogLevel::DEBUG)   { r = 0.6f; g = 0.8f; b = 1.0f; }
        std::string msg = e.message;
        if ((int)msg.length() > max_chars) msg = msg.substr(0, max_chars - 3) + "...";
        draw_text_sys(debug_x, startY + row * line_height, msg.c_str(), r, g, b);
      }
    }

    if (task_tree_view.show_help_) {
      const int vw = params.view_define_->viewport_width_;
      const int vh = params.view_define_->viewport_height_;
      const int menu_w  = 560;
      const int menu_h  = vh - 80;
      const int menu_x  = (vw - menu_w) / 2;
      const int menu_y  = 40; // top in screen coords
      const int row_h   = 16;
      const int pad     = 12;
      const int bar_w   = 14;  // vertical scrollbar width
      const int content_x = menu_x + pad;
      const int content_w = menu_w - 2 * pad - bar_w - 4;
      const int content_top = menu_y + 32;    // below title
      const int content_bot = menu_y + menu_h - 20;
      const int max_rows = (content_bot - content_top) / row_h;

      // Background — same semi-transparent dark as the TaskTree panel
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(0.04f, 0.06f, 0.04f, 0.82f);  // dark green-tint, slightly transparent
      {
        int gy1 = vh - menu_y, gy2 = vh - (menu_y + menu_h);
        glBegin(GL_QUADS);
        glVertex2i(menu_x, gy2); glVertex2i(menu_x + menu_w, gy2);
        glVertex2i(menu_x + menu_w, gy1); glVertex2i(menu_x, gy1);
        glEnd();
      }
      glDisable(GL_BLEND);
      // Bright green border (matches TaskTree style)
      glColor3f(0.0f, 1.0f, 0.0f);
      glLineWidth(2.0f);
      {
        int gy1 = vh - menu_y, gy2 = vh - (menu_y + menu_h);
        glBegin(GL_LINE_LOOP);
        glVertex2i(menu_x, gy2); glVertex2i(menu_x + menu_w, gy2);
        glVertex2i(menu_x + menu_w, gy1); glVertex2i(menu_x, gy1);
        glEnd();
      }
      glLineWidth(1.0f);
      // Yellow title, like TaskTree selected-item colour
      draw_text(menu_x + menu_w / 2 - 96, menu_y + 18,
                "KEYBINDINGS  [H / Ctrl+H to close]", 1.0f, 1.0f, 0.0f);

      // Draw visible rows from help_entries_
      const std::vector<std::string>* entries = task_tree_view.help_entries_;
      int scroll = task_tree_view.help_scroll_offset_;
      int total  = entries ? (int)entries->size() : 0;
      int clamped_scroll = std::max(0, std::min(scroll, std::max(0, total - max_rows)));

      for (int r = 0; r < max_rows && (clamped_scroll + r) < total; ++r) {
        const std::string& line = (*entries)[clamped_scroll + r];
        int ly = content_top + r * row_h;
        // Alternate yellow / light-yellow rows like the TaskTree selection highlight
        float lr = 1.0f, lg = (r & 1) ? 0.85f : 1.0f, lb = 0.0f;
        draw_text(content_x, ly + row_h - 4, line.c_str(), lr, lg, lb);
      }

      // Vertical scrollbar track + thumb
      if (total > max_rows) {
        int bar_x  = menu_x + menu_w - pad - bar_w;
        int bar_y1 = content_top;
        int bar_y2 = content_bot;
        int bar_gl_y1 = vh - bar_y1, bar_gl_y2 = vh - bar_y2;
        // Track
        glEnable(GL_BLEND);
        glColor4f(0.2f, 0.2f, 0.3f, 0.8f);
        glBegin(GL_QUADS);
        glVertex2i(bar_x, bar_gl_y2); glVertex2i(bar_x + bar_w, bar_gl_y2);
        glVertex2i(bar_x + bar_w, bar_gl_y1); glVertex2i(bar_x, bar_gl_y1);
        glEnd();
        // Thumb
        int track_h = bar_y2 - bar_y1;
        int thumb_h = std::max(20, track_h * max_rows / total);
        int thumb_top = bar_y1 + (track_h - thumb_h) * clamped_scroll / std::max(1, total - max_rows);
        int th_gl_y1 = vh - thumb_top, th_gl_y2 = vh - (thumb_top + thumb_h);
        glColor4f(0.5f, 0.7f, 1.0f, 0.9f);
        glBegin(GL_QUADS);
        glVertex2i(bar_x, th_gl_y2); glVertex2i(bar_x + bar_w, th_gl_y2);
        glVertex2i(bar_x + bar_w, th_gl_y1); glVertex2i(bar_x, th_gl_y1);
        glEnd();
        glDisable(GL_BLEND);
      }
    }

    // ── C2: IGI2-style property panel (left side, replaces tree) ───────────────
    if (task_tree_view.prop_editor_open_ && task_tree_view.selected_object_index_ >= 0 && task_tree_view.level_objects_) {
      const auto& objects = task_tree_view.level_objects_->GetObjects();
      int sel = task_tree_view.selected_object_index_;
      if (sel < (int)objects.size()) {
        const auto& obj = objects[sel];
        const TaskSchema* scp = GetSchema(obj.type);
        if (scp) {
          const TaskSchema& schema = *scp;
          int vh = params.view_define_->viewport_height_;

          // Gather editable child-task sections (weapon/ammo/AI sub-tasks). Only
          // shown when the task actually has children; appended to the layout so
          // their fields are real editable widgets (routed to the child object).
          std::vector<std::pair<int,const TaskSchema*>> child_schemas; // (child obj idx, schema)
          for (int ci : obj.childrenIndices) {
              if (ci < 0 || ci >= (int)objects.size()) continue;
              const auto& child = objects[ci];
              if (child.deleted) continue;
              const TaskSchema* cscp = GetSchema(child.type);
              if (cscp && !cscp->empty()) child_schemas.push_back({ci, cscp});
          }

          PropPanel::Layout L = PropPanel::BuildLayout(schema, task_tree_view.selected_obj_is_ai, child_schemas);

          // Apply vertical scroll: shift all widget Y positions.
          const int scroll = task_tree_view.prop_panel_scroll_;
          if (scroll > 0) {
              for (auto& w : L.widgets) { w.y1 -= scroll; w.y2 -= scroll; }
          }

          // GL y for a screen-top-down y.
          auto gl_y = [&](int sy) { return vh - sy; };

          // Enable scissor clipping so scrolled content doesn't bleed outside the panel.
          const int panel_vis_h = vh - PropPanel::kTop;
          glEnable(GL_SCISSOR_TEST);
          glScissor(PropPanel::kLeft, 0, PropPanel::kWidth + 20, panel_vis_h);
          // Filled quad in screen coords.
          auto quad = [&](int x1, int sy1, int x2, int sy2, float r, float g, float b, float a) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(r, g, b, a);
            int gy1 = gl_y(sy1), gy2 = gl_y(sy2);
            glBegin(GL_QUADS);
            glVertex2i(x1, gy1); glVertex2i(x2, gy1);
            glVertex2i(x2, gy2); glVertex2i(x1, gy2);
            glEnd();
            glDisable(GL_BLEND);
          };
          auto border = [&](int x1, int sy1, int x2, int sy2, float r, float g, float b) {
            glColor3f(r, g, b);
            int gy1 = gl_y(sy1), gy2 = gl_y(sy2);
            glBegin(GL_LINE_LOOP);
            glVertex2i(x1, gy1); glVertex2i(x2, gy1);
            glVertex2i(x2, gy2); glVertex2i(x1, gy2);
            glEnd();
          };

          // Current render target — reassigned per object (parent or a child) so
          // the shared field renderer reads values from / scopes edits to the right
          // LevelObject. This is what gives children the identical parent interface.
          const LevelObject* curObj = &obj;
          const TaskSchema*  curSchema = &schema;
          int curObjIdx = sel;
          // Does the active text-edit / drag target the object currently drawn?
          // prop_*_obj_index_ < 0 means "the parent" (selected object).
          auto resolveEdit = [&](int tIdx) {
            return task_tree_view.prop_edit_obj_index_ < 0 ? (tIdx == sel)
                 : (task_tree_view.prop_edit_obj_index_ == tIdx);
          };
          auto resolveDrag = [&](int tIdx) {
            return task_tree_view.prop_drag_obj_index_ < 0 ? (tIdx == sel)
                 : (task_tree_view.prop_drag_obj_index_ == tIdx);
          };

          auto tok = [&](int idx) -> std::string {
            return (idx >= 0 && idx < (int)curObj->argTokens.size()) ? curObj->argTokens[idx] : std::string("-");
          };

          // Like tok() but strips surrounding double-quotes for display.
          auto display_tok = [&](int idx) -> std::string {
            std::string v = tok(idx);
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
              return v.substr(1, v.size() - 2);
            return v;
          };

          // Caret blink (on/off ~2Hz). ~7px per char matches the wrap heuristic.
          const bool caret_on = ((glutGet(GLUT_ELAPSED_TIME) / 500) & 1) == 0;
          int caret_idx = task_tree_view.prop_text_caret_;
          if (caret_idx < 0) caret_idx = 0;
          if (caret_idx > (int)task_tree_view.prop_text_buf_.size())
            caret_idx = (int)task_tree_view.prop_text_buf_.size();
          // Draw an editable box's contents + blinking caret. `field_id` is the
          // edit-field key (fi*3+comp, or -2 for the note). `multiline` wraps.
          // Returns flat text offsets of each visual line start.
          // Lines split on '\n'; lines longer than max_chars wrap.
          auto aiLineStarts = [](const std::string& t, int mc) {
            std::vector<int> s; s.push_back(0);
            for (int i = 0; i < (int)t.size(); ) {
              if (t[i] == '\n') { s.push_back(i + 1); i++; }
              else {
                int cnt = 0;
                while (i < (int)t.size() && t[i] != '\n' && cnt < mc) { i++; cnt++; }
                if (i < (int)t.size() && t[i] != '\n') s.push_back(i);
              }
            }
            return s;
          };

          // draw_edit_box: start_line = first visible visual line (scroll offset for multiline).
          //                hscroll    = first visible char offset (scroll offset for single-line).
          auto draw_edit_box = [&](const PropPanel::Widget& w, int field_id,
                                   const std::string& live_text, bool multiline,
                                   int start_line = 0, int hscroll = 0) {
            bool editing = (resolveEdit(curObjIdx) && task_tree_view.prop_text_edit_field_ == field_id);
            const std::string& txt = editing ? task_tree_view.prop_text_buf_ : live_text;
            const int cw = 7;
            int max_chars = std::max(1, (w.x2 - w.x1 - 6) / cw);
            int box_lines_cap = std::max(1, (w.y2 - w.y1) / PropPanel::kBoxH);

            if (!multiline) {
              // Single-line with horizontal scroll
              int hs = std::max(0, std::min(hscroll, (int)txt.size()));
              std::string disp = txt.size() > (size_t)hs ? txt.substr(hs, max_chars) : "";
              draw_text(w.x1 + 3, w.y1 + 12, disp.c_str(), 1.0f, 1.0f, 0.85f);
              if (editing && caret_on) {
                int vis = std::max(0, caret_idx - hs);
                vis = std::min(vis, (int)disp.size());
                std::string before = disp.substr(0, vis);
                int cx = w.x1 + 3 + measure_text_width(before.c_str(), (int)before.size());
                glColor3f(1.0f, 0.95f, 0.2f);
                glBegin(GL_LINES);
                glVertex2i(cx, gl_y(w.y1 + 3)); glVertex2i(cx, gl_y(w.y1 + 16));
                glEnd();
              }
              return;
            }

            // Multiline: build visual line starts respecting \n and max_chars wrap
            auto lstarts = aiLineStarts(txt, max_chars);
            // Caret visual line/col
            int caret_line = -1, caret_col = -1;
            if (editing) {
              caret_line = (int)(std::upper_bound(lstarts.begin(), lstarts.end(), caret_idx) - lstarts.begin()) - 1;
              caret_line = std::max(0, std::min(caret_line, (int)lstarts.size() - 1));
              caret_col  = caret_idx - lstarts[caret_line];
            }
            // Render visible lines from start_line
            int render_line = 0;
            for (int li = start_line; li < (int)lstarts.size() && render_line < box_lines_cap; li++, render_line++) {
              int ls = lstarts[li];
              int le = (li + 1 < (int)lstarts.size()) ? lstarts[li + 1] : (int)txt.size();
              if (le > ls && txt[le - 1] == '\n') le--;
              int seg_len = std::min(le - ls, max_chars);
              if (seg_len < 0) seg_len = 0;
              std::string seg = txt.substr(ls, seg_len);
              draw_text(w.x1 + 3, w.y1 + 12 + render_line * PropPanel::kBoxH, seg.c_str(), 1.0f, 1.0f, 0.85f);
              if (editing && caret_on && caret_line == li) {
                int cc = std::min(caret_col, (int)seg.size());
                std::string before = seg.substr(0, cc);
                int cx  = w.x1 + 3 + measure_text_width(before.c_str(), (int)before.size());
                int cyt = w.y1 + 3 + render_line * PropPanel::kBoxH;
                glColor3f(1.0f, 0.95f, 0.2f);
                glBegin(GL_LINES);
                glVertex2i(cx, gl_y(cyt)); glVertex2i(cx, gl_y(cyt + 13));
                glEnd();
              }
            }
          };

          // Header text — track running screen y to mirror the layout.
          int ty = L.panel_y + PropPanel::kPad;
          char hdr[160];
          snprintf(hdr, sizeof(hdr), "QTasktype: %s", obj.type.c_str());
          draw_text(L.panel_x + PropPanel::kPad, ty + 11, hdr, 1.0f, 1.0f, 1.0f);
          ty += PropPanel::kRowH;
          draw_text(L.panel_x + PropPanel::kPad, ty + 11, "QTask Note (QTaskNote):", 1.0f, 0.9f, 0.2f);
          ty += PropPanel::kRowH;

          // Walk widgets; field headers + value labels are derived from layout y.
          // Note box (first widget).
          {
            const auto& w = L.widgets[0];
            bool editing = (task_tree_view.prop_text_edit_field_ == -2);
            quad(w.x1, w.y1, w.x2, w.y2, 0.0f, 0.0f, 0.0f, 0.40f);
            border(w.x1, w.y1, w.x2, w.y2, 1.0f,
                   editing ? 0.95f : 1.0f, editing ? 0.2f : 1.0f);
            draw_edit_box(w, -2, obj.name, false);
          }

          // Shared per-field renderer: used for the parent task AND each child so
          // both present the identical interface (pads, sliders, boxes). Reads from
          // / scopes edits to `tObj`, consuming widgets sequentially from `wi`.
          auto renderFields = [&](const LevelObject& tObj, const TaskSchema& tSchema,
                                  int tObjIdx, int& wi, int& y) {
            curObj = &tObj; curSchema = &tSchema; curObjIdx = tObjIdx;
            auto editVal = [&](int code){ return resolveEdit(tObjIdx) && task_tree_view.prop_text_edit_field_ == code; };
            auto dragVal = [&](int code){ return resolveDrag(tObjIdx) && task_tree_view.prop_field_index_ == code; };
            for (int fi = 0; fi < (int)tSchema.size(); ++fi) {
            const FieldDef& fd = tSchema[fi];
            const std::string& tn = fd.typeName;
            bool is_pos    = (tn == "ObjectPos");
            bool is_float3 = (tn == "Real32x3" || tn == "Real64x3");
            bool is_ori    = (tn == "Real32x9");
            bool is_rgb    = (tn == "RGB" || tn == "Colour");
            bool is_str    = (tn.find("String") != std::string::npos || tn == "VarString" ||
                              tn == "EnumString32" || tn == "DropDownCombo");
            bool is_bool   = (tn == "bool8" || tn == "PushButton");
            bool is_ro     = (tn == "Graph" || tn == "AnimData" || tn == "TrainPos1D");
            bool is_int    = (tn == "Int16" || tn == "Int32" || tn == "EnumInt32");

            // Field header line "Name (Type)(sub):"
            const char* sub = "";
            if (tn == "Angle")        sub = "(Angle)";
            else if (tn == "Degrees") sub = "(Degrees)";
            else if (tn == "PushButton") sub = "(Button)";
            char fhdr[160];
            if (sub[0]) snprintf(fhdr, sizeof(fhdr), "%s (%s)%s:", fd.name.c_str(), tn.c_str(), sub);
            else        snprintf(fhdr, sizeof(fhdr), "%s (%s):", fd.name.c_str(), tn.c_str());
            draw_text(L.panel_x + PropPanel::kPad, y + 11, fhdr, 1.0f, 0.9f, 0.2f);
            y += PropPanel::kRowH;

            // Helper: editable numeric box (NumBox) — label + box + caret.
            auto draw_numbox = [&](const PropPanel::Widget& w, const char* label) {
              int field_id = fi * 3 + w.comp;
              bool editing = editVal(field_id);
              if (label && label[0])
                draw_text(L.panel_x + PropPanel::kPad, w.y1 + 12, label, 1.0f, 0.9f, 0.2f);
              quad(w.x1, w.y1, w.x2, w.y2, 0.0f, 0.0f, 0.0f, 0.40f);
              border(w.x1, w.y1, w.x2, w.y2, 1.0f,
                     editing ? 0.95f : 1.0f, editing ? 0.2f : 1.0f);
              draw_edit_box(w, field_id, tok(fd.argOffset + w.comp), false);
            };

            if (is_pos) {
              // X/Y/Z editable numeric boxes.
              const char* lab[3] = {"X", "Y", "Z"};
              for (int c = 0; c < 3; ++c) {
                const auto& w = L.widgets[wi++];
                draw_numbox(w, lab[w.comp]);
              }
              // 2D pad
              const auto& pad = L.widgets[wi++];
              quad(pad.x1, pad.y1, pad.x2, pad.y2, 0.0f, 0.0f, 0.0f, 0.40f);
              border(pad.x1, pad.y1, pad.x2, pad.y2, 1.0f, 1.0f, 1.0f);
              {
                double px = 0, py = 0;
                if (editVal(fi * 3 + 0))
                  try { px = std::stod(task_tree_view.prop_text_buf_); } catch(...) {}
                else
                  try { px = std::stod(tok(fd.argOffset + 0)); } catch(...) {}
                if (editVal(fi * 3 + 1))
                  try { py = std::stod(task_tree_view.prop_text_buf_); } catch(...) {}
                else
                  try { py = std::stod(tok(fd.argOffset + 1)); } catch(...) {}
                int cx = pad.x1 + (pad.x2 - pad.x1) / 2;
                int cy = pad.y1 + (pad.y2 - pad.y1) / 2;
                int hx, hy;
                bool is_pad_dragging = (dragVal(fi * 3 + 0) || dragVal(fi * 3 + 1));
                if (is_pad_dragging) {
                  // During drag: show marker at cursor position relative to pad center
                  int half_w = (pad.x2 - pad.x1) / 2;
                  int half_h = (pad.y2 - pad.y1) / 2;
                  int raw_dx = task_tree_view.mouse_x_ - (pad.x1 + half_w);
                  int raw_dy = task_tree_view.mouse_y_ - (pad.y1 + half_h);
                  hx = cx + std::max(-half_w, std::min(half_w, raw_dx));
                  hy = cy + std::max(-half_h, std::min(half_h, raw_dy));
                } else {
                  hx = cx;  // not dragging: show at center
                  hy = cy;
                }
                quad(hx - 4, hy - 4, hx + 4, hy + 4, 1.0f, is_pad_dragging ? 0.95f : 0.85f, is_pad_dragging ? 0.2f : 0.0f, 1.0f);
              }
              // Z vertical slider
              const auto& zs = L.widgets[wi++];
              quad(zs.x1, zs.y1, zs.x2, zs.y2, 0.0f, 0.0f, 0.0f, 0.40f);
              border(zs.x1, zs.y1, zs.x2, zs.y2, 1.0f, 1.0f, 1.0f);
              {
                // Mirror the 2D pad: track the cursor within the slider during a
                // drag, centred when idle. The previous modulo-50 mapping wrapped
                // 0→1 every 50 world units, making the thumb flicker top↔bottom on
                // the huge absolute Z coordinates.
                bool drag = dragVal(fi * 3 + 2);
                int th;
                if (drag) {
                  int raw_dy = task_tree_view.mouse_y_ - zs.y1;
                  th = zs.y1 + std::max(0, std::min(zs.y2 - zs.y1, raw_dy));
                } else {
                  th = zs.y1 + (zs.y2 - zs.y1) / 2;
                }
                quad(zs.x1, th - 3, zs.x2, th + 3, 1.0f, drag ? 0.95f : 0.85f, drag ? 0.2f : 0.0f, 1.0f);
              }
              y = pad.y2 + 6;
              // Snap buttons
              const auto& bg = L.widgets[wi++];
              const auto& bo = L.widgets[wi++];
              quad(bg.x1, bg.y1, bg.x2, bg.y2, 0.0f, 0.0f, 0.0f, 0.40f);
              border(bg.x1, bg.y1, bg.x2, bg.y2, 1.0f, 1.0f, 1.0f);
              draw_text(bg.x1 + 6, bg.y1 + 12, "Snap to ground", 1.0f, 0.9f, 0.2f);
              quad(bo.x1, bo.y1, bo.x2, bo.y2, 0.0f, 0.0f, 0.0f, 0.40f);
              border(bo.x1, bo.y1, bo.x2, bo.y2, 1.0f, 1.0f, 1.0f);
              draw_text(bo.x1 + 6, bo.y1 + 12, "Snap to object", 1.0f, 0.9f, 0.2f);
              y = bg.y2 + 4;
              {
                char ab[80];
                snprintf(ab, sizeof(ab), "Altitude: %.6f meter", tObj.pos.z);
                draw_text(L.panel_x + PropPanel::kPad, y + 11, ab, 0.75f, 0.7f, 0.4f);
                y += PropPanel::kRowH;
              }
            } else if (is_float3) {
              // Generic 3-component float (Speed, etc.) — three labeled NumBoxes, no pad
              const char* lab[3] = {"X", "Y", "Z"};
              for (int c = 0; c < 3; ++c) {
                const auto& w = L.widgets[wi++];
                draw_numbox(w, lab[c]);
              }
              y = L.widgets[wi - 1].y2;
            } else if (is_ori) {
              // Count how many OriSlider widgets belong to this field (1 for AI-only Gamma, 3 otherwise).
              int ori_count = 0;
              {
                int tmp = wi;
                while (tmp < (int)L.widgets.size() &&
                       L.widgets[tmp].fieldIndex == fi &&
                       L.widgets[tmp].kind == PropPanel::WidgetKind::OriSlider)
                  ++tmp;
                ori_count = tmp - wi;
              }
              const char* lab[3] = {"A", "B", "G"};
              for (int ci = 0; ci < ori_count; ++ci) {
                const auto& w = L.widgets[wi++];
                // Short label on the far left
                draw_text(L.panel_x + PropPanel::kPad, w.y1 + 12, lab[w.comp], 1.0f, 0.9f, 0.2f);
                // Slider track ends 72px before right edge to leave room for value text
                int cy = (w.y1 + w.y2) / 2;
                int track_x2 = w.x2 - 72;
                quad(w.x1, cy - 2, track_x2, cy + 2, 0.0f, 0.0f, 0.0f, 0.40f);
                border(w.x1, cy - 2, track_x2, cy + 2, 1.0f, 1.0f, 1.0f);
                // Slider thumb
                float v = 0.f;
                if (editVal(fi * 3 + w.comp))
                  try { v = std::stof(task_tree_view.prop_text_buf_); } catch(...) {}
                else
                  try { v = std::stof(tok(fd.argOffset + w.comp)); } catch(...) {}
                float norm = std::max(0.f, std::min(1.f, (v + 3.14159f) / (2.f * 3.14159f)));
                int tx = w.x1 + (int)(norm * (track_x2 - w.x1 - 6));
                bool drag = dragVal(fi * 3 + w.comp);
                quad(tx, cy - 5, tx + 6, cy + 5, 1.0f, drag ? 0.95f : 0.85f, drag ? 0.2f : 0.0f, 1.0f);
                // Value text to the right of the slider
                draw_text(track_x2 + 4, w.y1 + 12, tok(fd.argOffset + w.comp).c_str(), 1.0f, 1.0f, 0.85f);
                y = w.y2;
              }
              if (ori_count == 0) y += PropPanel::kBoxH;  // fallback if no widgets
            } else if (is_rgb) {
              const char* rgbl[3] = {"R", "G", "B"};
              float rgb[3] = {0,0,0};
              for (int c = 0; c < 3; ++c) {
                const auto& w = L.widgets[wi++];
                draw_text(L.panel_x + PropPanel::kPad, w.y1 + 12, rgbl[w.comp], 1.0f, 0.9f, 0.2f);
                draw_text(L.panel_x + PropPanel::kPad + 24, w.y1 + 12,
                          tok(fd.argOffset + w.comp).c_str(), 1.0f, 1.0f, 0.85f);
                int cy = (w.y1 + w.y2) / 2;
                quad(w.x1, cy - 2, w.x2, cy + 2, 0.0f, 0.0f, 0.0f, 0.40f);
                border(w.x1, cy - 2, w.x2, cy + 2, 1.0f, 1.0f, 1.0f);
                float v = 0.f; try { v = std::stof(tok(fd.argOffset + w.comp)); } catch(...) {}
                rgb[w.comp] = std::max(0.f, std::min(1.f, v));
                float norm = rgb[w.comp];
                int tx = w.x1 + (int)(norm * (w.x2 - w.x1 - 6));
                bool drag = dragVal(fi * 3 + w.comp);
                quad(tx, cy - 5, tx + 6, cy + 5, 1.0f, drag ? 0.95f : 0.85f, drag ? 0.2f : 0.0f, 1.0f);
                // Swatch to the right of the last slider row.
                if (c == 2) {
                  int sw_x = w.x2 + 4, sw_y1 = L.widgets[wi - 3].y1, sw_y2 = w.y2;
                  quad(sw_x, sw_y1, sw_x + 18, sw_y2, rgb[0], rgb[1], rgb[2], 1.0f);
                  border(sw_x, sw_y1, sw_x + 18, sw_y2, 1.0f, 1.0f, 1.0f);
                }
                y = w.y2;
              }
            } else if (is_str) {
              const auto& w = L.widgets[wi++];
              bool multiline = (tn == "VarString" || tn == "String256");
              int field_id = fi * 3 + 0;
              bool editing = editVal(field_id);
              quad(w.x1, w.y1, w.x2, w.y2, 0.0f, 0.0f, 0.0f, 0.40f);
              border(w.x1, w.y1, w.x2, w.y2, 1.0f,
                     editing ? 0.95f : 1.0f, editing ? 0.2f : 1.0f);
              draw_edit_box(w, field_id, display_tok(fd.argOffset), multiline);
              y = w.y2 + 2;
            } else if (is_bool) {
              const auto& w = L.widgets[wi++];
              bool bv = false; try { bv = (std::stoi(tok(fd.argOffset)) != 0); } catch(...) {}
              // Draw a fixed 14×14 checkbox square; hit area is the full row (w.x1..w.x2)
              int cx2 = w.x1 + 14, cy2 = w.y1 + 14;
              quad(w.x1, w.y1, cx2, cy2, 0.0f, 0.0f, 0.0f, 0.40f);
              if (bv) quad(w.x1 + 2, w.y1 + 2, cx2 - 2, cy2 - 2, 1.0f, 1.0f, 1.0f, 0.9f);
              border(w.x1, w.y1, cx2, cy2, 1.0f, 1.0f, 1.0f);
              draw_text(cx2 + 6, w.y1 + 11, bv ? "TRUE" : "FALSE", 1.0f, 0.9f, 0.2f);
              y = w.y2;
            } else if (is_ro) {
              std::string v = tok(fd.argOffset);
              if (v.size() > 38) v = v.substr(0, 35) + "...";
              draw_text(L.panel_x + PropPanel::kPad + 4, y + 11, v.c_str(), 0.75f, 0.7f, 0.4f);
              y += PropPanel::kRowH;
            } else if (is_int) {
              const auto& w = L.widgets[wi++];
              draw_numbox(w, nullptr);
              y = w.y2;
            } else {
              // Real32/Real64/RangeReal32/Angle/Degrees: slider + editable box.
              const auto& sl = L.widgets[wi++];   // NumSlider
              const auto& bx = L.widgets[wi++];   // NumBox
              int cy = (sl.y1 + sl.y2) / 2;
              quad(sl.x1, cy - 2, sl.x2, cy + 2, 0.0f, 0.0f, 0.0f, 0.40f);
              border(sl.x1, cy - 2, sl.x2, cy + 2, 1.0f, 1.0f, 1.0f);
              float v = 0.f;
              if (editVal(fi * 3 + 0))
                try { v = std::stof(task_tree_view.prop_text_buf_); } catch(...) {}
              else
                try { v = std::stof(tok(fd.argOffset)); } catch(...) {}
              float norm = std::max(0.f, std::min(1.f, (v - std::floor(v / 200.f) * 200.f) / 200.f));
              int tx = sl.x1 + (int)(norm * (sl.x2 - sl.x1 - 6));
              bool drag = dragVal(fi * 3 + 0);
              quad(tx, cy - 5, tx + 6, cy + 5, 1.0f, drag ? 0.95f : 0.85f, drag ? 0.2f : 0.0f, 1.0f);
              draw_numbox(bx, nullptr);
              y = bx.y2;
            }
            y += 4;  // gap between fields
            }
          }; // end renderFields

          // Render the parent task fields, then each child task with the SAME
          // interface. Child fields are routed to their own LevelObject via the
          // widget objIndex / resolveEdit / resolveDrag, so weapons/AI/EditRigidObj
          // children get pads, sliders and boxes exactly like a top-level task.
          int wi = 1;                        // widget 0 is the parent note box
          int y  = L.widgets[0].y2 + 6;
          renderFields(obj, schema, sel, wi, y);
          for (const auto& [ci, cscp] : child_schemas) {
              if (wi < (int)L.widgets.size() &&
                  L.widgets[wi].kind == PropPanel::WidgetKind::ChildHeader) {
                  const auto& hw = L.widgets[wi++];
                  char sep[96];
                  snprintf(sep, sizeof(sep), "%s", objects[ci].type.c_str());
                  draw_text(hw.x1, hw.y1 + 11, sep, 0.45f, 0.85f, 1.0f);
                  y = hw.y2 + 2;
              }
              renderFields(objects[ci], *cscp, ci, wi, y);
              y += 4;
          }

          // AI script widgets (appended after child fields for AI task types)
          curObjIdx = sel;  // resolveEdit must see sel so edit state is applied correctly
          while (wi < (int)L.widgets.size()) {
              using K = PropPanel::WidgetKind;
              const auto& w = L.widgets[wi++];
              if (w.kind == K::AIScriptPath) {
                  bool ed = resolveEdit(sel) &&
                            task_tree_view.prop_text_edit_field_ == PropPanel::kAIScriptPathField;
                  draw_text(w.x1, w.y1 - PropPanel::kRowH + 12, "AI Script Path:", 0.8f, 0.8f, 1.0f);
                  quad(w.x1, w.y1, w.x2, w.y2, 0.0f, 0.0f, 0.0f, 0.40f);
                  border(w.x1, w.y1, w.x2, w.y2, 1.0f, ed ? 0.95f : 1.0f, ed ? 0.2f : 1.0f);
                  draw_edit_box(w, PropPanel::kAIScriptPathField,
                                task_tree_view.ai_script_path_, false,
                                0, task_tree_view.ai_script_path_hscroll_);
              } else if (w.kind == K::AIScriptText) {
                  bool ed = resolveEdit(sel) &&
                            task_tree_view.prop_text_edit_field_ == PropPanel::kAIScriptTextField;
                  const char* label = task_tree_view.ai_script_dirty_
                                          ? "AI Script (modified -- save to compile):"
                                          : "AI Script:";
                  draw_text(w.x1, w.y1 - PropPanel::kRowH + 12, label,
                            task_tree_view.ai_script_dirty_ ? 1.0f : 0.8f,
                            task_tree_view.ai_script_dirty_ ? 0.6f : 0.8f,
                            task_tree_view.ai_script_dirty_ ? 0.2f : 1.0f);
                  quad(w.x1, w.y1, w.x2, w.y2, 0.0f, 0.0f, 0.0f, 0.40f);
                  border(w.x1, w.y1, w.x2, w.y2, 1.0f, ed ? 0.95f : 1.0f, ed ? 0.2f : 1.0f);
                  draw_edit_box(w, PropPanel::kAIScriptTextField,
                                task_tree_view.ai_script_text_, true,
                                task_tree_view.ai_script_vscroll_, 0);
              }
              y = w.y2 + 6;
          }

          // ── Scrollbar ────────────────────────────────────────────────────────────
          {
              const int total_h = L.panel_h;   // full unshifted content height
              const int vis_h   = panel_vis_h;
              if (total_h > vis_h) {
                  const int bar_x = PropPanel::kLeft + PropPanel::kWidth + 2;
                  const int bar_w = 6;
                  // Track
                  glColor4f(0.1f, 0.1f, 0.1f, 0.8f);
                  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                  glBegin(GL_QUADS);
                  glVertex2i(bar_x, gl_y(PropPanel::kTop)); glVertex2i(bar_x+bar_w, gl_y(PropPanel::kTop));
                  glVertex2i(bar_x+bar_w, gl_y(PropPanel::kTop + vis_h)); glVertex2i(bar_x, gl_y(PropPanel::kTop + vis_h));
                  glEnd();
                  // Thumb
                  float thumb_f = (float)vis_h / (float)total_h;
                  int thumb_h  = std::max(20, (int)(vis_h * thumb_f));
                  int thumb_y  = PropPanel::kTop + (int)((float)scroll / (float)(total_h - vis_h) * (vis_h - thumb_h));
                  glColor4f(0.3f, 0.7f, 1.0f, 0.9f);
                  glBegin(GL_QUADS);
                  glVertex2i(bar_x, gl_y(thumb_y)); glVertex2i(bar_x+bar_w, gl_y(thumb_y));
                  glVertex2i(bar_x+bar_w, gl_y(thumb_y + thumb_h)); glVertex2i(bar_x, gl_y(thumb_y + thumb_h));
                  glEnd();
                  glDisable(GL_BLEND);
              }
          }

          glDisable(GL_SCISSOR_TEST);
        }
      }
    }

    // ── C3: Ctrl+F find bar (centered on screen) ───────────────────────────────
    if (task_tree_view.find_open_) {
      int vw = params.view_define_->viewport_width_;
      int vh = params.view_define_->viewport_height_;

      // Centered 400x80 box
      const int bar_w      = 400;
      const int bar_h      = 80;
      const int bar_x      = vw / 2 - bar_w / 2;
      const int bar_gl_y   = vh / 2 - bar_h / 2;   // GL bottom-up origin

      // Dark semi-transparent background
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(0.05f, 0.05f, 0.1f, 0.92f);
      glBegin(GL_QUADS);
      glVertex2i(bar_x,         bar_gl_y);
      glVertex2i(bar_x + bar_w, bar_gl_y);
      glVertex2i(bar_x + bar_w, bar_gl_y + bar_h);
      glVertex2i(bar_x,         bar_gl_y + bar_h);
      glEnd();

      // White border
      glColor4f(1.0f, 1.0f, 1.0f, 0.85f);
      glBegin(GL_LINE_LOOP);
      glVertex2i(bar_x,         bar_gl_y);
      glVertex2i(bar_x + bar_w, bar_gl_y);
      glVertex2i(bar_x + bar_w, bar_gl_y + bar_h);
      glVertex2i(bar_x,         bar_gl_y + bar_h);
      glEnd();
      glDisable(GL_BLEND);

      // Convert GL y to screen top-down y for draw_text
      // bar top (screen-top-down) = vh - (bar_gl_y + bar_h)
      int bar_screen_top = vh - (bar_gl_y + bar_h);

      // Title — shows current find mode
      {
        static const char* kTitles[] = {
            "Find task by type / name / ID:",
            "Find text in task parameters:",
            "Find task by ID:",
            "Find task by note / name:",
            "Set Task ID (empty = auto-assign):"
        };
        int mi = task_tree_view.find_mode_;
        if (mi < 0 || mi > 4) mi = 0;
        const char* title = kTitles[mi];
        int tw = glutBitmapLength(GLUT_BITMAP_HELVETICA_12, (const unsigned char*)title);
        draw_text(bar_x + (bar_w - tw) / 2, bar_screen_top + 14, title, 1.0f, 1.0f, 1.0f);
      }

      // Input box background
      {
        int ibx1 = bar_x + 8;
        int ibx2 = bar_x + bar_w - 8;
        int iby1 = bar_gl_y + 10;
        int iby2 = bar_gl_y + 36;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.1f, 0.1f, 0.15f, 0.95f);
        glBegin(GL_QUADS);
        glVertex2i(ibx1, iby1); glVertex2i(ibx2, iby1);
        glVertex2i(ibx2, iby2); glVertex2i(ibx1, iby2);
        glEnd();
        glColor4f(0.7f, 0.7f, 0.7f, 0.9f);
        glBegin(GL_LINE_LOOP);
        glVertex2i(ibx1, iby1); glVertex2i(ibx2, iby1);
        glVertex2i(ibx2, iby2); glVertex2i(ibx1, iby2);
        glEnd();
        glDisable(GL_BLEND);
      }

      // Search text with cursor
      char find_label[256];
      snprintf(find_label, sizeof(find_label), "%s_", task_tree_view.find_query_.c_str());
      // screen y for text inside input box: bar_screen_top + bar_h - 10 - 26 + 16 = bar_screen_top+20
      int input_text_y = bar_screen_top + bar_h - 10 - (bar_h - 36) - 10;
      draw_text(bar_x + 12, input_text_y, find_label, 1.0f, 1.0f, 1.0f);

      // Match / no-match feedback below the input box.
      if (task_tree_view.find_mode_ == 4) {
        // SetId: input-only, no search — show a hint instead of match feedback.
        draw_text(bar_x + 12, bar_screen_top + 68, "[Enter] apply  (empty = auto-assign)", 0.7f, 0.7f, 0.7f);
      } else if (task_tree_view.find_result_idx_ >= 0 && task_tree_view.level_objects_) {
        const auto& objects = task_tree_view.level_objects_->GetObjects();
        int ri = task_tree_view.find_result_idx_;
        if (ri < (int)objects.size()) {
          char match_buf[160];
          snprintf(match_buf, sizeof(match_buf), "%s \"%s\" (ID:%s)  [Enter]",
                   objects[ri].type.c_str(),
                   objects[ri].name.c_str(),
                   objects[ri].taskId.empty() ? "-1" : objects[ri].taskId.c_str());
          draw_text(bar_x + 12, bar_screen_top + 68, match_buf, 0.3f, 1.0f, 0.3f);
        }
      } else if (!task_tree_view.find_query_.empty()) {
        draw_text(bar_x + 12, bar_screen_top + 68, "No match found", 1.0f, 0.4f, 0.4f);
      }
    }

    // ── File dialog (SaveSubTask / LoadSubTask) ─────────────────────────────
    if (task_tree_view.file_dialog_mode_ != 0) {
      int vw = params.view_define_->viewport_width_;
      int vh = params.view_define_->viewport_height_;
      const int dw = 560, dh = 110;
      const int dx = vw / 2 - dw / 2;
      const int dgl = vh / 2 - dh / 2;
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(0.05f, 0.05f, 0.1f, 0.92f);
      glBegin(GL_QUADS);
      glVertex2i(dx,dgl); glVertex2i(dx+dw,dgl); glVertex2i(dx+dw,dgl+dh); glVertex2i(dx,dgl+dh);
      glEnd();
      glColor4f(1.0f, 0.85f, 0.0f, 0.7f);
      glBegin(GL_LINE_LOOP);
      glVertex2i(dx,dgl); glVertex2i(dx+dw,dgl); glVertex2i(dx+dw,dgl+dh); glVertex2i(dx,dgl+dh);
      glEnd();
      glDisable(GL_BLEND);
      int dsy = vh - (dgl + dh); // screen top
      static const char* kDlgTitles[] = {"", "Save Task File:", "Save Parent Task File:", "Load Task File:", "Save Objects File:"};
      int mi = task_tree_view.file_dialog_mode_;
      if (mi < 0 || mi > 4) mi = 0;
      draw_text(dx + 10, dsy + 14, kDlgTitles[mi], 1.0f, 0.9f, 0.1f);
      // Input box
      const int ibx1 = dx + 8, ibx2 = dx + dw - 8;
      const int iby1 = dgl + 14, iby2 = dgl + 38;
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(0.1f, 0.1f, 0.15f, 0.95f);
      glBegin(GL_QUADS); glVertex2i(ibx1,iby1); glVertex2i(ibx2,iby1); glVertex2i(ibx2,iby2); glVertex2i(ibx1,iby2); glEnd();
      glColor4f(1.0f, 0.85f, 0.0f, 0.7f);
      glBegin(GL_LINE_LOOP); glVertex2i(ibx1,iby1); glVertex2i(ibx2,iby1); glVertex2i(ibx2,iby2); glVertex2i(ibx1,iby2); glEnd();
      glDisable(GL_BLEND);
      int ity = vh - (iby2 + 14);
      char pathbuf[512]; snprintf(pathbuf, sizeof(pathbuf), "%s_", task_tree_view.file_dialog_path_.c_str());
      draw_text(ibx1 + 4, ity, pathbuf, 1.0f, 1.0f, 1.0f);
      draw_text(dx + 10, dsy + dh - 18, "[Enter] Confirm   [Esc] Cancel", 0.6f, 0.6f, 0.6f);
    }

    // ── Autocomplete task picker (right panel, Ctrl+N) ───────────────────────
    if (task_tree_view.ac_task_picker_open_ && task_tree_view.ac_task_items_) {
      int vw = params.view_define_->viewport_width_;
      int vh = params.view_define_->viewport_height_;
      const int pw = 280, px = vw - pw;
      // Build filtered list
      std::vector<std::string> filtered;
      std::string fl = task_tree_view.ac_task_filter_;
      std::transform(fl.begin(), fl.end(), fl.begin(), [](unsigned char c){ return std::tolower(c); });
      for (const auto& item : *task_tree_view.ac_task_items_) {
        if (fl.empty()) { filtered.push_back(item); }
        else {
          std::string il = item;
          std::transform(il.begin(), il.end(), il.begin(), [](unsigned char c){ return std::tolower(c); });
          if (il.find(fl) != std::string::npos) filtered.push_back(item);
        }
      }
      int count = (int)filtered.size();
      const int row_h = 16, hdr_h = 50, ftr_h = 20;
      int body_h = vh - hdr_h - ftr_h;
      int max_vis = std::max(1, body_h / row_h);

      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(0.0f, 0.0f, 0.0f, 0.45f);
      glBegin(GL_QUADS); glVertex2i(px,0); glVertex2i(px+pw,0); glVertex2i(px+pw,vh); glVertex2i(px,vh); glEnd();
      glColor4f(1.0f, 0.85f, 0.0f, 0.7f);
      glBegin(GL_LINE_LOOP); glVertex2i(px,0); glVertex2i(px+pw,0); glVertex2i(px+pw,vh); glVertex2i(px,vh); glEnd();
      glDisable(GL_BLEND);

      draw_text(px + 8, 14, "Task Types", 1.0f, 0.9f, 0.1f);

      // Filter box
      const int fbx1 = px + 4, fbx2 = px + pw - 4, fby1 = vh - hdr_h + 4, fby2 = vh - hdr_h + 22;
      glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(0.1f, 0.1f, 0.15f, 0.9f);
      glBegin(GL_QUADS); glVertex2i(fbx1,fby1); glVertex2i(fbx2,fby1); glVertex2i(fbx2,fby2); glVertex2i(fbx1,fby2); glEnd();
      glColor4f(1.0f, 0.85f, 0.0f, 0.6f);
      glBegin(GL_LINE_LOOP); glVertex2i(fbx1,fby1); glVertex2i(fbx2,fby1); glVertex2i(fbx2,fby2); glVertex2i(fbx1,fby2); glEnd();
      glDisable(GL_BLEND);
      char fb[64]; snprintf(fb, sizeof(fb), "%s_", task_tree_view.ac_task_filter_.c_str());
      int fty = vh - (fby2) + 4;
      draw_text(fbx1 + 4, fty, fb, 1.0f, 1.0f, 1.0f);

      // Items
      int sel = task_tree_view.ac_task_selected_idx_;
      int scroll = task_tree_view.ac_task_scroll_offset_;
      for (int r = 0; r < max_vis; ++r) {
        int idx = scroll + r;
        if (idx >= count) break;
        int item_sy = hdr_h + r * row_h;
        if (idx == sel) {
          glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
          glColor4f(1.0f, 0.85f, 0.0f, 0.35f);
          glBegin(GL_QUADS);
          int gy1 = vh - item_sy - row_h, gy2 = vh - item_sy;
          glVertex2i(px,gy1); glVertex2i(px+pw,gy1); glVertex2i(px+pw,gy2); glVertex2i(px,gy2);
          glEnd(); glDisable(GL_BLEND);
          draw_text(px + 4, item_sy + 13, filtered[idx].c_str(), 1.0f, 0.9f, 0.1f);
        } else {
          draw_text(px + 4, item_sy + 13, filtered[idx].c_str(), 1.0f, 1.0f, 1.0f);
        }
      }
      draw_text(px + 4, vh - ftr_h + 4, "[Enter] Insert  [Esc] Cancel  [Type] Filter", 0.5f, 0.5f, 0.5f);
    }

    // ── Model ID picker (right panel, Ctrl+O) ───────────────────────────────
    if (task_tree_view.model_picker_open_ && task_tree_view.model_ids_) {
      int vw = params.view_define_->viewport_width_;
      int vh = params.view_define_->viewport_height_;
      const int pw = 280, px = vw - pw;
      // Build filtered sorted list
      std::vector<std::string> filtered;
      std::string fl = task_tree_view.model_picker_filter_;
      std::transform(fl.begin(), fl.end(), fl.begin(), [](unsigned char c){ return std::tolower(c); });
      for (const auto& id : *task_tree_view.model_ids_) {
        if (fl.empty()) { filtered.push_back(id); }
        else {
          std::string idl = id;
          std::transform(idl.begin(), idl.end(), idl.begin(), [](unsigned char c){ return std::tolower(c); });
          if (idl.find(fl) != std::string::npos) filtered.push_back(id);
        }
      }
      int count = (int)filtered.size();
      const int row_h = 16, hdr_h = 50, ftr_h = 20;
      int body_h = vh - hdr_h - ftr_h;
      int max_vis = std::max(1, body_h / row_h);

      // 3D rotating preview of the highlighted model, centered in the editor area
      // (to the left of the picker panel). Drawn first, then 2D HUD state restored.
      {
        int sel0 = task_tree_view.model_picker_selected_;
        if (count > 0 && sel0 >= 0 && sel0 < count) {
          int avail = vw - pw;                       // editor area width (panel excluded)
          int s = (int)(std::min(avail, vh) * 0.6f); // square preview side
          if (s > 64) {
            int vpX = (avail - s) / 2;
            int vpY = (vh - s) / 2;
            static auto t0 = std::chrono::steady_clock::now();
            float t = std::chrono::duration<float>(
                          std::chrono::steady_clock::now() - t0).count();
            objects_.DrawModelPreview(filtered[sel0], ubo_mats_, vpX, vpY, s, s,
                                      t * 0.40f, t * 0.65f); // slow dual-axis spin
            // Restore the EXACT 2D HUD baseline (see the state set before the overlay
            // block). Critically, GL_TEXTURE_2D must be DISABLED with no texture bound,
            // otherwise the panel's colored quads sample the model texture and the whole
            // picker flickers black/golden as the selection changes.
            glViewport(0, 0, vw, vh);
            glUseProgram(0);
            glBindVertexArray(0);
            glBindBufferBase(GL_UNIFORM_BUFFER, 0, 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glDisable(GL_TEXTURE_2D);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);
            glDisable(GL_LIGHTING);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
          }
        }
      }

      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(0.0f, 0.0f, 0.0f, 0.45f);
      glBegin(GL_QUADS); glVertex2i(px,0); glVertex2i(px+pw,0); glVertex2i(px+pw,vh); glVertex2i(px,vh); glEnd();
      glColor4f(1.0f, 0.85f, 0.0f, 0.7f);
      glBegin(GL_LINE_LOOP); glVertex2i(px,0); glVertex2i(px+pw,0); glVertex2i(px+pw,vh); glVertex2i(px,vh); glEnd();
      glDisable(GL_BLEND);

      draw_text(px + 8, 14, "Model IDs", 1.0f, 0.9f, 0.1f);

      // Filter box
      const int fbx1 = px + 4, fbx2 = px + pw - 4, fby1 = vh - hdr_h + 4, fby2 = vh - hdr_h + 22;
      glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(0.1f, 0.1f, 0.15f, 0.9f);
      glBegin(GL_QUADS); glVertex2i(fbx1,fby1); glVertex2i(fbx2,fby1); glVertex2i(fbx2,fby2); glVertex2i(fbx1,fby2); glEnd();
      glColor4f(1.0f, 0.85f, 0.0f, 0.6f);
      glBegin(GL_LINE_LOOP); glVertex2i(fbx1,fby1); glVertex2i(fbx2,fby1); glVertex2i(fbx2,fby2); glVertex2i(fbx1,fby2); glEnd();
      glDisable(GL_BLEND);
      char fb[64]; snprintf(fb, sizeof(fb), "%s_", task_tree_view.model_picker_filter_.c_str());
      int fty = vh - (fby2) + 4;
      draw_text(fbx1 + 4, fty, fb, 1.0f, 1.0f, 1.0f);

      // Items
      int sel = task_tree_view.model_picker_selected_;
      int scroll = task_tree_view.model_picker_scroll_;
      for (int r = 0; r < max_vis; ++r) {
        int idx = scroll + r;
        if (idx >= count) break;
        int item_sy = hdr_h + r * row_h;
        if (idx == sel) {
          glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
          glColor4f(1.0f, 0.85f, 0.0f, 0.35f);
          glBegin(GL_QUADS);
          int gy1 = vh - item_sy - row_h, gy2 = vh - item_sy;
          glVertex2i(px,gy1); glVertex2i(px+pw,gy1); glVertex2i(px+pw,gy2); glVertex2i(px,gy2);
          glEnd(); glDisable(GL_BLEND);
          draw_text(px + 4, item_sy + 13, filtered[idx].c_str(), 1.0f, 0.9f, 0.1f);
        } else {
          draw_text(px + 4, item_sy + 13, filtered[idx].c_str(), 1.0f, 1.0f, 1.0f);
        }
      }
      draw_text(px + 4, vh - ftr_h + 4, "[Enter] Insert  [Esc] Cancel  [Type] Filter", 0.5f, 0.5f, 0.5f);
    }

    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
    glMatrixMode(GL_MODELVIEW);
    glPopMatrix();

    // Restore all states
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
  }

  GL_CHECK_ERROR;

  glFlush();
}

void Renderer::SetupUBOMats(const view_define_s &vd) {
  glm::mat4 mat_proj_persp = glm::perspective(
      vd.fovy_, (float)vd.viewport_width_ / vd.viewport_height_,
      vd.render_z_near_, vd.render_z_far_);

  glm::vec3 scaled_down_pos = vd.pos_ * RENDERER_MODEL_SCALE_DOWN;
  glm::mat4 mat_view = glm::lookAt(
      scaled_down_pos, scaled_down_pos + vd.forward_ * WORLD_UNITS_PER_METER,
      vd.up_);
  glm::mat4 mat_follow_view = glm::lookAt(VEC3_ORIGIN, vd.forward_, vd.up_);
  glm::mat4 mat_scale =
      glm::scale(glm::mat4(1.0f), glm::vec3(RENDERER_MODEL_SCALE_DOWN));

  mat_proj_ = mat_proj_persp;
  mat_view_ = mat_view;

  // setup uniform buffer

  ubo_mats_s ubo_mats;

  // skydome
  ubo_mats.mvp_mat_follow_view_ = mat_proj_persp * mat_follow_view * mat_scale;

  // flat sky layer
  ubo_mats.mvp_flat_sky_layer_ =
      glm::ortho(0.0f, (float)vd.viewport_width_, 0.0f,
                 (float)vd.viewport_height_, -1.0f, 1.0f);

  // terrain
  ubo_mats.mvp_objects_ = mat_proj_persp * mat_view * mat_scale;

  // flush ubo buffer
  GL_BufferData(ubo_mats_, GL_UNIFORM_BUFFER, sizeof(ubo_mats), &ubo_mats,
                GL_DYNAMIC_DRAW);
}
