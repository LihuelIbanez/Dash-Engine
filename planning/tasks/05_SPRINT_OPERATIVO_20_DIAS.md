# Sprint Operativo (20 dias)

Objetivo: ejecutar el plan de escalado en 20 dias laborables, con orden diario, horas estimadas y dependencias claras.

## Tablero del Sprint (To Do / Doing / Done)

Usa este tablero para mover el estado diario del sprint. Solo debe haber 1 item en Doing al mismo tiempo.

### To Do

(vacio)

### Doing

(vacio)

### Done

- [x] D01 - EntityId estable en escena (6h) — Completado
- [x] D02 - Seleccion por ID en editor (6h) — Completado
- [x] D03 - Infraestructura CommandStack (6h) — Completado
- [x] D04 - Comandos paint/place/erase (6h) — Completado
- [x] D05 - CMake + serializacion versionada (6h) — Completado
- [x] D06 - Dirty state y confirmaciones (5h) — Completado: modal Unsaved Changes (Save/Discard/Cancel), requestAction guard, titulo ventana con *
- [x] D07 - Base Asset System (6h) — Completado: AssetTypes.h, AssetRecord.h, carpetas assets/ y library/
- [x] D08 - AssetDatabase persistente (6h) — Completado: AssetDatabase.h/.cpp con load/save/upsert/findByGuid/findBySourcePath/removeMissing, integrado en EditorApp init/destructor, GUID v4
- [x] D09 - Integracion AssetDatabase en editor (6h) — Completado: importAll en init, Reimport All en File Browser, assetDb_.save() en destructor
- [x] D10 - ImportManager + IImporter (6h) — Completado: IImporter interfaz, ImportManager con importAsset/importAll/computeFileHash/inferAssetType, import incremental por hash
- [x] D11 - Importers iniciales (6h) — Completado: SceneImporter, TileSetImporter, GameplayConfigImporter con validacion JSON y copia a library/
- [x] D12 - Asset Browser + Inspector (6h) — Completado: AssetBrowserPanel con tabla/filtro/seleccion, AssetInspectorPanel con GUID/tipo/hash/deps/reimport, docking en layout
- [x] D13 - RuntimeContext + scheduler (6h) — Completado: RuntimeContext con world/player/enemies/score/dt, ISystem interfaz, SystemScheduler, 4 sistemas inline (PlayerMovement, AIUpdate, CombatResolve, Cleanup), Game.cpp delegacion total a scheduler
- [x] D14 - MovementSystem + CombatSystem (6h) — Completado: Extraidos a src/game/systems/MovementSystem.h/.cpp y CombatSystem.h/.cpp
- [x] D15 - AISystem + SpawnRewardSystem (6h) — Completado: Extraidos a src/game/systems/AISystem.h/.cpp y SpawnRewardSystem.h/.cpp
- [x] D16 - Gameplay data-driven (6h) — Completado: GameplayDatabase.h/.cpp, player_classes.json, enemies.json, loot_tables.json, Enemy constructor data-driven, Game.cpp usa GameplayDatabase
- [x] D17 - Pathfinding A* (6h) — Completado: GridNav.h/.cpp con A* 8-dir, terrainCost por tipo de tile, Enemy Chase usa waypoints A*, path refresh 0.5s, corner-cutting prevention
- [x] D18 - Play Mode con rollback (6h) — Completado: PlaySession.h/.cpp con capture/restore, EditorMode Edit/Play, boton Play/Stop en toolbar, snapshot de SceneData + World grid, overlay "PLAYING" en viewport, herramientas de edicion deshabilitadas en modo Play
- [x] D19 - Save/Load versionado (6h) — Completado: SaveGame.h/.cpp con save/load JSON, SaveVersioning.h/.cpp con migracion por version, captureState/applyState en Game, F5 quicksave / F9 quickload, carpeta saves/
- [x] D20 - Tests + profiling + cierre (6h) — Completado: 4 test suites (21 tests), Profiler.h/.cpp singleton con ScopeTimer RAII, Performance panel en editor, instrumentacion Game.cpp

## Registro Diario de Ejecucion

Usa una linea por dia para dejar trazabilidad de avance real.

