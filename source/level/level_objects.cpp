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
    if (a->type_ == QSC::arg_s::type_t::BOOL) return a->bool_ ? "1" : "0";
    return "";
}

static std::string EscapeQscString(const std::string& input) {
    std::string out;
    out.reserve(input.size() + 8);
    for (char c : input) {
        if (c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

static std::string FormatQscDouble(double v) {
    char buf[64];
    if (std::abs(v) >= 1000.0) {
        snprintf(buf, sizeof(buf), "%.2f", v);
    } else {
        snprintf(buf, sizeof(buf), "%.10g", v);
    }

    std::string s(buf);
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s.empty() || s == "-0") s = "0";
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos && s.find('E') == std::string::npos) {
        s += ".0";
    }
    return s;
}

static std::string FormatQscIntegerToken(const std::string& token) {
    std::string trimmed = Utils::Trim(token);
    if (trimmed.empty()) return trimmed;
    try {
        double value = std::stod(trimmed);
        int ivalue = (int)std::llround(value);
        return std::to_string(ivalue);
    } catch (...) {
        return trimmed;
    }
}

static std::string ArgTokenFromArg(const QSC::arg_s* a) {
    if (!a) return "";
    switch (a->type_) {
        case QSC::arg_s::type_t::STR:
            return "\"" + EscapeQscString(a->str_ ? a->str_ : "") + "\"";
        case QSC::arg_s::type_t::DBL:
            return FormatQscDouble(a->dbl_);
        case QSC::arg_s::type_t::BOOL:
            return a->bool_ ? "TRUE" : "FALSE";
        case QSC::arg_s::type_t::FUNC:
            return "";
    }
    return "";
}

static void SplitTopLevelArgs(const std::string& text, std::vector<std::string>& outArgs) {
    outArgs.clear();
    std::string current;
    int parenDepth = 0;
    bool inQuote = false;
    bool escape = false;

    for (char c : text) {
        if (inQuote) {
            current.push_back(c);
            if (escape) {
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                inQuote = false;
            }
            continue;
        }

        if (c == '"') {
            inQuote = true;
            current.push_back(c);
            continue;
        }
        if (c == '(') {
            parenDepth++;
            current.push_back(c);
            continue;
        }
        if (c == ')') {
            if (parenDepth > 0) parenDepth--;
            current.push_back(c);
            continue;
        }
        if (c == ',' && parenDepth == 0) {
            outArgs.push_back(Utils::Trim(current));
            current.clear();
            continue;
        }
        current.push_back(c);
    }

    std::string tail = Utils::Trim(current);
    if (!tail.empty()) outArgs.push_back(tail);
}


void LevelObjects::Load(ILevelDynCube* level_dyn_cube, const QSC* qsc_objects) {
    objects_.clear();
    qtasks_.clear();
    
    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Starting recursive Load...");

    for (int i = 0; i < qsc_objects->GetRootFuncCount(); ++i) {
        LoadRecursive(qsc_objects, qsc_objects->GetRootFunc(i), -1);
    }

    for (int i = 0; i < (int)objects_.size(); ++i) {
        objects_[i].qscLine = SerializeObjectRecursive(objects_, i);
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
    obj.qscFuncName = funcName;
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
        if (cur_a->type_ != QSC::arg_s::type_t::FUNC) {
            obj.argTokens.push_back(ArgTokenFromArg(cur_a));
        }

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
    // Only assign qscLine to top-level tasks to avoid sharing the same string among nested sub-calls
    if (parentIdx == -1) {
        obj.qscLine = rawLine;
    }
    obj.isNested = (parentIdx != -1);
    
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
                modelIds_[name] = id;
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

std::string LevelObjects::GetModelId(const std::string& modelName) {
    if (modelNames_.empty()) LoadModelNames();
    auto it = modelIds_.find(modelName);
    if (it != modelIds_.end()) return it->second;
    return "";
}

void LevelObjects::SaveToQSC(const std::string& qscPath) {
    std::string lowerPath = qscPath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
    if (lowerPath.find("qfiles") != std::string::npos) {
        Logger::Get().Log(LogLevel::ERR, "[LevelObjects::SaveToQSC] CRITICAL ERROR: Attempted to write to READ-ONLY QFiles path: " + qscPath);
        return;
    }

    std::ofstream outFile(qscPath);
    if (!outFile) {
        Logger::Get().Log(LogLevel::ERR, "[LevelObjects::SaveToQSC] Failed to open QSC file for writing: " + qscPath);
        return;
    }

    bool first = true;
    for (int i = 0; i < (int)objects_.size(); ++i) {
        const auto& obj = objects_[i];
        if (obj.parentIndex != -1 || obj.deleted) continue;

        if (!first) outFile << "\r\n";
        outFile << SerializeObjectRecursive(objects_, i) << ";";
        first = false;
    }
    outFile.close();

    for (auto& obj : objects_) {
        obj.modified = false;
        obj.original_pos = glm::dvec3(obj.pos.x, obj.pos.y, obj.pos.z - obj.snap_z_offset);
        obj.original_rot = obj.rot;
        obj.qscLine = GenerateTaskLine(obj);
    }

    Logger::Get().Log(LogLevel::INFO, "[LevelObjects::SaveToQSC] Successfully saved changes to: " + qscPath);
}

void LevelObjects::ParseTaskLine(const std::string& line, LevelObject& obj) {
    if (line.empty()) return;
    auto unquote = [](const std::string& token) -> std::string {
        if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
            std::string out;
            out.reserve(token.size() - 2);
            bool escape = false;
            for (size_t i = 1; i + 1 < token.size(); ++i) {
                char c = token[i];
                if (escape) {
                    out.push_back(c);
                    escape = false;
                } else if (c == '\\') {
                    escape = true;
                } else {
                    out.push_back(c);
                }
            }
            return out;
        }
        return token;
    };

    std::string trimmed = Utils::Trim(line);
    if (!trimmed.empty() && trimmed.back() == ';') trimmed.pop_back();

    size_t openParen = trimmed.find('(');
    size_t closeParen = trimmed.rfind(')');
    if (openParen == std::string::npos || closeParen == std::string::npos || closeParen <= openParen) {
        return;
    }

    obj.qscFuncName = Utils::Trim(trimmed.substr(0, openParen));

    std::vector<std::string> args;
    SplitTopLevelArgs(trimmed.substr(openParen + 1, closeParen - openParen - 1), args);

    obj.argTokens.clear();
    for (const auto& arg : args) {
        std::string token = Utils::Trim(arg);
        size_t funcPos = token.find('(');
        bool isFuncArg = !token.empty() && token.front() != '"' && funcPos != std::string::npos;
        if (!isFuncArg) obj.argTokens.push_back(token);
    }

    if (obj.qscFuncName == "Task_New" && obj.argTokens.size() >= 3) {
        obj.taskId = FormatQscIntegerToken(obj.argTokens[0]);
        if (obj.taskId != "-1") {
            try {
                int taskId = std::stoi(obj.taskId);
                if (taskId < 1 || taskId > 4000) {
                    obj.taskId = "-1";
                }
            } catch (...) {
                obj.taskId = "-1";
            }
        }
        obj.argTokens[0] = obj.taskId;
        obj.type = unquote(obj.argTokens[1]);
        obj.name = unquote(obj.argTokens[2]);
    }

    auto readDouble = [&](size_t idx, double& out) {
        if (idx >= obj.argTokens.size()) return;
        try {
            out = std::stod(obj.argTokens[idx]);
        } catch (...) {
        }
    };

    if (obj.qscFuncName == "Task_New") {
        if (obj.type == "Container" || obj.type == "Static" || obj.type == "Dynamic" || obj.type == "Level") {
            obj.isContainer = true;
        }

        if (obj.type == "HumanSoldier" || obj.type == "HumanSoldierFemale" || obj.type == "HumanPlayer") {
            readDouble(3, obj.pos.x);
            readDouble(4, obj.pos.y);
            readDouble(5, obj.pos.z);
            readDouble(6, obj.rot.z);
            if (obj.argTokens.size() > 7) obj.modelId = unquote(obj.argTokens[7]);
        } else if (obj.type == "Door") {
            readDouble(3, obj.pos.x);
            readDouble(4, obj.pos.y);
            readDouble(5, obj.pos.z);
            readDouble(9, obj.rot.x);
            readDouble(10, obj.rot.y);
            readDouble(11, obj.rot.z);
            if (obj.argTokens.size() > 12) obj.modelId = unquote(obj.argTokens[12]);
        } else if (obj.type == "SCamera") {
            readDouble(3, obj.pos.x);
            readDouble(4, obj.pos.y);
            readDouble(5, obj.pos.z);
            if (obj.argTokens.size() > 10) obj.modelId = unquote(obj.argTokens[10]);
        } else if (obj.type == "Heli" || obj.type == "Car") {
            readDouble(3, obj.pos.x);
            readDouble(4, obj.pos.y);
            readDouble(5, obj.pos.z);
            if (obj.argTokens.size() > 19) obj.modelId = unquote(obj.argTokens[19]);
        } else if (obj.type == "SplineObjWaypoint") {
            readDouble(6, obj.pos.x);
            readDouble(7, obj.pos.y);
            readDouble(8, obj.pos.z);
            if (obj.argTokens.size() > 9) obj.modelId = unquote(obj.argTokens[9]);
            if (obj.argTokens.size() > 10) obj.segmentModelId = unquote(obj.argTokens[10]);
        } else if (obj.argTokens.size() > 8) {
            readDouble(3, obj.pos.x);
            readDouble(4, obj.pos.y);
            readDouble(5, obj.pos.z);
            readDouble(6, obj.rot.x);
            readDouble(7, obj.rot.y);
            readDouble(8, obj.rot.z);
            if (obj.argTokens.size() > 9) obj.modelId = unquote(obj.argTokens[9]);
        }
    }

    obj.modified = true;
    obj.qscLine = trimmed;
}

void LevelObjects::UpdateCoordinatesInLine(LevelObject& obj) {
    auto setToken = [&](size_t idx, const std::string& value) {
        if (obj.argTokens.size() <= idx) obj.argTokens.resize(idx + 1);
        obj.argTokens[idx] = value;
    };
    auto setStringToken = [&](size_t idx, const std::string& value) {
        setToken(idx, "\"" + EscapeQscString(value) + "\"");
    };

    if (obj.qscFuncName == "Task_New") {
        setToken(0, obj.taskId.empty() ? "-1" : obj.taskId);
        setStringToken(1, obj.type);
        setStringToken(2, obj.name);

        double saveZ = obj.pos.z - obj.snap_z_offset;
        if (obj.type == "HumanSoldier" || obj.type == "HumanSoldierFemale" || obj.type == "HumanPlayer") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setToken(6, FormatQscDouble(obj.rot.z));
            setStringToken(7, obj.modelId);
        } else if (obj.type == "Door") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setToken(9, FormatQscDouble(obj.rot.x));
            setToken(10, FormatQscDouble(obj.rot.y));
            setToken(11, FormatQscDouble(obj.rot.z));
            setStringToken(12, obj.modelId);
        } else if (obj.type == "SCamera") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setStringToken(10, obj.modelId);
        } else if (obj.type == "Heli" || obj.type == "Car") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setStringToken(19, obj.modelId);
        } else if (obj.type == "SplineObjWaypoint") {
            setToken(6, FormatQscDouble(obj.pos.x));
            setToken(7, FormatQscDouble(obj.pos.y));
            setToken(8, FormatQscDouble(saveZ));
            setStringToken(9, obj.modelId);
            setStringToken(10, obj.segmentModelId);
        } else if (!obj.isContainer) {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setToken(6, FormatQscDouble(obj.rot.x));
            setToken(7, FormatQscDouble(obj.rot.y));
            setToken(8, FormatQscDouble(obj.rot.z));
            if (!obj.modelId.empty() || obj.argTokens.size() > 9) setStringToken(9, obj.modelId);
        }
    }

    obj.qscLine = GenerateTaskLine(obj);
}

