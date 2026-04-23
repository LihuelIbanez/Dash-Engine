#pragma once

#include <string>
#include <unordered_map>

#include <vulkan/vulkan.h>

#include "rendering/mesh/MeshBuffers.h"
#include "rendering/textures/TextureLoader.h"

namespace dash::vkexp {

struct CachedModel {
    MeshBuffers meshBuffers;
    TextureResource texture{};
    std::string hash;
};

class AssetCache3D {
public:
    CachedModel* get(const std::string& path);
    bool has(const std::string& path) const;
    CachedModel& store(const std::string& path, CachedModel model);
    void clear(VkDevice device);

private:
    std::unordered_map<std::string, CachedModel> cache_;
};

} // namespace dash::vkexp
