// test_runtime_subsystems.cpp - Unit and integration tests for C++ Game Mode Runtime Subsystems
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "../source/game_clock.h"
#include "../source/level/qvm_interpreter.h"
#include "../source/level/qvm_ai_bindings.h"
#include "../source/level/qvm_native_registry.h"
#include "../source/level/task_tree.h"
#include "../source/player_controller.h"
#include "../source/player_fall_impact.h"
#include "../source/player_collision.h"
#include "../source/player_motion.h"
#include "../source/player_ladder.h"
#include "../source/player_separation.h"
#include "../source/player_animation_driver.h"
#include "../source/weapon_system.h"
#include "../source/weapon_view_sway.h"
#include "../source/weapon_view_recoil.h"
#include "../source/ai_system.h"
#include "../source/animation_motion.h"
#include "../source/level_flow.h"
#include "../source/runtime/runtime_world.h"
#include "../source/runtime/door_state.h"
#include "../source/runtime/runtime_session.h"
#include "../source/runtime/editor_snapshot.h"
#include "../source/runtime/gameplay_host.h"
#include "../source/runtime/gameplay_spawn.h"
#include "../source/runtime/projectile_system.h"
#include "../source/runtime/window_input_router.h"
#include "../source/runtime/human_player_config.h"
#include "../source/runtime/render_target.h"
#include "../source/runtime/runtime_renderer.h"
#include "../source/runtime/magic_object_registry.h"
#include "../source/runtime/simulation_scheduler.h"
#include "../source/runtime/map_computer_camera.h"
#include "../source/runtime/map_computer_projection.h"

using namespace igi;

namespace {

class RecordingTask final : public GameTask {
public:
    RecordingTask(uint32_t task_id, std::vector<std::string>& events)
        : GameTask(task_id, 0x200, "RecordingTask"), events_(events) {}

    void OnCreate() override { events_.push_back("create:" + std::to_string(GetId())); }
    void OnDestroy() override { events_.push_back("destroy:" + std::to_string(GetId())); }
    void OnMessage(const RuntimeTaskMessage& message) override {
        if (message.message_id == 42) {
            events_.push_back("message:" + std::to_string(GetId()));
        }
    }

private:
    std::vector<std::string>& events_;
};

float FlatTerrain(float, float) {
    return 0.0f;
}

bool WallAtOneMeter(float, float y, float) {
    return y >= PlayerController::WORLD_METER;
}

bool LowRoofAtStandingHeight(float, float, float z) {
    return z >= 6000.0f;
}

AnimationClip BuildAnimationMotionFixture() {
    AnimationClip animation_clip;
    animation_clip.length_ms = 320;
    animation_clip.translationKeys = {
        AnimTranslationKey{0, 0, glm::vec3(0.0f, 0.0f, 0.0f)},
        AnimTranslationKey{0, 160, glm::vec3(1.0f, 2.0f, 0.0f)},
        AnimTranslationKey{0, 320, glm::vec3(2.0f, 4.0f, 0.0f)},
    };
    animation_clip.events = {
        AnimEvent{0, 8, 160, -1, glm::vec3(0.0f)},
        AnimEvent{1, 9, 0, -1, glm::vec3(0.0f)},
        AnimEvent{2, 10, 320, -1, glm::vec3(0.0f)},
    };
    return animation_clip;
}

AnimationClip BuildLadderAnimationFixture() {
    AnimationClip animation_clip;
    animation_clip.length_ms = 320;
    animation_clip.translationKeys = {
        AnimTranslationKey{0, 0, glm::vec3(0.0f)},
        AnimTranslationKey{0, 160, glm::vec3(0.0f, 0.0f, 600.0f / 40.96f)},
        AnimTranslationKey{0, 320, glm::vec3(0.0f, 0.0f, 1200.0f / 40.96f)},
    };
    animation_clip.events = {
        AnimEvent{0, 8, 160, -1, glm::vec3(0.0f)},
    };
    return animation_clip;
}

AnimationClip BuildTopTransitionAnimationFixture() {
    AnimationClip animation_clip;
    animation_clip.length_ms = 320;
    animation_clip.translationKeys = {
        AnimTranslationKey{0, 0, glm::vec3(0.0f)},
        AnimTranslationKey{0, 160, glm::vec3(0.0f, 0.0f, 1000.0f / 40.96f)},
        AnimTranslationKey{0, 320, glm::vec3(0.0f, 0.0f, 2000.0f / 40.96f)},
    };
    return animation_clip;
}

QVMFile BuildRetailAiPatrolScript() {
    QVMFile parsed_file;
    parsed_file.valid = true;
    parsed_file.identifiers = {
        "AIFunction_GetCurrentEventType",
        "AIEVENT_IDLE",
        "AIAction_Patrol",
        "AIACTIONFLAG_PUSHABLE",
    };

    auto make_instruction = [](QVMOpType type, uint32_t address, uint32_t size) {
        QVMInstruction instruction{};
        instruction.type = type;
        instruction.address = address;
        instruction.size = size;
        return instruction;
    };

    QVMInstruction get_event = make_instruction(QVMOpType::PUSHIIB, 0, 2);
    get_event.operand = 0;
    QVMInstruction get_event_call = make_instruction(QVMOpType::CALL, 2, 5);
    QVMInstruction skip_get_event_arguments = make_instruction(QVMOpType::BRA, 7, 5);

    QVMInstruction idle_event = make_instruction(QVMOpType::PUSHIIB, 12, 2);
    idle_event.operand = 1;
    QVMInstruction compare_event = make_instruction(QVMOpType::EQ, 14, 1);
    QVMInstruction skip_non_idle = make_instruction(QVMOpType::BF, 15, 5);
    skip_non_idle.operand = 33;
    skip_non_idle.signed_operand = 33;

    QVMInstruction patrol_action = make_instruction(QVMOpType::PUSHIIB, 20, 2);
    patrol_action.operand = 2;
    QVMInstruction patrol_call = make_instruction(QVMOpType::CALL, 22, 17);
    patrol_call.operand = 3;
    patrol_call.signed_operand = 3;
    patrol_call.call_targets = {44, 48, 50};
    QVMInstruction skip_patrol_arguments = make_instruction(QVMOpType::BRA, 39, 5);
    skip_patrol_arguments.operand = 9;
    skip_patrol_arguments.signed_operand = 9;

    QVMInstruction patrol_path = make_instruction(QVMOpType::PUSHW, 44, 3);
    patrol_path.operand = 700;
    patrol_path.signed_operand = 700;
    QVMInstruction patrol_path_end = make_instruction(QVMOpType::BRK, 47, 1);
    QVMInstruction patrol_start_index = make_instruction(QVMOpType::PUSH0, 48, 1);
    QVMInstruction patrol_start_index_end = make_instruction(QVMOpType::BRK, 49, 1);
    QVMInstruction patrol_flags = make_instruction(QVMOpType::PUSHIIB, 50, 2);
    patrol_flags.operand = 3;
    QVMInstruction patrol_flags_end = make_instruction(QVMOpType::BRK, 52, 1);
    QVMInstruction program_end = make_instruction(QVMOpType::BRK, 53, 1);

    parsed_file.instructions = {
        get_event,
        get_event_call,
        skip_get_event_arguments,
        idle_event,
        compare_event,
        skip_non_idle,
        patrol_action,
        patrol_call,
        skip_patrol_arguments,
        patrol_path,
        patrol_path_end,
        patrol_start_index,
        patrol_start_index_end,
        patrol_flags,
        patrol_flags_end,
        program_end,
    };
    return parsed_file;
}

QVMFile BuildRetailAiSoundScript() {
    QVMFile parsed_file;
    parsed_file.valid = true;
    parsed_file.identifiers = {"AIAction_PlaySound"};
    parsed_file.strings = {"guard_alert_1"};

    auto make_instruction = [](QVMOpType type, uint32_t address, uint32_t size) {
        QVMInstruction instruction{};
        instruction.type = type;
        instruction.address = address;
        instruction.size = size;
        return instruction;
    };

    QVMInstruction play_sound_symbol = make_instruction(QVMOpType::PUSHIIB, 0, 2);
    play_sound_symbol.operand = 0;
    QVMInstruction play_sound_call = make_instruction(QVMOpType::CALL, 2, 5);
    play_sound_call.operand = 3;
    play_sound_call.signed_operand = 3;
    play_sound_call.call_targets = {8, 11, 13};
    QVMInstruction program_end = make_instruction(QVMOpType::BRK, 7, 1);

    QVMInstruction sound_name = make_instruction(QVMOpType::PUSHSIB, 8, 2);
    sound_name.operand = 0;
    QVMInstruction sound_name_end = make_instruction(QVMOpType::BRK, 10, 1);
    QVMInstruction relative_to_guard = make_instruction(QVMOpType::PUSH0, 11, 1);
    QVMInstruction relative_to_guard_end = make_instruction(QVMOpType::BRK, 12, 1);
    QVMInstruction action_flags = make_instruction(QVMOpType::PUSH0, 13, 1);
    QVMInstruction action_flags_end = make_instruction(QVMOpType::BRK, 14, 1);

    parsed_file.instructions = {
        play_sound_symbol, play_sound_call, program_end,
        sound_name, sound_name_end,
        relative_to_guard, relative_to_guard_end,
        action_flags, action_flags_end,
    };
    return parsed_file;
}

TEST(RuntimeQvmTest, ResolvesLiteralIntegerFromNativeCallArgumentBlock) {
    const QVMFile script = BuildRetailAiPatrolScript();

    EXPECT_EQ(FindFirstCallIntegerArgument(script, "AIAction_Patrol"), 700);
    EXPECT_EQ(FindFirstCallIntegerArgument(script, "MissingNative"), -1);
}

} // namespace

TEST(RuntimeRenderTargetTest, EditorRepaintCannotBecomeGameplayTarget) {
    EXPECT_EQ(
        ResolveRenderTarget(false, false),
        RenderTarget::Editor);
    EXPECT_EQ(
        ResolveRenderTarget(true, false),
        RenderTarget::Gameplay);
    EXPECT_EQ(
        ResolveRenderTarget(true, true),
        RenderTarget::Editor);
    EXPECT_EQ(
        ResolveRenderTarget(false, true),
        RenderTarget::Editor);
}

TEST(RuntimeRenderTargetTest, SimulationUsesGameplaySnapshotDuringEditorRepaint) {
    EXPECT_EQ(
        ResolveRuntimeAssetTarget(false, true),
        RuntimeAssetTarget::EditorSource);
    EXPECT_EQ(
        ResolveRuntimeAssetTarget(true, false),
        RuntimeAssetTarget::EditorSource);
    EXPECT_EQ(
        ResolveRuntimeAssetTarget(true, true),
        RuntimeAssetTarget::GameplaySnapshot);
}

TEST(WeaponViewSwayTest, LowersAndRaisesTheRigInTheReferenceNumberOfTicks) {
    WeaponViewSway weapon_view_sway;

    weapon_view_sway.Lower();
    EXPECT_FALSE(weapon_view_sway.IsSettled());
    for (int tick = 0; tick < WeaponViewSway::TicksToTravel; ++tick) {
        weapon_view_sway.Advance();
    }

    EXPECT_TRUE(weapon_view_sway.IsSettled());
    EXPECT_FLOAT_EQ(
        weapon_view_sway.GetPitchRadians(),
        WeaponViewSway::LoweredPitchRadians);
    EXPECT_FLOAT_EQ(
        weapon_view_sway.GetYawRadians(),
        WeaponViewSway::LoweredYawRadians);

    weapon_view_sway.Raise();
    for (int tick = 0; tick < WeaponViewSway::TicksToTravel; ++tick) {
        weapon_view_sway.Advance();
    }

    EXPECT_TRUE(weapon_view_sway.IsSettled());
    EXPECT_FLOAT_EQ(weapon_view_sway.GetPitchRadians(), 0.0f);
    EXPECT_FLOAT_EQ(weapon_view_sway.GetYawRadians(), 0.0f);
}

TEST(WeaponViewRecoilTest, RecoversThePresentationKickOverFixedTicks) {
    WeaponViewRecoil weapon_view_recoil;

    weapon_view_recoil.TriggerDegrees(3.0f, -1.5f);
    EXPECT_NEAR(
        weapon_view_recoil.GetPitchRadians(),
        glm::radians(3.0f),
        0.000001f);
    EXPECT_NEAR(
        weapon_view_recoil.GetYawRadians(),
        glm::radians(-1.5f),
        0.000001f);

    weapon_view_recoil.Advance();
    EXPECT_NEAR(
        weapon_view_recoil.GetPitchRadians(),
        glm::radians(2.0f),
        0.000001f);
    EXPECT_NEAR(
        weapon_view_recoil.GetYawRadians(),
        glm::radians(-1.0f),
        0.000001f);

    weapon_view_recoil.Advance();
    weapon_view_recoil.Advance();
    EXPECT_FLOAT_EQ(weapon_view_recoil.GetPitchRadians(), 0.0f);
    EXPECT_FLOAT_EQ(weapon_view_recoil.GetYawRadians(), 0.0f);
}

TEST(RuntimeRenderTest, CapturesPresentationStateWithoutAliasingWorldContainers) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.GetPlayer().Reset(glm::vec3(100.0f, 200.0f, 0.0f), 45.0f);
    world.GetWeapons().SelectWeaponByScriptId("WEAPON_ID_MP5SD");

    ProjectileLaunch launch;
    launch.position = glm::vec3(300.0f, 400.0f, 500.0f);
    launch.velocity = glm::vec3(0.0f, 1.0f, 0.0f);
    launch.type = ProjectileType::FragGrenade;
    ASSERT_TRUE(world.GetProjectiles().Spawn(launch));

    RuntimeRenderCamera camera;
    camera.position = world.GetPlayer().GetEyePosition();
    camera.field_of_view_y_radians = 1.0f;
    camera.viewport_width = 1280;
    camera.viewport_height = 720;

    RuntimeRenderer renderer;
    renderer.Capture(world, camera);
    const RuntimeRenderSnapshot captured_snapshot = renderer.GetSnapshot();

    world.GetPlayer().ApplyDamage(20.0f);
    world.GetProjectiles().Clear();

    EXPECT_EQ(captured_snapshot.camera.viewport_width, 1280);
    EXPECT_EQ(captured_snapshot.active_weapon_name, "Mp5 SD3");
    EXPECT_EQ(captured_snapshot.active_weapon_model_id, "103_01_1");
    EXPECT_FLOAT_EQ(captured_snapshot.player_health, 100.0f);
    ASSERT_EQ(captured_snapshot.projectiles.size(), 1U);
    EXPECT_EQ(captured_snapshot.projectiles[0].type, ProjectileType::FragGrenade);
    EXPECT_EQ(renderer.GetSnapshot().projectiles.size(), 1U);
}

TEST(RuntimeRenderTest, CapturesMovingGuardStateWithoutAliasingAiWorld) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AiGuardEntity guard;
    guard.id = 23;
    guard.position = glm::vec3(100.0f, 200.0f, 0.0f);
    guard.yaw = 45.0f;
    world.GetAi().RegisterGuard(guard);

    RuntimeRenderer renderer;
    renderer.Capture(world, RuntimeRenderCamera());
    const RuntimeRenderSnapshot captured_snapshot = renderer.GetSnapshot();

    world.GetAi().GetGuards()[0].position = glm::vec3(300.0f, 400.0f, 0.0f);
    world.GetAi().GetGuards()[0].yaw = 90.0f;

    ASSERT_EQ(captured_snapshot.guards.size(), 1U);
    EXPECT_EQ(captured_snapshot.guards[0].guard_id, 23U);
    EXPECT_EQ(captured_snapshot.guards[0].position,
              glm::vec3(100.0f, 200.0f, 0.0f));
    EXPECT_FLOAT_EQ(captured_snapshot.guards[0].yaw, 45.0f);
    EXPECT_EQ(renderer.GetSnapshot().guards[0].position,
              glm::vec3(100.0f, 200.0f, 0.0f));
}

TEST(RuntimeRenderTest, CapturesFixedStepMuzzleFlashAfterPlayerFire) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    ASSERT_TRUE(world.GetWeapons().SelectWeapon(4));

    PlayerInputCmd fire_command;
    fire_command.fire = true;
    world.UpdateSimulationTick(0, fire_command);

    EXPECT_FLOAT_EQ(world.GetMuzzleFlashStrength(), 1.0f);

    RuntimeRenderer renderer;
    renderer.Capture(world, RuntimeRenderCamera());
    EXPECT_FLOAT_EQ(
        renderer.GetSnapshot().muzzle_flash_strength,
        1.0f);
    EXPECT_GT(renderer.GetSnapshot().weapon_recoil_pitch_radians, 0.0f);

    world.UpdateSimulationTick(1, PlayerInputCmd());
    EXPECT_NEAR(world.GetMuzzleFlashStrength(), 0.5f, 0.0001f);
    world.UpdateSimulationTick(2, PlayerInputCmd());
    EXPECT_FLOAT_EQ(world.GetMuzzleFlashStrength(), 0.0f);
}

TEST(RuntimeRenderTest, CapturesGuardFireAndPlayerDamageFeedback) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AiGuardEntity guard;
    guard.id = 17;
    guard.position = glm::vec3(0.0f, 2.0f * PlayerController::WORLD_METER, 0.0f);
    guard.yaw = 180.0f;
    guard.vision_config.primary_fov_pitch = glm::radians(90.0f);
    guard.state = AiGuardState::Combat;
    guard.weapon_script_id = "WEAPON_ID_M16A2";
    world.GetAi().RegisterGuard(guard);

    world.UpdateSimulationTick(0, PlayerInputCmd());

    EXPECT_GT(world.GetPlayerDamageEffectStrength(), 0.0f);
    ASSERT_EQ(world.GetGuardMuzzleFlashStates().size(), 1U);
    EXPECT_EQ(world.GetGuardMuzzleFlashStates()[0].guard_id, 17U);
    EXPECT_GT(world.GetGuardMuzzleFlashStates()[0].strength, 0.0f);

    RuntimeRenderer renderer;
    renderer.Capture(world, RuntimeRenderCamera());
    EXPECT_GT(renderer.GetSnapshot().player_damage_effect_strength, 0.0f);
    ASSERT_EQ(renderer.GetSnapshot().guard_muzzle_flashes.size(), 1U);
    EXPECT_EQ(renderer.GetSnapshot().guard_muzzle_flashes[0].guard_id, 17U);

    world.UpdateSimulationTick(1, PlayerInputCmd());
    EXPECT_LT(world.GetPlayerDamageEffectStrength(), 1.0f);
    EXPECT_LT(world.GetGuardMuzzleFlashStates()[0].strength, 1.0f);
}

TEST(RuntimeRenderTest, CapturesTransientExplosionPresentationState) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    ProjectileLaunch launch;
    launch.position = glm::vec3(300.0f, 400.0f, 500.0f);
    launch.type = ProjectileType::FragGrenade;
    launch.fuse_ticks = 1;
    launch.damage = 100.0f;
    launch.explosion_radius_units = 5.0f * PlayerController::WORLD_METER;
    ASSERT_TRUE(world.GetProjectiles().Spawn(launch));

    world.UpdateSimulationTick(0, PlayerInputCmd());

    RuntimeRenderer renderer;
    renderer.Capture(world, RuntimeRenderCamera());
    ASSERT_EQ(renderer.GetSnapshot().explosions.size(), 1U);
    EXPECT_EQ(
        renderer.GetSnapshot().explosions[0].position,
        glm::vec3(300.0f, 400.0f, 500.0f));
    EXPECT_EQ(
        renderer.GetSnapshot().explosions[0].remaining_ticks,
        RuntimeExplosionRenderState::DISPLAY_DURATION_TICKS);

    for (int tick = 1; tick <= 6; ++tick) {
        world.UpdateSimulationTick(static_cast<uint64_t>(tick), PlayerInputCmd());
    }
    renderer.Capture(world, RuntimeRenderCamera());
    EXPECT_TRUE(renderer.GetSnapshot().explosions.empty());
}