- [x] Dia 01 | ID: D01 | Plan: 6h | Real: ~6h | Bloqueos: Ninguno | Resultado: EntityData.id, nextEntityId y allocateEntityId() implementados. Backward compat con escenas sin id.
- [x] Dia 02 | ID: D02 | Plan: 6h | Real: ~6h | Bloqueos: Ninguno | Resultado: selectedEntityId_ reemplaza indice. findEntityById() integrado en jerarquia e inspector.
- [x] Dia 03 | ID: D03 | Plan: 6h | Real: ~3h | Bloqueos: Ninguno | Resultado: ICommand, CommandStack con execute/undo/redo/clear, performUndo/Redo en EditorApp, menu Edit, Cmd+Z shortcuts.
- [x] Dia 04 | ID: D04 | Plan: 6h | Real: ~3h | Bloqueos: Ninguno | Resultado: PaintTileCommand, PlaceEnemyCommand, EraseCommand. handleToolClick y drawSceneHierarchy refactorizados. Build limpio 0 warnings.
- [x] Dia 05 | ID: D05 | Plan: 6h | Real: ~2h | Bloqueos: Ninguno | Resultado: sceneVersion en SceneData, kCurrentVersion=1, validacion de JSON/bounds/types/entities, loadErrors, mensajes en Build Log.
- [x] Dia 06 | ID: D06 | Plan: 5h | Real: ~1h | Bloqueos: Ninguno | Resultado: PendingAction enum, requestAction/executePendingAction, modal Save/Discard/Cancel, titulo ventana con *, SDL_QUIT guard.
- [x] Dia 07 | ID: D07 | Plan: 6h | Real: ~0.5h | Bloqueos: Ninguno | Resultado: AssetTypes.h (enum AssetType), AssetRecord.h (struct con guid/sourcePath/importPath/hash/deps), carpetas assets/ y library/ creadas.
- [x] Dia 08 | ID: D08 | Plan: 6h | Real: ~0.5h | Bloqueos: Ninguno | Resultado: AssetDatabase con GUID v4, load/save JSON, upsert, find, removeMissing. CMakeLists con ASSET_SOURCES glob. EditorApp carga DB en init y persiste en destructor.
- [x] Dia 09 | ID: D09 | Plan: 6h | Real: ~0.3h | Bloqueos: Ninguno | Resultado: ImportManager.importAll en init, Reimport All boton en File Browser, save DB tras import.
- [x] Dia 10 | ID: D10 | Plan: 6h | Real: ~0.3h | Bloqueos: Ninguno | Resultado: IImporter.h interfaz, ImportManager.h/.cpp con hash incremental, inferAssetType por extension/carpeta.
- [x] Dia 11 | ID: D11 | Plan: 6h | Real: ~0.3h | Bloqueos: Ninguno | Resultado: SceneImporter, TileSetImporter, GameplayConfigImporter. Validacion JSON, copia a library/. CMakeLists con IMPORTER_SOURCES.
- [x] Dia 12 | ID: D12 | Plan: 6h | Real: ~0.5h | Bloqueos: Ninguno | Resultado: AssetBrowserPanel (tabla, filtro, seleccion por GUID), AssetInspectorPanel (metadata completa + Reimport boton), docking en layout, PANEL_SOURCES en CMakeLists.
- [x] Dia 13 | ID: D13 | Plan: 6h | Real: ~0.5h | Bloqueos: Ninguno | Resultado: RuntimeContext.h, ISystem.h, SystemScheduler.h/.cpp. 4 sistemas inline en Game.cpp (PlayerMovement, AIUpdate, CombatResolve, Cleanup). update() delega a scheduler. resolveAttacks() eliminado.
- [x] Dia 14 | ID: D14 | Plan: 6h | Real: ~0.3h | Bloqueos: Ninguno | Resultado: MovementSystem.h/.cpp y CombatSystem.h/.cpp extraidos de Game.cpp a src/game/systems/.
- [x] Dia 15 | ID: D15 | Plan: 6h | Real: ~0.3h | Bloqueos: Ninguno | Resultado: AISystem.h/.cpp y SpawnRewardSystem.h/.cpp extraidos. Game.cpp limpio solo con includes e initSystems().
- [x] Dia 16 | ID: D16 | Plan: 6h | Real: ~0.5h | Bloqueos: Ninguno | Resultado: GameplayDatabase.h/.cpp carga player_classes/enemies/loot_tables JSON. Enemy constructor data-driven. Game.cpp spawn desde datos.
- [x] Dia 17 | ID: D17 | Plan: 6h | Real: ~0.5h | Bloqueos: Ninguno | Resultado: GridNav.h/.cpp A* 8-dir con octile heuristic, terrainCost() en World (Sand 1.3, Forest 1.5, Mountain 2.0), Enemy Chase sigue waypoints con refresh 0.5s, prevencion corner-cutting.
- [x] Dia 18 | ID: D18 | Plan: 6h | Real: ~0.5h | Bloqueos: Ninguno | Resultado: PlaySession.h/.cpp con capture/restore, EditorMode Edit/Play, boton Play/Stop, snapshot SceneData+World, overlay PLAYING.
- [x] Dia 19 | ID: D19 | Plan: 6h | Real: ~0.5h | Bloqueos: Ninguno | Resultado: SaveGame.h/.cpp con JSON serialization, SaveVersioning con migracion, captureState/applyState en Game, F5/F9 quicksave/load.
- [x] Dia 20 | ID: D20 | Plan: 6h | Real: ~0.5h | Bloqueos: Ninguno | Resultado: 4 test suites (21 tests) pass, Profiler singleton con ScopeTimer RAII, Performance panel ImGui en editor, Game.cpp instrumentado.

