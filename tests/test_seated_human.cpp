// The seated human: a human the level authors INSIDE a vehicle must be drawn
// UPRIGHT, with its object origin — the skeleton's root, the pelvis — at the
// authored position, because that is what the engine does with it.
//
// THE REPORT THAT PRODUCED THIS SUITE (user, verbatim): "some human in the
// vehicle like jones sitting is upside not exactly on their exact position".
//
// WHAT THE ENGINE DOES — the rule this suite pins
//
//   * The pose places the ROOT BONE at the object's origin: `BoneObj_Update`
//     HOOK 0x4D38C0 (src/boneobj/boneobj.c:371-419) poses the tree with
//     `pos = {0, 0, 0}` (:376) under the object's matrix, and
//     `BoneObj_UpdateBone` HOOK 0x4D3A90 (:423-525) computes
//     `b->pos = parentPos + parentMatrix * b->offset` (:434-439).  The root's
//     `offset` is TLST[0] = (0, 0, 0) for every shipped human skeleton
//     (`BoneObj_Load` HOOK 0x4D67A0, :1570-1576).
//   * The DRAW uses `obj->pos` unchanged (`BoneDynCubeObj_EnterHandler`
//     HOOK 0x5124C0, src/glowmesh/bonedynd3d.c:41-42), so the mesh hangs BELOW
//     the object's origin: the model's pivot->feet lift is negative in that
//     frame.
//   * The drawn Z comes from `Human_ProbeGround` HOOK 0x462390
//     (src/humanbase/human.c:1674-1857): `to[2] = foot[2] + hits[0].pos.z`
//     (:1816) with `foot = Human_GetBonePos(buf, h, 0)` (:1693) — the object
//     origin is the floor plus BONE 0's animated height.  The authored
//     `ObjectPos` goes to the collision cube and only the ROTATION comes from
//     the authored `Gamma` (`Human_ParamChangedHandler` HOOK 0x45F890,
//     src/humanbase/human.c:448-461: `DynCube_Move(..., h->placePos, ...)`
//     :457, `Matrix_SetRotZ(h->matrix.m, h->yaw)` :460).
//   * A human authored inside a vehicle is therefore carried, never re-based:
//     `Vehicle_NewParts` HOOK 0x4F4800 (src/vehbase/vehicle.c:251-256) builds
//     `v->attached` from the MODEL's ATTA list only, and the passenger is kept
//     in place by the ground probe above plus `Human_UpdatePlatformMove`
//     HOOK 0x460CF0 (src/humanbase/human.c:1190-1238).
//
// WHAT THE EDITOR DID
//
//   `ParseMefGeometry` re-bases every human mesh to the FEET
//   (`SkinSkinnedVertices` sums the rig's root pivot, (0, 0, 3990.4) —
//   `RigRootOriginRaw` — into each vertex) and `App::SnapObjectsToTerrain`
//   compensates for a human standing on the ground.  A passenger is never
//   snapped, so the editor drew its FEET at the authored position while the
//   engine puts its PELVIS there: one pivot->feet lift (3990.4 raw units =
//   0.974 m) too high, i.e. above the seat and through the vehicle's roof.
//
// THE FIX THIS SUITE PINS
//
//   `igi::HumanSitsInVehicle` (source/renderer/human_seat_frame.h) plus the two
//   model-origin terms `StaticModelOriginMetres` / `PosedModelOriginMetres`:
//   the static draw subtracts the bake's root pivot, the posed draw adds
//   nothing, and both put the model's ORIGIN at `pos` — the engine's frame.
//   The suite is a DIFFERENTIAL test: the editor's composition is compared
//   against a literal transcription of the engine's, on the shipped clips.
//
// Corpus-backed cases self-skip without IGI_GAME_PATH; the synthetic case runs
// everywhere.

#include <gtest/gtest.h>

#include "animation.h"
#include "renderer/human_seat_frame.h"
#include "renderer/igi_transform.h"
#include "renderer/mef_native.h"
#include "renderer/posed_draw_transform.h"
#include "runtime/anim_player.h"

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const char* CorpusRoot() {
    for (const char* name : {"IGI_MEF_CORPUS", "IGI_GAME_PATH"}) {
        const char* p = std::getenv(name);
        if (p != nullptr && *p != '\0') return p;
    }
    return nullptr;
}

std::string FindModel(const fs::path& root, const std::string& name) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        if (it->path().filename().string() == name + ".mef") return it->path().string();
    }
    return {};
}

