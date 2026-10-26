/******************************************************************************
 * @file    test_menu_text_res.cpp
 * @brief   Retail menu labels from the per-language .RES archives.
 *
 * The corpus tests read the installed game (IGI_GAME_PATH, default
 * /Users/haseeb-mir/Downloads/IGI) and skip cleanly when it is absent. The
 * synthetic tests build a tiny IRES archive so the fallback and error paths are
 * pinned without depending on retail data.
 *****************************************************************************/

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "menu/menu_text_res.h"
#include "menu/qvm_image.h"

namespace {

using igi::MenuDefinition;
using igi::MenuValue;
using igi::MenuValueKind;
using igi::MenuWidget;
namespace mt = igi::menu_text;

// ─── corpus ──────────────────────────────────────────────────────────────────

std::filesystem::path CorpusRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0') {
        return std::filesystem::path(env);
    }
    return std::filesystem::path("/Users/haseeb-mir/Downloads/IGI");
}

std::filesystem::path IngameMenuPath() {
    return CorpusRoot() / "menusystem" / "ingamemenu.qvm";
}

std::filesystem::path MainMenuPath() {
    return CorpusRoot() / "menusystem" / "MAINMENU.QVM";
}

std::filesystem::path LanguageArchive(const std::string& language, const std::string& file) {
    return CorpusRoot() / "LANGUAGE" / language / file;
}

bool Exists(const std::filesystem::path& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

bool CorpusAvailable() {
    return Exists(IngameMenuPath()) && Exists(MainMenuPath()) &&
           Exists(LanguageArchive("ENGLISH", "menusystem.res"));
}

// The resolver the engine's menu system would build for one language.
mt::LanguageResources LoadEnglish(std::string& error) {
    mt::LanguageResources resources;
    resources.AddLanguageDirectory(CorpusRoot().string(), "ENGLISH", error);
    return resources;
}

// ─── a tiny IRES archive, so the non-retail paths are pinned too ─────────────

void AppendU32(std::vector<unsigned char>& out, uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>((value >> (8 * i)) & 0xFF));
}

void AppendChunk(std::vector<unsigned char>& out, const char* fourcc, const std::string& payload) {
    const uint32_t size = static_cast<uint32_t>(payload.size());
    const uint32_t skip = 16 + ((size + 3) & ~3u);  // 16-byte header, 4-byte aligned body
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(fourcc[i]));
    AppendU32(out, size);
    AppendU32(out, 4);   // flags, as in the retail archives
    AppendU32(out, skip);
    out.insert(out.end(), payload.begin(), payload.end());
    while (out.size() % 4 != 0) out.push_back(0);
}

// entries: (stored name, CSTR payload)
std::filesystem::path WriteSyntheticArchive(const std::string& file_name,
                                            const std::vector<std::pair<std::string, std::string>>& entries) {
    std::vector<unsigned char> bytes;
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<unsigned char>("ILFF"[i]));
    for (int i = 0; i < 12; ++i) bytes.push_back(0);
    for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<unsigned char>("IRES"[i]));
    for (const auto& entry : entries) {
        AppendChunk(bytes, "NAME", entry.first);
        AppendChunk(bytes, "CSTR", entry.second + std::string(1, '\0'));
    }
    const std::filesystem::path path = std::filesystem::temp_directory_path() / file_name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    out.close();
    return path;
}

std::filesystem::path WriteGarbageArchive(const std::string& file_name) {
    const std::filesystem::path path = std::filesystem::temp_directory_path() / file_name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const std::string junk = "this is not an IRES archive at all";
    out.write(junk.data(), static_cast<std::streamsize>(junk.size()));
    out.close();
    return path;
}

// ─── helpers ─────────────────────────────────────────────────────────────────

// The archives store Windows-1252, so a payload can contain bytes that are not
// valid UTF-8. Test output goes through the host runner, which decodes stdout as
// UTF-8 and fails on such bytes, so anything derived from an archive is printed
// (and compared) in this escaped form: ASCII only, one byte at a time.
std::string Printable(const std::string& text) {
    std::string out;
    for (const char ch : text) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c >= 0x20 && c < 0x7F) {
            out.push_back(static_cast<char>(c));
        } else {
            char buffer[8];
            std::snprintf(buffer, sizeof(buffer), "\\x%02x", c);
            out += buffer;
        }
    }
    return out;
}

