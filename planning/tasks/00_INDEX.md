# Isometric RPG - Plan de Implementacion Detallado

Este directorio separa las tareas por semana para ejecutar el escalado del editor y del juego.

**Ultima actualizacion: 2026-04-07**

## Estado actual del sprint

| Semana | Estado | Progreso |
|--------|--------|----------|
| 1 - Editor Foundations | ✅ Completada | D01-D06 completados |
| 2 - Asset Pipeline | 🔧 En progreso | D07-D08 completados, D09-D12 pendientes |
| 3 - Runtime Systems | ⬜ No iniciada | D13-D17 pendientes |
| 4 - Produccion/QA | ⬜ No iniciada | D18-D20 pendientes |

**Proximo paso:** D09 - Integracion AssetDatabase en editor (scan assets/, upsert records)

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

## Orden recomendado

1. Semana 1: Fundaciones del editor y modelo de datos.
2. Semana 2: Asset pipeline y base de importacion/caching.
3. Semana 3: Runtime por sistemas y gameplay data-driven.
4. Semana 4: Produccion, QA, testing y profiling.

## Archivos de trabajo

- 01_WEEK1_EDITOR_FOUNDATIONS.md
- 02_WEEK2_ASSET_PIPELINE.md
- 03_WEEK3_RUNTIME_SYSTEMS.md
- 04_WEEK4_PRODUCTION_QA.md
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
