// human_shadow.cpp - Human blob shadow quad projection.
#include "runtime/human_shadow.h"

#include <cmath>

namespace igi {

namespace {

void VectorCross(double out_result[3], const double vec_a[3], const double vec_b[3]) {
    out_result[0] = static_cast<double>(static_cast<long double>(vec_b[2]) * vec_a[1] - static_cast<long double>(vec_a[2]) * vec_b[1]);
    out_result[1] = static_cast<double>(static_cast<long double>(vec_a[2]) * vec_b[0] - static_cast<long double>(vec_b[2]) * vec_a[0]);
    out_result[2] = static_cast<double>(static_cast<long double>(vec_a[0]) * vec_b[1] - static_cast<long double>(vec_b[0]) * vec_a[1]);
}

void VectorNormalize(double vector[3]) {
    if (vector[0] == 0.0 && vector[1] == 0.0 && vector[2] == 0.0) return;
    const long double inv_len = 1.0L / sqrtl(static_cast<long double>(vector[0]) * vector[0] +
                                             static_cast<long double>(vector[1]) * vector[1] +
                                             static_cast<long double>(vector[2]) * vector[2]);
    vector[0] = static_cast<double>(static_cast<float>(vector[0] * inv_len));
    vector[1] = static_cast<double>(static_cast<float>(vector[1] * inv_len));
    vector[2] = static_cast<double>(static_cast<float>(vector[2] * inv_len));
}

}  // namespace

float HumanShadowFade(double height, double maxHeight) {
    long double fade_val = (1.0L - static_cast<long double>(height) / static_cast<long double>(maxHeight)) *
                           static_cast<long double>(kShadowFade);
    if (!(fade_val >= static_cast<long double>(kShadowMinFade)))
        fade_val = static_cast<long double>(kShadowMinFade);
    else if (fade_val < 0.0L)
        fade_val = 0.0L;
    else if (!(1.0L >= fade_val))
        fade_val = 1.0L;
    return static_cast<float>(fade_val);
}

void HumanShadowColor(float fade, float out_color[3]) {
    for (int axis = 0; axis < 3; ++axis)
        out_color[axis] = static_cast<float>(static_cast<long double>(fade) * static_cast<long double>(kShadowColor[axis]));
}

void HumanShadowBasis(const double up_vector[3], float out_matrix[9]) {
    double direction[3] = {up_vector[0], up_vector[1], up_vector[2]};
    double reference[3] = {0.0, 0.0, 0.0};
    double side_vector[3];
    double temp_vector_s[3];
    double temp_vector_t[3];
    double normalized_side[3];

    if (direction[0] == 0.0 && direction[1] == 0.0) {
        reference[0] = 0.0;
        reference[1] = 1.0;
    } else {
        reference[0] = direction[1];
        reference[1] = -direction[0];
        reference[2] = 0.0;
        VectorNormalize(reference);
    }
    reference[2] = 0.0;

    VectorCross(side_vector, reference, direction);
    normalized_side[0] = side_vector[0];
    normalized_side[1] = side_vector[1];
    normalized_side[2] = side_vector[2];
    VectorNormalize(normalized_side);

    VectorCross(temp_vector_s, direction, side_vector);
    VectorCross(temp_vector_t, normalized_side, temp_vector_s);

    VectorNormalize(temp_vector_s);
    VectorNormalize(temp_vector_t);

    for (int axis = 0; axis < 3; ++axis) {
        out_matrix[axis] = static_cast<float>(normalized_side[axis]);
        out_matrix[3 + axis] = static_cast<float>(temp_vector_s[axis]);
        out_matrix[6 + axis] = static_cast<float>(temp_vector_t[axis]);
    }
}

void HumanShadowQuad(const double up_vector[3], float size, float depth, float out_verts[4][3]) {
    float basis_matrix[9];
    HumanShadowBasis(up_vector, basis_matrix);

    const float half_size = size;
    const long double neg_size = -static_cast<long double>(half_size);
    const float depth_offset_0 = basis_matrix[6] * depth;
    const float depth_offset_1 = basis_matrix[7] * depth;
    const float depth_offset_2 = basis_matrix[8] * depth;
    const float up_term_0 = half_size * basis_matrix[3];
    const float up_term_1 = half_size * basis_matrix[4];
    const float up_term_2 = half_size * basis_matrix[5];
    long double pos_term_0 = static_cast<long double>(half_size) * basis_matrix[0];
    long double pos_term_1 = static_cast<long double>(half_size) * basis_matrix[1];
    long double pos_term_2 = static_cast<long double>(half_size) * basis_matrix[2];
    const long double corner_0_term_0 = pos_term_0;
    const long double corner_0_term_1 = pos_term_1;
    const long double corner_0_term_2 = pos_term_2;

    out_verts[0][0] = static_cast<float>(pos_term_0 + up_term_0 + depth_offset_0);
    out_verts[0][1] = static_cast<float>(pos_term_1 + up_term_1 + depth_offset_1);
    out_verts[0][2] = static_cast<float>(pos_term_2 + up_term_2 + depth_offset_2);

    pos_term_0 = neg_size * basis_matrix[0];
    pos_term_1 = neg_size * basis_matrix[1];
    pos_term_2 = neg_size * basis_matrix[2];
    const long double corner_1_term_0 = pos_term_0;
    const long double corner_1_term_1 = pos_term_1;
    const long double corner_1_term_2 = pos_term_2;
    out_verts[1][0] = static_cast<float>(pos_term_0 + up_term_0 + depth_offset_0);
    out_verts[1][1] = static_cast<float>(pos_term_1 + up_term_1 + depth_offset_1);
    out_verts[1][2] = static_cast<float>(pos_term_2 + up_term_2 + depth_offset_2);

    const long double down_term_0 = neg_size * basis_matrix[3];
    const long double down_term_1 = neg_size * basis_matrix[4];
    const long double down_term_2 = neg_size * basis_matrix[5];
    out_verts[2][0] = static_cast<float>(corner_0_term_0 + down_term_0 + depth_offset_0);
    out_verts[2][1] = static_cast<float>(corner_0_term_1 + down_term_1 + depth_offset_1);
    out_verts[2][2] = static_cast<float>(corner_0_term_2 + down_term_2 + depth_offset_2);
    out_verts[3][0] = static_cast<float>(corner_1_term_0 + down_term_0 + depth_offset_0);
    out_verts[3][1] = static_cast<float>(corner_1_term_1 + down_term_1 + depth_offset_1);
    out_verts[3][2] = static_cast<float>(corner_1_term_2 + down_term_2 + depth_offset_2);
}

HumanShadowTriangles HumanShadowBuildTriangles(const float verts[4][3]) {
    HumanShadowTriangles tri{};
    const int idx[2][3] = {{0, 1, 2}, {1, 3, 2}};
    const float uv[2][3][2] = {
        {{0.0f, 0.0f}, {1.0f, 0.0f}, {0.0f, 1.0f}},
        {{1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}},
    };

    for (int t = 0; t < 2; ++t) {
        for (int v = 0; v < 3; ++v) {
            for (int c = 0; c < 3; ++c)
                tri.pos[t][v][c] = verts[idx[t][v]][c];
            tri.uv[t][v][0] = uv[t][v][0];
            tri.uv[t][v][1] = uv[t][v][1];
        }
    }
    return tri;
}

bool IsHumanShadowOwner(const std::string& type) {
    return type == "Human" || type == "HumanSoldier" || type == "HumanSoldierPistol" ||
           type == "HumanSoldierRPG" || type == "HumanPlayer" ||
           type == "HumanSoldierFemale";
}

HumanShadowPlacement BuildHumanShadowPlacement(double x, double y, double placement_z,
                                               bool have_ground, double ground_z) {
    HumanShadowPlacement placement{};

    const double gz = have_ground ? ground_z : placement_z;
    placement.ground_pos[0] = x;
    placement.ground_pos[1] = y;
    placement.ground_pos[2] = gz;

    placement.fade = HumanShadowFade(placement_z - gz, static_cast<double>(kShadowMaxHeight));
    HumanShadowColor(placement.fade, placement.color);

    const double up_vector[3] = {0.0, 0.0, 1.0};
    HumanShadowQuad(up_vector, kShadowQuadSize, kShadowDepth, placement.verts);

    placement.valid = true;
    return placement;
}

}  // namespace igi
