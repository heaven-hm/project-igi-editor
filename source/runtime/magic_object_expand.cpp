/******************************************************************************
 * @file    magic_object_expand.cpp
 * @brief   The expansion rule from IGI-RE, implemented once and cited in place.
 *
 * Retail spawns magic objects from a placed object in
 * `Sector_CreateMagicObjs` (HOOK 0x4DFA50, `src/shadow/sector.c:538-687`), and
 * from every magic object that is itself created, through
 * `RigidDynCubeObj_CreateMagicObjs` (HOOK 0x4C4950,
 * `src/magicobj/rigidcoll.c:383-496`).
 *
 * The engine's rules, in the order it applies them:
 *
 *  1. `att->name` is looked up in the DefineMagicObj table by exact `strcmp`
 *     (`src/shadow/sector.c:656-658`, `src/magicobj/rigidcoll.c:471-473`).
 *     No match: `Sector_CreateMagicObjs` warns and skips the record
 *     (`:659-663`); `RigidDynCubeObj_CreateMagicObjs` calls `Error_Show` and
 *     then spins forever (`:475-480`) — a retail hang this editor must not
 *     copy.
 *  2. The spawn's model is the DEFINITION's model, never the attachment name:
 *     `arg.model = def->model` (`src/shadow/sector.c:667`), and the same in
 *     `src/magicobj/rigidcoll.c:484`. In the retail table every definition has
 *     `model == name` (measured: 374/374), so this is observationally
 *     identical on the shipped data — it is still the documented rule.
 *  3. At the top level the model must resolve: `VirModel_Find(def->model)`,
 *     and a miss warns and skips (`src/shadow/sector.c:668-672`).
 *     `RigidDynCubeObj_CreateMagicObjs` has no such check and always creates
 *     the task (`:485`); the spawned task's own create handler then decides.
 *  4. The child is created as a child Sector (`gSectorType`) exactly when the
 *     spawned model's mesh has portals —
 *     `if (*(int *)(model_info(model) + 0x98))` (`src/shadow/sector.c:673-676`).
 *     `model_info` is the LOD-0 mesh block (`src/shadow/sector.c:387-392`) and
 *     +0x98 is its `Portal *portals` field (`src/sector/sector.h:37`; the same
 *     struct's `center` at +0x50 is what `model_bounds` returns,
 *     `src/shadow/sector.c:395-398`). Otherwise the child is created as its
 *     definition's own type.
 *  5. Both paths expand the child's own ATTA list exactly once: the sector via
 *     `Sector_Create` -> `RigidDynCubeObj_CreateMagicObjs`
 *     (`src/shadow/sector.c:183-195`), the magic object via its create
 *     handler. Recursion therefore continues only while spawned definitions
 *     are of a type whose create handler expands (see the table below).
 *  6. The event-3 handler the engine then calls (`src/shadow/sector.c:682-684`)
 *     is registered for `TASKTYPE_MAGICOBJ` as `TASK_NOTHING`
 *     (`src/haze/magic.c:88`) and is absent for `gSectorType`
 *     (`src/sector/sector.c:895-901`), so it never spawns anything itself.
 *****************************************************************************/
#include "runtime/magic_object_expand.h"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

