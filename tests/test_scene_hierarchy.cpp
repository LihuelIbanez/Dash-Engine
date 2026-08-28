// ═════════════════════════════════════════════════════════════════════════════
// test_scene_hierarchy — parentesco de entidades y comandos deshacibles
//
// Cubre EntityHierarchy.h (composicion de transforms, validacion de reparenteo,
// aplanado para los renderers) y los dos comandos que la UI dispara sobre una
// multiseleccion: ReparentEntityCommand y MultiEditComponentFieldCommand.
// ═════════════════════════════════════════════════════════════════════════════
#include "CommandStack.h"
#include "EntityHierarchy.h"
#include "MultiEditComponentFieldCommand.h"
#include "Reflection.h"
#include "ReparentEntityCommand.h"
#include "SceneData.h"
#include "World.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

using dash::editor::Transform3D;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

namespace {

bool nearlyEqual(float a, float b, float eps = 1e-3f)
{
    return std::fabs(a - b) <= eps;
}

bool sameTransform(const Transform3D& a, const Transform3D& b, float eps = 1e-3f)
{
    return nearlyEqual(a.x, b.x, eps) && nearlyEqual(a.y, b.y, eps)
        && nearlyEqual(a.z, b.z, eps)
        && nearlyEqual(a.yawDeg, b.yawDeg, eps)
        && nearlyEqual(a.pitchDeg, b.pitchDeg, eps)
        && nearlyEqual(a.rollDeg, b.rollDeg, eps)
        && nearlyEqual(a.scale, b.scale, eps);
}

EntityData makeEntity(uint64_t id, const char* name, uint64_t parentId,
                      const Transform3D& local)
{
    EntityData e;
    e.id = id;
    e.name = name;
    e.type = EntityData::Type::Enemy;
    e.parentId = parentId;
    dash::editor::setLocalTransform(e, local);
    return e;
}

// Cadena de 3 niveles: 1 (raiz) -> 2 -> 3, con rotacion y escala no triviales.
SceneData makeChainScene()
{
    SceneData s;
    Transform3D root;
    root.x = 10.f; root.y = 4.f; root.z = 1.f; root.yawDeg = 90.f; root.scale = 2.f;

    Transform3D mid;
    mid.x = 1.f; mid.y = 0.f; mid.z = 0.5f; mid.yawDeg = 30.f; mid.scale = 0.5f;

    Transform3D leaf;
    leaf.x = 0.f; leaf.y = 2.f; leaf.z = 0.f; leaf.pitchDeg = 15.f; leaf.scale = 3.f;

    s.entities.push_back(makeEntity(1, "Root", 0, root));
    s.entities.push_back(makeEntity(2, "Mid", 1, mid));
    s.entities.push_back(makeEntity(3, "Leaf", 2, leaf));
    s.nextEntityId = 4;
    return s;
}

const PropertyInfo* findProp(ComponentType t, const std::string& name)
{
    for (const auto& p : getComponentMeta(t).properties)
        if (p.name == name) return &p;
    return nullptr;
}

int readHealth(const SceneData& s, uint64_t id)
{
    const EntityData* e = dash::editor::findEntity(s, id);
    if (!e) return -1;
    for (const auto& c : e->components)
        if (const auto* h = std::get_if<HealthComponent>(&c)) return h->health;
    return -1;
}

} // namespace

// ── compose / relative son inversas exactas ──────────────────────────────────
static void test_compose_relative_round_trip()
{
    std::printf("  test_compose_relative_round_trip\n");

    Transform3D parent;
    parent.x = 5.f; parent.y = -3.f; parent.z = 2.f;
    parent.yawDeg = 37.f; parent.pitchDeg = 5.f; parent.rollDeg = -12.f;
    parent.scale = 1.75f;

    Transform3D local;
    local.x = 2.5f; local.y = 4.f; local.z = -1.25f;
    local.yawDeg = -20.f; local.pitchDeg = 8.f; local.rollDeg = 3.f;
    local.scale = 0.4f;

    const Transform3D world = dash::editor::composeTransform(parent, local);
    const Transform3D back  = dash::editor::relativeTransform(parent, world);
    ASSERT(sameTransform(back, local), "compose -> relative devuelve el local original");

    // Un padre identidad no cambia nada.
    const Transform3D identityParent;
    ASSERT(sameTransform(dash::editor::composeTransform(identityParent, local), local),
           "un padre identidad deja el local intacto");

    // El yaw del padre rota el offset del hijo: 90 grados manda +X a +Y.
    Transform3D rot;
    rot.yawDeg = 90.f;
    Transform3D offset;
    offset.x = 1.f;
    const Transform3D rotated = dash::editor::composeTransform(rot, offset);
    ASSERT(nearlyEqual(rotated.x, 0.f) && nearlyEqual(rotated.y, 1.f),
           "yaw 90 lleva el offset +X a +Y");
}

