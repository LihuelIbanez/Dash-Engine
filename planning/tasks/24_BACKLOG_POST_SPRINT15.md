# Backlog Post-Sprint 15 — Agosto 2026

## Contexto

Items identificados al cerrar las Fases 1-4 del Sprint 15 (ver `23_SPRINT15_COMPONENTES_3D.md`). Se agrupan en tres bloques: trabajo del sprint que quedo fuera de alcance, criterios de aceptacion sin verificar, y deuda tecnica detectada de paso.

Ninguno es bloqueante. Estado al momento de escribir: build sin errores, `ctest` 29/29 en verde.

---

## Grupo A — Trabajo de Sprint 15 pendiente

### A1 — Instancing por malla (tarea 15.9)

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

**Archivos previstos:** `src/rendering/vulkan/Renderer.h/.cpp`, `assets/shaders/basic.vert`, `src/rendering/vulkan/PipelineBuilder.cpp`.

---

### A2 — Escena de benchmark y medicion de frame time

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

---

### A3 — `MaterialImporter` e integracion con Asset Browser

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

**Archivos:** `src/assets/importers/`, `src/assets/AssetTypes.h`, `src/assets/ImportManager.cpp`, `src/rendering/vulkan/Renderer.cpp`.

---

## Grupo B — Verificacion pendiente

### B1 — Verificar el Inspector con los componentes nuevos

- **Prioridad:** Media
- **Objetivo:** confirmar en el editor que la edicion de componentes se refleja en el preview.

**Contexto.** Es el criterio de aceptacion 7 del Sprint 15, sin verificar. `PhysicsComponent` quedo editable "gratis" gracias a la reflexion, y `RenderComponent`/`TransformComponent` ahora llegan al renderer, pero **nadie lo probo desde la UI**. La verificacion hasta ahora fue por logs y por `VulkanBootstrap`, no por el editor.

**Checklist de verificacion manual:**
- [ ] El Inspector muestra `Physics` en el desplegable "Add Component".
- [ ] Agregar y quitar `PhysicsComponent` funciona con Cmd+Z / Cmd+Shift+Z.
- [ ] Editar `yawDeg` de un `TransformComponent` rota la entidad en el viewport.
- [ ] Editar `RenderComponent::mesh` cambia la malla sin reiniciar.
- [ ] Editar `RenderComponent::visible` oculta la entidad.
- [ ] Cambiar `renderMode` alterna entre malla 3D y billboard.
- [ ] Guardar y recargar la escena preserva todo lo anterior.

**Criterio de aceptacion:** checklist completo, o los defectos encontrados registrados como bugs.

---

### B2 — Investigar picos de frame de ~1.2 s en el editor

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

- **Prioridad:** Baja (hardening)
- **Objetivo:** cerrar el pendiente deliberado de MEJORA-6 de la auditoria de agosto.

**Contexto.** `EditorApp.cpp` y `GameBuildPipeline.cpp` siguen usando `popen()` con comandos de shell construidos por concatenacion. Hoy **no hay riesgo**: solo interpolan `BUILD_DIR`, que es una constante de compilacion, sin input de usuario. Se mantuvieron porque necesitan capturar stdout incrementalmente para el Build Log, y migrarlos exige pipes manuales con `posix_spawn`.

**Cuando encararlo:** si `BUILD_DIR` deja de ser constante, o si el pipeline pasa a aceptar rutas configurables por el usuario. Ahi el riesgo de inyeccion pasa a ser real.

**Criterio de aceptacion:** subprocesos lanzados con arrays de argumentos, conservando la captura incremental de salida.

---

## Resumen

| ID | Tarea | Prioridad |
|---|---|---|
| B2 | Picos de frame de ~1.2 s en el editor | **Alta** |
| A2 | Escena de benchmark y medicion de frame time | Media |
| A3 | `MaterialImporter` + Asset Browser | Media |
| B1 | Verificar Inspector con componentes nuevos | Media |
| A1 | Instancing por malla | Baja (requiere A2) |
| C3 | `popen()` del pipeline de build | Baja |
| ~~C1~~ | ~~Artefactos mutables en `.library/`~~ | ✅ Resuelta |
| ~~C2~~ | ~~Clarificar `library/` vs `.library/`~~ | ✅ Resuelta |
