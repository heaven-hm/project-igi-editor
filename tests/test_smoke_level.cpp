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
    if (meshResolved) {
        model.meshSource = "res:level2.res";
        model.meshSourceKind = SmokeMeshSource::ThisLevelArchive;
        model.meshSourceLevel = 2;
    }
    return model;
}

SmokeModelResult MakeFromSource(const std::string& modelId, SmokeMeshSource kind, int level) {
    SmokeModelResult model = MakeResolved(modelId, true);
    model.meshSourceKind = kind;
    model.meshSourceLevel = level;
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

// ── the WHY half: where a model resolved from ────────────────────────────────
//
// The engine resolves a model from exactly one archive, the one of the level that
// owns it (Mesh_Load HOOK 0x4CF390, src/model/model.c:364).  Everything else is the
// editor's fallback, so the classification has to name the branch, not just say
// "resolved".

TEST(SmokeLevelCore, ArchivePathIsStrippedFromAResSourceButNotFromADiskPath) {
    EXPECT_EQ(SmokeArchivePathFromSource("res:C:\\IGI\\missions\\location0\\level7\\models\\level7.res"),
              "C:\\IGI\\missions\\location0\\level7\\models\\level7.res");
    EXPECT_EQ(SmokeArchivePathFromSource("C:\\editor\\models\\level7\\001_02_1.mef"), "");
    EXPECT_EQ(SmokeArchivePathFromSource(""), "");
    EXPECT_EQ(SmokeArchivePathFromSource("res:"), "");
}

TEST(SmokeLevelCore, SourceLevelComesFromTheLevelComponentOfThePath) {
    EXPECT_EQ(SmokeSourceLevelFromPath(
                  "Z:\\IGI\\missions\\location0\\level7\\models\\level7.res"), 7);
    EXPECT_EQ(SmokeSourceLevelFromPath(
                  "Z:\\IGI\\missions\\location0\\level12\\textures\\level12.res"), 12);
    // The shared archive is not a level archive.
    EXPECT_EQ(SmokeSourceLevelFromPath(
                  "Z:\\IGI\\missions\\location0\\common\\models\\location0.res"), 0);
    // A path with no level component, and a word that merely ends in "level".
    EXPECT_EQ(SmokeSourceLevelFromPath("Z:\\IGI\\textures\\001_02_1.tex"), 0);
    EXPECT_EQ(SmokeSourceLevelFromPath("Z:\\IGI\\sublevel3\\x.mef"), 0);
    // Case-insensitive, and the file name wins when the directory disagrees.
    EXPECT_EQ(SmokeSourceLevelFromPath("z:\\igi\\LEVEL9\\models\\level9.res"), 9);
}

TEST(SmokeLevelCore, SourceClassificationNamesTheBranchThatAnswered) {
    using S = SmokeMeshSource;
    // Nothing resolved.
    EXPECT_EQ(ClassifySmokeMeshSource(false, false, 0, 0, 5), S::Missing);
    // A loose file is never an archive, whatever the path says.
    EXPECT_EQ(ClassifySmokeMeshSource(true, true, 7, 7, 5), S::LooseFile);
    // Shared common archive.
    EXPECT_EQ(ClassifySmokeMeshSource(true, false, 0, 7, 5), S::CommonArchive);
    // The level being examined answered from its own archive: the engine's path.
    // The DAT owner map is "the first level whose .dat lists the model", so a hit
    // in level 5's archive while examining level 5 is the level's own archive even
    // when the map names level 1.
    EXPECT_EQ(ClassifySmokeMeshSource(true, false, 5, 1, 5), S::ThisLevelArchive);
    EXPECT_EQ(ClassifySmokeMeshSource(true, false, 5, 0, 5), S::ThisLevelArchive);
    // A different level answered and it is the one the DAT map names: the
    // renderer's owner-level branch.
    EXPECT_EQ(ClassifySmokeMeshSource(true, false, 3, 3, 5), S::OwnerLevelArchive);
    // A different level answered and it is not the DAT owner: the blind
    // cross-level search, which in game would find nothing.
    EXPECT_EQ(ClassifySmokeMeshSource(true, false, 7, 3, 5), S::OtherLevelArchive);
    EXPECT_EQ(ClassifySmokeMeshSource(true, false, 3, 0, 5), S::OtherLevelArchive);
    EXPECT_STREQ(SmokeMeshSourceLabel(S::ThisLevelArchive), "this level");
    EXPECT_STREQ(SmokeMeshSourceLabel(S::OwnerLevelArchive), "owner level");
    EXPECT_STREQ(SmokeMeshSourceLabel(S::OtherLevelArchive), "other level");
    EXPECT_STREQ(SmokeMeshSourceLabel(S::LooseFile), "loose file");
}

TEST(SmokeLevelCore, DurationIsHumanReadableAtEveryScale) {
    EXPECT_EQ(FormatSmokeDurationMs(0), "0 ms");
    EXPECT_EQ(FormatSmokeDurationMs(812), "812 ms");
    EXPECT_EQ(FormatSmokeDurationMs(1000), "1.00 s");
    EXPECT_EQ(FormatSmokeDurationMs(1234), "1.23 s");
    EXPECT_EQ(FormatSmokeDurationMs(61500), "1m 1.5s");
}

TEST(SmokeLevelCore, PerLevelCountsMeshSourcesAndDistinctTextures) {
    SmokeLevelResult result;
    result.levelNo = 7;
    result.models.push_back(MakeFromSource("001_02_1", SmokeMeshSource::ThisLevelArchive, 7));
    result.models.push_back(MakeFromSource("001_02_1", SmokeMeshSource::ThisLevelArchive, 7));
    result.models.push_back(MakeFromSource("435_01_1", SmokeMeshSource::OtherLevelArchive, 3));
    result.models.push_back(MakeFromSource("438_01_1", SmokeMeshSource::OwnerLevelArchive, 3));
    result.models.push_back(MakeFromSource("436_01_1", SmokeMeshSource::CommonArchive, 0));
    result.models.push_back(MakeFromSource("437_01_1", SmokeMeshSource::LooseFile, 7));

    // Two placements of one texture id is one distinct texture.
    result.models[0].textureIds = {"001_02_1", "001_02_2"};
    result.models[0].resolvedTextureIds = {"001_02_1"};
    result.models[0].missingTextureIds = {"001_02_2"};
    result.models[1].textureIds = {"001_02_1"};
    result.models[1].resolvedTextureIds = {"001_02_1"};

    const SmokeTotals totals = SummarizeSmokeLevel(result);
    EXPECT_EQ(totals.resolvedFromThisLevel, 2);
    EXPECT_EQ(totals.resolvedFromOwnerLevel, 1);
    EXPECT_EQ(totals.resolvedFromOtherLevel, 1);
    EXPECT_EQ(totals.resolvedFromCommonArchive, 1);
    EXPECT_EQ(totals.resolvedFromLooseFile, 1);
    EXPECT_EQ(totals.distinctTextures, 2);
    EXPECT_EQ(totals.distinctTexturesResolved, 1);
    EXPECT_EQ(totals.distinctTexturesMissing, 1);
    EXPECT_EQ(totals.texturesResolved, 2);   // placements, not distinct
    EXPECT_EQ(totals.texturesMissing, 1);
}

// A missing id carries the level(s) that reference it, so the run total can say
// "on level 7" without the reader scanning every level block.
TEST(SmokeLevelCore, MissingEntriesRecordTheirLevel) {
    SmokeLevelResult result;
    result.levelNo = 7;
    result.models.push_back(MakeResolved("s332_02_1", false));
    result.models.push_back(MakeResolved("s332_02_1", false));

    const SmokeTotals totals = SummarizeSmokeLevel(result);
    ASSERT_EQ(totals.missingModelIds.size(), 1u);
    ASSERT_EQ(totals.missingModelIds[0].levels.size(), 1u);
    EXPECT_EQ(totals.missingModelIds[0].levels[0], 7);
    EXPECT_EQ(totals.missingModelIds[0].placements, 2);
}

// ── the run total: unions, not sums of per-level distinct counts ─────────────

TEST(SmokeLevelCore, RunTotalsUnionDistinctModelsAcrossLevels) {
    SmokeLevelResult level7;
    level7.levelNo = 7;
    level7.models.push_back(MakeFromSource("001_02_1", SmokeMeshSource::ThisLevelArchive, 7));
    level7.models.push_back(MakeResolved("s332_02_1", false));

    SmokeLevelResult level8;
    level8.levelNo = 8;
    // The same two models appear again: distinct across the run stays two, and the
    // missing model's placements add up across levels.
    level8.models.push_back(MakeFromSource("001_02_1", SmokeMeshSource::OtherLevelArchive, 7));
    level8.models.push_back(MakeResolved("s332_02_1", false));

    const SmokeRunTotals run = SummarizeSmokeRun(
        {SummarizeSmokeLevel(level7), SummarizeSmokeLevel(level8)});

    EXPECT_EQ(run.levelsCounted, 2);
    EXPECT_EQ(run.objectsExamined, 4);
    EXPECT_EQ(run.objectsResolved, 2);
    EXPECT_EQ(run.modelsMissing, 2);      // placements: sum
    EXPECT_EQ(run.distinctModels, 2);     // union, NOT 4
    EXPECT_EQ(run.distinctResolved, 1);
    EXPECT_EQ(run.distinctMissing, 1);
    EXPECT_EQ(run.resolvedFromThisLevel, 1);
    EXPECT_EQ(run.resolvedFromOtherLevel, 1);

    ASSERT_EQ(run.missingModelIds.size(), 1u);
    EXPECT_EQ(run.missingModelIds[0].id, "s332_02_1");
    EXPECT_EQ(run.missingModelIds[0].placements, 2);
    ASSERT_EQ(run.missingModelIds[0].levels.size(), 2u);
    EXPECT_EQ(run.missingModelIds[0].levels[0], 7);
    EXPECT_EQ(run.missingModelIds[0].levels[1], 8);
}

TEST(SmokeLevelCore, RunTotalsUnionDistinctTexturesAndSumPlacements) {
    SmokeLevelResult level1;
    level1.levelNo = 1;
    SmokeModelResult model = MakeResolved("001_02_1", true);
    model.textureIds = {"tex_a", "tex_b"};
    model.resolvedTextureIds = {"tex_a"};
    model.missingTextureIds = {"tex_b"};
    level1.models.push_back(model);

    SmokeLevelResult level2;
    level2.levelNo = 2;
    SmokeModelResult second = MakeResolved("001_02_1", true);
    second.textureIds = {"tex_a"};
    second.resolvedTextureIds = {"tex_a"};
    level2.models.push_back(second);

    const SmokeRunTotals run = SummarizeSmokeRun(
        {SummarizeSmokeLevel(level1), SummarizeSmokeLevel(level2)});

    EXPECT_EQ(run.texturesResolved, 2);          // placements
    EXPECT_EQ(run.distinctTextures, 2);          // tex_a, tex_b
    EXPECT_EQ(run.distinctTexturesResolved, 1);
    EXPECT_EQ(run.distinctTexturesMissing, 1);
    ASSERT_EQ(run.missingTextureIds.size(), 1u);
    EXPECT_EQ(run.missingTextureIds[0].id, "tex_b");
    ASSERT_EQ(run.missingTextureIds[0].levels.size(), 1u);
    EXPECT_EQ(run.missingTextureIds[0].levels[0], 1);
}

TEST(SmokeLevelCore, EmptyRunTotalsAreZero) {
    const SmokeRunTotals run = SummarizeSmokeRun({});
    EXPECT_EQ(run.levelsCounted, 0);
    EXPECT_EQ(run.objectsExamined, 0);
    EXPECT_EQ(run.distinctModels, 0);
    EXPECT_TRUE(run.missingModelIds.empty());
}
