# Sprint 15 — Componentes 3D y Refinamiento del Pipeline de Render

## Meta del sprint

Cerrar la desconexion entre el modelo de componentes del editor y el renderer Vulkan. Hoy `RenderComponent` y los campos 3D de `TransformComponent` son datos muertos: `SceneLoader` solo lee `x`, `y` y `type` del JSON y hardcodea color, escala y altura, por lo que toda entidad se dibuja como el mismo cubo. La infraestructura de importacion 3D (Assimp, `MeshBuffers::loadFromFile`, `AssetCache3D`) existe desde Sprint 11 pero ninguna entidad la usa.

**Estado: 🟡 EN PROGRESO — Fase 1 completada (15.1, 15.2, 15.3) + 15.4. Tests: 27/27 en verde.**

### Diagnostico de partida (auditoria 2026-08-27)

| Componente / campo | Estado actual en el render 3D |
|---|---|
| `RenderComponent::mesh` / `material` | Nunca llegan al renderer; todo dibuja `meshBuffers_` (cubo builtin) |
| `RenderComponent::visible` / `layer` | Ignorados |
| `RenderMode::BillboardSprite` | Definido en el enum, sin implementacion en Vulkan |
| `TransformComponent::z` | Ignorado (`ey` hardcodeado a 0.6 / 1.0 en `SceneLoader`) |
| `TransformComponent::yawDeg/pitchDeg/rollDeg` | Imposibles de renderizar: `basic.vert` solo aplica `inPos * scale + offset` |
| `AssetCache3D` | Instanciado en `Renderer`, jamas poblado (solo se llama `clear()`) |
| Colliders por entidad | Inexistentes: solo hay 1 `createDynamicBox` (cubo demo) + plano estatico |
| Draw calls | 1 `vkCmdDrawIndexed` + 1 `vkCmdPushConstants` por entidad, sin culling ni instancing |
| Tests del path 3D | Solo layout de vertex (`test_model_import_pipeline`); `SceneLoader` sin cobertura |

### Orden de ejecucion

Las tareas 15.1-15.3 son habilitadoras: sin `mat4 model` en el shader y sin que `RenderInstance` transporte los datos del componente, las fases de mallas y fisica no tienen donde apoyarse. La tarea 15.3 (unificar el parseo de escena) debe ir **antes** de agregar campos nuevos, para no duplicar su lectura en dos lugares.

---

## Fase 1 — Conectar componentes → renderer

## Tarea 15.1 — Extender `RenderInstance` con datos de componente

**Estado: ✅ Completada**

### Objetivo
Que la estructura que viaja del loader al renderer transporte la informacion real de `RenderComponent` y `TransformComponent`, en vez de un subconjunto hardcodeado.

### Alcance
`RenderInstance` pasa de `{position, scale, color, isPlayer}` a incluir rotacion, identidad de malla/material, visibilidad y capa de render.

### Resultado esperado
- `RenderInstance` con campos `rotation` (yaw/pitch/roll), `meshId`, `materialId`, `visible`, `layer`.
- `isPlayer` se conserva por compatibilidad con `PlayerController::syncToInstances()`.
- Sin cambio visual todavia (los campos se llenan pero aun no se consumen).

### Archivos modificados
1. `src/rendering/vulkan/RenderTypes.h` — Campos nuevos en `RenderInstance`
2. `src/rendering/vulkan/PlayerController.cpp` — Ajuste de `syncToInstances()` si aplica

### Criterio de aceptacion
- Compila sin warnings nuevos.
- `VulkanBootstrap` renderiza exactamente igual que antes (cambio no observable).

### Implementacion
`RenderInstance` ahora incluye `yawDeg/pitchDeg/rollDeg`, `meshId` (default `"cube"`), `materialId`, `renderMode`, `layer` y `visible`. Se agrego el enum `InstanceRenderMode` espejando `RenderMode` de `Components.h`. Los campos existentes quedaron primero para no romper la inicializacion agregada de los call sites.

---

## Tarea 15.2 — Matriz de modelo con rotacion en los shaders

**Estado: ✅ Completada**

