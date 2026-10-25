# SCamera parameter layout and the Euler convention

Fixes the `SCamera` half of `docs/KNOWN_ISSUES.md` §1 "🎥 Camera Orientation" — cutscene
cameras rendering/orienting at the wrong angles, and camera tasks losing their model.

Evidence: the decompiled engine in `/Users/haseeb-mir/Documents/Code/IGI-RE` and the retail
level scripts shipped with IGI (`MISSIONS/location*/LEVEL*/objects.qsc`). No external
sources were used.

---

## 1. The engine's Euler convention (the actual root cause)

`Matrix_SetEuler` — `src/math/matrix.c:99-110`, `HOOK(0x4B38E0, ...)`:

```c
void Matrix_SetEuler(Matrix* m, Real32 ax, Real32 ay, Real32 az) {
    Matrix rx, ry, rz, t;
    Matrix_SetRotX(&rx, ax);
    Matrix_SetRotY(&ry, ay);
    Matrix_SetRotZ(&rz, az);
    Matrix_Mul(&t, &rz, &ry);
    Matrix_Mul(m, &t, &rx);     // m = Rz(az) * Ry(ay) * Rx(ax)
}
```

with `Matrix_Mul(out, a, b)` computing `out = a · b` on row-major 3×3 matrices, and
`ParamReal32x9_Parse` (`HOOK 0x4EFBA0`) passing QSC arguments straight through in that
order. The editor's viewport used a different composition order in six places, so a camera
(or any object) authored in the game's convention appeared rotated by the wrong axes. This
is the same defect that made generic object orientation disagree with the game.

**Fix**: `source/renderer/igi_transform.h` exposes
`igi::IgiEulerRotationMatrix(glm::dvec3)` = `Rz·Ry·Rx` (plus `IgiEulerRotationMat3` and
`IgiObjectMatrix`), and every Euler site in the editor now uses it —
`renderer_objects.cpp`, `renderer_objects_picking.cpp`, `app_view.cpp`,
`app_input_keyboard.cpp`, `runtime/graph_camera_target.h`.
`tests/test_igi_transform.cpp` is a line-by-line transcription of the engine's
`Matrix_SetEuler` / `SetRotX/Y/Z` / `Mul`, including a 343-case grid.

`glm::rotate(m, angle, axis)` post-multiplies, so an equivalent chain has to apply Z, then
Y, then X — which is what the helper does.

## 2. SCamera's parameter order

`src/scamera/scamera.c:116-131` (`HOOK(0x442730)`, `ParamTable_New(15)`) fixes the argument
order. Args 0–2 are the universal `id`, type, note; the declared parameters follow:

| QSC arg | parameter | notes |
| ---: | --- | --- |
| 3, 4, 5 | `place` (`ObjectPos`) | world position |
| 6 | `yaw` | holder yaw, folded with arg 9 for display |
| **7** | **`model`** | the model **this task draws** |
| 8 | `pitch` | |
| 9 | `yaw_offset` | an extra yaw, **not** roll |
| 10 | `lens_model` | the separate `SCameraLens` child's model |
| 11 | `broken_model` | camera-destroyed model |
| 12 | `yaw_min` (`Int16`) | |
| 13 | `yaw_max` (`Int16`) | |
| 14 | `turn_speed` | |
| 15 | `wait` (`Real32`) | |
| 16, 17 | `fov_x`, `fov_y` | |
| 18 | `range` | |
| 19 | `script` (`VarString`) | |

Three independent confirmations of this order:

1. **The param table itself** — `scamera.c:116-131`.
2. **The hard-coded defaults** at `scamera.c:181,184,185`: `c->model = "313_01_1"`,
   `lens_model = "313_02_1"`, `broken_model = "313_03_1"`.
3. **The retail data** — the single real `SCamera` line in `LEVEL1` passes `"313_01_1"`,
   `"313_02_1"`, `"313_03_1"` at args 7, 10 and 11 respectively.

The code that *draws* the camera confirms it too: `RigidDynCubeObj_SetModel(c,
VirModel_Find(c->model))` at `scamera.c:240` and `DynCube_Move(c->o.world, c, &c->place,
...)` at `:242` — arg 7 is the model this task renders. Arg 10 belongs to the separate
`SCameraLens` child (`scameralens.c:204`, `SCameraLens_Create` `HOOK 0x4415C0`), which is
added to `gLensList` internally; retail scripts never instantiate `SCameraLens` (0
occurrences in `LEVEL1`).

