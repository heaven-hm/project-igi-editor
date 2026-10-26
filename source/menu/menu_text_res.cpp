/******************************************************************************
 * @file    menu_text_res.cpp
 * @brief   Retail menu label text from the game's per-language .RES archives.
 *
 * Parity target and the measured archive naming: see menu_text_res.h.
 *****************************************************************************/

#include "menu_text_res.h"

#include <cctype>
#include <cstdint>
#include <filesystem>
#include <set>
#include <utility>

namespace igi {
namespace menu_text {
namespace {

namespace fs = std::filesystem;

// ASCII case-insensitive equality; the engine compares upper-cased names
// (src/res/resource.c:136-137).
bool AsciiEqualIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const unsigned char ca = static_cast<unsigned char>(a[i]);
        const unsigned char cb = static_cast<unsigned char>(b[i]);
        if (std::toupper(ca) != std::toupper(cb)) return false;
    }
    return true;
}

// A CSTR payload is a NUL-terminated byte string. The bytes are kept as they are
// on disk: the non-English archives store Windows-1252 (e.g. German "Hauptmen\xfc"),
// so this is deliberately not a UTF-8 decode.
std::string DecodeCStr(const std::vector<uint8_t>& raw) {
    std::size_t n = 0;
    while (n < raw.size() && raw[n] != 0) ++n;
    return std::string(reinterpret_cast<const char*>(raw.data()), n);
}

// <game_root>/LANGUAGE/<language_name>, matched case-insensitively: the install
// spells the directory upper-case while the engine builds "LOCAL:language/<name>/"
// (src/menu/menumanager.c:301) and leans on the platform's case-insensitive lookup.
// Returns the canonical path when it exists, else the conventional one so the
// caller reports a useful error.
std::string FindLanguageDir(const std::string& game_root, const std::string& language_name) {
    if (game_root.empty() || language_name.empty()) return {};
    const fs::path base = fs::path(game_root) / "LANGUAGE";
    std::error_code ec;
    if (fs::is_directory(base, ec)) {
        for (const fs::directory_entry& entry : fs::directory_iterator(base, ec)) {
            if (!entry.is_directory(ec)) continue;
            if (AsciiEqualIgnoreCase(entry.path().filename().string(), language_name)) {
                return entry.path().string();
            }
        }
    }
    return (base / language_name).string();
}

}  // namespace

std::string LanguageResources::Normalize(const std::string& name) {
    std::string out;
    out.reserve(name.size());
    for (const char ch : name) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '\\') {
            out.push_back('/');
        } else {
            out.push_back(static_cast<char>(std::toupper(c)));
        }
    }
    return out;
}

std::string LanguageResources::FinalComponent(const std::string& name) {
    const std::string norm = Normalize(name);
    const std::size_t slash = norm.rfind('/');
    return slash == std::string::npos ? norm : norm.substr(slash + 1);
}

bool LanguageResources::AddFile(const std::string& filepath, std::string& error) {
    if (filepath.empty()) {
        error = "empty resource path";
        return false;
    }
    FileIndex index;
    index.path = filepath;
    // RES_BuildIndex keeps only the final path component of each entry
    // (source/renderer/res_writer.cpp:233-240), so it serves the FinalComponent
    // form; the full stored name is needed for the engine-path form and is taken
    // from the same reader's streaming pass.
    std::unordered_map<std::string, ResEntryInfo> raw_index;
    if (!RES_BuildIndex(filepath, raw_index, error)) return false;
    // RES_BuildIndex keys keep the archive's own spelling ("Main Menu",
    // "LOCAL:english/..."); the engine compares upper-cased names
    // (src/res/resource.c:136-137), so both of this module's maps are keyed on the
    // normalised form. Retail data has no normalisation collision with differing
    // payloads (measured), and exact duplicates carry identical text.
    index.by_final.reserve(raw_index.size());
    for (const auto& entry : raw_index) {
        index.by_final.emplace(Normalize(entry.first), entry.second);
    }

    std::string scan_error;
    const bool scanned = RES_ForEachEntry(
        filepath,
        [&index](const std::string& raw_name, const uint8_t*, std::size_t) {
            index.full_to_final.emplace(Normalize(raw_name), FinalComponent(raw_name));
        },
        scan_error);
    if (!scanned) {
        error = scan_error.empty() ? ("cannot read entry names from " + filepath) : scan_error;
        return false;
    }
    files_.push_back(std::move(index));
    return true;
}

