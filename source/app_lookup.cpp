/******************************************************************************
 * @file    app_lookup.cpp
 * @brief   App: model name/id lookup, copy, search, and validation helpers
 *          Split from app.cpp; shares app_internal.h.
 *****************************************************************************/
#include "app_internal.h"

static bool containsIgnoreCase(const std::string& str, const std::string& substr) {
    if (substr.empty()) return true;
    auto it = std::search(
        str.begin(), str.end(),
        substr.begin(), substr.end(),
        [](char ch1, char ch2) { return std::tolower(ch1) == std::tolower(ch2); }
    );
    return it != str.end();
}
static int GetLookupObjectIndex(int hoverIdx, int selectedIdx) {
	if (hoverIdx >= 0) return hoverIdx;
	if (selectedIdx >= 0) return selectedIdx;
	return -1;
}

static void SetLookupStatus(std::string& status_message, const std::string& msg) {
	status_message = msg;
	Logger::Get().Log(LogLevel::INFO, msg);
	printf("%s\n", msg.c_str());
}

void App::LookupSelectedModelName() {
	auto& objects = level_.GetLevelObjects().GetObjects();
	int idx = GetLookupObjectIndex(hover_object_index_, selected_object_index_);
	if (idx < 0 || idx >= (int)objects.size()) {
		SetLookupStatus(status_message_, "[App] Model lookup: no hovered/selected object");
		return;
	}

	const auto& obj = objects[idx];
	std::string name = level_.GetLevelObjects().GetModelName(obj.modelId);
	if (name.empty()) {
		SetLookupStatus(status_message_, "[App] Model lookup: no friendly name for model ID " + obj.modelId);
		return;
	}

	SetLookupStatus(status_message_, "[App] Model lookup: " + obj.modelId + " -> " + name);
}

void App::LookupSelectedModelId() {
	auto& objects = level_.GetLevelObjects().GetObjects();
	int idx = GetLookupObjectIndex(hover_object_index_, selected_object_index_);
	if (idx < 0 || idx >= (int)objects.size()) {
		SetLookupStatus(status_message_, "[App] Model lookup: no hovered/selected object");
		return;
	}

	const auto& obj = objects[idx];
	std::string name = level_.GetLevelObjects().GetModelName(obj.modelId);
	if (name.empty()) {
		name = obj.name;
	}
	if (name.empty()) {
		SetLookupStatus(status_message_, "[App] Model lookup: object has no readable model name");
		return;
	}

	std::string modelId = level_.GetLevelObjects().GetModelId(name);
	if (modelId.empty()) {
		SetLookupStatus(status_message_, "[App] Model lookup: no model id for name \"" + name + "\"");
		return;
	}

	SetLookupStatus(status_message_, "[App] Model lookup: " + name + " -> " + modelId);
}

void App::CopySelectedModelName() {
	auto& objects = level_.GetLevelObjects().GetObjects();
	int idx = GetLookupObjectIndex(hover_object_index_, selected_object_index_);
	if (idx < 0 || idx >= (int)objects.size()) {
		SetLookupStatus(status_message_, "[App] Model copy: no hovered/selected object");
		return;
	}

	const auto& obj = objects[idx];
	std::string name = level_.GetLevelObjects().GetModelName(obj.modelId);
	if (name.empty()) {
		SetLookupStatus(status_message_, "[App] Model copy: no friendly name for model ID " + obj.modelId);
		return;
	}

	Utils::SetClipboardText(name);
	SetLookupStatus(status_message_, "[App] Copied model name: " + name);
}

void App::CopySelectedModelId() {
	auto& objects = level_.GetLevelObjects().GetObjects();
	int idx = GetLookupObjectIndex(hover_object_index_, selected_object_index_);
	if (idx < 0 || idx >= (int)objects.size()) {
		SetLookupStatus(status_message_, "[App] Model copy: no hovered/selected object");
		return;
	}

	const auto& obj = objects[idx];
	std::string name = level_.GetLevelObjects().GetModelName(obj.modelId);
	if (name.empty()) {
		name = obj.name;
	}
	if (name.empty()) {
		SetLookupStatus(status_message_, "[App] Model copy: object has no readable model name");
		return;
	}

	std::string modelId = level_.GetLevelObjects().GetModelId(name);
	if (modelId.empty()) {
		SetLookupStatus(status_message_, "[App] Model copy: no model id for name \"" + name + "\"");
		return;
	}

	Utils::SetClipboardText(modelId);
	SetLookupStatus(status_message_, "[App] Copied model id: " + modelId);
}

void App::LookupHoveredModelName() { LookupSelectedModelName(); }
void App::LookupHoveredModelId() { LookupSelectedModelId(); }

struct ModelEntry {
	std::string modelName;
	std::string modelId;
};

