#pragma once

#include "renderer/graph_writer.h"
#include "level/level_objects.h"

#include <glm/glm.hpp>

#include <cmath>
#include <optional>

enum class GraphCameraTargetKind {
    Object,
    GraphOrigin,
    GraphNode,
};

struct GraphCameraTarget {
    GraphCameraTargetKind kind = GraphCameraTargetKind::Object;
    glm::dvec3 position{0.0};
    int node_id = -1;
};

struct GraphCameraPose {
    glm::dvec3 position{0.0};
    float yaw_degrees = 0.0f;
    float pitch_degrees = 0.0f;
};

// A selected authored object remains the F11 destination while the graph
// overlay is open unless the user has explicitly selected a graph node.
inline bool ShouldF11SnapSelectedObjectDirectly(
    bool has_selected_object, bool has_model,
    bool overlay_visible, int selected_node_id) {
    (void)overlay_visible;
    (void)selected_node_id;
    return has_selected_object && has_model;
}

inline bool ShouldProcessF11WhilePaused(int key, int f11_key, bool paused) {
    return !paused || key == f11_key;
}

// Put the graph target in front of the camera instead of placing the camera
// inside the node/graph origin while retaining the previous view direction.
// The latter made F11 appear to do nothing when the target was not already in
// the camera frustum. The distance is expressed in the editor's native world
// units (4096 units per metre).
inline GraphCameraPose MakeF11GraphCameraPose(
    const glm::dvec3& target,
    const glm::dvec3& preferred_forward,
    double distance) {
    glm::dvec3 forward = preferred_forward;
    if (glm::dot(forward, forward) <= 0.000001) {
        forward = glm::dvec3(0.0, 1.0, 0.0);
    } else {
        forward = glm::normalize(forward);
    }
    const glm::dvec3 camera_position = target - forward * std::max(1.0, distance);
    const glm::dvec3 to_target = glm::normalize(target - camera_position);
    const double horizontal = std::sqrt(to_target.x * to_target.x +
                                        to_target.y * to_target.y);
    GraphCameraPose pose;
    pose.position = camera_position;
    pose.yaw_degrees = static_cast<float>(glm::degrees(
        std::atan2(-to_target.x, to_target.y)));
    pose.pitch_degrees = static_cast<float>(glm::degrees(
        std::atan2(to_target.z, std::max(0.000001, horizontal))));
    return pose;
}

// Computes the camera snap pose for an authored object, placing the camera
// back along the viewer's forward vector at a safe distance proportional to
// the model's bounding radius so the object is fully framed.
inline GraphCameraPose MakeF11ObjectCameraPose(
    const glm::dvec3& target,
    float boundRadius,
    const glm::dvec3& preferred_forward,
    bool shiftHeld) {
    if (boundRadius < 500.0f) boundRadius = 2000.0f;
    const double distMultiplier = shiftHeld ? 3.0 : 1.6;
    const double distance = std::max(static_cast<double>(boundRadius * distMultiplier),
                                     6.0 * 4096.0);
    glm::dvec3 center = target;
    center.z += boundRadius * 0.25;
    return MakeF11GraphCameraPose(center, preferred_forward, distance);
}

// Resolve the graph task referenced by a selected editor object. HumanSoldier
// stores the relationship on its nested HumanAI task, while AIGraph and
// HumanAI selections carry it directly. Keep this lookup independent of the
// renderer so F11 and tests use the same authored-object relationship.
inline std::string FindRelatedGraphTaskId(const std::vector<LevelObject>& objects,
                                           int selected_object_index) {
    if (selected_object_index < 0 ||
        selected_object_index >= static_cast<int>(objects.size())) return {};

    // The object parser normally exposes HumanAI as a direct child of the
    // soldier, but edited task trees can insert containers/weapon tasks between
    // the two.  Resolve through the authored hierarchy instead of depending on
    // one particular nesting shape.  A reverse walk also makes F11 useful when
    // the user selected the HumanAI or Gun child rather than its soldier parent.
    std::vector<int> parent(objects.size(), -1);
    for (int parent_index = 0;
         parent_index < static_cast<int>(objects.size());
         ++parent_index) {
        for (int child_index : objects[parent_index].childrenIndices) {
            if (child_index >= 0 && child_index < static_cast<int>(objects.size()) &&
                parent[child_index] < 0) {
                parent[child_index] = parent_index;
            }
        }
    }

    std::vector<int> pending{selected_object_index};
    std::vector<bool> visited(objects.size(), false);
    for (size_t cursor = 0; cursor < pending.size(); ++cursor) {
        const int index = pending[cursor];
        if (index < 0 || index >= static_cast<int>(objects.size()) ||
            visited[index]) continue;
        visited[index] = true;
        const LevelObject& object = objects[index];
        if (object.deleted) continue;

        if (object.type == "AIGraph" && !object.taskId.empty())
            return object.taskId;
        if (object.aiGraphTaskId >= 0)
            return std::to_string(object.aiGraphTaskId);
        if (!object.graphId.empty())
            return object.graphId;

        pending.insert(pending.end(), object.childrenIndices.begin(),
                       object.childrenIndices.end());
        if (parent[index] >= 0)
            pending.push_back(parent[index]);
    }
    return {};
}

