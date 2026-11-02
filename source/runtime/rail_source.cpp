// rail_source.cpp - see rail_source.h for the oracle and the scope.
#include "runtime/rail_source.h"

#include <algorithm>

namespace igi {

std::unordered_map<std::string, int> BuildTaskIndex(const std::vector<LevelObject>& objects) {
    std::unordered_map<std::string, int> index;
    index.reserve(objects.size());
    for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
        if (!objects[i].taskId.empty()) index[objects[i].taskId] = i;
    }
    return index;
}

int FindLastTaskIndexById(const std::vector<LevelObject>& objects, const std::string& task_id) {
    if (task_id.empty()) return -1;
    for (int i = static_cast<int>(objects.size()) - 1; i >= 0; --i) {
        if (objects[i].taskId == task_id) return i;
    }
    return -1;
}

RailSource BuildRailSource(const std::vector<LevelObject>& objects, int container_index) {
    RailSource out;
    if (container_index < 0 || container_index >= static_cast<int>(objects.size())) return out;

    const LevelObject& container = objects[container_index];
    if (container.childrenIndices.size() < 2) return out;

    // `SplineObjWaypoint_Changed` (HOOK 0x500EF0, src/tagitem/spline.c:771-772)
    // caps the count at the container's `maxSegments` and at 20, and a rigid span
    // is forced to ONE sub-segment (src/sound/segsetup.c:268-270) - both of which
    // `BuildSplineSpan` applies. The floor of 2 is NOT an engine rule: it is the
    // editor's guard against `inv = kOne / o->segments` (src/sound/segsetup.c:107)
    // dividing by zero for a container that declares 0, and it is carried over
    // unchanged from `App::EvaluateTrainTrackPositions` so this extraction cannot
    // move a single car. Every shipped `SplineObj` declares 20, so it is
    // unobservable on retail data.
    const int segments = std::clamp(container.splineSegmentCount, 2, 20);

    std::vector<spline_geometry::SplineSpan> spans;
    spans.reserve(container.childrenIndices.size() - 1);
    for (std::size_t c = 0; c + 1 < container.childrenIndices.size(); ++c) {
        const int from_index = container.childrenIndices[c];
        const int to_index = container.childrenIndices[c + 1];
        // A stale child index has no waypoint to read a position or a frame from.
        // The placement pass indexes the list unchecked, so this guard only
        // replaces undefined behaviour; it cannot change a defined result.
        if (from_index < 0 || from_index >= static_cast<int>(objects.size()) ||
            to_index < 0 || to_index >= static_cast<int>(objects.size())) {
            return RailSource{};
        }
        const LevelObject& from = objects[from_index];
        const LevelObject& to = objects[to_index];
        spline_geometry::SplineSpan span;
        span.from = from.pos;
        span.to = to.pos;
        // The tangent of each end is that waypoint's own local X axis, i.e. the
        // first column of its orientation frame (setup_segments,
        // src/sound/segsetup.c:98-100).
        span.rotFrom = spline_geometry::EngineEulerMatrix(from.rot);
        span.rotTo = spline_geometry::EngineEulerMatrix(to.rot);
        span.segments = segments;
        span.rigid = container.linearSegments;
        // A waypoint's own "Flip" (arg 13) is not modelled by LevelObject, so
        // every span is evaluated unflipped.
        span.reversed = false;
        spans.push_back(span);
    }

    out.path = spline_geometry::BuildRailPath(spans);
    out.first_waypoint = objects[container.childrenIndices.front()].pos;
    out.last_waypoint = objects[container.childrenIndices.back()].pos;
    out.container_index = container_index;
    out.valid = true;
    return out;
}

int SelectedRailContainer(const std::vector<LevelObject>& objects, int selected_index) {
    if (selected_index < 0 || selected_index >= static_cast<int>(objects.size())) return -1;
    const LevelObject& selected = objects[selected_index];
    if (selected.deleted) return -1;
    if (selected.isSplineContainer) return selected_index;
    return FindLastTaskIndexById(objects, selected.splineTaskId);
}

std::vector<int> RailContainersToShow(const std::vector<LevelObject>& objects, int selected_index) {
    std::vector<int> containers;

    auto add = [&](int container) {
        if (container < 0 || container >= static_cast<int>(objects.size())) return;
        if (objects[container].deleted) return;
        if (std::find(containers.begin(), containers.end(), container) != containers.end()) return;
        containers.push_back(container);
    };

    // One scan for the level's rail references - normally none, one or two, so
    // this stays cheap on the levels that have no rail at all.
    for (const LevelObject& obj : objects) {
        if (obj.deleted || obj.splineTaskId.empty()) continue;
        add(FindLastTaskIndexById(objects, obj.splineTaskId));
    }

    add(SelectedRailContainer(objects, selected_index));

    // Ascending container order: the draw order is then a property of the level,
    // not of the order the references happen to appear in.
    std::sort(containers.begin(), containers.end());
    return containers;
}

}  // namespace igi