Arg 9 is an extra **yaw**, never roll — `scameralens.c:204`:
`Matrix_SetEuler(&l->o.matrix, l->pitch, 0, l->yaw + c->yaw + c->yaw_offset)`.

## 3. What the editor had wrong

* The property layout came from a generic `Real32x9` at arg 6, which let
  `object_set_transform` overwrite arg 7 — a **string** — with a rotation value.
* The model the editor drew came from arg 10 (`lensModelId`) rather than arg 7.
* Arg 9 was not modelled at all, so the yaw offset was lost on load and not written back.
* The Euler order was the generic (wrong) one.

## 4. Fix, and the one visible behaviour change

* `source/level/level_objects.cpp` — the `SCamera` branch is now `case 6: rot.z`,
  `case 7: modelId` (string), `case 8: rot.x`, `case 9: yawOffset (+= rot.z)`,
  `case 10: lensModelId`, `case 11: secondaryModelId`.
* `source/level/level_objects_serialize.cpp` — `ParseTaskLine` reads
  `3/4/5 → pos`, `6 → rot.z`, `8 → rot.x`, `9 → yawOffset` then `rot.z += yawOffset`,
  `modelId = token[7]`, `lensModelId = token[10]`, `secondaryModelId = token[11]`.
  `UpdateCoordinatesInLine` writes arg 6 back as `rot.z − yawOffset`, arg 9 as
  `yawOffset`, and args 7/10/11 as strings, so a save is lossless.
* `source/level/level_objects.h` — added `double yawOffset`; comments on
  `lensModelId`/`secondaryModelId` now say which argument they are.
* `source/level/task_schema.cpp` — the `SCamera` schema is 15 correctly-offset fields
  (Holder Gamma@6, Holder Model@7, Camera Alpha@8, Camera Gamma@9, Camera Model@10,
  Destroyed Model@11, Rotate Gamma Right/Left/Speed@12/13/14, Gamma Delay@15,
  Viewcone Alpha/Gamma/Length@16/17/18, On Expression@19).
* `source/mcp/mcp_tools_objects.cpp` and `source/mcp/game_data_service.cpp` — the model
  rule accepts `"Model"` **or** `"Holder Model"`, so the MCP surface resolves arg 7 as the
  task's model.
* `tests/test_mcp_domain_tools.cpp` — asserts `model_id.parameter_index == 7` and that the
  `SCamera` field set no longer exposes `rotation_radians` (the old `Real32x9@6` that
  allowed arg 7 to be clobbered).

**Visible change to document:** the editor now draws the model from arg 7 instead of arg
10. For the retail data these differ (`313_01_1` vs `313_02_1`), so a cutscene camera's
viewport model changes. This is deliberate: arg 7 is the model the game itself draws at
the camera's place (`scamera.c:240`), while arg 10 belongs to a child object the editor
does not model.

## 5. Verification

| Check | Result |
| --- | --- |
| `tests/test_scamera_params.cpp` (new, 4 tests) | PASS |
| Retail corpus replay | all 4 `SCamera` lines across the 14 scripts parse to the declared layout (exactly 20 arg tokens each; arg 10/11 match `^\d{3}_\d{2}_\d$`; arg 12 parses as an int; args 6/8/9 parse as doubles) |
| Yaw-offset round trip | `rot.z` 1.5 → 2.0 saves as arg 6 = 1.5 and arg 9 = 0.5, keeps args 7/10/11 verbatim, and re-parses to 2.0 |
| `tests/test_igi_transform.cpp` | PASS, 343-case grid against the transcribed engine maths |
| Host lane | `PASS test_scamera_params 4 passed 0 failed`, exit 0 |

**Not verified:** no Windows editor session was run, so the visual result in the viewport
and the property panel is not confirmed by eye; only compile-, unit- and retail-data-level
evidence is claimed. The differential harness against retail `IGI.exe` is unusable here:
the installed executable's SHA-256 (`dea34892…`) is not the EU build IGI-RE targets
(`ae93669a…`), so its assets are used as data only.
