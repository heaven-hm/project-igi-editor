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
