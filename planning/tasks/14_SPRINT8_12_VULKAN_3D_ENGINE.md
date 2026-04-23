# Sprint 8-12 - Motor Grafico 3D (Vulkan, fisicas, audio, assets, portabilidad)

## Objetivo general

Construir una base de motor 3D en C++ con foco en macOS (MoltenVK) y compatibilidad futura con Windows, manteniendo CMake como fuente unica de verdad para build y dependencias.

## Stack recomendado

- Render: Vulkan + MoltenVK (macOS) + Vulkan SDK (LunarG)
- Ventana/Input: GLFW
- Matematicas: GLM
- Fisicas: Jolt (preferido por rendimiento) o Bullet (fallback)
- Modelos 3D: Assimp
- Audio: miniaudio (preferido) o SoLoud
- Build: CMake

## Criterios de exito global

1. En macOS renderiza escena 3D con camara libre y objetos fisicos.
2. Audio responde a eventos de colision y gameplay.
3. Se cargan modelos .gltf/.obj con texturas.
4. El proyecto compila en Windows con ruta de build definida.
5. Existe pipeline de QA con benchmarks minimos y checklist de release.

---

## Sprint 8 - Base Vulkan (D70-D76) — ✅ COMPLETADO

Meta: inicializar pipeline Vulkan funcional y dibujar un cubo con camara basica.

### D70 - Setup Vulkan en macOS
- Descripcion: Instalar/configurar MoltenVK y Vulkan SDK.
- Estrategia: Verificacion por ejecutable de diagnostico (instance/extensions/layers).
- Criterios de aceptacion: instancia Vulkan creada y validacion activable.
### D71 - Integracion GLFW + superficie Vulkan
- Descripcion: Crear ventana y ciclo de eventos independiente del editor actual.
- Estrategia: Integrar GLFWwindow -> VkSurfaceKHR.
- Criterios de aceptacion: resize y close estables sin crashes.
### D72 - Instance/PhysicalDevice/LogicalDevice
- Descripcion: Seleccion de GPU + colas graficas/present.
- Estrategia: Capa de abstraccion DeviceContext.
- Criterios de aceptacion: logs de dispositivo y features seleccionadas.
### D73 - Swapchain + sincronizacion de frame
- Descripcion: Crear swapchain, image views, semaforos y fences.
- Estrategia: Manejar recreacion por resize minimizando stutter.
- Criterios de aceptacion: 1200 frames continuos sin errores de sincronizacion.
### D74 - Pipeline grafico basico
- Descripcion: GLSL vertex/fragment y compilacion SPIR-V automatizada.
- Estrategia: DescriptorSetLayout inicial para MVP uniform.
- Criterios de aceptacion: pipeline creado y bind exitoso.
### D75 - Buffers de vertices/indices
- Descripcion: Upload de malla cubo con staging buffers.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: cubo visible y estable en pantalla.
### D76 - Tests/QA Sprint 8
- Descripcion: Smoke test de init y frame loop.
- Estrategia: Validacion de leaks basica y reporte de errores Vulkan.
- Criterios de aceptacion: suite smoke verde en macOS.
---

## Sprint 9 - Dinamica y mundo fisico (D80-D84) — ✅ COMPLETADO

Meta: vincular fisicas con render para interaccion real.
Nota: Se uso backend builtin propio en lugar de Jolt/Bullet.

### D80 - Integracion de libreria fisica
- Descripcion: Integrar Jolt (o Bullet) con modulo PhysicsWorld.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: world step estable con dt fijo.
### D81 - ECS/Scene bridge para rigid bodies
- Descripcion: Vincular entidad visual con rigid body.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: sincronizacion posicion/rotacion visual-fisica.
### D82 - Colision cubo-plano estatico
- Descripcion: Crear plano suelo y cubo dinamico.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: caida por gravedad con rebote/descanso correcto.
### D82 - Hitboxes y debug draw
- Descripcion: Colliders AABB/box para objetos de prueba.
- Estrategia: Overlay debug de colliders.
- Criterios de aceptacion: toggles debug desde menu de herramientas.
### D83 - Gravedad y colision con plano
- Descripcion: Crear plano suelo y cubo dinamico.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: caida por gravedad con rebote/descanso correcto.
### D84 - Tests Sprint 9
- Descripcion: Test de gravedad/colision determinista.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: tolerancias numericas definidas y estables.
---

