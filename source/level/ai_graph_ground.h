/******************************************************************************
 * @file    ai_graph_ground.h
 * @brief   Host-portable transcription of the ENGINE's Z for a human that has
 *          an AI controller, used by App::SnapObjectsToTerrain.
 *
 * THE ENGINE RULE (IGI-RE, /Users/haseeb-mir/Documents/Code/IGI-RE, read-only)
 *   Human_ProbeGround HOOK 0x462390 (src/humanbase/human.c:1676-1857) splits on
 *   `h->ai` at :1697:
 *
 *     * h->ai == NULL -> the collision query (:1752-1773) and, on a hit,
 *       `to[2] = foot[2] + hits[0].pos.z` (:1816).  This is the branch the
 *       editor has always transcribed (collision_drop_rules.h /
 *       collision_world.h).
 *
 *     * h->ai != NULL -> the collision query does NOT run.  With
 *       `ai->graph == NULL` the hits stay zeroed and the human is not grounded
 *       (:1701-1702 -> :1806-1813); otherwise the probe records the AI node as
 *       the ground (:1706-1711) and, while the move queue is empty
 *       (`List_Count(&ai->moves) <= 0`, :1713), its Z is the node's WORLD Z:
 *       `Graph_GetNodePos(d, node); hits[0].pos.z = d[2];` (:1714-1717).
 *       The move-queue case (:1719-1745) interpolates `h->pathCur`/`h->pathPrev`
 *       -- the AI's RUNTIME progress along its patrol, which no static placement
 *       has.  This header covers the level-load / no-path case only.
 *
 *   `ai->node` is not authored on the placement; it is chosen ONCE at level
 *   load.  Level_Load HOOK 0x4F0D40 (src/level/level.c:348-370) runs
 *   objects.qsc and then broadcasts Param_GetChangedEvent over the whole level
 *   (:364).  That reaches HumanAI_ParamsChanged HOOK 0x4505D0
 *   (src/humanai/humanai.c:724-765): `ai->graph = Graph_Find(ai->graphId)`
 *   (:739-740) and, from the human's authored ObjectPos (:746),
 *   `AIGraph_FindNearest(&near, ai->graph, pos); ai->node = *near` (:748-756).
 *   So the node is the nearest one to the AUTHORED placement.  The move queue
 *   is empty when the first probe runs: HumanAI_Create initialises it
 *   (src/humanai/humanai.c:62-77, list at :75-77), and the first HumanAI_Update
 *   sends the human its control message BEFORE it runs the create event or the
 *   AI script (:267-273).  The send is synchronous -- HumanAI_SendControl calls
 *   the human's Vehicle_GetControlEvent handler directly (:208-219), which is
 *   Soldier_InputHandler -> Soldier_Update -> Human_UpdateMove
 *   (src/soldier/soldier.c:846-853, :879) -- so the first Human_ProbeGround of
 *   an AI human sees List_Count(&ai->moves) == 0 and takes the node-Z branch.
 *
 *   AIGraph_FindNearest HOOK 0x5096F0 (src/sysmisc/aigraph.c:296-337) is a
 *   linear scan: skip nodes with the VIEW criteria (:311), score
 *   `distance - node->radius * kRadiusScale` with kRadiusScale = 4096
 *   (:70, :314), keep the smallest (:316-334).  There is NO distance cutoff:
 *   the three candidate slots start at kNearMax (:71, :300) but every one is
 *   overwritten by the first three nodes, so a far node still wins when it is
 *   the nearest.  GRAPH_NearestNode (renderer/graph_writer.h) is the editor's
 *   existing transcription of exactly this function -- this header calls it
 *   instead of writing a second one.
 *
 *   Graph_GetNodePos HOOK 0x4F9770 (src/graphnode/graph.c:341-370) adds the
 *   graph task's own authored position: `*out = node->pos; out->x += g->pos.x;
 *   ...` (:363-367) for an UNATTACHED graph.  When the graph's DIRECT parent is
 *   a RigidDynCubeObj, `g->attached` is set at Graph_Create
 *   (src/graphnode/graph.c:65-69) and the attached branch (:345-362) applies
 *   the parent's matrix and position -- the parent's POSE, which for a Heli,
 *   Car or Train is runtime state.  The editor claims an answer only for the
 *   unattached case; see GraphParentIsContainer.
 *
 * WHY obj.pos.z = hits[0].pos.z CARRIES OVER
 *   The engine's `to[]` is the human's OBJECT ORIGIN, which it draws with the
 *   skeleton's root at that origin and the mesh hanging below it
 *   (BoneDynCubeObj_EnterHandler HOOK 0x5124C0, src/glowmesh/bonedynd3d.c:36-61;
 *   BoneObj_Update HOOK 0x4D38C0, src/boneobj/boneobj.c:371-419, root pos
 *   {0,0,0}; the editor's own measurement lives in
 *   source/renderer/human_seat_frame.h).  The editor's human mesh is baked into
 *   a FEET frame instead (SkinSkinnedVertices, source/renderer/mef_native.cpp),
 *   so the engine's `foot[2]` term is the same in the collision and the graph
 *   branch and cancels in the mapping: the lowest point of the editor's mesh
 *   goes where `hits[0].pos.z` says.  The AI branch only changes WHERE that Z
 *   comes from: the nearest graph node instead of a collision hit.
 *****************************************************************************/
