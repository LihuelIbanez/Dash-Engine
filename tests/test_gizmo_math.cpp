// ═════════════════════════════════════════════════════════════════════════════
// test_gizmo_math — matematica pura detras de los gizmos del viewport
//
// GizmoMath no depende de ImGui ni de Vulkan, asi que todo el picking y el
// arrastre se pueden verificar sin abrir el editor. Las matrices usan el mismo
// layout column-major que EditorApp::buildViewProjMatrix.
// ═════════════════════════════════════════════════════════════════════════════
#include "GizmoMath.h"

#include <cmath>
#include <cstdio>

using namespace dash::gizmo;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

namespace {

bool nearlyEqual(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

// C = A * B, ambos column-major (m[col*4 + row]).
void matMul(const float a[16], const float b[16], float out[16])
{
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            out[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0]
                           + a[1 * 4 + r] * b[c * 4 + 1]
                           + a[2 * 4 + r] * b[c * 4 + 2]
                           + a[3 * 4 + r] * b[c * 4 + 3];
        }
    }
}

// Misma convencion que EditorApp::buildViewProjMatrix (Vulkan, depth 0..1).
void buildViewProj(const Vec3& eye, const Vec3& target, float aspect, float out[16])
{
    Vec3 f = normalize(sub(target, eye));
    Vec3 r = normalize(cross(f, Vec3{0.f, 1.f, 0.f}));
    Vec3 u = cross(r, f);

    const float view[16] = {
        r.x, u.x, -f.x, 0.f,
        r.y, u.y, -f.y, 0.f,
        r.z, u.z, -f.z, 0.f,
        -dot(r, eye), -dot(u, eye), dot(f, eye), 1.f
    };

    const float fov = 45.f * 3.14159265358979f / 180.f;
    const float tanHalf = std::tan(fov * 0.5f);
    const float nearP = 0.1f, farP = 500.f;
    const float proj[16] = {
        1.f / (aspect * tanHalf), 0.f, 0.f, 0.f,
        0.f, -1.f / tanHalf, 0.f, 0.f,
        0.f, 0.f, farP / (nearP - farP), -1.f,
        0.f, 0.f, (farP * nearP) / (nearP - farP), 0.f
    };

    matMul(proj, view, out);
}

// Distancia perpendicular de un punto a la recta que define el rayo.
float distancePointToRay(const Ray& ray, const Vec3& p)
{
    const Vec3 dir = normalize(ray.dir);
    const Vec3 diff = sub(p, ray.origin);
    return length(sub(diff, mul(dir, dot(diff, dir))));
}

} // namespace

// ── invertMatrix4 ────────────────────────────────────────────────────────────
static void test_invert_matrix_gives_identity()
{
    std::printf("  test_invert_matrix_gives_identity\n");

    float viewProj[16];
    buildViewProj(Vec3{6.f, 9.f, 12.f}, Vec3{1.f, 0.f, -2.f}, 16.f / 9.f, viewProj);

    float inv[16];
    ASSERT(invertMatrix4(viewProj, inv), "la viewProj es invertible");

    float ident[16];
    matMul(viewProj, inv, ident);

    bool ok = true;
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            ok = ok && nearlyEqual(ident[c * 4 + r], (c == r) ? 1.f : 0.f, 1e-3f);
    ASSERT(ok, "M * M^-1 es la identidad");

    // Una matriz singular no se puede invertir.
    float singular[16] = {};
    singular[0] = 1.f;
    float dummy[16];
    ASSERT(!invertMatrix4(singular, dummy), "una matriz singular devuelve false");
}

