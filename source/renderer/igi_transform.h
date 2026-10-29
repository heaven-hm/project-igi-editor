#pragma once

// Canonical IGI object transform maths, taken from the retail decompilation.
//
// Evidence (IGI-RE, the verified decompilation of the retail IGI.exe):
//   * `src/math/matrix.c` — `Matrix_SetEuler`, HOOK(0x4B38E0):
//
//         Matrix_SetRotZ(m, az);
//         Matrix_SetRotY(&r, ay);
//         Mul(m, m, &r);        // m = m * r
//         Matrix_SetRotX(&r, ax);
//         Mul(m, m, &r);        // m = m * r
//
//     so the game composes `Rz(az) * Ry(ay) * Rx(ax)` — i.e. X is applied to a
//     point first, then Y, then Z. `Mul` is `out = a * b` in the row-major 3x3
//     layout used by `Matrix` (`src/math/matrix.c:10-25`), and `Matrix_SetRotX/Y/Z`
//     build the ordinary right-handed rotation matrices for column vectors.
//   * `src/physobj/paramops.c` — `ParamReal32x9_Parse`, HOOK(0x4EFBA0), reads the
//     three authored QSC script arguments in order `x, y, z` and calls
//     `Matrix_SetEuler(m, x, y, z)`. So a level object's authored rotation triple
//     maps straight onto `(ax, ay, az)`; no permutation and no sign flip.
//   * `src/camera/humancamera.c:393` — `Matrix_SetEuler(&e, c->pitch, 0, c->yaw)`
//     confirms the axis convention: X is pitch, Z is yaw (the game's world is Z-up).
//
// `glm::rotate(m, angle, axis)` returns `m * R(axis, angle)`, so chaining
// Z, Y, X reproduces the game's product order exactly. Before this header the
// editor used `Z, X, Y` for most models and a hand-maintained `Z, Y, X`
// whitelist (`IsZyxEulerModel`) for two model prefixes, which is why multi-axis
// authored objects (issue #37) were mis-oriented.

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace igi {

// Rotation matrix of an authored IGI Euler triple: Rz * Ry * Rx.
inline glm::mat4 IgiEulerRotationMatrix(const glm::dvec3& rot) {
    glm::mat4 m(1.0f);
    m = glm::rotate(m, static_cast<float>(rot.z), glm::vec3(0.0f, 0.0f, 1.0f));
    m = glm::rotate(m, static_cast<float>(rot.y), glm::vec3(0.0f, 1.0f, 0.0f));
    m = glm::rotate(m, static_cast<float>(rot.x), glm::vec3(1.0f, 0.0f, 0.0f));
    return m;
}

// Same rotation as a 3x3, for normal/offset transforms.
inline glm::mat3 IgiEulerRotationMat3(const glm::dvec3& rot) {
    return glm::mat3(IgiEulerRotationMatrix(rot));
}

// Inverse of `IgiEulerRotationMat3`: the authored Euler triple of a rotation.
// This is a transcription of the engine's own decomposition, `Matrix_GetEuler`
// (HOOK 0x4B3C50, src/math/matrix.c:181-208), which works on the row-major 3x3
// `e = m->m`:
//
//     cy = sqrt(e[0]*e[0] + e[3]*e[3]);
//     if (cy > 0.0001) { rx = Atan2(e[7], e[8]); y = Atan2(-e[6], cy); rz = Atan2(e[3], e[0]); }
//     else             { rx = Atan2(-e[5], e[4]); y = Atan2(-e[6], cy); rz = 0; }
//
// `e[0]=M00 e[3]=M10 e[5]=M12 e[6]=M20 e[7]=M21 e[8]=M22` in row-major order, and
// GLM stores column-major, so `M_engine[r][c] == M_glm[c][r]`. The 0.0001 gimbal
// guard is the engine's, not an invention.
//
// The engine additionally maps a negative `rx`/`ry` into [0, 2*pi) and leaves
// `rz` in (-pi, pi]; that is a no-op modulo 2*pi for any caller that rebuilds a
// matrix from the result, so it is not reproduced here. A caller that compares
// raw numbers against the engine must apply it.
inline glm::dvec3 IgiMatrixToEuler(const glm::mat3& m) {
    // e[0]=M00, e[3]=M10 (row-major) -> glm m[0][0], m[0][1] (column-major).
    const double cy = std::sqrt(static_cast<double>(m[0][0]) * m[0][0] +
                                static_cast<double>(m[0][1]) * m[0][1]);
    double rx, ry, rz;
    if (cy > 0.0001) {
        rx = std::atan2(static_cast<double>(m[1][2]), static_cast<double>(m[2][2]));
        ry = std::atan2(-static_cast<double>(m[0][2]), cy);
        rz = std::atan2(static_cast<double>(m[0][1]), static_cast<double>(m[0][0]));
    } else {
        rx = std::atan2(-static_cast<double>(m[2][1]), static_cast<double>(m[1][1]));
        ry = std::atan2(-static_cast<double>(m[0][2]), cy);
        rz = 0.0;
    }
    return glm::dvec3(rx, ry, rz);
}

// The game's object placement is Scale, then Rotate, then Translate (SRT):
// `Matrix_SetScale` (HOOK 0x4B3C20) writes the scale into a rotation matrix that
// is then composed with the object's Euler matrix and translation.
//
// `total_scale` is the whole scale factor, exactly as in `IgiObjectMatrix` below
// (callers that mean "the editor's model scale" pass `40.96f * obj.scale`).
//
// `model_origin_metres` is a translation in the MODEL's own space -- the same
// space and the same units the mesh vertices are in -- applied inside the scale,
// so it contributes `R * total_scale * model_origin_metres` to the world
// position. A caller with no such offset passes the zero vector (the
// three-argument form below).
inline glm::mat4 IgiObjectMatrixAtModelOrigin(const glm::dvec3& pos, const glm::dvec3& rot,
                                              float total_scale,
                                              const glm::vec3& model_origin_metres) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(pos));
    m = m * IgiEulerRotationMatrix(rot);
    m = glm::scale(m, glm::vec3(total_scale));
    m = glm::translate(m, model_origin_metres);
    return m;
}

inline glm::mat4 IgiObjectMatrix(const glm::dvec3& pos, const glm::dvec3& rot, float scale) {
    return IgiObjectMatrixAtModelOrigin(pos, rot, scale, glm::vec3(0.0f));
}

}  // namespace igi
