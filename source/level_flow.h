// level_flow.h - Mission objective evaluation, flow state, and extraction logic
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace igi {

enum class ObjectiveState {
    Pending,
    Completed,
    Failed,
    Cancelled
};

// OpenIGI resolves each objective's LinkTaskId to the authored task position
// for the map-computer presentation. Keep this value type renderer-free so
// level-flow tests and the Windows presentation adapter share one contract.
struct MissionObjectiveLocation {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct MissionObjective {
    uint32_t id = 0;
    std::string description;
    std::string text_resource;
    int32_t link_task_id = -1;
    std::string completion_expression;
    std::string failure_expression;
    bool is_primary = true;
    ObjectiveState state = ObjectiveState::Pending;
    bool has_location = false;
    MissionObjectiveLocation location;
};

struct AuthoredMissionObjectiveDefinition {
    std::string text_resource;
    int32_t link_task_id = -1;
    std::string completion_expression;
    std::string failure_expression;
};

struct AuthoredMissionObjectiveSet {
    std::string valid_expression;
    std::vector<AuthoredMissionObjectiveDefinition> objectives;
};

// LevelFlow is separate from map-computer objectives in vanilla IGI1. It
// owns the authored mission result expressions and the optional interface
// countdown metadata.
struct AuthoredMissionFlowDefinition {
    double start_time_seconds = 0.0;
    std::string complete_expression;
    std::string failure_expression;
    bool interface_timer_enabled = false;
    double maximum_level_play_time_seconds = 0.0;
    bool has_level_flow = false;
};

using MissionObjectiveTextResolver = std::function<std::string(const std::string&)>;
using MissionObjectiveLocationResolver = std::function<bool(
    int32_t link_task_id,
    MissionObjectiveLocation& location)>;
using MissionExpressionEvaluator = std::function<bool(const std::string&)>;

enum class MissionStatus {
    InProgress,
    Success,
    Failed
};

class LevelFlow {
public:
    LevelFlow();

    void InitializeMission(uint32_t mission_number);
    void InitializeMission(
        uint32_t mission_number,
        const std::vector<AuthoredMissionObjectiveSet>& authored_objective_sets,
        MissionObjectiveTextResolver text_resolver = {},
        const AuthoredMissionFlowDefinition& authored_flow = {},
        MissionObjectiveLocationResolver location_resolver = {});
    void AddObjective(uint32_t id, const std::string& desc, bool is_primary = true);
    void SetObjectiveState(uint32_t id, ObjectiveState state);
    bool CompleteFirstPendingPrimaryObjective();

    void Update(
        bool player_alive,
        bool in_extraction_zone,
        MissionExpressionEvaluator expression_evaluator = {});

    MissionStatus GetStatus() const { return status_; }
    const std::vector<MissionObjective>& GetObjectives() const { return objectives_; }
    uint32_t GetMissionNumber() const { return mission_number_; }

    // The single line the HUD objective card shows, taken from the engine's own
    // sources -- never from an editor-invented string. See level_flow.cpp for
    // the per-branch citations:
    //   Failed    -> the StatusMessage text key MISSION_FAILED
    //   Success   -> the StatusMessage text key MISSION_COMPLETE
    //                (both flat in LANGUAGE:<lang>/MESSAGES.RES)
    //   otherwise -> the pending primary objective's own resource text
    //                (LANGUAGE:<lang>/objectives.res), or, once the set is
    //                finished, the engine's composed map-computer status line
    //                "<OBJECTIVE> <n>: <COMPLETE|FAILED|INCOMPLETE>".
    // A key no mounted archive defines renders the engine's "RM: <key>".
    std::string GetObjectiveDisplayText() const;
    bool HasAuthoredMissionFlow() const { return has_authored_mission_flow_; }
    bool IsInterfaceTimerEnabled() const {
        return authored_mission_flow_.interface_timer_enabled;
    }
    double GetMaximumLevelPlayTimeSeconds() const {
        return authored_mission_flow_.maximum_level_play_time_seconds;
    }
    uint64_t GetMissionFlowTick() const { return mission_flow_tick_; }

private:
    void InitializeFallbackObjectives(uint32_t mission_number);
    void LoadAuthoredObjectiveSet(size_t authored_set_index);
    bool AdvanceAuthoredObjectiveSet();
    void SelectValidAuthoredObjectiveSet(
        const MissionExpressionEvaluator& expression_evaluator);
    void EvaluateAuthoredObjectiveExpressions(
        const MissionExpressionEvaluator& expression_evaluator);
    bool EvaluateExpression(
        const std::string& expression,
        const MissionExpressionEvaluator& expression_evaluator) const;
    bool EvaluateAuthoredMissionFlow(
        const MissionExpressionEvaluator& expression_evaluator);

    // Resolve one bare mission text key the way the engine does (a flat lookup
    // over the mounted language archives, performed by the injected resolver).
    // With no resolver injected there is no archive to look in, so the engine's
    // own miss rendering "RM: <key>" is returned -- the raw key is never shown.
    std::string ResolveObjectiveText(const std::string& resource_key) const;

    // ComputerObjectives_Draw 0x469110 (src/compmap/objectives.c:137-145)
    // composes one line per objective: "<OBJECTIVE> <n>: " followed by the
    // objective's state word (FAILED / COMPLETE / INCOMPLETE).
    std::string EngineObjectiveStatusLine(const MissionObjective& objective) const;

    uint32_t mission_number_ = 1;
    std::vector<MissionObjective> objectives_;
    MissionStatus status_ = MissionStatus::InProgress;
    std::vector<AuthoredMissionObjectiveSet> authored_objective_sets_;
    MissionObjectiveTextResolver objective_text_resolver_;
    MissionObjectiveLocationResolver objective_location_resolver_;
    size_t active_authored_objective_set_index_ = 0;
    AuthoredMissionFlowDefinition authored_mission_flow_;
    bool has_authored_mission_flow_ = false;
    uint64_t mission_flow_tick_ = 0;
};

} // namespace igi
