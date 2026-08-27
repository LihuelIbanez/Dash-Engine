# Sprint 15 — Componentes 3D y Refinamiento del Pipeline de Render

## Meta del sprint

Cerrar la desconexion entre el modelo de componentes del editor y el renderer Vulkan. Hoy `RenderComponent` y los campos 3D de `TransformComponent` son datos muertos: `SceneLoader` solo lee `x`, `y` y `type` del JSON y hardcodea color, escala y altura, por lo que toda entidad se dibuja como el mismo cubo. La infraestructura de importacion 3D (Assimp, `MeshBuffers::loadFromFile`, `AssetCache3D`) existe desde Sprint 11 pero ninguna entidad la usa.

**Estado: 🟡 EN PROGRESO — Fases 1, 2, 3 completadas y Fase 4 parcial (15.10). Tests: 29/29 en verde. Pendiente: 15.9 (instancing, postergada con justificacion).**

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

**Estado: ✅ Completada**

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

### Archivos previstos pero no creados
1. ~~`src/assets/importers/MaterialImporter.h` / `.cpp`~~ — ver "Decisiones de alcance"

### Archivos modificados
1. `src/assets/ImportManager.cpp` — Registro del importer
2. `src/assets/AssetTypes.h` — Nuevo `AssetType::Material`
3. `src/rendering/vulkan/Renderer.cpp` — Bind de textura por material

### Criterio de aceptacion
- Dos entidades con la misma malla y materiales distintos se ven diferentes.
- Asset Browser lista los materiales con su GUID.

### Implementacion

#### Prerequisito no previsto en el spec: binding de texturas por draw
El diseno original tenia **un solo descriptor set por imagen de swapchain**, con `binding 1` apuntando a una unica `defaultTexture_` global (`maxSets = swapchain_.imageCount()`). Sin resolver eso, materiales distintos no pueden verse diferentes. Se evaluaron dos caminos:

1. Set 1 dedicado a material — mas limpio a largo plazo, pero cambia el pipeline layout compartido por los 4 pipelines (basic, textured, terrain, water) y obliga a tocar todos los shaders.
2. **N sets con el layout actual** (uno por material x imagen), bindeando el correcto por draw.

Se eligio la **opcion 2**: cero cambios de shaders y de pipeline layout, mucho menor riesgo, y suficiente para 15.5 y 15.6.

#### Cambios
- `src/assets/MaterialAsset.h/.cpp`: asset serializable con `guid`, `name`, `albedoTexture` y `baseColor`.
- `Renderer::resolveSceneMaterials()`: recolecta los material ids distintos, carga cada definicion y su textura albedo, crea un **pool de descriptors separado** (`materialDescriptorPool_`) dimensionado como `materiales x imagenes` y escribe un set por combinacion. El pool es aparte del original porque los materiales solo se conocen despues de cargar la escena, mientras que `createDescriptors()` corre antes.
- El loop de dibujo bindea el descriptor set solo cuando cambia el material, y multiplica `baseColor` sobre el color de instancia.
- El orden de instancias es por `meshId` y luego `materialId`, para minimizar rebinds de ambos.
- `destroySceneMaterials()` libera texturas propias y el pool; se invoca desde `shutdown()`.

#### Decisiones de alcance
- `"default"` (valor por defecto de `RenderComponent::material`) se trata igual que vacio: usa el descriptor set global. Sin esto, cada entidad sin material explicito generaba un warning espurio y descriptor sets desperdiciados (se veia como `Resolved 5 material(s)` en vez de 4).
- **No** se agregaron campos `specularStrength`/`shininess` al asset aunque el spec los mencionaba: los shaders actuales no los consumen, y agregarlos habria creado exactamente el tipo de dato muerto que esta auditoria vino a eliminar. Se agregaran cuando el modelo de iluminacion los use.
- El importer (`MaterialImporter`) y el listado en Asset Browser quedan pendientes; el renderer resuelve los materiales por path directo. El criterio "Asset Browser lista los materiales" no esta cumplido todavia.

