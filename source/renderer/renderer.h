/******************************************************************************
 * @file    renderer.h
 * @brief   main renderer
 *****************************************************************************/

#pragma once

#include <string>
#include <set>
#include <unordered_map>
#include <vector>
#include <utility>
#include "renderer_objects.h"
#include "renderer_splines.h"
#include "../level/task_schema.h"
#include <functional>

/*
================================================================================
 Property-panel layout — single source of truth shared by the renderer (drawing)
 and the input handler (hit-testing). All rects are in SCREEN top-down pixels
 (same coordinate space as GLUT mouse x,y). The renderer converts to GL bottom-up
 by gl_y = viewport_h - screen_y.
================================================================================
*/
namespace PropPanel {

enum class WidgetKind {
    NoteBox,       // editable note (obj.name)
    PosPad,        // 2D X/Y pad
    PosZSlider,    // vertical Z slider
    SnapGround,    // button
    SnapObject,    // button
    OriSlider,     // horizontal orientation slider (Real32x9 component)
    RgbSlider,     // horizontal RGB slider (0..1) with swatch
    NumSlider,     // horizontal numeric slider (Real/Angle/Degrees)
    NumBox,        // editable numeric input box (Int/Real X/Y/Z) — click to type, drag to scrub
    StringBox,     // editable text box (String*/VarString)
    Checkbox,      // bool8 / PushButton
    ChildHeader,   // non-interactive separator label for a child task section
    AIScriptPath,  // single-line editable: resolved .qvm file path
    AIScriptText,  // multiline editable: decompiled QSC source
};

struct Widget {
    WidgetKind kind;
    int x1, y1, x2, y2;   // screen top-down rect
    int fieldIndex = -1;  // schema field index (-1 for note/snap)
    int comp       = 0;   // sub-component (0=X/Alpha/R, 1=Y/Beta/G, 2=Z/Gamma/B)
    int objIndex   = -1;  // owning LevelObject index; -1 = the selected/parent object
};

struct Layout {
    int panel_x = 8, panel_y = 8, panel_w = 320, panel_h = 0;
    std::vector<Widget> widgets;
};

// Sentinel prop_text_edit_field_ values for AI script widgets.
// Must not collide with fi*3+comp range (always >= 0 for normal fields).
static constexpr int kAIScriptPathField = -10;
static constexpr int kAIScriptTextField = -11;

// Layout constants (kept in one place so draw + hit-test agree exactly).
static constexpr int kLeft       = 8;
static constexpr int kTop        = 8;
static constexpr int kWidth      = 460;
static constexpr int kPad        = 8;
static constexpr int kRowH       = 16;   // text line height (matches draw_text spacing)
static constexpr int kBoxH       = 18;   // editable box / slider row height
static constexpr int kPadSize    = 100;  // 2D pad square
static constexpr int kZSliderW   = 22;   // vertical Z slider width

// Build the layout for one task type's schema. `is_multi` types (ObjectPos /
// Real32x9 / RGB) expand to multiple sub-rows. Returns rows' y positions implicitly
// via widget rects; panel_h is the total height.
inline Layout BuildLayout(const TaskSchemaNS::TaskSchema& schema, bool is_ai = false,
                          const std::vector<std::pair<int, const TaskSchemaNS::TaskSchema*>>& children = {}) {
    using namespace TaskSchemaNS;
    Layout L;
    L.panel_x = kLeft; L.panel_y = kTop; L.panel_w = kWidth;

    int y = kTop + kPad;

    // Right-side editable box geometry (used for X/Y/Z, integer & float boxes).
    const int boxW = 110;
    const int boxX2 = kLeft + kWidth - kPad;
    const int boxX1 = boxX2 - boxW;

    // Emit every field widget for one schema. Used for BOTH the parent task and
    // each child task so they share an identical interface (pads/sliders/boxes).
    // All widgets pushed during the call are tagged with `objIndex` (-1 = parent).
    auto emitFields = [&](const TaskSchemaNS::TaskSchema& sch, bool ai, int objIndex) {
        size_t start = L.widgets.size();
        for (int fi = 0; fi < (int)sch.size(); ++fi) {
            const FieldDef& fd = sch[fi];
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

            y += kRowH;  // field header line

            if (is_pos) {
                const int pBoxX1 = kLeft + kPad + 20;
                const int pBoxX2 = pBoxX1 + 180;
                for (int c = 0; c < 3; ++c) {
                    L.widgets.push_back({WidgetKind::NumBox, pBoxX1, y, pBoxX2, y + kBoxH, fi, c});
                    y += kBoxH + 2;
                }
                int pad_x1 = kLeft + kPad;
                int pad_y1 = y;
                L.widgets.push_back({WidgetKind::PosPad, pad_x1, pad_y1,
                                     pad_x1 + kPadSize, pad_y1 + kPadSize, fi, 0});
                int zs_x1 = pad_x1 + kPadSize + 10;
                L.widgets.push_back({WidgetKind::PosZSlider, zs_x1, pad_y1,
                                     zs_x1 + kZSliderW, pad_y1 + kPadSize, fi, 2});
                y += kPadSize + 6;
                int bw = (kWidth - 2 * kPad - 8) / 2;
                L.widgets.push_back({WidgetKind::SnapGround, kLeft + kPad, y,
                                     kLeft + kPad + bw, y + kBoxH, -1, 0});
                L.widgets.push_back({WidgetKind::SnapObject, kLeft + kPad + bw + 8, y,
                                     kLeft + kPad + bw + 8 + bw, y + kBoxH, -1, 0});
                y += kBoxH + 4;
                y += kRowH;  // "Altitude: ... meter"
            } else if (is_float3) {
                const int pBoxX1 = kLeft + kPad + 20;
                const int pBoxX2 = pBoxX1 + 180;
                for (int c = 0; c < 3; ++c) {
                    L.widgets.push_back({WidgetKind::NumBox, pBoxX1, y, pBoxX2, y + kBoxH, fi, c});
                    y += kBoxH + 2;
                }
            } else if (is_ori) {
                int sx1 = kLeft + kPad + 64;
                int sx2 = kLeft + kWidth - kPad;
                int c_start = ai ? 2 : 0;
                for (int c = c_start; c < 3; ++c) {
                    L.widgets.push_back({WidgetKind::OriSlider, sx1, y, sx2, y + kBoxH, fi, c});
                    y += kBoxH;
                }
            } else if (is_rgb) {
                for (int c = 0; c < 3; ++c) {
                    int sx1 = kLeft + kPad + 64;
                    int sx2 = kLeft + kWidth - kPad - 24;
                    L.widgets.push_back({WidgetKind::RgbSlider, sx1, y, sx2, y + kBoxH, fi, c});
                    y += kBoxH;
                }
            } else if (is_str) {
                int h = (tn == "VarString" || tn == "String256") ? kBoxH * 3 : kBoxH;
                L.widgets.push_back({WidgetKind::StringBox, kLeft + kPad, y,
                                     kLeft + kWidth - kPad, y + h, fi, 0});
                y += h + 2;
            } else if (is_bool) {
                L.widgets.push_back({WidgetKind::Checkbox, kLeft + kPad, y,
                                     kLeft + kWidth - kPad, y + kBoxH, fi, 0});
                y += kBoxH;
            } else if (is_ro) {
                y += kRowH;  // read-only grey value line, no widget
            } else if (is_int) {
                L.widgets.push_back({WidgetKind::NumBox, boxX1, y, boxX2, y + kBoxH, fi, 0});
                y += kBoxH;
            } else {
                int sx1 = kLeft + kPad;
                int sx2 = boxX1 - 8;
                L.widgets.push_back({WidgetKind::NumSlider, sx1, y, sx2, y + kBoxH, fi, 0});
                L.widgets.push_back({WidgetKind::NumBox, boxX1, y, boxX2, y + kBoxH, fi, 0});
                y += kBoxH;
            }
            y += 4;  // gap between fields
        }
        if (objIndex != -1)
            for (size_t i = start; i < L.widgets.size(); ++i) L.widgets[i].objIndex = objIndex;
    };

    // Parent: type header + note box + fields.
    y += kRowH;            // "QTasktype: <type>"
    y += kRowH;            // "QTask Note (QTaskNote):"
    L.widgets.push_back({WidgetKind::NoteBox, kLeft + kPad, y, kLeft + kWidth - kPad, y + kBoxH, -1, 0});
    y += kBoxH + 6;
    emitFields(schema, is_ai, -1);

    // Children: a header label, then the SAME field widgets as a parent (routed
    // to the child object via objIndex). Only present when the task has children.
    for (const auto& [childIdx, cscp] : children) {
        if (!cscp) continue;
        L.widgets.push_back({WidgetKind::ChildHeader, kLeft + kPad, y,
                             kLeft + kWidth - kPad, y + kRowH, -1, 0, childIdx});
        y += kRowH + 2;
        emitFields(*cscp, false, childIdx);
        y += 4;
    }

    // AI Script section — only for AI tasks (HumanSoldier, HumanAI, etc.)
    if (is_ai) {
        y += kRowH;  // "AI Script Path:" label line
        L.widgets.push_back({WidgetKind::AIScriptPath,
                             kLeft + kPad, y, kLeft + kWidth - kPad, y + kBoxH,
                             kAIScriptPathField, 0});
        y += kBoxH + 6;

        y += kRowH;  // "AI Script:" label line
        const int scriptH = kBoxH * 12;
        L.widgets.push_back({WidgetKind::AIScriptText,
                             kLeft + kPad, y, kLeft + kWidth - kPad, y + scriptH,
                             kAIScriptTextField, 0});
        y += scriptH + 6;
    }

    L.panel_h = (y + kPad) - kTop;
    return L;
}

} // namespace PropPanel


