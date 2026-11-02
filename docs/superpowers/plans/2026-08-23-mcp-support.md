# MCP Support Implementation Plan

> For agentic workers: REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Build and ship a Win32 MCP server that exposes validated game-affecting Project IGI Editor data operations over stdio and opt-in authenticated localhost JSON HTTP. Event-stream negotiation is deferred.

**Architecture:** A dependency-free strict JSON layer feeds a transport-neutral JSON-RPC/MCP server. A file-backed GameDataService owns project scope, level snapshots, typed mutations, revision checks, backups, atomic persistence, and validation; stdio and HTTP call the same service. The GUI remains out of network-thread state, and visual verification launches the existing editor against persisted output.

**Tech Stack:** C++20, CMake, MSVC Win32, GoogleTest, Winsock2 on Windows, existing QSC/QVM/graph/terrain/converter code.

**Spec:** docs/superpowers/specs/2026-08-23-mcp-support-design.md

## Global Constraints

- Build targets are Win32 because the editor and game are 32-bit.
- MCP stdio stdout contains only one valid newline-delimited JSON-RPC message per line; diagnostics go to stderr.
- The protocol profile is pinned to MCP 2026-07-28: no mandatory initialize/initialized handshake or session identifier; requests carry `MCP-Protocol-Version` and `_meta` metadata, and HTTP uses `Mcp-Method` plus `Mcp-Name`.
- HTTP is opt-in, binds only to 127.0.0.1, requires bearer authentication, and validates localhost Origin values.
- Only configured project-root game data is addressable; arbitrary filesystem, shell, network, and UI-preference operations are not exposed.
- Every mutation validates arguments and cross-references, supports dry-run and stale-revision rejection, creates a backup before writing, atomically replaces files, reparses output, and returns before/after data.
- Existing binary/parser/converter implementations remain the source of truth; MCP code does not duplicate format logic.
- Tests are written before production code and must be observed failing for the new behavior.
- Every task ends with focused tests and a small commit; the task reviewer must approve before the next task.

---

### Task 1: Strict JSON value and JSON-RPC contract

**Files:**
- Create: source/mcp/mcp_json.h
- Create: source/mcp/mcp_json.cpp
- Create: source/mcp/mcp_json_rpc.h
- Create: source/mcp/mcp_json_rpc.cpp
- Test: tests/test_mcp_json.cpp
- Test: tests/test_mcp_json_rpc.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- mcp::JsonValue owns null/bool/finite-number/string/array/object values and exposes typed accessors.
- mcp::JsonParse(std::string_view input, JsonValue& output, JsonError& error) parses one complete value.
- mcp::JsonStringify(const JsonValue& value) emits deterministic object-key order and escaped JSON.
- mcp::JsonRpcRequest ParseJsonRpcRequest(const JsonValue& value) validates jsonrpc, id, method, and object params.
- JsonValue MakeJsonRpcResult(const JsonValue& id, JsonValue result) and MakeJsonRpcError(...) build responses.

- [ ] Step 1: Add failing JSON tests. Cover scalar/array/object round trips, escapes, duplicate-key rejection, trailing-input rejection, depth/size limits, non-finite-number rejection, and deterministic serialization. Add stateless request metadata, invalid-request, invalid-params, and error-response assertions.
- [ ] Step 2: Run the focused tests and confirm the failure is from missing MCP types/functions.

~~~powershell
cmake --build build-mcp --config Release --target igi_tests -- /m:1
.\build-mcp\Release\igi_tests.exe --gtest_filter="McpJson*:McpJsonRpc*"
~~~

- [ ] Step 3: Implement the smallest bounded parser/writer and JSON-RPC constructors. Enforce a maximum message size of 8 MiB and maximum nesting depth of 64; use C++20 storage and reject duplicate object keys.
- [ ] Step 4: Re-run the focused tests and confirm all new tests pass.
- [ ] Step 5: Commit.

~~~powershell
git add source/mcp tests/test_mcp_json.cpp tests/test_mcp_json_rpc.cpp CMakeLists.txt
git commit -m "feat: add strict MCP JSON-RPC core"
~~~

### Task 2: Project scope, revisions, backups, and atomic transactions

