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
        const QSC::func_s* f = qsc_funcs[i];
        const QSC::arg_s* a = f->args_;

        LevelObject obj;
        obj.isBuilding = true;

        int arg_idx = 0;
        while (a) {
            switch (arg_idx) {
                case 0: if (a->type_ == QSC::arg_s::type_t::STR) obj.taskId = a->str_; break;
                case 2: if (a->type_ == QSC::arg_s::type_t::STR) obj.name = a->str_; break;
                case 3: if (a->type_ == QSC::arg_s::type_t::DBL) obj.pos.x = a->dbl_; break;
                case 4: if (a->type_ == QSC::arg_s::type_t::DBL) obj.pos.y = a->dbl_; break;
                case 5: if (a->type_ == QSC::arg_s::type_t::DBL) obj.pos.z = a->dbl_; break;
                case 6: if (a->type_ == QSC::arg_s::type_t::DBL) obj.rot.x = a->dbl_; break; // Alpha/Param 6 -> Roll  (Around X)
                case 7: if (a->type_ == QSC::arg_s::type_t::DBL) obj.rot.y = a->dbl_; break; // Beta /Param 7 -> Pitch (Around Y)
                case 8: if (a->type_ == QSC::arg_s::type_t::DBL) obj.rot.z = a->dbl_; break; // Gamma/Param 8 -> Yaw   (Around Z)
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
    for (int i = 0; i < num_props; ++i) {
        const QSC::func_s* f = qsc_funcs[i];
        const QSC::arg_s* a = f->args_;

        LevelObject obj;
        obj.isBuilding = false;

        int arg_idx = 0;
        while (a) {
            switch (arg_idx) {
                case 0: if (a->type_ == QSC::arg_s::type_t::STR) obj.taskId = a->str_; break;
                case 2: if (a->type_ == QSC::arg_s::type_t::STR) obj.name = a->str_; break;
                case 3: if (a->type_ == QSC::arg_s::type_t::DBL) obj.pos.x = a->dbl_; break;
                case 4: if (a->type_ == QSC::arg_s::type_t::DBL) obj.pos.y = a->dbl_; break;
                case 5: if (a->type_ == QSC::arg_s::type_t::DBL) obj.pos.z = a->dbl_; break;
                case 6: if (a->type_ == QSC::arg_s::type_t::DBL) obj.rot.x = a->dbl_; break; // Alpha -> Roll (Around X)
                case 7: if (a->type_ == QSC::arg_s::type_t::DBL) obj.rot.y = a->dbl_; break; // Beta  -> Pitch (Around Y)
                case 8: if (a->type_ == QSC::arg_s::type_t::DBL) obj.rot.z = a->dbl_; break; // Gamma -> Yaw (Around Z)
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

    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Found " + std::to_string(num_props) + " props. Total objects: " + std::to_string(objects_.size()));

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

void LevelObjects::SaveToQSC(const std::string& qscPath) {
    // Read the QSC file
    char* buf = nullptr;
    if (!File_LoadText(qscPath.c_str(), buf)) {
        Logger::Get().Log(LogLevel::ERR, "[LevelObjects::SaveToQSC] Failed to read QSC file: " + qscPath);
        return;
    }

    std::string content(buf);
    File_FreeBuf(buf);

    Logger::Get().Log(LogLevel::INFO, "[LevelObjects::SaveToQSC] Processing " + std::to_string(objects_.size()) + " objects for save");

    // For each object, find its Task_New entry and update the position/rotation
    for (const auto& obj : objects_) {
        if (obj.taskId.empty()) {
            Logger::Get().Log(LogLevel::WARNING, "[LevelObjects::SaveToQSC] Object has no taskId, skipping: " + obj.name);
            continue;
        }

        // Search for Task_New(taskId, ...) pattern
        std::string searchPattern = "Task_New(" + obj.taskId + ",";
        size_t pos = content.find(searchPattern);

        if (pos == std::string::npos) {
            Logger::Get().Log(LogLevel::WARNING, "[LevelObjects::SaveToQSC] Task_New entry not found for taskId: " + obj.taskId);
            continue;
        }

        // Found the entry, now we need to find and replace the 6 numeric arguments
        // The pattern is: Task_New(taskId, x, y, z, rotX, rotY, rotZ, ...)
        // We need to replace args 1-6 (0-indexed: args 1,2,3 are position, args 4,5,6 are rotation)

        // Find the opening parenthesis after Task_New
        size_t parenStart = pos + searchPattern.length() - 1; // Position of '('
        size_t parenEnd = content.find(')', parenStart);
        if (parenEnd == std::string::npos) {
            Logger::Get().Log(LogLevel::ERR, "[LevelObjects::SaveToQSC] Malformed Task_New entry for taskId: " + obj.taskId);
            continue;
        }

        // Extract the arguments string
        std::string argsStr = content.substr(parenStart + 1, parenEnd - parenStart - 1);

        // Parse and replace arguments
        // We'll use a simple approach: split by comma and replace the numeric values
        std::vector<std::string> args;
        std::string currentArg;
        int parenDepth = 0;
        for (char c : argsStr) {
            if (c == '(') parenDepth++;
            else if (c == ')') parenDepth--;
            else if (c == ',' && parenDepth == 0) {
                args.push_back(currentArg);
                currentArg.clear();
            } else {
                currentArg += c;
            }
        }
        if (!currentArg.empty()) {
            args.push_back(currentArg);
        }

        // We need at least 7 arguments (taskId + 6 numeric values)
        if (args.size() < 7) {
            Logger::Get().Log(LogLevel::WARNING, "[LevelObjects::SaveToQSC] Not enough arguments in Task_New for taskId: " + obj.taskId);
            continue;
        }

        // Replace the 6 numeric arguments (indices 1-6)
        // Format them with appropriate precision
        args[1] = std::to_string(obj.pos.x);
        args[2] = std::to_string(obj.pos.y);
        args[3] = std::to_string(obj.pos.z);
        args[4] = std::to_string(obj.rot.x);
        args[5] = std::to_string(obj.rot.y);
        args[6] = std::to_string(obj.rot.z);

        // Reconstruct the arguments string
        std::string newArgsStr;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) newArgsStr += ",";
            newArgsStr += args[i];
        }

        // Replace the old arguments with the new ones in the content
        content.replace(parenStart + 1, parenEnd - parenStart - 1, newArgsStr);

        Logger::Get().Log(LogLevel::INFO, "[LevelObjects::SaveToQSC] Updated object: " + obj.name + " (taskId: " + obj.taskId + ")");
    }

    // Write the modified content back to the file
    std::ofstream outFile(qscPath);
    if (!outFile) {
        Logger::Get().Log(LogLevel::ERR, "[LevelObjects::SaveToQSC] Failed to open QSC file for writing: " + qscPath);
        return;
    }

    outFile << content;
    outFile.close();

    Logger::Get().Log(LogLevel::INFO, "[LevelObjects::SaveToQSC] Successfully saved changes to: " + qscPath);
}

