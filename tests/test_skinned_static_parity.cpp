// The posed (skinned) draw path and the static bind-pose draw path must put the
// SAME model at the SAME placement in the SAME place.
//
// The user's report that produced this suite: an AI is drawn below the terrain
// in the viewport and correctly in the pause-menu view; selecting the AI and
// opening the escape menu "properly places" it, and closing the menu puts it
// back under the ground. The same object at the same placement cannot depend on
// which pass draws it, so the two passes were compared directly.
//
// WHAT THE TWO PASSES ARE
//
//   * STATIC (the pause-menu view, the rigid draw, the picking ray): the mesh's
//     vertices are replaced at parse time by the rig's bind pose --
//     `SkinSkinnedVertices(geometry, nullptr, ...)`, i.e.
//     `(rawPos + ComputeBoneWorldPositions(rig)[bone]) * kMefNativeScale`
//     (source/renderer/mef_native.cpp:1122-1129), and drawn with the object
//     matrix `T(pos) * Rz*Ry*Rx * S(40.96*scale)` (source/renderer/
//     renderer_objects.cpp:1198-1268).
//   * SKINNED (the viewport's posed AI): `Renderer::DrawSkinnedMesh`
//     (source/renderer/renderer.cpp:365-533) runs the ENGINE's own skinner,
//     `BoneMesh_Skin` HOOK 0x49B700 (src/dxinit/d3drender.c:226-313), over the
//     same vertex records with the pose's bone records, and draws the result
//     with the object matrix source/app.cpp builds.
//
// The engine's skinned vertices live in the SKELETON's frame: `BoneMesh_Skin`
// computes `Bone::matrix * v + Bone::pos` (src/dxinit/d3drender.c:259-264) and
// the bone chain starts at the skeleton's root offset, which `BoneObj_Load`
// 0x4D67A0 copies straight out of the BOBJ's TLST (src/boneobj/boneobj.c:1570-1576);
// `BoneObj_UpdateBone` accumulates from `pos = {0,0,0}` (src/boneobj/boneobj.c:434-439).
// The editor's rig table (source/renderer/hardcoded_bones.h) agrees with that
// TLST on every bone EXCEPT the root, where it carries (0, 0, 3990.4) instead of
// (0, 0, 0) -- so the static bake is lifted by exactly 3990.4 raw units
// (0.974 m at the engine's 4096 units/m, kUnitsToMeters, src/humanbase/human.c:165)
// relative to any pose built from the retail skeleton.
//
// These tests pin both halves of that: the rig/skeleton relationship, and the
// resulting world-space parity of the two draw paths.
//
// Corpus-backed: the retail skeleton comes from `COMMON/ANIMS/<skel>.IFF` and the
// model from the install's `models/*.mef`; both self-skip without IGI_GAME_PATH.
// The synthetic case runs everywhere.

#include <gtest/gtest.h>

#include "animation.h"
#include "renderer/igi_transform.h"
#include "renderer/mef_native.h"
#include "renderer/posed_draw_transform.h"
#include "runtime/anim_player.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------------------
// The retail BOBJ skeleton: BOBH's PLST and TLST, which is what the engine's
// `BoneObj_Load` 0x4D67A0 copies into `gSkeletons[skel].parents`/`.offsets`
// (src/boneobj/boneobj.c:1570-1576).
// ---------------------------------------------------------------------------

struct RetailSkeleton {
    std::vector<int32_t> parents;   // PLST
    std::vector<float> offsets;     // TLST, 3 floats per bone
    bool valid() const {
        return !parents.empty() && offsets.size() == parents.size() * 3;
    }
};

uint32_t ReadBe32(const std::vector<uint8_t>& b, size_t off) {
    return (static_cast<uint32_t>(b[off]) << 24) | (static_cast<uint32_t>(b[off + 1]) << 16) |
           (static_cast<uint32_t>(b[off + 2]) << 8) | static_cast<uint32_t>(b[off + 3]);
}

