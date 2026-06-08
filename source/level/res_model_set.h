#pragma once
#include <string>
#include <unordered_set>
#include "parsers/res_parser.h"

// Set of model ids (NNN_NN_N) packed as <id>.mef entries inside a level .res.
// Used to warn when an object references a model the game archive lacks.
class ResModelSet {
public:
    ResModelSet() = default;
    explicit ResModelSet(const RESFile& res);
    bool Contains(const std::string& modelId) const;
    bool Empty() const { return ids_.empty(); }
private:
    std::unordered_set<std::string> ids_; // lower-cased model ids
};
