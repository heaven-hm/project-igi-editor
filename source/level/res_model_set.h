#pragma once
#include <string>
#include <unordered_set>
#include "../renderer/res_writer.h"

// Set of model ids (NNN_NN_N) packed as <id>.mef entries inside a level .res.
// Used to warn when an object references a model the game archive lacks.
class ResModelSet {
public:
    ResModelSet() = default;
    explicit ResModelSet(const RESFile& res);
    // Add a single .res entry by name (e.g. "models\\426_02_1.mef"). Non-.mef
    // entries are ignored. Lets callers stream names without materializing a RESFile.
    void AddEntry(const std::string& entryName);
    bool Contains(const std::string& modelId) const;
    bool Empty() const { return ids_.empty(); }
private:
    std::unordered_set<std::string> ids_; // lower-cased model ids
};
