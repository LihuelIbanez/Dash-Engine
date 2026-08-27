// ═════════════════════════════════════════════════════════════════════════════
// test_component_commands — backlog B1 (parte automatizable)
//
// Cubre la mecanica que respalda la checklist del Inspector sin necesidad de UI:
// que el componente este expuesto por reflexion, que agregar/quitar/editar pase
// por comandos deshacibles, y que el resultado sobreviva un round-trip de escena.
//
// Lo que NO se puede cubrir aca (dibujado real en el viewport) esta registrado
// como bugs B1-a/B1-b/B1-c en planning/tasks/24_BACKLOG_POST_SPRINT15.md.
// ═════════════════════════════════════════════════════════════════════════════
#include "AddComponentCommand.h"
#include "CommandStack.h"
#include "EditComponentFieldCommand.h"
#include "Reflection.h"
#include "RemoveComponentCommand.h"
#include "SceneData.h"
#include "World.h"

#include <cstdio>
#include <string>

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

namespace {

SceneData makeScene()
{
    SceneData s;
    EntityData e;
    e.id = 1;
    e.name = "Subject";
    e.type = EntityData::Type::Enemy;
    e.x = 4.0f;
    e.y = 6.0f;
    e.components.push_back(TransformComponent{4.0f, 6.0f});
    e.components.push_back(RenderComponent{});
    s.entities.push_back(e);
    s.nextEntityId = 2;
    return s;
}

const ComponentVariant* findComponent(const SceneData& s, ComponentType t)
{
    for (const auto& c : s.entities.front().components)
        if (getVariantType(c) == t) return &c;
    return nullptr;
}

const PropertyInfo* findProp(ComponentType t, const std::string& name)
{
    for (const auto& p : getComponentMeta(t).properties)
        if (p.name == name) return &p;
    return nullptr;
}

// Ejecuta una edicion de campo y devuelve el valor leido tras apply.
PropertyValue readProp(SceneData& s, ComponentType t, const PropertyInfo& prop)
{
    for (auto& c : s.entities.front().components)
        if (getVariantType(c) == t) return readFieldValue(fieldPtr(c, prop), prop.type);
    return PropertyValue{};
}

} // namespace

// ── Checklist 1: Physics esta expuesto por reflexion con todos sus campos ────
static void test_physics_exposed_by_reflection()
{
    std::printf("  test_physics_exposed_by_reflection\n");

    const ComponentMeta& meta = getComponentMeta(ComponentType::Physics);
    ASSERT(meta.name == "Physics", "el componente se llama Physics");

    // El desplegable "Add Component" se arma con estos nombres, asi que basta
    // con que el registro exista y este completo.
    const char* expected[] = {"shape", "halfExtentX", "halfExtentY",
                              "halfExtentZ", "mass", "isStatic"};
    for (const char* f : expected) {
        const std::string msg = std::string("Physics expone '") + f + "'";
        ASSERT(findProp(ComponentType::Physics, f) != nullptr, msg.c_str());
    }

    const PropertyInfo* shape = findProp(ComponentType::Physics, "shape");
    ASSERT(shape && shape->type == PropertyType::Enum, "shape es Enum");
    ASSERT(shape && !shape->enumValues.empty(), "shape declara sus valores");
}

// ── Checklist 2: agregar y quitar Physics es deshacible ─────────────────────
static void test_add_remove_physics_undo_redo()
{
    std::printf("  test_add_remove_physics_undo_redo\n");

    SceneData scene = makeScene();
    World world;
    CommandStack stack;

    ASSERT(findComponent(scene, ComponentType::Physics) == nullptr,
           "la entidad arranca sin Physics");

    PhysicsComponent phys;
    phys.mass = 7.5f;
    phys.isStatic = true;
    stack.execute(std::make_unique<AddComponentCommand>(1, ComponentVariant{phys}), scene, world);
    ASSERT(findComponent(scene, ComponentType::Physics) != nullptr, "Physics agregado");
    ASSERT(stack.canUndo(), "hay algo para deshacer");

    stack.undo(scene, world);
    ASSERT(findComponent(scene, ComponentType::Physics) == nullptr, "undo quita Physics");

    stack.redo(scene, world);
    const ComponentVariant* restored = findComponent(scene, ComponentType::Physics);
    ASSERT(restored != nullptr, "redo vuelve a agregar Physics");
    if (restored) {
        const auto& p = std::get<PhysicsComponent>(*restored);
        ASSERT(p.mass == 7.5f && p.isStatic, "redo conserva los valores del componente");
    }

    // Quitarlo tambien debe ser deshacible, conservando los valores.
    stack.execute(std::make_unique<RemoveComponentCommand>(1, *restored), scene, world);
    ASSERT(findComponent(scene, ComponentType::Physics) == nullptr, "Physics removido");

    stack.undo(scene, world);
    const ComponentVariant* back = findComponent(scene, ComponentType::Physics);
    ASSERT(back != nullptr, "undo restaura Physics");
    if (back) {
        const auto& p = std::get<PhysicsComponent>(*back);
        ASSERT(p.mass == 7.5f && p.isStatic, "undo restaura los valores exactos");
    }
}

