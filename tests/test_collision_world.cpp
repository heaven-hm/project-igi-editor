// test_collision_world.cpp -- the engine's OBJECT collision world for the
// vertical ground probe (source/level/collision_world.h), the nearest-hit rule
// that mixes it with the terrain (source/level/collision_drop_rules.h), and the
// snap decision (source/level/terrain_snap_rules.h).
//
// WHY
//   The terrain snap asked the terrain only, so an AI on a building floor, roof
//   or tower platform was dropped to the terrain below it.  The engine's drop is
//   ONE query with two halves and a nearest-hit comparison (Coll_Drop HOOK
//   0x4CABF0 body, src/coll/coll.c:575-614):
//       Coll_LinesEx(t, results, points, ends, ...)                 // :577
//       Coll_VerticalHit(&h, ...)                                   // :591
//       if (r->found && !(Abs(p.z - h.pos.z) < Abs(p.z - r->pos.z)))
//           continue;                                               // :593-594
//   `r` is the object/task hit from Coll_LinesEx (HOOK 0x4CDB20,
//   src/model/coll.c:432-560), `h` the terrain hit; the OBJECT wins a tie.
//
//   Each rigid object answers with RigidDynCubeObj_LineHit HOOK 0x4C1840
//   (src/qasm/rigidhit.c:104-470): the model's HSMC collision set `q->mesh`
//   converted to world space by the object's matrix and scale.  The human probe
//   passes mesh index 1 (Human_ProbeGround, src/humanbase/human.c:1759,1771 ->
//   Coll_DropPointsEx -> Coll_LinesEx `q.mesh`, src/model/coll.c:515), i.e. the
//   SECOND CVTX/CFCE/TAMC set (Direct3DRender_LoadModel HOOK 0x49FB10,
//   src/bonemesh/d3drender.c:245-260).
//
//   Triangle filter: with q.allFaces = 0 (src/model/coll.c:495) a triangle is
//   tested only when its CMAT record's +4 word is -1 (rigidhit.c:255-256).
//
// THE CORPUS CASE, and the numbers the change moves
//   The shipped LEVEL1 places a sniper on the WaterTower platform:
//     Building "WaterTower" model 435_01_1 at (24658918, -55966376, 174413136),
//       yaw pi                                          objects.qsc:657
//     HumanSoldier (AITYPE_SNIPER) at (24658674, -55980796, 174487424)
//       objects.qsc:1229 -- a child of Container "AI"/"Guards", NOT of the
//       building, so the editor's protected-interior-child rule does not apply.
//   The editor's own terrain query at that (x, y) answered 174413136
//   (igi1ed.log:237, "authoredZ=174487424 ... terrainZ=174413136
//   zDelta=74288"), so the terrain-only snap dropped the sniper 74,288 units
//   (18.13 m at the engine's 4096 units/m, kUnitsToMeters
//   src/humanbase/human.c:165).  The WaterTower's collision set 1 answers
//   174487421.875 -- 2.125 units under the authored foot -- and the nearest-hit
//   rule keeps THAT, not the terrain.
//
// Host-portable: no GL, no Level, no Renderer.  The corpus half self-skips when
// the retail install is absent, like the other corpus suites.

#include <gtest/gtest.h>

#include "level/collision_world.h"
#include "level/collision_drop_rules.h"
#include "level/terrain_snap_rules.h"
#include "renderer/igi_transform.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// A synthetic collision mesh: 8 corners, 12 triangles, one solid surface.
// ---------------------------------------------------------------------------

