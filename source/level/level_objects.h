#pragma once
// This header must NOT include ../pch.h. pch.h pulls in level.h, and level.h
// includes this header back (it holds a LevelObjects by value), so including
// pch.h here makes the pair order-dependent: when level_objects.h is the entry
// point, the cycle is cut by `#pragma once` and level.h:53 fails with
// "unknown type name 'LevelObjects'". The Windows build never noticed because
// CMake force-includes the PCH ahead of every translation unit — but that same
// dependency is what kept every level_objects-based suite out of the host-native
// test lane (tools/host-tests/host_test_runner.py compiles without a PCH).
// Include exactly what this header needs instead.
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <glew.h>
#include <glm/glm.hpp>
#include "level_common.h"
#include "task_schema.h"

std::pair<int, int> ResolveSoldierAnimationDefaults(const std::string& type,
                                                     int declaredHierarchy,
                                                     int declaredStandAnimation);

// True when a task's Task_DeclareParameters layout puts a world position at QSC args
// 3/4/5, i.e. its first declared parameter is ("Position","ObjectPos"). Used by the
// declared-position fallback in LevelObjects::Load() to decide whether args 3/4/5 of a
// task type the editor has no explicit branch for really are a position. Exactly the
// types that carry a world position declare it that way; types that merely have three
// numbers there (LightmapInfo, Dirlight, FlatSky, PatrolPathCommand, AddAmmo, ...) do not.
bool TaskDeclarationHasPositionAt3(const TaskSchemaNS::TaskSchema& schema);

struct LevelObject {
    std::string name;
    std::string modelId;
    std::string taskId;  // Task ID from QSC for save support
    std::string original_name; // Original name from QSC to avoid noise changes
    std::string aiId;    // AI ID from JSON
    std::string type;    // AI Type
    std::string graphId; // Graph ID from JSON
    std::string graphName; // Graph Name from JSON
    glm::dvec3 graphPos;   // Graph Position
    std::string primaryWeapon; // Primary weapon name from JSON
    std::string primaryAmmo;    // Primary ammo from JSON
    std::string secondaryWeapon; // Secondary weapon name from JSON
    std::string secondaryAmmo;    // Secondary ammo from JSON
    int team = 0; // 0 = Friendly, 1 = Enemy — read from argTokens at load; tooltip uses argTokens directly
    int boneHierarchy = -1;  // HumanSoldier-family arg@9; HumanPlayer uses OpenIGI set 000
    int standAnimation = -1; // HumanSoldier-family arg@10; HumanPlayer falls back to clip 2
    int graphNodeCount = -1; // AIGraph: Graphdata node_count (arg@7), -1 = not an AIGraph / not parsed
    int aiGraphTaskId = -1;  // HumanAI: arg@4, the AIGraph task this AI patrols (-1 = none)
    std::string weaponEnumId;  // Weapon-child task (type starts with "Gun"): its WEAPON_ID_* arg@3
    std::string weaponModelId; // HumanSoldier: resolved real model id for its weapon child, held in hand (empty = no weapon / excluded)
    glm::dvec3 pos = glm::dvec3(0.0);
    glm::dvec3 original_pos = glm::dvec3(0.0);  // Original position from QSC for fallback matching
    // Set by LoadRecursive for task types it has no explicit branch for, after reading
    // args 3/4/5 as a *candidate* position. LevelObjects::Load() then validates the
    // candidate against the level's own Task_DeclareParameters and reverts it unless the
    // type declares ("Position","ObjectPos") as its first parameter — see the position
    // fallback pass in Load(). Without this, ~1000 real objects (TerrainLightMap,
    // EditRigidObj, EditCamera, Building, StatusMessage, ...) loaded at the world origin.
    bool declaredPositionCandidate = false;
    glm::dvec3 rot = glm::dvec3(0.0);
    glm::dvec3 original_rot = glm::dvec3(0.0);  // Original rotation from QSC for change detection
    bool isBuilding = false;
    bool isContainer = false; // For grouping tasks like "Container", "Static", "Game"
    bool expanded = false;    // For TreeView HUD state
    bool modified = false;
    bool deleted = false;
    bool modelMissingInRes = false; // model not packed in level .res → invisible in-game (issue 2)
    bool has_original_name = false; // To distinguish between empty name and new object
    double snap_z_offset = 0.0;  // Z offset added by SnapObjectsToTerrain, subtracted when saving to QSC

    // ATTA proxy: set when this object represents a directly-edited ATTA record.
    // Not serialized to QSC — position changes are written to the MEF binary.
    bool isAttaProxy = false;
    int attaRecordIndex = -1;
    std::string attaParentModelId;
    bool attaIsBuilding = false;
    glm::mat4 attaInvParentMat = glm::mat4(1.0f);

    int parentIndex = -1; // Index in LevelObjects::objects_
    std::vector<int> childrenIndices;