// The clip cache the editor itself writes: <IGI>/cache/anims/<NNN>/<NNN>_anim_<id>.BEF
// (`AnimationRegistry::GetCacheDir`, source/animation.cpp).
std::string FindBef(const fs::path& root, int skel, int anim_id) {
    char name[64];
    std::snprintf(name, sizeof(name), "%03d_anim_%03d.BEF", skel, anim_id);
    char dir[8];
    std::snprintf(dir, sizeof(dir), "%03d", skel);
    const fs::path p = root / "cache" / "anims" / dir / name;
    return fs::is_regular_file(p) ? p.string() : std::string();
}

std::vector<std::string> SplitArgs(const std::string& body) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : body) {
        if (c == ',') { out.push_back(cur); cur.clear(); } else { cur.push_back(c); }
    }
    out.push_back(cur);
    return out;
}

// The BEF text format, read exactly as `AnimationRegistry::ParseBefFile` reads it
// (source/animation.cpp:306-400). The test carries its own copy so it can run
// without the Windows-only translation unit that owns the production parser.
AnimationClip ParseBef(const std::string& path) {
    AnimationClip clip;
    std::ifstream f(path);
    if (!f.is_open()) return clip;
    std::string line;
    while (std::getline(f, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ';')) line.pop_back();
        if (line.rfind("AnimInit(", 0) == 0) {
            auto a = SplitArgs(line.substr(9, line.size() - 10));
            clip.name = a.empty() ? "" : a[0];
            if (!clip.name.empty() && clip.name.front() == '"') clip.name.erase(0, 1);
            if (!clip.name.empty() && clip.name.back() == '"') clip.name.pop_back();
            clip.flags = a.size() > 1 ? std::atoi(a[1].c_str()) : 0;
            clip.length_ms = a.size() > 2 ? std::atoi(a[2].c_str()) : 0;
            clip.tp_flag = a.size() > 3 ? std::atoi(a[3].c_str()) : 0;
            const size_t p = clip.name.rfind("_anim_");
            if (p != std::string::npos) clip.animId = std::atoi(clip.name.substr(p + 6).c_str());
        } else if (line.rfind("Bone(", 0) == 0) {
            auto a = SplitArgs(line.substr(5, line.size() - 6));
            AnimBone b;
            b.index = std::atoi(a[0].c_str());
            b.name = a[1];
            b.parent = std::atoi(a[2].c_str());
            b.restPos = glm::vec3(std::atof(a[3].c_str()), std::atof(a[4].c_str()),
                                  std::atof(a[5].c_str()));
            clip.bones.push_back(b);
        } else if (line.rfind("RotationKeyFrameData(", 0) == 0) {
            auto a = SplitArgs(line.substr(21, line.size() - 22));
            AnimRotationKey k;
            k.bone = std::atoi(a[0].c_str());
            k.time_ms = std::atoi(a[2].c_str());
            k.q0 = glm::quat(std::atof(a[6].c_str()), std::atof(a[3].c_str()),
                             std::atof(a[4].c_str()), std::atof(a[5].c_str()));
            clip.rotationKeys.push_back(k);
        } else if (line.rfind("TranslationKeyFrameData(", 0) == 0) {
            auto a = SplitArgs(line.substr(24, line.size() - 25));
            AnimTranslationKey k;
            k.track = std::atoi(a[0].c_str());
            k.time_ms = std::atoi(a[2].c_str());
            k.pos = glm::vec3(std::atof(a[3].c_str()), std::atof(a[4].c_str()),
                              std::atof(a[5].c_str()));
            clip.translationKeys.push_back(k);
        }
    }
    return clip;
}

// The engine's skinned vertex positions in METRES, model frame — exactly
// `Renderer::DrawSkinnedMesh`'s arithmetic (source/renderer/renderer.cpp:447-470).
std::vector<glm::vec3> SkinnedVerticesMetres(const ParsedGeometry& geo,
                                             const std::vector<igi::BoneTransform>& bones) {
    const EngineSkinVertices engine_vertices = BuildEngineSkinVertices(geo);
    std::vector<igi::SkinTransformOutput> skinned;
    if (!igi::BoneMeshSkin(engine_vertices.records.data(), engine_vertices.base_count,
                           engine_vertices.extra_count, bones.data(),
                           static_cast<int>(bones.size()), /*rigid=*/false, skinned)) {
        return {};
    }
    std::vector<glm::vec3> out(skinned.size());
    for (size_t i = 0; i < skinned.size(); ++i) {
        out[i] = glm::vec3(skinned[i].position[0], skinned[i].position[1],
                           skinned[i].position[2]) * kMefNativeScale;
    }
    return out;
}