TEST(RuntimeRenderTest, CapturesAuthoredMissionStatusAndTimerPresentation) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionLevelTimer level_timer;
    level_timer.task_id = "95";
    level_timer.on_expression = "TimerEnable";

    AuthoredMissionStatusMessage status_message;
    status_message.task_id = "4010";
    status_message.send_expression = "LevelTimer_95.nTick >= 1";
    status_message.display_text = "MISSION COMPLETE";
    status_message.send_once = true;

    world.SetAuthoredMissionState({}, {}, {level_timer}, {status_message});
    world.SetMissionStateBoolean("TimerEnable", true);
    AuthoredMissionFlowDefinition authored_flow;
    authored_flow.has_level_flow = true;
    authored_flow.interface_timer_enabled = true;
    authored_flow.maximum_level_play_time_seconds = 60.0;
    authored_flow.complete_expression = "StatusMessage_4010.isSendt";
    world.GetLevelFlow().InitializeMission(1, {}, {}, authored_flow);
    world.UpdateSimulationTick(0, PlayerInputCmd());

    RuntimeRenderer renderer;
    renderer.Capture(world, RuntimeRenderCamera());

    EXPECT_EQ(renderer.GetSnapshot().mission_timer_remaining_ticks, 1799);
    ASSERT_EQ(renderer.GetSnapshot().mission_status_messages.size(), 1U);
    EXPECT_EQ(
        renderer.GetSnapshot().mission_status_messages[0].text,
        "MISSION COMPLETE");
}

TEST(RuntimeRenderTest, CapturesAuthoredObjectiveLinkLocation) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({"M1_OBJ1", 1120, "", ""});
    world.GetLevelFlow().InitializeMission(
        1,
        {objective_set},
        {},
        {},
        [](int32_t link_task_id, MissionObjectiveLocation& location) {
            if (link_task_id != 1120) {
                return false;
            }
            location = {100.0, 200.0, 300.0};
            return true;
        });

    RuntimeRenderer renderer;
    renderer.Capture(world, RuntimeRenderCamera());

    EXPECT_EQ(renderer.GetSnapshot().objective_link_task_id, 1120);
    EXPECT_TRUE(renderer.GetSnapshot().has_objective_location);
    EXPECT_EQ(
        renderer.GetSnapshot().objective_location,
        glm::vec3(100.0f, 200.0f, 300.0f));
}

TEST(RuntimeRenderTest, CapturesAllMapComputerObjectiveRows) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives = {
        {"M1_OBJECTIVE_ONE", 1120, "", ""},
        {"M1_OBJECTIVE_TWO", 1121, "", ""},
    };
    world.GetLevelFlow().InitializeMission(
        1,
        {objective_set},
        {},
        {},
        [](int32_t link_task_id, MissionObjectiveLocation& location) {
            if (link_task_id != 1120) {
                return false;
            }
            location = {10.0, 20.0, 30.0};
            return true;
        });

    PlayerInputCmd open_command;
    open_command.map_computer = true;
    world.UpdateSimulationTick(0, open_command);

    RuntimeRenderer renderer;
    renderer.Capture(world, RuntimeRenderCamera());

    const RuntimeRenderSnapshot& snapshot = renderer.GetSnapshot();
    ASSERT_TRUE(snapshot.map_computer_open);
    ASSERT_EQ(snapshot.map_computer_objectives.size(), 2U);
    EXPECT_EQ(snapshot.map_computer_objectives[0].text, "M1_OBJECTIVE_ONE");
    EXPECT_EQ(snapshot.map_computer_objectives[0].link_task_id, 1120);
    EXPECT_TRUE(snapshot.map_computer_objectives[0].has_location);
    EXPECT_EQ(snapshot.map_computer_objectives[0].location,
              glm::vec3(10.0f, 20.0f, 30.0f));
    EXPECT_EQ(snapshot.map_computer_objectives[1].text, "M1_OBJECTIVE_TWO");
    EXPECT_EQ(snapshot.map_computer_objectives[1].link_task_id, 1121);
}

TEST(RuntimeProjectileTest, ReferenceGrenadeBouncesAndDetonatesAtFuseExpiry) {
    ProjectileSystem projectiles;
    projectiles.SetCollisionQuery(
        [](const glm::vec3& start, const glm::vec3& end, ProjectileCollisionHit& hit) {
            if (start.z > 0.0f && end.z <= 0.0f) {
                hit.position = glm::vec3(end.x, end.y, 0.0f);
                hit.normal = glm::vec3(0.0f, 0.0f, 1.0f);
                return true;
            }
            return false;
        });

    ProjectileLaunch launch;
    launch.position = glm::vec3(0.0f, 0.0f, 100.0f);
    launch.velocity = glm::vec3(0.0f, 0.0f, -100.0f);
    launch.type = ProjectileType::FragGrenade;
    launch.fuse_ticks = 2;
    launch.damage = 100.0f;
    ASSERT_TRUE(projectiles.Spawn(launch));

    projectiles.Tick();
    ASSERT_EQ(projectiles.GetProjectiles().size(), 1U);
    EXPECT_GT(projectiles.GetProjectiles()[0].position.z, 0.0f);
    EXPECT_TRUE(projectiles.GetDetonations().empty());

    projectiles.Tick();
    EXPECT_TRUE(projectiles.GetProjectiles().empty());
    ASSERT_EQ(projectiles.GetDetonations().size(), 1U);
    EXPECT_EQ(projectiles.GetDetonations()[0].type, ProjectileType::FragGrenade);
}

// 1. Game Clock Determinism & Tick Tests
TEST(RuntimeClockTest, DeterministicTicksAndCatchUp) {
    GameClock clock;
    EXPECT_EQ(clock.GetTickCount(), 0);
    EXPECT_FALSE(clock.IsTickDue());

    // Establish time base, then advance by 100ms (~3 ticks)
    clock.Update(0);
    clock.Update(100);
    EXPECT_TRUE(clock.IsTickDue());

    int tick_count = 0;
    while (clock.IsTickDue()) {
        clock.CompleteTick();
        tick_count++;
        if (clock.GetTickCount() < GameClock::GUARDED_STARTUP_TICKS) {
            clock.CompleteRender();
        }
    }
    EXPECT_GE(tick_count, 3);
    EXPECT_EQ(clock.GetTickCount(), static_cast<uint64_t>(tick_count));

    // Test pause
    clock.SetPaused(true);
    clock.Update(300);
    EXPECT_FALSE(clock.IsTickDue());
}

TEST(RuntimeClockTest, UsesAbsoluteDeadlinesInsteadOfRoundedAccumulator) {
    GameClock clock;
    clock.Reset(1000);
    clock.Update(1000);
    clock.Update(1034);

    ASSERT_TRUE(clock.IsTickDue());
    EXPECT_EQ(clock.GetDueMilliseconds(), 1000);
    clock.CompleteTick();
    clock.CompleteRender();

    clock.Update(1067);
    EXPECT_TRUE(clock.IsTickDue());
    EXPECT_EQ(clock.GetDueMilliseconds(), 1033);
}

TEST(RuntimeClockTest, BoundsCatchUpBurstAndExcludesNestedWallTime) {
    GameClock clock;
    clock.Reset(0);
    clock.Update(0);
    clock.Update(1000);

    // The first three ticks are render-guarded in the reference loop.
    for (int startup_tick = 0; startup_tick < GameClock::GUARDED_STARTUP_TICKS; ++startup_tick) {
        ASSERT_TRUE(clock.IsTickDue());
        clock.CompleteTick();
        clock.CompleteRender();
    }

    int completed_ticks = 0;
    while (clock.IsTickDue()) {
        clock.CompleteTick();
        ++completed_ticks;
    }

    EXPECT_EQ(completed_ticks, 11);
    EXPECT_TRUE(clock.IsCatchUpCapped());

    clock.Reset(100);
    clock.Update(100);
    clock.BeginExcludedTime(100);
    clock.BeginExcludedTime(200);
    clock.EndExcludedTime(300);
    clock.EndExcludedTime(400);
    clock.Update(400);

    EXPECT_EQ(clock.GetExcludedMilliseconds(), 300);
    EXPECT_FALSE(clock.IsTickDue());
    clock.Update(401);
    EXPECT_TRUE(clock.IsTickDue());
}

// 2. QVM Bytecode Execution & Native Registry Tests
TEST(RuntimeQvmTest, BytecodeExecutionAndStack) {
    QvmNativeRegistry registry;
    registry.RegisterFunction(0x100, "AddTen", [](QvmExecutionContext& ctx, const std::vector<QvmRuntimeValue>& args) {
        int32_t val = args.empty() ? 0 : args[0].int_val;
        return QvmRuntimeValue::FromInt(val + 10);
    });

    QvmProgram program;
    // Bytecode: PUSH_INT 25, CALL_NATIVE 0x100 (1 arg), RETURN
    program.instructions.push_back({0x01, 0, 25, 0.0, ""});
    program.instructions.push_back({0x30, 0x100, 1, 0.0, ""});
    program.instructions.push_back({0xFF, 0, 0, 0.0, ""});

    QvmInterpreter interpreter(registry);
    auto ctx = interpreter.CreateContext(program);
    ASSERT_NE(ctx, nullptr);

    bool ok = ctx->Run();
    EXPECT_TRUE(ok);
    EXPECT_FALSE(ctx->HasErrored());
    EXPECT_EQ(ctx->StackSize(), 1);
    EXPECT_EQ(ctx->Pop().int_val, 35);
}

TEST(RuntimeQvmTest, RejectsMalformedBytecodeAndUnsafeControlFlow) {
    QvmNativeRegistry registry;
    QvmInterpreter interpreter(registry);
    QvmProgram program;

    EXPECT_FALSE(interpreter.LoadProgram(
        std::vector<uint8_t>{0x01, 0x02},
        program));
    EXPECT_NE(interpreter.GetLastError().find("truncated"), std::string::npos);

    QvmProgram invalid_jump_program;
    invalid_jump_program.instructions.push_back({0x20, 99, 0, 0.0, ""});
    auto invalid_jump_context = interpreter.CreateContext(invalid_jump_program);
    EXPECT_FALSE(invalid_jump_context->Run());
    EXPECT_TRUE(invalid_jump_context->HasErrored());
}

TEST(RuntimeQvmTest, FailsOnStackUnderflowAndPreservesNativeArgumentOrder) {
    QvmNativeRegistry registry;
    QvmInterpreter interpreter(registry);
    registry.RegisterFunction(0x101, "EncodePair", [](QvmExecutionContext&, const std::vector<QvmRuntimeValue>& args) {
        if (args.size() != 2U) {
            return QvmRuntimeValue::FromInt(-1);
        }
        return QvmRuntimeValue::FromInt(args[0].int_val * 10 + args[1].int_val);
    });

    QvmProgram underflow_program;
    underflow_program.instructions.push_back({0x10, 0, 0, 0.0, ""});
    auto underflow_context = interpreter.CreateContext(underflow_program);
    EXPECT_FALSE(underflow_context->Run());
    EXPECT_TRUE(underflow_context->HasErrored());

    QvmProgram native_program;
    native_program.instructions.push_back({0x01, 0, 1, 0.0, ""});
    native_program.instructions.push_back({0x01, 0, 2, 0.0, ""});
    native_program.instructions.push_back({0x30, 0x101, 2, 0.0, ""});
    native_program.instructions.push_back({0xFF, 0, 0, 0.0, ""});
    auto native_context = interpreter.CreateContext(native_program);
    ASSERT_TRUE(native_context->Run());
    ASSERT_EQ(native_context->StackSize(), 1U);
    EXPECT_EQ(native_context->Pop().int_val, 12);
}

TEST(RuntimeQvmTest, StopsDeterministicallyAtInstructionBudgetAndCanReset) {
    QvmNativeRegistry registry;
    QvmProgram looping_program;
    looping_program.instructions.push_back({0x20, 0, 0, 0.0, ""});

    QvmInterpreter interpreter(registry);
    auto context = interpreter.CreateContext(looping_program);
    EXPECT_FALSE(context->Run());
    EXPECT_TRUE(context->HasErrored());
    EXPECT_EQ(context->GetStepCount(), QvmExecutionContext::MAX_INSTRUCTION_STEPS);

    context->Reset();
    EXPECT_FALSE(context->HasErrored());
    EXPECT_EQ(context->GetStepCount(), 0U);
}

TEST(RuntimeQvmTest, ExecutesRetailLoopArithmeticAndDeferredNativeArguments) {
    QvmNativeRegistry registry;
    int native_evaluation_count = 0;
    registry.RegisterDeferredFunctionByName(
        "RecordValue",
        [&native_evaluation_count](QvmExecutionContext&, const QvmNativeCallArguments& arguments) {
            ++native_evaluation_count;
            return QvmRuntimeValue::FromInt(arguments.GetInt(0) + 1);
        });

    QVMFile parsed_file;
    parsed_file.valid = true;
    parsed_file.identifiers = {"RecordValue"};

    QVMInstruction push_symbol{};
    push_symbol.type = QVMOpType::PUSHIIB;
    push_symbol.operand = 0;
    push_symbol.address = 0;
    push_symbol.size = 2;

    QVMInstruction call{};
    call.type = QVMOpType::CALL;
    call.operand = 1;
    call.signed_operand = 1;
    call.call_targets = {16};
    call.address = 2;
    call.size = 9;

    QVMInstruction skip_arguments{};
    skip_arguments.type = QVMOpType::BRA;
    skip_arguments.operand = 5;
    skip_arguments.signed_operand = 5;
    skip_arguments.address = 11;
    skip_arguments.size = 5;

    QVMInstruction argument_value{};
    argument_value.type = QVMOpType::PUSHB;
    argument_value.operand = 7;
    argument_value.signed_operand = 7;
    argument_value.address = 16;
    argument_value.size = 2;

    QVMInstruction argument_one{};
    argument_one.type = QVMOpType::PUSH1;
    argument_one.address = 18;
    argument_one.size = 1;

    QVMInstruction argument_add{};
    argument_add.type = QVMOpType::ADD;
    argument_add.address = 19;
    argument_add.size = 1;

    QVMInstruction argument_end{};
    argument_end.type = QVMOpType::BRK;
    argument_end.address = 20;
    argument_end.size = 1;

    QVMInstruction program_end{};
    program_end.type = QVMOpType::BRK;
    program_end.address = 21;
    program_end.size = 1;

    parsed_file.instructions = {
        push_symbol,
        call,
        skip_arguments,
        argument_value,
        argument_one,
        argument_add,
        argument_end,
        program_end,
    };

    QvmInterpreter interpreter(registry);
    QvmProgram program;
    ASSERT_TRUE(interpreter.LoadProgram(parsed_file, program))
        << interpreter.GetLastError();
    ASSERT_TRUE(program.uses_loop_85_instruction_set);

    auto context = interpreter.CreateContext(program);
    ASSERT_TRUE(context->Run()) << context->GetLastError();
    ASSERT_EQ(context->StackSize(), 1U);
    EXPECT_EQ(context->Pop().int_val, 9);
    EXPECT_EQ(native_evaluation_count, 1);
}

TEST(RuntimeQvmTest, ConvertsDeferredNativeExceptionsToDeterministicVmFaults) {
    QvmNativeRegistry registry;
    registry.RegisterDeferredFunctionByName(
        "Explode",
        [](QvmExecutionContext&, const QvmNativeCallArguments&) -> QvmRuntimeValue {
            throw std::runtime_error("native failure");
        });

    QVMFile parsed_file;
    parsed_file.valid = true;
    parsed_file.identifiers = {"Explode"};

    QVMInstruction push_symbol{};
    push_symbol.type = QVMOpType::PUSHIIB;
    push_symbol.operand = 0;
    push_symbol.address = 0;
    push_symbol.size = 2;

    QVMInstruction call{};
    call.type = QVMOpType::CALL;
    call.operand = 0;
    call.signed_operand = 0;
    call.address = 2;
    call.size = 5;

    QVMInstruction program_end{};
    program_end.type = QVMOpType::BRK;
    program_end.address = 7;
    program_end.size = 1;

    parsed_file.instructions = {push_symbol, call, program_end};

    QvmInterpreter interpreter(registry);
    QvmProgram program;
    ASSERT_TRUE(interpreter.LoadProgram(parsed_file, program))
        << interpreter.GetLastError();

    auto context = interpreter.CreateContext(program);
    EXPECT_FALSE(context->Run());
    EXPECT_TRUE(context->HasErrored());
    EXPECT_NE(context->GetLastError().find("native failure"), std::string::npos);
}

// 3. Task Tree & Messaging Tests
TEST(RuntimeTaskTreeTest, LifecycleAndMessaging) {
    TaskTree tree;
    auto root = std::make_shared<ContainerTask>(1, 0x181, "RootContainer");
    auto child = std::make_shared<GameTask>(2, 0x200, "PlayerTask");
    root->AppendChild(child);
    tree.SetRoot(root);
    tree.RegisterTask(child);

    EXPECT_NE(tree.FindTask(2), nullptr);
    EXPECT_EQ(tree.FindTask(2)->GetName(), "PlayerTask");

    RuntimeTaskMessage msg;
    msg.message_id = 42;
    tree.QueueMessage(msg);
    tree.Update(1.0 / 30.0);

    EXPECT_TRUE(child->IsActive());
}

TEST(RuntimeTaskTreeTest, RejectsDuplicateOwnershipAndDestroysChildrenBeforeParents) {
    std::vector<std::string> lifecycle_events;
    auto root = std::make_shared<RecordingTask>(1, lifecycle_events);
    auto child = std::make_shared<RecordingTask>(2, lifecycle_events);
    auto duplicate_id = std::make_shared<RecordingTask>(2, lifecycle_events);

    EXPECT_TRUE(root->AppendChild(child));
    EXPECT_FALSE(root->AppendChild(child));
    EXPECT_FALSE(root->AppendChild(duplicate_id));
    EXPECT_FALSE(child->AppendChild(root));

    TaskTree tree;
    ASSERT_TRUE(tree.SetRoot(root));
    tree.RegisterTask(child);
    ASSERT_EQ(lifecycle_events, (std::vector<std::string>{"create:1", "create:2"}));

    RuntimeTaskMessage message;
    message.message_id = 42;
    message.target_id = child->GetId();
    tree.QueueMessage(message);
    tree.Update(1.0 / 30.0);
    EXPECT_EQ(lifecycle_events.back(), "message:2");

    child->MarkForDestruction();
    tree.Update(1.0 / 30.0);
    ASSERT_GE(lifecycle_events.size(), 4U);
    EXPECT_EQ(lifecycle_events[lifecycle_events.size() - 1], "destroy:2");

    tree.Clear();
    EXPECT_EQ(lifecycle_events.back(), "destroy:1");
}

TEST(AnimationMotionTest, SamplesRootTranslationAndPreservesReferenceWorldScale) {
    const AnimationClip animation_clip = BuildAnimationMotionFixture();

    const glm::vec3 sampled_position = AnimationMotionSampler::SampleRootTranslation(
        animation_clip,
        80.0f);

    EXPECT_NEAR(sampled_position.x, 20.48f, 0.0001f);
    EXPECT_NEAR(sampled_position.y, 40.96f, 0.0001f);
    EXPECT_FLOAT_EQ(sampled_position.z, 0.0f);
}

TEST(AnimationMotionTest, AdvancesForwardAndDispatchesEventsOnClosedInterval) {
    const AnimationClip animation_clip = BuildAnimationMotionFixture();

    const AnimationMotionStep step = AnimationMotionSampler::Advance(
        animation_clip,
        0.0f,
        160.0f,
        false);

    EXPECT_FLOAT_EQ(step.current_time_ms, 160.0f);
    EXPECT_FALSE(step.wrapped);
    EXPECT_FALSE(step.ended);
    EXPECT_NEAR(step.root_motion_delta.x, 40.96f, 0.0001f);
    EXPECT_NEAR(step.root_motion_delta.y, 81.92f, 0.0001f);
    EXPECT_NE(std::find(step.crossed_event_ids.begin(), step.crossed_event_ids.end(), 8),
        step.crossed_event_ids.end());
    EXPECT_EQ(std::find(step.crossed_event_ids.begin(), step.crossed_event_ids.end(), 9),
        step.crossed_event_ids.end());
}

TEST(AnimationMotionTest, WrapsForwardAndIncludesBothSidesOfTheAnimationSeam) {
    const AnimationClip animation_clip = BuildAnimationMotionFixture();

    const AnimationMotionStep step = AnimationMotionSampler::Advance(
        animation_clip,
        160.0f,
        160.0f,
        true);

    EXPECT_FLOAT_EQ(step.current_time_ms, 0.0f);
    EXPECT_TRUE(step.wrapped);
    EXPECT_FALSE(step.ended);
    EXPECT_NEAR(step.root_motion_delta.x, 40.96f, 0.0001f);
    EXPECT_NEAR(step.root_motion_delta.y, 81.92f, 0.0001f);
    EXPECT_NE(std::find(step.crossed_event_ids.begin(), step.crossed_event_ids.end(), 9),
        step.crossed_event_ids.end());
    EXPECT_NE(std::find(step.crossed_event_ids.begin(), step.crossed_event_ids.end(), 10),
        step.crossed_event_ids.end());
}

