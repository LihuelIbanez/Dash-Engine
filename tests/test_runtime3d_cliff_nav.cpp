// ═════════════════════════════════════════════════════════════════════════════
// test_runtime3d_cliff_nav — que el A* respete los acantilados del terreno 3D
//
// GridNav planifica sobre el grid 2D de tiles del World, que no sabe nada de
// los niveles de acantilado del TerrainMesh. Sin filtro, un enemigo planifica
// una ruta que atraviesa una pared de 12 unidades (CLIFF_STEP) y la "escala".
// Estos tests arman una meseta con una unica entrada y verifican:
//   1. que SIN filtro la ruta efectivamente atraviesa la pared (el bug),
//   2. que CON filtro no hay ruta a la meseta,
//   3. que la ruta al corredor si existe y nunca cambia de nivel,
//   4. que un arquetipo trepador (maxClimb=1) sube una pared de un nivel pero
//      no una de dos.
// ═════════════════════════════════════════════════════════════════════════════
#include "game/runtime3d/CliffNav.h"
#include "world/World.h"

#include <cstdio>
#include <vector>

using namespace dash::runtime3d;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

// Geometria de prueba, en coordenadas de tile:
//   x <  19          → nivel 0 (llanura)
//   x == 19          → la pared (cara con esquinas de dos niveles)
//   x >= 20          → nivel 1 (meseta)
//   filas 30..32     → corredor a nivel 0 que cruza de lado a lado
static constexpr int kWallX      = 19;
static constexpr int kPlateauX   = 20;
static constexpr int kGateYBegin = 30;
static constexpr int kGateYEnd   = 33;   // exclusivo

static void buildFlatWorld(World& world)
{
    for (int y = 0; y < WORLD_H; ++y) {
        for (int x = 0; x < WORLD_W; ++x) {
            world.grid[static_cast<size_t>(y)][static_cast<size_t>(x)] =
                Tile{TileType::Grass, true};
        }
    }
}

static void buildPlateau(TerrainMesh& terrain)
{
    for (int vy = 0; vy < TerrainMesh::VH; ++vy) {
        if (vy >= kGateYBegin && vy <= kGateYEnd) continue;   // el corredor
        for (int vx = kPlateauX; vx < TerrainMesh::VW; ++vx) {
            terrain.setCliffLevel(vx, vy, 1);
        }
    }
}

static bool pathCrosses(const std::vector<NavPoint>& path, int tileX)
{
    for (const NavPoint& p : path) {
        if (p.x == tileX) return true;
    }
    return false;
}

// ── El bug que se esta arreglando ────────────────────────────────────────────
static void test_unfiltered_path_walks_through_the_cliff()
{
    std::printf("  test_unfiltered_path_walks_through_the_cliff\n");

    World world;
    buildFlatWorld(world);
    TerrainMesh terrain;
    buildPlateau(terrain);

    const std::vector<NavPoint> path =
        GridNav::findPath(10, 10, 30, 10, world, 4096);

    ASSERT(!path.empty(), "sin filtro el A* encuentra ruta a la meseta");
    ASSERT(pathCrosses(path, kWallX),
           "y esa ruta atraviesa la pared del acantilado: el bug original");
}

// ── Con el filtro no hay forma de escalar ────────────────────────────────────
static void test_filtered_path_refuses_the_cliff()
{
    std::printf("  test_filtered_path_refuses_the_cliff\n");

    World world;
    buildFlatWorld(world);
    TerrainMesh terrain;
    buildPlateau(terrain);

    const std::vector<NavPoint> path =
        GridNav::findPath(10, 10, 30, 10, world, 8192, cliffStepFilter(terrain));

    ASSERT(path.empty(), "con el filtro no existe ruta que escale el acantilado");

    ASSERT(!cliffStepPassable(terrain, 18, 10, kWallX, 10),
           "entrar a la cara de pared esta prohibido");
    ASSERT(!cliffStepPassable(terrain, 18, 10, kPlateauX, 10),
           "y saltar directo a la meseta tambien");
    ASSERT(cliffStepPassable(terrain, 10, 10, 11, 10),
           "moverse dentro de la llanura sigue permitido");
    ASSERT(cliffStepPassable(terrain, 25, 10, 26, 10),
           "y moverse dentro de la meseta tambien");
}

