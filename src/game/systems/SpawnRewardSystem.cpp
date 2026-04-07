#include "SpawnRewardSystem.h"
#include <algorithm>

void SpawnRewardSystem::update(RuntimeContext& ctx)
{
    for (auto& e : *ctx.enemies) {
        if (!e->isAlive() && e->health == 0) {
            ctx.player->gainExperience(e->expReward);
            *ctx.score += 10;
            e->health = -1;
        }
    }
    ctx.enemies->erase(
        std::remove_if(ctx.enemies->begin(), ctx.enemies->end(),
            [](const std::unique_ptr<Enemy>& e){ return e->health < 0; }),
        ctx.enemies->end()
    );
}