igi::CollisionMesh MakeBox(float x0, float y0, float z0, float x1, float y1, float z1,
                           uint16_t portal = igi::kCollisionSolidPortal) {
    igi::CollisionMesh m;
    m.verts = {
        {x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
        {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1},
    };
    const uint16_t quads[6][4] = {
        {0, 3, 2, 1},  // bottom
        {4, 5, 6, 7},  // top
        {0, 1, 5, 4},  // side y0
        {1, 2, 6, 5},  // side x1
        {2, 3, 7, 6},  // side y1
        {3, 0, 4, 7},  // side x0
    };
    for (const auto& q : quads) {
        m.faces.push_back({{q[0], q[1], q[2]}, 0});
        m.faces.push_back({{q[0], q[2], q[3]}, 0});
    }
    m.surface_portal.assign(1, portal);
    m.min[0] = x0; m.min[1] = y0; m.min[2] = z0;
    m.max[0] = x1; m.max[1] = y1; m.max[2] = z1;
    return m;
}

// A single sloped plane z = 0.1*x + 0.2*y over 0 <= x, 0 <= y, x + y <= 100.
igi::CollisionMesh MakeSlope(uint16_t portal = igi::kCollisionSolidPortal) {
    igi::CollisionMesh m;
    m.verts = {{0, 0, 0}, {100, 0, 10}, {0, 100, 20}};
    m.faces = {{{0, 1, 2}, 0}};
    m.surface_portal.assign(1, portal);
    m.min[0] = 0; m.min[1] = 0; m.min[2] = 0;
    m.max[0] = 100; m.max[1] = 100; m.max[2] = 20;
    return m;
}

// Adds one instance of `mesh` at `pos` with the authored Euler triple `rot`.
void AddAt(igi::CollisionWorld& world, const igi::CollisionMesh* mesh, int index,
           double x, double y, double z, double ax = 0.0, double ay = 0.0, double az = 0.0,
           float scale = 1.0f) {
    float matrix[9];
    igi::CollisionMatrixFromEuler(ax, ay, az, matrix);
    const double pos[3] = {x, y, z};
    world.AddInstance(index, mesh, pos, matrix, scale);
}

}  // namespace

// ---------------------------------------------------------------------------
// The transform: the module's own Rz*Ry*Rx must equal the editor's canonical
// igi_transform matrix (source/renderer/igi_transform.h:24-35), which is itself
// the transcription of Matrix_SetEuler HOOK 0x4B38E0 (src/math/matrix.c).
// ---------------------------------------------------------------------------
TEST(CollisionWorld, EulerMatrixMatchesTheEditorTransform) {
    const double angles[][3] = {
        {0.0, 0.0, 0.0},
        {0.0, 0.0, 1.5707963267948966},
        {0.3, -0.7, 2.1},
        {-1.2, 0.4, -0.9},
    };
    for (const auto& a : angles) {
        float row_major[9];
        igi::CollisionMatrixFromEuler(a[0], a[1], a[2], row_major);
        const glm::mat3 g = igi::IgiEulerRotationMat3(glm::dvec3(a[0], a[1], a[2]));
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) {
                // glm is column-major: g[c][r] is row r, column c.
                EXPECT_NEAR(row_major[r * 3 + c], g[c][r], 1e-6)
                    << "row " << r << " col " << c << " for Euler (" << a[0] << ", " << a[1]
                    << ", " << a[2] << ")";
            }
        }
    }
}

// ---------------------------------------------------------------------------
// A box: the top face is hit, a probe from inside reaches the floor, and a
// probe outside the box misses.
// ---------------------------------------------------------------------------
TEST(CollisionWorld, VerticalProbeHitsTheTopFace) {
    igi::CollisionWorld world;
    const igi::CollisionMesh* box = world.InternMesh("box", MakeBox(0, 0, 0, 100, 100, 100));
    ASSERT_NE(box, nullptr);
    AddAt(world, box, 7, 0.0, 0.0, 0.0);

    const igi::CollisionProbeHit hit =
        world.ProbeVertical(50.0, 50.0, 200.0, /*up=*/false, igi::kDropProbeDistance);
    ASSERT_TRUE(hit.found);
    EXPECT_DOUBLE_EQ(hit.z, 100.0);
    EXPECT_EQ(hit.object_index, 7);
    EXPECT_DOUBLE_EQ(hit.distance, 100.0);
}

TEST(CollisionWorld, AProbeFromInsideReachesTheFloorAndTheUpProbeTheCeiling) {
    igi::CollisionWorld world;
    const igi::CollisionMesh* box = world.InternMesh("box", MakeBox(0, 0, 0, 100, 100, 100));
    AddAt(world, box, 0, 0.0, 0.0, 0.0);

    const igi::CollisionProbeHit down =
        world.ProbeVertical(50.0, 50.0, 50.0, /*up=*/false, igi::kDropProbeDistance);
    ASSERT_TRUE(down.found);
    EXPECT_DOUBLE_EQ(down.z, 0.0);

    const igi::CollisionProbeHit up =
        world.ProbeVertical(50.0, 50.0, 50.0, /*up=*/true, igi::kDropProbeDistance);
    ASSERT_TRUE(up.found);
    EXPECT_DOUBLE_EQ(up.z, 100.0);
}