static std::vector<ModelEntry> LoadAllModelsFromJson() {
	std::vector<ModelEntry> entries;
	std::string jsonPath = Utils::GetExeDirectory() + "\\editor\\tools\\IGIModels.json";

	if (!std::filesystem::exists(jsonPath)) {
		Logger::Get().Log(LogLevel::ERR, "[App] IGIModels.json not found in executable directory: " + jsonPath);
	} else {
		Logger::Get().Log(LogLevel::INFO, "[App] Loading model database from: " + jsonPath);
	}
	
	std::ifstream file(jsonPath, std::ios::binary);
	
	if (!file) {
		Logger::Get().Log(LogLevel::WARNING, "[App] Could not open database file: " + jsonPath);
		return entries;
	}
	
	std::stringstream ss;
	ss << file.rdbuf();
	std::string content = ss.str();
	
	size_t pos = 0;
	while ((pos = content.find("{", pos)) != std::string::npos) {
		size_t end = content.find("}", pos);
		if (end == std::string::npos) break;
		
		std::string entry = content.substr(pos, end - pos + 1);
		pos = end + 1;
		
		auto extractValue = [](const std::string& str, const std::string& key) -> std::string {
			size_t kpos = str.find("\"" + key + "\"");
			if (kpos == std::string::npos) return "";
			size_t colon = str.find(":", kpos);
			if (colon == std::string::npos) return "";
			size_t qStart = str.find("\"", colon);
			if (qStart == std::string::npos) return "";
			size_t qEnd = str.find("\"", qStart + 1);
			if (qEnd == std::string::npos) return "";
			return str.substr(qStart + 1, qEnd - qStart - 1);
		};
		
		ModelEntry item;
		item.modelName = extractValue(entry, "ModelName");
		item.modelId = extractValue(entry, "ModelId");
		
		if (!item.modelId.empty() || !item.modelName.empty()) {
			entries.push_back(item);
		}
	}
	
	return entries;
}

void App::SearchModelById(std::optional<std::string> query) {
	std::string searchId;
	if (query.has_value()) {
		searchId = query.value();
	} else {
		auto prompt = Utils::PromptForText("Search Model by ID", "Enter Model ID to search in IGIModels.json (e.g. 419_01_1):", "");
		if (!prompt.has_value()) return;
		searchId = prompt.value();
	}

	searchId = Utils::Trim(searchId);
	if (searchId.empty()) return;

	auto entries = LoadAllModelsFromJson();
	std::vector<ModelEntry> matches;
	
	for (const auto& entry : entries) {
		if (containsIgnoreCase(entry.modelId, searchId)) {
			matches.push_back(entry);
		}
	}
	
	std::string resultMessage;
	if (matches.empty()) {
		resultMessage = "No matching models found in IGIModels.json for ID: " + searchId;
	} else {
		resultMessage = "Found " + std::to_string(matches.size()) + " matches in IGIModels.json:\n\n";
		int count = 0;
		for (const auto& match : matches) {
			if (count >= 25) {
				resultMessage += "... and " + std::to_string(matches.size() - count) + " more matches.";
				break;
			}
			resultMessage += "- ID: " + match.modelId + "  ->  Name: " + match.modelName + "\n";
			count++;
		}
	}
	
	MessageBoxA(NULL, resultMessage.c_str(), "IGIModels.json Search Results", MB_OK | MB_ICONINFORMATION);
}

void App::SearchModelByName(std::optional<std::string> query) {
	std::string searchName;
	if (query.has_value()) {
		searchName = query.value();
	} else {
		auto prompt = Utils::PromptForText("Search Model by Name", "Enter Model Name to search in IGIModels.json (e.g. Soldier):", "");
		if (!prompt.has_value()) return;
		searchName = prompt.value();
	}

	searchName = Utils::Trim(searchName);
	if (searchName.empty()) return;

	auto entries = LoadAllModelsFromJson();
	std::vector<ModelEntry> matches;
	
	for (const auto& entry : entries) {
		if (containsIgnoreCase(entry.modelName, searchName)) {
			matches.push_back(entry);
		}
	}
	
	std::string resultMessage;
	if (matches.empty()) {
		resultMessage = "No matching models found in IGIModels.json for Name: " + searchName;
	} else {
		resultMessage = "Found " + std::to_string(matches.size()) + " matches in IGIModels.json:\n\n";
		int count = 0;
		for (const auto& match : matches) {
			if (count >= 25) {
				resultMessage += "... and " + std::to_string(matches.size() - count) + " more matches.";
				break;
			}
			resultMessage += "- Name: " + match.modelName + "  ->  ID: " + match.modelId + "\n";
			count++;
		}
	}
	
	MessageBoxA(NULL, resultMessage.c_str(), "IGIModels.json Search Results", MB_OK | MB_ICONINFORMATION);
}

