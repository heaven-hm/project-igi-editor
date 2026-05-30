/**
 * verify_level.cpp
 * Standalone CLI tool to cross-reference IGI Editor object log against a QSC file.
 *
 * Usage: verify_level <log_file> <qsc_file> [options]
 *   --level <N>         Filter to level N
 *   --strict-ori        Require orientation match
 *   --strict-tex        Require texture match
 *   --strict-mesh       Require mesh/model match
 */

#define _USE_MATH_DEFINES
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cassert>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <optional>

#include "level/task_schema.h"

// ---------------------------------------------------------------------------
// VerifyObj - one object parsed from the editor log
// ---------------------------------------------------------------------------
struct VerifyObj {
    std::string modelId;
    std::string type;
    std::string name;
    double px = 0, py = 0, pz = 0;
    double rx = 0, ry = 0, rz = 0;
    bool ori_logged = false;
    std::string texId;
    std::string meshId;
    bool tex_logged  = false;
    bool mesh_logged = false;
};

// ---------------------------------------------------------------------------
// ExpectedObj - one object parsed from the QSC file
// ---------------------------------------------------------------------------
struct ExpectedObj {
    std::string modelId;
    std::string type;
    std::string name;
    double px = 0, py = 0, pz = 0;
    double rx = 0, ry = 0, rz = 0;
    bool has_ori = false;
    std::string texId;   // optional
    std::string meshId;  // optional
    bool has_tex  = false;
    bool has_mesh = false;
};

// ---------------------------------------------------------------------------
// OriMatch: returns true if two orientation triples are within EPS radians
// ---------------------------------------------------------------------------
static bool OriMatch(const VerifyObj& q, const ExpectedObj& exp) {
    const double EPS = 0.05; // ~2.8 degrees
    auto angDiff = [](double a, double b) -> double {
        double d = std::fabs(a - b);
        // Normalise to [0, pi]
        while (d > 2 * M_PI) d -= 2 * M_PI;
        if (d > M_PI) d = 2 * M_PI - d;
        return d;
    };
    return angDiff(q.rx, exp.rx) < EPS &&
           angDiff(q.ry, exp.ry) < EPS &&
           angDiff(q.rz, exp.rz) < EPS;
}

// ---------------------------------------------------------------------------
// Parse the editor log into a list of VerifyObj
// ---------------------------------------------------------------------------
// Log line format (emitted by App::LoadLevel):
// [VERIFY] ID=<modelId>, Type=<type>, Name=<name>, Pos=<x>,<y>,<z>, Ori=<rx>,<ry>,<rz>, Tex=<tex>, Model=<mesh>
static std::vector<VerifyObj> ParseLog(const std::string& logPath) {
    std::vector<VerifyObj> result;
    std::ifstream f(logPath);
    if (!f.is_open()) {
        fprintf(stderr, "[verify_level] Cannot open log: %s\n", logPath.c_str());
        return result;
    }

    // Regex: mandatory ID/Type/Name/Pos, optional Ori/Tex/Model
    static const std::regex re(
        R"(\[VERIFY\]\s+ID=([^,]+),\s*Type=([^,]+),\s*Name=([^,]+),\s*)"
        R"(Pos=([-\d.e+]+),([-\d.e+]+),([-\d.e+]+))"
        R"((?:,\s*Ori=([-\d.e+]+),([-\d.e+]+),([-\d.e+]+))?)"
        R"((?:,\s*Tex=([^,)]+))?)"
        R"((?:,\s*Model=([^,)\s]+))?)"
    );

    std::string line;
    while (std::getline(f, line)) {
        std::smatch m;
        if (!std::regex_search(line, m, re)) continue;

        VerifyObj obj;
        obj.modelId = m[1].str();
        obj.type    = m[2].str();
        obj.name    = m[3].str();
        // Trim trailing spaces from name
        while (!obj.name.empty() && obj.name.back() == ' ') obj.name.pop_back();

        obj.px = std::stod(m[4].str());
        obj.py = std::stod(m[5].str());
        obj.pz = std::stod(m[6].str());

        if (m[7].matched) {
            obj.rx = std::stod(m[7].str());
            obj.ry = std::stod(m[8].str());
            obj.rz = std::stod(m[9].str());
            obj.ori_logged = true;
        }
        if (m[10].matched) {
            obj.texId = m[10].str();
            // trim
            while (!obj.texId.empty() && obj.texId.back() == ' ') obj.texId.pop_back();
            obj.tex_logged = true;
        }
        if (m[11].matched) {
            obj.meshId = m[11].str();
            while (!obj.meshId.empty() && obj.meshId.back() == ' ') obj.meshId.pop_back();
            obj.mesh_logged = true;
        }
        result.push_back(std::move(obj));
    }
    return result;
}

