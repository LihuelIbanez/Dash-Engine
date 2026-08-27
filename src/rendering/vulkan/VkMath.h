#pragma once

#include <cmath>
#include <cstdint>
#include <vulkan/vulkan.h>

namespace dash::vkexp {

// ─── Linear algebra types ────────────────────────────────────────────────────

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Mat4 {
    float m[16]{};
};

struct CameraUBO {
    Mat4 viewProj;
};

// ─── Matrix / vector operations ──────────────────────────────────────────────

inline Mat4 identity()
{
    Mat4 out{};
    out.m[0] = 1.0f;
    out.m[5] = 1.0f;
    out.m[10] = 1.0f;
    out.m[15] = 1.0f;
    return out;
}

inline Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 out{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

inline float dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline Vec3 normalize(const Vec3& v)
{
    const float len = std::sqrt(dot(v, v));
    if (len <= 0.00001f) return {0.0f, 0.0f, 0.0f};
    return {v.x / len, v.y / len, v.z / len};
}

inline Mat4 perspective(float fovYRadians, float aspect, float zNear, float zFar)
{
    Mat4 out{};
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    out.m[0] = f / aspect;
    out.m[5] = f;
    out.m[10] = zFar / (zNear - zFar);
    out.m[11] = -1.0f;
    out.m[14] = (zFar * zNear) / (zNear - zFar);
    return out;
}

inline Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up)
{
    const Vec3 f = normalize({center.x - eye.x, center.y - eye.y, center.z - eye.z});
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);

    Mat4 out = identity();
    out.m[0] = s.x;
    out.m[1] = u.x;
    out.m[2] = -f.x;
    out.m[4] = s.y;
    out.m[5] = u.y;
    out.m[6] = -f.y;
    out.m[8] = s.z;
    out.m[9] = u.z;
    out.m[10] = -f.z;
    out.m[12] = -dot(s, eye);
    out.m[13] = -dot(u, eye);
    out.m[14] = dot(f, eye);
    return out;
}

// Column-major model matrix: M = T * Ry(yaw) * Rx(pitch) * Rz(roll) * S.
inline Mat4 trs(const Vec3& position,
                float yawDeg, float pitchDeg, float rollDeg,
                const Vec3& scale)
{
    constexpr float kDeg2Rad = 3.14159265358979323846f / 180.0f;
    const float cy = std::cos(yawDeg * kDeg2Rad);
    const float sy = std::sin(yawDeg * kDeg2Rad);
    const float cp = std::cos(pitchDeg * kDeg2Rad);
    const float sp = std::sin(pitchDeg * kDeg2Rad);
    const float cr = std::cos(rollDeg * kDeg2Rad);
    const float sr = std::sin(rollDeg * kDeg2Rad);

    const float r00 = cy * cr + sy * sp * sr;
    const float r01 = -cy * sr + sy * sp * cr;
    const float r02 = sy * cp;
    const float r10 = cp * sr;
    const float r11 = cp * cr;
    const float r12 = -sp;
    const float r20 = -sy * cr + cy * sp * sr;
    const float r21 = sy * sr + cy * sp * cr;
    const float r22 = cy * cp;

    Mat4 out{};
    out.m[0]  = r00 * scale.x;
    out.m[1]  = r10 * scale.x;
    out.m[2]  = r20 * scale.x;
    out.m[4]  = r01 * scale.y;
    out.m[5]  = r11 * scale.y;
    out.m[6]  = r21 * scale.y;
    out.m[8]  = r02 * scale.z;
    out.m[9]  = r12 * scale.z;
    out.m[10] = r22 * scale.z;
    out.m[12] = position.x;
    out.m[13] = position.y;
    out.m[14] = position.z;
    out.m[15] = 1.0f;
    return out;
}

// ─── Vulkan memory helpers ───────────────────────────────────────────────────

inline uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) &&
            ((memProperties.memoryTypes[i].propertyFlags & properties) == properties)) {
            return i;
        }
    }
    return UINT32_MAX;
}

inline bool createHostVisibleBuffer(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkBuffer& outBuffer,
    VkDeviceMemory& outMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bufferInfo, nullptr, &outBuffer) != VK_SUCCESS) return false;

    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(device, outBuffer, &req);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = findMemoryType(
        physicalDevice,
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (allocInfo.memoryTypeIndex == UINT32_MAX) {
        vkDestroyBuffer(device, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }

    if (vkAllocateMemory(device, &allocInfo, nullptr, &outMemory) != VK_SUCCESS) {
        vkDestroyBuffer(device, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(device, outBuffer, outMemory, 0);
    return true;
}

} // namespace dash::vkexp
