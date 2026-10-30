#pragma once
#include "../pch.h"
#include <glm/glm.hpp>
#include <vector>

#include "../runtime/smoke_sim.h"
#include "../runtime/smoke_sprite.h"

// The retail "Smoke" emitter's PARTICLES in the viewport (issue #43).
//
// The simulation half is igi::SmokeSimulation (source/runtime/smoke_sim.h),
// which is the transcription of Smoke_Run HOOK 0x437DB0 (src/fxobj/smoke.c:
// 298-339) and Particle2D_Run HOOK 0x4FD660 (src/graphtask/particle.c:264-305).
// This class is the GL half: one dynamic vertex buffer, one shader, and the
// per-frame call that steps the simulation ONCE and draws what it holds.
//
// THE TICK RULE. `Draw` steps the simulation exactly once per call, and the
// renderer calls it once per rendered frame -- the engine's own rule (its loop
// has no elapsed time at all, src/win/appmain.c:224-227) and the one the
// weather particles already follow (renderer_rain.cpp's RainEffect_Run HOOK
// 0x436990). A long editor frame must not buy extra simulation steps: commit
// f4204d55 fixed exactly that mistake for the animation clock, and
// scripts/perf-guard.sh budgets the invariant.
//
// THE QUAD. Particle2D_Draw HOOK 0x4FD020 (particle.c:108-208) projects each
// particle with TransContext_Project (0x497F20) and draws a SCREEN-space quad
// whose half-extent is TransContext_ScaleSize(size, depth) = size * scaleX /
// depth (0x498040, src/transctx/transctx.c:282-288). A world-space camera-facing
// quad of half-extent `size` at the same depth projects to exactly that many
// pixels, so this draws the engine's quad as a billboard:
//
//   half = group.size * p.scale      (particle.c:145-151; group.size is
//                                     "Particle Size" * kUnit, smoke.c:394)
//   corners = the engine's own four, rotated in the camera plane by p.angle
//             when p.spin != 0 (particle.c:156-193)
//   colour/alpha = particle.c:194-203 -- flags & 0x80000 means "the particle
//             level IS the alpha", otherwise the level scales the colour
//
// THE TEXELS. The quad is filled with the retail sprite, not a procedural
// falloff (that divergence is gone):
//
//   * which sprite: Smoke_Changed's `switch (t->sprite)` (smoke.c:380-393) picks
//     one of the four Game_AddSprite names Smoke_Init interned (smoke.c:189-192);
//     igi::SmokeSpriteMemberName turns a file name into the pack member
//     Game_LoadSprites opens (src/game/game.c:536-547).
//   * which frame: Particle2D_Draw's `frame = t->animated ?
//     t->sprite->map->cells[p->frame] - 1 : 0` (particle.c:200-205), which is
//     igi::SmokeSprite::FrameForParticle.
//   * the texels themselves: igi::SmokeSpriteParse, which is Sprite_Load
//     HOOK 0x4B6720 plus D3DTex_DownloadTexture's format dispatch
//     HOOK 0x50AD90. The uploaded RGBA is that conversion's canonical BGRA,
//     byte-swapped; v=0 is the sprite's first (top) row, which is where the
//     engine's uv (0,0) sits on the quad's top edge (qvertex.c:74-87), so no
//     vertical flip is applied.
//
// THE BLEND. The quad's flags are the group's (Smoke_Changed's fade-mode
// switch, smoke.c:366-379, OR'd with 0x40000 by Particle2D_New, particle.c:515,
// OR'd again with 0x1000000 by Particle2D_Draw itself). They drive three D3D
// states that IGI-RE transcribes, and this file reproduces all three:
//
//   D3DQVertex_SetBlendStates  HOOK 0x50E000  src/qvertex/d3d.c:633-646
//       flags & 8          -> SRCALPHA / ONE            (additive)
//       flags & 0x10       -> ZERO / INVSRCCOLOR        (darken)
//       flags & 0x80000000 -> ZERO / SRCCOLOR
//       else               -> SRCALPHA / INVSRCALPHA    (the fade mode 0 case)
//   D3DQVertex_SetRenderStates HOOK 0x50DCC0  src/qvertex/d3d.c:556-587
//       flags & 0x8000003C -> ALPHATEST on, ALPHAREF 10, ALPHAFUNC GREATER
//       flags & 0x20000    -> z test LEQUAL, z write ON
//       flags & 0x40000    -> z test LEQUAL, z write OFF   (the Smoke case)
//       else               -> z test off
//   D3DQVertex_SetTextureStates HOOK 0x50DE00 src/qvertex/d3d.c:589-624
//       COLOROP/ALPHAOP MODULATE(TEXTURE, DIFFUSE), i.e. texel * vertex colour;
//       LINEAR min/mag, MIPFILTER NONE, CLAMP.
//
// The fade-mode 0 default is therefore the standard alpha blend this file used
// before, while fade modes 1 and 2 were drawn with the WRONG equation (always
// SRCALPHA/INVSRCALPHA, never additive/darken, and with the alpha test off).
//
// FALLBACK. When the retail pack is not there (no installed game, a headless
// profile run), no sprite can be loaded and the quad falls back to the old
// procedural radial falloff. That is a visible difference and is logged as a
// warning; it is not silent. With the pack present, `SpriteReport()` is what a
// headless run prints to prove the texels came from the game.
class Renderer_Smoke {
public:
    bool Init();
    void Shutdown();

