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
    // The literal arguments that FOLLOW the task type in the DefineMagicObj
    // call, in order, with quotes stripped. `DefineMagicObj(name, model, type,
    // data...)` parses `data` with the type's own handler
    // (src/magicobj/magicobj.c:189-220: size event, allocate, init event with
    // `index = 3`), and the AIStationaryGun handler reads them as
    // weapon, yawRange, pitchMax, pitchMin, ammo, pitchSpeed, yawSpeed, sound,
    // spinSpeed, range (AIStationaryGun_ParseDef HOOK 0x46FA50,
    // src/binocular/stationarygun.c:395-411). data_args[0] is therefore the
    // weapon key for TASKTYPE_AISTATIONARYGUN. Kept as strings: no interpretation
    // happens here, so a definition any caller does not understand is inert.
    std::vector<std::string> data_args;
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
