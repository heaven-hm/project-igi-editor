#include <gtest/gtest.h>

#include "mcp/game_data_service.h"
#include "mcp/mcp_transaction.h"
#include "support/temp_directory.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <atomic>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;

std::string ReadText(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::uint8_t> Bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

TEST(TempDirectoryTest, AllocatesDistinctOwnedDirectoriesAndCleansThem) {
    test_support::TempDirectory first;
    test_support::TempDirectory second;
    ASSERT_NE(first.path(), second.path());
    ASSERT_TRUE(fs::is_directory(first.path()));
    const fs::path first_path = first.path();
    std::ofstream(first_path / "owned.txt", std::ios::binary) << "owned";

    std::error_code error;
    ASSERT_TRUE(first.Cleanup(error)) << error.message();
    EXPECT_FALSE(fs::exists(first_path));
    EXPECT_TRUE(fs::is_directory(second.path()));
}

class McpTransactionTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = temporary_directory_.path();
        std::error_code error;
        fs::create_directories(root_ / "missions/location0/level1", error);
        ASSERT_FALSE(error) << error.message();
        target_ = root_ / "missions/location0/level1/objects.qvm";
        std::ofstream(target_, std::ios::binary) << "original";

        std::string open_error;
        scope_ = mcp::ProjectScope::Open(root_, open_error);
        ASSERT_TRUE(scope_.has_value()) << open_error;
    }

    void TearDown() override {
        std::error_code error;
        EXPECT_TRUE(temporary_directory_.Cleanup(error)) << error.message();
    }

    fs::path root_;
    test_support::TempDirectory temporary_directory_;
    fs::path target_;
    std::optional<mcp::ProjectScope> scope_;
};

TEST_F(McpTransactionTest, DryRunStagesWithoutWritingOrCreatingBackups) {
    mcp::MutationOptions options;
    options.dry_run = true;
    std::string error;
    mcp::Transaction transaction(*scope_, options);

    ASSERT_TRUE(transaction.Stage("missions/location0/level1/objects.qvm", Bytes("updated"), error))
        << error;
    ASSERT_TRUE(transaction.Commit(error)) << error;

    EXPECT_EQ(ReadText(target_), "original");
    EXPECT_TRUE(transaction.backup_directory().empty());
}

TEST_F(McpTransactionTest, DryRunRunsPostValidationWithoutChangingTarget) {
    mcp::MutationOptions options;
    options.dry_run = true;
    std::string error;
    bool post_validated = false;
    mcp::Transaction transaction(*scope_, options);
    ASSERT_TRUE(transaction.Stage("missions/location0/level1/objects.qvm", Bytes("updated"), error))
        << error;
    transaction.SetPostValidator([&](const fs::path&, const fs::path& path,
                                     std::string&) {
        post_validated = true;
        EXPECT_NE(path, target_);
        EXPECT_TRUE(fs::exists(path));
        return true;
    });

    ASSERT_TRUE(transaction.Commit(error)) << error;
    EXPECT_TRUE(post_validated);
    EXPECT_EQ(ReadText(target_), "original");
    EXPECT_TRUE(transaction.backup_directory().empty());
}

TEST_F(McpTransactionTest, BacksUpAndAtomicallyReplacesAStagedFile) {
    std::string error;
    mcp::Transaction transaction(*scope_, {});
    ASSERT_TRUE(transaction.Stage("missions/location0/level1/objects.qvm", Bytes("updated"), error))
        << error;
    ASSERT_TRUE(transaction.Commit(error)) << error;

    EXPECT_EQ(ReadText(target_), "updated");
    ASSERT_FALSE(transaction.backup_directory().empty());
    const fs::path backup = root_ / transaction.backup_directory() /
                            "missions/location0/level1/objects.qvm";
    EXPECT_EQ(ReadText(backup), "original");

    const fs::path parent = target_.parent_path();
    for (const auto& entry : fs::directory_iterator(parent)) {
        EXPECT_EQ(entry.path().extension(), ".qvm") << entry.path().filename().string();
    }
}

TEST_F(McpTransactionTest, RejectsInvalidStagedOutputWithoutChangingTarget) {
    std::string error;
    mcp::Transaction transaction(*scope_, {});
    ASSERT_TRUE(transaction.Stage("missions/location0/level1/objects.qvm", Bytes("invalid"), error))
        << error;
    transaction.SetValidator([](const fs::path&, const std::vector<std::uint8_t>&,
                                std::string& validator_error) {
        validator_error = "invalid binary data";
        return false;
    });

    EXPECT_FALSE(transaction.Commit(error));
    EXPECT_EQ(error, "validation_failed");
    EXPECT_EQ(ReadText(target_), "original");
    EXPECT_TRUE(transaction.backup_directory().empty());
}

TEST_F(McpTransactionTest, RollbackRestoresOriginalContentAndLeavesBackupRetained) {
    std::string error;
    mcp::Transaction transaction(*scope_, {});
    ASSERT_TRUE(transaction.Stage("missions/location0/level1/objects.qvm", Bytes("updated"), error))
        << error;
    ASSERT_TRUE(transaction.Commit(error)) << error;
    ASSERT_TRUE(transaction.Rollback(error)) << error;

    EXPECT_EQ(ReadText(target_), "original");
    EXPECT_TRUE(fs::exists(root_ / transaction.backup_directory() /
                           "missions/location0/level1/objects.qvm"));
}

