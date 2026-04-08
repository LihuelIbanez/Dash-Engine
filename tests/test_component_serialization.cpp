// ═════════════════════════════════════════════════════════════════════════════
// test_component_serialization — JSON roundtrip for all component types
// ═════════════════════════════════════════════════════════════════════════════
#include "Components.h"
#include "ComponentSerialization.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <stdexcept>

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

#define ASSERT_EQ(a, b, msg)   ASSERT((a) == (b), msg)
#define ASSERT_FEQ(a, b, msg)  ASSERT(std::fabs((a)-(b)) < 0.001f, msg)
#define ASSERT_STR(a, b, msg)  ASSERT(std::string(a) == std::string(b), msg)

// ── TransformComponent roundtrip ─────────────────────────────────────────────
static void test_transform_roundtrip()
{
    std::printf("  test_transform_roundtrip\n");

    TransformComponent orig;
    orig.x = 3.14f;
    orig.y = -2.72f;

    auto j    = componentToJson(ComponentVariant{orig});
    auto back = componentFromJson(j);

    ASSERT(std::holds_alternative<TransformComponent>(back), "back is TransformComponent");
    auto& t = std::get<TransformComponent>(back);
    ASSERT_FEQ(t.x, orig.x, "x preserved");
    ASSERT_FEQ(t.y, orig.y, "y preserved");
    ASSERT_STR(j["type"].get<std::string>(), "Transform", "type field");
}

// ── RenderComponent roundtrip ─────────────────────────────────────────────────
static void test_render_roundtrip()
{
    std::printf("  test_render_roundtrip\n");

    RenderComponent orig;
    orig.sprite  = "warrior_idle";
    orig.layer   = 2;
    orig.visible = false;

    auto back = componentFromJson(componentToJson(ComponentVariant{orig}));
    ASSERT(std::holds_alternative<RenderComponent>(back), "back is RenderComponent");
    auto& r = std::get<RenderComponent>(back);
    ASSERT_STR(r.sprite, orig.sprite, "sprite preserved");
    ASSERT_EQ(r.layer, orig.layer, "layer preserved");
    ASSERT_EQ(r.visible, orig.visible, "visible preserved");
}

// ── HealthComponent roundtrip ─────────────────────────────────────────────────
static void test_health_roundtrip()
{
    std::printf("  test_health_roundtrip\n");

    HealthComponent orig;
    orig.health    = 42;
    orig.maxHealth = 200;

    auto back = componentFromJson(componentToJson(ComponentVariant{orig}));
    ASSERT(std::holds_alternative<HealthComponent>(back), "back is HealthComponent");
    auto& h = std::get<HealthComponent>(back);
    ASSERT_EQ(h.health,    orig.health,    "health preserved");
    ASSERT_EQ(h.maxHealth, orig.maxHealth, "maxHealth preserved");
}

// ── ManaComponent roundtrip ───────────────────────────────────────────────────
static void test_mana_roundtrip()
{
    std::printf("  test_mana_roundtrip\n");

    ManaComponent orig;
    orig.mana    = 30;
    orig.maxMana = 80;

    auto back = componentFromJson(componentToJson(ComponentVariant{orig}));
    auto& m = std::get<ManaComponent>(back);
    ASSERT_EQ(m.mana,    orig.mana,    "mana preserved");
    ASSERT_EQ(m.maxMana, orig.maxMana, "maxMana preserved");
}

