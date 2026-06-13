/******************************************************************************
 * @file    graph_parser.cpp
 * @brief   Navigation graph (.dat) parser for I.G.I. 1
 *
 * Binary format (confirmed by reverse engineering):
 *
 *   [0-3]   Magic: CC DD EE FF
 *   [4-15]  MaxNodes record  (12 bytes: sig 0x04E6 + 2-byte tag + 4-byte unk + int32 value)
 *   [16-27] Second header record (12 bytes, sig 0x040D, contents unused)
 *   [28-29] 2 padding bytes
 *   [30 ... 30 + MaxNodes*MaxNodes*8 - 1]
 *           Adjacency table: MaxNodes x MaxNodes entries of (int32 nodeRef, float32 dist),
 *           each entry 8 bytes; -1 / -1.0f indicates no connection.
 *
 *   After the adjacency table: tagged node records, one per active node:
 *     NodeID       (0x04CE) - int32
 *     NodePosition (0x0495) - double x3 (24 bytes)
 *     NodeGamma    (0x049C) - float32
 *     NodeRadius   (0x0423) - float32
 *     NodeMaterial (0x0429) - int32
 *     NodeCriteria (0x04E5) - pascal string: 1 length byte then <length> bytes
 *                             (the last byte in <length> is a null terminator;
 *                              empty criteria has length==1 and the byte is 0x00)
 *
 *   After all node records: tagged edge records, one per edge:
 *     EdgeLink1    (0x044A) - int32 (node ID)
 *     EdgeLink2    (0x04F6) - int32 (node ID)
 *     LinkType     (0x0423) - int32
 *
 *   Each tagged record header is 8 bytes:
 *     [0-1] signature (big-endian uint16)
 *     [2-3] sub-tag   (2 bytes, not used by us)
 *     [4-7] unknown   (4 bytes, not used by us)
 *   Data payload begins at header_offset + 8.
 *****************************************************************************/

#include "graph_parser.h"
#include "../logger.h"

#include <fstream>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Signatures (read as big-endian uint16 from the file)
// ---------------------------------------------------------------------------
static constexpr uint16_t SIG_MAX_NODES    = 0x04E6;
static constexpr uint16_t SIG_NODE_ID      = 0x04CE;
static constexpr uint16_t SIG_NODE_POS     = 0x0495;
static constexpr uint16_t SIG_NODE_GAMMA   = 0x049C;
static constexpr uint16_t SIG_NODE_RADIUS  = 0x0423;
static constexpr uint16_t SIG_NODE_MAT     = 0x0429;
static constexpr uint16_t SIG_NODE_CRIT    = 0x04E5;
static constexpr uint16_t SIG_EDGE_LINK1   = 0x044A;
static constexpr uint16_t SIG_EDGE_LINK2   = 0x04F6;
// LinkType reuses SIG_NODE_RADIUS (0x0423) with a different sub-tag

static constexpr uint32_t GRAPH_MAGIC = 0xFFEEDDCC; // CC DD EE FF in file (LE read)

// ---------------------------------------------------------------------------
// Low-level read helpers
// ---------------------------------------------------------------------------

template <typename T>
static T ReadLE(const uint8_t* p) {
    T val;
    std::memcpy(&val, p, sizeof(T));
    return val;
}

static uint16_t ReadSigBE(const uint8_t* p) {
    // Signature is stored as two consecutive bytes; read big-endian.
    return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}

// ---------------------------------------------------------------------------
// Bounds-checked accessors
// ---------------------------------------------------------------------------

struct Buffer {
    const uint8_t* data;
    size_t         size;

    bool check(size_t offset, size_t needed) const {
        return offset + needed <= size;
    }

    uint16_t sig(size_t offset) const {
        return ReadSigBE(data + offset);
    }

    template <typename T>
    T read(size_t offset) const {
        return ReadLE<T>(data + offset);
    }

    // Read payload at [offset + 8] (standard record layout)
    template <typename T>
    T payload(size_t offset) const {
        return ReadLE<T>(data + offset + 8);
    }
};

// ---------------------------------------------------------------------------
// GRAPH_Parse
// ---------------------------------------------------------------------------

