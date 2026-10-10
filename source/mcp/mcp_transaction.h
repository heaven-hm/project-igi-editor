#pragma once

#include "game_data_service.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace mcp {

class Transaction {
public:
    using Validator = std::function<bool(const std::filesystem::path& relative_path,
                                         const std::vector<std::uint8_t>& bytes,
                                         std::string& error)>;
    using PostValidator = std::function<bool(const std::filesystem::path& relative_path,
                                             const std::filesystem::path& absolute_path,
                                             std::string& error)>;
    using CommitGuard = std::function<bool(std::string& error)>;
    using CommitObserver = std::function<void()>;
    using RollbackGuard = std::function<bool(std::string& error)>;

    Transaction(ProjectScope scope, MutationOptions options,
                std::filesystem::path allowed_prefix = {},
                std::shared_ptr<std::mutex> mutation_mutex = {},
                std::unique_lock<std::mutex> mutation_lock = {});
    ~Transaction();

    bool Stage(const std::filesystem::path& relative_path,
               std::vector<std::uint8_t> bytes, std::string& error);
    void SetValidator(Validator validator) { validator_ = std::move(validator); }
    void SetPostValidator(PostValidator validator) { post_validator_ = std::move(validator); }
    void SetCommitGuard(CommitGuard guard) { commit_guard_ = std::move(guard); }
    void SetCommitObserver(CommitObserver observer) { commit_observer_ = std::move(observer); }
    void SetRollbackGuard(RollbackGuard guard) { rollback_guard_ = std::move(guard); }
    bool Commit(std::string& error);
    bool Rollback(std::string& error);

    const std::filesystem::path& backup_directory() const noexcept {
        return backup_directory_;
    }

private:
    struct StagedFile {
        std::filesystem::path relative_path;
        std::filesystem::path target_path;
        std::vector<std::uint8_t> bytes;
        bool existed = false;
        bool replaced = false;
    };

    bool CreateBackups(std::string& error);
    bool AcquireFilesystemLocks(std::string& error);
    bool WriteAndReplace(StagedFile& file, std::size_t sequence, std::string& error);
    bool Restore(StagedFile& file, std::size_t sequence, std::string& error);
    void RemoveTemporaryFiles() noexcept;
    void ReleaseFilesystemLocks() noexcept;
    void ReleaseMutationLock() noexcept;

    ProjectScope scope_;
    MutationOptions options_;
    std::filesystem::path allowed_prefix_;
    std::shared_ptr<std::mutex> mutation_mutex_;
    std::unique_lock<std::mutex> mutation_lock_;
    Validator validator_;
    PostValidator post_validator_;
    CommitGuard commit_guard_;
    CommitObserver commit_observer_;
    RollbackGuard rollback_guard_;
    std::vector<StagedFile> staged_files_;
    std::filesystem::path backup_directory_;
    std::vector<std::filesystem::path> temporary_files_;
    std::vector<std::uintptr_t> filesystem_locks_;
    bool committed_ = false;
    bool committing_ = false;
};

}  // namespace mcp
