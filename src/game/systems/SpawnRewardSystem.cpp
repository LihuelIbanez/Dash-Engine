#include "SpawnRewardSystem.h"
#include "EventDispatcher.h"
#include "GameEvents.h"
#include <algorithm>

void SpawnRewardSystem::update(RuntimeContext& ctx)
{
    for (auto& e : *ctx.enemies) {
        if (!e->isAlive() && e->health == 0) {
            int oldLevel = ctx.player->stats.level;
            ctx.player->gainExperience(e->expReward);
            *ctx.score += 10;
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
