#pragma once

#include "logger.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace igi {

// Editor-side diagnostic seam, in the shape MissionTextLogSink already uses
// (mission_text_resource.h): this module is host-portable and must not call
// Logger itself (Logger::Log -> Config::Get(), and config.cpp is not
// host-portable), so the application injects the sink. A test can install its
// own and assert on the exact text.
using MissionExpressionLogSink = std::function<void(LogLevel, const std::string&)>;

// Small, deterministic expression state used by authored mission objectives.
// Values are numeric because vanilla QSC represents bool/int/real task fields
// through the same expression language; booleans are stored as 0 or 1.
//
// The grammar covers what the authored corpus needs: || && == != < <= > >=
// + - * / unary ! + - parentheses, numeric and `this.`-qualified state
// variables, the constants TRUE/FALSE and GAME_FREQUENCY, and the zero-argument
// script function LevelFlow_GetBreakCutSceneKey().
//
// KNOWINGLY UNPARSED (issue #56). The shipped OBJECTS.QVM of every level calls
// these script functions and this evaluator still cannot parse the expressions
// carrying them, so those objectives/containers evaluate as "not met" and are
// reported through the log sink instead of disappearing in silence:
//
//   MenuManager_SetEnabled(FALSE|TRUE)  3 call sites per level (2 in LEVEL14)
//   Game_DisableMusic()                 2 per level (1 in LEVEL14)
//   Game_EnableMusic()                  1 per level
//   Game_CutsceneDelete()               1 per level (0 in LEVEL14)
//
// (Counts measured over MISSIONS/location0/LEVEL*/OBJECTS.QVM in the installed
// game with `grep -a -o`.) They are deliberately not implemented: each mutates
// menu, music or cutscene state, which is a design decision rather than a
// mechanical port. Only LevelFlow_GetBreakCutSceneKey is input-only and fully
// specified by the oracle, so it is the one this grammar supports.
class MissionExpressionState {
public:
    void SetBoolean(const std::string& variable_name, bool value);
    void SetNumber(const std::string& variable_name, double value);
    // Clears the values and the unparsed-expression report: a new mission is a
    // new report.
    void Clear();

    // LevelFlow_GetBreakCutSceneKey HOOK 0x488E00 (src/levelflow/levelflow.c:73-82)
    // reads `gKeys[1] & 0x80` -- the DirectInput keyboard state
    // (src/win/winvars.h:13, filled by Keyboard_Update HOOK 0x490230,
    // src/win/input.c:296-315). Index 1 is Escape: the engine spells that
    // constant twice, `#define KEY_ESCAPE 1` (src/widget/controlsmenu.c:64) and
    // `#define KEY_ESCAPE 0x01` (src/game/introscreen.c:81), and masks both with
    // 0x80. The evaluator has no keyboard of its own, so the host supplies the
    // bit.
    void SetBreakCutSceneKeyHeld(bool held);

    // Returns false for malformed expressions or variables absent from the
    // state. A successful evaluation writes the boolean result to out_result.
    bool TryEvaluate(const std::string& expression, bool& out_result) const;
    // Numeric counterpart used by vanilla EditVariable add/sub expressions.
    // Boolean expressions evaluate to 0 or 1, matching the script runtime.
    bool TryEvaluateNumber(const std::string& expression, double& out_result) const;

    // Authored expressions this evaluator could not parse, in first-seen order
    // and deduplicated. Retail has no counterpart: an expression it cannot parse
    // is rejected when the QSC is compiled, so there is no engine oracle for
    // the text. Consumers read a failed evaluation as "condition not met",
    // which is why the strings are recorded here as well as sent to the sink.
    const std::vector<std::string>& GetUnparsedExpressions() const {
        return unparsed_expressions_;
    }

    void SetLogSink(MissionExpressionLogSink log_sink) {
        log_sink_ = std::move(log_sink);
    }

private:
    // Reports one unparseable expression to the sink, once per distinct string.
    void ReportUnparsed(const std::string& expression) const;

    std::unordered_map<std::string, double> values_;
    bool break_cutscene_key_held_ = false;
    MissionExpressionLogSink log_sink_;
    mutable std::unordered_set<std::string> reported_unparsed_;
    mutable std::vector<std::string> unparsed_expressions_;
};

} // namespace igi
