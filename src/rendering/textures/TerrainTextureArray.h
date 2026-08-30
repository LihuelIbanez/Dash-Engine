#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vulkan/vulkan.h>

namespace dash::vkexp {

// One layer per TerrainTextureId, sampled by terrain.frag at set 0 binding 4
// (albedo) and binding 6 (tangent-space normal in RGB, roughness in A).
struct TerrainTextureArray {
    VkImage        image     = VK_NULL_HANDLE;
    VkDeviceMemory memory    = VK_NULL_HANDLE;
    VkImageView    view      = VK_NULL_HANDLE;
    VkSampler      sampler   = VK_NULL_HANDLE;
    uint32_t       extent    = 0;
    uint32_t       layers    = 0;
    uint32_t       mipLevels = 0;

    bool valid() const { return view != VK_NULL_HANDLE && sampler != VK_NULL_HANDLE; }
};

struct TerrainTextureSet {
    TerrainTextureArray albedo;
    TerrainTextureArray normal;

    bool valid() const { return albedo.valid() && normal.valid(); }
};

// `terrainRoot` holds the *_4k.blend source folders. Layers without a photo
// source are synthesised procedurally so every TerrainTextureId is populated.
bool createTerrainTextureSet(VkPhysicalDevice physicalDevice,
                             VkDevice device,
                             VkQueue graphicsQueue,
                             VkCommandPool commandPool,
                             const std::string& terrainRoot,
                             TerrainTextureSet& out);

void destroyTerrainTextureSet(VkDevice device, TerrainTextureSet& set);

// Build-time assets directory + "/models/terrain/".
std::string defaultTerrainTextureRoot();

} // namespace dash::vkexp
