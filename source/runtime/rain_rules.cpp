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

// rain.c:223-224 kGridOrigin and kTileInv, the grid snap. kTileInv is
// 9.178218688807207e-05 (rain.c:679) = 1/kTile.
constexpr double kTileInv = 9.178218688807207e-05;

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
        std::floor((camera_x - kGridOriginUnits) * kTileInv + 0.5));
    const int iy = static_cast<int>(
        std::floor((camera_y - kGridOriginUnits) * kTileInv + 0.5));

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

}  // namespace igi::rain
