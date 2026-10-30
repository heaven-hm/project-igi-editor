/******************************************************************************
 * @file    utils_posix.cpp
 * @brief   macOS implementations of the Utils platform operations.
 *
 * Every function here is one that source/utils.cpp implements with a Win32
 * call.  The Windows definitions are compiled only on Windows (see the guards
 * in utils.cpp), so exactly one definition of each exists per platform.
 *
 * Fidelity notes -- what is equivalent and what is not:
 *
 *   keyboard     CGEventSourceKeyState() is the POSIX analogue of
 *                GetAsyncKeyState(): it reports the *current* physical state of
 *                a key without installing a hook and without Accessibility
 *                permission.  Windows virtual-key codes are mapped to macOS
 *                key codes, so a qedconfig.qsc written on Windows binds the
 *                same physical keys here.
 *   clipboard    pbcopy/pbpaste.  Equivalent for plain text, no NSPasteboard
 *                binding needed.  A missing tool yields "" (Get) or a no-op
 *                (Set) rather than crashing.
 *   dialogs      There is no modal Win32 dialog.  Messages go to the log and
 *                stderr; set IGI_MACOS_DIALOGS=1 to additionally raise a
 *                system alert via osascript.  PromptForText has no native
 *                replacement yet and returns nullopt -- the editor's in-window
 *                text fields cover editing, but the "search model by id/name"
 *                prompts are unavailable.
 *   elevation    geteuid()==0.
 *****************************************************************************/

#include "pch.h"
#include "utils.h"
#include "logger.h"

#if !defined(_WIN32)

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include <string>
#include <vector>

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>

namespace Utils {

// ── dialogs ────────────────────────────────────────────────────────────────
static void MacAlert(const char* kind, const std::string& message,
                     const std::string& title) {
    Logger::Get().LogAlways(kind == std::string("ERROR") ? LogLevel::ERR
                                                         : LogLevel::INFO,
                            "[" + title + "] " + message);
    std::fprintf(stderr, "[%s] %s: %s\n", kind, title.c_str(), message.c_str());
    std::fflush(stderr);

    const char* enable = std::getenv("IGI_MACOS_DIALOGS");
    if (!enable || std::string(enable) != "1") {
        return;
    }
    // Escape for AppleScript's double-quoted string literal.
    std::string escaped;
    escaped.reserve(message.size());
    for (char c : message) {
        if (c == '"' || c == '\\') escaped.push_back('\\');
        if (c == '\n') { escaped += "\\n"; continue; }
        escaped.push_back(c);
    }
    std::string cmd = "osascript -e 'display alert \"" + title +
                      "\" message \"" + escaped + "\"' >/dev/null 2>&1";
    if (FILE* p = popen(cmd.c_str(), "r")) {
        pclose(p);
    }
}

void ShowError(const std::string& message, const std::string& title) {
    MacAlert("ERROR", message, title);
}
void ShowWarning(const std::string& message, const std::string& title) {
    MacAlert("WARNING", message, title);
}
void ShowInfo(const std::string& message, const std::string& title) {
    MacAlert("INFO", message, title);
}

std::optional<std::string> PromptForText(const std::string& title,
                                         const std::string& label,
                                         const std::string& initial) {
    (void)initial;
    Logger::Get().Log(LogLevel::WARNING,
                      "[Utils] PromptForText is not available on macOS: " + title +
                          " (" + label + ")");
    return std::nullopt;
}

bool IsElevatedProcess() { return geteuid() == 0; }

std::string GetLastErrorAsString() {
    const int e = errno;
    return std::string(strerror(e)) + " (errno " + std::to_string(e) + ")";
}

// ── keyboard ───────────────────────────────────────────────────────────────
// Windows virtual-key code -> macOS virtual key code.  Letters and digits are
// not contiguous in Carbon's kVK_* values, so they are tables rather than
// arithmetic.
static int VkToMacKeyCode(int vk) {
    // 'A'..'Z'
    static const int kLetter[26] = {
        kVK_ANSI_A, kVK_ANSI_B, kVK_ANSI_C, kVK_ANSI_D, kVK_ANSI_E,
        kVK_ANSI_F, kVK_ANSI_G, kVK_ANSI_H, kVK_ANSI_I, kVK_ANSI_J,
        kVK_ANSI_K, kVK_ANSI_L, kVK_ANSI_M, kVK_ANSI_N, kVK_ANSI_O,
        kVK_ANSI_P, kVK_ANSI_Q, kVK_ANSI_R, kVK_ANSI_S, kVK_ANSI_T,
        kVK_ANSI_U, kVK_ANSI_V, kVK_ANSI_W, kVK_ANSI_X, kVK_ANSI_Y,
        kVK_ANSI_Z};
    // '0'..'9'
    static const int kDigit[10] = {
        kVK_ANSI_0, kVK_ANSI_1, kVK_ANSI_2, kVK_ANSI_3, kVK_ANSI_4,
        kVK_ANSI_5, kVK_ANSI_6, kVK_ANSI_7, kVK_ANSI_8, kVK_ANSI_9};
    // VK_F1..VK_F12 -> kVK_F1..kVK_F12 (contiguous in Carbon).
    static const int kFunction[12] = {
        kVK_F1, kVK_F2, kVK_F3,  kVK_F4,  kVK_F5,  kVK_F6,
        kVK_F7, kVK_F8, kVK_F9,  kVK_F10, kVK_F11, kVK_F12};

    if (vk >= 'A' && vk <= 'Z') return kLetter[vk - 'A'];
    if (vk >= 'a' && vk <= 'z') return kLetter[vk - 'a'];
    if (vk >= '0' && vk <= '9') return kDigit[vk - '0'];
    if (vk >= VK_F1 && vk <= VK_F12) return kFunction[vk - VK_F1];

    switch (vk) {
    case VK_LBUTTON: return kVK_ANSI_Keypad0;  // no mouse equivalent via CGEventSourceKeyState
    case VK_SHIFT:   return kVK_Shift;
    case VK_CONTROL: return kVK_Control;
    case VK_MENU:    return kVK_Option;
    case VK_SPACE:   return kVK_Space;
    case VK_PRIOR:   return kVK_PageUp;
    case VK_NEXT:    return kVK_PageDown;
    case VK_HOME:    return kVK_Home;
    case VK_END:     return kVK_End;
    case VK_LEFT:    return kVK_LeftArrow;
    case VK_RIGHT:   return kVK_RightArrow;
    case VK_UP:      return kVK_UpArrow;
    case VK_DOWN:    return kVK_DownArrow;
    case VK_INSERT:  return kVK_Help;
    case VK_DELETE:  return kVK_ForwardDelete;
    case VK_BACK:    return kVK_Delete;
    case VK_RETURN:  return kVK_Return;
    case VK_TAB:     return kVK_Tab;
    case VK_ESCAPE:  return kVK_Escape;
    case VK_MULTIPLY:return kVK_ANSI_KeypadMultiply;
    case VK_DIVIDE:  return kVK_ANSI_KeypadDivide;
    case VK_DECIMAL: return kVK_ANSI_KeypadDecimal;
    case VK_OEM_PLUS:  return kVK_ANSI_Equal;
    case VK_OEM_MINUS: return kVK_ANSI_Minus;
    default: return -1;
    }
}

static bool IsVkDown(int vk) {
    const int macKey = VkToMacKeyCode(vk);
    if (macKey < 0) return false;
    return CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState,
                                 static_cast<CGKeyCode>(macKey));
}

