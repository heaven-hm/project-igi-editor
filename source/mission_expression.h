#pragma once

#include <string>
#include <unordered_map>

namespace igi {

// Small, deterministic expression state used by authored mission objectives.
// Values are numeric because vanilla QSC represents bool/int/real task fields
// through the same expression language; booleans are stored as 0 or 1.
class MissionExpressionState {
public:
    void SetBoolean(const std::string& variable_name, bool value);
    void SetNumber(const std::string& variable_name, double value);
    void Clear();

    // Returns false for malformed expressions or variables absent from the
    // state. A successful evaluation writes the boolean result to out_result.
    bool TryEvaluate(const std::string& expression, bool& out_result) const;
    // Numeric counterpart used by vanilla EditVariable add/sub expressions.
    // Boolean expressions evaluate to 0 or 1, matching the script runtime.
    bool TryEvaluateNumber(const std::string& expression, double& out_result) const;

private:
    std::unordered_map<std::string, double> values_;
};

} // namespace igi
