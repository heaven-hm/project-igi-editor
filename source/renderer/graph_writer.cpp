/******************************************************************************
 * @file    graph_writer.cpp
 * @brief   IGI 1 navigation-graph (.dat) reader/writer, transcribed from the
 *          retail engine's graph stream. See graph_writer.h for the full
 *          function/address/file:line table this file is built from.
 *
 * Reader  = GraphOpen  0x4F9FF0  src/graphnode/stream.c:196-237
 *           Graph_Read 0x4FA160  src/graphnode/stream.c:252-314
 * Writer  = the inverse of those two, using the engine's own primitives
 *           GraphStream_WriteValue 0x4F9EB0 (stream.c:148-188) and the
 *           GraphStream_Write* family (stream.c:368-458).
 *
 * The engine's file writer itself is not in the decompiled tree (only
 * AIGraph_SaveRoutes 0x509520 writes a named value), so the record order here
 * is the order GraphOpen/Graph_Read require and that every retail file in this
 * repository uses - see the header comment in graph_writer.h.
 *****************************************************************************/

#include "graph_writer.h"
#include "../logger.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <vector>

// ---------------------------------------------------------------------------
// Constants and helpers from the engine's stream
// ---------------------------------------------------------------------------
namespace {

// stream.c:121-135
int GraphStreamHash(const char* name) {
    int h = 0;
    unsigned int m = 1;
    char c;

    while ((c = *name++)) {
        if (c >= 'a' && c <= 'z')
            c -= 0x20;
        h = static_cast<int>(static_cast<unsigned int>(h) +
                             static_cast<unsigned int>(c) * m);
        m = (m + 0x13D) & 0xFFFF;
    }
    return h;
}

// The handler table registered by Graph_Open 0x4FAA60 (src/graphtask/graph.c:
// 141-150) and AIGraph_Init 0x508FA0 (src/sysmisc/aigraph.c:130), in
// registration order. The type column is what the engine registered; the READER
// ignores it and uses the type byte stored in the file (GraphOpen :221-223 sets
// v.type from the file, GraphStream_ReadValue :61 switches on v.type).
enum HandlerId {
    H_MAX_NODES = 0,
    H_NODE_ID,
    H_NODE_POS,
    H_NODE_GAMMA,
    H_NODE_RADIUS,
    H_NODE_MATERIAL,
    H_NODE_CRITERIA,
    H_LINK_ID1,
    H_LINK_ID2,
    H_LINK_TYPE,
    H_ROUTE_TABLE,
    H_COUNT
};

struct HandlerSpec {
    const char* name;
    int         type;
};

const HandlerSpec kHandlers[H_COUNT] = {
    { "nMaxNodes",     TAG_INT },      // Graph_Open :141
    { "nNodeID",       TAG_INT },      // Graph_Open :142
    { "tNodePosition", TAG_VEC3D },    // Graph_Open :143
    { "vNodeGamma",    TAG_FLOAT },    // Graph_Open :144
    { "vNodeRadius",   TAG_FLOAT },    // Graph_Open :145
    { "nNodeMaterial", TAG_INT },      // Graph_Open :146
    { "nNodeCriteria", TAG_STRING },   // Graph_Open :147
    { "nLinkID1",      TAG_INT },      // Graph_Open :148
    { "nLinkID2",      TAG_INT },      // Graph_Open :149
    { "eLinkType",     TAG_INT },      // Graph_Open :150
    { "RouteTable",    TAG_BLOB },     // AIGraph_Init :130
};

// GraphStream_Find 0x4FA140 (stream.c:239-249). GraphStream_AddHandler :102-107
// refuses a name whose hash collides with a registered one; the eleven names
// above have eleven distinct hashes, so no name is ever unreachable.
HandlerId FindHandler(int hash) {
    for (int i = 0; i < H_COUNT; i++) {
        if (GraphStreamHash(kHandlers[i].name) == hash)
            return static_cast<HandlerId>(i);
    }
    return H_COUNT;
}

// ---------------------------------------------------------------------------
// StreamReader - GraphStream over a memory buffer.
//
// The engine reads through File_Read and never checks the tag it consumes
// (ReadInt :480-488 reads five bytes and keeps the last four, ReadByte :460-468
// reads two and keeps the second). Bounds checks are added so a truncated file
// reports an error instead of reading past the buffer; the byte counts and
// offsets are identical.
// ---------------------------------------------------------------------------
class StreamReader {
public:
    StreamReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool   eof() const { return pos_ >= size_; }
    size_t pos() const { return pos_; }
    // GraphStream_PeekByte 0x4FAA10 (src/graphtask/graph.c:66-76). At EOF the
    // engine returns the low byte of its stream pointer, which no caller can
    // observe because every peek is followed by a read; -1 is returned here.
    int    peekByte() const { return pos_ < size_ ? data_[pos_] : -1; }

    bool raw(void* out, size_t n) {
        if (pos_ + n > size_) return false;
        std::memcpy(out, data_ + pos_, n);
        pos_ += n;
        return true;
    }

    // Verbatim copy of the bytes already consumed between two positions.
    void copyRange(size_t from, size_t to, std::vector<uint8_t>& out) const {
        if (from >= to || to > size_) return;
        out.assign(data_ + from, data_ + to);
    }

