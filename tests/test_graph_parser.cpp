#include <gtest/gtest.h>
#include "../source/renderer/graph_writer.h"
#include "utils.h"
#include <string>
#include <cmath>
#include <cstring>
#include <fstream>
#include <algorithm>
#include <filesystem>

// ============================================================
//  Graph Parser — graph1.dat structural tests
//
//  Path: <exe_dir>\missions\location0\level1\graph1.dat
// ============================================================

static std::string GraphPath() {
    return Utils::GetIGIRootPath() + "\\missions\\location0\\level1\\graphs\\graph1.dat";
}

class GraphParserTest : public ::testing::Test {
protected:
    GraphFile graph;
    void SetUp() override {
        graph = GRAPH_Parse(GraphPath());
    }
};

TEST_F(GraphParserTest, FileExistsAndParsesValid) {
    ASSERT_TRUE(graph.valid) << "Graph parse failed\nPath: " << GraphPath();
}

TEST_F(GraphParserTest, HasNodes) {
    EXPECT_GT(graph.nodes.size(), 0u);
}

TEST_F(GraphParserTest, AllNodeIdsNonNegative) {
    for (const auto& n : graph.nodes)
        EXPECT_GE(n.id, 0) << "Negative node ID: " << n.id;
}

TEST_F(GraphParserTest, AllCoordinatesAreFinite) {
    for (const auto& n : graph.nodes) {
        EXPECT_TRUE(std::isfinite(n.x)) << "Non-finite X for node " << n.id;
        EXPECT_TRUE(std::isfinite(n.y)) << "Non-finite Y for node " << n.id;
        EXPECT_TRUE(std::isfinite(n.z)) << "Non-finite Z for node " << n.id;
    }
}

TEST_F(GraphParserTest, AllMaterialValuesInRange) {
    for (const auto& n : graph.nodes)
        EXPECT_TRUE(n.material >= 0 && n.material <= 23)
            << "Material " << n.material << " out of 0-23 range for node " << n.id;
}

// ------------------------------------------------------------
//  GRAPH_Save round-trip tests
// ------------------------------------------------------------

static std::string TempOut(const char* name) {
    return (std::filesystem::temp_directory_path() / name).string();
}

// graph1.dat is a sparse 1-node graph; the save round-trip tests need a graph
// with several nodes so "other nodes unchanged" is meaningful. graph7.dat in the
// same folder has 11 node records.
static std::string GraphPathMulti() {
    return Utils::GetIGIRootPath() + "\\missions\\location0\\level1\\graphs\\graph7.dat";
}

TEST_F(GraphParserTest, SaveRoundTripPersistsModifiedPosition) {
    GraphFile graph = GRAPH_Parse(GraphPathMulti());
    ASSERT_TRUE(graph.valid);
    ASSERT_GT(graph.nodes.size(), 1u);

    GraphFile edited = graph;
    const int targetId = edited.nodes[0].id;
    edited.nodes[0].x += 123.0;
    edited.nodes[0].y += 45.0;
    edited.nodes[0].z += 6.0;

    const std::string out = TempOut("igi_graph_roundtrip.dat");
    ASSERT_TRUE(GRAPH_Save(GraphPathMulti(), out, edited));

    GraphFile reloaded = GRAPH_Parse(out);
    ASSERT_TRUE(reloaded.valid);
    ASSERT_EQ(reloaded.nodes.size(), graph.nodes.size());

    const GraphNode* rn = nullptr;
    for (const auto& n : reloaded.nodes)
        if (n.id == targetId) { rn = &n; break; }
    ASSERT_NE(rn, nullptr);

    EXPECT_DOUBLE_EQ(rn->x, graph.nodes[0].x + 123.0);
    EXPECT_DOUBLE_EQ(rn->y, graph.nodes[0].y + 45.0);
    EXPECT_DOUBLE_EQ(rn->z, graph.nodes[0].z + 6.0);

    std::filesystem::remove(out);
}

// ------------------------------------------------------------
//  GRAPH_Write full-serializer round-trip
// ------------------------------------------------------------

TEST_F(GraphParserTest, WriteRoundTripPreservesNodesAndEdges) {
    GraphFile g = GRAPH_Parse(GraphPathMulti());
    ASSERT_TRUE(g.valid);
    ASSERT_GT(g.nodes.size(), 1u);

    const std::string out = TempOut("igi_graph_write.dat");
    ASSERT_TRUE(GRAPH_Write(GraphPathMulti(), out, g));

    GraphFile r = GRAPH_Parse(out);
    ASSERT_TRUE(r.valid);
    ASSERT_EQ(r.nodes.size(), g.nodes.size());
    ASSERT_EQ(r.edges.size(), g.edges.size());
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        EXPECT_EQ(r.nodes[i].id, g.nodes[i].id);
        EXPECT_DOUBLE_EQ(r.nodes[i].x, g.nodes[i].x);
        EXPECT_DOUBLE_EQ(r.nodes[i].y, g.nodes[i].y);
        EXPECT_DOUBLE_EQ(r.nodes[i].z, g.nodes[i].z);
        EXPECT_EQ(r.nodes[i].material, g.nodes[i].material);
        EXPECT_EQ(r.nodes[i].criteria, g.nodes[i].criteria);
    }
    std::filesystem::remove(out);
}