// ---------------------------------------------------------------------------
// Trim helper
// ---------------------------------------------------------------------------
static std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// ---------------------------------------------------------------------------
// Parse a QSC file (text) into ExpectedObj list using TaskSchemaNS schemas
// ---------------------------------------------------------------------------
static std::vector<ExpectedObj> ParseQSC(const std::string& qscPath) {
    std::vector<ExpectedObj> result;
    std::ifstream f(qscPath);
    if (!f.is_open()) {
        fprintf(stderr, "[verify_level] Cannot open QSC: %s\n", qscPath.c_str());
        return result;
    }

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());

    // Find Task_New(...) / Task_DeclareParameters calls
    // Very simple line-by-line scanner that finds lines like:
    //   SomeType.Task_New( taskid, modelid, name, x, y, z, rx, ry, rz, ... )
    // or
    //   Task_New( taskid, modelid, name, x, y, z, rx, ry, rz )

    // Split into logical "calls" by scanning for ( ... ) blocks
    // We'll use a simple state machine to collect parenthesised groups
    struct CallBlock {
        std::string funcName;
        std::vector<std::string> args;
    };

    std::vector<CallBlock> calls;
    size_t pos = 0;
    size_t len = content.size();

    auto skipSpace = [&]() {
        while (pos < len && std::isspace((unsigned char)content[pos])) ++pos;
    };

    auto readIdent = [&]() -> std::string {
        std::string id;
        while (pos < len && (std::isalnum((unsigned char)content[pos]) ||
                              content[pos] == '_' || content[pos] == '.')) {
            id += content[pos++];
        }
        return id;
    };

    while (pos < len) {
        skipSpace();
        if (pos >= len) break;
        // Skip comments (//)
        if (pos + 1 < len && content[pos] == '/' && content[pos+1] == '/') {
            while (pos < len && content[pos] != '\n') ++pos;
            continue;
        }
        // Try to read an identifier
        if (!std::isalpha((unsigned char)content[pos]) && content[pos] != '_') {
            ++pos;
            continue;
        }
        size_t identStart = pos;
        std::string ident = readIdent();
        skipSpace();
        if (pos >= len || content[pos] != '(') {
            continue;
        }
        // Read the arg list
        ++pos; // skip '('
        std::string argBlock;
        int depth = 1;
        while (pos < len && depth > 0) {
            if (content[pos] == '(') ++depth;
            else if (content[pos] == ')') { --depth; if (depth == 0) { ++pos; break; } }
            argBlock += content[pos++];
        }
        // Tokenise argBlock by commas (simple, no nested parens needed here)
        std::vector<std::string> args;
        {
            std::string cur;
            int d = 0;
            for (char c : argBlock) {
                if (c == '(') { ++d; cur += c; }
                else if (c == ')') { --d; cur += c; }
                else if (c == ',' && d == 0) {
                    args.push_back(Trim(cur));
                    cur.clear();
                }
                else {
                    // Strip quotes from strings
                    if (c != '"') cur += c;
                }
            }
            if (!cur.empty() || !argBlock.empty()) args.push_back(Trim(cur));
        }
        CallBlock cb;
        cb.funcName = ident;
        cb.args = args;
        calls.push_back(std::move(cb));
    }

    // Now process calls to extract ExpectedObj
    // We look for calls whose function name ends in a known task type
    const auto& schemas = TaskSchemaNS::GetBuiltinSchemas();

    for (auto& cb : calls) {
        // Extract type from funcName (e.g. "Building.Task_New" -> "Building")
        std::string taskType;
        auto dotPos = cb.funcName.rfind('.');
        if (dotPos != std::string::npos) {
            taskType = cb.funcName.substr(0, dotPos);
        } else {
            // e.g. just "Building"
            taskType = cb.funcName;
        }
        // Strip Task_New etc
        if (taskType == "Task_New" || taskType == "Task_DeclareParameters") continue;
        // Remove inner type prefix if any (e.g. "Static.Task_New" -> "Static")
        auto dot2 = taskType.rfind('.');
        if (dot2 != std::string::npos) taskType = taskType.substr(dot2 + 1);

        const TaskSchemaNS::TaskSchema* schema = TaskSchemaNS::GetBuiltinSchema(taskType);
        if (!schema || schema->empty()) continue;
        if (cb.args.empty()) continue;

        ExpectedObj exp;
        exp.type = taskType;

        // Map schema fields to arg positions
        for (const auto& fd : *schema) {
            int base = fd.argOffset;
            if (fd.name == "modelid") {
                if (base < (int)cb.args.size()) exp.modelId = cb.args[base];
            } else if (fd.name == "name") {
                if (base < (int)cb.args.size()) exp.name = cb.args[base];
            } else if (fd.name == "pos") {
                if (base + 2 < (int)cb.args.size()) {
                    try {
                        exp.px = std::stod(cb.args[base]);
                        exp.py = std::stod(cb.args[base+1]);
                        exp.pz = std::stod(cb.args[base+2]);
                    } catch (...) {}
                }
            } else if (fd.name == "rot") {
                if (base + 2 < (int)cb.args.size()) {
                    try {
                        exp.rx = std::stod(cb.args[base]);
                        exp.ry = std::stod(cb.args[base+1]);
                        exp.rz = std::stod(cb.args[base+2]);
                        exp.has_ori = true;
                    } catch (...) {}
                }
            }
        }
        result.push_back(std::move(exp));
    }

    return result;
}

