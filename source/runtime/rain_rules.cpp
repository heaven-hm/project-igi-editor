#include "runtime/rain_rules.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace igi::rain {
namespace {

// rain.c:683-684 DATA(534278) kLayerInv = 4.0690104166666664e-05 (= 1/24576),
// kLayerOrigin = 36864.0 (= 1.5 * kLayer). Rain_Draw (rain.c:283) uses them to
// snap the camera's z to the layer lattice.
constexpr double kLayerInv = 4.0690104166666664e-05;
constexpr double kLayerOrigin = 36864.0;

// rain.c:223-224 kGridOrigin and kTileInv, the grid snap. kTileInv lives in the
// header (rain.c:679, kTileInvUnits) so the cell parity test below can share the
// engine's exact multiply.

// Matches Level::LoadFogInfo / level_weather.cpp: [0]=task id, [1]=type,
// [2]=name, [3]=declared parameter 0.
constexpr std::size_t kFirstParam = 3;

std::string NormalizeToken(std::string token) {
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front())))
        token.erase(token.begin());
    while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back())))
        token.pop_back();
    if (token.size() >= 2 && token.front() == '"' && token.back() == '"')
        token = token.substr(1, token.size() - 2);
    std::string decoded;
    decoded.reserve(token.size());
    for (std::size_t i = 0; i < token.size(); ++i) {
        if (token[i] == '\\' && i + 1 < token.size()) {
            switch (token[i + 1]) {
            case 'n': decoded.push_back('\n'); ++i; continue;
            case 'r': decoded.push_back('\r'); ++i; continue;
            case 't': decoded.push_back('\t'); ++i; continue;
            default: break;
            }
        }
        decoded.push_back(token[i]);
    }
    while (!decoded.empty() && std::isspace(static_cast<unsigned char>(decoded.back())))
        decoded.pop_back();
    for (char& c : decoded) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return decoded;
}

std::optional<bool> ParseBool(const std::string& raw) {
    const std::string token = NormalizeToken(raw);
    if (token == "TRUE") return true;
    if (token == "FALSE") return false;
    char* end = nullptr;
    const long value = std::strtol(token.c_str(), &end, 10);
    if (end == token.c_str() || *end != '\0') return std::nullopt;
    return value != 0;
}

std::optional<float> ParseFloat(const std::string& raw) {
    char* end = nullptr;
    const float value = std::strtof(raw.c_str(), &end);
    if (end == raw.c_str()) return std::nullopt;
    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) ++end;
    if (*end != '\0') return std::nullopt;
    if (!std::isfinite(value)) return std::nullopt;
    return value;
}

const std::string& TokenAt(const std::vector<std::string>& tokens, std::size_t index) {
    static const std::string kEmpty;
    if (index >= tokens.size()) return kEmpty;
    return tokens[index];
}

}  // namespace

// ---------------------------------------------------------------------------
// R1
// ---------------------------------------------------------------------------

int AlphaByte(float authored_alpha) {
    // Guard the float->int conversion only; the arithmetic below is the engine's
    // verbatim expression (rain.c:648).
    if (!std::isfinite(authored_alpha)) return 0;
    if (authored_alpha < -1.0e9f || authored_alpha > 1.0e9f) return 0;

    // x87_fistp with round-toward-zero: FPU_SetTruncate (dxinit.c:75-80) is
    // called from AppMain_WinMain (win/appmain.c:212) before the game loop.
    const int n = static_cast<int>(authored_alpha * kAlphaScale);
    const std::uint32_t shifted = static_cast<std::uint32_t>(n) << 24;
    const std::uint32_t color = 0x00FFFFFFu - shifted;
    return static_cast<int>((color >> 24) & 0xFFu);
}

// ---------------------------------------------------------------------------
// R2
// ---------------------------------------------------------------------------

float SpeedUnitsPerTick(bool is_snow) {
    return is_snow ? kSnowSpeedUnitsPerTick : kRainSpeedUnitsPerTick;
}

