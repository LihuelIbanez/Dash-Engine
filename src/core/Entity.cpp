#include "Entity.h"
#include <algorithm>

Entity::Entity(float x, float y, int maxHp, const std::string& name)
    : x(x), y(y), health(maxHp), maxHealth(maxHp), name(name), alive(true)
{}

void Entity::takeDamage(int amount)
{
    health = std::max(0, health - amount);
    if (health == 0) alive = false;
}

void Entity::drawHealthBar(SDL_Renderer* renderer, float sx, float sy) const
{
    constexpr int BAR_W = 40;
    constexpr int BAR_H = 5;
    float ratio   = static_cast<float>(health) / static_cast<float>(maxHealth);
    float barX    = sx - BAR_W * 0.5f;
    float barY    = sy - TILE_H - 10.f;

    // Background (dark red)
    SDL_SetRenderDrawColor(renderer, 120, 20, 20, 220);
    SDL_FRect bg { barX, barY, static_cast<float>(BAR_W), static_cast<float>(BAR_H) };
    SDL_RenderFillRectF(renderer, &bg);

    // Foreground (green → red)
    Uint8 gr = static_cast<Uint8>(255 * (1.f - ratio));
    Uint8 gn = static_cast<Uint8>(255 * ratio);
    SDL_SetRenderDrawColor(renderer, gr, gn, 0, 220);
    SDL_FRect fg { barX, barY, BAR_W * ratio, static_cast<float>(BAR_H) };
    SDL_RenderFillRectF(renderer, &fg);
}