### Objetivo
Permitir que las entidades roten. Hoy el vertex shader solo soporta traslacion y escala, por lo que `yawDeg/pitchDeg/rollDeg` no pueden expresarse.

### Alcance
Reemplazar los push constants `vec4 offset` + `vec4 scale` por una `mat4 model`, y transformar las normales con la inversa-transpuesta para que la iluminacion no se rompa con escala no uniforme.

### Resultado esperado
- Entidades con rotacion arbitraria en los 3 ejes.
- Iluminacion correcta bajo escala no uniforme (`vNormal` deja de ser `inNormal` crudo).
- Pipelines `basic` y `textured` actualizados de forma consistente.

### Archivos modificados
1. `assets/shaders/basic.vert` — `mat4 model` en push constants, normal matrix
2. `assets/shaders/textured.vert` — Mismo cambio
3. `src/rendering/vulkan/PipelineBuilder.cpp` — Tamano del push constant range
4. `src/rendering/vulkan/Renderer.cpp` — Construccion de la matriz en `recordDrawCommands()`
5. `src/rendering/vulkan/VkMath.h` — Helpers de composicion TRS si no existen

### Criterio de aceptacion
- Una entidad con `yawDeg = 45` se ve rotada en el preview Vulkan.
- Escala no uniforme (ej. `{0.2, 0.8, 0.2}`) no produce iluminacion incorrecta.
- El terreno y el agua (pipelines propios) no se ven afectados.

### Riesgo
El push constant de `basic.vert` hoy es un bloque de 16 floats con significado posicional (`offset/scale/color/lightDir`). Al pasar a `mat4` hay que revisar el limite de 128 bytes de push constants en MoltenVK: `mat4` (64) + color (16) + lightDir (16) = 96 bytes, dentro del limite.

### Implementacion
- `VkMath.h`: helper `trs(position, yaw, pitch, roll, scale)` que compone `M = T * Ry * Rx * Rz * S` en column-major (coincide con el layout que espera GLSL).
- `basic.vert` / `textured.vert`: `mat4 model` reemplaza `offset`+`scale`; la normal se transforma con `transpose(inverse(mat3(model)))`, calculado en shader para no gastar push constants extra.
- `basic.frag`: bloque de push constants sincronizado con el vertex shader.
- `PipelineBuilder::createBasicPipeline`: rango de push constants de 64 → 96 bytes (verificado dentro del limite de 128).
- `Renderer.cpp`: helper `buildInstancePushConstants()` que arma el bloque una sola vez; reemplaza los 3 bloques de 16 floats escritos a mano (cubo de fisica, tiles fallback, entidades de escena).
- El dibujo de entidades ahora respeta `instance.visible`.

> `createBasicPipeline` es compartido por los pipelines basic y textured, por lo que un unico cambio de rango cubre ambos. Los pipelines de terreno y agua tienen su propio rango de 16 floats y no fueron tocados.

---

## Tarea 15.3 — `SceneLoader` consume `SceneData` en vez de reparsear JSON

**Estado: ✅ Completada**

### Objetivo
Eliminar la segunda fuente de verdad del formato de escena. Hoy `SceneLoader` abre y parsea el archivo **tres veces** (`loadSpawnPoint`, `loadInstances`, `loadTerrainInstances`), cada una con su propio conocimiento del esquema, desalineada de `SceneData::kCurrentVersion`.

### Alcance
`SceneLoader` pasa a construir un `SceneData` una sola vez y a leer los componentes tipados desde ahi.

### Resultado esperado
- Un unico parseo del archivo de escena por carga.
- `SceneLoader` lee `TransformComponent` y `RenderComponent` reales, no campos JSON sueltos.
- Migraciones de version de escena aplican automaticamente al runtime 3D.

### Archivos modificados
1. `src/rendering/vulkan/SceneLoader.h` — Firma basada en `SceneData`
2. `src/rendering/vulkan/SceneLoader.cpp` — Reemplazo del parseo manual
3. `src/rendering/vulkan/Renderer.cpp` — Un solo `load` en vez de tres llamadas
4. `CMakeLists.txt` — `SceneData.cpp` disponible para el target `vulkan_experimental`

