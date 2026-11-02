#include <gtest/gtest.h>
#include "utils.h"
#include <string>
#include <sstream>
#include <vector>
#include <filesystem>
#include <cstdlib>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// ============================================================
//  Verify Level — parameterized integration test (levels 1-14)
//
//  By default runs all 14 levels.
//  Set IGI_TEST_LEVEL=N (or comma-separated "1,3,5") to restrict.
//
//  Each test launches igi1ed.exe --verify-level N, waits up to
//  35 seconds (15s inner editor timeout + startup overhead),
//  and asserts exit code 0.
// ============================================================

static std::vector<int> LevelsToTest() {
    const char* env = std::getenv("IGI_TEST_LEVEL");
    if (env && *env) {
        std::vector<int> out;
        std::string envStr(env);
        std::istringstream ss(envStr);
        std::string tok;
        while (std::getline(ss, tok, ',')) {
            try {
                int n = std::stoi(tok);
                if (n >= 1 && n <= 14) out.push_back(n);
            } catch (...) {}
        }
        if (!out.empty()) return out;
    }
    std::vector<int> all;
    for (int i = 1; i <= 14; i++) all.push_back(i);
    return all;
}

class VerifyLevelIntegration : public ::testing::TestWithParam<int> {};

TEST_P(VerifyLevelIntegration, LevelPassesVerification) {
    int level = GetParam();
    const std::string exeDir  = Utils::GetExeDirectory();
    const std::string exePath = exeDir + "\\igi1ed.exe";

    ASSERT_TRUE(std::filesystem::exists(exePath))
        << "igi1ed.exe not found at: " << exePath
        << "\nMake sure the editor is built and game files are co-located.";

    std::string cmdLine = "\"" + exePath + "\" --verify-level " + std::to_string(level);
    std::vector<char> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back('\0');

    STARTUPINFOA si = {};
    si.cb        = sizeof(si);
    si.dwFlags   = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_SHOWMINNOACTIVE;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(nullptr, buf.data(),
                             nullptr, nullptr, FALSE, CREATE_NEW_CONSOLE,
                             nullptr, exeDir.c_str(), &si, &pi);
    ASSERT_TRUE(ok) << "CreateProcess failed, GetLastError=" << GetLastError();

    // Allow 35s: 15s inner editor budget + startup/log-flush overhead.
    const DWORD kTimeoutMs = 35000;
    DWORD waitResult = WaitForSingleObject(pi.hProcess, kTimeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        FAIL() << "igi1ed.exe --verify-level " << level << " timed out after 35 seconds.";
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    EXPECT_EQ(exitCode, 0u)
        << "Verify level " << level << " failed (exit code " << exitCode << ").\n"
        << "Check the verifier output for details.";
}

INSTANTIATE_TEST_SUITE_P(
    AllLevels,
    VerifyLevelIntegration,
    ::testing::ValuesIn(LevelsToTest()),
    [](const ::testing::TestParamInfo<int>& info) {
        return "Level" + std::to_string(info.param);
    }
);
