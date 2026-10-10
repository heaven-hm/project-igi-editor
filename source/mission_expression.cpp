#include "mission_expression.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <string_view>
#include <utility>

namespace igi {
namespace {

using VariableResolver = std::function<bool(std::string_view, double&)>;

class ExpressionParser {
public:
    ExpressionParser(std::string_view expression, VariableResolver resolver)
        : expression_(expression), resolver_(std::move(resolver)) {}

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
        return resolver_(identifier, out_value);
    }

    std::string_view expression_;
    VariableResolver resolver_;
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
            out_value = 30.0;
            return true;
        }

        const auto value_iterator = values_.find(std::string(variable_name));
        if (value_iterator == values_.end()) {
            return false;
        }
        out_value = value_iterator->second;
        return true;
    };

    ExpressionParser parser(expression, variable_resolver);
    return parser.TryParseNumber(out_result);
}

} // namespace igi
