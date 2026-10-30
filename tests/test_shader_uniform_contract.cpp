/******************************************************************************
 * @file    test_shader_uniform_contract.cpp
 * @brief   Regression guard: every uniform the C++ uploads is declared by the
 *          shaders, and every uniform the shaders declare is uploaded by the
 *          C++ -- in ALL FOUR tiers, and in the copy that actually ships.
 *
 * ============================================================================
 * THE INCIDENT THIS GUARDS (read before weakening any assertion here)
 * ============================================================================
 * Commit 33368d7c ("fix(fog): objects had NO fog, and the terrain fog was
 * ~1000x off") renamed the terrain fog uniform block's second field from
 * `g_fog_far` to `g_fog_scale` in source/renderer/renderer.h and taught
 * source/renderer/gl_helper.cpp to upload `g_fog_scale`.  It updated
 * shaders/{21,32,41,45}.  It did NOT touch assets/editor/shaders/{41,45}, and
 * that is the copy CMakeLists.txt actually ships: the POST_BUILD step copies
 * `assets/editor` into `<exe dir>/editor` and only copies `shaders/21`
 * separately (CMakeLists.txt:296-316).
 *
 * The result on a GL 4.1/4.5 host was a terrain that did not render: the
 * shipped shader read `g_fog_far`, the C++ wrote `g_fog_scale` into the same
 * std140 slot, `length(vs_out_pos_in_view)` compared clip-space against world
 * units, and the ramp clamped to `fog_a = 1.0` -- the terrain was drawn fully
 * fogged.  The user found it, not a test.
 *
 * The host cannot reach that tier at all (Apple's legacy profile offers GL 2.1
 * / GLSL 1.20, and only the 21 tier compiles), so no lane in this repository
 * executed the broken code.  This test closes that hole by reading the shader
 * SOURCE and the C++ UPLOAD SITES as text and asserting they agree.
 *
 * ============================================================================
 * WHAT IT CHECKS
 * ============================================================================
 *  A. every name the C++ uploads by string (glGetUniformLocation,
 *     LegacyUpload*) is declared by some shader -- file tier or inline string;
 *  B. every plain (non-block) uniform a file-tier shader declares is a name the
 *     C++ uploads -- an orphan declaration is a rename someone half-finished;
 *  C. every uniform BLOCK a file-tier shader declares is bound by the C++ at
 *     the same binding point, and its fields are uploaded by the C++ in the
 *     declared order (derived from the emulation's byte offsets);
 *  D. the four tiers declare the same uniform names and contain the same files,
 *     so a one-tier edit cannot pass;
 *  E. every shader filename the C++ loads exists in every tier;
 *  F. the SHIPPED copy (assets/editor/shaders/<tier>) is byte-identical to the
 *     repo source (shaders/<tier>) -- assertion F is the incident itself;
 *  G. every tier the runtime can select is shipped by one of the two copy
 *     mechanisms, so a tier cannot be silently left out of the install.
 *
 * The C++ side is derived from the upload sites, not from a hand-written list,
 * so the list cannot drift.
 *****************************************************************************/

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "support/repo_source_scan.h"

