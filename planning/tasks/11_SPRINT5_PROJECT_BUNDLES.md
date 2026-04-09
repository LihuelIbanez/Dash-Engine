# Sprint 5 — Project Bundles (Game vs Editor)

**Objetivo:** Separar el motor en dos productos independientes y distribuibles:
- **Game Bundle** (`MyGame.app` / `MyGame`) — contiene sólo el runtime del juego + assets del proyecto activo.
- **Editor Bundle** (`DashEngine.app` / `DashEngine`) — entorno completo de creación, con el runtime embebido para Play mode.

Un **proyecto** (`.dashproject`) es la unidad de trabajo del editor: especifica qué assets, escenas y configuración forman el juego. El editor puede abrir, crear y exportar proyectos. El juego sólo necesita el bundle compilado + la carpeta `assets/` de ese proyecto.

**Resultado esperado al completar el sprint:**
1. `DashEngine` abre/crea proyectos `.dashproject` y sabe dónde están sus assets/scenes.
2. `IsometricRPG` (o cualquier game bundle) corre de forma completamente autónoma sin dependencias del editor.
3. El editor puede "Build Game" → genera un bundle listo para distribuir.
4. Los paths de assets son relativos al proyecto, no al binario del editor.
5. Tests de integración validan la separación (ningún header de editor en game core).

---

## Tablero del Sprint

### To Do

(vacío)

### Doing

(vacío)

### Done

- D46 — ProjectManifest: modelo de datos + serialización
- D47 — ProjectManager: abrir / crear / cerrar proyectos en el editor
- D48 — Migrar AppPaths a paths relativos al proyecto
- D49 — Separar EditorMain / GameMain con sus propios CMake targets
- D50 — Build Game Pipeline: exportar bundle completo del proyecto
- D51 — Launcher / Recent Projects UI en el editor
- D52 — Aislamiento de headers editor/game (sin leakage)
- D53 — Tests de integración de separación
- D54 — Documentación y cierre

---

## D46 — ProjectManifest: modelo de datos + serialización

- **ID:** D46
- **Meta:** Definir el archivo `.dashproject` (JSON) que describe un proyecto completo. Crear `ProjectManifest.h/.cpp` en `src/editor/project/`.
- **Horas estimadas:** 4h
- **Dependencias:** ninguna

### Estructura del archivo `.dashproject`

```json
{
  "formatVersion": 1,
  "name": "MyGame",
  "defaultScene": "scenes/default.json",
  "assetsDir": "assets",
  "scenesDir": "scenes",
  "libraryDir": ".library",
  "buildOutputDir": "build_output",
  "gameConfig": {
    "screenWidth": 1280,
    "screenHeight": 720,
    "targetFps": 60
  }
}
```

### Archivos a crear

**`src/editor/project/ProjectManifest.h`**
```cpp
#pragma once
#include <string>

struct ProjectConfig {
    int screenWidth  = 1280;
    int screenHeight = 720;
    int targetFps    = 60;
};

struct ProjectManifest {
    static constexpr int kFormatVersion = 1;

    int           formatVersion  = kFormatVersion;
    std::string   name           = "Untitled";
    std::string   defaultScene   = "scenes/default.json";
    std::string   assetsDir      = "assets";
    std::string   scenesDir      = "scenes";
    std::string   libraryDir     = ".library";
    std::string   buildOutputDir = "build_output";
    ProjectConfig gameConfig;

    // Path resuelto al cargar (directorio que contiene el .dashproject)
    std::string   projectRoot;     // se rellena en tiempo de ejecución, no se serializa

    bool saveToFile(const std::string& path) const;
    bool loadFromFile(const std::string& path);

    // Helpers para obtener paths absolutos
    std::string absoluteAssetsDir()  const;
    std::string absoluteScenesDir()  const;
    std::string absoluteLibraryDir() const;
    std::string absoluteBuildDir()   const;
    std::string absoluteDefaultScene() const;
};
```

**`src/editor/project/ProjectManifest.cpp`**
- Serialización/deserialización con `nlohmann::json`.
- `loadFromFile` rellena `projectRoot = fs::path(path).parent_path().string()`.
- Helpers `absolute*` usan `fs::path(projectRoot) / <rel_dir>`.
- Validación: `assetsDir` y `scenesDir` no vacíos, `formatVersion <= kFormatVersion`.