// ---------------------------------------------------------------------------
// CrossRef - match logged objects against expected objects
// ---------------------------------------------------------------------------
struct MatchResult {
    enum Status { MATCH, POS_MISMATCH, ORI_MISMATCH, TEX_MISMATCH, MESH_MISMATCH, MISSING };
    Status status;
    const VerifyObj*    logged   = nullptr;
    const ExpectedObj*  expected = nullptr;
    // Mismatch details
    std::string logged_val;
    std::string expected_val;
};

struct LevelReport {
    struct Category {
        std::string name;
        std::vector<MatchResult> matches;
        std::vector<MatchResult> pos_mismatch;
        std::vector<MatchResult> ori_mismatch;
        std::vector<MatchResult> tex_mismatch;
        std::vector<MatchResult> mesh_mismatch;
        std::vector<MatchResult> missing;    // expected but not found in log
        std::vector<MatchResult> extra;      // in log but not in expected
    };
    std::map<std::string, Category> categories;
};

static const double POS_EPS = 0.5; // world units

static bool PosMatch(const VerifyObj& q, const ExpectedObj& exp) {
    return std::fabs(q.px - exp.px) < POS_EPS &&
           std::fabs(q.py - exp.py) < POS_EPS &&
           std::fabs(q.pz - exp.pz) < POS_EPS;
}

