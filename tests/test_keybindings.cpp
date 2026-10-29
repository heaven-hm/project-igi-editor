/******************************************************************************
 * @file    test_keybindings.cpp
 * @brief   Issue #48: the 25 stubbed keybindings must not silently no-op, plus
 *          the five adjacent silent no-ops fixed in task-11.
 *
 * These are repository-invariant tests over the files that together decide what
 * a key press does:
 *
 *   source/app_input.cpp                        the event dispatcher
 *   source/app_view.cpp                         per-frame binding consumers
 *   source/config.cpp / source/config.h         the binding -> named-field router
 *   source/app.h                                editor state (removed flags)
 *   assets/editor/qed/qedkeybindings.qsc        the shipped default bindings
 *   docs/igi-re-parity/KEYBINDINGS.md           the per-binding decision record
 *
 * They encode the issue's acceptance criteria directly:
 *   (a) no "[Keybind] ... not implemented" line survives in the dispatcher;
 *   (b) every removed binding is removed from the shipped default too, and is
 *       answered with a status-bar message (a `Removed("Name")` guard) rather
 *       than silently doing nothing;
 *   (c) every one of the 25 bindings has a row in the decision document, and the
 *       eight implemented ones really are wired up (bound, dispatched, and going
 *       through the host-tested helpers of runtime/editor_quick_actions.h).
 *
 * task-11 adds the adjacent defects of the same class:
 *   (d) no combo is bound to two different events in the shipped default;
 *   (e) every advertised binding has a handler (Check/CheckCont) or a visible
 *       Removed() message — an advertised binding is never a silent no-op;
 *   (f) no event that config.cpp routes to a named config field is advertised
 *       without a live consumer, and the two dead fields deleted in task-11 stay
 *       deleted;
 *   (g) the ManipulationMode::None drag is an explicit, mode-naming no-op.
 *
 * The file paths are resolved relative to this test file, then relative to the
 * working directory, so the tests run from the host lane (cwd = repo root) and
 * from a build directory.
 *****************************************************************************/

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

// ---------------------------------------------------------------------------
// Path resolution
// ---------------------------------------------------------------------------

std::string DirOfFile(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return (slash == std::string::npos) ? std::string(".") : path.substr(0, slash);
}

std::string JoinPath(const std::string& dir, const std::string& rel) {
    if (dir.empty() || dir == ".") return rel;
    const char last = dir[dir.size() - 1];
    if (last == '/' || last == '\\') return dir + rel;
    return dir + "/" + rel;
}

bool ReadFile(const std::string& path, std::string* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

// Locate a repository-relative file. Returns "" when it cannot be found; the
// caller decides whether that is a skip or a failure.
std::string FindRepoFile(const std::string& rel, std::string* tried) {
    static const std::string kTestFile = __FILE__;
    const std::string test_dir = DirOfFile(kTestFile);

    std::vector<std::string> candidates;
    if (!test_dir.empty()) {
        candidates.push_back(JoinPath(test_dir, "../" + rel));   // tests/ -> repo root
        candidates.push_back(JoinPath(test_dir, rel));
    }
    candidates.push_back(rel);                     // cwd = repo root
    candidates.push_back("../" + rel);             // cwd = repo/build*
    candidates.push_back("../../" + rel);          // cwd = repo/build/Release

    for (const std::string& c : candidates) {
        std::string content;
        if (ReadFile(c, &content)) {
            if (tried) *tried = c;
            return c;
        }
        if (tried) *tried += c + " ";
    }
    return "";
}

// Text with `//` comments removed. These tests assert things about *code*, and a
// comment that merely names an identifier (for example a note explaining that the
// old keyLookDown field was deleted) must neither satisfy nor break such a check.
// A string literal containing "//" would be truncated; none of the literals these
// tests look for contain one.
std::string StripComments(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const size_t c = line.find("//");
        out += (c == std::string::npos) ? line : line.substr(0, c);
        out += '\n';
    }
    return out;
}

// ---------------------------------------------------------------------------
// Tiny parsers (deliberately dependency-free: this test must build everywhere)
// ---------------------------------------------------------------------------

