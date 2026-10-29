#pragma once
#include "../pch.h"
#include <glm/glm.hpp>
#include <vector>

#include "../runtime/smoke_sim.h"

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
// NAMED DIVERGENCE: the sprite texture. The engine binds smoke1.spr /
// smoke2.spr / fireball.spr / blood1.spr through QVertex_DrawQuadEx
// (particle.c:205-206; the four files at smoke.c:189-192). Those live inside
// COMMON/SPRITES/SPRITES.RES and are a chunked LOOP/FONT container with
// palettes (Sprite_Load HOOK 0x4B6720, src/res/sprite.c:162-262), which the
// editor has no loader for. The quad is therefore filled with a procedural
// radial falloff instead of the sprite's texels: the GEOMETRY, the SIZE, the
// COLOUR, the ALPHA and the FADE are the engine's, the texels are not.
class Renderer_Smoke {
public:
    bool Init();
    void Shutdown();

    // The level's authored "Smoke" tasks. Called every frame with the level's
    // current list; an emitter whose parameters did not change keeps its live
    // particles (Smoke_Changed's own rule, smoke_sim.h).
    void SetEmitters(const std::vector<igi::SmokeParams>& emitters);
    void Clear();

    // One engine tick, then the draw. `camera_right` / `camera_up` /
    // `camera_forward` are the camera's screen axes; the quads face the camera
    // in that basis, and `camera_forward` carries Particle2D_Draw's depth gate.
    void Draw(GLuint ubo_mats, const glm::vec3& camera_pos, const glm::vec3& camera_right,
              const glm::vec3& camera_up, const glm::vec3& camera_forward);

    bool IsActive() const { return sim_.EmitterCount() != 0; }
    const igi::SmokeSimulation& Simulation() const { return sim_; }
    igi::SmokeSimulation& MutableSimulation() { return sim_; }

private:
    GLuint program_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    igi::SmokeSimulation sim_;
    std::vector<float> vertices_;  // pos(3) + colour(4) + uv(2) per vertex
};