GraphNodeKind GRAPH_NodeKind(const GraphNode& node) {
    const std::string& c = node.criteria;
    if (c.find("DOOR")  != std::string::npos) return GraphNodeKind::Door;
    if (c.find("STAIR") != std::string::npos) return GraphNodeKind::Stair;
    if (c.find("VIEW")  != std::string::npos) return GraphNodeKind::View;
    return GraphNodeKind::Default;
}

GraphNode* GRAPH_FindNode(GraphFile& graph, int id) {
    for (GraphNode& n : graph.nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const GraphNode* GRAPH_FindNode(const GraphFile& graph, int id) {
    for (const GraphNode& n : graph.nodes)
        if (n.id == id) return &n;
    return nullptr;
}

// Record codeids (uint32 LE) and typeids, matching the bytes GRAPH_Parse reads.
namespace {
    constexpr uint32_t CODE_NODE_ID    = 0x0735CE04;
    constexpr uint32_t CODE_NODE_POS   = 0x1D429504;
    constexpr uint32_t CODE_NODE_GAMMA = 0x0F7E9C04;
    constexpr uint32_t CODE_NODE_RAD   = 0x14302304;
    constexpr uint32_t CODE_NODE_MAT   = 0x1BB62904;
    constexpr uint32_t CODE_NODE_CRIT  = 0x1BD3E504;
    constexpr uint32_t CODE_LINK_A     = 0x09104A04;
    constexpr uint32_t CODE_LINK_B     = 0x0918F604;
    constexpr uint32_t CODE_LINK_TYPE  = 0x0DA92304;
    constexpr uint32_t TYPE_U32 = 0x05050000;
    constexpr uint32_t TYPE_F32 = 0x06060000;
    constexpr uint32_t TYPE_F6  = 0x08080000;
    constexpr uint32_t TYPE_BSTR = 0x09090000;

    void PutU32(std::vector<uint8_t>& b, uint32_t v) {
        b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8));
        b.push_back((uint8_t)(v >> 16)); b.push_back((uint8_t)(v >> 24));
    }
    void PutF32(std::vector<uint8_t>& b, float f) {
        uint32_t v; std::memcpy(&v, &f, 4); PutU32(b, v);
    }
    void PutF64(std::vector<uint8_t>& b, double d) {
        uint64_t v; std::memcpy(&v, &d, 8);
        for (int i = 0; i < 8; ++i) b.push_back((uint8_t)(v >> (i * 8)));
    }
    // codeid + typeid(UInt32) + int32 value (12 bytes).
    void PutU32Rec(std::vector<uint8_t>& b, uint32_t code, int32_t val) {
        PutU32(b, code); PutU32(b, TYPE_U32); PutU32(b, (uint32_t)val);
    }
    void PutF32Rec(std::vector<uint8_t>& b, uint32_t code, float val) {
        PutU32(b, code); PutU32(b, TYPE_F32); PutF32(b, val);
    }
}

