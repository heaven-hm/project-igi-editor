#include "level_objects.h"
#include "logger.h"
#include "../utils.h"
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
    
    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Starting recursive Load...");

    for (int i = 0; i < qsc_objects->GetRootFuncCount(); ++i) {
        LoadRecursive(qsc_objects, qsc_objects->GetRootFunc(i), -1);
    }

    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Load complete. Total objects: " + std::to_string(objects_.size()));
}

void LevelObjects::LoadRecursive(const QSC* qsc, const QSC::func_s* func, int parentIdx) {
    if (!func) return;

    const QSC::arg_s* a = func->args_;
    if (!a) return;

    std::string funcName = func->func_name_;
    std::string typeStr;

    // Check if it's a Task_New call (common wrapper)
    if (funcName == "Task_New") {
        if (a->next_ && a->next_->type_ == QSC::arg_s::type_t::STR) {
            typeStr = a->next_->str_;
        }
    } else {
        // Direct call (less common in modern IGI QSC but possible)
        typeStr = funcName;
    }

    bool isBuilding = (typeStr == "Building");
    bool isRigid = (typeStr == "EditRigidObj");
    bool isSoldier = (typeStr == "HumanSoldier" || typeStr == "HumanSoldierFemale" || typeStr == "HumanPlayer");
    bool isDoor = (typeStr == "Door");
    bool isTerminal = (typeStr == "Terminal");
    bool isCamera = (typeStr == "SCamera");
    bool isHeli = (typeStr == "Heli");
    bool isCar = (typeStr == "Car");
    bool isSpline = (typeStr == "SplineObjWaypoint");
    bool isSwitch = (typeStr == "Switch");
    bool isSplineContainer = (typeStr == "SplineObj");
    bool isWire = (typeStr == "Wire");

    bool isDecl = (typeStr == "Task_DeclareParameters");
    bool isGrouping = (typeStr == "Container" || typeStr == "Static" || typeStr == "Game" || typeStr == "Level" || typeStr == "Flow" || typeStr == "Task" || typeStr == "Folder" ||
                       typeStr == "container" || typeStr == "static" || typeStr == "game" || typeStr == "level" || typeStr == "flow" || typeStr == "task" || typeStr == "folder" || typeStr == "dynamic" || typeStr == "Dynamic");

    int currentObjIdx = -1;

    // Extract raw line from QSC buffer if available
    std::string rawLine;
    if (qsc && func->line_ > 0) {
        const char* scripts = qsc->GetScripts();
        if (scripts) {
            // Traverse scripts to find the start of the line
            int currentLine = 1;
            const char* p = scripts;
            while (*p && currentLine < func->line_) {
                if (*p == '\n') currentLine++;
                p++;
            }
            // Now p points to the start of the line
            const char* lineStart = p;
            while (*p && *p != '\n') p++;
            rawLine = std::string(lineStart, p - lineStart);
        }
    }

    // Always create an object entry for the tree view
    LevelObject obj;
    obj.type = typeStr;
    obj.isWire = isWire;
    obj.isSplineContainer = isSplineContainer;
    obj.isBuilding = isBuilding;
    bool hasTerminator = (rawLine.find(';') != std::string::npos);
    
    // Core logic: If it doesn't have a semicolon, it's a tree/container.
    // Exceptions: Declarations (isDecl) are never containers.
    obj.isContainer = !hasTerminator && !isDecl;
    
    // Fallback: Even if it HAS a terminator, some types are explicitly groupings (though rare)
    if (isGrouping) obj.isContainer = true;
    
    obj.parentIndex = parentIdx;
    obj.expanded = false; // Closed by default as requested
    obj.qscLine = rawLine;

    int arg_idx = 0;
    const QSC::arg_s* cur_a = a;
    while (cur_a) {
        if (isBuilding || isRigid || isTerminal) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 6: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.x = cur_a->dbl_; obj.original_rot.x = cur_a->dbl_; } break;
                case 7: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.y = cur_a->dbl_; obj.original_rot.y = cur_a->dbl_; } break;
                case 8: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = cur_a->dbl_; obj.original_rot.z = cur_a->dbl_; } break;
                case 9: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = Utils::Trim(cur_a->str_); break;
            }
        } else if (isGrouping) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
            }
        } else if (isSplineContainer) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) obj.linearSegments = (cur_a->dbl_ != 0); break;
                case 7: if (cur_a->type_ == QSC::arg_s::type_t::DBL) obj.splineSegmentCount = (int)cur_a->dbl_; break;
                case 9: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 10: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 11: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
            }
        } else if (isSoldier) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 1: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.type = cur_a->str_; break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 6: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = cur_a->dbl_; obj.original_rot.z = cur_a->dbl_; } break;
                case 7: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = Utils::Trim(cur_a->str_); break;
            }
        } else if (isDoor) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 9: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.x = cur_a->dbl_; obj.original_rot.x = cur_a->dbl_; } break;
                case 10: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.y = cur_a->dbl_; obj.original_rot.y = cur_a->dbl_; } break;
                case 11: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = cur_a->dbl_; obj.original_rot.z = cur_a->dbl_; } break;
                case 12: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = Utils::Trim(cur_a->str_); break;
            }
        } else if (isCamera) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 10: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = Utils::Trim(cur_a->str_); break;
            }
        } else if (isHeli || isCar) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 19: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = Utils::Trim(cur_a->str_); break;
            }
        } else if (isWire) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 9: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = Utils::Trim(cur_a->str_); break;
            }
        } else if (isSpline) {
            obj.isSplineWaypoint = true;
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: case 4: case 5:
                    if (cur_a->type_ == QSC::arg_s::type_t::DBL) {
                        int matIdx = arg_idx - 3;
                        if (matIdx < 3) obj.orientationMatrix[0][matIdx] = cur_a->dbl_; // Store as first row for Euler fallback
                    }
                    break;
                case 6: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 7: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 8: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 9: 
                    if (cur_a->type_ == QSC::arg_s::type_t::STR) {
                        obj.modelId = cur_a->str_; 
                        if (obj.modelId == "waypoint") obj.modelId = "";
                    }
                    break;
                case 10: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.segmentModelId = cur_a->str_; break;
            }
        } else if (isSwitch) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 12: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 13: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 14: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 15: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = cur_a->str_; break;
            }
        } else {
            // Default: just try to get taskId from first arg
            if (arg_idx == 0) obj.taskId = TaskIdFromArg(cur_a);
        }
        cur_a = cur_a->next_;
        arg_idx++;
    }

    // Clean up taskId
    if (!obj.taskId.empty() && obj.taskId.find("Task_New(") == 0) {
        size_t parenStart = obj.taskId.find('(');
        size_t parenEnd = obj.taskId.find(',', parenStart);
        if (parenStart != std::string::npos && parenEnd != std::string::npos) {
            obj.taskId = obj.taskId.substr(parenStart + 1, parenEnd - parenStart - 1);
        }
    }

    objects_.push_back(obj);
    currentObjIdx = (int)objects_.size() - 1;
    if (parentIdx != -1) {
        objects_[parentIdx].childrenIndices.push_back(currentObjIdx);
    }
    
    Logger::Get().Log(LogLevel::INFO, "[LevelObjects]   -> " + typeStr + ": " + obj.modelId + 
        " (parent: " + std::to_string(parentIdx) + ")");

    // Always recurse into FUNC arguments
    const QSC::arg_s* arg = func->args_;
    while (arg) {
        if (arg->type_ == QSC::arg_s::type_t::FUNC) {
            LoadRecursive(qsc, arg->func_, currentObjIdx);
        }
        arg = arg->next_;
    }
}