struct BindingLine {
    std::string event;
    std::string combo;
    int line = 0;
};

// Every SetEventBinding("Event", "Combo") line of the shipped default file.
std::vector<BindingLine> ParseBindingLines(const std::string& text) {
    std::vector<BindingLine> out;
    std::istringstream in(text);
    std::string raw;
    int line_no = 0;
    while (std::getline(in, raw)) {
        ++line_no;
        // Comment lines are inert in the shipped file (the editor's own readers
        // strip them: source/app_ui.cpp:123-128, source/config.cpp:337) — the
        // default file keeps a commented-out "TestEvent" pair, so skipping them
        // here is required for the duplicate-name check to mean anything.
        const size_t first = raw.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || raw[first] == '/') continue;
        const size_t at = raw.find("SetEventBinding(");
        if (at == std::string::npos) continue;
        const size_t q1 = raw.find('"', at);
        const size_t q2 = (q1 == std::string::npos) ? std::string::npos : raw.find('"', q1 + 1);
        const size_t q3 = (q2 == std::string::npos) ? std::string::npos : raw.find('"', q2 + 1);
        const size_t q4 = (q3 == std::string::npos) ? std::string::npos : raw.find('"', q3 + 1);
        if (q1 == std::string::npos || q2 == std::string::npos ||
            q3 == std::string::npos || q4 == std::string::npos) {
            BindingLine bad;
            bad.line = line_no;
            out.push_back(bad);   // malformed: empty event marks it
            continue;
        }
        BindingLine b;
        b.event = raw.substr(q1 + 1, q2 - q1 - 1);
        b.combo = raw.substr(q3 + 1, q4 - q3 - 1);
        b.line = line_no;
        out.push_back(b);
    }
    return out;
}

// Names routed through the Removed() guard in the dispatcher.
std::set<std::string> ParseRemovedNames(const std::string& app_input_cpp) {
    std::set<std::string> out;
    const std::string needle = "Removed(\"";
    size_t pos = 0;
    while ((pos = app_input_cpp.find(needle, pos)) != std::string::npos) {
        const size_t start = pos + needle.size();
        const size_t end = app_input_cpp.find('"', start);
        if (end == std::string::npos) break;
        out.insert(app_input_cpp.substr(start, end - start));
        pos = end + 1;
    }
    return out;
}

// The 25 bindings of issue #48, with the decision taken for each.
// "implemented" rows must have a live handler; "removed" rows must not be
// advertised by the shipped default file any more.
struct Decision {
    const char* name;
    bool implemented;
};

const Decision kDecisions[] = {
    {"CameraResetRadius", true},
    {"CameraCopyPosition", true},
    {"CameraCopyOrientation", true},
    {"TimerStartStop", true},
    {"TimerReset", true},
    {"TimerIncreaseSpeed", true},
    {"TimerDecreaseSpeed", true},
    {"ToggleObjects", true},
    {"CameraSnapToObjectWithRadius", false},
    {"CameraStrafe", false},
    {"AnimTaskIncreaseKeyframeInterpolation", false},
    {"AnimTaskDecreaseKeyframeInterpolation", false},
    {"AnimTaskToggleCameraRelative", false},
    {"AnimTaskGoToCursor", false},
    {"AnimTaskToggleWindowHidden", false},
    {"AnimTaskStartRecording", false},
    {"AnimTaskGoToTop", false},
    {"AnimTaskToggleSyncPlayback", false},
    {"Manipulate", false},
    {"ManipulatePositionXY", false},
    {"ManipulatePositionXZ", false},
    {"ManipulateOrientationAlpha", false},
    {"ManipulateOrientationBeta", false},
    {"ManipulateOrientationGamma", false},
    {"ManipulateOrientationReset", false},
};

