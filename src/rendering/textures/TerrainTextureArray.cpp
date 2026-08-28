#include "rendering/textures/TerrainTextureArray.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "game/rendering/stb_image.h"
#include "rendering/IsoRenderer.h"

namespace dash::vkexp {
namespace {

constexpr uint32_t kLayerCount    = static_cast<uint32_t>(TerrainTextureId::Count);
// 4k sources downsampled to 1024: with TILE_SCALE=2 a repeat spans a few world
// units, so 1024 keeps texel density above screen density at every editor zoom
// while the whole array (9 layers + full mip chain) stays around 50 MB.
constexpr uint32_t kAlbedoExtent  = 1024;
// Normals carry only low/mid frequency shading cues, so half resolution (~13 MB)
// is indistinguishable at gameplay distance.
constexpr uint32_t kNormalExtent  = 512;

struct LayerSpec {
    const char* folder;      // empty => fully procedural layer
    const char* stem;
    uint8_t     colorA[3];   // procedural base
    uint8_t     colorB[3];   // procedural highlight
    int         basePeriod;  // procedural noise lattice
    float       bump;        // normal-map slope gain
};

// Indexed by TerrainTextureId.
constexpr std::array<LayerSpec, kLayerCount> kLayers = {{
    {"",                        "",                  { 58,  92,  38}, {112, 142,  62},  8, 0.020f}, // Grass
    {"rocky_terrain_02_4k.blend", "rocky_terrain_02", {106,  84,  58}, {138, 116,  84},  6, 0.060f}, // Dirt
    {"gray_rocks_4k.blend",       "gray_rocks",       {120, 118, 112}, {158, 156, 150},  5, 0.090f}, // Rock
    {"sandy_gravel_02_4k.blend",  "sandy_gravel_02",  {186, 166, 126}, {214, 198, 162},  7, 0.050f}, // Sand
    {"snow_02_4k.blend",          "snow_02",          {224, 230, 240}, {252, 253, 255},  6, 0.030f}, // Snow
    {"",                        "",                  { 72,  52,  34}, { 98,  74,  50},  6, 0.045f}, // Mud
    {"",                        "",                  { 34,  62,  28}, { 64,  92,  42},  9, 0.022f}, // DarkGrass
    {"",                        "",                  { 96,  94,  88}, {148, 145, 138}, 16, 0.070f}, // Gravel
    {"",                        "",                  {176, 204, 220}, {226, 240, 248},  5, 0.015f}, // Ice
}};

// ── Tileable value-noise fbm ────────────────────────────────────────────────

uint32_t hashLattice(int x, int y, int period, uint32_t seed)
{
    const uint32_t ux = static_cast<uint32_t>(((x % period) + period) % period);
    const uint32_t uy = static_cast<uint32_t>(((y % period) + period) % period);
    uint32_t h = ux * 374761393u + uy * 668265263u + seed * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

float latticeValue(int x, int y, int period, uint32_t seed)
{
    return static_cast<float>(hashLattice(x, y, period, seed) & 0xFFFFFFu) / 16777215.0f;
}

float valueNoise(float x, float y, int period, uint32_t seed)
{
    const int xi = static_cast<int>(std::floor(x));
    const int yi = static_cast<int>(std::floor(y));
    const float fx = x - static_cast<float>(xi);
    const float fy = y - static_cast<float>(yi);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sy = fy * fy * (3.0f - 2.0f * fy);
    const float v00 = latticeValue(xi,     yi,     period, seed);
    const float v10 = latticeValue(xi + 1, yi,     period, seed);
    const float v01 = latticeValue(xi,     yi + 1, period, seed);
    const float v11 = latticeValue(xi + 1, yi + 1, period, seed);
    const float a = v00 + (v10 - v00) * sx;
    const float b = v01 + (v11 - v01) * sx;
    return a + (b - a) * sy;
}

// u,v in [0,1); tiles seamlessly because every octave wraps on its own period.
float fbm(float u, float v, int basePeriod, int octaves, uint32_t seed)
{
    float sum = 0.0f, amp = 0.5f, norm = 0.0f;
    int period = basePeriod;
    for (int o = 0; o < octaves; ++o) {
        sum  += amp * valueNoise(u * static_cast<float>(period),
                                 v * static_cast<float>(period),
                                 period, seed + static_cast<uint32_t>(o) * 131u);
        norm += amp;
        amp  *= 0.5f;
        period *= 2;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

// ── Resampling / mip generation ─────────────────────────────────────────────

// Area average: unlike point sampling it keeps the 4k detail as texture grain
// instead of aliasing it away.
template <typename T, int Channels>
void boxResize(const T* src, int sw, int sh, T* dst, uint32_t dw, uint32_t dh)
{
    for (uint32_t y = 0; y < dh; ++y) {
        int y0 = static_cast<int>(static_cast<uint64_t>(y) * static_cast<uint64_t>(sh) / dh);
        int y1 = static_cast<int>(static_cast<uint64_t>(y + 1) * static_cast<uint64_t>(sh) / dh);
        if (y1 <= y0) y1 = y0 + 1;
        for (uint32_t x = 0; x < dw; ++x) {
            int x0 = static_cast<int>(static_cast<uint64_t>(x) * static_cast<uint64_t>(sw) / dw);
            int x1 = static_cast<int>(static_cast<uint64_t>(x + 1) * static_cast<uint64_t>(sw) / dw);
            if (x1 <= x0) x1 = x0 + 1;

            uint64_t acc[Channels] = {};
            uint64_t n = 0;
            for (int sy = y0; sy < y1; ++sy) {
                const T* row = src + (static_cast<size_t>(sy) * sw) * Channels;
                for (int sx = x0; sx < x1; ++sx) {
                    const T* p = row + static_cast<size_t>(sx) * Channels;
                    for (int c = 0; c < Channels; ++c) acc[c] += p[c];
                    ++n;
                }
            }
            T* d = dst + (static_cast<size_t>(y) * dw + x) * Channels;
            for (int c = 0; c < Channels; ++c) d[c] = static_cast<T>(acc[c] / n);
        }
    }
}

uint32_t mipCount(uint32_t extent)
{
    uint32_t levels = 1;
    while (extent > 1) { extent /= 2; ++levels; }
    return levels;
}

VkDeviceSize mipChainBytes(uint32_t extent, uint32_t mipLevels)
{
    VkDeviceSize total = 0;
    uint32_t s = extent;
    for (uint32_t m = 0; m < mipLevels; ++m) {
        total += static_cast<VkDeviceSize>(s) * s * 4;
        s = std::max(1u, s / 2);
    }
    return total;
}

void encodeNormal(float nx, float ny, float nz, uint8_t* dst)
{
    const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
    const float inv = len > 1e-6f ? 1.0f / len : 0.0f;
    dst[0] = static_cast<uint8_t>(std::lround((nx * inv * 0.5f + 0.5f) * 255.0f));
    dst[1] = static_cast<uint8_t>(std::lround((ny * inv * 0.5f + 0.5f) * 255.0f));
    dst[2] = static_cast<uint8_t>(std::lround((nz * inv * 0.5f + 0.5f) * 255.0f));
    dst[3] = 255;
}

// Mip 0 must already be filled; the rest of the chain follows contiguously.
void generateMips(uint8_t* layerBase, uint32_t extent, uint32_t mipLevels, bool renormalize)
{
    uint8_t* src = layerBase;
    uint32_t sw = extent;
    for (uint32_t m = 1; m < mipLevels; ++m) {
        uint8_t* dst = src + static_cast<size_t>(sw) * sw * 4;
        const uint32_t dw = std::max(1u, sw / 2);
        for (uint32_t y = 0; y < dw; ++y) {
            for (uint32_t x = 0; x < dw; ++x) {
                const uint32_t x0 = std::min(sw - 1, x * 2), x1 = std::min(sw - 1, x * 2 + 1);
                const uint32_t y0 = std::min(sw - 1, y * 2), y1 = std::min(sw - 1, y * 2 + 1);
                int acc[4] = {0, 0, 0, 0};
                const uint32_t xs[2] = {x0, x1};
                const uint32_t ys[2] = {y0, y1};
                for (uint32_t sy : ys) {
                    for (uint32_t sx : xs) {
                        const uint8_t* p = src + (static_cast<size_t>(sy) * sw + sx) * 4;
                        for (int c = 0; c < 4; ++c) acc[c] += p[c];
                    }
                }
                uint8_t* d = dst + (static_cast<size_t>(y) * dw + x) * 4;
                if (renormalize) {
                    encodeNormal(static_cast<float>(acc[0]) / 510.0f - 1.0f,
                                 static_cast<float>(acc[1]) / 510.0f - 1.0f,
                                 static_cast<float>(acc[2]) / 510.0f - 1.0f, d);
                } else {
                    for (int c = 0; c < 4; ++c) d[c] = static_cast<uint8_t>(acc[c] / 4);
                }
            }
        }
        src = dst;
        sw = dw;
    }
}

// ── Layer content ───────────────────────────────────────────────────────────

// Height field reused for both the procedural albedo grain and its normal map.
void proceduralHeight(const LayerSpec& spec, uint32_t extent, uint32_t layerIndex,
                      std::vector<float>& out)
{
    out.resize(static_cast<size_t>(extent) * extent);
    const float inv = 1.0f / static_cast<float>(extent);
    for (uint32_t y = 0; y < extent; ++y) {
        for (uint32_t x = 0; x < extent; ++x) {
            const float u = static_cast<float>(x) * inv;
            const float v = static_cast<float>(y) * inv;
            out[static_cast<size_t>(y) * extent + x] =
                fbm(u, v, spec.basePeriod, 6, 1013u + layerIndex * 97u);
        }
    }
}

void fillProceduralAlbedo(const LayerSpec& spec, uint32_t extent,
                          const std::vector<float>& height, uint8_t* dst)
{
    for (uint32_t y = 0; y < extent; ++y) {
        for (uint32_t x = 0; x < extent; ++x) {
            const size_t i = static_cast<size_t>(y) * extent + x;
            const float t = std::clamp(height[i] * 1.6f - 0.3f, 0.0f, 1.0f);
            uint8_t* d = dst + i * 4;
            for (int c = 0; c < 3; ++c) {
                const float a = static_cast<float>(spec.colorA[c]);
                const float b = static_cast<float>(spec.colorB[c]);
                d[c] = static_cast<uint8_t>(std::clamp(a + (b - a) * t, 0.0f, 255.0f));
            }
            d[3] = 255;
        }
    }
}

bool loadPhotoAlbedo(const std::string& path, uint32_t extent, uint8_t* dst)
{
    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(0);
    unsigned char* raw = stbi_load(path.c_str(), &w, &h, &ch, STBI_rgb_alpha);
    if (!raw) return false;
    boxResize<unsigned char, 4>(raw, w, h, dst, extent, extent);
    stbi_image_free(raw);
    return true;
}

// The published normal maps are OpenEXR, which stb_image cannot decode, so the
// 16-bit displacement PNG is differentiated instead.
bool loadPhotoHeight(const std::string& path, uint32_t extent, std::vector<float>& out)
{
    int w = 0, h = 0, ch = 0;
    stbi_set_flip_vertically_on_load(0);
    stbi_us* raw = stbi_load_16(path.c_str(), &w, &h, &ch, 1);
    if (!raw) return false;

    std::vector<stbi_us> small(static_cast<size_t>(extent) * extent);
    boxResize<stbi_us, 1>(raw, w, h, small.data(), extent, extent);
    stbi_image_free(raw);

    out.resize(small.size());
    for (size_t i = 0; i < small.size(); ++i) out[i] = static_cast<float>(small[i]) / 65535.0f;
    return true;
}

void heightToNormal(const std::vector<float>& height, uint32_t extent, float bump, uint8_t* dst)
{
    const float gain = bump * static_cast<float>(extent);
    auto at = [&](uint32_t x, uint32_t y) {
        return height[static_cast<size_t>(y & (extent - 1)) * extent + (x & (extent - 1))];
    };
    for (uint32_t y = 0; y < extent; ++y) {
        for (uint32_t x = 0; x < extent; ++x) {
            const float dhdx = (at(x + 1, y) - at(x + extent - 1, y)) * 0.5f;
            const float dhdy = (at(x, y + 1) - at(x, y + extent - 1)) * 0.5f;
            // Image rows grow downward, so +Y in tangent space is +dh/dy_image.
            encodeNormal(-dhdx * gain, dhdy * gain, 1.0f,
                         dst + (static_cast<size_t>(y) * extent + x) * 4);
        }
    }
}

// ── Vulkan plumbing ─────────────────────────────────────────────────────────

uint32_t findMemoryType(VkPhysicalDevice pd, uint32_t typeFilter, VkMemoryPropertyFlags props)
{
    VkPhysicalDeviceMemoryProperties memProps{};
    vkGetPhysicalDeviceMemoryProperties(pd, &memProps);
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool createStagingBuffer(VkPhysicalDevice pd, VkDevice dev, VkDeviceSize size,
                         VkBuffer& buffer, VkDeviceMemory& memory)
{
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(dev, &bci, nullptr, &buffer) != VK_SUCCESS) return false;

    VkMemoryRequirements reqs{};
    vkGetBufferMemoryRequirements(dev, buffer, &reqs);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = reqs.size;
    mai.memoryTypeIndex = findMemoryType(pd, reqs.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mai.memoryTypeIndex == UINT32_MAX ||
        vkAllocateMemory(dev, &mai, nullptr, &memory) != VK_SUCCESS) {
        vkDestroyBuffer(dev, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
        return false;
    }
    vkBindBufferMemory(dev, buffer, memory, 0);
    return true;
}

bool uploadArray(VkPhysicalDevice pd, VkDevice dev, VkQueue queue, VkCommandPool pool,
                 const std::vector<uint8_t>& pixels, uint32_t extent, uint32_t mipLevels,
                 TerrainTextureArray& out)
{
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {extent, extent, 1};
    ici.mipLevels = mipLevels;
    ici.arrayLayers = kLayerCount;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(dev, &ici, nullptr, &out.image) != VK_SUCCESS) return false;

    VkMemoryRequirements reqs{};
    vkGetImageMemoryRequirements(dev, out.image, &reqs);
    VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = reqs.size;
    mai.memoryTypeIndex = findMemoryType(pd, reqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (mai.memoryTypeIndex == UINT32_MAX) return false;
    if (vkAllocateMemory(dev, &mai, nullptr, &out.memory) != VK_SUCCESS) return false;
    vkBindImageMemory(dev, out.image, out.memory, 0);

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    const VkDeviceSize total = static_cast<VkDeviceSize>(pixels.size());
    if (!createStagingBuffer(pd, dev, total, staging, stagingMem)) return false;

    void* mapped = nullptr;
    vkMapMemory(dev, stagingMem, 0, total, 0, &mapped);
    std::memcpy(mapped, pixels.data(), pixels.size());
    vkUnmapMemory(dev, stagingMem);

    VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cbai.commandPool = pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(dev, &cbai, &cmd) != VK_SUCCESS) return false;

    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &bi);

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = out.image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, kLayerCount};
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    std::vector<VkBufferImageCopy> regions;
    regions.reserve(static_cast<size_t>(kLayerCount) * mipLevels);
    const VkDeviceSize layerStride = mipChainBytes(extent, mipLevels);
    for (uint32_t layer = 0; layer < kLayerCount; ++layer) {
        VkDeviceSize offset = layerStride * layer;
        uint32_t s = extent;
        for (uint32_t mip = 0; mip < mipLevels; ++mip) {
            VkBufferImageCopy region{};
            region.bufferOffset = offset;
            region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            region.imageSubresource.mipLevel = mip;
            region.imageSubresource.baseArrayLayer = layer;
            region.imageSubresource.layerCount = 1;
            region.imageExtent = {s, s, 1};
            regions.push_back(region);
            offset += static_cast<VkDeviceSize>(s) * s * 4;
            s = std::max(1u, s / 2);
        }
    }
    vkCmdCopyBufferToImage(cmd, staging, out.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(regions.size()), regions.data());

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                         0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cmd);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(dev, pool, 1, &cmd);
    vkDestroyBuffer(dev, staging, nullptr);
    vkFreeMemory(dev, stagingMem, nullptr);

    VkImageViewCreateInfo vci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = out.image;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    vci.format = VK_FORMAT_R8G8B8A8_UNORM;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, mipLevels, 0, kLayerCount};
    if (vkCreateImageView(dev, &vci, nullptr, &out.view) != VK_SUCCESS) return false;