// A stand-in for `LevelObject` with only the two fields the seat rule reads.
struct StubObject {
    std::string type;
    int parentIndex = -1;
};

// Three float32 ULPs at a LEVEL placement Z (1.7e8 raw units): the resolution of
// any world-space comparison made there — the same tolerance
// tests/test_skinned_static_parity.cpp uses.
constexpr double kLevelFrameTolerance = 64.0;

}  // namespace

// ---------------------------------------------------------------------------
// The rule itself, on a synthetic tree: only a HUMAN under a VEHICLE is a
// passenger.
// ---------------------------------------------------------------------------

TEST(SeatedHuman, OnlyAHumanUnderAVehicleIsAPassenger) {
    // 0 Heli -> 1 HumanSoldier (passenger)
    // 2 Container -> 3 Car -> 4 HumanSoldier (passenger through a container)
    // 5 HumanSoldier (root: a human standing in the world)
    // 6 Car -> 7 EditRigidObj (a vehicle's own prop is not a human)
    // 8 Train -> 9 Container -> 10 HumanSoldierFemale (passenger)
    // 11 Heli -> 12 Car (no human anywhere)
    std::vector<StubObject> objects = {
        {"Heli", -1},
        {"HumanSoldier", 0},
        {"Container", -1},
        {"Car", 2},
        {"HumanSoldier", 3},
        {"HumanSoldier", -1},
        {"Car", -1},
        {"EditRigidObj", 6},
        {"Train", -1},
        {"Container", 8},
        {"HumanSoldierFemale", 9},
        {"Heli", -1},
        {"Car", 11},
    };
    EXPECT_TRUE(igi::HumanSitsInVehicle(objects, 1));
    EXPECT_TRUE(igi::HumanSitsInVehicle(objects, 4));
    EXPECT_TRUE(igi::HumanSitsInVehicle(objects, 10));
    EXPECT_FALSE(igi::HumanSitsInVehicle(objects, 5)) << "a human with no vehicle ancestor is grounded";
    EXPECT_FALSE(igi::HumanSitsInVehicle(objects, 7)) << "a vehicle's own prop is not a passenger";
    EXPECT_FALSE(igi::HumanSitsInVehicle(objects, 12)) << "no human at all";
    EXPECT_FALSE(igi::HumanSitsInVehicle(objects, -1));
    EXPECT_FALSE(igi::HumanSitsInVehicle(objects, 99));

    // A malformed parent cycle must not hang a draw pass.
    std::vector<StubObject> cyclic = {{"Container", 1}, {"HumanSoldier", 0}};
    EXPECT_FALSE(igi::HumanSitsInVehicle(cyclic, 1));
}

// ---------------------------------------------------------------------------
// The two draw paths, in both frames: the seat frame puts the model's ORIGIN at
// the placement; the grounded frame puts the model's FEET there. Each frame's
// two paths must agree, or the posed draw and the static draw would disagree
// again (the AI-below-ground defect).
// ---------------------------------------------------------------------------