int LifetimeTicks(bool is_snow) {
    const float speed = SpeedUnitsPerTick(is_snow);
    if (!(speed > 0.0f)) return 0;
    return static_cast<int>(kLayerUnits / speed + 0.5f);
}

int SpawnCountForTick(float speed_units_per_tick, float& accumulator, int drops_already) {
    float acc = speed_units_per_tick + accumulator;
    accumulator = acc;

    int remaining = kDropCapacity - drops_already;
    if (remaining < 0) remaining = 0;

    int spawned = 0;
    while (acc > kSpawnStep) {
        if (spawned >= remaining) break;  // rain.c:624 - pool full, accumulator kept
        ++spawned;
        acc = accumulator - kSpawnStep;
        accumulator = acc;
    }
    return spawned;
}

float WrapZ(float z_units, float speed_units_per_tick) {
    const float z = z_units - speed_units_per_tick;
    if (z < 0.0f) return z + kLayerUnits;  // rain.c:590-591
    return z;
}

// ---------------------------------------------------------------------------
// R3
// ---------------------------------------------------------------------------

GridPlacement PlaceGrid(double camera_x, double camera_y) {
    GridPlacement grid;
    const int ix = static_cast<int>(
        std::floor((camera_x - kGridOriginUnits) * kTileInvUnits + 0.5));
    const int iy = static_cast<int>(
        std::floor((camera_y - kGridOriginUnits) * kTileInvUnits + 0.5));

    int mx = ix % kGridHalfTiles;
    int my = iy % kGridHalfTiles;
    if (mx < 0) mx += kGridHalfTiles;
    if (my < 0) my += kGridHalfTiles;

    grid.tile_x = mx;
    grid.tile_y = my;
    grid.origin_x = static_cast<double>(ix - mx) * kTileUnits;
    grid.origin_y = static_cast<double>(iy - my) * kTileUnits;
    return grid;
}

double DrawTileX(const GridPlacement& grid, int window_index) {
    // Array row drawn by Rain_Draw (rain.c:282,288): cellX + 1 + index. The
    // world position of array row r is the fundamental row s = r mod 12, at
    // x0 + s*kTile, plus a 12-tile shift when s < mx (rain.c:249).
    const int r = grid.tile_x + kDrawWindowOffsetTiles + window_index;
    const int s = r % kGridHalfTiles;
    const double shift = (s < grid.tile_x) ? kGridHalfTiles * kTileUnits : 0.0;
    return grid.origin_x + static_cast<double>(s) * kTileUnits + shift;
}

double DrawTileY(const GridPlacement& grid, int window_index) {
    // Same mapping on y (rain.c:252-253).
    const int r = grid.tile_y + kDrawWindowOffsetTiles + window_index;
    const int s = r % kGridHalfTiles;
    const double shift = (s < grid.tile_y) ? kGridHalfTiles * kTileUnits : 0.0;
    return grid.origin_y + static_cast<double>(s) * kTileUnits + shift;
}

// ---------------------------------------------------------------------------
// R4
// ---------------------------------------------------------------------------

float FallDistanceUnits(float start_metres, float end_metres) {
    // RainEffect_Changed scales both authored values by 4096 (fxobj/rain.c:138-141)
    // and Rain_New adds them (rain.c:160).
    return (start_metres + end_metres) * kUnitsPerMetre;
}

float ProbeTopUnits(float camera_z, float start_metres) {
    return camera_z + start_metres * kUnitsPerMetre;
}

float LayerBaseZUnits(double camera_z, int layer) {
    if (layer < 0) layer = 0;
    if (layer >= kLayerCount) layer = kLayerCount - 1;

    // Rain_Draw rain.c:283: z = floor((pos.z - kLayerOrigin) * kLayerInv + 0.5) * kLayer.
    const double snapped =
        std::floor((camera_z - kLayerOrigin) * kLayerInv + 0.5) * static_cast<double>(kLayerUnits);

    // Rain_DrawCell rain.c:464-468: the first loop iteration (k = 3) subtracts one
    // kLayer from `snapped - camera.z + kTop` and is the HIGHEST layer, so the
    // lowest layer (index 0 here) subtracts kLayerCount * kLayer.
    const double base_offset =
        snapped - camera_z + static_cast<double>(kTopUnits) -
        static_cast<double>(kLayerCount - layer) * static_cast<double>(kLayerUnits);
    return static_cast<float>(base_offset);
}

