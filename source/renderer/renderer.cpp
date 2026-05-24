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

#include <freeglut.h>

/*
================================================================================
 Renderer
================================================================================
*/
Renderer::Renderer() : ubo_mats_(0), ubo_fog_(0), splines_(objects_) {
  LoadBuildingNames();
}

Renderer::~Renderer() { Shutdown(); }

static bool ExtractJsonStringValue(const std::string &text,
                                   const std::vector<std::string> &keys,
                                   std::string &value) {
  for (const auto &key : keys) {
    size_t keyPos = text.find(key);
    if (keyPos == std::string::npos)
      continue;
    size_t colonPos = text.find(':', keyPos + key.size());
    if (colonPos == std::string::npos)
      continue;
    size_t quoteStart = text.find('"', colonPos + 1);
    if (quoteStart == std::string::npos)
      continue;
    size_t quoteEnd = quoteStart + 1;
    bool escape = false;
    while (quoteEnd < text.size()) {
      char c = text[quoteEnd];
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == '"') {
        value = text.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
        return true;
      }
      ++quoteEnd;
    }
  }
  return false;
}

void Renderer::LoadBuildingNames() {
  char exePath[MAX_PATH];
  GetModuleFileNameA(NULL, exePath, MAX_PATH);
  std::string exeDir(exePath);
  size_t ls = exeDir.find_last_of("\\/");
  if (ls != std::string::npos)
    exeDir = exeDir.substr(0, ls);
  std::string jsonPath = exeDir + "\\content\\tools\\IGIModelsAllLevel.json";

  FILE *f = fopen(jsonPath.c_str(), "rb");
  if (!f) {
    std::cerr << "[Renderer] Failed to open IGIModelsAllLevel.json at: "
              << jsonPath << std::endl;
    return;
  }

  fseek(f, 0, SEEK_END);
  long fileSize = ftell(f);
  fseek(f, 0, SEEK_SET);

  char *buf = new char[fileSize + 1];
  fread(buf, 1, fileSize, f);
  buf[fileSize] = '\0';
  fclose(f);

  std::string content(buf);
  delete[] buf;

  building_names_.clear();
  task_ids_.clear();

  // Parse IGIModelsAllLevel.json structure
  size_t pos = 0;
  while ((pos = content.find("\"Level ", pos)) != std::string::npos) {
    size_t levelEnd = content.find("\"", pos + 7);
    if (levelEnd == std::string::npos)
      break;
    std::string levelStr = content.substr(pos + 7, levelEnd - pos - 7);
    int levelNum = 0;
    try {
      levelNum = std::stoi(levelStr);
    } catch (...) {
      pos = levelEnd + 1;
      continue;
    }

    size_t levelBlockStart = content.find('{', levelEnd);
    if (levelBlockStart == std::string::npos)
      break;
    int braceDepth = 0;
    size_t levelBlockEnd = std::string::npos;
    for (size_t i = levelBlockStart; i < content.size(); ++i) {
      if (content[i] == '{')
        braceDepth++;
      else if (content[i] == '}') {
        braceDepth--;
        if (braceDepth == 0) {
          levelBlockEnd = i;
          break;
        }
      }
    }
    if (levelBlockEnd == std::string::npos)
      break;

    std::string levelBlock =
        content.substr(levelBlockStart, levelBlockEnd - levelBlockStart + 1);

    size_t objectsPos = levelBlock.find("\"Objects\"");
    if (objectsPos != std::string::npos) {
      size_t arrayStart = levelBlock.find("[", objectsPos);
      size_t arrayEnd = levelBlock.find("]", arrayStart);
      if (arrayStart != std::string::npos && arrayEnd != std::string::npos &&
          arrayEnd > arrayStart) {
        std::string objectsArray =
            levelBlock.substr(arrayStart, arrayEnd - arrayStart + 1);
        ParseLevelObjects(objectsArray, levelNum, false);
      }
    }

    size_t buildingsPos = levelBlock.find("\"Buildings\"");
    if (buildingsPos != std::string::npos) {
      size_t arrayStart = levelBlock.find("[", buildingsPos);
      size_t arrayEnd = levelBlock.find("]", arrayStart);
      if (arrayStart != std::string::npos && arrayEnd != std::string::npos &&
          arrayEnd > arrayStart) {
        std::string buildingsArray =
            levelBlock.substr(arrayStart, arrayEnd - arrayStart + 1);
        ParseLevelObjects(buildingsArray, levelNum, true);
      }
    }

    pos = levelBlockEnd + 1;
  }

  Logger::Get().Log(
      LogLevel::INFO,
      "[Renderer] Loaded " + std::to_string(building_names_.size()) +
          " building names and " + std::to_string(task_ids_.size()) +
          " task IDs from IGIModelsAllLevel.json");
}

