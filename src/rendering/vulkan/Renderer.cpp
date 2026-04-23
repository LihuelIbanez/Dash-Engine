#include "rendering/vulkan/Renderer.h"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>

#include <GLFW/glfw3.h>
#include <nlohmann/json.hpp>

#include "game/physics/DebugPhysicsDraw.h"
#include "rendering/vulkan/PipelineBuilder.h"
#include "rendering/mesh/TerrainVertex.h"
#include "world/TerrainMesh.h"

#ifndef VULKAN_SHADER_DIR
#define VULKAN_SHADER_DIR ""
#endif

namespace dash::vkexp {

using json = nlohmann::json;

namespace {

static constexpr const char* kGetPhysicalDeviceProps2Ext = "VK_KHR_get_physical_device_properties2";

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

static Mat4 identity()
{
    Mat4 out{};
    out.m[0] = 1.0f;
    out.m[5] = 1.0f;
    out.m[10] = 1.0f;
    out.m[15] = 1.0f;
    return out;
}

static Mat4 multiply(const Mat4& a, const Mat4& b)
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

static float dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

static Vec3 normalize(const Vec3& v)
{
    const float len = std::sqrt(dot(v, v));
    if (len <= 0.00001f) return {0.0f, 0.0f, 0.0f};
    return {v.x / len, v.y / len, v.z / len};
}

static Mat4 perspective(float fovYRadians, float aspect, float zNear, float zFar)
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

static Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up)
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

static uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties)
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

static bool createHostVisibleBuffer(
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

static bool hasValidationLayer()
{
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    if (layerCount == 0) return false;

    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
    for (const auto& layer : layers) {
        if (std::string(layer.layerName) == "VK_LAYER_KHRONOS_validation") return true;
    }
    return false;
}

static bool loadSceneSpawnPoint(const std::string& scenePath, dash::physics::Vec3& outSpawn)
{
    std::ifstream in(scenePath);
    if (!in.is_open()) return false;

    json j;
    try {
        in >> j;
    } catch (...) {
        return false;
    }

    if (!j.contains("entities") || !j["entities"].is_array()) return false;

    const auto& entities = j["entities"];
    const json* selected = nullptr;
    for (const auto& e : entities) {
        if (!e.is_object()) continue;
        if (e.value("type", std::string{}) == "Player") {
            selected = &e;
            break;
        }
    }
    if (!selected && !entities.empty() && entities[0].is_object()) {
        selected = &entities[0];
    }
    if (!selected) return false;

    const float sx = selected->value("x", 0.0f);
    const float sy = selected->value("y", 0.0f);

    // Dash scenes store horizontal position in (x, y). Vulkan baseline uses y-up,
    // so map scene y to z and keep a default spawn height on physics y.
    outSpawn = {sx, 0.8f, sy};
    return true;
}

static std::vector<Renderer::RenderInstance> loadSceneInstances(const std::string& scenePath)
{
    std::vector<Renderer::RenderInstance> out;
    std::ifstream in(scenePath);
    if (!in.is_open()) return out;

    json j;
    try {
        in >> j;
    } catch (...) {
        return out;
    }

    if (!j.contains("entities") || !j["entities"].is_array()) return out;

    for (const auto& e : j["entities"]) {
        if (!e.is_object()) continue;
        const float ex = e.value("x", 0.0f);
        const float ez = e.value("y", 0.0f);
        float ey = 0.6f;
        dash::physics::Vec3 color{0.82f, 0.34f, 0.34f};
        dash::physics::Vec3 scale{0.22f, 0.40f, 0.22f};
        bool isPlayer = false;
        if (e.contains("type") && e["type"].is_string()) {
            const std::string t = e["type"].get<std::string>();
            if (t == "Player") {
                ey = 1.0f;
                color = {0.30f, 0.58f, 0.95f};
                scale = {0.26f, 0.52f, 0.26f};
                isPlayer = true;
            }
        }
        out.push_back({{ex, ey, ez}, scale, color, isPlayer});
    }
    return out;
}

// Helper: Get tile color based on tile type (0-8)
static dash::physics::Vec3 getTileColor(int tileType)
{
    // Tile types: 0=DeepWater, 1=Water, 2=Sand, 3=Grass, 4=Forest, 5=Dirt, 6=Stone, 7=Mountain, 8=Snow
    switch (tileType) {
        case 0:  return {0.04f, 0.07f, 0.22f};   // DeepWater
        case 1:  return {0.08f, 0.14f, 0.31f};   // Water
        case 2:  return {0.43f, 0.35f, 0.20f};   // Sand
        case 3:  return {0.14f, 0.22f, 0.10f};   // Grass
        case 4:  return {0.08f, 0.16f, 0.06f};   // Forest
        case 5:  return {0.25f, 0.16f, 0.10f};   // Dirt
        case 6:  return {0.27f, 0.25f, 0.24f};   // Stone
        case 7:  return {0.22f, 0.20f, 0.19f};   // Mountain
        case 8:  return {0.63f, 0.65f, 0.69f};   // Snow
        default: return {0.14f, 0.22f, 0.10f};   // Default to Grass
    }
}

static float getTileHeight(int tileType)
{
    // Match editor's isometric terrain elevation profile.
    switch (tileType) {
        case 0: return -0.30f; // DeepWater
        case 1: return -0.16f; // Water
        case 2: return  0.00f; // Sand
        case 3: return  0.05f; // Grass
        case 4: return  0.14f; // Forest
        case 5: return  0.08f; // Dirt
        case 6: return  0.22f; // Stone
        case 7: return  0.42f; // Mountain
        case 8: return  0.50f; // Snow
        default: return 0.05f;
    }
}

static float sampleTerrainHeight(const std::vector<float>& mapHeights, int mapWidth, int mapHeight, float x, float z)
{
    if (mapHeights.empty() || mapWidth <= 0 || mapHeight <= 0) return 0.0f;
    int tx = static_cast<int>(std::round(x));
    int tz = static_cast<int>(std::round(z));
    tx = std::max(0, std::min(mapWidth - 1, tx));
    tz = std::max(0, std::min(mapHeight - 1, tz));
    return mapHeights[static_cast<size_t>(tz * mapWidth + tx)];
}

// Helper: Load player position from scene JSON
static bool loadPlayerPosition(const std::string& scenePath, float& outX, float& outZ)
{
    std::ifstream in(scenePath);
    if (!in.is_open()) return false;

    json j;
    try {
        in >> j;
    } catch (...) {
        return false;
    }

    if (!j.contains("entities") || !j["entities"].is_array()) return false;

    for (const auto& e : j["entities"]) {
        if (!e.is_object()) continue;
        if (e.contains("type") && e["type"].is_string()) {
            const std::string t = e["type"].get<std::string>();
            if (t == "Player") {
                outX = e.value("x", 32.0f);
                outZ = e.value("y", 32.0f);
                return true;
            }
        }
    }
    return false;
}

static std::vector<Renderer::RenderInstance> loadTerrainInstances(
    const std::string& scenePath,
    std::vector<float>* outHeightMap,
    int* outMapWidth,
    int* outMapHeight)
{
    std::vector<Renderer::RenderInstance> out;
    if (outHeightMap) outHeightMap->clear();
    if (outMapWidth) *outMapWidth = 0;
    if (outMapHeight) *outMapHeight = 0;

    std::ifstream in(scenePath);
    if (!in.is_open()) return out;

    json j;
    try {
        in >> j;
    } catch (...) {
        return out;
    }

    // Try to load tilemap from JSON
    if (j.contains("tilemap") && j["tilemap"].is_array()) {
        const auto& tilemap = j["tilemap"];
        const int worldWidth = j.value("worldWidth", 64);
        const int worldHeight = j.value("worldHeight", worldWidth > 0 ? static_cast<int>(tilemap.size()) / worldWidth : 0);

        if (outHeightMap && worldWidth > 0 && worldHeight > 0) {
            outHeightMap->assign(static_cast<size_t>(worldWidth * worldHeight), 0.0f);
            if (outMapWidth) *outMapWidth = worldWidth;
            if (outMapHeight) *outMapHeight = worldHeight;
        }
        
        // Render all tiles with their corresponding colors
        for (int idx = 0; idx < static_cast<int>(tilemap.size()); ++idx) {
            const int y = idx / worldWidth;
            const int x = idx % worldWidth;
            
            const int tileType = tilemap[idx].is_number() ? tilemap[idx].get<int>() : 3;  // Default to Grass
            const dash::physics::Vec3 color = getTileColor(tileType);
            const float h = getTileHeight(tileType);

            if (outHeightMap && idx < static_cast<int>(outHeightMap->size())) {
                (*outHeightMap)[static_cast<size_t>(idx)] = h;
            }
            
            out.push_back({
                {static_cast<float>(x), h, static_cast<float>(y)},
                {0.48f, 0.03f, 0.48f},
                color
            });
        }
        return out;
    }

    // Fallback: Generate checkerboard around entities if no tilemap
    int minX = 28;
    int maxX = 36;
    int minZ = 28;
    int maxZ = 36;

    if (j.contains("entities") && j["entities"].is_array() && !j["entities"].empty()) {
        bool first = true;
        for (const auto& e : j["entities"]) {
            if (!e.is_object()) continue;
            const int ex = static_cast<int>(std::round(e.value("x", 0.0f)));
            const int ez = static_cast<int>(std::round(e.value("y", 0.0f)));
            if (first) {
                minX = maxX = ex;
                minZ = maxZ = ez;
                first = false;
            } else {
                minX = std::min(minX, ex);
                maxX = std::max(maxX, ex);
                minZ = std::min(minZ, ez);
                maxZ = std::max(maxZ, ez);
            }
        }
    }

    minX -= 3;
    maxX += 3;
    minZ -= 3;
    maxZ += 3;
    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            const bool checker = ((x + z) & 1) == 0;
            out.push_back({
                {static_cast<float>(x), -1.05f, static_cast<float>(z)},
                {0.48f, 0.03f, 0.48f},
                checker ? dash::physics::Vec3{0.24f, 0.34f, 0.24f}
                        : dash::physics::Vec3{0.18f, 0.28f, 0.18f}
            });
        }
    }
    return out;
}

} // namespace