namespace {

namespace fs = std::filesystem;
using igi_guard::FindRepoRoot;
using igi_guard::ReadFile;
using igi_guard::StripCMakeComments;
using igi_guard::StripCppComments;

const fs::path kRepo = FindRepoRoot();

std::string Relative(const fs::path& path) { return igi_guard::Relative(kRepo, path); }


// ─────────────────────────────────────────────────────────────────────────────
// shader source parsing
// ─────────────────────────────────────────────────────────────────────────────

struct UniformBlock {
    std::string name;
    std::vector<std::string> fields;  // declaration order -- std140 layout depends on it
    int binding = -1;                 // -1 when the shader declares no explicit binding
};

struct ShaderDecls {
    std::set<std::string> plain;                 // plain (non-block) uniforms
    std::map<std::string, UniformBlock> blocks;  // by block name
};

// `uniform <type> <name>;`  -- NOT `uniform <block> { ... };`
const std::regex& PlainUniformRe() {
    static const std::regex re(
        R"(\buniform\s+(?!\{)([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[\s*[0-9]+\s*\])?\s*;)");
    return re;
}
// `[layout (...) uniform] <block> { ... };`
const std::regex& UniformBlockRe() {
    static const std::regex re(
        R"(\buniform\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{([\s\S]*?)\}\s*(?:\[\s*[0-9]+\s*\])?\s*;)");
    return re;
}
// A field inside a block: `<type> <name>;`
const std::regex& BlockFieldRe() {
    static const std::regex re(
        R"(\b([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*(?:\[\s*[0-9]+\s*\])?\s*;)");
    return re;
}
// `layout (std140, binding = 1) uniform ub_fog` -- the explicit binding point.
const std::regex& BlockBindingRe() {
    static const std::regex re(
        R"(layout\s*\(([^)]*)\)\s*uniform\s+([A-Za-z_][A-Za-z0-9_]*)\s*\{)");
    return re;
}
const std::regex& BindingValueRe() {
    static const std::regex re(R"(binding\s*=\s*([0-9]+))");
    return re;
}

// Comments are stripped before parsing: every one of these shaders carries a
// long explanatory header that names the very uniforms under test (the fog
// shader documents `g_fog_far` in prose), and matching those would make the
// guard assert against its own documentation.  See tests/support/
// repo_source_scan.h for why the stripper is hand-rolled, not a std::regex.
ShaderDecls ParseShaderSource(const std::string& raw) {
    const std::string text = StripCppComments(raw);
    ShaderDecls decls;

    // Explicit binding points, collected before the blocks themselves (the
    // `layout (...)` qualifier sits outside the `uniform <block> {` match).
    for (std::sregex_iterator it(text.begin(), text.end(), BlockBindingRe()), end; it != end; ++it) {
        const std::string attributes = (*it)[1].str();  // named: regex_search rejects an rvalue
        std::smatch value;
        if (std::regex_search(attributes, value, BindingValueRe())) {
            decls.blocks[(*it)[2].str()].binding = std::stoi(value[1].str());
        }
    }

    // Blocks.  Blank the matched span out of `remainder` so a block field is
    // never also counted as a plain uniform.
    std::string remainder = text;
    for (std::sregex_iterator it(text.begin(), text.end(), UniformBlockRe()), end; it != end; ++it) {
        const std::string block_name = (*it)[1].str();
        UniformBlock& block = decls.blocks[block_name];
        block.name = block_name;
        const std::string body = (*it)[2].str();
        for (std::sregex_iterator fit(body.begin(), body.end(), BlockFieldRe()), fend; fit != fend; ++fit) {
            block.fields.push_back((*fit)[2].str());
        }
        const size_t at = static_cast<size_t>((*it).position());
        remainder.replace(at, (*it).length(), std::string((*it).length(), ' '));
    }

    for (std::sregex_iterator it(remainder.begin(), remainder.end(), PlainUniformRe()), end; it != end; ++it) {
        decls.plain.insert((*it)[2].str());
    }
    return decls;
}

// ─────────────────────────────────────────────────────────────────────────────
// C++ upload-site parsing
// ─────────────────────────────────────────────────────────────────────────────

struct CppSites {
    std::set<std::string> uploaded_names;                  // by name, any mechanism
    std::map<std::string, int> legacy_offsets;             // name -> byte offset in the emulated block
    std::map<std::string, int> block_bindings;             // block name -> binding point
    std::vector<std::string> shader_filenames;             // filenames passed to GL_CreateProgramVSFS
    std::set<std::string> inline_declared;                 // uniform names declared in inline shader strings
    std::set<std::string> files_scanned;
};

std::vector<fs::path> AllCppSources() {
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(kRepo / "source", ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        const fs::path& p = it->path();
        if (p.extension() == ".cpp" || p.extension() == ".h") out.push_back(p);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// A cheap substring gate, because `std::regex` over every translation unit in
// this repository costs minutes.  A file that contains none of these markers
// cannot contribute a uniform name; only ~20 of the ~100 files do.
bool CouldDeclareOrUploadUniforms(const std::string& raw) {
    static const char* const kMarkers[] = {
        "uniform", "glGetUniformLocation", "LegacyUpload",
        "GL_SetUniformBlockBinding", "GL_CreateProgramVSFS",
    };
    for (const char* marker : kMarkers) {
        if (raw.find(marker) != std::string::npos) return true;
    }
    return false;
}

CppSites ScanCppSites() {
    CppSites sites;
    // The first argument is a program id (`prog`, `prg_mat_.program_`,
    // `shader_program_`) and never contains a comma, so `[^,]+` is enough and
    // also survives `static_cast<GLuint>(prog)`.
    static const std::regex lookup_re(
        R"RE(glGetUniformLocation\s*\(\s*[^,]+,\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*\))RE");
    static const std::regex legacy_re(
        R"RE(LegacyUpload[A-Za-z0-9_]*\s*\(\s*[^,]+,\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*base\s*\+\s*([0-9]+)\s*\))RE");
    static const std::regex block_bind_re(
        R"RE(GL_SetUniformBlockBinding\s*\(\s*[^,]+,\s*"([A-Za-z_][A-Za-z0-9_]*)"\s*,\s*([0-9]+)\s*\))RE");
    static const std::regex create_re(
        R"RE(GL_CreateProgramVSFS\s*\(\s*"([^"]+)"\s*,\s*"([^"]+)")RE");

    for (const fs::path& path : AllCppSources()) {
        const std::string raw = ReadFile(path);
        if (raw.empty()) continue;
        if (!CouldDeclareOrUploadUniforms(raw)) continue;
        sites.files_scanned.insert(Relative(path));

        for (std::sregex_iterator it(raw.begin(), raw.end(), lookup_re), end; it != end; ++it) {
            sites.uploaded_names.insert((*it)[1].str());
        }
        for (std::sregex_iterator it(raw.begin(), raw.end(), legacy_re), end; it != end; ++it) {
            const std::string name = (*it)[1].str();
            sites.uploaded_names.insert(name);
            sites.legacy_offsets[name] = std::stoi((*it)[2].str());
        }
        for (std::sregex_iterator it(raw.begin(), raw.end(), block_bind_re), end; it != end; ++it) {
            sites.block_bindings[(*it)[1].str()] = std::stoi((*it)[2].str());
        }
        for (std::sregex_iterator it(raw.begin(), raw.end(), create_re), end; it != end; ++it) {
            sites.shader_filenames.push_back((*it)[1].str());
            sites.shader_filenames.push_back((*it)[2].str());
        }

        // The inline shader strings (renderer_objects*.cpp, renderer_rain.cpp,
        // renderer_smoke.cpp) declare uniforms too; those names are "provided"
        // by the same C++ that compiles them.
        const ShaderDecls inline_decls = ParseShaderSource(raw);
        sites.inline_declared.insert(inline_decls.plain.begin(), inline_decls.plain.end());
        for (const auto& [name, block] : inline_decls.blocks) {
            (void)name;
            sites.inline_declared.insert(block.fields.begin(), block.fields.end());
        }
    }
    std::sort(sites.shader_filenames.begin(), sites.shader_filenames.end());
    sites.shader_filenames.erase(
        std::unique(sites.shader_filenames.begin(), sites.shader_filenames.end()),
        sites.shader_filenames.end());
    return sites;
}

const CppSites& Cpp() {
    static const CppSites sites = ScanCppSites();
    return sites;
}

// ─────────────────────────────────────────────────────────────────────────────
// the tier directories
// ─────────────────────────────────────────────────────────────────────────────

const std::vector<std::string>& TierNames() {
    static const std::vector<std::string> tiers = {"21", "32", "41", "45"};
    return tiers;
}

std::vector<fs::path> ShaderFilesIn(const fs::path& dir) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return files;
    for (fs::directory_iterator it(dir, ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (it->is_regular_file(ec)) files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    return files;
}

fs::path RepoTierDir(const std::string& tier) { return kRepo / "shaders" / tier; }
fs::path ShippedTierDir(const std::string& tier) { return kRepo / "assets" / "editor" / "shaders" / tier; }

// The tiers the runtime can actually select, derived from source/main.cpp
// (`Str_Cat(g_folders.shader_folder_, ..., "/21")` and friends) rather than
// listed here, so adding a tier to the dispatcher without shipping it fails.
std::set<std::string> SelectableTiers() {
    static const std::regex append_re(
        R"RE(g_folders\.shader_folder_[\s\S]{0,80}?"/([0-9]+)")RE");
    const std::string text = StripCppComments(ReadFile(kRepo / "source" / "main.cpp"));
    std::set<std::string> tiers;
    for (std::sregex_iterator it(text.begin(), text.end(), append_re), end; it != end; ++it) {
        tiers.insert((*it)[1].str());
    }
    return tiers;
}

// The tiers CMakeLists.txt copies straight out of shaders/<tier> into the exe
// directory (`copy_directory "${PROJECT_SOURCE_DIR}/shaders/21" ...`).
std::set<std::string> CmakeCopiedTiers() {
    static const std::regex copy_re(
        R"RE(copy_directory\s+"\$\{PROJECT_SOURCE_DIR\}/shaders/([0-9]+)")RE");
    const std::string text = StripCMakeComments(ReadFile(kRepo / "CMakeLists.txt"));
    std::set<std::string> tiers;
    for (std::sregex_iterator it(text.begin(), text.end(), copy_re), end; it != end; ++it) {
        tiers.insert((*it)[1].str());
    }
    return tiers;
}

std::string Join(const std::set<std::string>& values) {
    std::string out;
    for (const std::string& v : values) {
        if (!out.empty()) out += ", ";
        out += v;
    }
    return out.empty() ? "<none>" : out;
}

// Parsed once, reused by every assertion below.
struct TierParse {
    std::set<std::string> plain;
    std::map<std::string, UniformBlock> blocks;  // merged across the tier's files
    std::vector<std::string> files;              // filenames, sorted
};

const std::map<std::string, TierParse>& TierCache() {
    static const std::map<std::string, TierParse> cache = [] {
        std::map<std::string, TierParse> parsed;
        for (const std::string& tier : TierNames()) {
            TierParse entry;
            for (const fs::path& file : ShaderFilesIn(RepoTierDir(tier))) {
                entry.files.push_back(file.filename().string());
                const ShaderDecls decls = ParseShaderSource(ReadFile(file));
                entry.plain.insert(decls.plain.begin(), decls.plain.end());
                for (const auto& [name, block] : decls.blocks) {
                    UniformBlock& merged = entry.blocks[name];
                    merged.name = name;
                    if (block.binding >= 0) merged.binding = block.binding;
                    for (const std::string& field : block.fields) {
                        if (std::find(merged.fields.begin(), merged.fields.end(), field) ==
                            merged.fields.end()) {
                            merged.fields.push_back(field);
                        }
                    }
                }
            }
            std::sort(entry.files.begin(), entry.files.end());
            parsed[tier] = std::move(entry);
        }
        return parsed;
    }();
    return cache;
}

// ─────────────────────────────────────────────────────────────────────────────
// preconditions -- a silent no-op guard is worse than no guard
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderUniformContract, TheGuardItselfIsLookingAtRealInput) {
    ASSERT_FALSE(kRepo.empty())
        << "could not locate the repository root from " << fs::current_path().string()
        << " (expected a directory holding CMakeLists.txt, shaders/ and source/renderer/)";
    ASSERT_TRUE(fs::is_regular_file(kRepo / "source" / "renderer" / "gl_helper.cpp"));

    // If either half of the contract parses to nothing, every assertion below
    // would pass vacuously.  Fail loudly instead.
    ASSERT_FALSE(Cpp().uploaded_names.empty())
        << "no glGetUniformLocation/LegacyUpload* call site was found under source/ -- "
           "the parser has drifted from the code and this guard is not testing anything";
    ASSERT_GE(Cpp().uploaded_names.size(), 20u);
    ASSERT_FALSE(Cpp().legacy_offsets.empty())
        << "the GL 2.1 legacy uniform upload in source/renderer/gl_helper.cpp was not found";
    ASSERT_FALSE(Cpp().block_bindings.empty())
        << "no GL_SetUniformBlockBinding call site was found";
    ASSERT_FALSE(Cpp().shader_filenames.empty())
        << "no GL_CreateProgramVSFS call site was found";
    ASSERT_FALSE(Cpp().inline_declared.empty());

    for (const std::string& tier : TierNames()) {
        const fs::path dir = RepoTierDir(tier);
        ASSERT_TRUE(fs::is_directory(dir)) << "missing shader tier directory: " << Relative(dir);
        EXPECT_GE(ShaderFilesIn(dir).size(), 10u)
            << "tier " << tier << " holds suspiciously few shader files";
    }
    EXPECT_FALSE(TierCache().at("41").plain.empty() || TierCache().at("41").blocks.empty())
        << "tier 41 parsed to no uniforms at all -- the shader parser has drifted";

    // The dispatcher and the build must still name tiers, or assertions F/G
    // below are vacuous.
    EXPECT_FALSE(SelectableTiers().empty())
        << "source/main.cpp no longer names any shader tier; "
           "SelectableTiers() has drifted from the dispatcher";
    EXPECT_FALSE(CmakeCopiedTiers().empty())
        << "CMakeLists.txt no longer copies any shader tier";
}

// ─────────────────────────────────────────────────────────────────────────────
// A. no upload targets a name no shader declares
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderUniformContract, EveryUniformTheCppUploadsIsDeclaredBySomeShader) {
    const CppSites& cpp = Cpp();

    // Names declared by the file-based tiers...
    std::set<std::string> declared;
    for (const auto& [tier, parsed] : TierCache()) {
        declared.insert(parsed.plain.begin(), parsed.plain.end());
        for (const auto& [block_name, block] : parsed.blocks) {
            (void)block_name;
            declared.insert(block.fields.begin(), block.fields.end());
        }
    }
    // ... and by the inline shader strings compiled from source/.
    declared.insert(cpp.inline_declared.begin(), cpp.inline_declared.end());

    std::set<std::string> orphans;
    for (const std::string& name : cpp.uploaded_names) {
        if (declared.count(name) == 0) orphans.insert(name);
    }
    EXPECT_TRUE(orphans.empty())
        << "the C++ uploads uniform name(s) that NO shader declares: " << Join(orphans) << "\n"
        << "This is the 33368d7c incident in the other direction: an upload whose name no "
           "shader answers is silently dropped by glGetUniformLocation (it returns -1) and "
           "the uniform keeps its default value.\n"
        << "Declared by the shaders: " << Join(declared);
}

// ─────────────────────────────────────────────────────────────────────────────
// B. no file-tier shader declares a uniform nobody uploads
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderUniformContract, EveryPlainUniformAShaderDeclaresIsUploadedByTheCpp) {
    const CppSites& cpp = Cpp();

    std::set<std::string> orphans;
    for (const auto& [tier, parsed] : TierCache()) {
        for (const std::string& name : parsed.plain) {
            if (cpp.uploaded_names.count(name) == 0) orphans.insert(tier + "/" + name);
        }
    }
    EXPECT_TRUE(orphans.empty())
        << "file-tier shader(s) declare plain uniform(s) that no C++ site uploads: "
        << Join(orphans) << "\n"
        << "A declared-but-never-uploaded uniform reads as zero.  This is the 33368d7c "
           "incident: the shader still said `g_fog_far` after the C++ started uploading "
           "`g_fog_scale`, and the terrain was drawn fully fogged.\n"
        << "Uploaded by the C++: " << Join(cpp.uploaded_names);
}

// ─────────────────────────────────────────────────────────────────────────────
// C. uniform blocks: same name, same binding point, same field ORDER
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderUniformContract, EveryShaderUniformBlockIsBoundByTheCpp) {
    const CppSites& cpp = Cpp();
    std::set<std::string> unbound;
    for (const auto& [tier, parsed] : TierCache()) {
        for (const auto& [block_name, block] : parsed.blocks) {
            (void)block;
            if (cpp.block_bindings.count(block_name) == 0) unbound.insert(tier + "/" + block_name);
        }
    }
    EXPECT_TRUE(unbound.empty())
        << "shader uniform block(s) that no GL_SetUniformBlockBinding call names: "
        << Join(unbound) << "\n"
        << "GL_SetUniformBlockBinding returns false for a block the program does not "
           "declare, and the renderer's initialisers turn that into a failed shader "
           "setup -- the pass then does not draw.\n"
        << "Bound by the C++: " << Join([&] {
               std::set<std::string> names;
               for (const auto& [name, point] : cpp.block_bindings) {
                   (void)point;
                   names.insert(name);
               }
               return names;
           }());
}