float ReadLeFloat(const std::vector<uint8_t>& b, size_t off) {
    float v = 0.0f;
    std::memcpy(&v, &b[off], sizeof(v));
    return v;
}

int32_t ReadLeInt(const std::vector<uint8_t>& b, size_t off) {
    int32_t v = 0;
    std::memcpy(&v, &b[off], sizeof(v));
    return v;
}

// IFF chunks: 4-byte big-endian id, 4-byte big-endian size, payload; a FORM/LIST
// adds a 4-byte type before its children. The engine walks the same tree with
// `Iff_Parse`/`Iff_FindChunk` (src/boneobj/boneobj.c:1543-1576).
void WalkIff(const std::vector<uint8_t>& b, size_t off, size_t end, RetailSkeleton& out) {
    while (off + 8 <= end) {
        const uint32_t size = ReadBe32(b, off + 4);
        const char* id = reinterpret_cast<const char*>(&b[off]);
        if (std::memcmp(id, "FORM", 4) == 0 || std::memcmp(id, "LIST", 4) == 0) {
            if (off + 12 <= end) WalkIff(b, off + 12, off + 8 + size, out);
        } else if (std::memcmp(id, "TLST", 4) == 0 && out.offsets.empty()) {
            const size_t n = size / 4;
            out.offsets.resize(n);
            for (size_t i = 0; i < n; ++i) out.offsets[i] = ReadLeFloat(b, off + 8 + i * 4);
        } else if (std::memcmp(id, "PLST", 4) == 0 && out.parents.empty()) {
            const size_t n = size / 4;
            out.parents.resize(n);
            for (size_t i = 0; i < n; ++i) out.parents[i] = ReadLeInt(b, off + 8 + i * 4);
        }
        off += 8 + size + (size & 1);
    }
}

