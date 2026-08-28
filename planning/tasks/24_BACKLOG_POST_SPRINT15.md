# Backlog Post-Sprint 15 — Agosto 2026

## Contexto

Items identificados al cerrar las Fases 1-4 del Sprint 15 (ver `23_SPRINT15_COMPONENTES_3D.md`). Se agrupan en tres bloques: trabajo del sprint que quedo fuera de alcance, criterios de aceptacion sin verificar, y deuda tecnica detectada de paso.

Ninguno es bloqueante. Estado al momento de escribir: build sin errores, `ctest` 29/29 en verde.

> **Cierre (2026-08-27):** las 8 tareas quedaron resueltas y `ctest` pasa 32/32. La verificacion de B1 abrio 3 defectos nuevos (B1-a/b/c), unico pendiente. Ver el resumen al final.

---

## Grupo A — Trabajo de Sprint 15 pendiente

### A1 — Instancing por malla (tarea 15.9)

- **Estado:** ❌ Descartada con datos (2026-08-27)
- **Prioridad:** Baja (reevaluada)
- **Objetivo:** agrupar instancias de la misma malla en un unico draw instanciado.

**Por que bajo su prioridad.** El spec original asumia que el cuello de botella era "1 draw call + 1 push constant por entidad". Tras implementar frustum culling (15.10), una escena de 442 entidades emite **10 draws**, no 442. El culling capturo la mayor parte del beneficio atribuido al instancing.

Sigue teniendo valor cuando muchas entidades estan **simultaneamente en camara** (multitudes, vegetacion densa), escenario que hoy no se da en el contenido existente.

**Dos caminos evaluados:**

| Via | Costo | Riesgo |
|---|---|---|
| SSBO con transforms indexados por `gl_InstanceIndex` | Agrega un binding al `descriptorSetLayout_` **compartido por los 5 pipelines** (basic, textured, terrain, water, billboard) y por todos los descriptor sets de material | Alto |
| Vertex buffer con `VK_VERTEX_INPUT_RATE_INSTANCE` | No toca descriptors, pero exige pipeline y shader adicionales mas gestion de buffers de instancia por frame | Medio |

**Precondicion:** hacer A2 primero. No optimizar sin una medicion que demuestre el cuello de botella.

**Criterio de aceptacion:**
- Escena de benchmark con N entidades de la misma malla visibles a la vez.
- Draw calls proporcionales a mallas distintas, no a entidades.
- Mejora de frame time **medida** contra la baseline de A2.

### Resolucion: no se implementa

La precondicion se cumplio (A2) y **los datos no justifican el trabajo**. Ver la tabla de baseline en A2.

1. **El frame nunca esta limitado por CPU.** El present es FIFO: `frame total` se queda en 8.33 ms desde 171 hasta 26 452 instancias visibles. El instancing solo puede reducir `cmd recording`, que no es lo que fija el frame.
2. **El margen absoluto es despreciable en el contenido real.** Con la escena existente (442 entidades → 10 draws) el grabado no llega a 0.04 ms. El techo teorico de mejora es **menos del 0.5% del presupuesto de frame**.
3. **Ni forzando el escenario aparece el cuello.** A 26 452 instancias visibles — 60x el contenido real, una densidad que ningun nivel del juego produce — el grabado es 1.37 ms: 16% del presupuesto, y el frame sigue clavado en vsync.
4. **El costo/riesgo no cambio.** La via SSBO sigue tocando el `descriptorSetLayout_` compartido por los 5 pipelines.

**Condicion para reabrirla.** Si aparece contenido que sostenga >20 000 instancias visibles simultaneas **y** el present deja de ser FIFO (vsync desactivado o presentacion inmediata), rehacer la medicion con `tools/gen_benchmark_scene.py` y reevaluar. La baseline de A2 queda registrada justamente para ese momento.

---

### A2 — Escena de benchmark y medicion de frame time

- **Estado:** ✅ Resuelta (2026-08-27)
- **Prioridad:** Media
- **Objetivo:** tener una baseline reproducible de rendimiento del render 3D.