    // stream.c:460-468
    bool readByte(uint8_t& out) {
        uint8_t b[2];
        if (!raw(b, 2)) return false;
        out = b[1];
        return true;
    }
    // stream.c:470-478
    bool readShort(int16_t& out) {
        uint8_t b[3];
        if (!raw(b, 3)) return false;
        std::memcpy(&out, b + 1, 2);
        return true;
    }
    // stream.c:480-488
    bool readInt(int& out) {
        uint8_t b[5];
        if (!raw(b, 5)) return false;
        std::memcpy(&out, b + 1, 4);
        return true;
    }
    // stream.c:491-501
    bool readFloat(float& out) {
        uint8_t b[5];
        if (!raw(b, 5)) return false;
        std::memcpy(&out, b + 1, 4);
        return true;
    }
    // stream.c:503-513
    bool readDouble(double& out) {
        uint8_t b[9];
        if (!raw(b, 9)) return false;
        std::memcpy(&out, b + 1, 8);
        return true;
    }
    // stream.c:515-523, sizeof(Vec3d) == 24 (src/types.h:4-6)
    bool readVec3d(double* out) {
        uint8_t b[25];
        if (!raw(b, 25)) return false;
        std::memcpy(out, b + 1, 24);
        return true;
    }
    // stream.c:526-537. The stored length counts the trailing NUL and `max` is
    // not checked. Graph_AddNode strcpy's the text (src/graphnode/graph.c:283),
    // so the value ends at the first NUL byte.
    bool readString(std::string& out) {
        uint8_t hdr[2];
        if (!raw(hdr, 2)) return false;
        const size_t len = hdr[1];
        if (pos_ + len > size_) return false;
        out.assign(reinterpret_cast<const char*>(data_ + pos_), len);
        pos_ += len;
        const size_t nul = out.find('\0');
        if (nul != std::string::npos) out.resize(nul);
        return true;
    }
    // stream.c:540-549. The stored size is skipped, `size` bytes are read.
    bool readBlob(void* out, size_t size) {
        uint8_t tag;
        int     stored;
        if (!readByte(tag)) return false;
        if (!readInt(stored)) return false;
        return raw(out, size);
    }
    // stream.c:317-366
    void skip(int count) {
        while (count-- > 0) {
            if (eof()) return;
            const uint8_t tag = data_[pos_];
            pos_ += 1;
            long long size = 0;
            switch (tag) {
            case TAG_UBYTE:
            case TAG_BYTE:
                size = 1;
                break;
            case TAG_USHORT:
            case TAG_SHORT:
                size = 2;
                break;
            case TAG_UINT:
            case TAG_INT:
            case TAG_FLOAT:
                size = 4;
                break;
            case TAG_DOUBLE:
                size = 8;
                break;
            case TAG_VEC3D:
                size = 24;
                break;
            case TAG_STRING: {
                uint8_t len = 0;
                if (!raw(&len, 1)) return;
                size = len;
                break;
            }
            case TAG_BLOB: {
                int blob = 0;
                if (!raw(&blob, 4)) return;
                size = blob;
                break;
            }
            default:
                continue;
            }
            if (size) {
                if (pos_ + static_cast<size_t>(size) > size_) {
                    pos_ = size_;
                    return;
                }
                pos_ += static_cast<size_t>(size);
            }
        }
    }

private:
    const uint8_t* data_;
    size_t         size_;
    size_t         pos_ = 0;
};

// ---------------------------------------------------------------------------
// StreamWriter - GraphStream_Write* (stream.c:368-458) and the named-record
// framing of GraphStream_WriteValue 0x4F9EB0 (stream.c:148-188).
// ---------------------------------------------------------------------------
class StreamWriter {
public:
    std::vector<uint8_t> bytes;

