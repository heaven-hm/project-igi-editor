#include <gtest/gtest.h>
#include "parsers/graph_parser.h"
#include "utils.h"
#include <string>
#include <cmath>

// ============================================================
//  Graph Parser — graph1.dat structural tests
//
//  Path: <exe_dir>\missions\location0\level1\graph1.dat
// ============================================================

static std::string GraphPath() {
    return Utils::GetIGIRootPath() + "\\missions\\location0\\level1\\graph1.dat";
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
