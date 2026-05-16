/******************************************************************************
 * @file    renderer.cpp
 * @brief   main renderer
 *   GL 4.1: need manually set binding point of uniform blocks
 *                             texture unit of sample object
 *   GL 4.5: binding point, texture unit can specified in shaders
 *****************************************************************************/

#include "pch.h"
#include "logger.h"
#include "config.h"
#include <glm/gtc/type_ptr.hpp>


#include <freeglut.h>

/*
================================================================================
 Renderer
================================================================================
*/
Renderer::Renderer():
	ubo_mats_(0),
	ubo_fog_(0),
	splines_(objects_)
{
	LoadBuildingNames();
}

Renderer::~Renderer() {
	Shutdown();
}

void Renderer::LoadBuildingNames() {
	char appDataPath[1024];
	GetEnvironmentVariableA("APPDATA", appDataPath, 1024);
	std::string jsonPath = std::string(appDataPath) + "\\QEditor\\IGIModelsAllLevel.json";

	FILE* f = fopen(jsonPath.c_str(), "rb");
	if (!f) {
		std::cerr << "[Renderer] Failed to open IGIModelsAllLevel.json at: " << jsonPath << std::endl;
		return;
	}

	fseek(f, 0, SEEK_END);
	long fileSize = ftell(f);
	fseek(f, 0, SEEK_SET);

	char* buf = new char[fileSize + 1];
	fread(buf, 1, fileSize, f);
	buf[fileSize] = '\0';
	fclose(f);

	std::string content(buf);
	delete[] buf;

	// Parse IGIModelsAllLevel.json structure
	// Format: { "Level 1": { "Objects": [...], "Buildings": [...] }, ... }
	size_t pos = 0;
	while ((pos = content.find("\"Level ", pos)) != std::string::npos) {
		size_t levelEnd = content.find("\"", pos + 7);
		if (levelEnd == std::string::npos) break;
		std::string levelStr = content.substr(pos + 7, levelEnd - pos - 7);
		int levelNum = std::stoi(levelStr);

		// Find Objects array for this level
		size_t objectsPos = content.find("\"Objects\"", levelEnd);
		if (objectsPos != std::string::npos) {
			size_t arrayStart = content.find("[", objectsPos);
			size_t arrayEnd = content.find("]", arrayStart);
			if (arrayStart != std::string::npos && arrayEnd != std::string::npos) {
				std::string objectsArray = content.substr(arrayStart, arrayEnd - arrayStart + 1);
				ParseLevelObjects(objectsArray, levelNum, false);
			}
		}

		// Find Buildings array for this level
		size_t buildingsPos = content.find("\"Buildings\"", levelEnd);
		if (buildingsPos != std::string::npos) {
			size_t arrayStart = content.find("[", buildingsPos);
			size_t arrayEnd = content.find("]", arrayStart);
			if (arrayStart != std::string::npos && arrayEnd != std::string::npos) {
				std::string buildingsArray = content.substr(arrayStart, arrayEnd - arrayStart + 1);
				ParseLevelObjects(buildingsArray, levelNum, true);
			}
		}

		pos = levelEnd + 1;
	}

	Logger::Get().Log(LogLevel::INFO, "[Renderer] Loaded " + std::to_string(building_names_.size()) + " building names and " + std::to_string(task_ids_.size()) + " task IDs from IGIModelsAllLevel.json");
}

void Renderer::ParseLevelObjects(const std::string& arrayContent, int levelNum, bool isBuilding) {
	size_t pos = 0;
	while ((pos = arrayContent.find("\"ModelID\"", pos)) != std::string::npos) {
		size_t idStart = arrayContent.find("\"", pos + 10) + 1;
		size_t idEnd = arrayContent.find("\"", idStart);
		if (idStart == std::string::npos || idEnd == std::string::npos) break;

		std::string modelId = arrayContent.substr(idStart, idEnd - idStart);

		// Find boundary of current JSON object (next ModelID or end)
		size_t nextModelIdPos = arrayContent.find("\"ModelID\"", idEnd);
		size_t objectEnd = (nextModelIdPos != std::string::npos) ? nextModelIdPos : arrayContent.length();

		// Find Name within this object only
		size_t namePos = arrayContent.find("\"Name\"", idEnd);
		if (namePos != std::string::npos && namePos < objectEnd) {
			size_t nameStart = arrayContent.find("\"", namePos + 7) + 1;
			size_t nameEnd = arrayContent.find("\"", nameStart);
			if (nameStart != std::string::npos && nameEnd != std::string::npos) {
				std::string name = arrayContent.substr(nameStart, nameEnd - nameStart);
				std::string key = std::to_string(levelNum) + "_" + modelId;
				building_names_[key] = name;
			}
		}

		// Find TaskID within this object only
		size_t taskIdPos = arrayContent.find("\"TaskID\"", idEnd);
		if (taskIdPos != std::string::npos && taskIdPos < objectEnd) {
			size_t taskIdStart = arrayContent.find("\"", taskIdPos + 9) + 1;
			size_t taskIdEnd = arrayContent.find("\"", taskIdStart);
			if (taskIdStart != std::string::npos && taskIdEnd != std::string::npos) {
				std::string taskId = arrayContent.substr(taskIdStart, taskIdEnd - taskIdStart);
				std::string key = std::to_string(levelNum) + "_" + modelId;
				task_ids_[key] = taskId;
			}
		}

		pos = idEnd + 1;
	}
}