**Files:**
- Create: source/mcp/game_data_service.h
- Create: source/mcp/game_data_service.cpp
- Create: source/mcp/mcp_transaction.h
- Create: source/mcp/mcp_transaction.cpp
- Test: tests/test_mcp_paths.cpp
- Test: tests/test_mcp_transaction.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- mcp::ProjectScope::Open(std::filesystem::path project_root, std::string& error) canonicalizes and validates the configured root.
- mcp::GameDataService::OpenLevel(int level, std::string& error) loads an immutable level snapshot and assigns a revision.
- mcp::GameDataService::CurrentRevision() const returns the revision/fingerprint pair.
- mcp::MutationOptions { bool dry_run; bool backup; optional expected_revision; } is shared by mutating calls.
- mcp::Transaction::Stage(path, bytes), Commit(), and Rollback() implement same-directory temporary writes and backup retention.

- [ ] Step 1: Write failing path and transaction tests. Test canonical project-root acceptance, traversal/absolute-outside rejection, invalid level rejection, stale revision rejection, dry-run no-write behavior, backup creation, rollback on validation failure, and atomic replacement.
- [ ] Step 2: Run `McpPaths*` and `McpTransaction*` and record the expected red failures.
- [ ] Step 3: Implement root allowlisting, revision fingerprints, backup naming, temporary writes, and rollback. Keep all returned paths relative to the configured root.
- [ ] Step 4: Run focused tests plus existing `UtilsTest*` and `Qsc*` regression filters.
- [ ] Step 5: Commit.

~~~powershell
git add source/mcp tests/test_mcp_paths.cpp tests/test_mcp_transaction.cpp CMakeLists.txt
git commit -m "feat: add MCP project scope and safe transactions"
~~~

### Task 3: Read-only game-data snapshots and session tools

**Files:**
- Modify: source/mcp/game_data_service.h
- Modify: source/mcp/game_data_service.cpp
- Create: source/mcp/mcp_tools_session.h
- Create: source/mcp/mcp_tools_session.cpp
- Test: tests/test_mcp_game_snapshot.cpp
- Test: tests/test_mcp_session_tools.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- JsonValue GameDataService::ProjectInfo() const.
- JsonValue GameDataService::ListLevels() const.
- JsonValue GameDataService::LevelManifest(int level) const.
- JsonValue GameDataService::ListObjects(int level) const.
- JsonValue GameDataService::GetObject(int level, std::string_view task_id) const.
- ToolDefinitionList SessionToolDefinitions() and CallSessionTool(...) expose project_info, project_list_levels, level_open, level_reload, and level_validate.

- [ ] Step 1: Add synthetic QSC fixtures and failing snapshot/tool tests. Assert stable task IDs, parent/child relationships, position/orientation/model/type fields, redacted relative paths, and unknown-id errors.
- [ ] Step 2: Run the new filters and confirm they fail because the service/tool functions are absent.
- [ ] Step 3: Load through existing qsc_lexer, qsc_parser, LevelObjects, TaskSchemaNS, and validation code. Serialize only game-affecting fields into snapshots; do not include editor UI state.
- [ ] Step 4: Run focused tests and the existing parser/unit suite.
- [ ] Step 5: Commit.

~~~powershell
git add source/mcp tests/test_mcp_game_snapshot.cpp tests/test_mcp_session_tools.cpp CMakeLists.txt
git commit -m "feat: expose MCP project and level snapshots"
~~~

### Task 4: Task, object, transform, model, and parameter mutations

**Files:**
- Create: source/mcp/mcp_tools_objects.h
- Create: source/mcp/mcp_tools_objects.cpp
- Modify: source/mcp/game_data_service.cpp
- Test: tests/test_mcp_object_tools.cpp
- Test: tests/test_mcp_schema_tools.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- CallObjectTool(GameDataService&, std::string_view name, const JsonValue& arguments) implements task_list, task_get, task_create, task_update, task_delete, task_duplicate, task_reparent, object_set_transform, object_set_model, object_set_type, object_set_parameter, and object_get_schema.
- Transform input is {position:[x,y,z], rotation_radians:[a,b,g]} with per-field optionality and finite/range validation; scale is explicitly rejected because it is editor-only render/snap state.
- Every object mutation resolves the target by stable task id, updates the existing LevelObject/QSC argument representation, records the changed serialized fields, and leaves unrelated objects unchanged.

- [ ] Step 1: Add failing tests for building movement/rotation, model-ID replacement, creation/deletion, parent-child compatibility, typed parameter ranges, schema lookup, dry-run, and stale revision conflicts.
- [ ] Step 2: Run the object/schema filters and confirm red failures.
- [ ] Step 3: Implement typed mutations using existing LevelObjects serialization and TaskSchemaNS; reject editor-only fields and unknown parameters.
- [ ] Step 4: Run focused filters, QSC round-trip tests, and byte-preservation assertions.
- [ ] Step 5: Commit.