Renderer::~Renderer()
{
    shutdown();
}

void Renderer::setScenePath(const std::string& scenePath)
{
    scenePath_ = scenePath;
}

void Renderer::setEditorStatePath(const std::string& statePath)
{
    editorStatePath_ = statePath;
}

void Renderer::setEmbeddedPreview(bool enabled)
{
    embeddedPreview_ = enabled;
}

void Renderer::applyEditorStateIfNeeded(GLFWwindow* window)
{
    if (editorStatePath_.empty()) return;

    static bool s_loggedStatePath = false;
    static bool s_loggedOpenFail = false;
    static bool s_loggedParseFail = false;
    static int s_stateReadCounter = 0;
    if (!s_loggedStatePath) {
        std::fprintf(stderr, "[VSTEP] editor state sync enabled path=%s\n", editorStatePath_.c_str());
        s_loggedStatePath = true;
    }

    const auto now = std::chrono::steady_clock::now();
    if (lastEditorStateRead_.time_since_epoch().count() != 0) {
        const auto deltaMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastEditorStateRead_).count();
        if (deltaMs < 16) return;
    }
    lastEditorStateRead_ = now;

    std::ifstream in(editorStatePath_);
    if (!in.is_open()) {
        if (!s_loggedOpenFail) {
            std::fprintf(stderr, "[VFAIL] could not open editor state file: %s\n", editorStatePath_.c_str());
            s_loggedOpenFail = true;
        }
        return;
    }
    s_loggedOpenFail = false;

    json j;
    try {
        in >> j;
    } catch (...) {
        if (!s_loggedParseFail) {
            std::fprintf(stderr, "[VFAIL] invalid JSON in editor state file: %s\n", editorStatePath_.c_str());
            s_loggedParseFail = true;
        }
        return;
    }
    s_loggedParseFail = false;
    ++s_stateReadCounter;

    if (j.contains("camera") && j["camera"].is_object()) {
        const auto& c = j["camera"];
        // Editor camera values represent viewport target/center in world space.
        // Convert to Vulkan free camera by placing the eye at an isometric offset.
        const float targetX = c.value("x", cameraX_);
        const float targetZ = c.value("z", c.value("forward", cameraZ_));
        const float zoom = std::max(0.10f, c.value("zoom", 1.0f));
        const float editorYaw = c.value("isoYawDeg", yawDegrees_);
        const float editorPitch = c.value("isoPitchDeg", std::fabs(pitchDegrees_));
        const float followDistance = std::max(0.10f, c.value("followDistance", followDistance_));
        const float followHeight = std::max(0.0f, c.value("followHeight", followHeight_));

        // Check if editor camera state changed significantly
        const float eps = 0.01f;
        bool posChanged = std::fabs(targetX - lastEditorTargetX_) > eps || 
                         std::fabs(targetZ - lastEditorTargetZ_) > eps;
        bool zoomChanged = std::fabs(zoom - lastEditorZoom_) > eps;
        bool angleChanged = std::fabs(editorYaw - lastEditorYaw_) > eps ||
                          std::fabs(editorPitch - lastEditorPitch_) > eps;
        bool followChanged = std::fabs(followDistance - lastEditorFollowDistance_) > eps ||
                             std::fabs(followHeight - lastEditorFollowHeight_) > eps;

        // Only update camera if something changed (don't overwrite WASD each frame)
        if (posChanged || zoomChanged || angleChanged || followChanged) {
            // Convert isometric yaw to Vulkan camera yaw:
            // Iso yaw=45° places camera at SE looking NW, which in Vulkan
            // forward-vector convention corresponds to yaw = -(isoYaw + 90).
            yawDegrees_ = -(editorYaw + 90.0f);
            pitchDegrees_ = -std::fabs(editorPitch);
            followDistance_ = followDistance;
            followHeight_ = followHeight;

            if (pitchDegrees_ > 89.0f) pitchDegrees_ = 89.0f;
            if (pitchDegrees_ < -89.0f) pitchDegrees_ = -89.0f;

            const float yawRad = yawDegrees_ * 0.0174532925f;
            const float pitchRad = pitchDegrees_ * 0.0174532925f;
            const float fx = std::cos(yawRad) * std::cos(pitchRad);
            const float fy = std::sin(pitchRad);
            const float fz = std::sin(yawRad) * std::cos(pitchRad);

            // Zoom in editor means "closer", so reduce distance with higher zoom.
            const float distance = 22.0f / zoom;
            cameraX_ = targetX * TILE_SCALE - fx * distance;
            cameraY_ = std::max(1.2f, 6.0f - fy * distance);
            cameraZ_ = targetZ * TILE_SCALE - fz * distance;

            static int s_camConvLogCount = 0;
            if (s_camConvLogCount < 3) {
                std::fprintf(stderr,
                             "[VSTEP] camera changed: editor(targetX=%.2f,targetZ=%.2f,zoom=%.2f) -> vulkan(camX=%.2f,camZ=%.2f)\n",
                             targetX, targetZ, zoom, cameraX_, cameraZ_);
                ++s_camConvLogCount;
            }

            // Remember these values for next frame
            lastEditorTargetX_ = targetX;
            lastEditorTargetZ_ = targetZ;
            lastEditorZoom_ = zoom;
            lastEditorYaw_ = editorYaw;
            lastEditorPitch_ = editorPitch;
            lastEditorFollowDistance_ = followDistance;
            lastEditorFollowHeight_ = followHeight;
        }
    }

    hasExternalSelection_ = false;
    if (j.contains("selection") && j["selection"].is_object()) {
        const auto& s = j["selection"];
        const uint64_t entityId = s.value("entityId", static_cast<uint64_t>(0));
        if (entityId != 0) {
            const float sx = s.value("x", cubeTransform_.position.x);
            const float sy = s.value("y", cubeTransform_.position.z);
            const float sz = s.value("z", cubeTransform_.position.y);
            // Editor selection uses (x,y,zHeight). Map to Vulkan (x,yVertical,zGround).
            cubeTransform_.position.x = sx;
            cubeTransform_.position.y = sz;
            cubeTransform_.position.z = sy;
            hasExternalSelection_ = true;
        }
    }

    if (embeddedPreview_ && window && j.contains("viewport") && j["viewport"].is_object()) {
        const auto& vp = j["viewport"];

        // Read fog parameters
        bool fogEnabled = vp.value("fogEnabled", true);
        fogStart_ = fogEnabled ? vp.value("fogStart", 80.0f) : 9999.0f;
        fogEnd_   = fogEnabled ? vp.value("fogEnd", 160.0f) : 9999.0f;

        int sx = static_cast<int>(vp.value("screenX", 0.0f));
        int sy = static_cast<int>(vp.value("screenY", 0.0f));
        int sw = std::max(64, static_cast<int>(vp.value("screenW", 640.0f)));
        int sh = std::max(64, static_cast<int>(vp.value("screenH", 360.0f)));

        // Use logical coordinates directly. On macOS this keeps editor and GLFW in
        // the same coordinate space for positioned utility windows.

        glfwSetWindowAttrib(window, GLFW_DECORATED, GLFW_FALSE);
        glfwSetWindowAttrib(window, GLFW_FLOATING, GLFW_TRUE);
        glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_FALSE);
        glfwSetWindowPos(window, sx, sy);
        glfwSetWindowSize(window, sw, sh);

        if (!loggedEmbeddedDocking_) {
            int wx = 0, wy = 0, ww = 0, wh = 0;
            glfwGetWindowPos(window, &wx, &wy);
            glfwGetWindowSize(window, &ww, &wh);
            std::fprintf(stderr,
                         "[D84] Embedded docking applied: target=(%d,%d %dx%d) actual=(%d,%d %dx%d)\n",
                         sx, sy, sw, sh, wx, wy, ww, wh);
            loggedEmbeddedDocking_ = true;
        }

        if ((s_stateReadCounter % 120) == 1) {
            int wx = 0, wy = 0, ww = 0, wh = 0;
            glfwGetWindowPos(window, &wx, &wy);
            glfwGetWindowSize(window, &ww, &wh);
            std::fprintf(stderr,
                         "[VSTEP] state tick #%d cam=(%.2f,%.2f,%.2f) yaw=%.2f pitch=%.2f dock target=(%d,%d %dx%d) actual=(%d,%d %dx%d)\n",
                         s_stateReadCounter,
                         cameraX_, cameraY_, cameraZ_,
                         yawDegrees_, pitchDegrees_,
                         sx, sy, sw, sh,
                         wx, wy, ww, wh);
        }
    }
}

