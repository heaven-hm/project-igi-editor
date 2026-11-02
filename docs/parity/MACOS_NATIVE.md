# The native macOS build (`igi1ed.app`)

The editor has always been a Win32 application. This document records the port to a
native Apple-Silicon macOS application, what is proven, and what is not.

    cmake -S platform/macos -B build-macos -DCMAKE_BUILD_TYPE=Release
    cmake --build build-macos --parallel
    bash platform/macos/make-bundle.sh --install "$IGI_GAME_PATH"

The result is `igi1ed.app/Contents/MacOS/igi1ed`, a Mach-O arm64 executable. It is
**not** Wine, not Rosetta, and not a Linux binary.

---

## 1. Feasibility findings (measured on this host, 2026-10-30)

| Question | Result |
|---|---|
| GUI toolkit | The editor is GLUT. Apple ships `GLUT.framework`; `main.cpp` uses 42 GLUT entry points, of which **5 are FreeGLUT-only** and absent from Apple's header (`glutLeaveMainLoop`, `glutMainLoopEvent`, `glutSetOption`, `glutCloseFunc`, `glutMouseWheelFunc`). |
| GL 4.1 reachable? | **Yes.** A probe with `GLUT_3_2_CORE_PROFILE` returned `GL_VERSION "4.1 Metal - 89.3"`, GLSL 4.10, `Apple M2`. The 2.1 ceiling recorded in `GL21_FALLBACK.md` is a **Wine** limit, not a macOS one. |
| Is the 4.1 tier usable natively? | **No, not yet.** The renderer contains ~128 immediate-mode calls (`glBegin`/`glEnd`), e.g. `source/app_ui.cpp` and `source/menu/menu_ui_draw.cpp`, which do not exist in a core profile. The build therefore asks for Apple's **legacy** profile and runs the proven **shaders/21** tier. |
| GLEW | Not present on macOS. `platform/macos/include/glew.h` is a shim over `<OpenGL/gl.h>`: it declares the eight core-named entry points Apple's header omits as GLEW-style **function pointers** and resolves them in `glewInit()` **only when the context advertises the version**. A 2.1 context leaves them NULL, which is what activates the existing `GL_APPLE_vertex_array_object` fallback. |
| FreeGLUT | Not present. `platform/macos/include/freeglut.h` + `src/glut_compat.cpp` provide the five functions and two constants above. |
| Window server | This host has an Aqua session; a GLUT window is created and stays up. |

## 2. Win32 inventory

Counts are symbol occurrences per file (including comments), produced by scanning
`source/**` for the Win32 API surface. 37 files, 275 occurrences; 33 files are in the
`igi-editor` target, the rest are MCP/test-only.

| file | total call sites | categories | in `igi-editor` target |
|---|---|---|---|
| `source/utils.cpp` | 65 | file I/O (7), path/module (6), clipboard (9), UI/dialog (29), process/thread (3), key state (5), other Win32 (6) | yes |
| `source/app_editor.cpp` | 29 | file I/O (3), UI/dialog (10), process/thread (12), other Win32 (4) | yes |
| `source/app.cpp` | 27 | file I/O (6), UI/dialog (16), process/thread (5) | yes |
| `source/utils_igi1conv.cpp` | 17 | file I/O (8), process/thread (6), other Win32 (3) | yes |
| `source/main.cpp` | 16 | path/module (2), UI/dialog (14) | yes |
| `source/debug_command_manager.cpp` | 12 | file I/O (11), path/module (1) | yes |
| `source/mcp/mcp_main.cpp` | 12 | file I/O (2), process/thread (10) | no — tests/MCP only |
| `source/mcp/mcp_tools_runtime.cpp` | 12 | file I/O (5), UI/dialog (3), process/thread (4) | no — tests/MCP only |
| `source/app_level.cpp` | 10 | audio (winmm/MCI) (10) | yes |
| `source/cli/verify_level.cpp` | 8 | file I/O (2), process/thread (5), other Win32 (1) | yes |
| `source/runtime/audio_system.cpp` | 8 | audio (winmm/MCI) (8) | yes |
| `source/renderer/renderer_objects_atta.cpp` | 7 | file I/O (2), path/module (1), process/thread (3), other Win32 (1) | yes |
| `source/app_input.cpp` | 6 | file I/O (3), process/thread (3) | yes |
| `source/common.cpp` | 6 | other Win32 (6) | yes |
| `source/app_input_keyboard.cpp` | 3 | key state (3) | yes |
| `source/app_internal.h` | 3 | process/thread (3) | yes |
| `source/config.cpp` | 3 | UI/dialog (3) | yes |
| `source/mcp/mcp_transaction.cpp` | 3 | file I/O (3) | no — tests/MCP only |
| `source/runtime/simulation_scheduler.cpp` | 3 | timing (3) | yes |
| `source/ai_script_host.cpp` | 2 | other Win32 (2) | yes |
| `source/level/qvm_interpreter.h` | 2 | other Win32 (2) | yes |
| `source/platform_compat.h` | 2 | UI/dialog (1), process/thread (1) | yes |
| `source/runtime/gameplay_window.cpp` | 2 | UI/dialog (2) | yes |
| `source/runtime/runtime_world.cpp` | 2 | other Win32 (2) | yes |
| `source/utils.h` | 2 | path/module (2) | yes |
| `source/utils_posix.cpp` | 2 | key state (2) | yes |
| `source/ai_script_host.h` | 1 | other Win32 (1) | yes |
| `source/game_clock.h` | 1 | timing (1) | yes |
| `source/level/task_tree.h` | 1 | other Win32 (1) | yes |
| `source/level/terrain_internal.h` | 1 | path/module (1) | yes |
| `source/mcp/game_data_service.cpp` | 1 | other Win32 (1) | no — tests/MCP only |
| `source/renderer/renderer_draw.cpp` | 1 | UI/dialog (1) | yes |
| `source/renderer/wic_loader.cpp` | 1 | other Win32 (1) | yes |
| `source/runtime/audio_asset_resolver.cpp` | 1 | audio (winmm/MCI) (1) | yes |
| `source/runtime/igi_sound_bank.h` | 1 | audio (winmm/MCI) (1) | yes |
| `source/runtime/lightmap_rules.h` | 1 | other Win32 (1) | yes |
| `source/runtime/viewport_size_policy.h` | 1 | UI/dialog (1) | yes |