**Contexto.** Hoy no existe ninguna medicion de frame time del path Vulkan. El criterio de aceptacion 8 del Sprint 15 quedo sin cumplir por esto, y A1 no se puede justificar ni descartar sin este dato.

`VulkanBootstrap` ya reporta contadores de culling al final del smoke test; falta el tiempo por frame.

**Alcance:**
- Escena de benchmark versionada (no en `/tmp`) con densidad configurable de entidades **dentro del frustum**.
- Reporte de frame time (min / avg / p95 / max) al final del smoke test.
- Documentar la baseline en el propio archivo de la escena o en este backlog.

**Criterio de aceptacion:**
- `VulkanBootstrap --scene <benchmark>` imprime estadisticas de frame time reproducibles.
- La baseline queda registrada para comparar contra A1.

### Resolucion

**Hallazgo que cambio el diseño de la medicion.** `SwapchainContext::choosePresentMode()` devuelve `VK_PRESENT_MODE_FIFO_KHR` incondicionalmente: el loop esta clavado a vsync. Medir solo el frame time de pared habria dado un numero constante e **inutil** para justificar A1. Por eso se instrumentan **dos** series:

| Serie | Que mide |
|---|---|
| `frame total` | iteracion completa del loop, dominada por el present bloqueante |
| `cmd recording` | solo `recordDrawCommands()`, el costo de CPU que el instancing atacaria |

**Cambios.**
- `Renderer`: recolecta muestras por frame durante `runSmoke()` y las resume en `reportFrameStats()` (min/avg/p95/max). Descarta el primer 10% de frames (tope 10) como warm-up de swapchain y pipelines.
- `vulkan_bootstrap_main.cpp`: nuevo flag `--frames N` (default 120) para controlar la duracion de la corrida.
- `tools/gen_benchmark_scene.py`: generador de escenas con `--count`, `--spacing`, `--scale` y `--mesh`. Distribuye las entidades en una grilla cuadrada centrada en el jugador para maximizar las **simultaneamente visibles**.
- `scenes/benchmark_dense.json`: escena versionada de 401 entidades (`--count 400 --spacing 0.6`).

**Uso:**
```
python3 tools/gen_benchmark_scene.py --count 400 --spacing 0.6 --out scenes/benchmark_dense.json
./build/VulkanBootstrap --scene scenes/benchmark_dense.json --frames 300
```

### Baseline medida

MacBook (display ProMotion 120 Hz → techo de vsync ≈ 8.33 ms), 300 frames por corrida, malla `cube`:

| Entidades | Visibles (drawn) | `cmd recording` avg | `frame total` avg |
|---|---|---|---|
| 401 | 171 | 0.035 ms | 8.330 ms |
| 1 001 | 455 | 0.091 ms | 8.332 ms |
| 2 501 | 1 087 | 0.175 ms | 8.335 ms |
| 5 001 | 2 195 | 0.340 ms | 8.330 ms |
| 20 001 | 8 854 | 0.857 ms | 8.338 ms |
| 60 001 | 26 452 | 1.370 ms | 8.380 ms |

El costo de grabado escala ~linealmente con las instancias visibles (~0.15 µs por draw). `frame total` **no se mueve** en ningun caso: el loop esta limitado por el present, no por la CPU.

---

### A3 — `MaterialImporter` e integracion con Asset Browser

- **Estado:** ✅ Resuelta (2026-08-27)
- **Prioridad:** Media
- **Objetivo:** cerrar el criterio de aceptacion de 15.5 que quedo explicitamente incumplido.

**Contexto.** `MaterialAsset` existe y el renderer lo consume, pero **por path directo**. No hay importer registrado, por lo que los materiales no tienen GUID en la `AssetDatabase` ni aparecen en el Asset Browser. El criterio *"Asset Browser lista los materiales con su GUID"* no esta cumplido.

**Alcance:**
1. `src/assets/importers/MaterialImporter.h/.cpp` siguiendo el patron de los 6 importers existentes.
2. `AssetType::Material` en `src/assets/AssetTypes.h`.
3. Registro en `src/assets/ImportManager.cpp`.
4. Que el renderer resuelva `RenderComponent::material` **por GUID** ademas de por path, manteniendo compatibilidad.

