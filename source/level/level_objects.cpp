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

    // Large coordinates (>=1000): keep 2 decimal places, trim trailing zeros.
    // Small values: use Real32 precision (7 significant digits) so a value that
    // was originally a float like 0.95 or 1.54 round-trips cleanly instead of
    // printing double-precision noise (0.949999988079071, 1.5399999618530273).
    if (std::abs(v) >= 1000.0) {
        snprintf(buf, sizeof(buf), "%.2f", v);
    } else {
        // Cast through float first to collapse the double-precision noise, then
        // format with 7 significant digits (max lossless for Real32).
        float f = static_cast<float>(v);
        snprintf(buf, sizeof(buf), "%.7g", static_cast<double>(f));
    }

    std::string s(buf);
    // Strip trailing zeros after decimal point.
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s.empty() || s == "-0") s = "0";
    // Ensure the value is always recognisable as a float token (has . or e).
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
            return "\"" + std::string(a->str_ ? a->str_ : "") + "\"";
        case QSC::arg_s::type_t::DBL: {
            double v = a->dbl_;
            // Only write as a plain integer when the original token was an integer
            // literal (no '.' or 'e'). Float literals like 24316832.0 must keep
            // their decimal point so a re-serialized line matches the source format.
            double intPart;
            if (!a->is_float_ && std::modf(v, &intPart) == 0.0 &&
                v >= -2147483648.0 && v <= 2147483647.0) {
                return std::to_string((long long)intPart);
            }
            return FormatQscDouble(v);
        }
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

    // Fallback: If a Task_New's task note/name is empty, query its child's model name in IGIModels.json
    for (auto& obj : objects_) {
        if (obj.qscFuncName == "Task_New" && obj.name.empty()) {
            std::string foundModelId = "";
            for (int childIdx : obj.childrenIndices) {
                if (childIdx >= 0 && childIdx < (int)objects_.size()) {
                    const auto& child = objects_[childIdx];
                    if (!child.modelId.empty()) {
                        foundModelId = child.modelId;
                        break;
                    }
                }
            }
            if (!foundModelId.empty()) {
                std::string friendlyName = GetModelName(foundModelId);
                if (!friendlyName.empty()) {
                    obj.name = friendlyName;
                    obj.original_name = friendlyName;
                    obj.has_original_name = true;
                    
                    // Also update the argument token in argTokens so that the UI text box matches
                    if (obj.argTokens.size() > 2) {
                        obj.argTokens[2] = "\"" + friendlyName + "\"";
                    }
                    
                    // Force re-generation of qscLine so the UI displays it and the compiler sees it
                    obj.qscLine.clear();
                    
                    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Resolved empty task note for Task " + obj.taskId + " to model friendly name: " + friendlyName);
                }
            }
        }
    }

    // Resolve WEAPON_ID_*/AMMO_ID_* enum strings to real model IDs for GunPickup/AmmoPickup.
    // This is the authoritative resolution pass — it runs after all objects are loaded so we
    // call LoadModelNames() exactly once, and it catches any objects where the per-arg
    // resolution inside LoadRecursive may have been skipped (e.g. JSON not yet on disk).
    LoadModelNames();
    for (auto& obj : objects_) {
        if (obj.type == "GunPickup" || obj.type == "AmmoPickup") {
            if (!obj.modelId.empty() &&
                (obj.modelId.rfind("WEAPON_ID_", 0) == 0 || obj.modelId.rfind("AMMO_ID_", 0) == 0)) {
                auto it = modelIds_.find(obj.modelId);
                if (it != modelIds_.end() && !it->second.empty()) {
                    Logger::Get().Log(LogLevel::INFO,
                        "[LevelObjects] Resolved pickup enum: " + obj.modelId + " -> " + it->second +
                        " (task " + obj.taskId + ")");
                    obj.modelId = it->second;
                } else {
                    Logger::Get().Log(LogLevel::WARNING,
                        "[LevelObjects] No model ID mapping found for pickup enum: " + obj.modelId);
                }
            }
        }
    }

    // Only generate qscLine for objects that didn't get a raw line from the parser
    for (int i = 0; i < (int)objects_.size(); ++i) {
        if (objects_[i].qscLine.empty()) {
            objects_[i].qscLine = GenerateTaskLine(objects_[i]);
        }
    }

    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Load complete. Total objects: " + std::to_string(objects_.size()));
}