TEST(AnimationMotionTest, SupportsReverseLadderPlaybackAndReverseSeams) {
    const AnimationClip animation_clip = BuildAnimationMotionFixture();

    const AnimationMotionStep reverse_step = AnimationMotionSampler::Advance(
        animation_clip,
        320.0f,
        -160.0f,
        false);
    EXPECT_FLOAT_EQ(reverse_step.current_time_ms, 160.0f);
    EXPECT_FALSE(reverse_step.ended);
    EXPECT_NEAR(reverse_step.root_motion_delta.x, -40.96f, 0.0001f);
    EXPECT_NEAR(reverse_step.root_motion_delta.y, -81.92f, 0.0001f);
    EXPECT_NE(std::find(reverse_step.crossed_event_ids.begin(), reverse_step.crossed_event_ids.end(), 8),
        reverse_step.crossed_event_ids.end());

    const AnimationMotionStep wrapped_reverse_step = AnimationMotionSampler::Advance(
        animation_clip,
        0.0f,
        -160.0f,
        true);
    EXPECT_FLOAT_EQ(wrapped_reverse_step.current_time_ms, 160.0f);
    EXPECT_TRUE(wrapped_reverse_step.wrapped);
    EXPECT_FALSE(wrapped_reverse_step.ended);
    EXPECT_NEAR(wrapped_reverse_step.root_motion_delta.x, -40.96f, 0.0001f);
    EXPECT_NEAR(wrapped_reverse_step.root_motion_delta.y, -81.92f, 0.0001f);
    EXPECT_NE(std::find(wrapped_reverse_step.crossed_event_ids.begin(), wrapped_reverse_step.crossed_event_ids.end(), 10),
        wrapped_reverse_step.crossed_event_ids.end());
    EXPECT_NE(std::find(wrapped_reverse_step.crossed_event_ids.begin(), wrapped_reverse_step.crossed_event_ids.end(), 8),
        wrapped_reverse_step.crossed_event_ids.end());
}

TEST(PlayerAnimationDriverTest, SelectsVanillaForwardClipAndSuppliesRootMotion) {
    RuntimeWorld runtime_world;
    runtime_world.Initialize(FlatTerrain);
    runtime_world.UpdateSimulationTick(0, PlayerInputCmd());

    AnimationClip forward_animation = BuildAnimationMotionFixture();
    PlayerAnimationDriver animation_driver;
    animation_driver.SetAnimationClip(4, &forward_animation);

    PlayerInputCmd input_command;
    input_command.forward = 1.0f;
    animation_driver.AugmentInput(runtime_world, input_command);

    EXPECT_EQ(animation_driver.GetActiveAnimationId(), 4);
    EXPECT_NEAR(input_command.root_motion_delta.x, 40.96f, 0.0001f);
    EXPECT_NEAR(input_command.root_motion_delta.y, 81.92f, 0.0001f);
    EXPECT_FALSE(input_command.suppress_root_motion_scale);
}

TEST(PlayerAnimationDriverTest, SuppliesLadderRootMotionAndBoundaryEventToRuntimeWorld) {
    RuntimeWorld runtime_world;
    runtime_world.Initialize(FlatTerrain);
    runtime_world.SetLadderPlacements({LadderPlacement(
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 20000.0f),
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 12288.0f),
        glm::mat3(1.0f))});
    runtime_world.GetPlayer().Reset(glm::vec3(0.0f, -2000.0f, 1000.0f), 0.0f);

    PlayerInputCmd mount_command;
    mount_command.interact = true;
    runtime_world.UpdateSimulationTick(0, mount_command);
    ASSERT_TRUE(runtime_world.IsPlayerOnLadder());

    AnimationClip ladder_animation = BuildLadderAnimationFixture();
    PlayerAnimationDriver animation_driver;
    animation_driver.SetAnimationClip(168, &ladder_animation);

    PlayerInputCmd climb_command;
    climb_command.forward = 1.0f;
    animation_driver.AugmentInput(runtime_world, climb_command);

    EXPECT_EQ(animation_driver.GetActiveAnimationId(), 168);
    EXPECT_NEAR(climb_command.root_motion_delta.z, 600.0f, 0.0001f);
    EXPECT_TRUE(climb_command.suppress_root_motion_scale);
    EXPECT_TRUE(climb_command.ladder_step_complete);

    runtime_world.UpdateSimulationTick(1, climb_command);
    EXPECT_EQ(runtime_world.GetLadderTraversal().GetStep(), 1);
    EXPECT_NEAR(
        runtime_world.GetPlayer().GetPosition().z,
        runtime_world.GetLadderPlacements()[0].GetBottomMount().z + 600.0f,
        0.0001f);
}

TEST(PlayerAnimationDriverTest, PreservesFirstTopExitMotionAtLadderBoundary) {
    RuntimeWorld runtime_world;
    runtime_world.Initialize(FlatTerrain);
    runtime_world.SetLadderPlacements({LadderPlacement(
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 20000.0f),
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 3000.0f),
        glm::mat3(1.0f))});
    runtime_world.GetPlayer().Reset(glm::vec3(0.0f, -2000.0f, 1000.0f), 0.0f);

    PlayerInputCmd mount_command;
    mount_command.interact = true;
    runtime_world.UpdateSimulationTick(0, mount_command);
    ASSERT_TRUE(runtime_world.IsPlayerOnLadder());
    ASSERT_EQ(runtime_world.GetLadderTraversal().GetTopStep(), 0);

    AnimationClip top_transition_animation = BuildTopTransitionAnimationFixture();
    PlayerAnimationDriver animation_driver;
    animation_driver.SetAnimationClip(170, &top_transition_animation);

    PlayerInputCmd top_exit_command;
    top_exit_command.forward = 1.0f;
    animation_driver.AugmentInput(runtime_world, top_exit_command);
    ASSERT_EQ(animation_driver.GetActiveAnimationId(), 170);
    ASSERT_NEAR(top_exit_command.root_motion_delta.z, 1000.0f, 0.0001f);

    const float position_before_transition =
        runtime_world.GetPlayer().GetPosition().z;
    runtime_world.UpdateSimulationTick(1, top_exit_command);

    EXPECT_EQ(
        runtime_world.GetLadderTraversal().GetPhase(),
        LadderTraversalPhase::GettingOffTop);
    EXPECT_NEAR(
        runtime_world.GetPlayer().GetPosition().z,
        position_before_transition + 1000.0f,
        0.0001f);
}

// 4. Player Locomotion, Physics & Obstacle Collision Integration Tests
TEST(RuntimePlayerTest, GravityAndJumpIntegration) {
    PlayerController player;
    player.Reset(glm::vec3(0.0f, 0.0f, 10000.0f));

    auto dummy_terrain = [](float x, float y) -> float { return 0.0f; };

    // Initially in air (Z = 100, terrain = 0)
    PlayerInputCmd cmd;
    player.Tick(cmd, dummy_terrain);
    EXPECT_FALSE(player.IsGrounded());
    EXPECT_LT(player.GetVelocity().z, 0.0f); // Falling

    // Place on ground
    player.Reset(glm::vec3(0.0f, 0.0f, 0.0f));
    player.Tick(cmd, dummy_terrain);
    EXPECT_TRUE(player.IsGrounded());

    // Jump trigger
    cmd.jump = true;
    player.Tick(cmd, dummy_terrain);
    EXPECT_FALSE(player.IsGrounded());
    EXPECT_GT(player.GetVelocity().z, 0.0f);

    // Obstacle collision test
    player.Reset(glm::vec3(100.0f, 100.0f, 0.0f));
    std::vector<ObstacleCollider> obstacles;
    ObstacleCollider enemy;
    enemy.center = glm::vec3(100.0f, 100.0f, 0.0f);
    enemy.radius = 1638.4f;
    enemy.height = 7372.8f;
    obstacles.push_back(enemy);

    PlayerCollision collision;
    glm::vec3 test_pos(100.0f, 100.0f, 0.0f);
    collision.ResolveObstacles(test_pos, obstacles, 1638.4f);
    // Should resolve without NaN/Inf
    EXPECT_FALSE(std::isnan(test_pos.x) || std::isnan(test_pos.y));
}

TEST(PlayerMotionTest, MatchesOpenIgiReferenceIntegrators) {
    const glm::vec3 airborne_velocity = PlayerMotion::IntegrateAirborneVelocity(
        glm::vec3(11.0f, -7.0f, 100.0f));
    EXPECT_FLOAT_EQ(airborne_velocity.x, 11.0f);
    EXPECT_FLOAT_EQ(airborne_velocity.y, -7.0f);
    EXPECT_NEAR(airborne_velocity.z, 15.258308f, 0.0001f);

    const glm::vec3 ladder_slide_velocity = PlayerMotion::IntegrateLadderSlideVelocity(
        glm::vec3(11.0f, -7.0f, 100.0f));
    EXPECT_NEAR(ladder_slide_velocity.x, 10.890000f, 0.0001f);
    EXPECT_NEAR(ladder_slide_velocity.y, -6.930000f, 0.0001f);
    EXPECT_NEAR(ladder_slide_velocity.z, 54.845122f, 0.0001f);
}

TEST(PlayerMotionTest, AppliesRootMotionScaleBeforeYawRotation) {
    const glm::vec3 scaled_step = PlayerMotion::ApplyRootMotion(
        glm::vec3(2.0f, 3.0f, 4.0f),
        90.0f);
    EXPECT_NEAR(scaled_step.x, -5.25f, 0.0001f);
    EXPECT_NEAR(scaled_step.y, 3.5f, 0.0001f);
    EXPECT_NEAR(scaled_step.z, 7.0f, 0.0001f);

    const glm::vec3 scale_suppressed_step = PlayerMotion::ApplyRootMotion(
        glm::vec3(2.0f, 3.0f, 4.0f),
        90.0f,
        PlayerMotion::DefaultDeltaTranslationScale,
        true);
    EXPECT_NEAR(scale_suppressed_step.x, -3.0f, 0.0001f);
    EXPECT_NEAR(scale_suppressed_step.y, 2.0f, 0.0001f);
    EXPECT_NEAR(scale_suppressed_step.z, 4.0f, 0.0001f);
}

TEST(PlayerMotionTest, PreservesReferenceAirControlMovementSlots) {
    const glm::vec3 air_control_delta = PlayerMotion::CalculateAirControl(
        1.0f,
        1.0f,
        90.0f,
        10.0f);

    EXPECT_NEAR(air_control_delta.x, -10.0f, 0.0001f);
    EXPECT_NEAR(air_control_delta.y, 10.0f, 0.0001f);
    EXPECT_FLOAT_EQ(air_control_delta.z, 0.0f);
}

TEST(PlayerLadderTest, BuildsReferenceClimbLineAndMountOffsets) {
    const LadderPlacement ladder(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 10000.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 12288.0f),
        glm::mat3(1.0f));

    EXPECT_EQ(ladder.GetClimbLine().GetStepCount(), 10);
    EXPECT_EQ(ladder.GetClimbLine().GetTopStep(), 4);
    EXPECT_NEAR(ladder.GetClimbLine().PositionAtStep(4).z, 4915.2f, 0.001f);
    EXPECT_NEAR(ladder.GetBottomMount().y, 1515.52f, 0.001f);
    EXPECT_NEAR(ladder.GetBottomMount().z, 3915.776f, 0.001f);
    EXPECT_NEAR(ladder.GetTopMount().y, -1638.4f, 0.001f);
    EXPECT_NEAR(ladder.GetTopMount().z, 15769.6f, 0.001f);
}

TEST(MagicObjectRegistryTest, ResolvesLadderTaskTypesWithoutHardCodedIds) {
    MagicObjectRegistry registry;
    ASSERT_TRUE(registry.LoadDecompiledSource(
        "DefineMagicObj(\"ladder.obj\", \"ladder.obj\", TASKTYPE_LADDER);\n"
        "DefineMagicObj(\"deathzone.obj\", \"deathzone.obj\", TASKTYPE_DEATHZONE);\n"
        "DefineMagicObj(\"ladder.obj\", \"other.obj\", TASKTYPE_DEATHZONE);\n"));

    ASSERT_EQ(registry.GetDefinitions().size(), 2U);
    EXPECT_TRUE(registry.IsLadderAttachment("ladder.obj"));
    EXPECT_FALSE(registry.IsLadderAttachment("deathzone.obj"));
    ASSERT_NE(registry.Find("ladder.obj"), nullptr);
    EXPECT_EQ(registry.Find("ladder.obj")->model_id, "ladder.obj");
}

TEST(PlayerLadderTest, ResolvesBottomAndTopActivationGeometry) {
    const LadderPlacement ladder(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 10000.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 12288.0f),
        glm::mat3(1.0f));

    bool at_top = true;
    EXPECT_TRUE(ladder.CanActivate(
        glm::vec3(0.0f, -2000.0f, 1000.0f),
        0.0f,
        at_top));
    EXPECT_FALSE(at_top);

    EXPECT_TRUE(ladder.CanActivate(
        glm::vec3(0.0f, -1000.0f, 9000.0f),
        0.0f,
        at_top));
    EXPECT_TRUE(at_top);
}

TEST(PlayerLadderTest, MatchesReferenceTraversalStateTransitions) {
    const LadderPlacement ladder(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 10000.0f),
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 0.0f, 12288.0f),
        glm::mat3(1.0f));
    LadderTraversal traversal;

    traversal.Mount(ladder, false);
    EXPECT_EQ(traversal.GetPhase(), LadderTraversalPhase::Climbing);
    EXPECT_EQ(traversal.Decide(true, false, false), LadderStepResult::SteppingUp);
    EXPECT_EQ(traversal.GetDirection(), 1);
    EXPECT_EQ(traversal.Decide(false, false, true), LadderStepResult::Sliding);
    EXPECT_EQ(traversal.GetPhase(), LadderTraversalPhase::SlidingDown);
    EXPECT_TRUE(traversal.CompleteSlide());
    EXPECT_EQ(traversal.GetPhase(), LadderTraversalPhase::Inactive);

    traversal.Mount(ladder, false);
    EXPECT_EQ(traversal.Decide(false, true, false), LadderStepResult::Dismounted);
    EXPECT_EQ(traversal.GetPhase(), LadderTraversalPhase::Inactive);

    traversal.Mount(ladder, true);
    EXPECT_EQ(traversal.GetPhase(), LadderTraversalPhase::GettingOnTop);
    traversal.Move(glm::vec3(0.0f, 0.0f, -500.0f));
    EXPECT_NEAR(traversal.GetPosition().z, ladder.GetTopMount().z - 500.0f, 0.001f);
    EXPECT_TRUE(traversal.CompleteTopTransition());
    EXPECT_EQ(traversal.GetPhase(), LadderTraversalPhase::Climbing);
    EXPECT_EQ(traversal.Decide(true, false, false), LadderStepResult::ReachedTop);
    EXPECT_TRUE(traversal.CompleteTopTransition());
    EXPECT_EQ(traversal.GetPhase(), LadderTraversalPhase::Inactive);
}

TEST(RuntimePlayerTest, AppliesAnimationRootMotionOnTheFixedStepBoundary) {
    PlayerController player;
    player.Reset(glm::vec3(0.0f));
    player.Tick(PlayerInputCmd(), FlatTerrain);

    PlayerInputCmd animation_command;
    animation_command.root_motion_delta = glm::vec3(0.0f, 100.0f, 0.0f);
    player.Tick(animation_command, FlatTerrain);

    EXPECT_NEAR(player.GetPosition().y, 175.0f, 0.0001f);
    EXPECT_FLOAT_EQ(player.GetVelocity().x, 0.0f);
    EXPECT_FLOAT_EQ(player.GetVelocity().y, 0.0f);
}

TEST(RuntimePlayerTest, VanillaFallImpactMatchesReferenceBoundaries) {
    constexpr float maximum_health = 100.0f;
    const auto safe_impact = CalculateVanillaFallImpact(
        -14.0f * PlayerController::WORLD_METER / 30.0f,
        maximum_health);
    const auto medium_impact = CalculateVanillaFallImpact(
        -15.0f * PlayerController::WORLD_METER / 30.0f,
        maximum_health);
    const auto heavy_impact = CalculateVanillaFallImpact(
        -16.0f * PlayerController::WORLD_METER / 30.0f,
        maximum_health);
    const auto lethal_impact = CalculateVanillaFallImpact(
        -27.0f * PlayerController::WORLD_METER / 30.0f,
        maximum_health);

    EXPECT_NEAR(safe_impact.speed_meters_per_second, 14.0f, 0.0001f);
    EXPECT_FLOAT_EQ(safe_impact.damage, 0.0f);
    EXPECT_TRUE(safe_impact.sound_name.empty());
    EXPECT_FLOAT_EQ(safe_impact.hearing_radius_units, 20480.0f);

    EXPECT_NEAR(medium_impact.damage, maximum_health / 13.0f, 0.0001f);
    EXPECT_EQ(medium_impact.sound_name, "player_fall_1");
    EXPECT_FLOAT_EQ(medium_impact.hearing_radius_units, 40960.0f);

    EXPECT_NEAR(heavy_impact.damage, maximum_health * 2.0f / 13.0f, 0.0001f);
    EXPECT_EQ(heavy_impact.sound_name, "player_fall_2");

    EXPECT_NEAR(lethal_impact.damage, maximum_health, 0.0001f);
    EXPECT_EQ(lethal_impact.sound_name, "player_fall_3");
    EXPECT_FLOAT_EQ(lethal_impact.view_kick_units, -1024.0f);
}

// Human-vs-human separation (vanilla A11, 0x462BA0): a move that would end
// nearer to another human is rejected outright; vertical separation above the
// cylinder height never blocks; an already-overlapping body may walk out.
TEST(RuntimePlayerTest, HumanSeparationMatchesReferenceCylinder) {
    const glm::vec3 start(0.0f, 0.0f, 0.0f);
    const glm::vec3 other(4096.0f, 0.0f, 0.0f); // 1 m away on +X

    // Move toward the blocker closes the gap -> refused. The end point lands
    // inside the 2252.8-unit cylinder around the other body.
    const glm::vec3 toward = HumanSeparation::Resolve(
        start, glm::vec3(3072.0f, 0.0f, 0.0f), other);
    EXPECT_EQ(toward, start);

    // Move away from the blocker opens the gap -> allowed.
    const glm::vec3 away = HumanSeparation::Resolve(
        start, glm::vec3(-1024.0f, 0.0f, 0.0f), other);
    EXPECT_EQ(away, glm::vec3(-1024.0f, 0.0f, 0.0f));

    // Beyond the radius (2252.8 units) never blocks.
    const glm::vec3 beyond = HumanSeparation::Resolve(
        start, glm::vec3(2048.0f, 0.0f, 0.0f), glm::vec3(8192.0f, 0.0f, 0.0f));
    EXPECT_EQ(beyond, glm::vec3(2048.0f, 0.0f, 0.0f));

    // Vertical gap at or above 7372.8 units never blocks.
    const glm::vec3 above = HumanSeparation::Resolve(
        start, glm::vec3(512.0f, 0.0f, 7372.8f), other);
    EXPECT_EQ(above, glm::vec3(512.0f, 0.0f, 7372.8f));

    // Already overlapping: only moves that close the gap are refused, so the
    // body can escape by walking out.
    const glm::vec3 overlapping_start(other.x - 100.0f, 0.0f, 0.0f);
    const glm::vec3 escape = HumanSeparation::Resolve(
        overlapping_start, glm::vec3(other.x + 100.0f, 0.0f, 0.0f), other);
    EXPECT_EQ(escape, glm::vec3(other.x + 100.0f, 0.0f, 0.0f));

    // ResolveAll lets each blocker overwrite the candidate in turn: one
    // refusal is enough even with a later permissive blocker.
    std::vector<glm::vec3> blockers = {
        glm::vec3(1024.0f, 0.0f, 0.0f),
        glm::vec3(-16384.0f, 0.0f, 0.0f),
    };
    const glm::vec3 blocked = HumanSeparation::ResolveAll(
        start, glm::vec3(512.0f, 0.0f, 0.0f), blockers.begin(), blockers.end());
    EXPECT_EQ(blocked, start);
}

