# Sprint 12 — Vulkan Platform Notes

## Platform Abstraction Strategy

The Vulkan renderer is already cross-platform by design. No `VulkanPlatformConfig` abstraction layer was needed because GLFW handles all platform-specific surface creation and extension enumeration.

## How It Works

### Instance Extensions (Renderer.cpp)
- `glfwGetRequiredInstanceExtensions()` returns platform-specific extensions automatically:
  - **macOS**: `VK_KHR_surface` + `VK_MVK_macos_surface` (MoltenVK)
  - **Windows**: `VK_KHR_surface` + `VK_KHR_win32_surface`
  - **Linux**: `VK_KHR_surface` + `VK_KHR_xcb_surface` or Wayland variant
- `VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME` is detected dynamically and enabled only when present (macOS via MoltenVK)
- `VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR` flag set conditionally

### Device Extensions (DeviceContext.cpp)
- `VK_KHR_portability_subset` detected and enabled on macOS (MoltenVK requirement)
- On Windows/Linux this extension won't be present and is skipped

### Surface Creation (WindowContext.cpp)
- `glfwCreateWindowSurface()` handles platform-specific `VkSurfaceKHR` creation
- No platform-specific code needed in our codebase

### Swapchain Format (SwapchainContext.cpp)
- Prefers `VK_FORMAT_B8G8R8A8_UNORM` + `VK_COLOR_SPACE_SRGB_NONLINEAR_KHR`
- Falls back to `formats[0]` if preferred format unavailable
- Both macOS (MoltenVK) and Windows (NVIDIA/AMD) typically support B8G8R8A8_UNORM

## Windows Testing Checklist

When testing on Windows for the first time:

1. **Vulkan SDK**: Install LunarG Vulkan SDK (provides loader, validation layers, headers)
2. **GPU Driver**: Ensure GPU driver supports Vulkan 1.1+
3. **Validation Layers**: Verify `VK_LAYER_KHRONOS_validation` is available
4. **Surface Format**: Check logs for selected surface format (should be B8G8R8A8_UNORM)
5. **Extension List**: Compare instance/device extensions between macOS and Windows runs
6. **Depth Format**: `VK_FORMAT_D32_SFLOAT` should be universally supported
7. **Shader SPIR-V**: Same compiled shaders work on both platforms (SPIR-V is platform-independent)

## Build Requirements by Platform

| Component | macOS | Windows |
|-----------|-------|---------|
| Vulkan Loader | MoltenVK (via Homebrew) | LunarG Vulkan SDK |
| Window System | GLFW (Homebrew) | GLFW (vcpkg) |
| Shader Compiler | glslc (Vulkan SDK) | glslc (Vulkan SDK) |
| Surface Extension | VK_MVK_macos_surface | VK_KHR_win32_surface |
| Portability | VK_KHR_portability_subset | Not needed |