static LevelReport CrossRef(const std::vector<VerifyObj>& logged,
                             const std::vector<ExpectedObj>& expected,
                             bool matchOri, bool matchTex, bool matchMesh) {
    LevelReport report;

    // Track which logged objects have been consumed
    std::vector<bool> logged_used(logged.size(), false);

    for (const auto& exp : expected) {
        auto& cat = report.categories[exp.type];
        cat.name = exp.type;

        // Find the best candidate in logged that matches by position
        int best_idx = -1;
        double best_dist = 1e18;
        for (size_t i = 0; i < logged.size(); ++i) {
            if (logged_used[i]) continue;
            const auto& q = logged[i];
            if (q.type != exp.type && q.modelId != exp.modelId) continue;
            double dx = q.px - exp.px;
            double dy = q.py - exp.py;
            double dz = q.pz - exp.pz;
            double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            if (dist < best_dist) {
                best_dist = dist;
                best_idx = (int)i;
            }
        }

        MatchResult mr;
        mr.expected = &exp;

        if (best_idx == -1 || best_dist >= POS_EPS) {
            // Position mismatch or missing
            if (best_idx == -1) {
                mr.status = MatchResult::MISSING;
                cat.missing.push_back(mr);
            } else {
                mr.logged = &logged[best_idx];
                mr.status = MatchResult::POS_MISMATCH;
                mr.logged_val   = std::to_string(logged[best_idx].px) + "," +
                                  std::to_string(logged[best_idx].py) + "," +
                                  std::to_string(logged[best_idx].pz);
                mr.expected_val = std::to_string(exp.px) + "," +
                                  std::to_string(exp.py) + "," +
                                  std::to_string(exp.pz);
                cat.pos_mismatch.push_back(mr);
                logged_used[best_idx] = true;
            }
            continue;
        }

        // Position matches — consume this logged object
        const VerifyObj& q = logged[best_idx];
        logged_used[best_idx] = true;
        mr.logged = &q;

        // Check orientation
        if (matchOri && exp.has_ori) {
            // STRICT: if ori not logged, it's a mismatch (not a silent pass)
            if (!q.ori_logged) {
                mr.status = MatchResult::ORI_MISMATCH;
                mr.logged_val   = "(not logged)";
                mr.expected_val = std::to_string(exp.rx) + "," +
                                  std::to_string(exp.ry) + "," +
                                  std::to_string(exp.rz);
                cat.ori_mismatch.push_back(mr);
                continue;
            }
            if (!OriMatch(q, exp)) {
                mr.status = MatchResult::ORI_MISMATCH;
                mr.logged_val   = std::to_string(q.rx) + "," +
                                  std::to_string(q.ry) + "," +
                                  std::to_string(q.rz);
                mr.expected_val = std::to_string(exp.rx) + "," +
                                  std::to_string(exp.ry) + "," +
                                  std::to_string(exp.rz);
                cat.ori_mismatch.push_back(mr);
                continue;
            }
        }

        // Check texture
        if (matchTex && exp.has_tex && q.tex_logged) {
            if (q.texId != exp.texId) {
                mr.status = MatchResult::TEX_MISMATCH;
                mr.logged_val   = q.texId;
                mr.expected_val = exp.texId;
                cat.tex_mismatch.push_back(mr);
                continue;
            }
        }

        // Check mesh
        if (matchMesh && exp.has_mesh && q.mesh_logged) {
            if (q.meshId != exp.meshId) {
                mr.status = MatchResult::MESH_MISMATCH;
                mr.logged_val   = q.meshId;
                mr.expected_val = exp.meshId;
                cat.mesh_mismatch.push_back(mr);
                continue;
            }
        }

        mr.status = MatchResult::MATCH;
        cat.matches.push_back(mr);
    }

    // Collect extra (logged but not matched)
    for (size_t i = 0; i < logged.size(); ++i) {
        if (logged_used[i]) continue;
        MatchResult mr;
        mr.logged  = &logged[i];
        mr.status  = MatchResult::MISSING; // overloaded: "extra"
        auto& cat  = report.categories[logged[i].type];
        cat.name   = logged[i].type;
        cat.extra.push_back(mr);
    }

    return report;
}