bool GRAPH_Write(const std::string& srcPath, const std::string& outPath,
                 const GraphFile& graph) {
    std::ifstream file(srcPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] Write: cannot open source: " + srcPath);
        return false;
    }
    const size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0);
    constexpr size_t HEADER_SIZE = 30;
    if (fileSize < HEADER_SIZE) return false;
    std::vector<uint8_t> src(fileSize);
    if (!file.read(reinterpret_cast<char*>(src.data()), static_cast<std::streamsize>(fileSize)))
        return false;
    file.close();

    Buffer b{ src.data(), fileSize };
    if (b.read<uint32_t>(0) != GRAPH_MAGIC) return false;
    if (b.sig(4) != SIG_MAX_NODES)          return false;
    const int maxNodes = b.payload<int32_t>(4);
    if (maxNodes <= 0 || maxNodes > 4096)   return false;

    const size_t adjBytes = static_cast<size_t>(maxNodes) * maxNodes * 8;
    const size_t nodeDataStart = HEADER_SIZE + adjBytes;
    if (nodeDataStart > fileSize) return false;

    // Preserve the header verbatim; regenerate the adjacency table below.
    std::vector<uint8_t> out(src.begin(), src.begin() + HEADER_SIZE);

    // --- Regenerate the adjacency / all-pairs shortest-path routing table. ---
    // The game stores, per node-id pair (a,b): the PREDECESSOR of b on the
    // shortest path a->b (ref) and the total path distance (dist); (-1,-1) for
    // self / unreachable. Edge weight = 3D euclidean distance between node
    // positions. Computed with Floyd-Warshall over the present node ids so that
    // adding/removing nodes or moving them keeps the routing table consistent
    // (a stale table with dangling refs crashes the game).
    {
        std::vector<uint8_t> adj(adjBytes);
        const int32_t kNeg = -1; const float kNegF = -1.0f;
        for (size_t k = 0; k < adjBytes / 8; ++k) {
            std::memcpy(&adj[k * 8],     &kNeg,  4);
            std::memcpy(&adj[k * 8 + 4], &kNegF, 4);
        }

        std::vector<int> ids;
        std::vector<const GraphNode*> np;
        for (const GraphNode& n : graph.nodes)
            if (n.id > 0 && n.id < maxNodes) { ids.push_back(n.id); np.push_back(&n); }
        const int n = static_cast<int>(ids.size());
        std::unordered_map<int, int> idx;
        for (int i = 0; i < n; ++i) idx[ids[i]] = i;

        const double INF = 1e30;
        std::vector<double> dist(static_cast<size_t>(n) * n, INF);
        std::vector<int>    pred(static_cast<size_t>(n) * n, -1);
        for (int i = 0; i < n; ++i) dist[static_cast<size_t>(i) * n + i] = 0.0;

        for (const GraphEdge& e : graph.edges) {
            auto a = idx.find(e.node1), b = idx.find(e.node2);
            if (a == idx.end() || b == idx.end() || a->second == b->second) continue;
            const int ia = a->second, ib = b->second;
            const double dx = np[ia]->x - np[ib]->x, dy = np[ia]->y - np[ib]->y,
                         dz = np[ia]->z - np[ib]->z;
            const double w = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (w < dist[static_cast<size_t>(ia) * n + ib]) {
                dist[static_cast<size_t>(ia) * n + ib] = w; pred[static_cast<size_t>(ia) * n + ib] = ids[ia];
                dist[static_cast<size_t>(ib) * n + ia] = w; pred[static_cast<size_t>(ib) * n + ia] = ids[ib];
            }
        }

        for (int k = 0; k < n; ++k)
            for (int i = 0; i < n; ++i) {
                const double dik = dist[static_cast<size_t>(i) * n + k];
                if (dik >= INF) continue;
                for (int j = 0; j < n; ++j) {
                    const double nd = dik + dist[static_cast<size_t>(k) * n + j];
                    if (nd < dist[static_cast<size_t>(i) * n + j]) {
                        dist[static_cast<size_t>(i) * n + j] = nd;
                        pred[static_cast<size_t>(i) * n + j] = pred[static_cast<size_t>(k) * n + j];
                    }
                }
            }

        for (int i = 0; i < n; ++i)
            for (int j = 0; j < n; ++j) {
                if (i == j) continue;
                const double dd = dist[static_cast<size_t>(i) * n + j];
                if (dd >= INF) continue;
                const size_t off = (static_cast<size_t>(ids[i]) * maxNodes + ids[j]) * 8;
                const int32_t ref = pred[static_cast<size_t>(i) * n + j];
                const float   fdd = static_cast<float>(dd);
                std::memcpy(&adj[off],     &ref, 4);
                std::memcpy(&adj[off + 4], &fdd, 4);
            }

        out.insert(out.end(), adj.begin(), adj.end());
    }

    for (const GraphNode& n : graph.nodes) {
        PutU32Rec(out, CODE_NODE_ID, n.id);
        PutU32(out, CODE_NODE_POS); PutU32(out, TYPE_F6);
        PutF64(out, n.x); PutF64(out, n.y); PutF64(out, n.z);
        PutF32Rec(out, CODE_NODE_GAMMA, n.gamma);
        PutF32Rec(out, CODE_NODE_RAD,   n.radius);
        PutU32Rec(out, CODE_NODE_MAT,   n.material);
        // Criteria: codeid + typeid(BSTR) + length byte + chars + null.
        PutU32(out, CODE_NODE_CRIT); PutU32(out, TYPE_BSTR);
        std::string crit = n.criteria;
        if (crit.size() > 254) crit.resize(254);
        out.push_back((uint8_t)(crit.size() + 1));   // length includes the null
        out.insert(out.end(), crit.begin(), crit.end());
        out.push_back(0);
    }

    for (const GraphEdge& e : graph.edges) {
        PutU32Rec(out, CODE_LINK_A,    e.node1);
        PutU32Rec(out, CODE_LINK_B,    e.node2);
        PutU32Rec(out, CODE_LINK_TYPE, e.link_type);
    }

    std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] Write: cannot open output: " + outPath);
        return false;
    }
    f.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    if (!f) return false;
    Logger::Get().Log(LogLevel::INFO, "[GRAPH] Wrote " + std::to_string(graph.nodes.size()) +
        " nodes, " + std::to_string(graph.edges.size()) + " edges to: " + outPath);
    return true;
}