// The retail editor's own manual, `editor/qed/QED.doc` in an installed IGI tree
// ("General keystrokes"), documents these commands and their keys:
//
//   Ctrl P            Copy camera position to clipboard
//   Ctrl R            Reset timer
//   Ctrl T            Timer Start / Stop
//   F12 / Shift F12   Increment / Decrement AnimTask speed
//
// The shipped default binds exactly those combos to these events, so the combos
// are pinned here: a rebind is a decision, and losing one would silently move a
// key the manual documents.
struct ImplementedCombo {
    const char* name;
    const char* combo;
};
const ImplementedCombo kIssue48ImplementedCombos[] = {
    {"CameraCopyPosition", "<Ctrl><P>"},
    {"CameraCopyOrientation", "<Ctrl><Shift><O>"},
    {"TimerStartStop", "<Ctrl><T>"},
    {"TimerReset", "<Ctrl><R>"},
    {"TimerIncreaseSpeed", "<F12>"},
    {"TimerDecreaseSpeed", "<Shift><F12>"},
    {"CameraResetRadius", "<Shift><Home>"},
    {"ToggleObjects", "<Ctrl><Shift><Z>"},
};

std::set<std::string> ExpectedRemoved() {
    std::set<std::string> out;
    for (const Decision& d : kDecisions)
        if (!d.implemented) out.insert(d.name);
    return out;
}

// task-11: adjacent silent no-ops, removed from the shipped default and answered
// by a Removed("Name") status-bar message. Listed explicitly (not a blanket
// allowance) so that removing a binding without deciding on it still fails this
// test: a name has to appear here, with a row in KEYBINDINGS.md, to be removed.
//
//   CameraAdjustRadius  no radius adjuster exists; keyAdjustCameraRadius had no
//                       reader and any rate/ladder would have been invented
//   CameraLookDown      keyLookDown had no reader; the mouse owns camera pitch
//   CameraStrafeFree    toggled a flag nothing read and printed "Strafe lock:
//                       ON/OFF" — a status message that lied
const char* kAdjacentRemovals[] = {
    "CameraAdjustRadius",
    "CameraLookDown",
    "CameraStrafeFree",
};

// task-11 rebindings: the loser of a duplicate combo moved, so both handlers stay
// reachable. Pinned here because a rebind is a decision, not a detail.
struct Rebind {
    const char* name;
    const char* combo;
};
const Rebind kTask11Rebinds[] = {
    {"AddModelToRes", "<Ctrl><Shift>M"},     // was <Ctrl><Shift><A>, shared with ToggleAutoSave
    {"ToggleAutoSave", "<Ctrl><Shift><A>"},  // keeps the mnemonic combo
};

std::set<std::string> ExpectedRemovedAll() {
    std::set<std::string> out = ExpectedRemoved();
    for (const char* n : kAdjacentRemovals) out.insert(n);
    return out;
}

// The names the dispatcher answers with a status-bar message.
std::set<std::string> RemovedNamesIn(const std::string& app_input) {
    return ParseRemovedNames(app_input);
}

// ---------------------------------------------------------------------------
// Routed config fields: `eventName == "X") data_.keyY = parsed`
// ---------------------------------------------------------------------------

struct RoutedField {
    std::string event;
    std::string field;
};

std::vector<RoutedField> ParseRoutedFields(const std::string& config_cpp) {
    std::vector<RoutedField> out;
    const std::string needle = "eventName == \"";
    size_t pos = 0;
    while ((pos = config_cpp.find(needle, pos)) != std::string::npos) {
        const size_t ev_start = pos + needle.size();
        const size_t ev_end = config_cpp.find('"', ev_start);
        if (ev_end == std::string::npos) break;
        const size_t assign = config_cpp.find("= parsed", ev_end);
        if (assign == std::string::npos) break;
        const std::string between = config_cpp.substr(ev_end, assign - ev_end);
        const size_t dpos = between.rfind("data_.");
        if (dpos != std::string::npos) {
            static const std::string kDataPrefix = "data_.";
            size_t s = dpos + kDataPrefix.size();
            size_t e = s;
            while (e < between.size() &&
                   (std::isalnum(static_cast<unsigned char>(between[e])) || between[e] == '_')) {
                ++e;
            }
            if (e > s) {
                RoutedField rf;
                rf.event = config_cpp.substr(ev_start, ev_end - ev_start);
                rf.field = between.substr(s, e - s);
                out.push_back(rf);
            }
        }
        pos = assign + 1;
    }
    return out;
}