std::vector<int> App::GetVisibleTreeNodes() {
    std::vector<int> visibleIndices;
    if (level_.GetLevelObjects().GetObjects().empty()) return visibleIndices;
    
    auto& objects = level_.GetLevelObjects().GetObjects();
    
    std::function<void(int)> traverse = [&](int idx) {
        if (idx < 0 || idx >= (int)objects.size()) return;
        const auto& obj = objects[idx];
        if (obj.deleted) return;
        
        visibleIndices.push_back(idx);
        
        if (obj.expanded) {
            for (int childIdx : obj.childrenIndices) {
                traverse(childIdx);
            }
        }
    };
    
    std::vector<int> root_decls;
    std::vector<int> root_others;
    for (int i = 0; i < (int)objects.size(); ++i) {
        if (objects[i].parentIndex == -1 && !objects[i].deleted) {
            if (objects[i].type == "Task_DeclareParameters") root_decls.push_back(i);
            else root_others.push_back(i);
        }
    }
    
    if (!root_decls.empty()) {
        visibleIndices.push_back(-2); // Virtual "Mission Declarations" folder
        if (tree_decl_expanded_) {
            for (int idx : root_decls) {
                traverse(idx);
            }
        }
    }
    
    for (int idx : root_others) {
        traverse(idx);
    }
    
    return visibleIndices;
}

bool App::IsComputer(const LevelObject& obj) {
	std::string name = obj.name;
	std::string type = obj.type;
	std::string modelId = obj.modelId;
	std::string qscFuncName = obj.qscFuncName;
	std::string friendlyName = level_.GetLevelObjects().GetModelName(obj.modelId);

	auto to_lower = [](std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
		return s;
	};

	name = to_lower(name);
	type = to_lower(type);
	modelId = to_lower(modelId);
	qscFuncName = to_lower(qscFuncName);
	friendlyName = to_lower(friendlyName);

	return (name.find("computer") != std::string::npos ||
			type.find("computer") != std::string::npos ||
			modelId.find("computer") != std::string::npos ||
			qscFuncName.find("computer") != std::string::npos ||
			friendlyName.find("computer") != std::string::npos);
}

bool App::IsWaterTower(const LevelObject& obj) {
	std::string name = obj.name;
	std::string type = obj.type;
	std::string modelId = obj.modelId;
	std::string qscFuncName = obj.qscFuncName;
	std::string friendlyName = level_.GetLevelObjects().GetModelName(obj.modelId);

	auto to_lower = [](std::string s) {
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
		return s;
	};

	name = to_lower(name);
	type = to_lower(type);
	modelId = to_lower(modelId);
	qscFuncName = to_lower(qscFuncName);
	friendlyName = to_lower(friendlyName);

	return (name.find("watertower") != std::string::npos || name.find("water_tower") != std::string::npos ||
			type.find("watertower") != std::string::npos || type.find("water_tower") != std::string::npos ||
			modelId.find("watertower") != std::string::npos || modelId.find("water_tower") != std::string::npos ||
			qscFuncName.find("watertower") != std::string::npos || qscFuncName.find("water_tower") != std::string::npos ||
			friendlyName.find("watertower") != std::string::npos || friendlyName.find("water_tower") != std::string::npos);
}

bool App::ValidateParentChildCompatibility(const LevelObject& parent, const std::vector<LevelObject>& addedSubtree) {
	if (!IsWaterTower(parent)) {
		return true;
	}
	for (const auto& obj : addedSubtree) {
		if (IsComputer(obj)) {
			return false;
		}
	}
	return true;
}