// ---------------------------------------------------------------------------
// R5
// ---------------------------------------------------------------------------

float SnowSwayHalfRangeUnits() {
    // rain.c:597-598 stores (Random_Float() - 0.5) * kSway.
    return kSwayUnits * 0.5f;
}

int SnowSwayTicksMin() { return kSwayTicksMin; }
int SnowSwayTicksMax() { return kSwayTicksMax; }
float SnowFlakeSizeUnits() { return kFlakeSizeUnits; }
float RainStreakLengthUnits() { return kStreakUnits; }
int DropKindCount() { return kDropKindCount; }

// ---------------------------------------------------------------------------
// R6
// ---------------------------------------------------------------------------

AuthoredRainEffect ParseRainEffect(const std::string& type,
                                   const std::vector<std::string>& arg_tokens) {
    AuthoredRainEffect out;

    // Five declared parameters, at token indices 3..7 (fxobj/rain.c:77-81).
    if (type != "RainEffect") return out;
    if (arg_tokens.size() < kFirstParam + 5) return out;
    out.present = true;

    if (const std::optional<bool> is_rain = ParseBool(TokenAt(arg_tokens, kFirstParam + 0)))
        out.is_rain = *is_rain;

    if (const std::optional<float> start = ParseFloat(TokenAt(arg_tokens, kFirstParam + 1)))
        out.traceline_start_metres = *start;
    if (const std::optional<float> end = ParseFloat(TokenAt(arg_tokens, kFirstParam + 2)))
        out.traceline_end_metres = *end;

    out.active_expression = TokenAt(arg_tokens, kFirstParam + 3);
    if (const std::optional<bool> active = ParseBool(out.active_expression)) {
        out.falling = *active;
        out.active_is_literal = true;
    }

    if (const std::optional<float> alpha = ParseFloat(TokenAt(arg_tokens, kFirstParam + 4)))
        out.alpha = *alpha;

    return out;
}

// ---------------------------------------------------------------------------
// R7 - snow is rain's `snow` flag, not a separate system (dxinit/rain.c:153)
// ---------------------------------------------------------------------------

bool SnowFromAuthoredIsRain(bool authored_is_rain) {
    // rain.c:153 `r->snow = rain == 0`, with `rain` = RainEffect::unk24
    // (fxobj/rain.c:141). The authored parameter is therefore inverted.
    return !authored_is_rain;
}

float SpawnRatePerTick(bool is_snow) {
    // Derived from rain.c:619-636: `acc = speed + r->spawn` emits one particle
    // per whole kSpawnStep, so the steady-state rate is speed / kSpawnStep.
    return SpeedUnitsPerTick(is_snow) / kSpawnStep;
}

float SpawnZUnits(bool is_snow) {
    // rain.c:631 `d->z = kLayer - speed`, shared by both modes but speed differs.
    return kLayerUnits - SpeedUnitsPerTick(is_snow);
}

float SnowSwayDisplacementUnits(float random_float) {
    // rain.c:597-598, verbatim: (Random_Float() - kHalfF) * kSway.
    return (random_float - 0.5f) * kSwayUnits;
}

int SnowSwayReloadTicks(int random_int_30) {
    // rain.c:596. Random_Int(r, 30) == Random_Next() % 30 (math/random.c:28-31),
    // so the draw is in [0, 29]; the clamp is a guard, not engine behaviour.
    if (random_int_30 < 0) random_int_30 = 0;
    if (random_int_30 > 29) random_int_30 = 29;
    return random_int_30 + kSwayTicksMin;  // kSwayTicksMin == 30
}