// ── StatsComponent roundtrip ──────────────────────────────────────────────────
static void test_stats_roundtrip()
{
    std::printf("  test_stats_roundtrip\n");

    StatsComponent orig;
    orig.attack      = 20;
    orig.defense     = 12;
    orig.magicAttack = 8;
    orig.speed       = 5;
    orig.critChance  = 0.15f;
    orig.level       = 7;
    orig.experience  = 350;
    orig.expToNextLevel = 500;

    auto back = componentFromJson(componentToJson(ComponentVariant{orig}));
    auto& s = std::get<StatsComponent>(back);
    ASSERT_EQ(s.attack,        orig.attack,        "attack");
    ASSERT_EQ(s.defense,       orig.defense,       "defense");
    ASSERT_EQ(s.magicAttack,   orig.magicAttack,   "magicAttack");
    ASSERT_EQ(s.speed,         orig.speed,         "speed");
    ASSERT_FEQ(s.critChance,   orig.critChance,    "critChance");
    ASSERT_EQ(s.level,         orig.level,         "level");
    ASSERT_EQ(s.experience,    orig.experience,    "experience");
    ASSERT_EQ(s.expToNextLevel, orig.expToNextLevel, "expToNextLevel");
}

// ── CombatComponent roundtrip ─────────────────────────────────────────────────
static void test_combat_roundtrip()
{
    std::printf("  test_combat_roundtrip\n");

    CombatComponent orig;
    orig.attackRange    = 2.5f;
    orig.attackCooldown = 1.2f;
    orig.cooldownTimer  = 0.3f;
    orig.isAttacking    = true;

    auto back = componentFromJson(componentToJson(ComponentVariant{orig}));
    auto& c = std::get<CombatComponent>(back);
    ASSERT_FEQ(c.attackRange,    orig.attackRange,    "attackRange");
    ASSERT_FEQ(c.attackCooldown, orig.attackCooldown, "attackCooldown");
    ASSERT_FEQ(c.cooldownTimer,  orig.cooldownTimer,  "cooldownTimer");
    ASSERT_EQ(c.isAttacking,     orig.isAttacking,    "isAttacking");
}

// ── AIComponent roundtrip (all behaviors) ────────────────────────────────────
static void test_ai_roundtrip()
{
    std::printf("  test_ai_roundtrip\n");

    auto check = [](AIComponent::Behavior beh, const char* label) {
        AIComponent orig;
        orig.behavior       = beh;
        orig.detectionRange = 8.f;
        orig.patrolRadius   = 4.f;

        auto back = componentFromJson(componentToJson(ComponentVariant{orig}));
        auto& a = std::get<AIComponent>(back);
        ASSERT(a.behavior == orig.behavior, label);
        ASSERT_FEQ(a.detectionRange, orig.detectionRange, "detectionRange");
        ASSERT_FEQ(a.patrolRadius,   orig.patrolRadius,   "patrolRadius");
    };

    check(AIComponent::Behavior::Idle,   "AI Idle roundtrip");
    check(AIComponent::Behavior::Patrol, "AI Patrol roundtrip");
    check(AIComponent::Behavior::Chase,  "AI Chase roundtrip");
    check(AIComponent::Behavior::Flee,   "AI Flee roundtrip");
}

// ── Unknown type throws ───────────────────────────────────────────────────────
static void test_unknown_type_throws()
{
    std::printf("  test_unknown_type_throws\n");

    nlohmann::json j;
    j["type"] = "NonExistentComponent";
    bool threw = false;
    try { componentFromJson(j); }
    catch (const std::runtime_error&) { threw = true; }
    ASSERT(threw, "unknown type throws runtime_error");
}

// ── componentTypeName <-> componentTypeFromName ────────────────────────────────
static void test_type_name_roundtrip()
{
    std::printf("  test_type_name_roundtrip\n");

    auto types = {
        ComponentType::Transform, ComponentType::Render, ComponentType::Health,
        ComponentType::Mana, ComponentType::Stats, ComponentType::Combat, ComponentType::AI
    };
    for (auto t : types) {
        std::string name = componentTypeName(t);
        ComponentType back = componentTypeFromName(name);
        ASSERT(back == t, ("type<->name roundtrip for " + name).c_str());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("=== test_component_serialization ===\n");

    test_transform_roundtrip();
    test_render_roundtrip();
    test_health_roundtrip();
    test_mana_roundtrip();
    test_stats_roundtrip();
    test_combat_roundtrip();
    test_ai_roundtrip();
    test_unknown_type_throws();
    test_type_name_roundtrip();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
