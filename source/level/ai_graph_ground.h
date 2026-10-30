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
 *   (src/graphnode/graph.c:65-69) and the attached branch (:345-362) computes,
 *   with parent pose P = parent->pos and M = parent->matrix.m:
 *
 *       world = P + M * (graph.pos + node.pos)
 *
 *   in this order: node + graph pos first (:351-355), then M (:356-358), then
 *   P (:360-362).  M is row-major: world.x = m[0]*x + m[1]*y + m[2]*z,
 *   world.y = m[3]*x + m[4]*y + m[5]*z, world.z = m[6]*x + m[7]*y + m[8]*z.
 *   The parent is the graph's DIRECT parent task, not an ancestor.
 *
 *   IS THE PARENT POSE AUTHORED OR RUNTIME?
 *   For the four parent types in the shipped data -- Heli, Car, Train, Building
 *   -- the pose at the human's FIRST probe is the parent's AUTHORED START POSE,
 *   not a value left over from later simulation:
 *
 *     * Heli_ParamsChanged HOOK 0x432950 (src/heli/heli.c:621-651) and
 *       Car_ParamsChanged HOOK 0x42F530 (src/heli/car.c:59-100) run from the
 *       same Level_Load Param_GetChangedEvent broadcast that binds the AI node,
 *       and unconditionally do `DynCube_Move(world, h, &startPos)` then
 *       `matrix = startMatrix` (heli.c:643-644, car.c:91-92).  After level load
 *       every Heli/Car is at its authored ObjectPos + Real32x9 angles.
 *     * The AnimTask can only re-assert that pose before the first probe.  Its
 *       Param_GetChangedEvent handler calls AnimTask_Seek(t, 0)
 *       (src/vehbase/animtask.c:235-243), and every Seek/Step/SendControl
 *       before the second frame sends tick 0 -- AnimTask_Step HOOK 0x4F3F60
 *       runs BEFORE AnimTask_Seek bumps t->tick (src/vehbase/animtask.c:403-410)
 *       -- which is the reset branch: Heli_Control HOOK 0x431B70
 *       (src/heli/heli.c:358-406) and Car_Control HOOK 0x42EA10
 *       (src/rotor/car.c:489-527) for tick 0 do DynCube_Move(startPos) and
 *       matrix = startMatrix and do NOT move (Heli_Move/Car_Drive run only for
 *       tick > 0).  A vehicle therefore cannot move before the frame-1 probe;
 *       from the second frame on the human is already a platform rider
 *       (Human_ProbeGround's `Human_SetPlatform(h, path->task.parent)`,
 *       src/humanbase/human.c:1746-1750) and Soldier_Update calls
 *       Human_MoveOnPlatform instead of Human_UpdateMove when `h->attach` is
 *       set (src/soldier/soldier.c:878-881), so the probe is not run again.
 *     * Building is a SectorObject: its pos/matrix ARE the authored
 *       Position/Orientation and nothing ever animates it, so its pose is the
 *       same at every instant.
 *
 *   The START pose is therefore what the engine reads, and it is derivable from
 *   the level file for Heli/Car/Building.  Train is NOT: a Train has no
 *   ObjectPos at all (its ParamTable is start_dist / start_speed / path / ...
 *   src/missile/train.c:384-393); its start pose is SplineObj_GetPoints(path,
 *   dist = start_dist) (Train_Place HOOK 0x42A2C0, src/missile/train.c:202-246),
 *   which this header does not evaluate, so Train keeps the authored placement
 *   Z (see GraphParentPoseIsAuthoredAtProbe).
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

#include <cmath>
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

// The engine's Matrix_SetEuler HOOK 0x4B38E0 (src/math/matrix.c:99-108),
// row-major and composed M = Rz(az) * Ry(ay) * Rx(ax) (Mul is
// src/math/matrix.c:8-20; the three SetRot* helpers are :112-128, :130-146 and
// :148-164).  Used only for a parent's AUTHORED Real32x9, which
// ParamReal32x9_Parse (0x4EFBA0, src/physobj/paramops.c:423-435) reads as three
// Euler angles and turns into exactly this matrix before the engine ever stores
// it in a RigidDynCubeObj.
struct PoseMatrix {
    double m[9];
};

