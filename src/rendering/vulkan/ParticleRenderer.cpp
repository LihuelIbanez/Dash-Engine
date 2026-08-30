#include "rendering/vulkan/ParticleRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "rendering/vulkan/PipelineBuilder.h"
#include "rendering/vulkan/SceneRenderer.h"

namespace dash::vkexp {

namespace {

constexpr float kPi = 3.14159265358979323846f;

float saturate(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }

// Smooth pseudo-noise over an angle, deterministic per frame. Three harmonics
// are enough to break a circle into something that reads as splatter.
float angularNoise(float angle, uint32_t seed)
{
    float sum = 0.f;
    float amp = 1.f;
    float freq = 3.f;
    for (int i = 0; i < 3; ++i) {
        const uint32_t h = seed * 2654435761u + static_cast<uint32_t>(i) * 40503u;
        const float phase = static_cast<float>(h & 0xFFFFu) / 65535.0f * 2.f * kPi;
        sum += amp * std::sin(angle * freq + phase);
        amp *= 0.5f;
        freq *= 2.1f;
    }
    return sum * 0.5f + 0.5f;  // → [0, 1]
}

// Soft round puff: smoke, mist, blood haze. Later frames are wider and softer.
float puffAlpha(float nx, float ny, int frame)
{
    const float t = static_cast<float>(frame) / 3.0f;
    const float r = std::sqrt(nx * nx + ny * ny);
    const float radius = 0.62f + 0.26f * t;
    const float softness = 0.30f + 0.42f * t;
    const float a = 1.f - saturate((r - (radius - softness)) / std::max(0.01f, softness));
    // Squared falloff keeps the core solid and the rim genuinely transparent.
    return a * a * (0.95f - 0.35f * t);
}

// Bright streak with a hot core, shrinking along its own axis frame by frame.
float sparkAlpha(float nx, float ny, int frame)
{
    const float t = static_cast<float>(frame) / 3.0f;
    const float stretch = 1.0f + 2.2f * (1.0f - t);
    const float sx = nx * (3.4f + 3.0f * t);
    const float sy = ny * (3.4f + 3.0f * t) / stretch;
    const float d = std::sqrt(sx * sx + sy * sy);
    const float core = std::exp(-d * d * 1.15f);
    const float glow = std::exp(-d * 0.85f) * 0.35f;
    return saturate(core + glow) * (1.0f - 0.25f * t);
}

// Expanding shockwave: an annulus that grows outward and thins as it goes.
float ringAlpha(float nx, float ny, int frame)
{
    const float t = static_cast<float>(frame) / 3.0f;
    const float r = std::sqrt(nx * nx + ny * ny);
    const float radius = 0.30f + 0.62f * t;
    const float thickness = 0.26f - 0.16f * t;
    const float d = std::fabs(r - radius) / std::max(0.02f, thickness);
    return saturate(1.0f - d * d) * (1.0f - 0.30f * t);
}

// Ragged blob for gore: a disc whose radius is modulated per angle.
float splatAlpha(float nx, float ny, int frame)
{
    const float t = static_cast<float>(frame) / 3.0f;
    const float r = std::sqrt(nx * nx + ny * ny);
    if (r < 1e-4f) return 1.0f;

    const float angle = std::atan2(ny, nx);
    const uint32_t seed = 17u + static_cast<uint32_t>(frame) * 977u;
    const float lobes = 0.66f + 0.34f * angularNoise(angle, seed);
    const float radius = (0.52f + 0.22f * t) * lobes;
    const float edge = 0.10f + 0.14f * t;
    return saturate((radius - r) / edge);
}

float cellAlpha(int row, int frame, float nx, float ny)
{
    switch (row) {
        case 0:  return puffAlpha(nx, ny, frame);
        case 1:  return sparkAlpha(nx, ny, frame);
        case 2:  return ringAlpha(nx, ny, frame);
        default: return splatAlpha(nx, ny, frame);
    }
}

} // namespace

bool ParticleRenderer::createAtlas(VkPhysicalDevice physicalDevice, VkDevice device,
                                   VkQueue graphicsQueue, VkCommandPool commandPool)
{
    const uint32_t width = kAtlasCols * kAtlasCellPixels;
    const uint32_t height = kAtlasRows * kAtlasCellPixels;
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 4u, 0);