inline std::optional<glm::dvec3> FindRelatedGraphOrigin(
    const std::vector<LevelObject>& objects, int selected_object_index) {
    const std::string graph_task_id =
        FindRelatedGraphTaskId(objects, selected_object_index);
    if (graph_task_id.empty()) return std::nullopt;
    for (const LevelObject& candidate : objects) {
        if (!candidate.deleted && candidate.type == "AIGraph" &&
            candidate.taskId == graph_task_id)
            return candidate.pos;
    }
    return std::nullopt;
}

inline bool GraphOverlayMatchesSelection(
    bool has_selected_object,
    const std::string& related_graph_task_id,
    const std::string& overlay_graph_task_id) {
    // An empty relation means the selection is unrelated to the overlay; it
    // must not inherit a stale node from a previously selected AIGraph. Only
    // the no-selection case is allowed to keep using the visible overlay.
    return !has_selected_object ||
        (!related_graph_task_id.empty() &&
         related_graph_task_id == overlay_graph_task_id);
}

// Graph node coordinates are local to the AIGraph task. F11 therefore uses
// the selected node plus the overlay world offset; with no selected node it
// visits the graph origin, and otherwise preserves object teleport behavior.
inline GraphCameraTarget ResolveGraphCameraTarget(
    const GraphFile& graph,
    bool graph_visible,
    int selected_node_id,
    const glm::dvec3& graph_offset,
    const glm::dvec3& object_position,
    const std::optional<glm::dvec3>& related_graph_origin = std::nullopt) {
    if (graph_visible && selected_node_id >= 0) {
        if (const GraphNode* node = GRAPH_FindNode(graph, selected_node_id)) {
            return {GraphCameraTargetKind::GraphNode,
                    graph_offset + glm::dvec3(node->x, node->y, node->z),
                    node->id};
        }
    }

    if (graph_visible && graph.valid) {
        return {GraphCameraTargetKind::GraphOrigin, graph_offset, -1};
    }

    // F11 is also useful before the overlay is opened.  A selected soldier
    // still has a resolved AIGraph task, so use that task's world origin
    // instead of silently teleporting to the soldier itself.
    if (!graph_visible && related_graph_origin.has_value()) {
        return {GraphCameraTargetKind::GraphOrigin, *related_graph_origin, -1};
    }

    return {GraphCameraTargetKind::Object, object_position, -1};
}

// Apply the complete F11 selection policy before resolving the destination.
// Keeping the overlay identity check beside the target resolver prevents the
// keyboard handler from accidentally reusing a node from a previously
// selected graph.
inline GraphCameraTarget ResolveF11CameraTarget(
    const GraphFile& graph,
    bool has_selected_object,
    bool overlay_visible,
    int selected_node_id,
    const std::string& related_graph_task_id,
    const std::string& overlay_graph_task_id,
    const glm::dvec3& graph_offset,
    const glm::dvec3& object_position,
    const std::optional<glm::dvec3>& related_graph_origin = std::nullopt) {
    const bool overlay_matches = GraphOverlayMatchesSelection(
        has_selected_object, related_graph_task_id, overlay_graph_task_id);
    const bool use_overlay = overlay_visible && overlay_matches;
    return ResolveGraphCameraTarget(
        graph, use_overlay, use_overlay ? selected_node_id : -1,
        graph_offset, object_position, related_graph_origin);
}
