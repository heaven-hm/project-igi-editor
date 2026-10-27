/******************************************************************************
 * @file    test_mission_text_resource.cpp
 * @brief   Which archive a mission text key comes from (the 44 "Resource not
 *          found" warnings in the play log).
 *
 * THE DEFECT THIS PINS
 *   A play session logged, for every StatusMessage in the level's objects.qsc:
 *
 *     [WARN] [RES] Resource not found: M1_CUT_01  in <IGI>/language/USA/objectives.res
 *     [WARN] [RES] Resource not found: M1_MSG_6   in <IGI>/language/USA/objectives.res
 *     [WARN] [RES] Resource not found: CAMERASOFFLINE_1MIN in .../objectives.res
 *     [WARN] [RES] Resource not found: MISSION_FAILED      in .../objectives.res
 *
 *   The names are NOT invented and the retail data is NOT missing: they are the
 *   `text` parameter of the authored StatusMessage tasks
 *   (MISSIONS/location0/level1/objects.qsc:1142, :1204-1208) and they live in
 *   LANGUAGE/<lang>/MESSAGES.RES, not objectives.res. The editor was looking in
 *   the wrong archive.
 *
 * ENGINE ORACLE (IGI-RE, decompiled retail IGI.exe)
 *   StatusMessageScreen_AddMessage 0x486650  src/mission/statusmessage.c:653-656
 *       if (strlen(m->text) < 0x80 && Resource_Find(m->text)) {
 *           char *text = Resource_Load(m->text, NULL);
 *   StatusMessages_Load            0x4868A0  src/mission/statusmessage.c:669-673
 *       gMessagesRes = Resource_Load("LANGUAGE:messages.res", NULL);   // :671
 *   ComputerObjectives_Draw        0x469110  src/compmap/objectives.c:148-149
 *       if (Resource_Find(obj->name)) s = Resource_Load(obj->name, 0);
 *   ComputerRes_Load               0x46B070  src/compmap/res.c:32
 *       r->objectivesRes = Resource_Load("LANGUAGE:objectives.res", 0);
 *   LevelStart                     0x415B30  src/game/game.c:433,435
 *       ComputerRes_Load();  then  StatusMessages_Load();
 *   Resource_Find                  0x4B64E0  src/res/resource.c:442-446
 *   Resource_IsLoaded              0x4B5B90  src/res/resource.c:124-147
 *       folded full-name compare while walking gResourceList tail-first (:137)
 *   Resource_Create                0x4B59F0  src/res/resource.c:65-96
 *       every pack member is appended to the one global list (:85)
 *   Resource_Load                  0x4B5F00  src/res/resource.c:247-269
 *
 *   So a mission text key resolves from WHICHEVER mounted archive defines it,
 *   and a key no archive defines renders "RM: <key>" (statusmessage.c:660,
 *   objectives.c:151).
 *
 * Host-portable: the synthetic archives below are written with the product's own
 * writer (RES_WriteEntries), and the corpus test reads IGI_GAME_PATH directly
 * instead of Utils::GetIGIRootPath() (source/utils.cpp needs windows.h and is
 * excluded from the host lane). It self-skips when the corpus is absent.
 *****************************************************************************/

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "mission_text_resource.h"
#include "renderer/res_compiler.h"
#include "renderer/res_writer.h"
#include "support/temp_directory.h"