TEST(CollisionWorld, AProbeOutsideTheBoxMisses) {
    igi::CollisionWorld world;
    const igi::CollisionMesh* box = world.InternMesh("box", MakeBox(0, 0, 0, 100, 100, 100));
    AddAt(world, box, 0, 0.0, 0.0, 0.0);
    EXPECT_FALSE(world.ProbeVertical(200.0, 200.0, 200.0, false, igi::kDropProbeDistance).found);
    // ... and so does a column that is inside in x but outside in y.
    EXPECT_FALSE(world.ProbeVertical(50.0, 200.0, 200.0, false, igi::kDropProbeDistance).found);
}

// The range is the engine's |dz| reach: Coll_VerticalHit's
// `if (Coll_Abs(p->z - hit.z) > range) break;` (src/coll/coll.c:706-707).  The
// object segment is bounded by its own endpoints, so a hit beyond it cannot be
// returned at all.
TEST(CollisionWorld, AProbeShorterThanTheGapMisses) {
    igi::CollisionWorld world;
    const igi::CollisionMesh* box = world.InternMesh("box", MakeBox(0, 0, 0, 100, 100, 100));
    AddAt(world, box, 0, 0.0, 0.0, 0.0);
    EXPECT_FALSE(world.ProbeVertical(50.0, 50.0, 1000.0, false, 500.0).found);
    EXPECT_TRUE(world.ProbeVertical(50.0, 50.0, 1000.0, false, 2000.0).found);
}

// ---------------------------------------------------------------------------
// Rotation and translation: the probe is transformed into model space, so a
// sloped plane can be queried with the object turned a quarter turn.
// ---------------------------------------------------------------------------
TEST(CollisionWorld, RotationIsAppliedToTheProbeSegment) {
    igi::CollisionWorld world;
    const igi::CollisionMesh* slope = world.InternMesh("slope", MakeSlope());
    AddAt(world, slope, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.5707963267948966);  // Rz(90)

    // Rz(90) maps model (x, y) to world (-y, x), so world (0, 10) is model (10, 0):
    // the plane there is z = 0.1*10 = 1.
    const igi::CollisionProbeHit hit =
        world.ProbeVertical(0.0, 10.0, 100.0, /*up=*/false, igi::kDropProbeDistance);
    ASSERT_TRUE(hit.found);
    // Float math on a sloped plane (the engine's own precision), so 1.0 +/- eps.
    EXPECT_NEAR(hit.z, 1.0, 1e-4);

    // world (10, 0) is model (0, -10), outside the triangle's y >= 0.
    EXPECT_FALSE(world.ProbeVertical(10.0, 0.0, 100.0, false, igi::kDropProbeDistance).found);
}

TEST(CollisionWorld, PositionAndScaleAreApplied) {
    igi::CollisionWorld world;
    const igi::CollisionMesh* box = world.InternMesh("box", MakeBox(0, 0, 0, 100, 100, 100));
    AddAt(world, box, 0, 1000.0, 2000.0, 50.0, 0.0, 0.0, 0.0, /*scale=*/2.0f);

    // Scaled to 200 units and lifted by 50: the top is at 50 + 200.
    const igi::CollisionProbeHit hit =
        world.ProbeVertical(1100.0, 2100.0, 1000.0, false, igi::kDropProbeDistance);
    ASSERT_TRUE(hit.found);
    EXPECT_DOUBLE_EQ(hit.z, 250.0);
    EXPECT_DOUBLE_EQ(hit.x, 1100.0);
    EXPECT_DOUBLE_EQ(hit.y, 2100.0);
}

