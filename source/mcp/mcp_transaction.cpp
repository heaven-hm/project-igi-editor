#include "mcp_transaction.h"

#include <atomic>
#include <chrono>
#include <algorithm>
#include <cctype>
#include <fstream>

#ifdef _WIN32
#include <windows.h>
#endif

namespace mcp {
namespace {

std::atomic_uint64_t g_transaction_sequence{0};

struct CommitScope {
    explicit CommitScope(bool& committing) : committing_(committing) { committing_ = true; }
    ~CommitScope() { committing_ = false; }
    bool& committing_;
};

std::string FoldPath(const std::filesystem::path& path) {
    std::string result = path.generic_string();
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return result;
}

std::string TrimWin32Component(std::string value) {
    while (!value.empty() && (value.back() == '.' || value.back() == ' ')) value.pop_back();
    return value;
}

bool IsWithin(const std::filesystem::path& prefix, const std::filesystem::path& candidate) {
    const std::string prefix_text = FoldPath(prefix.lexically_normal());
    const std::string candidate_text = FoldPath(candidate.lexically_normal());
    return candidate_text == prefix_text ||
           (candidate_text.size() > prefix_text.size() &&
            candidate_text.starts_with(prefix_text) && candidate_text[prefix_text.size()] == '/');
}

bool IsMcpBackupPath(const std::filesystem::path& path) {
    for (const auto& component : path) {
        if (TrimWin32Component(FoldPath(component)) == ".mcp-backups") return true;
    }
    return false;
}

std::string UniqueSuffix(std::size_t sequence) {
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::to_string(ticks) + "-" + std::to_string(g_transaction_sequence.fetch_add(1)) +
           "-" + std::to_string(sequence);
}

bool WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    output.flush();
    return output.good();
}

bool ReplaceSameDirectory(const std::filesystem::path& temporary_path,
                          const std::filesystem::path& target_path) {
#ifdef _WIN32
    return MoveFileExW(temporary_path.c_str(), target_path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary_path, target_path, error);
    return !error;
#endif
}

bool CopyToReplacement(const std::filesystem::path& source, const std::filesystem::path& target,
                       const std::string& suffix, std::vector<std::filesystem::path>& temporary_files) {
    const std::filesystem::path temporary = target.parent_path() /
        ("." + target.filename().string() + ".mcp-restore-" + suffix + ".tmp");
    std::error_code error;
    std::filesystem::copy_file(source, temporary, std::filesystem::copy_options::overwrite_existing, error);
    if (error) return false;
    temporary_files.push_back(temporary);
    if (!ReplaceSameDirectory(temporary, target)) return false;
    temporary_files.pop_back();
    return true;
}

#ifdef _WIN32
bool OpenDirectoryLock(const std::filesystem::path& directory, std::uintptr_t& handle) {
    const HANDLE opened = CreateFileW(
        directory.c_str(), FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (opened == INVALID_HANDLE_VALUE) return false;

    const DWORD attributes = GetFileAttributesW(directory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0 ||
        (attributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        CloseHandle(opened);
        return false;
    }
    handle = reinterpret_cast<std::uintptr_t>(opened);
    return true;
}

#endif

}  // namespace

Transaction::Transaction(ProjectScope scope, MutationOptions options,
                         std::filesystem::path allowed_prefix,
                         std::shared_ptr<std::mutex> mutation_mutex,
                         std::unique_lock<std::mutex> mutation_lock)
    : scope_(std::move(scope)), options_(std::move(options)),
      allowed_prefix_(std::move(allowed_prefix)), mutation_mutex_(std::move(mutation_mutex)),
      mutation_lock_(std::move(mutation_lock)) {}

Transaction::~Transaction() {
    RemoveTemporaryFiles();
    ReleaseFilesystemLocks();
}

bool Transaction::Stage(const std::filesystem::path& relative_path,
                        std::vector<std::uint8_t> bytes, std::string& error) {
    if (committed_) {
        error = "invalid_transaction";
        return false;
    }
    if (IsMcpBackupPath(relative_path)) {
        error = "path_forbidden";
        return false;
    }

    std::filesystem::path target_path;
    if (!scope_.ResolveRelative(relative_path, target_path, error)) return false;
    std::filesystem::path target_relative;
    if (!scope_.RelativeToRoot(target_path, target_relative, error)) return false;
    if (!allowed_prefix_.empty() && !IsWithin(allowed_prefix_, target_relative)) {
        error = "path_forbidden";
        return false;
    }
    for (const auto& staged : staged_files_) {
        if (FoldPath(staged.target_path) == FoldPath(target_path)) {
            error = "duplicate_stage_path";
            return false;
        }
    }
    staged_files_.push_back({relative_path.lexically_normal(), std::move(target_path), std::move(bytes)});
    error.clear();
    return true;
}

bool Transaction::CreateBackups(std::string& error) {
    const std::filesystem::path backup_root =
        std::filesystem::path(".mcp-backups") / ("transaction-" + UniqueSuffix(0));
    std::filesystem::path backup_directory;
    if (!scope_.ResolveRelative(backup_root, backup_directory, error, true)) return false;

    std::error_code directory_error;
    std::filesystem::create_directories(backup_directory, directory_error);
    if (directory_error) {
        error = "backup_failed";
        return false;
    }
    backup_directory_ = backup_root;

    const auto fail = [&](std::string failure) {
        std::error_code cleanup_error;
        std::filesystem::remove_all(backup_directory, cleanup_error);
        if (cleanup_error) {
            backup_directory_ = backup_root;
            error = "backup_cleanup_failed";
            return false;
        }
        backup_directory_.clear();
        error = std::move(failure);
        return false;
    };

    for (auto& file : staged_files_) {
        std::filesystem::path checked_target;
        if (!scope_.ResolveRelative(file.relative_path, checked_target, error)) {
            return fail(error.empty() ? "backup_failed" : error);
        }
        std::filesystem::path checked_relative;
        if (!scope_.RelativeToRoot(checked_target, checked_relative, error)) {
            return fail(error.empty() ? "backup_failed" : error);
        }
        if (FoldPath(checked_target) != FoldPath(file.target_path) ||
            (!allowed_prefix_.empty() && !IsWithin(allowed_prefix_, checked_relative))) {
            return fail("path_forbidden");
        }
        file.target_path = std::move(checked_target);

        std::error_code exists_error;
        file.existed = std::filesystem::exists(file.target_path, exists_error);
        if (exists_error) {
            return fail("backup_failed");
        }
        if (!file.existed) continue;

        const std::filesystem::path backup_path = backup_directory / file.relative_path;
        std::filesystem::create_directories(backup_path.parent_path(), directory_error);
        if (directory_error) return fail("backup_failed");
        std::filesystem::copy_file(file.target_path, backup_path,
                                   std::filesystem::copy_options::overwrite_existing, directory_error);
        if (directory_error) return fail("backup_failed");
    }

    error.clear();
    return true;
}

bool Transaction::AcquireFilesystemLocks(std::string& error) {
#ifdef _WIN32
    for (const auto& file : staged_files_) {
        std::filesystem::path current = scope_.root();
        std::uintptr_t root_handle = 0;
        if (!OpenDirectoryLock(current, root_handle)) {
            error = "path_forbidden";
            ReleaseFilesystemLocks();
            return false;
        }
        filesystem_locks_.push_back(root_handle);

        for (const auto& component : file.relative_path.parent_path()) {
            if (component == "." || component.empty()) continue;
            current /= component;
            std::uintptr_t directory_handle = 0;
            if (!OpenDirectoryLock(current, directory_handle)) {
                error = "path_forbidden";
                ReleaseFilesystemLocks();
                return false;
            }
            filesystem_locks_.push_back(directory_handle);
        }
    }
    error.clear();
    return true;
#else
    error.clear();
    return true;
#endif
}

bool Transaction::WriteAndReplace(StagedFile& file, std::size_t sequence, std::string& error) {
    std::filesystem::path checked_target;
    if (!scope_.ResolveRelative(file.relative_path, checked_target, error)) return false;
    std::filesystem::path checked_relative;
    if (!scope_.RelativeToRoot(checked_target, checked_relative, error)) return false;
    if (!allowed_prefix_.empty() && !IsWithin(allowed_prefix_, checked_relative)) {
        error = "path_forbidden";
        return false;
    }
    file.target_path = std::move(checked_target);

    std::error_code directory_error;
    std::filesystem::create_directories(file.target_path.parent_path(), directory_error);
    if (directory_error) {
        error = "write_failed";
        return false;
    }

    const std::filesystem::path temporary = file.target_path.parent_path() /
        ("." + file.target_path.filename().string() + ".mcp-stage-" + UniqueSuffix(sequence) + ".tmp");
    temporary_files_.push_back(temporary);
    if (!WriteBytes(temporary, file.bytes) || !ReplaceSameDirectory(temporary, file.target_path)) {
        error = "write_failed";
        return false;
    }
    temporary_files_.pop_back();
    file.replaced = true;
    error.clear();
    return true;
}

bool Transaction::Restore(StagedFile& file, std::size_t sequence, std::string& error) {
    if (!file.replaced) return true;
    if (!file.existed) {
        std::error_code remove_error;
        std::filesystem::remove(file.target_path, remove_error);
        if (remove_error) {
            error = "rollback_failed";
            return false;
        }
        file.replaced = false;
        return true;
    }

    std::filesystem::path backup_root;
    if (!scope_.ResolveRelative(backup_directory_, backup_root, error, true)) return false;
    if (!CopyToReplacement(backup_root / file.relative_path, file.target_path,
                           UniqueSuffix(sequence), temporary_files_)) {
        error = "rollback_failed";
        return false;
    }
    file.replaced = false;
    return true;
}

bool Transaction::Commit(std::string& error) {
    if (committed_ || staged_files_.empty()) {
        error = "invalid_transaction";
        ReleaseMutationLock();
        return false;
    }
    CommitScope commit_scope(committing_);
    if (commit_guard_ && !commit_guard_(error)) {
        ReleaseMutationLock();
        return false;
    }
    if (validator_) {
        for (const auto& file : staged_files_) {
            std::string validation_error;
            if (!validator_(file.relative_path, file.bytes, validation_error)) {
                error = "validation_failed";
                ReleaseFilesystemLocks();
                ReleaseMutationLock();
                return false;
            }
        }
    }
    if (options_.dry_run) {
        if (post_validator_) {
            std::error_code temp_directory_error;
            const std::filesystem::path temp_directory =
                std::filesystem::temp_directory_path(temp_directory_error);
            if (temp_directory_error) {
                error = "validation_failed";
                ReleaseMutationLock();
                return false;
            }
            for (std::size_t index = 0; index < staged_files_.size(); ++index) {
                const std::filesystem::path temporary = temp_directory /
                    ("igi-mcp-dry-run-" + UniqueSuffix(index) + "-" +
                     staged_files_[index].relative_path.filename().string());
                if (!WriteBytes(temporary, staged_files_[index].bytes)) {
                    std::error_code cleanup_error;
                    std::filesystem::remove(temporary, cleanup_error);
                    error = "validation_failed";
                    ReleaseMutationLock();
                    return false;
                }
                std::string validation_error;
                const bool valid = post_validator_(staged_files_[index].relative_path,
                                                   temporary, validation_error);
                std::error_code cleanup_error;
                std::filesystem::remove(temporary, cleanup_error);
                if (!valid || cleanup_error) {
                    error = "validation_failed";
                    ReleaseMutationLock();
                    return false;
                }
            }
        }
        committed_ = true;
        ReleaseMutationLock();
        error.clear();
        return true;
    }
    if (!AcquireFilesystemLocks(error)) {
        ReleaseMutationLock();
        return false;
    }
    if (commit_guard_ && !commit_guard_(error)) {
        ReleaseFilesystemLocks();
        ReleaseMutationLock();
        return false;
    }
    if (!CreateBackups(error)) {
        ReleaseFilesystemLocks();
        ReleaseMutationLock();
        return false;
    }
    if (commit_guard_ && !commit_guard_(error)) {
        std::filesystem::path backup_root;
        std::string resolve_error;
        std::error_code cleanup_error;
        if (!backup_directory_.empty() &&
            scope_.ResolveRelative(backup_directory_, backup_root, resolve_error, true)) {
            std::filesystem::remove_all(backup_root, cleanup_error);
            backup_directory_.clear();
        }
        ReleaseFilesystemLocks();
        ReleaseMutationLock();
        return false;
    }

    for (std::size_t index = 0; index < staged_files_.size(); ++index) {
        if (!WriteAndReplace(staged_files_[index], index, error)) {
            const std::string write_error = error;
            std::string rollback_error;
            if (!Rollback(rollback_error)) error = rollback_error;
            else error = write_error;
            return false;
        }
    }

    if (post_validator_) {
        for (const auto& file : staged_files_) {
            std::string validation_error;
            if (!post_validator_(file.relative_path, file.target_path, validation_error)) {
                std::string rollback_error;
            if (!Rollback(rollback_error)) error = rollback_error;
            else error = "validation_failed";
            return false;
            }
        }
    }
    if (!options_.backup) {
        std::filesystem::path backup_root;
        if (!scope_.ResolveRelative(backup_directory_, backup_root, error, true)) {
            std::string rollback_error;
            Rollback(rollback_error);
            error = "backup_cleanup_failed";
            return false;
        }
        std::error_code remove_error;
        std::filesystem::remove_all(backup_root, remove_error);
        if (remove_error) {
            std::string rollback_error;
            if (!Rollback(rollback_error)) error = rollback_error;
            else error = "backup_cleanup_failed";
            return false;
        }
        backup_directory_.clear();
    }
    if (commit_observer_) commit_observer_();
    committed_ = true;
    ReleaseMutationLock();
    error.clear();
    return true;
}

bool Transaction::Rollback(std::string& error) {
    if (mutation_mutex_ && !mutation_lock_.owns_lock())
        mutation_lock_.lock();
    if (backup_directory_.empty()) {
        error = "rollback_unavailable";
        ReleaseMutationLock();
        return false;
    }
    if (!committing_ && rollback_guard_ && !rollback_guard_(error)) {
        ReleaseMutationLock();
        return false;
    }
    bool succeeded = true;
    for (std::size_t index = staged_files_.size(); index > 0; --index) {
        if (!Restore(staged_files_[index - 1], index, error)) succeeded = false;
    }
    if (!succeeded) {
        error = "rollback_failed";
        ReleaseMutationLock();
        return false;
    }
    committed_ = false;
    if (commit_observer_) commit_observer_();
    ReleaseMutationLock();
    error.clear();
    return true;
}

void Transaction::RemoveTemporaryFiles() noexcept {
    for (const auto& temporary : temporary_files_) {
        std::error_code error;
        std::filesystem::remove(temporary, error);
    }
    temporary_files_.clear();
}

void Transaction::ReleaseFilesystemLocks() noexcept {
#ifdef _WIN32
    for (const std::uintptr_t value : filesystem_locks_) {
        CloseHandle(reinterpret_cast<HANDLE>(value));
    }
#endif
    filesystem_locks_.clear();
}

void Transaction::ReleaseMutationLock() noexcept {
    if (mutation_lock_.owns_lock()) mutation_lock_.unlock();
}

}  // namespace mcp