### Criterio de aceptacion
- Cargar una escena `sceneVersion < 5` en `VulkanBootstrap` aplica las mismas migraciones que el editor.
- El campo `z` y la escala del `TransformComponent` se respetan en el render.
- Play Mode embebido y Build & Run siguen funcionando.

### Nota de arquitectura
Es el mismo patron de doble-fuente-de-verdad que se corrigio en `SceneData` (BUG-1 de la auditoria de agosto). Conviene cerrarlo antes de agregar campos nuevos al formato.

### Implementacion
- **`SceneData` movido de `src/editor/` a `src/scene/`** y compilado dentro de `game_core`. Como todos los `#include "SceneData.h"` eran sin prefijo de ruta, el movimiento no requirio cambiar ninguna linea de codigo: solo directorios de include en CMake. Esto respeta la regla de arquitectura del proyecto (editor y runtime comparten datos por contratos serializables) sin invertir la direccion de dependencia editor→runtime.
- Se eliminaron las 9 referencias explicitas a `src/editor/SceneData.cpp` en `tests/CMakeLists.txt` (ahora vendria duplicado desde `game_core`).
- Nueva estructura `LoadedScene { SceneData data; tilemap; worldWidth; worldHeight; valid; }`. El `tilemap` no forma parte de `SceneData` porque lo inyecta el editor al exportar, asi que se conserva aparte pero se lee en el mismo parseo.
- `SceneLoader::load()` parsea **una sola vez**. Antes habia **cuatro** parseos independientes del mismo archivo: `loadSpawnPoint`, `loadInstances`, `loadTerrainInstances` y un `std::ifstream` suelto en `Renderer.cpp` solo para leer `worldSeed`.
- `loadInstances()` ahora lee `TransformComponent` (posicion, `z`, rotacion, escala) y `RenderComponent` (mesh, material, visible, layer, renderMode).
- `PlayerController::loadFromScene()` recibe el `LoadedScene` ya parseado.

#### Decision sobre la altura de entidades
`TransformComponent::z` se **suma** al offset base por tipo (1.0 player / 0.6 enemigo) en vez de reemplazarlo. Reemplazarlo hubiera hundido todas las entidades existentes a `y=0`, ya que `z` vale 0 en las escenas actuales. Asi el campo se respeta sin romper el contenido existente.

---

## Fase 2 — Mallas y materiales reales

## Tarea 15.4 — Resolucion de mallas via `AssetCache3D`

**Estado: ✅ Completada**

### Objetivo
Activar la infraestructura de importacion 3D de Sprint 11: que `RenderComponent::mesh` seleccione la malla efectivamente renderizada.

### Alcance
`"cube"` sigue usando la malla builtin; cualquier otro valor se resuelve como path/GUID y se carga con `MeshBuffers::loadFromFile`, cacheado por `AssetCache3D`.

### Resultado esperado
- Entidades con mallas distintas en la misma escena.
- Cache con hit/miss: una malla usada por N entidades se sube a GPU una sola vez.
- Fallback al cubo builtin si la malla no existe, con warning en log.

### Archivos modificados
1. `src/rendering/vulkan/Renderer.h` — Registro de mallas por instancia
2. `src/rendering/vulkan/Renderer.cpp` — Uso real de `assetCache_` (hoy solo `clear()`)
3. `src/assets/cache/AssetCache3D.cpp` — Ajustes de API si hacen falta
4. `src/rendering/vulkan/SceneLoader.cpp` — Propagacion de `meshId`

### Criterio de aceptacion
- Una escena con un `.gltf` importado renderiza ese modelo, no un cubo.
- Malla inexistente no crashea: cae al cubo y loguea el fallo.
- `AssetCache3D` reporta reuso al repetir la misma malla.