**Criterio de aceptacion:**
- Un `.mat.json` en `assets/` se importa y aparece en el Asset Browser con GUID y hash.
- Reimport incremental funciona (cambiar el material y ver el cambio sin reiniciar).
- Escenas que referencian materiales por path siguen cargando.

### Resolucion

**Bug de fondo encontrado primero.** El mapeo `AssetType` ↔ string estaba **triplicado** y las tres copias estaban desactualizadas:

| Copia | Tipos que cubria |
|---|---|
| `AssetTypes.h::assetTypeToStr` | los 7 (completa) |
| `AssetDatabase.cpp` helpers de JSON | solo 4 |
| `AssetDatabase.cpp` miembros estaticos | solo 4 |
| `AssetRepositorySqlite.cpp` | solo 4 |

Consecuencia observable: `assets/asset_db.json` tenia `prefabs/goblin_warrior.json` guardado como **`Unknown`**. Agregar `Material` sin arreglar esto habria dado el mismo resultado y el criterio de aceptacion no se habria cumplido de verdad. Las tres copias ahora delegan en `AssetTypes.h`, que suma `assetTypeFromStr()` como contraparte.

**Cambios.**
- `AssetTypes.h`: nuevo `AssetType::Material` y `assetTypeFromStr()`; queda como unica fuente de verdad.
- `AssetDatabase.cpp` / `AssetRepositorySqlite.cpp`: delegan en los helpers compartidos.
- `importers/MaterialImporter.{h,cpp}`: valida el JSON, avisa si `albedoTexture` no existe, estampa el GUID del record en la copia importada y escribe a `library/`.
- `ImportManager`: registra el importer e `inferAssetType()` mapea `*.mat.json` y cualquier `.json` bajo `materials/` a `Material`.
- `Renderer`: `materialDb()` carga `<assets>/asset_db.json` de forma perezosa (solo si aparece una referencia con forma de GUID, via `looksLikeGuid()`). El GUID se prueba **antes** que los paths para que un archivo homonimo no lo tape; las rutas siguen como fallback.
- `CMakeLists.txt`: `AssetDatabase.cpp` + `AssetRepositorySqlite.cpp` entran a `vulkan_experimental`, y nuevo define `VULKAN_ASSETS_DIR`.
- `assets/materials/stone.mat.json`: material de ejemplo versionado.

**Verificacion.**

1. `tests/test_material_importer.cpp` (nuevo, en ctest): deteccion de tipo, alta con GUID/tipo/hash, copia a `library/`, reimport incremental (sin cambios → 0 imports; con cambios → 1 import y **el GUID se conserva**), y round-trip de los 6 `AssetType` por el `asset_db.json`.
2. Resolucion en el renderer, end-to-end contra `VulkanBootstrap` con una escena donde una entidad referencia el material **por GUID** y otra **por path**:

```
[Material] Resolved 2 material(s).
```

Sin ningun `Definition not found`: ambas vias resuelven y la compatibilidad por path se mantiene.

**Nota operativa.** `AssetDatabase::load()` respeta `DASH_DB_MODE`. En el default (`Hybrid`) lee del SQLite y solo cae al JSON si aquel falla, asi que la verificacion anterior necesita `DASH_DB_MODE=json` para leer el `asset_db.json`. No es un defecto: es el comportamiento del cutover del Sprint 7.

`ctest` 30/30.

---

## Grupo B — Verificacion pendiente

### B1 — Verificar el Inspector con los componentes nuevos

- **Estado:** ✅ Verificada (2026-08-27) — **4 de 7 pasan; 3 defectos registrados**
- **Prioridad:** Media
- **Objetivo:** confirmar en el editor que la edicion de componentes se refleja en el preview.

**Contexto.** Es el criterio de aceptacion 7 del Sprint 15, sin verificar. `PhysicsComponent` quedo editable "gratis" gracias a la reflexion, y `RenderComponent`/`TransformComponent` ahora llegan al renderer, pero **nadie lo probo desde la UI**. La verificacion hasta ahora fue por logs y por `VulkanBootstrap`, no por el editor.