TEST(RuntimePlayerTest, LandingDamageBypassesArmor) {
    PlayerController player;
    player.Reset(glm::vec3(0.0f, 0.0f, 12.0f * PlayerController::WORLD_METER));

    PlayerInputCmd input_command;
    for (int tick = 0; tick < 120 && !player.IsGrounded(); ++tick) {
        player.Tick(input_command, FlatTerrain);
    }

    ASSERT_TRUE(player.IsGrounded());
    ASSERT_GT(player.GetLastLandingImpact().damage, 0.0f);
    EXPECT_FLOAT_EQ(player.GetArmor(), player.GetMaximumArmor());
    EXPECT_NEAR(
        player.GetHealth(),
        player.GetMaximumHealth() - player.GetLastLandingImpact().damage,
        0.0001f);
}

TEST(RuntimePlayerTest, LandingFromAHighFallAppliesDamage) {
    PlayerController player;
    player.Reset(glm::vec3(0.0f, 0.0f, 12.0f * PlayerController::WORLD_METER));

    PlayerInputCmd no_input;
    for (int tick = 0; tick < 240 && !player.IsGrounded(); ++tick) {
        player.Tick(no_input, FlatTerrain);
    }

    ASSERT_TRUE(player.IsGrounded());
    EXPECT_LT(player.GetHealth(), player.GetMaximumHealth());
}

TEST(RuntimePlayerTest, NormalJumpLandingDoesNotApplyFallDamage) {
    PlayerController player;
    player.Reset(glm::vec3(0.0f));

    PlayerInputCmd settle_input;
    player.Tick(settle_input, FlatTerrain);

    PlayerInputCmd jump_input;
    jump_input.jump = true;
    player.Tick(jump_input, FlatTerrain);

    PlayerInputCmd no_input;
    for (int tick = 0; tick < 120 && !player.IsGrounded(); ++tick) {
        player.Tick(no_input, FlatTerrain);
    }

    ASSERT_TRUE(player.IsGrounded());
    EXPECT_FLOAT_EQ(player.GetHealth(), player.GetMaximumHealth());
}

TEST(RuntimeCollisionTest, GroundProbeHonorsStepBudgetAndSlopeNormal) {
    PlayerCollision collision;
    auto sloped_terrain = [](float x, float) -> float { return x * 0.25f; };

    PlayerGroundQuery query = collision.QueryGround(
        glm::vec3(0.0f, 0.0f, 1024.0f),
        PlayerController::STANDING_EYE_HEIGHT,
        sloped_terrain,
        true,
        false);

    EXPECT_TRUE(query.is_grounded);
    EXPECT_FLOAT_EQ(query.ground_height, 0.0f);
    EXPECT_EQ(query.step_down_budget, 2048.0f);
    EXPECT_GT(query.surface_normal.z, 0.9f);
    EXPECT_LT(query.surface_normal.x, 0.0f);
}

TEST(RuntimeCollisionTest, AccumulatesReferenceSlopeSlideOnlyWhileGrounded) {
    const glm::vec3 moderate_slope_normal(-0.83205f, 0.0f, 0.55470f);

    const glm::vec3 slide_velocity = PlayerCollision::AccumulateSlopeSlide(
        glm::vec3(0.0f),
        moderate_slope_normal,
        true);
    EXPECT_LT(slide_velocity.x, 0.0f);
    EXPECT_FLOAT_EQ(slide_velocity.y, 0.0f);
    EXPECT_FLOAT_EQ(slide_velocity.z, 0.0f);

    EXPECT_EQ(
        PlayerCollision::AccumulateSlopeSlide(
            glm::vec3(100.0f, 20.0f, 0.0f),
            moderate_slope_normal,
            false),
        glm::vec3(0.0f));
    EXPECT_EQ(
        PlayerCollision::AccumulateSlopeSlide(
            glm::vec3(100.0f, 20.0f, 0.0f),
            glm::vec3(0.0f, 0.0f, 1.0f),
            true),
        glm::vec3(0.0f));
}

TEST(RuntimeCollisionTest, WallSweepStopsAtSolidGeometryAndSlides) {
    PlayerCollision collision;
    collision.SetSolidQuery([](const glm::vec3& sample_position) {
        return sample_position.x >= 4096.0f;
    });

    PlayerWallSweepResult result = collision.SweepWalls(
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(8192.0f, 8192.0f, 0.0f),
        512.0f,
        PlayerController::STANDING_EYE_HEIGHT);

    EXPECT_TRUE(result.hit_wall);
    EXPECT_LT(result.hit_fraction, 1.0f);
    EXPECT_LT(result.slide_velocity.x, 4096.0f);
    EXPECT_GT(result.slide_velocity.y, 7000.0f);
    EXPECT_LT(result.wall_normal.x, -0.9f);
}

TEST(RuntimeCollisionTest, CeilingQueryPreventsStandingInLowClearance) {
    PlayerCollision collision;
    collision.SetCeilingQuery([](const glm::vec3&) {
        return 6000.0f;
    });

    EXPECT_FALSE(collision.CanStandUp(
        glm::vec3(0.0f, 0.0f, 0.0f),
        PlayerController::STANDING_EYE_HEIGHT,
        FlatTerrain));
    EXPECT_TRUE(collision.CanStandUp(
        glm::vec3(0.0f, 0.0f, 0.0f),
        PlayerController::CROUCHING_EYE_HEIGHT,
        FlatTerrain));
}

TEST(RuntimePlayerTest, ControllerUsesCollisionBoundaryForFixedStepMovement) {
    PlayerController player;
    player.Reset(glm::vec3(0.0f, 0.0f, 0.0f));
    player.SetCollisionQuery([](const glm::vec3& sample_position) {
        return sample_position.x >= 100.0f;
    });

    PlayerInputCmd input;
    input.strafe = 1.0f;
    player.Tick(input, FlatTerrain);

    EXPECT_LT(player.GetPosition().x, 100.0f);
    EXPECT_TRUE(player.IsGrounded());
}

TEST(RuntimePlayerTest, AppliesExplicitMovementTuningToFixedStepController) {
    PlayerController player;
    PlayerController::Tuning tuning;
    tuning.maximum_health = 75.0f;
    tuning.maximum_armor = 25.0f;
    tuning.walk_speed_units_per_tick = 123.0f;
    tuning.jump_speed_units_per_tick = 456.0f;
    tuning.gravity_units_per_tick = 12.0f;
    tuning.standing_eye_height_units = 5000.0f;
    tuning.crouching_eye_height_units = 3000.0f;
    player.ApplyTuning(tuning);
    player.Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    EXPECT_FLOAT_EQ(player.GetMaximumHealth(), 75.0f);
    EXPECT_FLOAT_EQ(player.GetMaximumArmor(), 25.0f);
    EXPECT_FLOAT_EQ(player.GetEyeHeight(), 5000.0f);

    PlayerInputCmd move_command;
    move_command.forward = 1.0f;
    player.Tick(move_command, FlatTerrain);
    EXPECT_FLOAT_EQ(player.GetVelocity().y, 123.0f);

    player.Reset(glm::vec3(0.0f, 0.0f, 0.0f));
    move_command = PlayerInputCmd();
    move_command.jump = true;
    player.Tick(move_command, FlatTerrain);
    EXPECT_FLOAT_EQ(player.GetVelocity().z, 456.0f);
}

TEST(RuntimePlayerTest, ConvertsHumanPlayerMetersToRuntimeUnitsPerTick) {
    HumanPlayerTuning human_player_tuning;
    human_player_tuning.walk_speed = 3.0f;
    human_player_tuning.run_speed = 6.0f;
    human_player_tuning.crouch_speed = 1.5f;
    human_player_tuning.jump_impulse = 777.0f;
    human_player_tuning.gravity = 18.0f;
    human_player_tuning.eye_height_stand = 1.75f;
    human_player_tuning.eye_height_crouch = 0.90f;

    const PlayerController::Tuning controller_tuning =
        human_player_tuning.ToControllerTuning();

    EXPECT_FLOAT_EQ(
        controller_tuning.walk_speed_units_per_tick,
        3.0f * PlayerController::WORLD_METER / 30.0f);
    EXPECT_FLOAT_EQ(
        controller_tuning.run_speed_units_per_tick,
        6.0f * PlayerController::WORLD_METER / 30.0f);
    EXPECT_FLOAT_EQ(controller_tuning.jump_speed_units_per_tick, 777.0f);
    EXPECT_FLOAT_EQ(controller_tuning.gravity_units_per_tick,
                    18.0f * PlayerController::WORLD_METER / (30.0f * 30.0f));
    EXPECT_FLOAT_EQ(controller_tuning.standing_eye_height_units, 1.75f * PlayerController::WORLD_METER);
    EXPECT_FLOAT_EQ(controller_tuning.crouching_eye_height_units, 0.90f * PlayerController::WORLD_METER);
}

// 5. Weapon Fire & Ballistics Tests
TEST(RuntimeWeaponTest, UsesRetailPlayerCycleAndWeaponModels) {
    WeaponSystem weapons;

    EXPECT_EQ(weapons.GetActiveWeapon().id, 20U); // WEAPON_ID_KNIFE
    EXPECT_EQ(weapons.GetActiveWeapon().model_id, "133_01_1");

    ASSERT_TRUE(weapons.SelectWeaponSlot(6)); // WEAPON_ID_MP5SD
    EXPECT_EQ(weapons.GetActiveWeapon().id, 7U);
    EXPECT_EQ(weapons.GetActiveWeapon().script_id, "WEAPON_ID_MP5SD");
    EXPECT_EQ(weapons.GetActiveWeapon().model_id, "103_01_1");
    EXPECT_EQ(weapons.GetActiveWeapon().rounds_per_minute, 700.0f);
    EXPECT_EQ(weapons.GetActiveWeapon().clip_capacity, 32U);
    EXPECT_EQ(weapons.GetActiveWeapon().fire_sound, "mp5sd_loop");
}

TEST(RuntimeWeaponTest, ResolvesAuthoredWeaponIdentifiersWithDecorativeText) {
    WeaponSystem weapons;

    ASSERT_TRUE(weapons.SelectWeaponByScriptId("  \"WEAPON_ID_MP5SD\"  "));
    EXPECT_EQ(weapons.GetActiveWeapon().id, 7U);
}

TEST(RuntimeWeaponTest, ClassifiesVanillaThrownWeaponsForProjectileRuntime) {
    WeaponSystem weapons;

    ASSERT_TRUE(weapons.SelectWeapon(14));
    EXPECT_EQ(weapons.GetActiveWeapon().projectile_type, ProjectileType::FragGrenade);

    ASSERT_TRUE(weapons.SelectWeapon(15));
    EXPECT_EQ(weapons.GetActiveWeapon().projectile_type, ProjectileType::Flashbang);

    ASSERT_TRUE(weapons.SelectWeapon(16));
    EXPECT_EQ(weapons.GetActiveWeapon().projectile_type, ProjectileType::ProximityMine);

    ASSERT_TRUE(weapons.SelectWeapon(12));
    EXPECT_EQ(weapons.GetActiveWeapon().projectile_type, ProjectileType::Rocket);
}

TEST(RuntimeWeaponTest, UsesRetailAutomaticCadenceAndResetsBurstOnRelease) {
    WeaponSystem weapons;
    ASSERT_TRUE(weapons.SelectWeaponSlot(6)); // MP5SD: 700 RPM -> 2 fixed ticks

    BulletTrace first_trace;
    EXPECT_TRUE(weapons.TryFire(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), first_trace));
    EXPECT_NE(first_trace.direction, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 31U);

    weapons.Update(1.0 / 30.0, true);
    BulletTrace cooling_trace;
    EXPECT_FALSE(weapons.TryFire(
        glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), cooling_trace));

    weapons.Update(1.0 / 30.0, true);
    EXPECT_TRUE(weapons.TryFire(
        glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), cooling_trace));
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 30U);

    weapons.Update(1.0 / 30.0, false);
    weapons.Update(1.0 / 30.0, false);
    EXPECT_TRUE(weapons.TryFire(
        glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), cooling_trace));
}

TEST(RuntimeWeaponTest, PreservesAmmoWhenCyclingThePlayerLoadout) {
    WeaponSystem weapons;
    ASSERT_TRUE(weapons.SelectWeaponSlot(6)); // MP5SD

    BulletTrace trace;
    ASSERT_TRUE(weapons.TryFire(
        glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), trace));
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 31U);

    ASSERT_TRUE(weapons.SelectWeaponSlot(0));
    ASSERT_TRUE(weapons.SelectWeaponSlot(6));
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 31U);
}

TEST(RuntimeWorldTest, LowersViewBeforeApplyingWeaponSelection) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    ASSERT_EQ(world.GetWeapons().GetActiveWeapon().id, 20U);

    PlayerInputCmd select_weapon_command;
    select_weapon_command.switch_weapon = 6;
    world.UpdateSimulationTick(0, select_weapon_command);

    EXPECT_EQ(world.GetWeapons().GetActiveWeapon().id, 20U);
    EXPECT_TRUE(world.IsWeaponViewTransitioning());

    for (uint64_t tick = 1; tick <= WeaponViewSway::TicksToTravel; ++tick) {
        world.UpdateSimulationTick(tick, PlayerInputCmd());
    }

    EXPECT_EQ(world.GetWeapons().GetActiveWeapon().id, 7U);
    EXPECT_FLOAT_EQ(
        world.GetWeaponViewSway().GetPitchRadians(),
        WeaponViewSway::LoweredPitchRadians);

    for (uint64_t tick = WeaponViewSway::TicksToTravel + 1;
         tick <= WeaponViewSway::TicksToTravel * 2;
         ++tick) {
        world.UpdateSimulationTick(tick, PlayerInputCmd());
    }

    EXPECT_FALSE(world.IsWeaponViewTransitioning());
    EXPECT_FLOAT_EQ(world.GetWeaponViewSway().GetPitchRadians(), 0.0f);
}

TEST(RuntimeWeaponTest, EmitsRetailShotgunPelletsAsOneAmmoConsumingShot) {
    WeaponSystem weapons;
    ASSERT_TRUE(weapons.SelectWeaponSlot(9)); // WEAPON_ID_SPAS12

    std::vector<BulletTrace> traces;
    ASSERT_TRUE(weapons.TryFire(
        glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        traces));

    EXPECT_EQ(traces.size(), 30U);
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 6U);
    EXPECT_TRUE(std::any_of(
        traces.begin() + 1,
        traces.end(),
        [&traces](const BulletTrace& trace) {
            return trace.direction != traces.front().direction;
        }));
}

TEST(RuntimeWeaponTest, FireAndRecoilCooldown) {
    WeaponSystem weapons;
    ASSERT_TRUE(weapons.SelectWeapon(4)); // WEAPON_ID_M16A2

    BulletTrace trace;
    bool fired = weapons.TryFire(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), trace);
    EXPECT_TRUE(fired);
    EXPECT_TRUE(trace.hit);
    EXPECT_GT(trace.distance, PlayerController::WORLD_METER);
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 19U);

    // Rapid second shot must be throttled by RPM cooldown
    bool fired_immediately = weapons.TryFire(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), trace);
    EXPECT_FALSE(fired_immediately);

    // Advance cooldown
    for (int tick = 0; tick < 3; ++tick) {
        weapons.Update(1.0 / 30.0, true);
    }
    bool fired_after_cooldown = weapons.TryFire(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), trace);
    EXPECT_TRUE(fired_after_cooldown);
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 18U);
}

// 6. AI Perception Dual-Cone Tests
TEST(RuntimeAiTest, VisionConeDirectAndPeripheral) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 1;
    guard.position = glm::vec3(0.0f, 0.0f, 0.0f);
    guard.yaw = 0.0f; // Facing +Y
    ai.RegisterGuard(guard);

    // Unit scale: 1 world meter = 4096 units (PlayerController::WORLD_METER).
    const float kMeters = 4096.0f;
    const float kEyeHeight = 1.7f * kMeters; // eye sits ~1.7m above the feet

    // Target directly in front (30m ahead at eye level) -> Primary cone (0x101)
    AiVisionResult res1 = ai.CheckVision(guard, glm::vec3(0.0f, 30.0f * kMeters, kEyeHeight), false);
    EXPECT_EQ(res1, AiVisionResult::Primary);

    // Target behind guard -> None
    AiVisionResult res2 = ai.CheckVision(guard, glm::vec3(0.0f, -20.0f * kMeters, kEyeHeight), false);
    EXPECT_EQ(res2, AiVisionResult::None);

    // Target in wide peripheral angle (approx 60 degrees, past the 45-degree
    // primary yaw cone) -> Peripheral
    AiVisionResult res3 = ai.CheckVision(guard, glm::vec3(17.32f * kMeters, 10.0f * kMeters, kEyeHeight), false);
    EXPECT_EQ(res3, AiVisionResult::Peripheral);
}

TEST(RuntimeAiTest, VisionUsesPlayerEyeWhileChaseUsesPlayerFeet) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 8;
    guard.position = glm::vec3(0.0f);
    guard.yaw = 0.0f;
    guard.state = AiGuardState::Patrol;
    ai.RegisterGuard(guard);

    // Vanilla patrols ignore the player until an alarm source wakes the
    // mission; primary sight escalates to combat only then.
    ai.SetAlarmActive(true);

    const glm::vec3 player_feet(
        0.0f,
        10.0f * PlayerController::WORLD_METER,
        0.0f);
    const glm::vec3 player_eye = player_feet + glm::vec3(
        0.0f,
        0.0f,
        PlayerController::STANDING_EYE_HEIGHT);
    ai.Update(
        GameClock::TICK_INTERVAL_SECONDS,
        player_feet,
        player_eye,
        true);

    ASSERT_EQ(ai.GetGuards().size(), 1U);
    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Combat);
}

TEST(RuntimeAiTest, DormantAlarmKeepsPatrolCalmOnPrimarySight) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 8;
    guard.position = glm::vec3(0.0f);
    guard.yaw = 0.0f;
    guard.state = AiGuardState::Patrol;
    ai.RegisterGuard(guard);

    const glm::vec3 player_feet(
        0.0f,
        10.0f * PlayerController::WORLD_METER,
        0.0f);
    const glm::vec3 player_eye = player_feet + glm::vec3(
        0.0f,
        0.0f,
        PlayerController::STANDING_EYE_HEIGHT);
    ai.Update(
        GameClock::TICK_INTERVAL_SECONDS,
        player_feet,
        player_eye,
        true);

    ASSERT_EQ(ai.GetGuards().size(), 1U);
    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Patrol);
}

TEST(RuntimeAiTest, PatrolFallbackMovesGuardsWithoutScriptData) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 7;
    guard.state = AiGuardState::Patrol;
    guard.position = glm::vec3(0.0f, 0.0f, 0.0f);
    guard.waypoints = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 8192.0f, 0.0f),
    };
    ai.RegisterGuard(guard);

    ai.Update(GameClock::TICK_INTERVAL_SECONDS, glm::vec3(100000.0f), false);
    ai.Update(GameClock::TICK_INTERVAL_SECONDS, glm::vec3(100000.0f), false);

    ASSERT_EQ(ai.GetGuards().size(), 1U);
    EXPECT_GT(ai.GetGuards()[0].position.y, 0.0f);
}

// Vanilla AI has no per-move world probe: graph/waypoint movement is
// authored-safe, so an installed collision query must not block patrol steps.
TEST(RuntimeAiTest, PatrolMovementIgnoresWorldProbeLikeVanilla) {
    AiSystem ai;
    ai.SetMovementCollisionQuery([](const glm::vec3& position) {
        return position.y > 1.0f;
    });

    AiGuardEntity guard;
    guard.id = 12;
    guard.position = glm::vec3(0.0f);
    guard.current_waypoint = 1;
    guard.waypoints = {
        glm::vec3(0.0f, 0.0f, 0.0f),
        glm::vec3(0.0f, 100000.0f, 0.0f),
    };
    ai.RegisterGuard(guard);

    ai.Update(GameClock::TICK_INTERVAL_SECONDS, glm::vec3(100000.0f), false);

    ASSERT_EQ(ai.GetGuards().size(), 1U);
    EXPECT_GT(ai.GetGuards()[0].position.y, 0.0f);
}

TEST(RuntimeAiTest, PatrolAnimationCommandPublishesMonotonicRequest) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 14;
    guard.state = AiGuardState::Patrol;
    guard.patrol_commands = {
        AiPatrolCommand{AiPatrolCommandKind::Animation, 37},
        AiPatrolCommand{AiPatrolCommandKind::Quit, -1},
    };
    ai.RegisterGuard(guard);

    ai.Update(GameClock::TICK_INTERVAL_SECONDS, glm::vec3(100000.0f), false);

    ASSERT_EQ(ai.GetGuards().size(), 1U);
    EXPECT_EQ(ai.GetGuards()[0].requested_animation, 37);
    EXPECT_EQ(ai.GetGuards()[0].animation_request_serial, 1U);
}