## Sprint 10 - Audio espacial e interfaz (D89-D93) — ✅ COMPLETADO

Meta: feedback sonoro y configuracion persistente.

### D89 - Integracion backend audio
- Descripcion: Implementar AudioEngine (miniaudio recomendado).
- Estrategia: Canales: master, music, sfx.
- Criterios de aceptacion: reproduccion de WAV/OGG one-shot y loop.
### D90 - Triggers de sonido por eventos
- Descripcion: Conectar eventos de colision/ataque -> playSfx.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: efecto de ataque y colision funcionales.
### D91 - Persistencia de configuracion
- Descripcion: Guardar volumenes en SQLite de proyecto.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: valores se conservan entre sesiones.
### D92 - Input mapping avanzado
- Descripcion: Perfil de controles (WASD, mouse sensitivity, invert Y).
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: remapeo basico persistente.
### D93 - Tests Sprint 10
- Descripcion: Tests de serializacion config + smoke de AudioEngine.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: sin leaks ni crash al cerrar.
---

## Sprint 11 - Importacion de activos 3D (D97-D101) ✅ COMPLETADO

Meta: cargar modelos complejos y texturas.

### D97 - Integracion Assimp
- Descripcion: Modulo ModelImporter para .obj/.gltf.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: parse de nodos y meshes sin crash.
### D98 - Pipeline de mallas GPU
- Descripcion: Convertir datos Assimp -> vertex/index buffers Vulkan.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: render de malla estatica cargada de archivo.
### D99 - Materiales basicos
- Descripcion: Albedo + UV + sampler por mesh.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: texturas visibles correctamente.
### D99 - stb_image para texturas
- Descripcion: Carga de PNG/JPG y upload a VkImage.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: mip 0 valido y layout transitions correctas.
### D100 - Asset cache
- Descripcion: Cache de modelos/texturas por path y hash.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: segunda carga reutiliza recursos.
### D101 - Tests Sprint 11
- Descripcion: Tests de import y consistencia de datos de malla.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: casos edge (.gltf incompleto) manejados.
---

## Sprint 12 - Portabilidad Windows + release (D106-D110) — 🔧 PARCIAL (80%)

Meta: build cruzado robusto y paquete ejecutable final.
Nota: D106-D109 completados (CMake MSVC, build scripts, AppPaths). Falta D110 (CI/CD GitHub Actions).

### D106 - CMake cross-platform cleanup
- Descripcion: Flags y deteccion por plataforma (Apple/Windows/Linux).
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: presets separados por plataforma.
### D107 - Vulkan path por SO
- Descripcion: macOS usa MoltenVK; Windows usa Vulkan SDK nativo.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: mismo codigo fuente, backend correcto por toolchain.
### D108 - Dependencias multiplataforma
- Descripcion: Buscar GLFW/GLM/Assimp/Jolt/miniaudio en ambos entornos.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: configure exitoso en macOS y Windows.
### D109 - Rutas relativas y recursos
- Descripcion: Uniformar path handling (assets, db, shaders).
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: app corre desde directorio empaquetado.
### D109 - Compilacion MSVC
- Descripcion: Resolver warnings/errors Clang vs MSVC.
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: build Release limpio en Windows.
### D110 - CI de build dual
- Descripcion: Job macOS + Windows (build + smoke tests).
- Estrategia: Implementacion incremental con validacion tecnica y pruebas de smoke.
- Criterios de aceptacion: pipeline verde en ambas plataformas.
---

## Riesgos y mitigaciones

1. Complejidad Vulkan inicial alta.
- Mitigacion: arquitectura incremental por capas y smoke tests por sprint.

2. Divergencias macOS vs Windows en toolchains.
- Mitigacion: CMake presets y CI dual temprano (Sprint 12 inicial).

3. Sobrecarga de integracion de terceros.
- Mitigacion: wrappers internos para aislar librerias (audio/fisicas/import).

4. Regresiones de rendimiento en escenas complejas.
- Mitigacion: benchmark continuo y criterios de no-regresion por sprint.

## Entregables finales

1. Motor 3D base Vulkan funcional en macOS.
2. Soporte de fisicas, audio y modelos importados.
3. Build Windows funcional con pipeline reproducible.
4. Documentacion operativa completa y checklist de release.
