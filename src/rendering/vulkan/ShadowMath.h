#pragma once

#include <algorithm>
#include <cmath>

#include "rendering/vulkan/VkMath.h"

namespace dash::vkexp {

// The slice of the world the directional shadow map has to cover, as a sphere.
// A sphere (instead of an AABB) keeps the ortho box the same size whatever the
// light direction is, so rotating the light cannot make shadows pop.
struct ShadowVolume {
    Vec3  center{0.0f, 0.0f, 0.0f};
    float radius = 1.0f;
};

// Vulkan-style orthographic projection: x/y map to [-1, 1], z maps to [0, 1],
// right-handed view space looking down -Z.
inline Mat4 orthographic(float halfWidth, float halfHeight, float zNear, float zFar)
{
    const float depth = std::max(zFar - zNear, 1e-4f);

    Mat4 out{};
    out.m[0]  = 1.0f / std::max(halfWidth, 1e-4f);
    out.m[5]  = 1.0f / std::max(halfHeight, 1e-4f);
    out.m[10] = -1.0f / depth;
    out.m[14] = -zNear / depth;
    out.m[15] = 1.0f;
    return out;
}

// World -> light clip space for a directional light.
//
// `lightDir` is the direction the light travels (the shaders negate it to get
// the surface-to-light vector), so the virtual eye is placed up-light from the
// volume. It sits two radii back and the depth range spans [r, 3r], which puts
// the whole sphere between the near and far planes with no clipped casters.
inline Mat4 directionalLightMatrix(const Vec3& lightDir, const ShadowVolume& volume)
{
    Vec3 dir = normalize(lightDir);
    if (dir.x == 0.0f && dir.y == 0.0f && dir.z == 0.0f) dir = {0.0f, -1.0f, 0.0f};

    const float radius = std::max(volume.radius, 0.01f);
    const Vec3 eye{volume.center.x - dir.x * radius * 2.0f,
                   volume.center.y - dir.y * radius * 2.0f,
                   volume.center.z - dir.z * radius * 2.0f};

    // lookAt degenerates when forward and up are parallel.
    const Vec3 up = std::fabs(dir.y) > 0.999f ? Vec3{0.0f, 0.0f, 1.0f}
                                              : Vec3{0.0f, 1.0f, 0.0f};

    return multiply(orthographic(radius, radius, radius, radius * 3.0f),
                    lookAt(eye, volume.center, up));
}

// Bounding sphere of a world-space AABB, padded so thin scenes still get a
// usable depth range.
inline ShadowVolume shadowVolumeFromBounds(const Vec3& boundsMin, const Vec3& boundsMax)
{
    ShadowVolume volume;
    volume.center = {(boundsMin.x + boundsMax.x) * 0.5f,
                     (boundsMin.y + boundsMax.y) * 0.5f,
                     (boundsMin.z + boundsMax.z) * 0.5f};

    const float ex = (boundsMax.x - boundsMin.x) * 0.5f;
    const float ey = (boundsMax.y - boundsMin.y) * 0.5f;
    const float ez = (boundsMax.z - boundsMin.z) * 0.5f;
    volume.radius = std::max(std::sqrt(ex * ex + ey * ey + ez * ez), 1.0f);
    return volume;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cascades
//
// One shadow map cannot be both sharp at the character's feet and present at
// the far end of an isometric view: the texel budget forces a choice. Splitting
// the camera range into slices and giving each its own map is how that is
// avoided — the near slice is small enough to be sharp, the far one large
// enough to reach.
// ─────────────────────────────────────────────────────────────────────────────

// Three keeps the near slice tight without paying a fourth depth pass.
inline constexpr int kShadowCascades = 3;

// Beyond this the fog (which starts at 150) has already swallowed the detail,
// so shadowing it would spend texels on pixels nobody reads.
inline constexpr float kShadowMaxDistance = 100.0f;

// Practical split scheme: the logarithmic distribution respects perspective but
// starves the far cascades, the uniform one does the opposite. `lambda` mixes
// them; 0.7 is the usual compromise.
inline void computeCascadeSplits(float nearZ, float farZ, float lambda,
                                 float (&out)[kShadowCascades])
{
    const float safeNear = std::max(nearZ, 1e-3f);
    for (int i = 0; i < kShadowCascades; ++i) {
        const float p = static_cast<float>(i + 1) / static_cast<float>(kShadowCascades);
        const float logSplit = safeNear * std::pow(farZ / safeNear, p);
        const float uniformSplit = safeNear + (farZ - safeNear) * p;
        out[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
    }
}

// Bounding sphere of the camera frustum slice between `nearD` and `farD`.
// A sphere rather than a tight box, for the same reason ShadowVolume is one:
// it is invariant to camera rotation, so turning cannot make the cascade
// resize and the shadows swim.
inline ShadowVolume frustumSliceVolume(const Vec3& camPos,
                                       const Vec3& forward,
                                       const Vec3& right,
                                       const Vec3& up,
                                       float fovYRadians, float aspect,
                                       float nearD, float farD)
{
    const float tanHalf = std::tan(fovYRadians * 0.5f);
    const float nh = nearD * tanHalf, nw = nh * aspect;
    const float fh = farD * tanHalf,  fw = fh * aspect;

    Vec3 corners[8];
    int c = 0;
    for (int slice = 0; slice < 2; ++slice) {
        const float d = slice == 0 ? nearD : farD;
        const float h = slice == 0 ? nh : fh;
        const float w = slice == 0 ? nw : fw;
        for (int sy = -1; sy <= 1; sy += 2) {
            for (int sx = -1; sx <= 1; sx += 2) {
                corners[c++] = {
                    camPos.x + forward.x * d + right.x * w * sx + up.x * h * sy,
                    camPos.y + forward.y * d + right.y * w * sx + up.y * h * sy,
                    camPos.z + forward.z * d + right.z * w * sx + up.z * h * sy};
            }
        }
    }

    ShadowVolume volume;
    for (const Vec3& p : corners) {
        volume.center.x += p.x;
        volume.center.y += p.y;
        volume.center.z += p.z;
    }
    volume.center.x *= 0.125f;
    volume.center.y *= 0.125f;
    volume.center.z *= 0.125f;

    float r2 = 0.0f;
    for (const Vec3& p : corners) {
        const float dx = p.x - volume.center.x;
        const float dy = p.y - volume.center.y;
        const float dz = p.z - volume.center.z;
        r2 = std::max(r2, dx * dx + dy * dy + dz * dz);
    }
    volume.radius = std::max(std::sqrt(r2), 0.5f);
    return volume;
}

// World size of one shadow-map texel, the unit the normal-offset bias uses.
inline float shadowTexelWorldSize(const ShadowVolume& volume, uint32_t resolution)
{
    if (resolution == 0) return 0.0f;
    return (2.0f * std::max(volume.radius, 0.01f)) / static_cast<float>(resolution);
}

// Quantises the volume centre to the texel grid of the shadow map, in the
// light's own basis. A volume that follows the player slides by fractions of a
// texel every frame; without this the whole depth image resamples each time and
// every shadow edge crawls. Snapping makes the movement whole texels, so the
// image is the same one shifted.
inline ShadowVolume snapVolumeToTexelGrid(const Vec3& lightDir,
                                          const ShadowVolume& volume,
                                          uint32_t resolution)
{
    const float texel = shadowTexelWorldSize(volume, resolution);
    if (texel <= 0.0f) return volume;

    Vec3 dir = normalize(lightDir);
    if (dir.x == 0.0f && dir.y == 0.0f && dir.z == 0.0f) dir = {0.0f, -1.0f, 0.0f};

    // Same basis lookAt() derives inside directionalLightMatrix().
    const Vec3 up = std::fabs(dir.y) > 0.999f ? Vec3{0.0f, 0.0f, 1.0f}
                                              : Vec3{0.0f, 1.0f, 0.0f};
    const Vec3 right = normalize(cross(dir, up));
    const Vec3 realUp = cross(right, dir);

    const float u = dot(volume.center, right);
    const float v = dot(volume.center, realUp);
    const float du = std::round(u / texel) * texel - u;
    const float dv = std::round(v / texel) * texel - v;

    ShadowVolume snapped = volume;
    snapped.center = {volume.center.x + right.x * du + realUp.x * dv,
                      volume.center.y + right.y * du + realUp.y * dv,
                      volume.center.z + right.z * du + realUp.z * dv};
    return snapped;
}

} // namespace dash::vkexp