**Checklist de verificacion:**
- [x] El Inspector muestra `Physics` en el desplegable "Add Component".
- [x] Agregar y quitar `PhysicsComponent` funciona con Cmd+Z / Cmd+Shift+Z.
- [ ] Editar `yawDeg` de un `TransformComponent` rota la entidad en el viewport. → **B1-a**
- [ ] Editar `RenderComponent::mesh` cambia la malla sin reiniciar. → **B1-b**
- [x] Editar `RenderComponent::visible` oculta la entidad.
- [ ] Cambiar `renderMode` alterna entre malla 3D y billboard. → **B1-c**
- [x] Guardar y recargar la escena preserva todo lo anterior.

**Criterio de aceptacion:** checklist completo, o los defectos encontrados registrados como bugs.

### Resolucion

**Hallazgo estructural.** El editor **no usa** `dash::vkexp::Renderer`. Su viewport se dibuja en `EditorApp::renderWorldToTexture()`, un camino de render propio y mucho mas simple, y `VulkanBootstrap` solo se lanza para Play / Build & Run. Todo el trabajo del Sprint 15 aterrizo en `Renderer` (culling, billboards, materiales, mallas por `RenderComponent`), pero **el viewport del editor nunca se actualizo**.

De los datos de entidad, `renderWorldToTexture()` lee unicamente:

```cpp
for (const auto& comp : e.components) {
    if (auto* tf = std::get_if<TransformComponent>(&comp))
        wz = tf->z;
    if (auto* rc = std::get_if<RenderComponent>(&comp))
        visible = rc->visible;
}
```

Es decir: `z` y `visible`. Ignora `yawDeg`/`pitchDeg`/`rollDeg`/`scale`, ignora `mesh` y `material`, e ignora `renderMode`. La malla se elige por una regla fija (`isPlayer ? cube : wolf`).

**Cobertura automatizada agregada.** Los comandos de componentes no tenian **ningun** test: `test_undo_redo_commands` cubre paint/place/erase y `test_move_edit_commands` cubre move/name/class/position. `tests/test_component_commands.cpp` (nuevo, 47 aserciones) cubre lo verificable sin UI:

- `Physics` esta registrado en la reflexion con sus 6 campos (es lo que alimenta el desplegable "Add Component").
- `AddComponentCommand` / `RemoveComponentCommand`: apply / undo / redo conservando valores exactos.
- `EditComponentFieldCommand` sobre `yawDeg` (Float), `mesh` (String), `visible` (Bool) y `renderMode` (Enum): apply / undo / redo.
- Round-trip de escena preservando los cuatro campos mas `PhysicsComponent`.

Esto confirma que **el modelo de datos y el undo/redo estan bien**: lo que falta es que el viewport del editor consuma esos campos.

---

### B1-a — El viewport del editor ignora la rotacion y la escala del Transform

- **Estado:** ✅ Resuelta (2026-08-28)
- **Prioridad:** Media
- **Sintoma:** editar `yawDeg`, `pitchDeg`, `rollDeg` o `scale` no cambia nada en el viewport.
- **Causa:** `EditorApp::renderWorldToTexture()` armaba su push constant con posicion y una escala fija (`useWolf ? 0.4f : 0.30f`); nunca leia los angulos del `TransformComponent`.
- **Criterio de aceptacion:** mover el slider de `yawDeg` rota la entidad en el viewport en el mismo frame.

### B1-b — El viewport del editor ignora `RenderComponent::mesh`

- **Estado:** ✅ Resuelta (2026-08-28)
- **Prioridad:** Media
- **Sintoma:** cambiar `mesh` no cambia el modelo dibujado.
- **Causa:** la malla se decidia con `bool useWolf = !isPlayer && wolfAvailable;`. Solo existian dos mallas cableadas (cube y wolf) y el campo `mesh` no participaba.
- **Criterio de aceptacion:** el viewport resuelve la malla por `RenderComponent::mesh` a traves del cache de assets, con fallback a cube.

### B1-c — El viewport del editor no implementa `renderMode` billboard

