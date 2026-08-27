// ═════════════════════════════════════════════════════════════════════════════
// test_frustum_culling — Sprint 15: frustum plane extraction and AABB rejection
// ═════════════════════════════════════════════════════════════════════════════
#include "rendering/Frustum.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

// ── Column-major helpers mirroring dash::vkexp::VkMath ──────────────────────
struct M4 { float m[16]{}; };

static M4 multiply(const M4& a, const M4& b)
{
    M4 out{};
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row) {
            float sum = 0.f;
            for (int k = 0; k < 4; ++k) sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            out.m[col * 4 + row] = sum;
        }
    return out;
}

static M4 perspective(float fovY, float aspect, float zn, float zf)
{
    M4 out{};
    const float f = 1.0f / std::tan(fovY * 0.5f);
    out.m[0] = f / aspect;
    out.m[5] = f;
    out.m[10] = zf / (zn - zf);
    out.m[11] = -1.0f;
    out.m[14] = (zf * zn) / (zn - zf);
    return out;
}

// Camera at the origin looking down -Z, Y up.
static M4 lookDownNegZ()
{
    M4 out{};
    out.m[0] = 1.f; out.m[5] = 1.f; out.m[10] = 1.f; out.m[15] = 1.f;
    return out;
}

static dash::Frustum makeFrustum()
{
    const M4 view = lookDownNegZ();
    const M4 proj = perspective(60.0f * 0.0174532925f, 16.0f / 9.0f, 0.1f, 100.0f);
    const M4 vp = multiply(proj, view);
    return dash::Frustum::fromViewProj(vp.m);
}

// ── Test: a point straight ahead is inside ──────────────────────────────────
static void test_point_in_front_is_visible()
{
    std::printf("  test_point_in_front_is_visible\n");
    const auto f = makeFrustum();
    ASSERT(f.intersectsAabb(0.f, 0.f, -10.f, 0.5f, 0.5f, 0.5f), "point 10 units ahead is visible");
    ASSERT(f.intersectsAabb(0.f, 0.f, -1.f, 0.1f, 0.1f, 0.1f), "point just ahead is visible");
}

// ── Test: geometry behind the camera is rejected ────────────────────────────
static void test_behind_camera_is_culled()
{
    std::printf("  test_behind_camera_is_culled\n");
    const auto f = makeFrustum();
    ASSERT(!f.intersectsAabb(0.f, 0.f, 50.f, 0.5f, 0.5f, 0.5f), "far behind the camera is culled");
    ASSERT(!f.intersectsAabb(0.f, 0.f, 5.f, 0.5f, 0.5f, 0.5f), "just behind the camera is culled");
}

// ── Test: geometry beyond the far plane is rejected ─────────────────────────
static void test_beyond_far_plane_is_culled()
{
    std::printf("  test_beyond_far_plane_is_culled\n");
    const auto f = makeFrustum();
    ASSERT(!f.intersectsAabb(0.f, 0.f, -500.f, 1.f, 1.f, 1.f), "beyond far plane is culled");
}

// ── Test: far off to the side is rejected ───────────────────────────────────
static void test_lateral_is_culled()
{
    std::printf("  test_lateral_is_culled\n");
    const auto f = makeFrustum();
    ASSERT(!f.intersectsAabb(500.f, 0.f, -10.f, 0.5f, 0.5f, 0.5f), "far right is culled");
    ASSERT(!f.intersectsAabb(-500.f, 0.f, -10.f, 0.5f, 0.5f, 0.5f), "far left is culled");
    ASSERT(!f.intersectsAabb(0.f, 500.f, -10.f, 0.5f, 0.5f, 0.5f), "far above is culled");
}

// ── Test: a huge box straddling the frustum is kept (conservative) ──────────
static void test_large_box_straddling_is_kept()
{
    std::printf("  test_large_box_straddling_is_kept\n");
    const auto f = makeFrustum();
    // Centre is off to the side, but the box is big enough to overlap the view.
    ASSERT(f.intersectsAabb(60.f, 0.f, -10.f, 100.f, 100.f, 100.f),
           "large straddling box is not culled");
}

// ── Test: culling must never reject something at the exact centre ───────────
static void test_no_false_negative_on_axis()
{
    std::printf("  test_no_false_negative_on_axis\n");
    const auto f = makeFrustum();
    for (float z = -1.f; z > -90.f; z -= 7.3f) {
        ASSERT(f.intersectsAabb(0.f, 0.f, z, 0.2f, 0.2f, 0.2f),
               "on-axis point inside the view distance stays visible");
    }
}

int main()
{
    std::printf("=== test_frustum_culling ===\n");

    test_point_in_front_is_visible();
    test_behind_camera_is_culled();
    test_beyond_far_plane_is_culled();
    test_lateral_is_culled();
    test_large_box_straddling_is_kept();
    test_no_false_negative_on_axis();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