// ---------------------------------------------------------------------------
// GRAPH_Save — overwrite node positions in-place, preserving everything else.
// Walks the tagged node records the same way GRAPH_Parse does and patches the
// 3 position doubles of any node whose id appears in `graph.nodes`.
// ---------------------------------------------------------------------------
bool GRAPH_Save(const std::string& srcPath, const std::string& outPath,
                const GraphFile& graph) {
    std::ifstream file(srcPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] Save: cannot open source: " + srcPath);
        return false;
    }
    const size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0);

    constexpr size_t HEADER_SIZE = 30;
    if (fileSize < HEADER_SIZE) return false;

    std::vector<uint8_t> buf(fileSize);
    if (!file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(fileSize)))
        return false;
    file.close();

    Buffer b{ buf.data(), fileSize };

    if (b.read<uint32_t>(0) != GRAPH_MAGIC)   return false;
    if (b.sig(4) != SIG_MAX_NODES)            return false;
    const int maxNodes = b.payload<int32_t>(4);
    if (maxNodes <= 0 || maxNodes > 4096)     return false;

    const size_t adjTableBytes  = static_cast<size_t>(maxNodes) * maxNodes * 8;
    const size_t nodeDataStart  = HEADER_SIZE + adjTableBytes;
    if (nodeDataStart >= fileSize)            return false;

    // Fast lookup of edited positions by node id.
    auto findEdited = [&graph](int id) -> const GraphNode* {
        for (const GraphNode& n : graph.nodes)
            if (n.id == id) return &n;
        return nullptr;
    };

    auto writeDoubleLE = [&buf](size_t off, double v) {
        std::memcpy(buf.data() + off, &v, sizeof(double));
    };

    // Walk node records exactly like GRAPH_Parse, patching positions.
    size_t offset = nodeDataStart;
    while (offset + 2 <= fileSize && b.sig(offset) == SIG_NODE_ID) {
        if (!b.check(offset, 12)) break;
        const int id = b.payload<int32_t>(offset);
        offset += 12;

        if (!b.check(offset, 32) || b.sig(offset) != SIG_NODE_POS) break;
        if (const GraphNode* edited = findEdited(id)) {
            writeDoubleLE(offset + 8,  edited->x);
            writeDoubleLE(offset + 16, edited->y);
            writeDoubleLE(offset + 24, edited->z);
        }
        offset += 32;

        if (!b.check(offset, 12) || b.sig(offset) != SIG_NODE_GAMMA)  break;
        offset += 12;
        if (!b.check(offset, 12) || b.sig(offset) != SIG_NODE_RADIUS) break;
        offset += 12;
        if (!b.check(offset, 12) || b.sig(offset) != SIG_NODE_MAT)    break;
        offset += 12;
        if (!b.check(offset, 9)  || b.sig(offset) != SIG_NODE_CRIT)   break;
        const uint8_t strLen = buf[offset + 8];
        const size_t totalCrit = 8 + 1 + static_cast<size_t>(strLen);
        if (!b.check(offset, totalCrit)) break;
        offset += totalCrit;
    }

    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) {
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] Save: cannot open output: " + outPath);
        return false;
    }
    out.write(reinterpret_cast<const char*>(buf.data()), static_cast<std::streamsize>(fileSize));
    if (!out) return false;

    Logger::Get().Log(LogLevel::INFO,
        "[GRAPH] Saved " + std::to_string(graph.nodes.size()) + " node positions to: " + outPath);
    return true;
}