// True when `name` is answered by a live handler or by a visible Removed() message.
bool HasHandlerOrVisibleMessage(const std::string& dispatchers, const std::string& name) {
    return dispatchers.find("Check(\"" + name + "\")") != std::string::npos ||
           dispatchers.find("CheckCont(\"" + name + "\")") != std::string::npos ||
           dispatchers.find("Removed(\"" + name + "\")") != std::string::npos;
}

// ---------------------------------------------------------------------------
// Fixtures: read each artefact once per test (they are small)
// ---------------------------------------------------------------------------

struct RepoFiles {
    std::string app_input_path;
    std::string app_view_path;
    std::string config_cpp_path;
    std::string config_h_path;
    std::string app_h_path;
    std::string bindings_path;
    std::string doc_path;
    std::string app_input;
    std::string app_view;
    std::string config_cpp;
    std::string config_h;
    std::string app_h;
    std::string bindings;
    std::string doc;
    // Comment-stripped views: the assertions are about code, and comments explain
    // the removals by naming the identifiers that were deleted.
    std::string app_input_code;
    std::string app_view_code;
    std::string config_cpp_code;
    std::string config_h_code;
    std::string app_h_code;
    std::vector<BindingLine> lines;

    // The files that consume bindings: a Check()/CheckCont()/Removed() call in
    // either of these is what makes an advertised binding do something visible.
    std::string Dispatchers() const { return app_input_code + app_view_code; }

    bool Load() {
        std::string tried;
        app_input_path = FindRepoFile("source/app_input.cpp", &tried);
        app_view_path = FindRepoFile("source/app_view.cpp", &tried);
        config_cpp_path = FindRepoFile("source/config.cpp", &tried);
        config_h_path = FindRepoFile("source/config.h", &tried);
        app_h_path = FindRepoFile("source/app.h", &tried);
        bindings_path = FindRepoFile("assets/editor/qed/qedkeybindings.qsc", &tried);
        doc_path = FindRepoFile("docs/igi-re-parity/KEYBINDINGS.md", &tried);
        if (app_input_path.empty() || app_view_path.empty() || config_cpp_path.empty() ||
            config_h_path.empty() || app_h_path.empty() || bindings_path.empty() ||
            doc_path.empty()) {
            return false;
        }
        if (!ReadFile(app_input_path, &app_input)) return false;
        if (!ReadFile(app_view_path, &app_view)) return false;
        if (!ReadFile(config_cpp_path, &config_cpp)) return false;
        if (!ReadFile(config_h_path, &config_h)) return false;
        if (!ReadFile(app_h_path, &app_h)) return false;
        if (!ReadFile(bindings_path, &bindings)) return false;
        if (!ReadFile(doc_path, &doc)) return false;
        app_input_code = StripComments(app_input);
        app_view_code = StripComments(app_view);
        config_cpp_code = StripComments(config_cpp);
        config_h_code = StripComments(config_h);
        app_h_code = StripComments(app_h);
        lines = ParseBindingLines(bindings);
        return true;
    }
};

#define REQUIRE_REPO_FILES()                                                     \
    RepoFiles files;                                                             \
    if (!files.Load()) {                                                         \
        GTEST_SKIP() << "repository files not reachable from this working "      \
                        "directory (source/app_input.cpp, source/app_view.cpp, " \
                        "source/config.cpp, source/config.h, source/app.h, "     \
                        "assets/editor/qed/qedkeybindings.qsc, "                 \
                        "docs/igi-re-parity/KEYBINDINGS.md)";                    \
    }

// ---------------------------------------------------------------------------
// (a) No stub survives
// ---------------------------------------------------------------------------

TEST(Keybindings48, NoStubLineSurvivesInTheDispatcher) {
    REQUIRE_REPO_FILES();
    // The exact string the issue asked to eliminate.
    EXPECT_EQ(files.app_input_code.find("[Keybind]"), std::string::npos)
        << "a '[Keybind] ...' stub line is still present in " << files.app_input_path;
    EXPECT_EQ(files.app_input_code.find("not implemented"), std::string::npos)
        << "a 'not implemented' line is still present in " << files.app_input_path;
}