TEST_F(McpTransactionTest, RollsBackWhenPostWriteValidationFails) {
    std::string error;
    mcp::Transaction transaction(*scope_, {});
    ASSERT_TRUE(transaction.Stage("missions/location0/level1/objects.qvm", Bytes("updated"), error))
        << error;
    transaction.SetPostValidator([](const fs::path&, const fs::path&, std::string& validation_error) {
        validation_error = "reparse failed";
        return false;
    });

    EXPECT_FALSE(transaction.Commit(error));
    EXPECT_EQ(error, "validation_failed");
    EXPECT_EQ(ReadText(target_), "original");
    EXPECT_FALSE(transaction.backup_directory().empty());
}

TEST_F(McpTransactionTest, PairValidationFailureRestoresBothOriginalFiles) {
    const fs::path second = target_.parent_path() / "objects.qsc";
    std::ofstream(second, std::ios::binary) << "original-source";

    std::string error;
    mcp::Transaction transaction(*scope_, {});
    ASSERT_TRUE(transaction.Stage("missions/location0/level1/objects.qvm", Bytes("new-binary"), error))
        << error;
    ASSERT_TRUE(transaction.Stage("missions/location0/level1/objects.qsc", Bytes("new-source"), error))
        << error;
    transaction.SetPostValidator([](const fs::path& relative, const fs::path&, std::string& why) {
        if (relative.extension() == ".qsc") {
            why = "injected paired validation failure";
            return false;
        }
        return true;
    });

    EXPECT_FALSE(transaction.Commit(error));
    EXPECT_EQ(error, "validation_failed");
    EXPECT_EQ(ReadText(target_), "original");
    EXPECT_EQ(ReadText(second), "original-source");
}

TEST_F(McpTransactionTest, RejectsCaseInsensitiveBackupPath) {
    std::string error;
    mcp::Transaction transaction(*scope_, {});
    EXPECT_FALSE(transaction.Stage(".MCP-BACKUPS/escape.qvm", Bytes("bad"), error));
    EXPECT_EQ(error, "path_forbidden");
}

TEST_F(McpTransactionTest, RejectsNestedBackupPath) {
    std::string error;
    mcp::Transaction transaction(*scope_, {});
    EXPECT_FALSE(transaction.Stage("missions/location0/level1/.mcp-backups/escape.qvm",
                                   Bytes("bad"), error));
    EXPECT_EQ(error, "path_forbidden");
}

TEST_F(McpTransactionTest, RejectsWin32NormalizedBackupPathVariants) {
    std::string error;
    mcp::Transaction transaction(*scope_, {});
    EXPECT_FALSE(transaction.Stage(".mcp-backups./escape.qvm", Bytes("bad"), error));
    EXPECT_EQ(error, "path_forbidden");
    EXPECT_FALSE(transaction.Stage("missions/location0/level1/.mcp-backups. /escape.qvm",
                                   Bytes("bad"), error));
    EXPECT_EQ(error, "path_forbidden");

    fs::path resolved;
    EXPECT_FALSE(scope_->ResolveRelative("missions/location0/level1/.mcp-backups./escape.qvm",
                                         resolved, error));
    EXPECT_EQ(error, "path_forbidden");
}

TEST_F(McpTransactionTest, RejectsCaseInsensitiveDuplicateTargets) {
    std::string error;
    mcp::Transaction transaction(*scope_, {});
    ASSERT_TRUE(transaction.Stage("missions/location0/level1/new.qvm", Bytes("one"), error))
        << error;
    EXPECT_FALSE(transaction.Stage("MISSIONS/LOCATION0/LEVEL1/NEW.QVM", Bytes("two"), error));
    EXPECT_EQ(error, "duplicate_stage_path");
}

TEST_F(McpTransactionTest, BackupFalseNotifiesObserverOnce) {
    mcp::MutationOptions options;
    options.backup = false;
    std::string error;
    int observer_calls = 0;
    mcp::Transaction transaction(*scope_, options);
    ASSERT_TRUE(transaction.Stage("missions/location0/level1/objects.qvm", Bytes("updated"), error))
        << error;
    transaction.SetCommitObserver([&] { ++observer_calls; });
    ASSERT_TRUE(transaction.Commit(error)) << error;
    EXPECT_EQ(observer_calls, 1);
    EXPECT_TRUE(transaction.backup_directory().empty());
    EXPECT_EQ(ReadText(target_), "updated");
}

TEST_F(McpTransactionTest, RollbackReacquiresMutationLockForCallbacks) {
    auto mutation_mutex = std::make_shared<std::mutex>();
    std::unique_lock<std::mutex> mutation_lock(*mutation_mutex);
    std::string error;
    mcp::Transaction transaction(*scope_, {}, {}, mutation_mutex, std::move(mutation_lock));
    ASSERT_TRUE(transaction.Stage("missions/location0/level1/objects.qvm", Bytes("updated"), error))
        << error;

    const auto expect_lock_held = [&] {
        std::atomic<bool> acquired{false};
        std::thread probe([&] {
            if (mutation_mutex->try_lock()) {
                acquired.store(true);
                mutation_mutex->unlock();
            }
        });
        probe.join();
        EXPECT_FALSE(acquired.load());
    };
    transaction.SetCommitObserver(expect_lock_held);
    transaction.SetRollbackGuard([&](std::string&) {
        expect_lock_held();
        return true;
    });

    ASSERT_TRUE(transaction.Commit(error)) << error;
    ASSERT_TRUE(transaction.Rollback(error)) << error;
    EXPECT_EQ(ReadText(target_), "original");
}

}  // namespace
