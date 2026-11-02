# Editor End-to-End Testing Design

## Goal

Add a repeatable, data-driven test runner that exercises the real editor from the installed game directory, captures screenshots at every meaningful checkpoint, and reports failures with enough evidence to reproduce them. The runner must cover behavior that parser/unit tests cannot see: level rendering, weather, pause-menu interaction, persistence across restart, model import, and imported texture resolution.

## Scope and constraints

- The installed corpus and runtime root is `D:\IGI1`.
- The editor is launched visibly with `Win32_Process.Create`, with `--game-path D:\IGI1` and an explicit level.
- Scenarios are JSON data. The runner must not contain level-specific production logic or silently infer a test's intent from free-form text.
- Every scenario step produces a timestamped result. Screenshot steps save a PNG and the final run writes JSON and human-readable Markdown reports.
- Log assertions are bounded by the editor process start time or a captured byte offset, so stale log lines cannot make a test pass.
- Mutating scenarios require an explicit `-AllowGameDataMutation` switch and must declare their mutation in the manifest.
- The runner cleans up the editor it launched, but can preserve it for manual inspection with `-KeepEditorOpen`.
- Image checks use configurable regions and tolerant metrics, not exact pixel snapshots.

## Scenario vocabulary

The manifest contains `name`, `level`, `requiresMutation`, and an ordered `steps` array. Supported step types are:

- `launch_editor`
- `wait_for_window`
- `wait_for_log`
- `assert_process`
- `key`
- `click`
- `type_text`
- `wait`
- `screenshot`
- `assert_screenshot_region`
- `mark_log`
- `assert_log`
- `assert_file`
- `close_editor`

Each step has a unique `id`. Actions use explicit values such as a virtual key, screen coordinate, timeout, log pattern, or screenshot region. The runner validates the schema before launching anything.

## Evidence and failure behavior

The output directory contains one subdirectory per scenario, all screenshots, `run.json`, `run.md`, and a copy of the scenario manifest. A failed assertion captures a failure screenshot before cleanup. The report records the exact command line, process ID, session/health checks, log offsets, step timings, and failure text.

The initial corpus scenario set is intentionally small but representative:

1. Load Level 9 and assert the authored weather log plus a non-uniform viewport region.
2. Load Level 12 and assert the startup scene is visible before any picking click.
3. Open the pause menu and exercise logging enabled/disabled and severity changes, with screenshots and bounded log assertions.
4. Move an object, save, close, reopen, and assert the saved state through the editor log and screenshot.
5. Exercise the model-picker/import path for a manifest-provided model and assert both model registration and texture loading without `Texture NOT FOUND` errors.

The model/import scenario is data-driven because model IDs, coordinates, and expected log patterns belong in the manifest, not in runner code. Log assertions poll for a bounded timeout and texture checks target the imported model record instead of unrelated startup diagnostics.
