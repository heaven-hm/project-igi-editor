/******************************************************************************
 * @file    app_lookup.cpp
 * @brief   App: model name/id lookup, copy, search, and validation helpers
 *          Split from app.cpp; shares app_internal.h.
 *****************************************************************************/
#include "app_internal.h"
#include "renderer/spline_geometry.h"
#include "runtime/elevator_rules.h"

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
	
	// Parse by searching for each key directly so values containing '{' or '}' don't break extraction.
	auto extractNextValue = [&](size_t searchFrom, const std::string& key, std::string& outVal) -> size_t {
		std::string keyStr = "\"" + key + "\"";
		size_t kpos = content.find(keyStr, searchFrom);
		if (kpos == std::string::npos) return std::string::npos;
		size_t colon = content.find(":", kpos + keyStr.size());
		if (colon == std::string::npos) return std::string::npos;
		size_t qStart = content.find("\"", colon + 1);
		if (qStart == std::string::npos) return std::string::npos;
		size_t qEnd = content.find("\"", qStart + 1);
		if (qEnd == std::string::npos) return std::string::npos;
		outVal = content.substr(qStart + 1, qEnd - qStart - 1);
		return qEnd + 1;
	};

	size_t pos = 0;
	while (pos < content.size()) {
		std::string modelName, modelId;
		size_t nPos = extractNextValue(pos, "ModelName", modelName);
		size_t iPos = extractNextValue(pos, "ModelId",   modelId);
		if (nPos == std::string::npos && iPos == std::string::npos) break;
		if (!modelId.empty() || !modelName.empty())
			entries.push_back({modelName, modelId});
		// Advance past whichever key came last so we don't re-parse the same entry.
		pos = (nPos != std::string::npos && iPos != std::string::npos)
		    ? (nPos > iPos ? nPos : iPos)
		    : (nPos != std::string::npos ? nPos : iPos);
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
		// The rail as the engine sees it: one Hermite span per waypoint pair,
		// built from the waypoints' own frames (see getSpline). Arc distances are
		// the engine's SplineObj.length, the sum of the per-joint segment lengths
		// (src/tagitem/spline.c:824-830), not the chord sum.
		spline_geometry::RailPath rail;
		double totalLen = 0.0;
		// The AUTHORED first and last waypoint points (`w->point`, which
		// SplineObj_GetWaypointPos HOOK 0x502200 returns, src/editobj/spline.c:155-163).
		// An Elevator whose "Linear" flag is set interpolates between exactly these
		// two, not between curve joints.
		glm::dvec3 firstWaypoint{0.0};
		glm::dvec3 lastWaypoint{0.0};
	};

	// The engine reads a train's Flip Direction (arg 9) and uses it to pick the
	// sign of the rail spacing: offsets[0] = reverse ? +length : -length
	// (Train_Place, src/missile/train.c:221). The editor does not model that flag
	// as a field, so it is read from the raw tokens; when the token is absent the
	// sign of the authored 1D position is used as the historical fallback. The
	// result is captured BEFORE any placement runs, because the fallback reads the
	// authored position that pass 1 is about to overwrite.
	auto readReversedFlag = [](const LevelObject& obj) {
		if (obj.argTokens.size() > 9) {
			std::string flag = obj.argTokens[9];
			std::transform(flag.begin(), flag.end(), flag.begin(),
			               [](unsigned char c) { return std::tolower(c); });
			if (flag == "true" || flag == "1") return true;
			if (flag == "false" || flag == "0") return false;
		}
		return obj.pos.x < 0.0;
	};

	// Model-derived car length. The engine takes it from the model's local points
	// 0 and 1: t->length = |a.x - b.x| (Train_ParamsChanged,
	// src/missile/train.c:142-167). The editor does not parse those points, so the
	// value stays a documented constant — it is the only remaining approximation
	// here, and it only shifts the cars along the rail, never the rail itself.
	const double kDefaultCarLength = 52756.0;

	// Displacement X/Y (args 10/11), clamped to gTrainOffsetRange = +/-5 m by
	// PlaceTrainOnRail. Read from the raw tokens for the same reason as the flip
	// flag; absent or unparseable means no displacement.
	auto trainOffsetArg = [](const LevelObject& obj, size_t argIdx) {
		if (obj.argTokens.size() <= argIdx) return 0.0;
		const std::string& token = obj.argTokens[argIdx];
		char* end = nullptr;
		const double value = std::strtod(token.c_str(), &end);
		return (end && end != token.c_str()) ? value : 0.0;
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
		// One span per waypoint pair. The Hermite tangents are the waypoints' own
		// local X axes (src/sound/segsetup.c:99-101), the sub-segment count is the
		// container's "Number of Matrices / Segment" (arg 7, defaulted from
		// obj->maxSegments by SplineObjWaypoint_Create, src/tagitem/spline.c:561),
		// and the container's "Linear Segments" (arg 3) is the engine's `rigid`
		// flag (src/editobj/spline.c:205).
		const int segments = std::clamp(spline.splineSegmentCount, 2, 20);
		std::vector<spline_geometry::SplineSpan> spans;
		spans.reserve(spline.childrenIndices.size() - 1);
		for (size_t c = 0; c + 1 < spline.childrenIndices.size(); ++c) {
			const LevelObject& from = objects[spline.childrenIndices[c]];
			const LevelObject& to = objects[spline.childrenIndices[c + 1]];
			spline_geometry::SplineSpan span;
			span.from = from.pos;
			span.to = to.pos;
			span.rotFrom = spline_geometry::EngineEulerMatrix(from.rot);
			span.rotTo = spline_geometry::EngineEulerMatrix(to.rot);
			span.segments = segments;
			span.rigid = spline.linearSegments;
			// The waypoint's own Flip (arg 13) is not modelled by LevelObject, so
			// every span is evaluated unflipped.
			span.reversed = false;
			spans.push_back(span);
		}
		sd.rail = spline_geometry::BuildRailPath(spans);
		sd.totalLen = sd.rail.length;
		// SplineObj_GetWaypointPos(graph, 0) and (graph, count - 1): the authored
		// waypoint points, which the Elevator "Linear" branch interpolates.
		sd.firstWaypoint = objects[spline.childrenIndices.front()].pos;
		sd.lastWaypoint = objects[spline.childrenIndices.back()].pos;
		splineCache[id] = sd;
		return &splineCache[id];
	};

	// Save original rail positions from QSC (obj.pos.x) before modifying obj.pos,
	// together with each train's Flip Direction.
	std::vector<double> rawRailPos(objects.size(), 0.0);
	std::vector<char> railReversed(objects.size(), 0);
	for (int i = 0; i < (int)objects.size(); ++i) {
		if (objects[i].type == "Train" && !objects[i].deleted && !objects[i].splineTaskId.empty()) {
			rawRailPos[i] = objects[i].pos.x;
			railReversed[i] = readReversedFlag(objects[i]) ? 1 : 0;
		}
	}

	// Computed arc distances (from spline start) per object index — filled in pass 1
	std::map<int, double> arcByObjIdx;

	int evaluated = 0;

	// Pass 1: trains with an explicit non-zero 1D position from QSC.
	// The 1D position is a SIGNED arc distance: a negative value is measured back
	// from the end of the rail and wrapped into range by repeated addition
	// (SplineObj_GetPoints, src/tagitem/spline.c:242-255). The old code added the
	// rail length once and then clamped, so a |position| larger than one rail
	// length collapsed to arc 0 — retail train 1499 authors -4045627.5 on a
	// 4435504.8-unit rail, where the single add happens to work but any shorter
	// rail would not. WrapRailDistance loops exactly like the engine.
	for (int i = 0; i < (int)objects.size(); ++i) {
		auto& obj = objects[i];
		if (obj.type != "Train" || obj.deleted || obj.splineTaskId.empty()) continue;
		if (rawRailPos[i] == 0.0) continue;

		const SplineData* sd = getSpline(obj.splineTaskId);
		if (!sd || sd->totalLen <= 0.0) continue;

		const double arcPos = spline_geometry::WrapRailDistance(rawRailPos[i], sd->totalLen);
		arcByObjIdx[i] = arcPos;

		// Train_Place (src/missile/train.c:209-274) with the car's own frac = 0
		// (the model's local point 0 sits at the origin), the car's displacement
		// clamped to gTrainOffsetRange = +/-5 m and scaled by kFixed = 4096, and
		// the heading from Matrix_SetDirection + orient() rather than the old
		// atan2/asin reconstruction, which ignored the engine's fixed axis remap.
		const spline_geometry::TrainPlacement placed = spline_geometry::PlaceTrainOnRail(
			sd->rail, rawRailPos[i], kDefaultCarLength, 0.0,
			trainOffsetArg(obj, 10), trainOffsetArg(obj, 11), railReversed[i] != 0);
		if (!placed.found) continue;

		obj.pos = placed.position;
		obj.original_pos = placed.position;
		obj.rot = spline_geometry::EngineMatrixToEuler(placed.matrix);
		obj.original_rot = obj.rot;
		++evaluated;
	}

	// Pass 2: child wagons with position=0 — place them behind their parent lead car.
	// In Train_Place each point is stepped off the previous one by the PREVIOUS car's
	// length (offsets[m] = reverse ? +wagon_m.length : -wagon_m.length,
	// src/missile/train.c:221-225), so the m-th car sits at
	// arc = dist +/- m * length and its heading is the step to the next car. The old
	// code took the sign from the sign of the parent's authored position instead of
	// the Flip Direction flag and used a hardcoded spacing; the spacing constant
	// remains (the editor does not parse model local points) but the sign, the
	// chaining and the arithmetic now follow the engine.
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

		const LevelObject& parent = objects[obj.parentIndex];
		const bool reversed = railReversed[obj.parentIndex] != 0;
		const int wagonIdx = wagonCountByParent[obj.parentIndex]++;
		const double carsBack = (wagonIdx + 1) * kDefaultCarLength;
		// SplineObj_GetPoints walks the consist with target = dist + sum(offsets)
		// and CLAMPS at the rail ends (`goto off_end`, src/tagitem/spline.c:441-443)
		// — it never wraps a car, unlike the lead car's own scalar. Pre-clamping
		// here makes the helper's internal wrap a no-op, which is what the engine
		// does. Offset the distance, not the spacing: the heading must come from a
		// single car-length step, exactly as Train_Place builds pts[m] -> pts[m+1].
		const double leadArc = spline_geometry::WrapRailDistance(rawRailPos[obj.parentIndex], sd->totalLen);
		const double carDist = std::clamp(leadArc + (reversed ? carsBack : -carsBack), 0.0, sd->totalLen);

		const spline_geometry::TrainPlacement placed = spline_geometry::PlaceTrainOnRail(
			sd->rail, carDist, kDefaultCarLength, 0.0,
			trainOffsetArg(obj, 10), trainOffsetArg(obj, 11), reversed);
		if (!placed.found) continue;

		arcByObjIdx[i] = carDist;

		obj.pos = placed.position;
		obj.original_pos = placed.position;
		obj.rot = spline_geometry::EngineMatrixToEuler(placed.matrix);
		obj.original_rot = obj.rot;
		++evaluated;
	}

	if (evaluated > 0)
		Logger::Get().Log(LogLevel::INFO, "[App] Evaluated track positions for " + std::to_string(evaluated) + " train objects.");

	// ── Elevator / moving platform (GitHub issue #40) ────────────────────────
	// `Elevator` is the retail "moving platform": the shipped missions place it in
	// LEVEL3/5/8/11/12/13/14. Its authored fields are parsed into LevelObject by
	// level_objects.cpp (see the elevator block there) and the placement rule is
	// `Elevator_UpdatePlace` HOOK 0x43D960 (src/props/elevator.c:41-83), ported in
	// runtime/elevator_rules.h. Before this pass an elevator had no rail at all,
	// so it was drawn at its authored spot with no path — the issue's
	// "moving platforms/conveyors have no path or animation preview".
	//
	// The placement is idempotent: the rail comes from the waypoints' authored
	// points and the scalar from arg 13 ("Pos"), neither of which this pass
	// writes, so a second call lands in the same place.
	int elevatorsPlaced = 0;
	for (int i = 0; i < (int)objects.size(); ++i) {
		auto& obj = objects[i];
		if (obj.type != "Elevator" || obj.deleted || obj.splineTaskId.empty()) continue;

		igi::ElevatorParams params;
		params.parsed = true;
		params.graph = 0;   // non-(-1): the rail reference itself is resolved below
		params.linear = obj.elevatorLinear;
		params.force_orientation = obj.elevatorForceOrientation;
		params.start_height = obj.elevatorStartHeight;

		const SplineData* sd = getSpline(obj.splineTaskId);
		if (!sd || sd->totalLen <= 0.0) continue;

		igi::ElevatorRail rail;
		rail.path = sd->rail;
		rail.first_waypoint = sd->firstWaypoint;
		rail.last_waypoint = sd->lastWaypoint;

		// "Orientation" (args 6/7/8) is the authored `placeMatrix`, which
		// `Elevator_ParamsChanged` HOOK 0x43DDB0 (src/props/elevator.c:114-144)
		// copies into the object's frame before calling the placement rule.
		const glm::dmat3 placeMatrix = spline_geometry::EngineEulerMatrix(obj.rot);

		const igi::ElevatorPlacement placed = igi::PlaceElevatorOnRail(rail, params, placeMatrix);
		if (!placed.found) continue;

		obj.pos = placed.position;
		obj.original_pos = placed.position;
		obj.rot = spline_geometry::EngineMatrixToEuler(placed.matrix);
		obj.original_rot = obj.rot;
		++elevatorsPlaced;
	}

	if (elevatorsPlaced > 0)
		Logger::Get().Log(LogLevel::INFO, "[App] Evaluated rail positions for " + std::to_string(elevatorsPlaced) + " elevator objects.");
}

