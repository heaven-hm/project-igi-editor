#include "mission_expression.h"

#include "game_clock.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string_view>
#include <utility>

namespace igi {
namespace {

using VariableResolver = std::function<bool(std::string_view, double&)>;
// Zero-argument script functions. Retail registers them with an argument count
// of 0 (Script_RegisterFunction("LevelFlow_GetBreakCutSceneKey",
// LevelFlow_GetBreakCutSceneKey, NULL, 0), src/levelflow/levelflow.c:66) and the
// authored corpus calls them as `Name()`.
using FunctionResolver = std::function<bool(std::string_view, double&)>;

class ExpressionParser {
public:
    ExpressionParser(std::string_view expression,
                     VariableResolver resolver,
                     FunctionResolver function_resolver)
        : expression_(expression),
          resolver_(std::move(resolver)),
          function_resolver_(std::move(function_resolver)) {}

    bool TryParse(bool& out_result) {
        double numeric_result = 0.0;
        if (!TryParseNumber(numeric_result)) {
            return false;
        }
        out_result = IsTruthy(numeric_result);
        return true;
    }

    bool TryParseNumber(double& out_result) {
        if (!ParseOrExpression(out_result)) {
            return false;
        }
        SkipWhitespace();
        return cursor_ == expression_.size();
    }

private:
    static bool IsTruthy(double value) {
        return std::abs(value) > 0.0000001;
    }

    void SkipWhitespace() {
        while (cursor_ < expression_.size() &&
               std::isspace(static_cast<unsigned char>(expression_[cursor_]))) {
            ++cursor_;
        }
    }

    bool Match(std::string_view token) {
        SkipWhitespace();
        if (expression_.substr(cursor_, token.size()) != token) {
            return false;
        }
        cursor_ += token.size();
        return true;
    }

    bool ParseOrExpression(double& out_value) {
        if (!ParseAndExpression(out_value)) {
            return false;
        }
        while (Match("||")) {
            double right_value = 0.0;
            if (!ParseAndExpression(right_value)) {
                return false;
            }
            out_value = IsTruthy(out_value) || IsTruthy(right_value) ? 1.0 : 0.0;
        }
        return true;
    }

    bool ParseAndExpression(double& out_value) {
        if (!ParseEqualityExpression(out_value)) {
            return false;
        }
        while (Match("&&")) {
            double right_value = 0.0;
            if (!ParseEqualityExpression(right_value)) {
                return false;
            }
            out_value = IsTruthy(out_value) && IsTruthy(right_value) ? 1.0 : 0.0;
        }
        return true;
    }

    bool ParseEqualityExpression(double& out_value) {
        if (!ParseRelationalExpression(out_value)) {
            return false;
        }
        while (true) {
            const bool equals = Match("==");
            const bool not_equals = !equals && Match("!=");
            if (!equals && !not_equals) {
                return true;
            }

            double right_value = 0.0;
            if (!ParseRelationalExpression(right_value)) {
                return false;
            }
            const bool comparison = equals
                ? out_value == right_value
                : out_value != right_value;
            out_value = comparison ? 1.0 : 0.0;
        }
    }

    bool ParseRelationalExpression(double& out_value) {
        if (!ParseAdditiveExpression(out_value)) {
            return false;
        }
        while (true) {
            enum class Comparison { None, Less, LessOrEqual, Greater, GreaterOrEqual };
            Comparison comparison = Comparison::None;
            if (Match("<=")) {
                comparison = Comparison::LessOrEqual;
            } else if (Match(">=")) {
                comparison = Comparison::GreaterOrEqual;
            } else if (Match("<")) {
                comparison = Comparison::Less;
            } else if (Match(">")) {
                comparison = Comparison::Greater;
            } else {
                return true;
            }

            double right_value = 0.0;
            if (!ParseAdditiveExpression(right_value)) {
                return false;
            }
            switch (comparison) {
                case Comparison::Less:
                    out_value = out_value < right_value ? 1.0 : 0.0;
                    break;
                case Comparison::LessOrEqual:
                    out_value = out_value <= right_value ? 1.0 : 0.0;
                    break;
                case Comparison::Greater:
                    out_value = out_value > right_value ? 1.0 : 0.0;
                    break;
                case Comparison::GreaterOrEqual:
                    out_value = out_value >= right_value ? 1.0 : 0.0;
                    break;
                case Comparison::None:
                    return false;
            }
        }
    }

