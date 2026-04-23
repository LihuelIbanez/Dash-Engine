#include "assets/cache/AssetCache3D.h"

#include <cstdio>

namespace dash::vkexp {

CachedModel* AssetCache3D::get(const std::string& path)
{
    auto it = cache_.find(path);
    if (it != cache_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool AssetCache3D::has(const std::string& path) const
{
    return cache_.find(path) != cache_.end();
}

CachedModel& AssetCache3D::store(const std::string& path, CachedModel model)
{
    auto [it, inserted] = cache_.emplace(path, std::move(model));
    if (!inserted) {
        it->second = std::move(model);
    }
    std::fprintf(stdout, "[AssetCache3D] Cached model: %s\n", path.c_str());
    return it->second;
}

void AssetCache3D::clear(VkDevice device)
{
    for (auto& [path, cached] : cache_) {
        cached.meshBuffers.shutdown(device);
        TextureLoader::destroy(device, cached.texture);
    }
    cache_.clear();
    std::fprintf(stdout, "[AssetCache3D] Cache cleared.\n");
}

} // namespace dash::vkexp