bool Renderer::createInstance(const std::vector<const char*>& requiredExtensions)
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Dash Vulkan Bootstrap";
    appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.pEngineName = "Dash-Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    std::set<std::string> extensionsSet;
    for (const char* ext : requiredExtensions) {
        if (ext) extensionsSet.emplace(ext);
    }

    uint32_t availableExtCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, nullptr);
    std::vector<VkExtensionProperties> availableExts(availableExtCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &availableExtCount, availableExts.data());

    bool hasPortabilityEnumeration = false;
    bool hasGetPhysicalDeviceProps2 = false;
    for (const auto& ext : availableExts) {
        if (std::string(ext.extensionName) == VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) {
            hasPortabilityEnumeration = true;
        }
        if (std::string(ext.extensionName) == kGetPhysicalDeviceProps2Ext) {
            hasGetPhysicalDeviceProps2 = true;
        }
    }
    if (hasPortabilityEnumeration) extensionsSet.emplace(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    if (hasGetPhysicalDeviceProps2) extensionsSet.emplace(kGetPhysicalDeviceProps2Ext);

    std::vector<const char*> finalExtensions;
    finalExtensions.reserve(extensionsSet.size());
    for (const auto& ext : extensionsSet) finalExtensions.push_back(ext.c_str());

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(finalExtensions.size());
    createInfo.ppEnabledExtensionNames = finalExtensions.data();
    if (hasPortabilityEnumeration) {
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }

    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    if (hasValidationLayer()) {
        createInfo.enabledLayerCount = 1;
        createInfo.ppEnabledLayerNames = &validationLayer;
    }

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &instance_);
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] vkCreateInstance returned %d\n", static_cast<int>(result));
        return false;
    }
    return true;
}