// ---------------------------------------------------------------------------
// The CMAT filter: a face whose surface portal is not 0xFFFF is not solid for
// this query (RigidDynCubeObj_LineHit src/qasm/rigidhit.c:255-256, q.allFaces=0
// from src/model/coll.c:495).
// ---------------------------------------------------------------------------
TEST(CollisionWorld, ANonSolidSurfaceIsSkipped) {
    igi::CollisionWorld world;
    const igi::CollisionMesh* slope = world.InternMesh("portal", MakeSlope(/*portal=*/3));
    ASSERT_EQ(slope->SolidFaceCount(), 0u);
    AddAt(world, slope, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    EXPECT_FALSE(world.ProbeVertical(10.0, 10.0, 100.0, false, igi::kDropProbeDistance).found);

    igi::CollisionWorld solid;
    const igi::CollisionMesh* ok = solid.InternMesh("solid", MakeSlope());
    ASSERT_EQ(ok->SolidFaceCount(), 1u);
    AddAt(solid, ok, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    EXPECT_TRUE(solid.ProbeVertical(10.0, 10.0, 100.0, false, igi::kDropProbeDistance).found);
}

// ---------------------------------------------------------------------------
// Nearest of two: the object half keeps the NEAREST hit along the segment
// (RigidDynCubeObj_LineHit's `best`, src/qasm/rigidhit.c:346-360), and the
// terrain/object comparison is the engine's own (coll.c:593-594).
// ---------------------------------------------------------------------------
TEST(CollisionWorld, TheNearestOfTwoInstancesWins) {
    igi::CollisionWorld world;
    const igi::CollisionMesh* low = world.InternMesh("low", MakeBox(0, 0, 0, 100, 100, 50));
    const igi::CollisionMesh* high = world.InternMesh("high", MakeBox(0, 0, 0, 100, 100, 200));
    AddAt(world, low, 1, 0.0, 0.0, 0.0);
    AddAt(world, high, 2, 0.0, 0.0, 0.0);
    const igi::CollisionProbeHit hit =
        world.ProbeVertical(50.0, 50.0, 300.0, false, igi::kDropProbeDistance);
    ASSERT_TRUE(hit.found);
    EXPECT_DOUBLE_EQ(hit.z, 200.0);  // the higher top, nearer to the probe
    EXPECT_EQ(hit.object_index, 2);
}

TEST(CollisionDropRules, TheObjectHitWinsATieAndTheNearerOfTheTwoWinsOtherwise) {
    igi::DropHit object;
    object.found = true;
    object.z = 100.0;
    igi::DropHit terrain;
    terrain.found = true;
    terrain.z = 50.0;

    // The object is nearer: kept (Coll_Drop :593-594).
    EXPECT_DOUBLE_EQ(igi::ChooseNearestDropHit(200.0, object, terrain).z, 100.0);
    // The terrain is nearer: replaces it.
    terrain.z = 150.0;
    EXPECT_DOUBLE_EQ(igi::ChooseNearestDropHit(250.0, object, terrain).z, 150.0);
    // Equal distance: the engine's `!(a < b)` keeps the object.
    terrain.z = 200.0;
    igi::DropHit object_at_100 = object;
    const igi::DropHit tie = igi::ChooseNearestDropHit(150.0, object_at_100, terrain);
    EXPECT_DOUBLE_EQ(tie.z, 100.0);
    // A terrain-only answer is the answer; an object-only answer too.
    igi::DropHit miss;
    EXPECT_TRUE(igi::ChooseNearestDropHit(200.0, miss, terrain).found);
    EXPECT_FALSE(igi::ChooseNearestDropHit(200.0, miss, miss).found);
    EXPECT_TRUE(igi::ChooseNearestDropHit(200.0, object, miss).found);
}

TEST(CollisionDropRules, PinsTheHumanProbeReach) {
    // Human_ProbeGround's kFarDown/kFarUp (src/humanbase/human.c:192-193), used
    // by both calls (:1754, :1768).
    EXPECT_DOUBLE_EQ(igi::kHumanProbeDistance, 4096000.0);
    // Game_DropToGround's own reach, for the non-human drop users (misc.c:82).
    EXPECT_DOUBLE_EQ(igi::kDropProbeDistance, 1638400.0);
}

TEST(CollisionDropRules, TheTerrainCandidateObeysDirectionSlopAndRange) {
    // DOWN accepts a surface at or below the probe + 0.1 (Coll_LeafHit
    // src/coll/coll.c:321,466-468), and rejects one farther than |dz| (:706-707).
    EXPECT_TRUE(igi::TerrainDropCandidate(100.0, 100.05, -1000.0).found);
    EXPECT_FALSE(igi::TerrainDropCandidate(100.0, 100.2, -1000.0).found);
    // The UP probe accepts a surface above, of course, but not one below its
    // slop: 95 is 5 units below the UP probe start.
    EXPECT_TRUE(igi::TerrainDropCandidate(100.0, 100.2, 1000.0).found);
    EXPECT_TRUE(igi::TerrainDropCandidate(100.0, 100.05, 1000.0).found);
    EXPECT_FALSE(igi::TerrainDropCandidate(100.0, 95.0, 1000.0).found);
    EXPECT_TRUE(igi::TerrainDropCandidate(100.0, 105.0, 1000.0).found);
    EXPECT_FALSE(igi::TerrainDropCandidate(100.0, 1300.0, -1000.0).found);
    EXPECT_TRUE(igi::TerrainDropCandidate(100.0, 50.0, -1000.0).found);
}

// The decision consumes the WHOLE query's answer: a human with an object hit
// and no terrain hit still snaps (Coll_Drop's object half alone is a hit).
TEST(CollisionDropRules, AHitFromEitherHalfSnapsAHumanAndAMissMovesNothing) {
    EXPECT_EQ(igi::DecideCollisionSnap(/*hitFound=*/true, /*isHuman=*/true),
              igi::TerrainSnapDecision::SnapToTerrain);
    EXPECT_EQ(igi::DecideCollisionSnap(/*hitFound=*/false, /*isHuman=*/true),
              igi::TerrainSnapDecision::KeepAuthoredZ);
    EXPECT_EQ(igi::DecideCollisionSnap(/*hitFound=*/true, /*isHuman=*/false),
              igi::TerrainSnapDecision::KeepAuthoredZ);
    // The terrain-only entry point keeps its historical meaning.
    EXPECT_EQ(igi::DecideTerrainSnap(false, true), igi::TerrainSnapDecision::KeepAuthoredZ);
    EXPECT_EQ(igi::DecideTerrainSnap(true, true), igi::TerrainSnapDecision::SnapToTerrain);
}

// ---------------------------------------------------------------------------
// Memory: one mesh per MODEL, one small record per placement.  The 200-object
// case the task names is 200 INSTANCES of one mesh, not 200 copies of its
// triangles.
// ---------------------------------------------------------------------------
TEST(CollisionWorld, MeshesAreSharedBetweenInstances) {
    igi::CollisionWorld world;
    igi::CollisionMesh mesh;
    mesh.verts.resize(400);
    mesh.faces.resize(1000);
    for (std::size_t i = 0; i < 1000; ++i) mesh.faces[i] = {{0, 1, 2}, 0};
    mesh.surface_portal.assign(1, igi::kCollisionSolidPortal);
    const igi::CollisionMesh* shared = world.InternMesh("building", std::move(mesh));
    ASSERT_NE(shared, nullptr);

    std::size_t before = world.byte_size();
    for (int i = 0; i < 200; ++i) {
        AddAt(world, shared, i, i * 1000.0, 0.0, 0.0);
    }
    EXPECT_EQ(world.mesh_count(), 1u);
    EXPECT_EQ(world.instance_count(), 200u);
    EXPECT_EQ(world.triangle_count(), 1000u);  // NOT 200,000
    // 200 instances are ~200 * sizeof(CollisionInstance), not 200 * the mesh.
    const std::size_t added = world.byte_size() - before;
    EXPECT_LT(added, 200u * 200u);
    RecordProperty("shared_bytes", std::to_string(world.byte_size()));
    RecordProperty("added_bytes_for_200_instances", std::to_string(added));
}

// ---------------------------------------------------------------------------
// The corpus case: LEVEL1's WaterTower and the sniper the level authors on it.
// ---------------------------------------------------------------------------

namespace {

const char* CorpusRoot() {
    for (const char* name : {"IGI_GAME_PATH", "IGI_MEF_CORPUS"}) {
        const char* p = std::getenv(name);
        if (p != nullptr && *p != '\0') return p;
    }
    return nullptr;
}

bool ReadFile(const fs::path& path, std::vector<uint8_t>& out) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return false;
    const uintmax_t size = fs::file_size(path, ec);
    if (ec || size == 0 || size > (32u << 20)) return false;
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.resize(static_cast<std::size_t>(size));
    f.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(size));
    return static_cast<bool>(f);
}

fs::path WaterTowerMef() {
    const char* root = CorpusRoot();
    if (root == nullptr || *root == '\0') return {};
    const fs::path base = fs::path(root) / "MISSIONS" / "location0" / "level1";
    const fs::path candidates[] = {
        base / "MODELS" / "models" / "435_01_1.mef",
        base / "MODELS" / "LEVEL1_unpacked" / "models" / "435_01_1.mef",
    };
    for (const fs::path& p : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) return p;
    }
    return {};
}