    // The level's authored "Smoke" tasks. Called every frame with the level's
    // current list; an emitter whose parameters did not change keeps its live
    // particles (Smoke_Changed's own rule, smoke_sim.h). Loads the retail
    // sprites first (once), so the groups are built with the engine's frame
    // counts.
    void SetEmitters(const std::vector<igi::SmokeParams>& emitters);
    void Clear();

    // One engine tick, then the draw. `camera_right` / `camera_up` /
    // `camera_forward` are the camera's screen axes; the quads face the camera
    // in that basis, and `camera_forward` carries Particle2D_Draw's depth gate.
    void Draw(GLuint ubo_mats, const glm::vec3& camera_pos, const glm::vec3& camera_right,
              const glm::vec3& camera_up, const glm::vec3& camera_forward);

    // Reads the four sprites out of the mounted pack. Idempotent; returns the
    // number of slots that loaded (0 = the pack is missing, fallback active).
    // Needs no GL, so a profile run can call it without a context.
    int LoadSprites();

    // How many of the four sprite slots are loaded, and one line per slot for
    // the log: "<file> <w>x<h> fmt=<n> frames=<n> cols=<n>". Empty `out` when
    // nothing loaded.
    int LoadedSpriteCount() const { return loaded_sprites_; }
    std::vector<std::string> SpriteReport() const;

    bool IsActive() const { return sim_.EmitterCount() != 0; }
    const igi::SmokeSimulation& Simulation() const { return sim_; }
    igi::SmokeSimulation& MutableSimulation() { return sim_; }

    // The parsed sprite for a slot (0 fireball, 1 blood1, 2 smoke2, 3 smoke1),
    // or nullptr when it did not load.
    const igi::SmokeSprite* Sprite(int slot) const;

private:
    static constexpr int kSpriteSlots = 4;
    static constexpr int kMaxSpriteFrames = 16;  // blood1.spr has 7

    // One particle's screen-space quad, held until the whole frame's particle
    // set can be sorted by depth the way the engine's sort renderer does
    // (SortRender_Sort 0x4B7160, src/res/sortrender.c:55-87, fed by
    // D3DQVertex_Emit 0x50D4C0 with each strip's first z, src/qvertex/d3d.c:373-376).
    struct Quad {
        float depth = 0.0f;      // the sort key: TransContext_Project's z
        glm::vec3 centre{0.0f};
        glm::vec2 corners[4];    // screen axes, y measured DOWN the screen
        float rgba[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        int slot = 0;
        int frame = 0;
        std::uint32_t flags = 0;
    };

    // One batch of quads that share a texture and a blend/depth state: the
    // engine's own granularity, one Particle2D group per emitter.
    struct Batch {
        int slot = 0;
        int frame = 0;
        std::uint32_t flags = 0;
        std::size_t first = 0;   // first vertex in vertices_
        std::size_t count = 0;   // vertex count
    };

    int SpriteSlotForIndex(int sprite_index) const;
    Batch* BatchFor(int slot, int frame, std::uint32_t flags, std::size_t first);
    GLuint UploadFrame(const igi::SmokeSpriteFrame& frame);

    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLint u_tex_ = -1;
    GLint u_textured_ = -1;
    GLint u_alpha_test_ = -1;

    igi::SmokeSimulation sim_;
    std::vector<float> vertices_;  // pos(3) + colour(4) + uv(2) per vertex
    std::vector<Quad> quads_;      // this frame's particles, before sorting
    std::vector<Batch> batches_;

    struct LoadedSprite {
        igi::SmokeSprite sprite;
        GLuint textures[kMaxSpriteFrames] = {};
        int frame_count = 0;
    };
    LoadedSprite sprites_[kSpriteSlots];
    int loaded_sprites_ = 0;
    bool sprites_attempted_ = false;
    std::string sprites_root_;   // the game root the load was attempted for
};
