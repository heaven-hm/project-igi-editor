#include <gtest/gtest.h>
#include "utils.h"
#include <string>
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// ============================================================
//  Verify Level — integration test
//
//  Launches igi1ed.exe --verify-level 1 from the exe directory.
//  Waits up to 15 seconds. Asserts exit code 0.
//  Requires the full game deployment (missions/ co-located with exe).
// ============================================================

TEST(VerifyLevelIntegration, Level1PassesVerification) {
    const std::string exeDir  = Utils::GetExeDirectory();
    const std::string exePath = exeDir + "\\igi1ed.exe";

    ASSERT_TRUE(std::filesystem::exists(exePath))
        << "igi1ed.exe not found at: " << exePath
        << "\nMake sure the editor is built and game files are co-located.";

    std::string cmdLine = "\"" + exePath + "\" --verify-level 1";
    std::vector<char> buf(cmdLine.begin(), cmdLine.end());
    buf.push_back('\0');

    STARTUPINFOA si = {};
    si.cb           = sizeof(si);
    si.dwFlags      = STARTF_USESHOWWINDOW;
    si.wShowWindow  = SW_SHOWMINNOACTIVE;

    PROCESS_INFORMATION pi = {};
    BOOL ok = CreateProcessA(
        nullptr, buf.data(),
        nullptr, nullptr, FALSE,
        CREATE_NEW_CONSOLE,
        nullptr, exeDir.c_str(),
        &si, &pi);

    ASSERT_TRUE(ok) << "CreateProcess failed, GetLastError=" << GetLastError();

    // The outer process runs --verify-level which internally launches the
    // GUI editor and kills it after 15 seconds, then parses the log.
    // Allow 35 seconds total: 15s inner editor budget + startup/log overhead.
    const DWORD kTimeoutMs = 35000;
    DWORD waitResult = WaitForSingleObject(pi.hProcess, kTimeoutMs);

    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 5000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        FAIL() << "igi1ed.exe --verify-level 1 timed out after 35 seconds.";
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    EXPECT_EQ(exitCode, 0u)
        << "Verify level 1 failed (exit code " << exitCode << ").\n"
        << "Check igi1ed.log in " << exeDir << " for details.";
}
