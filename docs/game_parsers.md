# IGI Editor — Parsers Reference

> **Repo:** [jones-hm/project-igi1ed](https://github.com/jones-hm/project-igi1ed)  
> **Architecture (3.4.0-pre+):** All file conversion goes through the bundled `igi1conv.exe` (v1.7.0, located at `editor/tools/igi1conv/`). The editor's only in-process parsers are the few that feed runtime data every frame (`mef_native`, `fnt_parser`, `qsc_lexer`/`qsc_parser`, `terrain_files`, and the qvm pipeline used by `verify_level_core` / `--run-tests`).  
> **Source path:** `source/utils_igi1conv.{h,cpp}` is the shared spawner. Per-consumer parsers live next to their primary consumer (`source/renderer/`, `source/level/`).

***

## Architecture (3.4.0-pre+)

The editor is split into three layers:

1. **Shared CLI runner** — `source/utils_igi1conv.{h,cpp}` exposes a `igi1conv::` namespace with a `Run()` helper plus high-level wrappers for every `igi1conv` subcommand (`ResList`, `ResExtract`, `ResAppend`, `DatExportJson`, `DatToMtp`, `MtpDumpJson`, `GraphExportJson`, `QvmDecompile`, `QscCompile`, `TexToPng`, `FntExportPng`, `MefExportObj`, `TerrainExportLmp`, `TerrainExportCtr`, …).
2. **Editor consumers** — call the wrappers in `igi1conv::` whenever they need to read or write a game asset file.
3. **In-process parsers (kept)** — only the ones that need data the CLI cannot supply every frame: `mef_native` (raw `ParsedGeometry` for GL upload), `fnt_parser` (per-glyph UV/advance for HUD draw), `qsc_lexer`/`qsc_parser` (AI script token/AST walk), `terrain_files` (LMP/CTR runtime loaders), and the qvm pipeline (`qvm_parser`/`qvm_compiler`/`qvm_decompiler`) used by `verify_level_core` and the gtest roundtrip suite.

The `source/parsers/` folder **no longer exists**. Its files have been relocated to `source/renderer/` or `source/level/` (or deleted if dead code).

***

## igi1conv Subcommands Used by the Editor

| Format | Subcommand | Editor wrapper |
|--------|-----------|----------------|
| **DAT** | `dat export <f.dat> [-o out.json] [--text]` | `igi1conv::DatExportJson` |
| **DAT → MTP** | `dat to-mtp <f.dat> -o <out.mtp>` | `igi1conv::DatToMtp` |
| **MTP** | `mtp dump <f.mtp> [-o out.json]`, `mtp info <f.mtp>` | `igi1conv::MtpDumpJson`, `igi1conv::MtpInfo` |
| **Graph** | `graph export <f.dat> -o <out.json>`, `graph info <f.dat>` | `igi1conv::GraphExportJson`, `igi1conv::GraphInfo` |
| **QSC** | `qsc compile <f.qsc> -o <f.qvm>`, `qsc validate <f.qsc>` | `igi1conv::QscCompile`, `igi1conv::QscValidate` |
| **QVM** | `qvm decompile <f.qvm> -o <f.qsc>`, `qvm info <f.qvm>`, `qvm disasm <f.qvm>` | `igi1conv::QvmDecompile`, `igi1conv::QvmInfo`, `igi1conv::QvmDisasm` |
| **RES** | `res list <f.res>`, `res extract <f.res> -o <dir>`, `res extract --file <name>`, `res append <src> <files...> -o <dst> [--prefix ...]`, `res pack <dir> <out.res>` | `igi1conv::ResList`, `igi1conv::ResExtract`, `igi1conv::ResExtractOne`, `igi1conv::ResAppend`, `igi1conv::ResPack` |
| **TEX** | `tex to-png <f> [-o out.png]`, `tex to-tga <f> [-o out.tga]`, `tex decode <f> -o <dir>`, `tex info <f>` | `igi1conv::TexToPng`, `igi1conv::TexToTga`, `igi1conv::TexDecode`, `igi1conv::TexInfo` |
| **FNT** | `fnt info <f.fnt>`, `fnt export <f.fnt> -o <out.png>` | `igi1conv::FntInfo`, `igi1conv::FntExportPng` |
| **MEF** | `mef export <f.mef> -o <out.obj> [--dat ... --texdir ...]`, `mef info <f.mef>` | `igi1conv::MefExportObj`, `igi1conv::MefInfo` |
| **Terrain** | `terrain export-lmp <f.lmp> -o <out.pgm>`, `terrain export-ctr <f.ctr> -o <out.json>`, `terrain info <f>` | `igi1conv::TerrainExportLmp`, `igi1conv::TerrainExportCtr`, `igi1conv::TerrainInfo` |

Every wrapper constructs the right `igi1conv <cmd> ...` invocation, captures exit code, returns the produced path / stdout text, and logs through `Logger::Get()`. The default timeout is 120 s; pass a different value to `Run()` for long-running conversions.

***

## In-Process Parsers Kept After the Migration

| Parser | Location | Why it stays |
|---|---|---|
| `mef_native.{h,cpp}` | `source/renderer/` | `ParseMefFile` returns the raw `ParsedGeometry` (vertices, normals, UVs, magic-vertex flags) the renderer uploads directly to GL buffers. `igi1conv mef export` only writes an OBJ to disk. |
| `fnt_parser.{h,cpp}` | `source/renderer/` | `FNT_Parse` returns per-glyph `FntGlyph` (UV/advance/width) used every frame by the HUD draw path. `igi1conv fnt export` only writes a PNG atlas. |
| `qsc_lexer.{h,cpp}` | `source/level/` | `qsc::Lex` / `qsc::Parse` produce the token stream + AST walked by `app_editor.cpp` to find tasks in AI scripts. `igi1conv qsc` has no `--lex-tokens` / `--ast-json` subcommand. |
| `qvm_parser.{h,cpp}` | `source/level/` | `QVM_Parse` produces `QVMFile` used by `verify_level_core`, the gtest roundtrip suite, and `cli_tests --run-tests`. `igi1conv qvm info` is used in the editor's runtime hot path; the in-process parse stays for the gtest surface. |
| `qvm_compiler.{h,cpp}` | `source/level/` | `qvm::CompileToFile` powers the gtest roundtrip test. The editor's runtime qsc→qvm conversion goes through `igi1conv::QscCompile`. |
| `qvm_decompiler.{h,cpp}` | `source/level/` | `QVM_Decompile` / `QVM_DecompileToString` are used by the gtest roundtrip test and `verify_level_core`. The editor's runtime qvm→qsc conversion goes through `igi1conv::QvmDecompile`. |
| `terrain_files.{h,cpp}` | `source/level/` | `LMP_Load` / `CTR_Load` populate runtime mesh/lightmap structs the renderer streams. These are game runtime loaders, not asset converters. |

Writer-only classes (read side replaced by `igi1conv`, write side kept in C++ because no CLI subcommand covers it):

| Writer | Location | Purpose |
|---|---|---|
| `dat_writer.{h,cpp}` | `source/renderer/` | `DAT_AddModel`, `DAT_WriteNative` — used by the ATTA add-model flow to keep the manifest-boundary structure. |
| `graph_writer.{h,cpp}` | `source/renderer/` | `GRAPH_Write`, `GRAPH_Save` — full CRUD serializer that preserves header + adjacency table. `igi1conv graph` has no `write`/`save` subcommand. |
| `mtp_writer.{h,cpp}` | `source/level/` | `MTP_AddModel`, `MTP_Generate` — byte-preserving chunk writer. `igi1conv mtp sync` is the closest substitute but does not cover the in-process add-model flow. |
| `tex_writer.{h,cpp}` | `source/renderer/` | `TEX_ExportTGA` — used by the in-editor smoke test (`scratch\cpp_tests\...`). |
| `res_writer.{h,cpp}` | `source/renderer/` | `RES_StreamAppend` / `RES_WriteEntries` — used by the ATTA res-pack flow. |
| `res_compiler.{h,cpp}` | `source/renderer/` | `RES_GenerateQSC`, `RES_Compile`, `RES_WriteEntries`, `RES_StreamAppend` — used by the gtest `test_res_stream_append` roundtrip. |

Deleted (dead code, no external consumers): `mef_parser` (ASCII MEF parser — never invoked), `mef_exporter` (dead helper), `mtp_tool` (old `mtp_decoder.exe` runner — already superseded by `igi1conv dat to-mtp`).

***

## Conversion Chain

```
.qsc  ──►  igi1conv qsc compile   ──►  .qvm
.qvm  ──►  igi1conv qvm decompile ──►  .qsc
.mef  ──►  igi1conv mef export    ──►  .obj / .tga
.tex  ──►  igi1conv tex to-png    ──►  .png / .tga
.res  ──►  igi1conv res extract   ──►  any files
.dat  ──►  igi1conv dat export    ──►  .json
.dat  ──►  igi1conv dat to-mtp    ──►  .mtp
.mtp  ──►  igi1conv mtp dump      ──►  .json
graph*.dat ──► igi1conv graph export ──► .json
.lmp  ──►  igi1conv terrain export-lmp ──►  .pgm
.ctr  ──►  igi1conv terrain export-ctr ──►  .json
.fnt  ──►  igi1conv fnt export    ──►  .png
```

***

## Per-Subcommand CLI Reference

Every conversion the editor performs is one of the following `igi1conv` invocations. The same call works from the command line for ad-hoc inspection.

### DAT
```bash
igi1conv dat info  <file.dat>
igi1conv dat export <file.dat> -o <out.json> [--text]
igi1conv dat to-mtp <file.dat> -o <out.mtp>
```

### MTP
```bash
igi1conv mtp info  <file.mtp>
igi1conv mtp dump  <file.mtp> -o <out.json>
igi1conv mtp to-dat <file.mtp> -o <out.dat>
igi1conv mtp repair <file.mtp>
igi1conv mtp sync <file.mtp> <file.dat>
```

### Graph
```bash
igi1conv graph info  <file.dat>
igi1conv graph export <file.dat> -o <out.json>
```

### QSC / QVM
```bash
igi1conv qsc compile   <file.qsc> -o <out.qvm>
igi1conv qsc validate  <file.qsc>
igi1conv qvm info      <file.qvm>
igi1conv qvm disasm    <file.qvm> [-o <out.txt>]
igi1conv qvm decompile <file.qvm> -o <out.qsc>
```

### RES
```bash
igi1conv res list    <file.res>
igi1conv res extract <file.res> -o <out_dir>
igi1conv res extract <file.res> --file <name> -o <out_dir>
igi1conv res pack    <dir> <out.res>
igi1conv res append  <input.res> <file1> [file2...] -o <out.res> [--prefix LOCAL:textures/]
igi1conv res compile <file.qsc>
```

### TEX / FNT
```bash
igi1conv tex info    <file.tex|.spr|.pic>
igi1conv tex decode  <file.tex|.spr|.pic> -o <out_dir>
igi1conv tex to-png  <file> -o <out.png> [--resize W H]
igi1conv tex to-tga  <file> -o <out.tga> [--resize W H]
igi1conv fnt info    <file.fnt>
igi1conv fnt export  <file.fnt> -o <out.png>
```

### MEF
```bash
igi1conv mef info   <file.mef>
igi1conv mef export <file.mef> -o <out.obj> [--dat <file.dat> --texdir <dir>]
igi1conv mef bundle <file.mef> -o <outdir> --dat <file.dat> --texdir <dir>
igi1conv mef dump   <file.mef> [-o <out.txt>]
```

### Terrain
```bash
igi1conv terrain info       <file.lmp|.ctr>
igi1conv terrain export-lmp <file.lmp> -o <out.pgm>
igi1conv terrain export-ctr <file.ctr> -o <out.json>
```

***

## Format Reference

| File Extension | Format Type | Conversion via |
|---------------|-------------|----------------|
| `.dat` | Text — model/texture map | `igi1conv dat export` |
| `.mef` | Binary — 3D mesh (raw geometry parsed in-process via `mef_native`) | `igi1conv mef export` for OBJ |
| `.qvm` | Binary bytecode — VM script | `igi1conv qvm decompile` / `qvm info` |
| `.qsc` | Text — script source | `igi1conv qsc compile` / `qsc validate` (lexer/parser stay in-process for AI script walk) |
| `.res` | Binary — ILFF resource archive | `igi1conv res list/extract/append/pack` |
| `.tex` / `.spr` / `.pic` | Binary — proprietary texture | `igi1conv tex to-png/to-tga/decode` (raw RGBA still parsed in-process via `tex_writer` for cursor paint) |
| `.mtp` | Binary — FORM model-texture package | `igi1conv mtp dump`; writes stay in `mtp_writer` |
| `.fnt` | Binary — ILFF bitmap font | `igi1conv fnt export`; per-glyph metrics stay in `fnt_parser` |
| `.lmp` | Binary — lightmap pictures | `igi1conv terrain export-lmp`; runtime load stays in `terrain_files` |
| `.ctr` | Binary — terrain quadtree | `igi1conv terrain export-ctr`; runtime load stays in `terrain_files` |
| `graph*.dat` | Binary — AI navigation graph | `igi1conv graph export`; in-place edits stay in `graph_writer` |
| `.obj` / `.mtl` | Text — standard 3D model | `igi1conv mef export` output |
| `.tga` / `.png` | Binary — standard image | `igi1conv tex` / `mef` output |
| `.json` | Text — structured data | `igi1conv dat/mtp/graph/terrain` output |