void Renderer::ParseLevelObjects(const std::string &arrayContent, int levelNum,
                                 bool isBuilding) {
  (void)isBuilding;
  size_t pos = 0;
  while ((pos = arrayContent.find('{', pos)) != std::string::npos) {
    int braceDepth = 0;
    size_t objectEnd = std::string::npos;
    for (size_t i = pos; i < arrayContent.size(); ++i) {
      if (arrayContent[i] == '{')
        braceDepth++;
      else if (arrayContent[i] == '}') {
        braceDepth--;
        if (braceDepth == 0) {
          objectEnd = i;
          break;
        }
      }
    }
    if (objectEnd == std::string::npos)
      break;

    std::string objContent = arrayContent.substr(pos, objectEnd - pos + 1);
    std::string modelId;
    std::string name;
    std::string taskId;
    ExtractJsonStringValue(
        objContent, {"\"Model ID\"", "\"ModelID\"", "\"ModelId\""}, modelId);
    ExtractJsonStringValue(objContent, {"\"Name\"", "\"ModelName\""}, name);
    ExtractJsonStringValue(objContent, {"\"Task ID\"", "\"TaskID\""}, taskId);

    if (!modelId.empty()) {
      std::string key = std::to_string(levelNum) + "_" + modelId;
      if (!name.empty())
        building_names_[key] = name;
      if (!taskId.empty())
        task_ids_[key] = taskId;
    }

    pos = objectEnd + 1;
  }
}

std::string Renderer::GetBuildingName(const std::string &modelId) {
  std::string key = std::to_string(current_level_) + "_" + modelId;
  auto it = building_names_.find(key);
  if (it != building_names_.end()) {
    return it->second;
  }
  // Fallback to non-level-specific lookup for compatibility
  it = building_names_.find(modelId);
  if (it != building_names_.end()) {
    return it->second;
  }
  return "";
}