int SnowSwayTimerAfterReload(int random_int_30) {
    // rain.c:595-596: `if (d->timer-- < 0) d->timer += Random_Int(30) + 30;`
    // The decrement has already happened when the add runs, so the old value -1
    // becomes -2 + (Random_Int(30) + 30) == Random_Int(30) + 28.
    return SnowSwayReloadTicks(random_int_30) - 2;
}

int SnowSwayFirstRollTick() {
    // rain.c:630 sets `d->timer = 0` at spawn; rain.c:595 tests `timer-- < 0`.
    // Update 1: 0 is not < 0, timer becomes -1. Update 2: -1 < 0, so the first
    // dx/dy roll happens here.
    return 2;
}

SnowTileWrap SnowWrapTileX(float x_units) {
    // rain.c:603-605 `if (!(x >= 0)) { d->x += kTileF; d->kind ^= 1; }`
    // rain.c:606-608 `else if (d->x > kTileF) { d->x -= kTileF; d->kind ^= 1; }`
    if (!(x_units >= 0.0f)) return {x_units + kTileUnitsF, true};
    if (x_units > kTileUnitsF) return {x_units - kTileUnitsF, true};
    return {x_units, false};
}

SnowTileWrap SnowWrapTileY(float y_units) {
    // rain.c:610-615, the y twin: kind ^= 2.
    if (!(y_units >= 0.0f)) return {y_units + kTileUnitsF, true};
    if (y_units > kTileUnitsF) return {y_units - kTileUnitsF, true};
    return {y_units, false};
}

int CellVertexGroup(double tile_x, double tile_y) {
    // rain.c:404-408, verbatim: q = (floor(y * kTileInv) & 1) << 1;
    // q |= floor(x * kTileInv) & 1. The multiply by the engine's reciprocal is
    // kept, not a division by kTileUnits (see kTileInvUnits).
    int q = (static_cast<int>(std::floor(tile_y * kTileInvUnits)) & 1) << 1;
    q |= static_cast<int>(std::floor(tile_x * kTileInvUnits)) & 1;
    return q;
}

float SnowFlakeWidthUnits() {
    // rain.c:341-342 `x0 = vx - size; x1 = vx + size` with size = fade * kFlakeSize.
    return 2.0f * kFlakeSizeUnits;
}

int SnowVerticesPerParticle() { return 6; }  // rain.c:364, 447
int RainVerticesPerParticle() { return 2; }  // rain.c:372, 458

std::uint32_t SnowFlakeDiffuse() { return 0xFFFFFFFFu; }     // rain.c:353
std::uint32_t RainStreakTopDiffuse() { return 0x00FFFFFFu; } // rain.c:366

const char* SnowTextureName() { return "snow.tex"; }  // fxobj/rain.c:82

int SnowTextureStageArgCountWithTexture() { return 4; }     // rain.c:434
int SnowTextureStageArgCountWithoutTexture() { return 3; }  // rain.c:437

std::span<const SnowRuleDifference> SnowRuleDifferences() {
    static const SnowRuleDifference kDifferences[] = {
        {"flag", "Rain_New", 0x498960u, "src/dxinit/rain.c:153"},
        {"speed", "Rain_Update", 0x499CF0u, "src/dxinit/rain.c:572"},
        {"spawn-z", "Rain_Update", 0x499CF0u, "src/dxinit/rain.c:631"},
        {"sway-and-wrap", "Rain_Update", 0x499CF0u, "src/dxinit/rain.c:592-617"},
        {"quad-geometry", "Rain_FillVB", 0x499090u, "src/dxinit/rain.c:337-364"},
        {"billboard-matrix", "Rain_FillVB", 0x499090u, "src/dxinit/rain.c:382-388"},
        {"texture-and-primitive", "Rain_DrawCell", 0x499430u, "src/dxinit/rain.c:430-447"},
        {"texture-load", "RainEffect_Init", 0x436780u, "src/fxobj/rain.c:82"},
    };
    return kDifferences;
}