// The shipped placement (objects.qsc:657) and the sniper (objects.qsc:1229).
constexpr double kTowerX = 24658918.0;
constexpr double kTowerY = -55966376.0;
constexpr double kTowerZ = 174413136.0;
constexpr double kTowerYaw = 3.1415929794311523;
constexpr double kSniperX = 24658674.0;
constexpr double kSniperY = -55980796.0;
constexpr double kSniperZ = 174487424.0;
// The editor's own terrain answer at (kSniperX, kSniperY), measured before the
// object half existed: igi1ed.log:237
//   "Kept human 001_01_1 ... authoredZ=174487424.000000 terrainZ=174413136.000000
//    zDelta=74288.000000"
constexpr double kTerrainZ = 174413136.0;

}  // namespace

TEST(CollisionWorldCorpus, WaterTowerSetOnePutsTheSniperOnThePlatform) {
    const fs::path mef = WaterTowerMef();
    if (mef.empty()) {
        GTEST_SKIP() << "retail LEVEL1 435_01_1.mef not found (set IGI_GAME_PATH)";
    }
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(ReadFile(mef, bytes)) << mef;

    igi::CollisionMesh set1;
    ASSERT_TRUE(igi::ParseCollisionMeshSet(bytes.data(), bytes.size(), 1, set1));
    ASSERT_FALSE(set1.empty());
    // Measured on the shipped file: XTVC 2992/16 = 187 vertices, ECFC 2736/8 =
    // 342 triangles, TAMC 12 bytes = one surface with portal 0xFFFF.
    EXPECT_EQ(set1.verts.size(), 187u);
    EXPECT_EQ(set1.faces.size(), 342u);
    EXPECT_EQ(set1.surface_portal.size(), 1u);
    EXPECT_EQ(set1.SolidFaceCount(), 342u);

    igi::CollisionMesh set0;
    ASSERT_TRUE(igi::ParseCollisionMeshSet(bytes.data(), bytes.size(), 0, set0));
    ASSERT_FALSE(set0.empty());
    // Set 0 is the multi-material bullet/LOS mesh (446 triangles, 9 surfaces).
    EXPECT_EQ(set0.faces.size(), 446u);
    // 9 CMAT surfaces in set 0 vs ONE in set 1: the coarse walkable set.
    EXPECT_EQ(set0.surface_portal.size(), 9u);

    igi::CollisionWorld world;
    const igi::CollisionMesh* mesh = world.InternMesh("435_01_1", std::move(set1));
    ASSERT_NE(mesh, nullptr);
    AddAt(world, mesh, 0, kTowerX, kTowerY, kTowerZ, 0.0, 0.0, kTowerYaw);

    const igi::CollisionProbeHit object =
        world.ProbeVertical(kSniperX, kSniperY, kSniperZ, /*up=*/false, igi::kDropProbeDistance);
    ASSERT_TRUE(object.found);
    // The platform's set-1 top, measured by the transform above.
    EXPECT_NEAR(object.z, 174487421.875, 0.05);
    EXPECT_EQ(object.object_index, 0);

    // The drop the editor must perform: the object hit is 2.1 units under the
    // authored foot, the terrain hit is 74,288 units under it.
    igi::DropHit object_hit;
    object_hit.found = true;
    object_hit.z = object.z;
    igi::DropHit terrain_hit = igi::TerrainDropCandidate(kSniperZ, kTerrainZ, -igi::kDropProbeDistance);
    ASSERT_TRUE(terrain_hit.found);
    const igi::DropHit chosen = igi::ChooseNearestDropHit(kSniperZ, object_hit, terrain_hit);
    ASSERT_TRUE(chosen.found);
    EXPECT_DOUBLE_EQ(chosen.z, object.z);

    // BEFORE (terrain-only, the old behaviour): 174413136 -- the sniper dropped
    // 18.13 m.  AFTER: 174487421.875, 2.125 units under the authored foot.
    const double before = kTerrainZ;
    const double after = chosen.z;
    const double drop_units = before <= kSniperZ ? kSniperZ - before : before - kSniperZ;
    EXPECT_NEAR(drop_units, 74288.0, 0.001);
    EXPECT_NEAR(after - before, 74285.875, 0.05);
    // 4096 engine units per metre (kUnitsToMeters, src/humanbase/human.c:165).
    RecordProperty("terrain_only_before_z", std::to_string(before));
    RecordProperty("collision_after_z", std::to_string(after));
    RecordProperty("authored_z", std::to_string(kSniperZ));
    RecordProperty("old_drop_metres", std::to_string(drop_units / 4096.0));
    RecordProperty("world_bytes", std::to_string(world.byte_size()));
}