### Implementacion
- `Renderer::resolveMesh()` resuelve `RenderComponent::mesh`: `"cube"`/vacio → malla builtin; cualquier otro valor se busca en (1) ruta absoluta, (2) `VULKAN_MODEL_DIR`, (3) directorio de la escena, (4) ruta relativa al cwd. Si no se encuentra o falla la carga, cae al cubo con warning.
- `Renderer::resolveSceneMeshes()` ordena las instancias por `meshId` (`stable_sort`) para que el loop de dibujo haga bind una sola vez por malla distinta, y precomputa `sceneInstanceMeshes_` alineado por indice con `sceneInstances_`. Reordenar es seguro porque `PlayerController::syncToInstances()` busca por el flag `isPlayer`, no por posicion.
- Las fallas tambien se cachean, para no reintentar una referencia rota en cada resolucion.

#### Bug latente encontrado y corregido
`MeshBuffers::initCube()` genera indices **uint16** mientras que `initFromGLTF()` genera **uint32**, pero `MeshBuffers` no exponia cual tenia: el `Renderer` hardcodeaba `VK_INDEX_TYPE_UINT16` para el cubo. Al permitir mallas por entidad, bindear un GLTF como uint16 habria leido indices corruptos. Se agrego `MeshBuffers::indexType()` (seteado en `initCube` → UINT16, `initFromData` → parametro con default UINT32) y se propago por el move constructor y el move assignment, que tampoco lo habrian copiado.

### Verificacion realizada
Escena de prueba con malla GLTF + rotacion, entidad invisible y malla inexistente:
```
[AssetCache3D] Mesh not found: 'no_existe.gltf' (using builtin cube)
[MeshBuffers] Loaded GLTF: 3005 vertices, 8628 indices from .../Wolf-Blender-2.82a.gltf
[D76] Smoke test: 120 frames rendered successfully.
```
Con 4 entidades apuntando a la misma malla: **1 sola carga desde disco, 1 sola entrada en cache**. Los 120 frames renderizados confirman que el binding mixto uint16/uint32 es correcto.

---

## Tarea 15.5 — `MaterialAsset` minimo

### Objetivo
Dar consumidor al campo `RenderComponent::material`, que hoy es un string sin efecto.

### Alcance
Definir un material serializable con textura albedo, color base y parametros especulares, integrado al asset pipeline existente.

### Resultado esperado
- `MaterialAsset` con GUID, cargable por el `ImportManager`.
- Renderer aplica albedo + color + specular por instancia.
- Material por defecto cuando el campo esta vacio.

### Archivos creados
1. `src/assets/MaterialAsset.h` / `.cpp` — Estructura y serializacion
2. `src/assets/importers/MaterialImporter.h` / `.cpp` — Importer

### Archivos modificados
1. `src/assets/ImportManager.cpp` — Registro del importer
2. `src/assets/AssetTypes.h` — Nuevo `AssetType::Material`
3. `src/rendering/vulkan/Renderer.cpp` — Bind de textura por material

### Criterio de aceptacion
- Dos entidades con la misma malla y materiales distintos se ven diferentes.
- Asset Browser lista los materiales con su GUID.

---

## Tarea 15.6 — Implementar `RenderMode::BillboardSprite`

### Objetivo
Dar implementacion Vulkan al modo billboard, hoy presente en el enum y editable desde el inspector pero sin efecto.

### Alcance
Pipeline de quad orientado a camara que usa `RenderComponent::sprite`.

### Resultado esperado
- Entidades en modo billboard renderizan un quad siempre encarado a la camara.
- Alpha blending correcto contra el terreno.

### Archivos creados
1. `assets/shaders/billboard.vert` / `.frag`

### Archivos modificados
1. `src/rendering/vulkan/PipelineBuilder.cpp` — Pipeline billboard
2. `src/rendering/vulkan/Renderer.cpp` — Rama de dibujo por `renderMode`
3. `CMakeLists.txt` — Compilacion de los shaders nuevos

### Criterio de aceptacion
- Cambiar `renderMode` en el inspector alterna entre malla 3D y billboard sin reiniciar.
- El billboard mantiene su orientacion al rotar la camara.

---

## Fase 3 — Fisica por entidad

## Tarea 15.7 — `PhysicsComponent`

### Objetivo
Permitir que las entidades de escena tengan cuerpo fisico. Hoy `PhysicsWorld` solo administra el cubo de demo y un plano estatico.