TEST(RuntimeAiTest, GuardDoesNotHearItsOwnGunshotStimulus) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 15;
    guard.state = AiGuardState::Combat;
    ai.RegisterGuard(guard);

    AiStimulusEvent gunshot;
    gunshot.type = AiEventType::Gunshot;
    gunshot.originator_id = guard.id;
    gunshot.position = guard.position;
    gunshot.loudness = 1.0f;
    ai.GetEventQueue().Post(gunshot);

    ai.Update(GameClock::TICK_INTERVAL_SECONDS, glm::vec3(100000.0f), false);

    ASSERT_EQ(ai.GetGuards().size(), 1U);
    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Combat);
    EXPECT_FLOAT_EQ(ai.GetGuards()[0].suspicion, 0.0f);
}

TEST(RuntimeAiTest, RetailInvulnerabilityFlagBlocksDamage) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 13;
    guard.script_invulnerable = true;
    ai.RegisterGuard(guard);

    ai.ApplyDamage(guard.id, 25.0f);

    ASSERT_EQ(ai.GetGuards().size(), 1U);
    EXPECT_FLOAT_EQ(ai.GetGuards()[0].health, 100.0f);
    EXPECT_EQ(ai.GetGuards()[0].state, AiGuardState::Patrol);
}

// 7. Mission Flow & Objective Tests
TEST(RuntimeLevelFlowTest, ObjectiveSuccessFlow) {
    LevelFlow flow;
    flow.InitializeMission(1);
    EXPECT_EQ(flow.GetStatus(), MissionStatus::InProgress);

    // Player dies -> Mission failed
    flow.Update(false, false);
    EXPECT_EQ(flow.GetStatus(), MissionStatus::Failed);

    // Reinitialize and complete objective
    flow.InitializeMission(1);
    flow.SetObjectiveState(1, ObjectiveState::Completed);
    flow.Update(true, true); // Player alive in extraction
    EXPECT_EQ(flow.GetStatus(), MissionStatus::Success);
}

TEST(RuntimeLevelFlowTest, ActiveObjectiveAdvancesThroughInteraction) {
    LevelFlow flow;
    flow.InitializeMission(1);

    EXPECT_EQ(flow.GetObjectiveDisplayText(),
              "Infiltrate the trainyard and download train schedule");
    EXPECT_TRUE(flow.CompleteFirstPendingPrimaryObjective());
    EXPECT_EQ(flow.GetObjectiveDisplayText(), "Reach the extraction zone");
    EXPECT_FALSE(flow.CompleteFirstPendingPrimaryObjective());
}

TEST(RuntimeInputTest, ActivateIsMomentaryAndFocusGated) {
    WindowInputRouter router;
    router.SetFocus(WindowFocusTarget::GameplayWindow);
    router.OnKeyboardKey('E', true);

    PlayerInputCmd command = router.ConsumeGameplayInput();
    EXPECT_TRUE(command.interact);
    EXPECT_FALSE(router.ConsumeGameplayInput().interact);

    router.SetFocus(WindowFocusTarget::EditorWindow);
    router.OnKeyboardKey('E', true);
    EXPECT_FALSE(router.ConsumeGameplayInput().interact);
}

TEST(RuntimeInputTest, RightMouseIsAimWithoutImplicitReload) {
    WindowInputRouter router;
    router.SetFocus(WindowFocusTarget::GameplayWindow);
    router.OnMouseButton(2, true);

    const PlayerInputCmd command = router.ConsumeGameplayInput();
    EXPECT_TRUE(command.zoom);
    EXPECT_FALSE(command.reload);
}

TEST(RuntimeInputTest, MapComputerActionIsMomentaryAndFocusGated) {
    WindowInputRouter router;
    router.SetFocus(WindowFocusTarget::GameplayWindow);
    router.OnKeyboardKey('C', true);

    EXPECT_TRUE(router.ConsumeGameplayInput().map_computer);
    EXPECT_FALSE(router.ConsumeGameplayInput().map_computer);

    router.SetFocus(WindowFocusTarget::EditorWindow);
    router.OnKeyboardKey('C', true);
    EXPECT_FALSE(router.ConsumeGameplayInput().map_computer);
}

TEST(RuntimeInputTest, MapComputerDragAndWheelDoNotBecomePlayerLookOrFire) {
    WindowInputRouter router;
    router.SetFocus(WindowFocusTarget::GameplayWindow);
    router.SetMapComputerOpen(true);

    router.OnMouseButton(1, true);
    router.OnMouseMove(40.0f, -20.0f);
    router.OnMouseWheel(1);
    router.OnMouseButton(1, false);

    const PlayerInputCmd command = router.ConsumeGameplayInput();
    EXPECT_FLOAT_EQ(command.yaw_delta, 0.0f);
    EXPECT_FLOAT_EQ(command.pitch_delta, 0.0f);
    EXPECT_FLOAT_EQ(command.map_pan_delta_x, 0.04f);
    EXPECT_FLOAT_EQ(command.map_pan_delta_y, -0.02f);
    EXPECT_EQ(command.map_zoom_delta, 1);
}

TEST(RuntimeInputTest, VanillaDefaultSeparatesRightCtrlCrouchFromMapComputer) {
    WindowInputRouter router;
    router.SetFocus(WindowFocusTarget::GameplayWindow);

    router.OnKeyboardKey(17, true);
    EXPECT_TRUE(router.ConsumeGameplayInput().crouch);
    router.OnKeyboardKey(17, false);

    router.OnKeyboardKey('C', true);
    EXPECT_TRUE(router.ConsumeGameplayInput().map_computer);
    EXPECT_FALSE(router.ConsumeGameplayInput().crouch);
}

TEST(RuntimeWorldTest, MapComputerTogglesOnGroundRisingEdgesOnly) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    PlayerInputCmd open_command;
    open_command.map_computer = true;
    world.UpdateSimulationTick(0, open_command);
    EXPECT_TRUE(world.IsMapComputerOpen());

    world.UpdateSimulationTick(1, open_command);
    EXPECT_TRUE(world.IsMapComputerOpen());

    world.UpdateSimulationTick(2, PlayerInputCmd());
    world.UpdateSimulationTick(3, open_command);
    EXPECT_FALSE(world.IsMapComputerOpen());
}

TEST(RuntimeWorldTest, MapComputerSuppressesWeaponControlsUntilClosed) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    ASSERT_TRUE(world.GetWeapons().SelectWeapon(4));

    PlayerInputCmd open_command;
    open_command.map_computer = true;
    world.UpdateSimulationTick(0, open_command);
    ASSERT_TRUE(world.IsMapComputerOpen());

    const uint32_t ammo_before_map_fire =
        world.GetWeapons().GetCurrentClipAmmo();
    PlayerInputCmd fire_command;
    fire_command.fire = true;
    world.UpdateSimulationTick(1, fire_command);
    EXPECT_EQ(world.GetWeapons().GetCurrentClipAmmo(), ammo_before_map_fire);

    world.UpdateSimulationTick(2, PlayerInputCmd());
    PlayerInputCmd close_and_fire_command;
    close_and_fire_command.map_computer = true;
    close_and_fire_command.fire = true;
    world.UpdateSimulationTick(3, close_and_fire_command);
    EXPECT_FALSE(world.IsMapComputerOpen());
    EXPECT_LT(
        world.GetWeapons().GetCurrentClipAmmo(),
        ammo_before_map_fire);
}

TEST(RuntimeWorldTest, MapComputerPanAndZoomUpdateTheVanillaViewport) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    PlayerInputCmd open_command;
    open_command.map_computer = true;
    world.UpdateSimulationTick(0, open_command);
    ASSERT_TRUE(world.IsMapComputerOpen());

    const glm::vec3 center_before = world.GetMapComputerCenter();
    const float field_of_view_before = world.GetMapComputerFieldOfView();

    PlayerInputCmd map_command;
    map_command.map_pan_delta_x = 0.01f;
    map_command.map_pan_delta_y = -0.01f;
    map_command.map_zoom_delta = 1;
    world.UpdateSimulationTick(1, map_command);

    const glm::vec3 center_after = world.GetMapComputerCenter();
    EXPECT_LT(center_after.x, center_before.x);
    EXPECT_LT(center_after.y, center_before.y);
    EXPECT_LT(world.GetMapComputerFieldOfView(), field_of_view_before);
    EXPECT_EQ(world.GetMapComputerZoomLevel(), 2);
}

TEST(RuntimeWorldTest, MapComputerFreezesPlayerPresentationAndInteractionInput) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    int interaction_call_count = 0;
    world.SetInteractionQuery(
        [&interaction_call_count](
            const glm::vec3&,
            const glm::vec3&) {
            ++interaction_call_count;
            return RuntimeInteractionResult();
        });

    PlayerInputCmd open_command;
    open_command.map_computer = true;
    world.UpdateSimulationTick(0, open_command);
    ASSERT_TRUE(world.IsMapComputerOpen());

    const glm::vec3 position_before_map_input =
        world.GetPlayer().GetPosition();
    const float yaw_before_map_input = world.GetPlayer().GetYaw();
    const float pitch_before_map_input = world.GetPlayer().GetPitch();

    PlayerInputCmd map_input;
    map_input.forward = 1.0f;
    map_input.strafe = 1.0f;
    map_input.yaw_delta = 12.0f;
    map_input.pitch_delta = -6.0f;
    map_input.jump = true;
    map_input.crouch = true;
    map_input.interact = true;
    world.UpdateSimulationTick(1, map_input);

    EXPECT_EQ(world.GetPlayer().GetPosition(), position_before_map_input);
    EXPECT_FLOAT_EQ(world.GetPlayer().GetYaw(), yaw_before_map_input);
    EXPECT_FLOAT_EQ(world.GetPlayer().GetPitch(), pitch_before_map_input);
    EXPECT_EQ(interaction_call_count, 0);
}

TEST(RuntimeInputTest, RelativeMouseLookDeltasAreConsumedOnce) {
    WindowInputRouter router;
    router.SetFocus(WindowFocusTarget::GameplayWindow);
    router.OnMouseMove(12.0f, -4.0f);

    const PlayerInputCmd command = router.ConsumeGameplayInput();
    EXPECT_LT(command.yaw_delta, 0.0f);
    EXPECT_GT(command.pitch_delta, 0.0f);

    const PlayerInputCmd next_command = router.ConsumeGameplayInput();
    EXPECT_FLOAT_EQ(next_command.yaw_delta, 0.0f);
    EXPECT_FLOAT_EQ(next_command.pitch_delta, 0.0f);
}

TEST(RuntimeInputTest, OpposingMovementKeysRemainIndependent) {
    WindowInputRouter router;
    router.SetFocus(WindowFocusTarget::GameplayWindow);

    router.OnKeyboardKey('W', true);
    router.OnKeyboardKey('S', true);
    router.OnKeyboardKey('W', false);

    EXPECT_FLOAT_EQ(router.ConsumeGameplayInput().forward, -1.0f);

    router.OnKeyboardKey('S', false);
    EXPECT_FLOAT_EQ(router.ConsumeGameplayInput().forward, 0.0f);
}

TEST(RuntimeInputTest, FocusLossClearsHeldGameplayState) {
    WindowInputRouter router;
    router.SetFocus(WindowFocusTarget::GameplayWindow);
    router.OnKeyboardKey('W', true);
    router.OnMouseMove(12.0f, -4.0f);

    router.SetFocus(WindowFocusTarget::EditorWindow);
    const PlayerInputCmd editor_focused_input = router.ConsumeGameplayInput();
    EXPECT_FLOAT_EQ(editor_focused_input.forward, 0.0f);
    EXPECT_FLOAT_EQ(editor_focused_input.yaw_delta, 0.0f);
    EXPECT_FLOAT_EQ(editor_focused_input.pitch_delta, 0.0f);

    router.SetFocus(WindowFocusTarget::GameplayWindow);
    EXPECT_FLOAT_EQ(router.ConsumeGameplayInput().forward, 0.0f);
}

TEST(RuntimeDoorStateTest, SlidingDoorUsesAuthoredThirtyHertzOpenTime) {
    RuntimeDoorDefinition definition;
    definition.slide_offset_units = glm::vec3(-PlayerController::WORLD_METER, 0.0f, 0.0f);
    definition.open_time_seconds = 1.0f;

    RuntimeDoorState door(definition);
    EXPECT_TRUE(door.IsFullyClosed());
    EXPECT_FALSE(door.IsFullyOpen());

    door.CommandOpen();
    door.Tick();

    EXPECT_NEAR(door.GetSlideFraction(), 1.0f / 30.0f, 0.000001f);
    // Vanilla scales the authored stop positively by the fraction and never
    // flips it mid-travel (DoorRegistry.cs DrawPositionAt, lines 76-86).
    EXPECT_EQ(
        door.GetSlideOffsetUnits(),
        glm::vec3(-PlayerController::WORLD_METER / 30.0f, 0.0f, 0.0f));
    EXPECT_FALSE(door.IsFullyClosed());
    EXPECT_FALSE(door.IsFullyOpen());
    EXPECT_TRUE(door.WasFullyClosed());
    EXPECT_EQ(door.GetUseState(), RuntimeDoorUseState::Opening);

    for (int tick = 1; tick < 30; ++tick) {
        door.Tick();
    }

    EXPECT_FLOAT_EQ(door.GetSlideFraction(), 1.0f);
    EXPECT_EQ(door.GetSlideOffsetUnits(), glm::vec3(-PlayerController::WORLD_METER, 0.0f, 0.0f));
    EXPECT_TRUE(door.IsFullyOpen());
    EXPECT_FALSE(door.IsFullyClosed());
    EXPECT_EQ(door.GetTicksOpen(), 1);
    EXPECT_EQ(door.GetUseState(), RuntimeDoorUseState::Open);
}

TEST(RuntimeDoorStateTest, SwingAndSlideReturnToClosedStateWithoutOvershoot) {
    RuntimeDoorDefinition definition;
    definition.maximum_angle_degrees = 90.0f;
    definition.slide_offset_units = glm::vec3(0.0f, PlayerController::WORLD_METER, 0.0f);
    definition.open_time_seconds = 0.5f;

    RuntimeDoorState door(definition);
    door.CommandOpen();
    for (int tick = 0; tick < 15; ++tick) {
        door.Tick();
    }

    EXPECT_TRUE(door.IsFullyOpen());
    EXPECT_NEAR(door.GetAngleRadians(), glm::radians(90.0f), 0.000001f);
    EXPECT_EQ(door.GetSlideOffsetUnits(), glm::vec3(0.0f, PlayerController::WORLD_METER, 0.0f));

    door.CommandClosed();
    door.Tick();
    EXPECT_EQ(door.GetUseState(), RuntimeDoorUseState::Closing);
    for (int tick = 1; tick < 15; ++tick) {
        door.Tick();
    }

    EXPECT_TRUE(door.IsFullyClosed());
    EXPECT_NEAR(door.GetAngleRadians(), 0.0f, 0.000001f);
    EXPECT_EQ(door.GetSlideFraction(), 0.0f);
    EXPECT_EQ(door.GetTicksOpen(), 0);
}

// Vanilla carries the authored Max angle's sign through the whole swing: the
// channel steps |max|/(Open time x 30) degrees a tick from zero onto the
// authored target without normalizing it (DoorState.cs Tick, lines 127-145),
// and the renderer applies that angle as Orientation * RotationZ(angle)
// (DoorRegistry.cs DrawOrientationAt, lines 93-94).
TEST(RuntimeDoorStateTest, SwingFollowsAuthoredMaximumAngleSign) {
    RuntimeDoorDefinition positive;
    positive.maximum_angle_degrees = 90.0f;
    positive.open_time_seconds = 0.5f;

    RuntimeDoorState opens_positive(positive);
    opens_positive.CommandOpen();
    opens_positive.Tick();
    EXPECT_NEAR(
        opens_positive.GetAngleRadians(),
        glm::radians(90.0f / 15.0f),
        0.000001f);

    for (int tick = 1; tick < 15; ++tick) {
        opens_positive.Tick();
        EXPECT_GT(opens_positive.GetAngleRadians(), 0.0f);
    }

    EXPECT_NEAR(opens_positive.GetAngleRadians(), glm::radians(90.0f), 0.000001f);
    EXPECT_TRUE(opens_positive.IsFullyOpen());

    RuntimeDoorDefinition negative;
    negative.maximum_angle_degrees = -90.0f;
    negative.open_time_seconds = 0.5f;

    RuntimeDoorState opens_negative(negative);
    opens_negative.CommandOpen();
    opens_negative.Tick();
    EXPECT_NEAR(
        opens_negative.GetAngleRadians(),
        glm::radians(-90.0f / 15.0f),
        0.000001f);

    for (int tick = 1; tick < 15; ++tick) {
        opens_negative.Tick();
        EXPECT_LT(opens_negative.GetAngleRadians(), 0.0f);
    }

    EXPECT_NEAR(opens_negative.GetAngleRadians(), glm::radians(-90.0f), 0.000001f);
    EXPECT_TRUE(opens_negative.IsFullyOpen());
}

TEST(RuntimeWorldTest, AuthoredDoorPublishesMotionAndMissionState) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({
        "DOOR_OPEN",
        100,
        "Door_408.isOpen",
        "HumanPlayer_0.isDead",
    });
    world.GetLevelFlow().InitializeMission(1, {objective_set});

    RuntimeDoorDefinition definition;
    definition.object_index = 12;
    definition.task_id = "408";
    definition.slide_offset_units = glm::vec3(-PlayerController::WORLD_METER, 0.0f, 0.0f);
    definition.open_time_seconds = 1.0f;
    world.SetAuthoredDoors({definition});

    ASSERT_TRUE(world.ToggleDoor(12));
    EXPECT_FALSE(world.IsDoorFullyOpen(12));

    for (uint64_t tick = 0; tick < 30; ++tick) {
        world.UpdateSimulationTick(tick, PlayerInputCmd());
    }

    ASSERT_EQ(world.GetDoorSnapshots().size(), 1U);
    const RuntimeDoorSnapshot& snapshot = world.GetDoorSnapshots().front();
    EXPECT_TRUE(snapshot.is_fully_open);
    EXPECT_TRUE(snapshot.is_picked);
    EXPECT_EQ(snapshot.ticks_open, 1);
    EXPECT_EQ(world.GetLevelFlow().GetObjectives()[0].state, ObjectiveState::Completed);
}

TEST(RuntimeWorldTest, AuthoredDoorConditionsDriveOpenAndLockedStates) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    RuntimeDoorDefinition definition;
    definition.object_index = 19;
    definition.task_id = "419";
    definition.open_expression = "GatePower";
    definition.locked_expression = "GateLocked";
    definition.slide_offset_units = glm::vec3(-PlayerController::WORLD_METER, 0.0f, 0.0f);
    definition.open_time_seconds = 1.0f;
    world.SetAuthoredDoors({definition});

    world.SetMissionStateBoolean("GateLocked", true);
    world.UpdateSimulationTick(0, PlayerInputCmd());
    EXPECT_FALSE(world.ToggleDoor(19));

    world.SetMissionStateBoolean("GateLocked", false);
    world.SetMissionStateBoolean("GatePower", true);
    for (uint64_t tick = 1; tick < 31; ++tick) {
        world.UpdateSimulationTick(tick, PlayerInputCmd());
    }

    ASSERT_EQ(world.GetDoorSnapshots().size(), 1U);
    EXPECT_TRUE(world.GetDoorSnapshots()[0].is_fully_open);
    EXPECT_FALSE(world.GetDoorSnapshots()[0].is_locked);
}

TEST(RuntimeWorldTest, PlayerFireDamagesGuardUsingWorldUnits) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));
    world.GetPlayer().SetOrientation(0.0f, 0.0f);

    AiGuardEntity guard;
    guard.id = 17;
    guard.position = glm::vec3(0.0f, 4096.0f, 0.0f);
    guard.yaw = 180.0f;
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    PlayerInputCmd input;
    input.fire = true;
    world.UpdateSimulationTick(0, input);

    ASSERT_EQ(world.GetAi().GetGuards().size(), 1U);
    EXPECT_LT(world.GetAi().GetGuards()[0].health, 100.0f);
}