## Reglas de ejecucion

- Jornada objetivo: 6 horas efectivas por dia.
- Cada dia cierra con build y smoke test basico.
- No iniciar una tarea si su dependencia directa no esta completada.
- Si una tarea excede +25% de horas, dividir y mover remanente al siguiente dia.

## Leyenda

- ID: identificador de tarea diaria.
- Horas: estimacion total del dia.
- Dependencias: IDs previos obligatorios.
- Archivos: rutas a crear/modificar.

---

## Dia 1

- ID: D01
- Meta: Base de EntityId estable en modelo de escena.
- Horas: 6h
- Dependencias: Ninguna
- Tareas:
  - Agregar id y nextEntityId al modelo.
  - Definir helper allocateEntityId().
  - Compatibilidad minima con escenas antiguas sin id.
- Archivos:
  - Modificar: src/editor/SceneData.h
  - Modificar: src/editor/SceneData.cpp

## Dia 2

- ID: D02
- Meta: Migrar seleccion de entidad por ID en editor.
- Horas: 6h
- Dependencias: D01
- Tareas:
  - Reemplazar seleccion por indice con selectedEntityId.
  - Adaptar jerarquia e inspector a busqueda por id.
- Archivos:
  - Modificar: src/editor/EditorApp.h
  - Modificar: src/editor/EditorApp.cpp

## Dia 3

- ID: D03
- Meta: Crear infraestructura de comandos (undo/redo global).
- Horas: 6h
- Dependencias: D02
- Tareas:
  - Crear ICommand y CommandStack.
  - Integrar execute/undo/redo en editor.
- Archivos:
  - Crear: src/editor/commands/ICommand.h
  - Crear: src/editor/commands/CommandStack.h
  - Crear: src/editor/commands/CommandStack.cpp
  - Modificar: src/editor/EditorApp.h
  - Modificar: src/editor/EditorApp.cpp

## Dia 4

- ID: D04
- Meta: Comandos de paint/place/erase.
- Horas: 6h
- Dependencias: D03
- Tareas:
  - Implementar PaintTileCommand.
  - Implementar PlaceEnemyCommand.
  - Implementar EraseCommand.
  - Reemplazar mutacion directa en handleToolClick().
- Archivos:
  - Crear: src/editor/commands/PaintTileCommand.h
  - Crear: src/editor/commands/PaintTileCommand.cpp
  - Crear: src/editor/commands/PlaceEnemyCommand.h
  - Crear: src/editor/commands/PlaceEnemyCommand.cpp
  - Crear: src/editor/commands/EraseCommand.h
  - Crear: src/editor/commands/EraseCommand.cpp
  - Modificar: src/editor/EditorApp.cpp

## Dia 5

- ID: D05
- Meta: Integracion de build para nuevos modulos y hardening de serializacion.
- Horas: 6h
- Dependencias: D04
- Tareas:
  - Ajustar CMake para incluir subcarpetas del editor.
  - Agregar sceneVersion y validacion de carga.
  - Mejorar mensajes de error en open/save.
- Archivos:
  - Modificar: CMakeLists.txt
  - Modificar: src/editor/SceneData.h
  - Modificar: src/editor/SceneData.cpp
  - Modificar: src/editor/EditorApp.cpp

## Dia 6

