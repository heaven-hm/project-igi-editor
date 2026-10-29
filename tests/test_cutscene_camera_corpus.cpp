/******************************************************************************
 * @file    test_cutscene_camera_corpus.cpp
 * @brief   The authored CutScene camera programme over the SHIPPED level data
 *          (GitHub issue #80).
 *
 * tests/test_cutscene_camera.cpp pins the link and the tick on hand-built
 * argument tokens, which proves the rule but not the projection: which objects
 * are a CutScene's keys is decided by the editor's own object list, and that is
 * what this suite runs on real data:
 *
 *   MISSIONS/location0/LEVEL<n>/objects.qsc
 *     -> QSC::Load             (source/level/qsc_parser.cpp)
 *     -> LevelObjects::Load    (source/level/level_objects.cpp:670-676, which
 *                               records each nested task's parent index and
 *                               appends objects in file order)
 *     -> igi::CollectAuthoredCameraKeyTokens   (the engine's walk order)
 *     -> igi::BuildAuthoredCutSceneProgramme   (CutScene_CollectKeys)
 *     -> igi::EvaluateAuthoredCutSceneAtTick   (CutScene_Interpolate)
 *
 * The three levels that author a cutscene are LEVEL1, LEVEL2 and LEVEL4
 * (measured: 9 CutScene calls and 120 EditCamera keys in their `objects.qsc`,
 * the counts docs/igi-re-parity/CUTSCENES.md section 5.1 records).
 *
 * WINDOWS LANE ONLY: `LevelObjects::Load` cannot be linked by the host-native
 * lane, which is why this is a separate file from the pure-rule suite -- a
 * combined file would take the whole suite out of the host lane. It self-skips
 * when IGI_GAME_PATH is unset or a retail file is absent, so it never fails for
 * a missing corpus.
 *
 * Oracle: /Users/haseeb-mir/Documents/Code/IGI-RE
 *   src/cutscene/cutscene.c:163-180   CutScene_CollectKeys 0x4F4880
 *   src/cutscene/cutscene.c:417-462   CutScene_Update 0x4F5400
 *   src/cutscene/cutscene.c:631-651   CutScene_Spline 0x4F6290
 *   src/editcam/editcamera.c:88-107   EditCamera_Init 0x507F10
 *****************************************************************************/

#include <gtest/gtest.h>

#include "level/level_common.h"
#include "level/level_objects.h"
#include "runtime/cutscene_camera.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace {

// The levels whose `objects.qsc` authors a CutScene task.
constexpr int kCutSceneLevels[] = {1, 2, 4};
// Measured on the retail scripts (CUTSCENES.md section 5.1): 9 CutScene calls
// and 120 EditCamera keys across those three levels.
constexpr size_t kRetailCutScenes = 9u;
constexpr size_t kRetailCameraKeys = 120u;

std::filesystem::path LevelObjectsQsc(int level) {
    const char* root = std::getenv("IGI_GAME_PATH");
    if (root == nullptr || *root == '\0') return {};
    return std::filesystem::path(root) / "MISSIONS" / "location0" /
        ("LEVEL" + std::to_string(level)) / "objects.qsc";
}

// The editor's projection of one loaded level: the flat object list the walk
// needs, plus the indices of the CutScene tasks in it.
struct LevelProjection {
    bool loaded = false;
    std::vector<igi::AuthoredCameraKeyObject> objects;
    std::vector<int> cut_scene_indices;
    size_t edit_camera_count = 0;
};

LevelProjection LoadLevel(int level) {
    LevelProjection projection;
    const std::filesystem::path path = LevelObjectsQsc(level);
    if (path.empty() || !std::filesystem::exists(path)) return projection;

    QSC qsc;
    qsc.Load(path.string().c_str());
    LevelObjects objects;
    objects.Load(nullptr, &qsc);

    const std::vector<LevelObject>& loaded = objects.GetObjects();
    projection.objects.reserve(loaded.size());
    for (size_t index = 0; index < loaded.size(); ++index) {
        igi::AuthoredCameraKeyObject projected;
        projected.parent_index = loaded[index].parentIndex;
        projected.type = loaded[index].type;
        projected.arg_tokens = loaded[index].argTokens;
        projected.deleted = loaded[index].deleted;
        projection.objects.push_back(std::move(projected));
        if (loaded[index].type == "CutScene") {
            projection.cut_scene_indices.push_back(static_cast<int>(index));
        }
        if (loaded[index].type == "EditCamera") {
            ++projection.edit_camera_count;
        }
    }
    projection.loaded = true;
    return projection;
}

}  // namespace

