/******************************************************************************
 * @file    renderer.cpp
 * @brief   main renderer
 *   GL 4.1: need manually set binding point of uniform blocks
 *                             texture unit of sample object
 *   GL 4.5: binding point, texture unit can specified in shaders
 *****************************************************************************/

#include "config.h"
#include "logger.h"
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
static void DrawFontText(int x, int y_gl, const char* str, float r, float g, float b) {
  if (!g_editorFont.valid || !g_editorFontTex) {
    return;
  }

  const float texW = (float)g_editorFont.texWidth;
  (void)texW;
  const int spaceAdvance = g_editorFont.lineHeight > 0 ? g_editorFont.lineHeight / 2 : 4;

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, g_editorFontTex);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColor4f(r, g, b, 1.0f);

  int pen_x = x;
  int pen_y = y_gl; // top of current line in gl space (y up)

  glBegin(GL_QUADS);
  for (const char* p = str; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    if (c == '\n') {
      pen_x = x;
      pen_y -= g_editorFont.lineHeight;
      continue;
    }

    auto it = g_editorFont.glyphs.find((int)c);
    if (it == g_editorFont.glyphs.end()) {
      pen_x += spaceAdvance; // unknown char -> advance like a space
      continue;
    }

    const FntGlyph& gl = it->second;
    float x0 = (float)pen_x;
    float x1 = (float)(pen_x + gl.width);
    float yTop = (float)pen_y;             // y up: top of glyph
    float yBot = (float)(pen_y - gl.height);

    // Atlas V grows downward; gl V grows upward -> top of glyph uses v0.
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

  if (task_tree_view.show_hud_) {
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
          DrawFontText(x + 1, y_gl - 1, line.c_str(), 0.0f, 0.0f, 0.0f);
          DrawFontText(x, y_gl, line.c_str(), r, g, b);
          line_y += 15; // Vertical spacing
        }
        return;
      }

      // Fallback: GLUT bitmap font.
      std::stringstream ss(str);
      std::string line;
      int line_y = y;
      while (std::getline(ss, line)) {
        // Draw shadow
        glColor3f(0.0f, 0.0f, 0.0f);
        glRasterPos2i(x + 1,
                      params.view_define_->viewport_height_ - line_y - 1);
        for (char c : line)
          glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, c);

        // Draw main text
        glColor3f(r, g, b);
        glRasterPos2i(x, params.view_define_->viewport_height_ - line_y);
        for (char c : line)
          glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, c);

        line_y += 15; // Vertical spacing
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
    auto draw_text_sys = [&](int x, int y, const char *str, float r, float g,
                             float b) {
      std::stringstream ss(str);
      std::string line;
      int line_y = y;
      while (std::getline(ss, line)) {
        glColor3f(0.0f, 0.0f, 0.0f);
        glRasterPos2i(x + 1, params.view_define_->viewport_height_ - line_y - 1);
        for (char c : line) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, c);
        glColor3f(r, g, b);
        glRasterPos2i(x, params.view_define_->viewport_height_ - line_y);
        for (char c : line) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, c);
        line_y += 15;
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

    // --- TreeView HUD Implementation ---
    // Hidden while the property panel occupies the left strip.
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

    // Show Editor/Object Telemetry (Only if debug is enabled and explicitly
    // requested) Removed live info as requested

    // Display object info at mouse position
    int info_object_index = task_tree_view.hover_object_index_;
    if (!task_tree_view.status_msg_.empty() &&
        task_tree_view.selected_object_index_ >= 0) {
      info_object_index = task_tree_view.selected_object_index_;
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

    int tooltip_x = task_tree_view.mouse_x_;
    int tooltip_y = task_tree_view.mouse_y_ + 25;



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
        std::string display_name = obj.name;
        if (display_name.empty())
          display_name = obj.type;
        if (display_name.empty())
          display_name = obj.modelId;

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

          std::string teamStr =
              (obj.team == 1) ? "Enemy"
                              : ((obj.team == 0) ? "Friendly" : "Neutral");
          float tr = (obj.team == 1) ? 1.0f : 0.2f;
          float tg = (obj.team == 1) ? 0.2f : 1.0f;
          float tb = 0.2f;
          snprintf(buf, sizeof(buf), "Team: %s", teamStr.c_str());
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
          draw_text_sm(text_x, text_y, buf, obj.isBuilding ? 1.0f : 0.0f, 1.0f,
                    0.0f);
          text_y += 15;
        }

        snprintf(buf, sizeof(buf), "Pos: X: %.1f Y: %.1f Z: %.1f", obj.pos.x,
                 obj.pos.y, obj.pos.z);
        draw_text_sm(text_x, text_y, buf, 0.7f, 0.7f, 0.7f);
        text_y += 15;

        if (!task_tree_view.status_msg_.empty() &&
            info_object_index == task_tree_view.selected_object_index_) {
          draw_text_sm(text_x, text_y, task_tree_view.status_msg_.c_str(), 0.85f,
                    1.0f, 0.6f);
        }
      }
    } else if (!task_tree_view.pause_mode_ && (!task_tree_view.show_hud_ || task_tree_view.mouse_x_ >= 350)) {
      draw_text_sm(tooltip_x, tooltip_y, "Terrain ID: -1", 1.0f, 1.0f, 1.0f);
    }

    // Watermark
    int w_width = glutBitmapLength(
        GLUT_BITMAP_HELVETICA_12,
        (const unsigned char *)"IGI Editor Copyright - HeavenHM");
    int w_x = (params.view_define_->viewport_width_ - w_width) / 2;
    draw_text(w_x, params.view_define_->viewport_height_ - 20, "IGI Editor - HeavenHM", 0.7f, 0.7f, 0.7f);

    if (task_tree_view.task_picker_open_ && task_tree_view.level_objects_) {
      int picker_x = 20;
      int picker_w = 320;
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
      glColor4f(0.06f, 0.06f, 0.09f, 0.85f);
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
      draw_text(picker_x + 15, 38, "SELECT SUBTREE TO CLONE", 1.0f, 0.9f, 0.1f); // Vibrant yellow/gold

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
                "[Enter] Clone  [ESC] Cancel  [Type] Search", 0.7f, 0.7f, 0.7f);

      // Build task list mapping exactly like app.cpp
      const auto &objects = task_tree_view.level_objects_->GetObjects();
      std::vector<int> picker_to_objects;

      std::string search_lower = task_tree_view.task_picker_search_;
      std::transform(search_lower.begin(), search_lower.end(),
                     search_lower.begin(),
                     [](unsigned char c) { return std::tolower(c); });

      for (int i = 0; i < (int)objects.size(); ++i) {
        if (!objects[i].deleted) {
          const auto &obj = objects[i];
          std::string label = obj.type;
          if (!obj.taskId.empty() && obj.taskId != "-1") {
            label += " (" + obj.taskId;
            if (!obj.name.empty())
              label += ", \"" + obj.name + "\"";
            label += ")";
          } else if (!obj.name.empty()) {
            label += " (\"" + obj.name + "\")";
          }

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

          // Format label standard to HUD: Type (ID, "Name")
          std::string label = obj.type;
          if (!obj.taskId.empty() && obj.taskId != "-1") {
            label += " (" + obj.taskId;
            if (!obj.name.empty())
              label += ", \"" + obj.name + "\"";
            label += ")";
          } else if (!obj.name.empty()) {
            label += " (\"" + obj.name + "\")";
          }

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


    if (task_tree_view.edit_mode_) {
      // Flip Y because glOrtho has y=0 at bottom, but mouse_y is top-down
      // (GLUT)
      float cx = (float)(task_tree_view.mouse_x_);
      float cy = (float)(params.view_define_->viewport_height_ -
                         task_tree_view.mouse_y_);

      if (task_tree_view.enable_camera_mode_) {
        // Draw camera icon at center (since mouse is warped there)
        float ccx = (float)(params.view_define_->viewport_width_ / 2);
        float ccy = (float)(params.view_define_->viewport_height_ / 2);
        float w = 16.0f, h = 10.0f;
        glColor3f(0.4f, 0.7f, 1.0f); // Sky blue
        glLineWidth(2.5f);
        // Camera body
        glBegin(GL_LINE_LOOP);
        glVertex2f(ccx - w / 2, ccy - h / 2);
        glVertex2f(ccx + w / 2, ccy - h / 2);
        glVertex2f(ccx + w / 2, ccy + h / 2);
        glVertex2f(ccx - w / 2, ccy + h / 2);
        glEnd();
        // Lens (inner circle-ish)
        glBegin(GL_LINE_LOOP);
        for (int i = 0; i < 8; ++i) {
          float a = i * 6.28f / 8.0f;
          glVertex2f(ccx + cosf(a) * 3, ccy + sinf(a) * 3);
        }
        glEnd();
        // Viewfinder
        glBegin(GL_LINE_LOOP);
        glVertex2f(ccx - 3, ccy + h / 2);
        glVertex2f(ccx + 3, ccy + h / 2);
        glVertex2f(ccx + 4, ccy + h / 2 + 3);
        glVertex2f(ccx - 4, ccy + h / 2 + 3);
        glEnd();
        glLineWidth(1.0f);
      } else if (task_tree_view.terrain_edit_enabled_) {
        // Terrain edit mode: orange circle brush cursor
        float radius = 10.0f;
        float th = 2.0f;
        glColor3f(1.0f, 0.5f, 0.0f); // Orange
        glLineWidth(th);
        glBegin(GL_LINE_LOOP);
        int segments = 16;
        for (int i = 0; i < segments; ++i) {
          float angle = (float)i * 6.283185f / (float)segments;
          glVertex2f(cx + cosf(angle) * radius, cy + sinf(angle) * radius);
        }
        glEnd();
        // Small cross inside circle
        float csz = 4.0f;
        glBegin(GL_LINES);
        glVertex2f(cx - csz, cy);
        glVertex2f(cx + csz, cy);
        glVertex2f(cx, cy - csz);
        glVertex2f(cx, cy + csz);
        glEnd();
        glLineWidth(1.0f);
      } else {
        // Object edit mode: we rely on the OS cursor instead of a drawn +
      }
    } else {
      // Draw a small blue camera icon at the screen center
      float cx = (float)(params.view_define_->viewport_width_ / 2 + 12);
      float cy = (float)(params.view_define_->viewport_height_ / 2 + 12);
      float w = 12.0f, h = 8.0f;
      glColor3f(0.4f, 0.7f, 1.0f);
      glLineWidth(2.0f);
      // Camera body
      glBegin(GL_LINE_LOOP);
      glVertex2f(cx - w / 2, cy - h / 2);
      glVertex2f(cx + w / 2, cy - h / 2);
      glVertex2f(cx + w / 2, cy + h / 2);
      glVertex2f(cx - w / 2, cy + h / 2);
      glEnd();
      // Lens (inner rect)
      glBegin(GL_LINE_LOOP);
      glVertex2f(cx - w / 5, cy - h / 5);
      glVertex2f(cx + w / 5, cy - h / 5);
      glVertex2f(cx + w / 5, cy + h / 5);
      glVertex2f(cx - w / 5, cy + h / 5);
      glEnd();
      // Viewfinder bump on top
      glBegin(GL_LINE_LOOP);
      glVertex2f(cx - w / 5, cy - h / 2);
      glVertex2f(cx + w / 5, cy - h / 2);
      glVertex2f(cx + w / 5 + 2, cy - h / 2 - 4);
      glVertex2f(cx - w / 5 - 2, cy - h / 2 - 4);
      glEnd();
      glLineWidth(1.0f);
    }

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
      draw_text_sys(menu_x + menu_w / 2 - 45, screen_menu_top + 18, "IGI EDITOR",
                0.0f, 1.0f, 0.0f);
      draw_text_sys(menu_x + menu_w / 2 - 35, screen_menu_top + 32, "PAUSED", 0.8f,
                0.8f, 0.8f);

      const char *btn_labels[] = {"Resume", "Debug", "Reset Level",
                                  "Save Level", "Quit"};
      const int NUM_BTNS = 5;

      for (int i = 0; i < NUM_BTNS; ++i) {
        int screen_btn_y = screen_menu_top + 80 + i * 35;
        int gl_btn_y = viewport_h - screen_btn_y;

        bool hovered = (task_tree_view.mouse_x_ >= menu_x &&
                        task_tree_view.mouse_x_ <= menu_x + menu_w &&
                        task_tree_view.mouse_y_ >= screen_btn_y - 15 &&
                        task_tree_view.mouse_y_ <= screen_btn_y + 15);

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

    if (task_tree_view.show_debug_) {
      const auto &entries = Logger::Get().GetEntries();
      int debug_w = 600;
      int debug_h = 280;
      int debug_x = 10;
      int viewport_h = params.view_define_->viewport_height_;
      // glVertex uses bottom-left origin, so Y=10 = near bottom of screen
      int debug_y_gl = 10;
      // draw_text uses top-left origin (flips internally), convert:
      // draw_text_y = viewport_h - (gl_y + box_h)
      int debug_y_text = viewport_h - (debug_y_gl + debug_h);

      // Draw semi-transparent background
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(0.0f, 0.0f, 0.0f, 0.7f);
      glBegin(GL_QUADS);
      glVertex2i(debug_x, debug_y_gl);
      glVertex2i(debug_x + debug_w, debug_y_gl);
      glVertex2i(debug_x + debug_w, debug_y_gl + debug_h);
      glVertex2i(debug_x, debug_y_gl + debug_h);
      glEnd();
      glDisable(GL_BLEND);

      // Draw border
      glLineWidth(2.0f);
      glColor3f(0.5f, 0.5f, 0.5f);
      glBegin(GL_LINE_LOOP);
      glVertex2i(debug_x, debug_y_gl);
      glVertex2i(debug_x + debug_w, debug_y_gl);
      glVertex2i(debug_x + debug_w, debug_y_gl + debug_h);
      glVertex2i(debug_x, debug_y_gl + debug_h);
      glEnd();
      glLineWidth(1.0f);

      // Title at top of box (draw_text uses top-left origin)
      draw_text_sys(debug_x + 10, debug_y_text + 10, "DEBUG CONSOLE", font_r,
                font_g, font_b);

      int startY = debug_y_text + 30; // Just below title, inside the frame
      int line_height = 12;
      int count = 0;
      int max_lines = (debug_h - 50) / line_height;
      int max_chars = (debug_w - 20) / 7;

      for (auto it = entries.rbegin();
           it != entries.rend() && count < max_lines; ++it, ++count) {
        float r = 1.0f, g = 1.0f, b = 1.0f;
        if (it->level == LogLevel::ERR) {
          r = 1.0f;
          g = 0.2f;
          b = 0.2f;
        } else if (it->level == LogLevel::FATAL) {
          r = 1.0f;
          g = 0.0f;
          b = 0.0f;
        } else if (it->level == LogLevel::WARNING) {
          r = 1.0f;
          g = 1.0f;
          b = 0.0f;
        } else if (it->level == LogLevel::DEBUG) {
          r = 0.5f;
          g = 0.7f;
          b = 1.0f;
        }

        // Text truncation based on box width
        std::string msg = it->message;
        if (msg.length() > max_chars) {
          msg = msg.substr(0, max_chars - 3) + "...";
        }
        draw_text_sys(debug_x + 10, startY + count * line_height, msg.c_str(), r, g,
                  b);
      }
    }

    if (task_tree_view.show_help_) {
      int menu_w = 500;
      int menu_h = 450;
      int menu_x = (params.view_define_->viewport_width_ - menu_w) / 2;
      int menu_y = (params.view_define_->viewport_height_ - menu_h) / 2;

      // Draw semi-transparent background
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(0.0f, 0.0f, 0.0f, 0.8f);
      glBegin(GL_QUADS);
      glVertex2i(menu_x, menu_y);
      glVertex2i(menu_x + menu_w, menu_y);
      glVertex2i(menu_x + menu_w, menu_y + menu_h);
      glVertex2i(menu_x, menu_y + menu_h);
      glEnd();
      glDisable(GL_BLEND);

      // Draw border
      glColor3f(font_r, font_g, font_b);
      glLineWidth(2.0f);
      glBegin(GL_LINE_LOOP);
      glVertex2i(menu_x, menu_y);
      glVertex2i(menu_x + menu_w, menu_y);
      glVertex2i(menu_x + menu_w, menu_y + menu_h);
      glVertex2i(menu_x, menu_y + menu_h);
      glEnd();
      glLineWidth(1.0f);

      // Title
      draw_text_sys(menu_x + menu_w / 2 - 40, menu_y + menu_h - 30, "KEYBINDINGS",
                font_r, font_g, font_b);

      // Help items
      int line_y = menu_y + menu_h - 60;
      draw_text_sys(menu_x + 30, line_y, "[W/A/S/D] Movement", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text_sys(menu_x + 30, line_y, "[F3] Clip Toggle", font_r, font_g,
                font_b);
      line_y -= 20;
      // Edit Mode toggle removed as requested
      draw_text_sys(menu_x + 30, line_y, "[PageUp/Down] Move Speed", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text_sys(menu_x + 30, line_y, "[Left/Right] Roll", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text_sys(menu_x + 30, line_y, "[TAB] Select Next Object", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text_sys(menu_x + 30, line_y, "[T] Teleport to Height Map", font_r,
                font_g, font_b);
      line_y -= 20;
      draw_text_sys(menu_x + 30, line_y, "[S] Snap Object to Ground", font_r,
                font_g, font_b);
      line_y -= 20;
      draw_text_sys(menu_x + 30, line_y, "[ESC] Pause Menu", font_r, font_g,
                font_b);
      line_y -= 30;
      draw_text_sys(menu_x + 30, line_y, "PAUSE MENU:", font_r, font_g, font_b);
      line_y -= 20;
      draw_text_sys(menu_x + 30, line_y, "[CTRL+S] Save Level", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text_sys(menu_x + 30, line_y, "[CTRL+R] Reset Level", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text_sys(menu_x + 30, line_y, "[SHIFT+R] Reset Script", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text_sys(menu_x + 30, line_y, "[CTRL+D] Debug Toggle", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text_sys(menu_x + 30, line_y, "[CTRL+Q] Exit", font_r, font_g, font_b);
      line_y -= 20;
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

          PropPanel::Layout L = PropPanel::BuildLayout(schema);

          // GL y for a screen-top-down y.
          auto gl_y = [&](int sy) { return vh - sy; };
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

          auto tok = [&](int idx) -> std::string {
            return (idx >= 0 && idx < (int)obj.argTokens.size()) ? obj.argTokens[idx] : std::string("-");
          };

          // Caret blink (on/off ~2Hz). ~7px per char matches the wrap heuristic.
          const bool caret_on = ((glutGet(GLUT_ELAPSED_TIME) / 500) & 1) == 0;
          const int  caret_field = task_tree_view.prop_text_edit_field_;
          int caret_idx = task_tree_view.prop_text_caret_;
          if (caret_idx < 0) caret_idx = 0;
          if (caret_idx > (int)task_tree_view.prop_text_buf_.size())
            caret_idx = (int)task_tree_view.prop_text_buf_.size();
          // Draw an editable box's contents + blinking caret. `field_id` is the
          // edit-field key (fi*3+comp, or -2 for the note). `multiline` wraps.
          auto draw_edit_box = [&](const PropPanel::Widget& w, int field_id,
                                   const std::string& live_text, bool multiline) {
            bool editing = (caret_field == field_id);
            const std::string& txt = editing ? task_tree_view.prop_text_buf_ : live_text;
            const int cw = 7;
            int max_chars = std::max(1, (w.x2 - w.x1 - 6) / cw);
            int box_lines = std::max(1, (w.y2 - w.y1) / PropPanel::kBoxH);
            // Render wrapped (single-line types still wrap if oversized).
            int line = 0;
            for (size_t p = 0; (p < txt.size() || (txt.empty() && line == 0)) &&
                               line < (multiline ? box_lines : 1); p += max_chars, ++line) {
              std::string seg = txt.substr(p, (multiline ? (size_t)max_chars : txt.size()));
              if (!multiline && (int)seg.size() > max_chars && !editing)
                seg = seg.substr(0, max_chars);
              draw_text(w.x1 + 3, w.y1 + 12 + line * PropPanel::kBoxH, seg.c_str(), 1.0f, 1.0f, 0.85f);
              if (txt.empty()) break;
            }
            if (editing && caret_on) {
              int ci = caret_idx;
              int cl = multiline ? (ci / max_chars) : 0;
              int cc = multiline ? (ci % max_chars) : ci;
              int cx = w.x1 + 3 + cc * cw;
              int cyt = w.y1 + 3 + cl * PropPanel::kBoxH;
              int cyb = cyt + 13;
              glColor3f(1.0f, 0.95f, 0.2f);
              glBegin(GL_LINES);
              glVertex2i(cx, gl_y(cyt)); glVertex2i(cx, gl_y(cyb));
              glEnd();
            }
          };

          // Opaque panel background covering the left strip + border.
          quad(L.panel_x, L.panel_y, L.panel_x + L.panel_w, L.panel_y + L.panel_h,
               0.06f, 0.06f, 0.09f, 0.85f);
          border(L.panel_x, L.panel_y, L.panel_x + L.panel_w, L.panel_y + L.panel_h,
                 1.0f, 0.85f, 0.0f);

          // Header text — track running screen y to mirror the layout.
          int ty = L.panel_y + PropPanel::kPad;
          char hdr[160];
          snprintf(hdr, sizeof(hdr), "QTasktype: %s", obj.type.c_str());
          draw_text(L.panel_x + PropPanel::kPad, ty + 11, hdr, 1.0f, 0.9f, 0.1f);
          ty += PropPanel::kRowH;
          draw_text(L.panel_x + PropPanel::kPad, ty + 11, "QTask Note (QTaskNote):", 1.0f, 0.9f, 0.2f);
          ty += PropPanel::kRowH;

          // Walk widgets; field headers + value labels are derived from layout y.
          // Note box (first widget).
          {
            const auto& w = L.widgets[0];
            bool editing = (task_tree_view.prop_text_edit_field_ == -2);
            quad(w.x1, w.y1, w.x2, w.y2, 0.10f, 0.10f, 0.06f, 0.85f);
            border(w.x1, w.y1, w.x2, w.y2, editing ? 1.0f : 1.0f,
                   editing ? 0.95f : 0.85f, editing ? 0.2f : 0.0f);
            draw_edit_box(w, -2, obj.name, false);
          }

          // Per-field rendering driven by the same layout pass.
          int wi = 1; // widget cursor (0 was the note)
          int y  = L.widgets[0].y2 + 6;
          for (int fi = 0; fi < (int)schema.size(); ++fi) {
            const FieldDef& fd = schema[fi];
            const std::string& tn = fd.typeName;
            bool is_pos  = (tn == "ObjectPos" || tn == "Real32x3" || tn == "Real64x3");
            bool is_ori  = (tn == "Real32x9");
            bool is_rgb  = (tn == "RGB" || tn == "Colour");
            bool is_str  = (tn.find("String") != std::string::npos || tn == "VarString" ||
                            tn == "EnumString32" || tn == "DropDownCombo");
            bool is_bool = (tn == "bool8" || tn == "PushButton");
            bool is_ro   = (tn == "Graph" || tn == "AnimData" || tn == "TrainPos1D");
            bool is_int  = (tn == "Int16" || tn == "Int32" || tn == "EnumInt32");

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
              bool editing = (caret_field == field_id);
              if (label && label[0])
                draw_text(L.panel_x + PropPanel::kPad, w.y1 + 12, label, 1.0f, 0.9f, 0.2f);
              quad(w.x1, w.y1, w.x2, w.y2, 0.10f, 0.10f, 0.06f, 0.85f);
              border(w.x1, w.y1, w.x2, w.y2, editing ? 1.0f : 1.0f,
                     editing ? 0.95f : 0.85f, editing ? 0.2f : 0.0f);
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
              quad(pad.x1, pad.y1, pad.x2, pad.y2, 0.10f, 0.10f, 0.06f, 0.85f);
              border(pad.x1, pad.y1, pad.x2, pad.y2, 1.0f, 0.85f, 0.0f);
              {
                double px = 0, py = 0;
                try { px = std::stod(tok(fd.argOffset + 0)); } catch(...) {}
                try { py = std::stod(tok(fd.argOffset + 1)); } catch(...) {}
                const double win = 50.0; // ± window mapped across the pad
                int cx = pad.x1 + (pad.x2 - pad.x1) / 2;
                int cy = pad.y1 + (pad.y2 - pad.y1) / 2;
                int hx = cx + (int)(std::max(-1.0, std::min(1.0, (px - std::floor(px / win) * win - win / 2) / (win / 2))) * (pad.x2 - pad.x1) / 2);
                int hy = cy - (int)(std::max(-1.0, std::min(1.0, (py - std::floor(py / win) * win - win / 2) / (win / 2))) * (pad.y2 - pad.y1) / 2);
                bool drag = (task_tree_view.prop_field_index_ == fi * 3 + 0 ||
                             task_tree_view.prop_field_index_ == fi * 3 + 1);
                quad(hx - 4, hy - 4, hx + 4, hy + 4, 1.0f, drag ? 0.95f : 0.85f, drag ? 0.2f : 0.0f, 1.0f);
              }
              // Z vertical slider
              const auto& zs = L.widgets[wi++];
              quad(zs.x1, zs.y1, zs.x2, zs.y2, 0.10f, 0.10f, 0.06f, 0.85f);
              border(zs.x1, zs.y1, zs.x2, zs.y2, 1.0f, 0.85f, 0.0f);
              {
                double pz = 0; try { pz = std::stod(tok(fd.argOffset + 2)); } catch(...) {}
                const double zwin = 50.0;
                double f = (pz - std::floor(pz / zwin) * zwin) / zwin; // 0..1
                int th = zs.y2 - (int)(f * (zs.y2 - zs.y1));
                bool drag = (task_tree_view.prop_field_index_ == fi * 3 + 2);
                quad(zs.x1, th - 3, zs.x2, th + 3, 1.0f, drag ? 0.95f : 0.85f, drag ? 0.2f : 0.0f, 1.0f);
              }
              y = pad.y2 + 6;
              // Snap buttons
              const auto& bg = L.widgets[wi++];
              const auto& bo = L.widgets[wi++];
              quad(bg.x1, bg.y1, bg.x2, bg.y2, 0.10f, 0.10f, 0.06f, 0.85f);
              border(bg.x1, bg.y1, bg.x2, bg.y2, 1.0f, 0.85f, 0.0f);
              draw_text(bg.x1 + 6, bg.y1 + 12, "Snap to ground", 1.0f, 0.9f, 0.2f);
              quad(bo.x1, bo.y1, bo.x2, bo.y2, 0.10f, 0.10f, 0.06f, 0.85f);
              border(bo.x1, bo.y1, bo.x2, bo.y2, 1.0f, 0.85f, 0.0f);
              draw_text(bo.x1 + 6, bo.y1 + 12, "Snap to object", 1.0f, 0.9f, 0.2f);
              y = bg.y2 + 4;
              {
                char ab[80];
                snprintf(ab, sizeof(ab), "Altitude: %.6f meter", obj.pos.z);
                draw_text(L.panel_x + PropPanel::kPad, y + 11, ab, 0.75f, 0.7f, 0.4f);
                y += PropPanel::kRowH;
              }
            } else if (is_ori) {
              const char* lab[3] = {"Alpha", "Beta", "Gamma"};
              for (int c = 0; c < 3; ++c) {
                const auto& w = L.widgets[wi++];
                draw_text(L.panel_x + PropPanel::kPad, w.y1 + 12, lab[w.comp], 1.0f, 0.9f, 0.2f);
                draw_text(L.panel_x + PropPanel::kPad + 40, w.y1 + 12,
                          tok(fd.argOffset + w.comp).c_str(), 1.0f, 1.0f, 0.85f);
                int cy = (w.y1 + w.y2) / 2;
                quad(w.x1, cy - 2, w.x2, cy + 2, 0.10f, 0.10f, 0.06f, 0.85f);
                border(w.x1, cy - 2, w.x2, cy + 2, 1.0f, 0.85f, 0.0f);
                float v = 0.f; try { v = std::stof(tok(fd.argOffset + w.comp)); } catch(...) {}
                float norm = std::max(0.f, std::min(1.f, (v + 3.14159f) / (2.f * 3.14159f)));
                int tx = w.x1 + (int)(norm * (w.x2 - w.x1 - 6));
                bool drag = (task_tree_view.prop_field_index_ == fi * 3 + w.comp);
                quad(tx, cy - 5, tx + 6, cy + 5, 1.0f, drag ? 0.95f : 0.85f, drag ? 0.2f : 0.0f, 1.0f);
                y = w.y2;
              }
            } else if (is_rgb) {
              const char* rgbl[3] = {"R", "G", "B"};
              float rgb[3] = {0,0,0};
              for (int c = 0; c < 3; ++c) {
                const auto& w = L.widgets[wi++];
                draw_text(L.panel_x + PropPanel::kPad, w.y1 + 12, rgbl[w.comp], 1.0f, 0.9f, 0.2f);
                draw_text(L.panel_x + PropPanel::kPad + 24, w.y1 + 12,
                          tok(fd.argOffset + w.comp).c_str(), 1.0f, 1.0f, 0.85f);
                int cy = (w.y1 + w.y2) / 2;
                quad(w.x1, cy - 2, w.x2, cy + 2, 0.10f, 0.10f, 0.06f, 0.85f);
                border(w.x1, cy - 2, w.x2, cy + 2, 1.0f, 0.85f, 0.0f);
                float v = 0.f; try { v = std::stof(tok(fd.argOffset + w.comp)); } catch(...) {}
                rgb[w.comp] = std::max(0.f, std::min(1.f, v));
                float norm = rgb[w.comp];
                int tx = w.x1 + (int)(norm * (w.x2 - w.x1 - 6));
                bool drag = (task_tree_view.prop_field_index_ == fi * 3 + w.comp);
                quad(tx, cy - 5, tx + 6, cy + 5, 1.0f, drag ? 0.95f : 0.85f, drag ? 0.2f : 0.0f, 1.0f);
                // Swatch to the right of the last slider row.
                if (c == 2) {
                  int sw_x = w.x2 + 4, sw_y1 = L.widgets[wi - 3].y1, sw_y2 = w.y2;
                  quad(sw_x, sw_y1, sw_x + 18, sw_y2, rgb[0], rgb[1], rgb[2], 1.0f);
                  border(sw_x, sw_y1, sw_x + 18, sw_y2, 1.0f, 0.85f, 0.0f);
                }
                y = w.y2;
              }
            } else if (is_str) {
              const auto& w = L.widgets[wi++];
              bool multiline = (tn == "VarString" || tn == "String256");
              int field_id = fi * 3 + 0;
              bool editing = (caret_field == field_id);
              quad(w.x1, w.y1, w.x2, w.y2, 0.10f, 0.10f, 0.06f, 0.85f);
              border(w.x1, w.y1, w.x2, w.y2, editing ? 1.0f : 1.0f,
                     editing ? 0.95f : 0.85f, editing ? 0.2f : 0.0f);
              draw_edit_box(w, field_id, tok(fd.argOffset), multiline);
              y = w.y2 + 2;
            } else if (is_bool) {
              const auto& w = L.widgets[wi++];
              bool bv = false; try { bv = (std::stoi(tok(fd.argOffset)) != 0); } catch(...) {}
              quad(w.x1, w.y1, w.x2, w.y2, 0.10f, 0.10f, 0.06f, 0.85f);
              if (bv) quad(w.x1 + 2, w.y1 + 2, w.x2 - 2, w.y2 - 2, 1.0f, 0.85f, 0.0f, 0.9f);
              border(w.x1, w.y1, w.x2, w.y2, 1.0f, 0.85f, 0.0f);
              draw_text(w.x2 + 6, w.y1 + 11, bv ? "TRUE" : "FALSE", 1.0f, 0.9f, 0.2f);
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
              quad(sl.x1, cy - 2, sl.x2, cy + 2, 0.10f, 0.10f, 0.06f, 0.85f);
              border(sl.x1, cy - 2, sl.x2, cy + 2, 1.0f, 0.85f, 0.0f);
              float v = 0.f; try { v = std::stof(tok(fd.argOffset)); } catch(...) {}
              float norm = std::max(0.f, std::min(1.f, (v - std::floor(v / 200.f) * 200.f) / 200.f));
              int tx = sl.x1 + (int)(norm * (sl.x2 - sl.x1 - 6));
              bool drag = (task_tree_view.prop_field_index_ == fi * 3 + 0);
              quad(tx, cy - 5, tx + 6, cy + 5, 1.0f, drag ? 0.95f : 0.85f, drag ? 0.2f : 0.0f, 1.0f);
              draw_numbox(bx, nullptr);
              y = bx.y2;
            }
            y += 4;
          }
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

      // Title
      {
        const char* title = "Find task by type:";
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

      // Match / no-match feedback below the input box
      if (task_tree_view.find_result_idx_ >= 0 && task_tree_view.level_objects_) {
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
