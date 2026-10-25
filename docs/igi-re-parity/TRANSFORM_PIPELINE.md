# Object transform pipeline: issue #37 verdict

Issue #37 (`[Core] Fix object rotation, placement errors, and offset positioning`) lists four
"suspect areas". This document resolves each one against the decompiled engine
(`/Users/haseeb-mir/Documents/Code/IGI-RE`) and the retail scripts, and records which were
real defects, which were already correct, and which are refuted. No external sources were
used.

---

## 1. Coordinate system handedness — already correct (verified)

**Issue claim:** "IGI uses a right-handed Y-up coordinate system. Verify the editor's
internal matrix stack matches this."

**Oracle.** `Matrix_SetRotZ` (`src/math/matrix.c:148-164`, `HOOK 0x4B3BE0`) writes, on a
row-major 3×3:

```
m[0] =  cos a     m[1] = -sin a     m[2] = 0
m[3] =  sin a     m[4] =  cos a     m[5] = 0
m[6] =  0         m[7] =  0         m[8] = 1
```

That is the standard counter-clockwise rotation about **+Z** in a right-handed system, and
`Matrix_SetRotX`/`SetRotY` (`:112-146`) follow the same pattern. Y is up: `Matrix_SetDirection`
(`:244-277`, `HOOK 0x4B4010`) builds an orientation whose second column is the direction
vector, i.e. local +Y is "forward" — the same convention the editor uses for wires
(`docs/igi-re-parity/PROPS_WIRE_INTERACTABLES.md`).

**Editor.** `renderer_objects.cpp:810-882` builds the model matrix from `glm::translate`,
`igi::IgiEulerRotationMatrix` and `glm::scale` with no axis negation, and glm is
right-handed Y-up. A grep for negated components in the transform path
(`-obj.pos.`, `-obj.rot.`, `-pos.z`) finds nothing.

**Verdict:** no defect. No change made.

## 2. Euler rotation order — real defect, FIXED

**Issue claim:** "IGI … uses YXZ Euler order."

**Oracle:** the claim is **wrong**, and the decompiled code is unambiguous.
`Matrix_SetEuler` (`src/math/matrix.c:99-110`, `HOOK 0x4B38E0`) is:

```c
Matrix_SetRotX(&rx, ax); Matrix_SetRotY(&ry, ay); Matrix_SetRotZ(&rz, az);
Matrix_Mul(&t, &rz, &ry);      /* t = Rz · Ry */
Matrix_Mul(m, &t, &rx);        /* m = Rz · Ry · Rx */
```

`ParamReal32x9_Parse` (`src/physobj/paramops.c:423-436`, `HOOK 0x4EFBA0`) feeds the authored
QSC triple straight into it (`Matrix_SetEuler(m, x, y, z)`), so **every** authored object uses
`Rz(az)·Ry(ay)·Rx(ax)` — there is no per-model rotation order.

The inverse confirms it: `Matrix_GetEuler` (`:181-208`) decomposes with
`cy = sqrt(m[0]² + m[3]²)`, `rx = atan2(m[7], m[8])`, `ry = atan2(-m[6], cy)`,
`rz = atan2(m[3], m[0])` — the standard ZYX-composition decomposition.

**Editor defect.** The viewport composed rotations in a different order (and carried an
`IsZyxEulerModel` whitelist of models that were special-cased). A sweep of the whole tree for
every site that composes or decomposes an authored Euler triple — not just the render path —
found **ten** sites in total, all of which disagreed with the engine. Six were in the render
path (`renderer_objects.cpp`, `renderer_objects_picking.cpp`, `app_view.cpp`,
`app_input_keyboard.cpp`, `runtime/graph_camera_target.h`, plus the lightmap/attachment
lambdas); the other four were found later and fixed in the same sweep:

| Site | What it did | Consequence |
| --- | --- | --- |
| `renderer_objects.cpp:819-882` + the five render-path sites | composed Z→X→Y, with an `IsZyxEulerModel` whitelist | every multi-axis object and camera rendered at the wrong angle (#37) |
| `app.cpp:3126-3138` | composed Z→X→Y when building the runtime ladder-parent orientation | attached runtime ladders got wrong mount offsets and band axes |
| `app.cpp` door slide (was :2364-2378) | `Rz(closed)·Rx(rot.x)·Ry(rot.y)` | a rotated door slid along the wrong world axis |
| `app_level.cpp:754-761` | `Rz·Rx·Ry` when rebuilding a promoted ATTA child's world rotation for MEF write-back | the child was saved to the MEF with the wrong local rotation on a rotated parent |
| `debug_command_manager.cpp:395-401` | `Rz·Rx·Ry` when rotating the mesh-centre offset | "centre on object" targeted the wrong point for multi-axis objects |
| `app_editor.cpp:1223-1243` | an `Rz·Rx·Ry` **decomposition** of the captured world rotation | a promoted ATTA proxy inherited the wrong orientation |

`renderer/igi_transform.h` now carries both directions of the conversion: the composition
(`IgiEulerRotationMatrix`, `IgiEulerRotationMat3`, `IgiObjectMatrix`) and the inverse
(`IgiMatrixToEuler`), the latter a transcription of the engine's own `Matrix_GetEuler`
(`src/math/matrix.c:181-208`, `HOOK 0x4B3C50`) including its 0.0001 gimbal guard. The engine
additionally maps negative `rx`/`ry` into [0, 2π); that is a no-op modulo 2π for any caller
that rebuilds a matrix, so it is documented rather than reproduced.

The door site has a second, separate oracle worth stating: IGI-RE `Door_Place`
(`HOOK 0x446290`, `src/door/door.c:121-138`) rotates the slide offset by the door's **closed**
basis — `u = slide[0]*slidePos*kSlideScale`, `v = slide[1]*slidePos*kSlideScale`,
`x = m[1]*v + m[0]*u`, `y = m[4]*v + m[3]*u`, `z = m[7]*v + m[6]*u`, with `m = base.m` — and
the opening rotation is post-multiplied on top of it
(`Matrix_RotateZ(&d->o.matrix, &d->base, angle)`, `:446`). That is why the editor uses the
closed yaw and not `rot.z` when transforming the offset.

## 3. Object pivot / local origin offset — refuted

**Issue claim:** "MEF models have their own local-space pivot origin baked in. When placing
an object, the editor must account for the model's local origin offset (read from the MEF
header) before applying world-space position. Skipping this causes the 'entering walls'
offset."

**Oracle:** the engine applies no such offset, and the structures have no field for one.
`Mesh3D_GetWorldVertex` (`src/lod/mesh3d.c:425-443`) is the placement maths in full:

```c
x = v[0] * o->scale[0]; y = v[1] * o->scale[1]; z = v[2] * o->scale[2];
out[0] = z*m[2] + y*m[1] + x*m[0];
out[1] = z*m[5] + y*m[4] + x*m[3];
out[2] = z*m[8] + y*m[7] + x*m[6];      /* = M · (S · v), relative to the object position */
```

`v` is the raw model vertex from `lod_header(model->lods[0])->verts[i]`; the result is
"relative to the object position", which is then the object's `pos`. The object structures
carry exactly `pos[3]`, `m[9]`, `scale[3]` and the model pointer — `RigidMeshObj`
(`src/lod/mesh3d.c:35-43`) and `RigidDynCubeObj` (`src/magicobj/magicobjtypes.h:9-31`,
`_Static_assert`ed at 0xF0 bytes) — with no origin/pivot member. A model-space origin, if a
model has one, is already baked into its vertices.

**Verdict:** the proposed fix (offset placement by an MEF pivot) would *introduce* the
misalignment it describes. The real "objects clip into walls" causes in the editor were the
rotation-order error (§2) and objects loading at `(0,0,0)`
(`docs/igi-re-parity/DECLARED_POSITION_FALLBACK.md`) — 1757 retail objects were then snapped
to the terrain at the origin. Both are fixed.

## 4. Transform application order — already correct (verified)

**Issue claim:** "Correct order is Scale → Rotate → Translate (SRT)."

**Oracle:** `Mesh3D_GetWorldVertex` scales the vertex, then rotates it, and the object
position is added by the caller — i.e. `world = pos + M·(S·v)`, which is SRT.

**Editor:** `renderer_objects.cpp:819-882` builds `T(pos) · R · S`, so a vertex is transformed
as `T·R·S·v` — identical. The wire branch deliberately bypasses `T·R·S` because a wire's
rotation is derived from its anchors (`Matrix_SetYAxis`), not authored.

**Verdict:** no defect. No change made.

---

## Summary

| #37 area | Issue's guess | IGI-RE verdict | Editor status |
| --- | --- | --- | --- |
| Handedness | verify right-handed Y-up | confirmed right-handed Y-up | already correct |
| Euler order | "YXZ" | **wrong** — `Rz·Ry·Rx` | fixed (6 sites) |
| Pivot / MEF origin offset | apply it | **refuted** — no such offset exists | not implemented, correctly |
| Transform order | SRT | confirmed SRT | already correct |

## Verification

| Check | Result |
| --- | --- |
| `tests/test_igi_transform.cpp` | PASS (9 tests) — transcribes `Matrix_SetEuler`/`SetRotX/Y/Z`/`Mul` and checks the helper against them on a 343-case grid; transcribes `Matrix_GetEuler` and checks `IgiMatrixToEuler` against it on a 245-case grid, plus round-trips and a case that fails on the pre-fix `Rz·Rx·Ry` extraction |
| `tests/test_scamera_params.cpp` | PASS — 4 tests over the real retail `SCamera` line and all 14 retail scripts |
| `tests/test_declared_position_fallback.cpp` | PASS — 5 tests over 30 verbatim retail declarations |

**Not verified:** no Windows editor session was run, so the on-screen result ("no clipping
into walls") is not confirmed visually; the evidence is compile-, unit- and data-level. The
differential harness against retail `IGI.exe` is unusable on this host (installed SHA-256
`dea34892…` ≠ IGI-RE's EU target `ae93669a…`), so its assets are used as data only.