TEST(RuntimeWorldTest, OwnsLadderPlacementsAsRuntimeAssetState) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    std::vector<LadderPlacement> ladder_placements;
    ladder_placements.emplace_back(
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 10000.0f),
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 12288.0f),
        glm::mat3(1.0f));
    world.SetLadderPlacements(std::move(ladder_placements));

    ASSERT_EQ(world.GetLadderPlacements().size(), 1U);
    EXPECT_EQ(world.GetLadderPlacements()[0].GetClimbLine().GetStepCount(), 10);

    world.Reset();
    EXPECT_TRUE(world.GetLadderPlacements().empty());
}

TEST(RuntimeWorldTest, MountsAndTraversesLadderWithDeterministicFallback) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    world.SetLadderPlacements({LadderPlacement(
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 20000.0f),
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 12288.0f),
        glm::mat3(1.0f))});
    world.GetPlayer().Reset(glm::vec3(0.0f, -2000.0f, 1000.0f), 0.0f);

    PlayerInputCmd interact_command;
    interact_command.interact = true;
    world.UpdateSimulationTick(0, interact_command);

    ASSERT_TRUE(world.IsPlayerOnLadder());
    ASSERT_EQ(
        world.GetLadderTraversal().GetPhase(),
        LadderTraversalPhase::Climbing);
    EXPECT_NEAR(
        world.GetPlayer().GetPosition().z,
        world.GetLadderPlacements()[0].GetBottomMount().z,
        0.001f);

    PlayerInputCmd climb_command;
    climb_command.forward = 1.0f;
    world.UpdateSimulationTick(1, climb_command);

    EXPECT_EQ(world.GetLadderTraversal().GetStep(), 1);
    EXPECT_GT(
        world.GetPlayer().GetPosition().z,
        world.GetLadderPlacements()[0].GetBottomMount().z);

    PlayerInputCmd slide_command;
    slide_command.interact = true;
    world.UpdateSimulationTick(2, slide_command);
    EXPECT_EQ(
        world.GetLadderTraversal().GetPhase(),
        LadderTraversalPhase::SlidingDown);

    for (uint64_t tick = 3; tick < 120 && world.IsPlayerOnLadder(); ++tick) {
        world.UpdateSimulationTick(tick, PlayerInputCmd());
    }

    EXPECT_FALSE(world.IsPlayerOnLadder());
    EXPECT_NEAR(world.GetPlayer().GetPosition().z, 0.0f, 0.001f);
}

TEST(RuntimeWorldTest, CompletesAuthoredLadderStepFromRootMotionEvent) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.SetLadderPlacements({LadderPlacement(
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 20000.0f),
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 12288.0f),
        glm::mat3(1.0f))});
    world.GetPlayer().Reset(glm::vec3(0.0f, -2000.0f, 1000.0f), 0.0f);

    PlayerInputCmd mount_command;
    mount_command.interact = true;
    world.UpdateSimulationTick(0, mount_command);

    PlayerInputCmd authored_step_command;
    authored_step_command.forward = 1.0f;
    authored_step_command.root_motion_delta = glm::vec3(0.0f, 0.0f, 600.0f);
    authored_step_command.ladder_step_complete = true;
    world.UpdateSimulationTick(1, authored_step_command);

    EXPECT_EQ(world.GetLadderTraversal().GetStep(), 1);
    EXPECT_NEAR(
        world.GetPlayer().GetPosition().z,
        world.GetLadderPlacements()[0].GetBottomMount().z + 600.0f,
        0.001f);
}

TEST(RuntimeWorldTest, TopLadderMountCompletesFallbackTransition) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.SetLadderPlacements({LadderPlacement(
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 20000.0f),
        glm::vec3(0.0f),
        glm::vec3(0.0f, 0.0f, 12288.0f),
        glm::mat3(1.0f))});
    world.GetPlayer().Reset(glm::vec3(0.0f, -1000.0f, 9000.0f), 0.0f);

    PlayerInputCmd interact_command;
    interact_command.interact = true;
    world.UpdateSimulationTick(0, interact_command);
    ASSERT_EQ(
        world.GetLadderTraversal().GetPhase(),
        LadderTraversalPhase::GettingOnTop);

    world.UpdateSimulationTick(1, PlayerInputCmd());
    EXPECT_EQ(
        world.GetLadderTraversal().GetPhase(),
        LadderTraversalPhase::Climbing);
}

TEST(RuntimeWorldTest, ProjectileDetonationAppliesBlastDamageToNearbyGuard) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AiGuardEntity guard;
    guard.id = 28;
    guard.position = glm::vec3(0.0f, 1.0f * PlayerController::WORLD_METER, 0.0f);
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    ProjectileLaunch launch;
    launch.position = glm::vec3(0.0f);
    launch.type = ProjectileType::FragGrenade;
    launch.fuse_ticks = 1;
    launch.damage = 100.0f;
    launch.explosion_radius_units = 5.0f * PlayerController::WORLD_METER;
    ASSERT_TRUE(world.GetProjectiles().Spawn(launch));

    world.UpdateSimulationTick(0, PlayerInputCmd());

    ASSERT_EQ(world.GetAi().GetGuards().size(), 1U);
    EXPECT_LE(world.GetAi().GetGuards()[0].health, 0.0f);
    EXPECT_EQ(world.GetProjectiles().GetDetonations().size(), 1U);
}

TEST(RuntimeWorldTest, FlashbangAppliesVisiblePlayerExposureAndDecays) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.GetPlayer().Reset(glm::vec3(0.0f));

    ProjectileLaunch launch;
    launch.position = world.GetPlayer().GetEyePosition();
    launch.type = ProjectileType::Flashbang;
    launch.fuse_ticks = 1;
    ASSERT_TRUE(world.GetProjectiles().Spawn(launch));

    world.UpdateSimulationTick(0, PlayerInputCmd());

    EXPECT_GT(world.GetFlashEffectStrength(), 0.9f);
    world.UpdateSimulationTick(1, PlayerInputCmd());
    EXPECT_LT(world.GetFlashEffectStrength(), 1.0f);
    EXPECT_GT(world.GetFlashEffectStrength(), 0.0f);
}

TEST(RuntimeWorldTest, ZoomStateTracksFixedStepInput) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    PlayerInputCmd zoom_command;
    zoom_command.zoom = true;
    world.UpdateSimulationTick(0, zoom_command);
    EXPECT_TRUE(world.IsZoomActive());

    world.UpdateSimulationTick(1, PlayerInputCmd());
    EXPECT_FALSE(world.IsZoomActive());
}

TEST(RuntimeWorldTest, ProjectileWeaponLaunchesOncePerTriggerPress) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    ASSERT_TRUE(world.GetWeapons().SelectWeapon(14));

    PlayerInputCmd fire_command;
    fire_command.fire = true;
    world.UpdateSimulationTick(0, fire_command);
    world.UpdateSimulationTick(1, fire_command);

    EXPECT_EQ(world.GetProjectiles().GetProjectiles().size(), 1U);
    EXPECT_EQ(world.GetWeapons().GetCurrentClipAmmo(), 0U);

    world.UpdateSimulationTick(2, PlayerInputCmd());
    EXPECT_EQ(world.GetProjectiles().GetProjectiles().size(), 1U);
}

TEST(RuntimeWorldTest, MissionRestartRestoresTheInitialWeaponLoadout) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    ASSERT_TRUE(world.GetWeapons().SelectWeaponSlot(6)); // MP5SD

    BulletTrace trace;
    ASSERT_TRUE(world.GetWeapons().TryFire(
        glm::vec3(0.0f),
        glm::vec3(0.0f, 1.0f, 0.0f),
        trace));
    EXPECT_EQ(world.GetWeapons().GetCurrentClipAmmo(), 31U);

    world.Reset();

    ASSERT_TRUE(world.GetWeapons().SelectWeaponSlot(6));
    EXPECT_EQ(world.GetWeapons().GetCurrentClipAmmo(), 32U);
}

TEST(RuntimeWorldTest, ResetPreservesConfiguredPlayerTuning) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    PlayerController::Tuning tuning;
    tuning.maximum_health = 80.0f;
    tuning.maximum_armor = 35.0f;
    tuning.walk_speed_units_per_tick = 111.0f;
    tuning.gravity_units_per_tick = 22.0f;
    tuning.standing_eye_height_units = 6000.0f;
    world.SetPlayerTuning(tuning);
    world.GetPlayer().ApplyDamage(10.0f);

    world.Reset();

    EXPECT_FLOAT_EQ(world.GetPlayer().GetMaximumHealth(), 80.0f);
    EXPECT_FLOAT_EQ(world.GetPlayer().GetMaximumArmor(), 35.0f);
    EXPECT_FLOAT_EQ(world.GetPlayer().GetHealth(), 80.0f);
    EXPECT_FLOAT_EQ(world.GetPlayer().GetArmor(), 35.0f);
    EXPECT_FLOAT_EQ(world.GetPlayer().GetEyeHeight(), 6000.0f);
}

TEST(RuntimeWorldTest, ResetPreservesAuthoredWeaponCycle) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.SetPlayerWeaponCycle({999U, 7U, 7U, 4U});

    world.Reset();

    EXPECT_EQ(world.GetWeapons().GetActiveWeapon().id, 7U);
    ASSERT_TRUE(world.GetWeapons().SelectWeaponSlot(0));
    EXPECT_EQ(world.GetWeapons().GetActiveWeapon().id, 7U);
    ASSERT_TRUE(world.GetWeapons().SelectWeaponSlot(1));
    EXPECT_EQ(world.GetWeapons().GetActiveWeapon().id, 4U);
    EXPECT_FALSE(world.GetWeapons().SelectWeaponSlot(2));
}

TEST(RuntimeWorldTest, SolidGeometryOccludesPlayerFire) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain, WallAtOneMeter);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));
    world.GetPlayer().SetOrientation(0.0f, 0.0f);

    AiGuardEntity guard;
    guard.id = 18;
    guard.position = glm::vec3(0.0f, 2.0f * PlayerController::WORLD_METER, 0.0f);
    guard.yaw = 180.0f;
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    PlayerInputCmd input;
    input.fire = true;
    world.UpdateSimulationTick(0, input);

    ASSERT_EQ(world.GetAi().GetGuards().size(), 1U);
    EXPECT_FLOAT_EQ(world.GetAi().GetGuards()[0].health, 100.0f);
}

TEST(RuntimeWorldTest, CombatGuardDamagesPlayerAtFixedCadence) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.GetPlayer().ApplyTuning(100.0f, 0.0f);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    AiGuardEntity guard;
    guard.id = 23;
    guard.position = glm::vec3(0.0f, 4096.0f, 0.0f);
    guard.yaw = 180.0f;
    guard.state = AiGuardState::Combat;
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    const float initial_health = world.GetPlayer().GetHealth();
    PlayerInputCmd input;
    world.UpdateSimulationTick(30, input);

    EXPECT_LT(world.GetPlayer().GetHealth(), initial_health);
}

TEST(RuntimeWorldTest, CombatGuardFiresTheRuntimeWeaponAtThePlayer) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.GetPlayer().ApplyTuning(100.0f, 0.0f);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    AiGuardEntity guard;
    guard.id = 27;
    guard.position = glm::vec3(0.0f, 2.0f * PlayerController::WORLD_METER, 0.0f);
    guard.yaw = 180.0f;
    guard.state = AiGuardState::Combat;
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    world.UpdateSimulationTick(0, PlayerInputCmd());

    EXPECT_FLOAT_EQ(world.GetPlayer().GetHealth(), 65.0f);
}

TEST(RuntimeWorldTest, CombatGuardUsesAuthoredWeaponScriptIdentifier) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.GetPlayer().ApplyTuning(100.0f, 0.0f);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    AiGuardEntity guard;
    guard.id = 29;
    guard.weapon_script_id = "WEAPON_ID_DESERTEAGLE";
    guard.position = glm::vec3(0.0f, 2.0f * PlayerController::WORLD_METER, 0.0f);
    guard.yaw = 180.0f;
    guard.state = AiGuardState::Combat;
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    world.UpdateSimulationTick(0, PlayerInputCmd());

    EXPECT_FLOAT_EQ(world.GetPlayer().GetHealth(), 50.0f);
}

TEST(RuntimeWorldTest, CombatGuardReloadsAuthoredWeaponAfterEmptyMagazine) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    PlayerController::Tuning player_tuning;
    player_tuning.maximum_health = 10000.0f;
    player_tuning.maximum_armor = 0.0f;
    player_tuning.gravity_units_per_tick = 0.0f;
    world.SetPlayerTuning(player_tuning);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));
    world.UpdateSimulationTick(0, PlayerInputCmd());
    world.GetAi().GetEventQueue().Clear();

    AiGuardEntity guard;
    guard.id = 30;
    guard.weapon_script_id = "WEAPON_ID_M16A2";
    guard.position = glm::vec3(0.0f, 2.0f * PlayerController::WORLD_METER, 0.0f);
    guard.yaw = 180.0f;
    guard.state = AiGuardState::Combat;
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    for (uint64_t tick = 1; tick <= 61; ++tick) {
        world.UpdateSimulationTick(tick, PlayerInputCmd());
    }
    const float health_after_emptying_magazine = world.GetPlayer().GetHealth();

    for (uint64_t tick = 62; tick <= 131; ++tick) {
        world.UpdateSimulationTick(tick, PlayerInputCmd());
    }

    EXPECT_LT(health_after_emptying_magazine, 10000.0f);
    EXPECT_LT(world.GetPlayer().GetHealth(), health_after_emptying_magazine);
}

TEST(RuntimeWorldTest, SolidGeometryBlocksGuardLineOfSightDamage) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain, WallAtOneMeter);
    world.GetPlayer().ApplyTuning(100.0f, 0.0f);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    AiGuardEntity guard;
    guard.id = 24;
    guard.position = glm::vec3(0.0f, 2.0f * PlayerController::WORLD_METER, 0.0f);
    guard.yaw = 180.0f;
    guard.state = AiGuardState::Combat;
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    const float initial_health = world.GetPlayer().GetHealth();
    world.UpdateSimulationTick(30, PlayerInputCmd());

    EXPECT_FLOAT_EQ(world.GetPlayer().GetHealth(), initial_health);
}

TEST(RuntimeWorldTest, SolidGeometryBlocksGuardPerception) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain, WallAtOneMeter);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    AiGuardEntity guard;
    guard.id = 25;
    guard.position = glm::vec3(0.0f, 2.0f * PlayerController::WORLD_METER, 0.0f);
    guard.yaw = 180.0f;
    guard.state = AiGuardState::Patrol;
    world.GetAi().RegisterGuard(guard);

    world.UpdateSimulationTick(0, PlayerInputCmd());

    ASSERT_EQ(world.GetAi().GetGuards().size(), 1U);
    EXPECT_EQ(world.GetAi().GetGuards()[0].state, AiGuardState::Patrol);
    EXPECT_FLOAT_EQ(world.GetAi().GetGuards()[0].suspicion, 0.0f);
}

TEST(RuntimeWorldTest, StaticGeometryRoofPreventsPlayerFromStanding) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain, LowRoofAtStandingHeight);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    PlayerInputCmd crouch_command;
    crouch_command.crouch = true;
    world.UpdateSimulationTick(0, crouch_command);
    EXPECT_EQ(world.GetPlayer().GetStance(), PlayerStanceState::Crouching);

    world.UpdateSimulationTick(1, PlayerInputCmd());

    EXPECT_EQ(world.GetPlayer().GetStance(), PlayerStanceState::Crouching);
    EXPECT_LT(world.GetPlayer().GetEyeHeight(), PlayerController::STANDING_EYE_HEIGHT);
}

TEST(RuntimeWorldTest, RetailAiQvmDrivesGuardActionsOnFixedTicks) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AiGuardEntity guard;
    guard.id = 26;
    guard.position = glm::vec3(0.0f, 100.0f * PlayerController::WORLD_METER, 0.0f);
    guard.state = AiGuardState::Patrol;
    guard.patrol_routes[700] = {
        AiPatrolCommand{AiPatrolCommandKind::Delay, 3},
    };
    world.GetAi().RegisterGuard(guard);

    ASSERT_TRUE(world.AttachGuardScript(guard.id, BuildRetailAiPatrolScript()));

    world.UpdateSimulationTick(0, PlayerInputCmd());
    ASSERT_EQ(world.GetAi().GetGuards().size(), 1U);
    EXPECT_EQ(world.GetAi().GetGuards()[0].script_last_event_type, 0);
    EXPECT_EQ(world.GetAi().GetGuards()[0].script_patrol_path_id, -1);

    world.UpdateSimulationTick(1, PlayerInputCmd());
    EXPECT_EQ(world.GetAi().GetGuards()[0].script_last_event_type, 4);
    EXPECT_EQ(world.GetAi().GetGuards()[0].script_patrol_path_id, 700);
    EXPECT_EQ(world.GetAi().GetGuards()[0].active_patrol_path_id, 700);
    ASSERT_EQ(world.GetAi().GetGuards()[0].patrol_commands.size(), 1U);
    EXPECT_EQ(world.GetAi().GetGuards()[0].patrol_commands[0].operand, 3);
}

TEST(RuntimeWorldTest, RetailAiQvmPlaySoundPublishesAuthoredAudioEvent) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AiGuardEntity guard;
    guard.id = 31;
    guard.position = glm::vec3(100.0f * PlayerController::WORLD_METER, 0.0f, 0.0f);
    guard.state = AiGuardState::Patrol;
    world.GetAi().RegisterGuard(guard);

    ASSERT_TRUE(world.AttachGuardScript(guard.id, BuildRetailAiSoundScript()));
    world.UpdateSimulationTick(0, PlayerInputCmd());

    const std::vector<RuntimeAudioEvent> audio_events =
        world.ConsumePendingAudioEvents();
    ASSERT_EQ(audio_events.size(), 1U);
    EXPECT_EQ(audio_events[0].type, RuntimeAudioEventType::OneShot);
    EXPECT_EQ(audio_events[0].authored_sound, "guard_alert_1");
    EXPECT_FALSE(audio_events[0].relative_to_microphone);
    EXPECT_EQ(audio_events[0].position, guard.position);
}

TEST(RuntimeWorldTest, ActivateCompletesTheCurrentMissionObjective) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.SetExtractionZone(glm::vec3(100000.0f, 0.0f, 0.0f), 100.0f);
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    PlayerInputCmd input;
    input.interact = true;
    world.UpdateSimulationTick(0, input);

    ASSERT_FALSE(world.GetLevelFlow().GetObjectives().empty());
    EXPECT_EQ(world.GetLevelFlow().GetObjectives()[0].state, ObjectiveState::Completed);
    EXPECT_EQ(world.GetLevelFlow().GetStatus(), MissionStatus::InProgress);

    world.GetPlayer().SetPosition(glm::vec3(100000.0f, 0.0f, 0.0f));
    world.UpdateSimulationTick(1, PlayerInputCmd());
    EXPECT_EQ(world.GetLevelFlow().GetStatus(), MissionStatus::Success);
}

TEST(RuntimeWorldTest, ClearingExtractionZoneDisablesFallbackCompletion) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.SetExtractionZone(glm::vec3(100000.0f, 0.0f, 0.0f), 100.0f);
    world.ClearExtractionZone();
    world.GetPlayer().Reset(glm::vec3(0.0f, 0.0f, 0.0f));

    world.GetLevelFlow().SetObjectiveState(1, ObjectiveState::Completed);
    world.GetPlayer().SetPosition(glm::vec3(100000.0f, 0.0f, 0.0f));
    world.UpdateSimulationTick(0, PlayerInputCmd());

    EXPECT_EQ(world.GetLevelFlow().GetStatus(), MissionStatus::InProgress);
}

TEST(RuntimeWorldTest, InteractionProviderSeparatesDoorUseFromObjectiveCompletion) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.SetInteractionQuery(
        [](const glm::vec3&, const glm::vec3&) {
            return RuntimeInteractionResult{true, false};
        });

    PlayerInputCmd input;
    input.interact = true;
    world.UpdateSimulationTick(0, input);

    ASSERT_FALSE(world.GetLevelFlow().GetObjectives().empty());
    EXPECT_EQ(world.GetLevelFlow().GetObjectives()[0].state, ObjectiveState::Pending);
}

TEST(RuntimeWorldTest, MissionExpressionStateDrivesAuthoredObjectiveAtFixedTick) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({
        "M1_OBJ1",
        1120,
        "Terminal_501.isHacked",
        "HumanPlayer_0.isDead",
    });
    world.GetLevelFlow().InitializeMission(1, {objective_set});
    world.SetMissionStateBoolean("Terminal_501.isHacked", true);

    world.UpdateSimulationTick(0, PlayerInputCmd());

    ASSERT_EQ(world.GetLevelFlow().GetObjectives().size(), 1U);
    EXPECT_EQ(world.GetLevelFlow().GetObjectives()[0].state, ObjectiveState::Completed);
}

