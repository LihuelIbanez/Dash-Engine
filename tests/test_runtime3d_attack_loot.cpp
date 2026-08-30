// ─────────────────────────────────────────────────────────────────────────────
// EnemySimulation3D — player attack input and loot drops.
//
// Drives the real simulation headless (no Vulkan, no terrain mesh) and reads
// back what it put on the event bus:
//   * with the attack binding released and the auto-attack fallback off, the
//     player deals no damage at all;
//   * with the binding held, the same swing path fires DamageEvent /
//     HealthChangeEvent / DeathEvent;
//   * a dead enemy emits LootDropEvent with its id and its position.
// ─────────────────────────────────────────────────────────────────────────────

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "events/EventDispatcher.h"
#include "events/GameEvents.h"
#include "game/runtime3d/EnemySimulation3D.h"
#include "scene/SceneData.h"

static int g_failures = 0;

#define ASSERT(cond, msg)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("  FAIL: %s\n", (msg));                              \
            ++g_failures;                                                    \
        } else {                                                             \
            std::printf("  ok:   %s\n", (msg));                              \
        }                                                                    \
    } while (0)

namespace {

constexpr float kPlayerX = 32.0f;
constexpr float kPlayerZ = 32.0f;

struct Recorder {
    std::vector<DamageEvent>    damage;
    std::vector<DeathEvent>     deaths;
    std::vector<LootDropEvent>  loot;

    void attach(EventDispatcher& bus)
    {
        bus.subscribe<DamageEvent>([this](const DamageEvent& e) { damage.push_back(e); });
        bus.subscribe<DeathEvent>([this](const DeathEvent& e) { deaths.push_back(e); });
        bus.subscribe<LootDropEvent>([this](const LootDropEvent& e) { loot.push_back(e); });
    }

    size_t damageDealtBy(uint64_t attackerId) const
    {
        size_t n = 0;
        for (const DamageEvent& e : damage) {
            if (e.attackerId == attackerId) ++n;
        }
        return n;
    }
};

// Player plus `count` skeletons parked inside the player's attack radius.
SceneData makeScene(int count)
{
    SceneData scene;
    scene.worldSeed = 42u;

    EntityData player;
    player.id = 1;
    player.name = "Player";
    player.type = EntityData::Type::Player;
    player.x = kPlayerX;
    player.y = kPlayerZ;
    scene.entities.push_back(player);

    for (int i = 0; i < count; ++i) {
        EntityData enemy;
        enemy.id = static_cast<uint64_t>(10 + i);
        enemy.name = "Skeleton";
        enemy.type = EntityData::Type::Enemy;
        enemy.x = kPlayerX + 0.6f + 0.1f * static_cast<float>(i);
        enemy.y = kPlayerZ;
        scene.entities.push_back(enemy);
    }
    return scene;
}

// Runs the sim without terrain: sampleHeight is never consulted, so the agents
// stay on the flat plane the test placed them on.
void tick(dash::runtime3d::EnemySimulation3D& sim, EventDispatcher& bus,
          bool attackInput, int steps)
{
    for (int i = 0; i < steps; ++i) {
        sim.update(1.0f / 60.0f, kPlayerX, kPlayerZ, attackInput, nullptr, false, bus);
        bus.flush();
    }
}

} // namespace

// ── Test: no attack input, no fallback → the player never swings ─────────────
static void test_no_input_no_player_damage()
{
    std::printf("test_no_input_no_player_damage\n");

    EventDispatcher bus;
    Recorder rec;
    rec.attach(bus);

    dash::runtime3d::EnemySimulation3D sim;
    sim.build(makeScene(1), {});
    sim.setAutoAttackFallback(false);

    tick(sim, bus, /*attackInput=*/false, 240);   // 4 seconds

    ASSERT(rec.damageDealtBy(1) == 0, "released attack binding deals no damage");
    ASSERT(rec.deaths.empty(), "no enemy dies without an attack");
    ASSERT(rec.loot.empty(), "no loot without a kill");
    // The enemies still act: this is the control that the sim really ran.
    ASSERT(!rec.damage.empty(), "enemies did attack the player meanwhile");
}

