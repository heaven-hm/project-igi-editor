// magic_object_registry.cpp - Parsed magic-object definitions shared by runtime asset bridges.
#include "magic_object_registry.h"

#include <sstream>
#include <utility>

#include "../level/qvm_decompiler.h"
#include "../level/qvm_parser.h"

namespace igi {

namespace {

bool ReadNextQuotedString(
    const std::string& line,
    size_t& search_offset,
    std::string& value) {
    const size_t opening_quote = line.find('"', search_offset);
    if (opening_quote == std::string::npos) {
        return false;
    }

    const size_t closing_quote = line.find('"', opening_quote + 1);
    if (closing_quote == std::string::npos) {
        return false;
    }

    value = line.substr(opening_quote + 1, closing_quote - opening_quote - 1);
    search_offset = closing_quote + 1;
    return true;
}

// Everything after the task-type token, split on top-level commas and trimmed.
// A quoted string keeps no quotes (`Script_GetArgString` returns the text), and
// the call's closing `)` plus any trailing `,`/`;` is dropped. Nested calls are
// not expected in a DefineMagicObj body and stay one opaque token if they appear.
std::vector<std::string> SplitTrailingArgs(const std::string& line, size_t start) {
    std::vector<std::string> out;
    std::string current;
    int depth = 0;
    bool in_quotes = false;

    auto trim = [](std::string s) {
        size_t b = 0;
        while (b < s.size() && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
        size_t e = s.size();
        while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
        return s.substr(b, e - b);
    };
    auto flush = [&]() {
        std::string token = trim(current);
        current.clear();
        if (token.empty()) return;
        if (token.size() >= 2 && token.front() == '"' && token.back() == '"') {
            token = token.substr(1, token.size() - 2);
        }
        out.push_back(std::move(token));
    };

    for (size_t i = start; i < line.size(); ++i) {
        const char c = line[i];
        if (in_quotes) {
            current += c;
            if (c == '"') in_quotes = false;
            continue;
        }
        if (c == '"') {
            in_quotes = true;
            current += c;
        } else if (c == '(') {
            ++depth;
            current += c;
        } else if (c == ')') {
            if (depth == 0) break;
            --depth;
            current += c;
        } else if (c == ',' && depth == 0) {
            flush();
        } else {
            current += c;
        }
    }
    flush();
    return out;
}

} // namespace

bool MagicObjectRegistry::LoadDecompiledSource(std::string_view decompiled_source) {
    definitions_.clear();
    definition_index_by_name_.clear();

    std::istringstream source_stream{std::string(decompiled_source)};
    std::string line;
    while (std::getline(source_stream, line)) {
        if (line.find("DefineMagicObj") == std::string::npos) {
            continue;
        }

        size_t search_offset = 0;
        MagicObjectDefinition definition;
        if (!ReadNextQuotedString(line, search_offset, definition.attachment_name) ||
            !ReadNextQuotedString(line, search_offset, definition.model_id)) {
            continue;
        }

        const size_t type_start = line.find("TASKTYPE_", search_offset);
        if (type_start == std::string::npos) {
            continue;
        }
        const size_t type_end = line.find_first_of(" ,);\t\r\n", type_start);
        definition.task_type_name = line.substr(
            type_start,
            type_end == std::string::npos ? std::string::npos : type_end - type_start);

        // The DefineMagicObj data block, in order after the type token
        // (src/magicobj/magicobj.c:189-220 hands the type's own handlers
        // `args` + `index = 3`). Only stored; each consumer interprets it.
        if (type_end != std::string::npos) {
            definition.data_args = SplitTrailingArgs(line, type_end);
        }

        if (definition_index_by_name_.find(definition.attachment_name) !=
            definition_index_by_name_.end()) {
            continue;
        }

        definition_index_by_name_[definition.attachment_name] = definitions_.size();
        definitions_.push_back(std::move(definition));
    }

    return true;
}

bool MagicObjectRegistry::LoadQvmFile(const std::string& qvm_path) {
    const QVMFile parsed_file = QVM_Parse(qvm_path);
    if (!parsed_file.valid) {
        definitions_.clear();
        definition_index_by_name_.clear();
        return false;
    }

    return LoadDecompiledSource(QVM_DecompileToString(parsed_file));
}

const MagicObjectDefinition* MagicObjectRegistry::Find(
    std::string_view attachment_name) const {
    const auto definition_iterator = definition_index_by_name_.find(
        std::string(attachment_name));
    if (definition_iterator == definition_index_by_name_.end()) {
        return nullptr;
    }
    return &definitions_[definition_iterator->second];
}

bool MagicObjectRegistry::IsLadderAttachment(std::string_view attachment_name) const {
    const MagicObjectDefinition* definition = Find(attachment_name);
    return definition != nullptr && definition->task_type_name == "TASKTYPE_LADDER";
}

} // namespace igi