    VkPhysicalDeviceFeatures features{};
    vkGetPhysicalDeviceFeatures(pd, &features);
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(pd, &props);

    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = VK_FILTER_LINEAR;
    sci.minFilter = VK_FILTER_LINEAR;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sci.maxLod = static_cast<float>(mipLevels);
    // Ground planes are viewed at a shallow isometric angle, where trilinear
    // alone blurs the near field to mush.
    sci.anisotropyEnable = features.samplerAnisotropy ? VK_TRUE : VK_FALSE;
    sci.maxAnisotropy = features.samplerAnisotropy
                      ? std::min(8.0f, props.limits.maxSamplerAnisotropy) : 1.0f;
    if (vkCreateSampler(dev, &sci, nullptr, &out.sampler) != VK_SUCCESS) return false;

    out.extent = extent;
    out.layers = kLayerCount;
    out.mipLevels = mipLevels;
    return true;
}

} // namespace

std::string defaultTerrainTextureRoot()
{
#ifdef VULKAN_ASSETS_DIR
    return std::string(VULKAN_ASSETS_DIR) + "/models/terrain/";
#else
    return "assets/models/terrain/";
#endif
}

bool createTerrainTextureSet(VkPhysicalDevice physicalDevice,
                             VkDevice device,
                             VkQueue graphicsQueue,
                             VkCommandPool commandPool,
                             const std::string& terrainRoot,
                             TerrainTextureSet& out)
{
    const uint32_t albedoMips = mipCount(kAlbedoExtent);
    const uint32_t normalMips = mipCount(kNormalExtent);

    std::vector<uint8_t> albedo(static_cast<size_t>(mipChainBytes(kAlbedoExtent, albedoMips)) * kLayerCount);
    std::vector<uint8_t> normal(static_cast<size_t>(mipChainBytes(kNormalExtent, normalMips)) * kLayerCount);

    const size_t albedoStride = static_cast<size_t>(mipChainBytes(kAlbedoExtent, albedoMips));
    const size_t normalStride = static_cast<size_t>(mipChainBytes(kNormalExtent, normalMips));

    int photoLayers = 0, heightLayers = 0;

    for (uint32_t layer = 0; layer < kLayerCount; ++layer) {
        const LayerSpec& spec = kLayers[layer];
        uint8_t* albedoDst = albedo.data() + albedoStride * layer;
        uint8_t* normalDst = normal.data() + normalStride * layer;

        const bool hasPhoto = spec.folder[0] != '\0';
        const std::string base = hasPhoto
            ? terrainRoot + spec.folder + "/textures/" + spec.stem
            : std::string();

        bool albedoLoaded = false;
        if (hasPhoto) {
            albedoLoaded = loadPhotoAlbedo(base + "_diff_4k.jpg", kAlbedoExtent, albedoDst);
            if (albedoLoaded) ++photoLayers;
            else std::fprintf(stderr, "[TerrainTex] layer %u: %s_diff_4k.jpg unreadable, using procedural fill\n",
                              layer, spec.stem);
        }

        std::vector<float> heightAlbedoRes;
        if (!albedoLoaded) {
            proceduralHeight(spec, kAlbedoExtent, layer, heightAlbedoRes);
            fillProceduralAlbedo(spec, kAlbedoExtent, heightAlbedoRes, albedoDst);
        }
        generateMips(albedoDst, kAlbedoExtent, albedoMips, false);

        std::vector<float> height;
        bool heightLoaded = false;
        if (hasPhoto) {
            heightLoaded = loadPhotoHeight(base + "_disp_4k.png", kNormalExtent, height);
            if (heightLoaded) ++heightLayers;
        }
        if (!heightLoaded) proceduralHeight(spec, kNormalExtent, layer, height);
        heightToNormal(height, kNormalExtent, spec.bump, normalDst);
        generateMips(normalDst, kNormalExtent, normalMips, true);
    }

    if (!uploadArray(physicalDevice, device, graphicsQueue, commandPool,
                     albedo, kAlbedoExtent, albedoMips, out.albedo)) {
        std::fprintf(stderr, "[TerrainTex] Failed to upload albedo array\n");
        return false;
    }
    if (!uploadArray(physicalDevice, device, graphicsQueue, commandPool,
                     normal, kNormalExtent, normalMips, out.normal)) {
        std::fprintf(stderr, "[TerrainTex] Failed to upload normal array\n");
        return false;
    }

    std::fprintf(stdout,
                 "[TerrainTex] albedo %ux%u x%u (%u mips, %d photo) + normals %ux%u x%u "
                 "(%u mips, %d from displacement) = %.1f MB\n",
                 kAlbedoExtent, kAlbedoExtent, kLayerCount, albedoMips, photoLayers,
                 kNormalExtent, kNormalExtent, kLayerCount, normalMips, heightLayers,
                 static_cast<double>(albedo.size() + normal.size()) / (1024.0 * 1024.0));
    return true;
}

void destroyTerrainTextureSet(VkDevice device, TerrainTextureSet& set)
{
    for (TerrainTextureArray* arr : {&set.albedo, &set.normal}) {
        if (arr->sampler) vkDestroySampler(device, arr->sampler, nullptr);
        if (arr->view)    vkDestroyImageView(device, arr->view, nullptr);
        if (arr->image)   vkDestroyImage(device, arr->image, nullptr);
        if (arr->memory)  vkFreeMemory(device, arr->memory, nullptr);
        *arr = TerrainTextureArray{};
    }
}

} // namespace dash::vkexp
