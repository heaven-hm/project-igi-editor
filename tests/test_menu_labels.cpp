/******************************************************************************
 * @file    test_menu_labels.cpp
 * @brief   The retail menu model carries the text the GAME would draw, not the
 *          script's `Text Resource` keys.
 *
 * This is the integration test for the last step of the label pipeline: a retail
 * item's `Text Resource` is a key, `MenuManager_GetString` turns it into the
 * language archive's string, and `LoadEditorMenu` must put that string on the
 * model.  Until this test existed the resolution code and the model were both
 * correct but unconnected, so the menu would still have drawn keys.
 *
 * Oracle: IGI-RE (/Users/haseeb-mir/Documents/Code/IGI-RE)
 *   src/menu/menumanager.c:832-852  MenuManager_GetString HOOK 0x418EA0
 *       crt_sprintf(path, "LOCAL:%s/%s", Config_GetLanguageName(mm->language), name);
 *       miss -> "RM: %s"                                     (:849-851)
 *   src/menu/menumanager.c:254-255  MenuManager_LoadResources HOOK 0x417D50 loads
 *                                   "menusystem.res" and "missions.res"
 *   src/menu/menumanager.c:298-308  MenuManager_LoadLanguageRes HOOK 0x417FF0 builds
 *                                   "LOCAL:language/<LanguageName>/<file>"
 *   src/profile/profile.c:377-381   Config_GetLanguageName HOOK 0x406320
 *   src/profile/profile.c:419       gLanguageNames[6] = {"ENGLISH","GERMAN","FRENCH",
 *                                   "ITALIAN","SPANISH","USA"}
 *   src/res/resource.c:124-147      Resource_IsLoaded HOOK 0x4B5B90 (name normalisation)
 *   src/res/resource.c:328-372,65-70 Resource_AddChunks/Resource_Create upper-case the
 *                                   stored NAME chunk, which is why the registered name
 *                                   is the archive's own entry name upper-cased
 *
 * The retail install is required for the corpus cases (IGI_GAME_PATH, default
 * /Users/haseeb-mir/Downloads/IGI); they skip when it is absent, so the suite stays
 * green on a machine without the game.
 *****************************************************************************/

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "../source/menu/menu_ui.h"
#include "../source/menu/menu_text_res.h"

namespace {

namespace app_menu = igi::app_menu;

std::filesystem::path CorpusRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0') {
        return std::filesystem::path(env);
    }
    return std::filesystem::path("/Users/haseeb-mir/Downloads/IGI");
}

bool CorpusAvailable() {
    std::error_code ec;
    return std::filesystem::exists(CorpusRoot() / "menusystem" / "ingamemenu.qvm", ec) &&
           std::filesystem::exists(CorpusRoot() / "LANGUAGE" / "ENGLISH" / "menusystem.res", ec);
}

// Finds a collected entry by its VERBATIM key anywhere in the model.
const igi::MenuUiEntry* FindByKey(const igi::MenuUiModel& model, const std::string& key) {
    for (const igi::MenuUiScreen& screen : model.screens) {
        for (const igi::MenuUiEntry& entry : screen.entries) {
            if (entry.label == key) {
                return &entry;
            }
        }
    }
    return nullptr;
}

igi::MenuUiBuildOptions Options() {
    igi::MenuUiBuildOptions options;
    options.viewport_width = 1280;
    options.viewport_height = 720;
    return options;
}

}  // namespace

// ─── the retail model carries resolved text ──────────────────────────────────

