/******************************************************************************
 * @file    human_seat_frame.h
 * @brief   Where a human that the level authors INSIDE a vehicle is drawn:
 *          the engine's own object origin (the skeleton's root), not the
 *          editor's feet-rebased frame.
 *
 * WHY THIS EXISTS
 *   A level script nests a passenger/driver inside the vehicle task that owns
 *   it — LEVEL1's `objects.qsc` puts `Task_New(1505, "HumanSoldier", ...)` and
 *   `Task_New(4023, "HumanSoldier", "Jones drive truck", ...)` inside a `Heli`
 *   / next to a `Car`, and LEVEL4's "Harrison & Men in Heli" puts five
 *   HumanSoldiers inside a `Heli`.  The engine draws such a human with its
 *   OBJECT ORIGIN at the authored position, and that origin is the SKELETON'S
 *   ROOT (the pelvis), with the mesh hanging BELOW it.  The editor draws every
 *   other human with the mesh's FEET at the authored position instead, which is
 *   a different frame 3990.4 raw units (0.974 m) lower.
 *
 * ENGINE BASIS (IGI-RE, /Users/haseeb-mir/Documents/Code/IGI-RE, read-only)
 *
 *   * The pose puts the root bone AT THE OBJECT'S ORIGIN:
 *     `BoneObj_Update` HOOK 0x4D38C0 (`src/boneobj/boneobj.c:371-419`) poses
 *     the tree with `pos = {0, 0, 0}` (:376) and the object's matrix as the
 *     root frame, and `BoneObj_UpdateBone` HOOK 0x4D3A90 (`:423-525`) computes
 *     `b->pos = parentPos + parentMatrix * b->offset` (:434-439).  The root
 *     bone's `offset` is TLST[0] = (0, 0, 0) for every shipped human skeleton
 *     (`BoneObj_Load` HOOK 0x4D67A0, `:1570-1576`; measured: 000.IFF / 001.IFF
 *     / 006.IFF all have TLST[0..2] = (0, 0, 0)).
 *   * The DRAW uses that origin unchanged: `BoneDynCubeObj_EnterHandler`
 *     HOOK 0x5124C0 (`src/glowmesh/bonedynd3d.c:36-61`) draws the mesh with
 *     `obj->pos` / `obj->matrix` (:41-42), and `BoneMesh_Skin` HOOK 0x49B700
 *     (`src/dxinit/d3drender.c:259-264`) computes `Bone::matrix * v + Bone::pos`
 *     — so the skinned vertices are in the root bone's frame, whose origin is
 *     the object's position.  The mesh's own pivot→feet lift is therefore
 *     BELOW the object's origin, not at it.
 *   * The engine never re-bases that frame: the human's drawn Z comes from
 *     `Human_ProbeGround` HOOK 0x462390 (`src/humanbase/human.c:1674-1857`),
 *     whose `ground:` block is `to[2] = foot[2] + hits[0].pos.z` (:1816) with
 *     `foot = Human_GetBonePos(buf, h, 0)` (:1693) — i.e. the object origin is
 *     the floor plus BONE 0's animated height, and bone 0 is the root the pose
 *     placed at the origin.  `Human_ParamChangedHandler` HOOK 0x45F890
 *     (`:448-461`) sets only the matrix's ROTATION (`Matrix_SetRotZ(h->matrix.m,
 *     h->yaw)`, :460) from the authored `Gamma`; the authored `ObjectPos` goes
 *     to the collision cube (`DynCube_Move(Game_GetWorld(), h, h->placePos,
 *     h->radius)`, :457).
 *   * A human authored inside a vehicle is carried by the vehicle, not placed
 *     by it: `Vehicle_NewParts` HOOK 0x4F4800 (`src/vehbase/vehicle.c:251-256`)
 *     builds `v->attached` from the MODEL's ATTA list only, so a script child
 *     never receives the move event; what carries it is the ground probe above
 *     (the vehicle's own collision floor) plus `Human_UpdatePlatformMove`
 *     HOOK 0x460CF0 (`src/humanbase/human.c:1190-1238`) and the platform branch
 *     of `Human_UpdateBody` HOOK 0x4610C0 (`:1316-1365`).
 *
 * WHAT THE EDITOR DOES, AND WHY THIS HEADER
 *   `ParseMefGeometry` re-bases every human mesh to the FEET
 *   (`SkinSkinnedVertices`, `source/renderer/mef_native.cpp:1122-1129`: the
 *   rig's bone world positions are summed into each vertex, and that rig's root
 *   pivot is (0, 0, 3990.4) — `RigRootOriginRaw`, `source/renderer/mef_native.h`),
 *   and `App::SnapObjectsToTerrain` (`source/app_view.cpp`) then drops such a
 *   human so those feet land on the terrain.  That pair is self-consistent for a
 *   human standing on the ground, and it is what the user validated.
 *
 *   It is NOT self-consistent for a human the level authors inside a vehicle:
 *   the terrain snap never touches one (it is inside a vehicle, or in the air,
 *   or `modelId == "000_01_1"`), so the authored position keeps the ENGINE's
 *   meaning — the object origin — while the editor draws the mesh's feet there.
 *   The result is a seated human drawn one pivot→feet lift (3990.4 raw units =
 *   0.974 m) ABOVE its seat, standing through the vehicle's roof.
 *
 *   This header is the ONE place that decision lives, so the static draw
 *   (`renderer_objects.cpp`), the posed draw (`app.cpp`) and the host-lane test
 *   (`tests/test_seated_human.cpp`) cannot drift apart.
 *****************************************************************************/