/*
================================================================================
 Renderer
================================================================================
*/
class Renderer : public IRenderResLoader {
public:

	// draw parts
	static constexpr int	DRAW_TERRAIN = FLAG_BIT(0);        // 1
	static constexpr int	DRAW_SKY = FLAG_BIT(1);            // 2
	static constexpr int	DRAW_OBJECTS = FLAG_BIT(2);        // 4
	static constexpr int	DRAW_FLAT_SKY_LAYER = FLAG_BIT(3); // 8
    static constexpr int    DRAW_BUILDINGS = FLAG_BIT(4);      // 16
    static constexpr int    DRAW_PROPS = FLAG_BIT(5);          // 32
    static constexpr int    DRAW_AI = FLAG_BIT(6);             // 64
    static constexpr int    DRAW_SKYDOME = FLAG_BIT(1);        // Alias for SKY



	struct draw_params_s {
		const view_define_s* view_define_;
		bool				overlay_wireframe_;
		int					draw_parts_;
		int					draw_terrain_options_;
		bool				flat_sky_layer_is_visible_;
		int					num_terrain_render_chunk_;
		const class LevelObjects* level_objects_;
		int					selected_object_index_;
		bool				show_magic_obj_spheres_;

	};

	struct task_tree_view_params_s {
		bool show_hud_;
		std::string status_msg_;
		bool pause_mode_;
		bool show_debug_;
		bool show_help_;
		bool edit_mode_;
		bool terrain_edit_enabled_;
		int selected_object_index_;
		int hover_object_index_;
		int hover_tree_index_;
		int mouse_x_;
		int mouse_y_;
		int tree_scroll_offset = 0; // For scrolling the object tree
		bool tree_decl_expanded = false;
		const class LevelObjects* level_objects_;
		bool task_picker_open_;
		int task_picker_selected_idx_;
		int task_picker_scroll_offset_;
		std::string task_picker_search_;
		bool enable_camera_mode_;

