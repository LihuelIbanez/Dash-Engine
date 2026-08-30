// ═════════════════════════════════════════════════════════════════════════════
// test_procedural_mesh — generadores de props low-poly (src/rendering/mesh/
// ProceduralMesh.*). El modulo es CPU puro, asi que corre headless.
//
// Lo que realmente importa aca:
//  * DETERMINISMO: mismo seed => geometria byte a byte identica. Si esto se
//    rompe, dos corridas del mismo mapa ponen arboles distintos y el editor y
//    el runtime dejan de coincidir.
//  * VARIACION: seeds distintos => geometria distinta. Sin esto el bosque sale
//    clonado, que es exactamente lo que el sistema viene a evitar.
//  * FACETADO: cada triangulo tiene sus tres vertices propios (vertices ==
//    indices). Compartir vertices promediaria las normales y borraria el estilo.
//  * Particion trunk/foliage: los dos sub-modelos suman exactamente el modelo
//    completo, porque las dos ramas consumen el MISMO stream de random.
// ═════════════════════════════════════════════════════════════════════════════
#include "rendering/mesh/ProceduralMesh.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace dash::procmesh;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

static const ModelKind kAllKinds[] = {
    ModelKind::Conifer, ModelKind::Broadleaf, ModelKind::Rock,
    ModelKind::Bush,    ModelKind::Grass,     ModelKind::Stump, ModelKind::Log,
};

static bool sameGeometry(const MeshData& a, const MeshData& b)
{
    if (a.vertices.size() != b.vertices.size()) return false;
    if (a.indices.size() != b.indices.size()) return false;
    if (!a.vertices.empty() &&
        std::memcmp(a.vertices.data(), b.vertices.data(),
                    a.vertices.size() * sizeof(dash::vkexp::Vertex)) != 0) {
        return false;
    }
    return a.indices == b.indices;
}

// ── El mismo seed tiene que dar exactamente la misma malla ───────────────────
static void test_determinism()
{
    std::printf("  test_determinism\n");

    for (ModelKind kind : kAllKinds) {
        ModelParams p;
        p.kind = kind;
        p.seed = 4242;
        const MeshData a = generate(p);
        const MeshData b = generate(p);
        ASSERT(sameGeometry(a, b), "mismo seed => geometria identica");
    }

    // Y tambien via id, que es el camino que usa la escena.
    ModelParams viaId;
    ASSERT(parseMeshId("proc:conifer?seed=99", viaId), "id valido parsea");
    ModelParams direct;
    direct.kind = ModelKind::Conifer;
    direct.seed = 99;
    ASSERT(sameGeometry(generate(viaId), generate(direct)),
           "el id y los params directos generan lo mismo");
}

// ── Seeds distintos tienen que dar arboles distintos ─────────────────────────
static void test_seed_variation()
{
    std::printf("  test_seed_variation\n");

    for (ModelKind kind : kAllKinds) {
        int distinctCounts = 0;
        int distinctBounds = 0;
        MeshData first;

        for (uint32_t seed = 1; seed <= 8; ++seed) {
            ModelParams p;
            p.kind = kind;
            p.seed = seed;
            MeshData m = generate(p);
            if (seed == 1) { first = std::move(m); continue; }
            if (m.vertices.size() != first.vertices.size()) ++distinctCounts;
            if (std::fabs(m.extent(1) - first.extent(1)) > 1e-3f) ++distinctBounds;
        }

        // El conteo de vertices puede coincidir por casualidad; los bounds no.
        ASSERT(distinctBounds >= 5, "seeds distintos cambian el tamaño del modelo");
        ASSERT(distinctCounts + distinctBounds > 0, "seeds distintos cambian la geometria");
    }
}

// ── Ninguna combinacion de kind/part puede salir vacia ───────────────────────
static void test_no_empty_meshes()
{
    std::printf("  test_no_empty_meshes\n");

    const ModelPart parts[] = {ModelPart::All, ModelPart::Trunk, ModelPart::Foliage};
    for (ModelKind kind : kAllKinds) {
        for (ModelPart part : parts) {
            for (uint32_t seed = 0; seed < 6; ++seed) {
                ModelParams p;
                p.kind = kind;
                p.part = part;
                p.seed = seed;
                const MeshData m = generate(p);
                ASSERT(!m.empty(), "kind/part/seed produce geometria");
                ASSERT(m.triangleCount() >= 4, "al menos un solido cerrado");
            }
        }
    }
}

