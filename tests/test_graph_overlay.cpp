#include <gtest/gtest.h>
#include "renderer/graph_overlay.h"
#include <glm/glm.hpp>

// ============================================================
//  GraphWorldToScreen — pure projection math
// ============================================================

TEST(GraphWorldToScreenTest, OriginProjectsToViewportCenter) {
    glm::vec2 out{};
    bool vis = GraphWorldToScreen(glm::mat4(1.0f), glm::vec3(0.0f), 800.0f, 600.0f, out);
    ASSERT_TRUE(vis);
    EXPECT_FLOAT_EQ(out.x, 400.0f);
    EXPECT_FLOAT_EQ(out.y, 300.0f);
}

TEST(GraphWorldToScreenTest, NdcTopRightMapsToScreenTopRight) {
    // With identity view-proj, world (1,1,0) is NDC (1,1) -> top-right pixel.
    glm::vec2 out{};
    bool vis = GraphWorldToScreen(glm::mat4(1.0f), glm::vec3(1.0f, 1.0f, 0.0f), 800.0f, 600.0f, out);
    ASSERT_TRUE(vis);
    EXPECT_FLOAT_EQ(out.x, 800.0f);   // x: (1*0.5+0.5)*800
    EXPECT_FLOAT_EQ(out.y, 0.0f);     // y is flipped: (-1*0.5+0.5)*600
}

TEST(GraphWorldToScreenTest, ReturnsFalseWhenBehindCamera) {
    // Build a matrix whose clip.w becomes negative for z=+1.
    glm::mat4 m(1.0f);
    m[2][3] = -1.0f;  // column 2, row 3 -> clip.w gets -z
    m[3][3] = 0.0f;
    glm::vec2 out{};
    bool vis = GraphWorldToScreen(m, glm::vec3(0.0f, 0.0f, 1.0f), 800.0f, 600.0f, out);
    EXPECT_FALSE(vis);
}

// ============================================================
//  GRAPH_PickNode — nearest-node screen-space hit test
// ============================================================

static GraphNode NodeAt(int id, double x, double y, double z) {
    GraphNode n; n.id = id; n.x = x; n.y = y; n.z = z; return n;
}

TEST(GraphPickNodeTest, PicksNodeUnderCursor) {
    GraphFile g;
    g.nodes = { NodeAt(7, 0.0, 0.0, 0.0) };  // identity -> screen (400,300)
    int id = GRAPH_PickNode(g, glm::mat4(1.0f), 400.0f, 300.0f, 800.0f, 600.0f, 20.0f);
    EXPECT_EQ(id, 7);
}

TEST(GraphPickNodeTest, ReturnsMinusOneWhenNoneWithinThreshold) {
    GraphFile g;
    g.nodes = { NodeAt(7, 0.0, 0.0, 0.0) };  // screen (400,300)
    int id = GRAPH_PickNode(g, glm::mat4(1.0f), 700.0f, 300.0f, 800.0f, 600.0f, 20.0f);
    EXPECT_EQ(id, -1);
}

TEST(GraphPickNodeTest, PicksNearestOfMultiple) {
    GraphFile g;
    g.nodes = {
        NodeAt(1, 0.0, 0.0, 0.0),   // screen (400,300)
        NodeAt(2, 1.0, 1.0, 0.0),   // screen (800,0)
    };
    int near2 = GRAPH_PickNode(g, glm::mat4(1.0f), 790.0f, 10.0f, 800.0f, 600.0f, 30.0f);
    EXPECT_EQ(near2, 2);
    int near1 = GRAPH_PickNode(g, glm::mat4(1.0f), 405.0f, 305.0f, 800.0f, 600.0f, 30.0f);
    EXPECT_EQ(near1, 1);
}

TEST(GraphPickNodeTest, IgnoresNodesBehindCamera) {
    glm::mat4 m(1.0f);
    m[2][3] = -1.0f; m[3][3] = 0.0f;   // z=+1 -> behind
    GraphFile g;
    g.nodes = { NodeAt(5, 0.0, 0.0, 1.0) };
    int id = GRAPH_PickNode(g, m, 400.0f, 300.0f, 800.0f, 600.0f, 50.0f);
    EXPECT_EQ(id, -1);
}