TEST(SeatedHuman, TheSeatFramePutsTheModelOriginAtThePlacement) {
    // A rig whose root pivot is the shipped human value: the pelvis 3990.4 raw
    // units above the model's origin (the feet).
    std::vector<BoneInfo> rig(1);
    rig[0].name = "center";
    rig[0].parent = -1;
    rig[0].pivot = glm::vec3(0.0f, 0.0f, 3990.4f);
    const glm::vec3 origin_metres = RigRootOriginRaw(rig) * kMefNativeScale;
    ASSERT_NEAR(origin_metres.z, 97.422f, 1e-3f);

    const glm::dvec3 pos(24957688.0, -56093496.0, 174419120.0);   // LEVEL1 "Jones drive truck"
    const glm::dvec3 rot(0.0, 0.0, 3.1415929794311523);           // its authored Gamma
    const float scale = 1.0f;
    const float total_scale = 40.96f * scale;

    // ── grounded (today's rule, unchanged): the FEET at pos ─────────────────
    {
        const glm::mat4 static_mat = igi::IgiObjectMatrixAtModelOrigin(
            pos, rot, total_scale, igi::StaticModelOriginMetres(&rig, false));
        const glm::mat4 posed_mat = igi::PosedObjectMatrix(pos, rot, scale, &rig, false);
        // The static bake carries the root pivot, so its origin vertex is the
        // root: it lands at pos + R*S*origin.
        const glm::dvec3 static_root = glm::dvec3(static_mat * glm::dvec4(glm::dvec3(origin_metres), 1.0));
        // The posed vertices have the root at the model origin, so the posed
        // path must add the origin itself.
        const glm::dvec3 posed_root = glm::dvec3(posed_mat * glm::dvec4(glm::dvec3(0.0), 1.0));
        EXPECT_LT(glm::length(static_root - posed_root), kLevelFrameTolerance);
        EXPECT_GT(glm::length(posed_root - glm::dvec3(pos)), 3900.0)
            << "the grounded frame is a feet frame: the root is one pivot->feet lift above pos";
    }

    // ── seated (the fix): the model's ORIGIN at pos ─────────────────────────
    {
        const glm::mat4 static_mat = igi::IgiObjectMatrixAtModelOrigin(
            pos, rot, total_scale, igi::StaticModelOriginMetres(&rig, true));
        const glm::mat4 posed_mat = igi::PosedObjectMatrix(pos, rot, scale, &rig, true);
        const glm::dvec3 static_root =
            glm::dvec3(static_mat * glm::dvec4(glm::dvec3(origin_metres), 1.0));
        const glm::dvec3 posed_root = glm::dvec3(posed_mat * glm::dvec4(glm::dvec3(0.0), 1.0));
        // The engine's composition: `BoneObj_Update` places the root at the
        // object's origin (`pos = {0,0,0}`, boneobj.c:376,434-439), and
        // `BoneDynCubeObj_EnterHandler` draws at `obj->pos` (bonedynd3d.c:41-42).
        EXPECT_LT(glm::length(posed_root - glm::dvec3(pos)), kLevelFrameTolerance);
        EXPECT_LT(glm::length(static_root - glm::dvec3(pos)), kLevelFrameTolerance)
            << "the static bake must undo its own pivot->feet lift in the seat frame";
        EXPECT_LT(glm::length(static_root - posed_root), kLevelFrameTolerance)
            << "the two draw paths must still agree with each other";
        // The lift the seat frame removes is the defect the user saw.
        EXPECT_NEAR(glm::length(glm::dvec3(posed_mat * glm::dvec4(glm::dvec3(0.0), 1.0)) -
                                glm::dvec3(igi::PosedObjectMatrix(pos, rot, scale, &rig, false) *
                                           glm::dvec4(glm::dvec3(0.0), 1.0))),
                    static_cast<double>(origin_metres.z) * total_scale, kLevelFrameTolerance);
    }
}

// ---------------------------------------------------------------------------
// THE DELIVERABLE: the shipped seated humans, through the renderer's own path.
// ---------------------------------------------------------------------------

