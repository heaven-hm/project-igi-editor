#pragma once

#include "mcp_json.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifdef GetObject
#undef GetObject
#endif

namespace mcp {

class Transaction;

struct LevelRevision {
    int level = 0;
    std::string fingerprint;
};

struct MutationOptions {
    bool dry_run = false;
    bool backup = true;
    std::optional<std::string> expected_revision;
};

class ProjectScope {
public:
    static std::optional<ProjectScope> Open(const std::filesystem::path& project_root,
                                            std::string& error);

    const std::filesystem::path& root() const noexcept { return root_; }
    bool ResolveRelative(const std::filesystem::path& relative_path,
                         std::filesystem::path& resolved_path, std::string& error,
                         bool allow_internal_backup = false) const;
    bool RelativeToRoot(const std::filesystem::path& path,
                        std::filesystem::path& relative_path, std::string& error) const;
    bool IsSupportedPath(const std::filesystem::path& relative_path) const;
    bool LevelDirectory(int level, std::filesystem::path& level_directory,
                        std::string& error) const;

private:
    explicit ProjectScope(std::filesystem::path root) : root_(std::move(root)) {}

    std::filesystem::path root_;
};

class GameDataService {
public:
    explicit GameDataService(ProjectScope scope) : scope_(std::move(scope)) {}

    const ProjectScope& scope() const noexcept { return scope_; }

    bool OpenLevel(int level, std::string& error);
    bool HasOpenLevel() const;
    LevelRevision CurrentRevision() const;
    bool RefreshRevision(std::string& error);
    JsonValue ProjectInfo() const;
    JsonValue ListLevels(std::string& error) const;
    JsonValue LevelManifest(int level, std::string& error) const;
    JsonValue ListObjects(int level, std::string& error) const;
    JsonValue GetObject(int level, std::string_view task_id, std::string& error) const;
    JsonValue ObjectSnapshotFromSource(int level, std::string_view source,
                                       std::string_view task_id, std::string& error) const;
    JsonValue ObjectSnapshotFromSource(int level, std::string_view source,
                                       std::string_view task_id, int source_line,
                                       std::string& error) const;
    bool IsAvailablePickupId(std::string_view pickup_id, std::string& error) const;
    bool IsAvailableModelId(std::string_view model_id, std::string& error) const;
    bool IsAvailableWeaponId(std::string_view weapon_id, std::string& error) const;
    bool IsAvailableAmmoId(std::string_view ammo_id, std::string& error) const;
    JsonValue ValidateLevel(int level, std::string& error) const;
    bool LoadCurrentObjectSource(std::string& source, std::string& error) const;
    bool LoadProjectText(const std::filesystem::path& relative_path,
                         std::string& text, std::string& error) const;
    std::string ProjectRevision(const std::vector<std::filesystem::path>& relative_paths,
                                std::string& error) const;
    std::unique_ptr<Transaction> BeginProjectMutation(
        const MutationOptions& options,
        const std::vector<std::filesystem::path>& tracked_paths,
        std::string& error);
    bool SaveCurrentObjectSource(std::string_view source, const MutationOptions& options,
                                 std::string& error);
    std::unique_ptr<Transaction> BeginMutation(const MutationOptions& options,
                                               std::string& error);

private:
    bool RefreshRevisionUnlocked(std::string& error);
    bool CalculateRevision(int level, const std::filesystem::path& level_directory,
                           LevelRevision& revision, std::string& error) const;

    ProjectScope scope_;
    std::optional<LevelRevision> current_revision_;
    std::shared_ptr<std::mutex> mutation_mutex_ = std::make_shared<std::mutex>();
};

}  // namespace mcp
