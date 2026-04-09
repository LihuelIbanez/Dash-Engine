# Sprint 8 - Base de Renderizado Vulkan (Triangle/Cube)

## Meta del sprint

Inicializar el pipeline grafico Vulkan en macOS (MoltenVK) y renderizar un cubo estable en pantalla con loop seguro.

**Estado: PLANIFICADO**

---

## Tarea 8.1 (D70) - Instalacion de entorno (MoltenVK + Vulkan SDK)

### Objetivo
Configurar un toolchain Vulkan funcional en macOS con validacion activa.

### Resultado esperado
- CMake detecta Vulkan y valida dependencias de runtime.
- Se dispone de un ejecutable de diagnostico para instancia/extensiones/layers.

### Archivos a crear
1. src/rendering/vulkan/VulkanDiagnostics.cpp
2. src/rendering/vulkan/VulkanDiagnostics.h

### Archivos a modificar
1. CMakeLists.txt
2. cmake/ (deteccion de Vulkan SDK / MoltenVK)

### Criterio de aceptacion
- CMake detecta Vulkan en macOS.
- Ejecutable de diagnostico retorna OK.
- Validation layers habilitables por flag.

---

## Tarea 8.2 (D71) - Integracion GLFW para ventana + superficie Vulkan

### Objetivo
Crear y gestionar ventana con GLFW y enlazar VkSurfaceKHR.

### Resultado esperado
- Ventana Vulkan independiente de SDL para este track.
- Superficie creada/destruida de forma segura.

### Archivos a crear
1. src/rendering/platform/WindowContext.h
2. src/rendering/platform/WindowContext.cpp

### Archivos a modificar
1. CMakeLists.txt
2. src/main.cpp

### Criterio de aceptacion
- Ventana abre/cierra sin crash.
- VkSurfaceKHR se crea y destruye correctamente.
- Resize no rompe el loop.

---

## Tarea 8.3 (D72) - Instance, PhysicalDevice y LogicalDevice

### Objetivo
Seleccionar GPU y crear dispositivo logico con colas graficas/present.

### Resultado esperado
- Abstraccion DeviceContext desacoplada del main.
- Seleccion determinista de queue families y features.

### Archivos a crear
1. src/rendering/vulkan/DeviceContext.h
2. src/rendering/vulkan/DeviceContext.cpp

### Archivos a modificar
1. src/main.cpp
2. CMakeLists.txt

### Criterio de aceptacion
- DeviceContext inicializa en macOS.
- Se obtienen handles validos de colas.
- Logs de GPU/capabilities visibles.

---

## Tarea 8.4 (D73) - Swapchain y Render Pass

### Objetivo
Configurar swapchain, image views, render pass y sincronizacion base.

### Resultado esperado
- Ciclo acquire/submit/present robusto.
- Recreate de swapchain estable ante resize.

### Archivos a crear
1. src/rendering/vulkan/SwapchainContext.h
2. src/rendering/vulkan/SwapchainContext.cpp

### Archivos a modificar
1. src/rendering/vulkan/DeviceContext.cpp
2. src/main.cpp

### Criterio de aceptacion
- Render loop estable > 1000 frames.
- Validation layers sin errores criticos.

---

## Tarea 8.5 (D74) - Pipeline grafico + shaders GLSL/SPIR-V

### Objetivo
Compilar shaders y crear graphics pipeline minimo.

### Resultado esperado
- Toolchain de compilacion SPIR-V integrado a CMake.
- Pipeline basico listo para malla simple.

### Archivos a crear
1. assets/shaders/basic.vert
2. assets/shaders/basic.frag
3. src/rendering/vulkan/PipelineBuilder.h
4. src/rendering/vulkan/PipelineBuilder.cpp

### Archivos a modificar
1. CMakeLists.txt
2. src/rendering/vulkan/SwapchainContext.cpp

### Criterio de aceptacion
- Pipeline creation success en runtime.
- Vertex/fragment shader cargan correctamente.

---

## Tarea 8.6 (D75) - Dibujar primer cubo (vertex/index buffers)

### Objetivo
Renderizar cubo con buffers de vertices/indices y transformacion basica.

### Resultado esperado
- Cubo visible en viewport con profundidad correcta.
- Upload de geometria por staging buffers.

### Archivos a crear
1. src/rendering/mesh/Vertex.h
2. src/rendering/mesh/MeshBuffers.h
3. src/rendering/mesh/MeshBuffers.cpp

### Archivos a modificar
1. src/main.cpp
2. src/rendering/vulkan/PipelineBuilder.cpp

### Criterio de aceptacion
- Cubo visible en pantalla.
- Sin crash ni warnings criticos en validacion.

---

## Tarea 8.7 (D76) - QA de sprint

### Objetivo
Dejar baseline estable para el Sprint 9.

### Resultado esperado
- Smoke test de bootstrap/render/shutdown repetible.
- Checklist de salida del sprint documentada.

### Archivos a crear
1. tests/test_vulkan_smoke.cpp
2. planning/tasks/SPRINT8_QA_CHECKLIST.md

### Archivos a modificar
1. tests/CMakeLists.txt
2. README.md

### Criterio de aceptacion
- Smoke test verde.
- Checklist de sprint firmada.
