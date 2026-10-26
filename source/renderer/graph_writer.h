/******************************************************************************
 * @file    graph_writer.h
 * @brief   IGI 1 navigation-graph (.dat) reader/writer.
 *
 * Transcribed from the retail engine (IGI-RE). Every layout detail, constant and
 * branch below cites its origin function + HOOK address + file:line:
 *
 *   GraphOpen               0x4F9FF0  src/graphnode/stream.c:196-237
 *   Graph_Read              0x4FA160  src/graphnode/stream.c:252-314
 *   GraphStream_ReadValue   0x4F9CD0  src/graphnode/stream.c:58-91
 *   GraphStream_WriteValue  0x4F9EB0  src/graphnode/stream.c:148-188
 *   GraphStream_Hash        0x4F9E70  src/graphnode/stream.c:121-135
 *   GraphStream_Find        0x4FA140  src/graphnode/stream.c:239-249
 *   GraphStream_Skip        0x4FA3B0  src/graphnode/stream.c:317-366
 *   GraphStream_PeekByte    0x4FAA10  src/graphtask/graph.c:66-76
 *   GraphStream_Read*       0x4FA7E0..0x4FA9D0  src/graphnode/stream.c:460-549
 *   GraphStream_Write*      0x4FA4E0..0x4FA7A0  src/graphnode/stream.c:368-458
 *   Graph_Open (handlers)   0x4FAA60  src/graphtask/graph.c:141-150
 *   Graph_ReadMaxNodes      0x4FAEE0  src/graphtask/graph.c:207-221
 *   Graph_ReadNodeID        0x4FAF50  src/graphtask/graph.c:223-233
 *   Graph_ReadNodePosition  0x4FAF80  src/graphtask/graph.c:235-239
 *   Graph_ReadNodeGamma     0x4FAFA0  src/graphtask/graph.c:241-245
 *   Graph_ReadNodeRadius    0x4FAFC0  src/graphtask/graph.c:247-251
 *   Graph_ReadNodeMaterial  0x4FAFE0  src/graphtask/graph.c:253-260
 *   Graph_ReadNodeCriteria  0x4FB030  src/graphtask/graph.c:262-270
 *   Graph_ReadLinkID1/2     0x4FB080/0x4FB0B0  src/graphtask/graph.c:272-285
 *   Graph_ReadLinkType      0x4FB0D0  src/graphtask/graph.c:287-293
 *   Graph_ParseParam        0x4FAE50  src/graphtask/graph.c:188-205
 *   AIGraph_Init            0x508FA0  src/sysmisc/aigraph.c:96-132
 *   AIGraph_ReadRouteTable  0x5092A0  src/sysmisc/aigraph.c:134-142
 *   AIGraph_AllocRoutes     0x5092E0  src/sysmisc/aigraph.c:144-160
 *   AIGraph_ClearRoutes     0x509340  src/sysmisc/aigraph.c:162-175
 *   AIGraph_GetRoute        0x509390  src/sysmisc/aigraph.c:177-181
 *   AIGraph_OnAlloc         0x5093B0  src/sysmisc/aigraph.c:183-187
 *   AIGraph_LoadRoutes      0x509450  src/sysmisc/aigraph.c:207-211
 *   AIGraph_ReadNode        0x509480  src/sysmisc/aigraph.c:213-229
 *   AIGraph_SaveRoutes      0x509520  src/sysmisc/aigraph.c:231-238
 *   Graph_AddNode           0x4F9510  src/graphnode/graph.c:262-295
 *   Graph_Grow              0x4F9620  src/graphnode/graph.c:298-316
 *   Graph_AddLink           0x4F9B80  src/graphnode/graph.c:453-472
 *
 * TWO on-disk layouts. GraphOpen :208 picks between them from the first four
 * bytes; the magic is GRAPH_MAGIC 0xFFEEDDCC (stream.c:24), stored little-endian
 * so the file starts CC DD EE FF.
 *
 * NEW layout (magic present, GraphOpen :209-228): a stream of NAMED records.
 * Each record is (GraphStream_WriteValue :148-188):
 *
 *     [04] [hash LE32]   tag TAG_UINT + the GraphStream_Hash of the name
 *     [00] [type]        tag TAG_UBYTE + the value's TAG_*
 *     [tag][payload]     the value itself, self-describing
 *
 * A hash with no registered handler is skipped as two values, not one
 * (GraphOpen :225 -> GraphStream_Skip(s, 2) :317-366). The engine's own
 * writer is not in the decompiled tree; the record order written here is the
 * order the reader's state machine requires, and it is the order every retail
 * file in this repository uses:
 *
 *     nMaxNodes, RouteTable,
 *     { nNodeID, tNodePosition, vNodeGamma, vNodeRadius, nNodeMaterial,
 *       nNodeCriteria } per node,
 *     { nLinkID1, nLinkID2, eLinkType } per link
 *
 * (nMaxNodes must precede RouteTable - AIGraph_ReadRouteTable sizes the blob
 * from capacity, aigraph.c:140 - and must precede every node - Graph_ReadMaxNodes
 * allocates the node array, graphtask/graph.c:216-219. Graph_ReadNodeCriteria
 * :268 and Graph_ReadLinkType :291 are what actually create the node/link, so
 * the five fields before the criteria and the two before the link type are
 * mandatory order.)
 *
 * OLD layout (no magic, GraphOpen :229-233 rewinds and calls Graph_Read):
 *
 *     int nodeCount, int capacity, int linkCount          (Graph_Read :258-260)
 *     per node:  int id, Vec3d pos, float gamma, float radius,
 *                [float unused]  only when the next tag is TAG_FLOAT (:281-282)
 *                [int material]  only when the next tag is TAG_INT   (:283-284)
 *                string criteria,
 *                <node-load-handler data>  (AIGraph_ReadNode :214-228)
 *     per link:  int a, int b, int cost                   (Graph_Read :300-307)
 *     <load-handler data>                                 (AIGraph_LoadRoutes :207-211)
 *
 * Every scalar in both layouts is a 1-byte tag followed by the payload
 * (stream.c:27-39): TAG_UBYTE 0, TAG_BYTE 1, TAG_USHORT 2, TAG_SHORT 3,
 * TAG_UINT 4, TAG_INT 5, TAG_FLOAT 6, TAG_DOUBLE 7, TAG_VEC3D 8, TAG_STRING 9,
 * TAG_BLOB 10. The readers never check the tag they consume.
 *****************************************************************************/

