/******************************************************************************
 * @file    repo_source_scan.h
 * @brief   Shared, dependency-free helpers for the repository-hygiene guards:
 *          locate the checkout, read a file, and strip comments from C++ or
 *          from CMake.
 *
 * Used by tests/test_shader_uniform_contract.cpp (the C++/shader uniform
 * contract) and tests/test_build_manifest_files.cpp (every path CMakeLists.txt
 * names must exist).  Both read repository files as TEXT and assert on what
 * they find, because neither the shader tiers above 21 nor a fresh-clone
 * configure can be executed from the host lane.
 *
 * WHY THE COMMENT STRIPPER IS HAND-ROLLED
 *   A lazy `std::regex` block-comment match is quadratic on a file that opens a
 *   block comment and never closes it, and CMakeLists.txt in this repository
 *   does exactly that by accident: it writes the glob `source/menu/` followed
 *   by `*.*` (an opening slash-star) and, three hundred lines later, the path
 *   `WEAPONS/` followed by `*` `/WEAPON.QVM` (a closing star-slash).  A regex
 *   stripper therefore deleted everything between them -- including the shader
 *   copy step the uniform-contract guard reads -- and silently reported that
 *   CMakeLists.txt copies no shader tier at all.  That is a false NEGATIVE in a
 *   guard, which is worse than no guard, so both strippers below are
 *   single-pass scanners.
 *****************************************************************************/

#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

namespace igi_guard {

namespace fs = std::filesystem;

// The host lane runs the suites with cwd = the repository root
// (tools/host-tests/host_test_runner.py, `cwd=REPO`) and ctest is given
// WORKING_DIRECTORY ${PROJECT_SOURCE_DIR} (CMakeLists.txt).  Walk up anyway, so
// a caller that starts the binary from a subdirectory still works.
inline fs::path FindRepoRoot() {
    std::error_code ec;
    if (const char* env = std::getenv("IGI_REPO_ROOT"); env != nullptr && *env != '\0') {
        const fs::path candidate(env);
        if (fs::is_directory(candidate / "shaders", ec)) return candidate;
    }
    fs::path dir = fs::current_path(ec);
    for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
        if (fs::is_regular_file(dir / "CMakeLists.txt", ec) &&
            fs::is_directory(dir / "shaders", ec) &&
            fs::is_directory(dir / "source", ec)) {
            return dir;
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir) break;
        dir = parent;
    }
    return {};
}

inline std::string ReadFile(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

inline std::string Relative(const fs::path& root, const fs::path& path) {
    std::error_code ec;
    const fs::path rel = fs::relative(path, root, ec);
    return ec ? path.string() : rel.generic_string();
}

// C++ comments.  String literals are copied through verbatim (the inline shader
// sources ARE string literals) but are skipped over for comment detection, so a
// `//` inside a URL or a Windows path is not mistaken for a comment.
inline std::string StripCppComments(const std::string& source) {
    std::string out;
    out.reserve(source.size());
    const size_t n = source.size();
    size_t i = 0;
    while (i < n) {
        const char c = source[i];
        if (c == '/' && i + 1 < n && source[i + 1] == '*') {
            i += 2;
            while (i + 1 < n && !(source[i] == '*' && source[i + 1] == '/')) ++i;
            i = (i + 1 < n) ? i + 2 : n;
            out += ' ';
        } else if (c == '/' && i + 1 < n && source[i + 1] == '/') {
            while (i < n && source[i] != '\n') ++i;
            out += ' ';
        } else if (c == '"') {
            out += c;
            ++i;
            while (i < n && source[i] != '"') {
                if (source[i] == '\\' && i + 1 < n) {
                    out += source[i];
                    ++i;
                }
                out += source[i];
                ++i;
            }
            if (i < n) {
                out += source[i];
                ++i;
            }
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

// CMake comments start with `#` and run to end of line, outside quotes.  CMake
// is not C: `#` is the only comment marker, and applying StripCppComments() to a
// CMakeLists.txt is what produced the false negative described above.
inline std::string StripCMakeComments(const std::string& source) {
    std::string out;
    out.reserve(source.size());
    const size_t n = source.size();
    size_t i = 0;
    while (i < n) {
        const char c = source[i];
        if (c == '#') {
            while (i < n && source[i] != '\n') ++i;
            out += ' ';
        } else if (c == '"') {
            out += c;
            ++i;
            while (i < n && source[i] != '"') {
                out += source[i];
                ++i;
            }
            if (i < n) {
                out += source[i];
                ++i;
            }
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

}  // namespace igi_guard