### Alcance
Nuevo componente serializable, editable por el inspector generico y con soporte de undo/redo (via la reflexion ya existente).

### Resultado esperado
- `PhysicsComponent` con tipo de collider, half-extents, masa e `isStatic`.
- Registrado en `ComponentType` (ID nuevo, sin reordenar los existentes).
- Visible y editable en el Inspector.

### Archivos modificados
1. `src/core/components/Components.h` — Struct + entrada en `ComponentType` y `ComponentVariant`
2. `src/core/components/ComponentSerialization.cpp` — to/from JSON
3. `src/core/components/Reflection.cpp` — `ComponentMeta` para el inspector
4. `src/editor/SceneData.cpp` — Bump de `kCurrentVersion` + migracion

### Criterio de aceptacion
- Agregar/quitar el componente desde el inspector con undo/redo funcional.
- Escenas viejas cargan sin el componente y sin errores.
- `test_component_serialization` cubre el round-trip del componente nuevo.

---

## Tarea 15.8 — Cuerpos fisicos por entidad y eventos de colision

### Objetivo
Instanciar un cuerpo en `PhysicsWorld` por cada entidad con `PhysicsComponent` y conectar las colisiones al `EventDispatcher` existente.

### Alcance
Reemplazar el registro hardcodeado del cubo demo por un registro derivado de la escena.

### Resultado esperado
- N cuerpos dinamicos/estaticos segun la escena.
- Colisiones emitidas como eventos tipados consumibles por el gameplay.
- Determinismo preservado (fixed timestep ya existente).

### Archivos modificados
1. `src/rendering/vulkan/Renderer.cpp` — Registro de cuerpos desde `sceneInstances_`
2. `src/game/physics/PhysicsWorld.cpp` — Ajustes si se requieren
3. `src/core/events/GameEvents.h` — Evento de colision

### Criterio de aceptacion
- Dos entidades con colliders colisionan y emiten evento.
- `test_physics_determinism` sigue pasando.
- Escena sin `PhysicsComponent` se comporta como hoy.

---

## Fase 4 — Rendimiento

## Tarea 15.9 — Batching por malla e instancing

### Objetivo
Eliminar el cuello de botella de 1 draw call + 1 push constant por entidad, que no escala mas alla de ~100 entidades.

### Alcance
Agrupar instancias por malla y emitir draws instanciados con los transforms en un SSBO.

### Resultado esperado
- Draw calls proporcionales a la cantidad de mallas unicas, no de entidades.
- Transforms en storage buffer indexado por `gl_InstanceIndex`.

### Archivos modificados
1. `src/rendering/vulkan/Renderer.cpp` — Agrupacion y `vkCmdDrawIndexed` instanciado
2. `src/rendering/vulkan/Renderer.h` — Buffers de instancia
3. `assets/shaders/basic.vert` — Lectura del SSBO por `gl_InstanceIndex`

### Criterio de aceptacion
- Escena con 500 entidades de la misma malla: 1 draw call.
- Mejora medible de frame time en el panel Performance.

---

## Tarea 15.10 — Frustum culling y ordenamiento por capa

### Objetivo
No emitir trabajo por geometria fuera de camara y respetar `RenderComponent::layer` para el blending correcto.

### Alcance
Culling por AABB contra el frustum antes de construir los batches; orden estable por capa con translucidos al final.

### Resultado esperado
- Entidades fuera de camara no generan draws.
- Translucidos (agua, billboards) se dibujan despues de los opacos.

### Archivos modificados
1. `src/rendering/vulkan/CameraController.cpp` — Extraccion de planos del frustum
2. `src/rendering/vulkan/Renderer.cpp` — Culling + sort por `layer`

### Criterio de aceptacion
- Contador de entidades visibles baja al alejar la camara del cluster de entidades.
- Sin z-fighting ni parpadeo en translucidos.

---

## Fase 5 — Cobertura de tests

## Tarea 15.11 — Tests headless del pipeline 3D

**Estado: 🟡 Parcial — `test_scene_loader_3d` implementado junto con la Fase 1**