### Verificacion realizada
Escena con 4 materiales: textura real, solo color base, textura rota y definicion inexistente.
```
[Material] Definition not found: 'no_existe.mat.json' (using defaults)
[Material] '/tmp/mattest/broken.mat.json': albedo texture 'no_existe.png' unavailable (using white)
[Material] Resolved 4 material(s).
[TextureLoader] Loaded texture 4096x2048 from .../Material__wolf_col_tga_diffuse_jpeg.jpg
[D76] Smoke test: 120 frames rendered successfully.
```
Test `test_material_asset` (round-trip, defaults, archivo faltante, JSON malformado).

---

## Tarea 15.6 — Implementar `RenderMode::BillboardSprite`

**Estado: ✅ Completada**

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

### Implementacion
- `assets/shaders/billboard.vert/.frag`: el quad se genera **proceduralmente desde `gl_VertexIndex`** (6 vertices, sin vertex buffer ni index buffer). El fragment descarta pixeles con alpha < 0.05.
- `PipelineBuilder::createBillboardPipeline()`: `vertexBindingDescriptionCount = 0`, alpha blending, depth test ON y depth write OFF (para que los sprites transparentes no se ocluyan entre si).
- `CameraController::forwardVector()/rightVector()/upVector()`: ejes de camara, con la misma matematica que `computeViewProjection()`.
- Los billboards se saltan en el pase opaco y se dibujan en un **segundo pase despues**, para blending correcto contra las mallas.

#### Decision de diseno: ejes de camara por push constant
Orientar el quad requiere los ejes right/up de la camara. La opcion obvia era agregar la matriz `view` al `CameraUBO`, pero ese UBO lo declaran **todos** los shaders (basic, textured, terrain, water) y tocarlo arriesgaba regresiones en un renderer que ya funcionaba. En su lugar el billboard tiene su **propio push constant range** (20 floats: center, size, color, camRight, camUp), aislado del resto de pipelines. Cero impacto sobre los shaders existentes.

### Verificacion realizada
Escena mezclando 2 mallas 3D y 2 billboards con materiales distintos:
```
[Material] Resolved 2 material(s).
[Billboard] Graphics pipeline created successfully.
[TextureLoader] Loaded texture 4096x2048 from .../Material__wolf_col_tga_diffuse_jpeg.jpg
[D76] Smoke test: 120 frames rendered successfully.
```

---

## Fase 3 — Fisica por entidad

## Tarea 15.7 — `PhysicsComponent`

**Estado: ✅ Completada**

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

### Implementacion
- `PhysicsComponent{shape, halfExtentX/Y/Z, mass, isStatic}` + enum `ColliderShape`, con `ComponentType::Physics = 7` (sin reordenar los ids existentes).
- Serializacion en `ComponentSerialization.cpp` (nombre, to/from JSON con defaults).
- `Reflection.cpp`: `ComponentMeta` registrado, con el array de registro ampliado de 7 a 8 y el bound check actualizado. Esto lo hace editable desde el Inspector generico con undo/redo, sin escribir UI especifica.
- `EditorApp.cpp`: el menu "Add Component" iteraba hasta `ComponentType::AI` hardcodeado; ahora llega hasta `Physics` y el `switch` de construccion lo contempla.

#### Sobre el bump de version
`kCurrentVersion` 5 → 6. **No hay migracion de datos**: el componente es opcional y las escenas viejas cargan sin el. El bump existe para que un build anterior **rechace la escena por version** con un mensaje claro, en vez de fallar con `Unknown component type: Physics` al parsear. Se documento asi en el header en vez de escribir una migracion vacia.

### Verificacion realizada
Escena legacy sin `sceneVersion` sigue cargando (`scene instances loaded: 6`). Tests `test_physics_roundtrip` y `test_physics_defaults`, mas el round-trip nombre↔tipo extendido.

---

## Tarea 15.8 — Cuerpos fisicos por entidad y eventos de colision

**Estado: ✅ Completada**

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

### Implementacion
- `SceneLoader::loadPhysicsBodies()` devuelve una lista de `PhysicsSpawn` **separada** de los `RenderInstance`, para no mezclar datos de fisica dentro de una estructura de render. `RenderInstance` solo gano `entityId`, que es el nexo entre ambas listas (y sirve tambien para seleccion/picking).
- `Renderer::spawnSceneryPhysicsBodies()` crea un cuerpo por entidad con `PhysicsComponent` y mantiene `bodyToEntity_` (bodyId → entityId). Los cuerpos estaticos se registran con masa 0.
- `Renderer::syncPhysicsToInstances()` copia las posiciones simuladas de vuelta a las instancias tras cada paso de fisica.
- Nuevo evento tipado `CollisionEvent{phase, entityA, entityB}` en `GameEvents.h`, emitido por el `EventDispatcher` del renderer. Los cuerpos no mapeados a una entidad (piso y cubo demo de Sprint 9) reportan `entityId = 0`, que es la semantica documentada de "sin entidad asociada".

