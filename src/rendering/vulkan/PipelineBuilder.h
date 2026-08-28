#pragma once

#include <string>

#include <vulkan/vulkan.h>

namespace dash::vkexp {

class PipelineBuilder {
public:
    static bool createBasicPipeline(
        VkDevice device,
        VkExtent2D extent,
        VkRenderPass renderPass,
        VkDescriptorSetLayout descriptorSetLayout,
        const std::string& vertSpvPath,
        const std::string& fragSpvPath,
        VkPipelineLayout& outPipelineLayout,
        VkPipeline& outPipeline,
        std::string& outError);

    static bool createTerrainPipeline(
        VkDevice device,
        VkExtent2D extent,
        VkRenderPass renderPass,
        VkDescriptorSetLayout descriptorSetLayout,
        const std::string& vertSpvPath,
        const std::string& fragSpvPath,
        VkPipelineLayout& outPipelineLayout,
        VkPipeline& outPipeline,
        std::string& outError);

    static bool createWaterPipeline(
        VkDevice device,
        VkExtent2D extent,
        VkRenderPass renderPass,
        VkDescriptorSetLayout descriptorSetLayout,
        const std::string& vertSpvPath,
        const std::string& fragSpvPath,
        VkPipelineLayout& outPipelineLayout,
        VkPipeline& outPipeline,
        std::string& outError);

    // Camera-facing quads. No vertex input: the quad is generated in the
    // vertex shader from gl_VertexIndex (draw 6 vertices, no buffers bound).
    static bool createBillboardPipeline(
        VkDevice device,
        VkExtent2D extent,
        VkRenderPass renderPass,
        VkDescriptorSetLayout descriptorSetLayout,
        const std::string& vertSpvPath,
        const std::string& fragSpvPath,
        VkPipelineLayout& outPipelineLayout,
        VkPipeline& outPipeline,
        std::string& outError);

    // Linear-blend skinning. Two vertex bindings: 0 = the static Vertex stream
    // shared with the basic pipeline, 1 = the .dashmesh v2 SkinnedVertex stream.
    // `boneSetLayout` becomes set 1 and must hold the bone palette at binding 0;
    // set 0 and the push constant range match createBasicPipeline so the scene
    // descriptor stays bound across a pipeline switch.
    static bool createSkinnedPipeline(
        VkDevice device,
        VkExtent2D extent,
        VkRenderPass renderPass,
        VkDescriptorSetLayout descriptorSetLayout,
        VkDescriptorSetLayout boneSetLayout,
        const std::string& vertSpvPath,
        const std::string& fragSpvPath,
        VkPipelineLayout& outPipelineLayout,
        VkPipeline& outPipeline,
        std::string& outError);

    // Depth-only pass for the shadow map: vertex stage alone, no colour
    // attachment, and `mat4 model + mat4 lightViewProj` in push constants so no
    // descriptor set is needed for static casters. Passing a non-null
    // `boneSetLayout` builds the skinned variant, which adds the .dashmesh v2
    // stream at binding 1 and expects the bone palette at set 1.
    static bool createShadowDepthPipeline(
        VkDevice device,
        VkExtent2D extent,
        VkRenderPass renderPass,
        VkDescriptorSetLayout descriptorSetLayout,
        VkDescriptorSetLayout boneSetLayout,
        const std::string& vertSpvPath,
        VkPipelineLayout& outPipelineLayout,
        VkPipeline& outPipeline,
        std::string& outError);

    static void destroy(VkDevice device, VkPipelineLayout pipelineLayout, VkPipeline pipeline);
};

} // namespace dash::vkexp