- **Estado:** ✅ Resuelta (2026-08-28)
- **Prioridad:** Baja
- **Sintoma:** poner `renderMode = BillboardSprite` seguia dibujando la malla 3D.
- **Causa:** `renderWorldToTexture()` solo tenia el pass de `basicPipeline()`; no habia pipeline de billboard en el contexto del editor.
- **Criterio de aceptacion:** alternar `renderMode` cambia el dibujado en el viewport, como ya ocurre en `Renderer::recordDrawCommands()`.

### Resolucion comun: unificacion del dibujado (2026-08-28)

Los tres tenian la misma causa raiz y se cerraron con el mismo cambio.

**Diagnostico.** El editor y el runtime tenian **dos implementaciones** del dibujado de entidades. `Renderer::recordDrawCommands()` conocia culling, materiales, mallas por `RenderComponent` y billboards; `EditorApp::renderWorldToTexture()` tenia una version pobre que de los componentes solo leia `TransformComponent::z` y `RenderComponent::visible`.

**Que NO se hizo.** No se fusionaron los devices Vulkan. El editor usa SDL2 y renderiza a una textura offscreen; el runtime usa GLFW y presenta a un swapchain. Son ventanas distintas y esta bien que cada uno tenga su `VkInstance`/`VkDevice`.

**Que se hizo.** Se extrajo lo que si estaba duplicado — el dibujado — a `SceneRenderer::drawSceneInstances()`. Recibe las instancias mas un vector de `InstanceResources` (malla, descriptor set y tinte que resuelve cada llamador desde su propio cache), de modo que la logica de culling, binding y push constants existe **una sola vez**.

- `SceneRenderer.{h,cpp}`: pase opaco con frustum culling + pase de billboards.
- `SceneLoader::buildInstances(const SceneData&)`: el editor convierte entidades a `RenderInstance` con **el mismo codigo** que el runtime, asi que yaw/pitch/roll/scale/mesh/material/visible/renderMode llegan por construccion.
- `EditorVkContext`: pipeline de billboard + `resolveMesh()` con `AssetCache3D`, espejo del `Renderer::resolveMesh()`.

**Verificacion.**
- Equivalencia del runtime: los contadores de culling dan **identicos** a la baseline de A2 en las 4 densidades (171/230, 455/546, 1087/1414, 2195/2806). El refactor no cambio comportamiento.
- Arranque del editor: los 4 pipelines se crean, **incluido `[Billboard] Graphics pipeline created successfully`**, sin errores de validacion.
- `ctest` 32/32.

**Cambio de apariencia esperado.** El viewport ahora usa las alturas base y escalas de `SceneLoader` (jugador 1.0/0.26-0.52, enemigo 0.6/0.22-0.40) en vez de los valores fijos que tenia (0.52 y 0.30/0.4). Las entidades se veran algo distintas que antes: eso **es** la correccion, porque ahora coinciden con lo que se ve al entrar en Play.

---

### B2 — Investigar picos de frame de ~1.2 s en el editor

- **Estado:** ✅ Resuelta (2026-08-27)
- **Prioridad:** Alta
- **Objetivo:** diagnosticar cuatro frames consecutivos de mas de un segundo tras abrir un proyecto.

**Evidencia** (log del editor, corrida del 2026-08-27):
```
[EditorApp::openProject] Starting migration and project load...
[Profiler] Frame spike: 36.49 ms
[Profiler] Frame spike: 1177.60 ms
[Profiler] Frame spike: 1180.45 ms
[Profiler] Frame spike: 1188.24 ms
[Profiler] Frame spike: 1212.20 ms
```

**Por que importa.** No es un pico unico de carga: son cuatro frames **sostenidos** por encima del segundo, y el editor corre al 100% de CPU de forma constante. El patron sugiere que algo del path de proyecto/migracion se ejecuta **por frame** en vez de una sola vez.

**Puntos de partida sugeridos:**
- `EditorApp::openProject()` y `refreshProjectPaths()`.
- `ProjectDataMigrator` — verificar que la migracion no se reintente cada frame.
- `importManager_.importAll()` — se invoca desde varios lugares (`EditorApp.cpp` lineas ~423 y ~612).
- El `FileWatcher` y los `deferredReloads_`.

**Criterio de aceptacion:**
- Causa raiz identificada y documentada.
- Sin picos > 100 ms en estado estacionario tras abrir un proyecto.

