/******************************************************************************
 * @file    human_shadow.h
 * @brief   The engine's ONLY shadow: the flat blob quad every Human-derived task
 *          carries (GitHub issue #63, the half that HAS an engine oracle).
 *
 * WHY THIS EXISTS
 *   Issue #63 asked for "infer light fixtures from emissive geometry + baked
 *   lightmaps", ported from open-igi's `LightFixtureExtractor`. That premise is
 *   refuted: IGI-RE has no `LightFixture` type, no `fixture` symbol and no
 *   PointLight/SpotLight/DirectionalLight task type, so there is NO ENGINE ORACLE
 *   for an inferred fixture list and none is implemented here. The one shadow the
 *   engine does have is a flat blob quad under each human, and that is what this
 *   module ports.
 *
 * ENGINE BASIS (IGI-RE, /Users/haseeb-mir/Documents/Code/IGI-RE, read-only):
 *
 *   src/shadow/shadow.h:13-38   the `Shadow` task and its `ShadowPoly` triangles
 *                               ("a blob shadow quad under its owner")
 *   src/shadow/shadow.c:64-146  Shadow_MagicObjEnterHandler HOOK 0x4E09D0 -- the
 *                               visibility gate (:92), the fade (:94-100), the
 *                               colour (:101-104), the ground basis (:105), the
 *                               four corners (:106-140) and the PolyList position
 *                               (:141)
 *   src/shadow/shadow.c:154-198 Shadow_New HOOK 0x4E0CC0 -- the two triangles and
 *                               their UVs (:170-195)
 *   src/shadow/shadow.c:201-202 the colour constant `gShadowColor`, four floats
 *                               at IGI.exe 0x548708, INIT({0.9f, 0.9f, 0.9f, 0})
 *   src/soldier/human.c:189-190 the two placement constants kShadowSize and
 *                               kShadowHeight
 *   src/soldier/human.c:310     the texture, `Game_AddTexture("shadow1.tex")`
 *   src/soldier/human.c:337-373 Human_CreateHandler HOOK 0x45EB30 -- the ONLY
 *                               caller of Shadow_New in the whole engine (:357-358)
 *   src/task/task.c:373-401     TaskType_New copies the parent type's handler row
 *                               (:397-398), so every type derived from `Human`
 *                               inherits Human_CreateHandler and therefore a shadow
 *   src/math/matrix.c:244-277   Matrix_SetDirection HOOK 0x4B4010 -- the ground
 *                               basis the four corners are laid out in
 *   src/humanbase/human.c:1839-1857 Human_ProbeGround HOOK 0x462390 -- the ground
 *                               hit that supplies groundPos, groundDist, groundUp
 *                               and groundValid (:1844-1855); a miss clears
 *                               groundValid and leaves the rest untouched (:1840-1843)
 *   src/viewcam/human.c:782-790 Human_HideShadow HOOK 0x464230 / Human_ShowShadow
 *                               HOOK 0x464240 -- the script/camera visibility flag
 *   src/dxinit/polylist.c:294-417 PolyList_Draw HOOK 0x49A9B0 -- how the quad is
 *                               actually drawn: alpha blend SRCALPHA/INVSRCALPHA
 *                               with depth writes OFF (:344 SET_RS(0xE,0) is
 *                               D3DRS_ZWRITEENABLE in PolyList_SetLightsCB :260,
 *                               blend set at :368-372), COLOROP MODULATE with the
 *                               texture RGB and the vertex colour and ALPHAOP
 *                               SELECTARG1 = the TEXTURE's alpha (:400-408)
 *   src/math/mathvars.h:7       kFixed = 4096 units per metre
 *
 * THE RULE, and nothing beyond it:
 *   1. One shadow per Human-derived task, created by Human_CreateHandler
 *      (human.c:357-358). No guard, vehicle, door or explosion creates one:
 *      Shadow_New has exactly one caller.
 *   2. The shadow is skipped entirely when `visible` or `enabled` is zero
 *      (shadow.c:92). `visible` is `Human::unk7E8`, set to 1 right after creation
 *      (human.c:359) and toggled only by Human_HideShadow/Human_ShowShadow from
 *      the player's ladder/zipline states (src/weapon/humanplayer.c:289,332,382,
 *      394,459,513,586). `enabled` is `Human::groundValid`, which is 1 only when
 *      the ground probe found a surface (humanbase/human.c:1855).
 *   3. fade = (1 - height/maxHeight) * kShadowFade, then the clamp chain of
 *      shadow.c:95-100 -- `if (!(t >= kShadowMinFade)) t = kShadowMinFade;` first,
 *      which also catches NaN, then `else if (t < 0) t = 0;` (dead), then
 *      `else if (!(1 >= t)) t = 1;`.
 *   4. colour[i] = gShadowColor[i] * fade (shadow.c:101-104), i.e. 0.9 * fade on
 *      all three channels. The colour multiplies the TEXTURE's RGB; the blend
 *      alpha is the texture's own alpha channel (polylist.c:400-408).
 *   5. The quad is laid out in the ground basis Matrix_SetDirection builds from
 *      the ground normal (shadow.c:105, matrix.c:244-277): +-size along m[0..2],
 *      +-size along m[3..5], and every corner pushed +kShadowDepth along m[6..8]
 *      -- the basis' third axis is the normal itself. The PolyList's position is
 *      the owner's ground position (shadow.c:141), so the corners are LOCAL to it.
 *   6. Two triangles with fixed UVs (shadow.c:170-195):
 *      (v0,v1,v2) with (0,0),(1,0),(0,1) and (v1,v3,v2) with (1,0),(1,1),(0,1).
 *
 * WHAT THE RETAIL CONSTANTS MEAN, and the honest reading of them:
 *   * kShadowHeight = 0x1.ccccccp+10f = 1843.2 units = 0.45 m is the quad's
 *     HALF-extent (shadow.c:106 `h = sh->size`), i.e. a 0.9 m blob.
 *   * kShadowSize = 4096000 units = 1000 m is the FADE DENOMINATOR, not a size:
 *     shadow.c:94 divides the height by `sh->maxHeight`, and Shadow_New stores its
 *     7th argument there (:166). 4096000 is the engine's "far" distance (the same
 *     value is Human::kFarUp/kFarDown, humanbase/human.c:192-193, and Missile's
 *     kForward, missile.c:822). The retail fade therefore sits at ~0.8 for every
 *     reachable height and reaches kShadowMinFade only at 625 m; the clamp is a
 *     safety net, not a common case. Both readings are pinned by the tests.
 *   * shadow1.tex is a 32x32 32-bit blob whose RGB is pure black and whose alpha
 *     is a soft radial gradient (measured on the retail file, max alpha 199 at the
 *     centre, 0 at the corners), so in the shipped data the 0.9*fade modulation is
 *     applied to black and the visible shadow is the texture's alpha. That is a
 *     property of the ASSET, not of the rule: the rule is ported as written.
 *
 * OUT OF SCOPE, deliberately, and why:
 *   * The ground probe itself (Human_ProbeGround, humanbase/human.c) is a collision
 *     query against the retail world; the editor has no collision world. The editor
 *     supplies the surface it does have (the App's terrain query) and this module
 *     keeps the engine's own gate as `valid`.
 *   * Human_HideShadow/Human_ShowShadow are driven by the player's ladder and
 *     zipline states, which the editor does not simulate; a placed human is drawn
 *     with the post-creation value, visible = 1 (human.c:359).
 *****************************************************************************/
