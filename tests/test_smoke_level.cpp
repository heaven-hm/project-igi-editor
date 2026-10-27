/******************************************************************************
 * @file    test_smoke_level.cpp
 * @brief   Host-portable core of the `--smoke-level` loader check: which objects
 *          the check examines, and how the per-level totals are counted.
 *
 * The Windows half (source/cli/smoke_level.cpp) decompiles the level and resolves
 * each model through Renderer_Objects; that half cannot run here (it needs the
 * Windows GL target).  What is pinned here is the decision the PASS/FAIL hangs on:
 *   * the collector examines exactly the placements the object draw loop would
 *     call GetOrLoadMesh for (renderer_objects.cpp:941-976), and
 *   * the totals separate placements from distinct models, so a regression that
 *     makes every model resolve to nothing cannot report "0 missing".
 ******************************************************************************/

#include <gtest/gtest.h>

#include "../source/cli/smoke_level_core.h"

namespace {

LevelObject MakeObj(const std::string& modelId, const std::string& type = "EditRigidObj") {
    LevelObject obj;
    obj.modelId = modelId;
    obj.type = type;
    obj.name = "obj_" + modelId;
    obj.taskId = "100";
    return obj;
}

SmokeModelResult MakeResolved(const std::string& modelId, bool meshResolved) {
    SmokeModelResult model;
    model.ref.modelId = modelId;
    model.ref.type = "EditRigidObj";
    model.ref.name = "obj_" + modelId;
    model.meshResolved = meshResolved;
    if (meshResolved) model.meshSource = "res:level2.res";
    return model;
}

}  // namespace

// The collector must mirror the object draw loop's filters, or the check would
// report failures for things the renderer never resolves.
TEST(SmokeLevelCore, CollectorMirrorsTheObjectDrawLoopFilters) {
    std::vector<LevelObject> objects;
    objects.push_back(MakeObj("001_02_1"));                       // examined
    LevelObject building = MakeObj("200_01_1", "Building");
    building.isBuilding = true;
    objects.push_back(building);                                  // examined, isBuilding kept
    objects.push_back(MakeObj(""));                               // no model id
    LevelObject deleted = MakeObj("003_01_1");
    deleted.deleted = true;
    objects.push_back(deleted);                                   // deleted
    LevelObject waypoint = MakeObj("004_01_1");
    waypoint.isSplineWaypoint = true;
    objects.push_back(waypoint);                                  // drawn by renderer_splines.cpp
    LevelObject container = MakeObj("005_01_1");
    container.isSplineContainer = true;
    objects.push_back(container);
    objects.push_back(MakeObj("colbox"));                         // renderer skip predicate
    objects.push_back(MakeObj("colbox66"));

    SmokeCollectStats stats;
    const auto refs = CollectSmokeModels(
        objects,
        [](const std::string& id) {
            return id == "colbox" || id == "colbox2" || id == "colbox4" || id == "colbox66";
        },
        &stats);

    ASSERT_EQ(refs.size(), 2u);
    EXPECT_EQ(refs[0].modelId, "001_02_1");
    EXPECT_FALSE(refs[0].isBuilding);
    EXPECT_EQ(refs[1].modelId, "200_01_1");
    EXPECT_TRUE(refs[1].isBuilding);
    EXPECT_EQ(refs[1].type, "Building");

    EXPECT_EQ(stats.totalObjects, 8);
    EXPECT_EQ(stats.deleted, 1);
    EXPECT_EQ(stats.withoutModelId, 1);
    EXPECT_EQ(stats.splineRendered, 2);
    EXPECT_EQ(stats.rendererSkipped, 2);
}

// An empty skip predicate keeps every id (used when the caller has no renderer).
TEST(SmokeLevelCore, CollectorKeepsEveryIdWithoutASkipPredicate) {
    std::vector<LevelObject> objects{MakeObj("colbox"), MakeObj("001_02_1")};
    const auto refs = CollectSmokeModels(objects, nullptr);
    ASSERT_EQ(refs.size(), 2u);
    EXPECT_EQ(refs[0].modelId, "colbox");
}

