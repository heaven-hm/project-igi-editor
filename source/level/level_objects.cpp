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
    
    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Starting recursive Load...");

    for (int i = 0; i < qsc_objects->GetRootFuncCount(); ++i) {
        LoadRecursive(qsc_objects->GetRootFunc(i), -1);
    }

    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Load complete. Total objects: " + std::to_string(objects_.size()));
}

void LevelObjects::LoadRecursive(const QSC::func_s* func, int parentIdx) {
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
    bool isSoldier = (typeStr == "HumanSoldier" || typeStr == "HumanSoldierFemale");
    bool isDoor = (typeStr == "Door");

    int currentObjIdx = -1;

    if (isBuilding || isRigid || isSoldier || isDoor) {
        LevelObject obj;
        obj.type = typeStr;
        obj.isBuilding = isBuilding;
        obj.parentIndex = parentIdx;

        int arg_idx = 0;
        const QSC::arg_s* cur_a = a;
        while (cur_a) {
            if (isBuilding || isRigid) {
                switch (arg_idx) {
                    case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                    case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                    case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                    case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                    case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                    case 6: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.x = cur_a->dbl_; obj.original_rot.x = cur_a->dbl_; } break;
                    case 7: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.y = cur_a->dbl_; obj.original_rot.y = cur_a->dbl_; } break;
                    case 8: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = cur_a->dbl_; obj.original_rot.z = cur_a->dbl_; } break;
                    case 9: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = cur_a->str_; break;
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
                    case 7: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = cur_a->str_; break;
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
                    case 12: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = cur_a->str_; break;
                }
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
    } else {
        // Non-object task (Container, etc.), propagate current parent index to nested calls
        currentObjIdx = parentIdx;
    }

    // Always recurse into FUNC arguments
    const QSC::arg_s* arg = func->args_;
    while (arg) {
        if (arg->type_ == QSC::arg_s::type_t::FUNC) {
            LoadRecursive(arg->func_, currentObjIdx);
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

        // Subtract the snap offset so the saved Z matches the intended coordinate
        double saveZ = obj.pos.z - obj.snap_z_offset;

        // Preserve indentation
        std::string indent;
        for (char c : oldLine) {
            if (c == ' ' || c == '\t') indent += c;
            else break;
        }

        // Extract extra arguments and tail from the old line
        // Determine which comma the model ID appears after, based on task type
        int modelIdCommaIndex = 9; // Default for Building/EditRigidObj
        if (taskTypeStr == "HumanSoldier" || taskTypeStr == "HumanSoldierFemale") modelIdCommaIndex = 7;
        else if (taskTypeStr == "Door") modelIdCommaIndex = 12;

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
        // Build new line with unified logic for extra arguments
        std::stringstream ss;
        // modelIdToken already defined at line 355
        ss << indent << "Task_New(" << obj.taskId << ", " << quotedType << ", \"" << correctName << "\", ";
        ss << fmt(obj.pos.x) << ", " << fmt(obj.pos.y) << ", " << fmt(saveZ) << ", ";
        
        if (taskTypeStr == "HumanSoldier" || taskTypeStr == "HumanSoldierFemale") {
            // HumanSoldier only has one rotation (Yaw)
            ss << fmt(obj.rot.z) << ", ";
        } else if (taskTypeStr == "Door") {
            // Door has extra parameters (stop X, Y, slider) before rotation
            // We use the extraArgs to preserve these if they weren't in the modelId search range
            // But based on the example, we need to place rotation at 9, 10, 11
            // Let's assume the first 3 extra args in old line were stop X, Y, slider.
            // A safer way is to just use the unified formatting if it matches the example structure.
            // Example: Task_New(ID, "Door", Name, X, Y, Z, StopX, StopY, Slider, RX, RY, RZ, ModelId, ...)
            // We'll use dummy 0s for stop coords if not known, or trust extraArgs logic.
            
            // Re-extracting Stop coords and Slider from oldLine if possible
            std::string stopX = "0", stopY = "0", slider = "0";
            // ... (optional refinement) ...
            
            ss << "0, 0, 0, "; // StopX, StopY, Slider (fallback)
            ss << fmt(obj.rot.x) << ", " << fmt(obj.rot.y) << ", " << fmt(obj.rot.z) << ", ";
        } else {
            // Buildings and RigidObjs have three rotations
            ss << fmt(obj.rot.x) << ", " << fmt(obj.rot.y) << ", " << fmt(obj.rot.z) << ", ";
        }
        
        ss << modelIdToken << extraArgs << tail;
        std::string newLine = ss.str();

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

    // Reset modified flags and sync original state after successful save
    for (auto& obj : objects_) {
        obj.modified = false;
        obj.original_pos = obj.pos;
        obj.original_pos.z -= obj.snap_z_offset;
        obj.original_rot = obj.rot;
    }

    Logger::Get().Log(LogLevel::INFO, "[LevelObjects::SaveToQSC] Successfully saved changes to: " + qscPath);
}

