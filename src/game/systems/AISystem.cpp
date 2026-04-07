#include "AISystem.h"
#include <cmath>

void AISystem::update(RuntimeContext& ctx)
{
    // Auto-attack nearby enemy when close enough (Diablo behaviour)
    if (ctx.player->hasTarget) {
        for (auto& e : *ctx.enemies) {
            if (!e->isAlive()) continue;
            float dx = e->x - ctx.player->x, dy = e->y - ctx.player->y;
            if (std::sqrt(dx*dx + dy*dy) < 1.2f && ctx.player->canAttack()) {
                ctx.player->triggerAttack();
                break;
            }
        }
    }
    for (auto& e : *ctx.enemies) {
        if (!e->isAlive()) continue;
        e->updateAI(ctx.dt, ctx.player->x, ctx.player->y, ctx.world);
        e->update(ctx.dt);
    }
}
