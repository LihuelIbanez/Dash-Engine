// ═════════════════════════════════════════════════════════════════════════════
// test_event_system — EventDispatcher subscribe / emit / flush / clear
// ═════════════════════════════════════════════════════════════════════════════
#include "EventDispatcher.h"
#include "GameEvents.h"
#include <cstdio>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

// ── Test: subscribe + emit + flush → listener receives ───────────────────────
static void test_subscribe_emit_flush()
{
    std::printf("  test_subscribe_emit_flush\n");

    EventDispatcher dispatcher;
    int received = 0;

    dispatcher.subscribe<DamageEvent>([&](const DamageEvent& e) {
        ASSERT(e.damage == 42, "damage value correct");
        ++received;
    });

    dispatcher.emit(DamageEvent{ 1, 2, "Enemy", 42, 58 });
    ASSERT(received == 0, "not delivered before flush");

    dispatcher.flush();
    ASSERT(received == 1, "delivered exactly once after flush");
}

// ── Test: emit without flush → listener does not receive ─────────────────────
static void test_emit_no_flush()
{
    std::printf("  test_emit_no_flush\n");

    EventDispatcher dispatcher;
    int received = 0;

    dispatcher.subscribe<DamageEvent>([&](const DamageEvent&) {
        ++received;
    });

    dispatcher.emit(DamageEvent{});
    // No flush — should not deliver
    ASSERT(received == 0, "event not delivered without flush");
}

// ── Test: multiple subscribers → all receive ─────────────────────────────────
static void test_multiple_subscribers()
{
    std::printf("  test_multiple_subscribers\n");

    EventDispatcher dispatcher;
    int countA = 0, countB = 0, countC = 0;

    dispatcher.subscribe<DeathEvent>([&](const DeathEvent&) { ++countA; });
    dispatcher.subscribe<DeathEvent>([&](const DeathEvent&) { ++countB; });
    dispatcher.subscribe<DeathEvent>([&](const DeathEvent&) { ++countC; });

    dispatcher.emit(DeathEvent{ 99, 10.f, 10.f, "Goblin", 50 });
    dispatcher.flush();

    ASSERT(countA == 1, "subscriber A received event");
    ASSERT(countB == 1, "subscriber B received event");
    ASSERT(countC == 1, "subscriber C received event");
}

// ── Test: flush clears the queue ─────────────────────────────────────────────
static void test_flush_clears_queue()
{
    std::printf("  test_flush_clears_queue\n");

    EventDispatcher dispatcher;
    int received = 0;

    dispatcher.subscribe<LevelUpEvent>([&](const LevelUpEvent&) { ++received; });

    dispatcher.emit(LevelUpEvent{ 1, 2, 100 });
    dispatcher.flush();
    ASSERT(received == 1, "received once after first flush");

    // Second flush should not re-deliver anything
    dispatcher.flush();
    ASSERT(received == 1, "no re-delivery on second flush");
}

// ── Test: emit different type → does not affect other subscribers ─────────────
static void test_different_type_isolation()
{
    std::printf("  test_different_type_isolation\n");

    EventDispatcher dispatcher;
    int damageReceived = 0;
    int deathReceived  = 0;

    dispatcher.subscribe<DamageEvent>([&](const DamageEvent&) { ++damageReceived; });
    dispatcher.subscribe<DeathEvent> ([&](const DeathEvent&)  { ++deathReceived;  });

    dispatcher.emit(DamageEvent{});
    dispatcher.flush();

    ASSERT(damageReceived == 1, "DamageEvent subscriber received exactly one event");
    ASSERT(deathReceived  == 0, "DeathEvent subscriber unaffected by DamageEvent");
}

// ── Test: clear() removes all subscriptions ──────────────────────────────────
static void test_clear()
{
    std::printf("  test_clear\n");

    EventDispatcher dispatcher;
    int received = 0;

    dispatcher.subscribe<HealthChangeEvent>([&](const HealthChangeEvent&) { ++received; });

    dispatcher.emit(HealthChangeEvent{ 1, 100, 80, 100 });
    dispatcher.flush();
    ASSERT(received == 1, "received before clear");

    dispatcher.clear();

    dispatcher.emit(HealthChangeEvent{ 1, 80, 60, 100 });
    dispatcher.flush();
    ASSERT(received == 1, "no delivery after clear() — subscriptions removed");
}

// ── main ─────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("=== test_event_system ===\n");

    test_subscribe_emit_flush();
    test_emit_no_flush();
    test_multiple_subscribers();
    test_flush_clears_queue();
    test_different_type_isolation();
    test_clear();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
