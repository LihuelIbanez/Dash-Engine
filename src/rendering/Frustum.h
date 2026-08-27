#pragma once

#include <cmath>

namespace dash {

struct Plane {
    float a = 0.f, b = 0.f, c = 0.f, d = 0.f;
};

// View frustum extracted from a view-projection matrix (Gribb-Hartmann).
// Deliberately free of Vulkan types so it can be unit tested headless.
struct Frustum {
    // Order: left, right, bottom, top, near, far.
    Plane planes[6];

    // `m` is column-major (m[col * 4 + row]), matching dash::vkexp::Mat4.
    // Assumes a Vulkan-style [0, 1] depth range.
    static Frustum fromViewProj(const float m[16])
    {
        const float r0[4] = {m[0], m[4], m[8],  m[12]};
        const float r1[4] = {m[1], m[5], m[9],  m[13]};
        const float r2[4] = {m[2], m[6], m[10], m[14]};
        const float r3[4] = {m[3], m[7], m[11], m[15]};

        Frustum f;
        auto set = [](Plane& p, float a, float b, float c, float d) {
            const float len = std::sqrt(a * a + b * b + c * c);
            if (len > 1e-6f) { p = {a / len, b / len, c / len, d / len}; }
            else             { p = {0.f, 0.f, 0.f, 0.f}; }
        };

        set(f.planes[0], r3[0] + r0[0], r3[1] + r0[1], r3[2] + r0[2], r3[3] + r0[3]); // left
        set(f.planes[1], r3[0] - r0[0], r3[1] - r0[1], r3[2] - r0[2], r3[3] - r0[3]); // right
        set(f.planes[2], r3[0] + r1[0], r3[1] + r1[1], r3[2] + r1[2], r3[3] + r1[3]); // bottom
        set(f.planes[3], r3[0] - r1[0], r3[1] - r1[1], r3[2] - r1[2], r3[3] - r1[3]); // top
        set(f.planes[4], r2[0],         r2[1],         r2[2],         r2[3]);         // near
        set(f.planes[5], r3[0] - r2[0], r3[1] - r2[1], r3[2] - r2[2], r3[3] - r2[3]); // far
        return f;
    }

    // Conservative test: rejects only boxes fully outside a plane.
    bool intersectsAabb(float cx, float cy, float cz,
                        float hx, float hy, float hz) const
    {
        for (const auto& p : planes) {
            if (p.a == 0.f && p.b == 0.f && p.c == 0.f) continue;  // degenerate
            const float dist = p.a * cx + p.b * cy + p.c * cz + p.d;
            const float radius = hx * std::fabs(p.a) + hy * std::fabs(p.b) + hz * std::fabs(p.c);
            if (dist + radius < 0.f) return false;
        }
        return true;
    }
};

} // namespace dash