TEST(Keybindings48, EveryRemovedBindingIsGuardedByAStatusBarMessage) {
    REQUIRE_REPO_FILES();
    // The 23 removals of #48 plus the task-11 adjacent removals, listed explicitly
    // in kAdjacentRemovals. Anything outside those two sets is an undecided
    // removal and fails here.
    const std::set<std::string> expected = ExpectedRemovedAll();
    const std::set<std::string> actual = RemovedNamesIn(files.app_input);

    for (const std::string& name : expected) {
        EXPECT_TRUE(actual.count(name) == 1)
            << name << " is documented as removed but has no Removed(\"" << name
            << "\") guard in " << files.app_input_path;
    }
    for (const std::string& name : actual) {
        EXPECT_TRUE(expected.count(name) == 1)
            << "Removed(\"" << name << "\") is neither one of the 25 bindings of "
               "#48 nor one of the task-11 adjacent removals (see kAdjacentRemovals "
               "and docs/igi-re-parity/KEYBINDINGS.md)";
    }

    // The guard must actually write the status bar, not just log.
    EXPECT_NE(files.app_input.find("status_message_ = std::string(name)"), std::string::npos)
        << "the Removed() guard does not set status_message_";
}

// ---------------------------------------------------------------------------
// (b) The shipped default stops advertising the removed bindings
// ---------------------------------------------------------------------------

TEST(Keybindings48, ShippedDefaultsDoNotAdvertiseRemovedBindings) {
    REQUIRE_REPO_FILES();
    std::set<std::string> bound;
    for (const BindingLine& b : files.lines) {
        if (!b.event.empty()) bound.insert(b.event);
    }
    for (const std::string& name : ExpectedRemovedAll()) {
        EXPECT_TRUE(bound.count(name) == 0)
            << name << " was removed but is still bound in "
            << files.bindings_path << " (line "
            << [&] {
                   for (const BindingLine& b : files.lines)
                       if (b.event == name) return b.line;
                   return 0;
               }()
            << ")";
    }
}

TEST(Keybindings48, ImplementedBindingsAreStillBoundAndDispatched) {
    REQUIRE_REPO_FILES();
    std::set<std::string> bound;
    for (const BindingLine& b : files.lines) {
        if (!b.event.empty()) bound.insert(b.event);
    }
    for (const Decision& d : kDecisions) {
        if (!d.implemented) continue;
        EXPECT_TRUE(bound.count(d.name) == 1)
            << d.name << " is implemented but no longer bound in " << files.bindings_path;
        const std::string handler = "Check(\"" + std::string(d.name) + "\")";
        EXPECT_NE(files.app_input.find(handler), std::string::npos)
            << d.name << " has no " << handler << " handler in " << files.app_input_path;
    }
}

TEST(Keybindings48, DefaultFileIsWellFormedAndNamesAreUnique) {
    REQUIRE_REPO_FILES();
    ASSERT_FALSE(files.lines.empty());

    for (const BindingLine& b : files.lines) {
        EXPECT_FALSE(b.event.empty())
            << "malformed SetEventBinding line " << b.line << " in " << files.bindings_path;
        EXPECT_FALSE(b.combo.empty())
            << "binding " << b.event << " has an empty key combo (line " << b.line << ")";
    }

    std::set<std::string> names;
    for (const BindingLine& b : files.lines) {
        if (b.event.empty()) continue;
        EXPECT_TRUE(names.insert(b.event).second)
            << b.event << " is bound more than once (line " << b.line << ")";
    }
}

// ---------------------------------------------------------------------------
// task-11 (d): one combo, one event
// ---------------------------------------------------------------------------