namespace igi {
namespace {

struct TypeExpansionEntry {
    const char* type_name;
    MagicObjectChildExpansion behaviour;
};

// ─── Which task types expand their own attachments ──────────────────────────
// Derived mechanically from IGI-RE: for every `TASKTYPE_*` the retail table
// uses, `Script_RegisterConstant("TASKTYPE_X", gXType)` gives the task type,
// `TaskType_SetHandler(gXType, 0, Handler)` gives its create handler (slot 0),
// and the handler expands when its body calls
// `RigidDynCubeObj_CreateMagicObjs`. 13 of the 28 types in MAGICOBJ.QVM
// qualify; the other 15 do not (and one, TASKTYPE_SCAMERALENSVIEWCONE, has no
// create handler in the decompilation at all, so it is treated as kNo).
constexpr TypeExpansionEntry kTypeExpansion[] = {
    // registration                              create handler          call site
    {"TASKTYPE_AISTATIONARYGUN",    MagicObjectChildExpansion::kAlways}, // binocular/stationarygun.c:194 -> :149
    {"TASKTYPE_ALARMLIGHTREFLECTOR", MagicObjectChildExpansion::kAlways}, // fxobj/alarm.c:117 -> :148
    {"TASKTYPE_CABARREL",           MagicObjectChildExpansion::kAlways}, // statgun/gunca.c:67 -> :100
    {"TASKTYPE_CACLIP",             MagicObjectChildExpansion::kAlways}, // statgun/gunca.c:74 -> :100
    {"TASKTYPE_EXPLODEMAGICOBJ",    MagicObjectChildExpansion::kAlways}, // elevator/explodemagicobj.c:97 -> :84
    {"TASKTYPE_GLASS",              MagicObjectChildExpansion::kAlways}, // glass/glass.c:103 -> :84
    {"TASKTYPE_GUNCLIP",            MagicObjectChildExpansion::kAlways}, // guncasing/gunpart.c:79 -> :226
    {"TASKTYPE_GUNMOVINGPART",      MagicObjectChildExpansion::kAlways}, // guncasing/gunpart.c:92 -> :226
    {"TASKTYPE_GUNX2",              MagicObjectChildExpansion::kAlways}, // comprun/gunx2.c:48 -> :71
    {"TASKTYPE_LADDER",             MagicObjectChildExpansion::kAlways}, // props/ladder.c:109 -> :85
    {"TASKTYPE_RADARDISH",          MagicObjectChildExpansion::kAlways}, // glass/radardish.c:111 -> :90
    {"TASKTYPE_ROTOR",              MagicObjectChildExpansion::kAlways}, // rotor/rotor.c:196 -> :105
    // The one guarded handler: `if (model) RigidDynCubeObj_CreateMagicObjs(...)`
    // (`src/haze/magic.c:108-109`); without a model it warns (`:99-100`) and
    // creates no children.
    {"TASKTYPE_MAGICOBJ",           MagicObjectChildExpansion::kIfModelResolved}, // haze/magic.c:85 -> :108-109
};

struct WalkState {
    const MagicObjectRegistry* registry = nullptr;
    const MagicObjectExpansionSource* source = nullptr;
    MagicObjectExpansionResult* out = nullptr;
    int max_depth = kMagicObjectMaxDepth;
    std::vector<std::string> path;   // cycle guard; the engine has none
};

void AddUnique(std::vector<std::string>& list, const std::string& value) {
    if (std::find(list.begin(), list.end(), value) == list.end()) {
        list.push_back(value);
    }
}

void WalkModel(WalkState& state, const std::string& model_id, int depth);

void WalkModel(WalkState& state, const std::string& model_id, int depth) {
    if (depth >= state.max_depth) {
        return;
    }
    if (std::find(state.path.begin(), state.path.end(), model_id) != state.path.end()) {
        // A cyclic ATTA graph would make retail IGI recurse forever; the editor
        // stops instead of hanging.
        return;
    }

    const std::vector<MagicObjectAttachment>* attachments = nullptr;
    if (state.source->attachments_for) {
        attachments = state.source->attachments_for(model_id);
    }
    if (attachments == nullptr || attachments->empty()) {
        return;
    }

    state.path.push_back(model_id);
    ++state.out->stats.models_walked;

    for (const MagicObjectAttachment& attachment : *attachments) {
        ++state.out->stats.attachments_seen;

        // Rule 1: exact-name lookup in the DefineMagicObj table.
        const MagicObjectDefinition* definition = state.registry->Find(attachment.name);
        if (definition == nullptr) {
            ++state.out->stats.undefined_names;
            AddUnique(state.out->stats.undefined_name_list, attachment.name);
            continue;
        }

        // Rule 3: a top-level spawn needs its model; a nested one is created
        // regardless and only its expansion depends on the model resolving.
        bool available = true;
        if (state.source->model_available) {
            available = state.source->model_available(definition->model_id);
        }
        if (!available) {
            ++state.out->stats.missing_models;
            AddUnique(state.out->stats.missing_model_list, definition->model_id);
            if (depth == 0) {
                continue;
            }
        }

        MagicObjectSpawn spawn;
        spawn.attachment_name = attachment.name;
        spawn.parent_model = model_id;
        spawn.model_id = definition->model_id;
        spawn.task_type_name = definition->task_type_name;
        spawn.depth = depth + 1;
        spawn.model_available = available;
        for (int i = 0; i < 3; ++i) {
            spawn.pos[i] = attachment.pos[i];
        }
        for (int i = 0; i < 9; ++i) {
            spawn.m[i] = attachment.m[i];
        }
        // Rule 4: the portal test only chooses the child's container/handler,
        // and only the top-level walk has it.
        if (depth == 0 && state.source->model_has_sectors) {
            spawn.child_is_sector = state.source->model_has_sectors(definition->model_id);
        }

        state.out->spawns.push_back(spawn);
        ++state.out->stats.spawns;
        state.out->stats.max_depth = std::max(state.out->stats.max_depth, spawn.depth);

        // Rule 5: does the spawned task expand its own attachments?
        bool expands = false;
        if (spawn.child_is_sector) {
            // Sector_Create -> RigidDynCubeObj_CreateMagicObjs (sector.c:190).
            expands = true;
        } else {
            switch (MagicObjectTypeChildExpansion(definition->task_type_name)) {
            case MagicObjectChildExpansion::kAlways:
                expands = true;
                break;
            case MagicObjectChildExpansion::kIfModelResolved:
                expands = available;
                break;
            case MagicObjectChildExpansion::kNo:
                break;
            }
        }

        if (expands) {
            WalkModel(state, definition->model_id, depth + 1);
        }
    }

    state.path.pop_back();
}

} // namespace

MagicObjectChildExpansion MagicObjectTypeChildExpansion(const std::string& task_type_name) {
    for (const TypeExpansionEntry& entry : kTypeExpansion) {
        if (task_type_name == entry.type_name) {
            return entry.behaviour;
        }
    }
    return MagicObjectChildExpansion::kNo;
}

std::string MagicObjectExpansionResult::Inventory() const {
    std::ostringstream out;
    out << "definitions=" << stats.definitions
        << " models_walked=" << stats.models_walked
        << " attachments_seen=" << stats.attachments_seen
        << " spawns=" << stats.spawns
        << " undefined_names=" << stats.undefined_names
        << " missing_models=" << stats.missing_models
        << " max_depth=" << stats.max_depth << "\n";

    std::vector<std::pair<std::string, std::size_t>> by_type;
    std::vector<std::pair<std::string, std::size_t>> by_model;
    for (const MagicObjectSpawn& spawn : spawns) {
        auto bump = [](std::vector<std::pair<std::string, std::size_t>>& v, const std::string& key) {
            for (auto& kv : v) {
                if (kv.first == key) {
                    ++kv.second;
                    return;
                }
            }
            v.emplace_back(key, 1);
        };
        bump(by_type, spawn.task_type_name);
        bump(by_model, spawn.model_id);
    }
    std::sort(by_type.begin(), by_type.end());
    std::sort(by_model.begin(), by_model.end());

    out << "spawns_by_type:";
    for (const auto& kv : by_type) {
        out << " " << kv.first << "=" << kv.second;
    }
    out << "\n";
    out << "spawns_by_model:";
    for (const auto& kv : by_model) {
        out << " " << kv.first << "=" << kv.second;
    }
    out << "\n";

    std::size_t index = 0;
    for (const MagicObjectSpawn& spawn : spawns) {
        out << "spawn[" << index++ << "] depth=" << spawn.depth
            << " attachment=" << spawn.attachment_name
            << " parent=" << spawn.parent_model
            << " model=" << spawn.model_id
            << " type=" << spawn.task_type_name
            << " sector=" << (spawn.child_is_sector ? 1 : 0)
            << " model_available=" << (spawn.model_available ? 1 : 0)
            << "\n";
    }

    if (!stats.undefined_name_list.empty()) {
        out << "undefined:";
        for (const std::string& name : stats.undefined_name_list) {
            out << " " << name;
        }
        out << "\n";
    }
    if (!stats.missing_model_list.empty()) {
        out << "missing:";
        for (const std::string& model : stats.missing_model_list) {
            out << " " << model;
        }
        out << "\n";
    }
    return out.str();
}

MagicObjectExpansionResult ExpandMagicObjects(const MagicObjectRegistry& registry,
                                              const std::string& root_model,
                                              const MagicObjectExpansionSource& source,
                                              int max_depth) {
    MagicObjectExpansionResult result;
    result.stats.definitions = registry.GetDefinitions().size();

    if (max_depth <= 0) {
        return result;
    }

    WalkState state;
    state.registry = &registry;
    state.source = &source;
    state.out = &result;
    state.max_depth = max_depth;
    WalkModel(state, root_model, 0);
    return result;
}

} // namespace igi