// ── Checklist 3-6: editar campos pasa por comandos deshacibles ──────────────
static void test_edit_fields_undo_redo()
{
    std::printf("  test_edit_fields_undo_redo\n");

    SceneData scene = makeScene();
    World world;
    CommandStack stack;

    struct Case {
        const char*   label;
        ComponentType type;
        const char*   field;
        PropertyValue oldVal;
        PropertyValue newVal;
    };
    const Case cases[] = {
        {"yawDeg",     ComponentType::Transform, "yawDeg",     PropertyValue{0.0f},
                                                               PropertyValue{90.0f}},
        {"mesh",       ComponentType::Render,    "mesh",       PropertyValue{std::string("cube")},
                                                               PropertyValue{std::string("wolf")}},
        {"visible",    ComponentType::Render,    "visible",    PropertyValue{true},
                                                               PropertyValue{false}},
        {"renderMode", ComponentType::Render,    "renderMode", PropertyValue{0},
                                                               PropertyValue{1}},
    };

    for (const auto& c : cases) {
        const PropertyInfo* prop = findProp(c.type, c.field);
        const std::string expMsg = std::string(c.label) + " esta expuesto por reflexion";
        ASSERT(prop != nullptr, expMsg.c_str());
        if (!prop) continue;

        stack.execute(std::make_unique<EditComponentFieldCommand>(
                          1, c.type, prop->offset, prop->type,
                          c.oldVal, c.newVal, c.field),
                      scene, world);
        const std::string applyMsg = std::string(c.label) + ": apply escribe el valor nuevo";
        ASSERT(readProp(scene, c.type, *prop) == c.newVal, applyMsg.c_str());

        stack.undo(scene, world);
        const std::string undoMsg = std::string(c.label) + ": undo restaura el valor viejo";
        ASSERT(readProp(scene, c.type, *prop) == c.oldVal, undoMsg.c_str());

        stack.redo(scene, world);
        const std::string redoMsg = std::string(c.label) + ": redo reaplica el valor nuevo";
        ASSERT(readProp(scene, c.type, *prop) == c.newVal, redoMsg.c_str());
    }
}

// ── Checklist 7: guardar y recargar preserva todo lo editado ────────────────
static void test_scene_round_trip_preserves_edits()
{
    std::printf("  test_scene_round_trip_preserves_edits\n");

    SceneData scene = makeScene();
    World world;
    CommandStack stack;

    PhysicsComponent phys;
    phys.mass = 3.25f;
    phys.halfExtentY = 1.5f;
    phys.isStatic = true;
    stack.execute(std::make_unique<AddComponentCommand>(1, ComponentVariant{phys}), scene, world);

    for (auto& c : scene.entities.front().components) {
        if (auto* tf = std::get_if<TransformComponent>(&c)) {
            tf->yawDeg = 45.0f;
        } else if (auto* rc = std::get_if<RenderComponent>(&c)) {
            rc->mesh = "wolf";
            rc->material = "materials/stone.mat.json";
            rc->visible = false;
            rc->renderMode = static_cast<int>(RenderMode::BillboardSprite);
        }
    }

    SceneData reloaded;
    ASSERT(reloaded.loadFromJson(scene.toJson()), "la escena recarga sin errores");
    ASSERT(reloaded.entities.size() == 1, "una entidad tras recargar");
    if (reloaded.entities.empty()) return;

    bool sawTransform = false, sawRender = false, sawPhysics = false;
    for (const auto& c : reloaded.entities.front().components) {
        if (const auto* tf = std::get_if<TransformComponent>(&c)) {
            sawTransform = true;
            ASSERT(tf->yawDeg == 45.0f, "yawDeg preservado");
        } else if (const auto* rc = std::get_if<RenderComponent>(&c)) {
            sawRender = true;
            ASSERT(rc->mesh == "wolf", "mesh preservado");
            ASSERT(rc->material == "materials/stone.mat.json", "material preservado");
            ASSERT(rc->visible == false, "visible preservado");
            ASSERT(rc->renderMode == static_cast<int>(RenderMode::BillboardSprite),
                   "renderMode preservado");
        } else if (const auto* p = std::get_if<PhysicsComponent>(&c)) {
            sawPhysics = true;
            ASSERT(p->mass == 3.25f, "mass preservada");
            ASSERT(p->halfExtentY == 1.5f, "halfExtentY preservado");
            ASSERT(p->isStatic, "isStatic preservado");
        }
    }
    ASSERT(sawTransform, "Transform sobrevive el round-trip");
    ASSERT(sawRender, "Render sobrevive el round-trip");
    ASSERT(sawPhysics, "Physics sobrevive el round-trip");
}

int main()
{
    std::printf("=== test_component_commands ===\n");

    test_physics_exposed_by_reflection();
    test_add_remove_physics_undo_redo();
    test_edit_fields_undo_redo();
    test_scene_round_trip_preserves_edits();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