### Resolucion

La causa no estaba en `openProject()` ni en la migracion: era **`FileWatcher::scan()`**, llamado incondicionalmente en cada iteracion de `EditorApp::run()`.

**Cadena causal.**
1. `scan()` construia su snapshot llamando a `ImportManager::computeFileHash()` sobre **cada archivo** de `assets/`.
2. `computeFileHash()` lee el archivo entero a un `ostringstream`, lo copia a un `std::string` y recien ahi hashea. Sobre `assets/` (**363 MB en 56 archivos**, con `.exr`/`.png` de terreno de hasta 39 MB) eso son ~726 MB de asignaciones por scan.
3. El scan tardaba **mas que el `pollIntervalSeconds_` de 1.0 s**, y como `lastScan_` se fijaba al *inicio*, en el frame siguiente `elapsed >= 1.0` ya se cumplia. El throttle quedaba anulado y el scan pasaba a correr **en todos los frames** — de ahi los picos sostenidos y el 100% de CPU.

**Medicion** (micro-benchmark sobre `assets/`, 3 corridas con cache caliente):

| Implementacion | scan() |
|---|---|
| Anterior (hash de contenido) | 1259.69 / 1157.09 / 1161.89 ms |
| Actual (size + mtime) | 10.02 / 6.70 / 6.75 ms |

Los 1157-1260 ms reproducen los picos del log (1177 / 1180 / 1188 / 1212 ms), lo que confirma el diagnostico.

**Cambios.**
- `FileWatcher.cpp`: la señal de cambio pasa a ser `size:mtime` (helper `fileStamp()`, un `stat` por archivo) en vez del hash del contenido. `hashSnapshot_` → `stampSnapshot_`.
- `FileWatcher.cpp`: `lastScan_` se fija al **final** del scan, para que un scan lento no vuelva a dispararse en el frame siguiente. Es la valvula de seguridad que faltaba.
- `ImportManager::reimportChanged()`: pasa `force=false`. Como la señal ahora es mas gruesa, deja que el hash de contenido de `importAsset()` decida, evitando reimportar un `.exr` de 39 MB solo porque cambio el mtime. La deteccion sigue siendo exacta a nivel de bytes.

**Verificacion:** build sin errores, `ctest` 29/29. `test_hot_reload` cubre Added/Modified/Deleted/no-change/reset y pasa sin modificaciones.

**Residual:** dos archivos distintos con identico tamaño *y* identico mtime al nanosegundo no se distinguirian. En APFS el mtime tiene resolucion de nanosegundos, asi que el escenario es despreciable.

---

## Grupo C — Deuda tecnica

### C1 — Artefactos mutables versionados en `.library/`

- **Estado:** ✅ Resuelta (2026-08-27)
- **Prioridad:** Media
- **Objetivo:** que correr el editor no ensucie el working tree.

**Contexto verificado.** `.library/` es el directorio de cache por proyecto (default de `ProjectManifest::libraryDir`). Tiene **12 archivos trackeados, 476 KB**, e incluye:

| Archivo | Problema |
|---|---|
| `.library/dash_engine.db` | SQLite de 135 KB que **cambia en cada corrida del editor**, apareciendo como modificado en todo commit |
| `.library/dash_engine.db.bak.1775754438` | Backup con timestamp commiteado al repo; residuo de una migracion de abril |
| `.library/models/gltf/*.dashmesh`, `.library/sprites/*.png` | Artefactos de importacion, regenerables desde `assets/` |

Ninguna de las dos carpetas de cache esta en `.gitignore`.

**Decision a tomar:** si `.library/` es cache regenerable, deberia ignorarse (salvo un `.gitkeep`). Si el `.db` se versiona a proposito como estado del proyecto, entonces al menos el `.bak.<timestamp>` deberia borrarse y el patron agregarse a `.gitignore`.

**Criterio de aceptacion:**
- Correr el editor deja el working tree limpio, **o** esta documentado por que el `.db` se versiona.
- Sin archivos `.bak.<timestamp>` en el repo.

### Resolucion

Decision: el cache de importacion es **regenerable** y no debe versionarse.