TEST(MenuLabels, RetailModelResolvesKeysToTheGamesOwnText) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "retail corpus not installed";
    }

    const app_menu::EditorMenu menu = app_menu::LoadEditorMenu(
        CorpusRoot().string(), /*terrain_expanded=*/false, Options());

    ASSERT_TRUE(menu.retail);
    ASSERT_FALSE(menu.fallback);
    ASSERT_TRUE(menu.label_stats.language_data_loaded)
        << "no language archive was loaded from " << CorpusRoot().string();

    // Every retail label must have been looked up, and the great majority resolved.
    EXPECT_GT(menu.label_stats.labels, 0u);
    EXPECT_GT(menu.label_stats.resolved, 0u);
    EXPECT_EQ(menu.label_stats.labels,
              menu.label_stats.resolved + menu.label_stats.engine_miss +
                  menu.label_stats.no_language_data);
    EXPECT_EQ(menu.label_stats.no_language_data, 0u)
        << "archives loaded, so no label should report NoLanguageData";

    // THE POINT OF THE WHOLE PIPELINE: the key and the drawn text differ, and the
    // model must carry BOTH -- the key verbatim, and the game's string separately.
    const igi::MenuUiEntry* restart = FindByKey(menu.model, "Restart Game");
    ASSERT_NE(restart, nullptr) << "retail ingamemenu.qvm should declare 'Restart Game'";
    EXPECT_EQ(restart->label_key, "Restart Game");
    EXPECT_TRUE(restart->label_resolved)
        << "display_label='" << restart->display_label << "'";
    EXPECT_EQ(restart->display_label, "Restart Mission")
        << "the engine draws the archive's text, not the key";

    // `ingamemenu.qvm` declares exactly two labels whose text differs from the key
    // (measured; see docs/igi-re-parity/MENU_TEXT_RES.md).  Both must resolve to the
    // archive's text, because that is what the game draws.
    const igi::MenuUiEntry* reset = FindByKey(menu.model, "Reset to Default");
    ASSERT_NE(reset, nullptr) << "retail ingamemenu.qvm should declare 'Reset to Default'";
    EXPECT_TRUE(reset->label_resolved) << "display_label='" << reset->display_label << "'";
    EXPECT_EQ(reset->display_label, "Reset to Default Settings")
        << "the engine draws the archive's text, not the key";
    EXPECT_EQ(reset->label, "Reset to Default") << "the verbatim key must survive";

    // A key whose text happens to equal it must still be reported as resolved, not
    // as a miss: the distinction is provenance, not string equality.
    const igi::MenuUiEntry* resume = FindByKey(menu.model, "Resume Game");
    if (resume != nullptr) {
        EXPECT_TRUE(resume->label_resolved);
        EXPECT_EQ(resume->display_label, "Resume Game");
    }

    std::printf("[menu-labels] language=%s labels=%zu resolved=%zu engine_path=%zu "
                "final_component=%zu engine_miss=%zu no_language_data=%zu\n",
                menu.language.c_str(), menu.label_stats.labels, menu.label_stats.resolved,
                menu.label_stats.engine_path, menu.label_stats.final_component,
                menu.label_stats.engine_miss, menu.label_stats.no_language_data);
}

// The verbatim key fields must be untouched: existing consumers and tests read
// `label`, and changing its meaning would silently rewrite the editor's menu.
TEST(MenuLabels, VerbatimKeysAreNotRewritten) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "retail corpus not installed";
    }

    const app_menu::EditorMenu menu = app_menu::LoadEditorMenu(
        CorpusRoot().string(), /*terrain_expanded=*/false, Options());
    ASSERT_TRUE(menu.retail);

    const igi::MenuUiEntry* restart = FindByKey(menu.model, "Restart Game");
    ASSERT_NE(restart, nullptr);
    // `label` and `item_label` still hold the retail script data verbatim.
    EXPECT_EQ(restart->label, "Restart Game");
    EXPECT_NE(restart->display_label, restart->label)
        << "display_label must be the resolved text, not a copy of the key";
}

// The editor's own rows are display text already.  Pushing them through the archive
// would replace them with the engine's "RM: <key>" miss string, which would be a
// visible regression on every install.
TEST(MenuLabels, EditorAddedRowsAreNotPushedThroughTheArchive) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "retail corpus not installed";
    }

    const app_menu::EditorMenu menu = app_menu::LoadEditorMenu(
        CorpusRoot().string(), /*terrain_expanded=*/false, Options());
    ASSERT_TRUE(menu.retail);

    const igi::MenuUiEntry* options = FindByKey(menu.model, "Editor Options");
    ASSERT_NE(options, nullptr) << "the editor-added entry should be present";
    EXPECT_TRUE(options->editor_added);
    EXPECT_EQ(options->display_label, "Editor Options");
    EXPECT_FALSE(options->label_resolved)
        << "an editor label has no retail archive entry and must not claim one";
    EXPECT_EQ(options->display_label.find("RM: "), std::string::npos)
        << "the engine miss string must never reach an editor row";

    // The editor options screen's own rows keep their labels too.
    for (const igi::MenuUiScreen& screen : menu.model.screens) {
        for (const igi::MenuUiEntry& entry : screen.entries) {
            if (entry.editor_added && entry.has_text) {
                EXPECT_EQ(entry.display_label, entry.label)
                    << "editor row '" << entry.label << "' was altered";
            }
        }
    }
}

