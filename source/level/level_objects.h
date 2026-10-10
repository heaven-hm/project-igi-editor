#pragma once
#include "../pch.h"
#include "level_common.h"
#include <vector>
#include <string>
#include <map>

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
    int boneHierarchy = -1;  // HumanSoldier-family arg@9: index into common/ANIMS/<NNN>.IFF (-1 = none)
    int standAnimation = -1; // HumanSoldier-family arg@10: animation_id of the default clip to play (-1 = none)
    int graphNodeCount = -1; // AIGraph: Graphdata node_count (arg@7), -1 = not an AIGraph / not parsed
    int aiGraphTaskId = -1;  // HumanAI: arg@4, the AIGraph task this AI patrols (-1 = none)
    std::string weaponEnumId;  // Weapon-child task (type starts with "Gun"): its WEAPON_ID_* arg@3
    std::string weaponModelId; // HumanSoldier: resolved real model id for its weapon child, held in hand (empty = no weapon / excluded)
    glm::dvec3 pos = glm::dvec3(0.0);
    glm::dvec3 original_pos = glm::dvec3(0.0);  // Original position from QSC for fallback matching
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
    std::string secondaryModelId;  // For SCamera (Camera body)
    std::string lensModelId;       // For SCamera (Lens)
    std::string splineTaskId;      // For Train (RailroadQTaskID)
    bool linearSegments = false;   // If true, straight lines; if false, curved
    int splineSegmentCount = 20;   // Number of sub-segments per waypoint segment
    glm::dmat3 orientationMatrix = glm::dmat3(1.0); // 3x3 rotation matrix for Splines/Joints

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
};

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
