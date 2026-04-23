#include "rendering/textures/TextureLoader.h"

#include <cstdio>
#include <cstring>

// Use stb_image from existing project location
#include "game/rendering/stb_image.h"

namespace dash::vkexp {

uint32_t TextureLoader::findMemoryType(VkPhysicalDevice physicalDevice,
                                        uint32_t typeFilter,
                                        VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1 << i)) &&
            ((memProps.memoryTypes[i].propertyFlags & properties) == properties)) {
            return i;
        }
    }
    return UINT32_MAX;
}

void TextureLoader::transitionImageLayout(VkDevice device,
                                           VkQueue queue,
                                           VkCommandPool commandPool,
                                           VkImage image,
                                           VkImageLayout oldLayout,
                                           VkImageLayout newLayout)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmd = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &allocInfo, &cmd);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0,
                          0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, commandPool, 1, &cmd);
}

bool TextureLoader::createTextureFromPixels(VkPhysicalDevice physicalDevice,
                                             VkDevice device,
                                             VkQueue graphicsQueue,
                                             VkCommandPool commandPool,
                                             const unsigned char* pixels,
                                             uint32_t width,
                                             uint32_t height,
                                             TextureResource& out)
{
    VkDeviceSize imageSize = static_cast<VkDeviceSize>(width) * height * 4;

    // Create staging buffer
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo bufInfo{};
    bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufInfo.size = imageSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(device, &bufInfo, nullptr, &stagingBuffer) != VK_SUCCESS) return false;

    VkMemoryRequirements memReqs{};
    vkGetBufferMemoryRequirements(device, stagingBuffer, &memReqs);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memReqs.size;
    allocInfo.memoryTypeIndex = findMemoryType(physicalDevice, memReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        return false;
    }

    if (vkAllocateMemory(device, &allocInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        return false;
    }
    vkBindBufferMemory(device, stagingBuffer, stagingMemory, 0);

    void* mapped = nullptr;
    vkMapMemory(device, stagingMemory, 0, imageSize, 0, &mapped);
    std::memcpy(mapped, pixels, static_cast<size_t>(imageSize));
    vkUnmapMemory(device, stagingMemory);

    // Create VkImage
    VkImageCreateInfo imgInfo{};
    imgInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imgInfo.extent = {width, height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &imgInfo, nullptr, &out.image) != VK_SUCCESS) {
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);
        return false;
    }

    VkMemoryRequirements imgMemReqs{};
    vkGetImageMemoryRequirements(device, out.image, &imgMemReqs);

    VkMemoryAllocateInfo imgAlloc{};
    imgAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    imgAlloc.allocationSize = imgMemReqs.size;
    imgAlloc.memoryTypeIndex = findMemoryType(physicalDevice, imgMemReqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (imgAlloc.memoryTypeIndex == UINT32_MAX) {
        vkDestroyImage(device, out.image, nullptr);
        out.image = VK_NULL_HANDLE;
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);
        return false;
    }

    if (vkAllocateMemory(device, &imgAlloc, nullptr, &out.memory) != VK_SUCCESS) {
        vkDestroyImage(device, out.image, nullptr);
        out.image = VK_NULL_HANDLE;
        vkDestroyBuffer(device, stagingBuffer, nullptr);
        vkFreeMemory(device, stagingMemory, nullptr);
        return false;
    }
    vkBindImageMemory(device, out.image, out.memory, 0);

    // Transition to transfer dst, copy, transition to shader read
    transitionImageLayout(device, graphicsQueue, commandPool,
                          out.image, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

    // Copy buffer to image
    {
        VkCommandBufferAllocateInfo cmdAlloc{};
        cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cmdAlloc.commandPool = commandPool;
        cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cmdAlloc.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        vkAllocateCommandBuffers(device, &cmdAlloc, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};

        vkCmdCopyBufferToImage(cmd, stagingBuffer, out.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        vkFreeCommandBuffers(device, commandPool, 1, &cmd);
    }

    transitionImageLayout(device, graphicsQueue, commandPool,
                          out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    // Cleanup staging
    vkDestroyBuffer(device, stagingBuffer, nullptr);
    vkFreeMemory(device, stagingMemory, nullptr);

    // Create image view
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = out.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &out.imageView) != VK_SUCCESS) {
        destroy(device, out);
        return false;
    }

    // Create sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

    if (vkCreateSampler(device, &samplerInfo, nullptr, &out.sampler) != VK_SUCCESS) {
        destroy(device, out);
        return false;
    }

    out.width = width;
    out.height = height;
    return true;
}

bool TextureLoader::loadFromFile(VkPhysicalDevice physicalDevice,
                                  VkDevice device,
                                  VkQueue graphicsQueue,
                                  VkCommandPool commandPool,
                                  const std::string& path,
                                  TextureResource& out)
{
    int w = 0, h = 0, channels = 0;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, STBI_rgb_alpha);
    if (!pixels) {
        std::fprintf(stderr, "[TextureLoader] Failed to load image: %s\n", path.c_str());
        return false;
    }

    bool ok = createTextureFromPixels(physicalDevice, device, graphicsQueue, commandPool,
                                       pixels, static_cast<uint32_t>(w), static_cast<uint32_t>(h), out);
    stbi_image_free(pixels);

    if (ok) {
        std::fprintf(stdout, "[TextureLoader] Loaded texture %dx%d from %s\n", w, h, path.c_str());
    }
    return ok;
}

bool TextureLoader::createDefaultWhite(VkPhysicalDevice physicalDevice,
                                        VkDevice device,
                                        VkQueue graphicsQueue,
                                        VkCommandPool commandPool,
                                        TextureResource& out)
{
    const unsigned char white[4] = {255, 255, 255, 255};
    return createTextureFromPixels(physicalDevice, device, graphicsQueue, commandPool,
                                   white, 1, 1, out);
}

void TextureLoader::destroy(VkDevice device, TextureResource& tex)
{
    if (tex.sampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, tex.sampler, nullptr);
        tex.sampler = VK_NULL_HANDLE;
    }
    if (tex.imageView != VK_NULL_HANDLE) {
        vkDestroyImageView(device, tex.imageView, nullptr);
        tex.imageView = VK_NULL_HANDLE;
    }
    if (tex.image != VK_NULL_HANDLE) {
        vkDestroyImage(device, tex.image, nullptr);
        tex.image = VK_NULL_HANDLE;
    }
    if (tex.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, tex.memory, nullptr);
        tex.memory = VK_NULL_HANDLE;
    }
    tex.width = 0;
    tex.height = 0;
}

} // namespace dash::vkexp
