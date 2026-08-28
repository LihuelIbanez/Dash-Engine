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

// World size of one shadow-map texel, the unit the normal-offset bias uses.
inline float shadowTexelWorldSize(const ShadowVolume& volume, uint32_t resolution)
{
    if (resolution == 0) return 0.0f;
    return (2.0f * std::max(volume.radius, 0.01f)) / static_cast<float>(resolution);
}

} // namespace dash::vkexp