// ── Test: attack binding held → damage, death and loot ──────────────────────
static void test_attack_input_produces_damage_and_loot()
{
    std::printf("test_attack_input_produces_damage_and_loot\n");

    EventDispatcher bus;
    Recorder rec;
    rec.attach(bus);

    dash::runtime3d::EnemySimulation3D sim;
    sim.build(makeScene(3), {});
    sim.setAutoAttackFallback(false);

    tick(sim, bus, /*attackInput=*/false, 120);
    ASSERT(rec.damageDealtBy(1) == 0, "still nothing while the binding is released");

    tick(sim, bus, /*attackInput=*/true, 600);   // 10 seconds of held attack

    ASSERT(rec.damageDealtBy(1) > 0, "held attack binding produces DamageEvent");
    ASSERT(!rec.deaths.empty(), "held attack kills at least one enemy");

    bool namedPlayer = false;
    for (const DamageEvent& e : rec.damage) {
        if (e.attackerId == 1 && e.targetName == "Skeleton") namedPlayer = true;
    }
    ASSERT(namedPlayer, "the player's DamageEvent names the enemy it hit");

    ASSERT(!rec.loot.empty(), "a kill emits LootDropEvent");
    bool wellFormed = !rec.loot.empty();
    for (const LootDropEvent& e : rec.loot) {
        if (e.enemyId != "skeleton") wellFormed = false;
        if (e.items.empty()) wellFormed = false;
        for (const auto& item : e.items) {
            if (item.item.empty() || item.qty <= 0) wellFormed = false;
        }
        // Dropped where the enemy fell, next to the player.
        if (std::fabs(e.x - kPlayerX) > 4.0f || std::fabs(e.y - kPlayerZ) > 4.0f) {
            wellFormed = false;
        }
    }
    ASSERT(wellFormed, "LootDropEvent carries the enemy id, its position and real items");

    // The loot table came from assets/gameplay/loot_tables.json, not the
    // built-in fallback: "gold" is in both, "health_potion" only in the file.
    bool sawTableItem = false;
    for (const LootDropEvent& e : rec.loot) {
        for (const auto& item : e.items) {
            if (item.item == "health_potion" || item.item == "mana_potion") sawTableItem = true;
        }
    }
    std::printf("  info: %zu loot event(s); table-only item seen: %s\n",
                rec.loot.size(), sawTableItem ? "yes" : "no");

    for (const LootDropEvent& e : rec.loot) {
        std::printf("  [Loot] %s at (%.2f, %.2f):", e.enemyId.c_str(), e.x, e.y);
        for (const auto& item : e.items) std::printf(" %s x%d", item.item.c_str(), item.qty);
        std::printf("\n");
    }
}

// ── Test: the fallback retires the first time real input shows up ───────────
static void test_auto_attack_fallback_retires()
{
    std::printf("test_auto_attack_fallback_retires\n");

    EventDispatcher bus;
    Recorder rec;
    rec.attach(bus);

    dash::runtime3d::EnemySimulation3D sim;
    sim.build(makeScene(1), {});
    ASSERT(sim.autoAttackFallback(), "fallback is on for headless runs");

    tick(sim, bus, /*attackInput=*/false, 120);
    ASSERT(rec.damageDealtBy(1) > 0, "fallback swings when nobody is at the keyboard");

    tick(sim, bus, /*attackInput=*/true, 1);
    ASSERT(!sim.autoAttackFallback(), "real input retires the fallback");

    const size_t before = rec.damageDealtBy(1);
    tick(sim, bus, /*attackInput=*/false, 240);
    ASSERT(rec.damageDealtBy(1) == before,
           "with the fallback retired, releasing the binding stops the swings");
}

int main()
{
    std::printf("=== EnemySimulation3D: attack input + loot ===\n");
    test_no_input_no_player_damage();
    test_attack_input_produces_damage_and_loot();
    test_auto_attack_fallback_retires();

    if (g_failures == 0) {
        std::printf("ALL TESTS PASSED\n");
        return 0;
    }
    std::printf("%d FAILURE(S)\n", g_failures);
    return 1;
}
