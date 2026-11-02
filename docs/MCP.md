# MCP game-data integration

`igi_mcp.exe` is a headless Model Context Protocol server for Project IGI game
data. It is built as a Win32 target because the game data and editor runtime
are 32-bit.

The server is pinned to the stateless MCP profile `2026-07-28`. Requests must
carry the namespaced protocol metadata key
`io.modelcontextprotocol/protocolVersion` with that exact value. The server
does not create MCP sessions or emit `Mcp-Session-Id`.

## Start

The default transport is newline-delimited JSON-RPC over stdin/stdout:

```powershell
.\igi_mcp.exe --stdio --project D:\IGI1
```

For local integrations, loopback HTTP is available:

```powershell
.\igi_mcp.exe --http --project D:\IGI1 --host 127.0.0.1 --port 0
```

The endpoint and a cryptographically random bearer token are printed to
stderr. HTTP is restricted to `127.0.0.1`, `POST /mcp`, JSON request/response
content, an
`Authorization: Bearer ...` header, the `MCP-Protocol-Version: 2026-07-28`
header, the `Mcp-Method` and `Mcp-Name` headers, and the bounded request
body. Allowed origins are loopback only. Event-stream negotiation is not part
of this release; requests advertising `text/event-stream` are rejected.

## Game-data scope

The server exposes project and level manifests, object snapshots, validation,
and game-affecting tools for tasks, transforms, model IDs, AI fields, weapons,
mission objectives, navigation metadata, terrain metadata, lightmap metadata,
and level assets. Filesystem paths are resolved beneath the configured project
root and returned as root-relative paths.

Implemented persistent mutations use QSC span-preserving edits followed by
QSC parse and QVM compile validation. They honor `expected_revision`, support
`dry_run`, and use transaction backups and rollback validation.

Anonymous `Task_New(-1, ...)` records receive deterministic IDs. When a
mutation first targets one, the server stores its generated ID in a local QSC
comment marker so later field edits and source-line insertions do not retarget
the record. The marker is ignored by the game parser. `ai_set_weapon_loadout`
updates the listed weapon children while preserving their serialized QSC order;
the request order controls only the `before`/`after` arrays in the response.

The following boundaries are intentional and return the stable
`unsupported_operation` error without writing files:

- task structural create/delete/duplicate/reparent operations;
- `pickup_create_or_update` structural creation; the tool is update-only and
  requires an existing stable `task_id`;
- per-AI script-file replacement (script source validation and dry-run compile
  are available);
- graph node/link mutations;
- terrain edits and lightmap rebuild/clear;
- asset conversion and packing.

Client-visible save, backup, and restore commands are not part of this release.
Mutation backups and rollback are internal transaction safeguards; they are not
exposed as a restore API.

Graph, terrain, lightmap, and asset inspection currently report manifest-level
data; they do not claim to parse or rewrite binary graph or terrain payloads.
Object scale is also excluded because it is not a persisted game-data field in
the editor model.

Normal failures roll back the staged files and refresh the revision. A process
crash between replacing two companion files is not automatically repaired
across a later process start; callers should run `level_validate` before using
data after an interrupted process. A durable recovery journal is deferred until
it can be added without silently overwriting edits made while the editor was
offline.

## Tool groups

| Group | Tools |
| --- | --- |
| Session | `project_info`, `project_list_levels`, `level_open`, `level_reload`, `level_validate` |
| Objects/tasks | `task_list`, `task_get`, `task_create`, `task_update`, `task_delete`, `task_duplicate`, `task_reparent`, `object_set_transform`, `object_set_model`, `object_set_type`, `object_set_parameter`, `object_get_schema` |
| AI/weapons | `ai_get`, `ai_update`, `ai_validate_script`, `ai_compile_script`, `ai_list_weapons`, `ai_set_weapon_loadout`, `pickup_create_or_update` |
| AI/weapons (deferred) | `ai_set_script` |
| Mission | `mission_objective_list`, `mission_objective_update` |
| Graph/terrain | `graph_list`, `graph_get`, `graph_validate`, `terrain_get_metadata`, `terrain_validate`, `lightmap_get` |
| Graph/terrain (deferred) | `graph_node_create`, `graph_node_update`, `graph_node_delete`, `graph_link_create`, `graph_link_update`, `graph_link_delete`, `terrain_apply_edit`, `lightmap_rebuild_or_clear` |
| Assets | `asset_list`, `asset_inspect`, `asset_validate` |
| Assets (deferred) | `asset_convert`, `asset_pack_or_update` |

All tool schemas reject unknown properties. Tool failures are returned as
structured MCP tool errors with stable codes and redacted summaries; local
filesystem paths, command lines, provider payloads, and secrets are not
included in error responses.

The current registry publishes 46 tools.

## Verification

Build and run the focused MCP suites:

```powershell
cmake --build build --config Release --target igi_mcp igi_tests -- /m:1
$env:Path = (Join-Path (Get-Location) 'assets/dlls/x86') + ';' + $env:Path
.\bin\Release\igi_tests.exe --gtest_filter="Mcp*" --gtest_color=no
```

The HTTP integration test opens a real loopback socket and verifies stateless
discovery. The stdio smoke test should use a JSON-RPC request with the
`2026-07-28` metadata key.

The HTTP process remains alive until a control event or a line is supplied on
its standard input. Closing standard input alone does not signal shutdown.