bool Renderer::createDescriptors()
{
    VkDescriptorSetLayoutBinding uboBinding{};
    uboBinding.binding = 0;
    uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    uboBinding.descriptorCount = 1;
    uboBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.descriptorCount = 1;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    std::array<VkDescriptorSetLayoutBinding, 2> bindings = {uboBinding, samplerBinding};

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();
    if (vkCreateDescriptorSetLayout(deviceContext_.device(), &layoutInfo, nullptr, &descriptorSetLayout_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] Failed to create descriptor set layout.\n");
        return false;
    }

    std::array<VkDescriptorPoolSize, 2> poolSizes = {{
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, swapchain_.imageCount()},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, swapchain_.imageCount()}
    }};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = swapchain_.imageCount();
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    if (vkCreateDescriptorPool(deviceContext_.device(), &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] Failed to create descriptor pool.\n");
        return false;
    }

    descriptorSets_.resize(swapchain_.imageCount(), VK_NULL_HANDLE);
    std::vector<VkDescriptorSetLayout> layouts(swapchain_.imageCount(), descriptorSetLayout_);
    VkDescriptorSetAllocateInfo dsAlloc{};
    dsAlloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    dsAlloc.descriptorPool = descriptorPool_;
    dsAlloc.descriptorSetCount = swapchain_.imageCount();
    dsAlloc.pSetLayouts = layouts.data();
    if (vkAllocateDescriptorSets(deviceContext_.device(), &dsAlloc, descriptorSets_.data()) != VK_SUCCESS) {
        std::fprintf(stderr, "[D78] Failed to allocate descriptor sets.\n");
        return false;
    }

    return true;
}

bool Renderer::createPerFrameUniformBuffers()
{
    uniformBuffers_.resize(swapchain_.imageCount(), VK_NULL_HANDLE);
    uniformMemories_.resize(swapchain_.imageCount(), VK_NULL_HANDLE);

    // Create default white texture for sampler binding
    if (!TextureLoader::createDefaultWhite(
            deviceContext_.physicalDevice(),
            deviceContext_.device(),
            deviceContext_.graphicsQueue(),
            frameGraph_.commandPool(),
            defaultTexture_)) {
        std::fprintf(stderr, "[D78] Failed to create default white texture.\n");
        return false;
    }

    for (uint32_t i = 0; i < swapchain_.imageCount(); ++i) {
        if (!createHostVisibleBuffer(
                deviceContext_.physicalDevice(),
                deviceContext_.device(),
                sizeof(CameraUBO),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                uniformBuffers_[i],
                uniformMemories_[i])) {
            std::fprintf(stderr, "[D78] Failed to create uniform buffer %u.\n", i);
            return false;
        }

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = uniformBuffers_[i];
        bufferInfo.offset = 0;
        bufferInfo.range = sizeof(CameraUBO);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = defaultTexture_.imageView;
        imageInfo.sampler = defaultTexture_.sampler;

        std::array<VkWriteDescriptorSet, 2> writes{};

        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSets_[i];
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[0].descriptorCount = 1;
        writes[0].pBufferInfo = &bufferInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSets_[i];
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(deviceContext_.device(),
                               static_cast<uint32_t>(writes.size()),
                               writes.data(), 0, nullptr);
    }

    return true;
}

