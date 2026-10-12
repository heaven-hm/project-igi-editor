// test_runtime_subsystems.cpp - Unit and integration tests for C++ Game Mode Runtime Subsystems
#include <gtest/gtest.h>
#include "../source/game_clock.h"
#include "../source/level/qvm_interpreter.h"
#include "../source/level/qvm_native_registry.h"
#include "../source/level/task_tree.h"
#include "../source/player_controller.h"
#include "../source/player_collision.h"
#include "../source/weapon_system.h"
#include "../source/ai_system.h"
#include "../source/level_flow.h"
#include "../source/runtime/runtime_world.h"
#include "../source/runtime/editor_snapshot.h"
#include "../source/runtime/gameplay_host.h"
#include "../source/runtime/pause_menu_layout.h"

using namespace igi;

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
    }
    EXPECT_GE(tick_count, 3);
    EXPECT_EQ(clock.GetTickCount(), static_cast<uint64_t>(tick_count));

    // Test pause
    clock.SetPaused(true);
    clock.Update(300);
    EXPECT_FALSE(clock.IsTickDue());
}

TEST(RuntimeClockTest, ResumeDoesNotCatchUpTimeSpentPaused) {
    GameClock clock;
    clock.Update(1000);
    clock.Update(1033);
    ASSERT_TRUE(clock.IsTickDue());
    clock.CompleteTick();
    ASSERT_EQ(clock.GetTickCount(), 1);

    clock.SetPaused(true);
    clock.SetPaused(false);
    clock.Update(10000);

    EXPECT_FALSE(clock.IsTickDue());
    EXPECT_EQ(clock.GetTickCount(), 1);
}

TEST(PauseMenuInputTest, MenuIsModalInEditorAndGameplayModes) {
    EXPECT_TRUE(IsPauseMenuInputActive(true));
    EXPECT_FALSE(IsPauseMenuInputActive(false));

    EXPECT_TRUE(IsEditorInteractionActive(false, false));
    EXPECT_FALSE(IsEditorInteractionActive(true, false));
    EXPECT_FALSE(IsEditorInteractionActive(false, true));
    EXPECT_FALSE(IsEditorInteractionActive(true, true));
}

TEST(EditorPauseMenuLayoutTest, RestoresAllEditorControls) {
    EXPECT_EQ(PauseMenuItemCount(false), 12);
    EXPECT_EQ(PauseMenuItemCount(true), 17);
    EXPECT_EQ(PauseMenuItemAt(false, 0), PauseMenuItem::Resume);
    EXPECT_EQ(PauseMenuItemAt(false, 3), PauseMenuItem::LevelSelector);
    EXPECT_EQ(PauseMenuItemAt(false, 4), PauseMenuItem::AutoSave);
    EXPECT_EQ(PauseMenuItemAt(false, 6), PauseMenuItem::Music);
    EXPECT_EQ(PauseMenuItemAt(false, 7), PauseMenuItem::Lightmaps);
    EXPECT_EQ(PauseMenuItemAt(false, 8), PauseMenuItem::TerrainOptions);
    EXPECT_EQ(PauseMenuItemAt(true, 9), PauseMenuItem::TerrainTexture);
    EXPECT_EQ(PauseMenuItemAt(true, 13), PauseMenuItem::FogIntensity);
    EXPECT_EQ(PauseMenuItemAt(true, 16), PauseMenuItem::Quit);
}

TEST(EditorPauseMenuLevelTest, AcceptsEditorLevelSpinnerAndLeavesGameplaySafely) {
    const auto editor = ResolvePauseLevelSelection(2, false);
    EXPECT_TRUE(editor.valid);
    EXPECT_FALSE(editor.leave_gameplay);
    EXPECT_EQ(editor.level, 2);

    const auto gameplay = ResolvePauseLevelSelection(14, true);
    EXPECT_TRUE(gameplay.valid);
    EXPECT_TRUE(gameplay.leave_gameplay);
    EXPECT_EQ(gameplay.level, 14);

    EXPECT_FALSE(ResolvePauseLevelSelection(0, false).valid);
    EXPECT_FALSE(ResolvePauseLevelSelection(15, true).valid);
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

// 5. Weapon Fire & Ballistics Tests
TEST(RuntimeWeaponTest, FireAndRecoilCooldown) {
    WeaponSystem weapons;
    weapons.SelectWeapon(0);

    BulletTrace trace;
    bool fired = weapons.TryFire(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), trace);
    EXPECT_TRUE(fired);
    EXPECT_TRUE(trace.hit);
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 29);

    // Rapid second shot must be throttled by RPM cooldown
    bool fired_immediately = weapons.TryFire(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), trace);
    EXPECT_FALSE(fired_immediately);

    // Advance cooldown
    weapons.Update(0.2);
    bool fired_after_cooldown = weapons.TryFire(glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f), trace);
    EXPECT_TRUE(fired_after_cooldown);
    EXPECT_EQ(weapons.GetCurrentClipAmmo(), 28);
}

// 6. AI Perception Dual-Cone Tests
TEST(RuntimeAiTest, VisionConeDirectAndPeripheral) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 1;
    guard.position = glm::vec3(0.0f, 0.0f, 0.0f);
    guard.yaw = 0.0f; // Facing +Y
    ai.RegisterGuard(guard);

    // Target directly in front (30m ahead at eye level) -> Primary cone (0x101)
    AiVisionResult res1 = ai.CheckVision(guard, glm::vec3(0.0f, 3000.0f, 180.0f), false);
    EXPECT_EQ(res1, AiVisionResult::Primary);

    // Target behind guard -> None
    AiVisionResult res2 = ai.CheckVision(guard, glm::vec3(0.0f, -2000.0f, 180.0f), false);
    EXPECT_EQ(res2, AiVisionResult::None);

    // Target in wide peripheral angle (approx 59 degrees) -> Peripheral
    AiVisionResult res3 = ai.CheckVision(guard, glm::vec3(2000.0f, 1200.0f, 180.0f), false);
    EXPECT_EQ(res3, AiVisionResult::Peripheral);
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

// 8. Twin-Window & Editor Snapshot Tests
TEST(RuntimeHostTest, ModeSwitchingAndSnapshotRestore) {
    GameplayHost host;
    auto dummy_terrain = [](float x, float y) -> float { return 0.0f; };
    host.Initialize(dummy_terrain);

    EditorSnapshot snap;
    snap.camera_pos = glm::vec3(123.0f, 456.0f, 789.0f);
    snap.camera_yaw = 45.0f;

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
}

TEST(RuntimeHostTest, PausingClearsHeldGameplayInput) {
    GameplayHost host;
    auto dummy_terrain = [](float, float) -> float { return 0.0f; };
    host.Initialize(dummy_terrain);

    EditorSnapshot snap;
    ASSERT_TRUE(host.OpenGameplay(snap));
    host.GetInputRouter().OnKeyboardKey('W', true);
    EXPECT_FLOAT_EQ(host.GetInputRouter().ConsumeGameplayInput().forward, 1.0f);

    host.SetPaused(true);

    EXPECT_TRUE(host.IsPaused());
    EXPECT_FLOAT_EQ(host.GetInputRouter().ConsumeGameplayInput().forward, 0.0f);
}