- ID: D06
- Meta: Dirty state y proteccion de cambios no guardados.
- Horas: 5h
- Dependencias: D05
- Tareas:
  - Agregar sceneDirty_.
  - Confirmaciones en New/Open/Quit.
- Archivos:
  - Modificar: src/editor/EditorApp.h
  - Modificar: src/editor/EditorApp.cpp

## Dia 7

- ID: D07
- Meta: Crear base de Asset System (tipos y registro).
- Horas: 6h
- Dependencias: D06
- Tareas:
  - Crear tipos y estructura AssetRecord.
  - Crear carpetas assets y library.
- Archivos:
  - Crear: src/assets/AssetTypes.h
  - Crear: src/assets/AssetRecord.h
  - Crear: assets/.gitkeep
  - Crear: library/.gitkeep

## Dia 8

- ID: D08
- Meta: AssetDatabase persistente.
- Horas: 6h
- Dependencias: D07
- Tareas:
  - Implementar carga/guardado JSON de asset_db.
  - Búsqueda por GUID y por sourcePath.
- Archivos:
  - Crear: src/assets/AssetDatabase.h
  - Crear: src/assets/AssetDatabase.cpp
  - Crear: assets/asset_db.json
  - Modificar: CMakeLists.txt

## Dia 9

- ID: D09
- Meta: Integrar AssetDatabase al ciclo de vida del editor.
- Horas: 6h
- Dependencias: D08
- Tareas:
  - Cargar DB en init.
  - Persistir DB en salida.
  - Registrar logs de estado.
- Archivos:
  - Modificar: src/editor/EditorApp.h
  - Modificar: src/editor/EditorApp.cpp

## Dia 10

- ID: D10
- Meta: ImportManager e interfaz base de importers.
- Horas: 6h
- Dependencias: D09
- Tareas:
  - Definir IImporter.
  - Implementar ImportManager.
  - Resolver importer por tipo.
- Archivos:
  - Crear: src/assets/importers/IImporter.h
  - Crear: src/assets/ImportManager.h
  - Crear: src/assets/ImportManager.cpp

## Dia 11

- ID: D11
- Meta: Importers iniciales (scene/tileset/gameplay).
- Horas: 6h
- Dependencias: D10
- Tareas:
  - Implementar importers.
  - Soportar hash/mtime para import incremental.
- Archivos:
  - Crear: src/assets/importers/SceneImporter.h
  - Crear: src/assets/importers/SceneImporter.cpp
  - Crear: src/assets/importers/TileSetImporter.h
  - Crear: src/assets/importers/TileSetImporter.cpp
  - Crear: src/assets/importers/GameplayConfigImporter.h
  - Crear: src/assets/importers/GameplayConfigImporter.cpp

## Dia 12

- ID: D12
- Meta: UI de Asset Browser e inspector de metadata.
- Horas: 6h
- Dependencias: D11
- Tareas:
  - Crear paneles dedicados.
  - Integrar seleccion por GUID y boton Reimport.
  - Docking en layout por defecto.
- Archivos:
  - Crear: src/editor/panels/AssetBrowserPanel.h
  - Crear: src/editor/panels/AssetBrowserPanel.cpp
  - Crear: src/editor/panels/AssetInspectorPanel.h
  - Crear: src/editor/panels/AssetInspectorPanel.cpp
  - Modificar: src/editor/EditorApp.h
  - Modificar: src/editor/EditorApp.cpp

## Dia 13

- ID: D13
- Meta: RuntimeContext, ISystem y scheduler.
- Horas: 6h
- Dependencias: D12
- Tareas:
  - Crear contratos de sistema.
  - Inicializar scheduler en runtime.
- Archivos:
  - Crear: src/game/runtime/RuntimeContext.h
  - Crear: src/game/runtime/ISystem.h
  - Crear: src/game/runtime/SystemScheduler.h
  - Crear: src/game/runtime/SystemScheduler.cpp
  - Modificar: src/game/Game.h
  - Modificar: src/game/Game.cpp

## Dia 14

- ID: D14
- Meta: MovementSystem y CombatSystem.
- Horas: 6h
- Dependencias: D13
- Tareas:
  - Migrar movimiento a MovementSystem.
  - Migrar combate/daño/recompensa base a CombatSystem.
- Archivos:
  - Crear: src/game/systems/MovementSystem.h
  - Crear: src/game/systems/MovementSystem.cpp
  - Crear: src/game/systems/CombatSystem.h
  - Crear: src/game/systems/CombatSystem.cpp
  - Modificar: src/game/Game.cpp