// ---------------------------------------------------------------------------
// PrintCategory
// ---------------------------------------------------------------------------
static void PrintCategory(const LevelReport::Category& cat, bool verbose) {
    int total   = (int)(cat.matches.size() + cat.pos_mismatch.size() +
                        cat.ori_mismatch.size() + cat.tex_mismatch.size() +
                        cat.mesh_mismatch.size() + cat.missing.size());
    int pass    = (int)cat.matches.size();
    int pm      = (int)cat.pos_mismatch.size();
    int om      = (int)cat.ori_mismatch.size();
    int tm      = (int)cat.tex_mismatch.size();
    int mm      = (int)cat.mesh_mismatch.size();
    int miss    = (int)cat.missing.size();
    int extra   = (int)cat.extra.size();

    printf("  [%s] PASS=%d  pos_mismatch=%d  ori_mismatch=%d  tex_mismatch=%d  mesh_mismatch=%d  missing=%d  extra=%d\n",
           cat.name.c_str(), pass, pm, om, tm, mm, miss, extra);

    if (!verbose) return;

    if (pm > 0) {
        printf("    --- POSITION MISMATCH ---\n");
        printf("    %-30s  %-30s  %-30s\n", "Name/ModelID", "Expected Pos", "Logged Pos");
        for (auto& mr : cat.pos_mismatch) {
            std::string id = mr.expected ? mr.expected->name : "?";
            if (id.empty() && mr.expected) id = mr.expected->modelId;
            printf("    %-30s  %-30s  %-30s\n",
                   id.c_str(), mr.expected_val.c_str(), mr.logged_val.c_str());
        }
    }
    if (om > 0) {
        printf("    --- ORIENTATION MISMATCH ---\n");
        printf("    %-30s  %-30s  %-30s\n", "Name/ModelID", "Expected Ori", "Logged Ori");
        for (auto& mr : cat.ori_mismatch) {
            std::string id = mr.expected ? mr.expected->name : "?";
            if (id.empty() && mr.expected) id = mr.expected->modelId;
            printf("    %-30s  %-30s  %-30s\n",
                   id.c_str(), mr.expected_val.c_str(), mr.logged_val.c_str());
        }
    }
    if (tm > 0) {
        printf("    --- TEXTURE MISMATCH ---\n");
        printf("    %-30s  %-30s  %-30s\n", "Name/ModelID", "Expected Tex", "Found Tex");
        for (auto& mr : cat.tex_mismatch) {
            std::string id = mr.expected ? mr.expected->name : "?";
            if (id.empty() && mr.expected) id = mr.expected->modelId;
            printf("    %-30s  %-30s  %-30s\n",
                   id.c_str(), mr.expected_val.c_str(), mr.logged_val.c_str());
        }
    }
    if (mm > 0) {
        printf("    --- MESH MISMATCH ---\n");
        printf("    %-30s  %-30s  %-30s\n", "Name/ModelID", "Expected Model", "Found Model");
        for (auto& mr : cat.mesh_mismatch) {
            std::string id = mr.expected ? mr.expected->name : "?";
            if (id.empty() && mr.expected) id = mr.expected->modelId;
            printf("    %-30s  %-30s  %-30s\n",
                   id.c_str(), mr.expected_val.c_str(), mr.logged_val.c_str());
        }
    }
    if (miss > 0) {
        printf("    --- MISSING (in QSC but not in log) ---\n");
        for (auto& mr : cat.missing) {
            std::string id = mr.expected ? mr.expected->name : "?";
            if (id.empty() && mr.expected) id = mr.expected->modelId;
            printf("    %s\n", id.c_str());
        }
    }
    if (extra > 0) {
        printf("    --- EXTRA (in log but not in QSC) ---\n");
        for (auto& mr : cat.extra) {
            std::string id = mr.logged ? mr.logged->name : "?";
            if (id.empty() && mr.logged) id = mr.logged->modelId;
            printf("    %s\n", id.c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: verify_level <log_file> <qsc_file> [--strict-ori] [--strict-tex] [--strict-mesh] [--verbose]\n");
        return 1;
    }

    std::string logPath = argv[1];
    std::string qscPath = argv[2];
    bool strictOri  = false;
    bool strictTex  = false;
    bool strictMesh = false;
    bool verbose    = false;

    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "--strict-ori")  == 0) strictOri  = true;
        if (std::strcmp(argv[i], "--strict-tex")  == 0) strictTex  = true;
        if (std::strcmp(argv[i], "--strict-mesh") == 0) strictMesh = true;
        if (std::strcmp(argv[i], "--verbose")     == 0) verbose    = true;
    }

    printf("[verify_level] Log : %s\n", logPath.c_str());
    printf("[verify_level] QSC : %s\n", qscPath.c_str());

    auto logged   = ParseLog(logPath);
    auto expected = ParseQSC(qscPath);

    printf("[verify_level] Logged objects  : %zu\n", logged.size());
    printf("[verify_level] Expected objects: %zu\n", expected.size());

    if (logged.empty() && expected.empty()) {
        printf("[verify_level] Nothing to verify.\n");
        return 0;
    }

    LevelReport report = CrossRef(logged, expected, strictOri, strictTex, strictMesh);

    int total_pass = 0, total_fail = 0;
    printf("\n=== VERIFY REPORT ===\n");
    for (auto& [type, cat] : report.categories) {
        PrintCategory(cat, verbose);
        total_pass += (int)cat.matches.size();
        total_fail += (int)(cat.pos_mismatch.size() + cat.ori_mismatch.size() +
                            cat.tex_mismatch.size() + cat.mesh_mismatch.size() +
                            cat.missing.size() + cat.extra.size());
    }
    printf("=====================\n");
    printf("TOTAL PASS: %d   TOTAL FAIL: %d\n", total_pass, total_fail);
    return (total_fail > 0) ? 1 : 0;
}
