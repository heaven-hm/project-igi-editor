#include <gtest/gtest.h>

#include "mcp/game_data_service.h"
#include "mcp/mcp_transaction.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace {

namespace fs = std::filesystem;

std::string ReadText(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

class McpPathsTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / "igi_mcp_paths_test";
        std::error_code error;
        fs::remove_all(root_, error);
        WriteLevel("location0", 1);
    }

    void WriteLevel(std::string_view location, int level) {
        std::error_code error;
        const fs::path level_directory =
            root_ / "missions" / location / ("level" + std::to_string(level));
        fs::create_directories(level_directory, error);
        ASSERT_FALSE(error) << error.message();
        std::ofstream(level_directory / "objects.qvm", std::ios::binary)
            << "fixture-qvm";
    }

    void TearDown() override {
        std::error_code error;
        fs::remove_all(root_, error);
    }

    fs::path root_;
};

TEST_F(McpPathsTest, OpensCanonicalProjectRootAndMapsRelativePaths) {
    std::string error;
    const auto scope = mcp::ProjectScope::Open(root_ / "missions/..", error);

    ASSERT_TRUE(scope.has_value()) << error;
    EXPECT_EQ(scope->root(), fs::canonical(root_));

    fs::path resolved;
    ASSERT_TRUE(scope->ResolveRelative("missions/location0/level1/objects.qvm", resolved, error))
        << error;
    EXPECT_EQ(resolved, fs::canonical(root_ / "missions/location0/level1/objects.qvm"));

    fs::path relative;
    ASSERT_TRUE(scope->RelativeToRoot(resolved, relative, error)) << error;
    EXPECT_EQ(relative.generic_string(), "missions/location0/level1/objects.qvm");
}

TEST_F(McpPathsTest, RejectsTraversalAndAbsoluteOutsidePathsWithoutLeakingRoot) {
    std::string error;
    const auto scope = mcp::ProjectScope::Open(root_, error);
    ASSERT_TRUE(scope.has_value()) << error;

    fs::path resolved;
    EXPECT_FALSE(scope->ResolveRelative("../outside.qvm", resolved, error));
    EXPECT_EQ(error, "path_forbidden");

    const fs::path outside = fs::temp_directory_path() / "igi_mcp_outside.qvm";
    EXPECT_FALSE(scope->ResolveRelative(outside, resolved, error));
    EXPECT_EQ(error, "path_forbidden");
    EXPECT_EQ(error.find(root_.string()), std::string::npos);
}

TEST_F(McpPathsTest, RejectsNonIgi1LocationDirectories) {
    WriteLevel("location1", 7);
    WriteLevel("location2", 1);
    WriteLevel("location2", 6);
    WriteLevel("location3", 1);
    WriteLevel("location3", 6);

    std::string error;
    const auto scope = mcp::ProjectScope::Open(root_, error);
    ASSERT_TRUE(scope.has_value()) << error;

    fs::path level_directory;
    for (const int level : {11, 21, 31}) {
        EXPECT_FALSE(scope->LevelDirectory(level, level_directory, error));
        EXPECT_EQ(error, "invalid_level");
    }
    EXPECT_TRUE(scope->IsSupportedPath("missions/location0/level1/objects.qsc"));
    EXPECT_FALSE(scope->IsSupportedPath("missions/common/objects.qsc"));
}

TEST_F(McpPathsTest, RejectsMissionTraversalEvenWhenItNormalizesToLocation0) {
    std::string error;
    const auto scope = mcp::ProjectScope::Open(root_, error);
    ASSERT_TRUE(scope.has_value()) << error;

    EXPECT_FALSE(scope->IsSupportedPath("missions/location1/../location0/level1/objects.qsc"));
}

TEST_F(McpPathsTest, AssignsStableRevisionAndRejectsStaleMutations) {
    std::string error;
    const auto scope = mcp::ProjectScope::Open(root_, error);
    ASSERT_TRUE(scope.has_value()) << error;
    mcp::GameDataService service(*scope);

    ASSERT_TRUE(service.OpenLevel(1, error)) << error;
    const mcp::LevelRevision revision = service.CurrentRevision();
    EXPECT_EQ(revision.level, 1);
    EXPECT_FALSE(revision.fingerprint.empty());

    mcp::MutationOptions stale_options;
    stale_options.expected_revision = "wrong-revision";
    EXPECT_EQ(service.BeginMutation(stale_options, error), nullptr);
    EXPECT_EQ(error, "stale_revision");

    mcp::MutationOptions current_options;
    current_options.expected_revision = revision.fingerprint;
    EXPECT_NE(service.BeginMutation(current_options, error), nullptr) << error;
}

TEST_F(McpPathsTest, ChangesRevisionWhenAnOpenedLevelChanges) {
    std::string error;
    const auto scope = mcp::ProjectScope::Open(root_, error);
    ASSERT_TRUE(scope.has_value()) << error;
    mcp::GameDataService service(*scope);
    ASSERT_TRUE(service.OpenLevel(1, error)) << error;
    const std::string original = service.CurrentRevision().fingerprint;

    std::ofstream(root_ / "missions/location0/level1/objects.qvm", std::ios::binary | std::ios::trunc)
        << "changed-fixture-qvm";

    ASSERT_TRUE(service.OpenLevel(1, error)) << error;
    EXPECT_NE(service.CurrentRevision().fingerprint, original);
}

