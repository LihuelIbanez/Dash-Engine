#pragma once

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

namespace dash::vkexp {

class WindowContext {
public:
    WindowContext() = default;
    ~WindowContext();

    WindowContext(const WindowContext&) = delete;
    WindowContext& operator=(const WindowContext&) = delete;

    bool init(uint32_t width, uint32_t height, const char* title);
    void shutdown();

    GLFWwindow* handle() const { return window_; }
    bool shouldClose() const;
    void pollEvents() const;

    std::vector<const char*> requiredVulkanExtensions() const;
    bool createSurface(VkInstance instance, VkSurfaceKHR& outSurface) const;

private:
    GLFWwindow* window_ = nullptr;
};

} // namespace dash::vkexp
