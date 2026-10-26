/******************************************************************************
 * @file    menu_text_res.h
 * @brief   Retail menu label text, read from the game's per-language .RES archives.
 *
 * ORACLE: IGI-RE (/Users/haseeb-mir/Documents/Code/IGI-RE), a verified C
 * decompilation of retail IGI.exe.
 *
 *   src/menu/menumanager.c:832-852  MenuManager_GetString (HOOK 0x418EA0)
 *       crt_sprintf(path, "LOCAL:%s/%s", Config_GetLanguageName(mm->language), name);
 *       if (Resource_Find(path)) return Resource_Load(path, NULL);
 *       ... cached-by-name lookup, else:
 *       crt_sprintf(s->text, "RM: %s", name);
 *     The miss string is verbatim engine behaviour, not a placeholder.
 *
 *   src/profile/profile.c:377-381   Config_GetLanguageName -> gLanguageNames[language]
 *   src/profile/profile.c:419       gLanguageNames[6] =
 *                                   {"ENGLISH","GERMAN","FRENCH","ITALIAN","SPANISH","USA"}
 *   src/menu/menumanager.c:298-308  MenuManager_LoadLanguageRes loads
 *                                   "LOCAL:language/<name>/<file>" for every language
 *   src/menu/menumanager.c:254-255  it is called with "menusystem.res" and "missions.res"
 *   src/res/resource.c:124-147      Resource_IsLoaded (HOOK 0x4B5B90) resolves the name
 *                                   through File_ResolvePath (alias expansion), then
 *                                   String_ToUpper + Path_FixSlashes, then compares by
 *                                   hash + strcmp -- so matching is case-insensitive
 *   src/res/resource.c:442-446      Resource_Find (HOOK 0x4B64E0) == Resource_IsLoaded(name, NULL)
 *
 * The shipped archives name their entries `LOCAL:<langname>/<Key>` in all six
 * language directories (measured on the retail install; see
 * docs/igi-re-parity/MENU_TEXT_RES.md), which is exactly the path
 * MenuManager_GetString builds -- so that form is the primary lookup here.
 * `LookupForm::FinalComponent` is a documented fallback for archives that store a
 * mount path instead (the legacy `menusystem/ENGLISH.RES`), and which form matched
 * is reported on every result rather than assumed.
 *
 * The .res parsing itself is NOT duplicated: `RES_BuildIndex` / `RES_ReadEntry`
 * (source/renderer/res_writer.h) are reused.
 *****************************************************************************/

#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "qvm_image.h"              // igi::MenuDefinition / MenuWidget
#include "renderer/res_writer.h"    // ResEntryInfo

namespace igi {
namespace menu_text {

// The declared parameter whose value is the label key
// (`Task_DeclareParameters` schema; menu_ui.h names the same string in
// `menu_field::kTextResource`).
inline constexpr char kTextResourceField[] = "Text Resource";

// How a key was matched against an archive.
enum class LookupForm {
    None,            // not matched
    EnginePath,      // "LOCAL:<LanguageName>/<key>" -- what MenuManager_GetString builds
    FinalComponent,  // last path component only (deviation, see the header note)
};

// What the lookup produced.
enum class TextStatus {
    Resolved,        // text came from a language archive
    EngineMiss,      // archives loaded, key absent -> "RM: <key>" (engine parity)
    NoLanguageData,  // no archive loaded -> the raw key (the editor's previous behaviour)
};

struct TextResult {
    std::string text;
    TextStatus status = TextStatus::NoLanguageData;
    LookupForm form = LookupForm::None;

    bool resolved() const { return status == TextStatus::Resolved; }
    // True when the label is not the language text: a miss or no data at all.
    bool fallback() const { return status != TextStatus::Resolved; }
};

// Aggregate over a set of lookups, for the editor's diagnostics and for tests.
struct LookupCounters {
    std::size_t engine_path = 0;
    std::size_t final_component = 0;
    std::size_t engine_miss = 0;
    std::size_t no_language_data = 0;

    void Add(const TextResult& r);
    std::size_t resolved() const { return engine_path + final_component; }
    std::size_t fallback() const { return engine_miss + no_language_data; }
};

// One language's text resources, as the engine's menu system loads them.
class LanguageResources {
public:
    // Adds one .res archive. Returns false and sets `error` when the file cannot be
    // read as an IRES archive. Entries are indexed both by their full stored name
    // (normalised) and by their final path component.
    bool AddFile(const std::string& filepath, std::string& error);

    // Adds the archives the engine's menu system loads for one language
    // (menumanager.c:254-255): `<game_root>/LANGUAGE/<language_name>/menusystem.res`
    // and `.../missions.res`. The directory is matched case-insensitively, because
    // the install spells it `LANGUAGE/ENGLISH/` while the engine builds
    // `LOCAL:language/<name>/` (menumanager.c:301) and relies on the platform's
    // case-insensitive file lookup. Returns true when at least one archive loaded;
    // a missing `missions.res` alone is not a failure. Sets `language_name`.
    bool AddLanguageDirectory(const std::string& game_root,
                              const std::string& language_name,
                              std::string& error);

    // The engine's path form for this instance: "LOCAL:<language_name>/<key>".
    const std::string& language_name() const { return language_name_; }
    void set_language_name(std::string name) { language_name_ = std::move(name); }

    bool empty() const { return files_.empty(); }
    std::size_t file_count() const { return files_.size(); }
    // Distinct final-component keys across all archives.
    std::size_t entry_count() const;
    // The archive paths added so far, in load order.
    std::vector<std::string> file_paths() const;

    // Resolves one label key. Engine form first, then the final component; a miss
    // returns exactly "RM: <key>" when archives are loaded, and the raw key when
    // none are.
    TextResult Lookup(const std::string& key) const;
    // Text only, for callers that do not care why.
    std::string Resolve(const std::string& key) const { return Lookup(key).text; }

    // Engine name normalisation: ASCII upper-case, backslashes to slashes
    // (resource.c:136-137 does exactly this before comparing).
    static std::string Normalize(const std::string& name);
    // Last path component of a normalised name.
    static std::string FinalComponent(const std::string& name);

private:
    struct FileIndex {
        std::string path;
        std::unordered_map<std::string, ResEntryInfo> by_final;   // RES_BuildIndex
        std::unordered_map<std::string, std::string> full_to_final;  // normalised full name -> normalised final
    };

    bool ReadText(const FileIndex& file, const std::string& final_key, std::string& out) const;

    std::vector<FileIndex> files_;
    std::string language_name_ = "ENGLISH";
};

// ---------------------------------------------------------------------------
// Integration seam: every label a menu definition declares, resolved.
// ---------------------------------------------------------------------------

struct MenuLabel {
    int widget_id = -1;
    std::string widget_type;   // "MenuText" / "MenuTextConditional"
    std::string widget_name;
    std::string key;           // the raw `Text Resource` value
    TextResult result;
};

// Walks `menu` depth-first and resolves the `Text Resource` of every MenuText /
// MenuTextConditional widget. Widgets whose `Text Resource` is empty are skipped:
// the engine would answer "RM: " for an empty key (menumanager.c:840), but an empty
// Text Resource is not a label in a menu script, and neither retail menu file has
// one.
std::vector<MenuLabel> ResolveMenuLabels(const MenuDefinition& menu,
                                         const LanguageResources& resources);

LookupCounters CountLabels(const std::vector<MenuLabel>& labels);

}  // namespace menu_text
}  // namespace igi