### Criterio de aceptación
- `ProjectManifest` se serializa y deserializa sin pérdida de datos.
- Helpers `absoluteAssetsDir()` devuelven el path correcto dado `projectRoot`.
- Test unitario: crear, guardar, recargar y comparar campos.

---

## D47 — ProjectManager: abrir / crear / cerrar proyectos

- **ID:** D47
- **Meta:** Clase `ProjectManager` que mantiene el proyecto activo en el editor. `EditorApp` lo usa en lugar de las rutas hardcodeadas.
- **Horas estimadas:** 5h
- **Dependencias:** D46

### Archivos a crear / modificar

**`src/editor/project/ProjectManager.h`**
```cpp
#pragma once
#include "ProjectManifest.h"
#include <string>
#include <vector>
#include <functional>

class ProjectManager {
public:
    // Crea un nuevo proyecto en la ruta dadA.
    bool createProject(const std::string& dirPath, const std::string& name);

    // Abre un .dashproject existente.
    bool openProject(const std::string& manifestPath);

    // Cierra el proyecto activo (limpia estado).
    void closeProject();

    bool hasActiveProject() const { return active_; }
    const ProjectManifest& manifest() const { return manifest_; }

    // Lista de proyectos abiertos recientemente (~10 últimos).
    const std::vector<std::string>& recentProjects() const { return recentPaths_; }
    void addRecent(const std::string& path);
    void saveRecents() const;   // persiste en AppPaths::getConfigDir()/recents.json
    void loadRecents();

private:
    ProjectManifest      manifest_;
    bool                 active_ = false;
    std::vector<std::string> recentPaths_;
};
```

**`src/editor/EditorApp.h/.cpp`** — modificar:
- Añadir `ProjectManager projectManager_` como miembro.
- Reemplazar `assetsRoot_`, `libraryRoot_`, `scenesDir_` por wrappers sobre `projectManager_.manifest().absoluteAssetsDir()` etc.
- Mantener compatibilidad backward: si no hay proyecto activo, usar las rutas antiguas (modo compatibilidad).

### Criterio de aceptación
- `EditorApp` puede leer assets/scenes desde el directorio del proyecto, no del binario.
- Los paneles `AssetBrowserPanel`, `SpriteEditorPanel` y `drawPropertiesPanel` obtienen sus paths del `ProjectManager`.

---

## D48 — Migrar AppPaths a paths relativos al proyecto

- **ID:** D48
- **Meta:** Actualizar `AppPaths` para devolver paths del proyecto activo cuando existe uno, y conservar el comportamiento de bundle macOS/Linux como fallback.
- **Horas estimadas:** 3h
- **Dependencias:** D47

### Cambios en `src/editor/AppPaths.h`

```cpp
class AppPaths {
public:
    // Paths independientes del proyecto (instalación del editor).
    static std::string getEditorResourcesDir();   // fonts, icons, etc.
    static std::string getSavesDir();

    // Paths que dependen del proyecto activo.
    // Si no hay proyecto activo, retornan el comportamiento anterior.
    static std::string getAssetsDir();            // == project.absoluteAssetsDir() si hay proyecto
    static std::string getScenesDir();
    static std::string getLibraryDir();
    static std::string getBuildOutputDir();

    // Inyectar el proyecto activo (llamar desde ProjectManager::openProject).
    static void setActiveProject(const ProjectManifest* p);  // nullptr = sin proyecto

private:
    static const ProjectManifest* activeProject_;
};
```

### Casos de uso a cubrir
1. Editor sin proyecto: `getAssetsDir()` devuelve `<bundle>/resources/assets` (igual que ahora).
2. Editor con proyecto: `getAssetsDir()` devuelve `projectRoot/assets`.
3. Binario del juego (sin editor): AppPaths sencillo, `getResourcesDir()` == directorio del ejecutable.

### Criterio de aceptación
- Todos los sitios que usaban `AppPaths::getResourcesDir() + "/assets"` pasan a usar `AppPaths::getAssetsDir()`.
- Build sin warnings tras refactor.
- `test_scene_serialization` y `test_hot_reload` siguen pasando.

---

## D49 — CMake: targets separados con sus CMakeLists propios

