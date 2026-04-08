#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────────────────
// TextureCache — singleton that owns all SDL_Texture* loaded from disk.
//
// Usage:
//   SDL_Texture* t = TextureCache::instance().load(renderer, "/path/to/a.png");
//   // ... use t ...
//   // At shutdown:
//   TextureCache::instance().clear(renderer);
// ─────────────────────────────────────────────────────────────────────────────
class TextureCache {
public:
    static TextureCache& instance();

    // Returns cached texture, or loads it from 'path' via stb_image.
    // Returns nullptr if the file doesn't exist or fails to load.
    SDL_Texture* load(SDL_Renderer* r, const std::string& path);

    // Force-reload a specific entry (e.g. after the file was saved).
    void invalidate(SDL_Renderer* r, const std::string& path);

    // Release a single entry and destroy its texture.
    void release(const std::string& path);

    // Destroy all textures and clear the cache. Call before SDL_DestroyRenderer.
    void clear(SDL_Renderer* r);

private:
    TextureCache() = default;
    std::unordered_map<std::string, SDL_Texture*> cache_;
};