    // stream.c:386-394
    void pushTagged(int tag, const void* v, size_t size) {
        bytes.push_back(static_cast<uint8_t>(tag));
        const uint8_t* p = static_cast<const uint8_t*>(v);
        bytes.insert(bytes.end(), p, p + size);
    }
    // stream.c:368-374
    void writeUByte(uint8_t v) {
        bytes.push_back(TAG_UBYTE);
        bytes.push_back(v);
    }
    // stream.c:377-383
    void writeByte(int8_t v) {
        bytes.push_back(TAG_BYTE);
        bytes.push_back(static_cast<uint8_t>(v));
    }
    // stream.c:396-430
    void writeUShort(uint16_t v) { pushTagged(TAG_USHORT, &v, 2); }
    void writeShort(int16_t v)   { pushTagged(TAG_SHORT, &v, 2); }
    void writeUInt(uint32_t v)   { pushTagged(TAG_UINT, &v, 4); }
    void writeInt(int v)         { pushTagged(TAG_INT, &v, 4); }
    void writeFloat(float v)     { pushTagged(TAG_FLOAT, &v, 4); }
    void writeDouble(double v)   { pushTagged(TAG_DOUBLE, &v, 8); }
    // stream.c:432-436
    void writeVec3d(double x, double y, double z) {
        const double v[3] = { x, y, z };
        pushTagged(TAG_VEC3D, v, sizeof(v));
    }
    // stream.c:438-449. The engine stores strlen(v) + 1 in a uint8_t and then
    // memcpy's that many bytes into a 0x104 stack buffer, so a criteria longer
    // than 254 characters overruns it; the text is clamped here instead.
    void writeString(const std::string& s) {
        std::string t = s;
        if (t.size() > 254) t.resize(254);
        const int len = static_cast<int>(t.size()) + 1;
        bytes.push_back(TAG_STRING);
        bytes.push_back(static_cast<uint8_t>(len));
        bytes.insert(bytes.end(), t.begin(), t.end());
        bytes.push_back(0);
    }
    // stream.c:451-458
    void writeBlob(const void* data, size_t size) {
        writeUByte(TAG_BLOB);
        writeInt(static_cast<int>(size));
        const uint8_t* p = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), p, p + size);
    }

    // GraphStream_WriteValue :148-188
    void writeValueInt(const char* name, int v) {
        writeUInt(static_cast<uint32_t>(GraphStreamHash(name)));
        writeUByte(TAG_INT);
        writeInt(v);
    }
    void writeValueFloat(const char* name, float v) {
        writeUInt(static_cast<uint32_t>(GraphStreamHash(name)));
        writeUByte(TAG_FLOAT);
        writeFloat(v);
    }
    void writeValueVec3d(const char* name, double x, double y, double z) {
        writeUInt(static_cast<uint32_t>(GraphStreamHash(name)));
        writeUByte(TAG_VEC3D);
        writeVec3d(x, y, z);
    }
    void writeValueString(const char* name, const std::string& v) {
        writeUInt(static_cast<uint32_t>(GraphStreamHash(name)));
        writeUByte(TAG_STRING);
        writeString(v);
    }
    void writeValueBlob(const char* name, const void* data, size_t size) {
        writeUInt(static_cast<uint32_t>(GraphStreamHash(name)));
        writeUByte(TAG_BLOB);
        writeBlob(data, size);
    }
};

// ---------------------------------------------------------------------------
// GraphStream_ReadValue 0x4F9CD0 (stream.c:58-91): the value is read with the
// type byte stored in the file. Each helper writes through the destination the
// way the engine writes through its caller's pointer; a value wider than the
// destination is truncated (the engine would clobber the neighbouring field).
// ---------------------------------------------------------------------------
bool ReadValueInt(StreamReader& s, int type, int& out) {
    switch (type) {
    case TAG_UBYTE:
    case TAG_BYTE: {
        uint8_t v = 0;
        if (!s.readByte(v)) return false;
        std::memcpy(&out, &v, 1);
        return true;
    }
    case TAG_USHORT:
    case TAG_SHORT: {
        int16_t v = 0;
        if (!s.readShort(v)) return false;
        std::memcpy(&out, &v, 2);
        return true;
    }
    case TAG_UINT:
    case TAG_INT:
        return s.readInt(out);
    case TAG_FLOAT: {
        float v = 0.f;
        if (!s.readFloat(v)) return false;
        std::memcpy(&out, &v, 4);
        return true;
    }
    case TAG_DOUBLE: {
        double v = 0.0;
        if (!s.readDouble(v)) return false;
        std::memcpy(&out, &v, 4);
        return true;
    }
    default:
        return false;
    }
}

bool ReadValueFloat(StreamReader& s, int type, float& out) {
    switch (type) {
    case TAG_FLOAT:
        return s.readFloat(out);
    case TAG_UBYTE:
    case TAG_BYTE: {
        uint8_t v = 0;
        if (!s.readByte(v)) return false;
        std::memcpy(&out, &v, 1);
        return true;
    }
    case TAG_USHORT:
    case TAG_SHORT: {
        int16_t v = 0;
        if (!s.readShort(v)) return false;
        std::memcpy(&out, &v, 2);
        return true;
    }
    case TAG_UINT:
    case TAG_INT: {
        int v = 0;
        if (!s.readInt(v)) return false;
        std::memcpy(&out, &v, 4);
        return true;
    }
    case TAG_DOUBLE: {
        double v = 0.0;
        if (!s.readDouble(v)) return false;
        std::memcpy(&out, &v, 4);
        return true;
    }
    default:
        return false;
    }
}

bool ReadValueVec3d(StreamReader& s, int type, double* out) {
    if (type != TAG_VEC3D) return false;
    return s.readVec3d(out);
}

// ---------------------------------------------------------------------------
// Graph model operations (src/graphnode/graph.c)
// ---------------------------------------------------------------------------

struct LoadState {
    GraphFile graph;
    GraphNode temp;              // gGraphNodeTemp
    int       link_a = -1;       // gGraphLinkA
    int       link_b = -1;       // gGraphLinkB
    bool      failed = false;
    std::string fail;
};

void Fail(LoadState& st, const std::string& what) {
    st.failed = true;
    if (st.fail.empty()) st.fail = what;
}

// AIGraph_ClearRoutes 0x509340 (src/sysmisc/aigraph.c:162-175) over a freshly
// sized table.
void ClearRoutes(GraphFile& g, int n) {
    g.route_table.assign(static_cast<size_t>(n) * static_cast<size_t>(n),
                         GraphRouteEntry{});
}