bool Renderer::createPipeline()
{
    const std::string vert = std::string(VULKAN_SHADER_DIR) + "/basic.vert.spv";
    const std::string frag = std::string(VULKAN_SHADER_DIR) + "/basic.frag.spv";

    std::string pipelineError;
    if (!PipelineBuilder::createBasicPipeline(
            deviceContext_.device(),
            swapchain_.extent(),
            swapchain_.renderPass(),
            descriptorSetLayout_,
            vert,
            frag,
            pipelineLayout_,
            pipeline_,
            pipelineError)) {
        std::fprintf(stderr, "[D78] Pipeline creation failed: %s\n", pipelineError.c_str());
        return false;
    }

    // Create textured pipeline using the same layout but different shaders
    const std::string texVert = std::string(VULKAN_SHADER_DIR) + "/textured.vert.spv";
    const std::string texFrag = std::string(VULKAN_SHADER_DIR) + "/textured.frag.spv";

    std::string texPipelineError;
    if (!PipelineBuilder::createBasicPipeline(
            deviceContext_.device(),
            swapchain_.extent(),
            swapchain_.renderPass(),
            descriptorSetLayout_,
            texVert,
            texFrag,
            texturedPipelineLayout_,
            texturedPipeline_,
            texPipelineError)) {
        std::fprintf(stderr, "[D78] Textured pipeline creation failed: %s (non-fatal)\n",
                     texPipelineError.c_str());
        // Non-fatal — fall back to basic pipeline
    }

    // Create terrain pipeline for heightmap mesh rendering
    const std::string terrainVert = std::string(VULKAN_SHADER_DIR) + "/terrain.vert.spv";
    const std::string terrainFrag = std::string(VULKAN_SHADER_DIR) + "/terrain.frag.spv";

    std::string terrainPipelineError;
    if (!PipelineBuilder::createTerrainPipeline(
            deviceContext_.device(),
            swapchain_.extent(),
            swapchain_.renderPass(),
            descriptorSetLayout_,
            terrainVert,
            terrainFrag,
            terrainPipelineLayout_,
            terrainPipeline_,
            terrainPipelineError)) {
        std::fprintf(stderr, "[D78] Terrain pipeline creation failed: %s (non-fatal)\n",
                     terrainPipelineError.c_str());
        // Non-fatal — terrain will fall back to per-tile cube instances
    }

    return true;
}

bool Renderer::init(WindowContext& window)
{
    if (!createInstance(window.requiredVulkanExtensions())) return false;
    if (!window.createSurface(instance_, surface_)) return false;
    if (!deviceContext_.init(instance_, surface_)) return false;
    if (!swapchain_.init(deviceContext_, surface_, window.handle())) return false;

    if (!createDescriptors()) return false;
    if (!createPipeline()) return false;

    if (!meshBuffers_.initCube(deviceContext_.physicalDevice(), deviceContext_.device())) {
        std::fprintf(stderr, "[D78] Failed to create cube buffers.\n");
        return false;
    }

    if (!frameGraph_.init(
            deviceContext_.device(),
            deviceContext_.queueFamilies().graphicsFamily.value(),
            deviceContext_.graphicsQueue(),
            deviceContext_.presentQueue(),
            swapchain_.swapchain(),
            swapchain_.extent(),
            swapchain_.renderPass(),
            swapchain_.imageViews(),
            swapchain_.depthImageView())) {
        return false;
    }

    if (!createPerFrameUniformBuffers()) return false;

    if (!physicsWorld_.init()) {
        std::fprintf(stderr, "[D80] PhysicsWorld initialization failed.\n");
        return false;
    }
    physicsWorld_.setGravity({0.0f, -9.8f, 0.0f});
    physicsWorld_.setRestitution(0.20f);
    physicsWorld_.setCollisionCallback([](const dash::physics::CollisionEvent& ev) {
        if (ev.type == dash::physics::CollisionEventType::Enter) {
            std::printf("[D82] Collision Enter: %d <-> %d\n", ev.a, ev.b);
        }
    });

    floorBodyId_ = physicsWorld_.createStaticPlane(-0.7f);

    dash::physics::Vec3 spawn{0.0f, 0.8f, 0.0f};
    bool loadedSceneSpawn = false;
    if (!scenePath_.empty()) {
        dash::physics::Vec3 sceneSpawn{};
        if (loadSceneSpawnPoint(scenePath_, sceneSpawn)) {
            spawn = sceneSpawn;
            loadedSceneSpawn = true;
            std::printf("[D84] Loaded scene spawn from %s -> (%.3f, %.3f, %.3f)\n",
                        scenePath_.c_str(), spawn.x, spawn.y, spawn.z);
        } else {
            std::printf("[D84] Could not parse scene spawn from %s (using default).\n", scenePath_.c_str());
        }

        sceneInstances_ = loadSceneInstances(scenePath_);
        terrainInstances_ = loadTerrainInstances(scenePath_, &terrainHeightMap_, &terrainMapWidth_, &terrainMapHeight_);
        std::fprintf(stderr, "[VSTEP] scene instances loaded: %zu\n", sceneInstances_.size());
        std::fprintf(stderr, "[VSTEP] terrain instances loaded: %zu\n", terrainInstances_.size());

        // Build heightmap polygon mesh for Vulkan terrain rendering
        if (terrainPipeline_ != VK_NULL_HANDLE) {
            TerrainMesh terrainMesh;
            terrainMesh.generate(42);
            std::vector<TerrainVkVertex> terrainVerts;
            std::vector<uint32_t> terrainIndices;
            terrainMesh.buildVulkanMesh(terrainVerts, terrainIndices);

            if (!terrainVerts.empty() && !terrainIndices.empty()) {
                if (terrainMeshBuffers_.initFromData(
                        deviceContext_.physicalDevice(),
                        deviceContext_.device(),
                        terrainVerts.data(),
                        static_cast<uint32_t>(terrainVerts.size() * sizeof(TerrainVkVertex)),
                        terrainIndices.data(),
                        static_cast<uint32_t>(terrainIndices.size() * sizeof(uint32_t)),
                        static_cast<uint32_t>(terrainIndices.size()))) {
                    std::fprintf(stderr, "[VSTEP] Terrain mesh uploaded: %zu verts, %zu indices\n",
                                 terrainVerts.size(), terrainIndices.size());
                    // Clear old tile instances — terrain mesh replaces them
                    terrainInstances_.clear();
                } else {
                    std::fprintf(stderr, "[VSTEP] Failed to upload terrain mesh buffers\n");
                }
            }
        }

            // Load player position for WASD movement
            if (loadPlayerPosition(scenePath_, playerX_, playerZ_)) {
                playerLoaded_ = true;
                const float playerHalfHeight = 0.52f;
                playerY_ = sampleTerrainHeight(terrainHeightMap_, terrainMapWidth_, terrainMapHeight_, playerX_, playerZ_) + playerHalfHeight;
                playerVelY_ = 0.0f;
                std::fprintf(stderr, "[VSTEP] Player position loaded: (%.2f, %.2f)\n", playerX_, playerZ_);
            } else {
                playerLoaded_ = false;
                playerX_ = 32.0f;
                playerZ_ = 32.0f;
                playerY_ = 1.0f;
                playerVelY_ = 0.0f;
                std::fprintf(stderr, "[VSTEP] Player position not found, using default (32, 32)\n");
            }
    }

    if (sceneInstances_.empty()) {
        sceneInstances_.push_back({spawn, {0.26f, 0.52f, 0.26f}, {0.30f, 0.58f, 0.95f}});
    }

    // Generate a small checkerboard floor when no terrain was loaded
    if (terrainInstances_.empty()) {
        for (int z = -3; z <= 3; ++z) {
            for (int x = -3; x <= 3; ++x) {
                const bool checker = ((x + z) & 1) == 0;
                terrainInstances_.push_back({
                    {static_cast<float>(x), -0.7f, static_cast<float>(z)},
                    {0.48f, 0.03f, 0.48f},
                    checker ? dash::physics::Vec3{0.24f, 0.34f, 0.24f}
                            : dash::physics::Vec3{0.18f, 0.28f, 0.18f}
                });
            }
        }
    }

    cubeBodyId_ = physicsWorld_.createDynamicBox(spawn, {0.30f, 0.30f, 0.30f}, 1.0f);
    transformProxy_.syncFromPhysics(physicsWorld_, cubeBodyId_, cubeTransform_);

    // In standalone mode there is no editor camera sync file, so align camera
    // to the loaded scene spawn to avoid starting with an empty/black view.
    if (loadedSceneSpawn) {
        cameraX_ = spawn.x * TILE_SCALE + 3.0f;
        cameraY_ = spawn.y + 2.5f;
        cameraZ_ = spawn.z * TILE_SCALE + 3.0f;
        yawDegrees_ = -135.0f;
        pitchDegrees_ = -28.0f;
        pendingAutoFocus_ = true;
    } else if (scenePath_.empty()) {
        // No scene provided: position camera to see the default physics cube
        cameraX_ = 2.0f;
        cameraY_ = 2.0f;
        cameraZ_ = 2.0f;
        yawDegrees_ = -135.0f;
        pitchDegrees_ = -25.0f;
    }

    dash::physics::DebugPhysicsDraw::logBodyAabb(physicsWorld_, floorBodyId_, "floor");
    dash::physics::DebugPhysicsDraw::logBodyAabb(physicsWorld_, cubeBodyId_, "cube");

    initialized_ = true;
    std::puts("[D78] Renderer + FrameGraphLite initialized.");
    std::puts("[D80-D83] PhysicsWorld active (fixed-step + cube/plane baseline).");
    return true;
}

