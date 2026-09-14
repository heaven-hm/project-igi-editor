#include <gtest/gtest.h>

#include "animation.h"
#include "renderer/mef_native.h"
#include "level/level_objects.h"
#include "runtime/graph_camera_target.h"
#include "utils.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <regex>
#include <set>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

AnimationClip Clip(int duration, bool loop = false) {
    AnimationClip clip;
    clip.length_ms = duration;
    clip.tp_flag = loop ? 1 : 0;
    return clip;
}

GraphNode Node(int id, double x, double y, double z) {
    GraphNode node;
    node.id = id;
    node.x = x;
    node.y = y;
    node.z = z;
    return node;
}

}  // namespace

TEST(AnimationPlaybackTest, AdvancesWhilePlaying) {
    const AnimationClip clip = Clip(1000);
    AnimPlayback playback;
    playback.Start(&clip);

    playback.Update(125.0f);

    EXPECT_FLOAT_EQ(playback.currentTimeMs, 125.0f);
    EXPECT_TRUE(playback.playing);
}

TEST(AnimationPlaybackTest, ForceLoopKeepsNonLoopingAiAnimationAlive) {
    const AnimationClip clip = Clip(1000);
    AnimPlayback playback;
    playback.Start(&clip);
    playback.forceLoop = true;

    playback.Update(2250.0f);

    EXPECT_FLOAT_EQ(playback.currentTimeMs, 250.0f);
    EXPECT_TRUE(playback.playing);
}

TEST(AnimationPlaybackTest, NonLoopingClipStopsAtItsEnd) {
    const AnimationClip clip = Clip(1000);
    AnimPlayback playback;
    playback.Start(&clip);

    playback.Update(1250.0f);

    EXPECT_FLOAT_EQ(playback.currentTimeMs, 1000.0f);
    EXPECT_FALSE(playback.playing);
}

TEST(AnimationEvaluationTest, DistinguishesSingleFrameActionFromMovingClip) {
    AnimationClip pose = Clip(801);
    pose.rotationKeys.push_back(AnimRotationKey{0, 0, glm::quat(1.f, 0.f, 0.f, 0.f)});
    EXPECT_FALSE(AnimationClipHasTemporalMotion(pose));

    AnimationClip moving = Clip(14081);
    moving.rotationKeys = {
        AnimRotationKey{0, 0, glm::quat(1.f, 0.f, 0.f, 0.f)},
        AnimRotationKey{0, 4320, glm::quat(0.98f, 0.1f, 0.f, 0.f)},
    };
    EXPECT_TRUE(AnimationClipHasTemporalMotion(moving));
}

TEST(AnimationRenderGateTest, RequiresActivePlaybackAndSkinGeometry) {
    const AnimationClip clip = Clip(1000);
    AnimPlayback playback;
    playback.Start(&clip);

    EXPECT_TRUE(ShouldUseSkinnedReplacement(playback, 2, 5, false, true));
    EXPECT_FALSE(ShouldUseSkinnedReplacement(playback, 2, 5, false, false));
    playback.Pause();
    EXPECT_FALSE(ShouldUseSkinnedReplacement(playback, 2, 5, false, true));
    EXPECT_FALSE(ShouldUseSkinnedReplacement(playback, 5, 5, false, true));
    EXPECT_FALSE(ShouldUseSkinnedReplacement(playback, 2, 5, true, true));
}

TEST(AnimationRenderGateTest, RejectsIncompleteSkinnedGeometry) {
    ParsedGeometry geometry;
    geometry.bones.resize(1);
    geometry.vertices.resize(3);
    geometry.triangles.push_back({0, 1, 99});

    EXPECT_EQ(CountRenderableSkinnedTriangles(geometry), 0u);
    EXPECT_FALSE(HasRenderableSkinnedGeometry(geometry));
}

TEST(AnimationRenderGateTest, AcceptsCompleteSkinnedGeometry) {
    ParsedGeometry geometry;
    geometry.bones.resize(1);
    geometry.vertices.resize(3);
    geometry.triangles.push_back({0, 1, 2});

    EXPECT_EQ(CountRenderableSkinnedTriangles(geometry), 1u);
    EXPECT_TRUE(HasRenderableSkinnedGeometry(geometry));
}