TEST_F(GraphParserTest, WriteSupportsAddingAndRemovingNodes) {
    GraphFile g = GRAPH_Parse(GraphPathMulti());
    ASSERT_TRUE(g.valid);
    const size_t origCount = g.nodes.size();

    // Remove the first node and add a brand-new one.
    const int removedId = g.nodes.front().id;
    g.nodes.erase(g.nodes.begin());
    GraphNode added; added.id = 999; added.x = 1.0; added.y = 2.0; added.z = 3.0;
    g.nodes.push_back(added);

    const std::string out = TempOut("igi_graph_addrem.dat");
    ASSERT_TRUE(GRAPH_Write(GraphPathMulti(), out, g));

    GraphFile r = GRAPH_Parse(out);
    ASSERT_TRUE(r.valid);
    EXPECT_EQ(r.nodes.size(), origCount);  // -1 +1
    EXPECT_EQ(GRAPH_FindNode(r, removedId), nullptr);
    const GraphNode* a = GRAPH_FindNode(r, 999);
    ASSERT_NE(a, nullptr);
    EXPECT_DOUBLE_EQ(a->x, 1.0);
    std::filesystem::remove(out);
}

TEST_F(GraphParserTest, WriteRegeneratesAdjacencyWithoutDeletedNode) {
    GraphFile g = GRAPH_Parse(GraphPathMulti());  // graph7: nodes + edges
    ASSERT_TRUE(g.valid);
    ASSERT_GT(g.edges.size(), 0u);

    // Delete a node that participates in an edge (mirrors the editor delete).
    const int delId = g.edges.front().node1;
    g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(),
        [&](const GraphNode& n) { return n.id == delId; }), g.nodes.end());
    g.edges.erase(std::remove_if(g.edges.begin(), g.edges.end(),
        [&](const GraphEdge& e) { return e.node1 == delId || e.node2 == delId; }), g.edges.end());

    const std::string out = TempOut("igi_graph_adj.dat");
    ASSERT_TRUE(GRAPH_Write(GraphPathMulti(), out, g));

    // The adjacency matrix must no longer reference the deleted node.
    std::ifstream f(out, std::ios::binary);
    std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    int mn = 0; std::memcpy(&mn, d.data() + 12, 4);
    ASSERT_GT(mn, delId);
    auto refAt = [&](int r, int c) {
        int v; std::memcpy(&v, d.data() + 30 + ((size_t)r * mn + c) * 8, 4); return v;
    };
    for (int k = 0; k < mn; ++k) {
        EXPECT_EQ(refAt(delId, k), -1) << "deleted node row not cleared at col " << k;
        EXPECT_EQ(refAt(k, delId), -1) << "deleted node col not cleared at row " << k;
    }
    std::filesystem::remove(out);
}

TEST_F(GraphParserTest, WriteAdjacencyMatchesEdges) {
    GraphFile g = GRAPH_Parse(GraphPathMulti());
    ASSERT_TRUE(g.valid);
    ASSERT_GT(g.edges.size(), 0u);

    const std::string out = TempOut("igi_graph_adj2.dat");
    ASSERT_TRUE(GRAPH_Write(GraphPathMulti(), out, g));
    std::ifstream f(out, std::ios::binary);
    std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    f.close();
    int mn = 0; std::memcpy(&mn, d.data() + 12, 4);
    auto refAt = [&](int r, int c) {
        int v; std::memcpy(&v, d.data() + 30 + ((size_t)r * mn + c) * 8, 4); return v;
    };
    // Every edge sets matrix[a][b]=a and matrix[b][a]=b.
    for (const GraphEdge& e : g.edges) {
        if (e.node1 <= 0 || e.node2 <= 0 || e.node1 >= mn || e.node2 >= mn) continue;
        EXPECT_EQ(refAt(e.node1, e.node2), e.node1);
        EXPECT_EQ(refAt(e.node2, e.node1), e.node2);
    }
    std::filesystem::remove(out);
}