TEST(Keybindings48, NoComboIsBoundToTwoDifferentEvents) {
    REQUIRE_REPO_FILES();
    ASSERT_FALSE(files.lines.empty());

    // <Alt><Space> was CameraAdjustRadius + CameraStrafeFree and <Ctrl><Shift><A>
    // was ToggleAutoSave + AddModelToRes; whichever handler ran first swallowed the
    // other, so one of the two was a silent no-op. Combos are compared
    // case-insensitively and whitespace-insensitively, the way the editor's parser
    // normalises them (source/config.cpp:342-347).
    std::map<std::string, std::vector<const BindingLine*>> by_combo;
    for (const BindingLine& b : files.lines) {
        if (b.event.empty() || b.combo.empty()) continue;
        std::string key;
        for (const char c : b.combo) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                key.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
        }
        by_combo[key].push_back(&b);
    }

    for (const auto& entry : by_combo) {
        std::set<std::string> events;
        for (const BindingLine* b : entry.second) events.insert(b->event);
        if (events.size() <= 1) continue;
        std::string names;
        for (const std::string& n : events) names += (names.empty() ? "" : " + ") + n;
        EXPECT_EQ(events.size(), 1u)
            << "combo " << entry.second.front()->combo << " is bound to " << names
            << " in " << files.bindings_path << " (line "
            << entry.second.front()->line << "); one of them can never fire";
    }
}

TEST(Keybindings48, DuplicateComboLosersAreReboundOrVisible) {
    REQUIRE_REPO_FILES();
    // The two collisions of task-11 item 4, pinned as decisions: ToggleAutoSave
    // keeps <Ctrl><Shift><A> ("A" for AutoSave) and AddModelToRes moved to
    // <Ctrl><Shift>M ("M" for model), so both handlers stay reachable.
    for (const Rebind& r : kTask11Rebinds) {
        bool found = false;
        for (const BindingLine& b : files.lines) {
            if (b.event != r.name) continue;
            found = true;
            EXPECT_EQ(b.combo, std::string(r.combo))
                << r.name << " is bound to " << b.combo << " (line " << b.line
                << ") but task-11 decided " << r.combo
                << "; update kTask11Rebinds and the doc together";
        }
        EXPECT_TRUE(found) << r.name << " is no longer bound in " << files.bindings_path;
    }
}

// ---------------------------------------------------------------------------
// task-11 (e): every advertised binding does something visible
// ---------------------------------------------------------------------------

TEST(Keybindings48, EveryAdvertisedBindingHasAHandlerOrAVisibleMessage) {
    REQUIRE_REPO_FILES();
    ASSERT_FALSE(files.lines.empty());

    const std::string dispatchers = files.Dispatchers();
    for (const BindingLine& b : files.lines) {
        if (b.event.empty()) continue;
        EXPECT_TRUE(HasHandlerOrVisibleMessage(dispatchers, b.event))
            << b.event << " is advertised in " << files.bindings_path << " (line "
            << b.line << ") but no Check(\"" << b.event << "\"), CheckCont(\""
            << b.event << "\") or Removed(\"" << b.event
            << "\") call answers it: pressing it would be a silent no-op";
    }
}

// ---------------------------------------------------------------------------
// task-11 (f): routed config fields have a live consumer or are gone
// ---------------------------------------------------------------------------

TEST(Keybindings48, AdvertisedRoutedConfigKeysHaveALiveConsumer) {
    REQUIRE_REPO_FILES();
    const std::vector<RoutedField> routed = ParseRoutedFields(files.config_cpp_code);
    ASSERT_FALSE(routed.empty())
        << "no `eventName == \"...\") data_.keyX = parsed` routing found in "
        << files.config_cpp_path << " — did the parser move?";

    std::set<std::string> advertised;
    for (const BindingLine& b : files.lines) {
        if (!b.event.empty()) advertised.insert(b.event);
    }

    const std::string dispatchers = files.Dispatchers();
    size_t checked = 0;
    for (const RoutedField& rf : routed) {
        if (advertised.count(rf.event) == 0) continue;   // not advertised: not user-visible
        ++checked;
        // A routed key is fine when either the named field is read outside
        // config.{h,cpp}, or the event itself is answered in a dispatcher.
        const bool field_read =
            files.app_input.find(rf.field) != std::string::npos ||
            files.app_view.find(rf.field) != std::string::npos ||
            files.app_h.find(rf.field) != std::string::npos;
        EXPECT_TRUE(field_read || HasHandlerOrVisibleMessage(dispatchers, rf.event))
            << rf.event << " is advertised and routed to Config::" << rf.field
            << ", but nothing reads that field and no dispatcher answers the event: "
               "a dead config key (this is exactly the CameraAdjustRadius / "
               "CameraLookDown defect of task-11)";
    }
    EXPECT_GT(checked, 0u) << "no advertised binding is routed through config.cpp";
}

