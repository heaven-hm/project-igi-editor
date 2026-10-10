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

inline std::string SignatureToken(std::string_view kind, std::string_view value) {
    return std::string(kind) + std::to_string(value.size()) + ":" + std::string(value);
}

inline std::string AnonymousArgumentSignature(const qsc::Node& node) {
    std::string signature;
    switch (node.kind) {
    case qsc::NodeKind::IntLit:
        return SignatureToken("int:", std::to_string(node.i_val));
    case qsc::NodeKind::FloatLit:
        return SignatureToken("float:", StableNumberText(static_cast<double>(node.f_val)));
    case qsc::NodeKind::BoolLit:
        return SignatureToken("bool:", node.b_val ? "true" : "false");
    case qsc::NodeKind::StringLit:
        return SignatureToken("string:", node.s_val);
    case qsc::NodeKind::IdentLit:
        return SignatureToken("ident:", node.s_val);
    case qsc::NodeKind::Call:
        signature = SignatureToken("call:", node.s_val) + "(";
        for (const auto& child : node.children) {
            signature += AnonymousArgumentSignature(*child);
            signature.push_back(';');
        }
        signature.push_back(')');
        return signature;
    case qsc::NodeKind::Unary:
    case qsc::NodeKind::Binary:
        signature = SignatureToken(node.kind == qsc::NodeKind::Unary ? "unary:" : "binary:",
                                   node.s_val) + "(";
        for (const auto& child : node.children) {
            signature += AnonymousArgumentSignature(*child);
            signature.push_back(';');
        }
        signature.push_back(')');
        return signature;
    default: return SignatureToken("expression:", "");
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

inline std::optional<std::string> AnonymousTaskMarkerBefore(std::string_view source,
                                                              std::size_t position) {
    while (position > 0 && std::isspace(static_cast<unsigned char>(source[position - 1]))) --position;
    if (position < 2 || source.substr(position - 2, 2) != "*/") return std::nullopt;

    constexpr std::string_view prefix = "/* mcp-id:";
    const std::size_t marker_begin = source.rfind(prefix, position);
    if (marker_begin == std::string_view::npos) return std::nullopt;
    const std::size_t marker_end = source.find("*/", marker_begin + prefix.size());
    if (marker_end == std::string_view::npos || marker_end + 2 != position) return std::nullopt;
    std::size_t id_begin = marker_begin + prefix.size();
    std::size_t id_end = marker_end;
    while (id_end > id_begin && std::isspace(static_cast<unsigned char>(source[id_end - 1]))) --id_end;
    const std::string_view id = source.substr(id_begin, id_end - id_begin);
    if (id.empty() || id.size() > 128) return std::nullopt;
    return std::string(id);
}

inline void AddAnonymousTaskMarker(std::string& source, std::size_t position,
                                   std::string_view task_id) {
    if (AnonymousTaskMarkerBefore(source, position).has_value()) return;
    source.insert(position, "/* mcp-id:" + std::string(task_id) + " */ ");
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