TEST(LevelObjectAnimationDefaultsTest, HumanPlayerUsesOpenIgiDefaultSkeletonAndStand) {
    const auto defaults = ResolveSoldierAnimationDefaults("HumanPlayer", -1, -1);
    EXPECT_EQ(defaults.first, 0);
    EXPECT_EQ(defaults.second, 2);
}

TEST(LevelObjectAnimationDefaultsTest, AuthoredSoldierAnimationFieldsRemainAuthoritative) {
    const auto defaults = ResolveSoldierAnimationDefaults("HumanSoldier", 3, 17);
    EXPECT_EQ(defaults.first, 3);
    EXPECT_EQ(defaults.second, 17);
}

TEST(AnimationSkinningTest, AccumulatesExtraInfluencesIntoTheirBaseVertex) {
    ParsedGeometry geometry;
    geometry.baseVertexCount = 2;
    geometry.extraInfluenceCount = 1;
    geometry.bones.resize(1);
    geometry.vertices.resize(3);
    for (auto& vertex : geometry.vertices) {
        vertex.rawPos = glm::vec3(40.96f, 0.0f, 0.0f);
        vertex.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        vertex.boneIndex = 0;
        vertex.weight = 0.5f;
    }
    geometry.vertices[2].localVertexId = 0;

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    SkinSkinnedVertices(geometry, nullptr, positions, normals);

    ASSERT_EQ(positions.size(), 2u);
    EXPECT_NEAR(positions[0].x, 1.0f, 1e-5f);
    EXPECT_NEAR(positions[1].x, 0.5f, 1e-5f);
    EXPECT_EQ(normals[0], glm::vec3(0.0f, 0.0f, 1.0f));
}

TEST(AnimationEvaluationTest, RotationTrackChangesWorldPose) {
    AnimationRegistry registry;
    AnimationClip clip = Clip(1000, true);
    clip.bones = {
        AnimBone{0, "root", -1, glm::vec3(0.0f)},
        AnimBone{1, "arm", 0, glm::vec3(1.0f, 0.0f, 0.0f)},
    };
    AnimRotationKey key0;
    key0.bone = 1;
    key0.time_ms = 0;
    key0.q0 = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    AnimRotationKey key1 = key0;
    key1.time_ms = 1000;
    key1.q0 = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    clip.rotationKeys = {key0, key1};

    std::vector<glm::mat4> atStart;
    std::vector<glm::mat4> atEnd;
    registry.EvaluateWorld(&clip, 0.0f, atStart);
    registry.EvaluateWorld(&clip, 1000.0f, atEnd);

    ASSERT_EQ(atStart.size(), 2u);
    ASSERT_EQ(atEnd.size(), 2u);
    const glm::vec4 startPoint = atStart[1] * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    const glm::vec4 endPoint = atEnd[1] * glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    EXPECT_GT(glm::length(startPoint - endPoint), 0.01f);
}

TEST(AnimationEvaluationTest, AlignsBefWorldPoseToMefBindRoot) {
    AnimationClip clip = Clip(1000);
    clip.bones = {
        AnimBone{0, "root", -1, glm::vec3(5.0f)},
        AnimBone{1, "child", 0, glm::vec3(2.0f, 0.0f, 0.0f)},
    };
    clip.translationKeys = {
        AnimTranslationKey{0, 0, glm::vec3(5.0f)},
        AnimTranslationKey{0, 1000, glm::vec3(9.0f)},
    };

    AnimationRegistry registry;
    std::vector<glm::mat4> bindWorld;
    registry.EvaluateWorld(&clip, 0.0f, bindWorld);
    std::vector<glm::mat4> animatedWorld;
    registry.EvaluateWorld(&clip, 500.0f, animatedWorld);

    const std::vector<glm::vec3> mefBindWorld = {
        glm::vec3(10.0f), glm::vec3(12.0f, 0.0f, 0.0f)};
    AlignAnimationWorldToMefBind(bindWorld, mefBindWorld, animatedWorld);

    EXPECT_NEAR(animatedWorld[0][3].x, 12.0f, 0.001f);
    EXPECT_NEAR(animatedWorld[0][3].y, 12.0f, 0.001f);
    EXPECT_NEAR(animatedWorld[0][3].z, 12.0f, 0.001f);
}

