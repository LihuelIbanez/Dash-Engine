#include "CombatSystem.h"
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
                int dmg = ctx.player->rollDamage() - e->stats.defense;
                e->takeDamage(std::max(1, dmg));
            }
        }
    }
    // Enemies attack player
    for (auto& e : *ctx.enemies) {
        if (!e->isAlive()) continue;
        if (e->attackedThisFrame()) {
            int dmg = e->rollDamage() - ctx.player->stats.defense;
            ctx.player->takeDamage(std::max(1, dmg));
        }
    }
}