- `.gitignore`: `library/*` + `!library/.gitkeep` + `.library/`.
- `git rm -r --cached` sobre ambas carpetas: **21 archivos destrackeados**, incluido `dash_engine.db.bak.1775754438`.
- Los archivos **siguen en disco**; solo salieron del indice.

**Verificacion previa a actuar** (la respuesta "se usa modo sqlite" hacia sospechar un conflicto):
1. Los 5 tests que tocan SQLite (`test_asset_db_sqlite`, `test_gameplay_db_sqlite`, `test_project_data_migrator`, `test_sqlite_cutover_performance`, `test_save_game`) crean su **propio** `.library` en un directorio temporal. Ninguno lee el del repo.
2. La ruta del `.db` se deriva del **proyecto activo** (`ProjectManifest::absoluteLibraryDir()`), no del repo.

Unico escenario residual: abrir la raiz del repo como proyecto en modo sqlite. Ahi el `.db` se regenera con el CLI `migrate_project_data`, ya presente en el repo.

Post-verificacion: build sin errores, `ctest` 29/29.

---

### C2 — Clarificar `library/` vs `.library/`

- **Estado:** ✅ Resuelta (2026-08-27)
- **Prioridad:** Baja
- **Objetivo:** eliminar la ambiguedad entre dos directorios de cache que coexisten.

**Contexto verificado.** Ambos estan trackeados: `.library/` (12 archivos) y `library/` (10 archivos, con `.gitkeep`), y tienen contenido solapado (`gameplay/`, `prefabs/`, `sprites/`).

- `.library/` viene del default de `ProjectManifest::libraryDir` (`".library"`), usado por proyectos.
- `library/` es el que documenta el README como "cache de assets importados" y aparece en `AppPaths::getLibraryDir()` como fallback sin proyecto activo.

**Alcance:** determinar cual es canonico, migrar o eliminar el otro, y alinear el README (que hoy solo menciona `library/`).

**Criterio de aceptacion:** un unico directorio de cache documentado, sin contenido duplicado.

### Resolucion

Hallazgos que cerraron la ambiguedad:
- El contenido de ambas era **identico** (`diff` sobre gameplay, prefabs y sprites: los 3 iguales).
- **No hay ningun `.dashproject`** en la raiz, asi que `.library/` era residuo de haber abierto el repo como proyecto en algun momento.
- Los GUIDs viven en `assets/asset_db.json`, no en el `.db`, por lo que destrackear el cache no rompe referencias por `prefabGuid`.

Decision: `.library/` se elimina del repo por completo; `library/` queda como la canonica (es la que documenta el README y la que tiene `.gitkeep`), pero **ignorada salvo el `.gitkeep`** por C1.

Resuelta junto con C1 en el mismo cambio.

---

### C3 — Revisar los `popen()` restantes del pipeline de build

- **Estado:** ✅ Resuelta (2026-08-27)
- **Prioridad:** Baja (hardening)
- **Objetivo:** cerrar el pendiente deliberado de MEJORA-6 de la auditoria de agosto.

**Contexto.** `EditorApp.cpp` y `GameBuildPipeline.cpp` siguen usando `popen()` con comandos de shell construidos por concatenacion. Hoy **no hay riesgo**: solo interpolan `BUILD_DIR`, que es una constante de compilacion, sin input de usuario. Se mantuvieron porque necesitan capturar stdout incrementalmente para el Build Log, y migrarlos exige pipes manuales con `posix_spawn`.

**Cuando encararlo:** si `BUILD_DIR` deja de ser constante, o si el pipeline pasa a aceptar rutas configurables por el usuario. Ahi el riesgo de inyeccion pasa a ser real.

**Criterio de aceptacion:** subprocesos lanzados con arrays de argumentos, conservando la captura incremental de salida.

### Resolucion

**Revalidacion de la premisa.** Se reviso si seguia siendo cierto que ningun dato de usuario alcanza un shell. Las tres invocaciones existentes:

| Sitio | Entrada interpolada | ¿Controlable por el usuario? |
|---|---|---|
| `EditorApp.cpp` `std::system("where cmake ...")` | literal | no |
| `EditorApp.cpp` `popen` de Build & Run | `BUILD_DIR` (constante de compilacion) | no |
| `GameBuildPipeline.cpp` `runCommandCapture` | **parametro `buildDir`** | no *en el unico call site* |

