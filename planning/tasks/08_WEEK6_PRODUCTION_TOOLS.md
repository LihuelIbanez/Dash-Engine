# Semana 6 - Prefabs, Hot-Reload y Herramientas de Produccion

## Meta de la semana

Completar el pipeline de produccion: prefabs reutilizables con overrides por instancia, hot-reload de assets sin reiniciar el editor, herramientas de validacion de contenido, paquete de build reproducible y cierre de sprint con tests de regresion.

---

## Tarea 6.1 - Sistema de prefabs/arquetipos (D26)

### Objetivo
Permitir definir templates de entidades (prefabs) como archivos JSON con componentes default. Al instanciar un prefab en la escena, se copian sus componentes y se permiten overrides por instancia. Solo los overrides se guardan en la escena, no los datos base del prefab.

### Carpetas a crear

1. assets/prefabs/
- Contenido fuente de prefabs (JSON).

### Archivos a crear

1. src/core/components/PrefabAsset.h
2. src/core/components/PrefabAsset.cpp
- Struct `PrefabAsset`:
  - `std::string guid` — GUID unico del prefab (coincide con AssetDatabase).
  - `std::string name` — nombre legible ("Goblin Warrior", "Chest").
  - `std::vector<ComponentVariant> defaultComponents` — componentes template.
- Funciones:
  - `PrefabAsset loadPrefab(const std::string& path)` — carga desde JSON.
  - `bool savePrefab(const PrefabAsset& prefab, const std::string& path)`.
  - `std::vector<ComponentVariant> instantiate(const PrefabAsset& prefab)` — deep copy de componentes default.
  - `nlohmann::json computeOverrides(const PrefabAsset& prefab, const std::vector<ComponentVariant>& instance)` — diff entre prefab y la instancia actual.
  - `void applyOverrides(const PrefabAsset& prefab, std::vector<ComponentVariant>& instance, const nlohmann::json& overrides)` — mezcla defaults + overrides.

3. src/assets/importers/PrefabImporter.h
4. src/assets/importers/PrefabImporter.cpp
- Hereda de `IImporter`.
- `import()`: copia JSON de assets/prefabs/ a library/, valida estructura, registra en AssetDatabase con tipo Prefab.
- En ImportManager registrar PrefabImporter para AssetType::Prefab.

5. assets/prefabs/goblin_warrior.json (ejemplo)
- JSON de ejemplo con TransformComponent, HealthComponent, StatsComponent, AIComponent, CombatComponent.

6. tests/test_prefab_system.cpp
- Test: cargar prefab JSON, instantiate, verificar componentes copiados.
- Test: modificar instancia, computeOverrides retorna solo cambios.
- Test: applyOverrides sobre instancia fresh produce resultado correcto.
- Test: prefab con GUID no encontrado retorna error controlado.

### Archivos a modificar

1. src/assets/AssetTypes.h
- Seccion: enum AssetType.
- Cambios:
  - Agregar `Prefab` al enum.

2. src/editor/SceneData.h
- Seccion: struct EntityData.
- Cambios:
  - Agregar `std::string prefabGuid;` — vacio si no es instancia de prefab.
  - Agregar `nlohmann::json componentOverrides;` — solo los diffs vs prefab.

3. src/editor/SceneData.cpp
- Seccion: to_json / from_json de EntityData.
- Cambios:
  - Si `prefabGuid` no esta vacio, serializar `"prefabGuid"` y `"overrides"` en lugar de componentes completos.
  - En from_json: si tiene `"prefabGuid"`, cargar prefab base y aplicar overrides.

4. src/editor/EditorApp.cpp
- Seccion: drawViewport().
- Cambios:
  - Soportar drag de prefab desde Asset Browser al viewport: al soltar, instanciar prefab en posicion del mouse.
  - Crear PlaceEntityCommand (o extender PlaceEnemyCommand) para instanciar prefabs.

- Seccion: drawPropertiesPanel().
- Cambios:
  - Si entidad tiene prefabGuid, mostrar "(Prefab: nombre)" en header.
  - Propiedades con override: texto en negrita (ImGui::PushStyleColor).
  - Boton "Reset to Prefab" por componente: restaura valores default del prefab.
  - Boton "Reset All" en header de entidad.

5. src/assets/ImportManager.cpp
- Seccion: constructor.
- Cambios:
  - Registrar PrefabImporter para AssetType::Prefab.

6. tests/CMakeLists.txt
- Cambios:
  - Agregar test_prefab_system.cpp.

### Criterio de aceptacion

- Se puede crear un prefab JSON con componentes default.
- Instanciar prefab en escena copia componentes.
- Overrides por instancia se guardan como diff, no como copia completa.
- Al modificar el prefab base, instancias sin override heredan los cambios nuevos.
- Inspector distingue visualmente valores override vs default.
- "Reset to Prefab" restaura valores originales.

