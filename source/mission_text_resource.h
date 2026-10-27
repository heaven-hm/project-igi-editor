#pragma once

/******************************************************************************
 * @file    mission_text_resource.h
 * @brief   Localized mission text lookup (StatusMessage / map-computer
 *          objective names), transcribed from the retail engine's resource
 *          layer.
 *
 * The engine oracle is /Users/haseeb-mir/Documents/Code/IGI-RE (decompiled
 * retail IGI.exe). The behavioural claim this module encodes is:
 *
 *   A mission text parameter (a StatusMessage's `text`, a
 *   DefineComputerObjective slot's `name`) is a BARE resource name, and the
 *   engine resolves it against its ONE global resource list — not against a
 *   named archive chosen by the caller:
 *
 *     StatusMessageScreen_AddMessage 0x486650  src/mission/statusmessage.c:653
 *         if (strlen(m->text) < 0x80 && Resource_Find(m->text)) {
 *             char *text = Resource_Load(m->text, NULL);      // :654
 *     ComputerObjectives_Draw        0x469110  src/compmap/objectives.c:148-149
 *         if (Resource_Find(obj->name)) s = Resource_Load(obj->name, 0);
 *     Resource_Find                  0x4B64E0  src/res/resource.c:442-446
 *         return Resource_IsLoaded(name, NULL);
 *     Resource_IsLoaded              0x4B5B90  src/res/resource.c:124-147
 *         upper-cases the name ('\' folded to '/', String_ToUpper 0x4B5AF0,
 *         :98-104) and compares it in full while walking gResourceList
 *         from its tail (:137)
 *     Resource_Create                0x4B59F0  src/res/resource.c:65-96
 *         every member of every loaded pack is List_AddTail'd onto
 *         gResourceList (:85) as well as onto its pack (:324)
 *
 *   so a key resolves from whichever mounted archive defines it: the
 *   StatusMessage text keys ("M1_MSG_6", "M1_CUT_01", "CAMERASOFFLINE_1MIN",
 *   "MISSION_FAILED") come from LANGUAGE:messages.res, and the map computer's
 *   objective names ("M1_OBJ1") from LANGUAGE:objectives.res. Both archives
 *   are mounted for the same level load, so both are searched.
 *
 *   When no mounted archive defines the key the engine does not silently show
 *   the key: it renders "RM: %s" (statusmessage.c:660, objectives.c:151).
 *   Resolve() returns that same string.
 *****************************************************************************/

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "logger.h"
#include "renderer/res_writer.h"

namespace igi {

// The language text archives the engine has mounted while a level runs, in the
// order its global resource list resolves a duplicated name (see the .cpp for
// the load order and the tail-first walk). `game_root` is the install root
// (Utils::GetIGIRootPath()).
std::vector<std::string> MissionTextArchivePaths(const std::string& game_root);

// Editor-side diagnostic seam. The engine has no logger for a text key it cannot
// find — it renders "RM: <key>" and carries on (statusmessage.c:660,
// objectives.c:151) — so the editor's reporting is injected instead of
// hard-wired to Logger. That keeps this module host-portable (Logger::Log needs
// Config::Get(), which the host lane excludes) and lets a test assert on the
// exact message.
using MissionTextLogSink = std::function<void(LogLevel level, const std::string& message)>;

// Engine lookup of a bare mission text key over those archives.
//
// The index for each archive is built once (RES_BuildIndex), so a miss never
// goes through RES_Extract and therefore never produces that API's per-archive
// "Resource not found" warning; Resolve() reports at most one accurate line.
class MissionTextResources {
public:
    MissionTextResources() = default;
    explicit MissionTextResources(std::vector<std::string> archive_paths,
                                  MissionTextLogSink log_sink = {});

    // Resource_Find 0x4B64E0 (src/res/resource.c:442-446)
    bool Has(const std::string& resource_key) const;

    // Resource_Load 0x4B5F00 (src/res/resource.c:247-269) as the engine's
    // message/objective drawers call it: the stored NUL-terminated text, or
    // the engine's own missing-resource rendering "RM: <key>".
    std::string Resolve(const std::string& resource_key) const;

    const std::vector<std::string>& archive_paths() const { return archive_paths_; }

private:
    struct Archive {
        std::string path;
        std::unordered_map<std::string, ResEntryInfo> index;
    };

    void Report(LogLevel level, const std::string& message) const;

    std::vector<std::string> archive_paths_;
    std::vector<Archive> archives_;
    MissionTextLogSink log_sink_;
};

// "RM: %s" — the engine's rendering when Resource_Find fails
// (src/mission/statusmessage.c:660, src/compmap/objectives.c:151).
std::string MissionTextMissingRendering(const std::string& resource_key);

} // namespace igi