		// C2: Property editor
		bool prop_editor_open_     = false;
		int  prop_field_index_     = -1;
		int  prop_text_edit_field_ = -1;
		int  prop_edit_obj_index_  = -1;  // LevelObject targeted by active text edit (-1 = parent)
		int  prop_drag_obj_index_  = -1;  // LevelObject targeted by active slider/pad drag (-1 = parent)
		std::string prop_text_buf_;
		int  prop_text_caret_      = 0;
		int  prop_panel_scroll_    = 0;  // vertical scroll offset (pixels)

		// C3: Find bar
		bool find_open_       = false;
		std::string find_query_;
		int  find_result_idx_ = -1;
		bool selected_obj_is_ai    = false;

		// Help panel (keybindings from qedkeybindings.qsc)
		int  help_scroll_offset_    = 0;
		const std::vector<std::string>* help_entries_ = nullptr;

		// Task type column toggle
		bool show_task_type_ = false;

		// Find mode: 0=name/type/id, 1=text-in-task, 2=by-id, 3=by-note
		int  find_mode_ = 0;

		// File dialog
		int         file_dialog_mode_ = 0; // 0=none,1=SaveSubTask,2=SaveSubTaskParent,3=LoadSubTask
		std::string file_dialog_path_;
		int         file_dialog_caret_ = 0;

		// Autocomplete task picker (left panel)
		bool ac_task_picker_open_   = false;
		int  ac_task_selected_idx_  = 0;
		int  ac_task_scroll_offset_ = 0;
		std::string ac_task_filter_;
		const std::vector<std::string>* ac_task_items_ = nullptr;

		// Model ID picker (right panel)
		bool model_picker_open_    = false;
		int  model_picker_selected_ = 0;
		int  model_picker_scroll_   = 0;
		std::string model_picker_filter_;
		const std::set<std::string>* model_ids_ = nullptr;  // all XXX_XX_X model IDs from level objects