// ── rayFromNdc ───────────────────────────────────────────────────────────────
static void test_ray_from_ndc()
{
    std::printf("  test_ray_from_ndc\n");

    const Vec3 eye{0.f, 10.f, 10.f};
    const Vec3 target{0.f, 0.f, 0.f};
    float viewProj[16], inv[16];
    buildViewProj(eye, target, 1.f, viewProj);
    ASSERT(invertMatrix4(viewProj, inv), "viewProj invertible");

    Ray center;
    ASSERT(rayFromNdc(inv, 0.f, 0.f, center), "el centro de pantalla da un rayo");

    // El rayo central mira exactamente al target y nace en el near plane.
    const Vec3 forward = normalize(sub(target, eye));
    const Vec3 dir = normalize(center.dir);
    ASSERT(dot(dir, forward) > 0.999f, "el rayo central apunta al target");
    ASSERT(length(sub(center.origin, eye)) < 0.2f, "el origen esta sobre el near plane");

    // Round-trip: proyectar un punto y reconstruir el rayo debe pasar por el.
    const Vec3 p{2.f, 1.f, -3.f};
    float nx = 0.f, ny = 0.f, nz = 0.f;
    ASSERT(projectToNdc(viewProj, p, nx, ny, nz), "el punto esta delante de la camara");
    ASSERT(nz >= 0.f && nz <= 1.f, "la profundidad NDC cae en el rango 0..1 de Vulkan");

    Ray through;
    ASSERT(rayFromNdc(inv, nx, ny, through), "se reconstruye el rayo desde el NDC");
    ASSERT(distancePointToRay(through, p) < 1e-2f, "el rayo pasa por el punto original");

    // Detras de la camara no hay proyeccion valida.
    float bx = 0.f, by = 0.f, bz = 0.f;
    ASSERT(!projectToNdc(viewProj, Vec3{0.f, 20.f, 30.f}, bx, by, bz),
           "un punto detras de la camara no proyecta");
}

// ── pickAxisHandle ───────────────────────────────────────────────────────────
static void test_pick_axis_handle()
{
    std::printf("  test_pick_axis_handle\n");

    const Vec3 origin{0.f, 0.f, 0.f};
    const float axisLen = 1.f;
    const float radius  = 0.2f;

    // Rayo que atraviesa (0.5, 0, 0): justo sobre el mango X.
    const Ray onX{Vec3{0.5f, 0.f, -10.f}, Vec3{0.f, 0.f, 1.f}};
    ASSERT(pickAxisHandle(onX, origin, axisLen, radius) == Axis::X, "acierta el eje X");

    const Ray onY{Vec3{0.f, 0.5f, -10.f}, Vec3{0.f, 0.f, 1.f}};
    ASSERT(pickAxisHandle(onY, origin, axisLen, radius) == Axis::Y, "acierta el eje Y");

    // Para Z el rayo tiene que venir de costado, si no queda paralelo al eje.
    const Ray onZ{Vec3{-10.f, 0.f, 0.5f}, Vec3{1.f, 0.f, 0.f}};
    ASSERT(pickAxisHandle(onZ, origin, axisLen, radius) == Axis::Z, "acierta el eje Z");

    // Lejos de todos los mangos no hay pick.
    const Ray away{Vec3{5.f, 5.f, -10.f}, Vec3{0.f, 0.f, 1.f}};
    ASSERT(pickAxisHandle(away, origin, axisLen, radius) == Axis::None,
           "devuelve None cuando el rayo pasa lejos");

    // Mas alla del largo del mango tampoco: el segmento termina en 1.0.
    const Ray beyondTip{Vec3{1.6f, 0.f, -10.f}, Vec3{0.f, 0.f, 1.f}};
    ASSERT(pickAxisHandle(beyondTip, origin, axisLen, radius) == Axis::None,
           "no pickea mas alla de la punta del mango");

    // El gizmo se reubica con el pivote.
    const Vec3 moved{4.f, 0.f, 0.f};
    const Ray onMovedX{Vec3{4.5f, 0.f, -10.f}, Vec3{0.f, 0.f, 1.f}};
    ASSERT(pickAxisHandle(onMovedX, moved, axisLen, radius) == Axis::X,
           "el pick sigue al pivote");
}