TEST(Keybindings48, Task11DeadConfigFieldsStayDeleted) {
    REQUIRE_REPO_FILES();
    // task-11 items 1/2 deleted the fields and their routing branches: the events
    // are answered from the event map (and, for a stale user file, by the
    // Removed() guard), so a named mirror would only be dead config surface.
    EXPECT_EQ(files.config_h_code.find("keyAdjustCameraRadius"), std::string::npos)
        << "keyAdjustCameraRadius is back in " << files.config_h_path
        << " but nothing reads it (task-11 item 1)";
    EXPECT_EQ(files.config_h_code.find("keyLookDown"), std::string::npos)
        << "keyLookDown is back in " << files.config_h_path
        << " but nothing reads it (task-11 item 2)";
    EXPECT_EQ(files.config_cpp_code.find("data_.keyAdjustCameraRadius"), std::string::npos)
        << "the CameraAdjustRadius routing branch is back in " << files.config_cpp_path;
    EXPECT_EQ(files.config_cpp_code.find("data_.keyLookDown"), std::string::npos)
        << "the CameraLookDown routing branch is back in " << files.config_cpp_path;

    // The events themselves must still be answered, so a stale user file that
    // still binds them gets a message instead of silence.
    EXPECT_NE(files.app_input_code.find("Removed(\"CameraAdjustRadius\")"), std::string::npos);
    EXPECT_NE(files.app_input_code.find("Removed(\"CameraLookDown\")"), std::string::npos);
}

TEST(Keybindings48, CameraStrafeFreeStateAndLyingMessageAreGone) {
    REQUIRE_REPO_FILES();
    // task-11 item 3: the flag had no reader, so "Strafe lock: ON/OFF" announced a
    // state change that never happened. Flag, message and binding all go.
    EXPECT_EQ(files.app_h_code.find("camera_strafe_free_"), std::string::npos)
        << "camera_strafe_free_ is back in " << files.app_h_path << " (task-11 item 3)";
    EXPECT_EQ(files.app_input_code.find("camera_strafe_free_"), std::string::npos)
        << "camera_strafe_free_ is used again in " << files.app_input_path;
    EXPECT_EQ(files.app_input_code.find("Strafe lock"), std::string::npos)
        << "the 'Strafe lock: ON/OFF' message is back in " << files.app_input_path
        << " — it reports a state nothing reads";
}

// ---------------------------------------------------------------------------
// task-11 (g): the ManipulationMode::None drag is an explicit, named no-op
// ---------------------------------------------------------------------------

TEST(Keybindings48, PlainDragNoOpNamesTheModeAndTheWorkingModifiers) {
    REQUIRE_REPO_FILES();
    // app_view.cpp's UpdateMarkerManipulation must tell the user which mode did
    // nothing and which modifiers do work, instead of being a dead branch.
    EXPECT_NE(files.app_view_code.find("Manipulate (None)"), std::string::npos)
        << "the ManipulationMode::None drag no longer names its mode in "
        << files.app_view_path;
    EXPECT_NE(files.app_view_code.find("Shift = view"), std::string::npos)
        << "the plain-drag message does not name the working modifiers in "
        << files.app_view_path;
    EXPECT_NE(files.app_view_code.find("status_message_ = \"Manipulate (None)"), std::string::npos)
        << "the plain-drag no-op does not write the status bar in " << files.app_view_path;
}

// ---------------------------------------------------------------------------
// (c) The decision document covers all 25 bindings
// ---------------------------------------------------------------------------

TEST(Keybindings48, DecisionDocumentCoversEveryBinding) {
    REQUIRE_REPO_FILES();
    for (const Decision& d : kDecisions) {
        EXPECT_NE(files.doc.find(d.name), std::string::npos)
            << d.name << " has no row in " << files.doc_path;
    }
    EXPECT_NE(files.doc.find("Removed"), std::string::npos);
    EXPECT_NE(files.doc.find("Implemented"), std::string::npos);
}

