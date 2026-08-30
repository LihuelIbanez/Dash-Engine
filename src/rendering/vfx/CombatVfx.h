#pragma once

#include "rendering/vfx/ParticleSystem.h"

// ─────────────────────────────────────────────────────────────────────────────
// CombatVfx — the emitter presets combat events map to.
//
// Colours are authored in the linear HDR space the scene is shaded in, so the
// additive presets go well past 1.0 on purpose: the ACES pass in tonemap.frag
// rolls them back and that is what makes a spark read as a spark and not as a
// flat white square.
// ─────────────────────────────────────────────────────────────────────────────

namespace dash::vfx {

// Layout of the procedural atlas built by ParticleRenderer: 4x4 cells, one row
// per look. Frames inside a row are the animation.
inline constexpr int kAtlasCols = 4;
inline constexpr int kAtlasRows = 4;
inline constexpr int kFramePuff    = 0;   // soft round puff → smoke, mist
inline constexpr int kFrameSpark   = 4;   // bright streak   → sparks, magic
inline constexpr int kFrameRing    = 8;   // expanding ring  → impact shockwave
inline constexpr int kFrameSplat   = 12;  // ragged blob     → blood

// Blood thrown back along the hit direction. `dirX/dirZ` points away from the
// attacker; scale grows with the damage so a big hit is visibly bigger.
inline EmitParams bloodSplatter(float x, float y, float z,
                                float dirX, float dirZ,
                                int damage, float groundY)
{
    const float power = 0.6f + 0.06f * static_cast<float>(damage < 40 ? damage : 40);

    EmitParams p;
    p.x = x; p.y = y; p.z = z;
    p.radius = 0.06f;
    p.dirX = dirX; p.dirY = 0.55f; p.dirZ = dirZ;
    p.spread = 0.75f;
    p.speedMin = 1.2f * power;
    p.speedMax = 3.6f * power;
    p.lifeMin = 0.35f;
    p.lifeMax = 0.85f;
    p.sizeBegin = 0.075f;
    p.sizeEnd = 0.030f;
    p.colorBegin[0] = 0.62f; p.colorBegin[1] = 0.045f; p.colorBegin[2] = 0.030f; p.colorBegin[3] = 1.0f;
    p.colorEnd[0]   = 0.16f; p.colorEnd[1]   = 0.012f; p.colorEnd[2]   = 0.010f; p.colorEnd[3]   = 0.0f;
    p.gravity = -11.0f;
    p.drag = 1.1f;
    p.spinMax = 6.0f;
    p.floorY = groundY;
    p.frameFirst = kFrameSplat;
    p.frameCount = 4;
    p.blend = BlendMode::Alpha;
    p.count = 10 + damage / 3;
    return p;
}

// The hot flash at the point of contact. Additive, very short, no gravity.
inline EmitParams impactSparks(float x, float y, float z,
                               float dirX, float dirZ, int damage)
{
    EmitParams p;
    p.x = x; p.y = y; p.z = z;
    p.radius = 0.04f;
    p.dirX = dirX; p.dirY = 0.35f; p.dirZ = dirZ;
    p.spread = 1.15f;
    p.speedMin = 2.4f;
    p.speedMax = 6.5f;
    p.lifeMin = 0.10f;
    p.lifeMax = 0.28f;
    p.sizeBegin = 0.055f;
    p.sizeEnd = 0.006f;
    p.colorBegin[0] = 3.6f; p.colorBegin[1] = 1.85f; p.colorBegin[2] = 0.70f; p.colorBegin[3] = 1.0f;
    p.colorEnd[0]   = 1.4f; p.colorEnd[1]   = 0.28f; p.colorEnd[2]   = 0.06f; p.colorEnd[3]   = 0.0f;
    p.gravity = -5.5f;
    p.drag = 3.2f;
    p.spinMax = 10.0f;
    p.frameFirst = kFrameSpark;
    p.frameCount = 4;
    p.blend = BlendMode::Additive;
    p.count = 6 + damage / 4;
    return p;
}

// Death: one additive shockwave ring that expands and fades fast.
inline EmitParams deathShockwave(float x, float y, float z)
{
    EmitParams p;
    p.x = x; p.y = y; p.z = z;
    p.radius = 0.0f;
    p.dirX = 0.f; p.dirY = 1.f; p.dirZ = 0.f;
    p.spread = 0.0f;
    p.speedMin = 0.0f;
    p.speedMax = 0.35f;
    p.lifeMin = 0.30f;
    p.lifeMax = 0.40f;
    p.sizeBegin = 0.20f;
    p.sizeEnd = 1.55f;
    p.colorBegin[0] = 2.9f; p.colorBegin[1] = 0.55f; p.colorBegin[2] = 0.35f; p.colorBegin[3] = 1.0f;
    p.colorEnd[0]   = 0.7f; p.colorEnd[1]   = 0.05f; p.colorEnd[2]   = 0.03f; p.colorEnd[3]   = 0.0f;
    p.gravity = 0.0f;
    p.drag = 0.0f;
    p.spinMax = 0.6f;
    p.frameFirst = kFrameRing;
    p.frameCount = 4;
    p.blend = BlendMode::Additive;
    p.count = 3;
    return p;
}

// Death: the gore that follows the ring, thrown in every direction.
inline EmitParams deathGibs(float x, float y, float z, float groundY)
{
    EmitParams p;
    p.x = x; p.y = y; p.z = z;
    p.radius = 0.12f;
    p.dirX = 0.f; p.dirY = 1.f; p.dirZ = 0.f;
    p.spread = 1.7f;
    p.speedMin = 1.8f;
    p.speedMax = 5.2f;
    p.lifeMin = 0.55f;
    p.lifeMax = 1.30f;
    p.sizeBegin = 0.095f;
    p.sizeEnd = 0.045f;
    p.colorBegin[0] = 0.55f; p.colorBegin[1] = 0.040f; p.colorBegin[2] = 0.030f; p.colorBegin[3] = 1.0f;
    p.colorEnd[0]   = 0.10f; p.colorEnd[1]   = 0.008f; p.colorEnd[2]   = 0.008f; p.colorEnd[3]   = 0.0f;
    p.gravity = -12.5f;
    p.drag = 0.8f;
    p.spinMax = 8.0f;
    p.floorY = groundY;
    p.frameFirst = kFrameSplat;
    p.frameCount = 4;
    p.blend = BlendMode::Alpha;
    p.count = 26;
    return p;
}

// Death: the dark cloud left behind, drifting up and out.
inline EmitParams deathSmoke(float x, float y, float z)
{
    EmitParams p;
    p.x = x; p.y = y; p.z = z;
    p.radius = 0.18f;
    p.dirX = 0.f; p.dirY = 1.f; p.dirZ = 0.f;
    p.spread = 0.9f;
    p.speedMin = 0.25f;
    p.speedMax = 1.05f;
    p.lifeMin = 0.9f;
    p.lifeMax = 1.6f;
    p.sizeBegin = 0.16f;
    p.sizeEnd = 0.52f;
    p.colorBegin[0] = 0.10f; p.colorBegin[1] = 0.085f; p.colorBegin[2] = 0.095f; p.colorBegin[3] = 0.55f;
    p.colorEnd[0]   = 0.05f; p.colorEnd[1]   = 0.045f; p.colorEnd[2]   = 0.055f; p.colorEnd[3]   = 0.0f;
    p.gravity = 0.55f;
    p.drag = 1.4f;
    p.spinMax = 1.6f;
    p.frameFirst = kFramePuff;
    p.frameCount = 4;
    p.blend = BlendMode::Alpha;
    p.count = 12;
    return p;
}

} // namespace dash::vfx
