// ═════════════════════════════════════════════════════════════════════════════
// test_prefab_system — D26: PrefabAsset load, instantiate, overrides
// ═════════════════════════════════════════════════════════════════════════════
#include "PrefabAsset.h"
#include "Components.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <string>

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

// Helper: resolve path relative to PROJECT_DIR
static std::string projectDir()
{
#ifdef PROJECT_DIR
    return std::string(PROJECT_DIR);
#else
    // Fallback: walk up from CWD looking for CMakeLists.txt at root
    namespace fs = std::filesystem;
    auto p = fs::current_path();
    for (int i = 0; i < 6; ++i) {
        if (fs::exists(p / "CMakeLists.txt") && fs::exists(p / "assets"))
            return p.string();
        p = p.parent_path();
    }
    return ".";
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: load goblin_warrior.json and verify metadata + component count
// ─────────────────────────────────────────────────────────────────────────────
static void test_load_and_instantiate()
{
    std::printf("  test_load_and_instantiate\n");

    std::string path = projectDir() + "/assets/prefabs/goblin_warrior.json";
    PrefabAsset p = loadPrefab(path);

    ASSERT(!p.guid.empty(),          "guid non-empty after load");
    ASSERT_STR(p.guid, "prefab-goblin-warrior-001", "correct guid");
    ASSERT_STR(p.name, "Goblin Warrior",             "correct name");

    // goblin_warrior.json has 5 components: Transform, Health, Stats, AI, Combat
    ASSERT_EQ(static_cast<int>(p.defaultComponents.size()), 5,
              "5 components in goblin_warrior prefab");

    auto instance = instantiate(p);
    ASSERT_EQ(static_cast<int>(instance.size()), 5, "instantiate() yields 5 components");

    // Verify it is a deep copy (modify instance, original unchanged)
    for (auto& comp : instance) {
        if (std::holds_alternative<HealthComponent>(comp)) {
            std::get<HealthComponent>(comp).health = 999;
        }
    }
    auto instance2 = instantiate(p);
    for (auto& comp : instance2) {
        if (std::holds_alternative<HealthComponent>(comp)) {
            ASSERT(std::get<HealthComponent>(comp).health != 999,
                   "instantiate() is a deep copy (original intact)");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: computeOverrides detects a modified field
// ─────────────────────────────────────────────────────────────────────────────
static void test_compute_overrides_modified()
{
    std::printf("  test_compute_overrides_modified\n");

    std::string path = projectDir() + "/assets/prefabs/goblin_warrior.json";
    PrefabAsset p = loadPrefab(path);

    auto instance = instantiate(p);

    // Modify Health.health to 45
    for (auto& comp : instance) {
        if (std::holds_alternative<HealthComponent>(comp)) {
            std::get<HealthComponent>(comp).health = 45;
        }
    }

    nlohmann::json overrides = computeOverrides(p, instance);

    ASSERT(overrides.contains("modified"), "overrides has 'modified' key");
    ASSERT(overrides.contains("added"),    "overrides has 'added' key");
    ASSERT(overrides.contains("removed"),  "overrides has 'removed' key");

    ASSERT(overrides["added"].empty(),   "no added components");
    ASSERT(overrides["removed"].empty(), "no removed components");

    ASSERT(overrides["modified"].contains("Health"),
           "modified contains Health");
    ASSERT(overrides["modified"]["Health"].contains("health"),
           "modified Health contains 'health' field");
    ASSERT_EQ(overrides["modified"]["Health"]["health"].get<float>(), 45.f,
              "overridden health value is 45");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: applyOverrides on a fresh instance produces the modified value
// ─────────────────────────────────────────────────────────────────────────────
static void test_apply_overrides()
{
    std::printf("  test_apply_overrides\n");

    std::string path = projectDir() + "/assets/prefabs/goblin_warrior.json";
    PrefabAsset p = loadPrefab(path);

    // Build an overrides payload manually
    nlohmann::json overrides;
    overrides["added"]    = nlohmann::json::array();
    overrides["removed"]  = nlohmann::json::array();
    overrides["modified"] = nlohmann::json::object();
    overrides["modified"]["Health"]["health"] = 45.f;

    auto instance = instantiate(p);
    applyOverrides(p, instance, overrides);

    bool found = false;
    for (const auto& comp : instance) {
        if (std::holds_alternative<HealthComponent>(comp)) {
            found = true;
            ASSERT_FEQ(std::get<HealthComponent>(comp).health, 45.f,
                       "applyOverrides sets health to 45");
        }
    }
    ASSERT(found, "HealthComponent present after applyOverrides");
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 4: loadPrefab with non-existent path returns empty guid
// ─────────────────────────────────────────────────────────────────────────────
static void test_load_nonexistent()
{
    std::printf("  test_load_nonexistent\n");

    PrefabAsset p = loadPrefab("/does/not/exist/missing.json");
    ASSERT(p.guid.empty(), "non-existent path returns empty guid");
    ASSERT(p.name.empty(), "non-existent path returns empty name");
}

// ─────────────────────────────────────────────────────────────────────────────
int main()
{
    std::printf("=== test_prefab_system ===\n");

    test_load_and_instantiate();
    test_compute_overrides_modified();
    test_apply_overrides();
    test_load_nonexistent();

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return (g_fail == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