// With no game installed the menu still loads (from the documented fallback) and
// every row still has text to draw.
TEST(MenuLabels, FallbackRowsAlwaysHaveDisplayText) {
    const app_menu::EditorMenu menu = app_menu::LoadEditorMenu(
        "/nonexistent/igi/install", /*terrain_expanded=*/false, Options());

    ASSERT_TRUE(menu.fallback);
    ASSERT_FALSE(menu.model.screens.empty());
    EXPECT_FALSE(menu.label_stats.language_data_loaded);
    EXPECT_EQ(menu.label_stats.labels, 0u)
        << "no archive means no label was resolved";

    int with_text = 0;
    for (const igi::MenuUiScreen& screen : menu.model.screens) {
        for (const igi::MenuUiEntry& entry : screen.entries) {
            if (!entry.has_text) {
                continue;
            }
            ++with_text;
            EXPECT_FALSE(entry.display_label.empty())
                << "row '" << entry.label << "' has no text to draw";
            EXPECT_EQ(entry.display_label, entry.label);
        }
    }
    EXPECT_GT(with_text, 0);
}

// MAINMENU.QVM is the other retail menu and the one the doc's evidence comes from:
// it exercises the same lookup at a larger scale, and it contains the single key no
// language archive defines.  Resolved through the same seam `LoadEditorMenu` uses.
TEST(MenuLabels, MainMenuKeysResolveIncludingTheOneEngineMiss) {
    const std::filesystem::path main_menu = CorpusRoot() / "menusystem" / "MAINMENU.QVM";
    std::error_code ec;
    if (!std::filesystem::exists(main_menu, ec)) {
        GTEST_SKIP() << "retail corpus not installed";
    }

    igi::menu_text::LanguageResources resources;
    std::string error;
    if (!resources.AddLanguageDirectory(CorpusRoot().string(), "ENGLISH", error)) {
        GTEST_SKIP() << "retail corpus not installed: " << error;
    }

    // The doc's own pairs, each the text the game draws.
    EXPECT_EQ(resources.Resolve("Single Player"), "Play Project IGI");
    EXPECT_EQ(resources.Resolve("Performance Level"), "Graphics detail");
    EXPECT_EQ(resources.Resolve("Ok"), "OK");

    // 'Geometry High Detail' is declared by MAINMENU.QVM and defined by no language
    // archive, so the engine's own miss string is the correct answer -- not a bug,
    // and not something to paper over with the key.
    const igi::menu_text::TextResult miss = resources.Lookup("Geometry High Detail");
    EXPECT_EQ(miss.text, "RM: Geometry High Detail");
    EXPECT_EQ(miss.status, igi::menu_text::TextStatus::EngineMiss);
}

// The miss path must stay engine-exact: a key the archive does not have resolves to
// "RM: <key>", and is counted as a miss rather than as resolved.
TEST(MenuLabels, EngineMissStringIsExact) {
    igi::menu_text::LanguageResources resources;
    std::string error;
    if (!resources.AddLanguageDirectory(CorpusRoot().string(), "ENGLISH", error)) {
        GTEST_SKIP() << "retail corpus not installed: " << error;
    }

    const igi::menu_text::TextResult miss = resources.Lookup("Editor Options");
    EXPECT_EQ(miss.text, "RM: Editor Options");
    EXPECT_EQ(miss.status, igi::menu_text::TextStatus::EngineMiss);
    EXPECT_FALSE(miss.resolved());
    EXPECT_TRUE(miss.fallback());
}

// ─── the text the editor DRAWS is the editor's, not the game's ───────────────
//
// `label` / `display_label` above are the GAME's text and stay on the model: the
// loaded definition is data the editor edits and saves (`SaveEditorMenu`), and the
// reader tests read those fields.  What the editor's menu SHOWS is
// `MenuUiEntry::editor_label`, which `LoadEditorMenu` fills from
// `app_menu::EditorLabelForRetailEntry`.  These tests hold the two apart on the
// real retail container.

