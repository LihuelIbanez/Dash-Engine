#include "SpawnRewardSystem.h"
#include "EventDispatcher.h"
#include "GameEvents.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>

// Rolls loot from a table and emits LootDropEvent if anything drops.
static void rollLoot(const LootTableData& table, float x, float y,
                     const std::string& enemyId, EventDispatcher* events)
{
    LootDropEvent ev;
    ev.enemyId = enemyId;
    ev.x = x;
    ev.y = y;

    for (auto& drop : table.drops) {
        float roll = static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX);
        if (roll <= drop.chance) {
            int qty = drop.minQty;
            if (drop.maxQty > drop.minQty) {
                qty += std::rand() % (drop.maxQty - drop.minQty + 1);
            }
            ev.items.push_back({ drop.item, qty });
        }
    }

    if (!ev.items.empty()) {
        // Log the drops for immediate feedback
        std::printf("[Loot] %s drops:", enemyId.c_str());
        for (auto& it : ev.items)
            std::printf(" %s x%d", it.item.c_str(), it.qty);
        std::printf("\n");

        if (events)
            events->emit(ev);
    }
}

void SpawnRewardSystem::update(RuntimeContext& ctx)
{
    for (auto& e : *ctx.enemies) {
        if (!e->isAlive() && e->health == 0) {
            int oldLevel = ctx.player->stats.level;
            ctx.player->gainExperience(e->expReward);
            *ctx.score += 10;

            // ── Loot roll ────────────────────────────────────────────────────
            if (db_) {
                // Build lowercase enemy id from name
                std::string id = e->name;
                for (auto& c : id) c = static_cast<char>(std::tolower(c));

                if (const LootTableData* table = db_->findLootTableForEnemy(id)) {
                    rollLoot(*table, e->x, e->y, id, ctx.events);
                }
            }

            e->health = -1;

            // Emit level-up event if the player leveled
            if (ctx.events && ctx.player->stats.level > oldLevel) {
                ctx.events->emit(LevelUpEvent{
                    oldLevel,
                    ctx.player->stats.level,
                    ctx.player->stats.experience
                });
            }
        }
    }
    ctx.enemies->erase(
        std::remove_if(ctx.enemies->begin(), ctx.enemies->end(),
            [](const std::unique_ptr<Enemy>& e){ return e->health < 0; }),
        ctx.enemies->end()
    );
}
