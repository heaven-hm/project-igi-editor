// test_terrain_ctr_rules.cpp - the .ctr invariants the editor checks on behalf
// of the engine (source/level/terrain_ctr_rules.h), and the two shipped files
// that pin them down.
//
// WHY: a level-14 edit session logged
//     [ERROR] CTR cmd_offset out of bounds
//     [ERROR] [Terrain] FATAL: LoadCTRFile failed
//     [ERROR] [Terrain::Save] hmp_ is null, cannot save terrain
// and the whole terrain was gone.  The editor's parse is not wrong: on 13 of the
// 14 shipped levels EVERY record's 0x1C leaf field is a byte offset that lands
// exactly on a leaf module of terrain.cmd (measured: level 1..13 = 100%, level
// 14 = 802/15202).  Level 14's terrain.ctr does not hold the engine's 0x20-byte
// CollNode records at all - it is 486464 bytes = 15202*32 = 11056*44, and only a
// 44-byte record read produces a well-formed tree (11056 nodes, root at 1, max
// depth 17); the engine's 32-byte walk from node 1 reaches 2 nodes.  So the
// fatal-abort is what loses the level, not the parse.
//
// ORACLE (IGI-RE, read-only):
//   src/terrain/terrain.c:128-130  Terrain_Load HOOK 0x4C6240: t->ctr = Resource_Load(..., &ctr_size)
//   src/terrain/terrain.c:142-143  the loop is `i < (ctr_size >> 5)` and adds the .cmd base to leaf
//   src/coll/colltypes.h:95-101    CollNode: child[8] u16, code[8] u8, mask u8, pad[3], leaf
//   src/coll/coll.c:113            Coll_InitCursor: c->node = t->ctr + 1 (node 1 is the root)
//   src/coll/coll.c:230            Coll_Descend: node = nodes + node->child[child]
//   src/coll/coll.c:171            a NULL terrain->ctr is tolerated, collision is skipped
//   src/lod/qcube.c:366,392        a NULL w->ctr skips terrain mesh walk/render only
//
// The real-file cases read <IGI_GAME_PATH> READ-ONLY and self-skip when the
// corpus is absent, like the other corpus suites.

#include "../source/level/terrain_ctr_rules.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace {

ctr_item_s MakeNode(const int16_t children[8], uint8_t mask, uint32_t leaf) {
    ctr_item_s node;
    std::memset(&node, 0, sizeof(node));
    for (int i = 0; i < 8; ++i) node.children_[i] = children[i];
    node.children_mask_ = mask;
    node.cmd_offset_ = leaf;
    return node;
}

std::filesystem::path LevelCtr(int level) {
    const char* root = std::getenv("IGI_GAME_PATH");
    if (root == nullptr || *root == '\0') return {};
    return std::filesystem::path(root) / "MISSIONS" / "location0" /
           ("LEVEL" + std::to_string(level)) / "TERRAIN" / "TERRAIN.CTR";
}

// The engine's rule (src/terrain/terrain.c:142-143): the file is a flat array of
// ctr_item_s records, no header.
std::vector<ctr_item_s> ReadCtr(const std::filesystem::path& path) {
    std::vector<ctr_item_s> nodes;
    std::error_code ec;
    if (path.empty() || !std::filesystem::is_regular_file(path, ec)) return nodes;
    const uintmax_t size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || (size % sizeof(ctr_item_s)) != 0) return nodes;
    std::ifstream file(path, std::ios::binary);
    if (!file) return nodes;
    nodes.resize((size_t)(size / sizeof(ctr_item_s)));
    file.read(reinterpret_cast<char*>(nodes.data()), (std::streamsize)size);
    if (!file) nodes.clear();
    return nodes;
}

int LevelCmdSize(int level) {
    const char* root = std::getenv("IGI_GAME_PATH");
    if (root == nullptr || *root == '\0') return 0;
    const auto path = std::filesystem::path(root) / "MISSIONS" / "location0" /
                      ("LEVEL" + std::to_string(level)) / "TERRAIN" / "TERRAIN.CMD";
    std::error_code ec;
    const uintmax_t size = std::filesystem::file_size(path, ec);
    return ec ? 0 : (int)size;
}

} // namespace

// ── synthetic: the invariants themselves ─────────────────────────────────────

TEST(TerrainCtrRules, EmptyOrAbsentArrayIsReported) {
    EXPECT_EQ(igi::ValidateCtrNodes(nullptr, 10, 100).node_, 0);
    const ctr_item_s nodes[1] = {};
    EXPECT_EQ(igi::ValidateCtrNodes(nodes, 0, 100).node_, 0);
}