// Every `Text Resource` under `widget`, in depth-first order.
std::vector<std::string> DescendantLabelKeys(const MenuWidget& widget) {
    std::vector<std::string> keys;
    const std::string key = widget.FieldText(mt::kTextResourceField);
    if (widget.type == "MenuText" || widget.type == "MenuTextConditional") {
        if (!key.empty()) keys.push_back(key);
    }
    for (const MenuWidget& child : widget.children) {
        for (std::string& nested : DescendantLabelKeys(child)) keys.push_back(std::move(nested));
    }
    return keys;
}

std::set<std::string> DistinctKeys(const std::vector<mt::MenuLabel>& labels) {
    std::set<std::string> keys;
    for (const mt::MenuLabel& label : labels) keys.insert(label.key);
    return keys;
}

std::string Join(const std::set<std::string>& values) {
    std::string out;
    for (const std::string& value : values) {
        if (!out.empty()) out += ", ";
        out += value;
    }
    return out;
}

struct MenuReport {
    std::string file;
    std::size_t labels = 0;
    mt::LookupCounters counters;
    std::vector<std::pair<std::string, std::string>> changed;  // key -> text
    std::vector<std::string> misses;
};

MenuReport ReportOn(const std::string& file, const mt::LanguageResources& resources) {
    MenuReport report;
    report.file = file;
    const MenuDefinition menu = MenuDefinition::FromQvmFile((CorpusRoot() / file).string());
    if (!menu.ok) {
        report.misses.push_back("<menu did not parse: " + menu.error + ">");
        return report;
    }
    const std::vector<mt::MenuLabel> labels = mt::ResolveMenuLabels(menu, resources);
    report.labels = labels.size();
    report.counters = mt::CountLabels(labels);
    for (const mt::MenuLabel& label : labels) {
        if (!label.result.resolved()) {
            report.misses.push_back(label.key);
        } else if (label.result.text != label.key) {
            report.changed.emplace_back(label.key, label.result.text);
        }
    }
    return report;
}

}  // namespace

// ─── the engine's path form is the archives' own convention ──────────────────

TEST(MenuTextRes, EnginePathFormMatchesTheRetailArchives) {
    if (!CorpusAvailable()) GTEST_SKIP() << "retail corpus not installed";
    std::string error;
    const mt::LanguageResources resources = LoadEnglish(error);
    ASSERT_TRUE(resources.empty() == false) << "no language archive loaded: " << error;
    EXPECT_EQ(resources.language_name(), "ENGLISH");
    // The two archives MenuManager_LoadLanguageRes is called with
    // (IGI-RE src/menu/menumanager.c:254-255).
    EXPECT_EQ(resources.file_count(), 2u);
    EXPECT_EQ(resources.file_paths().size(), 2u);
    EXPECT_EQ(resources.entry_count(), 288u);  // 262 menusystem.res + 28 missions.res, 2 shared

    // MenuManager_GetString builds "LOCAL:<gLanguageNames[language]>/<name>"
    // (menumanager.c:837, profile.c:379, profile.c:419), and that is the form the
    // shipped archives store -- so the primary form must be the one that matches.
    const mt::TextResult resume = resources.Lookup("Resume Game");
    EXPECT_TRUE(resume.resolved());
    EXPECT_EQ(resume.form, mt::LookupForm::EnginePath);
    EXPECT_EQ(resume.text, "Resume Game");

    // Not an identity mapping: the archive's text differs from the key.
    EXPECT_EQ(resources.Lookup("Restart Game").text, "Restart Mission");
    EXPECT_EQ(resources.Lookup("Single Player").text, "Play Project IGI");
    EXPECT_EQ(resources.Lookup("Ok").text, "OK");
    EXPECT_EQ(resources.Lookup("Performance Level").text, "Graphics detail");
}

// ─── both retail menus, counted and printed ──────────────────────────────────

TEST(MenuTextRes, EveryRetailLabelOfBothMenusIsAccountedFor) {
    if (!CorpusAvailable()) GTEST_SKIP() << "retail corpus not installed";
    std::string error;
    const mt::LanguageResources resources = LoadEnglish(error);
    ASSERT_FALSE(resources.empty()) << error;

    const MenuReport ingame = ReportOn("menusystem/ingamemenu.qvm", resources);
    const MenuReport main = ReportOn("menusystem/MAINMENU.QVM", resources);

    EXPECT_EQ(ingame.labels, 35u);
    EXPECT_EQ(ingame.counters.engine_path, 35u);
    EXPECT_EQ(ingame.counters.final_component, 0u);
    EXPECT_EQ(ingame.counters.engine_miss, 0u);
    EXPECT_EQ(ingame.counters.no_language_data, 0u);

    EXPECT_EQ(main.labels, 81u);
    EXPECT_EQ(main.counters.engine_path, 80u);
    EXPECT_EQ(main.counters.final_component, 0u);
    EXPECT_EQ(main.counters.engine_miss, 1u);
    EXPECT_EQ(main.counters.no_language_data, 0u);
    // The one key the shipped English text has no entry for, so the engine itself
    // would render the miss string.
    ASSERT_EQ(main.misses.size(), 1u);
    EXPECT_EQ(main.misses[0], "Geometry High Detail");
}

