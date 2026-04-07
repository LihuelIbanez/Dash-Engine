# Dash-Engine

> Isometric RPG engine and level editor written in C++17 with SDL2 and Dear ImGui.

---

## Estado del Proyecto

```
Overall  [████████░░░░░░░░░░░░░░░░░░░░░░]  28%

Core Engine Foundation  [█████████████████████░░░░░░░░░]  70%
Level Editor (DashEngine)  [█████████████░░░░░░░░░░░░░░░░░]  45%
Asset Pipeline  [██░░░░░░░░░░░░░░░░░░░░░░░░░░░░]   5%
Game Runtime    [██████████░░░░░░░░░░░░░░░░░░░░]  35%
Production / QA [██░░░░░░░░░░░░░░░░░░░░░░░░░░░░]   5%
```

| Módulo | Hecho | Total estimado | % |
|---|---|---|---|
| Core Engine Foundation | 7 / 10 | Build system, SDL2, ImGui, renderer isom., procedural gen., entidades, combate | 70% |
| Level Editor | 9 / 20 | UI panels, tools, JSON scene, File Browser/Editor; faltan: Undo/Redo comandos, Play Mode, EntityId estable, Asset DB | 45% |
| Asset Pipeline | 1 / 20 | Solo carga/guarda JSON básico; falta: GUID, asset DB, importers, hot-reload | 5% |
| Game Runtime | 7 / 20 | Game loop, HUD Diablo, FSM AI, combate, stats/levels; faltan: sistemas independientes, pathfinding A*, data-driven, save/load | 35% |
| Production / QA | 1 / 20 | Solo smoke-test manual; faltan: tests de determinismo, profiling, tests undo/redo | 5% |

**Sprint activo:** 0 / 20 días completados — 20 tareas pendientes × 6h ≈ 120h de trabajo restante.

---

## Descripción General

**Dash-Engine** es un engine de RPG isométrico de acción con su propio editor de niveles integrado, inspirado en Diablo II para el gameplay y en Unity/Unreal para el flujo de trabajo del editor.

El proyecto compila dos binarios desde un mismo código base compartido:

| Binario | Descripción |
|---|---|
| `DashEngine` | Editor visual de niveles (Dear ImGui, estilo Unreal) |
| `IsometricRPG` | Ejecutable del juego (SDL2, sin GUI de editor) |

Ambos comparten la librería estática `game_core` que contiene el mundo, las entidades y el sistema de combate.

---

## Arquitectura

```
Dash-Engine/
├── src/
│   ├── core/           # Entity base, Character + Stats RPG, sistema de clases
│   ├── entities/       # Player (click-to-move Diablo), Enemy (FSM AI)
│   ├── world/          # World: grid 64×64 tiles, generación procedural por seed
│   ├── rendering/      # IsoRenderer (world→screen iso), drawDiamond, Font5x7
│   ├── game/           # Game: game loop, HUD, combate, input
│   └── editor/         # EditorApp: ImGui docking layout, SceneData, herramientas
├── scenes/             # Escenas .json (entities + tile overrides)
└── planning/           # Roadmap por semanas y sprint diario
```

### Dependencias

| Dependencia | Versión | Uso |
|---|---|---|
| SDL2 | sistema | Ventana, renderer 2D, input, audio |
| Dear ImGui | docking branch | UI del editor (panels, dockspace) |
| nlohmann/json | v3.11.3 | Serialización de escenas |

---

## Funcionalidades Implementadas

### Motor de Renderizado Isométrico
- Proyección world→screen configurable (`TILE_W=64`, `TILE_H=32`)
- Grid de 64×64 tiles con 9 tipos de terreno: Deep Water, Water, Sand, Grass, Forest, Dirt, Stone, Mountain, Snow
- Generación procedural por semilla (`World::generate(seed)`)
- Dibujado en painter order (arriba→abajo) con `SDL_RenderGeometry`
- Picking inverso mouse→world en ambos binarios
- Pixel font embebida `Font5x7` (bitmapped 5×7, sin dependencia de SDL_ttf)

### Sistema de Entidades y Combate RPG
- `Entity` → `Character` → `Player` / `Enemy`
- **4 clases de personaje:** Warrior, Mage, Rogue, Archer — cada una con stats distintos (attack, defense, magicAttack, speed, critChance)
- Sistema de stats completo: ataque base, defensa, magia, velocidad, crítico, nivel, XP, XP al siguiente nivel
- Combate melee con cooldowns, roll de daño, críticos, reducción por defensa
- Level-up automático con ganancia de exp al eliminar enemigos