#pragma once
#include <string>
#include <vector>
#include <cstdint>

// Node criteria flags. NodeCriteria_Register names, registered by AIGraph_Init
// 0x508FA0 (src/sysmisc/aigraph.c:127-129); Graph_AddNode stores
// Script_EvalExpr(criteria) into GraphNode.flags (src/graphnode/graph.c:286).
inline constexpr int NODECRITERIA_DOOR  = 1;
inline constexpr int NODECRITERIA_VIEW  = 2;
inline constexpr int NODECRITERIA_STAIR = 4;

// stream.c:27-39
enum GraphStreamTag {
    TAG_UBYTE  = 0,
    TAG_BYTE   = 1,
    TAG_USHORT = 2,
    TAG_SHORT  = 3,
    TAG_UINT   = 4,
    TAG_INT    = 5,
    TAG_FLOAT  = 6,
    TAG_DOUBLE = 7,
    TAG_VEC3D  = 8,
    TAG_STRING = 9,
    TAG_BLOB   = 10,
};

// stream.c:24
inline constexpr uint32_t GRAPH_MAGIC = 0xFFEEDDCC;

struct GraphNode {
    // GraphNode (src/graphnode/graphnodetypes.h:59-71). id == -1 marks a free
    // slot (Graph_ClearNodes 0x4F96E0, src/graphnode/graph.c:326).
    int    id       = -1;
    double x        = 0.0;   // Vec3d pos, local to the graph task origin
    double y        = 0.0;
    double z        = 0.0;
    float  gamma    = 0.f;   // node->gamma
    float  radius   = 0.f;   // node->radius (diameter = radius + radius, :278)
    // The material INDEX from the file. The engine maps it with
    // Terrain_GetMaterial and stores NULL when the id is -1
    // (Graph_ReadNodeMaterial :255-258, Graph_Read :276-286).
    int    material = -1;
    std::string criteria;    // GraphNode.criteria, copied by strcpy (:283)

