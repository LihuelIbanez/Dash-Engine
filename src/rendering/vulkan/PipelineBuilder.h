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

    static void destroy(VkDevice device, VkPipelineLayout pipelineLayout, VkPipeline pipeline);
};

} // namespace dash::vkexp