bool Renderer::updateCameraUbo(uint32_t imageIndex)
{
    auto forwardFromAngles = [&]() -> Vec3 {
        const float yaw = yawDegrees_ * 0.0174532925f;
        const float pitch = pitchDegrees_ * 0.0174532925f;
        return normalize({
            std::cos(yaw) * std::cos(pitch),
            std::sin(pitch),
            std::sin(yaw) * std::cos(pitch)
        });
    };

    Vec3 forward = forwardFromAngles();
    const Vec3 target{ cameraX_ + forward.x, cameraY_ + forward.y, cameraZ_ + forward.z };

    Mat4 view = lookAt({cameraX_, cameraY_, cameraZ_}, target, {0.0f, 1.0f, 0.0f});
    Mat4 proj = perspective(
        60.0f * 0.0174532925f,
        static_cast<float>(swapchain_.extent().width) / static_cast<float>(swapchain_.extent().height),
        0.1f,
        100.0f);
    proj.m[5] *= -1.0f;

    CameraUBO ubo{};
    ubo.viewProj = multiply(proj, view);

    void* mapped = nullptr;
    if (vkMapMemory(deviceContext_.device(), uniformMemories_[imageIndex], 0, sizeof(CameraUBO), 0, &mapped) != VK_SUCCESS) {
        return false;
    }
    std::memcpy(mapped, &ubo, sizeof(CameraUBO));
    vkUnmapMemory(deviceContext_.device(), uniformMemories_[imageIndex]);
    return true;
}