### Verificacion realizada
Escena con 3 cajas dinamicas, 1 cuerpo estatico y 1 entidad sin `PhysicsComponent`:
```
[VSTEP] scene instances loaded: 6
[Physics] Spawned 4 body(ies) from PhysicsComponent.
[D82] Collision Enter: 1 <-> 4 (entities 2 <-> 5)
[D82] Collision Enter: 1 <-> 2 (entities 2 <-> 3)
[D76] Smoke test: 120 frames rendered successfully.
```
6 instancias pero solo 4 cuerpos: la entidad sin componente no genera uno. Las colisiones resuelven a entity ids reales. `test_physics_determinism` sin regresion.

---

## Fase 4 — Rendimiento

## Tarea 15.9 — Batching por malla e instancing

**Estado: ⏸️ Postergada — ver nota de reevaluacion**

### Nota de reevaluacion (tras completar 15.10)
El spec asumia que el cuello de botella era "1 draw call + 1 push constant por entidad". Con **frustum culling ya implementado**, la escena de 442 entidades emite **10 draws**, no 442: el culling captura la mayor parte del beneficio que se le atribuia al instancing, y a un costo mucho menor.

El instancing sigue teniendo valor para escenas donde muchas entidades **si** estan en camara simultaneamente (multitudes, vegetacion densa), pero:
- La via por SSBO obliga a agregar un binding al `descriptorSetLayout_` **compartido por los 5 pipelines** (basic, textured, terrain, water, billboard) y por todos los descriptor sets de material.
- La via por vertex buffer con `VK_VERTEX_INPUT_RATE_INSTANCE` evita tocar los descriptors, pero exige un pipeline y un shader adicionales, mas gestion de buffers de instancia por frame.

Ambas son cambios de superficie amplia sobre un render path que hoy funciona y esta verificado. **Recomendacion:** hacerlo como tarea aislada, con una escena de benchmark que demuestre el cuello de botella primero (medir antes de optimizar), en vez de cerrarlo al final de este sprint.

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

**Estado: ✅ Completada**

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

### Implementacion
- `src/rendering/Frustum.h`: extraccion de los 6 planos por Gribb-Hartmann desde la matriz view-projection, y test AABB por "vertice positivo". Se dejo **libre de tipos Vulkan** a proposito para poder testearlo headless; opera sobre un `float[16]` column-major.
- Plano near tomado como `row2` (no `row3 + row2`), que es lo correcto para el rango de profundidad [0,1] de Vulkan en vez del [-1,1] de OpenGL.
- El culling se aplica tanto a las mallas opacas como a los billboards.
- Orden de instancias ahora es **layer → mesh → material**: la capa manda porque define el orden de dibujo (transparencias), y el agrupamiento por mesh/material solo reduce rebinds dentro de una misma capa.
- Contadores `lastDrawnInstances_` / `lastCulledInstances_` reportados al final del smoke test.

### Verificacion realizada
Test unitario `test_frustum_culling` (22 aserciones): puntos al frente visibles, geometria detras de la camara / mas alla del plano lejano / muy lateral descartada, caja grande a caballo del frustum conservada (sin falsos negativos), y barrido sobre el eje de vision.

En runtime, escena con 442 entidades esparcidas en un mundo de 256x256:
```
[VSTEP] scene instances loaded: 442
[Culling] Last frame: 10 drawn, 432 culled (of 442 instances).
```
**97.7% menos draw calls.**

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

## Resumen de archivos

> Refleja lo **efectivamente entregado**. Los archivos que el plan original preveia
> pero que no se implementaron estan listados aparte, mas abajo.

