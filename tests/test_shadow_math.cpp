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

// ── El snap a la grilla de texels ────────────────────────────────────────────
static void test_snap_to_texel_grid()
{
    std::printf("  test_snap_to_texel_grid\n");

    const Vec3 dir{0.0f, -1.0f, 0.0f};   // luz cenital: el plano del mapa es XZ
    ShadowVolume vol;
    vol.center = {0.0f, 0.0f, 0.0f};
    vol.radius = 16.0f;
    const float texel = shadowTexelWorldSize(vol, 1024);

    // Un centro ya alineado no se mueve.
    const ShadowVolume aligned = snapVolumeToTexelGrid(dir, vol, 1024);
    ASSERT(std::fabs(aligned.center.x) < 1e-4f, "un centro alineado no se desplaza");
    ASSERT(std::fabs(aligned.center.z) < 1e-4f, "un centro alineado no se desplaza en Z");
    ASSERT(std::fabs(aligned.radius - vol.radius) < 1e-4f, "el radio no cambia");

    // Un desplazamiento sub-texel se cancela: es justo el jitter que hace
    // hervir los bordes cuando el volumen sigue al jugador.
    ShadowVolume jittered = vol;
    jittered.center = {texel * 0.25f, 0.0f, texel * 0.25f};
    const ShadowVolume snapped = snapVolumeToTexelGrid(dir, jittered, 1024);
    ASSERT(std::fabs(snapped.center.x) < 1e-3f, "el jitter sub-texel en X se cancela");
    ASSERT(std::fabs(snapped.center.z) < 1e-3f, "el jitter sub-texel en Z se cancela");

    // Cualquier centro cae siempre en un multiplo entero de texel.
    ShadowVolume arbitrary = vol;
    arbitrary.center = {123.456f, 7.0f, -89.012f};
    const ShadowVolume onGrid = snapVolumeToTexelGrid(dir, arbitrary, 1024);
    for (float axis : {onGrid.center.x, onGrid.center.z}) {
        const float rem = std::fabs(axis / texel - std::round(axis / texel));
        ASSERT(rem < 1e-2f, "el centro queda sobre la grilla de texels");
    }

    // El centro no puede alejarse mas de medio texel del pedido.
    ASSERT(std::fabs(onGrid.center.x - arbitrary.center.x) <= texel * 0.5f + 1e-3f,
           "el snap mueve como mucho medio texel");
}

// ── Los cortes de cascada cubren el rango y crecen ───────────────────────────
static void test_cascade_splits()
{
    std::printf("  test_cascade_splits\n");

    float splits[kShadowCascades];
    computeCascadeSplits(0.5f, 100.0f, 0.7f, splits);

    ASSERT(splits[kShadowCascades - 1] > 99.9f &&
           splits[kShadowCascades - 1] < 100.1f,
           "la ultima cascada llega al limite pedido");

    for (int i = 1; i < kShadowCascades; ++i) {
        ASSERT(splits[i] > splits[i - 1], "los cortes son estrictamente crecientes");
    }
    ASSERT(splits[0] > 0.5f, "el primer corte queda por delante del near");

    // El reparto tiene que ser logaritmico-ish: la cascada cercana cubre mucho
    // menos rango que la lejana, que es lo que da nitidez a los pies.
    const float firstRange = splits[0];
    const float lastRange = splits[kShadowCascades - 1] - splits[kShadowCascades - 2];
    ASSERT(lastRange > firstRange, "la cascada lejana cubre mas rango que la cercana");

    // lambda = 0 es uniforme puro: los tramos deben ser iguales.
    float uniform[kShadowCascades];
    computeCascadeSplits(0.0f, 90.0f, 0.0f, uniform);
    ASSERT(std::fabs(uniform[0] - 30.0f) < 0.1f, "lambda=0 reparte uniforme");
}

// ── El volumen de una rebanada del frustum la contiene ───────────────────────
static void test_frustum_slice_volume()
{
    std::printf("  test_frustum_slice_volume\n");

    const Vec3 camPos{0.0f, 0.0f, 0.0f};
    const Vec3 forward{0.0f, 0.0f, -1.0f};
    const Vec3 right{1.0f, 0.0f, 0.0f};
    const Vec3 up{0.0f, 1.0f, 0.0f};
    const float fov = 60.0f * 3.14159265f / 180.0f;
    const float aspect = 16.0f / 9.0f;

    const ShadowVolume vol = frustumSliceVolume(camPos, forward, right, up,
                                                fov, aspect, 10.0f, 30.0f);

    // Las 8 esquinas de la rebanada tienen que caer dentro de la esfera.
    const float tanHalf = std::tanf(fov * 0.5f);
    for (float d : {10.0f, 30.0f}) {
        const float h = d * tanHalf;
        const float w = h * aspect;
        for (int sy = -1; sy <= 1; sy += 2) {
            for (int sx = -1; sx <= 1; sx += 2) {
                const Vec3 corner{w * sx, h * sy, -d};
                const float dx = corner.x - vol.center.x;
                const float dy = corner.y - vol.center.y;
                const float dz = corner.z - vol.center.z;
                ASSERT(std::sqrt(dx * dx + dy * dy + dz * dz) <= vol.radius + 1e-3f,
                       "la esquina de la rebanada entra en la esfera");
            }
        }
    }

    // El centro tiene que estar sobre el eje de vista, entre near y far.
    ASSERT(std::fabs(vol.center.x) < 1e-3f, "el centro esta sobre el eje en X");
    ASSERT(std::fabs(vol.center.y) < 1e-3f, "el centro esta sobre el eje en Y");
    ASSERT(vol.center.z < -10.0f && vol.center.z > -30.0f,
           "el centro cae dentro de la rebanada");

    // Una rebanada mas lejana es mas grande: es justo el motivo de las cascadas.
    const ShadowVolume farSlice = frustumSliceVolume(camPos, forward, right, up,
                                                     fov, aspect, 30.0f, 100.0f);
    ASSERT(farSlice.radius > vol.radius, "la rebanada lejana necesita mas radio");
}

int main()
{
    std::printf("=== test_shadow_math ===\n");

    test_volume_center_is_inside();
    test_inside_and_outside_the_volume();
    test_light_direction_orients_the_matrix();
    test_volume_from_bounds();
    test_texel_world_size();
    test_snap_to_texel_grid();
    test_cascade_splits();
    test_frustum_slice_volume();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