#pragma once

#include <string>

#include "renderer/graph_writer.h"

namespace igi {

// What Human_ProbeGround's AI branch would use as the human's ground.
struct AiGraphGround {
    bool found = false;       // false: ai->node stays -1, the Z is left alone
    int node_id = -1;         // ai->node
    double node_local_z = 0.0;  // node->pos.z in graph-local units
    double world_z = 0.0;     // Graph_GetNodePos(node).z for an unattached graph
};

// The engine's "human with an AI controller, no queued move" ground for an
// UNATTACHED graph:
//   ai->node       = AIGraph_FindNearest(ai->graph, human authored position)
//   ground z       = Graph_GetNodePos(node).z = graph->pos.z + node->pos.z
//   Human_ProbeGround then uses it at :1714-1717, and `to[2] = foot[2] +
//   hits[0].pos.z` (:1816) puts the module's contact point there.
//
// `graph_origin` is the AIGraph task's authored ObjectPos -- the same field
// Graph_GetWorldPos treats as the graph position (graphtask/graph.c:114).
// The search is done in graph-local coordinates because an unattached graph's
// origin is a pure translation, so `|worldNode - worldHuman|` is unchanged by
// subtracting it; the returned world Z adds it back.
inline AiGraphGround AiGraphGroundZ(const GraphFile& graph,
                                    const double graph_origin[3],
                                    const double human_pos[3]) {
    AiGraphGround result;
    if (!graph.valid || graph.nodes.empty()) return result;

    const int node_id = GRAPH_NearestNode(graph,
                                          human_pos[0] - graph_origin[0],
                                          human_pos[1] - graph_origin[1],
                                          human_pos[2] - graph_origin[2]);
    if (node_id < 0) return result;
    const GraphNode* node = GRAPH_FindNode(graph, node_id);
    if (node == nullptr) return result;

    result.found = true;
    result.node_id = node_id;
    result.node_local_z = node->z;
    result.world_z = graph_origin[2] + node->z;
    return result;
}

// True when a graph's DIRECT parent is a task the engine does NOT attach to,
// i.e. a plain container.  The engine's test is
// `TaskType_IsA(parent->type, RigidDynCubeObj_GetType())`
// (Graph_Create HOOK 0x4F90B0, src/graphnode/graph.c:65-69).
//
// The container types that can own an AIGraph are all root-level task classes:
//   Container              src/editcam/container.c:31   (Task_GetRootType)
//   ConditionalContainer   src/mission/conditionalcontainer.c:46 (-> Container)
//   Level, Static, Dynamic, Idle, CameraBase
//                          src/physobj/paramops.c:207-216 (Task_GetRootType)
//   Game                   src/game/game.c:249          (Task_GetRootType)
//
// In the three retail levels whose objects.qsc is unpacked and measured for
// this rule (level1, LEVEL2, LEVEL4: 104 AI placements), every AIGraph is
// either a child of `Container` (unattached; 86 placements) or of a
// Heli/Car/Train/Building (attached; 18).  Those parents derive from
// RigidDynCubeObj: Vehicle->PhysicsObj->RigidDynCubeObj
// (src/vehbase/vehicle.c:95, src/graphtask/physobj.c:64), Heli/Car/Train->
// Vehicle (src/heli/heli.c:162, src/rotor/car.c:238, src/missile/train.c:361),
// Building->SectorObject->Sector->RigidDynCubeObj (src/glass/building.c:50,
// src/sector/sector.c:901, :895).
//
// Anything this predicate does not recognise is treated as possibly attached:
// the caller keeps the authored Z rather than inventing a node position from a
// graph origin whose meaning depends on the parent's pose.
inline bool GraphParentIsContainer(const std::string& parent_type) {
    return parent_type.empty() ||          // top-level task: parent is the Level task
           parent_type == "Container" ||
           parent_type == "ConditionalContainer" ||
           parent_type == "Level" ||
           parent_type == "Static" ||
           parent_type == "Dynamic" ||
           parent_type == "Idle" ||
           parent_type == "CameraBase" ||
           parent_type == "Game";
}

}  // namespace igi
