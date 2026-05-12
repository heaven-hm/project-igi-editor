#include "level_objects.h"
#include "logger.h"
#include <iostream>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>


static std::string TaskIdFromArg(const QSC::arg_s* a) {
    if (!a) return "";
    if (a->type_ == QSC::arg_s::type_t::STR) return a->str_;
    if (a->type_ == QSC::arg_s::type_t::DBL) return std::to_string((int)a->dbl_);
    return "";
}


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
                case 0: obj.taskId = TaskIdFromArg(a); break;
                case 2: if (a->type_ == QSC::arg_s::type_t::STR) obj.name = a->str_; break;
                case 3:
                    if (a->type_ == QSC::arg_s::type_t::DBL) {
                        obj.pos.x = a->dbl_;
                        obj.original_pos.x = a->dbl_;
                    }
                    break;
                case 4:
                    if (a->type_ == QSC::arg_s::type_t::DBL) {
                        obj.pos.y = a->dbl_;
                        obj.original_pos.y = a->dbl_;
                    }
                    break;
                case 5:
                    if (a->type_ == QSC::arg_s::type_t::DBL) {
                        obj.pos.z = a->dbl_;
                        obj.original_pos.z = a->dbl_;
                    }
                    break;
                case 6: if (a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.x = a->dbl_; obj.original_rot.x = a->dbl_; } break;
                case 7: if (a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.y = a->dbl_; obj.original_rot.y = a->dbl_; } break;
                case 8: if (a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = a->dbl_; obj.original_rot.z = a->dbl_; } break;
                case 9: if (a->type_ == QSC::arg_s::type_t::STR) obj.modelId = a->str_; break;
            }
            a = a->next_;
            arg_idx++;
        }

        // Extract numeric ID from taskId if it contains Task_New pattern
        // Example: "Task_New(90, ...)" -> extract "90"
        if (!obj.taskId.empty() && obj.taskId.find("Task_New(") == 0) {
            size_t parenStart = obj.taskId.find('(');
            size_t parenEnd = obj.taskId.find(',', parenStart);
            if (parenStart != std::string::npos && parenEnd != std::string::npos) {
                std::string idStr = obj.taskId.substr(parenStart + 1, parenEnd - parenStart - 1);
                // Trim whitespace
                size_t start = idStr.find_first_not_of(" \t");
                size_t end = idStr.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos) {
                    obj.taskId = idStr.substr(start, end - start + 1);
                } else {
                    obj.taskId = idStr;
                }
            }
        }
        objects_.push_back(obj);
        Logger::Get().Log(LogLevel::INFO, "[LevelObjects]   -> Building: " + obj.modelId + 
            " at (" + std::to_string(obj.pos.x) + ", " + std::to_string(obj.pos.y) + ", " + std::to_string(obj.pos.z) + ") " +
            "rot (" + std::to_string(obj.rot.x) + ", " + std::to_string(obj.rot.y) + ", " + std::to_string(obj.rot.z) + ")");
    }

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
                case 0: obj.taskId = TaskIdFromArg(a); break;
                case 2: if (a->type_ == QSC::arg_s::type_t::STR) obj.name = a->str_; break;
                case 3:
                    if (a->type_ == QSC::arg_s::type_t::DBL) {
                        obj.pos.x = a->dbl_;
                        obj.original_pos.x = a->dbl_;
                    }
                    break;
                case 4:
                    if (a->type_ == QSC::arg_s::type_t::DBL) {
                        obj.pos.y = a->dbl_;
                        obj.original_pos.y = a->dbl_;
                    }
                    break;
                case 5:
                    if (a->type_ == QSC::arg_s::type_t::DBL) {
                        obj.pos.z = a->dbl_;
                        obj.original_pos.z = a->dbl_;
                    }
                    break;
                case 6: if (a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.x = a->dbl_; obj.original_rot.x = a->dbl_; } break;
                case 7: if (a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.y = a->dbl_; obj.original_rot.y = a->dbl_; } break;
                case 8: if (a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = a->dbl_; obj.original_rot.z = a->dbl_; } break;
                case 9: if (a->type_ == QSC::arg_s::type_t::STR) obj.modelId = a->str_; break;
            }
            a = a->next_;
            arg_idx++;
        }

        // Extract numeric ID from taskId if it contains Task_New pattern
        if (!obj.taskId.empty() && obj.taskId.find("Task_New(") == 0) {
            size_t parenStart = obj.taskId.find('(');
            size_t parenEnd = obj.taskId.find(',', parenStart);
            if (parenStart != std::string::npos && parenEnd != std::string::npos) {
                std::string idStr = obj.taskId.substr(parenStart + 1, parenEnd - parenStart - 1);
                // Trim whitespace
                size_t start = idStr.find_first_not_of(" \t");
                size_t end = idStr.find_last_not_of(" \t");
                if (start != std::string::npos && end != std::string::npos) {
                    obj.taskId = idStr.substr(start, end - start + 1);
                } else {
                    obj.taskId = idStr;
                }
            }
        }

        objects_.push_back(obj);
        Logger::Get().Log(LogLevel::INFO, "[LevelObjects]   -> Prop: " + obj.modelId + 
            " at (" + std::to_string(obj.pos.x) + ", " + std::to_string(obj.pos.y) + ", " + std::to_string(obj.pos.z) + ") " +
            "rot (" + std::to_string(obj.rot.x) + ", " + std::to_string(obj.rot.y) + ", " + std::to_string(obj.rot.z) + ")");
    }

    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Load complete. Total objects: " + std::to_string(objects_.size()));
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
        while ((pos = content.find("{", pos)) != std::string::npos) {
            size_t objStart = pos;
            size_t objEnd = content.find("}", pos);
            if (objEnd == std::string::npos) break;

            std::string objContent = content.substr(objStart, objEnd - objStart + 1);

            // Extract ModelName
            size_t namePos = objContent.find("\"ModelName\":");
            std::string name;
            if (namePos != std::string::npos) {
                size_t nameStart = objContent.find("\"", namePos + 12);
                if (nameStart != std::string::npos) {
                    nameStart++;
                    size_t nameEnd = objContent.find("\"", nameStart);
                    if (nameEnd != std::string::npos) {
                        name = objContent.substr(nameStart, nameEnd - nameStart);
                    }
                }
            }

            // Extract ModelId
            size_t idPos = objContent.find("\"ModelId\":");
            std::string id;
            if (idPos != std::string::npos) {
                size_t idStart = objContent.find("\"", idPos + 10);
                if (idStart != std::string::npos) {
                    idStart++;
                    size_t idEnd = objContent.find("\"", idStart);
                    if (idEnd != std::string::npos) {
                        id = objContent.substr(idStart, idEnd - idStart);
                    }
                }
            }

            if (!name.empty() && !id.empty()) {
                modelNames_[id] = name;
                // Debug log for specific IDs
                if (id == "419_01_1" || id == "400_20_1") {
                    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Loaded mapping: " + id + " -> " + name);
                }
            }

            pos = objEnd + 1;
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

    // Track if any changes were made
    bool anyChanges = false;

    // Format double: enough precision for both large coords and small rotations.
    // Use %.10g to avoid scientific notation truncation and preserve rotation accuracy.
    auto fmt = [](double v) -> std::string {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.10g", v);
        std::string s(buf);
        // Ensure at least one decimal point so compiler treats it as float literal
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
            s += ".0";
        return s;
    };

    for (const auto& obj : objects_) {
        if (obj.modelId.empty()) continue;

        // Skip if nothing changed vs what was loaded from QSC
        bool changed = (obj.pos.x != obj.original_pos.x ||
                        obj.pos.y != obj.original_pos.y ||
                        obj.pos.z != obj.original_pos.z ||
                        obj.rot.x != obj.original_rot.x ||
                        obj.rot.y != obj.original_rot.y ||
                        obj.rot.z != obj.original_rot.z);
        if (!changed) {
            Logger::Get().Log(LogLevel::INFO, "[LevelObjects::SaveToQSC] SKIP (unchanged): " + obj.name + " / " + obj.modelId);
            continue;
        }

        anyChanges = true;
        Logger::Get().Log(LogLevel::INFO, "[LevelObjects::SaveToQSC] CHANGE DETECTED: " + obj.name + " / " + obj.modelId);

        std::string modelIdToken = "\"" + obj.modelId + "\"";
        std::string typeBuilding = "\"Building\"";
        std::string typeRigid    = "\"EditRigidObj\"";

        // Find the matching line:
        // - Building:      line contains Task_New(taskId, + "Building" + modelId
        // - EditRigidObj:  line contains "EditRigidObj" + modelId (ID is always -1, search by model)
        size_t lineStart = std::string::npos;
        size_t searchFrom = 0;

        if (obj.isBuilding) {
            // Search by taskId for Buildings
            std::string taskIdToken = "Task_New(" + obj.taskId + ",";
            while (true) {
                size_t found = content.find(taskIdToken, searchFrom);
                if (found == std::string::npos) break;
                size_t ls = content.rfind('\n', found);
                ls = (ls == std::string::npos) ? 0 : ls + 1;
                size_t le = content.find('\n', found);
                if (le == std::string::npos) le = content.size();
                std::string line = content.substr(ls, le - ls);
                if (line.find(modelIdToken) != std::string::npos &&
                    line.find(typeBuilding) != std::string::npos) {
                    lineStart = ls;
                    break;
                }
                searchFrom = found + 1;
            }
        } else {
            // Search by modelId + original position for EditRigidObj
            // (taskId is always -1, and multiple objects can share the same modelId)
            // Use the integer part of origX to match regardless of decimal formatting in QSC
            char origXBuf[64];
            snprintf(origXBuf, sizeof(origXBuf), "%.0f", obj.original_pos.x);
            std::string origX = std::string(origXBuf);
            while (true) {
                size_t found = content.find(modelIdToken, searchFrom);
                if (found == std::string::npos) break;
                size_t ls = content.rfind('\n', found);
                ls = (ls == std::string::npos) ? 0 : ls + 1;
                size_t le = content.find('\n', found);
                if (le == std::string::npos) le = content.size();
                std::string line = content.substr(ls, le - ls);
                if (line.find(typeRigid) != std::string::npos &&
                    line.find(origX) != std::string::npos) {
                    lineStart = ls;
                    break;
                }
                searchFrom = found + 1;
            }
        }

        if (lineStart == std::string::npos) {
            Logger::Get().Log(LogLevel::WARNING, "[LevelObjects::SaveToQSC] Line not found for: "
                + obj.name + " taskId=" + obj.taskId + " modelId=" + obj.modelId);
            continue;
        }

        size_t lineEnd = content.find('\n', lineStart);
        if (lineEnd == std::string::npos) lineEnd = content.size();
        std::string oldLine = content.substr(lineStart, lineEnd - lineStart);

        // Preserve indentation
        std::string indent;
        for (char c : oldLine) {
            if (c == ' ' || c == '\t') indent += c;
            else break;
        }

        // Detect how the original line ends:
        // Case A: has closing ')' on this line  -> standalone or inline, use ");", ")," or just ","
        // Case B: no closing ')' at all         -> multi-line parent (children on next lines),
        //         end with "modelId"," and NO closing paren
        bool hasClosingParen = (oldLine.rfind(')') != std::string::npos);

        std::string terminator = ");";
        if (hasClosingParen) {
            for (int ci = (int)oldLine.size() - 1; ci >= 0; --ci) {
                char c = oldLine[ci];
                if (c == ' ' || c == '\t' || c == '\r') continue;
                if (c == ',') { terminator = "),"; break; }
                if (c == ';') { terminator = ");"; break; }
                break;
            }
        }

        // Build new line
        // Building (single-line):  Task_New(id,"Building","name",x,y,z,rx,ry,rz,"modelId")term
        // Building (multi-line):   Task_New(id,"Building","name",x,y,z,rx,ry,rz,"modelId",   <- no closing paren
        // EditRigidObj:            Task_New(-1,"EditRigidObj","name",x,y,z,rx,ry,rz,"modelId",1,1,1,0,0,0)term
        std::string taskType  = obj.isBuilding ? typeBuilding : typeRigid;
        std::string extraArgs = obj.isBuilding ? "" : ",1,1,1,0,0,0";

        // Use the correct friendly name from JSON mapping instead of the loaded name
        std::string correctName = GetModelName(obj.modelId);
        if (correctName.empty()) {
            correctName = obj.name; // Fallback to loaded name if not found in JSON
        }

        // Subtract the snap offset so the saved Z matches the original game coordinate
        double saveZ = obj.pos.z - obj.snap_z_offset;

        std::string newLine;
        if (!hasClosingParen) {
            // Multi-line parent: preserve trailing comma, no closing paren on this line
            newLine = indent
                + "Task_New(" + obj.taskId + "," + taskType + ",\"" + correctName + "\","
                + fmt(obj.pos.x) + "," + fmt(obj.pos.y) + "," + fmt(saveZ) + ","
                + fmt(obj.rot.x) + "," + fmt(obj.rot.y) + "," + fmt(obj.rot.z) + ","
                + modelIdToken + ",";
        } else {
            newLine = indent
                + "Task_New(" + obj.taskId + "," + taskType + ",\"" + correctName + "\","
                + fmt(obj.pos.x) + "," + fmt(obj.pos.y) + "," + fmt(saveZ) + ","
                + fmt(obj.rot.x) + "," + fmt(obj.rot.y) + "," + fmt(obj.rot.z) + ","
                + modelIdToken + extraArgs + terminator;
        }

        Logger::Get().Log(LogLevel::INFO, "[LevelObjects::SaveToQSC] ["
            + std::string(obj.isBuilding ? "Building" : "EditRigidObj") + "] " + obj.name
            + " pos(" + fmt(obj.pos.x) + "," + fmt(obj.pos.y) + "," + fmt(obj.pos.z) + ")"
            + " rot(" + fmt(obj.rot.x) + "," + fmt(obj.rot.y) + "," + fmt(obj.rot.z) + ")");
        Logger::Get().Log(LogLevel::INFO, "[LevelObjects::SaveToQSC]   OLD: " + oldLine);
        Logger::Get().Log(LogLevel::INFO, "[LevelObjects::SaveToQSC]   NEW: " + newLine);

        content.replace(lineStart, lineEnd - lineStart, newLine);
    }

    // Only write the file if there were any changes
    if (!anyChanges) {
        Logger::Get().Log(LogLevel::INFO, "[LevelObjects::SaveToQSC] No changes detected, skipping save to: " + qscPath);
        return;
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