---

## Tarea 6.2 - Hot-reload de assets (D27)

### Objetivo
Detectar automaticamente cambios en archivos de assets/ y re-importar sin reiniciar el editor. Recargar datos en caliente (GameplayDatabase, prefabs, etc.).

### Archivos a crear

1. src/assets/FileWatcher.h
2. src/assets/FileWatcher.cpp
- Clase FileWatcher:
  - `FileWatcher(const std::string& watchRoot, float pollIntervalSeconds = 1.0f)`.
  - `void scan()` — recorre watchRoot recursivamente, computa hash de cada archivo, compara con snapshot anterior.
  - `struct FileChange { std::string relativePath; enum Type { Added, Modified, Deleted } type; }`.
  - `const std::vector<FileChange>& changes() const` — cambios detectados en ultimo scan.
  - `void reset()` — acepta estado actual como baseline.
- Almacenamiento interno:
  - `std::unordered_map<std::string, std::string> hashSnapshot_` — path → hash.
  - `std::vector<FileChange> pendingChanges_`.
- Usa `ImportManager::computeFileHash()` para hashing consistente.

### Archivos a modificar

1. src/assets/ImportManager.h
- Seccion: API publica.
- Cambios:
  - Agregar `bool reimportChanged(const std::vector<FileWatcher::FileChange>& changes, ...)` — reimport solo los archivos cambiados.

2. src/assets/ImportManager.cpp
- Seccion: nueva funcion.
- Cambios:
  - Implementar `reimportChanged()`: iterar changes, para cada Added/Modified llamar importAsset(), para Deleted remover record de AssetDatabase.

3. src/editor/EditorApp.h
- Seccion: miembros privados.
- Cambios:
  - Agregar `#include "assets/FileWatcher.h"`.
  - Agregar `FileWatcher fileWatcher_;`.
  - Agregar `bool autoReload_ = true;` — toggle.
  - Agregar `std::vector<FileWatcher::FileChange> deferredReloads_;` — encolados durante Play mode.

4. src/editor/EditorApp.cpp
- Seccion: init().
- Cambios:
  - Inicializar `fileWatcher_` con assetsRoot_.
  - `fileWatcher_.scan()` para establecer baseline.

- Seccion: run() main loop (al inicio de cada frame).
- Cambios:
  - Si `autoReload_` y modo Edit:
    - `fileWatcher_.scan()`.
    - Si hay changes: `importManager_.reimportChanged(changes, ...)`.
    - Log cada cambio: "[Hot-Reload] Reimported: relative/path".
    - Notificar a assetBrowserPanel_ para refrescar lista.
    - Si algun .json de gameplay/ cambio, recargar GameplayDatabase (si existe instancia).
  - Si modo Play y hay changes: encolar en `deferredReloads_`.

- Seccion: exitPlayMode().
- Cambios:
  - Despues de restaurar escena, procesar `deferredReloads_` encolados.
  - Limpiar `deferredReloads_`.

- Seccion: main menu bar.
- Cambios:
  - Agregar menu View → "Auto-Reload Assets" como MenuItem toggle (`&autoReload_`).
  - Agregar menu Assets → "Scan for Changes" para forzar scan manual.

### Criterio de aceptacion

- Modificar un archivo en assets/ se detecta automaticamente en ~1 segundo.
- Se re-importa solo lo cambiado (no full reimport).
- Log en Build Log indica que se re-importo.
- Asset Browser refleja archivos nuevos/eliminados.
- Durante Play mode, los cambios se encolan y se aplican al volver a Edit.
- Toggle "Auto-Reload" funciona para desactivar la feature.

---

## Tarea 6.3 - Herramientas de validacion de contenido (D28)

### Objetivo
Crear un validador que recorre la escena y reporta problemas potenciales: entidades fuera de bounds, prefabs rotos, datos invalidos. Panel en el editor para navegar a los problemas.

### Archivos a crear

1. src/editor/validation/ContentValidator.h
2. src/editor/validation/ContentValidator.cpp
- Struct `ValidationIssue`:
  - `enum class Severity { Warning, Error }`.
  - `Severity severity`.
  - `std::string message`.
  - `uint64_t entityId = 0` — 0 si no aplica a entidad especifica.
  - `int tileX = -1, tileY = -1` — (-1,-1) si no aplica a tile.