// ─── the ingame main screen, label by label ──────────────────────────────────

TEST(MenuTextRes, IngameMenuMainScreenLabelsAllResolve) {
    if (!CorpusAvailable()) GTEST_SKIP() << "retail corpus not installed";
    std::string error;
    const mt::LanguageResources resources = LoadEnglish(error);
    ASSERT_FALSE(resources.empty()) << error;

    const MenuDefinition menu = MenuDefinition::FromQvmFile(IngameMenuPath().string());
    ASSERT_TRUE(menu.ok) << menu.error;
    const MenuWidget* screen = menu.FindById(900);
    ASSERT_NE(screen, nullptr) << "no screen 900 in ingamemenu.qvm";
    EXPECT_EQ(screen->type, "MenuScreen");

    const std::vector<std::string> keys = DescendantLabelKeys(*screen);
    const std::set<std::string> distinct(keys.begin(), keys.end());
    EXPECT_EQ(distinct, (std::set<std::string>{"Controls", "Game Paused", "Quit to Main Menu",
                                               "Restart Game", "Resume Game", "Sound"}))
        << "main screen labels: " << Join(distinct);

    for (const std::string& key : keys) {
        const mt::TextResult result = resources.Lookup(key);
        EXPECT_TRUE(result.resolved()) << "main screen label '" << key << "' did not resolve";
        EXPECT_EQ(result.form, mt::LookupForm::EnginePath) << "for '" << key << "'";
    }
    // The labels the player actually sees on that screen.
    EXPECT_EQ(resources.Lookup("Game Paused").text, "Game Paused");
    EXPECT_EQ(resources.Lookup("Resume Game").text, "Resume Game");
    EXPECT_EQ(resources.Lookup("Restart Game").text, "Restart Mission");
    EXPECT_EQ(resources.Lookup("Quit to Main Menu").text, "Quit to Main Menu");
}

// ─── the editor's own labels are not in the retail text ──────────────────────

TEST(MenuTextRes, EditorAddedLabelDoesNotResolve) {
    if (!CorpusAvailable()) GTEST_SKIP() << "retail corpus not installed";
    std::string error;
    const mt::LanguageResources resources = LoadEnglish(error);
    ASSERT_FALSE(resources.empty()) << error;

    const mt::TextResult editor = resources.Lookup("Editor Options");
    EXPECT_FALSE(editor.resolved());
    EXPECT_EQ(editor.status, mt::TextStatus::EngineMiss);
    EXPECT_EQ(editor.form, mt::LookupForm::None);
    EXPECT_EQ(editor.text, "RM: Editor Options");
}

// ─── the miss string and the no-data degradation ─────────────────────────────

TEST(MenuTextRes, MissIsVerbatimAndNoLanguageDataKeepsTheRawKey) {
    if (!CorpusAvailable()) GTEST_SKIP() << "retail corpus not installed";
    std::string error;
    const mt::LanguageResources resources = LoadEnglish(error);
    ASSERT_FALSE(resources.empty()) << error;

    // IGI-RE src/menu/menumanager.c:849-851 -- crt_sprintf(s->text, "RM: %s", name).
    EXPECT_EQ(resources.Lookup("No Such Label").text, "RM: No Such Label");
    // The engine would answer "RM: " for an empty name; ResolveMenuLabels skips it.
    EXPECT_EQ(resources.Lookup("").text, "RM: ");

    // No archive loaded at all: the editor keeps what it had, the raw key.
    mt::LanguageResources none;
    none.set_language_name("ENGLISH");
    const mt::TextResult raw = none.Lookup("Resume Game");
    EXPECT_EQ(raw.status, mt::TextStatus::NoLanguageData);
    EXPECT_EQ(raw.form, mt::LookupForm::None);
    EXPECT_EQ(raw.text, "Resume Game");

    // A missing language directory degrades the same way, and says why.
    mt::LanguageResources missing;
    std::string load_error;
    EXPECT_FALSE(missing.AddLanguageDirectory("/nonexistent/game/root", "KLINGON", load_error));
    EXPECT_FALSE(load_error.empty());
    EXPECT_TRUE(missing.empty());
    EXPECT_EQ(missing.Lookup("Resume Game").text, "Resume Game");
    EXPECT_EQ(missing.Lookup("Resume Game").status, mt::TextStatus::NoLanguageData);
}