// ── Integridad del buffer: indices en rango, multiplos de 3, normales unitarias
static void test_buffer_integrity()
{
    std::printf("  test_buffer_integrity\n");

    for (ModelKind kind : kAllKinds) {
        for (uint32_t seed = 10; seed < 14; ++seed) {
            ModelParams p;
            p.kind = kind;
            p.seed = seed;
            const MeshData m = generate(p);

            ASSERT(m.indices.size() % 3 == 0, "los indices forman triangulos completos");

            bool inRange = true;
            for (uint32_t i : m.indices) {
                if (i >= m.vertices.size()) { inRange = false; break; }
            }
            ASSERT(inRange, "todo indice apunta dentro del vertex buffer");

            bool finite = true, unitNormals = true;
            for (const auto& v : m.vertices) {
                for (int a = 0; a < 3; ++a) {
                    if (!std::isfinite(v.position[static_cast<size_t>(a)])) finite = false;
                }
                const float n = std::sqrt(v.normal[0] * v.normal[0] + v.normal[1] * v.normal[1] +
                                          v.normal[2] * v.normal[2]);
                if (std::fabs(n - 1.0f) > 1e-3f) unitNormals = false;
            }
            ASSERT(finite, "ninguna posicion es NaN/inf");
            ASSERT(unitNormals, "toda normal de cara es unitaria");
        }
    }
}

// ── El estilo depende de no compartir vertices entre caras ───────────────────
static void test_faceted_topology()
{
    std::printf("  test_faceted_topology\n");

    for (ModelKind kind : kAllKinds) {
        ModelParams p;
        p.kind = kind;
        p.seed = 7;
        const MeshData m = generate(p);
        ASSERT(m.vertices.size() == m.indices.size(),
               "cada triangulo tiene sus 3 vertices propios (flat shading)");
    }
}

// ── Bounds razonables: nada de 0 ni de 10000 ─────────────────────────────────
static void test_reasonable_bounds()
{
    std::printf("  test_reasonable_bounds\n");

    struct Expect { ModelKind kind; float minH; float maxH; };
    const Expect expects[] = {
        {ModelKind::Conifer,   3.0f, 12.0f},
        {ModelKind::Broadleaf, 3.0f, 12.0f},
        {ModelKind::Rock,      0.2f,  3.0f},
        {ModelKind::Bush,      0.3f,  2.5f},
        {ModelKind::Grass,     0.1f,  1.0f},
        {ModelKind::Stump,     0.2f,  1.5f},
        {ModelKind::Log,       0.2f,  1.5f},
    };

    for (const Expect& e : expects) {
        for (uint32_t seed = 0; seed < 24; ++seed) {
            ModelParams p;
            p.kind = e.kind;
            p.seed = seed;
            const MeshData m = generate(p);
            const float h = m.extent(1);
            ASSERT(h > e.minH && h < e.maxH, "altura dentro del rango del tipo");
            ASSERT(m.extent(0) > 0.02f && m.extent(0) < 30.0f, "ancho X sano");
            ASSERT(m.extent(2) > 0.02f && m.extent(2) < 30.0f, "ancho Z sano");
        }
    }

    // El parametro height manda sobre el default sembrado.
    ModelParams tall;
    tall.kind = ModelKind::Conifer;
    tall.seed = 3;
    tall.height = 12.0f;
    ModelParams shortTree = tall;
    shortTree.height = 4.0f;
    ASSERT(generate(tall).extent(1) > generate(shortTree).extent(1) * 2.0f,
           "height escala el modelo");
}

// ── trunk + foliage particionan el modelo completo ───────────────────────────
static void test_part_partition()
{
    std::printf("  test_part_partition\n");

    const ModelKind split[] = {ModelKind::Conifer, ModelKind::Broadleaf, ModelKind::Bush};
    for (ModelKind kind : split) {
        ModelParams all;
        all.kind = kind;
        all.seed = 555;
        ModelParams trunk = all;
        trunk.part = ModelPart::Trunk;
        ModelParams foliage = all;
        foliage.part = ModelPart::Foliage;

        const MeshData a = generate(all);
        const MeshData t = generate(trunk);
        const MeshData f = generate(foliage);
        ASSERT(t.vertices.size() + f.vertices.size() == a.vertices.size(),
               "las partes suman el modelo entero");
        ASSERT(t.vertices.size() > 0 && f.vertices.size() > 0, "ninguna parte esta vacia");
        ASSERT(f.maxBounds[1] > t.maxBounds[1] * 0.9f, "la copa no queda debajo del tronco");
    }

    // Los tipos sin division devuelven el modelo entero para cualquier part.
    ModelParams rock;
    rock.kind = ModelKind::Rock;
    rock.seed = 12;
    ModelParams rockFoliage = rock;
    rockFoliage.part = ModelPart::Foliage;
    ASSERT(sameGeometry(generate(rock), generate(rockFoliage)),
           "una roca ignora part y devuelve todo");
}

