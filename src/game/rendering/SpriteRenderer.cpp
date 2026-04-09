#include "rendering/SpriteRenderer.h"
#include "rendering/stb_image.h"

#include <filesystem>

namespace fs = std::filesystem;

SpriteRenderer::~SpriteRenderer()
{
    clearCache();
}

void SpriteRenderer::init(SDL_Renderer* renderer, const std::string& assetsDir)
{
    if (renderer_ != renderer) {
        clearCache();
        renderer_ = renderer;
    }
    assetsDir_ = assetsDir;
}

bool SpriteRenderer::loadIfNeeded(const std::string& spriteName)
{
    if (spriteName.empty() || spriteName == "default" || !renderer_) return false;

    auto it = cache_.find(spriteName);
    if (it != cache_.end() && it->second.tex) return true;

    std::string filePath = (fs::path(assetsDir_) / "sprites" / (spriteName + ".png")).string();

    int w = 0;
    int h = 0;
    int comp = 0;
    unsigned char* pixels = stbi_load(filePath.c_str(), &w, &h, &comp, 4);
    if (!pixels || w <= 0 || h <= 0) {
        if (pixels) stbi_image_free(pixels);
        return false;
    }

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        pixels,
        w, h,
        32,
        w * 4,
        SDL_PIXELFORMAT_RGBA32
    );

    if (!surface) {
        stbi_image_free(pixels);
        return false;
    }

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_FreeSurface(surface);
    stbi_image_free(pixels);

    if (!tex) return false;

    cache_[spriteName] = SpriteTex{tex, w, h};
    return true;
}

bool SpriteRenderer::draw(const std::string& spriteName,
                          float screenX, float screenY,
                          float pivotX, float pivotY)
{
    if (!loadIfNeeded(spriteName)) return false;

    auto it = cache_.find(spriteName);
    if (it == cache_.end() || !it->second.tex) return false;

    const SpriteTex& st = it->second;
    SDL_Rect dst = {
        static_cast<int>(screenX - pivotX * static_cast<float>(st.width)),
        static_cast<int>(screenY - pivotY * static_cast<float>(st.height)),
        st.width,
        st.height
    };

    SDL_RenderCopy(renderer_, st.tex, nullptr, &dst);
    return true;
}

void SpriteRenderer::clearCache()
{
    for (auto& kv : cache_) {
        if (kv.second.tex) {
            SDL_DestroyTexture(kv.second.tex);
            kv.second.tex = nullptr;
        }
    }
    cache_.clear();
}
