// atta_children.h - Model attachment and child node transforms.
//
// MIT License.
#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace igi {

// ─── ATTA record layout ─────────────────────────────────────────────────────
// Attachment layout: name[16], position[3], orientation[9], padding/tail[4].
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
inline glm::mat3 IgiAttaLocalRotation(const float m[9]) {
    return glm::mat3(m[0], m[1], m[2],
                     m[3], m[4], m[5],
                     m[6], m[7], m[8]);
}

// ─── Engine Matrix -> GLM world rotation ────────────────────────────────────
inline glm::mat3 IgiEngineMatrixToGlm(const float m[9]) {
    return glm::mat3(m[0], m[3], m[6],
                     m[1], m[4], m[7],
                     m[2], m[5], m[8]);
}

// The same basis as a translation-free world transform, for the draw path.
inline glm::mat4 IgiEngineMatrixToGlm4(const float m[9]) {
    const glm::mat3 rot = IgiEngineMatrixToGlm(m);
    glm::mat4 out(1.0f);
    out[0] = glm::vec4(rot[0], 0.0f);
    out[1] = glm::vec4(rot[1], 0.0f);
    out[2] = glm::vec4(rot[2], 0.0f);
    return out;
}

// ─── child_world = parent_world * child_local ───────────────────────────────
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
inline glm::mat4 IgiRotorSpinMatrix(const glm::mat4& base, float angle) {
    return base * glm::rotate(glm::mat4(1.0f), angle, glm::vec3(1.0f, 0.0f, 0.0f));
}

} // namespace igi