## Dia 15

- ID: D15
- Meta: AISystem y SpawnRewardSystem.
- Horas: 6h
- Dependencias: D14
- Tareas:
  - Migrar IA enemiga a AISystem.
  - Separar recompensas/spawn logic.
- Archivos:
  - Crear: src/game/systems/AISystem.h
  - Crear: src/game/systems/AISystem.cpp
  - Crear: src/game/systems/SpawnRewardSystem.h
  - Crear: src/game/systems/SpawnRewardSystem.cpp
  - Modificar: src/game/Game.cpp

## Dia 16

- ID: D16
- Meta: Gameplay data-driven.
- Horas: 6h
- Dependencias: D15
- Tareas:
  - Crear JSON de clases, enemigos y loot.
  - Crear GameplayDatabase.
  - Cargar datos en inicializacion de runtime.
- Archivos:
  - Crear: assets/gameplay/player_classes.json
  - Crear: assets/gameplay/enemies.json
  - Crear: assets/gameplay/loot_tables.json
  - Crear: src/game/data/GameplayDatabase.h
  - Crear: src/game/data/GameplayDatabase.cpp
  - Modificar: src/game/Game.cpp

## Dia 17

- ID: D17
- Meta: Pathfinding A* con costos de terreno.
- Horas: 6h
- Dependencias: D16
- Tareas:
  - Implementar GridNav.
  - Exponer costo de tiles en World.
  - Integrar pathfinding en AISystem.
- Archivos:
  - Crear: src/game/nav/GridNav.h
  - Crear: src/game/nav/GridNav.cpp
  - Modificar: src/world/World.h
  - Modificar: src/world/World.cpp
  - Modificar: src/game/systems/AISystem.cpp

## Dia 18

- ID: D18
- Meta: Play mode seguro en editor (snapshot + rollback).
- Horas: 6h
- Dependencias: D17
- Tareas:
  - Crear PlaySession.
  - Agregar botones Play/Stop.
  - Restaurar estado al salir de Play.
- Archivos:
  - Crear: src/editor/playmode/PlaySession.h
  - Crear: src/editor/playmode/PlaySession.cpp
  - Modificar: src/editor/EditorApp.h
  - Modificar: src/editor/EditorApp.cpp

## Dia 19

- ID: D19
- Meta: Save/load versionado del juego.
- Horas: 6h
- Dependencias: D18
- Tareas:
  - Implementar SaveGame y versioning.
  - Integrar persistencia de estado core.
- Archivos:
  - Crear: src/game/save/SaveGame.h
  - Crear: src/game/save/SaveGame.cpp
  - Crear: src/game/save/SaveVersioning.h
  - Crear: src/game/save/SaveVersioning.cpp
  - Modificar: src/game/Game.h
  - Modificar: src/game/Game.cpp
  - Crear: saves/.gitkeep

## Dia 20

- ID: D20
- Meta: Testing + profiling + cierre de sprint.
- Horas: 6h
- Dependencias: D19
- Tareas:
  - Crear suite minima de tests.
  - Integrar tests en CMake.
  - Crear profiler base y panel en editor.
  - Cierre con checklist de aceptacion.
- Archivos:
  - Crear: tests/CMakeLists.txt
  - Crear: tests/test_scene_serialization.cpp
  - Crear: tests/test_undo_redo_commands.cpp
  - Crear: tests/test_world_seed_determinism.cpp
  - Crear: tests/test_pathfinding.cpp
  - Crear: src/core/profiling/Profiler.h
  - Crear: src/core/profiling/Profiler.cpp
  - Modificar: CMakeLists.txt
  - Modificar: src/game/Game.cpp
  - Modificar: src/editor/EditorApp.cpp

---

## Dependencias globales (resumen)

- Bloque Editor Foundations: D01 -> D06
- Bloque Asset Pipeline: D07 -> D12
- Bloque Runtime Systems: D13 -> D17
- Bloque Produccion/QA: D18 -> D20

No paralelizar bloques sin completar su base anterior.

## Estimacion total

- Horas totales: 119h
- Promedio diario: 5.95h

## Buffer recomendado

- Reservar 1 dia extra (fuera de los 20) para contingencia de integracion.
- Si no se usa buffer, invertirlo en hardening de tests y documentacion tecnica.
