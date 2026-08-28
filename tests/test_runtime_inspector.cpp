// ═════════════════════════════════════════════════════════════════════════════
// test_runtime_inspector — logica pura del panel de inspeccion de runtime
//
// El dibujado con ImGui no se puede testear headless, asi que RuntimeInspector
// deja fuera de la UI todo lo que decide algo: el diff contra el snapshot de
// Play, el filtro de texto y el buffer circular que alimenta el sparkline.
// ═════════════════════════════════════════════════════════════════════════════
#include "panels/RuntimeInspectorPanel.h"

#include <cstdio>
#include <string>

namespace ri = dash::editor::runtimeinspect;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

namespace {

EntityData makeEntity(uint64_t id, const char* name)
{
    EntityData e;
    e.id   = id;
    e.name = name;
    e.type = EntityData::Type::Enemy;
    return e;
}

// Un heroe con los cuatro tipos de campo que el diff tiene que distinguir:
// float (Transform.x), int (Health.health), bool (Render.visible) y string
// (Render.mesh).
SceneData makeScene()
{
    SceneData s;
    s.sceneName = "diff_fixture";

    EntityData hero = makeEntity(1, "Hero");
    TransformComponent tf;
    tf.x = 1.f; tf.y = 2.f; tf.z = 0.f;
    hero.components.push_back(tf);
    HealthComponent hp;
    hp.health = 100; hp.maxHealth = 100;
    hero.components.push_back(hp);
    RenderComponent rc;
    rc.mesh = "hero_mesh"; rc.visible = true;
    hero.components.push_back(rc);
    s.entities.push_back(hero);

    EntityData orc = makeEntity(2, "OrcGrunt");
    TransformComponent otf;
    otf.x = 8.f; otf.y = 8.f;
    orc.components.push_back(otf);
    HealthComponent ohp;
    ohp.health = 40; ohp.maxHealth = 40;
    orc.components.push_back(ohp);
    s.entities.push_back(orc);

    return s;
}

ComponentVariant* componentOf(SceneData& s, uint64_t id, ComponentType type)
{
    for (auto& e : s.entities) {
        if (e.id != id) continue;
        for (auto& c : e.components)
            if (getVariantType(c) == type) return &c;
    }
    return nullptr;
}

bool reports(const ri::SceneDiff& diff, uint64_t id, ComponentType type, const char* field)
{
    return diff.fieldChanged(id, type, field);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
void test_diff_detects_each_field_type()
{
    std::printf("\n[test_diff_detects_each_field_type]\n");

    SceneData baseline = makeScene();
    SceneData live     = baseline;

    std::get<TransformComponent>(*componentOf(live, 1, ComponentType::Transform)).x = 5.5f;
    std::get<HealthComponent>(*componentOf(live, 1, ComponentType::Health)).health = 42;
    std::get<RenderComponent>(*componentOf(live, 1, ComponentType::Render)).visible = false;
    std::get<RenderComponent>(*componentOf(live, 1, ComponentType::Render)).mesh = "hero_dead";

    const ri::SceneDiff diff = ri::diffScenes(baseline, live);

    ASSERT(reports(diff, 1, ComponentType::Transform, "x"), "detecta el float cambiado");
    ASSERT(reports(diff, 1, ComponentType::Health, "health"), "detecta el int cambiado");
    ASSERT(reports(diff, 1, ComponentType::Render, "visible"), "detecta el bool cambiado");
    ASSERT(reports(diff, 1, ComponentType::Render, "mesh"), "detecta el string cambiado");
    ASSERT(diff.changedFields.size() == 4, "reporta exactamente los 4 campos tocados");

    // Los vecinos de esos campos, dentro del mismo componente, no se reportan.
    ASSERT(!reports(diff, 1, ComponentType::Transform, "y"), "no reporta el float intacto");
    ASSERT(!reports(diff, 1, ComponentType::Transform, "scale"), "no reporta scale intacto");
    ASSERT(!reports(diff, 1, ComponentType::Health, "maxHealth"), "no reporta el int intacto");
    ASSERT(!reports(diff, 1, ComponentType::Render, "material"), "no reporta el string intacto");
    ASSERT(!reports(diff, 1, ComponentType::Render, "layer"), "no reporta layer intacto");

    ASSERT(diff.entityChanged(1), "la entidad tocada queda marcada");
    ASSERT(!diff.entityChanged(2), "la entidad intacta no queda marcada");

    // El before/after es lo que muestra el tooltip del panel.
    bool sawBeforeAfter = false;
    for (const auto& ch : diff.changedFields) {
        if (ch.key.component != ComponentType::Health || ch.key.field != "health") continue;
        sawBeforeAfter = (std::get<int>(ch.before) == 100 && std::get<int>(ch.after) == 42);
    }
    ASSERT(sawBeforeAfter, "guarda el valor previo y el actual del campo");
}

// ─────────────────────────────────────────────────────────────────────────────
void test_diff_ignores_identical_scenes()
{
    std::printf("\n[test_diff_ignores_identical_scenes]\n");

    SceneData baseline = makeScene();
    SceneData live     = baseline;

    const ri::SceneDiff diff = ri::diffScenes(baseline, live);
    ASSERT(diff.empty(), "dos escenas iguales no producen diff");
    ASSERT(diff.changedFields.empty(), "sin campos cambiados");
    ASSERT(diff.addedEntities.empty() && diff.removedEntities.empty(),
           "sin altas ni bajas de entidades");

    // Ruido numerico por debajo del epsilon no cuenta como cambio.
    std::get<TransformComponent>(*componentOf(live, 1, ComponentType::Transform)).x += 1e-7f;
    const ri::SceneDiff jitter = ri::diffScenes(baseline, live);
    ASSERT(jitter.changedFields.empty(), "el jitter float por debajo del epsilon se ignora");
}

// ─────────────────────────────────────────────────────────────────────────────
void test_diff_handles_added_and_removed_entities()
{
    std::printf("\n[test_diff_handles_added_and_removed_entities]\n");

    SceneData baseline = makeScene();
    SceneData live     = baseline;

    // El orco muere y aparece un proyectil: el caso tipico de un playtest.
    live.entities.erase(live.entities.begin() + 1);
    EntityData bullet = makeEntity(77, "Fireball");
    TransformComponent btf;
    btf.x = 3.f;
    bullet.components.push_back(btf);
    live.entities.push_back(bullet);

    const ri::SceneDiff diff = ri::diffScenes(baseline, live);

    ASSERT(diff.addedEntities.size() == 1 && diff.addedEntities[0] == 77,
           "reporta la entidad nueva");
    ASSERT(diff.entityAdded(77), "entityAdded encuentra la nueva");
    ASSERT(diff.removedEntities.size() == 1 && diff.removedEntities[0] == 2,
           "reporta la entidad borrada");
    ASSERT(diff.entityRemoved(2), "entityRemoved encuentra la borrada");
    ASSERT(!diff.entityRemoved(1), "la entidad que sigue viva no figura como borrada");
    ASSERT(diff.changedFields.empty(),
           "una entidad nueva no genera campos cambiados fantasma");

    // Un componente agregado en vivo no tiene contra que compararse.
    SceneData grown = baseline;
    for (auto& e : grown.entities)
        if (e.id == 2) e.components.push_back(RenderComponent{});
    const ri::SceneDiff addedComp = ri::diffScenes(baseline, grown);
    ASSERT(addedComp.changedFields.empty(),
           "un componente agregado en vivo no se reporta como campo cambiado");

    // Y el diff contra una escena vacia tampoco explota.
    const ri::SceneDiff wiped = ri::diffScenes(baseline, SceneData{});
    ASSERT(wiped.removedEntities.size() == 2, "escena vacia = todas las entidades borradas");
    ASSERT(wiped.addedEntities.empty(), "escena vacia no agrega nada");
}

// ─────────────────────────────────────────────────────────────────────────────
void test_filter_matches_entity_and_component()
{
    std::printf("\n[test_filter_matches_entity_and_component]\n");

    SceneData s = makeScene();
    const EntityData& hero = s.entities[0];
    const EntityData& orc  = s.entities[1];

    ASSERT(ri::entityMatchesFilter(hero, ""), "el filtro vacio deja pasar todo");

    ASSERT(ri::entityMatchesFilter(orc, "orc"), "matchea por nombre de entidad");
    ASSERT(ri::entityMatchesFilter(orc, "ORC"), "el nombre de entidad ignora mayusculas");
    ASSERT(ri::entityMatchesFilter(orc, "GrUnT"), "matchea un substring en el medio");
    ASSERT(!ri::entityMatchesFilter(orc, "hero"), "no matchea otro nombre de entidad");

    ASSERT(ri::entityMatchesFilter(hero, "render"), "matchea por nombre de componente");
    ASSERT(ri::entityMatchesFilter(hero, "RENDER"), "el componente ignora mayusculas");
    ASSERT(!ri::entityMatchesFilter(orc, "render"), "el orco no tiene Render");
    ASSERT(ri::entityMatchesFilter(orc, "health"), "el orco si tiene Health");

    // Con un hit por nombre de entidad se muestran todos sus componentes; con un
    // hit por componente, solo el que matchea.
    ASSERT(ri::componentMatchesFilter(hero, hero.components[1], "hero"),
           "el hit por entidad deja pasar cualquier componente");
    ASSERT(ri::componentMatchesFilter(hero, hero.components[2], "render"),
           "el hit por componente deja pasar el componente correcto");
    ASSERT(!ri::componentMatchesFilter(hero, hero.components[0], "render"),
           "el hit por componente descarta los demas componentes");

    ASSERT(ri::containsCaseInsensitive("Hero", ""), "needle vacio siempre matchea");
    ASSERT(!ri::containsCaseInsensitive("Hero", "Heroic"), "needle mas largo no matchea");
}

// ─────────────────────────────────────────────────────────────────────────────
void test_value_history_is_circular()
{
    std::printf("\n[test_value_history_is_circular]\n");

    ri::ValueHistory h(4);
    ASSERT(h.empty(), "arranca vacio");
    ASSERT(h.capacity() == 4, "respeta la capacidad pedida");

    h.push(1.f);
    h.push(2.f);
    h.push(3.f);
    ASSERT(h.size() == 3, "cuenta las muestras mientras no se llena");
    ASSERT(h.at(0) == 1.f && h.at(2) == 3.f, "conserva el orden de llegada");
    ASSERT(h.latest() == 3.f, "latest devuelve la ultima muestra");

    h.push(4.f);
    h.push(5.f);   // desborda: descarta el 1
    h.push(6.f);   // desborda: descarta el 2
    ASSERT(h.size() == 4, "nunca crece mas alla de la capacidad");
    ASSERT(h.at(0) == 3.f, "descarta la muestra mas vieja");
    ASSERT(h.at(3) == 6.f, "la ultima posicion es la muestra mas nueva");

    const std::vector<float> ordered = h.ordered();
    ASSERT(ordered.size() == 4, "ordered() devuelve exactamente las retenidas");
    ASSERT(ordered[0] == 3.f && ordered[1] == 4.f && ordered[2] == 5.f && ordered[3] == 6.f,
           "ordered() va de la mas vieja a la mas nueva");

    float lo = 0.f, hi = 0.f;
    h.minMax(lo, hi);
    ASSERT(lo == 3.f && hi == 6.f, "minMax cubre solo las muestras retenidas");

    h.clear();
    ASSERT(h.empty() && h.size() == 0, "clear vacia el buffer");
    ASSERT(h.at(0) == 0.f, "leer fuera de rango devuelve 0 en vez de romper");

    ri::ValueHistory degenerate(0);
    degenerate.push(9.f);
    ASSERT(degenerate.size() == 1 && degenerate.latest() == 9.f,
           "capacidad 0 se corrige a 1 en vez de dividir por cero");
}

// ─────────────────────────────────────────────────────────────────────────────
void test_value_formatting_and_plotting()
{
    std::printf("\n[test_value_formatting_and_plotting]\n");

    ASSERT(ri::valueToString(PropertyValue{42}) == "42", "formatea int");
    ASSERT(ri::valueToString(PropertyValue{true}) == "true", "formatea bool");
    ASSERT(ri::valueToString(PropertyValue{std::string("mesh")}) == "mesh", "formatea string");
    ASSERT(ri::valueToString(PropertyValue{1.5f}) == "1.5", "formatea float");

    ASSERT(!ri::valuesEqual(PropertyValue{1}, PropertyValue{1.f}),
           "int y float no son el mismo valor aunque numericamente coincidan");

    float out = -1.f;
    ASSERT(ri::asPlottable(PropertyValue{7}, out) && out == 7.f, "el int se puede graficar");
    ASSERT(ri::asPlottable(PropertyValue{true}, out) && out == 1.f, "el bool grafica como 0/1");
    ASSERT(!ri::asPlottable(PropertyValue{std::string("x")}, out),
           "el string no alimenta el sparkline");
}

// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("=== test_runtime_inspector ===\n");

    test_diff_detects_each_field_type();
    test_diff_ignores_identical_scenes();
    test_diff_handles_added_and_removed_entities();
    test_filter_matches_entity_and_component();
    test_value_history_is_circular();
    test_value_formatting_and_plotting();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
