#include "AISystem.h"
#include <cmath>
#include <algorithm>

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

    // Soft enemy-enemy separation to avoid overlapping packs.
    static constexpr float kMinSeparation = 0.85f;
    static constexpr float kSeparationStrength = 0.55f;

    auto& enemies = *ctx.enemies;
    for (size_t i = 0; i < enemies.size(); ++i) {
        Enemy* a = enemies[i].get();
        if (!a || !a->isAlive()) continue;

        for (size_t j = i + 1; j < enemies.size(); ++j) {
            Enemy* b = enemies[j].get();
            if (!b || !b->isAlive()) continue;

            const float dx = b->x - a->x;
            const float dy = b->y - a->y;
            const float distSq = dx * dx + dy * dy;
            if (distSq <= 1e-6f) continue;

            const float dist = std::sqrt(distSq);
            if (dist >= kMinSeparation) continue;

            const float overlap = kMinSeparation - dist;
            const float nx = dx / dist;
            const float ny = dy / dist;
            const float push = overlap * kSeparationStrength * 0.5f;

            const float ax = a->x - nx * push;
            const float ay = a->y - ny * push;
            const float bx = b->x + nx * push;
            const float by = b->y + ny * push;

            if (!ctx.world || ctx.world->isWalkable(ax, ay)) {
                a->x = std::clamp(ax, 0.5f, static_cast<float>(WORLD_W) - 1.5f);
                a->y = std::clamp(ay, 0.5f, static_cast<float>(WORLD_H) - 1.5f);
            }
            if (!ctx.world || ctx.world->isWalkable(bx, by)) {
                b->x = std::clamp(bx, 0.5f, static_cast<float>(WORLD_W) - 1.5f);
                b->y = std::clamp(by, 0.5f, static_cast<float>(WORLD_H) - 1.5f);
            }
        }
    }
}