TEST(Keybindings48, DecisionTableIsTheTwentyFiveBindingsOfTheIssue) {
    EXPECT_EQ(sizeof(kDecisions) / sizeof(kDecisions[0]), 25u);
    size_t implemented = 0;
    for (const Decision& d : kDecisions)
        if (d.implemented) ++implemented;
    EXPECT_EQ(implemented, 8u)
        << "issue #48's first pass left two implemented; the camera-clipboard and "
           "animation-timer groups were implementable against facilities the editor "
           "already had (Utils::SetClipboardText, igi::AnimPlayback) and are now "
           "implemented too. If this number changes again, the decision document "
           "and the shipped default must change with it.";
    EXPECT_EQ(ExpectedRemoved().size(), 17u);
    EXPECT_EQ(sizeof(kAdjacentRemovals) / sizeof(kAdjacentRemovals[0]), 3u)
        << "task-11 removed exactly three adjacent bindings; if that changed, the "
           "list and docs/igi-re-parity/KEYBINDINGS.md must change together";
}

// The eight implemented bindings must keep the combos the retail manual and the
// shipped default document — an implemented feature whose key silently moved is
// the same defect as a removed key that is still advertised.
TEST(Keybindings48, ImplementedBindingsKeepTheirDocumentedCombos) {
    REQUIRE_REPO_FILES();
    for (const ImplementedCombo& c : kIssue48ImplementedCombos) {
        bool found = false;
        for (const BindingLine& b : files.lines) {
            if (b.event != c.name) continue;
            found = true;
            EXPECT_EQ(b.combo, std::string(c.combo))
                << c.name << " is bound to " << b.combo << " (line " << b.line
                << ") but issue #48 decided " << c.combo;
        }
        EXPECT_TRUE(found) << c.name << " is implemented but not bound in "
                           << files.bindings_path;
    }
}

// The implemented handlers must go through the pure, host-tested helpers in
// source/runtime/editor_quick_actions.h rather than re-deriving the decision
// inline: that is what makes the behaviour testable off-Windows.
TEST(Keybindings48, ImplementedQuickActionsUseTheHostTestedHelpers) {
    REQUIRE_REPO_FILES();
    EXPECT_NE(files.app_input.find("Utils::SetClipboardText"), std::string::npos)
        << "CameraCopyPosition/CameraCopyOrientation do not use the editor's "
           "existing clipboard (source/utils.cpp:445)";
    EXPECT_NE(files.app_input.find("FormatCameraTripleForClipboard"), std::string::npos)
        << "the camera copy handlers do not use the tested formatter";
    EXPECT_NE(files.app_input.find("AnimTimerSpeedIndex"), std::string::npos)
        << "TimerIncreaseSpeed/TimerDecreaseSpeed do not use the tested speed ladder";
    EXPECT_NE(files.app_input.find("pb->Reset()"), std::string::npos)
        << "TimerReset does not rewind the playback clock (AnimPlayback::Reset)";
    EXPECT_NE(files.app_input.find("pb->Pause()"), std::string::npos)
        << "TimerStartStop does not stop the playback clock (AnimPlayback::Pause)";
}

TEST(Keybindings48, Task11DocumentRecordsTheFiveAdjacentDecisions) {
    REQUIRE_REPO_FILES();
    // The second table of the doc: each of the five task-11 items must be recorded
    // with what the user now sees, and the doc must state plainly that these are
    // editor-only decisions with no IGI-RE oracle.
    for (const char* name : kAdjacentRemovals) {
        EXPECT_NE(files.doc.find(name), std::string::npos)
            << name << " has no row in " << files.doc_path;
    }
    for (const char* needed : {"task-11", "ManipulationMode::None", "editor-only"}) {
        EXPECT_NE(files.doc.find(needed), std::string::npos)
            << "the doc does not mention " << needed;
    }
    EXPECT_NE(files.doc.find("AddModelToRes"), std::string::npos)
        << "the doc does not record the duplicate-combo rebind of AddModelToRes";
    EXPECT_NE(files.doc.find("<Ctrl><Shift>M"), std::string::npos)
        << "the doc does not record the new AddModelToRes combo";
}

}  // namespace