// ── mover el padre arrastra al hijo ──────────────────────────────────────────
static void test_parent_move_drags_child()
{
    std::printf("  test_parent_move_drags_child\n");

    SceneData s = makeChainScene();
    const Transform3D beforeMid  = dash::editor::worldTransform(s, 2);
    const Transform3D beforeLeaf = dash::editor::worldTransform(s, 3);

    EntityData* root = dash::editor::findEntity(s, 1);
    ASSERT(root != nullptr, "la raiz existe");
    Transform3D rootLocal = dash::editor::localTransform(*root);
    rootLocal.x += 3.f;
    rootLocal.y -= 1.5f;
    dash::editor::setLocalTransform(*root, rootLocal);

    const Transform3D afterMid  = dash::editor::worldTransform(s, 2);
    const Transform3D afterLeaf = dash::editor::worldTransform(s, 3);

    ASSERT(nearlyEqual(afterMid.x - beforeMid.x, 3.f), "el hijo acompana el desplazamiento X");
    ASSERT(nearlyEqual(afterMid.y - beforeMid.y, -1.5f), "el hijo acompana el desplazamiento Y");
    ASSERT(nearlyEqual(afterLeaf.x - beforeLeaf.x, 3.f), "el nieto tambien se mueve en X");
    ASSERT(nearlyEqual(afterLeaf.y - beforeLeaf.y, -1.5f), "el nieto tambien se mueve en Y");

    // La escala se acumula por la cadena: 2 * 0.5 * 3.
    ASSERT(nearlyEqual(afterLeaf.scale, 3.f), "la escala se compone a lo largo de la cadena");

    // setWorldTransform es el camino inverso: fija mundo, guarda local.
    Transform3D target = afterLeaf;
    target.x = 0.f;
    target.y = 0.f;
    dash::editor::setWorldTransform(s, 3, target);
    ASSERT(sameTransform(dash::editor::worldTransform(s, 3), target),
           "setWorldTransform deja la entidad en el mundo pedido");
}

// ── canReparent ──────────────────────────────────────────────────────────────
static void test_can_reparent_rules()
{
    std::printf("  test_can_reparent_rules\n");

    SceneData s = makeChainScene();

    ASSERT(!dash::editor::canReparent(s, 2, 2), "no se puede reparentar a si mismo");
    ASSERT(!dash::editor::canReparent(s, 1, 2), "no se puede reparentar a un hijo directo");
    ASSERT(!dash::editor::canReparent(s, 1, 3), "no se puede reparentar a un nieto");
    ASSERT(dash::editor::canReparent(s, 3, 0), "siempre se puede soltar en la raiz (0)");
    ASSERT(dash::editor::canReparent(s, 2, 0), "un hijo puede volverse raiz");
    ASSERT(dash::editor::canReparent(s, 3, 1), "un nieto puede colgar del abuelo");
    ASSERT(!dash::editor::canReparent(s, 0, 1), "la entidad 0 no es reparentable");
    ASSERT(!dash::editor::canReparent(s, 99, 1), "una entidad inexistente no es reparentable");
    ASSERT(!dash::editor::canReparent(s, 1, 99), "un padre inexistente se rechaza");
}

// ── isDescendantOf ───────────────────────────────────────────────────────────
static void test_is_descendant_of()
{
    std::printf("  test_is_descendant_of\n");

    SceneData s = makeChainScene();

    ASSERT(dash::editor::isDescendantOf(s, 2, 1), "2 desciende de 1");
    ASSERT(dash::editor::isDescendantOf(s, 3, 2), "3 desciende de 2");
    ASSERT(dash::editor::isDescendantOf(s, 3, 1), "3 desciende de 1 saltando un nivel");
    ASSERT(!dash::editor::isDescendantOf(s, 1, 3), "la relacion no es simetrica");
    ASSERT(!dash::editor::isDescendantOf(s, 1, 1), "nadie desciende de si mismo");
    ASSERT(!dash::editor::isDescendantOf(s, 1, 0), "la raiz virtual 0 no cuenta como ancestro");

    // Un hermano suelto no comparte la cadena.
    Transform3D t;
    t.x = 1.f;
    s.entities.push_back(makeEntity(4, "Sibling", 0, t));
    ASSERT(!dash::editor::isDescendantOf(s, 4, 1), "una raiz aparte no desciende de 1");

    ASSERT(dash::editor::childrenOf(s, 1).size() == 1, "1 tiene un solo hijo directo");
    const std::vector<uint64_t> roots = dash::editor::rootEntities(s);
    ASSERT(roots.size() == 2, "hay dos entidades raiz");
}

