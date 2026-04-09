# Isometric RPG - Plan de Implementacion Detallado

Este directorio separa las tareas por semana para ejecutar el escalado del editor y del juego.

**Ultima actualizacion: 2026-04-09**

## Estado actual del sprint

| Semana | Estado | Progreso |
|--------|--------|----------|
| 1 - Editor Foundations | ✅ Completada | D01-D06 completados |
| 2 - Asset Pipeline | ✅ Completada | D07-D12 completados |
| 3 - Runtime Systems | ✅ Completada | D13-D17 completados |
| 4 - Produccion/QA | ✅ Completada | D18-D20 completados |
| Sprint 2 - Features Avanzadas | ✅ Completada | D21-D30 completados, tag v2.0-alpha |
| Sprint 3 - Polish & Ship | 🔧 En progreso | D31-D35 pendientes |
| Sprint 4 - Sprite Editor | ✅ Completada | D36-D45 completados |
| Sprint 5 - Project Bundles | ✅ Completada | D46-D54 completados |

**Proximo paso:** Retomar Sprint 3 (D31-D35) y consolidar release final post-bundles.

## Funcionalidades ya implementadas (fuera del plan)

El proyecto ya cuenta con una base funcional significativa:
- Editor con docking layout completo (viewport isometrico, jerarquia, propiedades, tile palette, file browser, build log)
- Herramientas: Select, Paint Tile, Place Enemy, Erase, Build & Run
- Camera WASD + right-drag + scroll
- File editor con tabs, undo/redo de texto (200 niveles), auto-save
- Sistema de build integrado (CMake) con output en Build Log
- Scene serialization JSON (load/save con backward compat)
- Juego standalone con: movimiento WASD + click-to-move, combate con cooldowns/criticos, sistema de clases RPG (4 arquetipos), enemigos con FSM (Idle/Patrol/Chase/Attack), HUD estilo Diablo 2, mundo procedural 64x64 con Perlin noise
- Packaging macOS (.app bundle)
- Sistema de prefabs con drag-drop desde AssetBrowser al viewport
- Hot-reload de assets via FileWatcher
- ContentValidator con ValidationPanel
- EventDispatcher tipado (DamageEvent/DeathEvent/LevelUpEvent/HealthChangeEvent)
- Inspector generico con reflection y undo/redo de componentes
- Sprite Editor completo con preview isometrico y metadatos de pivot (.sprite.json)
- 18 tests automatizados (ctest)

## Orden recomendado

1. Semana 1: Fundaciones del editor y modelo de datos.
2. Semana 2: Asset pipeline y base de importacion/caching.
3. Semana 3: Runtime por sistemas y gameplay data-driven.
4. Semana 4: Produccion, QA, testing y profiling.
5. Sprint 2: Arquitectura de componentes, prefabs, hot-reload, validacion, packaging.
6. Sprint 3: Bundle paths, Game Over, loot runtime, tests coverage, documentacion final.

## Archivos de trabajo

### Sprint 1 (D01-D20)
- 01_WEEK1_EDITOR_FOUNDATIONS.md
- 02_WEEK2_ASSET_PIPELINE.md
- 03_WEEK3_RUNTIME_SYSTEMS.md
- 04_WEEK4_PRODUCTION_QA.md
- 05_SPRINT_OPERATIVO_20_DIAS.md

### Sprint 4 (D36-D45)
- 10_SPRINT4_SPRITE_EDITOR.md
- 06_SPRINT_2_FEATURES_AVANZADAS.md (tablero + registro diario)
- 07_WEEK5_COMPONENT_ARCHITECTURE.md (D21-D25: eventos, comandos, componentes, registry, inspector)
- 08_WEEK6_PRODUCTION_TOOLS.md (D26-D30: prefabs, hot-reload, validacion, packaging, cierre)

### Sprint 3 (D31-D35)
- 09_SPRINT3_POLISH_AND_SHIP.md (tablero + registro diario + specs por dia)

### Sprint 5 (D46-D54)
- 11_SPRINT5_PROJECT_BUNDLES.md

### General
- 99_ACCEPTANCE_CHECKLIST.md

## Convenciones para ejecutar tareas

- Cada tarea define:
  - objetivo tecnico
  - alcance
  - cambios concretos por archivo
  - seccion de codigo a modificar o crear
  - criterio de aceptacion
- No mezclar tareas de distintas semanas salvo dependencias criticas.
- Mantener cambios pequenos y verificables en commits separados.

## Regla de arquitectura

- Editor y runtime deben compartir datos por contratos (estructuras serializables), no por acceso directo a UI.
- Toda funcionalidad nueva debe llegar como modulo/sistema y no como bloque monolitico en un solo cpp.