// ============================================================
//  GRAPH_ToggleCriteriaFlag — the properties panel's criteria toggles
//
//  The criteria field is an EXPRESSION the engine evaluates into a bitmask
//  (Graph_AddNode: n->flags = Script_EvalExpr(n->criteria), 0x4F9510,
//  src/graphnode/graph.c:286) and Expr_EvalInt implements bitwise OR
//  (OP_OR, src/script/expr.c:230), so the three registered constants
//  (AIGraph_Init 0x508FA0, src/sysmisc/aigraph.c:127-129) compose. Toggling
//  must add/remove ONE flag and leave the others in place.
//
//  Lives in this suite (not test_graph_parser.cpp) because this file is
//  host-portable, so the same tests run in both the native host lane and the
//  Windows igi_tests binary.
// ============================================================

TEST(GraphCriteriaFlagTest, AddsFlagToEmptyCriteria) {
    EXPECT_EQ(GRAPH_ToggleCriteriaFlag("", "NODECRITERIA_DOOR"),
              "NODECRITERIA_DOOR");
}
TEST(GraphCriteriaFlagTest, RemovesFlagWhenAlreadyPresent) {
    EXPECT_EQ(GRAPH_ToggleCriteriaFlag("NODECRITERIA_DOOR", "NODECRITERIA_DOOR"),
              "");
}
TEST(GraphCriteriaFlagTest, ComposesTwoFlagsInsteadOfReplacing) {
    // The regression this guards: the old panel toggle overwrote the string,
    // so a node could never carry two criteria even though the engine ORs them.
    EXPECT_EQ(GRAPH_ToggleCriteriaFlag("NODECRITERIA_DOOR", "NODECRITERIA_VIEW"),
              "NODECRITERIA_DOOR|NODECRITERIA_VIEW");
}
TEST(GraphCriteriaFlagTest, RemovesOneFlagAndKeepsTheOther) {
    EXPECT_EQ(GRAPH_ToggleCriteriaFlag("NODECRITERIA_DOOR|NODECRITERIA_VIEW",
                                       "NODECRITERIA_DOOR"),
              "NODECRITERIA_VIEW");
}
TEST(GraphCriteriaFlagTest, ComposesAllThreeRegisteredFlags) {
    std::string c = GRAPH_ToggleCriteriaFlag("", "NODECRITERIA_DOOR");
    c = GRAPH_ToggleCriteriaFlag(c, "NODECRITERIA_VIEW");
    c = GRAPH_ToggleCriteriaFlag(c, "NODECRITERIA_STAIR");
    EXPECT_EQ(c, "NODECRITERIA_DOOR|NODECRITERIA_VIEW|NODECRITERIA_STAIR");
    // Toggling one back off leaves the other two intact.
    c = GRAPH_ToggleCriteriaFlag(c, "NODECRITERIA_VIEW");
    EXPECT_EQ(c, "NODECRITERIA_DOOR|NODECRITERIA_STAIR");
}
TEST(GraphCriteriaFlagTest, TrimsBlanksAndDropsEmptyFields) {
    EXPECT_EQ(GRAPH_ToggleCriteriaFlag("  NODECRITERIA_DOOR | ", "NODECRITERIA_VIEW"),
              "NODECRITERIA_DOOR|NODECRITERIA_VIEW");
}
TEST(GraphCriteriaFlagTest, EmptyFlagNameIsANoOp) {
    EXPECT_EQ(GRAPH_ToggleCriteriaFlag("NODECRITERIA_DOOR", ""),
              "NODECRITERIA_DOOR");
}
TEST(GraphCriteriaFlagTest, ComposedCriteriaStillClassifies) {
    // GRAPH_NodeKind matches substrings, so a composed string must still be
    // classified -- Door keeps precedence over Stair (docs/graph_editor.md 3).
    const std::string c = GRAPH_ToggleCriteriaFlag("NODECRITERIA_STAIR",
                                                   "NODECRITERIA_DOOR");
    EXPECT_EQ(c, "NODECRITERIA_STAIR|NODECRITERIA_DOOR");
    GraphNode n; n.id = 1; n.criteria = c;
    EXPECT_EQ(GRAPH_NodeKind(n), GraphNodeKind::Door);
}