// ---------------------------------------------------------------------------
// R8 - Rain_ClipCells / Rain_DrawCell's cell clipping
// ---------------------------------------------------------------------------

CellClip ClipCell(bool probed, double cell_x, double cell_y, double z,
                  double camera_x, double camera_y, double camera_z,
                  const ViewBasis& basis, const ClipViewport& viewport) {
    CellClip out;
    // rain.c:504 `unsigned flags = 0x1F;` then rain.c:506 `if (c->flags & 0x20)`.
    // A cell Rain_UpdateGrid never probed keeps the seeded word, whose bit 5 is
    // clear, so the block test at rain.c:290 can never pass for it.
    if (!probed) return out;

    // TransContext_ProjectView HOOK 0x498000 (src/transctx/transctx.c:272-278):
    //   k = 1 / v[2];  out[0] = v[0]*scaleX*k + centerX; out[1] = v[1]*scaleY*k + centerY.
    // The engine passes a FLOAT v[3] (rain.c:507) although the values were
    // computed in long double, so the reciprocal sees the rounded depth; the
    // casts below reproduce that (the cull is coarse, but the boundary cases
    // are the same).
    const auto project = [&viewport](double view_x, double view_y, double depth,
                                     double* sx, double* sy) {
        const double k = 1.0 / static_cast<double>(static_cast<float>(depth));
        *sx = static_cast<double>(static_cast<float>(view_x)) * viewport.scale_x * k +
              viewport.center_x;
        *sy = static_cast<double>(static_cast<float>(view_y)) * viewport.scale_y * k +
              viewport.center_y;
    };

    unsigned flags;
    double ground_depth;
    double top_depth;

    // The ground point: rain.c:508-527.
    {
        const double px = cell_x - camera_x;
        const double py = cell_y - camera_y;
        const double pz = z - camera_z;
        const double view_x = basis.m[0] * px + basis.m[1] * py + basis.m[2] * pz;
        const double view_y = basis.m[3] * px + basis.m[4] * py + basis.m[5] * pz;
        ground_depth = basis.m[6] * px + basis.m[7] * py + basis.m[8] * pz;

        if (ground_depth > 0.0) {
            double sx = 0.0, sy = 0.0;
            project(view_x, view_y, ground_depth, &sx, &sy);
            // rain.c:518-523. The first statement ASSIGNS (0x3F-based), so bit 5
            // survives; the rest AND.
            flags = (sx > 0.0 ? ~1u : ~0u) & 0x3Fu;
            if (sy > 0.0) flags &= ~2u;
            if (!(sx >= viewport.width)) flags &= ~4u;
            flags &= !(sy >= viewport.height) ? ~0x18u : ~0x10u;
        } else {
            // rain.c:525-526 - behind the camera: the horizontal test mirrors
            // onto the opposite edge and bit 4 (behind) stays set.
            flags = (view_x > 0.0 ? ~1u : ~4u) & 0x3Fu;
            flags &= view_y > 0.0 ? ~2u : ~8u;
        }
    }

    // The point kTop above it: rain.c:528-548. These tests only AND, so both
    // points must be inside for the word to reach 0x20.
    {
        const double z2 = z + kTopUnits;
        const double px = cell_x - camera_x;
        const double py = cell_y - camera_y;
        const double pz = z2 - camera_z;
        const double view_x = basis.m[0] * px + basis.m[1] * py + basis.m[2] * pz;
        const double view_y = basis.m[3] * px + basis.m[4] * py + basis.m[5] * pz;
        top_depth = basis.m[6] * px + basis.m[7] * py + basis.m[8] * pz;

        if (top_depth > 0.0) {
            double sx = 0.0, sy = 0.0;
            project(view_x, view_y, top_depth, &sx, &sy);
            if (sx > 0.0) flags &= ~1u;
            if (sy > 0.0) flags &= ~2u;
            if (!(sx >= viewport.width)) flags &= ~4u;
            flags &= !(sy >= viewport.height) ? ~0x18u : ~0x10u;
        } else {
            flags &= view_x > 0.0 ? ~1u : ~4u;
            flags &= view_y > 0.0 ? ~2u : ~8u;
        }
    }

    out.flags = flags;
    // rain.c:549 `c->depth = (u[2] + v[2]) * kHalfF` - the mean view depth of
    // the two points, the key Rain_Draw sorts the cells by (rain.c:293-296).
    out.depth = (top_depth + ground_depth) * 0.5;
    return out;
}