void App::EvaluateTrainTrackPositions() {
	auto& objects = level_.GetLevelObjects().GetObjects();

	std::map<std::string, int> taskToIdx;
	for (int i = 0; i < (int)objects.size(); ++i) {
		if (!objects[i].taskId.empty())
			taskToIdx[objects[i].taskId] = i;
	}

	struct SplineData {
		std::vector<glm::dvec3> pts;
		std::vector<double> cumDist;
		double totalLen = 0.0;
	};
	std::map<std::string, SplineData> splineCache;

	auto getSpline = [&](const std::string& id) -> const SplineData* {
		auto cached = splineCache.find(id);
		if (cached != splineCache.end()) return &cached->second;
		auto it = taskToIdx.find(id);
		if (it == taskToIdx.end()) return nullptr;
		const auto& spline = objects[it->second];
		if (spline.childrenIndices.size() < 2) return nullptr;
		SplineData sd;
		for (int ci : spline.childrenIndices)
			sd.pts.push_back(objects[ci].pos);
		sd.cumDist.resize(sd.pts.size(), 0.0);
		for (int i = 1; i < (int)sd.pts.size(); ++i)
			sd.cumDist[i] = sd.cumDist[i-1] + glm::length(sd.pts[i] - sd.pts[i-1]);
		sd.totalLen = sd.cumDist.back();
		splineCache[id] = sd;
		return &splineCache[id];
	};

	// Evaluate world position+rotation for a given arc distance on a spline
	auto evalOnSpline = [](const SplineData& sd, double arcLen, glm::dvec3& outPos, glm::dvec3& outRot) {
		double clamped = glm::clamp(arcLen, 0.0, sd.totalLen);
		int seg = 0;
		for (int i = 1; i < (int)sd.cumDist.size(); ++i) {
			if (sd.cumDist[i] >= clamped) { seg = i - 1; break; }
			if (i == (int)sd.cumDist.size() - 1) { seg = i - 1; break; }
		}
		int segNext = std::min(seg + 1, (int)sd.pts.size() - 1);
		double segLen = sd.cumDist[segNext] - sd.cumDist[seg];
		double t = (segLen > 0.0) ? (clamped - sd.cumDist[seg]) / segLen : 0.0;
		outPos = sd.pts[seg] + t * (sd.pts[segNext] - sd.pts[seg]);
		glm::dvec3 fwd = glm::normalize(sd.pts[segNext] - sd.pts[seg]);
		outRot.z = atan2(-fwd.y, -fwd.x); // face opposite to arc direction (cab toward trainyard)
		outRot.x = asin(glm::clamp(-fwd.z, -1.0, 1.0));
		outRot.y = 0.0;
	};

	// Save original rail positions from QSC (obj.pos.x) before modifying obj.pos
	std::vector<double> rawRailPos(objects.size(), 0.0);
	for (int i = 0; i < (int)objects.size(); ++i) {
		if (objects[i].type == "Train" && !objects[i].deleted && !objects[i].splineTaskId.empty())
			rawRailPos[i] = objects[i].pos.x;
	}

	// Computed arc distances (from spline start) per object index — filled in pass 1
	std::map<int, double> arcByObjIdx;

	int evaluated = 0;

	// Pass 1: trains with an explicit non-zero 1D position from QSC.
	// Negative position = distance from the END of the track (game convention):
	//   arcFromStart = totalLen + negativePos
	for (int i = 0; i < (int)objects.size(); ++i) {
		auto& obj = objects[i];
		if (obj.type != "Train" || obj.deleted || obj.splineTaskId.empty()) continue;
		if (rawRailPos[i] == 0.0) continue;

		const SplineData* sd = getSpline(obj.splineTaskId);
		if (!sd || sd->totalLen <= 0.0) continue;

		double rawPos = rawRailPos[i];
		double arcPos = (rawPos < 0.0) ? sd->totalLen + rawPos : rawPos;
		arcPos = glm::clamp(arcPos, 0.0, sd->totalLen);
		arcByObjIdx[i] = arcPos;

		glm::dvec3 newPos, newRot;
		evalOnSpline(*sd, arcPos, newPos, newRot);
		obj.pos = newPos;
		obj.original_pos = newPos;
		obj.rot = newRot;
		obj.original_rot = newRot;
		++evaluated;
	}

	// Pass 2: child wagons with position=0 — place them behind their parent lead car.
	// Wagon length ~52,756 game units (644 local half-extent * 2 * scale 40.96).
	const double WAGON_SPACING = 52756.0;
	std::map<int, int> wagonCountByParent;

	for (int i = 0; i < (int)objects.size(); ++i) {
		auto& obj = objects[i];
		if (obj.type != "Train" || obj.deleted || obj.splineTaskId.empty()) continue;
		if (rawRailPos[i] != 0.0) continue;
		if (obj.parentIndex < 0 || objects[obj.parentIndex].type != "Train") continue;

		auto parentArcIt = arcByObjIdx.find(obj.parentIndex);
		if (parentArcIt == arcByObjIdx.end()) continue;

		const SplineData* sd = getSpline(obj.splineTaskId);
		if (!sd || sd->totalLen <= 0.0) continue;

		int wagonIdx = wagonCountByParent[obj.parentIndex]++;
		// Negative rawRailPos on the parent means the train moves toward arc=0 (Flip=TRUE),
		// so wagons trail behind at HIGHER arc positions (where the train came from).
		double dir = (rawRailPos[obj.parentIndex] < 0.0) ? 1.0 : -1.0;
		double arcPos = glm::clamp(parentArcIt->second + dir * (wagonIdx + 1) * WAGON_SPACING, 0.0, sd->totalLen);
		arcByObjIdx[i] = arcPos;

		glm::dvec3 newPos, newRot;
		evalOnSpline(*sd, arcPos, newPos, newRot);
		obj.pos = newPos;
		obj.original_pos = newPos;
		obj.rot = newRot;
		obj.original_rot = newRot;
		++evaluated;
	}

	if (evaluated > 0)
		Logger::Get().Log(LogLevel::INFO, "[App] Evaluated track positions for " + std::to_string(evaluated) + " train objects.");
}

