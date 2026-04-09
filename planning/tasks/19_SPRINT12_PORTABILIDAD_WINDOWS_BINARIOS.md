# Sprint 12 - Portabilidad y Binarios de Windows

## Meta del sprint

Conseguir build reproducible en Windows sin romper macOS, con ejecutable empaquetado y flujo CI basico.

**Estado: PLANIFICADO**

---

## Tarea 12.1 (D106) - CMake cross-platform

### Objetivo
Ajustar CMake para detectar dependencias en macOS y Windows.

### Resultado esperado
- Presets por plataforma y resolucion estable de dependencias.
- Sin hardcodes de rutas locales.

### Archivos a crear
1. CMakePresets.json

### Archivos a modificar
1. CMakeLists.txt
2. cmake/Packaging.cmake

### Criterio de aceptacion
- Configure + build release exitoso en macOS y Windows.

---

## Tarea 12.2 (D107) - Ruta Vulkan por plataforma

### Objetivo
Usar MoltenVK en macOS y Vulkan SDK nativo en Windows.

### Resultado esperado
- Bootstrap de Vulkan desacoplado por plataforma.
- Catalogo de extensiones requeridas por SO.

### Archivos a crear
1. src/rendering/vulkan/VulkanPlatformConfig.h
2. src/rendering/vulkan/VulkanPlatformConfig.cpp

### Archivos a modificar
1. src/rendering/vulkan/DeviceContext.cpp
2. CMakeLists.txt

### Criterio de aceptacion
- Runtime inicia renderer en ambos SO con mismo codigo fuente.

---

## Tarea 12.3 (D108) - Abstraccion de rutas y recursos

### Objetivo
Normalizar acceso a archivos (assets, db, shaders) de forma relativa.

### Resultado esperado
- Resolver root de ejecucion de forma consistente.
- Carga de recursos estable en build local y paquete.

### Archivos a crear
1. src/core/io/AppPaths.h
2. src/core/io/AppPaths.cpp

### Archivos a modificar
1. src/assets/
2. src/rendering/

### Criterio de aceptacion
- Ejecutable encuentra recursos sin hardcodes absolutos.

---

## Tarea 12.4 (D109) - Compilacion MSVC y resolucion de discrepancias

### Objetivo
Resolver incompatibilidades Clang vs MSVC.

### Resultado esperado
- Build Release en MSVC sin errores.
- Warnings relevantes corregidos de raiz.

### Archivos a crear
1. planning/tasks/SPRINT12_MSVC_FIXES.md

### Archivos a modificar
1. CMakeLists.txt
2. src/ (correcciones de compatibilidad)

### Criterio de aceptacion
- Build Release MSVC limpio y ejecutable funcional.

---

## Tarea 12.5 (D110) - CI/CD basico y empaquetado final

### Objetivo
Automatizar generacion de binario Windows y smoke tests.

### Resultado esperado
- Workflow CI dual (macOS/Windows) con build + smoke.
- Artefacto .exe empaquetado con recursos.

### Archivos a crear
1. .github/workflows/build-cross-platform.yml
2. packaging/windows/install_app.bat

### Archivos a modificar
1. README.md
2. packaging/

### Criterio de aceptacion
- Se genera .exe distribuible con recursos.
- Smoke test de arranque pasa en runner Windows.