bool Renderer::runSmoke(WindowContext& window, uint32_t targetFrames)
{
    if (!initialized_) return false;

    const bool infiniteRun = (targetFrames == 0);

    uint32_t renderedFrames = 0;
    auto lastTime = std::chrono::steady_clock::now();

    while ((infiniteRun || renderedFrames < targetFrames) && !window.shouldClose()) {
        window.pollEvents();

        if (infiniteRun) {
            applyEditorStateIfNeeded(window.handle());
        }

        const auto now = std::chrono::steady_clock::now();
        const float dt = std::chrono::duration<float>(now - lastTime).count();
        lastTime = now;
        elapsedSeconds_ += dt;

        if (!(infiniteRun && hasExternalSelection_)) {
            fixedAccumulator_ += dt;
            static constexpr float kFixedDt = 1.0f / 60.0f;
            static constexpr int kMaxSubsteps = 4;
            int substeps = 0;
            while (fixedAccumulator_ >= kFixedDt && substeps < kMaxSubsteps) {
                physicsWorld_.step(kFixedDt);
                fixedAccumulator_ -= kFixedDt;
                ++substeps;
            }
            transformProxy_.syncFromPhysics(physicsWorld_, cubeBodyId_, cubeTransform_);
        }

        // Standalone startup helper: align camera to look at the loaded scene body
        // on the first frame so we don't start with an empty/black-looking view.
        if (pendingAutoFocus_) {
            const float dx = cubeTransform_.position.x - cameraX_;
            const float dy = cubeTransform_.position.y - cameraY_;
            const float dz = cubeTransform_.position.z - cameraZ_;
            const float flat = std::sqrt(dx * dx + dz * dz);
            if (flat > 0.0001f || std::fabs(dy) > 0.0001f) {
                yawDegrees_ = std::atan2(dz, dx) * 57.2957795f;
                pitchDegrees_ = std::atan2(dy, std::max(0.0001f, flat)) * 57.2957795f;
                if (pitchDegrees_ > 89.0f) pitchDegrees_ = 89.0f;
                if (pitchDegrees_ < -89.0f) pitchDegrees_ = -89.0f;
            }
            pendingAutoFocus_ = false;
        }

        // Isometric camera controls (synchronized with editor viewport)

        const float moveSpeed = inputBindings_.moveSpeed;

        // ── Isometric WASD controls (like editor) ──────────────────────────
        // These work in both standalone and embedded preview modes
        // World movement: W/S move along diagonal, A/D move along perpendicular diagonal

        // In isometric view (looking at X-Z plane from above):
        // W: move north-west (negative X, negative Z)
        // S: move south-east (positive X, positive Z)
        // A: move south-west (negative X, positive Z)
        // D: move north-east (positive X, negative Z)

        // ── Player movement with WASD (player-centric gameplay) ──────────────
        // Move the Hero entity and keep camera centered in fixed isometric view.
        if (playerLoaded_) {
            float inputX = 0.0f;
            float inputZ = 0.0f;
            if (glfwGetKey(window.handle(), inputBindings_.keyForward) == GLFW_PRESS) {
                inputX -= 1.0f;
                inputZ -= 1.0f;
            }
            if (glfwGetKey(window.handle(), inputBindings_.keyBackward) == GLFW_PRESS) {
                inputX += 1.0f;
                inputZ += 1.0f;
            }
            if (glfwGetKey(window.handle(), inputBindings_.keyLeft) == GLFW_PRESS) {
                inputX -= 1.0f;
                inputZ += 1.0f;
            }
            if (glfwGetKey(window.handle(), inputBindings_.keyRight) == GLFW_PRESS) {
                inputX += 1.0f;
                inputZ -= 1.0f;
            }

            const float len = std::sqrt(inputX * inputX + inputZ * inputZ);
            if (len > 0.0001f) {
                playerX_ += (inputX / len) * moveSpeed * dt;
                playerZ_ += (inputZ / len) * moveSpeed * dt;
            }

            // Keep movement inside terrain bounds when map dimensions are known.
            if (terrainMapWidth_ > 0) {
                playerX_ = std::max(0.0f, std::min(static_cast<float>(terrainMapWidth_ - 1), playerX_));
            }
            if (terrainMapHeight_ > 0) {
                playerZ_ = std::max(0.0f, std::min(static_cast<float>(terrainMapHeight_ - 1), playerZ_));
            }

            // Apply gravity against terrain height.
            const float playerHalfHeight = 0.52f;
            const float gravity = -9.8f;
            playerVelY_ += gravity * dt;
            playerY_ += playerVelY_ * dt;
            const float groundY = sampleTerrainHeight(terrainHeightMap_, terrainMapWidth_, terrainMapHeight_, playerX_, playerZ_) + playerHalfHeight;
            if (playerY_ < groundY) {
                playerY_ = groundY;
                if (playerVelY_ < 0.0f) playerVelY_ = 0.0f;
            }

            for (auto& instance : sceneInstances_) {
                if (instance.isPlayer) {
                    instance.position.x = playerX_;
                    instance.position.y = playerY_;
                    instance.position.z = playerZ_;
                }
            }

            // ── Camera follows player (centered isometric view) ────────────────
            // Fixed iso orbit around Hero.
            const float yawRad = yawDegrees_ * 0.0174532925f;
            const float pitchRad = pitchDegrees_ * 0.0174532925f;
            const float fx = std::cos(yawRad) * std::cos(pitchRad);
            const float fy = std::sin(pitchRad);
            const float fz = std::sin(yawRad) * std::cos(pitchRad);

            const float targetY = playerY_;
            cameraX_ = playerX_ * TILE_SCALE - fx * followDistance_;
            cameraY_ = targetY - fy * followDistance_ + followHeight_;
            cameraZ_ = playerZ_ * TILE_SCALE - fz * followDistance_;
        }
        // ── Mouse look with right-click (legacy mode) ──────────────────────
        if (glfwGetMouseButton(window.handle(), inputBindings_.mouseButtonLook) == GLFW_PRESS) {
            double mouseX = 0.0;
            double mouseY = 0.0;
            glfwGetCursorPos(window.handle(), &mouseX, &mouseY);
            if (!hadLookFrame_) {
                hadLookFrame_ = true;
                lastMouseX_ = mouseX;
                lastMouseY_ = mouseY;
            }
            const float dx = static_cast<float>(mouseX - lastMouseX_);
            const float dy = static_cast<float>(mouseY - lastMouseY_);
            lastMouseX_ = mouseX;
            lastMouseY_ = mouseY;

            yawDegrees_ += dx * inputBindings_.mouseSensitivity;
            pitchDegrees_ -= dy * inputBindings_.mouseSensitivity;
            if (pitchDegrees_ > inputBindings_.pitchMax) pitchDegrees_ = inputBindings_.pitchMax;
            if (pitchDegrees_ < inputBindings_.pitchMin) pitchDegrees_ = inputBindings_.pitchMin;
        } else {
            hadLookFrame_ = false;
        }

        // Frame rendering (always execute regardless of input mode)
        uint32_t imageIndex = 0;
        if (!frameGraph_.beginFrame(imageIndex)) break;
        if (!updateCameraUbo(imageIndex)) break;

        VkCommandBuffer cmd = frameGraph_.commandBuffer(imageIndex);
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        if (vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) break;

        VkClearValue clearValues[2]{};
        clearValues[0].color = { {0.14f, 0.16f, 0.20f, 1.0f} };
        clearValues[1].depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = swapchain_.renderPass();
        rpBegin.framebuffer = frameGraph_.framebuffer(imageIndex);
        rpBegin.renderArea.offset = {0, 0};
        rpBegin.renderArea.extent = swapchain_.extent();
        rpBegin.clearValueCount = 2;
        rpBegin.pClearValues = clearValues;

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          texturedPipeline_ != VK_NULL_HANDLE ? texturedPipeline_ : pipeline_);
        vkCmdBindDescriptorSets(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout_,
            0,
            1,
            &descriptorSets_[imageIndex],
            0,
            nullptr);

        VkBuffer vertexBuffers[] = { meshBuffers_.vertexBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
        vkCmdBindIndexBuffer(cmd, meshBuffers_.indexBuffer(), 0, VK_INDEX_TYPE_UINT16);

        if (!embeddedPreview_) {
            const float pc[12] = {
                cubeTransform_.position.x * TILE_SCALE,
                cubeTransform_.position.y,
                cubeTransform_.position.z * TILE_SCALE,
                0.0f,
                0.30f, 0.30f, 0.30f, 0.0f,
                0.86f, 0.34f, 0.34f, 1.0f
            };
            vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), pc);
            vkCmdDrawIndexed(cmd, meshBuffers_.indexCount(), 1, 0, 0, 0);
        }

        for (const auto& tile : terrainInstances_) {
            const float pc[12] = {
                tile.position.x * TILE_SCALE,
                tile.position.y,
                tile.position.z * TILE_SCALE,
                0.0f,
                tile.scale.x,
                tile.scale.y,
                tile.scale.z,
                0.0f,
                tile.color.x,
                tile.color.y,
                tile.color.z,
                1.0f
            };
            vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), pc);
            vkCmdDrawIndexed(cmd, meshBuffers_.indexCount(), 1, 0, 0, 0);
        }

        // ── Terrain heightmap mesh (single draw call) ─────────────────────
        if (terrainPipeline_ != VK_NULL_HANDLE && terrainMeshBuffers_.indexCount() > 0) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, terrainPipeline_);
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                terrainPipelineLayout_,
                0, 1,
                &descriptorSets_[imageIndex],
                0, nullptr);

            VkBuffer terrainVB[] = { terrainMeshBuffers_.vertexBuffer() };
            VkDeviceSize terrainOffsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, terrainVB, terrainOffsets);
            vkCmdBindIndexBuffer(cmd, terrainMeshBuffers_.indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

            // Push constants: eyePos, time, fog params
            const float terrainPC[8] = {
                cameraX_, cameraY_, cameraZ_,
                elapsedSeconds_,
                fogStart_, fogEnd_,
                0.0f, 0.0f
            };
            vkCmdPushConstants(cmd, terrainPipelineLayout_,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                0, sizeof(terrainPC), terrainPC);

            vkCmdDrawIndexed(cmd, terrainMeshBuffers_.indexCount(), 1, 0, 0, 0);

            // Re-bind the basic pipeline for scene entities that follow
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              texturedPipeline_ != VK_NULL_HANDLE ? texturedPipeline_ : pipeline_);
            vkCmdBindDescriptorSets(
                cmd,
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout_,
                0, 1,
                &descriptorSets_[imageIndex],
                0, nullptr);
            VkBuffer cubeVB[] = { meshBuffers_.vertexBuffer() };
            vkCmdBindVertexBuffers(cmd, 0, 1, cubeVB, terrainOffsets);
            vkCmdBindIndexBuffer(cmd, meshBuffers_.indexBuffer(), 0, VK_INDEX_TYPE_UINT16);
        }

        for (const auto& instance : sceneInstances_) {
            const float pc[12] = {
                instance.position.x * TILE_SCALE,
                instance.position.y,
                instance.position.z * TILE_SCALE,
                0.0f,
                instance.scale.x,
                instance.scale.y,
                instance.scale.z,
                0.0f,
                instance.color.x,
                instance.color.y,
                instance.color.z,
                1.0f
            };
            vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), pc);
            vkCmdDrawIndexed(cmd, meshBuffers_.indexCount(), 1, 0, 0, 0);
        }
        vkCmdEndRenderPass(cmd);
        if (vkEndCommandBuffer(cmd) != VK_SUCCESS) break;

        if (!frameGraph_.endFrame(imageIndex)) break;
        ++renderedFrames;

        if (!infiniteRun && renderedFrames == targetFrames) {
            const dash::physics::Vec3 p = physicsWorld_.position(cubeBodyId_);
            std::printf("[D83] Baseline settled cube position: (%.3f, %.3f, %.3f)\n", p.x, p.y, p.z);
        }
    }

    vkDeviceWaitIdle(deviceContext_.device());
    if (infiniteRun) {
        if (embeddedPreview_) {
            std::printf("[D76] Embedded preview loop finished after %u frames.\n", renderedFrames);
        } else {
            std::printf("[D76] Standalone persistent loop finished after %u frames.\n", renderedFrames);
        }
        return true;
    }

    if (renderedFrames >= targetFrames) {
        std::printf("[D76] Smoke test: %u frames rendered successfully.\n", targetFrames);
        return true;
    }

    std::fprintf(stderr, "[D76] Smoke test interrupted at frame %u.\n", renderedFrames);
    return false;
}