    // --- old-format round-trip state (Graph_Read only) ---------------------
    // Graph_Read reads an extra float only when the next tag is TAG_FLOAT and an
    // extra int only when it is TAG_INT (:281-284). Both are optional in the
    // file, so their presence is remembered to write the same bytes back.
    bool  has_unused_float = false;
    float unused_float     = 0.f;
    bool  has_material     = false;
    // Verbatim bytes of the node-load-handler data that follows the criteria
    // string in an old-format node record (AIGraph_ReadNode :214-228). The
    // engine discards the values, so they are preserved, never interpreted.
    std::vector<uint8_t> load_tail;
};

// GraphLink (src/graphnode/graphnodetypes.h:47-52); the third int is the link
// cost (Graph_AddLink 0x4F9B80, src/graphnode/graph.c:460-462).
struct GraphEdge {
    int node1     = -1;
    int node2     = -1;
    int link_type = 0;
};

// GraphStep (src/sysmisc/sysmisctypes.h:16-19): one cell of the precomputed
// route table. {-1, -1.0f} is the "no route" sentinel written by
// AIGraph_ClearRoutes (src/sysmisc/aigraph.c:170-171).
struct GraphRouteEntry {
    int   next = -1;
    float dist = -1.0f;
};

struct GraphFile {
    // The engine's Graph.capacity, i.e. the nMaxNodes record. Graph_GetNode
    // rejects any id >= capacity (src/graphnode/graph.c:334).
    int                     max_nodes = 0;
    std::vector<GraphNode>  nodes;
    std::vector<GraphEdge>  edges;
    // capacity * capacity cells, indexed [to * capacity + from]
    // (AIGraph_GetRoute 0x509390, src/sysmisc/aigraph.c:179). Empty when the
    // file carried no route table.
    std::vector<GraphRouteEntry> route_table;
    // True when the file carried a route table: the RouteTable record in the
    // new layout, or the load-event blob in the old one.
    bool                    has_route_table = false;
    bool                    valid = false;
    std::string             error;
    // True when the file had no magic, i.e. GraphOpen took its else branch and
    // called Graph_Read (stream.c:229-233).
    bool                    is_legacy = false;
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

// Query the precomputed route table. `from`/`to` are node ids in
// [0, max_nodes). Returns true and sets `next`/`dist` when a route exists,
// false for the {-1,-1} sentinel (self, unreachable, or absent table).
bool GRAPH_RouteStep(const GraphFile& graph, int from, int to,
                     int& next, float& dist);

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

// GraphOpen (0x4F9FF0, stream.c:196-237): read a graph .dat file.
// Filepath example: missions/location0/level1/graphs/graph1.dat
GraphFile GRAPH_Parse(const std::string& filepath);

// Graph_Read (0x4FA160, stream.c:252-314) over an in-memory buffer: the
// no-magic branch of GraphOpen. `data` is the file's first byte.
GraphFile GRAPH_ParseLegacy(const uint8_t* data, size_t size, const std::string& filepath);

// Save edited node positions back to disk.
// Re-reads the original file from `srcPath`, overwrites the X/Y/Z position of
// every node record whose id matches an entry in `graph.nodes`, then writes the
// result to `outPath` (may equal srcPath). All other data (criteria, gamma,
// radius, material, links, route table, old-format node tails) is preserved, so
// the file size never changes. Returns false on error.
bool GRAPH_Save(const std::string& srcPath, const std::string& outPath, const GraphFile& graph);

// Full serializer: regenerates every record from `graph` in the engine's own
// layout (the one GRAPH_Parse selected, `graph.is_legacy`), including the
// capacity record and the route table. Supports adding, removing and editing
// nodes/edges (position, criteria, gamma, radius, material, links). Returns
// false on error.
bool GRAPH_Write(const std::string& srcPath, const std::string& outPath, const GraphFile& graph);
