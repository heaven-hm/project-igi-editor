#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace test_support {

class TempDirectory {
public:
    TempDirectory() {
        static std::atomic<unsigned long long> sequence{0};
        const auto base = std::filesystem::temp_directory_path();
        for (unsigned attempt = 0; attempt < 128; ++attempt) {
            const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
            const auto candidate = base / ("igi-editor-test-" +
                std::to_string(stamp) + "-" +
                std::to_string(sequence.fetch_add(1)));
            std::error_code error;
            if (std::filesystem::create_directory(candidate, error)) {
                path_ = candidate;
                return;
            }
            if (error && error != std::errc::file_exists) {
                throw std::runtime_error(error.message());
            }
        }
        throw std::runtime_error("Cannot create an exclusive test directory");
    }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    const std::filesystem::path& path() const noexcept { return path_; }

    bool Cleanup(std::error_code& error) noexcept {
        if (path_.empty()) return true;
        std::filesystem::remove_all(path_, error);
        if (error) return false;
        path_.clear();
        return true;
    }

    ~TempDirectory() {
        std::error_code error;
        Cleanup(error);
    }

private:
    std::filesystem::path path_;
};

}  // namespace test_support
