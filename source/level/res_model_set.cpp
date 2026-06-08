#include "level/res_model_set.h"
#include <algorithm>
#include <cctype>

static std::string LowerStem(const std::string& name) {
    // Take basename, strip ".mef" (case-insensitive), lower-case.
    size_t slash = name.find_last_of("\\/");
    std::string base = (slash == std::string::npos) ? name : name.substr(slash + 1);
    std::string lower; lower.reserve(base.size());
    for (char c : base) lower.push_back((char)std::tolower((unsigned char)c));
    const std::string ext = ".mef";
    if (lower.size() >= ext.size() &&
        lower.compare(lower.size() - ext.size(), ext.size(), ext) == 0)
        lower.erase(lower.size() - ext.size());
    else
        return ""; // not a mef entry
    return lower;
}

ResModelSet::ResModelSet(const RESFile& res) {
    for (const auto& e : res.entries) {
        std::string id = LowerStem(e.name);
        if (!id.empty()) ids_.insert(id);
    }
}

bool ResModelSet::Contains(const std::string& modelId) const {
    std::string lower; lower.reserve(modelId.size());
    for (char c : modelId) lower.push_back((char)std::tolower((unsigned char)c));
    return ids_.find(lower) != ids_.end();
}
