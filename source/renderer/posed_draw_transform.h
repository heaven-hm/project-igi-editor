#pragma once

// How the POSED (skinned) draw places an object -- the one place that decision
// lives, so `App::Frame` and `tests/test_skinned_static_parity.cpp` cannot drift
// apart.
//
// A posed model is drawn from the engine's own skinned vertices: `BoneMesh_Skin`
// HOOK 0x49B700 (src/dxinit/d3drender.c:259-264) computes
// `Bone::matrix * v + Bone::pos`, and the bone chain starts at the skeleton's
// root offset, which `BoneObj_Load` 0x4D67A0 copies out of the BOBJ's TLST
// (src/boneobj/boneobj.c:1570-1576) -- (0, 0, 0) for every shipped human rig.
// Those vertices are therefore in the SKELETON's frame, with the model hanging
// BELOW the object's origin.
//
// Everything else in the editor treats the object's position as the model's
// ORIGIN as the STATIC bind-pose bake defines it: `ParseMefGeometry` replaces
// every base vertex with `SkinSkinnedVertices(geometry, nullptr, ...)`
// (source/renderer/mef_native.cpp:1122-1129), which sums the EDITOR's rig pivots
// into each vertex, and that rig's root pivot is (0, 0, 3990.4) -- 0.974 m at the
// engine's 4096 units/m (`kUnitsToMeters`, src/humanbase/human.c:165). The static
// draw, the terrain snap (`App::SnapObjectsToTerrain`), the picking ray
// (`Renderer_Objects::PickObject`) and the level's own authored placements are all
// calibrated to that bake.
//
// So a posed draw that uses the object matrix unchanged lands 3990.4 raw units
// (0.974 m) below the static one: the AI stands waist-deep in the terrain. This
// header adds the rig's root origin to the posed object matrix, which puts the
// two draws in the same frame. See `RigRootOriginRaw` (renderer/mef_native.h) for
// the engine citations and `IgiObjectMatrixAtModelOrigin`
// (renderer/igi_transform.h) for why the offset is a model-local translation.

#include <vector>

#include <glm/glm.hpp>

#include "igi_transform.h"
#include "mef_native.h"

namespace igi {

// `rig` is the model's parsed rig (`ParsedGeometry::bones`); null (or a rig with
// no root) means "no origin to add", i.e. the object matrix alone.
inline glm::mat4 PosedObjectMatrix(const glm::dvec3& pos, const glm::dvec3& rot, float scale,
                                   const std::vector<BoneInfo>* rig) {
    const glm::vec3 origin_metres =
        (rig != nullptr) ? RigRootOriginRaw(*rig) * kMefNativeScale : glm::vec3(0.0f);
    return IgiObjectMatrixAtModelOrigin(pos, rot, 40.96f * scale, origin_metres);
}

}  // namespace igi