// ── La ruta legitima existe y nunca cambia de nivel ──────────────────────────
static void test_path_detours_through_the_gate()
{
    std::printf("  test_path_detours_through_the_gate\n");

    World world;
    buildFlatWorld(world);
    TerrainMesh terrain;
    buildPlateau(terrain);

    const std::vector<NavPoint> path =
        GridNav::findPath(10, 10, 30, kGateYBegin + 1, world, 16384,
                          cliffStepFilter(terrain));

    ASSERT(!path.empty(), "hay ruta hasta el corredor a nivel 0");
    ASSERT(pathCrosses(path, kWallX), "y pasa por la columna del corredor");

    bool gateUsed = false;
    int  levelChanges = 0;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i].x == kWallX &&
            path[i].y >= kGateYBegin && path[i].y < kGateYEnd) {
            gateUsed = true;
        }
        ASSERT(tileCliffSpan(terrain, path[i].x, path[i].y) == 0,
               "ningun waypoint cae sobre una cara de pared");
        if (i > 0) {
            const int a = tileCliffLevel(terrain, path[i - 1].x, path[i - 1].y);
            const int b = tileCliffLevel(terrain, path[i].x, path[i].y);
            if (a != b) ++levelChanges;
        }
    }
    ASSERT(gateUsed, "el rodeo entra justo por el hueco del acantilado");
    ASSERT(levelChanges == 0, "ningun paso de la ruta cambia de nivel de acantilado");
}

// ── Arquetipos trepadores ────────────────────────────────────────────────────
static void test_climber_limits()
{
    std::printf("  test_climber_limits\n");

    TerrainMesh terrain;
    buildPlateau(terrain);
    // Segunda meseta, dos niveles por encima de la primera.
    for (int vy = 0; vy < TerrainMesh::VH; ++vy) {
        for (int vx = 40; vx < TerrainMesh::VW; ++vx) {
            terrain.setCliffLevel(vx, vy, 3);
        }
    }

    ASSERT(!cliffStepPassable(terrain, 18, 10, kWallX, 10, 0, 0),
           "sin capacidad de trepar, la pared de un nivel bloquea");
    ASSERT(cliffStepPassable(terrain, 18, 10, kWallX, 10, 1, 1),
           "un trepador de un nivel si entra a la pared de un nivel");
    ASSERT(cliffStepPassable(terrain, kWallX, 10, kPlateauX, 10, 1, 1),
           "y desde la pared alcanza la meseta");

    ASSERT(!cliffStepPassable(terrain, 38, 10, 39, 10, 1, 1),
           "pero la pared de dos niveles sigue siendo infranqueable");

    World world;
    buildFlatWorld(world);
    const std::vector<NavPoint> climbed =
        GridNav::findPath(10, 10, 30, 10, world, 8192, cliffStepFilter(terrain, 1, 1));
    ASSERT(!climbed.empty(), "el trepador si tiene ruta a la primera meseta");

    const std::vector<NavPoint> blocked =
        GridNav::findPath(10, 10, 45, 10, world, 16384, cliffStepFilter(terrain, 1, 1));
    ASSERT(blocked.empty(), "y ninguna a la meseta de dos niveles mas arriba");
}

// ── Compatibilidad: sin filtro nada cambia para el runtime 2D legacy ─────────
static void test_default_filter_is_transparent()
{
    std::printf("  test_default_filter_is_transparent\n");

    World world;   // generacion procedural real, con agua y montanas
    const std::vector<NavPoint> a = GridNav::findPath(20, 20, 40, 40, world, 4096);
    const std::vector<NavPoint> b =
        GridNav::findPath(20, 20, 40, 40, world, 4096, GridNav::StepFilter{});

    ASSERT(a.size() == b.size(), "un filtro vacio no altera el resultado");
    bool identical = a.size() == b.size();
    for (size_t i = 0; identical && i < a.size(); ++i) {
        if (a[i].x != b[i].x || a[i].y != b[i].y) identical = false;
    }
    ASSERT(identical, "la ruta es exactamente la misma que sin el parametro");
}

int main()
{
    std::printf("test_runtime3d_cliff_nav\n");

    test_unfiltered_path_walks_through_the_cliff();
    test_filtered_path_refuses_the_cliff();
    test_path_detours_through_the_gate();
    test_climber_limits();
    test_default_filter_is_transparent();

    std::printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
