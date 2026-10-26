// rain_rules.h - the retail weather-particle rules of IGI's "Rain" task, extracted
// from IGI-RE as pure arithmetic.
//
// NOT WIRED. Nothing in the editor calls this module yet. The editor's live rain
// path is source/renderer/renderer_rain.cpp (a GPU vertex-shader particle field)
// fed by source/runtime/level_weather.cpp; that path is owned elsewhere and was
// deliberately not modified. This module exists so the retail rules are recorded
// as executable, tested arithmetic instead of prose, and so the divergences in
// docs/igi-re-parity/RAIN.md are pinned by tests rather than asserted.
//
// Every constant and every rule below is cited to IGI-RE
// (/Users/haseeb-mir/Documents/Code/IGI-RE), function + address + file:line.
// open-igi (C#) is not an authority and was not consulted.
//
// Two distinct precipitation systems exist in the oracle:
//   * `Rain` / `Rain_*`  - HOOK 0x498960..0x499FE0, src/dxinit/rain.c. A
//     camera-anchored grid of falling streaks/flakes owned by the `RainEffect`
//     level task (src/fxobj/rain.c). This is what this module ports.
//   * `RainObj_*`        - HOOK 0x4F65E0.., src/cutscene/rain.c. A world-space
//     DynCube volume with its own 11-parameter schema ("RainDynCubeObj"). A
//     different feature; not ported here.
// IGI-RE contains NO symbol named `Snow`; `Rain_New`'s `snow` flag (rain.c:153)
// is the only snow path, selected by the authored "Is Rain" == FALSE.
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace igi::rain {

// ---------------------------------------------------------------------------
// Engine constants. Every one is IGI-RE DATA at the address shown.
// ---------------------------------------------------------------------------

// Game units per metre (mathvars.h:6-7 `extern const float k4096`; rain.c:675-676
// stores the tile as 10895.36 units == 2.66 m, and the cutscene rain uses
// `k4096D = 4096.0` at cutscene/rain.c:168).
constexpr float kUnitsPerMetre = 4096.0f;

// rain.c:122-123. The ONLY vertical motion term: drops move by this many units
// per Rain_Update call. There is no gravity and no acceleration.
constexpr float kRainSpeedUnitsPerTick = 2048.0f;  // == 0.5 m/tick
constexpr float kSnowSpeedUnitsPerTick = 204.8f;   // == 0.05 m/tick

// rain.c:671 DATA(5334C0) kFadeStep = 0.016666668f == 1/60. `fade` ramps to
// 0 or 1 by one step per tick (rain.c:574-583), so a full fade is 60 ticks.
constexpr float kFadeStep = 0.016666668f;
constexpr int kFadeTicks = 60;

// rain.c:65 `RainDrop drops[80]` and rain.c:624 `if (r->dropCount == 80) return;`
// - the spawn pool is a fixed 80 and is never compacted.
constexpr int kDropCapacity = 80;

// rain.c:689 DATA kSpawnStep = 307.2f. The spawn accumulator adds `speed` each
// tick and emits one drop per whole 307.2 units.
constexpr float kSpawnStep = 307.2f;

// rain.c:681 DATA kLayer = 24576.0f == 6 m. A drop that falls below z = 0 wraps
// to z + kLayer (rain.c:590-591): the wrap band is always 6 m tall and is
// independent of the authored Traceline start/end.
constexpr float kLayerUnits = 24576.0f;

// rain.c:687 DATA kTop = kTopF = 73728.0f == 18 m == 3 * kLayer. Rain_DrawCell
// (rain.c:464-468) draws three kLayer-tall layers whose bases span this range.
constexpr int kLayerCount = 3;
constexpr float kTopUnits = 73728.0f;

// rain.c:678-680 DATA kTile = 10895.3603515625, kTileF = 10895.36,
// kGridOrigin = 59924.48193359375. The drop grid is a lattice of these tiles.
constexpr double kTileUnits = 10895.3603515625;
constexpr float kTileUnitsF = 10895.36f;
constexpr double kGridOriginUnits = 59924.48193359375;

// rain.c:674 DATA kTwelve = 12.0f and rain.c:675 kTwoTiles = 130744.32421875
// (== 12 * kTile despite the name). The 24x24 cell array (rain.c:67) is a 2x2
// mirror of a 12x12 fundamental block, so the world content repeats every 12
// tiles (rain.c:249,253 add the 12-tile shift for i < mx / j < my).
constexpr int kGridHalfTiles = 12;
constexpr int kGridTiles = 24;

// Rain_Draw (rain.c:282,288-289) starts at array cell (cellX+1, cellY+1) and
// walks a 9x9 window. Derived: the drawn world tiles are always the nine
// consecutive tiles (ix+1 .. ix+9) in x and (iy+1 .. iy+9) in y, where (ix, iy)
// is the camera's snapped tile from PlaceGrid below.
constexpr int kDrawWindowOffsetTiles = 1;
constexpr int kDrawWindowTiles = 9;

// rain.c:671 DATA(533688) kStreak = 2048.0f - the rain streak length, applied as
// `len = fade * kStreak` (rain.c:311).
constexpr float kStreakUnits = 2048.0f;

// rain.c:685 DATA kFlakeSize = 81.92f - the snow quad half-extent, applied as
// `size = fade * kFlakeSize` (rain.c:309).
constexpr float kFlakeSizeUnits = 81.92f;

// rain.c:691 DATA kSway = 136.53334f, and rain.c:595-598: a snow flake re-rolls
// its (dx, dy) every Random_Int(30)+30 ticks, i.e. every 30..59 ticks, drawing
// each component from (Random_Float() - 0.5) * kSway, so dx, dy lie in
// (-kSway/2, +kSway/2).
constexpr float kSwayUnits = 136.53334f;
constexpr int kSwayTicksMin = 30;
constexpr int kSwayTicksMax = 59;

// rain.c:632 `d->kind = Random_Int(gDxRainRandom, 4)` - four vertex groups,
// selected by which tile edge a snow flake has wrapped across (rain.c:604-616).
constexpr int kDropKindCount = 4;

// kAlphaScale is ALIAS(kAlphaScale, kMinus255) (polylist.c:434) and
// AT(kAlphaScale, 0x5342D8) (addr.c:286); kMinus255 = -255.0f
// (polylist.c:432). Used by Rain_SetAlpha (rain.c:646-649).
constexpr float kAlphaScale = -255.0f;

// The authored "Traceline start"/"Traceline end" and "Rain Alpha" ParamTable
// ranges (RainEffect_Init, fxobj/rain.c:64-67,79,81). They are stored by
// ParamTable_Add into the entry's `extra` slot and nothing in src/param/param.c
// reads them back - they are editor-UI metadata, NOT load-time clamps, so this
// module does not clamp.
constexpr float kTracelineMinMetres = 5.0f;
constexpr float kTracelineMaxMetres = 150.0f;
constexpr float kRainLevelMin = 0.0f;
constexpr float kRainLevelMax = 1.0f;

// ---------------------------------------------------------------------------
// R1 - Rain_SetAlpha, HOOK 0x499FA0 (src/dxinit/rain.c:646-649)
// ---------------------------------------------------------------------------
//
//   r->color = 0xFFFFFF - ((unsigned)fistp(alpha * kAlphaScale) << 24);
//
// `fistp` is x87 FISTP with the current rounding mode (dxinit.h:22-29). The game
// runs with round-toward-zero: FPU_SetTruncate (HOOK 0x498460, dxinit.c:75-80)
// sets RC = 11 and is called once from AppMain_WinMain before the game loop
// (win/appmain.c:212). So the product truncates.
//
// Derived and test-pinned: the bit trick yields an ARGB colour whose alpha byte
// is exactly trunc(alpha * 255), and whose RGB is white. See
// docs/igi-re-parity/RAIN.md section 3 for the derivation.
int AlphaByte(float authored_alpha);

// ---------------------------------------------------------------------------
// R2 - Rain_Update, HOOK 0x499CF0 (src/dxinit/rain.c:565-637)
// ---------------------------------------------------------------------------

// rain.c:572 - `speed = r->snow ? kSnowSpeed : kRainSpeed`.
float SpeedUnitsPerTick(bool is_snow);

// Derived from R2's wrap: kLayerUnits / speed, i.e. 12 ticks for rain and 120
// for snow. A drop is never destroyed; it wraps (rain.c:590-591).
int LifetimeTicks(bool is_snow);

// rain.c:619-636. `accumulator` is Rain::spawn (rain.c:71). Returns how many
// drops this tick spawns and leaves the unspent remainder in `accumulator`,
// matching `acc = speed + r->spawn; r->spawn = acc; while (acc > kSpawnStep)`.
// Steady state is speed / kSpawnStep drops per tick: 6.667 rain, 0.667 snow.
// `drops_already` is Rain::dropCount; at kDropCapacity the engine returns from
// Rain_Update without draining the accumulator (rain.c:624-625), which the
// returned count and the surviving accumulator reproduce.
int SpawnCountForTick(float speed_units_per_tick, float& accumulator, int drops_already);

// rain.c:587-591 - `z = d->z - speed; if (z < 0) d->z = z + kLayer;`.
float WrapZ(float z_units, float speed_units_per_tick);

// ---------------------------------------------------------------------------
// R3 - Rain_UpdateGrid / Rain_Draw, HOOK 0x498BD0 (rain.c:220-278) and
//      HOOK 0x498F10 (rain.c:280-303)
// ---------------------------------------------------------------------------

// The camera-anchored tile lattice. `origin_x/origin_y` are the world x/y of
// array tile (0, 0) in the fundamental 12x12 block; `tile_x/tile_y` are the
// camera's tile indices modulo 12 (rain.c:223-236). The camera sits within half
// a tile of (tile_x, tile_y)'s world origin.
struct GridPlacement {
    double origin_x = 0.0;
    double origin_y = 0.0;
    int tile_x = 0;
    int tile_y = 0;
};

GridPlacement PlaceGrid(double camera_x, double camera_y);

// The world x of drawn window column/row `window_index` in [0, 9), i.e. the
// world tile (camera tile + 1 + window_index). Derived in RAIN.md section 5.
double DrawTileX(const GridPlacement& grid, int window_index);
double DrawTileY(const GridPlacement& grid, int window_index);

// ---------------------------------------------------------------------------
// R4 - the authored height/depth and the vertical layer band
// ---------------------------------------------------------------------------

// Rain_New HOOK 0x498960 (rain.c:159-160): `r->height = height;
// r->fallDist = height + depth;` where RainEffect_Changed (fxobj/rain.c:138-141)
// passes `height = Traceline start * 4096`, `depth = Traceline end * 4096`.
// Rain_UpdateGrid probes each cell DOWN this far (rain.c:258,263-265), so the
// two authored values only size the ground raycast - they do not move the drops.
float FallDistanceUnits(float start_metres, float end_metres);

// rain.c:263 - `c->z = gTransContext.pos.z + r->height`: the raycast starts this
// far above the camera.
float ProbeTopUnits(float camera_z, float start_metres);

// Rain_DrawCell (rain.c:464-468) with Rain_Draw's quantised camera z
// (rain.c:283). Derived and test-pinned: the three layers always span exactly
// kTop (18 m) and slide in kLayer (6 m) steps, so the band sits 6..12 m below
// the camera and 6..12 m above it. `layer` is 0 (lowest) .. 2 (highest); the
// returned value is an offset from camera_z, not an absolute world z.
float LayerBaseZUnits(double camera_z, int layer);

// ---------------------------------------------------------------------------
// R5 - snow-only motion (rain.c:592-617)
// ---------------------------------------------------------------------------

// The per-tick horizontal displacement limit. Rain_Update stores
// (Random_Float() - 0.5) * kSway, so a component lies in (-kSway/2, kSway/2).
float SnowSwayHalfRangeUnits();
int SnowSwayTicksMin();
int SnowSwayTicksMax();
float SnowFlakeSizeUnits();
float RainStreakLengthUnits();
int DropKindCount();

// ---------------------------------------------------------------------------
// R6 - the authored schema (the authoritative ParamTable)
// ---------------------------------------------------------------------------
//
// RainEffect_Init HOOK 0x436780 (src/fxobj/rain.c:60-84) registers five
// parameters, in this order:
//
//   #  struct field  ParamTable type  range                    QSC name
//   0  unk24         Param_Bool8()    -                        "Is Rain"
//   1  unk28         Param_Real32()   gRainSizeRange  5..150   "Traceline start"
//   2  unk2C         Param_Real32()   gRainSizeRange  5..150   "Traceline end"
//   3  expr          Param_VarString() -                       "Is Active"
//   4  level         Param_Real32()   gRainLevelRange 0..1     "Rain Alpha"
//
// The names come from the shipped declaration, which the retail data repeats
// verbatim: MISSIONS/location0/LEVEL4/objects.qsc:55
//   Task_DeclareParameters("RainEffect", "Is Rain", "bool8", "Traceline start",
//   "Real32", "Traceline end", "Real32", "Is Active", "VarString",
//   "Rain Alpha", "Real32");
//
// Defaults are RainEffect_Create HOOK 0x436950 (fxobj/rain.c:86-96):
// unk24 = 1 (rain), unk28 = 50.0, unk2C = 20.0, level = 0.3125.
//
// Semantics that are NOT obvious from the names:
//   * "Is Rain" is inverted: Rain_New (rain.c:153) does `r->snow = rain == 0`,
//     so TRUE means rain streaks and FALSE means snow flakes.
//   * "Is Active" is a VarString EXPRESSION, not a boolean. RainEffect_Run
//     (fxobj/rain.c:102-103) re-evaluates it every tick and calls
//     Rain_SetFalling with the result, so the level can drive precipitation
//     from script state at runtime.
//   * "Rain Alpha" is the only field whose runtime change does not rebuild the
//     rain object: RainEffect_Changed (fxobj/rain.c:131-146) frees and rebuilds
//     only when `EQ_OR_NAN(t->level, t->lastLevel)` (fxobj/fxobj.h:62 - true
//     when the two are equal), i.e. a change to any OTHER field rebuilds while
//     a level-only change just calls Rain_SetAlpha.
struct AuthoredRainEffect {
    bool present = false;               // an "RainEffect" task with 5 declared params
    bool is_rain = true;                // param 0, default TRUE
    float traceline_start_metres = 50.0f;  // param 1, default 50.0
    float traceline_end_metres = 20.0f;    // param 2, default 20.0
    bool falling = true;                // param 3, the evaluated "Is Active" expression
    float alpha = 0.3125f;              // param 4, default 0.3125
    int param_token_base = 3;           // param k lives at arg_tokens[k + 3]

    // The raw "Is Active" VarString. The oracle keeps this as an expression and
    // re-evaluates it every tick (fxobj/rain.c:102-103); `falling` above is only
    // its literal value when the token parses as a boolean/number. A level may
    // author a script expression here, which no static parse can reduce.
    std::string active_expression;
    bool active_is_literal = false;     // true when `falling` came from the token
};

// Token layout matches the editor's own convention and Level::LoadFogInfo:
// [0] = task id, [1] = type, [2] = name, [3] = declared parameter 0.
// Returns present == false when `type` is not "RainEffect" or the object
// declares fewer than five parameters.
AuthoredRainEffect ParseRainEffect(const std::string& type,
                                   const std::vector<std::string>& arg_tokens);

// ---------------------------------------------------------------------------
// R7 - SNOW. There is no snow system: snow is rain's `snow` flag set.
// ---------------------------------------------------------------------------
//
// IGI-RE contains no symbol named `Snow`. `Rain_New`'s third argument becomes
// `r->snow = rain == 0` (rain.c:153) and every snow difference is a branch on
// that one field. The authored selector is the `Is Rain` bool8 parameter
// (fxobj/rain.c:77), fed from `RainEffect::unk24` at fxobj/rain.c:141, so the
// parameter is INVERTED: TRUE selects rain streaks, FALSE selects snow flakes.
//
// Every site where `snow` changes a Rain_* rule (the table returned by
// SnowRuleDifferences below):
//
//   rain.c:153      Rain_New      r->snow = rain == 0 - the flag itself
//   rain.c:572      Rain_Update   speed = snow ? 204.8 : 2048.0
//   rain.c:631      Rain_Update   d->z = kLayer - speed (so the spawn z differs)
//   rain.c:592-617  Rain_Update   the horizontal random walk and tile wrap
//   rain.c:337-364  Rain_FillVB   6-vertex camera-facing quad, not a 2-vertex line
//   rain.c:382-388  Rain_FillVB   r->matrix = camera basis * gWorldScale (billboard)
//   rain.c:430-447  Rain_DrawCell binds snow.tex and draws D3DPT_TRIANGLELIST
//   fxobj/rain.c:82 RainEffect_Init loads "snow.tex" once, for both modes
//
// Rain and snow share everything else: the same pool, accumulator, tile lattice,
// 6 m wrap band, 18 m draw band, ground probe, fade, vertex groups, blend and
// clip states. See docs/igi-re-parity/SNOW.md.

// rain.c:153 `r->snow = rain == 0`, where `rain` is the authored `Is Rain` bool
// (fxobj/rain.c:141). Returns true when the object is a snow emitter.
bool SnowFromAuthoredIsRain(bool authored_is_rain);

// Derived from the accumulator (rain.c:619-636): the steady-state number of
// particles spawned per Rain_Update call is speed / kSpawnStep, i.e. 6.6667 for
// rain and 0.6667 for snow - snow fills the pool ten times more slowly. The per-tick
// count is not constant: the accumulator test is a strict `>` against kSpawnStep,
// so rain alternates 6/7 and snow alternates 0/1.
float SpawnRatePerTick(bool is_snow);

// rain.c:631 `d->z = kLayer - speed`: a new particle is placed one speed step
// below the top of the 6 m band, so a snow flake starts at 24371.2 units
// (5.95 m) where a rain streak starts at 22528 units (5.5 m).
float SpawnZUnits(bool is_snow);

// rain.c:597-598 `d->dx = (Random_Float() - 0.5f) * kSway`. `random_float` is a
// Random_Float draw (math/random.c:22-26, in [0, 1)), so a component lies in
// [-kSway/2, +kSway/2) = [-68.26667, +68.26667) units. dx/dy are a PER-TICK
// displacement, held constant until the next re-roll (rain.c:600-602).
float SnowSwayDisplacementUnits(float random_float);

// rain.c:596 `d->timer += Random_Int(gDxRainRandom, 30) + 30`. Random_Int is
// `Random_Next() % n` (math/random.c:28-31), so the reload value is 30..59 ticks
// for a draw in [0, 29]. The argument is clamped to that range.
int SnowSwayReloadTicks(int random_int_30);

// rain.c:595-596: the test is `if (d->timer-- < 0)`, and the reload happens
// AFTER the post-decrement, so the stored timer becomes
// `(timer - 1) + (Random_Int(30) + 30)` = 28..57 when the old value is -1.
int SnowSwayTimerAfterReload(int random_int_30);

// Derived from rain.c:630 (`d->timer = 0` at spawn) and the post-decrement at
// rain.c:595: a fresh flake's first sway direction is rolled on its SECOND
// Rain_Update, not after 30..59 ticks. Between spawn and that roll dx/dy are
// whatever the drop slot already held - Rain_Update never initialises them.
int SnowSwayFirstRollTick();

// rain.c:603-616. The horizontal wrap is a single conditional add/subtract by
// kTileF (rain.c:690, 10895.36 units), NOT a modulo, and it XORs a bit of
// RainDrop::kind (bit 0 for x, bit 1 for y) which selects the vertex group.
// `wrapped` is true when the engine would have adjusted the coordinate.
struct SnowTileWrap {
    float value = 0.0f;
    bool wrapped = false;
};

SnowTileWrap SnowWrapTileX(float x_units);
SnowTileWrap SnowWrapTileY(float y_units);

// rain.c:605 and rain.c:612 - the kind bits the x/y wraps toggle.
constexpr int kSnowWrapKindBitX = 1;
constexpr int kSnowWrapKindBitY = 2;

// rain.c:341-342 builds the quad as `vx +/- size` with `size = fade * kFlakeSize`
// (rain.c:309, kFlakeSize = 81.92 at rain.c:685), so the half-extent is 81.92
// units and the flake is 163.84 units == 0.04 m across. `SnowFlakeSizeUnits`
// above is that half-extent.
float SnowFlakeWidthUnits();

// rain.c:364 (`v += 6`), rain.c:372 (`v += 2`) and the draw counts at
// rain.c:447 (`mult = 6`) and rain.c:458 (`mult = 2`).
int SnowVerticesPerParticle();
int RainVerticesPerParticle();

// rain.c:353 sets all four quad vertices to 0xFFFFFFFF; rain.c:366-367 gives the
// rain line an opaque-white top vertex and the alpha-coloured bottom vertex.
std::uint32_t SnowFlakeDiffuse();
std::uint32_t RainStreakTopDiffuse();

// fxobj/rain.c:82 `gRainTexture = Game_AddTexture("snow.tex")`. RainEffect_Changed
// passes `gRainTexture + 0x18` for BOTH modes (fxobj/rain.c:141); 0x18 is the
// embedded `Texture` inside `GameTexture { char name[0x18]; char tex[0x24]; }`
// (game/game.c:167-170) and rain.c:432 binds its `surface` (Texture::surface is
// at offset 0x20, res/restypes.h:13-25). Only the snow branch binds it; the rain
// branch binds NULL (rain.c:449).
const char* SnowTextureName();

// rain.c:434 (`k = 4`) when the texture pointer is non-null and rain.c:437
// (`k = 3`) when it is null; `k` is the stage-0 texture-argument count.
int SnowTextureStageArgCountWithTexture();
int SnowTextureStageArgCountWithoutTexture();

// One row per site where `snow` changes a Rain_* rule. Addresses are the IGI.exe
// HOOK addresses; citations are IGI-RE file:line.
struct SnowRuleDifference {
    const char* rule;
    const char* function;
    std::uint32_t address;
    const char* citation;
};

std::span<const SnowRuleDifference> SnowRuleDifferences();

}  // namespace igi::rain