// AIGraph_AllocRoutes 0x5092E0 (src/sysmisc/aigraph.c:144-160): the buffer
// exists only when capacity > 1.
void AllocRoutes(GraphFile& g) {
    if (g.max_nodes > 1) {
        ClearRoutes(g, g.max_nodes);
    } else {
        g.route_table.clear();
    }
}

// Graph_Grow 0x4F9620 (src/graphnode/graph.c:298-316): room for `id`, in steps
// of 100 nodes. The allocation event fires AIGraph_OnAlloc 0x5093B0, which
// reallocates and clears the route table.
void GraphGrow(GraphFile& g, int id) {
    const int cap = g.max_nodes + ((id - g.max_nodes) / 100 + 1) * 100;
    g.max_nodes = cap;
    AllocRoutes(g);
}

// Graph_AddNode 0x4F9510 (src/graphnode/graph.c:262-295).
// Not modelled: n->diameter = radius + radius (:278), n->unk2C = -1 (:282),
// n->flags = Script_EvalExpr(criteria) (:286) and the node-add event (:289-291).
GraphNode* GraphAddNode(LoadState& st, int id, double x, double y, double z,
                        float gamma, float radius, int material,
                        const std::string& criteria) {
    GraphFile& g = st.graph;

    if (id >= 1000) return nullptr;                     // :268-269
    if (id >= g.max_nodes) GraphGrow(g, id);            // :270-271
    if (GRAPH_FindNode(g, id)) {                        // :272-276
        // The engine prints "GRAPHNODE ID ALREADY USED" and spins forever;
        // the editor reports the duplicate instead of hanging.
        Fail(st, "GRAPHNODE ID ALREADY USED: " + std::to_string(id));
        return nullptr;
    }

    GraphNode n;
    n.id       = id;
    n.radius   = radius;
    n.gamma    = gamma;
    n.criteria = criteria;
    n.material = material;
    n.x        = x;
    n.y        = y;
    n.z        = z;
    g.nodes.push_back(n);
    return &g.nodes.back();
}

// Graph_AddLink 0x4F9B80 (src/graphnode/graph.c:453-472). GraphNode_AddLink
// (:474-486) fills the node's 64 link slots; the editor keeps the links only in
// GraphFile.edges.
void GraphAddLink(GraphFile& g, int a, int b, int cost) {
    GraphEdge e;
    e.node1     = a;
    e.node2     = b;
    e.link_type = cost;
    g.edges.push_back(e);
}

// Graph_Read 0x4FA160 (src/graphnode/stream.c:252-314): the old layout, read
// from offset 0 (GraphOpen :230-232 rewinds first).
bool ReadOldFormat(LoadState& st, StreamReader& s) {
    GraphFile& g = st.graph;
    g.is_legacy = true;

    int nodes = 0, capacity = 0, links = 0;
    if (!s.readInt(nodes) || !s.readInt(capacity) || !s.readInt(links)) {
        Fail(st, "old-format header");
        return false;
    }
    g.nodes.clear();
    g.max_nodes = capacity;
    AllocRoutes(g);                                   // :267 alloc event

    for (int i = 0; i < nodes; i++) {
        int id = 0;
        double pos[3] = { 0.0, 0.0, 0.0 };
        float gamma = 0.f, radius = 0.f;
        float unused = 0.f;
        int   mat = -1;                               // :271
        std::string criteria;
        bool has_unused = false, has_mat = false;

        if (!s.readInt(id))    { Fail(st, "old-format node id");  return false; }
        if (!s.readVec3d(pos)) { Fail(st, "old-format node pos"); return false; }
        if (!s.readFloat(gamma))  { Fail(st, "old-format gamma");  return false; }
        if (!s.readFloat(radius)) { Fail(st, "old-format radius"); return false; }
        if (s.peekByte() == TAG_FLOAT) {              // :281-282
            if (!s.readFloat(unused)) { Fail(st, "old-format unused float"); return false; }
            has_unused = true;
        }
        if (s.peekByte() == TAG_INT) {                // :283-284
            if (!s.readInt(mat)) { Fail(st, "old-format material"); return false; }
            has_mat = true;
        }
        if (!s.readString(criteria)) { Fail(st, "old-format criteria"); return false; }

        GraphNode* n = GraphAddNode(st, id, pos[0], pos[1], pos[2], gamma, radius,
                                    mat, criteria);
        if (n) {                                      // :288-298
            n->has_unused_float = has_unused;
            n->unused_float     = unused;
            n->has_material     = has_mat;

            // Node-load event: AIGraph_ReadNode 0x509480
            // (src/sysmisc/aigraph.c:213-229) - two int-counted lists of int
            // pairs, all four values discarded.
            const size_t tail_start = s.pos();
            int count = 0;
            if (!s.readInt(count)) { Fail(st, "old-format node tail"); return false; }
            for (int k = 0; k < count; k++) {
                int a = 0, b = 0;
                if (!s.readInt(a) || !s.readInt(b)) { Fail(st, "old-format node tail"); return false; }
            }
            if (!s.readInt(count)) { Fail(st, "old-format node tail"); return false; }
            for (int k = 0; k < count; k++) {
                int a = 0, b = 0;
                if (!s.readInt(a) || !s.readInt(b)) { Fail(st, "old-format node tail"); return false; }
            }
            s.copyRange(tail_start, s.pos(), n->load_tail);
        }
    }

    for (int i = 0; i < links; i++) {                 // :300-307
        int a = 0, b = 0, cost = 0;
        if (!s.readInt(a) || !s.readInt(b) || !s.readInt(cost)) {
            Fail(st, "old-format link");
            return false;
        }
        GraphAddLink(g, a, b, cost);
    }

    // Load event: AIGraph_LoadRoutes 0x509450 (src/sysmisc/aigraph.c:207-211).
    // The engine passes g->routes without checking it; a capacity of 1 or less
    // leaves it NULL, so the read is skipped here instead of writing through a
    // null pointer.
    if (g.max_nodes > 1) {
        const size_t bytes = static_cast<size_t>(g.max_nodes) * g.max_nodes *
                             sizeof(GraphRouteEntry);
        if (!s.readBlob(g.route_table.data(), bytes)) {
            Fail(st, "old-format route table");
            return false;
        }
        g.has_route_table = true;
    }
    return true;
}