    // Spline / Pathing Support
    bool isSplineContainer = false;
    bool isSplineWaypoint = false;
    bool isWire = false;
    std::string segmentModelId;    // The model repeated along the path (e.g. 368_01_1)
    // SplineObjWaypoint's remaining authored parameters, at the arg indices the engine's
    // own ParamTable fixes (SplineObj_Open HOOK 0x502250, src/editobj/spline.c:215-223,
    // eight entries in the same order as the shipped declaration in
    // MISSIONS/location0/level1/objects.qsc):
    //   arg 11 "NumAreas"              Int32  -> SplineObjWaypoint::segments
    //   arg 12 "Align"                 bool8  -> SplineObjWaypoint::straight
    //   arg 13 "Flip"                  bool8  -> SplineObjWaypoint::reversed
    //   arg 14 "Automatic Orientation" bool8  -> SplineObjWaypoint::autoOrient
    // (src/editobj/editobjtypes.h:6-25).  SplineObjWaypoint_Changed (HOOK 0x500EF0,
    // src/tagitem/spline.c:769-772) then derives `rigid = container.rigid || straight`
    // and caps the count at the container's maxSegments and at 20.
    int splineSegments = 0;        // arg 11, 0 when the waypoint declares none
    bool splineStraight = false;   // arg 12
    bool splineReversed = false;   // arg 13
    bool splineAutoOrient = false; // arg 14
    std::string secondaryModelId;  // SCamera arg 11: "Camera Destroyed Model" (the broken model)
    std::string lensModelId;       // SCamera arg 10: "Camera Model" — the SCameraLens child's model
    // SCamera arg 7 ("Holder Model") is the model THIS task draws, so it is stored in
    // modelId: IGI-RE src/scamera/scamera.c:116-131 declares it third in the param
    // table and SCamera_Create hands `c->model` to RigidDynCubeObj_SetModel
    // (scamera.c:240) before placing it at the authored spot (scamera.c:242). Arg 10
    // is `FIELD(lens_model)` and belongs to the separate SCameraLens child task
    // (scameralens.c:204), which the retail scripts never instantiate.
    // SCamera "Camera Gamma" (QSC arg 9): an extra yaw added to the lens direction.
    // IGI-RE src/scamera/scameralens.c:204 builds the camera eye with
    //   Matrix_SetEuler(&l->o.matrix, l->pitch, 0, l->yaw + c->yaw + c->yaw_offset)
    // i.e. arg 9 is a YAW offset, never roll. The editor keeps the authored value
    // separately so the QSC round-trip stays lossless, and folds it into rot.z for
    // display (the renderer only ever sees rot).
    double yawOffset = 0.0;
    std::string splineTaskId;      // For Train (RailroadQTaskID); also an Elevator's rail
    bool linearSegments = false;   // If true, straight lines; if false, curved
    int splineSegmentCount = 20;   // Number of sub-segments per waypoint segment
    glm::dmat3 orientationMatrix = glm::dmat3(1.0); // 3x3 rotation matrix for Splines/Joints

    // ── Elevator / moving platform (GitHub issue #40) ────────────────────────
    // `Elevator_Init` HOOK 0x43CC70 (src/elevator/elevator.c:44-83) declares 25
    // parameters; the four this editor reads are, in registration order,
    // "SplineObj taskid" (Int32), "Linear" (bool8), "Force orientation" (bool8)
    // and "Pos" (Real32). `Param_ObjectPos` consumes 3 script tokens and
    // `Param_Real32x9` 3 (ParamReal32x9_Parse HOOK 0x4EFBA0,
    // src/physobj/paramops.c:423-435), so their QSC argument indices are
    // 3 + 3 + N = 10, 11, 12, 13. The rail id shares `splineTaskId` with Train:
    // both fields are a reference to a `SplineObj` task and mean the same thing.
    // The retail LEVEL3 script confirms every index — `Task_New(698, "Elevator",
    // "", x, y, z, 0, 0, 0, "200_01_1", 699, TRUE, FALSE, 0, ...)` where 699 is
    // the `SplineObj` declared immediately above it.
    bool elevatorLinear = false;            // arg 11, "Linear"
    bool elevatorForceOrientation = false;  // arg 12, "Force orientation"
    double elevatorStartHeight = 0.0;       // arg 13, "Pos": scalar along the rail

    // Lighting
    float dirlightR = 1.0f, dirlightG = 1.0f, dirlightB = 1.0f;
    float ambientR = 0.3f, ambientG = 0.3f, ambientB = 0.3f;
    float scale = 1.0f;
    std::string qscFuncName = "Task_New";
    std::vector<std::string> argTokens; // Non-child argument tokens in source order
    std::string qscLine; // Raw QSC line for "Notepad" editing
    bool isNested = false; // True if this is a sub-call (like LightmapInfo inside Building)
    bool preserveTaskId = false; // Keep Task_New arg[0] as an integer token
    int modelIdArgIdx = -1; // Index of the modelId argument in argTokens