inline PoseMatrix PoseMatrixSetEuler(double ax, double ay, double az) {
    const double cx = std::cos(ax), sx = std::sin(ax);
    const double cy = std::cos(ay), sy = std::sin(ay);
    const double cz = std::cos(az), sz = std::sin(az);
    const double rx[9] = {1.0, 0.0, 0.0, 0.0, cx, -sx, 0.0, sx, cx};
    const double ry[9] = {cy, 0.0, sy, 0.0, 1.0, 0.0, -sy, 0.0, cy};
    const double rz[9] = {cz, -sz, 0.0, sz, cz, 0.0, 0.0, 0.0, 1.0};
    double rzy[9];
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            rzy[i * 3 + j] = rz[i * 3] * ry[j] + rz[i * 3 + 1] * ry[3 + j] +
                             rz[i * 3 + 2] * ry[6 + j];
    PoseMatrix out{};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++)
            out.m[i * 3 + j] = rzy[i * 3] * rx[j] + rzy[i * 3 + 1] * rx[3 + j] +
                               rzy[i * 3 + 2] * rx[6 + j];
    return out;
}

// Graph_GetNodePos's ATTACHED branch (HOOK 0x4F9770, src/graphnode/graph.c:
// 345-362) for a graph whose DIRECT parent is a rigid task with an authored
// start pose (GraphParentPoseIsAuthoredAtProbe):
//
//   world = parent.pos + M * (graph.pos + node.pos)
//
// `parent_pos` / `parent_euler` are the parent's authored ObjectPos and
// Real32x9 angles (Heli/Car startPos+startMatrix, Building Position+
// Orientation).  M is orthonormal, so the nearest-node search is done by
// pulling the human back through M^T into the graph's own frame -- the same
// nodes, the same score, the same VIEW-node skip that GRAPH_NearestNode already
// transcribes from AIGraph_FindNearest 0x5096F0 -- and the selected node's
// world Z is then evaluated with the engine's own expression.
inline AiGraphGround AiGraphGroundZAttached(const GraphFile& graph,
                                            const double graph_origin[3],
                                            const double parent_pos[3],
                                            const double parent_euler[3],
                                            const double human_pos[3]) {
    AiGraphGround result;
    if (!graph.valid || graph.nodes.empty()) return result;

    const PoseMatrix M = PoseMatrixSetEuler(parent_euler[0], parent_euler[1], parent_euler[2]);
    const double dx = human_pos[0] - parent_pos[0];
    const double dy = human_pos[1] - parent_pos[1];
    const double dz = human_pos[2] - parent_pos[2];
    // M^T * (human - parent.pos) - graph.pos, i.e. the human in graph space.
    const double local[3] = {
        M.m[0] * dx + M.m[3] * dy + M.m[6] * dz - graph_origin[0],
        M.m[1] * dx + M.m[4] * dy + M.m[7] * dz - graph_origin[1],
        M.m[2] * dx + M.m[5] * dy + M.m[8] * dz - graph_origin[2],
    };
    const int node_id = GRAPH_NearestNode(graph, local[0], local[1], local[2]);
    if (node_id < 0) return result;
    const GraphNode* node = GRAPH_FindNode(graph, node_id);
    if (node == nullptr) return result;

    result.found = true;
    result.node_id = node_id;
    result.node_local_z = node->z;
    result.world_z = parent_pos[2] +
                     M.m[6] * (graph_origin[0] + node->x) +
                     M.m[7] * (graph_origin[1] + node->y) +
                     M.m[8] * (graph_origin[2] + node->z);
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
// the caller must then decide whether the parent's authored start pose is
// available (GraphParentPoseIsAuthoredAtProbe) or keeps the authored Z.
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

// True when the graph's DIRECT parent has an authored start pose that the
// engine has ALREADY APPLIED before the human's first probe, so
// AiGraphGroundZAttached can evaluate Graph_GetNodePos's attached branch from
// level data alone:
//
//   Heli      startPos + startMatrix, ObjectPos + Real32x9 at QSC args 3..8
//             (Heli_Init 0x4310F0, src/heli/heli.c:186-187), applied at load by
//             Heli_ParamsChanged 0x432950 (:643-644, :653).
//   Car       the same fields and the same reset (Car_Init HOOK 0x42DCF0, the
//             table at src/rotor/car.c:263-264; Car_ParamsChanged HOOK 0x42F530,
//             src/heli/car.c:91-92).
//   Building  Position + Orientation (Building->SectorObject: src/sector/
//             sector.c:901, :895); a static SectorObject whose matrix/pos are
//             the authored values for the whole level.
//
// The header comment explains why the first probe cannot see later motion: the
// vehicle is reset to this pose at level load and the AnimTask's tick-0 step can
// only repeat the reset, while the human becomes a platform rider on the same
// probe.  Anything else (notably Train, whose pose is SplineObj_GetPoints at
// start_dist rather than an authored ObjectPos) has no statically derivable
// pose here and must keep the authored placement Z.
inline bool GraphParentPoseIsAuthoredAtProbe(const std::string& parent_type) {
    return parent_type == "Heli" || parent_type == "Car" || parent_type == "Building";
}

}  // namespace igi