void Renderer::shutdown()
{
    if (!initialized_ && instance_ == VK_NULL_HANDLE) return;

    if (deviceContext_.device() != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(deviceContext_.device());
    }

    frameGraph_.shutdown();

    for (size_t i = 0; i < uniformBuffers_.size(); ++i) {
        if (uniformBuffers_[i] != VK_NULL_HANDLE) {
            vkDestroyBuffer(deviceContext_.device(), uniformBuffers_[i], nullptr);
        }
        if (uniformMemories_[i] != VK_NULL_HANDLE) {
            vkFreeMemory(deviceContext_.device(), uniformMemories_[i], nullptr);
        }
    }
    uniformBuffers_.clear();
    uniformMemories_.clear();
    descriptorSets_.clear();

    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(deviceContext_.device(), descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(deviceContext_.device(), descriptorSetLayout_, nullptr);
        descriptorSetLayout_ = VK_NULL_HANDLE;
    }

    meshBuffers_.shutdown(deviceContext_.device());
    terrainMeshBuffers_.shutdown(deviceContext_.device());
    PipelineBuilder::destroy(deviceContext_.device(), pipelineLayout_, pipeline_);
    pipelineLayout_ = VK_NULL_HANDLE;
    pipeline_ = VK_NULL_HANDLE;

    PipelineBuilder::destroy(deviceContext_.device(), texturedPipelineLayout_, texturedPipeline_);
    texturedPipelineLayout_ = VK_NULL_HANDLE;
    texturedPipeline_ = VK_NULL_HANDLE;

    PipelineBuilder::destroy(deviceContext_.device(), terrainPipelineLayout_, terrainPipeline_);
    terrainPipelineLayout_ = VK_NULL_HANDLE;
    terrainPipeline_ = VK_NULL_HANDLE;

    TextureLoader::destroy(deviceContext_.device(), defaultTexture_);

    assetCache_.clear(deviceContext_.device());

    swapchain_.shutdown(deviceContext_.device());
    deviceContext_.shutdown();

    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }

    initialized_ = false;
}

} // namespace dash::vkexp