// GraphOpen 0x4F9FF0 (src/graphnode/stream.c:196-237), magic branch (:209-228).
bool ReadNewFormat(LoadState& st, StreamReader& s) {
    GraphFile& g = st.graph;

    while (!s.eof()) {                                // :214
        int hash = 0;
        if (!s.readInt(hash)) break;                  // :218
        const HandlerId h = FindHandler(hash);
        if (h == H_COUNT) {                           // :224-226
            s.skip(2);
            continue;
        }
        uint8_t type = 0;
        if (!s.readByte(type)) break;                 // :221

        switch (h) {
        case H_MAX_NODES: {                           // Graph_ReadMaxNodes :207-221
            int n = g.max_nodes;
            if (!ReadValueInt(s, type, n)) { Fail(st, "nMaxNodes"); return false; }
            g.nodes.clear();                          // :212-214
            g.max_nodes = n;                          // :219
            break;
        }
        case H_NODE_ID:                               // Graph_ReadNodeID :223-233
            st.temp = GraphNode();
            if (!ReadValueInt(s, type, st.temp.id)) { Fail(st, "nNodeID"); return false; }
            break;
        case H_NODE_POS:                              // Graph_ReadNodePosition :235-239
            if (!ReadValueVec3d(s, type, &st.temp.x)) { Fail(st, "tNodePosition"); return false; }
            break;
        case H_NODE_GAMMA:                            // Graph_ReadNodeGamma :241-245
            if (!ReadValueFloat(s, type, st.temp.gamma)) { Fail(st, "vNodeGamma"); return false; }
            break;
        case H_NODE_RADIUS:                           // Graph_ReadNodeRadius :247-251
            if (!ReadValueFloat(s, type, st.temp.radius)) { Fail(st, "vNodeRadius"); return false; }
            break;
        case H_NODE_MATERIAL: {                       // Graph_ReadNodeMaterial :253-260
            int id = -1;
            if (!ReadValueInt(s, type, id)) { Fail(st, "nNodeMaterial"); return false; }
            // The engine stores Terrain_GetMaterial(world, id), or NULL for -1;
            // the editor keeps the file's material index.
            st.temp.material = id;
            break;
        }
        case H_NODE_CRITERIA: {                       // Graph_ReadNodeCriteria :262-270
            if (type != TAG_STRING) { Fail(st, "nNodeCriteria type"); return false; }
            if (!s.readString(st.temp.criteria)) { Fail(st, "nNodeCriteria"); return false; }
            // v->node = Graph_AddNode(...); a null result for id >= 1000 drops
            // the node, exactly as the engine does.
            const GraphNode* added = GraphAddNode(st, st.temp.id, st.temp.x, st.temp.y,
                                                  st.temp.z, st.temp.gamma, st.temp.radius,
                                                  st.temp.material, st.temp.criteria);
            if (!added && st.failed) return false;
            break;
        }
        case H_LINK_ID1:                              // Graph_ReadLinkID1 :272-279
            st.link_a = -1;
            st.link_b = -1;
            if (!ReadValueInt(s, type, st.link_a)) { Fail(st, "nLinkID1"); return false; }
            break;
        case H_LINK_ID2:                              // Graph_ReadLinkID2 :281-285
            if (!ReadValueInt(s, type, st.link_b)) { Fail(st, "nLinkID2"); return false; }
            break;
        case H_LINK_TYPE: {                           // Graph_ReadLinkType :287-293
            int cost = 0;
            if (!ReadValueInt(s, type, cost)) { Fail(st, "eLinkType"); return false; }
            GraphAddLink(g, st.link_a, st.link_b, cost);
            break;
        }
        case H_ROUTE_TABLE: {                         // AIGraph_ReadRouteTable :134-142
            AllocRoutes(g);                           // :138
            if (g.route_table.empty()) break;         // the engine's `if (g->routes)`
            const size_t bytes = static_cast<size_t>(g.max_nodes) * g.max_nodes *
                                 sizeof(GraphRouteEntry);
            if (!s.readBlob(g.route_table.data(), bytes)) {
                Fail(st, "RouteTable");
                return false;
            }
            g.has_route_table = true;
            break;
        }
        default:
            break;
        }
    }
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

GraphNodeKind GRAPH_NodeKind(const GraphNode& node) {
    const std::string& c = node.criteria;
    if (c.find("DOOR")  != std::string::npos) return GraphNodeKind::Door;
    if (c.find("STAIR") != std::string::npos) return GraphNodeKind::Stair;
    if (c.find("VIEW")  != std::string::npos) return GraphNodeKind::View;
    return GraphNodeKind::Default;
}

std::string GRAPH_ToggleCriteriaFlag(const std::string& criteria,
                                     const std::string& flag) {
    if (flag.empty()) return criteria;

    // Split the expression on '|' -- the engine's OR operator
    // (Expr_EvalInt OP_OR, src/script/expr.c:230) -- trimming blanks and
    // dropping empty fields so a hand-edited file survives a round-trip.
    std::vector<std::string> flags;
    for (size_t start = 0; start <= criteria.size(); ) {
        const size_t bar = criteria.find('|', start);
        const std::string field = criteria.substr(
            start, bar == std::string::npos ? std::string::npos : bar - start);
        const size_t first = field.find_first_not_of(" \t");
        if (first != std::string::npos) {
            const size_t last = field.find_last_not_of(" \t");
            flags.push_back(field.substr(first, last - first + 1));
        }
        if (bar == std::string::npos) break;
        start = bar + 1;
    }

    // Toggle: drop every occurrence of `flag`, otherwise append it.
    std::vector<std::string> kept;
    bool removed = false;
    for (const std::string& f : flags) {
        if (f == flag) { removed = true; continue; }
        kept.push_back(f);
    }
    if (!removed) kept.push_back(flag);

    std::string joined;
    for (size_t i = 0; i < kept.size(); ++i) {
        if (i) joined += '|';
        joined += kept[i];
    }
    return joined;
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

bool GRAPH_RouteStep(const GraphFile& graph, int from, int to,
                     int& next, float& dist) {
    if (from < 0 || to < 0 || from >= graph.max_nodes || to >= graph.max_nodes)
        return false;
    // AIGraph_GetRoute 0x509390: &g->routes[capacity * to + from].
    const size_t slot = static_cast<size_t>(from) +
                        static_cast<size_t>(to) * static_cast<size_t>(graph.max_nodes);
    if (slot >= graph.route_table.size()) return false;
    const GraphRouteEntry& e = graph.route_table[slot];
    next = e.next;
    dist = e.dist;
    // AIGraph_GetStep 0x5096C0 returns the step only when next != -1.
    return next != -1;
}

std::vector<int> GRAPH_EnumerateRoute(const GraphFile& graph, int from, int to) {
    std::vector<int> route;
    if (from < 0 || to < 0 || from >= graph.max_nodes || to >= graph.max_nodes)
        return route;
    int at = from;
    for (int guard = 0; guard <= graph.max_nodes; guard++) {
        if (at == to) return route;
        int next; float dist;
        if (!GRAPH_RouteStep(graph, at, to, next, dist)) return {};
        route.push_back(next);
        at = next;
    }
    return {};
}

int GRAPH_NearestNode(const GraphFile& graph, double x, double y, double z,
                      GraphNearestResult* out_result) {
    constexpr int kCandidateCount = 3;
    constexpr double kNoCandidateScore = 67108864.0;  // 2^26 units, ~16 km
    const double target[3] = { x, y, z };

    const GraphNode* best[kCandidateCount] = {};
    double     scores[kCandidateCount] = { kNoCandidateScore, kNoCandidateScore, kNoCandidateScore };

    for (const GraphNode& node : graph.nodes) {
        // Retail skips VIEW-criteria nodes entirely (they can never be the answer).
        if (GRAPH_NodeKind(node) == GraphNodeKind::View) continue;

        double dx = node.x - target[0];
        double dy = node.y - target[1];
        double dz = node.z - target[2];
        double score = std::sqrt((dx * dx) + (dy * dy) + (dz * dz))
                     - static_cast<double>(node.radius) * 4096.0;

        for (int slot = 0; slot < kCandidateCount; slot++) {
            if (!best[slot]) {
                best[slot] = &node;
                scores[slot] = score;
                break;
            }
            if (score < scores[slot]) {
                for (int shift = kCandidateCount - 1; shift > slot; shift--) {
                    best[shift] = best[shift - 1];
                    scores[shift] = scores[shift - 1];
                }
                best[slot] = &node;
                scores[slot] = score;
                break;
            }
        }
    }

    if (!best[0]) {
        if (out_result) *out_result = GraphNearestResult::None;
        return -1;
    }
    if (out_result) {
        *out_result = scores[0] >= 0.0 ? GraphNearestResult::Outside
                                       : GraphNearestResult::Inside;
    }
    return best[0]->id;
}

GraphFile GRAPH_ParseLegacy(const uint8_t* data, size_t size, const std::string& filepath) {
    LoadState st;
    StreamReader s(data, size);
    ReadOldFormat(st, s);
    if (st.failed) {
        st.graph.valid = false;
        st.graph.error = st.fail + ": " + filepath;
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] " + st.graph.error);
        return st.graph;
    }
    st.graph.valid = true;
    Logger::Get().Log(LogLevel::INFO,
        "[GRAPH] Parsed " + filepath + " | old layout | nodes=" +
        std::to_string(st.graph.nodes.size()) + " links=" +
        std::to_string(st.graph.edges.size()) + " capacity=" +
        std::to_string(st.graph.max_nodes));
    return st.graph;
}

GraphFile GRAPH_Parse(const std::string& filepath) {
    GraphFile result;

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        result.error = "Unable to open " + filepath + " for reading";
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] " + result.error);
        return result;
    }
    const size_t fileSize = static_cast<size_t>(file.tellg());
    file.seekg(0);
    std::vector<uint8_t> buf(fileSize);
    if (fileSize && !file.read(reinterpret_cast<char*>(buf.data()),
                               static_cast<std::streamsize>(fileSize))) {
        result.error = "Read error on file: " + filepath;
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] " + result.error);
        return result;
    }
    file.close();

    // GraphOpen :208 reads four bytes and compares them with GRAPH_MAGIC.
    if (fileSize < 4) {
        result.error = "File too small to be a graph: " + filepath;
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] " + result.error);
        return result;
    }
    uint32_t magic = 0;
    std::memcpy(&magic, buf.data(), 4);

    LoadState st;
    bool ok = true;
    if (magic == GRAPH_MAGIC) {
        StreamReader s(buf.data() + 4, fileSize - 4);     // :209-228
        ok = ReadNewFormat(st, s);
    } else {
        StreamReader s(buf.data(), fileSize);             // :229-233
        ok = ReadOldFormat(st, s);
    }

    if (!ok || st.failed) {
        st.graph.valid = false;
        st.graph.error = (st.fail.empty() ? std::string("graph parse failed")
                                          : st.fail) + ": " + filepath;
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] " + st.graph.error);
        return st.graph;
    }

    st.graph.valid = true;
    Logger::Get().Log(LogLevel::INFO,
        "[GRAPH] Parsed " + filepath +
        " | " + (st.graph.is_legacy ? "old" : "new") + " layout" +
        " | capacity=" + std::to_string(st.graph.max_nodes) +
        " nodes=" + std::to_string(st.graph.nodes.size()) +
        " links=" + std::to_string(st.graph.edges.size()) +
        " routes=" + std::to_string(st.graph.route_table.size()));
    return st.graph;
}

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