- Clase ContentValidator:
  - `std::vector<ValidationIssue> validate(const SceneData& scene, const World& world, const AssetDatabase& db)`.
  - Validaciones de mapa:
    - Entidades con posicion fuera de bounds del World (0..width, 0..height).
    - Player en tile no-walkable.
    - Tiles con tipo desconocido.
  - Validaciones de datos:
    - Entidad con prefabGuid pero GUID no encontrado en AssetDatabase.
    - Entidad sin TransformComponent.
    - Entidad con HealthComponent donde health > maxHealth o maxHealth <= 0.
    - Entidades con IDs duplicados.
    - StatsComponent con valores negativos.
  - Validaciones de gameplay:
    - Escena sin entidad de tipo Player.
    - Enemigo en posicion sin path A* posible al player (usar GridNav::findPath).
    - Mas de 1 Player en la escena.

3. src/editor/panels/ValidationPanel.h
4. src/editor/panels/ValidationPanel.cpp
- Clase ValidationPanel:
  - `void draw(const std::vector<ValidationIssue>& issues, uint64_t& selectedEntityId, float& camX, float& camY)`.
  - UI:
    - Lista scrollable con icono Warning/Error + mensaje.
    - Columnas: Severity | Message | Entity/Tile.
    - Click en issue: si tiene entityId, seleccionar entidad y centrar camara. Si tiene tileX/tileY, centrar camara en tile.
    - Header con contadores: "3 Errors, 5 Warnings".
    - Boton "Refresh" para re-ejecutar validacion.

### Archivos a modificar

1. src/editor/EditorApp.h
- Seccion: miembros privados.
- Cambios:
  - Agregar `#include "editor/validation/ContentValidator.h"`.
  - Agregar `#include "editor/panels/ValidationPanel.h"`.
  - Agregar `ContentValidator contentValidator_;`.
  - Agregar `ValidationPanel validationPanel_;`.
  - Agregar `std::vector<ValidationIssue> validationIssues_;`.
  - Agregar `bool showValidationPanel_ = false;`.

2. src/editor/EditorApp.cpp
- Seccion: main menu bar.
- Cambios:
  - Menu Tools → "Validate Scene" — ejecuta `contentValidator_.validate(...)` y guarda resultados.
  - Menu View → "Validation Panel" toggle.

- Seccion: toolbar.
- Cambios:
  - Boton con icono check/shield para Validate (al lado de Build & Run).

- Seccion: draw loop.
- Cambios:
  - Si `showValidationPanel_`: dibujar `validationPanel_.draw(validationIssues_, selectedEntityId_, camX_, camY_)`.

3. tests/CMakeLists.txt
- Cambios:
  - Agregar test_content_validation.cpp.

### Archivos a crear (tests)

1. tests/test_content_validation.cpp
- Test: escena valida (player presente, todos en bounds) → 0 errores.
- Test: entidad fuera de bounds → Error detectado.
- Test: escena sin player → Error detectado.
- Test: prefabGuid invalido → Warning detectado.
- Test: health > maxHealth → Warning detectado.

### Criterio de aceptacion

- El validador detecta al menos 8 tipos de problemas distintos.
- El panel muestra issues con severity y permite navegar a la entidad/tile.
- Una escena limpia retorna 0 issues.
- Tests pasan para escenas validas e invalidas.

---

## Tarea 6.4 - Paquete de build reproducible (D29)

### Objetivo
Generar un distribuible del juego y del editor como .app bundle en macOS, con assets embebidos, version y commit hash compilados en el binario.

### Archivos a crear

1. cmake/Packaging.cmake
- Modulo CMake incluido desde CMakeLists.txt principal.
- Incluir version del proyecto via `project(DashEngine VERSION 2.0.0)`.
- `configure_file()` para generar `VersionInfo.h` con:
  - `DASH_VERSION_MAJOR`, `DASH_VERSION_MINOR`, `DASH_VERSION_PATCH`.
  - `DASH_VERSION_STRING` ("2.0.0-alpha").
  - `DASH_GIT_COMMIT` (via `execute_process(git rev-parse --short HEAD)`).
  - `DASH_BUILD_DATE` (via `string(TIMESTAMP ...)`).
- Reglas de install():
  - `install(TARGETS DashEngine BUNDLE DESTINATION .)`.
  - `install(TARGETS IsometricRPG RUNTIME DESTINATION .)`.
  - `install(DIRECTORY assets/ DESTINATION DashEngine.app/Contents/Resources/assets)`.
  - `install(DIRECTORY library/ DESTINATION DashEngine.app/Contents/Resources/library)`.
  - `install(DIRECTORY scenes/ DESTINATION DashEngine.app/Contents/Resources/scenes)`.
- CPack config:
  - Generator: DragNDrop (macOS .dmg).
  - Bundle identifier desde packaging/Info.plist.

2. src/core/VersionInfo.h.in
- Template para configure_file:
  ```
  #pragma once
  #define DASH_VERSION_STRING "@PROJECT_VERSION@-alpha"
  #define DASH_GIT_COMMIT "@GIT_COMMIT@"
  #define DASH_BUILD_DATE "@BUILD_DATE@"
  ```