TEST(GraphCameraTargetTest, SelectedNodeUsesGraphOffset) {
    GraphFile graph;
    graph.valid = true;
    graph.nodes.push_back(Node(7, 10.0, 20.0, 30.0));

    const GraphCameraTarget target = ResolveGraphCameraTarget(
        graph, true, 7, glm::dvec3(100.0, 200.0, 300.0), glm::dvec3(1.0));

    EXPECT_EQ(target.kind, GraphCameraTargetKind::GraphNode);
    EXPECT_EQ(target.node_id, 7);
    EXPECT_DOUBLE_EQ(target.position.x, 110.0);
    EXPECT_DOUBLE_EQ(target.position.y, 220.0);
    EXPECT_DOUBLE_EQ(target.position.z, 330.0);
}

TEST(GraphCameraTargetTest, VisibleGraphWithoutNodeSelectionUsesOrigin) {
    GraphFile graph;
    graph.valid = true;
    graph.nodes.push_back(Node(1, 10.0, 20.0, 30.0));

    const GraphCameraTarget target = ResolveGraphCameraTarget(
        graph, true, -1, glm::dvec3(100.0, 200.0, 300.0), glm::dvec3(1.0));

    EXPECT_EQ(target.kind, GraphCameraTargetKind::GraphOrigin);
    EXPECT_DOUBLE_EQ(target.position.x, 100.0);
    EXPECT_DOUBLE_EQ(target.position.y, 200.0);
    EXPECT_DOUBLE_EQ(target.position.z, 300.0);
}

TEST(GraphCameraTargetTest, FallsBackToObjectWhenGraphIsHidden) {
    GraphFile graph;
    graph.valid = true;

    const GraphCameraTarget target = ResolveGraphCameraTarget(
        graph, false, 3, glm::dvec3(100.0), glm::dvec3(4.0, 5.0, 6.0));

    EXPECT_EQ(target.kind, GraphCameraTargetKind::Object);
    EXPECT_EQ(target.position, glm::dvec3(4.0, 5.0, 6.0));
}

TEST(GraphCameraTargetTest, HiddenOverlayUsesRelatedAiGraphOrigin) {
    GraphFile graph;
    graph.valid = true;

    const GraphCameraTarget target = ResolveGraphCameraTarget(
        graph, false, -1, glm::dvec3(100.0), glm::dvec3(4.0, 5.0, 6.0),
        glm::dvec3(400.0, 500.0, 600.0));

    EXPECT_EQ(target.kind, GraphCameraTargetKind::GraphOrigin);
    EXPECT_EQ(target.position, glm::dvec3(400.0, 500.0, 600.0));
}

TEST(GraphCameraTargetTest, SelectedSoldierUsesNestedHumanAiGraphOrigin) {
    std::vector<LevelObject> objects(3);
    objects[0].type = "HumanSoldier";
    objects[0].childrenIndices = {1};
    objects[1].type = "HumanAI";
    objects[1].aiGraphTaskId = 7;
    objects[2].type = "AIGraph";
    objects[2].taskId = "7";
    objects[2].pos = glm::dvec3(1000.0, 2000.0, 3000.0);

    const auto origin = FindRelatedGraphOrigin(objects, 0);

    ASSERT_TRUE(origin.has_value());
    EXPECT_EQ(*origin, glm::dvec3(1000.0, 2000.0, 3000.0));
}

TEST(GraphCameraTargetTest, UnrelatedSelectionRejectsStaleOverlay) {
    EXPECT_FALSE(GraphOverlayMatchesSelection(true, "", "7"));
    EXPECT_FALSE(GraphOverlayMatchesSelection(true, "12", "7"));
    EXPECT_TRUE(GraphOverlayMatchesSelection(true, "12", "12"));
    EXPECT_TRUE(GraphOverlayMatchesSelection(false, "", "7"));
}