TEST_F(GraphParserTest, WriteUnchangedIsByteIdentical) {
    const std::string src = Utils::GetIGIRootPath() +
        "\\editor\\backup\\level1\\graphs\\graph8.dat";
    GraphFile g = GRAPH_Parse(src);
    if (!g.valid) GTEST_SKIP() << "no pristine backup graph8";

    const std::string out = TempOut("igi_graph_identical.dat");
    ASSERT_TRUE(GRAPH_Write(src, out, g));

    auto readAll = [](const std::string& p) {
        std::ifstream f(p, std::ios::binary);
        std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return d;
    };
    std::vector<char> o = readAll(src), w = readAll(out);
    EXPECT_EQ(o.size(), w.size()) << "size differs orig=" << o.size() << " new=" << w.size();
    const size_t n = std::min(o.size(), w.size());
    int firstDiff = -1, diffs = 0;
    for (size_t i = 0; i < n; ++i)
        if (o[i] != w[i]) { if (firstDiff < 0) firstDiff = (int)i; ++diffs; }
    if (firstDiff >= 0) {
        ADD_FAILURE() << "bytes differ: count=" << diffs << " firstDiffOffset=" << firstDiff
                      << " (nodeDataStart=" << (30 + 100*100*8) << ")";
    }
    std::filesystem::remove(out);
}

TEST_F(GraphParserTest, WriteReproducesOriginalRoutingTable) {
    // Pristine backup so the adjacency reflects the game's own routing table.
    const std::string src = Utils::GetIGIRootPath() +
        "\\editor\\backup\\level1\\graphs\\graph8.dat";
    GraphFile g = GRAPH_Parse(src);
    if (!g.valid) GTEST_SKIP() << "no pristine backup graph8";

    const std::string out = TempOut("igi_graph_repro.dat");
    ASSERT_TRUE(GRAPH_Write(src, out, g));

    auto readAll = [](const std::string& p) {
        std::ifstream f(p, std::ios::binary);
        std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        return d;
    };
    std::vector<char> o = readAll(src), w = readAll(out);
    ASSERT_EQ(o.size(), w.size());
    int mn = 0; std::memcpy(&mn, o.data() + 12, 4);

    auto cell = [&](std::vector<char>& d, int r, int c, int& ref, float& dist) {
        const size_t off = 30 + ((size_t)r * mn + c) * 8;
        std::memcpy(&ref, d.data() + off, 4);
        std::memcpy(&dist, d.data() + off + 4, 4);
    };
    int mismatch = 0;
    for (int r = 0; r < mn && mismatch < 6; ++r)
        for (int c = 0; c < mn && mismatch < 6; ++c) {
            int ro, rw; float dO, dW;
            cell(o, r, c, ro, dO); cell(w, r, c, rw, dW);
            // Same reachability (both -1 or both set), and matching distance.
            if ((ro == -1) != (rw == -1)) {
                ++mismatch; ADD_FAILURE() << "reachability differs at [" << r << "][" << c
                    << "] orig=" << ro << " new=" << rw;
            } else if (ro != -1 && std::fabs(dO - dW) > 1.0f) {
                ++mismatch; ADD_FAILURE() << "distance differs at [" << r << "][" << c
                    << "] orig=" << dO << " new=" << dW;
            }
        }
    EXPECT_EQ(mismatch, 0);
    std::filesystem::remove(out);
}

// ------------------------------------------------------------
//  Legacy (alternate tagged) format — level 8 graph1.dat
//  These files have no magic (first byte 0x05) and use 1-byte tags.
// ------------------------------------------------------------
static std::string GraphPathLegacy() {
    return Utils::GetIGIRootPath() + "\\missions\\location0\\level8\\graphs\\graph1.dat";
}

TEST_F(GraphParserTest, LegacyFormatParsesValid) {
    GraphFile g = GRAPH_Parse(GraphPathLegacy());
    ASSERT_TRUE(g.valid) << "Legacy parse failed: " << g.error;
    EXPECT_TRUE(g.is_legacy);
    EXPECT_GT(g.nodes.size(), 0u);
    EXPECT_GT(g.edges.size(), 0u);
}

TEST_F(GraphParserTest, LegacyFormatNodeDataIsSane) {
    GraphFile g = GRAPH_Parse(GraphPathLegacy());
    ASSERT_TRUE(g.valid);
    for (const auto& n : g.nodes) {
        EXPECT_GE(n.id, 0);
        EXPECT_TRUE(std::isfinite(n.x));
        EXPECT_TRUE(std::isfinite(n.y));
        EXPECT_TRUE(std::isfinite(n.z));
    }
}