// ── axisDragDelta ────────────────────────────────────────────────────────────
static void test_axis_drag_delta()
{
    std::printf("  test_axis_drag_delta\n");

    const Vec3 origin{0.f, 0.f, 0.f};
    const Vec3 axisX = axisDirection(Axis::X);

    const Ray from{Vec3{0.f, 0.f, -10.f}, Vec3{0.f, 0.f, 1.f}};
    const Ray to  {Vec3{2.f, 0.f, -10.f}, Vec3{0.f, 0.f, 1.f}};

    float delta = 0.f;
    ASSERT(axisDragDelta(from, to, origin, axisX, delta), "el arrastre se proyecta");
    ASSERT(nearlyEqual(delta, 2.f, 1e-3f), "avanzar 2 en X da delta 2");

    float back = 0.f;
    ASSERT(axisDragDelta(to, from, origin, axisX, back), "el arrastre inverso se proyecta");
    ASSERT(nearlyEqual(back, -2.f, 1e-3f), "el sentido opuesto da delta negativo");

    // Un arrastre perpendicular al eje no lo mueve.
    const Ray up{Vec3{0.f, 3.f, -10.f}, Vec3{0.f, 0.f, 1.f}};
    float perp = 1.f;
    ASSERT(axisDragDelta(from, up, origin, axisX, perp), "el arrastre perpendicular se proyecta");
    ASSERT(nearlyEqual(perp, 0.f, 1e-3f), "moverse en Y no desplaza sobre X");

    // Un eje degenerado no produce delta.
    float bad = 0.f;
    ASSERT(!axisDragDelta(from, to, origin, Vec3{0.f, 0.f, 0.f}, bad),
           "un eje nulo devuelve false");
}

// ── snapTo / snapAngleDeg ────────────────────────────────────────────────────
static void test_snapping()
{
    std::printf("  test_snapping\n");

    ASSERT(nearlyEqual(snapTo(0.7f, 0.5f), 0.5f), "0.7 cae al 0.5 mas cercano");
    ASSERT(nearlyEqual(snapTo(0.8f, 0.5f), 1.0f), "0.8 sube a 1.0");
    ASSERT(nearlyEqual(snapTo(2.0f, 0.5f), 2.0f), "un multiplo exacto no se mueve");
    ASSERT(nearlyEqual(snapTo(-0.7f, 0.5f), -0.5f), "-0.7 cae a -0.5");
    ASSERT(nearlyEqual(snapTo(-0.8f, 0.5f), -1.0f), "-0.8 baja a -1.0");
    ASSERT(nearlyEqual(snapTo(3.3f, 1.0f), 3.0f), "redondea con paso 1");

    // Un paso nulo o negativo deja el valor intacto (snap desactivado).
    ASSERT(nearlyEqual(snapTo(1.234f, 0.f), 1.234f), "paso 0 no aplica snap");
    ASSERT(nearlyEqual(snapTo(1.234f, -1.f), 1.234f), "paso negativo no aplica snap");

    ASSERT(nearlyEqual(snapAngleDeg(37.f, 15.f), 30.f), "37 grados cae a 30");
    ASSERT(nearlyEqual(snapAngleDeg(38.f, 15.f), 45.f), "38 grados sube a 45");
    ASSERT(nearlyEqual(snapAngleDeg(-37.f, 15.f), -30.f), "-37 grados cae a -30");
    ASSERT(nearlyEqual(snapAngleDeg(-38.f, 15.f), -45.f), "-38 grados baja a -45");
    ASSERT(nearlyEqual(snapAngleDeg(90.f, 90.f), 90.f), "un multiplo exacto no se mueve");
}

int main()
{
    std::printf("=== test_gizmo_math ===\n");

    test_invert_matrix_gives_identity();
    test_ray_from_ndc();
    test_pick_axis_handle();
    test_axis_drag_delta();
    test_snapping();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