bool HotKeysDown(const std::vector<int>& keys) {
    if (keys.empty()) return false;
    for (int key : keys) {
        if (!IsVkDown(key)) return false;
    }
    return true;
}

bool IsKeyPressed(int keycode) { return IsVkDown(keycode); }

bool IsKeyToggled(int keycode) {
    // GetAsyncKeyState's low bit is the toggle state.  macOS has no equivalent
    // for an arbitrary key without tracking edges; report "not toggled" rather
    // than invent a state.  No caller relies on it today.
    (void)keycode;
    return false;
}

bool IsKeyBindingPressed(const KeyBinding& kb) {
    std::vector<int> keys;
    if (kb.ctrl) keys.push_back(VK_CONTROL);
    if (kb.shift) keys.push_back(VK_SHIFT);
    if (kb.alt) keys.push_back(VK_MENU);
    if (kb.vkCode) keys.push_back(kb.vkCode);
    return HotKeysDown(keys);
}

bool IsKeyBindingPressedExact(const KeyBinding& kb) {
    if (!kb.vkCode && !kb.ctrl && !kb.shift && !kb.alt) return false;
    if (kb.vkCode && !IsVkDown(kb.vkCode)) return false;
    return ModifiersExactMatch(kb, IsVkDown(VK_CONTROL), IsVkDown(VK_SHIFT),
                               IsVkDown(VK_MENU));
}

// ── clipboard ──────────────────────────────────────────────────────────────
void SetClipboardText(const std::string& text) {
    FILE* p = popen("pbcopy", "w");
    if (!p) return;
    if (!text.empty()) {
        std::fwrite(text.data(), 1, text.size(), p);
    }
    pclose(p);
}

std::string GetClipboardText() {
    FILE* p = popen("pbpaste", "r");
    if (!p) return "";
    std::string out;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), p)) > 0) {
        out.append(buf, n);
    }
    pclose(p);
    return out;
}

} // namespace Utils

#endif // !_WIN32