### Objetivo
Cubrir la logica de escena→render, hoy sin tests. `test_model_import_pipeline` solo valida offsets y `sizeof` del vertex.

### Alcance
Tests que no requieren contexto Vulkan: transformacion de datos, resolucion de mallas y culling.

### Resultado esperado
- Test de `SceneLoader`: una escena produce los `RenderInstance` esperados (posicion con `z`, rotacion, `visible`, `meshId`).
- Test de resolucion de malla: `"cube"` → builtin; path invalido → fallback + warning.
- Test de frustum culling con AABBs conocidos.

### Archivos creados
1. `tests/test_scene_loader_3d.cpp`
2. `tests/test_render_instance_build.cpp`

### Archivos modificados
1. `tests/CMakeLists.txt` — Registro de los tests nuevos

### Criterio de aceptacion
- `ctest` sigue en verde con los tests nuevos incluidos.
- Los tests corren sin dispositivo Vulkan (aptos para CI).

### Implementacion parcial
`tests/test_scene_loader_3d.cpp` (33 aserciones) cubre el mapeo escena→render de la Fase 1: `TransformComponent` como fuente de posicion/rotacion/escala, `RenderComponent` como fuente de mesh/material/visible/layer/renderMode, fallback de entidades legacy sin componentes, posicion del player y degradado ante archivo inexistente. Corre headless porque `SceneLoader` no arrastra dependencias de Vulkan.

Pendiente: tests de resolucion de malla (15.4) y de frustum culling (15.10), que dependen de esas tareas.

---

## Resumen de archivos

| Archivo | Cambios |
|---------|---------|
| `src/rendering/vulkan/RenderTypes.h` | `RenderInstance` extendido (rotacion, mesh, material, visible, layer) |
| `src/rendering/vulkan/SceneLoader.h/.cpp` | Consume `SceneData`; parseo unico |
| `src/rendering/vulkan/Renderer.h/.cpp` | Matriz modelo, `AssetCache3D` real, cuerpos fisicos, batching, culling |
| `src/rendering/vulkan/PipelineBuilder.cpp` | Push constants nuevos, pipeline billboard |
| `src/rendering/vulkan/CameraController.cpp` | Planos del frustum |
| `src/rendering/vulkan/VkMath.h` | Helpers TRS / normal matrix |
| `assets/shaders/basic.vert` | `mat4 model`, normal matrix, SSBO de instancias |
| `assets/shaders/textured.vert` | `mat4 model`, normal matrix |
| `assets/shaders/billboard.vert/.frag` | Nuevos |
| `src/core/components/Components.h` | `PhysicsComponent` |
| `src/core/components/ComponentSerialization.cpp` | Serializacion del componente nuevo |
| `src/core/components/Reflection.cpp` | Metadata para el inspector |
| `src/core/events/GameEvents.h` | Evento de colision |
| `src/assets/MaterialAsset.h/.cpp` | Nuevo |
| `src/assets/importers/MaterialImporter.h/.cpp` | Nuevo |
| `src/assets/ImportManager.cpp` | Registro del importer |
| `src/assets/AssetTypes.h` | `AssetType::Material` |
| `src/editor/SceneData.cpp` | Bump de version + migracion |
| `tests/test_scene_loader_3d.cpp` | Nuevo |
| `tests/test_render_instance_build.cpp` | Nuevo |
| `tests/CMakeLists.txt` | Registro de tests |
| `CMakeLists.txt` | Shaders nuevos, `SceneData` en `vulkan_experimental` |

## Verificacion

1. `cmake --build build --parallel` — compila sin errores ni warnings nuevos
2. `ctest --test-dir build` — 100% en verde, incluidos los tests nuevos
3. Editor: inspector permite editar mesh/material/rotacion y el preview lo refleja
4. `VulkanBootstrap`: escena con mallas importadas, rotaciones y billboards
5. Fisica: entidades con collider colisionan; `test_physics_determinism` sin regresion
6. Performance: escena de 500 entidades con draw calls agrupados y frame time estable
7. Escenas de version anterior cargan sin errores (migracion automatica)