~~~powershell
git add source/mcp tests/test_mcp_object_tools.cpp tests/test_mcp_schema_tools.cpp CMakeLists.txt
git commit -m "feat: expose MCP task and object editing"
~~~

### Task 5: AI, weapons, pickups, and mission tools

**Files:**
- Create: source/mcp/mcp_tools_ai.h
- Create: source/mcp/mcp_tools_ai.cpp
- Create: source/mcp/mcp_tools_mission.h
- Create: source/mcp/mcp_tools_mission.cpp
- Test: tests/test_mcp_ai_tools.cpp
- Test: tests/test_mcp_mission_tools.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- CallAiTool(...) implements ai_get, ai_update, ai_set_script, ai_validate_script, ai_compile_script, ai_list_weapons, ai_set_weapon_loadout, and pickup_create_or_update.
- CallMissionTool(...) implements mission_objective_list and mission_objective_update.
- Script operations accept text or a configured existing script identity, run QSC parse/compile/decompile validation, and never execute arbitrary commands.

- [ ] Step 1: Add failing tests for AI type/team/graph updates, valid/invalid scripts, weapon child/loadout changes, pickup enum/count updates, and mission objective serialization.
- [ ] Step 2: Run the focused filters and observe expected red failures.
- [ ] Step 3: Implement through existing QVM/QSC, model-resolution, task-schema, and mission-loader seams; compile to a temporary destination and commit only after validation.
- [ ] Step 4: Prove invalid script compilation preserves the original source/bytecode and valid changes round-trip.
- [ ] Step 5: Commit.

~~~powershell
git add source/mcp tests/test_mcp_ai_tools.cpp tests/test_mcp_mission_tools.cpp CMakeLists.txt
git commit -m "feat: expose MCP AI weapons and mission data"
~~~

### Task 6: Navigation graph, terrain, lightmap, and asset tools

**Files:**
- Create: source/mcp/mcp_tools_graph.h
- Create: source/mcp/mcp_tools_graph.cpp
- Create: source/mcp/mcp_tools_assets.h
- Create: source/mcp/mcp_tools_assets.cpp
- Test: tests/test_mcp_graph_tools.cpp
- Test: tests/test_mcp_terrain_asset_tools.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- CallGraphTool(...) implements graph listing, node/link create/update/delete, criteria/material/radius updates, and graph validation.
- CallTerrainAssetTool(...) implements terrain metadata/edit/validate, lightmap get/rebuild-or-clear, asset list/inspect/validate/convert/pack-or-update.
- Converter operations call existing igi1conv wrappers with fixed subcommands and validated input/output paths.

- [ ] Step 1: Add failing synthetic graph/terrain tests and converter-wrapper tests. Assert adjacency regeneration, bounds rejection, graph round-trip, no arbitrary command arguments, and unchanged output on converter failure.
- [ ] Step 2: Run the filters and confirm red failures.
- [ ] Step 3: Implement graph/terrain/asset adapters using existing graph_writer, terrain files, lightmap, and utils_igi1conv seams.
- [ ] Step 4: Run focused tests and existing graph/terrain/asset parser tests.
- [ ] Step 5: Commit.

~~~powershell
git add source/mcp tests/test_mcp_graph_tools.cpp tests/test_mcp_terrain_asset_tools.cpp CMakeLists.txt
git commit -m "feat: expose MCP graph terrain and asset operations"
~~~

### Task 7: MCP tool registry, resources, and stdio transport

**Files:**
- Create: source/mcp/mcp_server.h
- Create: source/mcp/mcp_server.cpp
- Create: source/mcp/mcp_transport_stdio.h
- Create: source/mcp/mcp_transport_stdio.cpp
- Create: source/mcp/main.cpp
- Test: tests/test_mcp_server.cpp
- Test: tests/test_mcp_stdio_integration.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- McpServer::Handle(const JsonValue& request) handles optional server/discover, tools/list, tools/call, resources/list, and resources/read using the stateless MCP 2026-07-28 contract.
- McpServer::ToolDefinitions() returns deterministic schemas for all tools from Tasks 3–6.
- StdioTransport::Run(McpServer&) reads one line, emits one JSON-RPC line, and logs only to stderr.
- igi_mcp --stdio --project <root> starts the service and rejects missing/invalid project roots before accepting requests.

