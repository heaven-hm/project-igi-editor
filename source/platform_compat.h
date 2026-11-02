/******************************************************************************
 * @file    platform_compat.h
 * @brief   The Win32 *vocabulary* the shared code uses, for non-Windows builds.
 *
 * This header supplies names, not behaviour.  Nothing here emulates a Win32
 * call: it defines the scalar types (`DWORD`, `HANDLE`, ...) and the virtual-key
 * constants (`VK_*`) that portable translation units mention, so those units can
 * compile on macOS.  A Win32 *function* is deliberately NOT declared -- code that
 * calls TerminateProcess() or CreateWindowExA() must be excluded per platform in
 * its own file, and the linker is the backstop that proves it was.
 *
 * Only included for __APPLE__ (see source/pch.h); the Windows build is byte-for-
 * byte unaffected.
 *****************************************************************************/

#pragma once

#if !defined(__APPLE__)
#error "platform_compat.h is the non-Windows compatibility layer; include it only on macOS"
#endif

#include <stddef.h>
#include <stdint.h>

/* --- Win32 scalar / opaque handle vocabulary ----------------------------- */
typedef int32_t BOOL;
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef uint8_t BYTE;
typedef int32_t LONG;
typedef int64_t LONG_PTR;
typedef uint32_t UINT;
typedef uint16_t USHORT;
typedef void *LPVOID;
typedef void *HANDLE;
typedef void *HWND;
typedef void *HINSTANCE;
typedef void *HMODULE;
typedef void *HICON;
typedef void *HBRUSH;
typedef void *HCURSOR;
typedef void *HGLOBAL;
typedef uintptr_t WPARAM;
typedef intptr_t LPARAM;
typedef intptr_t LRESULT;
typedef uint32_t COLORREF;
typedef uint32_t UINT32;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif

/* --- Windows virtual-key codes -------------------------------------------
 * The editor stores these numbers in its config as key identifiers, so the
 * values must match the Windows ones exactly or a config written on Windows
 * would bind different keys on macOS.  The translation from these VK codes to
 * macOS key codes lives in one place: Utils::IsKeyPressed() in source/utils.cpp.
 */
#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SELECT 0x29
#define VK_PRINT 0x2A
#define VK_EXECUTE 0x2B
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_HELP 0x2F
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE

#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
