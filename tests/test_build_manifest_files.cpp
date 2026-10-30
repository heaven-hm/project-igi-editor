/******************************************************************************
 * @file    test_build_manifest_files.cpp
 * @brief   Regression guard: every file CMakeLists.txt NAMES must exist, and
 *          every test file that exists must be BUILT.
 *
 * ============================================================================
 * THE INCIDENT THIS GUARDS (four occurrences on this branch, all 2026-10-29)
 * ============================================================================
 * A commit staged a shared `CMakeLists.txt` whose new content belonged to
 * another agent, WITHOUT the files that content named.  HEAD then referenced
 * files that were not in the repository, so a fresh clone could not even
 * CONFIGURE -- CMake fails at generate time on a missing source file, which
 * means the build gate never gets to run.
 *
 * Measured against this repository's own history (every commit that touched
 * CMakeLists.txt, its named paths checked against that commit's tree):
 *
 *   eb0d5b9d  missing source/runtime/human_shadow.cpp, tests/test_human_shadow.cpp,
 *                      tests/test_terrain_lod_settings.cpp
 *   bf831cbf  missing source/runtime/heli_preview.cpp, source/runtime/smoke_sim.cpp,
 *                      tests/test_heli_preview.cpp, tests/test_smoke_particles.cpp
 *   0e0c3d9f  missing tests/test_skinned_static_parity.cpp
 *   b0968c34  missing source/runtime/rail_source.cpp, tests/test_rail_polyline.cpp,
 *                      tests/test_rail_source_corpus.cpp
 *
 * The fix-forward commit for the first one says it in its own message:
 *
 *   76a3ec17  "fix-forward: commit the human-shadow files the pushed branch
 *              references ... That is the third occurrence of this failure on
 *              this branch and it is mine: staging a shared file whose new
 *              content belongs to another agent."
 *
 * The check below is two lines of logic and would have caught all four at the
 * commit that introduced them, before the push.
 *
 * ============================================================================
 * WHAT IT CHECKS
 * ============================================================================
 *  A. every concrete `source/...` / `tests/...` path CMakeLists.txt names
 *     exists in the working tree (globs and `${...}` expansions are excluded,
 *     and so is the build tree, which is not in the repository);
 *  B. the extra top-level prefixes CMakeLists.txt names as files
 *     (`cmake/`, `assets/`, `third_party/`) exist too;
 *  C. every `tests/*.cpp` on disk is either registered in CMakeLists.txt or is
 *     on the explicit, reviewed "not built" list below -- a test file that
 *     nothing compiles is a test that never runs, and it fails silently.
 *****************************************************************************/

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "support/repo_source_scan.h"