TEST(RuntimeWorldTest, InteractionEventsCanDriveAuthoredMissionExpressions) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({
        "M8_OBJ2",
        3127,
        "Door_240.isOpen",
        "HumanPlayer_0.isDead",
    });
    world.GetLevelFlow().InitializeMission(8, {objective_set});
    world.SetInteractionQuery(
        [&world](const glm::vec3&, const glm::vec3&) {
            world.SetMissionStateBoolean("Door_240.isOpen", true);
            return RuntimeInteractionResult{true, false};
        });

    PlayerInputCmd input;
    input.interact = true;
    world.UpdateSimulationTick(0, input);

    ASSERT_EQ(world.GetLevelFlow().GetObjectives().size(), 1U);
    EXPECT_EQ(world.GetLevelFlow().GetObjectives()[0].state, ObjectiveState::Completed);
}

TEST(RuntimeWorldTest, AreaActivationLatchesEditVariableBeforeObjectiveEvaluation) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionAreaActivation area;
    area.task_id = "200";
    area.position = glm::vec3(0.0f, 0.0f, 0.0f);
    area.dimensions = glm::vec3(2.0f * PlayerController::WORLD_METER);
    area.criteria = "CRITERIA_HUMAN0";

    AuthoredMissionEditVariable edit_variable;
    edit_variable.task_id = "105";
    edit_variable.initial_value = 0;
    edit_variable.add_expression =
        "EditVariable_105.nValue == 0 && AreaActivate_200.nActive";

    world.SetAuthoredMissionState({area}, {edit_variable});

    AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({
        "M1_OBJ1",
        1120,
        "EditVariable_105.nValue == 1",
        "HumanPlayer_0.isDead",
    });
    world.GetLevelFlow().InitializeMission(1, {objective_set});
    world.GetPlayer().SetPosition(glm::vec3(0.0f, 0.0f, 0.0f));

    world.UpdateSimulationTick(0, PlayerInputCmd());

    ASSERT_EQ(world.GetLevelFlow().GetObjectives().size(), 1U);
    EXPECT_EQ(world.GetLevelFlow().GetObjectives()[0].state, ObjectiveState::Completed);
}

TEST(RuntimeWorldTest, DeadGuardPublishesAuthoredMissionState) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AiGuardEntity guard;
    guard.id = 401;
    guard.mission_state_type = "HumanSoldier";
    guard.mission_task_id = "2050";
    guard.position = glm::vec3(10.0f * PlayerController::WORLD_METER, 0.0f, 0.0f);
    guard.waypoints.push_back(guard.position);
    world.GetAi().RegisterGuard(guard);

    AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({
        "M13_OBJ2",
        2500,
        "HumanSoldier_2050.isDead",
        "HumanPlayer_0.isDead",
    });
    world.GetLevelFlow().InitializeMission(13, {objective_set});
    world.GetAi().ApplyDamage(401, 1000.0f);

    world.UpdateSimulationTick(0, PlayerInputCmd());

    ASSERT_EQ(world.GetLevelFlow().GetObjectives().size(), 1U);
    EXPECT_EQ(world.GetLevelFlow().GetObjectives()[0].state, ObjectiveState::Completed);
}

TEST(RuntimeWorldTest, LevelTimerAndStatusMessageDriveAuthoredMissionFlow) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionLevelTimer level_timer;
    level_timer.task_id = "95";
    level_timer.on_expression = "TimerEnable";

    AuthoredMissionStatusMessage status_message;
    status_message.task_id = "4010";
    status_message.send_expression = "LevelTimer_95.nTick >= 2";
    status_message.text_resource = "MISSION_COMPLETE";
    status_message.display_text = "MISSION COMPLETE";
    status_message.send_once = true;
    status_message.duration_seconds = 0.0f;

    world.SetAuthoredMissionState(
        {},
        {},
        {level_timer},
        {status_message});
    world.SetMissionStateBoolean("TimerEnable", true);

    AuthoredMissionFlowDefinition authored_flow;
    authored_flow.has_level_flow = true;
    authored_flow.complete_expression = "StatusMessage_4010.isSendt";
    world.GetLevelFlow().InitializeMission(1, {}, {}, authored_flow);

    world.UpdateSimulationTick(0, PlayerInputCmd());
    EXPECT_EQ(world.GetLevelFlow().GetStatus(), MissionStatus::InProgress);
    EXPECT_TRUE(world.GetDisplayedMissionStatusMessages().empty());

    world.UpdateSimulationTick(1, PlayerInputCmd());
    EXPECT_EQ(world.GetLevelFlow().GetStatus(), MissionStatus::Success);
    ASSERT_EQ(world.GetDisplayedMissionStatusMessages().size(), 1U);
    EXPECT_EQ(
        world.GetDisplayedMissionStatusMessages()[0].text,
        "MISSION COMPLETE");
}

TEST(RuntimeWorldTest, AuthoredCutScenePublishesFixedTickCompletion) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionCutScene cut_scene;
    cut_scene.task_id = "1204";
    cut_scene.run_expression = "!CutScene_1204.isFinished";
    cut_scene.time_scale = 1.0f;
    cut_scene.duration_seconds = 1.0f;

    AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({
        "CUTSCENE_DONE",
        100,
        "CutScene_1204.isFinished",
        "HumanPlayer_0.isDead",
    });
    world.SetAuthoredMissionState({}, {}, {}, {}, {cut_scene});
    world.GetLevelFlow().InitializeMission(1, {objective_set});

    world.UpdateSimulationTick(0, PlayerInputCmd());
    EXPECT_EQ(world.GetLevelFlow().GetStatus(), MissionStatus::InProgress);
    for (uint64_t tick = 1; tick < 32; ++tick) {
        world.UpdateSimulationTick(tick, PlayerInputCmd());
    }

    EXPECT_EQ(world.GetLevelFlow().GetStatus(), MissionStatus::Success);
}

TEST(RuntimeWorldTest, AuthoredCutSceneHonorsStartTimeAndTimeScale) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionCutScene cut_scene;
    cut_scene.task_id = "1204";
    cut_scene.run_expression = "RunCutScene";
    cut_scene.start_time_seconds = 0.5f;
    cut_scene.time_scale = 2.0f;
    cut_scene.duration_seconds = 1.0f;

    AuthoredMissionObjectiveSet objective_set;
    objective_set.objectives.push_back({
        "CUTSCENE_TICK_STARTED",
        100,
        "CutScene_1204.nTick > 15",
        "HumanPlayer_0.isDead",
    });
    world.SetAuthoredMissionState({}, {}, {}, {}, {cut_scene});
    world.SetMissionStateBoolean("RunCutScene", true);
    world.GetLevelFlow().InitializeMission(1, {objective_set});

    world.UpdateSimulationTick(0, PlayerInputCmd());

    EXPECT_EQ(world.GetLevelFlow().GetStatus(), MissionStatus::Success);

    AuthoredMissionObjectiveSet completion_set;
    completion_set.objectives.push_back({
        "CUTSCENE_SCALED_DONE",
        100,
        "CutScene_1204.isFinished",
        "HumanPlayer_0.isDead",
    });
    world.GetLevelFlow().InitializeMission(1, {completion_set});
    for (uint64_t tick = 1; tick < 62; ++tick) {
        world.UpdateSimulationTick(tick, PlayerInputCmd());
    }

    EXPECT_EQ(world.GetLevelFlow().GetStatus(), MissionStatus::Success);
}

TEST(RuntimeWorldTest, AuthoredCutScenePublishesCameraSnapshot) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionCutScene cut_scene;
    cut_scene.task_id = "1204";
    cut_scene.initial_run = true;
    cut_scene.duration_seconds = 1.0f;
    cut_scene.camera_shots.push_back({
        glm::vec3(100.0f, 200.0f, 300.0f),
        glm::vec3(0.0f),
        1.2f,
        1.0f,
        false,
    });

    world.SetAuthoredMissionState({}, {}, {}, {}, {cut_scene});
    world.UpdateSimulationTick(0, PlayerInputCmd());

    const RuntimeCutSceneCamera& camera = world.GetActiveCutSceneCamera();
    EXPECT_TRUE(camera.active);
    EXPECT_EQ(camera.position, glm::vec3(100.0f, 200.0f, 300.0f));
    EXPECT_EQ(camera.forward, glm::vec3(0.0f, 1.0f, 0.0f));
    EXPECT_FLOAT_EQ(camera.field_of_view_y_radians, 1.2f);
    EXPECT_EQ(camera.shot_index, 0);
}

TEST(RuntimeWorldTest, AuthoredConditionalContainersGateDescendantSnapshots) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionConditionalContainer parent_container;
    parent_container.object_index = 10;
    parent_container.task_id = "10";
    parent_container.condition_expression = "ContainerGate";
    parent_container.descendant_object_indices = {11, 12};

    AuthoredMissionConditionalContainer child_container;
    child_container.object_index = 11;
    child_container.task_id = "11";
    child_container.condition_expression = "TRUE";
    child_container.descendant_object_indices = {13};

    world.SetAuthoredMissionState(
        {}, {}, {}, {}, {}, {}, {},
        {parent_container, child_container});

    ASSERT_EQ(world.GetConditionalContainerSnapshots().size(), 2U);
    EXPECT_FALSE(world.GetConditionalContainerSnapshots()[0].is_running);
    EXPECT_TRUE(world.GetConditionalContainerSnapshots()[1].is_running);
    EXPECT_EQ(
        world.GetConditionalContainerSnapshots()[0].descendant_object_indices,
        std::vector<int>({11, 12}));

    world.SetMissionStateBoolean("ContainerGate", true);
    world.UpdateSimulationTick(0, PlayerInputCmd());

    EXPECT_TRUE(world.GetConditionalContainerSnapshots()[0].is_running);
    EXPECT_TRUE(world.GetConditionalContainerSnapshots()[1].is_running);
}

TEST(RuntimeWorldTest, AuthoredGuardGeneratorGatesPreauthoredGuards) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AiGuardEntity first_guard;
    first_guard.id = 41;
    first_guard.position = glm::vec3(4096.0f, 0.0f, 0.0f);
    first_guard.waypoints = {first_guard.position};
    world.GetAi().RegisterGuard(first_guard);

    AiGuardEntity second_guard;
    second_guard.id = 42;
    second_guard.position = glm::vec3(8192.0f, 0.0f, 0.0f);
    second_guard.waypoints = {second_guard.position};
    world.GetAi().RegisterGuard(second_guard);

    AuthoredMissionGuardGenerator guard_generator;
    guard_generator.object_index = 40;
    guard_generator.task_id = "-1";
    guard_generator.condition_expression = "SpawnGuards";
    guard_generator.maximum_spawns = 1;
    guard_generator.guard_object_indices = {41, 42};

    world.SetAuthoredMissionState(
        {}, {}, {}, {}, {}, {}, {}, {}, {guard_generator});

    ASSERT_EQ(world.GetGuardGeneratorSnapshots().size(), 1U);
    EXPECT_FALSE(world.GetGuardGeneratorSnapshots()[0].is_on);
    EXPECT_TRUE(
        world.GetGuardGeneratorSnapshots()[0].active_guard_object_indices.empty());
    ASSERT_NE(world.GetAi().FindGuard(41), nullptr);
    ASSERT_NE(world.GetAi().FindGuard(42), nullptr);
    EXPECT_FALSE(world.GetAi().FindGuard(41)->runtime_enabled);
    EXPECT_FALSE(world.GetAi().FindGuard(42)->runtime_enabled);

    world.SetMissionStateBoolean("SpawnGuards", true);
    world.UpdateSimulationTick(0, PlayerInputCmd());

    EXPECT_TRUE(world.GetGuardGeneratorSnapshots()[0].is_on);
    EXPECT_EQ(
        world.GetGuardGeneratorSnapshots()[0].active_guard_object_indices,
        std::vector<int>({41}));
    EXPECT_TRUE(world.GetAi().FindGuard(41)->runtime_enabled);
    EXPECT_FALSE(world.GetAi().FindGuard(42)->runtime_enabled);
}

TEST(RuntimeWorldTest, AuthoredConditionalSoundEmitsRisingAndStoppingEdges) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionConditionalSound conditional_sound;
    conditional_sound.task_id = "3005";
    conditional_sound.condition_expression = "SoundTrigger";
    conditional_sound.sound_name = "land_ground_2";
    conditional_sound.simple = false;
    conditional_sound.one_shot = false;

    std::vector<RuntimeMissionSoundEvent> events;
    world.SetMissionSoundEventHandler(
        [&events](const RuntimeMissionSoundEvent& sound_event) {
            events.push_back(sound_event);
        });
    world.SetAuthoredMissionState(
        {}, {}, {}, {}, {}, {conditional_sound});
    world.SetMissionStateBoolean("SoundTrigger", true);
    world.UpdateSimulationTick(0, PlayerInputCmd());
    ASSERT_EQ(events.size(), 1U);
    EXPECT_TRUE(events[0].playing);
    EXPECT_EQ(events[0].sound_name, "land_ground_2");

    world.UpdateSimulationTick(1, PlayerInputCmd());
    EXPECT_EQ(events.size(), 1U);

    world.SetMissionStateBoolean("SoundTrigger", false);
    world.UpdateSimulationTick(2, PlayerInputCmd());
    ASSERT_EQ(events.size(), 2U);
    EXPECT_FALSE(events[1].playing);
}

TEST(RuntimeWorldTest, AuthoredOneShotSoundDoesNotRetrigger) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionConditionalSound conditional_sound;
    conditional_sound.task_id = "3998";
    conditional_sound.condition_expression = "VoiceTrigger";
    conditional_sound.sound_name = "_cut01_01";
    conditional_sound.simple = true;
    conditional_sound.one_shot = true;

    int play_count = 0;
    world.SetMissionSoundEventHandler(
        [&play_count](const RuntimeMissionSoundEvent& sound_event) {
            if (sound_event.playing) {
                ++play_count;
            }
        });
    world.SetAuthoredMissionState(
        {}, {}, {}, {}, {}, {conditional_sound});
    world.SetMissionStateBoolean("VoiceTrigger", true);
    world.UpdateSimulationTick(0, PlayerInputCmd());
    world.SetMissionStateBoolean("VoiceTrigger", false);
    world.UpdateSimulationTick(1, PlayerInputCmd());
    world.SetMissionStateBoolean("VoiceTrigger", true);
    world.UpdateSimulationTick(2, PlayerInputCmd());

    EXPECT_EQ(play_count, 1);
}

TEST(RuntimeWorldTest, FixedStepGameplayQueuesOneShotAudioEvents) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    ASSERT_TRUE(world.GetWeapons().SelectWeapon(4));
    PlayerInputCmd fire_command;
    fire_command.fire = true;

    world.UpdateSimulationTick(0, fire_command);

    const std::vector<RuntimeAudioEvent> audio_events =
        world.ConsumePendingAudioEvents();
    const auto gunshot_event = std::find_if(
        audio_events.begin(),
        audio_events.end(),
        [](const RuntimeAudioEvent& audio_event) {
            return audio_event.type == RuntimeAudioEventType::OneShot &&
                audio_event.fallback_effect == SoundEffect::Gunshot;
        });
    ASSERT_NE(gunshot_event, audio_events.end());
    EXPECT_EQ(gunshot_event->authored_sound, "m16_loop");
    EXPECT_TRUE(world.ConsumePendingAudioEvents().empty());
}

TEST(RuntimeWorldTest, FixedStepGameplayQueuesConditionalSoundEdges) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionConditionalSound conditional_sound;
    conditional_sound.task_id = "Alarm";
    conditional_sound.condition_expression = "AlarmActive";
    conditional_sound.sound_name = "alarm_loop";
    world.SetAuthoredMissionState({}, {}, {}, {}, {}, {conditional_sound});

    world.SetMissionStateBoolean("AlarmActive", true);
    world.UpdateSimulationTick(0, PlayerInputCmd{});
    const std::vector<RuntimeAudioEvent> start_events =
        world.ConsumePendingAudioEvents();
    ASSERT_EQ(start_events.size(), 1U);
    EXPECT_EQ(start_events[0].type, RuntimeAudioEventType::StartLoop);
    EXPECT_EQ(start_events[0].channel_id, "Alarm");
    EXPECT_EQ(start_events[0].authored_sound, "alarm_loop");

    world.SetMissionStateBoolean("AlarmActive", false);
    world.UpdateSimulationTick(1, PlayerInputCmd{});
    const std::vector<RuntimeAudioEvent> stop_events =
        world.ConsumePendingAudioEvents();
    ASSERT_EQ(stop_events.size(), 1U);
    EXPECT_EQ(stop_events[0].type, RuntimeAudioEventType::StopLoop);
    EXPECT_EQ(stop_events[0].channel_id, "Alarm");
}

TEST(RuntimeWorldTest, AuthoredExplodeObjectPublishesDelayedDestroyedState) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionExplodeObject explode_object;
    explode_object.object_index = 77;
    explode_object.task_id = "-1";
    explode_object.destroyed_model_name = "300_02_1";
    explode_object.explosion_expression = "TriggerExplosion";
    explode_object.explosion_delay_seconds =
        2.0f / static_cast<float>(GameClock::TICK_RATE_HZ);

    world.SetAuthoredMissionState(
        {}, {}, {}, {}, {}, {}, {explode_object});
    world.SetMissionStateBoolean("TriggerExplosion", false);
    world.UpdateSimulationTick(0, PlayerInputCmd());
    ASSERT_EQ(world.GetExplodeObjectSnapshots().size(), 1U);
    EXPECT_FALSE(world.GetExplodeObjectSnapshots()[0].is_exploded);

    world.SetMissionStateBoolean("TriggerExplosion", true);
    world.UpdateSimulationTick(1, PlayerInputCmd());
    EXPECT_FALSE(world.GetExplodeObjectSnapshots()[0].is_exploded);
    world.UpdateSimulationTick(2, PlayerInputCmd());
    EXPECT_FALSE(world.GetExplodeObjectSnapshots()[0].is_exploded);
    world.UpdateSimulationTick(3, PlayerInputCmd());
    EXPECT_TRUE(world.GetExplodeObjectSnapshots()[0].is_exploded);
    EXPECT_EQ(
        world.GetExplodeObjectSnapshots()[0].destroyed_model_name,
        "300_02_1");

    world.SetMissionStateBoolean("TriggerExplosion", false);
    world.UpdateSimulationTick(4, PlayerInputCmd());
    world.SetMissionStateBoolean("TriggerExplosion", true);
    world.UpdateSimulationTick(5, PlayerInputCmd());
    EXPECT_TRUE(world.GetExplodeObjectSnapshots()[0].is_exploded);
}

TEST(RuntimeWorldTest, PlayerWeaponDestroysNearestAuthoredExplodeObject) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.GetPlayer().SetPosition(glm::vec3(0.0f));
    world.GetPlayer().SetOrientation(0.0f, 0.0f);
    ASSERT_TRUE(world.GetWeapons().SelectWeapon(4));

    AuthoredMissionExplodeObject explode_object;
    explode_object.object_index = 91;
    explode_object.task_id = "-1";
    explode_object.position = glm::vec3(
        0.0f,
        10.0f * PlayerController::WORLD_METER,
        PlayerController::STANDING_EYE_HEIGHT);
    explode_object.explosion_radius_meters = 1.0f;
    explode_object.explosion_sound = "explo_01_s";
    world.SetAuthoredMissionState(
        {}, {}, {}, {}, {}, {}, {explode_object});

    PlayerInputCmd input_command;
    input_command.fire = true;
    world.UpdateSimulationTick(0, input_command);

    ASSERT_EQ(world.GetExplodeObjectSnapshots().size(), 1U);
    EXPECT_TRUE(world.GetExplodeObjectSnapshots()[0].is_exploded);
}

