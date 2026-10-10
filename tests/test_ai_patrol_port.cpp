// AI patrol port tests — verifies the OpenIGI AiPatrolRoute cursor and
// graph-route navigation (GoTo/Advance/TurnTowards/FaceNode) against a
// synthetic graph with a precomputed route table.
#include <gtest/gtest.h>
#include "../source/ai_system.h"
#include "../source/renderer/graph_writer.h"

using namespace igi;

namespace {

// Builds a small nav graph whose nodes sit on the +Y axis at 0, 1, 2, 3 metres
// and whose route table sends every destination straight down the list.
std::shared_ptr<GraphFile> MakeLineGraph(int nodeCount) {
    auto g = std::make_shared<GraphFile>();
    g->max_nodes = nodeCount;
    g->valid = true;
    for (int i = 0; i < nodeCount; ++i) {
        GraphNode n;
        n.id = i;
        n.x = 0.0;
        n.y = i * 4096.0;
        n.z = 0.0;
        n.radius = 100.0f;
        g->nodes.push_back(n);
    }
    // Route table: from + to*max. To the destination-major axis the table is
    // indexed; for a line graph, step one node closer to `to` per hop.
    g->route_table.resize((size_t)nodeCount * nodeCount, GraphRouteEntry{-1, -1.0f});
    for (int from = 0; from < nodeCount; ++from) {
        for (int to = 0; to < nodeCount; ++to) {
            if (from == to) continue;
            int next = from < to ? from + 1 : from - 1;
            g->route_table[(size_t)from + (size_t)to * nodeCount] = GraphRouteEntry{next, 1000.0f};
        }
    }
    return g;
}

} // namespace

TEST(AiPatrolPortTest, GroundImpactUsesAuthoredHearingRadius) {
    AiSystem nearby_ai;
    AiGuardEntity nearby_guard;
    nearby_guard.id = 1;
    nearby_guard.position = glm::vec3(15.0f * 4096.0f, 0.0f, 0.0f);
    nearby_guard.state = AiGuardState::Patrol;
    nearby_ai.RegisterGuard(nearby_guard);

    AiStimulusEvent nearby_impact;
    nearby_impact.type = AiEventType::GroundImpact;
    nearby_impact.position = glm::vec3(0.0f);
    nearby_impact.hearing_radius_units = 20.0f * 4096.0f;
    nearby_ai.GetEventQueue().Post(nearby_impact);
    nearby_ai.Update(1.0 / 30.0, glm::vec3(0.0f), false);

    ASSERT_EQ(nearby_ai.GetGuards().size(), 1U);
    EXPECT_EQ(nearby_ai.GetGuards()[0].state, AiGuardState::Suspicious);

    AiSystem distant_ai;
    AiGuardEntity distant_guard;
    distant_guard.id = 2;
    distant_guard.position = glm::vec3(25.0f * 4096.0f, 0.0f, 0.0f);
    distant_guard.state = AiGuardState::Patrol;
    distant_ai.RegisterGuard(distant_guard);

    AiStimulusEvent distant_impact = nearby_impact;
    distant_ai.GetEventQueue().Post(distant_impact);
    distant_ai.Update(1.0 / 30.0, glm::vec3(0.0f), false);

    ASSERT_EQ(distant_ai.GetGuards().size(), 1U);
    EXPECT_EQ(distant_ai.GetGuards()[0].state, AiGuardState::Patrol);
}

// WalkTo a node: the guard moves along the graph route until it reaches the
// destination node, then the cursor advances to the next command.
TEST(AiPatrolPortTest, WalkToReachesNodeAlongRoute) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 1;
    guard.position = glm::vec3(0.0f, 0.0f, 0.0f);
    guard.yaw = 0.0f;
    guard.graph = MakeLineGraph(4);
    guard.graph_offset = glm::vec3(0.0f);
    guard.current_node = 0;
    guard.patrol_commands = {
        AiPatrolCommand{ AiPatrolCommandKind::WalkTo, 3 },
    };
    ai.RegisterGuard(guard);

    // Run enough fixed 30 Hz ticks for a ~3 metre walk at ~1.6 m/s walk speed.
    // The guard advances node-by-node and finishes on the destination node.
    glm::vec3 last = guard.position;
    for (int i = 0; i < 120; ++i) {
        ai.Update(1.0 / 30.0, glm::vec3(0.0f, 100.0f, 0.0f), true);
    }

    const auto& g = ai.GetGuards()[0];
    EXPECT_EQ(g.state, AiGuardState::Patrol);
    EXPECT_EQ(g.current_node, 3);                          // arrived at destination node
    EXPECT_TRUE(g.route.empty());                          // no pending legs
    EXPECT_GT(g.position.y, 3.0f * 4096.0f - 2000.0f);     // reached ~3 m
    EXPECT_GT(g.position.y, last.y);                       // moved
}