std::string Renderer::GetBuildingName(const std::string& modelId) {
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

std::string Renderer::GetTaskId(const std::string& modelId) {
	std::string key = std::to_string(current_level_) + "_" + modelId;
	auto it = task_ids_.find(key);
	if (it != task_ids_.end()) {
		return it->second;
	}
	return "0";
}

bool Renderer::Init() {
	ubo_mats_ = GL_CreateBuffer(GL_UNIFORM_BUFFER, sizeof(ubo_mats_s), nullptr, GL_DYNAMIC_DRAW);
	ubo_fog_ = GL_CreateBuffer(GL_UNIFORM_BUFFER, sizeof(ubo_fog_s), nullptr, GL_STATIC_DRAW);

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
	glPolygonOffset(-64.0f, -64.0f);	// tune this
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

void Renderer::SetupClearColor(const glm::vec4& color) {
	glClearColor(color.r, color.g, color.b, 1.0f);
}

void Renderer::SetupFog(const glm::vec4& color, float fog_far) {
	ubo_fog_s ubo_fog;

	ubo_fog.color_ = color;
	ubo_fog.far_ = fog_far;

	GL_BufferData(ubo_fog_, GL_UNIFORM_BUFFER, sizeof(ubo_fog), &ubo_fog, GL_STATIC_DRAW);
}

void Renderer::SetupSkydome(const skydome_define_s& d) {
	skydome_.UpdateVertices(d);
}

void Renderer::LoadFlatSkyLayerTex(int layer_no, const pic_s* pic) {
	flat_sky_layers_.LoadLayerTex(layer_no, pic);
}

void Renderer::LoadTerrainMatTex(const pic_s* pic) {
	terrain_.LoadMatTex(pic);
}

void Renderer::LoadTerrainLMPTex(const pic_s* pic) {
	terrain_.LoadLMPTex(pic);
}

vert_flat_sky_layer_s* Renderer::MapFlatSkyLayersVB() {
	return flat_sky_layers_.MapVB();
}

void Renderer::UnmapFlatSkyLayersVB() {
	flat_sky_layers_.UnmapVB();
}

vert_pos_a_uv_s* Renderer::MapTerrainVB() {
	return terrain_.MapVB();
}

void Renderer::UnmapTerrainVB() {
	terrain_.UnmapVB();
}

uint32_t* Renderer::MapTerrainIB() {
	return terrain_.MapIB();
}

void Renderer::UnmapTerrainIB() {
	terrain_.UnmapIB();
}

render_chunk_s* Renderer::GetTerrainRenderChunckBuffer() {
	return terrain_.GetRenderChunckBuffer();
}

void Renderer::Draw(const draw_params_s& params, const hud_params_s& hud) {
    static bool logged_params = false;
    if (!logged_params) {
        Logger::Get().Log(LogLevel::INFO, "[Renderer] Draw Params: draw_parts=" + std::to_string(params.draw_parts_) + " level_objects=" + (params.level_objects_ ? "VALID" : "NULL"));
        logged_params = true;
    }
    SetupUBOMats(*params.view_define_);


        // start draw
        glDepthMask(GL_TRUE);   // insure clear depth buffer
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glViewport(0, 0, params.view_define_->viewport_width_, params.view_define_->viewport_height_);

        if (params.draw_parts_ & DRAW_SKYDOME) {
                skydome_.Draw(ubo_mats_, params.overlay_wireframe_);
        }

        if ((params.draw_parts_ & DRAW_FLAT_SKY_LAYER) && params.flat_sky_layer_is_visible_) {
                flat_sky_layers_.Draw(ubo_mats_, params.overlay_wireframe_);
        }

        if (params.draw_parts_ & DRAW_TERRAIN) {
                terrain_.Draw(ubo_mats_, ubo_fog_, params.overlay_wireframe_, params.draw_terrain_options_, params.num_terrain_render_chunk_);      
        }

        if ((params.draw_parts_ & (DRAW_OBJECTS | DRAW_BUILDINGS | DRAW_PROPS)) && params.level_objects_) {
                glMatrixMode(GL_PROJECTION);
                glLoadMatrixf(glm::value_ptr(mat_proj_));
                glMatrixMode(GL_MODELVIEW);
                glLoadMatrixf(glm::value_ptr(mat_view_));
                objects_.Draw(ubo_mats_, params.overlay_wireframe_, params.level_objects_->GetObjects(), params.selected_object_index_, params.draw_parts_);
                splines_.Draw(params.level_objects_->GetObjects(), ubo_mats_, objects_.GetShaderProgram());
        }



        if (hud.show_hud_) {
                glUseProgram(0); // Disable any active shaders for fixed-function HUD
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
                glOrtho(0, params.view_define_->viewport_width_, 0, params.view_define_->viewport_height_, -1, 1);

                glMatrixMode(GL_MODELVIEW);
                glPushMatrix();
                glLoadIdentity();

                auto draw_text = [&](int x, int y, const char* str, float r, float g, float b) {
                        std::stringstream ss(str);
                        std::string line;
                        int line_y = y;
                        while (std::getline(ss, line)) {
                            // Draw shadow
                            glColor3f(0.0f, 0.0f, 0.0f);
                            glRasterPos2i(x + 1, params.view_define_->viewport_height_ - line_y - 1);
                            for (char c : line) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, c);
                            
                            // Draw main text
                            glColor3f(r, g, b);
                            glRasterPos2i(x, params.view_define_->viewport_height_ - line_y);
                            for (char c : line) glutBitmapCharacter(GLUT_BITMAP_HELVETICA_12, c);
                            
                            line_y += 15; // Vertical spacing
                        }
                };

                auto draw_text_mono = [&](int x, int y, const char* str, float r, float g, float b) {
                        std::stringstream ss(str);
                        std::string line;
                        int line_y = y;
                        while (std::getline(ss, line)) {
                            // Draw shadow
                            glColor3f(0.0f, 0.0f, 0.0f);
                            glRasterPos2i(x + 1, params.view_define_->viewport_height_ - line_y - 1);
                            for (char c : line) glutBitmapCharacter(GLUT_BITMAP_9_BY_15, c);
                            
                            // Draw main text
                            glColor3f(r, g, b);
                            glRasterPos2i(x, params.view_define_->viewport_height_ - line_y);
                            for (char c : line) glutBitmapCharacter(GLUT_BITMAP_9_BY_15, c);
                            
                            line_y += 18; // Vertical spacing for 9x15
                        }
                };

                // Helper to convert KeyBinding to display string
                auto keybinding_to_string = [&](const KeyBinding& kb) -> std::string {
                        std::string result;
                        if (kb.ctrl) result += "CTRL+";
                        if (kb.shift) result += "SHIFT+";
                        if (kb.alt) result += "ALT+";
                        
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
                ConfigData& cfg = Config::Get();
                float font_r = cfg.fontColorR / 255.0f;
                float font_g = cfg.fontColorG / 255.0f;
                float font_b = cfg.fontColorB / 255.0f;

                int line_y = 30;
                
                // --- TreeView HUD Implementation ---
                if (hud.level_objects_) {
                    const auto& objects = hud.level_objects_->GetObjects();
                    int tree_x = 20;
                    int tree_y = 30; // Starting Y for tree
                    int row_h = 16;
                    int start_y = 30;
                    int current_row = 0;
                    int scroll_offset = hud.tree_scroll_offset;
                    int viewport_h = params.view_define_->viewport_height_;

                    // Recursive helper to draw tree nodes
                current_row = 0;
                std::function<void(int, int)> draw_node = [&](int idx, int depth) {
                        if (idx < 0 || idx >= (int)objects.size()) return;
                        const auto& obj = objects[idx];
                        if (obj.deleted) return;

                        int x = tree_x + (depth * 18);
                        int y = start_y + (current_row - hud.tree_scroll_offset) * row_h;
                        current_row++;

                        if (y >= start_y && y < params.view_define_->viewport_height_ - 50) {
                                // Highlight if selected or hovered
                                if (idx == hud.selected_object_index_) {
                                        glEnable(GL_BLEND);
                                        glColor4f(0.0f, 0.5f, 0.0f, 0.4f);
                                        glBegin(GL_QUADS);
                                        glVertex2i(tree_x - 5, params.view_define_->viewport_height_ - (y - 2));
                                        glVertex2i(tree_x + 300, params.view_define_->viewport_height_ - (y - 2));
                                        glVertex2i(tree_x + 300, params.view_define_->viewport_height_ - (y + row_h - 2));
                                        glVertex2i(tree_x - 5, params.view_define_->viewport_height_ - (y + row_h - 2));
                                        glEnd();
                                } else if (idx == hud.hover_tree_index_) {
                                        glEnable(GL_BLEND);
                                        glColor4f(0.3f, 0.3f, 0.3f, 0.3f);
                                        glBegin(GL_QUADS);
                                        glVertex2i(tree_x - 5, params.view_define_->viewport_height_ - (y - 2));
                                        glVertex2i(tree_x + 300, params.view_define_->viewport_height_ - (y - 2));
                                        glVertex2i(tree_x + 300, params.view_define_->viewport_height_ - (y + row_h - 2));
                                        glVertex2i(tree_x - 5, params.view_define_->viewport_height_ - (y + row_h - 2));
                                        glEnd();
                                }
                            
                            // Draw Hierarchy Line (dotted vertical)
                            if (depth > 0) {
                                glLineStipple(1, 0xAAAA);
                                glEnable(GL_LINE_STIPPLE);
                                glColor3f(0.5f, 0.5f, 0.5f);
                                glBegin(GL_LINES);
                                // Vertical line from parent down to this node
                                glVertex2i(x - 9, viewport_h - (y - row_h/2));
                                glVertex2i(x - 9, viewport_h - (y + row_h/2));
                                // Horizontal line to the node
                                glVertex2i(x - 9, viewport_h - (y + row_h/2));
                                glVertex2i(x - 2, viewport_h - (y + row_h/2));
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
                                if (!obj.name.empty()) label += ", \"" + obj.name + "\"";
                                label += ")";
                            } else if (!obj.name.empty()) {
                                label += " (\"" + obj.name + "\")";
                            }

                            float tr = font_r, tg = font_g, tb = font_b;
                            if (idx == hud.selected_object_index_) { tr = 1.0f; tg = 1.0f; tb = 0.0f; } // Selected = Yellow
                            else if (idx == hud.hover_object_index_) { tr = 0.5f; tg = 0.8f; tb = 1.0f; } // Hover = Blue
                            
                            // Note: We use y + 11 for draw_text so the baseline aligns correctly with the hitbox
                            draw_text(x + 16, y + 11, label.c_str(), tr, tg, tb);
                        }

                        if (obj.expanded) {
                            for (int childIdx : obj.childrenIndices) {
                                draw_node(childIdx, depth + 1);
                            }
                        }
                    };

                    // To keep the root clean, group all Task_DeclareParameters into a virtual "Mission Declarations" folder
                    std::vector<int> root_decls;
                    std::vector<int> root_others;

                    for (int i = 0; i < (int)objects.size(); ++i) {
                        if (objects[i].parentIndex == -1 && !objects[i].deleted) {
                            if (objects[i].type == "Task_DeclareParameters") root_decls.push_back(i);
                            else root_others.push_back(i);
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
                            if (!hud.tree_decl_expanded) {
                                glVertex2i(vx - 10, viewport_h - (y + 6));
                                glVertex2i(vx - 10, viewport_h - (y + 10));
                            }
                            glEnd();

                            float dtr = 0.7f, dtg = 0.7f, dtb = 0.7f;
                            if (hud.hover_object_index_ == -2) { dtr = 0.5f; dtg = 0.8f; dtb = 1.0f; }
                            draw_text(vx + 16, y + 11, "Mission Declarations", dtr, dtg, dtb);
                        }
                        if (hud.tree_decl_expanded) {
                            for (int idx : root_decls) draw_node(idx, 1);
                        }
                    }

                    for (int idx : root_others) {
                        draw_node(idx, 0);
                    }
                    
                    line_y = start_y + (current_row - scroll_offset) * row_h + 20;
                }

                // Show Editor/Object Telemetry (Only if debug is enabled and explicitly requested)
                // Removed live info as requested

                // Display object info at mouse position
                int info_object_index = hud.hover_object_index_;
                if (info_object_index >= 0 && hud.level_objects_) {
                        const auto& objects = hud.level_objects_->GetObjects();
                        if (info_object_index < (int)objects.size()) {
                                const auto& obj = objects[info_object_index];

                                // Skip labels for skipped models (Poles, Wires, etc.)
                                if (Renderer_Objects::IsSkippedModelId(obj.modelId)) {
                                    info_object_index = -1; // Reset to hide label
                                }
                        }
                }

                if (info_object_index >= 0 && hud.level_objects_) {
                        const auto& objects = hud.level_objects_->GetObjects();
                        if (info_object_index < (int)objects.size()) {
                                const auto& obj = objects[info_object_index];

                                char buf[512];
                                std::string display_name = obj.name;
                                if (display_name.empty()) {
                                        display_name = GetBuildingName(obj.modelId);
                                }
                                if (display_name.empty()) {
                                        display_name = obj.modelId;
                                }

                                std::string task_id = obj.taskId.empty() ? "-1" : obj.taskId;

                                int text_x = hud.mouse_x_;
                                int text_y = hud.mouse_y_ + 25;
                                if (text_x < 0) text_x = 0;
                                if (text_x > params.view_define_->viewport_width_ - 200) text_x = params.view_define_->viewport_width_ - 200;
                                if (text_y < 0) text_y = 0;
                                if (text_y > params.view_define_->viewport_height_ - 50) text_y = params.view_define_->viewport_height_ - 50;

                                // Check if this is an AI model (ID starts with 000-019)
                                bool isAI = false;
                                if (obj.modelId.size() >= 3) {
                                        try {
                                                int prefixNum = std::stoi(obj.modelId.substr(0, 3));
                                                if (prefixNum >= 0 && prefixNum <= 19) {
                                                        isAI = true;
                                                }
                                        } catch (...) {}
                                }

                                if (isAI) {
                                        snprintf(buf, sizeof(buf), "Type: \"%s\"", obj.type.c_str());
                                        draw_text(text_x, text_y, buf, 1.0f, 1.0f, 1.0f);
                                        text_y += 15;
                                        snprintf(buf, sizeof(buf), "Name: \"%s\"", display_name.c_str());
                                        draw_text(text_x, text_y, buf, 1.0f, 1.0f, 1.0f);

                                        text_y += 20;
                                        snprintf(buf, sizeof(buf), "Soldier ID: %s", task_id.c_str());
                                        draw_text(text_x, text_y, buf, 1.0f, 1.0f, 0.0f);
                                        text_y += 15;
                                        snprintf(buf, sizeof(buf), "AI ID: %s", obj.aiId.c_str());
                                        draw_text(text_x, text_y, buf, 0.0f, 1.0f, 1.0f);

                                        text_y += 20;
                                        snprintf(buf, sizeof(buf), "Position: X: %.7f Y: %.7f Z: %.7f", obj.pos.x, obj.pos.y, obj.pos.z);
                                        draw_text(text_x, text_y, buf, 1.0f, 1.0f, 1.0f);

                                        text_y += 20;
                                        snprintf(buf, sizeof(buf), "Rotation: %.10f", obj.rot.z);
                                        draw_text(text_x, text_y, buf, 1.0f, 1.0f, 1.0f);

                                        text_y += 20;
                                        snprintf(buf, sizeof(buf), "Model: ID: \"%s\" Name: \"%s\"", obj.modelId.c_str(), obj.type.c_str());
                                        draw_text(text_x, text_y, buf, 0.0f, 1.0f, 1.0f);

                                        text_y += 20;
                                        snprintf(buf, sizeof(buf), "Graph: ID: %s Name: \"%s\"", obj.graphId.c_str(), obj.graphName.c_str());
                                        draw_text(text_x, text_y, buf, 0.0f, 1.0f, 1.0f);

                                        text_y += 20;
                                        snprintf(buf, sizeof(buf), "Graph Position: X: %.7f Y: %.7f Z: %.7f", obj.graphPos.x, obj.graphPos.y, obj.graphPos.z);
                                        draw_text(text_x, text_y, buf, 1.0f, 1.0f, 1.0f);

                                        text_y += 20;
                                        snprintf(buf, sizeof(buf), "Primary Weapon: Name: \"%s\" Ammo: %s", obj.primaryWeapon.c_str(), obj.primaryAmmo.empty() ? "0" : obj.primaryAmmo.c_str());
                                        draw_text(text_x, text_y, buf, 1.0f, 0.5f, 0.0f);

                                        text_y += 20;
                                        snprintf(buf, sizeof(buf), "Secondary Weapon: Name: \"%s\" Ammo: %s", obj.secondaryWeapon.c_str(), obj.secondaryAmmo.empty() ? "0" : obj.secondaryAmmo.c_str());
                                        draw_text(text_x, text_y, buf, 1.0f, 0.5f, 0.0f);

                                        text_y += 20;
                                        snprintf(buf, sizeof(buf), "Team: %s", obj.team == 0 ? "Friendly" : "Enemy");
                                        draw_text(text_x, text_y, buf, obj.team == 0 ? 0.0f : 1.0f, obj.team == 0 ? 1.0f : 0.0f, 0.0f);
                                } else {
                                        // Line 1: ModelName ID: Task_New first param
                                        snprintf(buf, sizeof(buf), "%s ID: %s", display_name.c_str(), task_id.c_str());
                                        draw_text(text_x, text_y, buf, 1.0f, 1.0f, 1.0f);

                                        // Line 2: Full ModelID
                                        text_y += 15;
                                        snprintf(buf, sizeof(buf), "%s", obj.modelId.c_str());
                                        draw_text(text_x, text_y, buf, obj.isBuilding ? 1.0f : 0.0f, 1.0f, 0.0f);
                                }
                        }
                }

                if (!hud.status_msg_.empty()) {
                        int status_x = 20;
                        int status_y = 18;
                        draw_text(status_x, status_y, hud.status_msg_.c_str(), 0.85f, 1.0f, 0.6f);
                }

                // Watermark
                int w_width = glutBitmapLength(GLUT_BITMAP_HELVETICA_12, (const unsigned char*)"IGI Editor Copyright - JonesHM");
                int w_x = (params.view_define_->viewport_width_ - w_width) / 2;
                draw_text(w_x, params.view_define_->viewport_height_ - 20, "IGI Editor Copyright - JonesHM", 0.7f, 0.7f, 0.7f);

                if (hud.task_editor_open_) {
                        // Render Task Editor Box
                        int box_w = hud.edit_box_w_;
                        int box_h = hud.edit_box_h_;
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
                        draw_text(box_x + 10, viewport_h - (box_y + box_h - 20), "Task Editor (ESC: Save, Arrows: Resize)", 1.0f, 1.0f, 1.0f);
                        draw_text(box_x + 10, viewport_h - (box_y + box_h - 40), "Contents:", 0.6f, 0.6f, 0.6f);
                        
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

                        int scroll_offset_px = hud.edit_scroll_x_ * 9;

                        // Selection Highlight
                        if (hud.edit_selection_start_ != -1 && hud.edit_selection_end_ != -1 && hud.edit_selection_start_ != hud.edit_selection_end_) {
                                int s = std::min(hud.edit_selection_start_, hud.edit_selection_end_);
                                int e = std::max(hud.edit_selection_start_, hud.edit_selection_end_);
                                int sel_x1 = box_x + 20 + (s * 9) - scroll_offset_px;
                                int sel_x2 = box_x + 20 + (e * 9) - scroll_offset_px;
                                
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
                        int cursor_x = box_x + 20 + (hud.edit_cursor_pos_ * 9) - scroll_offset_px;
                        glColor3f(1.0f, 1.0f, 1.0f); // White cursor
                        glLineWidth(2.0f);
                        glBegin(GL_LINES);
                        glVertex2i(cursor_x, box_y + 28);
                        glVertex2i(cursor_x, box_y + 42);
                        glEnd();
                        glLineWidth(1.0f);

                        // Draw text (Scrolled)
                        std::string displayString = hud.edit_string_;
                        displayString.erase(std::remove(displayString.begin(), displayString.end(), '\r'), displayString.end());
                        displayString.erase(std::remove(displayString.begin(), displayString.end(), '\n'), displayString.end());

                        draw_text_mono(box_x + 20 - scroll_offset_px, viewport_h - (box_y + 35), displayString.c_str(), 1.0f, 1.0f, 1.0f);
                        
                        glDisable(GL_SCISSOR_TEST);
                }

                if (hud.edit_mode_) {
                        // Flip Y because glOrtho has y=0 at bottom, but mouse_y is top-down (GLUT)
                        float cx = (float)(hud.mouse_x_);
                        float cy = (float)(params.view_define_->viewport_height_ - hud.mouse_y_);

                        if (hud.enable_camera_mode_) {
                                // Draw camera icon at center (since mouse is warped there)
                                float ccx = (float)(params.view_define_->viewport_width_ / 2);
                                float ccy = (float)(params.view_define_->viewport_height_ / 2);
                                float w = 16.0f, h = 10.0f;
                                glColor3f(0.4f, 0.7f, 1.0f); // Sky blue
                                glLineWidth(2.5f);
                                // Camera body
                                glBegin(GL_LINE_LOOP);
                                glVertex2f(ccx - w/2, ccy - h/2);
                                glVertex2f(ccx + w/2, ccy - h/2);
                                glVertex2f(ccx + w/2, ccy + h/2);
                                glVertex2f(ccx - w/2, ccy + h/2);
                                glEnd();
                                // Lens (inner circle-ish)
                                glBegin(GL_LINE_LOOP);
                                for(int i=0; i<8; ++i) {
                                    float a = i * 6.28f / 8.0f;
                                    glVertex2f(ccx + cosf(a)*3, ccy + sinf(a)*3);
                                }
                                glEnd();
                                // Viewfinder
                                glBegin(GL_LINE_LOOP);
                                glVertex2f(ccx - 3, ccy + h/2);
                                glVertex2f(ccx + 3, ccy + h/2);
                                glVertex2f(ccx + 4, ccy + h/2 + 3);
                                glVertex2f(ccx - 4, ccy + h/2 + 3);
                                glEnd();
                                glLineWidth(1.0f);
                        } else if (hud.terrain_edit_enabled_) {
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
                                glVertex2f(cx - csz, cy); glVertex2f(cx + csz, cy);
                                glVertex2f(cx, cy - csz); glVertex2f(cx, cy + csz);
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
                        glVertex2f(cx - w/2, cy - h/2);
                        glVertex2f(cx + w/2, cy - h/2);
                        glVertex2f(cx + w/2, cy + h/2);
                        glVertex2f(cx - w/2, cy + h/2);
                        glEnd();
                        // Lens (inner rect)
                        glBegin(GL_LINE_LOOP);
                        glVertex2f(cx - w/5, cy - h/5);
                        glVertex2f(cx + w/5, cy - h/5);
                        glVertex2f(cx + w/5, cy + h/5);
                        glVertex2f(cx - w/5, cy + h/5);
                        glEnd();
                        // Viewfinder bump on top
                        glBegin(GL_LINE_LOOP);
                        glVertex2f(cx - w/5, cy - h/2);
                        glVertex2f(cx + w/5, cy - h/2);
                        glVertex2f(cx + w/5 + 2, cy - h/2 - 4);
                        glVertex2f(cx - w/5 - 2, cy - h/2 - 4);
                        glEnd();
                        glLineWidth(1.0f);
                }

                if (hud.pause_mode_) {
                        const int menu_w = 380;
                        const int menu_h = 280;
                        const int menu_x = (params.view_define_->viewport_width_  - menu_w) / 2;
                        const int menu_y = (params.view_define_->viewport_height_ - menu_h) / 2;
                        const int viewport_h = params.view_define_->viewport_height_;

                        // Glassmorphism-style background
                        glEnable(GL_BLEND);
                        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                        glColor4f(0.02f, 0.15f, 0.02f, 0.94f); // Deep emerald
                        glBegin(GL_QUADS);
                        glVertex2i(menu_x,          menu_y);
                        glVertex2i(menu_x + menu_w, menu_y);
                        glVertex2i(menu_x + menu_w, menu_y + menu_h);
                        glVertex2i(menu_x,          menu_y + menu_h);
                        glEnd();
                        glDisable(GL_BLEND);

                        // Sharp green border
                        glLineWidth(2.5f);
                        glColor3f(0.0f, 1.0f, 0.0f);
                        glBegin(GL_LINE_LOOP);
                        glVertex2i(menu_x,          menu_y);
                        glVertex2i(menu_x + menu_w, menu_y);
                        glVertex2i(menu_x + menu_w, menu_y + menu_h);
                        glVertex2i(menu_x,          menu_y + menu_h);
                        glEnd();

                        // Header separator
                        glBegin(GL_LINES);
                        glVertex2i(menu_x + 10,     menu_y + menu_h - 45);
                        glVertex2i(menu_x + menu_w - 10, menu_y + menu_h - 45);
                        glEnd();
                        glLineWidth(1.0f);

                        int screen_menu_top = (viewport_h - menu_h) / 2;
                        draw_text(menu_x + menu_w/2 - 45, screen_menu_top + 18, "IGI EDITOR", 0.0f, 1.0f, 0.0f);
                        draw_text(menu_x + menu_w/2 - 35, screen_menu_top + 32, "PAUSED", 0.8f, 0.8f, 0.8f);

                        const char* btn_labels[] = { "Resume", "Debug", "Reset Level", "Save Level", "Quit" };
                        const int NUM_BTNS = 5;

                        for (int i = 0; i < NUM_BTNS; ++i) {
                                int screen_btn_y = screen_menu_top + 80 + i * 35;
                                int gl_btn_y     = viewport_h - screen_btn_y;

                                bool hovered = (hud.mouse_x_ >= menu_x && hud.mouse_x_ <= menu_x + menu_w &&
                                                hud.mouse_y_ >= screen_btn_y - 15 && hud.mouse_y_ <= screen_btn_y + 15);

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
                                        draw_text(menu_x + menu_w/2 - (int)(strlen(btn_labels[i]) * 4),
                                                  screen_btn_y, btn_labels[i], 1.0f, 1.0f, 1.0f);
                                } else {
                                        draw_text(menu_x + menu_w/2 - (int)(strlen(btn_labels[i]) * 4),
                                                  screen_btn_y, btn_labels[i], 0.0f, 0.85f, 0.0f);
                                }
                        }
                }


                if (hud.show_debug_) {
                        const auto& entries = Logger::Get().GetEntries();
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
                        draw_text(debug_x + 10, debug_y_text + 10, "DEBUG CONSOLE", font_r, font_g, font_b);

                        int startY = debug_y_text + 30; // Just below title, inside the frame
                        int line_height = 12;
                        int count = 0;
                        int max_lines = (debug_h - 50) / line_height;
                        int max_chars = (debug_w - 20) / 7;

                        for (auto it = entries.rbegin(); it != entries.rend() && count < max_lines; ++it, ++count) {
                                float r = 1.0f, g = 1.0f, b = 1.0f;
                                if (it->level == LogLevel::ERR) { r = 1.0f; g = 0.2f; b = 0.2f; }
                                else if (it->level == LogLevel::FATAL) { r = 1.0f; g = 0.0f; b = 0.0f; }
                                else if (it->level == LogLevel::WARNING) { r = 1.0f; g = 1.0f; b = 0.0f; }

                                // Text truncation based on box width
                                std::string msg = it->message;
                                if (msg.length() > max_chars) {
                                        msg = msg.substr(0, max_chars - 3) + "...";
                                }
                                draw_text(debug_x + 10, startY + count * line_height, msg.c_str(), r, g, b);
                        }
                }

                if (hud.show_help_) {
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
                        draw_text(menu_x + menu_w/2 - 40, menu_y + menu_h - 30, "KEYBINDINGS", font_r, font_g, font_b);

                        // Help items
                        int line_y = menu_y + menu_h - 60;
                        draw_text(menu_x + 30, line_y, "[W/A/S/D] Movement", font_r, font_g, font_b); line_y -= 20;
                        draw_text(menu_x + 30, line_y, "[F2] Terrain Edit Toggle", font_r, font_g, font_b); line_y -= 20;
                        draw_text(menu_x + 30, line_y, "[F3] Clip Toggle", font_r, font_g, font_b); line_y -= 20;
                        // Edit Mode toggle removed as requested
                        draw_text(menu_x + 30, line_y, "[PageUp/Down] Move Speed", font_r, font_g, font_b); line_y -= 20;
                        draw_text(menu_x + 30, line_y, "[Left/Right] Roll", font_r, font_g, font_b); line_y -= 20;
                        draw_text(menu_x + 30, line_y, "[TAB] Select Next Object", font_r, font_g, font_b); line_y -= 20;
                        draw_text(menu_x + 30, line_y, "[T] Teleport to Height Map", font_r, font_g, font_b); line_y -= 20;
                        draw_text(menu_x + 30, line_y, "[L] HUD Toggle", font_r, font_g, font_b); line_y -= 20;
                        draw_text(menu_x + 30, line_y, "[H/CTRL+H] Help Toggle", font_r, font_g, font_b); line_y -= 20;
                        draw_text(menu_x + 30, line_y, "[S] Snap Object to Ground", font_r, font_g, font_b); line_y -= 20;
                        draw_text(menu_x + 30, line_y, "[ESC] Pause Menu", font_r, font_g, font_b); line_y -= 30;
                        draw_text(menu_x + 30, line_y, "PAUSE MENU:", font_r, font_g, font_b); line_y -= 20;
                        draw_text(menu_x + 30, line_y, "[CTRL+S] Save Level", font_r, font_g, font_b); line_y -= 20;
                        draw_text(menu_x + 30, line_y, "[CTRL+R] Reset Level", font_r, font_g, font_b); line_y -= 20;
                        draw_text(menu_x + 30, line_y, "[SHIFT+R] Reset Script", font_r, font_g, font_b); line_y -= 20;
                        draw_text(menu_x + 30, line_y, "[CTRL+D] Debug Toggle", font_r, font_g, font_b); line_y -= 20;
                        draw_text(menu_x + 30, line_y, "[CTRL+Q] Exit", font_r, font_g, font_b); line_y -= 20;
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

void Renderer::SetupUBOMats(const view_define_s& vd) {
	glm::mat4 mat_proj_persp = glm::perspective(vd.fovy_, (float)vd.viewport_width_ / vd.viewport_height_, vd.render_z_near_, vd.render_z_far_);

	glm::vec3 scaled_down_pos = vd.pos_ * RENDERER_MODEL_SCALE_DOWN;
	glm::mat4 mat_view = glm::lookAt(scaled_down_pos, scaled_down_pos + vd.forward_ * WORLD_UNITS_PER_METER, vd.up_);
	glm::mat4 mat_follow_view = glm::lookAt(VEC3_ORIGIN, vd.forward_, vd.up_);
	glm::mat4 mat_scale = glm::scale(glm::mat4(1.0f), glm::vec3(RENDERER_MODEL_SCALE_DOWN));

	mat_proj_ = mat_proj_persp;
	mat_view_ = mat_view;

	// setup uniform buffer

	ubo_mats_s ubo_mats;

	// skydome
	ubo_mats.mvp_mat_follow_view_ = mat_proj_persp * mat_follow_view * mat_scale;

	// flat sky layer
	ubo_mats.mvp_flat_sky_layer_ = glm::ortho(0.0f, (float)vd.viewport_width_, 0.0f, (float)vd.viewport_height_, -1.0f, 1.0f);

	// terrain
	ubo_mats.mvp_objects_ = mat_proj_persp * mat_view * mat_scale;

	// flush ubo buffer
	GL_BufferData(ubo_mats_, GL_UNIFORM_BUFFER, sizeof(ubo_mats), &ubo_mats, GL_DYNAMIC_DRAW);
}
