#include "level_objects.h"
#include "logger.h"
#include <iostream>

#include <algorithm>
#include <filesystem>
#include <fstream>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>



void LevelObjects::Load(ILevelDynCube* level_dyn_cube, const QSC* qsc_objects) {
    objects_.clear();
    qtasks_.clear();
    
    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Starting Load...");

    const QSC::func_s* qsc_funcs[1024];

    
    // Parse Buildings
    int num_buildings = qsc_objects->FindFuncByStr("Building", qsc_funcs);
    for (int i = 0; i < num_buildings; ++i) {
        // if (objects_.size() >= 3) break; // Load only 3 buildings for testing
        const QSC::func_s* f = qsc_funcs[i];
        const QSC::arg_s* a = f->args_;

        LevelObject obj;
        obj.isBuilding = true;

        int arg_idx = 0;
        while (a) {
            switch (arg_idx) {
                case 2: if (a->type_ == QSC::arg_s::type_t::STR) obj.name = a->str_; break;
                case 3: if (a->type_ == QSC::arg_s::type_t::DBL) obj.pos.x = (float)a->dbl_; break;
                case 4: if (a->type_ == QSC::arg_s::type_t::DBL) obj.pos.y = (float)a->dbl_; break;
                case 5: if (a->type_ == QSC::arg_s::type_t::DBL) obj.pos.z = (float)a->dbl_; break;
                case 6: if (a->type_ == QSC::arg_s::type_t::DBL) obj.rot.x = (float)a->dbl_; break; // Alpha/Param 6 -> Roll  (Around X)
                case 7: if (a->type_ == QSC::arg_s::type_t::DBL) obj.rot.y = (float)a->dbl_; break; // Beta /Param 7 -> Pitch (Around Y)
                case 8: if (a->type_ == QSC::arg_s::type_t::DBL) obj.rot.z = (float)a->dbl_; break; // Gamma/Param 8 -> Yaw   (Around Z)
                case 9: if (a->type_ == QSC::arg_s::type_t::STR) { 
                    obj.modelId = a->str_; 
                    std::string friendly = GetModelName(obj.modelId);
                    if (!friendly.empty()) obj.name = friendly;
                } break;
            }

            a = a->next_;
            arg_idx++;
        }
        objects_.push_back(obj);
        Logger::Get().Log(LogLevel::INFO, "[LevelObjects]   -> Object: " + obj.name + " (" + obj.modelId + ") at (" + std::to_string(obj.pos.x) + ", " + std::to_string(obj.pos.y) + ", " + std::to_string(obj.pos.z) + ") rot (" + std::to_string(obj.rot.x) + ", " + std::to_string(obj.rot.y) + ", " + std::to_string(obj.rot.z) + ")");
    }

    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Found " + std::to_string(num_buildings) + " buildings. Total objects in vector: " + std::to_string(objects_.size()));

    // Parse EditRigidObjs

    int num_props = qsc_objects->FindFuncByStr("EditRigidObj", qsc_funcs);
    /*
    for (int i = 0; i < num_props; ++i) {
        if (objects_.size() >= 10) break;
        const QSC::func_s* f = qsc_funcs[i];
        const QSC::arg_s* a = f->args_;

        LevelObject obj;
        obj.isBuilding = false;

        int arg_idx = 0;
        while (a) {
            switch (arg_idx) {
                case 2: if (a->type_ == QSC::arg_s::type_t::STR) obj.name = a->str_; break;
                case 3: if (a->type_ == QSC::arg_s::type_t::DBL) obj.pos.x = (float)a->dbl_; break;
                case 4: if (a->type_ == QSC::arg_s::type_t::DBL) obj.pos.y = (float)a->dbl_; break;
                case 5: if (a->type_ == QSC::arg_s::type_t::DBL) obj.pos.z = (float)a->dbl_; break;
                case 6: if (a->type_ == QSC::arg_s::type_t::DBL) obj.rot.x = (float)a->dbl_; break; // Alpha -> Roll (Around X)
                case 7: if (a->type_ == QSC::arg_s::type_t::DBL) obj.rot.y = (float)a->dbl_; break; // Beta  -> Pitch (Around Y)
                case 8: if (a->type_ == QSC::arg_s::type_t::DBL) obj.rot.z = (float)a->dbl_; break; // Gamma -> Yaw (Around Z)
                case 9: if (a->type_ == QSC::arg_s::type_t::STR) {
                    obj.modelId = a->str_;
                    std::string friendly = GetModelName(obj.modelId);
                    if (!friendly.empty()) obj.name = friendly;
                } break;
            }

            a = a->next_;
            arg_idx++;
        }
        objects_.push_back(obj);
        Logger::Get().Log(LogLevel::INFO, "[LevelObjects]   -> Object: " + obj.name + " (" + obj.modelId + ") at (" + std::to_string(obj.pos.x) + ", " + std::to_string(obj.pos.y) + ", " + std::to_string(obj.pos.z) + ") rot (" + std::to_string(obj.rot.x) + ", " + std::to_string(obj.rot.y) + ", " + std::to_string(obj.rot.z) + ")");
    }
    */

    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Found " + std::to_string(num_props) + " props. Total objects: " + std::to_string(objects_.size()));

    // Optional: Add to dynamic cubes for spatial partitioning/culling if needed

    // For now we just keep them in the objects_ list.
    
    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Load COMPLETE. Final object count: " + std::to_string(objects_.size()));
}

void LevelObjects::Unload() {
    objects_.clear();
    qtasks_.clear();
}

void LevelObjects::LoadModelNames() {
    if (!modelNames_.empty()) return;


    char appData[1024];
    GetEnvironmentVariableA("APPDATA", appData, 1024);

    char jsonPath[1024];
    Str_SPrintf(jsonPath, 1024, "%s\\QEditor\\IGIModels.json", appData);

    char* buf = nullptr;
    if (File_LoadText(jsonPath, buf)) {
        Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Loading model names from: " + std::string(jsonPath));
        std::string content(buf);

        File_FreeBuf(buf);

        size_t pos = 0;
        while ((pos = content.find("\"ModelName\":", pos)) != std::string::npos) {
            size_t nameStart = content.find("\"", pos + 12);
            if (nameStart == std::string::npos) break;
            nameStart++;
            size_t nameEnd = content.find("\"", nameStart);
            if (nameEnd == std::string::npos) break;
            std::string name = content.substr(nameStart, nameEnd - nameStart);

            size_t idPos = content.find("\"ModelId\":", nameEnd);
            if (idPos == std::string::npos) break;
            size_t idStart = content.find("\"", idPos + 10);
            if (idStart == std::string::npos) break;
            idStart++;
            size_t idEnd = content.find("\"", idStart);
            if (idEnd == std::string::npos) break;
            std::string id = content.substr(idStart, idEnd - idStart);

            modelNames_[id] = name;
            pos = idEnd;
        }
        printf("Loaded %zu friendly model names from IGIModels.json\n", modelNames_.size());
    }
}

std::string LevelObjects::GetModelName(const std::string& modelId) {
    if (modelNames_.empty()) LoadModelNames();
    if (modelNames_.count(modelId)) return modelNames_[modelId];
    return "";
}