TEST(CutSceneCameraCorpus, EveryAuthoredCutSceneResolvesToItsOwnKeys) {
    size_t cut_scenes = 0;
    size_t keys = 0;
    for (const int level : kCutSceneLevels) {
        const LevelProjection projection = LoadLevel(level);
        if (!projection.loaded) {
            GTEST_SKIP() << "retail LEVEL" << level
                         << "/objects.qsc not found (set IGI_GAME_PATH)";
        }
        ASSERT_FALSE(projection.cut_scene_indices.empty())
            << "LEVEL" << level << " authors no CutScene";
        cut_scenes += projection.cut_scene_indices.size();

        for (const int cut_scene_index : projection.cut_scene_indices) {
            const std::vector<std::vector<std::string>> key_tokens =
                igi::CollectAuthoredCameraKeyTokens(projection.objects, cut_scene_index);
            // CutScene_Update returns before CollectKeys when the task has no
            // EditCamera child (cutscene.c:429-430); every retail CutScene has one.
            EXPECT_FALSE(key_tokens.empty())
                << "LEVEL" << level << " CutScene index " << cut_scene_index;
            keys += key_tokens.size();

            const igi::AuthoredCutSceneProgramme programme =
                igi::BuildAuthoredCutSceneProgramme(
                    projection.objects[static_cast<size_t>(cut_scene_index)].arg_tokens,
                    key_tokens);
            // Every retail key is a complete 23-token EditCamera call, so a
            // refusal here means the projection or the parse is wrong, not the data.
            EXPECT_TRUE(programme.valid)
                << "LEVEL" << level << " CutScene index " << cut_scene_index;
            EXPECT_EQ(programme.keys.size(), key_tokens.size())
                << "LEVEL" << level << " CutScene index " << cut_scene_index;
            if (programme.valid && !programme.keys.empty()) {
                // The last key's own duration is part of `end` (cutscene.c:163-180).
                EXPECT_GT(programme.end, 0.0f);
            }
        }
    }

    EXPECT_EQ(cut_scenes, kRetailCutScenes);
    EXPECT_EQ(keys, kRetailCameraKeys);
}

TEST(CutSceneCameraCorpus, EveryAuthoredEditCameraIsAKeyOfExactlyOneCutScene) {
    for (const int level : kCutSceneLevels) {
        const LevelProjection projection = LoadLevel(level);
        if (!projection.loaded) {
            GTEST_SKIP() << "retail LEVEL" << level
                         << "/objects.qsc not found (set IGI_GAME_PATH)";
        }

        size_t projected = 0;
        for (const int cut_scene_index : projection.cut_scene_indices) {
            projected += igi::CollectAuthoredCameraKeyTokens(
                projection.objects, cut_scene_index).size();
        }
        // An EditCamera that no CutScene owns would be authored data the engine
        // never plays either (CutScene_Update walks its own child chain), so the
        // two counts must agree. A key owned by TWO scenes would show up here as
        // projected > authored.
        EXPECT_EQ(projected, projection.edit_camera_count) << "LEVEL" << level;
    }
}