#pragma once

#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "igi_transform.h"
#include "mef_native.h"

namespace igi {

// The vehicle task types a level can own a passenger with.  These are the task
// names the shipped QSC uses (`Car`, `CarAI`, `Heli`, `Plane`, `Train`); the
// engine's own `TASKTYPE_*` constants for them are registered in
// `src/rotor/car.c:276`, `src/heli/heli.c:201` and `src/missile/train.c:375`.
inline bool IsVehicleTaskType(std::string_view type) {
    return type == "Car" || type == "CarAI" || type == "Heli" || type == "Plane" ||
           type == "Train";
}

// The human task types whose mesh is re-based to the feet by
// `SkinSkinnedVertices` (the same family `App::SnapObjectsToTerrain` and
// `Renderer::DrawSkinnedMesh` treat as humans).
inline bool IsHumanTaskType(std::string_view type) {
    return type == "HumanSoldier" || type == "HumanSoldierFemale" ||
           type == "HumanSoldierRPG" || type == "HumanPlayer" || type == "Human" ||
           type == "Soldier";
}

// True when `objects[index]` is a human whose PARENT CHAIN contains a vehicle
// task — the level's own way of saying "this human rides in that vehicle".  The
// walk is bounded, so a malformed parent cycle cannot hang a draw pass.
template <typename ObjectList>
inline bool HumanSitsInVehicle(const ObjectList& objects, int index) {
    if (index < 0 || index >= static_cast<int>(objects.size())) return false;
    if (!IsHumanTaskType(objects[index].type)) return false;
    int parent = objects[index].parentIndex;
    for (int guard = 0; parent >= 0 && parent < static_cast<int>(objects.size()); ++guard) {
        if (guard > 64) break;
        if (IsVehicleTaskType(objects[parent].type)) return true;
        parent = objects[parent].parentIndex;
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
//     engine's object origin is the skeleton's root, so the POSED path adds
//     nothing and the STATIC path subtracts the bake's root pivot.  Both put
//     the model's ORIGIN (the pelvis) at `pos` — which is where the engine puts
//     it (`BoneObj_Update` 0x4D38C0, `BoneDynCubeObj_EnterHandler` 0x5124C0).
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