    bool ParseAdditiveExpression(double& out_value) {
        if (!ParseMultiplicativeExpression(out_value)) {
            return false;
        }
        while (true) {
            const bool add = Match("+");
            const bool subtract = !add && Match("-");
            if (!add && !subtract) {
                return true;
            }

            double right_value = 0.0;
            if (!ParseMultiplicativeExpression(right_value)) {
                return false;
            }
            out_value = add ? out_value + right_value : out_value - right_value;
        }
    }

    bool ParseMultiplicativeExpression(double& out_value) {
        if (!ParseUnaryExpression(out_value)) {
            return false;
        }
        while (true) {
            const bool multiply = Match("*");
            const bool divide = !multiply && Match("/");
            if (!multiply && !divide) {
                return true;
            }

            double right_value = 0.0;
            if (!ParseUnaryExpression(right_value)) {
                return false;
            }
            if (divide && std::abs(right_value) <= 0.0000001) {
                return false;
            }
            out_value = multiply ? out_value * right_value : out_value / right_value;
        }
    }

    bool ParseUnaryExpression(double& out_value) {
        if (Match("!")) {
            if (!ParseUnaryExpression(out_value)) {
                return false;
            }
            out_value = IsTruthy(out_value) ? 0.0 : 1.0;
            return true;
        }
        if (Match("-")) {
            if (!ParseUnaryExpression(out_value)) {
                return false;
            }
            out_value = -out_value;
            return true;
        }
        if (Match("+")) {
            return ParseUnaryExpression(out_value);
        }
        return ParsePrimaryExpression(out_value);
    }

    bool ParsePrimaryExpression(double& out_value) {
        SkipWhitespace();
        if (Match("(")) {
            if (!ParseOrExpression(out_value) || !Match(")")) {
                return false;
            }
            return true;
        }

        if (cursor_ >= expression_.size()) {
            return false;
        }

        const char current_character = expression_[cursor_];
        if (std::isdigit(static_cast<unsigned char>(current_character)) ||
            current_character == '.') {
            return ParseNumber(out_value);
        }
        return ParseIdentifier(out_value);
    }

    bool ParseNumber(double& out_value) {
        const char* number_start = expression_.data() + cursor_;
        char* number_end = nullptr;
        out_value = std::strtod(number_start, &number_end);
        if (number_end == number_start) {
            return false;
        }
        cursor_ = static_cast<size_t>(number_end - expression_.data());
        return std::isfinite(out_value);
    }

    bool ParseIdentifier(double& out_value) {
        SkipWhitespace();
        const size_t identifier_start = cursor_;
        while (cursor_ < expression_.size()) {
            const unsigned char character =
                static_cast<unsigned char>(expression_[cursor_]);
            if (!std::isalnum(character) && character != '_' && character != '.') {
                break;
            }
            ++cursor_;
        }
        if (identifier_start == cursor_) {
            return false;
        }

        const std::string_view identifier = expression_.substr(
            identifier_start,
            cursor_ - identifier_start);
        if (identifier == "TRUE") {
            out_value = 1.0;
            return true;
        }
        if (identifier == "FALSE") {
            out_value = 0.0;
            return true;
        }

        // A script function call, `Name()`. Retail's script functions carry an
        // argument count (Script_RegisterFunction, src/script/symbol.c:146-150)
        // and the authored corpus only ever calls the zero-argument ones, so an
        // argument list is not part of this grammar: `Name(arg)` fails to parse
        // (and is reported) rather than being silently accepted.
        SkipWhitespace();
        if (cursor_ < expression_.size() && expression_[cursor_] == '(') {
            ++cursor_;
            SkipWhitespace();
            if (cursor_ >= expression_.size() || expression_[cursor_] != ')') {
                return false;
            }
            ++cursor_;
            return function_resolver_(identifier, out_value);
        }
        return resolver_(identifier, out_value);
    }

