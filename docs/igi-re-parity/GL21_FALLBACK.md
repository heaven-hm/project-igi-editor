# The OpenGL 2.1 fallback

The editor's renderer was written against an OpenGL 4.1+ core context: its shader
tiers are `shaders/41` (`#version 410 core`) and `shaders/45` (`#version 450
core`), and it uses uniform buffer objects and core-profile vertex array objects.

On this Mac none of that is reachable. Wine only ever hands out Apple's **legacy**
profile, so the editor died during `App::Init` in the `skydome` subsystem on every
run. `igi1ed.log` recorded the same failure 92 lines deep:

```
[ERROR] shader ".../editor/shaders/41/skydome.vert" compile error:
        ERROR: 0:6: '' :  version '410' is not supported
        ERROR: 0:14: 'layout' : syntax error: syntax error
[ERROR] [renderer] initialisation failed in 'skydome' (GL_VERSION=2.1 Metal - 89.3, GLSL=1.20)
```

This document records the fallback that fixes it, and exactly how much of it is
proven.

## 1. Why a fallback is the only route

Wine on macOS cannot produce a 4.1 context. A direct probe of
`wglCreateContextAttribsARB` under Wine 11.0 returned **NULL for both a 4.1 core
and a 4.1 compatibility request**; the only context available reports:

```
GL_VERSION  = 2.1 Metal - 89.3
GL_VENDOR   = Apple
GL_RENDERER = Apple M2
GLSL        = 1.20
```

So no context attribute the editor can set will reach 4.1, and the 410/450 shaders
can never compile. A GLSL 1.20 tier is the only way the editor runs on this host.

Note that the renderer's ~128 immediate-mode calls (`glBegin`/`glEnd`) are **not** a
problem: immediate mode exists in GL 2.1 and was only removed in core 3.2+. The
blockers were the shader `#version` lines and the core-profile-only features below.

## 2. What a 2.1 context actually provides

Measured, not assumed. A probe compiled every GL entry point this source calls
(110 names extracted from `source/`) and asked GLEW whether each resolved on the
real 2.1 context. Result: **97 usable, 8 NULL**.

| NULL | Handled by |
| --- | --- |
| `glGenVertexArrays`, `glBindVertexArray`, `glDeleteVertexArrays` | patched to the `APPLE_vertex_array_object` equivalents (§3) |
| `glBindBufferBase`, `glGetUniformBlockIndex`, `glUniformBlockBinding` | uniform blocks emulated with plain uniforms (§4) |
| `glBindTextureUnit` | already guarded by `g_gl_info.support_version_45_` |
| `glDepthRangef` | patched to `glDepthRange` (§3) |

Available and used as-is: `GL_ARB_framebuffer_object`, `GL_EXT_framebuffer_object`,
`glGenerateMipmap`, `GL_ARB_draw_elements_base_vertex`, `GL_ARB_instanced_arrays`,
`GL_ARB_texture_non_power_of_two`, `GL_EXT_texture_array`,
`GL_ARB_seamless_cube_map`, `glActiveTexture`, `glBlendEquation`,
`glBlendFuncSeparate`.

Not present, but **not used** by this source at all: `glGenSamplers`/`glBindSampler`
(`GL_ARB_sampler_objects`), `glMapBufferRange` (`GL_ARB_map_buffer_range`),
`glTexStorage2D` (`GL_ARB_texture_storage`).

## 3. Repairs at the entry-point level

`GL_Init()` sets `g_gl_info.legacy_gl21_` when the context has no GL 3.0, then
repoints the NULL GLEW function pointers:

- `glGenVertexArrays` / `glBindVertexArray` / `glDeleteVertexArrays` →
  `glGenVertexArraysAPPLE` / `glBindVertexArrayAPPLE` / `glDeleteVertexArraysAPPLE`
  (the casts are only for the APPLE prototypes' `const GLuint*`).
- `glBindBufferBase` → the uniform-block emulation in §4.
- `glDepthRangef` → `glDepthRange` with the arguments widened to `GLdouble`.

This is why the ~103 VAO call sites spread over 14 files and the 26
`glBindBufferBase` call sites needed **no edits at all**.

If neither the core nor the APPLE VAO entry points exist, `GL_Init()` fails with an
explicit message rather than letting a NULL call crash later.

## 4. Uniform blocks, emulated

The 2.1 context has no `GL_ARB_uniform_buffer_object`, so `uniform ub_mats { ... }`
cannot exist. There is no emulation that preserves the GL semantics, so the values
are re-uploaded as plain uniforms:

- `GL_BufferData()` keeps the bytes a UBO upload would have written, in
  `g_legacy_ubos`, keyed by buffer id, instead of calling GL.
- The patched `glBindBufferBase(GL_UNIFORM_BUFFER, ...)` uploads those bytes into
  whichever program is current.

The block layouts come from `renderer.h`:

| Block | Layout | Uniforms uploaded |
| --- | --- | --- |
| `ubo_mats_s` | three `mat4` | `g_mvp_mat_follow_view` (0), `g_mvp_flat_sky_layer` (64), `g_mvp_objects` (128), and `u_mvp` (128) |
| `ubo_fog_s` | `vec4` + `float` | `g_fog_color` (0), `g_fog_far` (16) |

`u_mvp` is a second name for the same third matrix because the inline object and
rain shaders declare `uniform Matrices { mat4 u_unused1; mat4 u_unused2; mat4
u_mvp; }` over the same buffer.

Two details that are required rather than defensive:

1. **Every upload is guarded by `loc >= 0`.** Measured: the linker eliminates every
   uniform a program does not actually read, so `glGetUniformLocation` returns `-1`
   for most slots (in the skydome pairs only `g_mvp_mat_follow_view` is live; in the
   flat-sky pairs only `g_mvp_flat_sky_layer`; in the terrain pairs only
   `g_mvp_objects`). Uploading without the guard would be a GL error.
2. **`GL_SetUniformBlockBinding()` returns true without doing anything** in legacy
   mode. Callers treat a false return as a fatal init failure, and there is no block
   to bind.

`renderer_objects_visual.cpp` patched a single matrix inside the shared buffer with
raw `glBindBuffer(GL_UNIFORM_BUFFER, ...)` + `glBufferSubData(...)`. That one site
now goes through `GL_BufferSubData()`, which writes into the emulated cache on a
legacy context and calls GL otherwise.

## 5. Shaders

### 5.1 The file tier: `shaders/21/`

Thirteen GLSL 1.20 files, one per file in `shaders/41/`, selected by `main.cpp`
when `g_gl_info.legacy_gl21_` is set.

**Proven**: all 13 compile and all 8 renderer pairs link on a real GL 2.1 / GLSL
1.20 context (Apple's own GLSL compiler through Wine), with `glBindAttribLocation`
applied using the original 410 indices. Negative controls: the same harness pointed
at `shaders/41` fails all 13 with `version '410' is not supported`, and reverting
the varying renames makes the `skydome` and `flat_sky_layer` pairs fail to link with
`Input of fragment shader 'ps_in_clr' not written by vertex shader`.

Conversions, and the ones that carry risk:

| Change | Note |
| --- | --- |
| `#version 410 core` → `#version 120` | |
| `layout(location = N) in` → `attribute` | indices re-bound from C++ before linking |
| `in`/`out` varyings → `varying` | |
| `layout(location = 0) out vec4 X` → `gl_FragColor` | every original has exactly one output |
| `texture(s, uv)` → `texture2D(s, uv)` | no original passes a bias |
| `uniform ub_mats { ... }` → plain uniforms | §4 |
| **varying renames** | `ps_in_clr` → `vs_out_clr` (skydome.frag, skydome_wireframe.frag); `ps_interpolated_uvw` → `vs_interpolated_uvw`, `ps_in_clr` → `vs_out_clr` (flat_sky_layer.frag) |

Varyings match **by name** in GLSL 1.20, whereas the 410 shaders matched by
explicit location, which is why the renames are load-bearing rather than cosmetic.

**Known divergence.** `flat_sky_layer.vert` declares its varying `noperspective`,
which does not exist in 1.20. Dropping it means the varying is interpolated
`smooth` (perspective-correct) and is then divided by RHW again — double
perspective correction. The flat-sky-layer texture mapping therefore will not match
the 410 path; expect warping or scale error on that layer. Nothing else in the tier
is affected.

### 5.2 The inline tier

Twelve `#version 330 core` shaders are embedded in C++ (`renderer_objects.cpp`,
`renderer_objects_visual.cpp`, `renderer_rain.cpp` — two vertex/fragment pairs
each). The file tier does not cover them, and they were the second startup failure
after the file tier was fixed. Each has a 1.20 variant selected by the same flag.
Their `uniform Matrices` block becomes a plain `uniform mat4 u_mvp` (§4), and the
programs call `GL_LegacyBindInlineAttribLocations()` before linking.

## 6. Deployment

The runtime loads from `<exe>/editor/shaders/<tier>`, which is populated by copying
`assets/editor/` in a POST_BUILD step. The `21` tier is copied **directly from
`shaders/21`** rather than being kept as a second copy under
`assets/editor/shaders/`:

```cmake
COMMAND ${CMAKE_COMMAND} -E copy_directory
  "${PROJECT_SOURCE_DIR}/shaders/21"
  "$<TARGET_FILE_DIR:igi-editor>/editor/shaders/21"
```

That is deliberate. `shaders/32` (a `#version 330 core` tier) is tracked in the
repository but absent from `assets/editor/shaders/`, so it has never shipped and
nothing selects it — dead code produced by exactly the duplication this avoids.

## 7. Two further blockers found during bring-up

Getting from "shaders compile" to "the editor runs" needed two more fixes. The second
is **not** a 2.1 problem — it is a pre-existing state-management bug that the 4.1 path
has too.

### 7.1 `glGetUniformBlockIndex` is NULL and was called directly

`GL_SetUniformBlockBinding()` returns early in legacy mode, but three files call
`glGetUniformBlockIndex` / `glUniformBlockBinding` **directly**, not through it
(`renderer_objects.cpp`, `renderer_objects_visual.cpp`, `renderer_rain.cpp`). On a 2.1
context both are NULL, and the first call was a hard crash:

```
wine: Unhandled page fault on read access to 00000000 at address 00000000
```

Reproduced in isolation before fixing. Fixed at the entry point, like the VAOs:
`glGetUniformBlockIndex` is repointed to a shim returning `GL_INVALID_INDEX`, which is the
truthful answer and makes every existing `if (blk != GL_INVALID_INDEX)` guard skip the
bind; `glUniformBlockBinding` gets a no-op shim so a NULL call can never be reached.

### 7.2 `FlushGizmo` left program 0 bound, killing the object shader

`Renderer_Objects::Draw` binds its object shader, then walks its per-object loop. Inside
that loop it calls `DrawPropsGizmos()`, which calls `FlushGizmo()`, which binds its own
gizmo program and then ended with an unconditional:

```cpp
glUseProgram(0);   // renderer_objects_visual.cpp:893
```

So from the first prop onward, **no program was bound for the rest of the frame.** Every
subsequent `glUniform*` raised `GL_INVALID_OPERATION` — on this context even for a `-1`
location — and the objects drew through the fixed-function pipeline instead of their
shader.

The symptom is misleading: the per-frame error was reported at the object stage, and every
individual call looked innocent (program current, location valid, uniform type matching).
It was localised only by draining `glGetError()` at the top of each loop iteration and
printing `GL_CURRENT_PROGRAM` at the failure site, which showed `curProg=0` where the
object shader was expected. An isolated harness that linked the real `obj.vert`/`obj.frag`
on the 2.1 context and drew in all four sampler states the loop produces returned
`glGetError() == GL_NO_ERROR` throughout — which is what ruled the shader port out and
pointed at caller state instead.

`FlushGizmo` now saves the incoming program and restores it, which is correct both when
the caller had a shader bound and when the caller really was in fixed-function state.

**This bug is not specific to the 2.1 fallback.** On a 4.1 core context the same
`glUseProgram(0)` would leave the object pass with no program. It is probably masked on
Windows only because `DrawPropsGizmos` early-returns when its gizmo program failed to
build (`if (prog == 0) return;`). Worth checking against the Windows build.

## 8. What is proven, and what is not

**Proven**

- The 2.1 context's capabilities and its 8 NULL entry points, by probe.
- `shaders/21`: 13/13 compile, 8/8 pairs link on the real 2.1 context, with negative
  controls.
- The 12 inline shaders: 12/12 compile, 6/6 pairs link, attribute indices land correctly,
  and the port is token-identical to the 330 originals after normalisation (the one
  non-lexical rewrite, `pick.frag`'s bitwise ID packing, is proven bit-identical over all
  16 777 216 ids).
- The editor **runs**: it activates the fallback, compiles every shader, loads its model
  list, and runs a steady frame loop. After §7.2 it logs **zero** GL errors over a 40 s
  run, where before the fix it logged roughly 1 700 `GL_INVALID_OPERATION` per minute.

**Not proven**

- That the 2.1 path **renders** identically to the 410 path. No frame has been compared
  pixel-for-pixel, and the `noperspective` drop (§5.1) is a known intentional divergence.
- That the rendered image is correct at all. A window capture of the running editor shows
  colour variation consistent with rendered terrain, but that is a weak signal and is not
  offered as proof of correct output.
- Any behaviour on a machine that has a genuine 4.1+ context: the 41/45 tiers and the
  non-legacy code paths are unchanged and were not exercised.