    for (uint32_t row = 0; row < kAtlasRows; ++row) {
        for (uint32_t col = 0; col < kAtlasCols; ++col) {
            for (uint32_t py = 0; py < kAtlasCellPixels; ++py) {
                for (uint32_t px = 0; px < kAtlasCellPixels; ++px) {
                    const float nx = (static_cast<float>(px) + 0.5f)
                                   / static_cast<float>(kAtlasCellPixels) * 2.f - 1.f;
                    const float ny = (static_cast<float>(py) + 0.5f)
                                   / static_cast<float>(kAtlasCellPixels) * 2.f - 1.f;

                    const float a = saturate(cellAlpha(static_cast<int>(row),
                                                       static_cast<int>(col), nx, ny));

                    const uint32_t x = col * kAtlasCellPixels + px;
                    const uint32_t y = row * kAtlasCellPixels + py;
                    const size_t idx = (static_cast<size_t>(y) * width + x) * 4u;

                    // RGB stays white: the tint travels per instance, so one
                    // atlas serves blood, sparks and smoke alike.
                    pixels[idx + 0] = 255;
                    pixels[idx + 1] = 255;
                    pixels[idx + 2] = 255;
                    pixels[idx + 3] = static_cast<unsigned char>(a * 255.0f + 0.5f);
                }
            }
        }
    }

    if (!TextureLoader::createTextureFromPixels(physicalDevice, device, graphicsQueue,
                                                commandPool, pixels.data(), width, height,
                                                atlas_)) {
        std::fprintf(stderr, "[VFX] Failed to upload the particle atlas.\n");
        return false;
    }
    std::printf("[VFX] Particle atlas generated: %ux%u (%ux%u frames).\n",
                width, height, kAtlasCols, kAtlasRows);
    return true;
}

bool ParticleRenderer::createDescriptors(VkDevice device)
{
    VkDescriptorSetLayoutBinding atlasBinding{};
    atlasBinding.binding = 0;
    atlasBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    atlasBinding.descriptorCount = 1;
    atlasBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &atlasBinding;
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &setLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "[VFX] Failed to create the particle set layout.\n");
        return false;
    }

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "[VFX] Failed to create the particle descriptor pool.\n");
        return false;
    }

    VkDescriptorSetAllocateInfo alloc{};
    alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc.descriptorPool = descriptorPool_;
    alloc.descriptorSetCount = 1;
    alloc.pSetLayouts = &setLayout_;
    if (vkAllocateDescriptorSets(device, &alloc, &descriptorSet_) != VK_SUCCESS) {
        std::fprintf(stderr, "[VFX] Failed to allocate the particle descriptor set.\n");
        return false;
    }

    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = atlas_.imageView;
    imageInfo.sampler = atlas_.sampler;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet_;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.descriptorCount = 1;
    write.pImageInfo = &imageInfo;
    vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    return true;
}

bool ParticleRenderer::createBuffers(VkPhysicalDevice physicalDevice, VkDevice device,
                                     uint32_t frameCount)
{
    const VkDeviceSize bytes = static_cast<VkDeviceSize>(kMaxInstancesPerFrame)
                             * sizeof(float) * kParticleInstanceFloats;

    instanceBuffers_.assign(frameCount, VK_NULL_HANDLE);
    instanceMemories_.assign(frameCount, VK_NULL_HANDLE);
    for (uint32_t i = 0; i < frameCount; ++i) {
        if (!createHostVisibleBuffer(physicalDevice, device, bytes,
                                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                                     instanceBuffers_[i], instanceMemories_[i])) {
            std::fprintf(stderr, "[VFX] Failed to create particle instance buffer %u.\n", i);
            return false;
        }
    }
    return true;
}

bool ParticleRenderer::init(VkPhysicalDevice physicalDevice, VkDevice device,
                            VkQueue graphicsQueue, VkCommandPool commandPool,
                            VkRenderPass hdrRenderPass, uint32_t frameCount,
                            const std::string& shaderDir)
{
    if (frameCount == 0) return false;
    if (!createAtlas(physicalDevice, device, graphicsQueue, commandPool)) return false;
    if (!createDescriptors(device)) return false;
    if (!createBuffers(physicalDevice, device, frameCount)) return false;

    std::string error;
    if (!PipelineBuilder::createParticlePipeline(
            device, hdrRenderPass, setLayout_,
            shaderDir + "/particle.vert.spv", shaderDir + "/particle.frag.spv",
            /*additive=*/false, alphaLayout_, alphaPipeline_, error)) {
        std::fprintf(stderr, "[VFX] Particle alpha pipeline unavailable: %s\n", error.c_str());
        return false;
    }
    if (!PipelineBuilder::createParticlePipeline(
            device, hdrRenderPass, setLayout_,
            shaderDir + "/particle.vert.spv", shaderDir + "/particle.frag.spv",
            /*additive=*/true, additiveLayout_, additivePipeline_, error)) {
        std::fprintf(stderr, "[VFX] Particle additive pipeline unavailable: %s\n", error.c_str());
        return false;
    }

    std::printf("[VFX] Particle renderer ready: %u instances/frame, %u frames in flight.\n",
                kMaxInstancesPerFrame, frameCount);
    return true;
}