TEST(GraphCameraTargetTest, F11UsesOnlyTheSelectedGraphsNode) {
    GraphFile graph;
    graph.valid = true;
    graph.nodes.push_back(Node(7, 10.0, 20.0, 30.0));

    const GraphCameraTarget stale = ResolveF11CameraTarget(
        graph, true, true, 7, "12", "7",
        glm::dvec3(100.0), glm::dvec3(1.0));
    EXPECT_EQ(stale.kind, GraphCameraTargetKind::Object);
    EXPECT_EQ(stale.position, glm::dvec3(1.0));

    const GraphCameraTarget selected = ResolveF11CameraTarget(
        graph, true, true, 7, "7", "7",
        glm::dvec3(100.0), glm::dvec3(1.0));
    EXPECT_EQ(selected.kind, GraphCameraTargetKind::GraphNode);
    EXPECT_EQ(selected.position, glm::dvec3(110.0, 120.0, 130.0));
}

TEST(GraphCameraTargetTest, F11UsesRelatedGraphOriginBeforeOverlayOpen) {
    GraphFile graph;
    graph.valid = true;

    const GraphCameraTarget target = ResolveF11CameraTarget(
        graph, true, false, -1, "12", "",
        glm::dvec3(100.0), glm::dvec3(1.0),
        glm::dvec3(400.0, 500.0, 600.0));
    EXPECT_EQ(target.kind, GraphCameraTargetKind::GraphOrigin);
    EXPECT_EQ(target.position, glm::dvec3(400.0, 500.0, 600.0));
}

TEST(GraphCameraTargetTest, F11GraphPosePlacesCameraBackAndFacesTarget) {
    const GraphCameraPose pose = MakeF11GraphCameraPose(
        glm::dvec3(100.0, 200.0, 300.0),
        glm::dvec3(0.0, 1.0, 0.0),
        4096.0);

    EXPECT_EQ(pose.position, glm::dvec3(100.0, -3896.0, 300.0));
    EXPECT_NEAR(pose.yaw_degrees, 0.0f, 0.001f);
    EXPECT_NEAR(pose.pitch_degrees, 0.0f, 0.001f);
}

TEST(GraphCameraTargetTest, SelectedNestedChildWalksBackToSoldierGraph) {
    std::vector<LevelObject> objects(4);
    objects[0].type = "HumanSoldier";
    objects[0].childrenIndices = {1, 2};
    objects[1].type = "Gun";
    objects[1].childrenIndices = {3};
    objects[3].type = "HumanAI";
    objects[3].aiGraphTaskId = 12;
    objects[2].type = "AIGraph";
    objects[2].taskId = "12";
    objects[2].pos = glm::dvec3(12.0, 24.0, 36.0);

    EXPECT_EQ(FindRelatedGraphTaskId(objects, 1), "12");
    const auto origin = FindRelatedGraphOrigin(objects, 1);
    ASSERT_TRUE(origin.has_value());
    EXPECT_EQ(*origin, glm::dvec3(12.0, 24.0, 36.0));
}

