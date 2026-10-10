# Task: Goto Model & Screenshot Feature

## Objective
Implement a developer-mode debug command system that watches for commands (like `goto` and `capture-model`) from an external CLI via `commands.txt`. The editor will read these, move the camera, and take screenshots using bounding-box calculated distances.

## Phase 1: Debug Command Manager
1. Create `source/debug_command_manager.h` and `.cpp`.
2. Implement a background thread `std::thread watcher_thread_` that opens and reads `commands.txt` periodically (e.g., every 500ms).
3. The thread pushes parsed commands into a thread-safe `std::queue<DebugCommand>`.
4. It should only run when "Developer Mode" is enabled.

## Phase 2: App Integration & Developer Mode
1. In `app.h`, add `DebugCommandManager debug_cmd_mgr_;`. Add a `bool developer_mode_ = false;`.
2. In `app_input_keyboard.cpp` under F10 handling, toggle `developer_mode_`. When true, start the watcher thread. When false, stop it.
3. In `App::Update` (or similar main loop), call `debug_cmd_mgr_.Update()`.
4. `Update()` will process the queue on the main thread safely.

## Phase 3: Command Handlers (Goto & Screenshot)
1. Implement handling for `goto level=<N> model=<ID>`.
   - Ensure level is loaded.
   - Find model by ID in `level_.GetLevelObjects()`.
   - Update `viewer_.pos_` to `obj.pos` and orientation using `obj.rot`.
2. Implement screenshot capturing.
   - Using `glReadPixels(..., GL_RGB, GL_UNSIGNED_BYTE, data)`.
   - Save via `stb_image_write.h`.
   - Naming convention: `LevelXX_ModelYYYY_Front.png` etc.
   - Calculate camera distance based on bounding box (`obj.bbox` or similar).

## Phase 4: CLI Creation
1. Create `tools/debug_cli.py`.
2. Use `argparse` to allow commands like `python debug_cli.py goto --level 5 --model 1234` or `capture-model --level 5 --model 1234`.
3. The CLI writes this to `commands.txt`.

## Phase 5: Tests
1. Add C++ unit tests for `DebugCommandManager` in `tests/test_debug_command_manager.cpp`.
2. Add Python tests for `tools/debug_cli.py` in `tests/test_debug_cli.py` (or execute python `unittest`).