std::string Renderer::GetTaskId(const std::string &modelId) {
  std::string key = std::to_string(current_level_) + "_" + modelId;
  auto it = task_ids_.find(key);
  if (it != task_ids_.end()) {
    return it->second;
  }
  return "0";
}

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
                  params.draw_parts_, params.view_define_->pos_);
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

    auto draw_text = [&](int x, int y, const char *str, float r, float g,
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
    if (task_tree_view.level_objects_) {
      const auto &objects = task_tree_view.level_objects_->GetObjects();
      int tree_x = 0;
      int tree_y = 0; // Starting Y for tree
      int row_h = 16;
      int start_y = 0;
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
    int tooltip_x = task_tree_view.mouse_x_;
    int tooltip_y = task_tree_view.mouse_y_ + 25;
    if (tooltip_x < 0)
      tooltip_x = 0;
    if (tooltip_x > params.view_define_->viewport_width_ - 260)
      tooltip_x = params.view_define_->viewport_width_ - 260;
    if (tooltip_y < 0)
      tooltip_y = 0;
    if (tooltip_y > params.view_define_->viewport_height_ - 100)
      tooltip_y = params.view_define_->viewport_height_ - 100;

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

    if (info_object_index >= 0 && task_tree_view.level_objects_) {
      const auto &objects = task_tree_view.level_objects_->GetObjects();
      if (info_object_index < (int)objects.size()) {
        const auto &obj = objects[info_object_index];

        char buf[512];
        std::string display_name = GetBuildingName(obj.modelId);
        if (display_name.empty())
          display_name = obj.name;
        if (display_name.empty())
          display_name = obj.type;
        if (display_name.empty())
          display_name = obj.modelId;

        std::string task_id = obj.taskId.empty() ? "-1" : obj.taskId;

        int text_x = tooltip_x;
        int text_y = tooltip_y;

        bool isAI = !obj.aiId.empty() || obj.type.find("AITYPE") == 0 ||
                    obj.type == "HumanSoldier" || obj.type == "HumanAI";
        if (isAI) {
          // Draw header line with Name and Type
          std::string aiName = obj.name.empty() ? "AI Soldier" : obj.name;
          snprintf(buf, sizeof(buf), "Name: %s (Type: %s)", aiName.c_str(),
                   obj.type.c_str());
          draw_text(text_x, text_y, buf, 0.0f, 0.8f, 1.0f); // Sky blue title
          text_y += 15;

          snprintf(buf, sizeof(buf), "Soldier ID: %s | AI ID: %s",
                   task_id.c_str(), obj.aiId.empty() ? "-1" : obj.aiId.c_str());
          draw_text(text_x, text_y, buf, 1.0f, 1.0f, 1.0f);
          text_y += 15;

          std::string teamStr =
              (obj.team == 1) ? "Enemy"
                              : ((obj.team == 0) ? "Friendly" : "Neutral");
          float tr = (obj.team == 1) ? 1.0f : 0.2f;
          float tg = (obj.team == 1) ? 0.2f : 1.0f;
          float tb = 0.2f;
          snprintf(buf, sizeof(buf), "Team: %s", teamStr.c_str());
          draw_text(text_x, text_y, buf, tr, tg, tb);
          text_y += 15;

          if (!obj.primaryWeapon.empty()) {
            snprintf(buf, sizeof(buf), "Pri: %s (%s Ammo)",
                     obj.primaryWeapon.c_str(), obj.primaryAmmo.c_str());
            draw_text(text_x, text_y, buf, 0.9f, 0.9f, 0.9f);
            text_y += 15;
          }

          if (!obj.secondaryWeapon.empty()) {
            snprintf(buf, sizeof(buf), "Sec: %s (%s Ammo)",
                     obj.secondaryWeapon.c_str(), obj.secondaryAmmo.c_str());
            draw_text(text_x, text_y, buf, 0.8f, 0.8f, 0.8f);
            text_y += 15;
          }

          if (!obj.graphName.empty() || !obj.graphId.empty()) {
            snprintf(buf, sizeof(buf), "Graph: %s (ID: %s)",
                     obj.graphName.c_str(), obj.graphId.c_str());
            draw_text(text_x, text_y, buf, 0.9f, 0.6f, 0.9f);
            text_y += 15;
          }
        } else {
          // Standard building / prop tooltip
          snprintf(buf, sizeof(buf), "%s ID: %s", display_name.c_str(),
                   task_id.c_str());
          draw_text(text_x, text_y, buf, 1.0f, 1.0f, 1.0f);
          text_y += 15;

          snprintf(buf, sizeof(buf), "%s", obj.modelId.c_str());
          draw_text(text_x, text_y, buf, obj.isBuilding ? 1.0f : 0.0f, 1.0f,
                    0.0f);
          text_y += 15;
        }

        // Always display X, Y, Z coordinates for all objects
        snprintf(buf, sizeof(buf), "Pos: X: %.1f Y: %.1f Z: %.1f", obj.pos.x,
                 obj.pos.y, obj.pos.z);
        draw_text(text_x, text_y, buf, 0.7f, 0.7f, 0.7f);
        text_y += 15;

        if (!task_tree_view.status_msg_.empty() &&
            info_object_index == task_tree_view.selected_object_index_) {
          draw_text(text_x, text_y, task_tree_view.status_msg_.c_str(), 0.85f,
                    1.0f, 0.6f);
        }
      }
    }

    // Watermark
    int w_width = glutBitmapLength(
        GLUT_BITMAP_HELVETICA_12,
        (const unsigned char *)"IGI Editor Copyright - HeavenHM");
    int w_x = (params.view_define_->viewport_width_ - w_width) / 2;
    draw_text(w_x, params.view_define_->viewport_height_ - 20, "IGI Editor - HeavenHM", 0.7f, 0.7f, 0.7f);

    if (task_tree_view.task_picker_open_ && task_tree_view.level_objects_) {
      int picker_x = 350;
      int picker_w = 400;
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
      glColor4f(0.95f, 0.95f, 0.90f,
                0.25f); // Semi-transparent warm white/yellow
      glBegin(GL_QUADS);
      glVertex2i(picker_x, card_bottom_y);
      glVertex2i(picker_x + picker_w, card_bottom_y);
      glVertex2i(picker_x + picker_w, card_top_y);
      glVertex2i(picker_x, card_top_y);
      glEnd();

      // Warm Yellow/Gold transparent border
      glColor4f(1.0f, 0.85f, 0.0f, 0.7f); // Transparent yellow/gold
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

        // Draw thumb (semi-transparent yellow/gold)
        glColor4f(1.0f, 0.85f, 0.0f, 0.7f); // Transparent yellow/gold
        glLineWidth(3.0f);
        glBegin(GL_LINES);
        glVertex2i(track_x, thumb_y_bottom);
        glVertex2i(track_x, thumb_y_top);
        glEnd();
        glLineWidth(1.0f);
      }
    }

    if (task_tree_view.task_editor_open_) {
      // Render Task Editor Box
      int box_w = task_tree_view.edit_box_w_;
      int box_h = task_tree_view.edit_box_h_;
      int box_x = (params.view_define_->viewport_width_ - box_w) / 2;
      int box_y = (params.view_define_->viewport_height_ - box_h) / 2;

      // Dark background
      glColor4f(0.05f, 0.05f, 0.05f, 0.95f);
      glBegin(GL_QUADS);
      glVertex2i(box_x, box_y);
      glVertex2i(box_x + box_w, box_y);
      glVertex2i(box_x + box_w, box_y + box_h);
      glVertex2i(box_x, box_y + box_h);
      glEnd();

      // Border
      glColor3f(1.0f, 0.4f, 0.0f); // Bright Orange border
      glLineWidth(2.0f);
      glBegin(GL_LINE_LOOP);
      glVertex2i(box_x, box_y);
      glVertex2i(box_x + box_w, box_y);
      glVertex2i(box_x + box_w, box_y + box_h);
      glVertex2i(box_x, box_y + box_h);
      glEnd();
      glLineWidth(1.0f);

      int viewport_h = params.view_define_->viewport_height_;
      draw_text(box_x + 10, viewport_h - (box_y + box_h - 20),
                "Task Editor (ESC: Discard, Save: Commit)", 1.0f, 1.0f, 1.0f);
      draw_text(box_x + 10, viewport_h - (box_y + box_h - 40),
                "Contents:", 0.6f, 0.6f, 0.6f);

      // Input field bg
      glColor4f(0.15f, 0.15f, 0.15f, 1.0f);
      glBegin(GL_QUADS);
      glVertex2i(box_x + 10, box_y + 10);
      glVertex2i(box_x + box_w - 10, box_y + 10);
      glVertex2i(box_x + box_w - 10, box_y + box_h - 50);
      glVertex2i(box_x + 10, box_y + box_h - 50);
      glEnd();

      // Use Scissor Test for clipping
      glEnable(GL_SCISSOR_TEST);
      glScissor(box_x + 10, box_y + 10, box_w - 20, box_h - 60);

      int visible_chars = std::max(1, (box_w - 40) / 9);

      // Selection Highlight (relative to visible window)
      if (task_tree_view.edit_selection_start_ != -1 &&
          task_tree_view.edit_selection_end_ != -1 &&
          task_tree_view.edit_selection_start_ !=
              task_tree_view.edit_selection_end_) {
        int s = std::min(task_tree_view.edit_selection_start_,
                         task_tree_view.edit_selection_end_);
        int e = std::max(task_tree_view.edit_selection_start_,
                         task_tree_view.edit_selection_end_);
        int sel_x1 = box_x + 20 + ((s - task_tree_view.edit_scroll_x_) * 9);
        int sel_x2 = box_x + 20 + ((e - task_tree_view.edit_scroll_x_) * 9);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(0.0f, 0.5f, 1.0f, 0.4f); // Semi-transparent blue
        glBegin(GL_QUADS);
        glVertex2i(sel_x1, box_y + 25);
        glVertex2i(sel_x2, box_y + 25);
        glVertex2i(sel_x2, box_y + 45);
        glVertex2i(sel_x1, box_y + 45);
        glEnd();
        glDisable(GL_BLEND);
      }

      // Cursor (Scrolled) - 9px width
      int cursor_x =
          box_x + 20 +
          ((task_tree_view.edit_cursor_pos_ - task_tree_view.edit_scroll_x_) *
           9);
      glColor3f(1.0f, 1.0f, 1.0f); // White cursor
      glLineWidth(2.0f);
      glBegin(GL_LINES);
      glVertex2i(cursor_x, box_y + 28);
      glVertex2i(cursor_x, box_y + 42);
      glEnd();
      glLineWidth(1.0f);

      // Draw only visible text window to avoid blank rendering on very long
      // lines
      std::string displayString = task_tree_view.edit_string_;
      displayString.erase(
          std::remove(displayString.begin(), displayString.end(), '\r'),
          displayString.end());
      displayString.erase(
          std::remove(displayString.begin(), displayString.end(), '\n'),
          displayString.end());
      int start = std::max(0, task_tree_view.edit_scroll_x_);
      if (start > (int)displayString.size())
        start = (int)displayString.size();
      std::string visible =
          displayString.substr((size_t)start, (size_t)visible_chars + 2);
      draw_text_mono(box_x + 20, viewport_h - (box_y + 35), visible.c_str(),
                     1.0f, 1.0f, 1.0f);

      glDisable(GL_SCISSOR_TEST);

      const int save_btn_w = 84;
      const int save_btn_h = 26;
      const int save_btn_x = box_x + box_w - save_btn_w - 14;
      const int save_btn_y = box_y + box_h - 40;
      bool save_hovered = task_tree_view.mouse_x_ >= save_btn_x &&
                          task_tree_view.mouse_x_ <= save_btn_x + save_btn_w &&
                          task_tree_view.mouse_y_ >= save_btn_y &&
                          task_tree_view.mouse_y_ <= save_btn_y + save_btn_h;

      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(save_hovered ? 0.10f : 0.05f, save_hovered ? 0.55f : 0.35f,
                0.10f, 0.95f);
      glBegin(GL_QUADS);
      glVertex2i(save_btn_x, save_btn_y);
      glVertex2i(save_btn_x + save_btn_w, save_btn_y);
      glVertex2i(save_btn_x + save_btn_w, save_btn_y + save_btn_h);
      glVertex2i(save_btn_x, save_btn_y + save_btn_h);
      glEnd();
      glDisable(GL_BLEND);

      glColor3f(1.0f, 1.0f, 1.0f);
      glBegin(GL_LINE_LOOP);
      glVertex2i(save_btn_x, save_btn_y);
      glVertex2i(save_btn_x + save_btn_w, save_btn_y);
      glVertex2i(save_btn_x + save_btn_w, save_btn_y + save_btn_h);
      glVertex2i(save_btn_x, save_btn_y + save_btn_h);
      glEnd();
      int text_w = glutBitmapLength(GLUT_BITMAP_HELVETICA_12,
                                    (const unsigned char *)"Save");
      draw_text(save_btn_x + (save_btn_w - text_w) / 2,
                viewport_h - (save_btn_y + 9), "Save", 1.0f, 1.0f, 1.0f);
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
      draw_text(menu_x + menu_w / 2 - 45, screen_menu_top + 18, "IGI EDITOR",
                0.0f, 1.0f, 0.0f);
      draw_text(menu_x + menu_w / 2 - 35, screen_menu_top + 32, "PAUSED", 0.8f,
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
          draw_text(menu_x + menu_w / 2 - (int)(strlen(btn_labels[i]) * 4),
                    screen_btn_y, btn_labels[i], 1.0f, 1.0f, 1.0f);
        } else {
          draw_text(menu_x + menu_w / 2 - (int)(strlen(btn_labels[i]) * 4),
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
      draw_text(debug_x + 10, debug_y_text + 10, "DEBUG CONSOLE", font_r,
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
        draw_text(debug_x + 10, startY + count * line_height, msg.c_str(), r, g,
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
      draw_text(menu_x + menu_w / 2 - 40, menu_y + menu_h - 30, "KEYBINDINGS",
                font_r, font_g, font_b);

      // Help items
      int line_y = menu_y + menu_h - 60;
      draw_text(menu_x + 30, line_y, "[W/A/S/D] Movement", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text(menu_x + 30, line_y, "[F2] Terrain Edit Toggle", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text(menu_x + 30, line_y, "[F3] Clip Toggle", font_r, font_g,
                font_b);
      line_y -= 20;
      // Edit Mode toggle removed as requested
      draw_text(menu_x + 30, line_y, "[PageUp/Down] Move Speed", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text(menu_x + 30, line_y, "[Left/Right] Roll", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text(menu_x + 30, line_y, "[TAB] Select Next Object", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text(menu_x + 30, line_y, "[T] Teleport to Height Map", font_r,
                font_g, font_b);
      line_y -= 20;
      draw_text(menu_x + 30, line_y, "[L] HUD Toggle", font_r, font_g, font_b);
      line_y -= 20;
      draw_text(menu_x + 30, line_y, "[H/CTRL+H] Help Toggle", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text(menu_x + 30, line_y, "[S] Snap Object to Ground", font_r,
                font_g, font_b);
      line_y -= 20;
      draw_text(menu_x + 30, line_y, "[ESC] Pause Menu", font_r, font_g,
                font_b);
      line_y -= 30;
      draw_text(menu_x + 30, line_y, "PAUSE MENU:", font_r, font_g, font_b);
      line_y -= 20;
      draw_text(menu_x + 30, line_y, "[CTRL+S] Save Level", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text(menu_x + 30, line_y, "[CTRL+R] Reset Level", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text(menu_x + 30, line_y, "[SHIFT+R] Reset Script", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text(menu_x + 30, line_y, "[CTRL+D] Debug Toggle", font_r, font_g,
                font_b);
      line_y -= 20;
      draw_text(menu_x + 30, line_y, "[CTRL+Q] Exit", font_r, font_g, font_b);
      line_y -= 20;
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
