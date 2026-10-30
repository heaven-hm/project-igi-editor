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
// NOTE: `tests/` is not globbed, so a file that is not named in CMakeLists.txt
// is simply absent from the build and its TEST() cases never run in any lane.
// Silence is the only symptom.
//
// ─────────────────────────────────────────────────────────────────────────────
// EMPTY, AND THAT IS THE POINT -- two entries were removed on 2026-10-30 by
// deleting the files, not by registering them.  Both had never been compiled by
// any target in their entire history, and neither could be made to compile
// against this tree without inventing production API:
//
//   tests/test_ai_behavior.cpp  (17 TESTs, added ba75efac 2026-09-03, deleted)
//     Referenced six symbols that exist in NO revision of this repository --
//     `AiArchetype`, `ResolveAiArchetype`, `AiSystem::SetDetectionEnabled`,
//     `AiGuardState::Panic`, `AiGuardEntity::stationary` and
//     `AiGuardEntity::saw_player_this_tick` (verified with
//     `git log -S <symbol> --all`, which finds them only in the test file
//     itself).  The report `reports/review-2026-10-30.md` (F6) read the file as
//     merely unbuilt because `AiGuardState` does exist; that reading was wrong,
//     and this is the correction.  It was never a regression guard for this
//     code -- it was written against a different design.
//
//     It also contradicted the engine oracle, so "fix the test" would have
//     meant adding behaviour, not repairing a test:
//       * AITYPE_* are script CONSTANTS, not an archetype classifier.  They are
//         a 30-entry name table, gAITypeNames AT 0x53C9C0
//         (src/addr.c:449, src/humanai/humanai.c:1171), registered as script
//         values by HumanAI_RegisterConstants HOOK 0x44EEB0
//         (src/aiaction/aiaction.c:1032-1065) and read back by AIType_GetName
//         HOOK 0x452910 (src/patrol/grid.c:45-49).  There is no
//         civilian-vs-soldier brain split: one HumanAI_Update
//         HOOK 0x44F410 (src/humanai/humanai.c:413-415) drives every AITYPE.
//       * `Panic` is a QVM script action, AIAction_RunPanicking
//         HOOK 0x44DA00 (src/aiaction/aiaction.c:595), not a C++ guard state --
//         which is exactly the design the current source comment records
//         ("until the alarm-driven panic state lands", source/ai_system.cpp:559).
//       * Its stealth model was inverted against the shipped constant: the test
//         asserted a gunshot must leave a guard in Patrol, while retail defines
//         HUMANAI_DETECTIONEVENT_GUNSHOT_RANGE = 0x1E000 = 30 m
//         (src/aiaction/aiaction.c:1062), which source/ai_system.cpp:440-446
//         honours by waking the guard to Suspicious.
//     The behaviour that IS in this tree is already pinned by
//     tests/test_ai_patrol_port.cpp (9 TESTs, registered).
//
//   tests/test_debug_command_manager.cpp  (1 TEST, added e36d90b1, deleted)
//     Constructed `DebugCommandManager` with a hand-rolled fake `class App`
//     { GetCurLevelNo(); LoadLevel(); } whose layout does not match the real
//     App (source/app.h:34).  It asserted that "commands.txt" in the CWD is
//     emptied, but the manager polls `editor/tools/debug-command.txt`
//     (source/debug_command_manager.cpp:30), and it never called Update(), so
//     nothing could ever have consumed the queue.  It could not pass in either
//     lane: on the host the manager's translation unit needs windows.h and
//     app.h; in igi_tests the manager is not compiled at all (it is built only
//     into igi-editor -- build-mingw/compile_commands.json has exactly one
//     entry for it, CMakeFiles/igi-editor.dir).  Registering it would have
//     meant adding a new executable target for the manager plus a link stub for
//     App, i.e. new build surface for a test that verifies a file path, and the
//     Windows gate that would have to accept that cannot be run from this host.
//     What it was reaching for is already covered, better, by
//     tests/test_debug_command_parser.cpp (5 TESTs) and
//     tests/test_debug_command_queue.cpp (3 TESTs), both registered.
//
// A file deleted with a cited reason beats a permanently-excluded file: the
// exclusion list can only ever say "this never runs", while a deletion plus
// this comment says why, and stops a future reader counting it as coverage.
// ─────────────────────────────────────────────────────────────────────────────
const char* const kNotBuiltTestFiles[][2] = {
    // (empty)
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