void LevelObjects::Unload() {
    objects_.clear();
    qtasks_.clear();
}

void LevelObjects::LoadModelNames() {
    if (!modelNames_.empty()) return;


    std::string qeditor_path = Config::Get().qEditorPath;

    char jsonPath[1024];
    Str_SPrintf(jsonPath, 1024, "%s\\IGIModels.json", qeditor_path.c_str());

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
        // Use %.2f for coordinates and %.10g for everything else to keep it clean but precise
        if (std::abs(v) > 1000.0) {
            snprintf(buf, sizeof(buf), "%.2f", v);
        } else {
            snprintf(buf, sizeof(buf), "%.10g", v);
        }
        std::string s(buf);
        // Ensure it doesn't end with .00 if we used %.2f
        if (s.find('.') != std::string::npos) {
            while (s.back() == '0') s.pop_back();
            if (s.back() == '.') s.pop_back();
        }
        // But for QSC, we usually want at least one decimal or just the int if it's 0
        if (s == "0") return "0";
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
            s += ".0";
        return s;
    };

    // Helper for fuzzy comparison of doubles to avoid noise changes
    auto isNear = [](double a, double b) { return std::abs(a - b) < 1e-4; };

    for (const auto& obj : objects_) {
        if (obj.modelId.empty()) continue;

        // Skip if the object wasn't intentionally modified (user move, rotation, or AI sync)
        // This prevents automatic visual-only changes (like terrain snapping) from polluting the QSC.
        if (!obj.modified) {
            continue;
        }

        anyChanges = true;
        Logger::Get().Log(LogLevel::INFO, "[LevelObjects::SaveToQSC] SAVING MODIFIED OBJECT: " + obj.name + " / " + obj.modelId);

        std::string taskTypeStr = obj.type.empty() ? (obj.isBuilding ? "Building" : "EditRigidObj") : obj.type;
        std::string quotedType = "\"" + taskTypeStr + "\"";
        
        size_t searchFrom = 0;
        size_t lineStart = std::string::npos;
        std::string modelIdToken = "\"" + obj.modelId + "\"";

        if (obj.taskId != "-1" && !obj.taskId.empty()) {
            // Search by taskId for Buildings and AI
            std::string taskIdToken = "Task_New(" + obj.taskId + ",";
            while (true) {
                size_t found = content.find(taskIdToken, searchFrom);
                if (found == std::string::npos) break;
                size_t ls = content.rfind('\n', found);
                ls = (ls == std::string::npos) ? 0 : ls + 1;
                size_t le = content.find('\n', found);
                if (le == std::string::npos) le = content.size();
                std::string line = content.substr(ls, le - ls);
                if (line.find(quotedType) != std::string::npos) {
                    lineStart = ls;
                    break;
                }
                searchFrom = found + 1;
            }
        } else {
            // Search by modelId + original position for EditRigidObj (taskId -1)
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
                if (line.find(quotedType) != std::string::npos &&
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

        // Use the current name.
        // Only use friendly name if the object is NEW (wasn't in QSC) and name is currently empty.
        std::string correctName = obj.name;
        if (correctName.empty() && !obj.has_original_name) {
            correctName = GetModelName(obj.modelId);
        }

        // Always save the original QSC coordinates (before any terrain snapping/rendering offsets).
        // obj.pos is the RENDER position (snapped to terrain). Writing it back would corrupt the file.
        // obj.original_pos is the exact value parsed from QSC - this is what we must preserve.
        double saveX = obj.original_pos.x;
        double saveY = obj.original_pos.y;
        double saveZ = obj.original_pos.z;
        // Only use the live pos if the object was explicitly moved by the user (modified flag)
        if (obj.modified) {
            saveX = obj.pos.x;
            saveY = obj.pos.y;
            saveZ = obj.pos.z - obj.snap_z_offset; // strip render offset
        }

        // Preserve indentation
        std::string indent;
        for (char c : oldLine) {
            if (c == ' ' || c == '\t') indent += c;
            else break;
        }

        // Extract extra arguments and tail from the old line
        // Determine which comma the model ID appears after, based on task type
        int modelIdCommaIndex = 9; // Default for Building/EditRigidObj/Terminal
        if (taskTypeStr == "HumanSoldier" || taskTypeStr == "HumanSoldierFemale") modelIdCommaIndex = 7;
        else if (taskTypeStr == "Door") modelIdCommaIndex = 12;
        else if (taskTypeStr == "SCamera" || taskTypeStr == "SplineObjWaypoint") modelIdCommaIndex = 10;
        else if (taskTypeStr == "Heli" || taskTypeStr == "Car") modelIdCommaIndex = 19;
        else if (taskTypeStr == "Switch") modelIdCommaIndex = 17;

        // Find the position in the old line where the model ID and extra args begin
        size_t modelIdPosInLine = std::string::npos;
        size_t currentPos = 0;
        for (int c = 0; c < modelIdCommaIndex; ++c) {
            currentPos = oldLine.find(',', currentPos);
            if (currentPos == std::string::npos) break;
            currentPos++;
        }
        
        std::string extraArgs;
        std::string tail = ");";
        if (obj.childrenIndices.size() > 0) tail = ")"; // No semicolon for tree parents
        
        if (currentPos != std::string::npos) {
            // Found the comma before model ID. Skip the old model ID to find extra args.
            size_t oldModelIdEnd = oldLine.find(',', currentPos);
            if (oldModelIdEnd == std::string::npos) {
                // No more commas, check for closing paren
                oldModelIdEnd = oldLine.find_last_of(')');
            }
            
            if (oldModelIdEnd != std::string::npos) {
                size_t tailPos = oldLine.find_last_of(')');
                if (tailPos != std::string::npos) {
                    // Backtrack to find all consecutive parens
                    while (tailPos > oldModelIdEnd && oldLine[tailPos - 1] == ')') {
                        tailPos--;
                    }
                    extraArgs = oldLine.substr(oldModelIdEnd, tailPos - oldModelIdEnd);
                    tail = oldLine.substr(tailPos);
                } else {
                    extraArgs = oldLine.substr(oldModelIdEnd);
                    tail = "";
                }
            }
        }
        std::string newLine = oldLine;
        if (!obj.qscLine.empty()) {
            newLine = obj.qscLine;
        } else {
            // Build new line with unified logic for extra arguments (fallback for new objects)
            std::stringstream ss;
            ss << indent << "Task_New(" << obj.taskId << ", " << quotedType << ", \"" << correctName << "\", ";
            
            if (taskTypeStr == "SplineObjWaypoint") {
                // Special case for Spline: 3 rot args before Pos (based on latest snippet)
                // Extract the 3 rotation arguments (args 3, 4, 5) from old line
                size_t rotStart = 0;
                for(int i=0; i<3; ++i) { rotStart = oldLine.find(',', rotStart); if(rotStart != std::string::npos) rotStart++; }
                size_t rotEnd = rotStart;
                for(int i=0; i<3; ++i) { rotEnd = oldLine.find(',', rotEnd); if(rotEnd != std::string::npos) rotEnd++; }
                std::string rotBlock = "0, 0, 0, ";
                if(rotStart != std::string::npos && rotEnd != std::string::npos) rotBlock = oldLine.substr(rotStart, rotEnd - rotStart);
                
                ss << rotBlock << fmt(saveX) << ", " << fmt(saveY) << ", " << fmt(saveZ) << ", \"" << obj.modelId << "\", ";
            } else {
                ss << fmt(saveX) << ", " << fmt(saveY) << ", " << fmt(saveZ) << ", ";
                
                if (taskTypeStr == "HumanSoldier" || taskTypeStr == "HumanSoldierFemale") {
                    ss << fmt(obj.rot.z) << ", ";
                } else if (taskTypeStr == "Door") {
                    ss << "0, 0, 0, "; // StopX, StopY, Slider (fallback)
                    ss << fmt(obj.rot.x) << ", " << fmt(obj.rot.y) << ", " << fmt(obj.rot.z) << ", ";
                } else if (taskTypeStr == "SCamera") {
                    ss << fmt(obj.rot.z) << ", \"313_01_1\", -0.1, 0, "; // HolderGamma, HolderModel, CameraAlpha, CameraGamma (placeholders)
                } else if (taskTypeStr == "Heli" || taskTypeStr == "Car") {
                    ss << "1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, "; // Orient, Thrust, Speed (placeholders)
                } else if (taskTypeStr == "Switch") {
                    ss << "1, 0, 0, 0, 1, 0, 0, 0, 1, \"\", 0, "; // Orient, On, InitialOn (placeholders)
                } else {
                    // Buildings, RigidObjs, Terminals
                    ss << fmt(obj.rot.x) << ", " << fmt(obj.rot.y) << ", " << fmt(obj.rot.z) << ", ";
                }
            }
            
            ss << modelIdToken << extraArgs << tail;
            newLine = ss.str();
        }

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

    // Reset modified flags and sync original state after successful save
    for (auto& obj : objects_) {
        // Reset modified flag after save. original_pos stays exactly as loaded from QSC.
        obj.modified = false;
        // Do NOT overwrite original_pos here - it must remain the pristine QSC value
        // until the user explicitly moves the object.
    }

    Logger::Get().Log(LogLevel::INFO, "[LevelObjects::SaveToQSC] Successfully saved changes to: " + qscPath);
}

void LevelObjects::ParseTaskLine(const std::string& line, LevelObject& obj) {
    if (line.empty()) return;
    obj.modified = true;
    
    // Rudimentary parser for the Task Editor "Notepad" edits
    // It looks for patterns like Task_New(ID, "Type", "Name", X, Y, Z, RX, RY, RZ, "Model", ...)
    
    // Extract taskId
    size_t p1 = line.find('(');
    size_t p2 = line.find(',', p1);
    if (p1 != std::string::npos && p2 != std::string::npos) {
        obj.taskId = Utils::Trim(line.substr(p1 + 1, p2 - p1 - 1));
    }
    
    // Extract Type and Name
    size_t quote1 = line.find('"', p2);
    size_t quote2 = line.find('"', quote1 + 1);
    if (quote1 != std::string::npos && quote2 != std::string::npos) {
        obj.type = line.substr(quote1 + 1, quote2 - quote1 - 1);
        
        size_t quote3 = line.find('"', quote2 + 1);
        size_t quote4 = line.find('"', quote3 + 1);
        if (quote3 != std::string::npos && quote4 != std::string::npos) {
            obj.name = line.substr(quote3 + 1, quote4 - quote3 - 1);
        }
    }
    
    // Extract numeric arguments (Pos and Rot)
    // We split by comma and try to find where X,Y,Z are based on type
    std::vector<std::string> args;
    std::string currentArg;
    bool inQuote = false;
    for (size_t i = (p1 != std::string::npos ? p1 + 1 : 0); i < line.size(); ++i) {
        if (line[i] == '"') inQuote = !inQuote;
        if (line[i] == ',' && !inQuote) {
            args.push_back(Utils::Trim(currentArg));
            currentArg = "";
        } else if (line[i] == ')' && !inQuote) {
            break;
        } else {
            currentArg += line[i];
        }
    }
    args.push_back(Utils::Trim(currentArg));
    
    // Args indices for Task_New:
    // 0: taskId, 1: type, 2: name, 3: X, 4: Y, 5: Z, 6: RX, 7: RY, 8: RZ, 9: modelId
    if (args.size() > 5) {
        try {
            if (obj.type == "HumanSoldier" || obj.type == "HumanSoldierFemale") {
                obj.pos.x = std::stod(args[3]);
                obj.pos.y = std::stod(args[4]);
                obj.pos.z = std::stod(args[5]);
                if (args.size() > 6) obj.rot.z = std::stod(args[6]);
                if (args.size() > 7) obj.modelId = Utils::Trim(args[7]);
            } else if (obj.type == "Door") {
                obj.pos.x = std::stod(args[3]);
                obj.pos.y = std::stod(args[4]);
                obj.pos.z = std::stod(args[5]);
                // RX, RY, RZ are later in Door (after 3 sliding args)
                if (args.size() > 11) {
                    obj.rot.x = std::stod(args[9]);
                    obj.rot.y = std::stod(args[10]);
                    obj.rot.z = std::stod(args[11]);
                }
                if (args.size() > 12) obj.modelId = Utils::Trim(args[12]);
            } else {
                // Buildings, RigidObjs, etc.
                obj.pos.x = std::stod(args[3]);
                obj.pos.y = std::stod(args[4]);
                obj.pos.z = std::stod(args[5]);
                if (args.size() > 8) {
                    obj.rot.x = std::stod(args[6]);
                    obj.rot.y = std::stod(args[7]);
                    obj.rot.z = std::stod(args[8]);
                }
                if (args.size() > 9) obj.modelId = Utils::Trim(args[9]);
            }
        } catch (...) {
            // Ignore parse errors from user typos
        }
    }
    
    obj.modified = true;
}

void LevelObjects::UpdateCoordinatesInLine(LevelObject& obj) {
    if (obj.qscLine.empty()) return;
    
    // We update the numbers in the qscLine based on the current obj.pos/rot
    // Since manually parsing and replacing numbers in a string is error-prone,
    // we use a simplified regeneration for the "core" part of the task.
    
    std::string typeToken = "\"" + obj.type + "\"";
    std::string nameToken = "\"" + obj.name + "\"";
    
    auto fmt = [](double v) {
        char buf[64];
        sprintf(buf, "%.1f", v);
        std::string s = buf;
        if (s.find('.') != std::string::npos) {
            while (s.size() > 1 && s.back() == '0') s.pop_back();
            if (s.back() == '.') s.pop_back();
        }
        return s;
    };
    
    // Find the args start
    size_t p3 = obj.qscLine.find(nameToken);
    if (p3 == std::string::npos) return;
    p3 = obj.qscLine.find(',', p3 + nameToken.size());
    if (p3 == std::string::npos) return;
    p3++; // Start of X arg
    
    // Find the end of RX,RY,RZ or ModelID block
    // We basically want to replace the numbers between p3 and the modelId or end.
    // To be safe, we reconstruct the line head and preserve the tail.
    
    size_t headEnd = p3;
    // ... complex logic ...
    // For now, let's just mark it as modified and SaveToQSC will handle it using the obj.pos/rot.
    // BUT the user wants the Notepad to show the live data if they move it.
    
    // Let's do a simple replacement for the first 3 numbers (X, Y, Z)
    std::stringstream ss;
    ss << " " << fmt(obj.pos.x) << ", " << fmt(obj.pos.y) << ", " << fmt(obj.pos.z - obj.snap_z_offset) << ", ";
    
    // Find the 3rd comma after p3
    size_t pEnd = p3;
    for(int i=0; i<3; ++i) {
        pEnd = obj.qscLine.find(',', pEnd);
        if (pEnd == std::string::npos) break;
        pEnd++;
    }
    
    if (pEnd != std::string::npos) {
        obj.qscLine.replace(p3, pEnd - p3, ss.str());
    }
}

std::string LevelObjects::GenerateTaskLine(const LevelObject& obj) {
    if (!obj.qscLine.empty()) {
        return obj.qscLine;
    }
    
    // Fallback if not populated
    std::string correctName = obj.name;
    if (correctName.empty() && !obj.has_original_name) {
        correctName = GetModelName(obj.modelId);
    }
    
    auto fmt = [](double v) {
        char buf[64];
        sprintf(buf, "%.1f", v);
        std::string s = buf;
        if (s.find('.') != std::string::npos) {
            while (s.size() > 1 && s.back() == '0') s.pop_back();
            if (s.back() == '.') s.pop_back();
        }
        return s;
    };

    std::stringstream ss;
    ss << "Task_New(" << obj.taskId << ", \"" << obj.type << "\", \"" << correctName << "\", ";
    
    if (obj.type == "SplineObjWaypoint") {
        ss << "0, 0, 0, " << fmt(obj.pos.x) << ", " << fmt(obj.pos.y) << ", " << fmt(obj.pos.z) << ", \"" << obj.modelId << "\", );";
    } else {
        ss << fmt(obj.pos.x) << ", " << fmt(obj.pos.y) << ", " << fmt(obj.pos.z) << ", ";
        
        if (obj.type == "HumanSoldier" || obj.type == "HumanSoldierFemale") {
            ss << fmt(obj.rot.z) << ", \"" << obj.modelId << "\", );";
        } else if (obj.type == "Door") {
            ss << "0, 0, 0, " << fmt(obj.rot.x) << ", " << fmt(obj.rot.y) << ", " << fmt(obj.rot.z) << ", \"" << obj.modelId << "\", );";
        } else if (obj.type == "SCamera") {
            ss << fmt(obj.rot.z) << ", \"313_01_1\", -0.1, 0, \"" << obj.modelId << "\", );";
        } else if (obj.type == "Heli" || obj.type == "Car") {
            ss << "1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, \"" << obj.modelId << "\", );";
        } else if (obj.type == "Switch") {
            ss << "1, 0, 0, 0, 1, 0, 0, 0, 1, \"\", 0, \"" << obj.modelId << "\", );";
        } else if (obj.isContainer) {
            ss << ");";
        } else {
            // Buildings, RigidObjs, Terminals
            ss << fmt(obj.rot.x) << ", " << fmt(obj.rot.y) << ", " << fmt(obj.rot.z) << ", \"" << obj.modelId << "\", );";
        }
    }
    
    return ss.str();
}