void LevelObjects::LoadRecursive(const QSC* qsc, const QSC::func_s* func, int parentIdx) {
    if (!func) return;

    const QSC::arg_s* a = func->args_;
    if (!a) return;

    std::string funcName = func->func_name_;
    std::string typeStr;

    Logger::Get().Log(LogLevel::INFO, "[DEBUG] Function name: " + funcName);
    // Check if it's a Task_New call (common wrapper)
    if (funcName == "Task_New") {
        const QSC::arg_s* cur = a;
        int argCount = 0;
        while (cur) {
            if (argCount == 1 && cur->type_ == QSC::arg_s::type_t::STR) {
                typeStr = cur->str_;
                Logger::Get().Log(LogLevel::INFO, "[DEBUG] Task_New typeStr: '" + typeStr + "'");
            }
            cur = cur->next_;
            argCount++;
        }
    } else {
        // Direct call (less common in modern IGI QSC but possible)
        typeStr = funcName;
    }

    bool isBuilding = (typeStr == "Building");
    bool isRigid = (typeStr == "EditRigidObj");
    bool isSoldier = (typeStr == "HumanSoldier" || typeStr == "HumanSoldierFemale" || typeStr == "HumanPlayer" || typeStr == "HumanSoldierRPG" || typeStr == "Cabinet");
    bool isDoor = (typeStr == "Door");
    bool isTerminal = (typeStr == "Terminal");
    bool isCamera = (typeStr == "SCamera");
    bool isHeli = (typeStr == "Heli");
    bool isCar = (typeStr == "Car");
    bool isSpline = (typeStr == "SplineObjWaypoint");
    bool isSwitch = (typeStr == "Switch");
    bool isSplineContainer = (typeStr == "SplineObj");
    bool isWire = (typeStr == "Wire");
    bool isAlarm = (typeStr == "AlarmControl");
    bool isSCameraCtrl = (typeStr == "SCameraControl");
    bool isExplode = (typeStr == "ExplodeObject");
    bool isAmbient = (typeStr == "AmbientArea");
    bool isFence = (typeStr == "Fence");
    bool isTrain = (typeStr == "Train");

    bool isDecl = (typeStr == "Task_DeclareParameters");
    bool isGrouping = (typeStr == "Container" || typeStr == "Static" || typeStr == "Game" || typeStr == "Level" || typeStr == "Flow" || typeStr == "Task" || typeStr == "Folder" ||
                       typeStr == "container" || typeStr == "static" || typeStr == "game" || typeStr == "level" || typeStr == "flow" || typeStr == "task" || typeStr == "folder" || typeStr == "dynamic" || typeStr == "Dynamic" ||
                       typeStr == "ConditionalContainer" || typeStr == "SequenceContainer" || typeStr == "Rooms");

    bool isPickup = (typeStr == "GunPickup" || typeStr == "AmmoPickup");
    bool isMissingGeneric = (typeStr == "AIStationaryGunHolder" || typeStr == "AlarmLight" || typeStr == "Elevator" || typeStr == "Generator" || typeStr == "GenericPickup" || typeStr == "GenericTBA" || typeStr == "Plane" || typeStr == "Radio" || typeStr == "RotatingObject" || typeStr == "Siren" || typeStr == "StationaryGun");

    int currentObjIdx = -1;

    // Extract exact raw string from QSC buffer using offsets
    std::string rawLine;
    if (qsc && func->start_offset_ >= 0 && func->end_offset_ >= func->start_offset_) {
        const char* scripts = qsc->GetScripts();
        if (scripts) {
            rawLine = std::string(scripts + func->start_offset_, func->end_offset_ - func->start_offset_);
        }
    }

    // Always create an object entry for the tree view
    LevelObject obj;
    obj.type = typeStr;
    obj.qscFuncName = funcName;
    obj.preserveTaskId = (funcName == "Task_New");
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
    obj.expanded = false; // Closed by default
    obj.qscLine = rawLine;

    int arg_idx = 0;
    const QSC::arg_s* cur_a = a;
    while (cur_a) {
        if (cur_a->type_ != QSC::arg_s::type_t::FUNC) {
            obj.argTokens.push_back(ArgTokenFromArg(cur_a));
            if (obj.preserveTaskId && arg_idx == 0) {
                obj.argTokens.back() = FormatQscIntegerToken(TaskIdFromArg(cur_a));
            }
        }

        if (obj.qscFuncName == "Task_New" && arg_idx == 2) {
            if (cur_a->type_ == QSC::arg_s::type_t::STR) {
                obj.name = cur_a->str_;
                obj.original_name = cur_a->str_;
                obj.has_original_name = true;
            }
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
        } else if (isPickup) {
            // GunPickup: Task_New(id, "GunPickup", name, x, y, z, rx, ry, rz, "WEAPON_ID_*")
            // AmmoPickup: Task_New(id, "AmmoPickup", name, x, y, z, rx, ry, rz, "AMMO_ID_*", count)
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 6: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.x = cur_a->dbl_; obj.original_rot.x = cur_a->dbl_; } break;
                case 7: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.y = cur_a->dbl_; obj.original_rot.y = cur_a->dbl_; } break;
                case 8: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = cur_a->dbl_; obj.original_rot.z = cur_a->dbl_; } break;
                case 9: {
                    // Arg 9 is the weapon/ammo enum string (e.g. "WEAPON_ID_UZI", "AMMO_ID_919")
                    // Resolve it to a model ID for rendering via IGIModels.json
                    if (cur_a->type_ == QSC::arg_s::type_t::STR) {
                        std::string enumId = Utils::Trim(cur_a->str_);
                        LoadModelNames();
                        auto it = modelIds_.find(enumId);
                        if (it != modelIds_.end() && !it->second.empty()) {
                            obj.modelId = it->second;
                        } else {
                            obj.modelId = enumId; // fallback: keep raw enum string
                        }
                    }
                    break;
                }
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
        } else if (isHeli || isCar) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 6: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.x = cur_a->dbl_; obj.original_rot.x = cur_a->dbl_; } break;
                case 7: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.y = cur_a->dbl_; obj.original_rot.y = cur_a->dbl_; } break;
                case 8: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = cur_a->dbl_; obj.original_rot.z = cur_a->dbl_; } break;
                case 13: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = Utils::Trim(cur_a->str_); break;
            }
        } else if (isTrain) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break; // Position is 1D (Real32) for Train
                case 5: obj.splineTaskId = TaskIdFromArg(cur_a); break;
                case 6: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = Utils::Trim(cur_a->str_); break;
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
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.x = cur_a->dbl_; obj.original_rot.x = cur_a->dbl_; } break; // alpha
                case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.y = cur_a->dbl_; obj.original_rot.y = cur_a->dbl_; } break; // beta
                case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = cur_a->dbl_; obj.original_rot.z = cur_a->dbl_; } break; // gamma
                case 6: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 7: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 8: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 9:
                    if (cur_a->type_ == QSC::arg_s::type_t::STR) {
                        obj.modelId = cur_a->str_;
                        if (obj.modelId == "waypoint" || obj.modelId.empty()) obj.modelId = "";
                    }
                    break;
                case 10: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.segmentModelId = cur_a->str_; break;
            }
        } else if (isSwitch) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 6: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.x = cur_a->dbl_; obj.original_rot.x = cur_a->dbl_; } break;
                case 7: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.y = cur_a->dbl_; obj.original_rot.y = cur_a->dbl_; } break;
                case 8: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = cur_a->dbl_; obj.original_rot.z = cur_a->dbl_; } break;
                case 11: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = Utils::Trim(cur_a->str_); break;
            }
        } else if (isAlarm || isSCameraCtrl || isExplode) {
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
        } else if (isCamera) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 6: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = cur_a->dbl_; obj.original_rot.z = cur_a->dbl_; } break;
                case 8: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.x = cur_a->dbl_; obj.original_rot.x = cur_a->dbl_; } break;
                case 9: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.y = cur_a->dbl_; obj.original_rot.y = cur_a->dbl_; } break;
                case 10: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = Utils::Trim(cur_a->str_); break;
                case 11: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.secondaryModelId = Utils::Trim(cur_a->str_); break;
                case 12: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.lensModelId = Utils::Trim(cur_a->str_); break;
            }
        } else if (isFence) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 6: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = cur_a->dbl_; obj.original_rot.z = cur_a->dbl_; } break;
                case 7: if (cur_a->type_ == QSC::arg_s::type_t::STR) obj.modelId = Utils::Trim(cur_a->str_); break;
                case 8: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.x = cur_a->dbl_; obj.original_rot.x = cur_a->dbl_; } break;
                case 9: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.y = cur_a->dbl_; obj.original_rot.y = cur_a->dbl_; } break;
            }
        } else if (isAmbient) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 6: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.x = cur_a->dbl_; obj.original_rot.x = cur_a->dbl_; } break;
                case 7: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.y = cur_a->dbl_; obj.original_rot.y = cur_a->dbl_; } break;
                case 8: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = cur_a->dbl_; obj.original_rot.z = cur_a->dbl_; } break;
            }
        } else if (isMissingGeneric) {
            switch (arg_idx) {
                case 0: obj.taskId = TaskIdFromArg(cur_a); break;
                case 2: if (cur_a->type_ == QSC::arg_s::type_t::STR) { obj.name = cur_a->str_; obj.original_name = cur_a->str_; obj.has_original_name = true; } break;
                case 3: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.x = cur_a->dbl_; obj.original_pos.x = cur_a->dbl_; } break;
                case 4: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.y = cur_a->dbl_; obj.original_pos.y = cur_a->dbl_; } break;
                case 5: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.pos.z = cur_a->dbl_; obj.original_pos.z = cur_a->dbl_; } break;
                case 6: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.x = cur_a->dbl_; obj.original_rot.x = cur_a->dbl_; } break;
                case 7: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.y = cur_a->dbl_; obj.original_rot.y = cur_a->dbl_; } break;
                case 8: if (cur_a->type_ == QSC::arg_s::type_t::DBL) { obj.rot.z = cur_a->dbl_; obj.original_rot.z = cur_a->dbl_; } break;
                default:
                    if (arg_idx >= 9 && obj.modelId.empty() && cur_a->type_ == QSC::arg_s::type_t::STR) {
                        std::string val = Utils::Trim(cur_a->str_);
                        if (val.length() == 8 && val[3] == '_' && val[6] == '_') {
                            obj.modelId = val;
                            obj.modelIdArgIdx = arg_idx;
                        }
                    }
                    break;
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
    // Assign raw line from parser to all objects for verbatim save of unmodified nodes
    if (!rawLine.empty()) {
        obj.qscLine = rawLine;
    }
    obj.isNested = (parentIdx != -1);
    
    objects_.push_back(obj);
    currentObjIdx = (int)objects_.size() - 1;
    if (parentIdx != -1) {
        objects_[parentIdx].childrenIndices.push_back(currentObjIdx);
    }
    
    Logger::Get().Log(LogLevel::DEBUG, "[LevelObjects]   -> " + typeStr + ": " + obj.modelId + 
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


    char jsonPath[1024];
    std::string exeDir = Utils::GetExeDirectory();
    Str_SPrintf(jsonPath, 1024, "%s\\content\\tools\\IGIModels.json", exeDir.c_str());
    bool usingBackup = false;

    if (!std::filesystem::exists(jsonPath)) {
        std::string backupPath = Utils::GetExeDirectory() + "\\content\\tools\\IGIModels.json";
        if (std::filesystem::exists(backupPath)) {
            Str_Copy(jsonPath, 1024, backupPath.c_str());
            usingBackup = true;
            Logger::Get().Log(LogLevel::WARNING, "[LevelObjects] QEditor not found at APPDATA or configured path. Using backup IGIModels.json from executable directory: " + backupPath);
        } else {
            Logger::Get().Log(LogLevel::ERR, "[LevelObjects] QEditor missing and backup IGIModels.json not found in executable directory!");
        }
    }

    char* buf = nullptr;
    if (File_LoadText(jsonPath, buf)) {
        if (!usingBackup) {
            Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Loading model names from: " + std::string(jsonPath));
        }
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

std::string LevelObjects::GetModelName(const std::string& modelId) const {
    if (modelNames_.empty()) const_cast<LevelObjects*>(this)->LoadModelNames();
    auto it = modelNames_.find(modelId);
    if (it != modelNames_.end()) return it->second;
    return "";
}

std::string LevelObjects::GetModelId(const std::string& modelName) const {
    if (modelNames_.empty()) const_cast<LevelObjects*>(this)->LoadModelNames();
    auto it = modelIds_.find(modelName);
    if (it != modelIds_.end()) return it->second;
    return "";
}

static bool IsSubtreeModified(const std::vector<LevelObject>& objects, int idx) {
    if (idx < 0 || idx >= (int)objects.size()) return false;
    const auto& node = objects[idx];
    if (node.modified || node.deleted) return true;
    for (int childIdx : node.childrenIndices) {
        if (IsSubtreeModified(objects, childIdx)) return true;
    }
    return false;
}

void LevelObjects::SaveToQSC(const std::string& qscPath) {
    std::string lowerPath = qscPath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
    if (lowerPath.find("qfiles") != std::string::npos) {
        Logger::Get().Log(LogLevel::ERR, "[LevelObjects::SaveToQSC] CRITICAL ERROR: Attempted to write to READ-ONLY QFiles path: " + qscPath);
        return;
    }

    // Clear stale cached lines and update coordinates for modified objects
    for (auto& obj : objects_) {
        if (!obj.deleted && obj.modified) {
            obj.qscLine.clear();
            UpdateCoordinatesInLine(obj);
        }
    }

    std::ofstream outFile(qscPath);
    if (!outFile) {
        Logger::Get().Log(LogLevel::ERR, "[LevelObjects::SaveToQSC] Failed to open QSC file for writing: " + qscPath);
        return;
    }

    std::stringstream ss;
    bool first = true;
    for (int i = 0; i < (int)objects_.size(); ++i) {
        const auto& obj = objects_[i];
        if (obj.parentIndex != -1 || obj.deleted) continue;

        std::string serialized = SerializeObjectRecursive(objects_, i);
        if (serialized.empty()) {
            Logger::Get().Log(LogLevel::WARNING,
                "[LevelObjects::SaveToQSC] Root object idx=" + std::to_string(i) +
                " name='" + objects_[i].name + "' type='" + objects_[i].type +
                "' serialized to empty — will be absent from saved QSC");
            continue;
        }

        if (!first) ss << "\n";
        ss << serialized << ";";
        first = false;
    }
    outFile << Utils::Trim(ss.str());
    outFile.close();
    Utils::TrimFileInPlace(qscPath);

    for (auto& obj : objects_) {
        obj.modified = false;
        obj.original_pos = glm::dvec3(obj.pos.x, obj.pos.y, obj.pos.z - obj.snap_z_offset);
        obj.original_rot = obj.rot;
        if (obj.childrenIndices.empty()) {
            obj.qscLine = GenerateTaskLine(obj);
        }
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
        obj.argTokens[0] = obj.taskId;
        obj.type = unquote(obj.argTokens[1]);
        obj.name = unquote(obj.argTokens[2]);
        obj.preserveTaskId = true;
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

        if (obj.type == "HumanSoldier" || obj.type == "HumanSoldierFemale" || obj.type == "HumanPlayer" || obj.type == "HumanSoldierRPG" || obj.type == "Cabinet") {
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
            readDouble(6, obj.rot.z);
            readDouble(8, obj.rot.x);
            readDouble(9, obj.rot.y);
            if (obj.argTokens.size() > 10) obj.modelId = unquote(obj.argTokens[10]);
        } else if (obj.type == "Fence") {
            readDouble(3, obj.pos.x);
            readDouble(4, obj.pos.y);
            readDouble(5, obj.pos.z);
            readDouble(6, obj.rot.z);
            if (obj.argTokens.size() > 7) obj.modelId = unquote(obj.argTokens[7]);
        } else if (obj.type == "AnimTask" || obj.type == "CutScene") {
            readDouble(3, obj.pos.x);
            readDouble(4, obj.pos.y);
            readDouble(5, obj.pos.z);
        } else if (obj.type == "Heli" || obj.type == "Car") {
            readDouble(3, obj.pos.x);
            readDouble(4, obj.pos.y);
            readDouble(5, obj.pos.z);
            readDouble(8, obj.rot.z);
            if (obj.argTokens.size() > 13) obj.modelId = unquote(obj.argTokens[13]);
        } else if (obj.type == "SplineObjWaypoint") {
            readDouble(3, obj.rot.x);
            readDouble(4, obj.rot.y);
            readDouble(5, obj.rot.z);
            readDouble(6, obj.pos.x);
            readDouble(7, obj.pos.y);
            readDouble(8, obj.pos.z);
            if (obj.argTokens.size() > 9) { obj.modelId = unquote(obj.argTokens[9]); if (obj.modelId == "waypoint") obj.modelId = ""; }
            if (obj.argTokens.size() > 10) obj.segmentModelId = unquote(obj.argTokens[10]);
        } else if (obj.type == "Switch") {
            readDouble(3, obj.pos.x);
            readDouble(4, obj.pos.y);
            readDouble(5, obj.pos.z);
            readDouble(6, obj.rot.x);
            readDouble(7, obj.rot.y);
            readDouble(8, obj.rot.z);
            if (obj.argTokens.size() > 17) obj.modelId = unquote(obj.argTokens[17]);
        } else if (obj.type == "AlarmControl" || obj.type == "SCameraControl" || obj.type == "ExplodeObject") {
            readDouble(3, obj.pos.x);
            readDouble(4, obj.pos.y);
            readDouble(5, obj.pos.z);
            readDouble(6, obj.rot.x);
            readDouble(7, obj.rot.y);
            readDouble(8, obj.rot.z);
            if (obj.argTokens.size() > 15) obj.modelId = unquote(obj.argTokens[15]);
        } else if (obj.type == "AmbientArea") {
            readDouble(3, obj.pos.x);
            readDouble(4, obj.pos.y);
            readDouble(5, obj.pos.z);
            readDouble(6, obj.rot.x);
            readDouble(7, obj.rot.y);
            readDouble(8, obj.rot.z);
        } else if (obj.type == "Train") {
            // Position (arg[3]), RailroadQTaskID (arg[5]), Model (arg[6])
            readDouble(3, obj.pos.x); // 1D position along spline
            if (obj.argTokens.size() > 5) obj.splineTaskId = Utils::Trim(unquote(obj.argTokens[5]));
            if (obj.argTokens.size() > 6) obj.modelId = unquote(obj.argTokens[6]);
        } else if (obj.type == "TextureModifier" || obj.type == "TerrainLightMap" || obj.type == "HeightMap" || obj.type == "DiscardTerrain" || obj.type == "GlobalLight" || obj.type == "GlobalLightKeyframe" || obj.type == "Dirlight" || obj.type == "DirlightKeyframe" || obj.type == "FlatSkyLayer" || obj.type == "FlatSky" || obj.type == "MipMapControl" || obj.type == "LODSettings" || obj.type == "SoundSource") {
            // Do not parse coordinates for environmental/terrain types as they do not follow the generic pos/rot argument layout

        } else if (obj.type == "GunPickup" || obj.type == "AmmoPickup") {
            // GunPickup/AmmoPickup: pos@3-5, rot@6-8, weapon/ammo enum ID@9, [ammo count@10]
            readDouble(3, obj.pos.x);
            readDouble(4, obj.pos.y);
            readDouble(5, obj.pos.z);
            readDouble(6, obj.rot.x);
            readDouble(7, obj.rot.y);
            readDouble(8, obj.rot.z);
            if (obj.argTokens.size() > 9) {
                std::string enumId = unquote(obj.argTokens[9]);
                // Resolve WEAPON_ID_* / AMMO_ID_* to a render model ID via IGIModels.json
                LoadModelNames();
                auto it = modelIds_.find(enumId);
                if (it != modelIds_.end() && !it->second.empty()) {
                    obj.modelId = it->second;
                } else {
                    obj.modelId = enumId; // fallback: keep raw enum string
                }
                // Keep argTokens[9] unchanged so the QSC file preserves the original enum string
            }
        } else if (obj.type == "AIStationaryGunHolder" || obj.type == "AlarmLight" || obj.type == "Elevator" || obj.type == "Generator" || obj.type == "GenericPickup" || obj.type == "GenericTBA" || obj.type == "Plane" || obj.type == "Radio" || obj.type == "RotatingObject" || obj.type == "Siren" || obj.type == "StationaryGun") {
            readDouble(3, obj.pos.x);
            readDouble(4, obj.pos.y);
            readDouble(5, obj.pos.z);
            readDouble(6, obj.rot.x);
            readDouble(7, obj.rot.y);
            readDouble(8, obj.rot.z);
            for (size_t i = 9; i < obj.argTokens.size(); ++i) {
                std::string val = unquote(obj.argTokens[i]);
                if (val.length() == 8 && val[3] == '_' && val[6] == '_') {
                    obj.modelId = val;
                    obj.modelIdArgIdx = (int)i;
                    break;
                }
            }
        } else if (obj.argTokens.size() > 8) {
            readDouble(3, obj.pos.x);
            readDouble(4, obj.pos.y);
            readDouble(5, obj.pos.z);
            readDouble(6, obj.rot.x);
            readDouble(7, obj.rot.y);
            readDouble(8, obj.rot.z);
            if (obj.argTokens.size() > 9) {
                obj.modelId = unquote(obj.argTokens[9]);
            }
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
        if (obj.type == "HumanSoldier" || obj.type == "HumanSoldierFemale" || obj.type == "HumanPlayer" || obj.type == "HumanSoldierRPG" || obj.type == "Cabinet") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setToken(6, FormatQscDouble(obj.rot.z));
            if (!obj.modelId.empty() || obj.argTokens.size() > 7) setStringToken(7, obj.modelId);
        } else if (obj.type == "Door") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setToken(9, FormatQscDouble(obj.rot.x));
            setToken(10, FormatQscDouble(obj.rot.y));
            setToken(11, FormatQscDouble(obj.rot.z));
            if (!obj.modelId.empty() || obj.argTokens.size() > 12) setStringToken(12, obj.modelId);
        } else if (obj.type == "SCamera") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setToken(6, FormatQscDouble(obj.rot.z));
            setToken(8, FormatQscDouble(obj.rot.x));
            setToken(9, FormatQscDouble(obj.rot.y));
            if (!obj.modelId.empty() || obj.argTokens.size() > 10) setStringToken(10, obj.modelId);
        } else if (obj.type == "Fence") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setToken(6, FormatQscDouble(obj.rot.z));
            if (!obj.modelId.empty() || obj.argTokens.size() > 7) setStringToken(7, obj.modelId);
        } else if (obj.type == "AnimTask" || obj.type == "CutScene") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
        } else if (obj.type == "Heli" || obj.type == "Car") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setToken(8, FormatQscDouble(obj.rot.z));
            if (!obj.modelId.empty() || obj.argTokens.size() > 13) setStringToken(13, obj.modelId);
        } else if (obj.type == "Train") {
            setToken(3, FormatQscDouble(obj.pos.x)); // 1D Position
            if (!obj.splineTaskId.empty() || obj.argTokens.size() > 5) setToken(5, obj.splineTaskId.empty() ? "0" : obj.splineTaskId);
            if (!obj.modelId.empty() || obj.argTokens.size() > 6) setStringToken(6, obj.modelId);
        } else if (obj.type == "SplineObjWaypoint") {
            setToken(3, FormatQscDouble(obj.rot.x));
            setToken(4, FormatQscDouble(obj.rot.y));
            setToken(5, FormatQscDouble(obj.rot.z));
            setToken(6, FormatQscDouble(obj.pos.x));
            setToken(7, FormatQscDouble(obj.pos.y));
            setToken(8, FormatQscDouble(saveZ));
            if (!obj.modelId.empty() || obj.argTokens.size() > 9) setStringToken(9, obj.modelId.empty() ? "waypoint" : obj.modelId);
            if (!obj.segmentModelId.empty() || obj.argTokens.size() > 10) setStringToken(10, obj.segmentModelId);
        } else if (obj.type == "Switch") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setToken(6, FormatQscDouble(obj.rot.x));
            setToken(7, FormatQscDouble(obj.rot.y));
            setToken(8, FormatQscDouble(obj.rot.z));
            if (!obj.modelId.empty() || obj.argTokens.size() > 11) setStringToken(11, obj.modelId);
        } else if (obj.type == "AlarmControl" || obj.type == "SCameraControl" || obj.type == "ExplodeObject") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setToken(6, FormatQscDouble(obj.rot.x));
            setToken(7, FormatQscDouble(obj.rot.y));
            setToken(8, FormatQscDouble(obj.rot.z));
            if (!obj.modelId.empty() || obj.argTokens.size() > 15) setStringToken(15, obj.modelId);
        } else if (obj.type == "AmbientArea") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setToken(6, FormatQscDouble(obj.rot.x));
            setToken(7, FormatQscDouble(obj.rot.y));
            setToken(8, FormatQscDouble(obj.rot.z));
        } else if (obj.type == "Wire") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            if (!obj.modelId.empty() || obj.argTokens.size() > 9) setStringToken(9, obj.modelId);
        } else if (obj.type == "Building" || obj.type == "EditRigidObj" || obj.type == "Terminal") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setToken(6, FormatQscDouble(obj.rot.x));
            setToken(7, FormatQscDouble(obj.rot.y));
            setToken(8, FormatQscDouble(obj.rot.z));
            if (!obj.modelId.empty() || obj.argTokens.size() > 9) setStringToken(9, obj.modelId);
        } else if (obj.type == "TextureModifier" || obj.type == "TerrainLightMap" || obj.type == "HeightMap" || obj.type == "DiscardTerrain" || obj.type == "GlobalLight" || obj.type == "GlobalLightKeyframe" || obj.type == "Dirlight" || obj.type == "DirlightKeyframe" || obj.type == "FlatSkyLayer" || obj.type == "FlatSky" || obj.type == "MipMapControl" || obj.type == "LODSettings" || obj.type == "SoundSource") {
            // Do not update coordinates for environmental/terrain types as they do not follow the generic pos/rot argument layout

        } else if (obj.type == "GunPickup" || obj.type == "AmmoPickup") {
            // Update position/rotation only; preserve original WEAPON_ID/AMMO_ID string at arg 9
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setToken(6, FormatQscDouble(obj.rot.x));
            setToken(7, FormatQscDouble(obj.rot.y));
            setToken(8, FormatQscDouble(obj.rot.z));
            // argTokens[9] already holds the original WEAPON_ID_*/AMMO_ID_* string - do not overwrite
        } else if (obj.type == "AIStationaryGunHolder" || obj.type == "AlarmLight" || obj.type == "Elevator" || obj.type == "Generator" || obj.type == "GenericPickup" || obj.type == "GenericTBA" || obj.type == "Plane" || obj.type == "Radio" || obj.type == "RotatingObject" || obj.type == "Siren" || obj.type == "StationaryGun") {
            setToken(3, FormatQscDouble(obj.pos.x));
            setToken(4, FormatQscDouble(obj.pos.y));
            setToken(5, FormatQscDouble(saveZ));
            setToken(6, FormatQscDouble(obj.rot.x));
            setToken(7, FormatQscDouble(obj.rot.y));
            setToken(8, FormatQscDouble(obj.rot.z));
            if (!obj.modelId.empty()) {
                if (obj.modelIdArgIdx != -1) {
                    setStringToken(obj.modelIdArgIdx, obj.modelId);
                } else if (obj.argTokens.size() > 9) {
                    setStringToken(9, obj.modelId);
                }
            }
        }
    }

    if (obj.childrenIndices.empty()) {
        obj.qscLine = GenerateTaskLine(obj);
    } else {
        obj.qscLine.clear(); // force re-serialization of this container subtree
    }
}