- **ID:** D49
- **Meta:** Dividir el `CMakeLists.txt` raíz en tres: `game/CMakeLists.txt`, `editor/CMakeLists.txt` y el raíz como orquestador. Introduce el target `game_runtime` como librería estática sin dependencias del editor.
- **Horas estimadas:** 5h
- **Dependencias:** D48

### Estructura objetivo

```
CMakeLists.txt            ← orquestador (find_package, FetchContent, subdirs)
cmake/
  deps.cmake              ← SDL2, ImGui, json, stb descargados aquí
  GameBundle.cmake        ← función helper para generar un game bundle
src/
  game/
    CMakeLists.txt        ← target game_runtime (lib estática), target IsometricRPG (exe)
  editor/
    CMakeLists.txt        ← target DashEngine (exe), depende de game_runtime
```

### Reglas de separación

**`game_runtime` (lib estática) sólo puede incluir:**
- `src/core/**`
- `src/entities/**`
- `src/world/**`
- `src/rendering/**`
- `src/game/**`
- Headers de SDL2, nlohmann/json

**`game_runtime` NO puede incluir:**
- Nada de `src/editor/**`
- `imgui.h` ni ningún header de ImGui
- `stb_image_write.h`

**`DashEngine` puede incluir todo** (editor + game_runtime + ImGui + stb).

### Tarea de CI (manual)
- Añadir comentario en el tope de cada `.cpp` de `game_runtime` que compile con `-Wno-unused`:
  `// [GAME_RUNTIME] Archivo de runtime. Sin dependencias del editor.`

### Criterio de aceptación
- `cmake --build build --target game_runtime` compila sin errores.
- `cmake --build build --target IsometricRPG` compila sin incluir ningún header de `src/editor/`.
- `cmake --build build --target DashEngine` compila completo.

---

## D50 — Build Game Pipeline: exportar bundle completo

- **ID:** D50
- **Meta:** El editor puede exportar un directorio auto-contenido con el ejecutable del juego + assets del proyecto activo (listo para distribución).
- **Horas estimadas:** 6h
- **Dependencias:** D49

### Flujo de exportación

```
Editor → "Build Game" → GameBuildPipeline::build(manifest, outputDir)
  1. Compilar target IsometricRPG si está desactualizado.
  2. Copiar binario a outputDir/bin/.
  3. Copiar manifest.assetsDir/ → outputDir/assets/.
  4. Copiar manifest.scenesDir/ → outputDir/scenes/.
  5. Copiar assets/fonts/ y assets/data/ necesarios.
  6. En macOS: crear .app bundle (Info.plist + Contents/MacOS/).
  7. Escribir outputDir/project.json con config del juego.
  8. Log de resultado en Build Log del editor.
```

### Archivos a crear

**`src/editor/project/GameBuildPipeline.h/.cpp`**
```cpp
class GameBuildPipeline {
public:
    struct BuildResult {
        bool        success     = false;
        std::string outputPath;
        std::vector<std::string> log;
    };
    static BuildResult build(const ProjectManifest& manifest,
                             const std::string& outputDir);
};
```

### Archivos a modificar

**`src/editor/EditorApp.cpp`** — sección `Build & Run`:
- Añadir botón "Export Game Bundle" en el toolbar que abre modal de directorio de salida.
- Llamar a `GameBuildPipeline::build(...)` y volcar `.log` al `Build Log`.

### Criterio de aceptación
- El directorio exportado contiene el ejecutable y assets necesarios.
- El ejecutable exportado arranca sin el editor presente.
- En macOS genera `.app` con `Info.plist` correcto.

---

## D51 — Launcher / Recent Projects UI

- **ID:** D51
- **Meta:** Al arrancar DashEngine sin argumentos, mostrar un panel de "Welcome" con opciones: New Project, Open Project, Recent Projects. Cerrar el panel cuando se carga un proyecto.
- **Horas estimadas:** 4h
- **Dependencias:** D47

### Comportamiento esperado

```
DashEngine arranca →
  si hay argumentos CLI: abrir el .dashproject pasado como primer argumento
  si no: mostrar WelcomePanel con:
    - Botón "New Project" → modal con nombre + directorio
    - Botón "Open Project" → diálogo de archivo (*.dashproject)
    - Lista de "Recent Projects" (hasta 8) → click abre directamente
```

### Archivos a crear