namespace {

// Graph_Grow 0x4F9620:298-316 applied to every node id at or past the capacity.
// The engine grows once per added node, in the order the nodes are added; the
// step-of-100 rule is order independent for a set of ids (checked by
// construction: each grow rounds up past the id it is given).
int CapacityFor(const GraphFile& graph) {
    int cap = graph.max_nodes;
    std::vector<int> ids;
    ids.reserve(graph.nodes.size());
    for (const GraphNode& n : graph.nodes) ids.push_back(n.id);
    std::sort(ids.begin(), ids.end());
    for (int id : ids) {
        if (id >= cap) cap = cap + ((id - cap) / 100 + 1) * 100;
    }
    return cap;
}

// A node the engine cannot represent: Graph_AddNode refuses id >= 1000
// (src/graphnode/graph.c:268-269) and -1 is the free-slot marker
// (Graph_ClearNodes :326), and a duplicate id makes it spin (:272-276).
bool ValidateNodes(const GraphFile& graph, std::string& why) {
    std::vector<int> ids;
    ids.reserve(graph.nodes.size());
    for (const GraphNode& n : graph.nodes) {
        if (n.id < 0) { why = "node id " + std::to_string(n.id) + " is negative"; return false; }
        if (n.id >= 1000) { why = "node id " + std::to_string(n.id) + " >= 1000"; return false; }
        ids.push_back(n.id);
    }
    std::sort(ids.begin(), ids.end());
    for (size_t i = 1; i < ids.size(); i++) {
        if (ids[i] == ids[i - 1]) {
            why = "duplicate node id " + std::to_string(ids[i]);
            return false;
        }
    }
    return true;
}

// The route table exactly as the engine would hold it: the loaded cells, or a
// cleared table when the capacity had to grow (Graph_Grow fires the allocation
// event, AIGraph_OnAlloc 0x5093B0 -> AIGraph_AllocRoutes 0x5092E0 ->
// AIGraph_ClearRoutes 0x509340).
std::vector<GraphRouteEntry> RoutesFor(const GraphFile& graph, int capacity) {
    const size_t cells = static_cast<size_t>(capacity) * static_cast<size_t>(capacity);
    if (graph.route_table.size() == cells) return graph.route_table;
    return std::vector<GraphRouteEntry>(cells, GraphRouteEntry{});
}

// The new layout: magic, then the named records (graph_writer.h documents why
// this order is the one the reader's state machine needs).
void WriteNewLayout(const GraphFile& graph, int capacity,
                    const std::vector<GraphRouteEntry>& routes, bool write_routes,
                    StreamWriter& w) {
    uint8_t magic[4];
    const uint32_t m = GRAPH_MAGIC;
    std::memcpy(magic, &m, 4);
    w.bytes.insert(w.bytes.end(), magic, magic + 4);

    w.writeValueInt("nMaxNodes", capacity);
    if (write_routes) {
        // AIGraph_SaveRoutes 0x509520: WriteValue("RouteTable", BLOB, routes,
        // capacity * capacity * sizeof(GraphStep)).
        w.writeValueBlob("RouteTable", routes.data(),
                         routes.size() * sizeof(GraphRouteEntry));
    }
    for (const GraphNode& n : graph.nodes) {
        w.writeValueInt("nNodeID", n.id);
        w.writeValueVec3d("tNodePosition", n.x, n.y, n.z);
        w.writeValueFloat("vNodeGamma", n.gamma);
        w.writeValueFloat("vNodeRadius", n.radius);
        w.writeValueInt("nNodeMaterial", n.material);
        w.writeValueString("nNodeCriteria", n.criteria);
    }
    for (const GraphEdge& e : graph.edges) {
        w.writeValueInt("nLinkID1", e.node1);
        w.writeValueInt("nLinkID2", e.node2);
        w.writeValueInt("eLinkType", e.link_type);
    }
}

// The old layout: the inverse of Graph_Read 0x4FA160 (stream.c:252-314).
void WriteOldLayout(const GraphFile& graph, int capacity,
                    const std::vector<GraphRouteEntry>& routes, bool write_routes,
                    StreamWriter& w) {
    w.writeInt(static_cast<int>(graph.nodes.size()));   // :258
    w.writeInt(capacity);                               // :259
    w.writeInt(static_cast<int>(graph.edges.size()));   // :260

    for (const GraphNode& n : graph.nodes) {
        w.writeInt(n.id);                               // :277
        w.writeVec3d(n.x, n.y, n.z);                    // :278
        w.writeFloat(n.gamma);                          // :279
        w.writeFloat(n.radius);                         // :280
        if (n.has_unused_float) w.writeFloat(n.unused_float);   // :281-282
        if (n.has_material)     w.writeInt(n.material);         // :283-284
        w.writeString(n.criteria);                      // :287
        if (!n.load_tail.empty()) {
            w.bytes.insert(w.bytes.end(), n.load_tail.begin(), n.load_tail.end());
        } else {
            // AIGraph_ReadNode 0x509480 requires two int-counted pair lists; a
            // node the editor created has neither, i.e. two empty lists.
            w.writeInt(0);
            w.writeInt(0);
        }
    }
    for (const GraphEdge& e : graph.edges) {            // :300-307
        w.writeInt(e.node1);
        w.writeInt(e.node2);
        w.writeInt(e.link_type);
    }
    if (write_routes) {                                 // AIGraph_LoadRoutes :209
        w.writeBlob(routes.data(), routes.size() * sizeof(GraphRouteEntry));
    }
}

} // namespace