// The same world on a point OUTSIDE the tower: the terrain answer is the only
// one, so nothing changed for ordinary ground placements.
TEST(CollisionWorldCorpus, OffTheTowerTheTerrainAnswerIsUnchanged) {
    const fs::path mef = WaterTowerMef();
    if (mef.empty()) {
        GTEST_SKIP() << "retail LEVEL1 435_01_1.mef not found (set IGI_GAME_PATH)";
    }
    std::vector<uint8_t> bytes;
    ASSERT_TRUE(ReadFile(mef, bytes)) << mef;
    igi::CollisionMesh set1;
    ASSERT_TRUE(igi::ParseCollisionMeshSet(bytes.data(), bytes.size(), 1, set1));
    igi::CollisionWorld world;
    const igi::CollisionMesh* mesh = world.InternMesh("435_01_1", std::move(set1));
    ASSERT_NE(mesh, nullptr);
    AddAt(world, mesh, 0, kTowerX, kTowerY, kTowerZ, 0.0, 0.0, kTowerYaw);

    const double x = kSniperX + 400000.0;  // ~97 m east of the tower
    EXPECT_FALSE(world.ProbeVertical(x, kSniperY, kSniperZ, false, igi::kDropProbeDistance).found);

    igi::DropHit terrain_hit = igi::TerrainDropCandidate(kSniperZ, kTerrainZ, -igi::kDropProbeDistance);
    ASSERT_TRUE(terrain_hit.found);
    igi::DropHit no_object;
    const igi::DropHit chosen = igi::ChooseNearestDropHit(kSniperZ, no_object, terrain_hit);
    ASSERT_TRUE(chosen.found);
    EXPECT_DOUBLE_EQ(chosen.z, kTerrainZ);
}

