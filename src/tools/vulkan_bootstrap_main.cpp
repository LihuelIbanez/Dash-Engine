#include <cstdio>

#include "rendering/platform/WindowContext.h"
#include "rendering/vulkan/Renderer.h"
#include "rendering/vulkan/VulkanDiagnostics.h"

int main()
{
    std::printf("%s", dash::vkexp::VulkanDiagnostics::buildReport().c_str());

    dash::vkexp::WindowContext window;
    if (!window.init(1280, 720, "Dash Vulkan Bootstrap (D70-D84)")) {
        return 1;
    }

    dash::vkexp::Renderer renderer;
    if (!renderer.init(window)) {
        return 1;
    }

    std::puts("[D77] Camera MVP UBO active (WASD move + RMB mouse look).");
    const bool smokeOk = renderer.runSmoke(window, 120);
    renderer.shutdown();
    return smokeOk ? 0 : 1;
}