// ─── the other shipped languages: same convention, own bytes ─────────────────

TEST(MenuTextRes, NonEnglishArchivesUseTheSameConventionAndKeepTheirBytes) {
    if (!Exists(LanguageArchive("GERMAN", "menusystem.res"))) GTEST_SKIP() << "no GERMAN archive";
    mt::LanguageResources german;
    std::string error;
    ASSERT_TRUE(german.AddLanguageDirectory(CorpusRoot().string(), "GERMAN", error)) << error;

    const mt::TextResult menu = german.Lookup("Main Menu");
    EXPECT_EQ(menu.form, mt::LookupForm::EnginePath);
    // Windows-1252, not UTF-8: "Hauptmen" + 0xFC. The bytes are passed through as
    // they are on disk; transcoding is the renderer's business. Compared and
    // printed escaped, because the host runner decodes stdout as UTF-8.
    EXPECT_EQ(Printable(menu.text), "Hauptmen\\xfc");
    EXPECT_EQ(Printable(german.Lookup("Select Mission").text), "Mission w\\xe4hlen");

    if (Exists(LanguageArchive("FRENCH", "menusystem.res"))) {
        mt::LanguageResources french;
        ASSERT_TRUE(french.AddLanguageDirectory(CorpusRoot().string(), "FRENCH", error)) << error;
        EXPECT_EQ(Printable(french.Lookup("French").text), "Fran\\xe7ais");
        EXPECT_EQ(Printable(french.Lookup("Main Menu").text), "Menu principal");
    }
}

// ─── the legacy menusystem/ENGLISH.RES: fallback form only, different text ────

TEST(MenuTextRes, LegacyEnglishResOnlyMatchesByFinalComponent) {
    const std::filesystem::path legacy = CorpusRoot() / "menusystem" / "ENGLISH.RES";
    if (!Exists(legacy)) GTEST_SKIP() << "no legacy ENGLISH.RES";
    mt::LanguageResources resources;
    resources.set_language_name("ENGLISH");
    std::string error;
    ASSERT_TRUE(resources.AddFile(legacy.string(), error)) << error;

    // Its entries are named "LOCAL:menusystem/language/<Key>", not
    // "LOCAL:ENGLISH/<Key>", so only the documented fallback form can match it.
    const mt::TextResult player = resources.Lookup("Single Player");
    EXPECT_TRUE(player.resolved());
    EXPECT_EQ(player.form, mt::LookupForm::FinalComponent);
    EXPECT_EQ(player.text, "Single Player");

    // And its text is not the menu system's text: the engine loads
    // "LOCAL:language/<name>/{menusystem,missions}.res" (menumanager.c:254-255),
    // where the same key reads differently.
    if (Exists(LanguageArchive("ENGLISH", "menusystem.res"))) {
        std::string load_error;
        const mt::LanguageResources authoritative = LoadEnglish(load_error);
        ASSERT_FALSE(authoritative.empty()) << load_error;
        EXPECT_EQ(authoritative.Lookup("Single Player").text, "Play Project IGI");
        EXPECT_EQ(authoritative.Lookup("Restart Game").text, "Restart Mission");
    }
}

// ─── synthetic archives: both forms, plus the error paths ────────────────────

TEST(MenuTextRes, SyntheticArchivePinsBothFormsAndTheErrorPaths) {
    const std::filesystem::path archive = WriteSyntheticArchive(
        "menu_text_res_synthetic.res",
        {{"LOCAL:ENGLISH/Gamma", "G3"},
         {"LOCAL:menusystem/language/Alpha", "A1"},
         {"LOCAL:other/Beta", "B2"}});
    mt::LanguageResources resources;
    resources.set_language_name("ENGLISH");
    std::string error;
    ASSERT_TRUE(resources.AddFile(archive.string(), error)) << error;
    EXPECT_EQ(resources.entry_count(), 3u);

    const mt::TextResult gamma = resources.Lookup("Gamma");
    EXPECT_EQ(gamma.form, mt::LookupForm::EnginePath);
    EXPECT_EQ(gamma.text, "G3");

    const mt::TextResult alpha = resources.Lookup("Alpha");
    EXPECT_EQ(alpha.form, mt::LookupForm::FinalComponent);
    EXPECT_EQ(alpha.text, "A1");

    // Case-insensitive and slash-insensitive, as the engine's comparison is.
    EXPECT_EQ(resources.Lookup("gamma").text, "G3");
    EXPECT_EQ(resources.Lookup("GAMMA").text, "G3");

    const mt::TextResult missing = resources.Lookup("Delta");
    EXPECT_EQ(missing.status, mt::TextStatus::EngineMiss);
    EXPECT_EQ(missing.text, "RM: Delta");

    // An unreadable archive is reported, never guessed at.
    mt::LanguageResources broken;
    const std::filesystem::path garbage = WriteGarbageArchive("menu_text_res_garbage.res");
    std::string broken_error;
    EXPECT_FALSE(broken.AddFile(garbage.string(), broken_error));
    EXPECT_FALSE(broken_error.empty());
    EXPECT_TRUE(broken.empty());
    EXPECT_EQ(broken.Lookup("Gamma").status, mt::TextStatus::NoLanguageData);
    EXPECT_EQ(broken.Lookup("Gamma").text, "Gamma");

    std::error_code ec;
    std::filesystem::remove(archive, ec);
    std::filesystem::remove(garbage, ec);
}

