#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "rendering/textures/TextureLoader.h"
#include "rendering/vfx/ParticleSystem.h"
#include "rendering/vulkan/VkMath.h"

namespace dash::vkexp {

// ─────────────────────────────────────────────────────────────────────────────
// ParticleRenderer — the GPU half of the VFX system.
//
// Owns a procedurally built 4x4 sprite atlas, one host-visible instance buffer
// per swapchain image, and the two blend pipelines. Both batches of a frame go
// into the same buffer (alpha first, additive after) and are drawn with two
// instanced calls: a vkCmdDraw per particle would cost one push-constant write
// and one draw per sprite, which at a few hundred sprites is the dominant cost
// of the frame. The per-frame buffer is written once, sequentially, and read
// straight by the vertex stage.
//
// It has its own descriptor set layout instead of extending the scene one: the
// only thing it samples is the atlas, and set 0 of the scene layout is already
// full (bindings 0..6) and shared with the editor, which has no VFX pass.
// ─────────────────────────────────────────────────────────────────────────────
class ParticleRenderer {
public:
    ParticleRenderer() = default;
    ~ParticleRenderer() = default;

    ParticleRenderer(const ParticleRenderer&) = delete;
    ParticleRenderer& operator=(const ParticleRenderer&) = delete;

    // Instances a single frame may draw, across both blend modes.
    static constexpr uint32_t kMaxInstancesPerFrame = 4096;
    static constexpr uint32_t kAtlasCols = 4;
    static constexpr uint32_t kAtlasRows = 4;
    static constexpr uint32_t kAtlasCellPixels = 64;

    // Atlas, descriptors and the per-image instance buffers. Size independent:
    // the pipelines use dynamic viewport/scissor, so nothing here has to be
    // rebuilt when the target resizes.
    bool init(VkPhysicalDevice physicalDevice, VkDevice device,
              VkQueue graphicsQueue, VkCommandPool commandPool,
              VkRenderPass hdrRenderPass, uint32_t frameCount,
              const std::string& shaderDir);

    void shutdown(VkDevice device);

    bool valid() const
    {
        return alphaPipeline_ != VK_NULL_HANDLE && additivePipeline_ != VK_NULL_HANDLE
            && !instanceBuffers_.empty();
    }

    // Uploads this frame's instances and records the draws inside the already
    // begun HDR pass. Returns the number of draw calls emitted (0, 1 or 2).
    uint32_t record(VkCommandBuffer cmd, VkDevice device, uint32_t frameIndex,
                    VkExtent2D extent,
                    const Mat4& viewProj, const Vec3& camRight, const Vec3& camUp,
                    const std::vector<dash::vfx::ParticleInstance>& alphaBatch,
                    const std::vector<dash::vfx::ParticleInstance>& additiveBatch);

    // Instances actually submitted by the most recent record() call.
    uint32_t lastAlphaDrawn() const { return lastAlphaDrawn_; }
    uint32_t lastAdditiveDrawn() const { return lastAdditiveDrawn_; }

private:
    bool createAtlas(VkPhysicalDevice physicalDevice, VkDevice device,
                     VkQueue graphicsQueue, VkCommandPool commandPool);
    bool createDescriptors(VkDevice device);
    bool createBuffers(VkPhysicalDevice physicalDevice, VkDevice device, uint32_t frameCount);

    TextureResource atlas_{};

    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool      descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet       descriptorSet_ = VK_NULL_HANDLE;

    VkPipelineLayout alphaLayout_ = VK_NULL_HANDLE;
    VkPipeline       alphaPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout additiveLayout_ = VK_NULL_HANDLE;
    VkPipeline       additivePipeline_ = VK_NULL_HANDLE;

    std::vector<VkBuffer>       instanceBuffers_;
    std::vector<VkDeviceMemory> instanceMemories_;

    uint32_t lastAlphaDrawn_ = 0;
    uint32_t lastAdditiveDrawn_ = 0;
};

} // namespace dash::vkexp
