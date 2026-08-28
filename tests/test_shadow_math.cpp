// ═════════════════════════════════════════════════════════════════════════════
// test_shadow_math — light-space matrix for directional shadow mapping
//
// The Vulkan side (render target, depth pass) cannot be tested without a
// device; this covers the maths that decides what the shadow map actually sees.
// ═════════════════════════════════════════════════════════════════════════════
#include "rendering/vulkan/ShadowMath.h"

#include <cmath>
#include <cstdio>
#include <string>

using namespace dash::vkexp;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

namespace {

struct Ndc { float x, y, z, w; };

Ndc project(const Mat4& m, const Vec3& p)
{
    // Column-major, same convention as VkMath::multiply.
    Ndc o;
    o.x = m.m[0] * p.x + m.m[4] * p.y + m.m[8]  * p.z + m.m[12];
    o.y = m.m[1] * p.x + m.m[5] * p.y + m.m[9]  * p.z + m.m[13];
    o.z = m.m[2] * p.x + m.m[6] * p.y + m.m[10] * p.z + m.m[14];
    o.w = m.m[3] * p.x + m.m[7] * p.y + m.m[11] * p.z + m.m[15];
    return o;
}

bool insideNdc(const Ndc& n)
{
    const float w = (std::fabs(n.w) > 1e-6f) ? n.w : 1.0f;
    const float x = n.x / w, y = n.y / w, z = n.z / w;
    return x >= -1.0f && x <= 1.0f && y >= -1.0f && y <= 1.0f && z >= 0.0f && z <= 1.0f;
}

} // namespace

// ── El centro del volumen siempre cae dentro del mapa ────────────────────────
static void test_volume_center_is_inside()
{
    std::printf("  test_volume_center_is_inside\n");

    ShadowVolume vol;
    vol.center = {10.0f, 2.0f, -5.0f};
    vol.radius = 20.0f;

    // Cualquier direccion de luz debe seguir cubriendo el centro.
    const Vec3 dirs[] = {
        {0.0f, -1.0f, 0.0f},
        {0.3f, -0.9f, 0.2f},
        {-0.7f, -0.5f, 0.5f},
        {1.0f, -0.1f, 0.0f},
    };
    for (const Vec3& d : dirs) {
        const Mat4 m = directionalLightMatrix(d, vol);
        ASSERT(insideNdc(project(m, vol.center)), "el centro cae dentro del NDC");
    }
}

// ── Un punto dentro del radio entra; uno muy afuera, no ──────────────────────
static void test_inside_and_outside_the_volume()
{
    std::printf("  test_inside_and_outside_the_volume\n");

    ShadowVolume vol;
    vol.center = {0.0f, 0.0f, 0.0f};
    vol.radius = 10.0f;
    const Mat4 m = directionalLightMatrix({0.0f, -1.0f, 0.0f}, vol);

    // La luz apunta hacia -Y, asi que el plano perpendicular es XZ.
    ASSERT(insideNdc(project(m, {5.0f, 0.0f, 5.0f})), "punto dentro del radio entra");
    ASSERT(insideNdc(project(m, {0.0f, 0.0f, 0.0f})), "el centro entra");
    ASSERT(!insideNdc(project(m, {200.0f, 0.0f, 0.0f})), "punto lejano en X queda afuera");
    ASSERT(!insideNdc(project(m, {0.0f, 0.0f, -200.0f})), "punto lejano en Z queda afuera");
}

// ── La direccion de la luz orienta la matriz ─────────────────────────────────
static void test_light_direction_orients_the_matrix()
{
    std::printf("  test_light_direction_orients_the_matrix\n");

    ShadowVolume vol;
    vol.center = {0.0f, 0.0f, 0.0f};
    vol.radius = 10.0f;

    const Mat4 down = directionalLightMatrix({0.0f, -1.0f, 0.0f}, vol);
    const Mat4 side = directionalLightMatrix({1.0f, 0.0f, 0.0f}, vol);

    bool differs = false;
    for (int i = 0; i < 16; ++i)
        if (std::fabs(down.m[i] - side.m[i]) > 1e-4f) { differs = true; break; }
    ASSERT(differs, "cambiar la direccion cambia la matriz");

    // Con la luz mirando hacia -Y, alejarse en Y es alejarse de la camara de luz:
    // ese eje se convierte en profundidad, no en X/Y del mapa.
    const Ndc near = project(down, {0.0f,  5.0f, 0.0f});
    const Ndc far  = project(down, {0.0f, -5.0f, 0.0f});
    ASSERT(near.z < far.z, "la profundidad crece alejandose de la luz");

    // Una direccion degenerada no debe producir NaN.
    const Mat4 degenerate = directionalLightMatrix({0.0f, 0.0f, 0.0f}, vol);
    bool finite = true;
    for (int i = 0; i < 16; ++i)
        if (!std::isfinite(degenerate.m[i])) { finite = false; break; }
    ASSERT(finite, "una direccion nula cae a un default finito");
}

// ── El volumen derivado de un AABB lo contiene ───────────────────────────────
static void test_volume_from_bounds()
{
    std::printf("  test_volume_from_bounds\n");

    const Vec3 lo{-4.0f, 0.0f, -6.0f};
    const Vec3 hi{ 8.0f, 4.0f,  2.0f};
    const ShadowVolume vol = shadowVolumeFromBounds(lo, hi);

    ASSERT(std::fabs(vol.center.x - 2.0f) < 1e-4f, "centro en X");
    ASSERT(std::fabs(vol.center.y - 2.0f) < 1e-4f, "centro en Y");
    ASSERT(std::fabs(vol.center.z - (-2.0f)) < 1e-4f, "centro en Z");

    // El radio tiene que alcanzar las esquinas del AABB.
    const float ex = (hi.x - lo.x) * 0.5f;
    const float ey = (hi.y - lo.y) * 0.5f;
    const float ez = (hi.z - lo.z) * 0.5f;
    ASSERT(vol.radius >= std::sqrt(ex * ex + ey * ey + ez * ez) - 1e-4f,
           "el radio cubre las esquinas");

    // Un AABB degenerado no puede dar radio cero: dividiriamos por cero al proyectar.
    const ShadowVolume tiny = shadowVolumeFromBounds({0, 0, 0}, {0, 0, 0});
    ASSERT(tiny.radius > 0.0f, "un AABB degenerado conserva un radio usable");
}

// ── El tamano de texel escala con el radio y la resolucion ───────────────────
static void test_texel_world_size()
{
    std::printf("  test_texel_world_size\n");

    ShadowVolume vol;
    vol.radius = 16.0f;

    const float at1024 = shadowTexelWorldSize(vol, 1024);
    const float at2048 = shadowTexelWorldSize(vol, 2048);

    ASSERT(at1024 > 0.0f, "tamano de texel positivo");
    ASSERT(std::fabs(at1024 - 2.0f * at2048) < 1e-4f,
           "duplicar la resolucion parte el texel al medio");
    ASSERT(shadowTexelWorldSize(vol, 0) == 0.0f, "resolucion cero no divide por cero");
}

int main()
{
    std::printf("=== test_shadow_math ===\n");

    test_volume_center_is_inside();
    test_inside_and_outside_the_volume();
    test_light_direction_orients_the_matrix();
    test_volume_from_bounds();
    test_texel_world_size();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