TEST(CutSceneCameraCorpus, EveryProjectedKeyHoldsItsAuthoredPoseAtItsStartTime) {
    // At t == keys[i].start_time the selected window's u is exactly 0, so
    // Hermite_Eval returns p1 unchanged (flare.c:55-61) whatever the smooth flags
    // do to the tangent neighbours. Evaluated at the EXACT start time, not at a
    // tick: `CutSceneTickFromTime` rounds to the nearest 1/30 s, so a tick-based
    // probe lands up to half a tick away and is a different question.
    //
    // CutSceneRulesCorpus.EveryRetailKeyHoldsItsAuthoredPoseAtItsStartTime asks
    // it of the rules module over a decompiled-text parse. This asks it of the
    // EDITOR'S OWN projection (LevelObjects -> CollectAuthoredCameraKeyTokens),
    // which is the half that suite cannot see.
    //
    // A zero-duration key shares its start time with the next one, and the scan
    // (cutscene.c:436-441) takes the LAST key whose time is <= t, so the later
    // key is the one that holds the pose. Those keys are skipped and the
    // resolution is asserted instead.
    int zero_duration_keys = 0;
    for (const int level : kCutSceneLevels) {
        const LevelProjection projection = LoadLevel(level);
        if (!projection.loaded) {
            GTEST_SKIP() << "retail LEVEL" << level
                         << "/objects.qsc not found (set IGI_GAME_PATH)";
        }

        for (const int cut_scene_index : projection.cut_scene_indices) {
            const std::vector<std::vector<std::string>> key_tokens =
                igi::CollectAuthoredCameraKeyTokens(projection.objects, cut_scene_index);
            const igi::AuthoredCutSceneProgramme programme =
                igi::BuildAuthoredCutSceneProgramme(
                    projection.objects[static_cast<size_t>(cut_scene_index)].arg_tokens,
                    key_tokens);
            if (!programme.valid) continue;

            for (size_t index = 0; index < programme.keys.size(); ++index) {
                igi::CutSceneCameraState state;
                ASSERT_TRUE(igi::EvaluateCutScene(programme.keys, programme.end,
                                                  programme.screen,
                                                  programme.keys[index].start_time, false,
                                                  &state))
                    << "LEVEL" << level << " key " << index;

                if (index + 1 < programme.keys.size() &&
                    programme.keys[index + 1].start_time <= programme.keys[index].start_time) {
                    ++zero_duration_keys;
                    EXPECT_EQ(state.active_key, static_cast<int>(index) + 1)
                        << "LEVEL" << level << " key " << index;
                    continue;
                }

                EXPECT_EQ(state.active_key, static_cast<int>(index))
                    << "LEVEL" << level << " key " << index;
                for (int axis = 0; axis < 3; ++axis) {
                    EXPECT_NEAR(state.position[axis],
                                programme.keys[index].params.position[axis], 1e-6)
                        << "LEVEL" << level << " key " << index << " axis " << axis;
                }
            }
        }
    }
    // The retail data really does contain hold keys, so this branch is exercised
    // rather than assumed.
    EXPECT_GT(zero_duration_keys, 0);
}

TEST(CutSceneCameraCorpus, TheTickEntryPointAgreesWithTheExactTimeAtTickZero) {
    // `CutSceneTimeFromTick(0)` is exactly 0, so the two entry points coincide
    // there and the tick path's own arithmetic is pinned against the exact one on
    // real data. (At a non-zero tick they legitimately differ: the engine's clock
    // is 30 Hz, cutscene.c:427.)
    for (const int level : kCutSceneLevels) {
        const LevelProjection projection = LoadLevel(level);
        if (!projection.loaded) {
            GTEST_SKIP() << "retail LEVEL" << level
                         << "/objects.qsc not found (set IGI_GAME_PATH)";
        }

        for (const int cut_scene_index : projection.cut_scene_indices) {
            const igi::AuthoredCutSceneProgramme programme =
                igi::BuildAuthoredCutSceneProgramme(
                    projection.objects[static_cast<size_t>(cut_scene_index)].arg_tokens,
                    igi::CollectAuthoredCameraKeyTokens(projection.objects, cut_scene_index));
            if (!programme.valid) continue;

            igi::CutSceneCameraState via_tick;
            ASSERT_TRUE(igi::EvaluateAuthoredCutSceneAtTick(programme, 0, &via_tick))
                << "LEVEL" << level;
            igi::CutSceneCameraState via_time;
            ASSERT_TRUE(igi::EvaluateCutScene(programme.keys, programme.end,
                                              programme.screen, 0.0f, false, &via_time))
                << "LEVEL" << level;

            EXPECT_EQ(via_tick.active_key, via_time.active_key) << "LEVEL" << level;
            for (int axis = 0; axis < 3; ++axis) {
                EXPECT_DOUBLE_EQ(via_tick.position[axis], via_time.position[axis])
                    << "LEVEL" << level << " axis " << axis;
            }
        }
    }
}