#pragma once

#include <string>

namespace igi {

// ---------------------------------------------------------------------------
// Engine constants. Every one is IGI-RE DATA.
// ---------------------------------------------------------------------------

// statgun/mp5sd.c:120-122 -- DATA(5336CC) float kZoomIn INIT(0.8f), and
// `ALIAS(kShadowFade, kZoomIn, 0)`: the two names are the same 4 bytes.
inline constexpr float kShadowFade = 0.8f;

// heli/wheel.c:285-286 -- DATA(5336A4) const float kThreeTenths INIT(0.3f),
// `ALIAS(kShadowMinFade, kThreeTenths, 0)`.
inline constexpr float kShadowMinFade = 0.3f;

// glass/shatter.c:579-581 -- DATA(533480) float kTen INIT(10.0f),
// `ALIAS(kShadowDepth, kTen, 0)`. 10 units = 2.44 mm at 4096 units/m: the quad's
// lift along the ground normal, so it does not z-fight with the surface.
inline constexpr float kShadowDepth = 10.0f;

// shadow.c:201-202 -- STORE(igd, 548708, 4) float store_548708[4]
// INIT({0.9f, 0.9f, 0.9f, 0.0f}), `ALIAS(gShadowColor, store_548708, 0)`.
inline constexpr float kShadowColor[3] = {0.9f, 0.9f, 0.9f};

// human.c:189 -- `static const float kShadowSize = 4096000.0f;`. Passed as
// Shadow_New's 7th argument, stored in `sh->maxHeight` (shadow.c:166) and used as
// the fade DENOMINATOR (shadow.c:94).
inline constexpr float kShadowMaxHeight = 4096000.0f;

// human.c:190 -- `static const float kShadowHeight = 0x1.ccccccp+10f;` = 1843.2f.
// Passed as Shadow_New's 8th argument, stored in `sh->size` (shadow.c:167) and used
// as the quad's HALF-extent (shadow.c:106).
inline constexpr float kShadowQuadSize = 0x1.ccccccp+10f;

// human.c:310 -- `gShadowTex = Game_AddTexture("shadow1.tex");`. The engine's
// Game_LoadTextures resolves it as the resource identity
// "LOCAL:common/textures/shadow1.tex" out of <IGI>/common/textures/textures.res
// (src/game/game.c:524-534), which is what the editor's own game-texture loader
// builds for this name.
inline constexpr const char* kShadowTextureName = "shadow1.tex";

// The engine's own unit: mathvars.h:7 `kFixed = 4096` units per metre. Used only
// to READ the constants above in metres; the maths never converts.
inline constexpr double kShadowUnitsPerMetre = 4096.0;

// ---------------------------------------------------------------------------
// The rule
// ---------------------------------------------------------------------------

// shadow.c:94-100. `height` is the owner's height above its ground contact and
// `maxHeight` is the value Shadow_New stored (the retail constant above). The
// comparison chain is reproduced in the original's order because the FIRST test is
// the one that catches NaN (`!(NaN >= kShadowMinFade)` is true), which is why NaN
// clamps to kShadowMinFade and not to 1.
float HumanShadowFade(double height, double maxHeight);

// shadow.c:101-104: out[i] = gShadowColor[i] * fade.
void HumanShadowColor(float fade, float out[3]);

// Matrix_SetDirection (matrix.c:244-277) into a row-major 3x3: m[0..2] is the
// side axis, m[3..5] the second in-plane axis and m[6..8] the normal itself
// (t = side x s = dir for a unit `up`). `up` need not be unit: the original
// normalizes the first and second axes but never the third.
void HumanShadowBasis(const double up[3], float m[9]);

// shadow.c:106-140. `size` is the half-extent, `depth` the lift along the normal.
// The corners are LOCAL to the owner's ground position (shadow.c:141 sets the
// PolyList position to `*sh->pos`), in the engine's own order:
//   verts[0] = ( h*m0 + h*m3 + m6*depth,  h*m1 + h*m4 + m7*depth,  h*m2 + h*m5 + m8*depth)
//   verts[1] = (-h*m0 + h*m3 + c,        -h*m1 + h*m4 + c,        -h*m2 + h*m5 + c)
//   verts[2] = ( h*m0 - h*m3 + c,         h*m1 - h*m4 + c,         h*m2 - h*m5 + c)
//   verts[3] = (-h*m0 - h*m3 + c,        -h*m1 - h*m4 + c,        -h*m2 - h*m5 + c)
void HumanShadowQuad(const double up[3], float size, float depth, float verts[4][3]);

// shadow.c:170-195: the two triangles Shadow_New installs, as world-space
// (ground-position-relative) vertices plus the UVs that go with them.
struct HumanShadowTriangles {
    float pos[2][3][3];   // [triangle][vertex][xyz]
    float uv[2][3][2];    // [triangle][vertex][uv]
    int count = 2;        // Shadow_New passes count = 2 (shadow.c:169)
};

HumanShadowTriangles HumanShadowBuildTriangles(const float verts[4][3]);

// ---------------------------------------------------------------------------
// Placement
// ---------------------------------------------------------------------------

// The engine's two gates, shadow.c:92 -- `if (!*sh->visible || !*sh->enabled)
// return;`. Both are ints in the engine (human.c:357 passes `&h->unk7E8` and
// `&h->groundValid`).
inline bool HumanShadowDrawn(int visible, int enabled) {
    return visible != 0 && enabled != 0;
}

// One blob shadow, ready to draw: the PolyList position (shadow.c:141) plus the
// four local corners, the fade and the colour.
struct HumanShadowPlacement {
    // true for a placement that has a quad. The engine's own gate is
    // `HumanShadowDrawn` above; the editor's mapping of it (visible = 1,
    // human.c:359; enabled = the placement is on the ground) is always satisfied
    // for a level placement, which is why this is set and never cleared.
    bool valid = false;
    double ground_pos[3] = {0.0, 0.0, 0.0};
    float verts[4][3] = {};
    float color[3] = {0.0f, 0.0f, 0.0f};
    float fade = 0.0f;
};

// Which task types carry a shadow. TaskType_New copies the parent's handler row
// (task.c:397-398), so the answer is exactly "derives from Human": `Human` itself
// (human.c:246) and the types created with Human_GetType() as parent --
// HumanSoldier (soldier.c:718), HumanSoldierPistol (pistol.c:550),
// HumanSoldierRPG (rpg.c:593) and HumanPlayer (humanplayer.c:1091). `HumanAI` is
// NOT one of them: it is parented to the root type (aiaction.c:194) and its
// create handler never runs Human_CreateHandler. In the retail levels the types
// that actually appear are HumanSoldier and HumanPlayer (the 7 shipped levels
// declare "HumanSoldier"/"HumanPlayer" plus "HumanPlayerInput"/"HumanAI"; the
// pistol/RPG/female variants are soldier subtypes, not separate level types).
bool IsHumanShadowOwner(const std::string& type);

// The editor's placement rule, which is the engine's rule with the one input the
// editor cannot produce -- the collision ground probe -- supplied by the caller.
//
//   * `have_ground`/`ground_z` are the caller's ground hit, the analogue of
//     Human_ProbeGround's `hits[0].pos.z` (humanbase/human.c:1844). When the
//     caller has no hit, the placement's own Z is used and the height above ground
//     is 0 -- the editor's placements are authored on the ground, and the engine's
//     own `groundValid = 0` branch (humanbase/human.c:1840-1843) would otherwise
//     hide the shadow on every surface the terrain query cannot see.
//   * `height` is the analogue of `Human::groundDist`, which Human_ProbeGround
//     computes as a Z distance from the foot to the hit (humanbase/human.c:1801).
//   * The ground normal is vertical: the editor stores no surface normal, and the
//     engine's own Matrix_SetDirection falls back to the (0,1,0) reference for a
//     Z-aligned direction (matrix.c:249-257), which yields the identity basis for
//     up = (0,0,1). IGI is Z-up (the ground probe compares Z, humanbase/human.c:1801).
HumanShadowPlacement BuildHumanShadowPlacement(double x, double y, double placement_z,
                                               bool have_ground, double ground_z);

}  // namespace igi