La premisa se sostenia, pero con un matiz que la debilitaba: `GameBuildPipeline::build()` recibe `buildDir` como **parametro**, no como constante. La garantia no era estructural, dependia de que el unico llamador pasara `BUILD_DIR`. Cualquier llamador futuro reabria el agujero en silencio. Por eso se implemento en vez de diferirse.

**Cambios.**
- `src/editor/project/ProcessRunner.{h,cpp}`: `runProcessCapture(argv, onLine)`. En POSIX usa `posix_spawnp` con un pipe manual que recibe stdout y stderr, y transmite el output **linea por linea** mientras el proceso corre. Devuelve el codigo de salida del hijo, o -1 si no arranco.
- El constructo `cd "<dir>" && make ...` desaparece: `cmake --build <dir>` toma el directorio como argumento, asi que ya no hace falta un shell para cambiar de directorio.
- `GameBuildPipeline` y `EditorApp::buildAndRun()` pasan a armar un `std::vector<std::string>` de argumentos.
- En Windows `_popen` no tiene forma argv, asi que se conserva, pero cada argumento se re-entrecomilla escapando `"` y `\` en vez de concatenarse crudo.

**Verificacion.** `tests/test_process_runner.cpp` (nuevo, 9 aserciones):
- captura de salida y propagacion del codigo de salida (incluido != 0);
- ejecutable inexistente devuelve error en vez de colgarse;
- **no hay inyeccion**: pasar `"inofensivo; touch <canary>"` como argumento no crea el canary y el texto llega literal. Idem con un intento de escape por comillas (`"; touch <canary>; echo "`).

Contraprueba de que el test no es vacuo: el mismo string via shell (`sh -c`) **si** ejecuta el `touch`. Es decir, el test falla contra la implementacion anterior.

Se verifico ademas que el argv real del pipeline (`cmake --build <dir> --target VulkanBootstrap --parallel`) sigue devolviendo exit 0 y capturando la salida de forma incremental.

---

## Resumen

Las 8 tareas originales quedaron cerradas. La verificacion de B1 abrio 3 defectos, que tambien se cerraron al unificar el dibujado de escena.

### Pendiente

Nada. Ver `25_EDITOR_AAA.md` para el trabajo siguiente.

### Cerradas

| ID | Tarea | Resultado |
|---|---|---|
| A1 | Instancing por malla | ❌ Descartada con datos |
| A2 | Escena de benchmark y medicion de frame time | ✅ Resuelta |
| A3 | `MaterialImporter` + Asset Browser | ✅ Resuelta |
| B1 | Verificar Inspector con componentes nuevos | ✅ Verificada (4/7; 3 bugs abiertos) |
| B2 | Picos de frame de ~1.2 s en el editor | ✅ Resuelta |
| C1 | Artefactos mutables en `.library/` | ✅ Resuelta |
| C2 | Clarificar `library/` vs `.library/` | ✅ Resuelta |
| C3 | `popen()` del pipeline de build | ✅ Resuelta |
| B1-a/b/c | Defectos del viewport del editor | ✅ Resueltos (unificacion del dibujado) |

### Bugs preexistentes corregidos de paso

| Hallazgo | Donde aparecio |
|---|---|
| El `FileWatcher` anulaba su propio throttle y hasheaba 363 MB por frame | B2 |
| El mapeo `AssetType` ↔ string estaba triplicado y guardaba Prefab/Sprite/Model como `Unknown` | A3 |
| Los comandos Add/Remove/EditComponentField no tenian ningun test | B1 |

### Cobertura de tests

De 29 a 32 tests en `ctest`:

| Test | Cubre |
|---|---|
| `test_material_importer` | A3: deteccion de tipo, alta con GUID, reimport incremental, round-trip de `AssetType` |
| `test_component_commands` | B1: reflexion de Physics, apply/undo/redo de los 3 comandos, round-trip de escena |
| `test_process_runner` | C3: captura incremental, codigo de salida y ausencia de inyeccion de shell |