bool LanguageResources::AddLanguageDirectory(const std::string& game_root,
                                            const std::string& language_name,
                                            std::string& error) {
    error.clear();
    if (game_root.empty() || language_name.empty()) {
        error = "no game root or language name given";
        return false;
    }
    const std::string dir = FindLanguageDir(game_root, language_name);
    if (dir.empty()) {
        error = "no language directory for '" + language_name + "' under " + game_root;
        return false;
    }

    // Exactly the two archives the engine's menu system loads for a language
    // (src/menu/menumanager.c:254-255).
    static const char* const kLanguageFiles[] = {"menusystem.res", "missions.res"};
    std::string problems;
    bool loaded_any = false;
    for (const char* file : kLanguageFiles) {
        const std::string path = (fs::path(dir) / file).string();
        std::string file_error;
        if (AddFile(path, file_error)) {
            loaded_any = true;
        } else {
            if (!problems.empty()) problems += "; ";
            problems += file_error;
        }
    }
    if (!loaded_any) {
        error = problems.empty() ? ("no language resources in " + dir) : problems;
        return false;
    }
    language_name_ = language_name;
    error = problems;  // empty on a complete load; otherwise what was skipped
    return true;
}

std::size_t LanguageResources::entry_count() const {
    std::set<std::string> keys;
    for (const FileIndex& file : files_) {
        for (const auto& kv : file.by_final) keys.insert(kv.first);
    }
    return keys.size();
}

std::vector<std::string> LanguageResources::file_paths() const {
    std::vector<std::string> paths;
    paths.reserve(files_.size());
    for (const FileIndex& file : files_) paths.push_back(file.path);
    return paths;
}

bool LanguageResources::ReadText(const FileIndex& file,
                                const std::string& final_key,
                                std::string& out) const {
    const auto it = file.by_final.find(final_key);
    if (it == file.by_final.end()) return false;
    if (it->second.data_size == 0) {
        out.clear();
        return true;
    }
    const std::vector<uint8_t> raw = RES_ReadEntry(file.path, it->second);
    if (raw.empty()) return false;  // I/O error: treat as absent, never as text
    out = DecodeCStr(raw);
    return true;
}

TextResult LanguageResources::Lookup(const std::string& key) const {
    TextResult result;
    if (files_.empty()) {
        result.text = key;  // no language data: the editor's previous behaviour
        result.status = TextStatus::NoLanguageData;
        return result;
    }

    // 1. The engine's own path: "LOCAL:%s/%s" with gLanguageNames[language].
    const std::string engine_path = Normalize("LOCAL:" + language_name_ + "/" + key);
    for (const FileIndex& file : files_) {
        const auto it = file.full_to_final.find(engine_path);
        if (it == file.full_to_final.end()) continue;
        std::string text;
        if (!ReadText(file, it->second, text)) continue;
        result.text = std::move(text);
        result.status = TextStatus::Resolved;
        result.form = LookupForm::EnginePath;
        return result;
    }

    // 2. Deviation, measured per lookup: match the entry name's final component.
    const std::string final_key = FinalComponent(key);
    for (const FileIndex& file : files_) {
        std::string text;
        if (!ReadText(file, final_key, text)) continue;
        result.text = std::move(text);
        result.status = TextStatus::Resolved;
        result.form = LookupForm::FinalComponent;
        return result;
    }

    // 3. The engine's miss string, verbatim.
    result.text = "RM: " + key;
    result.status = TextStatus::EngineMiss;
    return result;
}

void LookupCounters::Add(const TextResult& r) {
    switch (r.status) {
        case TextStatus::Resolved:
            if (r.form == LookupForm::EnginePath) {
                ++engine_path;
            } else {
                ++final_component;
            }
            break;
        case TextStatus::EngineMiss:
            ++engine_miss;
            break;
        case TextStatus::NoLanguageData:
            ++no_language_data;
            break;
    }
}

std::vector<MenuLabel> ResolveMenuLabels(const MenuDefinition& menu,
                                         const LanguageResources& resources) {
    std::vector<MenuLabel> labels;
    if (!menu.ok) return labels;
    for (const MenuWidget* widget : menu.Flatten()) {
        if (widget == nullptr) continue;
        if (widget->type != "MenuText" && widget->type != "MenuTextConditional") continue;
        MenuLabel label;
        label.key = widget->FieldText(kTextResourceField);
        if (label.key.empty()) continue;  // not a label (see the header note)
        label.widget_id = widget->id;
        label.widget_type = widget->type;
        label.widget_name = widget->name;
        label.result = resources.Lookup(label.key);
        labels.push_back(std::move(label));
    }
    return labels;
}

LookupCounters CountLabels(const std::vector<MenuLabel>& labels) {
    LookupCounters counters;
    for (const MenuLabel& label : labels) counters.Add(label.result);
    return counters;
}

}  // namespace menu_text
}  // namespace igi
