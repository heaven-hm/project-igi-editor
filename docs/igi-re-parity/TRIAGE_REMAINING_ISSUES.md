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
| 62 | LOD chains | `0x4CED50` | `VirModel_InitLods` (chain resolution delivered; see below) |
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

## #62 remainder, stated precisely

#62's chain-resolution half is delivered (`source/lod/lod_chain.h`,
`tests/test_lod_chain.cpp`): the name-increment rule, the resolution stop, the
five-LOD cap and the corpus measurement. Two halves are **not** delivered and are
the reason the issue is not closed by this branch:

1. **Loader wiring.** `lod_chain.h` is not yet consulted by the mesh lookup path
   (`renderer_objects.cpp` / `renderer_objects_mesh.cpp`), so the editor still
   resolves a single mesh name rather than walking the chain.
2. **Per-LOD lightmap counts.** `ObjectLightmapManager::GetLightmapTexture`
   (`source/renderer/object_lightmap.cpp:125-137`) has zero callers. The engine
   indexes lightmap pages by counting `lod_count` references per object
   (`src/sector/sector.c:546-562`).

`renderer_objects_metadata.cpp:13-57` already parses the distance data correctly,
and the drawn-LOD decision itself is **not** part of this rule: it comes from
`lod_select` (`src/lod/mesh3d.c:160-208`) using the authored table set by
`ModelLODSettings_Changed` (HOOK `0x4EDB60`). `lod_dist[5] = 100/(i*i)` in the
model file is dead data with no reader, and `0x2000` is `ModelFile_Setup`'s
cross-bundle reference scheme (`src/model/model.c:250`, `:281-282`, `:302-309`),
not an LOD rule — both are easy to implement wrongly from the issue text alone.