TEST_F(McpPathsTest, UsesRetailLocationZeroLayoutWhenPresent) {
    WriteLevel("location0", 1);

    std::string error;
    const auto scope = mcp::ProjectScope::Open(root_, error);
    ASSERT_TRUE(scope.has_value()) << error;

    fs::path level_directory;
    ASSERT_TRUE(scope->LevelDirectory(1, level_directory, error)) << error;
    EXPECT_EQ(level_directory, fs::canonical(root_ / "missions/location0/level1"));
}

TEST_F(McpPathsTest, RejectsExternalChangesBeforeBeginningMutation) {
    std::string error;
    const auto scope = mcp::ProjectScope::Open(root_, error);
    ASSERT_TRUE(scope.has_value()) << error;
    mcp::GameDataService service(*scope);
    ASSERT_TRUE(service.OpenLevel(1, error)) << error;
    const std::string original = service.CurrentRevision().fingerprint;

    std::ofstream(root_ / "missions/location0/level1/objects.qvm", std::ios::binary | std::ios::trunc)
        << "changed-outside-mutation";

    mcp::MutationOptions options;
    options.expected_revision = original;
    EXPECT_EQ(service.BeginMutation(options, error), nullptr);
    EXPECT_EQ(error, "stale_revision");
    EXPECT_NE(service.CurrentRevision().fingerprint, original);
}

TEST_F(McpPathsTest, LimitsServiceMutationToTheOpenedLevelDirectory) {
    std::string error;
    const auto scope = mcp::ProjectScope::Open(root_, error);
    ASSERT_TRUE(scope.has_value()) << error;
    mcp::GameDataService service(*scope);
    ASSERT_TRUE(service.OpenLevel(1, error)) << error;
    auto transaction = service.BeginMutation({}, error);
    ASSERT_NE(transaction, nullptr) << error;

    EXPECT_FALSE(transaction->Stage("missions/location0/level2/objects.qvm", {}, error));
    EXPECT_EQ(error, "path_forbidden");
}

TEST_F(McpPathsTest, RejectsExternalChangesAtTransactionCommit) {
    std::string error;
    const auto scope = mcp::ProjectScope::Open(root_, error);
    ASSERT_TRUE(scope.has_value()) << error;
    mcp::GameDataService service(*scope);
    ASSERT_TRUE(service.OpenLevel(1, error)) << error;
    auto transaction = service.BeginMutation({}, error);
    ASSERT_NE(transaction, nullptr) << error;
    ASSERT_TRUE(transaction->Stage("missions/location0/level1/objects.qvm", {'u', 'p', 'd', 'a', 't', 'e', 'd'}, error))
        << error;

    std::ofstream(root_ / "missions/location0/level1/objects.qvm", std::ios::binary | std::ios::trunc)
        << "external-change";
    EXPECT_FALSE(transaction->Commit(error));
    EXPECT_EQ(error, "stale_revision");
    EXPECT_EQ(ReadText(root_ / "missions/location0/level1/objects.qvm"), "external-change");
}

TEST_F(McpPathsTest, SuccessfulServiceTransactionRefreshesRevisionAndReleasesLock) {
    std::string error;
    const auto scope = mcp::ProjectScope::Open(root_, error);
    ASSERT_TRUE(scope.has_value()) << error;
    mcp::GameDataService service(*scope);
    ASSERT_TRUE(service.OpenLevel(1, error)) << error;
    const std::string original = service.CurrentRevision().fingerprint;
    auto transaction = service.BeginMutation({}, error);
    ASSERT_NE(transaction, nullptr) << error;
    ASSERT_TRUE(transaction->Stage("missions/location0/level1/objects.qvm",
                                   {'u', 'p', 'd', 'a', 't', 'e', 'd'}, error)) << error;
    ASSERT_TRUE(transaction->Commit(error)) << error;

    EXPECT_NE(service.CurrentRevision().fingerprint, original);
}

TEST_F(McpPathsTest, RejectsReparsePointInsideOpenedLevel) {
    std::error_code link_error;
    const fs::path link = root_ / "missions/location0/level1/linked";
    fs::create_directory_symlink(root_ / "missions/location0/level1", link, link_error);
    if (link_error) GTEST_SKIP() << "directory symlinks unavailable: " << link_error.message();

    std::string error;
    const auto scope = mcp::ProjectScope::Open(root_, error);
    ASSERT_TRUE(scope.has_value()) << error;
    fs::path resolved;
    EXPECT_FALSE(scope->ResolveRelative("missions/location0/level1/linked/objects.qvm",
                                       resolved, error));
    EXPECT_EQ(error, "path_forbidden");
}

TEST_F(McpPathsTest, ExcludesTransactionBackupsFromLevelRevisionFingerprint) {
    std::string error;
    const auto scope = mcp::ProjectScope::Open(root_, error);
    ASSERT_TRUE(scope.has_value()) << error;
    mcp::GameDataService service(*scope);
    ASSERT_TRUE(service.OpenLevel(1, error)) << error;
    const std::string original = service.CurrentRevision().fingerprint;

    const fs::path backup_file =
        root_ / "missions/location0/level1/.mcp-backups/transaction-1/objects.qvm";
    fs::create_directories(backup_file.parent_path());
    std::ofstream(backup_file, std::ios::binary) << "backup-only-change";

    ASSERT_TRUE(service.OpenLevel(1, error)) << error;
    EXPECT_EQ(service.CurrentRevision().fingerprint, original);
}

}  // namespace