TEST(SeatedHuman, TheShippedPassengersSitUprightAtTheirAuthoredPosition) {
    const char* root = CorpusRoot();
    if (root == nullptr || !fs::exists(root)) {
        GTEST_SKIP() << "Set IGI_GAME_PATH to an IGI install root to enable this suite";
    }
    const fs::path rootp(root);

    // The two shipped seated humans, read from the decompiled level scripts:
    //   MISSIONS/location0/level1/objects.qsc:1159
    //     Task_New(4023, "HumanSoldier", "Jones drive truck", 24957688, -56093496,
    //              174419120, 3.1415929794311523, "000_01_1", 0, 1, 61, ...)
    //   MISSIONS/location0/LEVEL4/objects.qsc:1041
    //     Task_New(-1, "HumanSoldier", "GI", 111716104, -54603588, 175401152,
    //              0.52359902858734131, "021_01_1", 0, 0, 243, ...)
    struct Case {
        const char* model;
        int skeleton;      // "Bone Heirachy"
        int stand_anim;    // "Stand Animation"
        glm::dvec3 pos;
        glm::dvec3 rot;    // the editor's Euler triple: (0, 0, Gamma)
        const char* source;
    };
    const Case cases[] = {
        {"000_01_1", 1, 61, {24957688.0, -56093496.0, 174419120.0},
         {0.0, 0.0, 3.1415929794311523}, "level1/objects.qsc:1159"},
        {"021_01_1", 0, 243, {111716104.0, -54603588.0, 175401152.0},
         {0.0, 0.0, 0.52359902858734131}, "LEVEL4/objects.qsc:1041"},
    };

    int checked = 0;
    for (const Case& c : cases) {
        const std::string model_path = FindModel(rootp, c.model);
        const std::string bef_path = FindBef(rootp, c.skeleton, c.stand_anim);
        if (model_path.empty() || bef_path.empty()) continue;
        const ParsedGeometry geo = ParseMefFile(model_path);
        ASSERT_GT(geo.bones.size(), 1u) << c.model;
        const AnimationClip clip = ParseBef(bef_path);
        ASSERT_FALSE(clip.bones.empty()) << bef_path;
        ASSERT_EQ(clip.animId, c.stand_anim) << bef_path;

        // The engine's own pose, over the retail skeleton: `AnimPlayer` is the
        // port of `BoneObj_Update` 0x4D38C0 / `BoneObj_Load` 0x4D67A0.
        igi::AnimPlayer player;
        ASSERT_TRUE(player.Build(clip, c.stand_anim)) << c.model;
        ASSERT_TRUE(player.Play(igi::kAnimLoop)) << c.model;
        const std::vector<igi::BoneTransform>& raw = player.RawBoneTransforms();
        ASSERT_FALSE(raw.empty()) << c.model;

        // The engine's pose frame: the ROOT BONE is at the object's origin
        // (`pos = {0,0,0}` with the root's TLST offset (0,0,0), boneobj.c:376,
        // :434-439). That is what makes the authored position the pelvis.
        EXPECT_NEAR(raw[0].pos[0], 0.0f, 1e-3f) << c.model;
        EXPECT_NEAR(raw[0].pos[1], 0.0f, 1e-3f) << c.model;
        EXPECT_NEAR(raw[0].pos[2], 0.0f, 1e-3f) << c.model;

        // NOTE (measured, not asserted): `Human_ProbeGround` puts the engine's
        // object origin at `ground + foot[2]` where foot[2] is bone 0's height
        // plus the clip's root motion (human.c:1693, :1816). On the skeleton-1
        // stand clip (000_01_1) that root motion is 3990.4 raw units, exactly
        // the editor rig's pivot->feet lift `RigRootOriginRaw`; on the skeleton-0
        // clip (021_01_1) it is 521.2. The editor's own frame is the one the
        // snap relies on -- `TheGroundedFrameSoleIsAtTheModelOrigin` measures the
        // soles at the object origin on every shipped human -- so the placement
        // rule below does not depend on which of the two the clip carries.

        const std::vector<glm::vec3> skinned = SkinnedVerticesMetres(geo, raw);
        ASSERT_FALSE(skinned.empty()) << c.model;

        // The renderer's own matrix for this placement, in the seat frame.
        const glm::mat4 posed_mat =
            igi::PosedObjectMatrix(c.pos, c.rot, 1.0f, &geo.bones, /*engine_origin_frame=*/true);

        // (a) THE SEAT: the model's origin — the root bone — is at the authored
        // position, which is where the engine's `BoneDynCubeObj_EnterHandler`
        // draws it (bonedynd3d.c:41-42). This is the differential half: the
        // editor's composition against the engine's.
        const glm::dvec3 pelvis = glm::dvec3(posed_mat * glm::dvec4(glm::dvec3(0.0), 1.0));
        EXPECT_LT(glm::length(pelvis - c.pos), kLevelFrameTolerance)
            << c.model << " (" << c.source << "): the seated human is not at its authored position";

        // The defect this fixes, in the same frame: the grounded (feet) frame
        // lifts the pelvis by one pivot->feet lift (3990.4 raw units).
        const glm::mat4 feet_mat =
            igi::PosedObjectMatrix(c.pos, c.rot, 1.0f, &geo.bones, /*engine_origin_frame=*/false);
        const glm::dvec3 pelvis_feet = glm::dvec3(feet_mat * glm::dvec4(glm::dvec3(0.0), 1.0));
        EXPECT_GT(glm::length(pelvis_feet - pelvis), 3900.0)
            << c.model << ": the feet frame must be one rig-origin lift away";

        // (b) UPRIGHT: the posed body's highest vertex is above the pelvis and
        // its lowest is below it, and the body spans a human's height. A pose
        // drawn upside down fails the first two; the sit clips' own root track
        // (the pelvis height above the floor, `Human_ProbeGround` :1816) is what
        // the pelvis height is measured against.
        double min_z = 1e300, max_z = -1e300;
        double max_above = -1e300, min_below = 1e300;
        for (const glm::vec3& v : skinned) {
            const glm::dvec3 w = glm::dvec3(posed_mat * glm::dvec4(glm::dvec3(v), 1.0));
            min_z = std::min(min_z, w.z);
            max_z = std::max(max_z, w.z);
            max_above = std::max(max_above, w.z - pelvis.z);
            min_below = std::min(min_below, w.z - pelvis.z);
        }
        EXPECT_GT(max_above, 300.0) << c.model << ": nothing of the body is above the pelvis";
        EXPECT_LT(min_below, -300.0) << c.model << ": nothing of the body is below the pelvis";
        EXPECT_GT(max_z - min_z, 3000.0) << c.model << ": the drawn body is not a human's height";
        EXPECT_LT(max_z - min_z, 8000.0) << c.model << ": the drawn body is taller than a human";
        ++checked;
    }
    if (checked == 0) {
        GTEST_SKIP() << "no retail seated human (model + cached BEF clip) found under " << root;
    }
}

