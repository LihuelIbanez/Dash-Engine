#include "rendering/platform/WindowContext.h"

#include <cstdio>

namespace dash::vkexp {

WindowContext::~WindowContext()
{
    shutdown();
}

bool WindowContext::init(uint32_t width, uint32_t height, const char* title)
{
    if (!glfwInit()) {
        std::fprintf(stderr, "[D71] glfwInit failed.\n");
        return false;
    }

    if (!glfwVulkanSupported()) {
        std::fprintf(stderr, "[D71] GLFW reports Vulkan unsupported on this system.\n");
        glfwTerminate();
        return false;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window_ = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title, nullptr, nullptr);
    if (!window_) {
        std::fprintf(stderr, "[D71] glfwCreateWindow failed.\n");
        glfwTerminate();
        return false;
    }

    return true;
}

void WindowContext::shutdown()
{
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

bool WindowContext::shouldClose() const
{
    return window_ == nullptr || glfwWindowShouldClose(window_);
}

void WindowContext::pollEvents() const
{
    glfwPollEvents();
}

std::vector<const char*> WindowContext::requiredVulkanExtensions() const
{
    uint32_t count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&count);
    std::vector<const char*> out;
    out.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        out.push_back(extensions[i]);
    }
    return out;
}

bool WindowContext::createSurface(VkInstance instance, VkSurfaceKHR& outSurface) const
{
    if (!window_) return false;
    if (glfwCreateWindowSurface(instance, window_, nullptr, &outSurface) != VK_SUCCESS) {
        std::fprintf(stderr, "[D71] glfwCreateWindowSurface failed.\n");
        return false;
    }
    return true;
}

} // namespace dash::vkexp