// ─── the integration seam ────────────────────────────────────────────────────

TEST(MenuTextRes, ResolveMenuLabelsReadsTheDeclaredTextFieldAndSkipsEmptyOnes) {
    MenuDefinition menu;
    menu.ok = true;
    menu.root.type = "MenuManager";
    menu.root.name = "root";

    auto text_widget = [](const char* type, const char* name, const std::string& text) {
        MenuWidget widget;
        widget.id = -1;
        widget.type = type;
        widget.name = name;
        widget.fields.push_back(igi::MenuField{
            mt::kTextResourceField, "String32",
            {MenuValue{MenuValueKind::String, 0, 0.0, text}}});
        return widget;
    };
    menu.root.children.push_back(text_widget("MenuText", "paused", "Game Paused"));
    menu.root.children.push_back(text_widget("MenuText", "spacer", ""));
    menu.root.children.push_back(text_widget("MenuTextConditional", "resume", "Resume Game"));
    menu.root.children.push_back(text_widget("MenuFrame", "frame", "not a label"));

    mt::LanguageResources resources;
    resources.set_language_name("ENGLISH");
    const std::vector<mt::MenuLabel> labels = mt::ResolveMenuLabels(menu, resources);

    ASSERT_EQ(labels.size(), 2u);  // the empty Text Resource is not a label
    EXPECT_EQ(labels[0].key, "Game Paused");
    EXPECT_EQ(labels[0].widget_type, "MenuText");
    EXPECT_EQ(labels[0].widget_name, "paused");
    EXPECT_EQ(labels[1].key, "Resume Game");
    EXPECT_EQ(labels[1].widget_type, "MenuTextConditional");

    // Without language data every label is the raw key, and that is visible in the
    // counters rather than silent.
    const mt::LookupCounters counters = mt::CountLabels(labels);
    EXPECT_EQ(counters.resolved(), 0u);
    EXPECT_EQ(counters.no_language_data, 2u);
    EXPECT_EQ(counters.fallback(), 2u);

    EXPECT_TRUE(DistinctKeys(labels).count("Resume Game") == 1);
}

// ─── the printed inventory ───────────────────────────────────────────────────

TEST(MenuTextRes, PrintResolvedInventory) {
    if (!CorpusAvailable()) GTEST_SKIP() << "retail corpus not installed";
    std::string error;
    const mt::LanguageResources resources = LoadEnglish(error);
    ASSERT_FALSE(resources.empty()) << error;

    std::cout << "[menu-text] language " << resources.language_name() << ", "
              << resources.file_count() << " archive(s), " << resources.entry_count()
              << " distinct keys\n";
    for (const std::string& path : resources.file_paths()) {
        std::cout << "[menu-text]   " << path << "\n";
    }
    if (!error.empty()) std::cout << "[menu-text]   warning: " << error << "\n";

    for (const char* file : {"menusystem/ingamemenu.qvm", "menusystem/MAINMENU.QVM"}) {
        const MenuReport report = ReportOn(file, resources);
        std::cout << "[menu-text] " << report.file << ": " << report.labels << " labels, "
                  << report.counters.resolved() << " resolved ("
                  << report.counters.engine_path << " engine-path, "
                  << report.counters.final_component << " final-component), "
                  << report.counters.fallback() << " fell back ("
                  << report.counters.engine_miss << " miss, "
                  << report.counters.no_language_data << " no data)\n";
        for (const auto& change : report.changed) {
            std::cout << "[menu-text]   text differs from key: '" << Printable(change.first) << "' -> '"
                      << Printable(change.second) << "'\n";
        }
        for (const std::string& miss : report.misses) {
            std::cout << "[menu-text]   no entry: '" << Printable(miss) << "'\n";
        }
    }
}
