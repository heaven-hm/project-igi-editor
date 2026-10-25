/******************************************************************************
 * @file    atta_children.h
 * @brief   Pure, host-portable ATTA (model attachment / child node) maths:
 *          record layout, the parent -> child world transform, and the rotor
 *          spin frame.
 *
 * Every rule below is taken from IGI-RE (the verified decompilation of the
 * retail IGI.exe) and cites the exact `file:line`. Nothing here touches GL or
 * Windows, so the rules are unit-testable on the host build.
 *****************************************************************************/
#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace igi {

// ─── ATTA record layout ─────────────────────────────────────────────────────
// IGI-RE `src/model/modeltypes.h:26` — the MESH header's `attach` pointer is
// documented "ATTA, 0x44 bytes each", and `ModelLod_GetAttach`
// (`src/lod/mesh3d.c:493-497`) strides the chunk by exactly 0x44. The field
// order is the decompiler's `MagicAttach` (`src/magicobj/rigidcoll.c:359-366`),
// mirrored by `ModelAttach` (`src/shadow/sector.c:516-520`):
//
//     char name[16];  float pos[3];  float m[9];  int unk40;   == 0x44
//
// `name` is a DefineMagicObj name (`src/shadow/sector.c:516`), `pos` is the
// child's position in the parent's local frame, `m` is the child's basis and
// `unk40` is never read by the retail code.
constexpr std::size_t kAttaRecordSize = 0x44;
constexpr std::size_t kAttaNameOffset = 0x00;
constexpr std::size_t kAttaPosOffset  = 0x10;
constexpr std::size_t kAttaMatOffset  = 0x1C;
constexpr std::size_t kAttaTailOffset = 0x40;

static_assert(kAttaRecordSize == 68, "ATTA record is 0x44 bytes");
static_assert(kAttaPosOffset - kAttaNameOffset == 16, "ATTA name is 16 bytes");
static_assert(kAttaMatOffset - kAttaPosOffset == 12, "ATTA pos is 3 floats");
static_assert(kAttaTailOffset - kAttaMatOffset == 36, "ATTA m is 9 floats");
static_assert(kAttaRecordSize - kAttaTailOffset == 4, "ATTA tail is one int32");

// ─── Child local rotation ───────────────────────────────────────────────────
// The game stores the attachment basis as the COLUMNS of `m` and builds the
// child frame with those columns as the ROWS of R:
// `src/magicobj/rigidcoll.c:419-436` takes W = (m[0], m[3], m[6]) (column 0),
// E = the component of (m[1], m[4], m[7]) (column 1) orthogonal to W, and
// F = W x E, then writes `R = arg.local.m` with rows W, E, F; the same
// construction is in `src/shadow/sector.c:545-603`. For an orthonormal basis
// that is exactly `R = transpose(m)`.
//
// GLM's `mat3(c0x,c0y,c0z, c1x,...)` fills COLUMNS, so passing the file's
// `m[0..8]` in order yields the transposed (mathematical) matrix the game uses.
inline glm::mat3 IgiAttaLocalRotation(const float m[9]) {
    return glm::mat3(m[0], m[1], m[2],
                     m[3], m[4], m[5],
                     m[6], m[7], m[8]);
}

// ─── child_world = parent_world * child_local ───────────────────────────────
// IGI-RE `src/magicobj/rigidcoll.c:399-409`:
//     rx = M[0]*pos[0] + M[1]*pos[1] + M[2]*pos[2];   (rows 1 and 2 likewise)
//     arg.pos = (rx, ry, rz) + o->pos;
// so the child's world position is `M * att->pos + o->pos`, with M the parent's
// row-major 3x3 (`o->matrix.m`) and `o->pos` its world position. The world
// frame is `P = M * R` (`:439-450`), i.e. the parent rotation composed with the
// child's local rotation. `src/shadow/sector.c:610-630` is the same product.
//
// `parentWorld` must be the parent's UNSCALED world transform (translation +
// rotation); the renderer applies the 40.96 world scale only at the leaf draw.
inline glm::mat4 IgiAttaChildWorld(const glm::mat4& parentWorld,
                                   const float pos[3],
                                   const float m[9]) {
    const glm::mat3 parentRot(parentWorld);           // translation dropped
    const glm::vec3 localPos(pos[0], pos[1], pos[2]);
    const glm::vec3 worldPos =
        glm::vec3(parentWorld * glm::vec4(localPos, 1.0f));
    const glm::mat3 rot = parentRot * IgiAttaLocalRotation(m);

    glm::mat4 child(1.0f);
    child[0] = glm::vec4(rot[0], 0.0f);
    child[1] = glm::vec4(rot[1], 0.0f);
    child[2] = glm::vec4(rot[2], 0.0f);
    child[3] = glm::vec4(worldPos, 1.0f);
    return child;
}

// ─── Rotor spin ─────────────────────────────────────────────────────────────
// A `Rotor` magic object (`src/rotor/rotor.c:88-107`, TASKTYPE_ROTOR) draws each
// blade copy from a local COPY of its own matrix:
//     `src/rotor/rotor.c:139-152`
//         copy = src->obj;  base = copy.matrix;
//         a = src->value * kSpin;                     // kSpin = 21.362831
//         for (i = 0; i < d->dir; i++) {
//             Matrix_SetRotX(&copy.matrix, a);
//             mat_mul(&t, &base, &copy.matrix); ...   // copy = base * RotX(a)
//             mat_mul(&t, &copy.matrix, &rz); ...     // copy = copy * RotZ(tilt)
//             draw(copy);
//             a += step;                              // step = speed * car speed
//         }
// So the spin is a POST-multiplied rotation about the part's own local X axis,
// and it is applied to the drawing copy only: the part's own matrix — which its
// children receive — stays unspun (`src/rotor/rotor.c:180-181` sets the child
// frame from `o->obj.matrix`). The editor therefore spins the rotor's own mesh
// and keeps passing the unspun frame down to nested attachments.
inline glm::mat4 IgiRotorSpinMatrix(const glm::mat4& base, float angle) {
    return base * glm::rotate(glm::mat4(1.0f), angle, glm::vec3(1.0f, 0.0f, 0.0f));
}

} // namespace igi