uint32_t ParticleRenderer::record(VkCommandBuffer cmd, VkDevice device, uint32_t frameIndex,
                                  VkExtent2D extent,
                                  const Mat4& viewProj, const Vec3& camRight, const Vec3& camUp,
                                  const std::vector<dash::vfx::ParticleInstance>& alphaBatch,
                                  const std::vector<dash::vfx::ParticleInstance>& additiveBatch)
{
    lastAlphaDrawn_ = 0;
    lastAdditiveDrawn_ = 0;
    if (!valid() || frameIndex >= instanceBuffers_.size()) return 0;
    if (alphaBatch.empty() && additiveBatch.empty()) return 0;

    uint32_t alphaCount = static_cast<uint32_t>(
        std::min<std::size_t>(alphaBatch.size(), kMaxInstancesPerFrame));
    uint32_t additiveCount = static_cast<uint32_t>(
        std::min<std::size_t>(additiveBatch.size(), kMaxInstancesPerFrame - alphaCount));

    const VkDeviceSize stride = sizeof(dash::vfx::ParticleInstance);
    const VkDeviceSize total = static_cast<VkDeviceSize>(alphaCount + additiveCount) * stride;
    if (total == 0) return 0;

    void* mapped = nullptr;
    if (vkMapMemory(device, instanceMemories_[frameIndex], 0, total, 0, &mapped) != VK_SUCCESS) {
        return 0;
    }
    auto* dst = static_cast<unsigned char*>(mapped);
    if (alphaCount > 0) {
        std::memcpy(dst, alphaBatch.data(), static_cast<size_t>(alphaCount) * stride);
    }
    if (additiveCount > 0) {
        std::memcpy(dst + static_cast<size_t>(alphaCount) * stride, additiveBatch.data(),
                    static_cast<size_t>(additiveCount) * stride);
    }
    vkUnmapMemory(device, instanceMemories_[frameIndex]);

    float pc[kParticlePushConstantFloats];
    std::memcpy(pc, viewProj.m, sizeof(viewProj.m));
    pc[16] = camRight.x; pc[17] = camRight.y; pc[18] = camRight.z; pc[19] = 0.0f;
    pc[20] = camUp.x;    pc[21] = camUp.y;    pc[22] = camUp.z;    pc[23] = 0.0f;

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, extent};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    uint32_t draws = 0;
    auto emit = [&](VkPipeline pipeline, VkPipelineLayout layout,
                    uint32_t count, VkDeviceSize offset) {
        if (count == 0) return;
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                                0, 1, &descriptorSet_, 0, nullptr);
        vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), pc);
        VkBuffer vb[] = { instanceBuffers_[frameIndex] };
        VkDeviceSize offsets[] = { offset };
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdDraw(cmd, 6, count, 0, 0);
        ++draws;
    };

    emit(alphaPipeline_, alphaLayout_, alphaCount, 0);
    emit(additivePipeline_, additiveLayout_, additiveCount,
         static_cast<VkDeviceSize>(alphaCount) * stride);

    lastAlphaDrawn_ = alphaCount;
    lastAdditiveDrawn_ = additiveCount;
    return draws;
}

void ParticleRenderer::shutdown(VkDevice device)
{
    if (device == VK_NULL_HANDLE) return;

    PipelineBuilder::destroy(device, alphaLayout_, alphaPipeline_);
    alphaLayout_ = VK_NULL_HANDLE;
    alphaPipeline_ = VK_NULL_HANDLE;

    PipelineBuilder::destroy(device, additiveLayout_, additivePipeline_);
    additiveLayout_ = VK_NULL_HANDLE;
    additivePipeline_ = VK_NULL_HANDLE;

    for (size_t i = 0; i < instanceBuffers_.size(); ++i) {
        if (instanceBuffers_[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, instanceBuffers_[i], nullptr);
        }
        if (instanceMemories_[i] != VK_NULL_HANDLE) {
            vkFreeMemory(device, instanceMemories_[i], nullptr);
        }
    }
    instanceBuffers_.clear();
    instanceMemories_.clear();

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    if (setLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
        setLayout_ = VK_NULL_HANDLE;
    }
    descriptorSet_ = VK_NULL_HANDLE;

    TextureLoader::destroy(device, atlas_);
}

} // namespace dash::vkexp
