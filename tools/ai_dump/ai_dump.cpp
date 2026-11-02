// ai_dump.cpp - Batch-decompile AI QVMs (COMMON/AI + per-level ai/) and each
// level's objects.qvm to readable QSC text for behaviour analysis.
#include "../../source/level/qvm_parser.h"
#include "../../source/level/qvm_decompiler.h"
#include "../../source/renderer/graph_writer.h"
#include "../../source/config.h"
#include <direct.h>
#include <io.h>
#include <string>
#include <iostream>
#include <vector>
#include <algorithm>

static int GraphProbe(const char* graph_path, int count, char** ids) {
    GraphFile g = GRAPH_Parse(graph_path);
    if (!g.valid) { std::cerr << "invalid graph: " << g.error << "\n"; return 1; }
    std::cout << "graph=" << graph_path
              << " nodes=" << g.nodes.size()
              << " edges=" << g.edges.size()
              << " maxNodes=" << g.max_nodes
              << " route_table=" << g.route_table.size() << "\n";
    if (!g.nodes.empty()) {
        int min_id = g.nodes.front().id, max_id = g.nodes.front().id;
        for (const auto& n : g.nodes) {
            min_id = std::min(min_id, n.id);
            max_id = std::max(max_id, n.id);
        }
        std::cout << "id range: [" << min_id << " .. " << max_id << "]\n";
    }
    for (int i = 0; i < count; ++i) {
        const int want = std::atoi(ids[i]);
        const GraphNode* n = GRAPH_FindNode(g, want);
        std::cout << "node " << want << ": "
                  << (n ? "FOUND" : "MISSING");
        if (n) std::cout << " pos=" << (long long)n->x << "," << (long long)n->y;
        std::cout << "\n";
    }
    // Route table health: count valid entries + sample routes to node 63.
    size_t valid = 0;
    for (const auto& e : g.route_table) if (e.next >= 0) ++valid;
    std::cout << "route entries with next>=0: " << valid
              << " / " << g.route_table.size() << "\n";
    if (!g.nodes.empty()) {
        const int from = g.nodes.front().id;
        auto route = GRAPH_EnumerateRoute(g, from, 63);
        std::cout << "route " << from << "->63: hops=" << route.size();
        if (!route.empty()) std::cout << " next=" << route.front();
        std::cout << "\n";
    }
    return 0;
}

// Standalone tool: satisfy Logger's Config dependency without linking the
// full app (config.cpp pulls in qsc/qvm compiler + render globals).
ConfigData Config::data_{};
ConfigData& Config::Get() { return Config::data_; }

static bool g_recurse = true;

static bool DumpOne(const std::string& in_path, const std::string& out_dir) {
    QVMFile qvm = QVM_Parse(in_path);
    if (!qvm.valid) {
        std::cerr << "[skip] invalid qvm: " << in_path << "\n";
        return false;
    }
    // out name: last two path components (levelN_ai_1205.qsc) to stay unique.
    std::string flat = in_path;
    for (auto& ch : flat) if (ch == '\\' || ch == '/' || ch == ':') ch = '_';
    const std::string out_path = out_dir + "\\" + flat + ".qsc";
    if (!QVM_Decompile(qvm, out_path)) {
        std::cerr << "[fail] decompile: " << in_path << "\n";
        return false;
    }
    return true;
}

static void ScanDir(const std::string& dir, const std::string& out_dir,
                    int& dumped, int& failed, bool recurse) {
    std::string pattern = dir + "\\*";
    struct _finddata_t fd;
    intptr_t handle = _findfirst(pattern.c_str(), &fd);
    if (handle == -1) return;
    do {
        const std::string name = fd.name;
        if (name == "." || name == "..") continue;
        const std::string full = dir + "\\" + name;
        if (fd.attrib & _A_SUBDIR) {
            if (recurse) ScanDir(full, out_dir, dumped, failed, recurse);
        } else if (name.size() > 4 && _stricmp(name.c_str() + name.size() - 4, ".qvm") == 0) {
            DumpOne(full, out_dir) ? ++dumped : ++failed;
        }
    } while (_findnext(handle, &fd) == 0);
    _findclose(handle);
}

int main(int argc, char** argv) {
    if (argc >= 4 && std::string(argv[1]) == "probe") {
        // probe <graph.dat> <nodeId> [nodeId...] — print graph stats + id hits
        QVMFile dummy; // avoid unused warnings if headers change
        (void)dummy;
        extern int GraphProbe(const char*, int, char**);
        return GraphProbe(argv[2], argc - 3, argv + 3);
    }
    if (argc < 3) {
        std::cerr << "usage: ai_dump <out_dir> [-norecurse] <file-or-dir> [...]\n";
        return 1;
    }
    const std::string out_dir = argv[1];
    _mkdir(out_dir.c_str());

    int dumped = 0, failed = 0;
    for (int i = 2; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "-norecurse") { g_recurse = false; continue; }
        // File or directory?
        struct _finddata_t fd;
        intptr_t h = _findfirst(arg.c_str(), &fd);
        if (h == -1) { std::cerr << "[miss] " << arg << "\n"; continue; }
        _findclose(h);
        if (fd.attrib & _A_SUBDIR) {
            ScanDir(arg, out_dir, dumped, failed, g_recurse);
        } else {
            DumpOne(arg, out_dir) ? ++dumped : ++failed;
        }
    }
    std::cout << "dumped=" << dumped << " failed=" << failed
              << " -> " << out_dir << "\n";
    return 0;
}