// ── flattenHierarchy ─────────────────────────────────────────────────────────
static void test_flatten_hierarchy()
{
    std::printf("  test_flatten_hierarchy\n");

    const SceneData s = makeChainScene();
    const SceneData flat = dash::editor::flattenHierarchy(s);

    ASSERT(flat.entities.size() == s.entities.size(), "el aplanado conserva la cantidad");

    bool allRoots = true;
    for (const auto& e : flat.entities) allRoots = allRoots && (e.parentId == 0);
    ASSERT(allRoots, "todos los parentId quedan en 0");

    for (const auto& e : s.entities) {
        const Transform3D world = dash::editor::worldTransform(s, e.id);
        const Transform3D flatLocal =
            dash::editor::localTransform(*dash::editor::findEntity(flat, e.id));
        ASSERT(sameTransform(flatLocal, world),
               "el local del aplanado es el world del original");
        // worldTransform sobre el aplanado tiene que dar lo mismo: ya no hay padres.
        ASSERT(sameTransform(dash::editor::worldTransform(flat, e.id), world),
               "el aplanado se ve igual para un renderer sin jerarquia");
    }

    // Los campos legacy x/y siguen el transform aplanado.
    const EntityData* flatLeaf = dash::editor::findEntity(flat, 3);
    const Transform3D leafWorld = dash::editor::worldTransform(s, 3);
    ASSERT(nearlyEqual(flatLeaf->x, leafWorld.x) && nearlyEqual(flatLeaf->y, leafWorld.y),
           "EntityData.x/y queda sincronizado con el transform de mundo");
}

// ── ReparentEntityCommand: undo/redo conserva el mundo ───────────────────────
static void test_reparent_command_undo_redo()
{
    std::printf("  test_reparent_command_undo_redo\n");

    SceneData s = makeChainScene();
    World world;
    CommandStack stack;

    const Transform3D worldBefore = dash::editor::worldTransform(s, 3);
    const Transform3D localBefore = dash::editor::localTransform(*dash::editor::findEntity(s, 3));

    stack.execute(std::make_unique<ReparentEntityCommand>(3, 1), s, world);
    ASSERT(dash::editor::findEntity(s, 3)->parentId == 1, "el nieto cuelga del abuelo");
    ASSERT(sameTransform(dash::editor::worldTransform(s, 3), worldBefore),
           "el reparenteo conserva la pose de mundo");
    ASSERT(stack.canUndo(), "el comando quedo en el historial");

    stack.undo(s, world);
    ASSERT(dash::editor::findEntity(s, 3)->parentId == 2, "el undo restaura el padre");
    ASSERT(sameTransform(dash::editor::localTransform(*dash::editor::findEntity(s, 3)),
                         localBefore),
           "el undo restaura el transform local exacto");
    ASSERT(sameTransform(dash::editor::worldTransform(s, 3), worldBefore),
           "el undo deja la pose de mundo intacta");

    stack.redo(s, world);
    ASSERT(dash::editor::findEntity(s, 3)->parentId == 1, "el redo vuelve a reparentar");
    ASSERT(sameTransform(dash::editor::worldTransform(s, 3), worldBefore),
           "el redo conserva la pose de mundo");

    // Soltar en la raiz tambien conserva el mundo.
    stack.execute(std::make_unique<ReparentEntityCommand>(3, 0), s, world);
    ASSERT(dash::editor::findEntity(s, 3)->parentId == 0, "el nieto pasa a ser raiz");
    ASSERT(sameTransform(dash::editor::worldTransform(s, 3), worldBefore),
           "soltar en la raiz conserva la pose de mundo");

    // Un ciclo se rechaza y no ensucia la escena.
    stack.execute(std::make_unique<ReparentEntityCommand>(1, 2), s, world);
    ASSERT(dash::editor::findEntity(s, 1)->parentId == 0, "el reparenteo ciclico no se aplica");
}