TEST(RuntimeWorldTest, AuthoredExplodeObjectAppliesBlastDamageToNearbyActors) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);
    world.GetPlayer().SetPosition(glm::vec3(0.0f));

    AiGuardEntity guard;
    guard.id = 92;
    guard.position = glm::vec3(0.0f, PlayerController::WORLD_METER, 0.0f);
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    AuthoredMissionExplodeObject explode_object;
    explode_object.object_index = 93;
    explode_object.task_id = "-1";
    explode_object.position = glm::vec3(0.0f);
    explode_object.damage_scale = 1.0f;
    explode_object.explosion_radius_meters = 5.0f;
    explode_object.explosion_damage_scale = 1.0f;
    explode_object.explosion_expression = "TriggerExplosion";
    world.SetAuthoredMissionState(
        {}, {}, {}, {}, {}, {}, {explode_object});

    const float initial_player_health = world.GetPlayer().GetHealth();
    world.SetMissionStateBoolean("TriggerExplosion", true);
    world.UpdateSimulationTick(0, PlayerInputCmd());

    EXPECT_LT(world.GetPlayer().GetHealth(), initial_player_health);
    ASSERT_EQ(world.GetAi().GetGuards().size(), 1U);
    EXPECT_EQ(world.GetAi().GetGuards()[0].state, AiGuardState::Dead);
}

TEST(RuntimeWorldTest, ProjectileBlastTriggersNearbyAuthoredExplodeObject) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AuthoredMissionExplodeObject explode_object;
    explode_object.object_index = 94;
    explode_object.task_id = "-1";
    explode_object.position = glm::vec3(0.0f);
    explode_object.destroyed_model_name = "300_02_1";
    explode_object.explosion_radius_meters = 2.0f;
    explode_object.explosion_falloff_radius_meters = 1.5f;
    explode_object.explosion_damage_scale = 0.0f;
    world.SetAuthoredMissionState(
        {}, {}, {}, {}, {}, {}, {explode_object});

    ProjectileLaunch launch;
    launch.position = glm::vec3(0.0f);
    launch.type = ProjectileType::FragGrenade;
    launch.fuse_ticks = 1;
    launch.damage = 100.0f;
    launch.explosion_radius_units = 5.0f * PlayerController::WORLD_METER;
    ASSERT_TRUE(world.GetProjectiles().Spawn(launch));

    world.UpdateSimulationTick(0, PlayerInputCmd());

    ASSERT_EQ(world.GetExplodeObjectSnapshots().size(), 1U);
    EXPECT_TRUE(world.GetExplodeObjectSnapshots()[0].is_exploded);
}

TEST(RuntimeWorldTest, ImpactRocketDetonatesOnLivingGuard) {
    RuntimeWorld world;
    world.Initialize(FlatTerrain);

    AiGuardEntity guard;
    guard.id = 95;
    guard.position = glm::vec3(
        0.0f,
        PlayerController::WORLD_METER,
        0.0f);
    guard.health = 100.0f;
    world.GetAi().RegisterGuard(guard);

    ProjectileLaunch launch;
    launch.position = glm::vec3(
        0.0f,
        0.0f,
        0.9f * PlayerController::WORLD_METER);
    launch.velocity = glm::vec3(
        0.0f,
        2.0f * PlayerController::WORLD_METER,
        0.0f);
    launch.type = ProjectileType::Rocket;
    launch.fuse_ticks = ProjectileSystem::ReferenceGrenadeFuseTicks;
    launch.damage = 100.0f;
    launch.damage_factor = 5.0f;
    launch.explosion_radius_units = 2.5f * PlayerController::WORLD_METER;
    launch.explosion_falloff_units = 1.75f * PlayerController::WORLD_METER;
    launch.detonate_on_impact = true;
    ASSERT_TRUE(world.GetProjectiles().Spawn(launch));

    world.UpdateSimulationTick(0, PlayerInputCmd());

    EXPECT_TRUE(world.GetProjectiles().GetProjectiles().empty());
    ASSERT_EQ(world.GetProjectiles().GetDetonations().size(), 1U);
    ASSERT_EQ(world.GetAi().GetGuards().size(), 1U);
    EXPECT_EQ(world.GetAi().GetGuards()[0].state, AiGuardState::Dead);
}

// 8. Twin-Window & Editor Snapshot Tests
TEST(RuntimeHostTest, GameplayInputModifierRunsBeforeEachFixedWorldTick) {
    GameplayHost host;
    host.Initialize(FlatTerrain);

    std::vector<uint64_t> modifier_ticks;
    host.SetGameplayInputModifier(
        [&modifier_ticks](uint64_t tick_number, PlayerInputCmd& input_command) {
            modifier_ticks.push_back(tick_number);
            input_command.forward = 1.0f;
        });

    EditorSnapshot snapshot;
    ASSERT_TRUE(host.OpenGameplay(snapshot));
    host.Update(0);
    host.Update(100);

    ASSERT_GE(modifier_ticks.size(), 3U);
    EXPECT_EQ(modifier_ticks.front(), 0U);
    EXPECT_GT(host.GetWorld().GetPlayer().GetPosition().y, 0.0f);

    EditorSnapshot restored_snapshot;
    ASSERT_TRUE(host.CloseGameplay(restored_snapshot));
}

TEST(RuntimeHostTest, ModeSwitchingAndSnapshotRestore) {
    GameplayHost host;
    auto dummy_terrain = [](float x, float y) -> float { return 0.0f; };
    host.Initialize(dummy_terrain);

    EditorSnapshot snap;
    snap.camera_pos = glm::vec3(123.0f, 456.0f, 789.0f);
    snap.camera_yaw = 45.0f;
    snap.was_noclip_mode = false;
    snap.was_hud_visible = false;

    EXPECT_FALSE(host.IsGameplayActive());
    bool opened = host.OpenGameplay(snap);
    EXPECT_TRUE(opened);
    EXPECT_TRUE(host.IsGameplayActive());

    // Update simulation
    host.Update(100);

    EditorSnapshot restored;
    bool closed = host.CloseGameplay(restored);
    EXPECT_TRUE(closed);
    EXPECT_FALSE(host.IsGameplayActive());
    EXPECT_FLOAT_EQ(restored.camera_pos.x, 123.0f);
    EXPECT_FLOAT_EQ(restored.camera_yaw, 45.0f);
    EXPECT_FALSE(restored.was_noclip_mode);
    EXPECT_FALSE(restored.was_hud_visible);
}

TEST(RuntimeHostTest, PausingFreezesTicksAndRepeatedSessionsStayIsolated) {
    GameplayHost host;
    host.Initialize(FlatTerrain);

    EditorSnapshot snapshot;
    ASSERT_TRUE(host.OpenGameplay(snapshot));
    EXPECT_FALSE(host.OpenGameplay(snapshot));

    host.GetInputRouter().OnKeyboardKey('W', true);
    host.Update(100);
    const glm::vec3 position_after_running_tick = host.GetWorld().GetPlayer().GetPosition();

    host.SetPaused(true);
    host.Update(1000);
    EXPECT_EQ(host.GetWorld().GetPlayer().GetPosition(), position_after_running_tick);

    host.SetPaused(false);
    host.Update(1100);
    EXPECT_NE(host.GetWorld().GetPlayer().GetPosition(), position_after_running_tick);

    EditorSnapshot restored;
    ASSERT_TRUE(host.CloseGameplay(restored));
    EXPECT_EQ(host.GetInputRouter().GetFocus(), WindowFocusTarget::EditorWindow);
    EXPECT_FALSE(host.IsPaused());

    ASSERT_TRUE(host.OpenGameplay(snapshot));
    EXPECT_EQ(host.GetWorld().GetPlayer().GetPosition(), glm::vec3(0.0f));
    ASSERT_TRUE(host.CloseGameplay(restored));
}

TEST(RuntimeSessionTest, LifecycleIsExplicitAndRestoresEditorSnapshot) {
    RuntimeSession session;
    EditorSnapshot snapshot;
    snapshot.camera_pos = glm::vec3(11.0f, 22.0f, 33.0f);
    snapshot.camera_yaw = 90.0f;

    EXPECT_EQ(session.GetState(), RuntimeSessionState::Stopped);
    EXPECT_FALSE(session.Open(snapshot));

    session.Initialize(FlatTerrain);
    PlayerController::Tuning tuning;
    tuning.maximum_health = 80.0f;
    tuning.maximum_armor = 35.0f;
    session.GetWorld().SetPlayerTuning(tuning);
    EXPECT_EQ(session.GetState(), RuntimeSessionState::Created);
    EXPECT_TRUE(session.Open(snapshot));
    EXPECT_EQ(session.GetState(), RuntimeSessionState::Running);
    EXPECT_EQ(session.GetInputRouter().GetFocus(), WindowFocusTarget::GameplayWindow);
    EXPECT_FLOAT_EQ(session.GetWorld().GetPlayer().GetMaximumHealth(), 80.0f);
    EXPECT_FLOAT_EQ(session.GetWorld().GetPlayer().GetMaximumArmor(), 35.0f);

    session.SetPaused(true);
    EXPECT_EQ(session.GetState(), RuntimeSessionState::Paused);
    session.Update(1000);
    session.Restart();
    EXPECT_EQ(session.GetState(), RuntimeSessionState::Running);
    EXPECT_EQ(session.GetWorld().GetPlayer().GetPosition(), glm::vec3(0.0f));
    EXPECT_FLOAT_EQ(session.GetWorld().GetPlayer().GetMaximumHealth(), 80.0f);
    EXPECT_FLOAT_EQ(session.GetWorld().GetPlayer().GetMaximumArmor(), 35.0f);

    EditorSnapshot restored;
    ASSERT_TRUE(session.Close(restored));
    EXPECT_EQ(session.GetState(), RuntimeSessionState::Stopped);
    EXPECT_EQ(session.GetInputRouter().GetFocus(), WindowFocusTarget::EditorWindow);
    EXPECT_EQ(restored.camera_pos, snapshot.camera_pos);
    EXPECT_FLOAT_EQ(restored.camera_yaw, snapshot.camera_yaw);
    EXPECT_FALSE(session.Close(restored));
}

TEST(RuntimeSessionTest, RestartDoesNotMutateTheCapturedEditorSnapshot) {
    RuntimeSession session;
    session.Initialize(FlatTerrain);

    EditorSnapshot snapshot;
    snapshot.selected_object_id = 42;
    ASSERT_TRUE(session.Open(snapshot));
    session.GetWorld().GetPlayer().SetPosition(glm::vec3(100.0f, 200.0f, 300.0f));

    session.Restart();

    EditorSnapshot restored;
    ASSERT_TRUE(session.Close(restored));
    EXPECT_EQ(restored.selected_object_id, 42U);
    EXPECT_EQ(restored.camera_pos, snapshot.camera_pos);
}

TEST(RuntimeSessionTest, ApplyingEditorSnapshotRebuildsRuntimeAndRestoresTheAppliedState) {
    RuntimeSession session;
    session.Initialize(FlatTerrain);

    EditorSnapshot original_snapshot;
    original_snapshot.selected_object_id = 7;
    ASSERT_TRUE(session.Open(original_snapshot));
    session.GetWorld().GetPlayer().SetPosition(glm::vec3(100.0f, 200.0f, 300.0f));
    session.SetPaused(true);

    EditorSnapshot applied_snapshot;
    applied_snapshot.camera_pos = glm::vec3(11.0f, 22.0f, 33.0f);
    applied_snapshot.camera_yaw = 135.0f;
    applied_snapshot.selected_object_id = 99;

    EXPECT_TRUE(session.ApplyEditorSnapshot(applied_snapshot));
    EXPECT_EQ(session.GetState(), RuntimeSessionState::Running);
    EXPECT_EQ(session.GetInputRouter().GetFocus(), WindowFocusTarget::GameplayWindow);
    EXPECT_EQ(session.GetWorld().GetPlayer().GetPosition(), glm::vec3(0.0f));

    EditorSnapshot restored_snapshot;
    ASSERT_TRUE(session.Close(restored_snapshot));
    EXPECT_EQ(restored_snapshot.camera_pos, applied_snapshot.camera_pos);
    EXPECT_FLOAT_EQ(restored_snapshot.camera_yaw, applied_snapshot.camera_yaw);
    EXPECT_EQ(restored_snapshot.selected_object_id, applied_snapshot.selected_object_id);
}

TEST(RuntimeHostTest, ApplyAndRestartRequiresAnActiveGameplaySession) {
    GameplayHost host;
    host.Initialize(FlatTerrain);

    EditorSnapshot snapshot;
    EXPECT_FALSE(host.ApplyAndRestartGameplay(snapshot));

    ASSERT_TRUE(host.OpenGameplay(snapshot));
    EXPECT_TRUE(host.ApplyAndRestartGameplay(snapshot));

    EditorSnapshot restored_snapshot;
    ASSERT_TRUE(host.CloseGameplay(restored_snapshot));
}

TEST(RuntimeHostTest, FocusCommandsMoveInputOwnershipWithoutRestartingGameplay) {
    GameplayHost host;
    host.Initialize(FlatTerrain);

    EditorSnapshot snapshot;
    ASSERT_TRUE(host.OpenGameplay(snapshot));
    host.GetInputRouter().OnKeyboardKey('W', true);
    EXPECT_EQ(host.GetInputRouter().GetFocus(), WindowFocusTarget::GameplayWindow);

    host.FocusEditorWindow();
    EXPECT_EQ(host.GetInputRouter().GetFocus(), WindowFocusTarget::EditorWindow);
    EXPECT_TRUE(host.IsGameplayActive());

    host.FocusGameplayWindow();
    EXPECT_EQ(host.GetInputRouter().GetFocus(), WindowFocusTarget::GameplayWindow);
    EXPECT_TRUE(host.IsGameplayActive());

    EditorSnapshot restored_snapshot;
    ASSERT_TRUE(host.CloseGameplay(restored_snapshot));
}

TEST(RuntimeSpawnTest, PrefersGameplayPlayerTaskOverEditorCameraFallback) {
    std::vector<RuntimeSpawnCandidate> candidates = {
        RuntimeSpawnCandidate{
            true,
            17,
            RuntimeSpawnPoint{glm::vec3(100.0f, 200.0f, 300.0f), 45.0f, 0.0f}},
        RuntimeSpawnCandidate{
            true,
            0,
            RuntimeSpawnPoint{glm::vec3(400.0f, 500.0f, 600.0f), 90.0f, 5.0f}},
    };

    const std::optional<RuntimeSpawnPoint> selected_spawn =
        SelectAuthoredPlayerSpawn(candidates);

    ASSERT_TRUE(selected_spawn.has_value());
    EXPECT_EQ(selected_spawn->position, glm::vec3(400.0f, 500.0f, 600.0f));
    EXPECT_FLOAT_EQ(selected_spawn->yaw, 90.0f);
    EXPECT_FLOAT_EQ(selected_spawn->pitch, 5.0f);
}

TEST(RuntimeSpawnTest, ReturnsNoSpawnWhenLevelHasNoHumanPlayerTask) {
    const std::vector<RuntimeSpawnCandidate> candidates = {
        RuntimeSpawnCandidate{
            false,
            -1,
            RuntimeSpawnPoint{glm::vec3(100.0f), 0.0f, 0.0f}},
    };

    EXPECT_FALSE(SelectAuthoredPlayerSpawn(candidates).has_value());
}

TEST(AiSystemTest, ClearResetsFixedStepSimulationTick) {
    AiSystem ai;
    ai.Update(1.0 / 30.0, glm::vec3(0.0f), true);
    EXPECT_GT(ai.GetSimulationTick(), 0U);

    ai.Clear();

    EXPECT_EQ(ai.GetSimulationTick(), 0U);
}

TEST(MapComputerCameraTest, OpensFromEyeThroughBootIntoInteractiveMap) {
    const RuntimeMapComputerPose eye{
        glm::vec3(120.0f, 340.0f, 12.5f), 2.1f, -0.12f};
    const RuntimeMapComputerPose vantage{
        glm::vec3(120.0f, 340.0f, 912.5f), 0.0f,
        RuntimeMapComputerCamera::kMapPitchRadians};

    RuntimeMapComputerCamera camera;
    camera.BeginOpen(eye, 1.05f, vantage, 0.30f);
    camera.Update(1.0f / 60.0f, eye, vantage, 0.30f);

    EXPECT_EQ(camera.GetPhase(), RuntimeMapComputerPhase::Ascend);
    EXPECT_LT(std::abs(camera.GetPose().position.z - eye.position.z), 0.1f);
    EXPECT_FALSE(camera.IsInteractive());

    for (int tick = 0; tick < 60; ++tick) {
        camera.Update(1.0f / 30.0f, eye, vantage, 0.30f);
    }

    EXPECT_TRUE(camera.GetPhase() == RuntimeMapComputerPhase::Boot ||
                camera.GetPhase() == RuntimeMapComputerPhase::Open);
    EXPECT_NEAR(camera.GetPose().position.z, vantage.position.z, 0.01f);
    EXPECT_NEAR(camera.GetPose().pitch, vantage.pitch, 0.001f);
    EXPECT_TRUE(camera.CanClose());

    for (int tick = 0; tick < 30 && !camera.IsInteractive(); ++tick) {
        camera.Update(1.0f / 30.0f, eye, vantage, 0.30f);
    }
    EXPECT_TRUE(camera.IsInteractive());
}

TEST(MapComputerCameraTest, CloseReturnsExactlyToTheLivePlayerEye) {
    const RuntimeMapComputerPose eye{
        glm::vec3(120.0f, 340.0f, 12.5f), 2.1f, -0.12f};
    const RuntimeMapComputerPose vantage{
        glm::vec3(1400.0f, 2200.0f, 912.5f), 0.0f,
        RuntimeMapComputerCamera::kMapPitchRadians};

    RuntimeMapComputerCamera camera;
    camera.BeginOpen(eye, 1.05f, vantage, 0.30f);
    for (int tick = 0; tick < 60; ++tick) {
        camera.Update(1.0f / 30.0f, eye, vantage, 0.30f);
    }
    for (int tick = 0; tick < 30; ++tick) {
        camera.Update(1.0f / 30.0f, eye, vantage, 0.30f);
    }

    camera.BeginClose(vantage, 0.30f, eye, 1.05f);
    for (int tick = 0; tick < 60; ++tick) {
        camera.Update(1.0f / 30.0f, eye, vantage, 0.30f);
    }

    EXPECT_EQ(camera.GetPhase(), RuntimeMapComputerPhase::Done);
    EXPECT_EQ(camera.GetPose().position, eye.position);
    EXPECT_FLOAT_EQ(camera.GetPose().yaw, eye.yaw);
    EXPECT_FLOAT_EQ(camera.GetPose().pitch, eye.pitch);
    EXPECT_FLOAT_EQ(camera.GetFieldOfView(), 1.05f);
    EXPECT_FLOAT_EQ(camera.GetMotionBlur(), 0.0f);
}

TEST(MapComputerCameraTest, CloseDuringOpeningFlightRemainsContinuous) {
    const RuntimeMapComputerPose eye{
        glm::vec3(120.0f, 340.0f, 12.5f), 2.1f, -0.12f};
    const RuntimeMapComputerPose vantage{
        glm::vec3(120.0f, 340.0f, 912.5f), 0.0f,
        RuntimeMapComputerCamera::kMapPitchRadians};

    RuntimeMapComputerCamera camera;
    camera.BeginOpen(eye, 1.05f, vantage, 0.30f);
    camera.Update(0.30f, eye, vantage, 0.30f);
    const RuntimeMapComputerPose close_start = camera.GetPose();
    ASSERT_EQ(camera.GetPhase(), RuntimeMapComputerPhase::Ascend);
    ASSERT_TRUE(camera.CanClose());

    camera.BeginClose(
        close_start,
        camera.GetFieldOfView(),
        eye,
        1.05f);
    camera.Update(1.0f / 30.0f, eye, vantage, 0.30f);

    EXPECT_EQ(camera.GetPhase(), RuntimeMapComputerPhase::Shutdown);
    EXPECT_EQ(camera.GetPose().position, close_start.position);
    EXPECT_FLOAT_EQ(camera.GetPose().yaw, close_start.yaw);
}

TEST(MapComputerProjectionTest, CentersPlayerAndRejectsOffPanelMarkers) {
    const RuntimeMapComputerProjection projection =
        BuildRuntimeMapComputerProjection(
            glm::vec3(1000.0f, 2000.0f, 40960.0f),
            1.57079632679f,
            1600,
            900,
            100,
            50,
            900,
            650);

    const glm::vec2 center = projection.Project(
        glm::vec3(1000.0f, 2000.0f, 0.0f));
    EXPECT_NEAR(center.x, 500.0f, 0.01f);
    EXPECT_NEAR(center.y, 350.0f, 0.01f);
    EXPECT_TRUE(projection.Contains(center));

    const glm::vec2 outside = projection.Project(
        glm::vec3(200000.0f, 2000.0f, 0.0f));
    EXPECT_FALSE(projection.Contains(outside));
}