// ---------------------------------------------------------------------------
// The WHOLE level-1 building set, as a measured memory/cost case.
//
// The 26 `Building` placements of the shipped LEVEL1 (objects.qsc:603-...):
// 25 of them resolve a set-1 collision mesh, 13 distinct models (410_01_1 is
// placed 4 times, 418_01_1 5 times, 412_01_1 and 439_01_1 3 and 2 times, ...).
// 472_01_1 ships with no set-1 collision, so the engine has none for it either.
// Measured on the retail install: 1,720 distinct triangles, 13 meshes, ~30 KB.
// This is the answer to "200 objects x 1000 triangles": the triangles are per
// MODEL, not per placement.
// ---------------------------------------------------------------------------
namespace {

struct BuildingPlacement {
    const char* model;
    double x, y, z;
    double yaw;
};

const BuildingPlacement kLevel1Buildings[] = {
    {"438_01_1", 24316832.0, -55962088.0, 174413136.0, 0.0},
    {"438_01_1", 24316856.0, -56158704.0, 174413136.0, 0.0},
    {"438_01_1", 24316848.0, -56224228.0, 174413136.0, 0.0},
    {"438_01_1", 24316850.0, -56027612.0, 174413136.0, 0.0},
    {"400_20_1", 24510124.0, -55925804.0, 174413136.0, 3.141592025756836},
    {"435_01_1", 24658918.0, -55966376.0, 174413136.0, 3.1415929794311523},
    {"405_01_1", 24448182.0, -56243864.0, 174413136.0, 0.0},
    {"410_01_1", 24590148.0, -56231896.0, 174413136.0, 1.570796012878418},
    {"410_01_1", 24689824.0, -56330996.0, 174413136.0, 3.1415929794311523},
    {"439_01_1", 24400124.0, -55959316.0, 174413136.0, 3.1415929794311523},
    {"439_01_1", 24400096.0, -55992080.0, 174413136.0, 3.1415929794311523},
    {"425_01_1", 24395832.0, -56232516.0, 174413136.0, 0.0},
    {"418_01_1", 24410330.0, -56065180.0, 174413136.0, 1.570796012878418},
    {"418_01_1", 24410252.0, -56129860.0, 174413136.0, 1.570796012878418},
    {"418_01_1", 24939494.0, -56262652.0, 174413136.0, 4.71238899230957},
    {"418_01_1", 24939486.0, -56308872.0, 174413136.0, 4.71238899230957},
    {"412_01_1", 24330570.0, -56373844.0, 174413120.0, 4.71238899230957},
    {"409_01_1", 24804232.0, -56300024.0, 174413136.0, 4.71238899230957},
    {"410_01_1", 24932566.0, -56062908.0, 174413136.0, 0.0},
    {"417_01_1", 24366322.0, -55776688.0, 174413136.0, 0.0},
    {"419_01_1", 24869976.0, -56002460.0, 174413120.0, 4.7123870849609375},
    {"412_01_1", 24425072.0, -56442564.0, 174413120.0, 0.0},
    {"412_01_1", 24525738.0, -56251940.0, 174413120.0, 3.1415929794311523},
    {"418_01_1", 24521546.0, -56192172.0, 174413136.0, 3.1415929794311523},
    {"404_01_1", 24717730.0, -56088708.0, 174413136.0, 3.1415929794311523},
    {"472_01_1", 24077060.0, -55683660.0, 161158576.0, 0.0},
};

fs::path LevelOneModelMef(const char* model) {
    const char* root = CorpusRoot();
    if (root == nullptr || *root == '\0') return {};
    const fs::path base = fs::path(root) / "MISSIONS" / "location0" / "level1";
    const fs::path candidates[] = {
        base / "MODELS" / "models" / (std::string(model) + ".mef"),
        base / "MODELS" / "LEVEL1_unpacked" / "models" / (std::string(model) + ".mef"),
    };
    for (const fs::path& p : candidates) {
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) return p;
    }
    return {};
}

}  // namespace