std::string LevelObjects::GenerateTaskLine(const LevelObject& obj) {
    std::stringstream ss;
    ss << obj.qscFuncName << "(";
    for (size_t i = 0; i < obj.argTokens.size(); ++i) {
        if (i) ss << ", ";
        ss << obj.argTokens[i];
    }
    ss << ")";
    return ss.str();
}

std::string LevelObjects::SerializeObjectRecursive(const std::vector<LevelObject>& objects, int idx) {
    if (idx < 0 || idx >= (int)objects.size()) return "";

    const LevelObject& obj = objects[idx];
    std::function<std::string(int, int)> serialize = [&](int objectIdx, int depth) -> std::string {
        const LevelObject& node = objects[objectIdx];
        std::string indent(depth * 2, ' ');

        std::stringstream ss;
        ss << indent << node.qscFuncName << "(";

        bool first = true;
        for (size_t i = 0; i < node.argTokens.size(); ++i) {
            if (!first) ss << ", ";
            ss << node.argTokens[i];
            first = false;
        }

        std::vector<int> liveChildren;
        for (int childIdx : node.childrenIndices) {
            if (childIdx < 0 || childIdx >= (int)objects.size()) continue;
            if (objects[childIdx].deleted) continue;
            liveChildren.push_back(childIdx);
        }

        if (!liveChildren.empty()) {
            for (size_t i = 0; i < liveChildren.size(); ++i) {
                ss << ",\r\n" << serialize(liveChildren[i], depth + 1);
            }
            ss << "\r\n" << indent;
        }

        ss << ")";
        return ss.str();
    };

    return serialize(idx, 0);
}
