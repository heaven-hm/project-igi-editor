// magic_object_registry.h - Parsed magic-object definitions shared by runtime asset bridges.
#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace igi {

struct MagicObjectDefinition {
    std::string attachment_name;
    std::string model_id;
    std::string task_type_name;
};

class MagicObjectRegistry final {
public:
    // Loads the decompiled DefineMagicObj source emitted by magicobj.qvm.
    // The first definition for a name wins, matching the reference lookup.
    bool LoadDecompiledSource(std::string_view decompiled_source);

    // Loads and decompiles a vanilla magicobj.qvm file.
    bool LoadQvmFile(const std::string& qvm_path);

    const MagicObjectDefinition* Find(std::string_view attachment_name) const;
    bool IsLadderAttachment(std::string_view attachment_name) const;
    const std::vector<MagicObjectDefinition>& GetDefinitions() const {
        return definitions_;
    }

private:
    std::vector<MagicObjectDefinition> definitions_;
    std::unordered_map<std::string, size_t> definition_index_by_name_;
};

} // namespace igi
