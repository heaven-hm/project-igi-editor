#include "level_objects_internal.h"

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
                    // DISPLAY ONLY: show the friendly name in the tree/picker for unnamed tasks.
                    // Must NOT touch argTokens[2] or qscLine — those are the serialization
                    // source of truth. Mutating them persists the synthetic name into the saved
                    // QSC/QVM, which corrupts the level (the game crashes on load). original_name
                    // / has_original_name stay as parsed (the real name is empty) so a save still
                    // writes the original empty note unless the user explicitly renames the task.
                    obj.name = friendlyName;

                    Logger::Get().Log(LogLevel::INFO, "[LevelObjects] Display name for empty-note Task " + obj.taskId + " resolved to model friendly name: " + friendlyName);
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
            if (!obj.modelId.empty()) {
                bool isEnum = (obj.modelId.rfind("WEAPON_ID_", 0) == 0 ||
                               obj.modelId.rfind("AMMO_ID_", 0) == 0);
                std::string resolved = ResolvePickupModelId(obj.modelId);
                if (resolved != obj.modelId) {
                    Logger::Get().Log(LogLevel::INFO,
                        "[LevelObjects] Resolved pickup enum: " + obj.modelId +
                        " -> " + resolved + " (task " + obj.taskId + ")");
                    obj.modelId = resolved;
                } else if (isEnum) {
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
                        obj.modelId = ResolvePickupModelId(enumId);
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
    Str_SPrintf(jsonPath, 1024, "%s\\editor\\tools\\IGIModels.json", exeDir.c_str());

    if (!std::filesystem::exists(jsonPath)) {
        Logger::Get().Log(LogLevel::ERR, "[LevelObjects] IGIModels.json not found in executable directory: " + std::string(jsonPath));
    }

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

std::string LevelObjects::ResolvePickupModelId(const std::string& enumId) {
    if (enumId.rfind("WEAPON_ID_", 0) != 0 && enumId.rfind("AMMO_ID_", 0) != 0)
        return enumId;
    LoadModelNames();
    auto it = modelIds_.find(enumId);
    if (it != modelIds_.end() && !it->second.empty())
        return it->second;
    return enumId; // fallback: keep raw enum string
}