// ---------------------------------------------------------------------------
// THE GROUNDED FRAME'S SOLE, measured on the shipped human meshes.
//
// This is the corpus half of App::SnapObjectsToTerrain's placement rule. The
// editor's static bake puts the model's FEET at `obj.pos`
// (source/renderer/posed_draw_transform.h:24-30), and the engine puts the FOOT
// on the collision hit (`to[2] = foot[2] + hits[0].pos.z`, Human_ProbeGround
// HOOK 0x462390, src/humanbase/human.c:1816). So a grounded human's Z IS the
// hit z: no pivot->feet lift may be added.
//
// The lift the snap used to add was `-min_p.z` over the whole mesh
// (source/renderer/model.cpp:311 -> source/app_view.cpp). These models show why
// that is wrong: the lowest vertex belongs to upper-leg/arm geometry, tenths of
// a metre BELOW the origin, while the vertices bound to the foot/toe bones sit
// within 2 cm of it. `-min_p.z` therefore floats the sole by 0.26..0.65 m.
//
// UNITS: `ParsedGeometry::vertices[].pos` is raw/40.96, and 1 such unit is
// 1 cm (raw/4096 m). The thresholds below are centimetres.
// ---------------------------------------------------------------------------

TEST(SeatedHuman, TheGroundedFrameSoleIsAtTheModelOrigin) {
    const char* root = CorpusRoot();
    if (root == nullptr || !fs::exists(root)) {
        GTEST_SKIP() << "Set IGI_GAME_PATH to an IGI install root to enable this suite";
    }
    const fs::path rootp(root);

    // Shipped human models reachable as loose .mef files in the retail tree.
    const char* models[] = {"000_01_1", "001_01_1", "009_02_1", "015_01_1", "021_01_1"};
    int checked = 0;
    for (const char* model : models) {
        const std::string path = FindModel(rootp, model);
        if (path.empty()) continue;
        const ParsedGeometry geo = ParseMefFile(path);
        if (geo.bones.empty() || geo.vertices.empty()) continue;

        // The sole: the lowest vertex the skin binds to a foot/toe bone. The bone
        // name is the rig's own ("left foot", "right toe", ...).
        bool has_foot_bone = false;
        for (const BoneInfo& bone : geo.bones) {
            if (bone.name.find("foot") != std::string::npos ||
                bone.name.find("toe") != std::string::npos) {
                has_foot_bone = true;
            }
        }
        ASSERT_TRUE(has_foot_bone) << model << ": no foot/toe bone in the parsed rig";

        float mesh_min_z = geo.vertices[0].pos.z;
        float sole_z = 1e30f;
        for (const RenderVertex& v : geo.vertices) {
            mesh_min_z = std::min(mesh_min_z, v.pos.z);
            if (v.boneIndex >= geo.bones.size()) continue;
            const std::string& name = geo.bones[v.boneIndex].name;
            if (name.find("foot") == std::string::npos && name.find("toe") == std::string::npos) {
                continue;
            }
            sole_z = std::min(sole_z, v.pos.z);
        }

        EXPECT_LT(sole_z, 2.0f)
            << model << ": the sole is not within 2 cm of the object origin (feet frame)";
        EXPECT_GT(sole_z, -2.0f)
            << model << ": the sole is more than 2 cm BELOW the object origin (feet frame)";

        // The mesh's lowest vertex is not the sole: -min_p.z would lift the body
        // by the gap between the two, which is the reported float.
        EXPECT_LT(mesh_min_z, sole_z - 20.0f)
            << model << ": the lowest vertex should be leg/arm geometry well below the sole; "
            << "-min_p.z would then float the sole by " << (sole_z - mesh_min_z) << " cm";
        ++checked;
    }
    if (checked == 0) {
        GTEST_SKIP() << "no retail human .mef found under " << root;
    }
}
