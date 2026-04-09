#pragma once

#include <SDL2/SDL.h>
#include <string>
#include <unordered_map>

// Runtime-only sprite renderer with local texture cache.
// Keeps game runtime independent from editor TextureCache.
class SpriteRenderer {
public:
    ~SpriteRenderer();

    void init(SDL_Renderer* renderer, const std::string& assetsDir);

    // Draw sprite by name (resolved as <assetsDir>/sprites/<name>.png).
    // Returns false if sprite cannot be loaded/drawn.
    bool draw(const std::string& spriteName,
              float screenX, float screenY,
              float pivotX, float pivotY);

    void clearCache();

private:
    struct SpriteTex {
        SDL_Texture* tex = nullptr;
        int width = 0;
        int height = 0;
    };

    SDL_Renderer* renderer_ = nullptr;
    std::string assetsDir_;
    std::unordered_map<std::string, SpriteTex> cache_;

    bool loadIfNeeded(const std::string& spriteName);
};
