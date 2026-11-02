#pragma once

#include "../level/qsc_lexer.h"
#include "../level/qsc_parser.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cctype>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace mcp {

inline std::string StableNumberText(double value) {
    char buffer[128]{};
    const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (result.ec == std::errc{}) return std::string(buffer, result.ptr);

    std::ostringstream output;
    output << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    return output.str();
}

inline std::string AnonymousArgumentSignature(const qsc::Node& node) {
    std::string signature;
    switch (node.kind) {
    case qsc::NodeKind::IntLit: return std::to_string(node.i_val);
    case qsc::NodeKind::FloatLit: return StableNumberText(static_cast<double>(node.f_val));
    case qsc::NodeKind::BoolLit: return node.b_val ? "true" : "false";
    case qsc::NodeKind::StringLit:
    case qsc::NodeKind::IdentLit: return node.s_val;
    case qsc::NodeKind::Call:
        signature = "call:" + node.s_val + "(";
        for (const auto& child : node.children) {
            signature += AnonymousArgumentSignature(*child);
            signature.push_back(';');
        }
        signature.push_back(')');
        return signature;
    case qsc::NodeKind::Unary:
    case qsc::NodeKind::Binary:
        signature = node.kind == qsc::NodeKind::Unary ? "unary:" : "binary:";
        signature += node.s_val + "(";
        for (const auto& child : node.children) {
            signature += AnonymousArgumentSignature(*child);
            signature.push_back(';');
        }
        signature.push_back(')');
        return signature;
    default: return "expression";
    }
}

inline std::string AnonymousArgumentSignature(std::string_view source) {
    const qsc::LexResult lexed = qsc::Lex(std::string(source) + ";");
    if (!lexed.ok) return "expression";
    const qsc::ParseResult parsed = qsc::Parse(lexed.tokens);
    if (!parsed.ok || !parsed.program || parsed.program->children.size() != 1 ||
        parsed.program->children.front()->kind != qsc::NodeKind::ExprStmt ||
        parsed.program->children.front()->children.empty()) return "expression";
    return AnonymousArgumentSignature(*parsed.program->children.front()->children.front());
}

inline std::string AnonymousTaskId(std::string_view parent_id,
                                   std::string_view stable_signature) {
    constexpr std::uint64_t kOffsetBasis = 14695981039346656037ull;
    constexpr std::uint64_t kPrime = 1099511628211ull;
    std::uint64_t hash = kOffsetBasis;
    const auto mix = [&](std::string_view value) {
        for (const unsigned char character : value) {
            hash ^= character;
            hash *= kPrime;
        }
    };
    mix(parent_id);
    mix("|");
    mix(stable_signature);
    mix("|");

    std::ostringstream result;
    result << "anon-" << std::hex << hash;
    return result.str();
}

// The engine's script reader knows only LINE comments: Reader_ReadLine
// HOOK(0x4BEFE0), src/qsc/reader.c:104-107 cuts a line at "//" and nothing else,
// and Lexer_Next HOOK(0x4BFC70), src/qsc/lexer.c:114-138 lexes "/*" as two
// ordinary operators.  A block-comment marker therefore turns the written .qsc
// into a file the game cannot parse, so the marker is a "//" line of its own.
inline std::optional<std::string> AnonymousTaskMarkerBefore(std::string_view source,
                                                              std::size_t position) {
    // Start of the line `position` sits on.
    std::size_t line_begin = 0;
    if (position > 0) {
        const std::size_t nl = source.rfind('\n', position - 1);
        line_begin = (nl == std::string_view::npos) ? 0 : nl + 1;
    }
    if (line_begin == 0) return std::nullopt;  // no preceding line

    // The candidate marker line is the whole line above.
    const std::size_t marker_end = line_begin - 1;  // index of the '\n'
    const std::size_t prev_nl =
        (marker_end == 0) ? std::string_view::npos : source.rfind('\n', marker_end - 1);
    const std::size_t marker_begin = (prev_nl == std::string_view::npos) ? 0 : prev_nl + 1;

    std::string_view line = source.substr(marker_begin, marker_end - marker_begin);
    constexpr std::string_view prefix = "// mcp-id:";
    if (line.size() < prefix.size() || line.substr(0, prefix.size()) != prefix)
        return std::nullopt;

    std::string_view id = line.substr(prefix.size());
    while (!id.empty() && std::isspace(static_cast<unsigned char>(id.back()))) id.remove_suffix(1);
    if (id.empty() || id.size() > 128) return std::nullopt;
    return std::string(id);
}

inline void AddAnonymousTaskMarker(std::string& source, std::size_t position,
                                   std::string_view task_id) {
    if (AnonymousTaskMarkerBefore(source, position).has_value()) return;
    std::size_t line_begin = 0;
    if (position > 0) {
        const std::size_t nl = source.rfind('\n', position - 1);
        line_begin = (nl == std::string::npos) ? 0 : nl + 1;
    }
    // A line comment has to start a line, so the marker goes above the call
    // rather than inline before it.
    source.insert(line_begin, "// mcp-id:" + std::string(task_id) + "\n");
}

inline std::string UniqueTaskId(std::string_view base_id,
                               std::unordered_map<std::string, int>& next_suffix,
                               std::unordered_set<std::string>& used_ids) {
    std::string candidate(base_id);
    int& suffix = next_suffix[std::string(base_id)];
    if (suffix == 0) suffix = 1;
    while (!used_ids.insert(candidate).second) {
        candidate = std::string(base_id) + "#" + std::to_string(suffix++);
    }
    return candidate;
}

}  // namespace mcp
