# Checklist Rapido macOS - Vulkan (MoltenVK + GLFW + glslc)

## 1) Instalar dependencias

```bash
brew install glfw shaderc molten-vk vulkan-loader vulkan-headers vulkan-tools vulkan-validationlayers
```

## 2) Verificar herramientas

```bash
command -v glslc
command -v vulkaninfo
pkg-config --modversion glfw3
```

Esperado:
- `glslc` y `vulkaninfo` disponibles en PATH.
- GLFW instalado y detectable.

## 3) Configurar variables de entorno Vulkan (MoltenVK)

```bash
export VK_ICD_FILENAMES=/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json
export VK_LAYER_PATH=/opt/homebrew/opt/vulkan-validationlayers/share/vulkan/explicit_layer.d
```

Opcional (persistente en zsh): agregar esas dos lineas en `~/.zshrc`.

## 4) Validar runtime Vulkan

```bash
vulkaninfo --summary | head -n 40
```

Esperado:
- Version de Vulkan visible.
- `VK_LAYER_KHRONOS_validation` presente.
- GPU listada.

## 5) Build del bootstrap D70-D74

```bash
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build --target VulkanBootstrap --parallel
```

Esperado:
- Se compila `vulkan_experimental`.
- Se generan shaders SPIR-V en `build/generated/shaders/`.
- Se crea el binario `build/VulkanBootstrap`.

## 6) Ejecutar evidencia D70-D74

```bash
./build/VulkanBootstrap
```

Logs esperados:
- `[D70] Vulkan API version available: ...`
- `[D72] Logical device created successfully.`
- `[D73] Swapchain + render pass created successfully.`
- `[D74] Graphics pipeline created successfully.`
- `[D70-D74] Vulkan bootstrap completed successfully.`

## Nota sobre validation warning en MoltenVK

Puede aparecer este warning de validacion en `vkCreateDevice` relacionado con `VK_KHR_portability_subset` y `VK_KHR_get_physical_device_properties2`, aun cuando la inicializacion continua correctamente en MoltenVK.