bool GRAPH_Write(const std::string& srcPath, const std::string& outPath,
                 const GraphFile& graph) {
    // srcPath is part of the historical signature; the layout to write is the
    // one GRAPH_Parse selected and recorded in the graph itself.
    (void)srcPath;

    std::string why;
    if (!ValidateNodes(graph, why)) {
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] Write refused: " + why + " (" + outPath + ")");
        return false;
    }

    const int capacity = CapacityFor(graph);
    const bool write_routes = graph.has_route_table && capacity > 1;
    const std::vector<GraphRouteEntry> routes =
        write_routes ? RoutesFor(graph, capacity) : std::vector<GraphRouteEntry>();

    StreamWriter w;
    if (graph.is_legacy) {
        WriteOldLayout(graph, capacity, routes, write_routes, w);
    } else {
        WriteNewLayout(graph, capacity, routes, write_routes, w);
    }

    std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
    if (!f.is_open()) {
        Logger::Get().Log(LogLevel::ERR, "[GRAPH] Write: cannot open output: " + outPath);
        return false;
    }
    f.write(reinterpret_cast<const char*>(w.bytes.data()),
            static_cast<std::streamsize>(w.bytes.size()));
    if (!f) return false;
    f.close();

    Logger::Get().Log(LogLevel::INFO,
        "[GRAPH] Wrote " + std::to_string(graph.nodes.size()) + " nodes, " +
        std::to_string(graph.edges.size()) + " links, capacity " +
        std::to_string(capacity) + " (" +
        (graph.is_legacy ? "old" : "new") + " layout) to: " + outPath);
    return true;
}

bool GRAPH_Save(const std::string& srcPath, const std::string& outPath,
                const GraphFile& graph) {
    // Everything the file holds, with the positions the caller edited.
    GraphFile merged = GRAPH_Parse(srcPath);
    if (!merged.valid) return false;
    for (const GraphNode& n : graph.nodes) {
        GraphNode* target = GRAPH_FindNode(merged, n.id);
        if (!target) continue;
        target->x = n.x;
        target->y = n.y;
        target->z = n.z;
    }
    return GRAPH_Write(srcPath, outPath, merged);
}