TEST(TerrainCtrRules, EngineShapedArrayIsAccepted) {
    // The shape every retail level 1..13 file has: node 0 is the zeroed dummy,
    // node 1 is the root (src/coll/coll.c:113), children are indices into the
    // array, leaves are byte offsets into .cmd (src/terrain/terrain.c:142-143).
    int16_t root_children[8] = {2, 3, 0, 0, 0, 0, 0, 0};
    int16_t leaf_children[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<ctr_item_s> nodes(4);
    nodes[0] = MakeNode(leaf_children, 0x00, 0);
    nodes[1] = MakeNode(root_children, 0x03, 0);   // interior: leaf 0 is legal
    nodes[2] = MakeNode(leaf_children, 0x00, 76);
    nodes[3] = MakeNode(leaf_children, 0x00, 152);

    const igi::ctr_violation_s bad = igi::ValidateCtrNodes(nodes.data(), (int)nodes.size(), 200);
    EXPECT_EQ(bad.node_, -1);
}

TEST(TerrainCtrRules, LeafOutsideCmdIsReported) {
    int16_t none[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<ctr_item_s> nodes(3);
    nodes[0] = MakeNode(none, 0x00, 0);
    nodes[1] = MakeNode(none, 0x00, 0xFFFFFFFFu);  // exactly the level-14 value
    nodes[2] = MakeNode(none, 0x00, 0);

    const igi::ctr_violation_s bad = igi::ValidateCtrNodes(nodes.data(), (int)nodes.size(), 469644);
    EXPECT_EQ(bad.node_, 1);
    EXPECT_FALSE(bad.child_);
    EXPECT_EQ(bad.value_, 0xFFFFFFFFu);
}

TEST(TerrainCtrRules, LeafAtEndOfCmdIsRejected) {
    // `leaf += cmd` then dereference: an offset equal to the size is one past the
    // buffer, so the check is >= and not >.
    int16_t none[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<ctr_item_s> nodes(2);
    nodes[0] = MakeNode(none, 0x00, 0);
    nodes[1] = MakeNode(none, 0x00, 100);
    EXPECT_EQ(igi::ValidateCtrNodes(nodes.data(), 2, 100).node_, 1);
}

TEST(TerrainCtrRules, ChildIndexOutsideArrayIsReported) {
    int16_t root_children[8] = {0, 5, 0, 0, 0, 0, 0, 0};  // slot 1 -> node 5 of 3
    int16_t none[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<ctr_item_s> nodes(3);
    nodes[0] = MakeNode(none, 0x00, 0);
    nodes[1] = MakeNode(root_children, 0x02, 0);
    nodes[2] = MakeNode(none, 0x00, 0);

    const igi::ctr_violation_s bad = igi::ValidateCtrNodes(nodes.data(), 3, 1000);
    EXPECT_EQ(bad.node_, 1);
    EXPECT_TRUE(bad.child_);
    EXPECT_EQ(bad.slot_, 1);
    EXPECT_EQ(bad.value_, 5u);
}

TEST(TerrainCtrRules, UnsetMaskBitsAreNotChecked) {
    // A slot the mask does not select is dead data (the retail files leave 0xFFFF
    // or 0 there); Coll_Descend only ever follows mask bits (src/coll/coll.c:225-230).
    int16_t children[8] = {-1, -1, -1, -1, 0, 0, 0, 0};
    int16_t none[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    std::vector<ctr_item_s> nodes(2);
    nodes[0] = MakeNode(none, 0x00, 0);
    nodes[1] = MakeNode(children, 0x00, 0);
    EXPECT_EQ(igi::ValidateCtrNodes(nodes.data(), 2, 1000).node_, -1);
}

// ── the shipped corpus (READ-ONLY) ───────────────────────────────────────────

TEST(TerrainCtrRulesCorpus, RetailLevel1CtrMatchesTheEngineRule) {
    const std::vector<ctr_item_s> nodes = ReadCtr(LevelCtr(1));
    const int cmd_size = LevelCmdSize(1);
    if (nodes.empty() || cmd_size == 0) {
        GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";
    }
    // 352704 bytes / 32 = 11022 records - the engine's ctr_size >> 5.
    EXPECT_EQ((int)nodes.size(), 11022);
    const igi::ctr_violation_s bad = igi::ValidateCtrNodes(nodes.data(), (int)nodes.size(), cmd_size);
    EXPECT_EQ(bad.node_, -1) << "node " << bad.node_ << " value " << bad.value_;
}

TEST(TerrainCtrRulesCorpus, RetailLevel14CtrIsNotTheEngineFormat) {
    const std::vector<ctr_item_s> nodes = ReadCtr(LevelCtr(14));
    const int cmd_size = LevelCmdSize(14);
    if (nodes.empty() || cmd_size == 0) {
        GTEST_SKIP() << "set IGI_GAME_PATH to a retail install to run this test";
    }
    // 486464 bytes / 32 = 15202 records the engine would read.
    EXPECT_EQ((int)nodes.size(), 15202);

    // The very first record after the root breaks the rule: 0xFFFFFFFF is not a
    // byte offset into a 469644-byte terrain.cmd.  This is the value the editor's
    // log printed as "CTR cmd_offset out of bounds".
    const igi::ctr_violation_s bad = igi::ValidateCtrNodes(nodes.data(), (int)nodes.size(), cmd_size);
    EXPECT_EQ(bad.node_, 1);
    EXPECT_FALSE(bad.child_);
    EXPECT_EQ(bad.value_, 0xFFFFFFFFu);

    // ... and it stays broken: the 44-byte-record layout means only 802 of 15202
    // 32-byte slots carry a leaf that exists in .cmd.  Count them, so a future
    // "fix" that merely skips the first bad node cannot pass this test.
    int leaves_in_cmd = 0;
    for (const ctr_item_s& node : nodes) {
        if ((int)node.cmd_offset_ < cmd_size) ++leaves_in_cmd;
    }
    EXPECT_EQ(leaves_in_cmd, 802);
}
