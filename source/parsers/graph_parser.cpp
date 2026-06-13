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