// ── Parsing del esquema de id ────────────────────────────────────────────────
static void test_mesh_id_parsing()
{
    std::printf("  test_mesh_id_parsing\n");

    ASSERT(isProceduralMeshId("proc:rock"), "el prefijo se reconoce");
    ASSERT(!isProceduralMeshId("Wolf.dashmesh"), "un path no es procedural");
    ASSERT(!isProceduralMeshId("cube"), "el cubo builtin no es procedural");
    ASSERT(!isProceduralMeshId("C:/models/tree.gltf"),
           "una letra de unidad de Windows no confunde al prefijo");
    ASSERT(!isProceduralMeshId(""), "id vacio no es procedural");

    ModelParams p;
    ASSERT(parseMeshId("proc:conifer", p) && p.kind == ModelKind::Conifer && p.seed == 0 &&
               p.part == ModelPart::All,
           "sin query se usan los defaults");

    ASSERT(parseMeshId("proc:broadleaf?seed=42&height=6.5&part=foliage", p), "query completa");
    ASSERT(p.kind == ModelKind::Broadleaf, "kind");
    ASSERT(p.seed == 42u, "seed");
    ASSERT(std::fabs(p.height - 6.5f) < 1e-4f, "height");
    ASSERT(p.part == ModelPart::Foliage, "part");

    ASSERT(parseMeshId("proc:rock?part=trunk&seed=8", p) && p.seed == 8u,
           "el orden de las claves no importa");
    ASSERT(parseMeshId("proc:grass?lod=2&seed=5", p) && p.seed == 5u,
           "una clave desconocida se ignora");

    ASSERT(!parseMeshId("proc:dragon?seed=1", p), "kind desconocido falla");
    ASSERT(!parseMeshId("proc:rock?seed=abc", p), "seed no numerico falla");
    ASSERT(!parseMeshId("proc:rock?height=-3", p), "height negativa falla");
    ASSERT(!parseMeshId("proc:rock?part=leaves", p), "part invalida falla");
    ASSERT(!parseMeshId("proc:rock?seed", p), "par sin '=' falla");
    ASSERT(!parseMeshId("assets/models/rock.gltf", p), "un path no parsea");

    ASSERT(std::string(kindName(ModelKind::Stump)) == "stump", "kindName");
    ASSERT(std::string(partName(ModelPart::Trunk)) == "trunk", "partName");
}

// ── Evidencia impresa: conteos por tipo y variacion entre seeds ──────────────
static void report_counts()
{
    std::printf("  --- conteos por tipo (seed 1 / seed 2) ---\n");
    for (ModelKind kind : kAllKinds) {
        ModelParams a;
        a.kind = kind;
        a.seed = 1;
        ModelParams b = a;
        b.seed = 2;
        const MeshData ma = generate(a);
        const MeshData mb = generate(b);
        std::printf("  %-10s seed1: %5zu v / %5zu i / %5.2f x %5.2f x %5.2f  |  "
                    "seed2: %5zu v / %5zu i / %5.2f x %5.2f x %5.2f\n",
                    kindName(kind), ma.vertices.size(), ma.indices.size(),
                    static_cast<double>(ma.extent(0)), static_cast<double>(ma.extent(1)),
                    static_cast<double>(ma.extent(2)), mb.vertices.size(), mb.indices.size(),
                    static_cast<double>(mb.extent(0)), static_cast<double>(mb.extent(1)),
                    static_cast<double>(mb.extent(2)));
    }
}

int main()
{
    std::printf("=== test_procedural_mesh ===\n");

    test_determinism();
    test_seed_variation();
    test_no_empty_meshes();
    test_buffer_integrity();
    test_faceted_topology();
    test_reasonable_bounds();
    test_part_partition();
    test_mesh_id_parsing();
    report_counts();

    std::printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
