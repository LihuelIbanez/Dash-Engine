#pragma once

#include <cstdint>
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
    // `vertexStride` overrides the binding 0 stride for streams that are not the
    // shared Vertex layout — the shader only reads location 0, which every one of
    // them keeps at offset 0. `label` names the log line, nothing else.
    static bool createShadowDepthPipeline(
        VkDevice device,
        VkExtent2D extent,
        VkRenderPass renderPass,
        VkDescriptorSetLayout descriptorSetLayout,
        VkDescriptorSetLayout boneSetLayout,
        const std::string& vertSpvPath,
        VkPipelineLayout& outPipelineLayout,
        VkPipeline& outPipeline,
        std::string& outError,
        uint32_t vertexStride = 0,
        const char* label = nullptr);

    // Fullscreen resolve of the HDR target. No vertex input (the triangle comes
    // from gl_VertexIndex), no depth, and dynamic viewport/scissor so one
    // pipeline serves every target size — the editor viewport resizes freely.
    static bool createTonemapPipeline(
        VkDevice device,
        VkRenderPass renderPass,
        VkDescriptorSetLayout descriptorSetLayout,
        const std::string& vertSpvPath,
        const std::string& fragSpvPath,
        VkPipelineLayout& outPipelineLayout,
        VkPipeline& outPipeline,
        std::string& outError);

    // The shape behind createTonemapPipeline, exposed for the other fullscreen
    // passes (SSAO, blur). `label` only names the success log line; the push
    // constant range is always kTonemapPushConstantFloats of fragment stage.
    static bool createFullscreenPipeline(
        VkDevice device,
        VkRenderPass renderPass,
        VkDescriptorSetLayout descriptorSetLayout,
        const std::string& vertSpvPath,
        const std::string& fragSpvPath,
        const char* label,
        VkPipelineLayout& outPipelineLayout,
        VkPipeline& outPipeline,
        std::string& outError);

    // Depth-only billboard pass: the same procedural quad as
    // createBillboardPipeline, but with a fragment stage that discards on the
    // sprite alpha so the caster is the silhouette and not a solid rectangle.
    // Needs set 0 bound for that sampler, unlike the static depth pipeline.
    static bool createShadowBillboardPipeline(
        VkDevice device,
        VkExtent2D extent,
        VkRenderPass renderPass,
        VkDescriptorSetLayout descriptorSetLayout,
        const std::string& vertSpvPath,
        const std::string& fragSpvPath,
        VkPipelineLayout& outPipelineLayout,
        VkPipeline& outPipeline,
        std::string& outError);

    // Instanced particle quads. One vertex binding at INSTANCE rate carrying
    // four vec4s (centre+size, colour, uv rect, params) — every one of them has
    // a matching location in assets/shaders/particle.vert. `additive` swaps the
    // premultiplied-alpha blend for ONE/ONE; the shader is the same either way.
    // Viewport and scissor are dynamic so one pipeline outlives every resize.
    static bool createParticlePipeline(
        VkDevice device,
        VkRenderPass renderPass,
        VkDescriptorSetLayout descriptorSetLayout,
        const std::string& vertSpvPath,
        const std::string& fragSpvPath,
        bool additive,
        VkPipelineLayout& outPipelineLayout,
        VkPipeline& outPipeline,
        std::string& outError);

    static void destroy(VkDevice device, VkPipelineLayout pipelineLayout, VkPipeline pipeline);
};

} // namespace dash::vkexp
