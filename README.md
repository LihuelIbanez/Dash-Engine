# Dash-Engine

> Isometric RPG engine and level editor written in C++17 with SDL2 and Dear ImGui.

[![Build Cross-Platform](https://github.com/ueno/Dash-Engine/actions/workflows/build-cross-platform.yml/badge.svg)](https://github.com/ueno/Dash-Engine/actions/workflows/build-cross-platform.yml)

---

## Estado del Proyecto

```
Overall  [██████████████████████████████]  100%  v14.0-alpha (Sprint 14)

Core Engine Foundation  [██████████████████████████████]  100%
Level Editor (DashEngine)  [██████████████████████████████]  100%
Asset Pipeline  [██████████████████████████████]  100%
Game Runtime    [██████████████████████████████]  100%
Production / QA [██████████████████████████████]  100%
SQLite Persistence [██████████████████████████████]  100%
Vulkan 3D Render   [██████████████████████████████]  100%  (D70-D76)
Mundo Fisico       [██████████████████████████████]  100%  (D80-D84)
Audio e Interfaz   [██████████████████████████████]  100%  (D89-D93)
Importacion 3D     [██████████████████████████████]  100%  (D97-D101)
Windows Port       [██████████████████████████████]  100%  (D106-D110)
Visual Polish      [██████████████████████████████]  100%  (Sprint 13)
Terrain 3D + Gravity [██████████████████████████████]  100%  (Sprint 14)
```

| Modulo | Estado | Detalle |
|---|---|---|
| Core Engine Foundation | ✅ Completo | Build system, SDL2, ImGui docking, renderer isometrico, generacion procedural, entidades, combate, profiling, EventDispatcher |
| Level Editor (DashEngine) | ✅ Completo | 10+ paneles dockables, Undo/Redo por comandos, Play Mode embebido en viewport, Asset Browser/Inspector, dirty state, ValidationPanel, About modal |
| Asset Pipeline | ✅ Completo | AssetDatabase con GUID v4, ImportManager con hash incremental, 6 importers (Scene, TileSet, GameplayConfig, Prefab, Sprite, Model), FileWatcher hot-reload |
| Game Runtime | ✅ Completo | 4 sistemas independientes, data-driven desde JSON, A* pathfinding, save/load versionado, EventDispatcher integrado |
| Production / QA | ✅ Completo | Suite ctest automatizada, ContentValidator, Packaging cmake, VersionInfo embebida |
| SQLite Persistence | ✅ Completo | Migraciones versionadas, repositorios de assets/scenes/savegame en SQLite, fallback JSON/hybrid/sqlite |
| Vulkan 3D Render | ✅ Completo | MoltenVK/macOS, GLFW window, DeviceContext, SwapchainContext, PipelineBuilder, shaders SPIR-V, MeshBuffers, cubo renderizado, FrameGraphLite |
| Mundo Fisico | ✅ Completo | PhysicsWorld con backend builtin, fixed timestep (60/120 FPS), AABB colliders, gravedad, TransformProxy, DebugPhysicsDraw, tests de determinismo |
| Audio e Interfaz | ✅ Completo | miniaudio backend, canales master/music/sfx, triggers de colision/ataque, input mapping persistente, config en SQLite |
| Importacion 3D | ✅ Completo | Assimp (.obj/.gltf/.glb), vertex extendido (pos+normal+uv), depth buffer, TextureLoader (stb_image), textured pipeline, AssetCache3D |
| Windows Port | ✅ Completo | CMake MSVC + CMakePresets.json, build_windows.ps1, Packaging cross-platform (DMG/ZIP+NSIS), CI/CD GitHub Actions dual |
| Visual Polish | ✅ Completo | Tema VS Code Dark+, status bar con modo/escena/FPS, separadores verticales, audit funcional completo |
| Terrain 3D + Gravity | ✅ Completo | TILE_SCALE configurable, smooth normals, paleta mejorada, AO, Blinn-Phong shaders, niebla, animacion de agua, terrain-following para personajes |

**Sprint 14 completado:** terreno 3D con visual polish avanzado (smooth shading, iluminacion Blinn-Phong, AO, niebla) y gravedad/terrain-following para personajes.

---

## Descripción General

**Dash-Engine** es un engine de RPG isométrico de acción con su propio editor de niveles integrado, inspirado en Diablo II para el gameplay y en Unity/Unreal para el flujo de trabajo del editor.

El proyecto compila tres binarios desde un mismo código base compartido:

| Binario | Descripción |
|---|---|
| `DashEngine` | Editor visual de niveles (Dear ImGui, estilo Unreal) |
| `VulkanBootstrap` | Runtime 3D Vulkan standalone (sin GUI de editor); usado por Build & Run del editor y por `dash vulkan` |
| `IsometricRPG` | Runtime 2D legado (SDL2 renderer) mantenido como modo alternativo vía `dash game`; no participa del pipeline Build & Run |

Los tres comparten la librería estática `game_core` que contiene el mundo, las entidades, pathfinding y profiling.

---

## Arquitectura

```
Dash-Engine/
├── src/
│   ├── core/               # Entity base, Character + Stats RPG, sistema de clases
│   │   └── profiling/      # Profiler singleton, ScopeTimer RAII, EMA smoothing
│   │   └── db/             # SqliteDb, SqliteStatement, SchemaManager, migraciones
│   ├── entities/           # Player (click-to-move Diablo), Enemy (FSM AI + A*)
│   ├── world/              # World: grid 64×64 tiles, generación procedural Perlin
│   ├── rendering/          # IsoRenderer (world→screen iso), drawDiamond, Font5x7
│   ├── game/
│   │   ├── Game.cpp        # Game loop, HUD Diablo II, input, modo embebido
│   │   ├── runtime/        # RuntimeContext, ISystem, SystemScheduler
│   │   ├── systems/        # MovementSystem, AISystem, CombatSystem, SpawnRewardSystem
│   │   ├── data/           # GameplayDatabase (player_classes, enemies, loot_tables JSON)
│   │   ├── nav/            # GridNav: A* 8-dir con costo por terreno
│   │   └── save/           # SaveGame JSON/SQLite + SaveVersioning (migración por versión)
│   ├── editor/
│   │   ├── EditorApp.cpp   # ImGui docking layout, 10+ paneles, Play Mode embebido
│   │   ├── SceneData.cpp   # Modelo de escena versionado (JSON + SQLite)
│   │   ├── commands/       # ICommand, CommandStack, PaintTile/PlaceEnemy/Erase
│   │   ├── playmode/       # PlaySession: snapshot & rollback de escena + world
│   │   └── panels/         # AssetBrowserPanel, AssetInspectorPanel
│   └── assets/
│       ├── AssetDatabase.cpp  # GUID v4, load/save asset_db.json, upsert/find/remove
│       ├── ImportManager.cpp  # Hash incremental, inferencia de tipo por extensión
│       └── importers/         # SceneImporter, TileSetImporter, GameplayConfigImporter
├── assets/                 # Archivos fuente de gameplay (JSON configs)
│   └── gameplay/           # player_classes.json, enemies.json, loot_tables.json
├── library/                # Cache de assets importados
├── saves/                  # Savegames (.json / .db)
├── scenes/                 # Escenas JSON y/o persistencia SQLite
├── tests/                  # Suite automatizada ctest (editor/runtime/db)
└── planning/               # Roadmap por semanas y sprint diario
```

### Dependencias

| Dependencia | Versión | Uso |
|---|---|---|
| SDL2 | sistema | Ventana, renderer 2D, input |
| Dear ImGui | docking branch | UI del editor (panels, dockspace) |
| nlohmann/json | v3.11.3 | Serialización de escenas, assets, savegames, gameplay data |
| SQLite3 | sistema | Persistencia de proyecto, assets, escenas, savegames y metadatos |

---

## Funcionalidades Implementadas

### Motor de Renderizado Isométrico
- Proyección world→screen configurable (`TILE_W=64`, `TILE_H=32`)
- Grid de 64×64 tiles con 9 tipos de terreno: Deep Water, Water, Sand, Grass, Forest, Dirt, Stone, Mountain, Snow
- Generación procedural por semilla con Perlin noise (elevación + humedad + detalle)
- Dibujado en painter order (arriba→abajo) con `SDL_RenderGeometry`
- Picking inverso mouse→world en ambos binarios
- Pixel font embebida `Font5x7` (bitmapped 5×7, sin dependencia de SDL_ttf)

### Sistema de Entidades y Combate RPG
- `Entity` → `Character` → `Player` / `Enemy`
- **4 clases de personaje:** Warrior, Mage, Rogue, Archer — cada una con stats distintos (attack, defense, magicAttack, speed, critChance)
- Sistema de stats completo: ataque base, defensa, magia, velocidad, crítico, nivel, XP, XP al siguiente nivel
- Combate melee con cooldowns, roll de daño, críticos, reducción por defensa
- Level-up automático con ganancia de exp al eliminar enemigos
- Stats cargados desde JSON (data-driven via `GameplayDatabase`)

### Player (Estilo Diablo II)
- Movimiento click-to-move con target en mundo isométrico
- Ataque click izquierdo (melee) con cooldown por clase
- Cámara centrada en el jugador
- HUD estilo Diablo II: orb de vida (rojo) y orb de maná (azul), barra XP, belt de items, cooldown bar

### Enemy AI (FSM + A* Pathfinding)
- Estados: **Idle → Patrol → Chase → Attack**
- Detección de jugador por radio configurable (`detectionRadius`)
- Radio de ataque (`attackRadius`)
- **Navegación A* 8-direccional** con costo por tipo de terreno (Sand 1.3×, Forest 1.5×, Mountain 2.0×)
- Path refresh cada 0.5s, prevención de corner-cutting
- Constructor data-driven desde `EnemyData` JSON
- Recompensa en XP al morir (`expReward`)

### Runtime por Sistemas
- **SystemScheduler** ejecuta sistemas en orden cada frame
- **MovementSystem** — movimiento de player y enemigos
- **AISystem** — FSM + pathfinding A* de enemigos
- **CombatSystem** — resolución de daño, muerte, cooldowns
- **SpawnRewardSystem** — drops de XP, respawn
- `RuntimeContext` comparte punteros a world, player, enemies, score

### Data-Driven Gameplay
- `GameplayDatabase` carga y valida `player_classes.json`, `enemies.json`, `loot_tables.json`
- Stats de player y enemigos definidos en JSON, no hardcodeados
- Build & Run lanza la escena editada (scene export + argv pass-through)

### Editor DashEngine (Level Editor)
- **Layout dockable** estilo Unity con 10+ paneles configurables:
  - Scene Hierarchy: lista de entidades con EntityId estable
  - Viewport: render en texture target con juego embebido en Play mode
  - Properties: inspector de entidad seleccionada
  - Tile Palette: selector de tipo de tile
  - Asset Browser: tabla filtrable de assets con GUID, tipo, hash
  - Asset Inspector: metadata completa + botón Reimport
  - File Browser: explorador de `src/`, abre archivos en editor
  - File Editor: editor de código con undo/redo por archivo
  - Build Log: registro de acciones y errores
  - Performance: FPS, frame timing, tabla de subsistemas (last/avg/peak ms)
  - Toolbar: Build & Run, Play/Stop, herramientas de edición
- **Undo/Redo global:** `ICommand` + `CommandStack` (Cmd+Z / Cmd+Shift+Z)
  - Comandos: PaintTileCommand, PlaceEnemyCommand, EraseCommand
- **Play Mode embebido:** ejecuta el juego dentro del viewport del editor
  - Click izquierdo = mover/atacar, click derecho = atacar en lugar
  - Snapshot automático al entrar, restauración completa al salir
- **Herramientas:** Select, Paint Tile, Place Enemy, Erase
- **Escenas:** New, Open (dialog), Save / Save As (JSON versionado)
- **Build & Run:** compila y lanza `VulkanBootstrap` como proceso externo
- **Dirty state:** modal "Unsaved Changes" (Save/Discard/Cancel), título con `*`
- Tema oscuro estilo Unreal Engine, cursors personalizados por herramienta

### Asset Pipeline
- **AssetDatabase** con GUID v4 persistente (`asset_db.json`)
- **ImportManager** con detección de cambios por hash SHA, reimport incremental
- **3 importers:** SceneImporter, TileSetImporter, GameplayConfigImporter
- Carpetas `assets/` (fuente) y `library/` (cache importado)
- Asset Browser con filtro por tipo + Asset Inspector con metadata completa

### Save/Load de Partida
- Formato JSON versionado (`SaveGame::save()` / `load()`)
- Captura estado completo: world seed, score, player (posición, stats, clase, nivel, XP), todos los enemigos activos
- Migración automática de versiones viejas (`SaveVersioning`)
- F5 = Quick Save, F9 = Quick Load

### Profiling y Observabilidad
- `Profiler` singleton con `ScopeTimer` RAII
- EMA smoothing (α=0.05), tracking de picos, logging de spikes >33.3ms
- Panel "Performance" en editor: FPS, frame time (last/avg/peak), tabla por subsistema
- Instrumentado: Game::update(), Game::render()

### Testing Automatizado
- Suite automatizada en `ctest` (editor, runtime y capa SQLite): **26/26 tests en verde** (última verificación: 2026-08-27).
- Incluye cobertura para:
  - Serialización de escenas y comandos undo/redo.
  - Sistemas runtime (pathfinding, save/load, gameplay database).
  - Pipeline de assets (hot-reload, validación de contenido).
  - Migración SQLite (schema, repositorios, migrador, fallback y rendimiento).
- Integrados en CMake (`-DBUILD_TESTING=ON` + `ctest`).

### Formato de Escena (JSON)
```json
{
  "name": "Default Scene",
  "scene_version": 1,
  "worldSeed": 12345,
  "nextEntityId": 3,
  "tileOverrides": [],
  "entities": [
    {"id": 1, "type": "Player", "name": "Hero", "x": 32.0, "y": 32.0, "class": "Warrior"},
    {"id": 2, "type": "Enemy",  "name": "Skeleton", "x": 36.0, "y": 35.0}
  ]
}
```

---

## Backlog Futuro

- [x] Sprint 8 (D70-D76): bootstrap Vulkan en macOS (MoltenVK), swapchain, pipeline y cubo base.
- [x] Sprint 9 (D80-D84): integración de físicas 3D y pruebas deterministas.
- [ ] Sprint 10 (D89-D93): audio espacial, triggers por eventos, input mapping y persistencia.
- [ ] Sprint 11 (D97-D101): importación 3D (.obj/.gltf), texturas y cache de assets.
- [ ] Sprint 12 (D106-D110): portabilidad Windows (80% — falta CI/CD), empaquetado final.

---

## Build

### Requisitos

| Dependencia | Instalación (macOS) | Instalación (Windows) |
|---|---|---|
| CMake ≥ 3.16 | `brew install cmake` | Visual Studio / `winget install cmake` |
| SDL2 | `brew install sdl2` | vcpkg (`C:\vcpkg`) |
| SQLite3 | incluido en macOS SDK | vcpkg |
| GLFW | `brew install glfw` | vcpkg |
| Vulkan (opcional) | `brew install molten-vk vulkan-headers vulkan-loader` | Vulkan SDK |

### CLI `dash` (recomendado)

El proyecto incluye un CLI multiplataforma que simplifica compilación y ejecución:

| Archivo | Plataforma |
|---|---|
| `dash.sh` | macOS / Linux |
| `dash.ps1` | Windows |

**Configurar el alias (una sola vez):**

```bash
# macOS / Linux
echo 'alias dash="/ruta/a/Dash-Engine/dash.sh"' >> ~/.zshrc
source ~/.zshrc

# Windows (PowerShell)
Add-Content $PROFILE 'function dash { powershell -ExecutionPolicy Bypass -File "C:\ruta\a\Dash-Engine\dash.ps1" @args }'
. $PROFILE
```

**Comandos disponibles:**

| Comando | Descripción |
|---|---|
| `dash build` | Compila todo (editor + runtime + vulkan) |
| `dash editor` | Compila y abre el editor DashEngine |
| `dash run` | Abre el editor sin recompilar |
| `dash game` | Abre el runtime IsometricRPG sin recompilar |
| `dash vulkan` | Compila y abre VulkanBootstrap |
| `dash clean` | Elimina el directorio build/ |
| `dash update` | git pull + recompila |
| `dash test` | Compila y ejecuta los tests |
| `dash config` | Solo configura CMake (sin compilar) |
| `dash help` | Muestra esta ayuda |

### Build manual

```bash
# Requiere: CMake ≥ 3.16, SDL2, compilador C++17
mkdir build && cd build
cmake ..
make -j$(nproc)

# Editor
./DashEngine

# Runtime standalone
./VulkanBootstrap

# Tests
cmake .. -DBUILD_TESTING=ON && make -j$(nproc)
ctest --output-on-failure
```

---

## Versión Actual

**v14.0-alpha** — Sprint 14 completado (Terreno 3D y Gravedad). Incluye TILE_SCALE configurable, smooth normals per-vertex, paleta de biomas mejorada con blending, ambient occlusion, shaders Vulkan con Blinn-Phong/hemispheric ambient/niebla/animacion de agua, y terrain-following para Player y Enemy via heightmap sampling.

**v13.0-alpha** — Sprint 13 completado (Visual Polish). Tema VS Code Dark+ completo, status bar, separadores verticales, audit funcional de todos los controles.

**v7.0-alpha** — Sprint 7 completado (SQLite Cutover). Incluye base de datos SQLite como backend principal para proyecto/editor/runtime, migraciones versionadas, migrador de datos con fallback y validaciones de integridad.

**v6.0-alpha** — Sprint 6 completado (SQLite Foundation). Incluye wrapper SQLite, statements, schema manager, repositorios iniciales y coexistencia JSON/hybrid/sqlite.

**v5.0-alpha** — Sprint 5 completado (Project Bundles). Incluye `ProjectManifest`, `ProjectManager`, rutas por proyecto activo, separación CMake por bundles (`src/game` / `src/editor`), `GameBuildPipeline` para exportar bundles y aislamiento de runtime con `SpriteRenderer` sin dependencia de `src/editor`.

**v2.0-alpha** — Sprint 2 completado (30/30 días). Sistema de componentes (Transform, Health, Stats, AI, Combat, Render, Inventory), EventDispatcher tipado, sistema de prefabs con overrides por instancia, hot-reload de assets con FileWatcher, panel de validación de contenido (10+ checks), packaging reproducible con VersionInfo embebida (versión, commit, fecha), About modal en editor.

**v1.0-alpha** — Sprint de escalado completado (20/20 días). Editor con undo/redo, asset pipeline con GUID, Play Mode embebido en viewport, runtime por sistemas con A* pathfinding y datos JSON, save/load versionado, profiler con panel en editor.