namespace {

namespace fs = std::filesystem;

// The reported keys, as the play log and the authored tasks spell them.
constexpr const char* kCutSceneKey = "M1_CUT_01";
constexpr const char* kMessageKey = "M1_MSG_6";
constexpr const char* kCameraKey = "CAMERASOFFLINE_1MIN";
constexpr const char* kFailedKey = "MISSION_FAILED";
constexpr const char* kCompleteKey = "MISSION_COMPLETE";

// The four flat names ComputerObjectives_Draw composes its per-objective line
// from. They are the mirror image of the StatusMessage keys: they live in
// objectives.res, not messages.res.
constexpr const char* kObjectiveWordKey = "OBJECTIVE";
constexpr const char* kCompleteWordKey = "COMPLETE";
constexpr const char* kIncompleteWordKey = "INCOMPLETE";
constexpr const char* kFailedWordKey = "FAILED";

// A resource body is a NUL-terminated C string (the engine hands the resource
// pointer straight to strcpy: statusmessage.c:656).
std::vector<uint8_t> Body(const std::string& text) {
    std::vector<uint8_t> data(text.begin(), text.end());
    data.push_back(0);
    return data;
}

fs::path CorpusRoot() {
    if (const char* env = std::getenv("IGI_GAME_PATH"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path("/Users/haseeb-mir/Downloads/IGI");
}

class MissionTextResourceTest : public ::testing::Test {
protected:
    fs::path WriteArchive(const std::string& file_name,
                          const std::vector<RESEntry>& entries) {
        const fs::path path = temporary_directory_.path() / file_name;
        std::string error;
        EXPECT_TRUE(RES_WriteEntries(entries, path.string(), error)) << error;
        return path;
    }

    // The resolver's only editor-side addition over the engine is the
    // missing-key report, injected so it can be asserted here.
    igi::MissionTextResources MakeResources(
        const std::vector<std::string>& archive_paths) {
        return igi::MissionTextResources(
            archive_paths,
            [this](LogLevel level, const std::string& message) {
                logged_levels_.push_back(level);
                logged_messages_.push_back(message);
            });
    }

    test_support::TempDirectory temporary_directory_;
    std::vector<LogLevel> logged_levels_;
    std::vector<std::string> logged_messages_;
};

} // namespace

// ===========================================================================
// The archive list mirrors the engine's load order
// ===========================================================================

TEST(MissionTextArchivePathsTest, ArchiveOrderIsMessagesThenObjectivesPerLanguage) {
    // LevelStart 0x415B30 loads objectives.res first (:433) and messages.res
    // second (:435); Resource_IsLoaded 0x4B5B90 resolves a duplicated name from
    // the newest end of gResourceList (src/res/resource.c:137), so the archive
    // listed FIRST here is the one the engine would have found last — i.e. the
    // winner. ENGLISH precedes USA because that is the editor's existing probe
    // order (the engine mounts exactly one language: Config_Apply 0x405B30,
    // src/profile/profile.c:146-148).
    const std::vector<std::string> paths = igi::MissionTextArchivePaths("X:");
    ASSERT_EQ(paths.size(), 4U);
    EXPECT_EQ(paths[0], "X:\\language\\ENGLISH\\MESSAGES.RES");
    EXPECT_EQ(paths[1], "X:\\language\\ENGLISH\\objectives.res");
    EXPECT_EQ(paths[2], "X:\\language\\USA\\MESSAGES.RES");
    EXPECT_EQ(paths[3], "X:\\language\\USA\\objectives.res");
}

// ===========================================================================
// The defect: a StatusMessage key lives in messages.res, not objectives.res
// ===========================================================================

TEST_F(MissionTextResourceTest, StatusMessageKeyResolvesFromTheMessagesArchive) {
    // The retail split, reproduced: the key is in messages.res only.
    const fs::path messages = WriteArchive("MESSAGES.RES", {
        RESEntry{kMessageKey, Body("ANYA: These gates are locked.")},
        RESEntry{kCutSceneKey, Body("ANYA: Good work getting here so fast.")},
        RESEntry{kCameraKey, Body("Security cameras offline for 1 minute")},
        RESEntry{kFailedKey, Body("Mission Failed!")},
    });
    const fs::path objectives = WriteArchive("objectives.res", {
        RESEntry{"M1_OBJ1", Body("Steal the truck near the main entrance.")},
    });

    const igi::MissionTextResources resources =
        MakeResources({messages.string(), objectives.string()});

    // Resource_Find 0x4B64E0 returns true for every reported key...
    EXPECT_TRUE(resources.Has(kCutSceneKey));
    EXPECT_TRUE(resources.Has(kMessageKey));
    EXPECT_TRUE(resources.Has(kCameraKey));
    EXPECT_TRUE(resources.Has(kFailedKey));

    // ...and Resource_Load 0x4B5F00 yields the stored text, never the key.
    EXPECT_EQ(resources.Resolve(kCutSceneKey),
              "ANYA: Good work getting here so fast.");
    EXPECT_EQ(resources.Resolve(kMessageKey),
              "ANYA: These gates are locked.");
    EXPECT_EQ(resources.Resolve(kCameraKey),
              "Security cameras offline for 1 minute");
    EXPECT_EQ(resources.Resolve(kFailedKey), "Mission Failed!");

    // The map computer's objective name still comes from objectives.res.
    EXPECT_EQ(resources.Resolve("M1_OBJ1"),
              "Steal the truck near the main entrance.");

    // The defect itself: resolving the reported keys produces NO diagnostic.
    EXPECT_TRUE(logged_messages_.empty())
        << "unexpected diagnostics: " << logged_messages_.front();
}

TEST_F(MissionTextResourceTest, ObjectivesArchiveAloneCannotAnswerAMessageKey) {
    // This is the pre-fix behaviour, pinned as the reason the fix is needed:
    // looking a StatusMessage key up in objectives.res is a guaranteed miss.
    const fs::path objectives = WriteArchive("objectives.res", {
        RESEntry{"M1_OBJ1", Body("Steal the truck near the main entrance.")},
    });

    std::unordered_map<std::string, ResEntryInfo> index;
    std::string error;
    ASSERT_TRUE(RES_BuildIndex(objectives.string(), index, error)) << error;
    ResEntryInfo info;
    EXPECT_FALSE(RES_FindIndexEntry(index, kMessageKey, info));
    EXPECT_FALSE(RES_FindIndexEntry(index, kCutSceneKey, info));
    EXPECT_FALSE(RES_FindIndexEntry(index, kCameraKey, info));
    EXPECT_FALSE(RES_FindIndexEntry(index, kFailedKey, info));
    EXPECT_TRUE(RES_FindIndexEntry(index, "M1_OBJ1", info));
}

TEST_F(MissionTextResourceTest, ObjectiveStateWordsResolveFromTheObjectivesArchive) {
    // The HUD objective card's mission-level branches (MISSION_FAILED /
    // MISSION_COMPLETE) and its finished-set composition come from two DIFFERENT
    // archives, and this pins which is which:
    //
    //   StatusMessageScreen_AddMessage 0x486650  src/mission/statusmessage.c:653-660
    //       MISSION_FAILED / MISSION_COMPLETE are StatusMessage text, resolved
    //       flat; StatusMessages_Load 0x4868A0 mounts LANGUAGE:messages.res (:671)
    //   ComputerObjectives_Draw        0x469110  src/compmap/objectives.c:137-143
    //       crt_sprintf(text, "%s %d: ", Resource_Load("OBJECTIVE", 0), i + 1);
    //       if (obj->failed) s = Resource_Load("FAILED", 0); ...
    //       OBJECTIVE / FAILED / COMPLETE / INCOMPLETE come from
    //       LANGUAGE:objectives.res, the archive ComputerRes_Load 0x46B070 mounts
    //       (src/compmap/res.c:32)
    const fs::path messages = WriteArchive("MESSAGES.RES", {
        RESEntry{kFailedKey, Body("Mission Failed!")},
        RESEntry{kCompleteKey, Body("Mission Complete!")},
    });
    const fs::path objectives = WriteArchive("objectives.res", {
        RESEntry{kObjectiveWordKey, Body("Objective")},
        RESEntry{kCompleteWordKey, Body("Complete")},
        RESEntry{kIncompleteWordKey, Body("Incomplete")},
        RESEntry{kFailedWordKey, Body("Failed!")},
    });

    const igi::MissionTextResources resources =
        MakeResources({messages.string(), objectives.string()});

    // The engine's per-objective status words...
    EXPECT_EQ(resources.Resolve(kObjectiveWordKey), "Objective");
    EXPECT_EQ(resources.Resolve(kCompleteWordKey), "Complete");
    EXPECT_EQ(resources.Resolve(kIncompleteWordKey), "Incomplete");
    EXPECT_EQ(resources.Resolve(kFailedWordKey), "Failed!");
    // ...and the mission-level StatusMessage key, in one mounted set.
    EXPECT_EQ(resources.Resolve(kFailedKey), "Mission Failed!");
    EXPECT_EQ(resources.Resolve(kCompleteKey), "Mission Complete!");

    // A full-name compare, not a prefix one: COMPLETE does not answer
    // MISSION_COMPLETE (Resource_IsLoaded 0x4B5B90, src/res/resource.c:136-138).
    EXPECT_NE(resources.Resolve(kCompleteKey),
              resources.Resolve(kCompleteWordKey));

    EXPECT_TRUE(logged_messages_.empty())
        << "unexpected diagnostics: " << logged_messages_.front();
}

// ===========================================================================
// Engine lookup semantics
// ===========================================================================

TEST_F(MissionTextResourceTest, DuplicatedNameResolvesFromTheArchiveLoadedLast) {
    // M14_OBJ1 and M14_OBJ2 are the two names both retail archives define. The
    // engine walks gResourceList tail-first (src/res/resource.c:137) and
    // messages.res is loaded after objectives.res (game.c:433,435), so
    // messages.res wins. (In the shipped data the two texts are identical, so
    // this is only observable with different bodies.)
    const fs::path messages = WriteArchive("MESSAGES.RES", {
        RESEntry{"M14_OBJ1", Body("from messages.res")},
    });
    const fs::path objectives = WriteArchive("objectives.res", {
        RESEntry{"M14_OBJ1", Body("from objectives.res")},
    });

    const igi::MissionTextResources resources =
        MakeResources({messages.string(), objectives.string()});
    EXPECT_EQ(resources.Resolve("M14_OBJ1"), "from messages.res");
}

TEST_F(MissionTextResourceTest, LookupFoldsCaseLikeTheEngine) {
    // String_ToUpper 0x4B5AF0 (src/res/resource.c:98-104) + Resource_IsLoaded
    // 0x4B5B90 (src/res/resource.c:136-138): the comparison is on the folded
    // full name, so an authored lowercase key still resolves.
    const fs::path messages = WriteArchive("MESSAGES.RES", {
        RESEntry{kMessageKey, Body("ANYA: These gates are locked.")},
    });

    const igi::MissionTextResources resources = MakeResources({messages.string()});
    EXPECT_TRUE(resources.Has("m1_msg_6"));
    EXPECT_EQ(resources.Resolve("m1_msg_6"), "ANYA: These gates are locked.");
}

TEST_F(MissionTextResourceTest, MissingKeyRendersTheEnginesOwnMarker) {
    // StatusMessageScreen_AddMessage 0x486650 (src/mission/statusmessage.c:660)
    // and ComputerObjectives_Draw 0x469110 (src/compmap/objectives.c:151) both
    // render "RM: <key>" when Resource_Find fails — the key itself is never the
    // displayed text.
    const fs::path messages = WriteArchive("MESSAGES.RES", {
        RESEntry{kMessageKey, Body("ANYA: These gates are locked.")},
    });

    const igi::MissionTextResources resources = MakeResources({messages.string()});
    EXPECT_FALSE(resources.Has("NO_SUCH_MISSION_TEXT_KEY"));
    EXPECT_EQ(resources.Resolve("NO_SUCH_MISSION_TEXT_KEY"),
              "RM: NO_SUCH_MISSION_TEXT_KEY");

    // The editor reports the genuine gap ONCE, naming the archives it searched —
    // not once per archive with the name of an archive that was never the home
    // of the key (which is what the play log showed).
    ASSERT_EQ(logged_messages_.size(), 1U);
    EXPECT_EQ(logged_levels_[0], LogLevel::WARNING);
    EXPECT_NE(logged_messages_[0].find("NO_SUCH_MISSION_TEXT_KEY"), std::string::npos);
    EXPECT_NE(logged_messages_[0].find(messages.string()), std::string::npos);
}

TEST_F(MissionTextResourceTest, UnreadableArchiveIsAMissNotACrash) {
    // The engine's "LANGUAGE:" alias names exactly one directory
    // (src/profile/profile.c:146-148); an editor probing two spellings must
    // tolerate the one that is not installed.
    const igi::MissionTextResources resources =
        MakeResources({(temporary_directory_.path() / "absent.res").string()});
    EXPECT_FALSE(resources.Has(kMessageKey));
    EXPECT_EQ(resources.Resolve(kMessageKey), "RM: M1_MSG_6");
}

// ===========================================================================
// The retail corpus: the reported keys really are in MESSAGES.RES
// ===========================================================================

TEST_F(MissionTextResourceTest, RetailMessagesArchiveCarriesTheReportedKeys) {
    const fs::path language = CorpusRoot() / "language";
    const fs::path messages = language / "USA" / "MESSAGES.RES";
    const fs::path objectives = language / "USA" / "objectives.res";
    if (!fs::is_regular_file(messages) || !fs::is_regular_file(objectives)) {
        GTEST_SKIP() << "retail language archives not found under "
                     << language.string() << " (set IGI_GAME_PATH)";
    }

    // The diagnosis, on the shipped data: objectives.res does NOT define a
    // single one of the reported keys...
    std::unordered_map<std::string, ResEntryInfo> objectives_index;
    std::string error;
    ASSERT_TRUE(RES_BuildIndex(objectives.string(), objectives_index, error))
        << error;
    ResEntryInfo info;
    for (const char* key : {kCutSceneKey, kMessageKey, kCameraKey, kFailedKey,
                            kCompleteKey}) {
        EXPECT_FALSE(RES_FindIndexEntry(objectives_index, key, info))
            << key << " unexpectedly IS in objectives.res";
    }
    // ...while the map computer's objective name IS there.
    EXPECT_TRUE(RES_FindIndexEntry(objectives_index, "M1_OBJ1", info));

    // The other half of the split, on the same shipped data: the four names
    // ComputerObjectives_Draw composes its objective line from are defined by
    // objectives.res and by nothing else -- messages.res does not carry them
    // (src/compmap/objectives.c:137-143 vs src/compmap/res.c:32).
    std::unordered_map<std::string, ResEntryInfo> messages_index;
    ASSERT_TRUE(RES_BuildIndex(messages.string(), messages_index, error))
        << error;
    for (const char* key : {kObjectiveWordKey, kCompleteWordKey,
                            kIncompleteWordKey, kFailedWordKey}) {
        EXPECT_TRUE(RES_FindIndexEntry(objectives_index, key, info))
            << key << " is not in objectives.res";
        EXPECT_FALSE(RES_FindIndexEntry(messages_index, key, info))
            << key << " unexpectedly IS in messages.res";
    }
    // And both mission-status keys are in messages.res, the reverse direction.
    for (const char* key : {kFailedKey, kCompleteKey}) {
        EXPECT_TRUE(RES_FindIndexEntry(messages_index, key, info))
            << key << " is not in messages.res";
        EXPECT_FALSE(RES_FindIndexEntry(objectives_index, key, info))
            << key << " unexpectedly IS in objectives.res";
    }

    // The engine's mounted set answers all of them, and none of them renders the
    // resource-missing marker.
    const igi::MissionTextResources resources =
        MakeResources({messages.string(), objectives.string()});
    for (const char* key : {kCutSceneKey, kMessageKey, kCameraKey, kFailedKey,
                            kCompleteKey, kObjectiveWordKey, kCompleteWordKey,
                            kIncompleteWordKey, kFailedWordKey, "M1_OBJ1"}) {
        ASSERT_TRUE(resources.Has(key)) << key << " not found in the retail set";
        const std::string text = resources.Resolve(key);
        EXPECT_FALSE(text.empty()) << key;
        EXPECT_NE(text.rfind("RM: ", 0), 0u) << key << " rendered as " << text;
    }

    // The exact strings the HUD card now shows for the two mission-level states,
    // straight out of the shipped archive.
    EXPECT_EQ(resources.Resolve(kFailedKey), "Mission Failed!");
    EXPECT_EQ(resources.Resolve(kCompleteKey), "Mission Complete!");
    EXPECT_EQ(resources.Resolve(kObjectiveWordKey), "Objective");
    EXPECT_EQ(resources.Resolve(kCompleteWordKey), "Complete");

    // A clean run of the retail corpus must not report a single missing key:
    // that is exactly the 44-warning defect.
    EXPECT_TRUE(logged_messages_.empty())
        << "unexpected diagnostics: " << logged_messages_.front();
}