// ── MultiEditComponentFieldCommand sobre 3 entidades ─────────────────────────
static void test_multi_edit_command_undo_redo()
{
    std::printf("  test_multi_edit_command_undo_redo\n");

    SceneData s = makeChainScene();
    World world;
    CommandStack stack;

    const int initial[3] = {40, 55, 70};
    for (int i = 0; i < 3; ++i) {
        HealthComponent h;
        h.health = initial[i];
        h.maxHealth = 100;
        s.entities[static_cast<std::size_t>(i)].components.push_back(h);
    }

    const PropertyInfo* prop = findProp(ComponentType::Health, "health");
    ASSERT(prop != nullptr, "Health.health esta expuesto por reflexion");
    if (!prop) return;

    std::vector<MultiEditComponentFieldCommand::Target> targets;
    for (int i = 0; i < 3; ++i)
        targets.push_back({static_cast<uint64_t>(i + 1), PropertyValue{initial[i]}});

    auto cmd = std::make_unique<MultiEditComponentFieldCommand>(
        targets, ComponentType::Health, prop->offset, prop->type,
        PropertyValue{99}, prop->name);
    ASSERT(cmd->targetCount() == 3, "el comando abarca las 3 entidades");
    ASSERT(std::string(cmd->name()).find("(3)") != std::string::npos,
           "el nombre del comando muestra la cantidad");

    stack.execute(std::move(cmd), s, world);
    ASSERT(readHealth(s, 1) == 99, "la entidad 1 recibe el valor");
    ASSERT(readHealth(s, 2) == 99, "la entidad 2 recibe el valor");
    ASSERT(readHealth(s, 3) == 99, "la entidad 3 recibe el valor");

    // Un solo undo tiene que revertir las tres.
    stack.undo(s, world);
    ASSERT(readHealth(s, 1) == initial[0], "el undo restaura la entidad 1");
    ASSERT(readHealth(s, 2) == initial[1], "el undo restaura la entidad 2");
    ASSERT(readHealth(s, 3) == initial[2], "el undo restaura la entidad 3");
    ASSERT(!stack.canUndo(), "un solo comando cubre toda la multiseleccion");

    stack.redo(s, world);
    ASSERT(readHealth(s, 1) == 99, "el redo reaplica a la entidad 1");
    ASSERT(readHealth(s, 2) == 99, "el redo reaplica a la entidad 2");
    ASSERT(readHealth(s, 3) == 99, "el redo reaplica a la entidad 3");

    // Editar Transform.x tiene que sincronizar el x legacy de EntityData.
    const PropertyInfo* txProp = findProp(ComponentType::Transform, "x");
    ASSERT(txProp != nullptr, "Transform.x esta expuesto por reflexion");
    if (!txProp) return;

    std::vector<MultiEditComponentFieldCommand::Target> tfTargets;
    for (uint64_t id = 1; id <= 3; ++id)
        tfTargets.push_back({id, PropertyValue{dash::editor::localTransform(
            *dash::editor::findEntity(s, id)).x}});

    stack.execute(std::make_unique<MultiEditComponentFieldCommand>(
        tfTargets, ComponentType::Transform, txProp->offset, txProp->type,
        PropertyValue{7.5f}, txProp->name), s, world);

    for (uint64_t id = 1; id <= 3; ++id) {
        const EntityData* e = dash::editor::findEntity(s, id);
        ASSERT(nearlyEqual(dash::editor::localTransform(*e).x, 7.5f),
               "el TransformComponent recibe el valor");
        ASSERT(nearlyEqual(e->x, 7.5f), "EntityData.x queda sincronizado");
    }

    stack.undo(s, world);
    for (uint64_t id = 1; id <= 3; ++id) {
        const EntityData* e = dash::editor::findEntity(s, id);
        ASSERT(nearlyEqual(e->x, dash::editor::localTransform(*e).x),
               "el undo deja EntityData.x sincronizado");
    }
}

int main()
{
    std::printf("=== test_scene_hierarchy ===\n");

    test_compose_relative_round_trip();
    test_parent_move_drags_child();
    test_can_reparent_rules();
    test_is_descendant_of();
    test_flatten_hierarchy();
    test_reparent_command_undo_redo();
    test_multi_edit_command_undo_redo();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
