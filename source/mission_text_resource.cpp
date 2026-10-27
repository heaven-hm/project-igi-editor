/******************************************************************************
 * @file    mission_text_resource.cpp
 * @brief   Localized mission text lookup — engine oracle citations in the
 *          header; the load order and the duplicate-name tie-break are here.
 *
 * Where the language archives come from (engine):
 *
 *   LevelStart            0x415B30  src/game/game.c:374-493
 *       ComputerRes_Load();       // :433   -> LANGUAGE:objectives.res
 *       StatusMessages_Load();    // :435   -> LANGUAGE:messages.res
 *   ComputerRes_Load      0x46B070  src/compmap/res.c:25-62
 *       r->objectivesRes = Resource_Load("LANGUAGE:objectives.res", 0);  // :32
 *   StatusMessages_Load   0x4868A0  src/mission/statusmessage.c:669-673
 *       gMessagesRes = Resource_Load("LANGUAGE:messages.res", NULL);     // :671
 *
 * The "LANGUAGE:" alias is the active profile's language directory, not a
 * fixed one:
 *
 *   Config_Apply          0x405B30  src/profile/profile.c:140-151
 *       crt_sprintf(path, "LOCAL:language/%s/",
 *                   gLanguageNames[gProfiles[gActiveProfile].language]);  // :146
 *       Alias_Set("LANGUAGE:", path);                                    // :148
 *
 * so exactly one language directory is mounted per run. The editor has no
 * profile; it probes the two English spellings the retail install ships
 * (LANGUAGE/ENGLISH, LANGUAGE/USA) in the pre-existing ENGLISH-then-USA order.
 *
 * Duplicate names between the two archives: both are loaded as packs, and every
 * pack member is appended to the one global list (Resource_Create 0x4B59F0,
 * src/res/resource.c:85) as well as to its pack (:324). Resource_IsLoaded
 * 0x4B5B90 walks that list from gResourceList.tailpred backwards
 * (src/res/resource.c:137), so the archive loaded LAST wins a name both
 * archives define. objectives.res is loaded first (:433) and messages.res
 * second (:435), so messages.res precedes objectives.res here. In the shipped
 * data this tie-break only ever applies to M14_OBJ1 and M14_OBJ2, whose text is
 * byte-identical in both archives.
 *****************************************************************************/

#include "mission_text_resource.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "logger.h"

namespace igi {
namespace {

// The engine names these by alias ("LANGUAGE:messages.res"); the retail files
// are LANGUAGE/<lang>/MESSAGES.RES and LANGUAGE/<lang>/objectives.res.
constexpr const char* kMessagesArchiveName = "\\MESSAGES.RES";
constexpr const char* kObjectivesArchiveName = "\\objectives.res";

} // namespace

std::vector<std::string> MissionTextArchivePaths(const std::string& game_root) {
    // The engine mounts one language (Config_Apply 0x405B30,
    // src/profile/profile.c:146-148); the editor probes the two English
    // spellings the retail install ships, ENGLISH first (pre-existing editor
    // behaviour). Within one language, messages.res precedes objectives.res
    // because it is loaded second (LevelStart 0x415B30, src/game/game.c:433,435)
    // and Resource_IsLoaded resolves a duplicate name from the newest end of
    // gResourceList (src/res/resource.c:137).
    static const char* const kLanguageDirectories[] = {"ENGLISH", "USA"};

    std::vector<std::string> paths;
    for (const char* language : kLanguageDirectories) {
        const std::string language_root =
            game_root + "\\language\\" + language;
        paths.push_back(language_root + kMessagesArchiveName);
        paths.push_back(language_root + kObjectivesArchiveName);
    }
    return paths;
}

std::string MissionTextMissingRendering(const std::string& resource_key) {
    // crt_sprintf(l->text, "RM: %s", m->text);  statusmessage.c:660
    // crt_sprintf(text, "RM: %s", obj->name);   objectives.c:151
    return "RM: " + resource_key;
}

MissionTextResources::MissionTextResources(std::vector<std::string> archive_paths,
                                           MissionTextLogSink log_sink)
    : archive_paths_(std::move(archive_paths)),
      log_sink_(std::move(log_sink)) {
    archives_.reserve(archive_paths_.size());
    for (const std::string& path : archive_paths_) {
        Archive archive;
        archive.path = path;
        // A missing archive is not an error: the engine's "LANGUAGE:" alias
        // points at one directory only, so an editor probing two spellings must
        // tolerate the one that is not installed. RES_BuildIndex reports the
        // failure through its own error string, which is discarded here on
        // purpose — the only observable is whether a key resolves.
        std::string error;
        RES_BuildIndex(path, archive.index, error);
        archives_.push_back(std::move(archive));
    }
}

bool MissionTextResources::Has(const std::string& resource_key) const {
    for (const Archive& archive : archives_) {
        ResEntryInfo info;
        if (RES_FindIndexEntry(archive.index, resource_key, info)) {
            return true;
        }
    }
    return false;
}

std::string MissionTextResources::Resolve(const std::string& resource_key) const {
    for (const Archive& archive : archives_) {
        ResEntryInfo info;
        if (!RES_FindIndexEntry(archive.index, resource_key, info)) {
            continue;
        }

        // Resource_Find returned true, so the engine takes the Resource_Load
        // branch (statusmessage.c:653-656, objectives.c:148-149). A zero-length
        // body is an empty string, not a miss.
        if (info.data_size == 0) {
            return std::string();
        }

        const std::vector<uint8_t> resource_data =
            RES_ReadEntry(archive.path, info);
        if (resource_data.empty()) {
            Report(
                LogLevel::ERR,
                "[RES] Mission text '" + resource_key +
                    "' is defined in " + archive.path +
                    " but its body could not be read");
            return std::string();
        }

        // The body is a NUL-terminated C string; the engine hands the resource
        // pointer straight to strcpy (statusmessage.c:656).
        const auto terminator = std::find(
            resource_data.begin(),
            resource_data.end(),
            static_cast<uint8_t>(0));
        return std::string(resource_data.begin(), terminator);
    }

    // No mounted archive defines the key. The engine shows "RM: <key>"
    // (statusmessage.c:660, objectives.c:151); the editor additionally reports
    // the genuine data gap once, naming the archives it searched. The engine has
    // no such log — this is editor-side diagnostics, not engine behaviour.
    if (!resource_key.empty()) {
        std::string searched;
        for (const std::string& path : archive_paths_) {
            if (!searched.empty()) {
                searched += ", ";
            }
            searched += path;
        }
        Report(
            LogLevel::WARNING,
            "[RES] Mission text resource not found: " + resource_key +
                " (no mounted language archive defines it; searched " +
                searched + ")");
    }
    return MissionTextMissingRendering(resource_key);
}

void MissionTextResources::Report(LogLevel level, const std::string& message) const {
    if (log_sink_) {
        log_sink_(level, message);
    }
}

} // namespace igi