- [ ] Step 1: Add failing registry/stdout-purity integration tests. Spawn the binary, send stateless MCP 2026-07-28 requests, assert tool/resource names and schemas, call project_info, and verify malformed requests return JSON-RPC errors without process termination.
- [ ] Step 2: Run the tests and observe missing-server/transport failures.
- [ ] Step 3: Implement registry dispatch, optional discovery, resource URIs, and the line-oriented stdio loop. Do not write banners or logs to stdout.
- [ ] Step 4: Run protocol tests with a real spawned executable and existing unit tests.
- [ ] Step 5: Commit.

~~~powershell
git add source/mcp tests/test_mcp_server.cpp tests/test_mcp_stdio_integration.cpp CMakeLists.txt
git commit -m "feat: add MCP server and stdio transport"
~~~

### Task 8: Opt-in authenticated localhost JSON HTTP

**Files:**
- Create: source/mcp/mcp_transport_http.h
- Create: source/mcp/mcp_transport_http.cpp
- Test: tests/test_mcp_http_integration.cpp
- Test: tests/test_mcp_http_security.cpp
- Modify: source/mcp/main.cpp
- Modify: CMakeLists.txt

**Interfaces:**
- HttpTransport::Start(const HttpOptions&, McpServer&, HttpEndpoint& endpoint, std::string& error) binds only to IPv4 loopback and returns an ephemeral port/token when requested.
- HttpTransport::HandlePost(...) accepts the MCP endpoint and returns bounded JSON responses with the required protocol headers; event-stream negotiation is explicitly deferred.
- HttpTransport::ValidateRequest(...) enforces bearer token, Origin, content type, message size, and method/path rules.

- [ ] Step 1: Add failing socket-level tests for valid discover/list/call, missing token, wrong token, invalid Origin, non-loopback bind, oversized body, malformed JSON, and stateless protocol-header behavior.
- [ ] Step 2: Run the HTTP filters and confirm red failures.
- [ ] Step 3: Implement bounded Winsock HTTP parsing with one /mcp endpoint, localhost binding, bearer token, Origin validation, `Mcp-Method`/`Mcp-Name` and `MCP-Protocol-Version` checks, JSON responses, and explicit rejection of event-stream requests.
- [ ] Step 4: Run HTTP tests against a real listening server; assert LISTENING, verify there is no session-state requirement, and make a real tools/call.
- [ ] Step 5: Commit.

~~~powershell
git add source/mcp tests/test_mcp_http_integration.cpp tests/test_mcp_http_security.cpp CMakeLists.txt
git commit -m "feat: add secure opt-in MCP HTTP transport"
~~~

### Task 9: Documentation, packaging, visual verification, and release gates

**Files:**
- Create: docs/MCP.md
- Create: tests/test_mcp_contract.cpp
- Create: tests/mcp_protocol_smoke.ps1
- Modify: README.md
- Modify: docs/CLI.md
- Modify: docs/TESTS.md
- Modify: CHANGELOGS.md
- Modify: CMakeLists.txt

- [ ] Step 1: Add the contract test that rejects missing required game-affecting tools and rejects editor-only settings/tools.
- [ ] Step 2: Run the contract test red against the incomplete registry, then complete documentation and packaging entries.
- [ ] Step 3: Add documented stdio/HTTP setup, complete tool schemas/examples, security model, backup/revision behavior, and troubleshooting evidence.
- [ ] Step 4: Build all Win32 Release targets.

~~~powershell
cmake -S . -B build-mcp -G "Visual Studio 17 2022" -A Win32 -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=D:/Code/project-igi-editor/build/_deps/googletest-src
cmake --build build-mcp --config Release --target igi_mcp igi_tests igi-editor -- /m:1
~~~

- [ ] Step 5: Run MCP unit/protocol/security tests, the complete existing igi_tests.exe suite with documented level bounds, and the PowerShell protocol smoke test.
- [ ] Step 6: Launch the built editor with an installed/fixture level, use MCP to move/rotate a visible building, change its model id, edit an AI weapon/script field, save, reload, and capture before/after screenshots. Compare screenshots and serialized output.
- [ ] Step 7: Run git diff --check, inspect the complete diff, run the security/path/secret scan, request final two-axis code review, and commit any required fixes in a separate small commit.
- [ ] Step 8: Commit documentation/release artifacts.

~~~powershell
git add docs/MCP.md tests/test_mcp_contract.cpp tests/mcp_protocol_smoke.ps1 README.md docs/CLI.md docs/TESTS.md CHANGELOGS.md CMakeLists.txt
git commit -m "docs: document and verify MCP support"
~~~