    std::string_view expression_;
    VariableResolver resolver_;
    FunctionResolver function_resolver_;
    size_t cursor_ = 0;
};

} // namespace

void MissionExpressionState::SetBoolean(const std::string& variable_name, bool value) {
    values_[variable_name] = value ? 1.0 : 0.0;
}

void MissionExpressionState::SetNumber(const std::string& variable_name, double value) {
    values_[variable_name] = value;
}

void MissionExpressionState::Clear() {
    values_.clear();
    reported_unparsed_.clear();
    unparsed_expressions_.clear();
}

void MissionExpressionState::SetBreakCutSceneKeyHeld(bool held) {
    break_cutscene_key_held_ = held;
}

void MissionExpressionState::ReportUnparsed(const std::string& expression) const {
    if (!reported_unparsed_.insert(expression).second) {
        return;
    }
    unparsed_expressions_.push_back(expression);
    if (log_sink_) {
        log_sink_(
            LogLevel::WARNING,
            "[MissionExpression] cannot parse authored condition (treated as "
            "not met): " + expression);
    }
}

bool MissionExpressionState::TryEvaluate(
    const std::string& expression,
    bool& out_result) const {
    double numeric_result = 0.0;
    if (!TryEvaluateNumber(expression, numeric_result)) {
        return false;
    }
    out_result = std::abs(numeric_result) > 0.0000001;
    return true;
}

bool MissionExpressionState::TryEvaluateNumber(
    const std::string& expression,
    double& out_result) const {
    const VariableResolver variable_resolver = [this](
        std::string_view variable_name,
        double& out_value) {
        if (variable_name == "GAME_FREQUENCY") {
            // The engine's tick rate. Oracle: Game_Init registers
            // Script_RegisterConstant("GAME_FREQUENCY", 30) (src/game/game.c:262)
            // and the engine counts every authored duration at that same rate
            // (#define FPS 30.0f, src/humanstate/humanstate.c:91, feeding
            // FRAMES(), src/humanstate/humanstate.c:520).
            //
            // Resolved from the simulation clock's constant instead of a second
            // literal: a mission threshold expressed in GAME_FREQUENCY and the
            // clock that counts the ticks were independent 30s before this, and
            // nothing asserted they agreed. tests/test_mission_expression.cpp
            // now fails if either side moves.
            out_value = static_cast<double>(GameClock::TICK_RATE_HZ);
            return true;
        }

        const auto value_iterator = values_.find(std::string(variable_name));
        if (value_iterator == values_.end()) {
            return false;
        }
        out_value = value_iterator->second;
        return true;
    };

    const FunctionResolver function_resolver = [this](
        std::string_view function_name,
        double& out_value) {
        // LevelFlow_GetBreakCutSceneKey HOOK 0x488E00
        // (src/levelflow/levelflow.c:73-82):
        //
        //     int key = gKeys[1] & 0x80;
        //
        // so the value is the masked DirectInput byte -- 128 while Escape is
        // held, 0 while it is not -- not the 1 the engine's own comment claims.
        // The authored corpus only ever tests it for truth
        // ("EditVariable_4004.nValue == 0 && LevelFlow_GetBreakCutSceneKey()",
        // LEVEL1/OBJECTS.QVM), but the exact value is reproduced anyway.
        if (function_name == "LevelFlow_GetBreakCutSceneKey") {
            out_value = break_cutscene_key_held_ ? 128.0 : 0.0;
            return true;
        }

        // MenuManager_SetEnabled, Game_DisableMusic, Game_EnableMusic and
        // Game_CutsceneDelete are deliberately NOT implemented: each mutates
        // menu, music or cutscene state, so implementing one is a design
        // decision rather than a mechanical port (see mission_expression.h for
        // the call-site counts). They stay unparsed, and the caller reports
        // them instead of silently answering "not met".
        return false;
    };

    ExpressionParser parser(expression, variable_resolver, function_resolver);
    if (parser.TryParseNumber(out_result)) {
        return true;
    }

    // Unchanged semantics: an unparseable expression still fails the call, so
    // every consumer keeps reading it as "condition not met". It is now also
    // recorded and reported once per distinct string instead of vanishing.
    ReportUnparsed(expression);
    return false;
}

} // namespace igi
