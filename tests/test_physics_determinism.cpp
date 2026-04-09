// ═════════════════════════════════════════════════════════════════════════════
// test_physics_determinism — fixed-step simulation must be reproducible
// ═════════════════════════════════════════════════════════════════════════════
#include "game/physics/PhysicsWorld.h"

#include <cmath>
#include <cstdio>

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

static bool nearlyEqual(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

static void setupBaseline(dash::physics::PhysicsWorld& world, int& planeId, int& cubeId)
{
    const bool ok = world.init();
    ASSERT(ok, "world.init() returns true");
    world.setGravity({0.0f, -9.8f, 0.0f});
    world.setRestitution(0.20f);

    planeId = world.createStaticPlane(-0.7f);
    cubeId = world.createDynamicBox({0.0f, 0.8f, 0.0f}, {0.30f, 0.30f, 0.30f}, 1.0f);
    ASSERT(planeId >= 0, "plane body id is valid");
    ASSERT(cubeId >= 0, "cube body id is valid");
}

static void test_same_steps_same_result()
{
    std::printf("  test_same_steps_same_result\n");

    dash::physics::PhysicsWorld a;
    dash::physics::PhysicsWorld b;
    int planeA = -1, cubeA = -1;
    int planeB = -1, cubeB = -1;
    setupBaseline(a, planeA, cubeA);
    setupBaseline(b, planeB, cubeB);

    (void)planeA;
    (void)planeB;

    constexpr float kDt = 1.0f / 60.0f;
    constexpr int kSteps = 600;
    for (int i = 0; i < kSteps; ++i) {
        a.step(kDt);
        b.step(kDt);
    }

    const dash::physics::Vec3 pa = a.position(cubeA);
    const dash::physics::Vec3 pb = b.position(cubeB);
    const dash::physics::Vec3 va = a.velocity(cubeA);
    const dash::physics::Vec3 vb = b.velocity(cubeB);

    ASSERT(nearlyEqual(pa.x, pb.x), "same setup => same x");
    ASSERT(nearlyEqual(pa.y, pb.y), "same setup => same y");
    ASSERT(nearlyEqual(pa.z, pb.z), "same setup => same z");
    ASSERT(nearlyEqual(va.y, vb.y), "same setup => same vy");
}

static void test_fixed_floor_penetration_bound()
{
    std::printf("  test_fixed_floor_penetration_bound\n");

    dash::physics::PhysicsWorld world;
    int plane = -1, cube = -1;
    setupBaseline(world, plane, cube);

    constexpr float kDt = 1.0f / 120.0f;
    for (int i = 0; i < 1200; ++i) {
        world.step(kDt);
    }

    const dash::physics::Vec3 p = world.position(cube);
    const float floorY = world.position(plane).y;
    const float halfY = 0.30f;
    const float minY = p.y - halfY;

    ASSERT(minY >= floorY - 1e-3f, "cube does not penetrate floor beyond tolerance");
}

int main()
{
    std::printf("=== test_physics_determinism ===\n");

    test_same_steps_same_result();
    test_fixed_floor_penetration_bound();

    std::printf("\n  Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
