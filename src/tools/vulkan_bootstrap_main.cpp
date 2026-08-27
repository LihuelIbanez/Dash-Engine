#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "rendering/platform/WindowContext.h"
#include "rendering/vulkan/Renderer.h"
#include "rendering/vulkan/VulkanDiagnostics.h"

int main(int argc, char** argv)
{
    bool editorPreview = false;
    bool embeddedWindow = false;
    bool persistentRun = false;
    unsigned int smokeFrames = 120u;
    std::string scenePath;
    std::string statePath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--editor-preview") == 0) {
            editorPreview = true;
        } else if (std::strcmp(argv[i], "--embedded-window") == 0) {
            embeddedWindow = true;
        } else if (std::strcmp(argv[i], "--persistent") == 0) {
            persistentRun = true;
        } else if (std::strcmp(argv[i], "--scene") == 0 && i + 1 < argc) {
            scenePath = argv[++i];
        } else if (std::strcmp(argv[i], "--state") == 0 && i + 1 < argc) {
            statePath = argv[++i];
        } else if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            smokeFrames = static_cast<unsigned int>(std::strtoul(argv[++i], nullptr, 10));
        }
    }

    std::fprintf(stderr,
                 "[VSTEP] bootstrap args parsed: editorPreview=%d embeddedWindow=%d persistent=%d scene=%s state=%s\n",
                 editorPreview ? 1 : 0,
                 embeddedWindow ? 1 : 0,
                 persistentRun ? 1 : 0,
                 scenePath.empty() ? "<none>" : scenePath.c_str(),
                 statePath.empty() ? "<none>" : statePath.c_str());

    std::printf("%s", dash::vkexp::VulkanDiagnostics::buildReport().c_str());

    dash::vkexp::WindowContext window;
    std::fprintf(stderr, "[VSTEP] creating GLFW window context...\n");
    if (!window.init(1280, 720, "Dash Vulkan Bootstrap (D70-D84)", editorPreview && embeddedWindow)) {
        std::fprintf(stderr, "[VFAIL] window.init failed\n");
        return 1;
    }
    std::fprintf(stderr, "[VOK] window.init success\n");

    dash::vkexp::Renderer renderer;
    if (!scenePath.empty()) {
        renderer.setScenePath(scenePath);
    }
    std::fprintf(stderr, "[VSTEP] renderer.init begin\n");
    if (!renderer.init(window)) {
        std::fprintf(stderr, "[VFAIL] renderer.init failed\n");
        return 1;
    }
    std::fprintf(stderr, "[VOK] renderer.init success\n");

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
        if (!scenePath.empty()) {
            std::printf("[D84] Scene path: %s\n", scenePath.c_str());
        }
    } else {
        std::puts("[D77] Camera MVP UBO active (WASD move + RMB mouse look).");
        if (!scenePath.empty()) {
            std::printf("[D84] Standalone scene path: %s\n", scenePath.c_str());
        }
        if (persistentRun) {
            std::puts("[D84] Standalone persistent run enabled.");
        }
    }

    const bool smokeOk = renderer.runSmoke(window, (editorPreview || persistentRun) ? 0u : smokeFrames);
    renderer.shutdown();
    return smokeOk ? 0 : 1;
}