// WalkTo sets walking gait and the guard advances toward the waypoint, rotating
// its yaw to face it as it goes.
TEST(AiPatrolPortTest, WalkToFacesAndMovesTowardWaypoint) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 1;
    guard.position = glm::vec3(0.0f, 0.0f, 0.0f);
    guard.yaw = 180.0f;  // facing -Y, opposite the target at +Y
    guard.graph = MakeLineGraph(4);
    guard.graph_offset = glm::vec3(0.0f);
    guard.current_node = 0;
    guard.patrol_commands = {
        AiPatrolCommand{ AiPatrolCommandKind::WalkTo, 3 },
    };
    ai.RegisterGuard(guard);

    ai.Update(1.0 / 30.0, glm::vec3(0.0f, 100.0f, 0.0f), true);
    ai.Update(1.0 / 30.0, glm::vec3(0.0f, 100.0f, 0.0f), true);

    const auto& g = ai.GetGuards()[0];
    EXPECT_TRUE(g.walking);
    EXPECT_GT(g.position.y, 0.0f);                          // moved toward +Y
    // Yaw rotated toward 0 (facing +Y) — no longer exactly 180.
    EXPECT_LT(std::abs(g.yaw), 180.0f);
}

// LookAtNode turns to face a node to within 3 degrees and then advances.
TEST(AiPatrolPortTest, LookAtNodeTurnsWithinTolerance) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 1;
    guard.position = glm::vec3(0.0f, 0.0f, 0.0f);
    guard.yaw = 90.0f;  // facing +X, target at +Y
    guard.graph = MakeLineGraph(4);
    guard.graph_offset = glm::vec3(0.0f);
    guard.current_node = 0;
    guard.patrol_commands = {
        AiPatrolCommand{ AiPatrolCommandKind::LookAtNode, 3 },
        AiPatrolCommand{ AiPatrolCommandKind::Quit, 0 },
    };
    ai.RegisterGuard(guard);

    // The aim yaw clamp allows only ~1.47 degrees of rotation per tick, so a
    // 90 degree turn needs about 61 ticks; run well past that plus one more
    // tick for the cursor to advance onto Quit and stop the patrol.
    for (int i = 0; i < 120; ++i) {
        ai.Update(1.0 / 30.0, glm::vec3(0.0f, 100.0f, 0.0f), true);
    }

    const auto& g = ai.GetGuards()[0];
    // Facing +Y (0 deg) within the 3 degree look-at tolerance.
    EXPECT_NEAR(std::remainder(g.yaw, 360.0f), 0.0f, 3.0f);
    EXPECT_TRUE(g.patrol_stopped);   // Quit consumed after the look finished
}

// Delay waits the authored tick count before advancing.
TEST(AiPatrolPortTest, DelayWaitsAuthoredTicks) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 1;
    guard.position = glm::vec3(0.0f, 0.0f, 0.0f);
    guard.yaw = 0.0f;
    guard.graph = MakeLineGraph(4);
    guard.graph_offset = glm::vec3(0.0f);
    guard.current_node = 0;
    guard.patrol_commands = {
        AiPatrolCommand{ AiPatrolCommandKind::Delay, 30 },   // 1 second at 30 Hz
        AiPatrolCommand{ AiPatrolCommandKind::WalkTo, 1 },
    };
    ai.RegisterGuard(guard);

    // 29 ticks: still on the Delay (30-tick deadline occupies 31 ticks).
    for (int i = 0; i < 29; ++i) {
        ai.Update(1.0 / 30.0, glm::vec3(0.0f, 100.0f, 0.0f), true);
    }
    {
        const auto& g = ai.GetGuards()[0];
        EXPECT_EQ(g.command_index, 0);
        EXPECT_EQ(g.position.y, 0.0f);   // never started walking
    }

    // A few more ticks: delay expires, the guard starts walking to node 1.
    for (int i = 0; i < 5; ++i) {
        ai.Update(1.0 / 30.0, glm::vec3(0.0f, 100.0f, 0.0f), true);
    }
    {
        const auto& g = ai.GetGuards()[0];
        EXPECT_EQ(g.command_index, 1);
        EXPECT_GT(g.position.y, 0.0f);
    }
}

