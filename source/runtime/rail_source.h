#pragma once
// rail_source.h - "which rail does this task ride, and what does that rail look
// like" answered in ONE place, so the viewport preview and the placement pass
// cannot disagree about the curve.
//
// The curve itself is the engine's, and it is not re-derived here: one cubic
// Hermite span per waypoint pair (`setup_segments`, IGI-RE
// src/sound/segsetup.c:88-199) chained into the joint list
// `spline_geometry::BuildRailPath` returns, which is what the editor already
// places cars and platforms on - `Train_Place` HOOK 0x42A2C0
// (src/missile/train.c:209-274) and `Elevator_UpdatePlace` HOOK 0x43D960
// (src/props/elevator.c:41-83), both reached through `SplineObj_GetPoints`
// HOOK 0x4FEC10 (src/tagitem/spline.c:219-454). This module only resolves the
// AUTHORED inputs that curve is built from out of the editor's `LevelObject`
// list.
//
// There is no engine function with this shape: the engine holds the rail by
// pointer (`SplineObj*`, or the `graph` int a task stores). The editor's objects
// are a flat list, so a container is found by its task id - the value a `Train`
// writes from its "RailroadQTaskID" (arg 5) and an `Elevator` from its
// "SplineObj taskid" (arg 10), the only two fields the loader stores in
// `LevelObject::splineTaskId` (source/level/level_objects.cpp:363,556).

#include <string>
#include <unordered_map>
#include <vector>

#include "level/level_objects.h"
#include "renderer/spline_geometry.h"

namespace igi {

// The rail one `SplineObj` container describes.
struct RailSource {
    // The curve, exactly as `BuildRailPath` builds it: joints in waypoint order
    // plus the cumulative arc table the sampler walks.
    spline_geometry::RailPath path;
    // `SplineObj_GetWaypointPos` HOOK 0x502200 (src/editobj/spline.c:155-163)
    // returns `w->point`, the AUTHORED waypoint position, not a joint of the bent
    // curve. The Elevator "Linear" branch interpolates between exactly these two
    // (src/props/elevator.c:58-66), which is why they are carried separately.
    glm::dvec3 first_waypoint{0.0};
    glm::dvec3 last_waypoint{0.0};
    int container_index = -1;
    // False when the index is out of range or the container has fewer than two
    // waypoint children - with no span there is no rail, which is the state
    // `SplineObj_GetLength` reports as length 0.
    bool valid = false;
};

// `task id -> object index`, last declaration wins. The placement pass builds
// the same map, so both callers resolve a rail reference identically.
std::unordered_map<std::string, int> BuildTaskIndex(const std::vector<LevelObject>& objects);

// Index of the LAST object that declares `task_id` — the resolution
// `BuildTaskIndex` performs, without materialising the map. The viewport overlay
// resolves a handful of references per frame and most levels declare no rail at
// all, so it scans instead of building a table. -1 when nothing declares the id.
int FindLastTaskIndexById(const std::vector<LevelObject>& objects, const std::string& task_id);

// Builds the rail of `objects[container_index]` from its `childrenIndices` chain
// with the inputs the placement pass uses:
//   * one span per consecutive waypoint pair,
//   * the container's "Number of Matrices / Segment" (arg 7) clamped to 2..20,
//   * the container's "Linear Segments" (arg 3) as the rigid flag,
//   * each waypoint's own orientation frame as that end's Hermite tangent.
// A waypoint's own "NumAreas" (arg 11) and "Flip" (arg 13) are not modelled by
// `LevelObject`, so the container-wide count and an unflipped span are used -
// the same substitutions `App::EvaluateTrainTrackPositions` makes.
RailSource BuildRailSource(const std::vector<LevelObject>& objects, int container_index);

// The container `selected_index` is itself, or the one it rides; -1 when the
// selection names no rail. Used to highlight the rail being edited.
int SelectedRailContainer(const std::vector<LevelObject>& objects, int selected_index);

// The rails the viewport overlay draws, as container indices in ascending order:
//   * every `SplineObj` a rail consumer names (a non-empty `splineTaskId` is
//     exactly "this task rides a rail" - only Train and Elevator set it), and
//   * the selected object's own rail, so a rail can be inspected before anything
//     is placed on it.
// Deleted objects contribute nothing. No model id, type list or level is
// special-cased: the set is derived from the references the level itself holds.
std::vector<int> RailContainersToShow(const std::vector<LevelObject>& objects, int selected_index);

}  // namespace igi