### Player (Estilo Diablo II)
- Movimiento click-to-move con target en mundo isométrico
- Ataque click izquierdo (melee) con cooldown por clase
- Cámara centrada en el jugador
- HUD estilo Diablo II: orb de vida (rojo) y orb de maná (azul), score

### Enemy AI (FSM)
- Estados: **Idle → Patrol → Chase → Attack**
- Detección de jugador por radio configurable (`detectionRadius`)
- Radio de ataque (`attackRadius`)
- Patrullaje aleatorio con cambio de dirección periódico
- Recompensa en XP al morir (`expReward`)

### Editor DashEngine (Level Editor)
- **Layout dockable** estilo Unity con 8 paneles configurables:
  - Scene Hierarchy: lista de entidades en la escena
  - Viewport: render en texture target, explorable con cámara
  - Properties: inspector de entidad seleccionada
  - Tile Palette: selector de tipo de tile
  - File Browser: explorador de `src/`, abre archivos en editor
  - File Editor: editor de código con undo/redo por archivo (texto)
  - Build Log: registro de acciones y errores
  - Toolbar: botones de modo (Select, Paint, Place Enemy, Erase)
- **Herramientas:** Select, Paint Tile, Place Enemy, Erase
- **Escenas:** New, Open (dialog), Save / Save As (JSON)
- **Build & Run:** compila y lanza `IsometricRPG` desde el editor
- Tema oscuro estilo Unreal Engine, cursors personalizados por herramienta

### Formato de Escena (JSON)
```json
{
  "name": "Default Scene",
  "worldSeed": 12345,
  "tileOverrides": [],
  "entities": [
    {"type": "Player", "name": "Hero", "x": 32.0, "y": 32.0, "class": "Warrior"},
    {"type": "Enemy",  "name": "Skeleton", "x": 36.0, "y": 35.0}
  ]
}
```

---

## Lo Que Falta (Roadmap)

### Semana 1 — Fundaciones del Editor
- [ ] `EntityId` estable (int/uint64) para selección, serialización y runtime
- [ ] `CommandStack` global con `ICommand::apply()` / `undo()` (Undo/Redo de acciones del editor: pintar tile, colocar entidad, borrar)
- [ ] Formato de escena versionado (`scene_version`) con validación al cargar
- [ ] Guardado canónico (orden estable en JSON), mensajes de error en Build Log

### Semana 2 — Asset Pipeline
- [ ] `AssetDatabase`: GUID por asset, índice `asset_db.json`, sin dependencia de path absoluto
- [ ] Importers básicos: texturas/tilesets, escenas
- [ ] Detección de cambios por hash/mtime, reimport incremental
- [ ] Inspector mejorado con metadata de asset (GUID, tipo, estado dirty)

### Semana 3 — Runtime por Sistemas
- [ ] Separación en módulos: `MovementSystem`, `CombatSystem`, `AISystem`, `SpawnSystem`, `InteractionSystem`
- [ ] Stats de entidades cargados desde archivos JSON (data-driven)
- [ ] Pathfinding A* sobre grid walkable con costo por tipo de terreno
- [ ] Enemigos navegan con A* en lugar de persecución directa

### Semana 4 — Producción y QA
- [ ] **Play Mode:** snapshot del estado al entrar, restaurar al salir, sin contaminar datos
- [ ] Save/Load de partida versionado (estado completo del mundo + entidades)
- [ ] Tests: determinismo procedural por seed, carga de escenas inválidas, undo/redo
- [ ] Profiling por frame: update, render, AI, pathfinding; log de picos

### Backlog Futuro
- [ ] Sistema de prefabs/arquetipos con overrides por instancia
- [ ] Hot-reload de assets sin reiniciar editor
- [ ] Inspector genérico con reflection
- [ ] Sistema de eventos desacoplado (`OnDamage`, `OnDeath`, etc.)

---

## Build

```bash
# Requiere: CMake ≥ 3.16, SDL2, compilador C++17
mkdir build && cd build
cmake ..
make -j$(nproc)

# Editor
./DashEngine

# Juego standalone
./IsometricRPG
```

---

## Versión Actual

**v0.3-alpha** — Base funcional del engine y editor. El loop de juego corre, el editor permite crear y guardar escenas, el sistema de combate y la IA básica funcionan. No apto para producción: falta el pipeline de assets, undo/redo del editor, pathfinding y save/load del juego.