// The End marker wraps the cursor once into the loop body and then stops:
// "the wrap happens once... a patrol runs its prologue, runs its loop body
// once more, and then repeats its final command" (AiPatrolRoute.cs, re A7.3).
TEST(AiPatrolPortTest, PathWrapsThroughEndMarkerOnceThenStops) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 1;
    guard.position = glm::vec3(0.0f, 0.0f, 0.0f);
    guard.yaw = 0.0f;
    guard.graph = MakeLineGraph(4);
    guard.graph_offset = glm::vec3(0.0f);
    guard.current_node = 0;
    guard.patrol_commands = {
        AiPatrolCommand{ AiPatrolCommandKind::WalkTo, 1 },   // prologue
        AiPatrolCommand{ AiPatrolCommandKind::End, 0 },      // wrap marker
        AiPatrolCommand{ AiPatrolCommandKind::WalkTo, 2 },   // loop body
    };
    ai.RegisterGuard(guard);

    // The guard walks to node 1, wraps through the End onto the body walk,
    // walks to node 2, and on the second list end stops with the cursor left
    // on that final command.
    for (int i = 0; i < 300; ++i) {
        ai.Update(1.0 / 30.0, glm::vec3(0.0f, 100.0f, 0.0f), true);
    }

    const auto& g = ai.GetGuards()[0];
    EXPECT_TRUE(g.patrol_stopped);        // second lap through End stops
    EXPECT_EQ(g.end_index, 1);            // the End marker was recorded
    EXPECT_EQ(g.command_index, 2);        // cursor rests on the final command
    EXPECT_EQ(g.current_node, 2);         // both legs were walked in order
}

// A guard with no patrol commands stands in place (OpenIGI idle).
TEST(AiPatrolPortTest, NoPatrolStandsStill) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 1;
    guard.position = glm::vec3(5.0f, 5.0f, 0.0f);
    guard.yaw = 0.0f;
    guard.graph = MakeLineGraph(4);
    guard.graph_offset = glm::vec3(0.0f);
    guard.current_node = 0;
    ai.RegisterGuard(guard);

    for (int i = 0; i < 60; ++i) {
        ai.Update(1.0 / 30.0, glm::vec3(0.0f, 100.0f, 0.0f), true);
    }

    const auto& g = ai.GetGuards()[0];
    EXPECT_EQ(g.position, glm::vec3(5.0f, 5.0f, 0.0f));
}

// Quit stops the patrol outright: the cursor stays on the Quit command.
TEST(AiPatrolPortTest, QuitStopsPatrol) {
    AiSystem ai;
    AiGuardEntity guard;
    guard.id = 1;
    guard.position = glm::vec3(0.0f, 0.0f, 0.0f);
    guard.yaw = 0.0f;
    guard.graph = MakeLineGraph(4);
    guard.graph_offset = glm::vec3(0.0f);
    guard.current_node = 0;
    guard.patrol_commands = {
        AiPatrolCommand{ AiPatrolCommandKind::WalkTo, 1 },
        AiPatrolCommand{ AiPatrolCommandKind::Quit, 0 },
    };
    ai.RegisterGuard(guard);

    for (int i = 0; i < 300; ++i) {
        ai.Update(1.0 / 30.0, glm::vec3(0.0f, 100.0f, 0.0f), true);
    }

    const auto& g = ai.GetGuards()[0];
    EXPECT_TRUE(g.patrol_stopped);
}