    // ── Wire / zipline (IGI-RE src/props/wire.c, GitHub issue #42) ────────────
    // A "Wire" task owns two ObjectPos anchors: start at args 3-5 (mirrored by the
    // live `pos`) and end at args 6-8. The end anchor is read straight from
    // argTokens so an inspector edit of the "End" field previews immediately.
    // Returns false when this is not a Wire, or the end anchor is absent/not
    // numeric (in which case start/end are left untouched).
    bool WireAnchors(glm::dvec3& start, glm::dvec3& end) const;

    // Resolved fire arc / ammo of a StationaryGun (args 10-14, IGI-RE
    // src/statgun/statgun.c:142-150). `valid` is false for any other task type.
    TaskSchemaNS::Props::StationaryGunParams StatGunParams() const;

    // Resolved detection arc / range / expressions of an AIStationaryGunHolder
    // (args 10-14, IGI-RE src/comprun/gunholder.c:157-165).
    TaskSchemaNS::Props::GunHolderParams GunHolderParams() const;
};

// Viewport gizmos for the props above (GitHub issue #42), implemented in
// renderer/renderer_objects_visual.cpp. Declared here — rather than in
// renderer_objects.h, which is outside this change's write scope — and called
// once per object from the object draw loop in renderer_objects.cpp. `ubo_mats`
// is the renderer's "Matrices" uniform buffer (std140: u_mvp at offset 32) and
// `ubo_binding` is the binding point it is bound to
// (Renderer_Objects::ubo_binding_point_); the gizmo is a no-op for other types.
void DrawPropsGizmos(const LevelObject& obj, GLuint ubo_mats, GLuint ubo_binding);

// Stable per-placement key for a lightmap binding. Most objects use their unique
// Task_New id. Nested / ATTA tasks all share the literal taskId "-1" (not unique),
// so those key off their ORIGINAL authored position instead (which the QSC binding
// is resolved against and which does NOT change when the object is moved in-editor,
// keeping the key stable across edits). Used by both the apply path (App) and the
// render path (Renderer_Objects) so they agree on where each lightmap is stored.
inline std::string LightmapTaskKey(const LevelObject& obj) {
    if (!obj.taskId.empty() && obj.taskId != "-1") return obj.taskId;
    char buf[96];
    std::snprintf(buf, sizeof(buf), "pos:%.1f,%.1f,%.1f",
                  obj.original_pos.x, obj.original_pos.y, obj.original_pos.z);
    return std::string(buf);
}


struct qtask_object_s : qtask_s {
    char model_id_[32];
    double script_defined_pos_[3];
    float rot_z_;
};

class LevelObjects {
public:
    static constexpr int32_t TASK_TYPE_BUILDING = 200;
    static constexpr int32_t TASK_TYPE_EDIT_RIGID_OBJ = 201;

    void Load(ILevelDynCube* level_dyn_cube, const QSC* qsc_objects);
    void Unload();
    void LoadModelNames();
    std::string GetModelName(const std::string& modelId) const;
    std::string GetModelId(const std::string& modelName) const;
    // Resolve a GunPickup/AmmoPickup WEAPON_ID_*/AMMO_ID_* enum string to a render
    // model id via IGIModels.json. Returns the input unchanged if it is not a known
    // enum (caller then renders/keeps the raw string).
    std::string ResolvePickupModelId(const std::string& enumId);
    // (Re)compute the held-weapon model id for the soldier at the given index from
    // its current children (weapon enum + AI graph), applying the single-node /
    // cutscene exclusion. Called once per soldier at load and again after a live
    // property edit so the weapon shown in the editor updates immediately.
    void ResolveSoldierWeapon(int soldierIndex);
    const std::map<std::string, std::string>& GetModelNamesMap() const { return modelNames_; }
    void SaveToQSC(const std::string& qscPath);
    // Write ONLY the subtree rooted at idx (the task + its descendants) as a proper
    // nested QSC block. Used for "save sub-task" / templates so it never dumps the
    // whole object set.
    void SaveSubtreeToQSC(int idx, const std::string& qscPath);


    const std::vector<LevelObject>& GetObjects() const { return objects_; }
    std::vector<LevelObject>& GetObjects() { return objects_; }


    void ParseTaskLine(const std::string& line, LevelObject& out);
    void UpdateCoordinatesInLine(LevelObject& obj);
    std::string GenerateTaskLine(const LevelObject& obj);

private:
    std::vector<LevelObject> objects_;
    std::vector<qtask_object_s> qtasks_;
    std::map<std::string, std::string> modelNames_;
    std::map<std::string, std::string> modelIds_;

    void LoadRecursive(const QSC* qsc, const QSC::func_s* func, int parentIdx);
    static std::string SerializeObjectRecursive(const std::vector<LevelObject>& objects, int idx);
};