TEST(CollisionWorldCorpus, TheWholeLevelOneBuildingSetCostsAboutThirtyKilobytes) {
    igi::CollisionWorld world;
    std::unordered_map<std::string, const igi::CollisionMesh*> meshes;
    std::vector<std::string> missing;
    std::vector<uint8_t> bytes;
    for (const BuildingPlacement& p : kLevel1Buildings) {
        const igi::CollisionMesh* mesh = nullptr;
        auto it = meshes.find(p.model);
        if (it != meshes.end()) {
            mesh = it->second;
        } else {
            const fs::path mef = LevelOneModelMef(p.model);
            if (mef.empty()) {
                // 472_01_1 ships only inside LEVEL1.RES (no loose .mef on the
                // retail install) and has no set-1 collision: it contributes
                // nothing to the human collision world, so its absence from the
                // loose corpus is not a reason to skip the measurement.
                if (std::string(p.model) != "472_01_1") missing.push_back(p.model);
                meshes.emplace(p.model, nullptr);
                continue;
            }
            igi::CollisionMesh parsed;
            ASSERT_TRUE(ReadFile(mef, bytes)) << mef;
            ASSERT_TRUE(igi::ParseCollisionMeshSet(bytes.data(), bytes.size(), 1, parsed)) << mef;
            // 472_01_1 has no set-1 collision: an empty part, not an error.
            mesh = parsed.empty() ? nullptr : world.InternMesh(p.model, std::move(parsed));
            meshes.emplace(p.model, mesh);
        }
        if (mesh == nullptr) continue;
        float matrix[9];
        igi::CollisionMatrixFromEuler(0.0, 0.0, p.yaw, matrix);
        const double pos[3] = {p.x, p.y, p.z};
        world.AddInstance(0, mesh, pos, matrix, 1.0f);
    }
    if (!missing.empty()) {
        GTEST_SKIP() << "retail LEVEL1 models not found: " << missing.front()
                     << " (set IGI_GAME_PATH)";
    }
    EXPECT_EQ(world.instance_count(), 25u);   // 472_01_1 has no set-1 collision
    EXPECT_EQ(world.mesh_count(), 13u);       // 410_01_1 x4, 418_01_1 x5, ...
    EXPECT_EQ(world.triangle_count(), 1720u); // distinct triangles, NOT x25
    EXPECT_LT(world.byte_size(), 64u * 1024u);
    RecordProperty("building_instances", std::to_string(world.instance_count()));
    RecordProperty("distinct_meshes", std::to_string(world.mesh_count()));
    RecordProperty("distinct_triangles", std::to_string(world.triangle_count()));
    RecordProperty("world_bytes", std::to_string(world.byte_size()));
}