**`src/editor/panels/WelcomePanel.h/.cpp`**
- Modal ImGui centrado en la pantalla.
- Campos: `nameInput_[128]`, `dirInput_[512]`, botón Browse, botón Create/Open.
- Listas de recientes con path acortado + timestamp de último acceso.
- `bool isOpen = true` — se cierra al seleccionar un proyecto.

**`src/editor/EditorMain.cpp`** — modificar:
- Leer `argv[1]` si existe; pasarlo a `EditorApp::init(const std::string& projectPath)`.
- Si no hay argumento, `EditorApp` inicia en modo "no project" y muestra el WelcomePanel.

### Criterio de aceptación
- Primera vez que se ejecuta DashEngine: aparece WelcomePanel.
- Crear un nuevo proyecto genera el directorio con `.dashproject` y carpetas `assets/`, `scenes/`, `.library/`.
- Abrir proyecto reciente lo carga sin pasar por el diálogo.

---

## D52 — Aislamiento headers editor/game

- **ID:** D52
- **Meta:** Garantizar en tiempo de compilación que `game_runtime` no depende de headers del editor. Implementar check estático vía CMake.
- **Horas estimadas:** 3h
- **Dependencias:** D49

### Cambios

**`cmake/CheckGameRuntimeIsolation.cmake`**
- Script que lista todos los `.cpp` del target `game_runtime` y aplica grep sobre sus `#include` para detectar cualquier inclusión de `src/editor/`, `imgui`, `stb_image_write`.
- Se ejecuta como custom target `check_isolation` en CI.

**Regla de compilación:**
```cmake
# En src/game/CMakeLists.txt
if(ENABLE_ISOLATION_CHECK)
    target_compile_definitions(game_runtime PRIVATE GAME_RUNTIME_ONLY)
endif()
```
- Cualquier sitio en `game/` que incluya headers de editor queda protegido con:
  ```cpp
  #ifndef GAME_RUNTIME_ONLY
  #include "../editor/TextureCache.h"
  #endif
  ```
  > Nota: `Game.cpp` actualmente usa `TextureCache` para dibujar sprites —
  > en D52 esto se mueve a un `SpriteRenderer` liviano autónomo en `src/game/rendering/`.

**`src/game/rendering/SpriteRenderer.h/.cpp`** — nuevo módulo:
```cpp
class SpriteRenderer {
public:
    void init(SDL_Renderer* r, const std::string& assetsDir);
    bool draw(SDL_Renderer* r, const std::string& spriteName,
              float screenX, float screenY, float pivotX, float pivotY);
    void clearCache();
private:
    std::unordered_map<std::string, SDL_Texture*> cache_;
    std::string assetsDir_;
    // usa stb_image.h (sólo lectura, no escritura)
};
```
- `stb_image.h` se copia / incluye en `src/game/rendering/` también (sólo para lectura).
- Elimina la dependencia de `Game.cpp` sobre `src/editor/TextureCache.h`.

### Criterio de aceptación
- `cmake --build build --target game_runtime -DENABLE_ISOLATION_CHECK=ON` compila sin warnings de aislamiento.
- `check_isolation` target no reporta inclusiones de editor en runtime.
- `test_sprite_editor` y `test_hot_reload` siguen pasando (14/14).

---

## D53 — Tests de integración de separación

- **ID:** D53
- **Meta:** Añadir tests que validan que el flujo proyecto → exportar → ejecutar funciona end-to-end.
- **Horas estimadas:** 4h
- **Dependencias:** D50, D52

### Tests a añadir en `tests/`

| Test | Descripción |
|------|-------------|
| `test_project_manifest` | Crear, guardar, recargar ProjectManifest. Validar paths absolutos. |
| `test_project_manager` | Crear proyecto en directorio temporal, verificar estructura de carpetas. |
| `test_game_build_pipeline` | Mock del pipeline: verificar que se copian los assets y se genera project.json. |
| `test_runtime_isolation` | Compilar un `.cpp` que sólo incluye `src/game/**` y verifica que no hay símbolos de editor. |

### Total tests esperado

Actualmente: 14 tests. Post-D53: ≥ 18 tests.

### Criterio de aceptación
- `ctest --output-on-failure` = 100% pass con los nuevos tests.
- `test_project_manifest` valida roundtrip serialización/deserialización.

---

## D54 — Documentación y cierre