RetailSkeleton ReadRetailSkeleton(const fs::path& iff_path) {
    RetailSkeleton out;
    std::ifstream in(iff_path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return out;
    const std::streamsize size = in.tellg();
    if (size <= 0) return out;
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    in.seekg(0);
    if (!in.read(reinterpret_cast<char*>(bytes.data()), size)) return out;
    WalkIff(bytes, 0, bytes.size(), out);
    return out;
}

const char* CorpusRoot() {
    for (const char* name : {"IGI_MEF_CORPUS", "IGI_GAME_PATH"}) {
        const char* p = std::getenv(name);
        if (p != nullptr && *p != '\0') return p;
    }
    return nullptr;
}

// The first `<name>.mef` anywhere under the install (the editor's own resolver
// searches the same per-level `MODELS` trees).
std::string FindModel(const fs::path& root, const std::string& name) {
    std::error_code ec;
    for (fs::recursive_directory_iterator it(root, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        if (it->path().filename().string() == name + ".mef") return it->path().string();
    }
    return {};
}

// ---------------------------------------------------------------------------
// A clip in the editor's own form whose skeleton IS the retail one: `Bone()`
// rest positions are TLST / 40.96 (the BEF's own convention, anim_player.h:62-67)
// and one identity rotation key per bone, so the pose is the bind pose.
// ---------------------------------------------------------------------------

AnimationClip BindPoseClipFromSkeleton(const RetailSkeleton& skel, int anim_id) {
    AnimationClip clip;
    clip.name = "bind_pose";
    clip.animId = anim_id;
    clip.length_ms = 2;   // the engine length is `length - 1` (anim_player.cpp:195)
    clip.tp_flag = 0;
    const size_t bones = skel.parents.size();
    for (size_t i = 0; i < bones; ++i) {
        AnimBone b;
        b.index = static_cast<int>(i);
        b.name = "Bone_" + std::to_string(i);
        b.parent = skel.parents[i];
        b.restPos = glm::vec3(skel.offsets[i * 3 + 0], skel.offsets[i * 3 + 1],
                              skel.offsets[i * 3 + 2]) /
                    igi::AnimPlayer::kRawUnitsPerMetre;
        clip.bones.push_back(b);

        AnimRotationKey key;
        key.bone = static_cast<int>(i);
        key.time_ms = 0;
        key.q0 = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        key.q1 = key.q0;
        key.q2 = key.q0;
        clip.rotationKeys.push_back(key);
    }
    // `BoneObj_InitAnimInst` dereferences `desc->pos->keys[count - 1]`
    // (src/boneobj/boneobj.c:1014), so a clip must carry a position track; it is
    // root motion only and never enters the pose (boneobj.c:434-439,
    // `BoneObj_AnimPositionCB` writes the instance, not a bone).
    AnimTranslationKey pos_key;
    pos_key.track = 0;
    pos_key.time_ms = 0;
    pos_key.pos = glm::vec3(0.0f);
    clip.translationKeys.push_back(pos_key);
    return clip;
}

// The engine's skinned vertex positions, in METRES, for a posed model: exactly
// `Renderer::DrawSkinnedMesh`'s arithmetic (source/renderer/renderer.cpp:409-432).
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

// World position of a vertex through each of the two paths, for one placement.
// `origin_metres` is what `App::Frame` adds for the posed draw: the rig's root
// origin, in the same units the mesh vertices are in.
glm::dvec3 StaticWorld(const glm::mat4& static_mat, const glm::vec3& vertex_metres) {
    return glm::dvec3(static_mat * glm::dvec4(glm::dvec3(vertex_metres), 1.0));
}

glm::dvec3 SkinnedWorld(const glm::mat4& skinned_mat, const glm::vec3& vertex_metres) {
    return glm::dvec3(skinned_mat * glm::dvec4(glm::dvec3(vertex_metres), 1.0));
}

// The same placement the editor's static pass uses (renderer_objects.cpp:1189-1268)
// and the posed pass must use.
//
// TWO placements, on purpose. The editor's object matrices are float32
// (`glm::mat4`), and a real LEVEL1 placement has z = 1.749e8 raw units, where the
// float32 ULP is 2^-23 * 1.749e8 = 21 units -- so two paths that compute the SAME
// world point still differ by ~20 units there, and no tighter claim is possible
// in that frame. `kPosLocal` is the same geometry at a well-conditioned position,
// where the parity can be asserted to 0.05 units (12 um).
const glm::dvec3 kPosLevel(22683652.0, -60990340.0, 174936032.0);   // LEVEL1 objects.qsc
const glm::dvec3 kPosLocal(1000.0, -2000.0, 3000.0);
const glm::dvec3 kRot(0.0, 0.0, 0.0);
const float kScale = 1.0f;

// Three float32 ULPs at the level's own Z: the resolution of any world-space
// comparison made there.
constexpr double kLevelFrameTolerance = 64.0;

}  // namespace

// ---------------------------------------------------------------------------
// The rig / retail-skeleton relationship the two spaces differ by.
// ---------------------------------------------------------------------------

TEST(SkinnedStaticParity, RetailSkeletonMatchesTheEditorRigExceptAtTheRoot) {
    const char* root = CorpusRoot();
    if (root == nullptr || !fs::exists(root)) {
        GTEST_SKIP() << "Set IGI_GAME_PATH to an IGI install root to enable this suite";
    }
    struct Case { const char* model; const char* skel; };
    const Case cases[] = {{"000_01_1", "000"}, {"003_01_1", "001"}, {"015_01_1", "006"}};
    int checked = 0;
    for (const Case& c : cases) {
        const std::string model_path = FindModel(root, c.model);
        const fs::path iff = fs::path(root) / "COMMON" / "ANIMS" / (std::string(c.skel) + ".IFF");
        if (model_path.empty() || !fs::exists(iff)) continue;
        const ParsedGeometry geo = ParseMefFile(model_path);
        const RetailSkeleton skel = ReadRetailSkeleton(iff);
        ASSERT_TRUE(skel.valid()) << iff;
        ASSERT_EQ(geo.bones.size(), skel.parents.size()) << c.model;
        ++checked;

        const std::vector<glm::vec3> rig_world = ComputeBoneWorldPositionsPublic(geo.bones);
        const glm::vec3 origin = RigRootOriginRaw(geo.bones);
        for (size_t i = 0; i < geo.bones.size(); ++i) {
            EXPECT_EQ(geo.bones[i].parent, skel.parents[i])
                << c.model << " bone " << i << " parent (REIH vs PLST)";
            const glm::vec3 tlst(skel.offsets[i * 3 + 0], skel.offsets[i * 3 + 1],
                                 skel.offsets[i * 3 + 2]);
            if (i == 0) {
                // The one bone the two tables disagree about: the editor's root
                // pivot is the animation's root height, the skeleton's is 0.
                EXPECT_GT(glm::length(origin), 1000.0f) << c.model;
                EXPECT_NEAR(glm::length(tlst), 0.0f, 1e-3f) << c.model;
            } else {
                EXPECT_NEAR(glm::length(tlst - geo.bones[i].pivot), 0.0f, 0.01f)
                    << c.model << " bone " << i << " pivot (REIH vs TLST)";
            }
            // Every bone's ABSOLUTE rest position differs by the same constant:
            // the rig's root origin. That is why one model-origin translation is
            // enough to reconcile the two draw paths.
            glm::vec3 skel_world = tlst;
            for (int p = skel.parents[i]; p >= 0 && p < static_cast<int>(skel.parents.size());
                 p = skel.parents[p]) {
                skel_world += glm::vec3(skel.offsets[p * 3 + 0], skel.offsets[p * 3 + 1],
                                        skel.offsets[p * 3 + 2]);
            }
            EXPECT_NEAR(glm::length((rig_world[i] - skel_world) - origin), 0.0f, 0.02f)
                << c.model << " bone " << i << " absolute rest position";
        }
    }
    if (checked == 0) GTEST_SKIP() << "no retail human model + skeleton pair found under " << root;
}

// ---------------------------------------------------------------------------
// THE DELIVERABLE: one model, one placement, a vertex through BOTH paths.
// ---------------------------------------------------------------------------

TEST(SkinnedStaticParity, PosedVertexLandsWhereTheStaticBindPosePutsIt) {
    const char* root = CorpusRoot();
    if (root == nullptr || !fs::exists(root)) {
        GTEST_SKIP() << "Set IGI_GAME_PATH to an IGI install root to enable this suite";
    }
    struct Case { const char* model; const char* skel; };
    const Case cases[] = {{"000_01_1", "000"}, {"003_01_1", "001"}, {"015_01_1", "006"}};
    int checked = 0;
    for (const Case& c : cases) {
        const std::string model_path = FindModel(root, c.model);
        const fs::path iff = fs::path(root) / "COMMON" / "ANIMS" / (std::string(c.skel) + ".IFF");
        if (model_path.empty() || !fs::exists(iff)) continue;
        const ParsedGeometry geo = ParseMefFile(model_path);
        const RetailSkeleton skel = ReadRetailSkeleton(iff);
        if (!skel.valid()) continue;
        ++checked;

        // The pose the viewport draws: the engine's own records, posed by the
        // engine's own `BoneObj_Update` through `AnimPlayer` (the retail
        // skeleton, so the root offset is the engine's).
        igi::AnimPlayer player;
        ASSERT_TRUE(player.Build(BindPoseClipFromSkeleton(skel, 2), 2)) << c.model;
        ASSERT_TRUE(player.Play(igi::kAnimLoop)) << c.model;
        const std::vector<igi::BoneTransform>& raw_bones = player.RawBoneTransforms();
        ASSERT_FALSE(raw_bones.empty()) << c.model;

        const std::vector<glm::vec3> skinned = SkinnedVerticesMetres(geo, raw_bones);
        ASSERT_EQ(skinned.size(), geo.baseVertexCount) << c.model;

        // The placement, through each pass's own matrix. The posed pass adds the
        // rig's root origin -- the model-local point the static bake already has
        // folded into every vertex.
        const float total_scale = 40.96f * kScale;
        const glm::vec3 origin_metres = RigRootOriginRaw(geo.bones) * kMefNativeScale;
        const glm::vec3 zero_origin(0.0f);

        // The model origin is not a fudge factor: it is a real, large offset
        // (0.974 m for every shipped human rig), and it is exactly the defect --
        // a posed model drawn without it lands this far below the static one.
        EXPECT_GT(glm::length(origin_metres), 0.5f) << c.model;

        const size_t sample = std::min<size_t>(geo.baseVertexCount, 256);

        // (a) The tight claim, in a well-conditioned frame: the two paths put the
        // vertex in the same place to within 0.05 raw units (12 um at the
        // engine's 4096 units/m).
        {
            const glm::mat4 static_mat =
                igi::IgiObjectMatrixAtModelOrigin(kPosLocal, kRot, total_scale, zero_origin);
            // The production decision: `igi::PosedObjectMatrix` is what
            // App::Frame hands to Renderer::DrawSkinnedMesh.
            const glm::mat4 posed_mat =
                igi::PosedObjectMatrix(kPosLocal, kRot, kScale, &geo.bones);
            double max_error = 0.0;
            for (size_t i = 0; i < sample; ++i) {
                const glm::dvec3 via_static = StaticWorld(static_mat, geo.vertices[i].pos);
                const glm::dvec3 via_posed = SkinnedWorld(posed_mat, skinned[i]);
                max_error = std::max(max_error, glm::length(via_static - via_posed));
            }
            EXPECT_LT(max_error, 0.05) << c.model << ": posed and static draws disagree";
        }

        // (b) The same comparison at the object's REAL level placement, where the
        // float32 object matrix can only resolve ~21 units (see kPosLevel): the
        // paths must agree to that resolution, and the defect the origin closes
        // must be far larger than it.
        {
            const glm::mat4 static_mat =
                igi::IgiObjectMatrixAtModelOrigin(kPosLevel, kRot, total_scale, zero_origin);
            const glm::mat4 posed_mat =
                igi::PosedObjectMatrix(kPosLevel, kRot, kScale, &geo.bones);
            const glm::mat4 posed_mat_without_origin =
                igi::IgiObjectMatrixAtModelOrigin(kPosLevel, kRot, total_scale, zero_origin);
            double max_error = 0.0;
            double min_defect = std::numeric_limits<double>::max();
            double max_defect = 0.0;
            for (size_t i = 0; i < sample; ++i) {
                const glm::dvec3 via_static = StaticWorld(static_mat, geo.vertices[i].pos);
                const glm::dvec3 via_posed = SkinnedWorld(posed_mat, skinned[i]);
                max_error = std::max(max_error, glm::length(via_static - via_posed));

                const glm::dvec3 via_posed_no_origin =
                    SkinnedWorld(posed_mat_without_origin, skinned[i]);
                const double defect = via_static.z - via_posed_no_origin.z;
                min_defect = std::min(min_defect, defect);
                max_defect = std::max(max_defect, defect);
            }
            EXPECT_LT(max_error, kLevelFrameTolerance) << c.model;
            // The un-fixed posed draw is 3984..4000 raw units (0.97 m) BELOW the
            // static one -- the defect the user saw -- and adding the rig origin
            // is what closes it.
            EXPECT_GT(min_defect, 3000.0) << c.model;
            EXPECT_NEAR(min_defect, static_cast<double>(origin_metres.z) * total_scale,
                        kLevelFrameTolerance)
                << c.model;
            EXPECT_NEAR(min_defect, max_defect, kLevelFrameTolerance) << c.model;
        }
    }
    if (checked == 0) GTEST_SKIP() << "no retail human model + skeleton pair found under " << root;
}

// ---------------------------------------------------------------------------
// The same invariant without the retail install: a synthetic rig whose root
// pivot is non-zero, and a skeleton built from the same table with the root
// zeroed -- exactly the relationship the retail TLST has to the editor's rig.
// ---------------------------------------------------------------------------

TEST(SkinnedStaticParity, SyntheticRigOriginReconcilesTheTwoPaths) {
    // One bone, root pivot (0, 0, 100): the static bake lifts both vertices by
    // it, the skeleton (root offset 0) does not.
    const glm::vec3 kRootPivot(0.0f, 0.0f, 100.0f);
    ParsedGeometry geo;
    geo.modelType = 1;
    geo.fromRenderMesh = true;
    geo.baseVertexCount = 2;
    geo.extraInfluenceCount = 0;
    for (const glm::vec3& raw : {glm::vec3(0.0f, 0.0f, -100.0f), glm::vec3(0.0f, 0.0f, 0.0f)}) {
        RenderVertex v;
        v.rawPos = raw;
        v.pos = raw * kMefNativeScale;
        v.normal = glm::vec3(0.0f, 0.0f, 1.0f);
        v.boneIndex = 0;
        v.weight = 1.0f;
        geo.vertices.push_back(v);
    }
    geo.triangles.push_back({0, 1, 0});
    BoneInfo bone;
    bone.name = "center";
    bone.parent = -1;
    bone.pivot = kRootPivot;
    geo.bones.push_back(bone);

    // The static pass's vertices: the rig's bind-pose bake, in metres.
    std::vector<glm::vec3> rest_pos;
    std::vector<glm::vec3> rest_normal;
    SkinSkinnedVertices(geo, nullptr, rest_pos, rest_normal);
    ASSERT_EQ(rest_pos.size(), 2u);

    // The posed pass's vertices: the ENGINE's skeleton (root offset 0), posed
    // with the identity object matrix, which is what `AnimPlayer` does.
    RetailSkeleton skel;
    skel.parents = {-1};
    skel.offsets = {0.0f, 0.0f, 0.0f};
    igi::AnimPlayer player;
    ASSERT_TRUE(player.Build(BindPoseClipFromSkeleton(skel, 2), 2));
    ASSERT_TRUE(player.Play(igi::kAnimLoop));
    const std::vector<glm::vec3> skinned = SkinnedVerticesMetres(geo, player.RawBoneTransforms());
    ASSERT_EQ(skinned.size(), 2u);

    EXPECT_FLOAT_EQ(RigRootOriginRaw(geo.bones).z, kRootPivot.z);

    const float total_scale = 40.96f * kScale;
    const glm::mat4 static_mat =
        igi::IgiObjectMatrixAtModelOrigin(kPosLocal, kRot, total_scale, glm::vec3(0.0f));
    const glm::vec3 origin_metres = RigRootOriginRaw(geo.bones) * kMefNativeScale;
    const glm::mat4 posed_mat = igi::PosedObjectMatrix(kPosLocal, kRot, kScale, &geo.bones);

    for (size_t i = 0; i < rest_pos.size(); ++i) {
        const glm::dvec3 via_static = StaticWorld(static_mat, rest_pos[i]);
        const glm::dvec3 via_posed = SkinnedWorld(posed_mat, skinned[i]);
        EXPECT_LT(glm::length(via_static - via_posed), 0.05) << "vertex " << i;

        // Without the model origin the posed vertex is the rig origin lower --
        // 100 raw units here, 3990.4 for the shipped human rigs.
        const glm::dvec3 via_posed_no_origin =
            SkinnedWorld(igi::IgiObjectMatrixAtModelOrigin(kPosLocal, kRot, total_scale,
                                                           glm::vec3(0.0f)),
                         skinned[i]);
        EXPECT_NEAR(via_static.z - via_posed_no_origin.z, 100.0, 0.05) << "vertex " << i;
    }
}