// Every retail text row has editor text to draw, and it is never the game's.
TEST(MenuLabels, EveryRetailRowHasEditorTextThatIsNotTheGames) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "retail corpus not installed";
    }

    const app_menu::EditorMenu menu = app_menu::LoadEditorMenu(
        CorpusRoot().string(), /*terrain_expanded=*/false, Options());
    ASSERT_TRUE(menu.retail);

    std::size_t retail_text_rows = 0;
    for (const igi::MenuUiScreen& screen : menu.model.screens) {
        for (const igi::MenuUiEntry& entry : screen.entries) {
            if (!entry.has_text) {
                continue;  // a container draws its sprite resources, not text
            }
            if (entry.editor_added) {
                continue;  // the editor's own rows, checked below
            }
            ++retail_text_rows;
            EXPECT_FALSE(entry.editor_label.empty())
                << "retail row '" << entry.label << "' has no editor text";
            // The drawn text must differ from both forms of the game's text.  If a
            // future mapping ever returned the retail string, this is the test that
            // fails rather than the user seeing the game's words again.
            EXPECT_NE(entry.editor_label, entry.label)
                << "row '" << entry.label << "' would draw the game's script key";
            EXPECT_NE(entry.editor_label, entry.display_label)
                << "row '" << entry.label << "' would draw the game's resolved text";
            EXPECT_EQ(entry.editor_label.find("RM: "), std::string::npos)
                << "the engine's miss string must never reach an editor row";
        }
    }
    EXPECT_GT(retail_text_rows, 0u);
}

// Screen 900 -- the screen the editor's "Game Menu" row opens -- keeps its five
// retail rows, in retail order, with the retail text verbatim, and draws the
// editor's own label for each.
TEST(MenuLabels, TheRetailMainScreenKeepsItsStructureAndDrawsEditorText) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "retail corpus not installed";
    }

    const app_menu::EditorMenu menu = app_menu::LoadEditorMenu(
        CorpusRoot().string(), /*terrain_expanded=*/false, Options());
    ASSERT_TRUE(menu.retail);
    const igi::MenuUiScreen* main = menu.model.FindScreen(900);
    ASSERT_NE(main, nullptr);

    struct Expected {
        const char* widget_name;
        const char* retail_key;   // verbatim retail data, untouched
        const char* retail_text;  // the archive's answer for that key
        const char* editor_label; // what the editor draws
    };
    const Expected expected[] = {
        // The title is the screen's first MenuText (its retail key is
        // "Game Paused"; the editor shows its own panel title).
        {"", "Game Paused", "Game Paused", "IGI EDITOR"},
        {"resume", "Resume Game", "Resume Game", "Resume"},
        {"restart", "Restart Game", "Restart Mission", "Reset Level"},
        {"sound", "Sound", "Sound", "Music"},
        {"controls", "Controls", "Controls", "Keybindings"},
        {"quit", "Quit to Main Menu", "Quit to Main Menu", "Quit"},
    };

    for (const Expected& row : expected) {
        const igi::MenuUiEntry* entry = nullptr;
        for (const igi::MenuUiEntry& candidate : main->entries) {
            if (candidate.widget_name == row.widget_name && candidate.label == row.retail_key) {
                entry = &candidate;
                break;
            }
        }
        ASSERT_NE(entry, nullptr) << "retail row '" << row.retail_key << "' is missing";
        EXPECT_EQ(entry->label, row.retail_key) << "the script key must stay verbatim";
        EXPECT_EQ(entry->display_label, row.retail_text)
            << "the game's resolved text must stay on the model";
        EXPECT_EQ(entry->editor_label, row.editor_label);
    }

    // The retail items keep their order, and the editor's own entry is still
    // appended after them.
    ASSERT_GE(main->selectable.size(), 5u);
    const char* order[] = {"resume", "restart", "sound", "controls", "quit"};
    for (size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(main->entries[static_cast<size_t>(main->selectable[i])].widget_name, order[i]);
    }
    const igi::MenuUiEntry& added =
        main->entries[static_cast<size_t>(main->selectable.back())];
    EXPECT_TRUE(added.editor_added);
    EXPECT_EQ(added.editor_label, std::string(app_menu::kEditorOptionsLabel));
}

// The editor's own rows are not run through the mapping: they are editor text
// already, and their labels are drawn verbatim.
TEST(MenuLabels, EditorRowsKeepTheirOwnEditorLabels) {
    if (!CorpusAvailable()) {
        GTEST_SKIP() << "retail corpus not installed";
    }

    const app_menu::EditorMenu menu = app_menu::LoadEditorMenu(
        CorpusRoot().string(), /*terrain_expanded=*/false, Options());
    ASSERT_TRUE(menu.retail);

    const igi::MenuUiScreen* own = menu.model.FindScreen(app_menu::kEditorOptionsScreenId);
    ASSERT_NE(own, nullptr);
    std::size_t rows = 0;
    for (const igi::MenuUiEntry& row : own->entries) {
        if (!row.has_text) {
            continue;
        }
        ++rows;
        EXPECT_TRUE(row.editor_added);
        EXPECT_EQ(row.editor_label, row.label) << "editor row '" << row.label << "' was altered";
    }
    EXPECT_GT(rows, 0u);
}
