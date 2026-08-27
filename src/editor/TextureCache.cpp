#include "TextureCache.h"
#include "game/rendering/stb_image.h"
#include <SDL2/SDL.h>

TextureCache& TextureCache::instance()
{
    static TextureCache inst;
    return inst;
}

SDL_Texture* TextureCache::load(SDL_Renderer* r, const std::string& path)
{
    auto it = cache_.find(path);
    if (it != cache_.end())
        return it->second;

    // Load via stb_image (handles PNG, BMP, JPG, etc.)
    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 4); // force RGBA
    if (!data)
        return nullptr;

    SDL_Texture* tex = SDL_CreateTexture(r,
        SDL_PIXELFORMAT_RGBA32,
        SDL_TEXTUREACCESS_STATIC,
        w, h);

    if (tex) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_UpdateTexture(tex, nullptr, data, w * 4);
        cache_[path] = tex;
    }

    stbi_image_free(data);
    return tex;
}

void TextureCache::invalidate(SDL_Renderer* r, const std::string& path)
{
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        SDL_DestroyTexture(it->second);
        cache_.erase(it);
    }
    load(r, path); // re-load immediately
}

void TextureCache::release(const std::string& path)
{
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        SDL_DestroyTexture(it->second);
        cache_.erase(it);
    }
}

void TextureCache::clear(SDL_Renderer* /*r*/)
{
    for (auto& [path, tex] : cache_)
        SDL_DestroyTexture(tex);
    cache_.clear();
}
