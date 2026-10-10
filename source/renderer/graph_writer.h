/******************************************************************************
 * @file    graph_parser.h
 * @brief   Navigation graph (.dat) parser for I.G.I. 1
 *
 * Parses binary graph files (e.g. graph1.dat) that contain AI navigation
 * nodes and edges used by the game's pathfinding system.
 *****************************************************************************/

#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Node criteria flags (stored as string names in the file)
inline constexpr int NODECRITERIA_DOOR  = 1;
inline constexpr int NODECRITERIA_VIEW  = 2;
inline constexpr int NODECRITERIA_STAIR = 4;

struct GraphNode {
    int    id       = -1;
    double x        = 0.0;
    double y        = 0.0;
    double z        = 0.0;
    float  gamma    = 0.f;
    float  radius   = 0.f;
    int    material = 0;         // 0-23
    std::string criteria;        // e.g. "NODECRITERIA_VIEW"
    int    link1    = -1;        // connected node ID (-1 = none)
    int    link2    = -1;        // connected node ID (-1 = none)
    // Legacy (alternate tagged) format only — preserved verbatim on save.
    float  f3       = 0.5f;      // 3rd float after radius (purpose unknown)
    std::vector<int> legacy_link_targets;  // per-node link target IDs
    std::vector<int> legacy_link_types;    // per-node link types
    int    legacy_graph_link     = 0;      // 0 = no inter-graph link, 1 = has
    int    legacy_graph_link_tgt = 0;
    int    legacy_graph_link_typ = 0;
};

struct GraphEdge {
    int node1     = -1;
    int node2     = -1;
    int link_type = 0;
};

// One entry of the precomputed all-pairs route table (APSP). Mirrors the retail
// graph<N>.dat layout: { next node on the path from `from` to `to`, remaining
// cost }. {-1, -1.0f} is the sentinel meaning "no route".
struct GraphRouteEntry {
    int   next = -1;
    float cost = -1.0f;
};

struct GraphFile {
    int                     max_nodes = 0;
    std::vector<GraphNode>  nodes;
    std::vector<GraphEdge>  edges;
    // max_nodes^2 entries, indexed from + to*max_nodes (destination major axis),
    // exactly as the game walks it. Empty for legacy-format graphs.
    std::vector<GraphRouteEntry> route_table;
    bool                    valid = false;
    std::string             error;
    bool                    is_legacy = false;  // alternate tagged format (no magic)
};

// Visual/semantic classification of a node, derived from its criteria string.
// Precedence when multiple keywords are present: Door > Stair > View > Default.
enum class GraphNodeKind { Default, Door, View, Stair };

// Classify a node by its criteria text (case-sensitive substring match,
// e.g. "NODECRITERIA_DOOR" -> Door). Empty/unknown criteria -> Default.
GraphNodeKind GRAPH_NodeKind(const GraphNode& node);

// Find a node by id. Returns nullptr if absent.
GraphNode*       GRAPH_FindNode(GraphFile& graph, int id);
const GraphNode* GRAPH_FindNode(const GraphFile& graph, int id);

// Query the precomputed route table. `from`/`to` are node ids in [0, max_nodes).
// Returns true and sets `next`/`cost` when a route exists, false for the
// {-1,-1} sentinel (self, unreachable, or absent table).
bool GRAPH_RouteStep(const GraphFile& graph, int from, int to,
                     int& next, float& cost);

// Walk the route table from `from` to `to`, yielding every node along the way
// (destination last; `from` not included). Empty when no route exists. Bounded
// by max_nodes steps so a malformed table cannot hang the caller.
std::vector<int> GRAPH_EnumerateRoute(const GraphFile& graph, int from, int to);

// Result of a nearest-node query (GRAPH_NearestNode).
enum class GraphNearestResult { None, Outside, Inside };

// Find the graph node nearest a world position (retail 0x5096F0): linear scan
// scoring distance - radius*4096, skipping VIEW-criteria nodes, keeping the
// best three insertion-sorted. Returns the best node id, or -1 when none
// qualifies.
int GRAPH_NearestNode(const GraphFile& graph, double x, double y, double z,
                      GraphNearestResult* out_result = nullptr);

// Parse a navigation graph .dat file.
// Filepath example: missions/location0/level1/graphs/graph1.dat
GraphFile GRAPH_Parse(const std::string& filepath);

// Parse the legacy alternate tagged format (no magic; 1-byte type tags).
// Called internally by GRAPH_Parse when the standard magic is absent.
GraphFile GRAPH_ParseLegacy(const uint8_t* data, size_t size, const std::string& filepath);

// Save edited node positions back to disk.
// Re-reads the original bytes from `srcPath`, overwrites the X/Y/Z position of
// every node record whose id matches an entry in `graph.nodes`, then writes the
// result to `outPath` (may equal srcPath). All other bytes (criteria, gamma,
// radius, material, edges, adjacency table) are preserved verbatim, so the file
// size never changes. Returns false on error.
bool GRAPH_Save(const std::string& srcPath, const std::string& outPath, const GraphFile& graph);

// Full serializer: preserves the original header + adjacency table from srcPath,
// then regenerates ALL node and edge tagged records from `graph`. Unlike
// GRAPH_Save (which only patches positions in place), this supports adding,
// removing, and editing nodes/edges (position, criteria, gamma, radius,
// material, links). Returns false on error.
bool GRAPH_Write(const std::string& srcPath, const std::string& outPath, const GraphFile& graph);
