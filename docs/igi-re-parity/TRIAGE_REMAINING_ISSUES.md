# Remaining open issues: triage against IGI-RE

Nine of the 32 open issues are defects or parity gaps and are fixed on
`fix/igi-re-known-issues` (#37, #40, #42, #44, #47, #48, #59, #62, #68 — see
`docs/ISSUE_FIXES.md` and `docs/igi-re-parity/`). The remaining 23 are feature
requests: each asks for a subsystem the editor does not have, so there is no
defect to locate and no single call site to correct.

This file records, for each of them, what the oracle actually offers. It is a
triage, not a claim of work done. The evidence rule for this pass was that
IGI-RE (the decompilation of retail `IGI.exe`) is the only source; where an issue
cites a name that does not exist in IGI-RE, that is stated rather than papered
over.

## Status: the "remaining 23" count above is stale

This triage was written before the porting work started. Landed since, on
`fix/igi-re-known-issues`:

| # | Issue | Landed by |
| --- | --- | --- |
| 67 | Magic-object table | `f65d3768` — attachments spawn the definition's model, recursively |
| 66 | Fog presets from FlatSky | `4d30ad3b`, `19d226a4` — the engine's own fog, *not* the issue's FlatSky premise |
| 74 | QVM in-game menu | `42a689bb`, `41d4d33a` (retail menu definition + `.RES` labels) |
| 62 | LOD chains | `c7bd9e2a` (lightmap ref index), `013b06bb` (authored distances), `aa225a6e` (loader wiring) — the per-frame switch remains |
| 43 | Create/edit lights and smoke FX | `aa225a6e` |
| 58 | Rain renderer audit | `aa225a6e` — audited and extracted; renderer wiring remains |
| 70 | Editor sound system | `aa225a6e` — rules ported; wiring remains |
| 75 | Weapon/ammo pickup parity | `aa225a6e` — rules ported; wiring remains |
| 68 | Ladder placement | `8551c6b1`, `140d152b` |
| 57 | Snow particles | Snow is **not** a separate system: it is the rain task's `snow` mode (`r->snow = rain == 0`, `Rain_New` `0x498960`, `src/dxinit/rain.c:153`). Ported into `source/runtime/rain_rules.*`; see `docs/igi-re-parity/SNOW.md` |
| 37, 40, 42, 44, 47, 48, 59 | defects / parity gaps | `8551c6b1` |
| 80 | Cutscenes | `273bb24f` — `source/runtime/cutscene_rules.*`, `docs/igi-re-parity/CUTSCENES.md` |
| 79, 69 | Roads / spline segment placement | `273bb24f` — `source/runtime/spline_rules.*`, `docs/igi-re-parity/ROAD_SPLINES.md` |
| 73 | Animation playback | `273bb24f` — `source/runtime/anim_rules.*`, `docs/igi-re-parity/ANIMATION.md` |
| 72 | Lightmap bake | `273bb24f` — `source/runtime/lightmap_rules.*`, `docs/igi-re-parity/LIGHTMAP_BAKE.md` |
| 60 | Helicopter rotor + flight preview | `273bb24f` — `source/runtime/heli_rules.*`, `docs/igi-re-parity/HELICOPTER.md` |

Still genuinely open after that, in rough order of tractability:

| # | Issue | State |
| --- | --- | --- |
| 29 | OBJ→MEF importer | In progress — native MEF writer + `--import-mef`, static models first |
| 63 | Light fixtures from emissive geometry + baked lightmaps | Editor design. I.G.I. declares no point lights, so there is no fixture symbol in IGI-RE to port; the issue's own premise is open-igi's `LightFixtureExtractor`. The oracle can only supply the lightmap bake side (#72) |
| 56, 54 | Gameplay umbrellas | Feature requests |
| 52 | Post-processing | **No retail counterpart** — IGI.exe has no post-process pass to mirror |
| 51 | 3-layer lightmap | Editor design, not a retail mode set |
| 50 | Split `renderer_draw.cpp` | Pure refactor, no behavioural oracle |
| 49 | Roadmap | Tracking issue |
| 7 | AI / waypoint editor | IGI-RE exposes no nav-mesh or waypoint-authoring symbols |

**Wiring remains** for several rows whose *rules* are now ported and tested but which are
not yet driven by the renderer or GUI: #80, #72, #60, #70, #58, and #62's per-frame LOD
switch. Each is recorded as "ported, unwired" in its own `docs/igi-re-parity/*.md`. Porting
a rule and calling it from a draw path are separate claims; the tests pin the former only.


## A caveat that applies to several rows

Some issue titles cite identifiers from **open-igi**, a separate C# reimplementation,
as though they were `IGI.exe` symbols. IGI-RE contains no `MefSkinner`, no `Snow`
renderer, no `Waypoint`-prefixed navigation symbol and no `LightFixtureExtractor`,
because those are open-igi class names. The retail behaviour exists, but under
different names, and finding it is part of the work. Verified absence in
`symbols/names.tsv`:

| name cited by an issue | in IGI-RE |
| --- | --- |
| `MefSkinner` | absent |
| `Snow` (any symbol) | absent |
| `LightFixtureExtractor` | absent |
| `Waypoint*` (navigation) | absent |

## Rows whose cited address does resolve

These were checked by address in `symbols/names.tsv`, so the oracle entry point is
known and the work is a port, not a search.

| # | Issue | Cited address | Resolves to |
| --- | --- | --- | --- |
| 75 | Weapon/ammo pickup parity | `0x46C630`, `0x46D230` | `GenericPickup_Touch`, `GunPickup_Touch` |
| 67 | Magic-object table | `0x4C4520`, `0x4DFA50` | `DefineMagicObj`, `Sector_CreateMagicObjs` |
| 60 | Helicopter rotor + flight preview | `0x431B70` | `Heli_Control` |
| 62 | LOD chains | `0x4CED50` | `VirModel_InitLods` (rule + loader wiring delivered; the per-frame switch is the remainder — see below) |
| 68 | Ladder placement | `0x4404C0`, `0x4113E0` | `Ladder_Use`, `HumanPlayer_UpdateLadder` (delivered) |

## Neighbouring entry points found for the rest

Where the issue cites no address, the nearest oracle symbols are recorded so the
next pass starts from a function rather than from a search.

| # | Issue | IGI-RE entry points |
| --- | --- | --- |
| 80 | Intro/outro cutscenes | `MenuManager_Init` `0x4170A0`, `MenuManager_Create` `0x417260`, `MenuManager_RequestScreen` `0x4174A0`; SCamera/SCameraLens are already parsed |
| 79 | Road/track spline objects | `SplineCache_*` `0x49A010`/`0x49A340`, `SplineMesh_GetVerts` `0x49A050`; #59's rail work is the same family |
| 74 | QVM in-game menu | the `MenuManager_*` block above, plus `menusystem/ingamemenu.qvm` as data |
| 73 | Animation playback + skinning | `GenericTBA_GetAnim` `0x4485D0`, `AIAction_PlayAnimation` `0x44D6E0`, `HumanPlayer_SetWeaponAnim` `0x4137E0`; the skinning side has no IGI-RE symbol by that name |
| 72 | Lightmap bake button | `Lightmap_*` `0x521040`+, `Lightmap_GetResPath` `0x5210E0`; baking is a tool feature, not a retail code path |
| 70 | Editor sound system | `ConditionalSound_*` `0x424E80`+, `AmbientArea_*` `0x44C130`+ |
| 69 | Hermite road/bridge segments | spline family above; the editor already has `spline_geometry.cpp` from #59 |
| 66 | Fog presets from FlatSky | `Fog_SetRange` `0x49B2D0`, `Fog_Compute` `0x4DA790` |
| 63 | Light fixtures from emissive geometry | `AlarmLight_*` `0x4397C0`+ is the closest retail light type; inference itself is new tooling |
| 58 | Rain renderer audit | `Rain_New` `0x498960`, `Rain_Init` `0x498AB0` |
| 57 | Snow particles | no IGI-RE symbol; rain (`Rain_*`) is the only weather particle system in the oracle |
| 56 | Vanilla gameplay parity | umbrella issue; not decomposable into one change |
| 54 | Windows runtime vertical slice | umbrella issue; needs a Windows host |
| 52 | Post-processing/HDR/motion blur | no IGI-RE counterpart (retail has no such pipeline) — engine feature, not parity |
| 51 | 3-layer lightmap pipeline | `Lightmap_*`; the three modes are an editor design, not a retail mode set |
| 50 | Split `renderer_draw.cpp` | pure refactor; no oracle needed |
| 49 | Roadmap issue | tracking issue, not work |
| 43 | Create/edit lights and smoke FX | `Smoke_Init` `0x437510`, `Smoke_Create` `0x437B90`, `Smoke_Move` `0x437AD0`, `AlarmLight_*` |
| 29 | OBJ → MEF importer | writer-side tooling; `ModelFile_Setup` (`src/model/model.c`) and `MTP_TEXF` are the format references |
| 7 | AI and waypoint path editor | no IGI-RE navigation symbols under those names; the AI graph is reachable from the task types the editor already parses |

## #62 status after the loader wiring

#62 has three parts. Two are delivered on this branch; the third is a feature
step with a named blocker.

**Delivered — the rule.** `source/lod/lod_chain.h` (`VirModel_InitLods`,
`src/model/model.c:145-163`, `HOOK(0x4CED50)`): last-character name increment,
first-miss terminator, five-LOD cap, `100/(i*i)` distances. Exercised by the unit
tests and the corpus histogram test in `tests/test_lod_chain.cpp`.

**Delivered — the loader wiring.** `Renderer_Objects` now resolves the chain in the
mesh lookup path (`source/renderer/renderer_objects_mesh.cpp:16-115`):

* `MeshNameResolves` — exact `<name>.mef` existence over the indexed level/common
  `.res` archives, then exact on-disk files. That is `ModelLod_Find`'s `strcmp`
  semantics (`src/model/model.c:165-178`, `HOOK(0x4CEE30)`); `FindModelFile`'s
  fuzzy fallback is deliberately **not** used, because it would resolve
  `435_01_6` to `435_01_1` and invent a level the engine never sees.
* `GetLodChain` / `GetLodMeshName` — the chain, cached per level and dropped
  whenever the loaded `.res` set changes (`renderer_objects.cpp:367-379`,
  `:435`); `GetOrLoadMesh` consults it, so any model the editor loads carries
  its chain.
* `GetAuthoredLodDistances` / `SelectLodForDistance` — the authored table from
  `lod.qvm` in world units (non-decreasing clamp, x4096: exactly
  `ModelLODSettings_Changed`, `src/animtrig/lodsettings.c:157-171`,
  `HOOK(0x4EDB60)`) and `lod_select`'s authored branch (`src/lod/mesh3d.c:172-184`)
  through the existing `igi::lod::SelectAuthoredLod`. No new selection rule was
  invented.
* the `lod.qvm` `ModelLODSettings` tokenizer moved to
  `source/lod/lod_qvm_table.h`, so the parse has one host-tested implementation
  instead of an inline loop.

Measured on the retail corpus (`tests/test_lod_chain.cpp`, host lane):

* `[lod-res] levels=14 resMefNames=6605 vnamNames=6605 onlyVnam=0 onlyRes=0 differingChains=0`
  — the packed model archive's `.mef` name set **equals** the engine's VNAM set in
  all 14 bundles, and every name's chain is identical under both. The wiring's
  existence semantics are therefore faithful, not merely plausible.
* `[lod-qvm] markerLines=462 authoredEntries=462 distinctModels=450 trailing_1=462 resolvedInBundles=431 commonArchiveNames=189 monotonizeChanged=0 selected=431 culled=431`
  — every resolvable authored model selects inside its chain and is culled past
  its authored cutoff. Note the previously quoted "449 authored entries" is off:
  the renderer's own tokenizer, moved verbatim, sees **462** lines over 450
  distinct names.
* the 31 authored entries whose model is in no shipped bundle are the engine's
  `"missing"` fallback case (`VirModel_Find`, `src/model/model.c:107-116`, called
  from `ModelLODSettings_Changed`): the editor has no substitute, so those names
  simply have no chain.

**Not delivered — the per-frame LOD switch (the issue's override/selection
control).** The rule and the data are exposed, but the drawn mesh is still the
object's own name. Switching it changes the submesh set, and the editor's lightmap
binding is a per-task texture vector indexed by submesh of the **LOD 0** mesh
(`source/renderer/model.cpp:241-250`), so drawing a lower LOD without its own bake
would bind the wrong atlas page or none. That design step is the blocker, and it
is what would give `source/renderer/lightmap_ref_index.h` a consumer.

**Non-applicable — per-LOD lightmap reference counts.** The engine reserves one
`LightmapRef` per LOD (`fill_refs`, `src/sector/sector.c:544-563`) for the
hemicube **bake** renderer `Hemicube_DrawLightmapSet`
(`src/litmap/hemirender.c:78-86`), which picks the mesh at the requested LOD, and
for `Lightmap_BuildLod`'s `<name>_<refIndex>.olm` naming
(`src/dirlight/lightmapbuild.c:59`). The editor has no `LightmapSet` and no
hemicube bake: it resolves retail `.olm` files through `igi1conv lightmap resolve`
per model + task id (`source/utils_igi1conv.cpp:177-190`,
`source/app_editor.cpp:341-399`) and binds them by task key
(`renderer_objects.cpp:990`, `:1096-1102`). Computing the engine's ref index would
need the sector-object walk order (`SectorObject_FillMagicRefs`,
`sector.c:565-575`) over a task tree the editor's flat object list does not model,
plus `LightmapInfo_MakeName`'s first-free `obj%05d` slot allocation
(`src/litmap/lightmap.c:231-252`). `ObjectLightmapManager::GetLightmapTexture`
(`source/renderer/object_lightmap.cpp:125-137`) is therefore genuinely dead in this
architecture and is **not** force-wired.

`renderer_objects_metadata.cpp:9-54` parses the distance data and now also keeps
the engine's clamped, scaled table; the drawn-LOD decision itself is **not** part
of the chain rule: it comes from `lod_select` (`src/lod/mesh3d.c:160-208`) using
the authored table set by `ModelLODSettings_Changed` (HOOK `0x4EDB60`).
`lod_dist[5] = 100/(i*i)` in the model file is dead data with no reader, and
`0x2000` is `ModelFile_Setup`'s cross-bundle reference scheme
(`src/model/model.c:250`, `:281-282`, `:302-309`), not an LOD rule — both are easy
to implement wrongly from the issue text alone.

---

## Latent defects in shared code

Both were found while building the retail menu reader (`source/menu/qvm_image.cpp`).
The "no test that can fail" justification this section originally used for both has
now been checked against the code and the corpus. It was **wrong for one of them**
and **right for the other**, so one is patched and one is documented in place.

### `QVM_OperandSize` disagrees with the interpreter for `BLK` and `JSR` — RECORDED, NOT PATCHED

`source/level/qvm_parser.cpp` gives `BLK` and `JSR` a one-byte layout, while the
interpreter reads `BLK` as `pc += 5 + read32(pc+1)` and treats `JSR` as illegal. The
parser matches the *assembler*, which emits one byte, so this is an
assembler-versus-interpreter divergence rather than a parser bug per se.

Verified measurement (whole installed corpus, `$IGI_GAME_PATH`, 1043 `.qvm` files,
1043 parsed, 0 rejected): **0** `PUSHA`, **0** `BLK`, **0** `JSR` instructions, so no
existing `QVM_Parse` call site can observe it. Enforced by
`QvmCorpusLayoutTest.NoRetailQvmDecodesPushaBlkOrJsr` (`tests/test_qvm_roundtrip.cpp`),
which prints `[corpus] .qvm files=1043 parsed=1043 rejected=0 PUSHA=0 BLK=0 JSR=0`.

Reading both sides in IGI-RE changes the picture in one respect the original entry
missed: **`PUSHA` diverges too, in the opposite direction.** The full three-way
picture, and the side this parser follows:

| opcode | interpreter | assembler / disassembler | parser follows |
| --- | --- | --- | --- |
| `BLK` (47) | `Qvm_OpBlk` `0x4BE980` — `src/qsc/qvmop.c:338-342`: `pc += 5 + Read32(code + pc + 1)`, i.e. a 4-byte skip count plus that many skipped bytes | `QvmAsm_OpPlain` `0x4C0F50` — `src/qasm/asm.c:170-176` writes 1 byte; `QvmDis_OpPlain` `src/qasm/dis.c:66-68` steps 1 | **assembler** (1 byte) |
| `JSR` (23) | `Qvm_OpError` = `Qvm_OpIllegal` `0x4BEC90` — `src/qvm/qvm.c:545`, `src/qsc/qvmop.c:491-496`: sets the error, does not advance `pc`; `Qvm_Execute` (`src/qvm/qvm.c:326-337`) stops | `QvmAsm_OpPlain`, 1 byte | **assembler** (1 byte) |
| `PUSHA` (6) | `Qvm_OpPush` `0x4BE8F0` — `src/qvm/qvm.c:538`, `src/qsc/qvmop.c:311-327`: 4-byte operand, `pc += 5` | `QvmAsm_OpPlain`, 1 byte (`PUSHA` is absent from the `QvmAsm_OpPush` registrations, `src/qasm/asm.c:46-57`) | **interpreter** (5 bytes) |

Which side is authoritative for a *reader*: the assembler, for the byte layout. A
`.qvm` is an assembler artefact and a linear instruction walk must follow the bytes
the producer wrote or it desynchronises. The interpreter is authoritative for
*semantics*, and the parser is not an interpreter (`PORTING_SPECIFICATION.md:425`:
"Do not make the decompiler an interpreter"). The interpreter's reading of `BLK` is a
latent retail defect — a 1-byte `BLK` makes it eat the next four bytes as a skip
count — which is exactly why no working script contains one.

Why nothing is changed: the retail *script compiler* never names any of the three
(`src/qvm/qscgen.c` emits only `OP_BRK` `:134`, `OP_BRA` `:216`, `:229`, `:252`,
`:346`, `:373`, `OP_PUSH` `:308`, `OP_PUSHS` `:313`, `OP_PUSHF` `:318`, `OP_PUSHI`
`:324`, `OP_CALL` `:331` via `QscGen_WriteOpcode` `0x4BBF40`, `src/qvm/qscgen.c:180-183`,
which writes the opcode *name* for the assembler), and a file that did name them
would not run. With no producer and no consumer for the divergent encoding, altering
the table would be a blind change to a function shared by every `QVM_Parse` caller
(17 source files, plus `tools/ai_dump`; the reader-side list is in
`source/menu/qvm_image.h:6-12`). The chosen layout is
instead stated in the parser and pinned by
`QvmOperandLayoutTest.BlkAndJsrAreOneByteAndPushaIsFiveBytes`.

### `EscapeQSCString` did not escape a backslash — FIXED

`source/level/qvm_decompiler.cpp` escaped `\n` and `"` but not `\`, so a string
containing a backslash could not round-trip through `QVM_DecompileToString`: the
emitted QSC text was ambiguous and the repository's own QSC lexer misread it. The
ambiguous case is a backslash immediately before a quote — the value `\"` printed as
`"\\""`, which the lexer reads as one backslash and then a *closed* literal, so the
value was destroyed and the rest of the line became a stray string.

Verified measurement, wider than the original entry: across **all** 1043 retail
`.qvm` files, **0 of 29070 strings** (string pool + identifier pool) contain a
backslash. The two menu files match the recorded numbers exactly — `ingamemenu.qvm`
139 strings / 0 backslashes / 14 with a newline, `MAINMENU.QVM` 249 / 0 / 28 — enforced
by `QvmCorpusLayoutTest.RetailMenuStringsContainNoBackslash`. Nine strings in six
files do contain a double quote, which is what the existing `\"` escaping covers.

The "no test that can fail" justification was **wrong**. The input is synthetic, not
unreachable: a QVM whose string pool holds a single backslash is a two-byte pool
entry plus a six-byte code section, and with the fix reverted all four new tests fail:

```
[  FAILED  ] QvmStringEscapeTest.PooledStringWithBackslashSurvivesPrinting
[  FAILED  ] QvmStringEscapeTest.BackslashBeforeQuotePrintsUnambiguously
[  FAILED  ] QvmStringEscapeTest.InlinePushStringEscapesBackslashToo
[  FAILED  ] QvmStringEscapeTest.CompileDecompilePipelinePreservesEscapedStrings
 4 FAILED TESTS
```

with `QVM_DecompileToString` printing `"\\"";` where the round-trip requires `"\\\"";`.

### The reader side: the engine does not unescape either — MATCHED, NOT A DEFECT

The escaping above is the *decompiler's* half. The other half is `QSC::Load`
(`source/level/level_common.cpp:483`), which calls
`CleanBloatedBackslashesInPlace` (`:11-36`) and then `Parser_ForwardToDoubleQuoto`
(`:768-787`). Neither unescapes `\\` or `\"`: `CleanBloatedBackslashesInPlace`
collapses only runs of **four or more** backslashes down to two, and
`Parser_ForwardToDoubleQuoto` skips two characters on a backslash purely to find
the closing quote.

That is exactly what the engine does. `Reader_ReadLine`
(`src/qsc/reader.c:36-111`) scans a quoted region with `if (c == '\\') c = s[++i];`
and rewrites nothing — the backslash survives in the token. Its only purpose is to
terminate the closing-quote search and to keep a `//` *inside* a string from being
taken as a comment (`reader.c:98-101`, "starts on the opening quote and stops
there, so `//` is cut even inside strings").

So the editor must **not** add an unescape: that would diverge from the engine. The
asymmetry is inherent to the format and is inert on retail data, because 0 of
29070 retail strings contain a backslash. Recorded here so the next pass does not
"fix" it in the wrong direction.

The fix is `\` -> `\\` in `EscapeQSCString`. No reader needed changing: both in-repo
inverses already accept `\\` — `qsc::Lex` (`source/level/qsc_lexer.cpp:141-150`) and
`igi::MenuDefinition::FromDecompiledSource` (`source/menu/qvm_image.cpp:480-489`) —
so the printer was the only inconsistent side. `\\` for a literal backslash is also
the repository's own convention elsewhere (`VisualIntegrity::EscapeJson`,
`source/visual_integrity.cpp:120-128`, escapes `\` and `"` the same way, and the
backslash-escape strippers in `source/level/level_objects_serialize.cpp:78-97`,
`source/level/level_objects_internal.h:111-144` and the `mission_*_loader.cpp` files
all consume `\\` as one backslash), so the printer was the outlier, not the readers.
A third reader, the retail-style raw
parser `QSC::Load` (`source/level/level_common.cpp:483`, string scan at `:768-785`),
performs no unescaping at all and therefore never round-tripped `\n` either; it is not
the contract `EscapeQSCString` is written against and it is unchanged.

Tests: `QvmStringEscapeTest.*` in `tests/test_qvm_roundtrip.cpp` (pooled string,
inline `PUSHS` string, exact printed form for the ambiguous case, and the full
Lex -> Parse -> Compile -> QVM bytes -> Parse -> Decompile -> Lex pipeline).

Two test-lane repairs were needed to run any of this: `tests/test_qvm_roundtrip.cpp`
referenced `Utils::GetIGIRootPath()` (defined in the non-host-portable
`source/utils.cpp`), so the whole suite failed to link natively and was reported as
"not host-portable (runtime unresolved symbol)"; it now builds the corpus path from
`IGI_GAME_PATH` / `GetExeDirectory()`. And `FixturePath` built a literal
`\fixtures\<name>` path, which is a single filename on POSIX, so every
fixture-backed test failed once the suite could link; it now searches
`<exeDir>/fixtures` then `<exeDir>/tests/fixtures`.
