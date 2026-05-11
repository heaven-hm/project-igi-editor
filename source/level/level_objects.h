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
    glm::dvec3 pos;
    glm::dvec3 rot;
    bool isBuilding;

    // Lighting
    float dirlightR = 1.0f, dirlightG = 1.0f, dirlightB = 1.0f;
    float ambientR = 0.3f, ambientG = 0.3f, ambientB = 0.3f;
    float scale = 1.0f;
};


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
    std::string GetModelName(const std::string& modelId);
    void SaveToQSC(const std::string& qscPath);


    const std::vector<LevelObject>& GetObjects() const { return objects_; }
    std::vector<LevelObject>& GetObjects() { return objects_; }


private:
    std::vector<LevelObject> objects_;
    std::vector<qtask_object_s> qtasks_;
    std::map<std::string, std::string> modelNames_;
};
