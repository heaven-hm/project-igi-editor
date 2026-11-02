// human_seat_frame.h - Origin frame adjustments for seated humans inside vehicles.
//
// MIT License.
#pragma once

#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "igi_transform.h"
#include "mef_native.h"

namespace igi {

// Vehicle task types that can hold passengers.
inline bool IsVehicleTaskType(std::string_view type) {
    return type == "Car" || type == "CarAI" || type == "Heli" || type == "Plane";
}

// The human task types whose mesh is re-based to the feet by
// `SkinSkinnedVertices` (the same family `App::SnapObjectsToTerrain` and
// `Renderer::DrawSkinnedMesh` treat as humans).
inline bool IsHumanTaskType(std::string_view type) {
    return type == "HumanSoldier" || type == "HumanSoldierFemale" ||
           type == "HumanSoldierRPG" || type == "HumanPlayer" || type == "Human" ||
           type == "Soldier";
}

template <typename T, typename = void>
struct HasChildrenIndices : std::false_type {};

template <typename T>
struct HasChildrenIndices<T, std::void_t<decltype(std::declval<T>().childrenIndices)>> : std::true_type {};

template <typename T, typename = void>
struct HasStandAnimation : std::false_type {};

template <typename T>
struct HasStandAnimation<T, std::void_t<decltype(std::declval<T>().standAnimation)>> : std::true_type {};

inline bool IsVehicleSeatedAnimation(int anim) {
    // 61: Driver (car/truck) - e.g. Level 1 & 2 Jones driving truck
    // 240, 243: Seated passenger - e.g. seated in helicopter or truck
    return anim == 61 || anim == 240 || anim == 243;
}

// True when `objects[index]` is a human whose parent chain contains a vehicle
// task, or whose HumanAI child attaches to an AIGraph that is parented to a vehicle.
// The walk is bounded, so a malformed parent cycle cannot hang a draw pass.
template <typename ObjectList>
inline bool HumanSitsInVehicle(const ObjectList& objects, int index) {
    if (index < 0 || index >= static_cast<int>(objects.size())) return false;
    if (!IsHumanTaskType(objects[index].type)) return false;

    // 1. Check direct or indirect parent in parentIndex chain
    int parent = objects[index].parentIndex;
    for (int guard = 0; parent >= 0 && parent < static_cast<int>(objects.size()); ++guard) {
        if (guard > 64) break;
        if (IsVehicleTaskType(objects[parent].type)) return true;
        parent = objects[parent].parentIndex;
    }

    // 2. Check if HumanAI child attaches to an AIGraph parented to a vehicle
    using ItemType = std::decay_t<decltype(objects[0])>;
    if constexpr (HasChildrenIndices<ItemType>::value) {
        for (int ci : objects[index].childrenIndices) {
            if (ci < 0 || ci >= static_cast<int>(objects.size())) continue;
            const auto& child = objects[ci];
            if (child.type == "HumanAI" && child.aiGraphTaskId >= 0) {
                const std::string graphTaskIdStr = std::to_string(child.aiGraphTaskId);
                for (size_t gi = 0; gi < objects.size(); ++gi) {
                    if (objects[gi].type == "AIGraph" && objects[gi].taskId == graphTaskIdStr) {
                        int gParent = objects[gi].parentIndex;
                        for (int gGuard = 0; gParent >= 0 && gParent < static_cast<int>(objects.size()); ++gGuard) {
                            if (gGuard > 64) break;
                            if (IsVehicleTaskType(objects[gParent].type)) {
                                if constexpr (HasStandAnimation<ItemType>::value) {
                                    // If the human has an explicit stand animation, it must be a seated/driving animation
                                    // (e.g. 61 for driver, 240/243 for passenger). Running (244) or walking is on foot outside.
                                    if (objects[index].standAnimation >= 0 &&
                                        !IsVehicleSeatedAnimation(objects[index].standAnimation)) {
                                        return false;
                                    }
                                }
                                return true;
                            }
                            gParent = objects[gParent].parentIndex;
                        }
                        break;
                    }
                }
            }
        }
    }

    return false;
}

// The model-origin translation each draw path must apply, in the model's own
// space and in the units the mesh vertices are in (metres; the matrix scales by
// 40.96).  `rig` is the parsed rig (`ParsedGeometry::bones`); null means "no
// origin to move".
//
//   * `engine_origin_frame == false` (a human on the ground, or any other
//     object): the STATIC bake already carries the rig's root pivot, so the
//     static path adds nothing and the POSED path adds the rig origin to reach
//     the same frame (`posed_draw_transform.h`).  Both put the FEET at `pos`.
//   * `engine_origin_frame == true` (a human authored inside a vehicle): the
//     object origin is the skeleton root, so the posed path adds nothing and
//     the static path subtracts the bake's root pivot.
inline glm::vec3 StaticModelOriginMetres(const std::vector<BoneInfo>* rig,
                                         bool engine_origin_frame) {
    if (!engine_origin_frame || rig == nullptr) return glm::vec3(0.0f);
    return -RigRootOriginRaw(*rig) * kMefNativeScale;
}

inline glm::vec3 PosedModelOriginMetres(const std::vector<BoneInfo>* rig,
                                        bool engine_origin_frame) {
    if (engine_origin_frame) return glm::vec3(0.0f);
    if (rig == nullptr) return glm::vec3(0.0f);
    return RigRootOriginRaw(*rig) * kMefNativeScale;
}

}  // namespace igi
