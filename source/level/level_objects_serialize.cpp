/******************************************************************************
 * @file    level_objects_serialize.cpp
 * @brief   LevelObjects QSC serialization: SaveToQSC, ParseTaskLine,
 *          UpdateCoordinatesInLine, GenerateTaskLine, SaveSubtreeToQSC,
 *          SerializeObjectRecursive. Split from level_objects.cpp.
 *****************************************************************************/
#include "level_objects_internal.h"

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
        if (obj.parentIndex != -1 || obj.deleted || obj.isAttaProxy) continue;

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
                obj.modelId = ResolvePickupModelId(enumId);
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
            if (!obj.modelId.empty() || obj.argTokens.size() > 17) setStringToken(17, obj.modelId);
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

void LevelObjects::SaveSubtreeToQSC(int idx, const std::string& qscPath) {
    if (idx < 0 || idx >= (int)objects_.size()) return;
    std::string lowerPath = qscPath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);
    if (lowerPath.find("qfiles") != std::string::npos) {
        Logger::Get().Log(LogLevel::ERR, "[LevelObjects::SaveSubtreeToQSC] Refusing to write READ-ONLY QFiles path: " + qscPath);
        return;
    }
    // Refresh modified lines in the subtree so the serialization reflects edits.
    std::function<void(int)> refresh = [&](int i) {
        if (i < 0 || i >= (int)objects_.size() || objects_[i].deleted) return;
        if (objects_[i].modified) { objects_[i].qscLine.clear(); UpdateCoordinatesInLine(objects_[i]); }
        for (int c : objects_[i].childrenIndices) refresh(c);
    };
    refresh(idx);

    std::string serialized = SerializeObjectRecursive(objects_, idx);
    std::ofstream out(qscPath);
    if (!out.is_open()) {
        Logger::Get().Log(LogLevel::ERR, "[LevelObjects::SaveSubtreeToQSC] Failed to open for writing: " + qscPath);
        return;
    }
    out << Utils::Trim(serialized) << ";\n";
    out.close();
    Logger::Get().Log(LogLevel::INFO, "[LevelObjects::SaveSubtreeToQSC] Saved subtree idx=" + std::to_string(idx) +
                      " type='" + objects_[idx].type + "' to: " + qscPath);
}

std::string LevelObjects::SerializeObjectRecursive(const std::vector<LevelObject>& objects, int idx) {
    if (idx < 0 || idx >= (int)objects.size()) return "";

    std::function<std::string(int)> serialize = [&](int objectIdx) -> std::string {
        const LevelObject& node = objects[objectIdx];

        // Collect live (non-deleted, non-proxy) children.
        // isAttaProxy objects are editor-only virtual nodes — they must never
        // appear in the QSC because they have no qscFuncName/argTokens.
        std::vector<int> liveChildren;
        for (int childIdx : node.childrenIndices) {
            if (childIdx < 0 || childIdx >= (int)objects.size()) continue;
            if (objects[childIdx].deleted) continue;
            if (objects[childIdx].isAttaProxy) continue;
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