| Archivo | Cambios |
|---------|---------|
| `src/rendering/vulkan/RenderTypes.h` | `RenderInstance` extendido (rotacion, mesh, material, visible, layer, entityId); `PhysicsSpawn` |
| `src/rendering/vulkan/SceneLoader.h/.cpp` | Consume `SceneData`; parseo unico; `loadPhysicsBodies()` |
| `src/rendering/vulkan/Renderer.h/.cpp` | Matriz modelo, `AssetCache3D` real, materiales por descriptor set, billboards, cuerpos fisicos, culling |
| `src/rendering/vulkan/PipelineBuilder.h/.cpp` | Push constants 64→96 bytes, `createBillboardPipeline()` |
| `src/rendering/vulkan/CameraController.h/.cpp` | `forwardVector()/rightVector()/upVector()` |
| `src/rendering/vulkan/VkMath.h` | Helper `trs()` (composicion TRS column-major) |
| `src/rendering/Frustum.h` | **Nuevo** — planos del frustum y test AABB, libre de Vulkan |
| `src/rendering/mesh/MeshBuffers.h/.cpp` | `indexType()` por malla (fix uint16/uint32) |
| `assets/shaders/basic.vert/.frag` | `mat4 model` + normal matrix |
| `assets/shaders/textured.vert` | `mat4 model` + normal matrix |
| `assets/shaders/billboard.vert/.frag` | **Nuevos** |
| `src/core/components/Components.h` | `PhysicsComponent` + `ColliderShape` |
| `src/core/components/ComponentSerialization.cpp` | Serializacion de `PhysicsComponent` |
| `src/core/components/Reflection.cpp` | Metadata para el inspector (registro 7→8) |
| `src/core/events/GameEvents.h` | `CollisionEvent` tipado |
| `src/assets/MaterialAsset.h/.cpp` | **Nuevo** |
| `src/scene/SceneData.h/.cpp` | **Movido** desde `src/editor/`; bump a v6 |
| `src/editor/EditorApp.cpp` | Menu "Add Component" incluye `Physics` |
| `src/editor/EditorVkContext.cpp` | Fix de segfault al cerrar (bug preexistente) |
| `tests/test_scene_loader_3d.cpp` | **Nuevo** |
| `tests/test_material_asset.cpp` | **Nuevo** |
| `tests/test_frustum_culling.cpp` | **Nuevo** |
| `tests/test_component_serialization.cpp` | Cobertura de `PhysicsComponent` |
| `tests/CMakeLists.txt` | Registro de tests; `SceneData.cpp` ya no se compila suelto |
| `CMakeLists.txt` | Shaders billboard, `MaterialAsset.cpp`, `SceneData` en `game_core` |

### Previsto en el plan pero NO implementado

| Archivo | Motivo |
|---------|---------|
| `src/assets/importers/MaterialImporter.h/.cpp` | Fuera de alcance de 15.5; el renderer resuelve materiales por path directo |
| `src/assets/ImportManager.cpp` (registro) | Depende del importer anterior |
| `src/assets/AssetTypes.h` (`AssetType::Material`) | Idem: sin importer no hay tipo de asset que registrar |
| `tests/test_render_instance_build.cpp` | Su cobertura quedo dentro de `test_scene_loader_3d.cpp` |
| SSBO de instancias en `basic.vert` | Tarea 15.9, postergada (ver nota de reevaluacion) |

## Verificacion

| # | Criterio | Estado |
|---|---|---|
| 1 | `cmake --build build --parallel` sin errores | ✅ |
| 2 | `ctest` 100% en verde (29/29) | ✅ |
| 3 | `VulkanBootstrap`: mallas importadas, rotaciones y billboards | ✅ verificado visualmente |
| 4 | Fisica: entidades con collider colisionan; `test_physics_determinism` sin regresion | ✅ por logs |
| 5 | Escenas de version anterior cargan sin errores | ✅ |
| 6 | Culling reduce draws al alejar la camara | ✅ 442 → 10 |
| 7 | Editor: inspector edita mesh/material/rotacion y el preview lo refleja | ⬜ **sin verificar** |
| 8 | Performance: frame time medido con escena densa | ⬜ **sin medir** (ver 15.9) |

> Los items no cumplidos y la deuda detectada estan registrados como tareas en
> [`24_BACKLOG_POST_SPRINT15.md`](24_BACKLOG_POST_SPRINT15.md).