		// AI Script editor state
		std::string ai_script_path_;
		std::string ai_script_text_;
		bool        ai_script_dirty_        = false;
		int         ai_script_vscroll_      = 0;
		int         ai_script_path_hscroll_ = 0;
	};


	Renderer();
	~Renderer();

	bool					Init();
	void					Shutdown();

	void					BeginLoadLevel();
	void					SetLevel(int level) { objects_.SetLevel(level); }

	// Diagnostics: live object cache occupancy (for level-switch logging).
	size_t					GetMeshCacheCount() const { return objects_.GetMeshCacheCount(); }
	size_t					GetTextureCacheCount() const { return objects_.GetTextureCacheCount(); }

	// interface of IRendererLoader
	void					SetupClearColor(const glm::vec4& color) override;
	void					SetupFog(const glm::vec4& color, float fog_far) override;
	void					SetupSkydome(const skydome_define_s& d) override;

	void					LoadFlatSkyLayerTex(int layer_no, const pic_s* pic) override;
	void					LoadTerrainMatTex(const pic_s* pic) override;
	void					LoadTerrainLMPTex(const pic_s* pic) override;

	vert_flat_sky_layer_s *	MapFlatSkyLayersVB();
	void					UnmapFlatSkyLayersVB();

	vert_pos_a_uv_s*		MapTerrainVB();
	void					UnmapTerrainVB();

	uint32_t*				MapTerrainIB();
	void					UnmapTerrainIB();

	render_chunk_s*			GetTerrainRenderChunckBuffer();

	void					Draw(const draw_params_s& params, const task_tree_view_params_s& task_tree_view);
    void                    SetSplineTerrainQuery(std::function<bool(double, double, float&)> fn) { splines_.SetTerrainQuery(std::move(fn)); }
	glm::vec3				GetMeshExtents(const std::string& modelId, bool isBuilding) { return objects_.GetMeshExtents(modelId, isBuilding); }
	float					GetMeshZOffset(const std::string& modelId, bool isBuilding) { return objects_.GetMeshZOffset(modelId, isBuilding); }
	int						PickObjectAtScreen(int x, int y, int w, int h,
												const view_define_s& vd,
												const std::vector<LevelObject>& objects,
												int draw_parts) {
		SetupUBOMats(vd);
		return objects_.PickObjectAtScreen(x, y, w, h, ubo_mats_, objects, draw_parts, vd.pos_);
	}
	// Pass-throughs for ATTA promotion (see Renderer_Objects).
	static constexpr int kAttaPickBase = Renderer_Objects::kAttaPickBase;
	bool GetAttaPickEntry(int entry, AttaPickEntry& out) const { return objects_.GetAttaPickEntry(entry, out); }
	void SuppressAtta(const std::string& key) { objects_.SuppressAtta(key); }
	void ClearSuppressedAttas() { objects_.ClearSuppressedAttas(); }
	bool SuppressAttachmentInMef(const std::string& parentModelId, const std::string& attModelId, const glm::vec3& localPos) {
		return objects_.SuppressAttachmentInMef(parentModelId, attModelId, localPos);
	}
	bool AddModelToLevelRes(const std::string& modelId) {
		return objects_.AddModelToLevelRes(modelId);
	}
	bool UpdateAttaLocalPosInMef(const std::string& parentModelId, bool isBuilding, int recordIndex, const glm::vec3& newLocalPos, const glm::mat3& newLocalRot) {
		return objects_.UpdateAttaLocalPosInMef(parentModelId, isBuilding, recordIndex, newLocalPos, newLocalRot);
	}
	void MarkAttaPromotedByRecord(const std::string& parentModelId, int recordIndex) {
		objects_.MarkAttaPromotedByRecord(parentModelId, recordIndex);
	}
	static std::string AttaOccupancyKey(const std::string& modelId, const glm::vec3& worldPos) {
		return Renderer_Objects::AttaOccupancyKey(modelId, worldPos);
	}

private:


	struct ubo_mats_s {
		glm::mat4			mvp_mat_follow_view_;
		glm::mat4			mvp_flat_sky_layer_;
		glm::mat4			mvp_objects_;
	};

	struct ubo_fog_s {
		glm::vec4			color_;
		float				far_;
	};

	GLuint					ubo_mats_;
	GLuint					ubo_fog_;

	Renderer_Skydome		skydome_;
	Renderer_FlatSkyLayers	flat_sky_layers_;
	Renderer_Terrain		terrain_;
	Renderer_Objects		objects_;
	Renderer_Splines		splines_;

	glm::mat4				mat_proj_;
	glm::mat4				mat_view_;
	void					SetupUBOMats(const view_define_s& vd);

};