// Placements and distinct models are different numbers: two placements of one
// missing model is one distinct missing name, not two.
TEST(SmokeLevelCore, TotalsSeparatePlacementsFromDistinctModels) {
    SmokeLevelResult result;
    result.levelNo = 2;
    result.models.push_back(MakeResolved("001_02_1", true));
    result.models.push_back(MakeResolved("001_02_1", true));
    result.models.push_back(MakeResolved("003_01_1", true));
    result.models.push_back(MakeResolved("700_08_1", false));
    result.models.push_back(MakeResolved("700_08_1", false));
    result.models.push_back(MakeResolved("700_09_1", false));

    const SmokeTotals totals = SummarizeSmokeLevel(result);
    EXPECT_EQ(totals.objectsExamined, 6);
    EXPECT_EQ(totals.objectsResolved, 3);
    EXPECT_EQ(totals.modelsMissing, 3);
    EXPECT_EQ(totals.distinctModels, 4);
    EXPECT_EQ(totals.distinctResolved, 2);
    EXPECT_EQ(totals.distinctMissing, 2);
    EXPECT_FALSE(totals.clean());
}

// Every missing id must be named, with the number of placements that reference it
// and an example object, so the failure can be located.
TEST(SmokeLevelCore, MissingModelsAreNamedWithTheirPlacementCount) {
    SmokeLevelResult result;
    SmokeModelResult first = MakeResolved("700_08_1", false);
    first.ref.type = "EditRigidObj";
    first.ref.name = "crate";
    SmokeModelResult second = MakeResolved("700_08_1", false);
    second.ref.name = "crate2";
    SmokeModelResult third = MakeResolved("435_01_6", false);
    third.ref.type = "HumanSoldier";
    third.ref.name = "guard";
    result.models = {first, second, third};

    const SmokeTotals totals = SummarizeSmokeLevel(result);
    ASSERT_EQ(totals.missingModelIds.size(), 2u);
    // Sorted by id: "435_01_6" before "700_08_1".
    EXPECT_EQ(totals.missingModelIds[0].id, "435_01_6");
    EXPECT_EQ(totals.missingModelIds[0].placements, 1);
    EXPECT_EQ(totals.missingModelIds[0].exampleType, "HumanSoldier");
    EXPECT_EQ(totals.missingModelIds[0].exampleName, "guard");
    EXPECT_EQ(totals.missingModelIds[1].id, "700_08_1");
    EXPECT_EQ(totals.missingModelIds[1].placements, 2);
    EXPECT_EQ(totals.missingModelIds[1].exampleName, "crate");
}

// Textures are counted across every placement, and a missing texture id is a
// failure on its own even when every mesh resolved.
TEST(SmokeLevelCore, MissingTexturesFailEvenWhenEveryMeshResolved) {
    SmokeLevelResult result;
    SmokeModelResult model = MakeResolved("001_02_1", true);
    model.textureIds = {"001_02_1", "001_02_2"};
    model.resolvedTextureIds = {"001_02_1"};
    model.missingTextureIds = {"001_02_2"};
    result.models.push_back(model);

    const SmokeTotals totals = SummarizeSmokeLevel(result);
    EXPECT_EQ(totals.modelsMissing, 0);
    EXPECT_EQ(totals.texturesResolved, 1);
    EXPECT_EQ(totals.texturesMissing, 1);
    ASSERT_EQ(totals.missingTextureIds.size(), 1u);
    EXPECT_EQ(totals.missingTextureIds[0].id, "001_02_2");
    EXPECT_FALSE(totals.clean());
}

// A level that loaded cleanly is the only clean case.
TEST(SmokeLevelCore, CleanLevelReportsPass) {
    SmokeLevelResult result;
    SmokeModelResult model = MakeResolved("001_02_1", true);
    model.textureIds = {"001_02_1"};
    model.resolvedTextureIds = {"001_02_1"};
    result.models.push_back(model);

    const SmokeTotals totals = SummarizeSmokeLevel(result);
    EXPECT_TRUE(totals.clean());
    EXPECT_EQ(totals.distinctMissing, 0);
    EXPECT_TRUE(totals.missingModelIds.empty());
    EXPECT_TRUE(totals.missingTextureIds.empty());
}