TEST_F(GraphParserTest, LegacyWriteRoundTrip) {
    GraphFile g = GRAPH_Parse(GraphPathLegacy());
    ASSERT_TRUE(g.valid);
    const std::string out = TempOut("igi_graph_legacy_rt.dat");
    ASSERT_TRUE(GRAPH_Write(GraphPathLegacy(), out, g));

    GraphFile r = GRAPH_Parse(out);
    ASSERT_TRUE(r.valid);
    EXPECT_TRUE(r.is_legacy);
    ASSERT_EQ(r.nodes.size(), g.nodes.size());
    ASSERT_EQ(r.edges.size(), g.edges.size());
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        EXPECT_EQ(r.nodes[i].id, g.nodes[i].id);
        EXPECT_DOUBLE_EQ(r.nodes[i].x, g.nodes[i].x);
        EXPECT_DOUBLE_EQ(r.nodes[i].y, g.nodes[i].y);
        EXPECT_DOUBLE_EQ(r.nodes[i].z, g.nodes[i].z);
        EXPECT_EQ(r.nodes[i].criteria, g.nodes[i].criteria);
    }
    std::filesystem::remove(out);
}

TEST_F(GraphParserTest, LegacySavePatchesPosition) {
    GraphFile g = GRAPH_Parse(GraphPathLegacy());
    ASSERT_TRUE(g.valid);
    ASSERT_GT(g.nodes.size(), 0u);
    GraphFile edited = g;
    edited.nodes[0].x += 500.0;

    const std::string out = TempOut("igi_graph_legacy_save.dat");
    ASSERT_TRUE(GRAPH_Save(GraphPathLegacy(), out, edited));

    GraphFile r = GRAPH_Parse(out);
    ASSERT_TRUE(r.valid);
    const GraphNode* rn = GRAPH_FindNode(r, g.nodes[0].id);
    ASSERT_NE(rn, nullptr);
    EXPECT_DOUBLE_EQ(rn->x, g.nodes[0].x + 500.0);
    std::filesystem::remove(out);
}

static GraphNode NodeWithCriteria(const std::string& c) {
    GraphNode n; n.id = 1; n.criteria = c; return n;
}

TEST(GraphNodeKindTest, DoorCriteriaIsDoor) {
    EXPECT_EQ(GRAPH_NodeKind(NodeWithCriteria("NODECRITERIA_DOOR")), GraphNodeKind::Door);
}
TEST(GraphNodeKindTest, ViewCriteriaIsView) {
    EXPECT_EQ(GRAPH_NodeKind(NodeWithCriteria("NODECRITERIA_VIEW")), GraphNodeKind::View);
}
TEST(GraphNodeKindTest, StairCriteriaIsStair) {
    EXPECT_EQ(GRAPH_NodeKind(NodeWithCriteria("NODECRITERIA_STAIR")), GraphNodeKind::Stair);
}
TEST(GraphNodeKindTest, EmptyCriteriaIsDefault) {
    EXPECT_EQ(GRAPH_NodeKind(NodeWithCriteria("")), GraphNodeKind::Default);
}
TEST(GraphNodeKindTest, UnknownCriteriaIsDefault) {
    EXPECT_EQ(GRAPH_NodeKind(NodeWithCriteria("SOMETHING_ELSE")), GraphNodeKind::Default);
}
TEST(GraphNodeKindTest, DoorTakesPrecedenceOverView) {
    EXPECT_EQ(GRAPH_NodeKind(NodeWithCriteria("DOOR_VIEW")), GraphNodeKind::Door);
}

// ------------------------------------------------------------
//  GRAPH_FindNode lookup (pure, no game file)
// ------------------------------------------------------------

TEST(GraphFindNodeTest, FindsExistingId) {
    GraphFile g;
    GraphNode a; a.id = 10; GraphNode b; b.id = 25;
    g.nodes = { a, b };
    const GraphNode* found = GRAPH_FindNode(g, 25);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->id, 25);
}
TEST(GraphFindNodeTest, ReturnsNullForMissingId) {
    GraphFile g;
    GraphNode a; a.id = 10;
    g.nodes = { a };
    EXPECT_EQ(GRAPH_FindNode(g, 999), nullptr);
}

TEST_F(GraphParserTest, SaveLeavesOtherNodesUnchanged) {
    GraphFile graph = GRAPH_Parse(GraphPathMulti());
    ASSERT_TRUE(graph.valid);
    ASSERT_GT(graph.nodes.size(), 1u);

    GraphFile edited = graph;
    edited.nodes[0].x += 999.0;

    const std::string out = TempOut("igi_graph_unchanged.dat");
    ASSERT_TRUE(GRAPH_Save(GraphPathMulti(), out, edited));

    GraphFile reloaded = GRAPH_Parse(out);
    ASSERT_TRUE(reloaded.valid);
    ASSERT_EQ(reloaded.nodes.size(), graph.nodes.size());

    // Node at index 1 (untouched) must be byte-identical in position.
    EXPECT_DOUBLE_EQ(reloaded.nodes[1].x, graph.nodes[1].x);
    EXPECT_DOUBLE_EQ(reloaded.nodes[1].y, graph.nodes[1].y);
    EXPECT_DOUBLE_EQ(reloaded.nodes[1].z, graph.nodes[1].z);

    std::filesystem::remove(out);
}
