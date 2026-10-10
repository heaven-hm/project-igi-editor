#include <gtest/gtest.h>

#include "../source/mission_state_loader.h"

TEST(MissionStateLoaderTest, LoadsOrientedAreaActivationDefinition) {
    igi::MissionStateTaskSource source;
    source.task_type = "AreaActivate";
    source.task_id = "200";
    source.argument_tokens = {
        "200", "AreaActivate", "", "100", "200", "300", "0", "0",
        "1.5707963", "400", "500", "600", "CRITERIA_HUMAN0",
    };

    const igi::AuthoredMissionStateDefinitions definitions =
        igi::LoadAuthoredMissionStateDefinitions({source});

    ASSERT_EQ(definitions.area_activations.size(), 1U);
    const igi::AuthoredMissionAreaActivation& area =
        definitions.area_activations.front();
    EXPECT_EQ(area.task_id, "200");
    EXPECT_EQ(area.position, glm::vec3(100.0f, 200.0f, 300.0f));
    EXPECT_EQ(area.dimensions, glm::vec3(400.0f, 500.0f, 600.0f));
    EXPECT_EQ(area.criteria, "CRITERIA_HUMAN0");
}

TEST(MissionStateLoaderTest, LoadsEditVariableExpressionsAndInitialValue) {
    igi::MissionStateTaskSource source;
    source.task_type = "EditVariable";
    source.task_id = "105";
    source.argument_tokens = {
        "105", "EditVariable", "", "0", "0", "0", "2",
        "EditVariable_105.nValue == 2", "EditVariable_105.nValue == 3",
    };

    const igi::AuthoredMissionStateDefinitions definitions =
        igi::LoadAuthoredMissionStateDefinitions({source});

    ASSERT_EQ(definitions.edit_variables.size(), 1U);
    const igi::AuthoredMissionEditVariable& edit =
        definitions.edit_variables.front();
    EXPECT_EQ(edit.task_id, "105");
    EXPECT_EQ(edit.initial_value, 2);
    EXPECT_EQ(edit.add_expression, "EditVariable_105.nValue == 2");
    EXPECT_EQ(edit.subtract_expression, "EditVariable_105.nValue == 3");
}

TEST(MissionStateLoaderTest, LoadsTimersAndStatusMessagePresentationFields) {
    igi::MissionStateTaskSource timer_source;
    timer_source.task_type = "LevelTimer";
    timer_source.task_id = "95";
    timer_source.argument_tokens = {
        "95", "LevelTimer", "", "0", "0", "0", "0", "0", "0",
        "Terminal_501.isHacked", "Terminal_501.isHackedThisTick", "FALSE",
    };

    igi::MissionStateTaskSource message_source;
    message_source.task_type = "StatusMessage";
    message_source.task_id = "1391";
    message_source.argument_tokens = {
        "1391", "StatusMessage", "", "0", "0", "0", "0", "0", "0",
        "HumanPlayer_0.isDead", "MISSION_FAILED", "", "fail", "TRUE", "FALSE", "2.0",
    };

    const igi::AuthoredMissionStateDefinitions definitions =
        igi::LoadAuthoredMissionStateDefinitions({timer_source, message_source});

    ASSERT_EQ(definitions.level_timers.size(), 1U);
    EXPECT_EQ(definitions.level_timers[0].task_id, "95");
    EXPECT_EQ(definitions.level_timers[0].on_expression, "Terminal_501.isHacked");
    EXPECT_FALSE(definitions.level_timers[0].initial_run);

    ASSERT_EQ(definitions.status_messages.size(), 1U);
    EXPECT_EQ(definitions.status_messages[0].task_id, "1391");
    EXPECT_EQ(definitions.status_messages[0].send_expression, "HumanPlayer_0.isDead");
    EXPECT_EQ(definitions.status_messages[0].text_resource, "MISSION_FAILED");
    EXPECT_EQ(definitions.status_messages[0].sound_name, "fail");
    EXPECT_TRUE(definitions.status_messages[0].send_once);
    EXPECT_FALSE(definitions.status_messages[0].cutscene_message);
    EXPECT_FLOAT_EQ(definitions.status_messages[0].duration_seconds, 2.0f);
}