GraphFile GRAPH_Parse(const std::string& filepath) {
    GraphFile result;

    // ------------------------------------------------------------------
    // 1. Load file
    // ------------------------------------------------------------------
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        result.error = "Cannot open file: " + filepath;
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] " + result.error);
        return result;
    }

    const size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0);

    // Minimum viable header: magic(4) + MaxNodes record(12) + second record(12) + pad(2) = 30
    constexpr size_t HEADER_SIZE = 30;
    if (fileSize < HEADER_SIZE) {
        result.error = "File too small to be a valid graph: " + filepath;
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] " + result.error);
        return result;
    }

    std::vector<uint8_t> buf(fileSize);
    if (!file.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(fileSize))) {
        result.error = "Read error on file: " + filepath;
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] " + result.error);
        return result;
    }

    Buffer b{ buf.data(), fileSize };

    // ------------------------------------------------------------------
    // 2. Validate magic
    // ------------------------------------------------------------------
    const uint32_t magic = b.read<uint32_t>(0);
    if (magic != GRAPH_MAGIC) {
        result.error = "Bad magic in graph file: " + filepath;
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] " + result.error);
        return result;
    }

    // ------------------------------------------------------------------
    // 3. MaxNodes record at offset 4
    //    Signature: 0x04E6 at [4-5], value (int32 LE) at [4+8] = [12]
    // ------------------------------------------------------------------
    if (b.sig(4) != SIG_MAX_NODES) {
        result.error = "Missing MaxNodes signature at offset 4: " + filepath;
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] " + result.error);
        return result;
    }

    const int maxNodes = b.payload<int32_t>(4);
    if (maxNodes <= 0 || maxNodes > 4096) {
        result.error = "Implausible MaxNodes value " + std::to_string(maxNodes) + " in: " + filepath;
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] " + result.error);
        return result;
    }
    result.max_nodes = maxNodes;

    // ------------------------------------------------------------------
    // 4. Skip adjacency table
    //    Starts at offset 30, size = MaxNodes * MaxNodes * 8 bytes
    // ------------------------------------------------------------------
    const size_t adjTableBytes = static_cast<size_t>(maxNodes) * maxNodes * 8;
    const size_t nodeDataStart = HEADER_SIZE + adjTableBytes;

    if (nodeDataStart >= fileSize) {
        result.error = "Adjacency table overruns file size in: " + filepath;
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] " + result.error);
        return result;
    }

    Logger::Get().Log(LogLevel::INFO,
        "[GRAPH] Parsing " + filepath +
        " | MaxNodes=" + std::to_string(maxNodes) +
        " | AdjTable=" + std::to_string(adjTableBytes) + " bytes" +
        " | NodeData@" + std::to_string(nodeDataStart));

    // ------------------------------------------------------------------
    // 5. Parse node records
    //    Each node: NodeID(12B) + NodePos(32B) + NodeGamma(12B) +
    //               NodeRadius(12B) + NodeMaterial(12B) + NodeCriteria(8+1+L B)
    // ------------------------------------------------------------------
    size_t offset = nodeDataStart;

    while (offset + 2 <= fileSize) {
        uint16_t s = b.sig(offset);
        if (s != SIG_NODE_ID) break;  // No more node records; move on to edges

        GraphNode node;

        // NodeID  ---------------------------------------------------
        if (!b.check(offset, 12)) break;
        node.id = b.payload<int32_t>(offset);
        offset += 12;

        // NodePosition (3 doubles = 24 bytes of payload) ------------
        if (!b.check(offset, 32)) break;
        if (b.sig(offset) != SIG_NODE_POS) {
            Logger::Get().Log(LogLevel::WARNING,
                "[GRAPH] Expected NodePos sig at offset " + std::to_string(offset));
            break;
        }
        node.x = b.payload<double>(offset);
        node.y = b.read<double>(offset + 16);
        node.z = b.read<double>(offset + 24);
        offset += 32;

        // NodeGamma -------------------------------------------------
        if (!b.check(offset, 12)) break;
        if (b.sig(offset) != SIG_NODE_GAMMA) {
            Logger::Get().Log(LogLevel::WARNING,
                "[GRAPH] Expected NodeGamma sig at offset " + std::to_string(offset));
            break;
        }
        node.gamma = b.payload<float>(offset);
        offset += 12;

        // NodeRadius ------------------------------------------------
        if (!b.check(offset, 12)) break;
        if (b.sig(offset) != SIG_NODE_RADIUS) {
            Logger::Get().Log(LogLevel::WARNING,
                "[GRAPH] Expected NodeRadius sig at offset " + std::to_string(offset));
            break;
        }
        node.radius = b.payload<float>(offset);
        offset += 12;

        // NodeMaterial ----------------------------------------------
        if (!b.check(offset, 12)) break;
        if (b.sig(offset) != SIG_NODE_MAT) {
            Logger::Get().Log(LogLevel::WARNING,
                "[GRAPH] Expected NodeMaterial sig at offset " + std::to_string(offset));
            break;
        }
        node.material = b.payload<int32_t>(offset);
        offset += 12;

        // NodeCriteria (pascal-style: 8-byte header + 1-byte length + <length> bytes)
        // The length byte counts all bytes including the null terminator.
        // An empty criteria has length == 1 and the single byte is 0x00.
        if (!b.check(offset, 9)) break;
        if (b.sig(offset) != SIG_NODE_CRIT) {
            Logger::Get().Log(LogLevel::WARNING,
                "[GRAPH] Expected NodeCriteria sig at offset " + std::to_string(offset));
            break;
        }
        {
            const size_t strHeaderEnd = offset + 8;  // payload starts here
            const uint8_t strLen = buf[strHeaderEnd]; // length byte
            const size_t  totalCritBytes = 8 + 1 + static_cast<size_t>(strLen);
            if (!b.check(offset, totalCritBytes)) break;

            if (strLen > 1) {
                // Readable string: bytes [strHeaderEnd+1 .. strHeaderEnd+strLen-1]
                // (last byte is null terminator, excluded)
                const char* strPtr = reinterpret_cast<const char*>(buf.data() + strHeaderEnd + 1);
                node.criteria.assign(strPtr, strLen - 1);
                // Strip trailing null if present
                while (!node.criteria.empty() && node.criteria.back() == '\0')
                    node.criteria.pop_back();
            }
            offset += totalCritBytes;
        }

        result.nodes.push_back(std::move(node));
    }

    // ------------------------------------------------------------------
    // 6. Parse edge records
    //    Each edge: EdgeLink1(12B) + EdgeLink2(12B) + LinkType(12B)
    //    LinkType reuses the 0x0423 signature with a different sub-tag.
    // ------------------------------------------------------------------
    while (offset + 2 <= fileSize) {
        uint16_t s = b.sig(offset);
        if (s != SIG_EDGE_LINK1) break;

        if (!b.check(offset, 12)) break;
        const int link1 = b.payload<int32_t>(offset);
        offset += 12;

        if (!b.check(offset, 12)) break;
        if (b.sig(offset) != SIG_EDGE_LINK2) {
            Logger::Get().Log(LogLevel::WARNING,
                "[GRAPH] Expected EdgeLink2 sig at offset " + std::to_string(offset));
            break;
        }
        const int link2 = b.payload<int32_t>(offset);
        offset += 12;

        int linkType = 0;
        if (offset + 12 <= fileSize && b.sig(offset) == SIG_NODE_RADIUS) {
            // 0x0423 doubles as LinkType when it follows EdgeLink2
            linkType = b.payload<int32_t>(offset);
            offset += 12;
        }

        GraphEdge edge;
        edge.node1     = link1;
        edge.node2     = link2;
        edge.link_type = linkType;
        result.edges.push_back(edge);
    }

    Logger::Get().Log(LogLevel::INFO,
        "[GRAPH] Parsed " + std::to_string(result.nodes.size()) + " nodes, " +
        std::to_string(result.edges.size()) + " edges from: " + filepath);

    // ------------------------------------------------------------------
    // 7. Attach edge link IDs to the corresponding node entries
    //    (GraphNode::link1/link2 store the connected-node IDs for the
    //     first two edges touching this node, for quick lookup by callers)
    // ------------------------------------------------------------------
    for (const GraphEdge& e : result.edges) {
        // Find node with id == e.node1 and attach e.node2 as its link
        for (GraphNode& n : result.nodes) {
            if (n.id == e.node1) {
                if (n.link1 == -1) n.link1 = e.node2;
                else if (n.link2 == -1) n.link2 = e.node2;
            } else if (n.id == e.node2) {
                if (n.link1 == -1) n.link1 = e.node1;
                else if (n.link2 == -1) n.link2 = e.node1;
            }
        }
    }

    result.valid = true;
    return result;
}