std::string LevelObjects::GenerateTaskLine(const LevelObject& obj) {
    std::stringstream ss;
    ss << obj.qscFuncName << "(";
    for (size_t i = 0; i < obj.argTokens.size(); ++i) {
        if (i) ss << ", ";
        ss << Utils::Trim(obj.argTokens[i]);
    }
    ss << ")";
    return ss.str();
}

std::string LevelObjects::SerializeObjectRecursive(const std::vector<LevelObject>& objects, int idx) {
    if (idx < 0 || idx >= (int)objects.size()) return "";

    std::function<std::string(int)> serialize = [&](int objectIdx) -> std::string {
        const LevelObject& node = objects[objectIdx];

        // Collect live (non-deleted) children
        std::vector<int> liveChildren;
        for (int childIdx : node.childrenIndices) {
            if (childIdx < 0 || childIdx >= (int)objects.size()) continue;
            if (objects[childIdx].deleted) continue;
            liveChildren.push_back(childIdx);
        }

        // Helper to check if node and all descendants are unmodified and undeleted
        auto IsTreeUnmodified = [&](auto& self, int idx) -> bool {
            const auto& obj = objects[idx];
            if (obj.modified || obj.deleted || obj.qscLine.empty()) return false;
            // If the number of live children differs from original child count, something was deleted/added
            int liveCount = 0;
            for (int childIdx : obj.childrenIndices) {
                if (childIdx < 0 || childIdx >= (int)objects.size()) continue;
                if (objects[childIdx].deleted) return false;
                liveCount++;
                if (!self(self, childIdx)) return false;
            }
            if (liveCount != obj.childrenIndices.size()) return false;
            return true;
        };

        // For leaf nodes only: if unmodified, return the EXACT original qscLine to preserve float precision.
        // Container nodes must always recurse into children — qscLine only holds the opening line, not the full block.
        if (liveChildren.empty() && IsTreeUnmodified(IsTreeUnmodified, objectIdx)) {
            std::string raw = Utils::Trim(node.qscLine);
            while (!raw.empty() && (raw.back() == ';' || raw.back() == ',' || raw.back() == '\r' || raw.back() == '\n' || raw.back() == ' ' || raw.back() == '\t')) {
                raw.pop_back();
            }
            return raw;
        }

        // Skip implicitly generated empty Static nodes that aren't in the original text
        if (node.qscFuncName == "Task_New" && node.type == "Static" && node.argTokens.size() <= 3) {
            if (!node.qscLine.empty() && node.qscLine.find("Static") == std::string::npos) {
                return "";
            }
        }

        std::stringstream ss;
        ss << node.qscFuncName << "(";

        bool first = true;
        for (size_t i = 0; i < node.argTokens.size(); ++i) {
            if (!first) ss << ", ";
            ss << Utils::Trim(node.argTokens[i]);
            first = false;
        }

        if (!liveChildren.empty()) {
            std::vector<std::string> childStrs;
            for (size_t i = 0; i < liveChildren.size(); ++i) {
                std::string s = serialize(liveChildren[i]);
                if (!s.empty()) childStrs.push_back(s);
            }
            if (!childStrs.empty()) {
                ss << ", \n";
                for (size_t i = 0; i < childStrs.size(); ++i) {
                    ss << childStrs[i];
                    if (i < childStrs.size() - 1) {
                        ss << ", \n";
                    }
                }
            }
            ss << ")";
        } else {
            ss << ")";
        }

        return ss.str();
    };

    return serialize(idx);
}