TEST(MissionStateLoaderTest, LoadsCutSceneExpressionsAndAuthoredDuration) {
    igi::MissionStateTaskSource source;
    source.task_type = "CutScene";
    source.task_id = "1204";
    source.authored_duration_seconds = 15.0f;
    source.authored_camera_shots.push_back({
        glm::vec3(1.0f, 2.0f, 3.0f),
        glm::vec3(0.1f, 0.2f, 0.3f),
        1.2f,
        4.0f,
        true,
    });
    source.argument_tokens = {
        "1204", "CutScene", "", "0", "0", "0", "0", "0", "0",
        "!CutScene_1204.isFinished", "", "", "0", "FALSE", "0.7",
        "0.8", "0.25", "0.5", "0", "", "",
    };

    const igi::AuthoredMissionStateDefinitions definitions =
        igi::LoadAuthoredMissionStateDefinitions({source});

    ASSERT_EQ(definitions.cut_scenes.size(), 1U);
    const igi::AuthoredMissionCutScene& cut_scene = definitions.cut_scenes.front();
    EXPECT_EQ(cut_scene.task_id, "1204");
    EXPECT_EQ(cut_scene.run_expression, "!CutScene_1204.isFinished");
    EXPECT_FALSE(cut_scene.initial_run);
    EXPECT_FLOAT_EQ(cut_scene.time_scale, 0.7f);
    EXPECT_FLOAT_EQ(cut_scene.duration_seconds, 15.0f);
    EXPECT_FLOAT_EQ(cut_scene.viewport_height_factor, 0.8f);
    ASSERT_EQ(cut_scene.camera_shots.size(), 1U);
    EXPECT_EQ(
        cut_scene.camera_shots[0].position,
        glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_TRUE(cut_scene.camera_shots[0].smooth_to_next);
}

TEST(MissionStateLoaderTest, RejectsNonFiniteCutSceneTiming) {
    igi::MissionStateTaskSource source;
    source.task_type = "CutScene";
    source.task_id = "1204";
    source.authored_duration_seconds = 1.0f;
    source.argument_tokens = {
        "1204", "CutScene", "", "0", "0", "0", "0", "0", "0",
        "", "", "", "nan", "FALSE", "1.0", "0", "0", "0", "", "", "",
    };

    const igi::AuthoredMissionStateDefinitions definitions =
        igi::LoadAuthoredMissionStateDefinitions({source});

    EXPECT_TRUE(definitions.cut_scenes.empty());
}

TEST(MissionStateLoaderTest, LoadsConditionalSoundEdgeDefinition) {
    igi::MissionStateTaskSource source;
    source.task_type = "ConditionalSound";
    source.task_id = "3005";
    source.argument_tokens = {
        "3005", "ConditionalSound", "land", "CutScene_1201.nTick > 7",
        "land_ground_2", "10", "20", "30", "FALSE", "TRUE", "TRUE",
    };

    const igi::AuthoredMissionStateDefinitions definitions =
        igi::LoadAuthoredMissionStateDefinitions({source});

    ASSERT_EQ(definitions.conditional_sounds.size(), 1U);
    const igi::AuthoredMissionConditionalSound& sound =
        definitions.conditional_sounds.front();
    EXPECT_EQ(sound.task_id, "3005");
    EXPECT_EQ(sound.condition_expression, "CutScene_1201.nTick > 7");
    EXPECT_EQ(sound.sound_name, "land_ground_2");
    EXPECT_EQ(sound.position, glm::vec3(10.0f, 20.0f, 30.0f));
    EXPECT_FALSE(sound.simple);
    EXPECT_TRUE(sound.one_shot);
    EXPECT_TRUE(sound.relative_to_microphone);
}

TEST(MissionStateLoaderTest, LoadsConditionalContainerGateAndDescendants) {
    igi::MissionStateTaskSource source;
    source.task_type = "ConditionalContainer";
    source.task_id = "4001";
    source.object_index = 12;
    source.argument_tokens = {
        "4001", "ConditionalContainer", "Intro cutscene",
        "!CutScene_1203.isFinished && !EditVariable_4004.nValue",
        "MenuManager_SetEnabled(FALSE)",
        "MenuManager_SetEnabled(TRUE)",
    };
    source.descendant_object_indices = {13, 14, 15};

    const igi::AuthoredMissionStateDefinitions definitions =
        igi::LoadAuthoredMissionStateDefinitions({source});

    ASSERT_EQ(definitions.conditional_containers.size(), 1U);
    const igi::AuthoredMissionConditionalContainer& container =
        definitions.conditional_containers.front();
    EXPECT_EQ(container.object_index, 12);
    EXPECT_EQ(container.task_id, "4001");
    EXPECT_EQ(
        container.condition_expression,
        "!CutScene_1203.isFinished && !EditVariable_4004.nValue");
    EXPECT_EQ(container.run_at_start_expression, "MenuManager_SetEnabled(FALSE)");
    EXPECT_EQ(container.run_at_stop_expression, "MenuManager_SetEnabled(TRUE)");
    EXPECT_EQ(container.descendant_object_indices,
              std::vector<int>({13, 14, 15}));
}

TEST(MissionStateLoaderTest, LoadsGuardGeneratorConditionAndSpawnChildren) {
    igi::MissionStateTaskSource source;
    source.task_type = "GuardGenerator";
    source.task_id = "-1";
    source.object_index = 30;
    source.argument_tokens = {
        "-1", "GuardGenerator", "AI Army",
        "AlarmControl_97.isAlarm", "2",
    };
    source.guard_object_indices = {31, 32, 33};

    const igi::AuthoredMissionStateDefinitions definitions =
        igi::LoadAuthoredMissionStateDefinitions({source});

    ASSERT_EQ(definitions.guard_generators.size(), 1U);
    const igi::AuthoredMissionGuardGenerator& guard_generator =
        definitions.guard_generators.front();
    EXPECT_EQ(guard_generator.object_index, 30);
    EXPECT_EQ(guard_generator.task_id, "-1");
    EXPECT_EQ(
        guard_generator.condition_expression,
        "AlarmControl_97.isAlarm");
    EXPECT_EQ(guard_generator.maximum_spawns, 2);
    EXPECT_EQ(
        guard_generator.guard_object_indices,
        std::vector<int>({31, 32, 33}));
}

TEST(MissionStateLoaderTest, LoadsVanillaExplodeObjectArguments) {
    igi::MissionStateTaskSource source;
    source.task_type = "ExplodeObject";
    source.task_id = "-1";
    source.object_index = 77;
    source.argument_tokens = {
        "-1", "ExplodeObject", "Barrel", "100", "200", "300",
        "0", "0", "1.5708", "300_04_1", "300_02_1", "1.0",
        "2.0", "1.5", "1.0", "3.0", "3", "3",
        "CutScene_667.nTick > 15", "explo_02_m",
    };

    const igi::AuthoredMissionStateDefinitions definitions =
        igi::LoadAuthoredMissionStateDefinitions({source});

    ASSERT_EQ(definitions.explode_objects.size(), 1U);
    const igi::AuthoredMissionExplodeObject& explode_object =
        definitions.explode_objects.front();
    EXPECT_EQ(explode_object.object_index, 77);
    EXPECT_EQ(explode_object.task_id, "-1");
    EXPECT_EQ(explode_object.position, glm::vec3(100.0f, 200.0f, 300.0f));
    EXPECT_EQ(explode_object.model_name, "300_04_1");
    EXPECT_EQ(explode_object.destroyed_model_name, "300_02_1");
    EXPECT_FLOAT_EQ(explode_object.explosion_radius_meters, 2.0f);
    EXPECT_FLOAT_EQ(explode_object.explosion_falloff_radius_meters, 1.5f);
    EXPECT_FLOAT_EQ(explode_object.explosion_delay_seconds, 3.0f);
    EXPECT_EQ(explode_object.explosion_expression, "CutScene_667.nTick > 15");
    EXPECT_EQ(explode_object.explosion_sound, "explo_02_m");
}
