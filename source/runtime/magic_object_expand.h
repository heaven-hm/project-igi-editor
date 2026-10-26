/******************************************************************************
 * @file    magic_object_expand.h
 * @brief   Pure, host-portable magic-object expansion: walk a placed model's
 *          ATTA records and produce the child tasks retail IGI creates.
 *
 * Issue #67: the editor drew attachment nodes but never spawned the magic
 * objects the engine spawns for them, so a placed building came up empty.
 * The registry (`runtime/magic_object_registry.*`) already knows the
 * `DefineMagicObj` table; this module is the consumer that turns an attachment
 * NAME into the definition's MODEL and walks the graph the way the engine does.
 *
 * Every rule below cites IGI-RE (the verified decompilation of retail
 * IGI.exe) as `file:line`. Nothing here touches GL, the filesystem or Windows:
 * the model data is supplied through callbacks, so the whole rule set is
 * unit-testable on the host build.
 *****************************************************************************/
#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "runtime/magic_object_registry.h"

namespace igi {

// ─── Input: one ATTA record ─────────────────────────────────────────────────
// Same layout as `renderer/atta_children.h` documents (0x44 bytes:
// `char name[16]; float pos[3]; float m[9]; int unk40;`). The expansion only
// reads `name`; `pos`/`m` are carried onto the spawn so the caller can place it
// with the existing #37 helper `igi::IgiAttaChildWorld` instead of re-deriving
// the parent -> child frame here.
struct MagicObjectAttachment {
    std::string name;
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float m[9] = {1.0f, 0.0f, 0.0f,
                  0.0f, 1.0f, 0.0f,
                  0.0f, 0.0f, 1.0f};
};

// ─── The recursion rule ─────────────────────────────────────────────────────
// A spawned magic object expands ITS OWN attachments when its create handler
// calls `RigidDynCubeObj_CreateMagicObjs` (HOOK 0x4C4950). That is a per-task-
// type property, not a per-model one:
//
//   kAlways          the handler calls it unconditionally
//   kIfModelResolved the handler guards the call with `if (model)`
//   kNo              the handler never calls it
//
// The `*(int *)(model_info(model) + 0x98)` test in `Sector_CreateMagicObjs` is
// a SEPARATE decision: it only chooses whether the child is created as a child
// Sector (`gSectorType`) or as its own magic-object type
// (`src/shadow/sector.c:673-676`). Both paths then expand (the sector one via
// `Sector_Create` -> `RigidDynCubeObj_CreateMagicObjs`,
// `src/shadow/sector.c:183-195`), so the test adds expansions only for types
// whose handler does NOT expand on its own.
enum class MagicObjectChildExpansion {
    kNo,
    kAlways,
    kIfModelResolved,
};

// Case-sensitive lookup of the `TASKTYPE_*` name the registry stored. Unknown
// names return `kNo`: the editor never spawns a child for a type it cannot
// resolve, and guessing would invent objects the engine does not create.
MagicObjectChildExpansion MagicObjectTypeChildExpansion(const std::string& task_type_name);

// ─── Output: one spawned task ───────────────────────────────────────────────
struct MagicObjectSpawn {
    std::string attachment_name;   // ATTA name that produced it
    std::string parent_model;      // model whose ATTA list held the record
    std::string model_id;          // definition's model (`def->model`)
    std::string task_type_name;    // `TASKTYPE_*` of the spawned task
    int depth = 1;                 // 1 = direct child of the placed model
    bool child_is_sector = false;  // created as a child Sector, not a magic obj
    bool model_available = true;   // definition model resolved in the level
    float pos[3] = {0.0f, 0.0f, 0.0f};   // attachment local position, verbatim
    float m[9] = {1.0f, 0.0f, 0.0f,
                  0.0f, 1.0f, 0.0f,
                  0.0f, 0.0f, 1.0f};     // attachment local basis, verbatim
};

struct MagicObjectExpansionStats {
    std::size_t definitions = 0;       // entries in the registry
    std::size_t models_walked = 0;     // distinct models whose ATTA list was read
    std::size_t attachments_seen = 0;  // ATTA records examined
    std::size_t spawns = 0;            // tasks the engine would create
    std::size_t undefined_names = 0;   // attachments with no definition
    std::size_t missing_models = 0;    // definitions whose model did not resolve
    int max_depth = 0;                 // deepest spawn depth reached
    std::vector<std::string> undefined_name_list;
    std::vector<std::string> missing_model_list;
};

// ─── Input: the world the walk reads ───────────────────────────────────────
// All three callbacks are optional; an empty callback means "nothing known":
// no attachments, every model available, no model has sectors.
struct MagicObjectExpansionSource {
    // ATTA records of `model_id`, or nullptr when the model is unknown.
    std::function<const std::vector<MagicObjectAttachment>*(const std::string& model_id)> attachments_for;
    // Does `model_id` resolve to a model in the current level?
    std::function<bool(const std::string& model_id)> model_available;
    // Does `model_id`'s mesh have portals (rooms)? See the header note in the
    // .cpp: the engine tests the SectorMesh `portals` pointer at +0x98.
    std::function<bool(const std::string& model_id)> model_has_sectors;
};

struct MagicObjectExpansionResult {
    std::vector<MagicObjectSpawn> spawns;   // in engine creation order
    MagicObjectExpansionStats stats;
    // Human-readable one-line-per-fact summary used by the corpus test and by
    // the renderer's debug log.
    std::string Inventory() const;
};

// Safety net only: the engine has no depth guard (a cyclic ATTA graph would
// hang retail IGI). Level data is a shallow DAG, so 16 is far above anything
// observed; the walk stops there and reports the depth it reached.
constexpr int kMagicObjectMaxDepth = 16;

// Walk `root_model`'s attachments and return the tasks the engine creates for
// the object. `root_model` itself is not a spawn: it is the placed object.
MagicObjectExpansionResult ExpandMagicObjects(const MagicObjectRegistry& registry,
                                              const std::string& root_model,
                                              const MagicObjectExpansionSource& source,
                                              int max_depth = kMagicObjectMaxDepth);

} // namespace igi