#if defined(_WIN32)
TEST(AnimationIntegrationTest, EditorSubmitsSkinnedAiReplacements) {
    if (std::getenv("IGI_RUN_LIVE_EDITOR_TESTS") == nullptr) {
        GTEST_SKIP() << "Set IGI_RUN_LIVE_EDITOR_TESTS=1 for the opt-in editor integration run";
    }

    namespace fs = std::filesystem;
    const char* gamePathEnv = std::getenv("IGI_GAME_PATH");
    if (gamePathEnv == nullptr || *gamePathEnv == '\0') {
        GTEST_SKIP() << "IGI_GAME_PATH must point to the co-located IGI install";
    }

    const fs::path gameRoot(gamePathEnv);
    const fs::path configPath = gameRoot / "editor" / "qed" / "qedconfig.qsc";
    const fs::path configQvmPath = gameRoot / "editor" / "qed" / "qedconfig.qvm";
    const fs::path builtExePath = fs::path(Utils::GetExeDirectory()) / "igi1ed.exe";
    const fs::path deployedExePath = gameRoot / "igi1ed.exe";
    const fs::path exePath = fs::exists(deployedExePath) ? deployedExePath : builtExePath;
    const fs::path logPath = exePath.parent_path() / "igi1ed.log";
    ASSERT_TRUE(fs::exists(configPath));
    ASSERT_TRUE(fs::exists(configQvmPath));
    ASSERT_TRUE(fs::exists(exePath));

    const std::string configSource = [&]() {
        std::ifstream in(configPath, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(in)), {});
    }();
    std::ifstream qvmIn(configQvmPath, std::ios::binary);
    const std::vector<char> configQvm(
        (std::istreambuf_iterator<char>(qvmIn)), std::istreambuf_iterator<char>());
    const auto restoreConfig = [&]() {
        std::ofstream out(configPath, std::ios::binary | std::ios::trunc);
        out.write(configSource.data(), static_cast<std::streamsize>(configSource.size()));
        out.close();
        std::ofstream qvmOut(configQvmPath, std::ios::binary | std::ios::trunc);
        qvmOut.write(configQvm.data(), static_cast<std::streamsize>(configQvm.size()));
    };

    const size_t loggingSetting = configSource.find("QEDLogs(FALSE);");
    ASSERT_NE(loggingSetting, std::string::npos);
    std::string enabledConfig = configSource;
    enabledConfig.replace(loggingSetting, std::string("QEDLogs(FALSE);").size(),
                          "QEDLogs(TRUE);");
    {
        std::ofstream out(configPath, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(out.is_open());
        out << enabledConfig;
    }
    struct RestoreOnExit {
        std::function<void()> restore;
        ~RestoreOnExit() { restore(); }
    } restore{restoreConfig};

    const auto logBefore = fs::exists(logPath) ? fs::file_size(logPath) : 0;
    std::string command = "\"" + exePath.string() + "\" -level 12";
    std::vector<char> commandBuffer(command.begin(), command.end());
    commandBuffer.push_back('\0');
    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo{};
    ASSERT_TRUE(CreateProcessA(nullptr, commandBuffer.data(), nullptr, nullptr, FALSE,
                               CREATE_NEW_CONSOLE, nullptr, gameRoot.string().c_str(),
                               &startupInfo, &processInfo));
    const DWORD waitResult = WaitForSingleObject(processInfo.hProcess, 10000);
    if (waitResult == WAIT_TIMEOUT) {
        ASSERT_TRUE(TerminateProcess(processInfo.hProcess, 0));
        ASSERT_EQ(WaitForSingleObject(processInfo.hProcess, 5000), WAIT_OBJECT_0);
    } else {
        ASSERT_EQ(waitResult, WAIT_OBJECT_0);
    }
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    std::ifstream logIn(logPath, std::ios::binary);
    const std::string log((std::istreambuf_iterator<char>(logIn)), {});
    ASSERT_GT(log.size(), logBefore);
    const std::string newLog = log.substr(logBefore);
    const auto initPos = newLog.find("Animation init complete: ");
    ASSERT_NE(initPos, std::string::npos);
    std::smatch initMatch;
    const std::string initLog = newLog.substr(initPos);
    ASSERT_TRUE(std::regex_search(
        initLog, initMatch,
        std::regex(R"(Animation init complete: (\d+) of (\d+) eligible AI NPCs)")));
    const int initialized = std::stoi(initMatch[1].str());
    const int eligible = std::stoi(initMatch[2].str());
    EXPECT_GT(initialized, 0);
    EXPECT_EQ(initialized, eligible);
    EXPECT_NE(newLog.find("Skinned/animated replacement active"), std::string::npos);
    EXPECT_NE(newLog.find("Skinned draw submitted for"), std::string::npos);
    EXPECT_NE(newLog.find("Skinned pose advanced for"), std::string::npos);

    // A single log line can pass while only one model animates. Every model
    // that replaced its static mesh must also produce a later pose change.
    const std::regex draw_re(R"(Skinned draw submitted for ([^ ]+) \()");
    const std::regex pose_re(R"(Skinned pose advanced for ([^\r\n]+))");
    std::set<std::string> submitted_models;
    std::set<std::string> advanced_models;
    for (std::sregex_iterator it(newLog.begin(), newLog.end(), draw_re), end;
         it != end; ++it) {
        submitted_models.insert((*it)[1].str());
    }
    for (std::sregex_iterator it(newLog.begin(), newLog.end(), pose_re), end;
         it != end; ++it) {
        advanced_models.insert((*it)[1].str());
    }
    ASSERT_FALSE(submitted_models.empty());
    EXPECT_EQ(advanced_models, submitted_models)
        << "submitted AI model set did not all advance a rendered pose";
}
#endif
