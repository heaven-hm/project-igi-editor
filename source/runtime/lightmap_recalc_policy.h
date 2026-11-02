#pragma once

#include <glm/glm.hpp>

namespace igi {

// Inputs for one `lightmap recalc` invocation. --rot-orig must be the pose the
// .olm on disk was baked at: the authored QSC pose for a shipped bake, or the
// live pose a previous recalc baked. Recording the object's live pose as the
// bake origin (or falling back to live-vs-live when no bake is recorded) makes
// the rotation delta zero and turns the whole recalc into an identity no-op —
// rotating or moving a building then appeared to "do nothing" to its bake.
struct LightmapBakeOrigin {
    bool has_bake_pose = false;
    glm::dvec3 baked_pos{0.0}, baked_rot{0.0};       // pose recorded with the bound .olm
    glm::dvec3 original_pos{0.0}, original_rot{0.0}; // authored QSC pose (shipped bake)
    glm::dvec3 current_pos{0.0}, current_rot{0.0};   // live pose to bake toward
};

struct LightmapRecalcPoses {
    glm::dvec3 orig_pos, orig_rot; // --rot-orig
    glm::dvec3 new_pos, new_rot;   // --rot-new
};

inline LightmapRecalcPoses ComputeLightmapRecalcPoses(const LightmapBakeOrigin& in) {
    if (in.has_bake_pose)
        return {in.baked_pos, in.baked_rot, in.current_pos, in.current_rot};
    return {in.original_pos, in.original_rot, in.current_pos, in.current_rot};
}

// The bake pose to record together with a bound .olm texture is the pose that
// .olm was baked at: the authored QSC pose for shipped/reloaded bakes, or the
// live pose right after a recalc re-lighted the .olm toward it.
inline glm::dvec3 RecordedBakePos(const LightmapBakeOrigin& in, bool olm_from_recalc) {
    return olm_from_recalc ? in.current_pos
                           : (in.has_bake_pose ? in.baked_pos : in.original_pos);
}

inline glm::dvec3 RecordedBakeRot(const LightmapBakeOrigin& in, bool olm_from_recalc) {
    return olm_from_recalc ? in.current_rot
                           : (in.has_bake_pose ? in.baked_rot : in.original_rot);
}

} // namespace igi
