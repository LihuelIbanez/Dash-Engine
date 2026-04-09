#include <cstdio>
#include <cstring>
#include <string>

#include "rendering/platform/WindowContext.h"
#include "rendering/vulkan/Renderer.h"
#include "rendering/vulkan/VulkanDiagnostics.h"

int main(int argc, char** argv)
{
    bool editorPreview = false;
    bool embeddedWindow = false;
    std::string statePath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--editor-preview") == 0) {
            editorPreview = true;
        } else if (std::strcmp(argv[i], "--embedded-window") == 0) {
            embeddedWindow = true;
        } else if (std::strcmp(argv[i], "--state") == 0 && i + 1 < argc) {
            statePath = argv[++i];
        }
    }

    std::printf("%s", dash::vkexp::VulkanDiagnostics::buildReport().c_str());

    dash::vkexp::WindowContext window;
    if (!window.init(1280, 720, "Dash Vulkan Bootstrap (D70-D84)")) {
        return 1;
    }

    dash::vkexp::Renderer renderer;
    if (!renderer.init(window)) {
        return 1;
    }

    if (!statePath.empty()) {
        renderer.setEditorStatePath(statePath);
    }
    renderer.setEmbeddedPreview(editorPreview && embeddedWindow);

    if (editorPreview) {
        std::puts("[D84] Editor preview mode enabled (persistent loop).");
        if (embeddedWindow) {
            std::puts("[D84] Embedded window docking mode enabled.");
        }
        if (!statePath.empty()) {
            std::printf("[D84] Viewport state path: %s\n", statePath.c_str());
        }
    } else {
        std::puts("[D77] Camera MVP UBO active (WASD move + RMB mouse look).");
    }

    const bool smokeOk = renderer.runSmoke(window, editorPreview ? 0u : 120u);
    renderer.shutdown();
    return smokeOk ? 0 : 1;
}