### Archivos a modificar

1. CMakeLists.txt
- Seccion: al inicio (despues de project()).
- Cambios:
  - `include(cmake/Packaging.cmake)`.
  - `target_compile_definitions(DashEngine PRIVATE DASH_VERSION="${PROJECT_VERSION}")`.

2. packaging/Info.plist
- Seccion: version strings.
- Cambios:
  - Actualizar `CFBundleShortVersionString` a "2.0.0".
  - Actualizar `CFBundleVersion` a "2.0.0".

3. packaging/install_app.sh
- Seccion: copia de recursos.
- Cambios:
  - Copiar assets/, library/, scenes/ al bundle Contents/Resources/.
  - Usar script como alternativa a CPack para builds rapidos.

4. src/editor/EditorApp.cpp
- Seccion: main menu bar.
- Cambios:
  - Menu Help → "About DashEngine":
    - Modal con version string, commit hash y fecha de build.
    - Leer de `DASH_VERSION_STRING`, `DASH_GIT_COMMIT`, `DASH_BUILD_DATE` (defines o VersionInfo.h).
  - Formatear como:
    ```
    DashEngine v2.0.0-alpha
    Commit: abc1234
    Built: 2026-04-15
    ```

### Criterio de aceptacion

- `cmake --build . --target package` genera un .dmg funcional.
- El .app bundle arranca y encuentra assets sin paths absolutos.
- La version y commit hash se muestran en el About dialog.
- El juego standalone (IsometricRPG) tambien se puede empaquetar.

---

## Tarea 6.5 - Tests de regresion y cierre de sprint (D30)

### Objetivo
Agregar tests que cubren las features nuevas del sprint, verificar que toda la suite previa sigue pasando, build release limpio y cerrar sprint.

### Archivos a crear

1. tests/test_event_system.cpp
- Test: subscribe + emit + flush → listener recibe evento.
- Test: emit sin flush → listener no recibe nada.
- Test: multiples suscriptores al mismo evento → todos reciben.
- Test: flush limpia la cola.
- Test: emit de tipo diferente → no afecta otros suscriptores.
- Test: clear() remueve todas las suscripciones.

2. tests/test_hot_reload.cpp
- Test: crear FileWatcher sobre directorio temporal.
- Test: agregar archivo → scan detecta FileChange::Added.
- Test: modificar archivo → scan detecta FileChange::Modified.
- Test: eliminar archivo → scan detecta FileChange::Deleted.
- Test: sin cambios → scan retorna 0 changes.
- Test: reset() establece nueva baseline.

### Archivos a modificar

1. tests/CMakeLists.txt
- Seccion: lista de test sources.
- Cambios:
  - Agregar test_event_system.cpp.
  - Agregar test_hot_reload.cpp.

2. README.md
- Seccion: Features y progreso.
- Cambios:
  - Agregar features de Sprint 2: componentes, eventos, prefabs, hot-reload, validacion, packaging.
  - Actualizar barras de progreso al 100%.
  - Actualizar version a v2.0-alpha.
  - Actualizar lista de tests (agregar nuevas suites).

3. SCALING_CHECKLIST.md
- Seccion: Sprint 2 items.
- Cambios:
  - Marcar todos los items de Sprint 2 como [x] completados.
  - Actualizar tabla de progreso: Sprint 2 = 100%.

4. planning/tasks/06_SPRINT_2_FEATURES_AVANZADAS.md
- Seccion: tablero y registro.
- Cambios:
  - Mover todos los items D21-D30 a Done.
  - Llenar horas reales en registro diario.

### Tareas de cierre

- Build completo en modo Release: `cmake -DCMAKE_BUILD_TYPE=Release ..` con 0 warnings.
- Ejecutar suite completa de tests: `ctest --output-on-failure`.
- Verificar que tests D01-D20 siguen pasando (regresion).
- Crear tag git: `git tag -a v2.0-alpha -m "Sprint 2: Component architecture, events, prefabs, hot-reload, validation, packaging"`.
- Verificar .app bundle arranca correctamente.

### Criterio de aceptacion

- Todas las suites de tests pasan (Sprint 1 + Sprint 2).
- Build release con 0 warnings en Clang.
- README refleja estado real del proyecto.
- Tag v2.0-alpha creado.

---

## Entregables de la semana

- Sistema de prefabs con overrides por instancia.
- Hot-reload de assets con FileWatcher.
- Panel de validacion de contenido con navegacion a problemas.
- Paquete .dmg reproducible con version embebida.
- Tests de regresion para eventos, hot-reload y prefabs.
- Cierre de sprint con tag v2.0-alpha.