double SnapLayerZ(double camera_z) {
    // rain.c:283 `crt_floor((pos.z - kLayerOrigin) * kLayerInv + kHalfD) * kLayer`
    // with kLayerOrigin = 36864.0, kLayerInv = 4.0690104166666664e-05 and
    // kLayer = 24576.0 (rain.c:681-684).
    return std::floor((camera_z - kLayerOrigin) * kLayerInv + 0.5) *
           static_cast<double>(kLayerUnits);
}

double LayerClipBase(double max_cell_z, double snapped_layer_z) {
    // rain.c:402 `float h = (a > b ? a : b) - it->z - kTopF;` where it->z is the
    // snapped camera z Rain_Draw passed in (rain.c:283,293).
    return max_cell_z - snapped_layer_z - static_cast<double>(kTopUnits);
}

int LayerClipVertexCount(double hl, const float* drop_z, int count) {
    // rain.c:466-484, verbatim:
    //   r->matrix[14] -= gWorldScale * kLayer;  hl = h + kLayer;  h = hl;
    //   if (!(hl >= 0))            n = list->count;
    //   else if (!(h > kLayer))    for (n = 0; n < list->count; n++)
    //                                  if (!(list->z[n] > h)) break;
    //   else                       n = 0;
    // `hl` is already h + kLayer for this layer. The negated comparisons keep
    // the engine's NaN behaviour (a NaN hl falls into the `n = count` arm).
    if (!(hl >= 0.0)) return count;
    if (!(hl > static_cast<double>(kLayerUnits))) {
        int n = 0;
        while (n < count && !(static_cast<double>(drop_z[n]) > hl)) ++n;
        return n;
    }
    return 0;
}

int ProbeBudgetForFrame(bool has_last, double camera_x, double camera_y,
                        double last_x, double last_y) {
    // rain.c:245-253. `budget` bounds how many cells Rain_UpdateGrid may drop a
    // ray through this frame; the first update probes everything it can.
    if (!has_last) return 10000;
    // dx is long double, dy is truncated to float - the engine's own types
    // (rain.c:249-252).
    const double dx = std::fabs(static_cast<long double>(camera_x) - last_x);
    const float dy = static_cast<float>(std::fabs(static_cast<long double>(camera_y) - last_y));
    int budget = static_cast<int>(static_cast<long long>(std::ceil(
        dx * static_cast<double>(kTileInvUnitsF) * static_cast<double>(kGridHalfTiles))));
    budget += static_cast<int>(static_cast<long long>(std::ceil(
        static_cast<double>(dy) * static_cast<double>(kTileInvUnitsF) *
        static_cast<double>(kGridHalfTiles))));
    return budget;
}

std::uint32_t RainStreakBottomDiffuse(float authored_alpha) {
    // rain.c:648 `r->color = 0xFFFFFF - ((unsigned)x87_fistp(alpha * kAlphaScale) << 24)`,
    // with kAlphaScale = -255.0f (rain.c:691, polylist.c:432-434) and FISTP
    // truncating (FPU_SetTruncate, dxinit.c:75-80). The subtraction is the
    // 32-bit word: the top byte becomes trunc(alpha * 255), the RGB stays white.
    const int n = static_cast<int>(authored_alpha * kAlphaScale);
    const std::uint32_t shifted = static_cast<std::uint32_t>(n) << 24;
    return 0x00FFFFFFu - shifted;
}

int RainStageArgCount() { return 3; }   // rain.c:454-455, SELECTARG2
bool RainBindsTexture() { return false; }  // rain.c:448-449, SetTexture(device, 0, NULL)

}  // namespace igi::rain