TOTAL files with Win32 calls: 37; call sites: 275; in editor target: 33

### What the seam did about each class

| class | files converted | macOS implementation |
|---|---|---|
| file I/O (stdio) | `common.cpp` (`File_Open`, `File_Exists`) | `Utils::ToNativePath()` converts `\\` to `/` at the last responsible moment; covers every `File_Load*` caller. |
| file I/O (streams) | `fnt_parser.cpp`, `qvm_parser.cpp`, `res_writer.cpp`, `level.cpp`, `level_objects.cpp`, `app_lookup.cpp` | path normalised before `std::ifstream` / `std::filesystem`. |
| path/module | `utils.h` (`GetExeDirectory`), `level.cpp`, `terrain_internal.h` | `_NSGetExecutablePath`; inside an `.app` it resolves `Contents/MacOS` -> `Contents/Resources`. The duplicated Win32 copies were replaced by this one implementation. |
| clipboard | `utils.cpp` -> `utils_posix.cpp` | `pbcopy` / `pbpaste`. Plain text only, like `CF_TEXT`. |
| key state | `utils.cpp` -> `utils_posix.cpp`; `app_input_keyboard.cpp` | `CGEventSourceKeyState` with a VK -> Carbon keycode table. `GetAsyncKeyState` call sites now call `Utils::IsKeyPressed`, which is the same function on both platforms. |
| UI/dialog | `utils.cpp` | No modal Win32 dialog. Messages go to the log and stderr; `IGI_MACOS_DIALOGS=1` raises a system alert instead. `PromptForText` returns `nullopt` (no native replacement yet). |
| timing | — | already portable (`std::chrono`) |
| process/thread | `app_internal.h`, `app.cpp`, `app_editor.cpp`, `app_input.cpp`, `utils_igi1conv.cpp`, `renderer_objects_atta.cpp`, `verify_level.cpp` | **Not portable.** Launching `igi.exe`, the process monitor thread, the global hotkey and the `igi1conv.exe` runner are compiled out and report that they are Windows-only. |
| registry | — | none in the editor target |
| audio | `app_level.cpp`, `audio_system.cpp`, `app_level.cpp` include | Level music used MCI (`mciSendStringA`); `PlayLevelMusic` now reports that music is unavailable and returns false. |
| GDI / WIC | `renderer/wic_loader.cpp` | Excluded from the macOS target: `WIC_LoadImage()` has no call sites in the tree. |
| process memory (`MEMORYSTATUSEX`) | `app_editor.cpp` | 32-bit address-space guard; meaningless in a 64-bit arm64 process, so compiled only on Windows. |

### Bugs the port exposed in shared code

* `source/common.cpp` `Mem_Alloc` used `aligned_alloc(align, sz)` with an unrounded
  size. C11 requires the size to be a multiple of the alignment; Apple's libc returns
  NULL, which surfaced as `[ERROR] memory overflow` and killed start-up. Fixed by
  rounding up (Windows takes the `_aligned_malloc` branch and is unchanged).
* `platform/macos` must not mix C and C++ sources: CMake then compiles the C++ PCH with
  the C compiler (`-x c-header`), which cannot parse glm/STL. The GLUT shim is C++.

## 3. Status

Proven:

* builds and links as arm64 (`file` reports `Mach-O 64-bit executable arm64`);
* launches a real Cocoa/GLUT window and stays running;
* reports `GL_VERSION 2.1 Metal - 89.3` and selects the `shaders/21` tier;
* loads level 1: terrain, `level1.res`, `.fnt` fonts and `IGIModels.json` all resolve;
* the 1280x720 centre of the screen during a run contains ~19k distinct colours with
  high variance (rendered content, not an empty window);
* the bundle launches with **no environment variables**, using
  `Contents/Resources/igi-game-path.txt` to find the install.

Not proven / known gaps:

* Model resolution logs `Model NOT FOUND` for ids such as `003_01_1`, `001_01_1`,
  `319_02_1`. The `.res` archives parse, so this is a remaining path/asset-resolution
  defect, not a missing game tree.
* Mouse wheel: Apple's GLUT delivers no wheel events, so `glutMouseWheelFunc` is stored
  and never called.
* `glutLeaveMainLoop` terminates the process after running the registered close
  callback (`App::Shutdown`), because Apple's GLUT cannot return from `glutMainLoop`.
* Keyboard *toggle* state (`IsKeyToggled`) is always false; no caller depends on it.
* Not checked against the Windows build's behaviour pixel-for-pixel; the Windows gate is
  the regression evidence for the shared code.