TEST(ShaderUniformContract, ShaderBlockBindingPointsMatchTheCpp) {
    const CppSites& cpp = Cpp();
    for (const auto& [tier, parsed] : TierCache()) {
        for (const auto& [block_name, block] : parsed.blocks) {
            if (block.binding < 0) continue;  // no explicit binding; the C++ assigns one
            const auto it = cpp.block_bindings.find(block_name);
            if (it == cpp.block_bindings.end()) continue;  // reported by the test above
            EXPECT_EQ(block.binding, it->second)
                << "shaders/" << tier << " declares `layout (binding = " << block.binding
                << ") uniform " << block_name << "` but the C++ binds that block to point "
                << it->second << ". A block read from the wrong binding point reads "
                   "whatever another block left there.";
        }
    }
}

TEST(ShaderUniformContract, ShaderBlockFieldOrderMatchesTheCppUploadOrder) {
    const CppSites& cpp = Cpp();
    ASSERT_FALSE(cpp.legacy_offsets.empty());

    // The GL 2.1 emulation in gl_helper.cpp uploads the block fields as plain
    // uniforms at the std140 byte offsets, so the offsets ARE the C++ layout:
    // `base + 0`, `base + 64`, `base + 128` for the three mat4 of ub_mats and
    // `base + 0`, `base + 16` for ub_fog's vec4-then-float.
    for (const auto& [tier, parsed] : TierCache()) {
        for (const auto& [block_name, block] : parsed.blocks) {
            int previous = -1;
            for (const std::string& field : block.fields) {
                const auto it = cpp.legacy_offsets.find(field);
                if (it == cpp.legacy_offsets.end()) {
                    ADD_FAILURE() << "shaders/" << tier << " block " << block_name
                                  << " declares field `" << field
                                  << "`, which the legacy uniform upload in "
                                     "source/renderer/gl_helper.cpp never writes";
                    continue;
                }
                EXPECT_GT(it->second, previous)
                    << "shaders/" << tier << " block " << block_name << " declares `" << field
                    << "` at byte offset " << it->second << ", after a field the C++ writes at "
                    << previous << ". std140 layout is declaration order: the shader would read "
                       "the wrong member.";
                previous = it->second;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// D. the four tiers must agree with each other
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderUniformContract, EveryTierDeclaresTheSameUniformNames) {
    // The tiers are ports of one another: the 21 tier spells the two blocks out
    // as plain uniforms, the others declare blocks.  Flatten both to a name set.
    std::map<std::string, std::set<std::string>> flattened;
    for (const auto& [tier, parsed] : TierCache()) {
        std::set<std::string>& names = flattened[tier];
        names.insert(parsed.plain.begin(), parsed.plain.end());
        for (const auto& [block_name, block] : parsed.blocks) {
            (void)block_name;
            names.insert(block.fields.begin(), block.fields.end());
        }
    }
    ASSERT_EQ(flattened.size(), TierNames().size());

    const std::set<std::string>& reference = flattened.at(TierNames().front());
    for (const auto& [tier, names] : flattened) {
        std::set<std::string> missing, extra;
        std::set_difference(reference.begin(), reference.end(), names.begin(), names.end(),
                            std::inserter(missing, missing.end()));
        std::set_difference(names.begin(), names.end(), reference.begin(), reference.end(),
                            std::inserter(extra, extra.end()));
        EXPECT_TRUE(missing.empty() && extra.empty())
            << "shader tier " << tier << " does not declare the same uniforms as tier "
            << TierNames().front() << ".\n"
            << "  missing from tier " << tier << ": " << Join(missing) << "\n"
            << "  only in tier " << tier << ": " << Join(extra) << "\n"
            << "A one-tier edit is how the fog rename reached production half-done: the host "
               "only ever compiles the 21 tier, so 32/41/45 are never executed here.";
    }
}

TEST(ShaderUniformContract, EveryTierContainsTheSameShaderFiles) {
    const std::vector<fs::path> reference_files = ShaderFilesIn(RepoTierDir(TierNames().front()));
    ASSERT_FALSE(reference_files.empty());
    std::set<std::string> reference;
    for (const fs::path& file : reference_files) reference.insert(file.filename().string());

    for (const std::string& tier : TierNames()) {
        std::set<std::string> names;
        for (const fs::path& file : ShaderFilesIn(RepoTierDir(tier))) {
            names.insert(file.filename().string());
        }
        std::set<std::string> missing, extra;
        std::set_difference(reference.begin(), reference.end(), names.begin(), names.end(),
                            std::inserter(missing, missing.end()));
        std::set_difference(names.begin(), names.end(), reference.begin(), reference.end(),
                            std::inserter(extra, extra.end()));
        EXPECT_TRUE(missing.empty() && extra.empty())
            << "shader tier " << tier << " does not hold the same files as tier "
            << TierNames().front() << ".\n"
            << "  missing: " << Join(missing) << "\n  extra:   " << Join(extra);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// E. every shader the C++ loads must exist in every tier
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderUniformContract, EveryShaderTheCppLoadsExistsInEveryTier) {
    const CppSites& cpp = Cpp();
    ASSERT_FALSE(cpp.shader_filenames.empty());

    for (const std::string& tier : TierNames()) {
        for (const std::string& filename : cpp.shader_filenames) {
            const fs::path path = RepoTierDir(tier) / filename;
            EXPECT_TRUE(fs::is_regular_file(path))
                << "the C++ loads `" << filename << "` (GL_CreateProgramVSFS) but shaders/"
                << tier << "/" << filename << " does not exist.\n"
                << "GL_CreateProgramVSFS returns false when either file is missing, and the "
                   "renderer's initialiser turns that into a failed pass.";
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// F. THE INCIDENT: the copy that ships must equal the copy in the repo
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderUniformContract, ShippedShaderCopyIsByteIdenticalToTheRepoSource) {
    int tiers_checked = 0;
    for (const std::string& tier : TierNames()) {
        const fs::path shipped = ShippedTierDir(tier);
        std::error_code ec;
        if (!fs::is_directory(shipped, ec)) continue;  // not shipped this way; see test G
        ++tiers_checked;

        std::set<std::string> repo_names, shipped_names;
        for (const fs::path& file : ShaderFilesIn(RepoTierDir(tier))) {
            repo_names.insert(file.filename().string());
        }
        for (const fs::path& file : ShaderFilesIn(shipped)) {
            shipped_names.insert(file.filename().string());
        }
        std::set<std::string> missing, extra;
        std::set_difference(repo_names.begin(), repo_names.end(), shipped_names.begin(),
                            shipped_names.end(), std::inserter(missing, missing.end()));
        std::set_difference(shipped_names.begin(), shipped_names.end(), repo_names.begin(),
                            repo_names.end(), std::inserter(extra, extra.end()));
        EXPECT_TRUE(missing.empty())
            << "assets/editor/shaders/" << tier << " is missing shader(s) present in shaders/"
            << tier << ": " << Join(missing);
        EXPECT_TRUE(extra.empty())
            << "assets/editor/shaders/" << tier << " holds shader(s) absent from shaders/"
            << tier << ": " << Join(extra);

        for (const std::string& name : repo_names) {
            if (shipped_names.count(name) == 0) continue;
            const std::string repo_text = ReadFile(RepoTierDir(tier) / name);
            const std::string shipped_text = ReadFile(shipped / name);
            EXPECT_EQ(repo_text, shipped_text)
                << "assets/editor/shaders/" << tier << "/" << name
                << " DIFFERS from shaders/" << tier << "/" << name << ".\n"
                << "assets/editor is what CMakeLists.txt POST_BUILD copies into "
                   "<exe dir>/editor, so the install runs THIS copy while every reader looks "
                   "at shaders/.  That divergence is the 33368d7c incident: the fog block was "
                   "renamed to g_fog_scale in shaders/ and in the C++, assets/editor/shaders "
                   "was never updated, and the terrain rendered fully fogged on every GL 4.1 "
                   "host.  Sync the two copies; never edit one of them alone.";
        }
    }
    EXPECT_GT(tiers_checked, 0)
        << "no tier has an assets/editor/shaders copy -- either the shipped layout moved or "
           "this guard is looking in the wrong place";
}

// ─────────────────────────────────────────────────────────────────────────────
// G. every tier the runtime can select must reach the install
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShaderUniformContract, EverySelectableTierIsShipped) {
    const std::set<std::string> selectable = SelectableTiers();
    const std::set<std::string> copied_by_cmake = CmakeCopiedTiers();
    ASSERT_FALSE(selectable.empty());

    for (const std::string& tier : selectable) {
        std::error_code ec;
        const bool via_assets = fs::is_directory(ShippedTierDir(tier), ec);
        const bool via_cmake = copied_by_cmake.count(tier) != 0;
        EXPECT_TRUE(via_assets || via_cmake)
            << "source/main.cpp can select shader tier " << tier
            << " (`shader_folder_ ... \"/" << tier << "\"`) but nothing ships it:\n"
            << "  assets/editor/shaders/" << tier << " does not exist, and CMakeLists.txt has "
               "no copy_directory from shaders/" << tier << ".\n"
            << "On that context every GL_CreateProgramVSFS would fail and the pass would not "
               "draw.\n"
            << "  selectable tiers : " << Join(selectable) << "\n"
            << "  shipped via assets: " << [&] {
                   std::set<std::string> names;
                   for (const std::string& t : TierNames()) {
                       if (fs::is_directory(ShippedTierDir(t), ec)) names.insert(t);
                   }
                   return Join(names);
               }() << "\n"
            << "  copied by CMake   : " << Join(copied_by_cmake);
    }
}

}  // namespace