namespace {

namespace fs = std::filesystem;
using igi_guard::FindRepoRoot;
using igi_guard::ReadFile;
using igi_guard::Relative;
using igi_guard::StripCMakeComments;

const fs::path kRepo = FindRepoRoot();
const fs::path kCMakeLists = kRepo / "CMakeLists.txt";

// A concrete path with a filename extension.  `*` is deliberately NOT in the
// character class, so `file(GLOB ... "source/level/*.*")` and
// `"source/*.h" "source/*.cpp"` never match -- those are patterns, not files,
// and asserting on them would be asserting on nothing.
//
// The extension must START with a letter.  Without that, the DIRECTORY
// `third_party/glm-1.0.1` reads as `third_party/glm-1.0` plus extension `.1`,
// and the guard reports a missing file that was never a file.
const std::regex& ConcretePathRe() {
    static const std::regex re(
        R"RE(\b((?:source|tests|cmake|assets|third_party)/[A-Za-z0-9_./+-]+\.[A-Za-z_][A-Za-z0-9_]*))RE");
    return re;
}

// Every concrete path CMakeLists.txt names, deduplicated and sorted.
std::vector<std::string> NamedPaths() {
    const std::string text = StripCMakeComments(ReadFile(kCMakeLists));
    std::set<std::string> unique;
    for (std::sregex_iterator it(text.begin(), text.end(), ConcretePathRe()), end; it != end; ++it) {
        const std::string path = (*it)[1].str();
        if (path.find('*') != std::string::npos) continue;   // a glob, not a file
        if (path.find("${") != std::string::npos) continue;  // an expansion
        unique.insert(path);
    }
    return {unique.begin(), unique.end()};
}

// ─────────────────────────────────────────────────────────────────────────────
// preconditions -- a silent no-op guard is worse than no guard
// ─────────────────────────────────────────────────────────────────────────────

TEST(BuildManifestFiles, TheGuardItselfIsLookingAtRealInput) {
    ASSERT_FALSE(kRepo.empty())
        << "could not locate the repository root from " << fs::current_path().string()
        << " (expected a directory holding CMakeLists.txt, shaders/ and source/)";
    ASSERT_TRUE(fs::is_regular_file(kCMakeLists)) << Relative(kRepo, kCMakeLists);

    const std::vector<std::string> named = NamedPaths();
    // If the extractor stops matching, every assertion below passes vacuously.
    // The manifest names ~280 concrete files today; require a sane floor.
    ASSERT_GE(named.size(), 100u)
        << "only " << named.size() << " concrete source/tests paths were extracted from "
        << Relative(kRepo, kCMakeLists) << " -- the extractor has drifted from the file, "
           "and this guard is no longer testing anything";

    // Both halves of the manifest must be represented, or the extractor is
    // matching one section and silently missing the other.
    const size_t sources = static_cast<size_t>(std::count_if(
        named.begin(), named.end(), [](const std::string& p) { return p.rfind("source/", 0) == 0; }));
    const size_t tests = static_cast<size_t>(std::count_if(
        named.begin(), named.end(), [](const std::string& p) { return p.rfind("tests/", 0) == 0; }));
    EXPECT_GE(sources, 50u) << "the source/ half of the manifest was not extracted";
    EXPECT_GE(tests, 50u) << "the tests/ half of the manifest was not extracted";

    // And the working tree must actually hold them, or the test would report
    // "missing" for everything and look like a real failure.
    EXPECT_TRUE(fs::is_directory(kRepo / "source"));
    EXPECT_TRUE(fs::is_directory(kRepo / "tests"));
}

// ─────────────────────────────────────────────────────────────────────────────
// A. the incident: every named file must exist
// ─────────────────────────────────────────────────────────────────────────────

TEST(BuildManifestFiles, EverySourceAndTestFileCmakeListsNamesExists) {
    const std::vector<std::string> named = NamedPaths();
    ASSERT_GE(named.size(), 100u);

    std::vector<std::string> missing;
    for (const std::string& path : named) {
        if (!fs::is_regular_file(kRepo / path)) missing.push_back(path);
    }

    EXPECT_TRUE(missing.empty())
        << Relative(kRepo, kCMakeLists) << " names " << missing.size()
        << " file(s) that are NOT in the repository:\n  "
        << [&] {
               std::string joined;
               for (const std::string& path : missing) {
                   joined += path;
                   joined += "\n  ";
               }
               return joined;
           }()
        << "\nA fresh clone cannot even CONFIGURE with a named source file absent -- CMake "
           "fails at generate time, so the build gate never runs.  This has happened four "
           "times on this branch (eb0d5b9d, bf831cbf, 0e0c3d9f, b0968c34): a commit staged a "
           "shared CMakeLists.txt whose new content belonged to another agent, without the "
           "files it named.\n"
           "Either commit the files with the manifest line, or remove the line in the same "
           "commit.";
}

TEST(BuildManifestFiles, NamedPathsAreAllInsideTheRepository) {
    // A manifest line that escapes the checkout (`../elsewhere/foo.cpp`) would
    // pass the existence test on one machine and fail on every other.
    for (const std::string& path : NamedPaths()) {
        EXPECT_EQ(path.find(".."), std::string::npos)
            << "CMakeLists.txt names `" << path << "`, which leaves the repository";
        EXPECT_NE(path.front(), '/')
            << "CMakeLists.txt names the absolute path `" << path << "`";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// C. a test file nothing compiles is a test that never runs
// ─────────────────────────────────────────────────────────────────────────────

// Reviewed, deliberate exclusions.  Every entry needs a reason; an entry that
// is here because nobody noticed is exactly what this list exists to prevent.
//
// NOTE: both of these hold real TEST() cases (17 and 1) and neither has ever
// been compiled by any lane -- `tests/` is not globbed, so a file that is not
// named in CMakeLists.txt is simply absent from the build.  They are recorded
// rather than registered here because registering them changes what the
// WINDOWS suite (igi_tests) compiles, and that gate cannot be run from this
// host.  Registering or deleting them is a deliberate follow-up, not a silent
// side effect of adding a test.
const char* const kNotBuiltTestFiles[][2] = {
    {"tests/test_ai_behavior.cpp",
     "predates the current suite layout; includes ../source/ai_system.h and has never been "
     "in any CMake target"},
    {"tests/test_debug_command_manager.cpp",
     "predates the current suite layout; forward-declares App and has never been in any "
     "CMake target"},
};

bool IsDeliberatelyNotBuilt(const std::string& path, std::string* reason) {
    for (const auto& entry : kNotBuiltTestFiles) {
        if (path == entry[0]) {
            if (reason != nullptr) *reason = entry[1];
            return true;
        }
    }
    return false;
}

TEST(BuildManifestFiles, EveryTestFileOnDiskIsRegisteredOrDeliberatelyExcluded) {
    std::error_code ec;
    std::vector<std::string> on_disk;
    for (fs::directory_iterator it(kRepo / "tests", ec), end; it != end; it.increment(ec)) {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".cpp") continue;
        on_disk.push_back(Relative(kRepo, it->path()));
    }
    std::sort(on_disk.begin(), on_disk.end());
    ASSERT_GE(on_disk.size(), 50u)
        << "found only " << on_disk.size() << " test files under tests/ -- wrong directory?";

    const std::vector<std::string> named = NamedPaths();
    const std::set<std::string> named_set(named.begin(), named.end());

    std::vector<std::string> unregistered;
    for (const std::string& path : on_disk) {
        if (named_set.count(path) != 0) continue;
        std::string reason;
        if (IsDeliberatelyNotBuilt(path, &reason)) continue;
        unregistered.push_back(path);
    }

    EXPECT_TRUE(unregistered.empty())
        << "test file(s) exist under tests/ but no CMake target compiles them, so their "
           "TEST() cases never run in ANY lane:\n  "
        << [&] {
               std::string joined;
               for (const std::string& path : unregistered) {
                   joined += path;
                   joined += "\n  ";
               }
               return joined;
           }()
        << "\n`tests/` is not globbed, so silence is the only symptom: the suite still "
           "prints OK.  Add the file to add_executable(igi_tests ...), or add it to "
           "kNotBuiltTestFiles above with a reason.";
}

TEST(BuildManifestFiles, DeliberateExclusionsStillExist) {
    // A stale entry in the exclusion list would silently keep excluding a file
    // that has since been registered or deleted.
    for (const auto& entry : kNotBuiltTestFiles) {
        EXPECT_TRUE(fs::is_regular_file(kRepo / entry[0]))
            << entry[0] << " is on the not-built list but does not exist -- remove the entry";
        EXPECT_NE(std::string(entry[1]).size(), 0u) << entry[0] << " has no recorded reason";
    }
}

}  // namespace