- **ID:** D54
- **Meta:** Actualizar README, SCALING_CHECKLIST y acceptance checklist. Tag `v5.0-alpha`.
- **Horas estimadas:** 2h
- **Dependencias:** D53

### Tareas

1. **`README.md`** — actualizar sección "Estructura del proyecto" con la nueva jerarquía de carpetas y explicar la distinción editor/game.
2. **`SCALING_CHECKLIST.md`** — marcar Sprint 5 en progreso → completado.
3. **`planning/tasks/99_ACCEPTANCE_CHECKLIST.md`** — añadir sección "Sprint 5 — Project Bundles".
4. **`planning/tasks/00_INDEX.md`** — registrar Sprint 5 como completado.
5. `git tag v5.0-alpha` sobre el commit de cierre.

---

## Dependencias del sprint

```
D46 ──→ D47 ──→ D48 ──→ D49 ──→ D50 ──→ D53 ──→ D54
                          └──→ D52 ──→ D53
         └──→ D51 (paralelo con D48+)
```

## Estimación total

| ID  | Tarea                               | Horas |
|-----|-------------------------------------|-------|
| D46 | ProjectManifest                     | 4h    |
| D47 | ProjectManager                      | 5h    |
| D48 | AppPaths migración                  | 3h    |
| D49 | CMake separación                    | 5h    |
| D50 | Build Game Pipeline                 | 6h    |
| D51 | Launcher / Welcome UI               | 4h    |
| D52 | Aislamiento headers                 | 3h    |
| D53 | Tests de integración                | 4h    |
| D54 | Documentación y cierre              | 2h    |
| **Total** |                             | **36h** |

---

## Estructura de directorios objetivo (post-sprint)

```
Dash-Engine/                        ← repositorio / workspace del editor
├── CMakeLists.txt                  ← orquestador
├── cmake/
│   ├── deps.cmake
│   └── GameBundle.cmake
├── src/
│   ├── core/                       ← game_core lib (sin deps del editor)
│   ├── entities/
│   ├── world/
│   ├── rendering/
│   ├── game/                       ← game_runtime lib + IsometricRPG exe
│   │   ├── CMakeLists.txt
│   │   └── rendering/
│   │       └── SpriteRenderer.h/.cpp  ← NUEVO, reemplaza uso de TextureCache
│   ├── editor/                     ← sólo DashEngine exe
│   │   ├── CMakeLists.txt
│   │   └── project/                ← NUEVO
│   │       ├── ProjectManifest.h/.cpp
│   │       ├── ProjectManager.h/.cpp
│   │       ├── GameBuildPipeline.h/.cpp
│   │       └── WelcomePanel.h/.cpp (en panels/)
│   └── assets/
├── tests/
│   ├── test_project_manifest.cpp   ← NUEVO
│   ├── test_project_manager.cpp    ← NUEVO
│   ├── test_game_build_pipeline.cpp ← NUEVO
│   └── test_runtime_isolation.cpp  ← NUEVO
└── planning/
    └── tasks/
        └── 11_SPRINT5_PROJECT_BUNDLES.md  ← este archivo

Proyectos de usuario (fuera del repo):
my-rpg/
├── my-rpg.dashproject
├── assets/
│   ├── sprites/
│   ├── scenes/
│   └── data/
├── scenes/
├── .library/                       ← generado por el editor (gitignore)
└── build_output/                   ← bundles exportados
    └── MyGame.app (macOS)
```

---

## Notas de diseño

### ¿Por qué `.dashproject` en lugar de carpetas fijas?
- Permite tener múltiples proyectos en el mismo sistema sin conflicto de paths.
- El editor puede abrir cualquier proyecto desde cualquier ubicación.
- Facilita el workflow colaborativo: el `.dashproject` va al control de versiones, `.library/` no.

### Estrategia de backward compatibility
- Si el editor arranca sin argumento y sin un `.dashproject` reciente, sigue funcionando con los paths hardcodeados actuales (modo legacy).  
- El modo legacy muestra un banner "No active project — using legacy paths" en el Build Log.

### Game bundle standalone
- El binario del juego exportado no necesita el editor en la misma máquina.
- `SpriteRenderer` en el game bundle usa `stb_image` (read-only) para cargar PNGs: sin dependencia de `TextureCache` del editor ni de ImGui.
- La configuración del juego (`screenWidth`, `targetFps`, etc.) se lee desde `project.json` en el directorio del bundle.
