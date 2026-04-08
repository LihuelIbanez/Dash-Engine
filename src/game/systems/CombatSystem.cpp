#include "CombatSystem.h"
#include "EventDispatcher.h"
#include "GameEvents.h"
#include <cmath>
#include <algorithm>

void CombatSystem::update(RuntimeContext& ctx)
{
    // Player attacks enemies
    if (ctx.player->attackedThisFrame()) {
        for (auto& e : *ctx.enemies) {
            if (!e->isAlive()) continue;
            float dx = e->x - ctx.player->x, dy = e->y - ctx.player->y;
            if (std::sqrt(dx*dx + dy*dy) <= 1.8f) {
                int oldHp = e->health;
                int dmg = ctx.player->rollDamage() - e->stats.defense;
                dmg = std::max(1, dmg);
                e->takeDamage(dmg);

                if (ctx.events) {
                    ctx.events->emit(DamageEvent{ 0, 0, e->name, dmg, e->health });
                    ctx.events->emit(HealthChangeEvent{ 0, oldHp, e->health, e->maxHealth });
                    if (!e->isAlive()) {
                        ctx.events->emit(DeathEvent{ 0, e->x, e->y, e->name, e->expReward });
                    }
                }
            }
        }
    }
    // Enemies attack player
    for (auto& e : *ctx.enemies) {
        if (!e->isAlive()) continue;
        if (e->attackedThisFrame()) {
            int oldHp = ctx.player->health;
            int dmg = e->rollDamage() - ctx.player->stats.defense;
            dmg = std::max(1, dmg);
            ctx.player->takeDamage(dmg);

            if (ctx.events) {
                ctx.events->emit(DamageEvent{ 0, 0, ctx.player->name, dmg, ctx.player->health });
                ctx.events->emit(HealthChangeEvent{ 0, oldHp, ctx.player->health, ctx.player->maxHealth });
                if (!ctx.player->isAlive()) {
                    ctx.events->emit(DeathEvent{ 0, ctx.player->x, ctx.player->y, ctx.player->name, 0 });
                }
            }
        }
    }
}
