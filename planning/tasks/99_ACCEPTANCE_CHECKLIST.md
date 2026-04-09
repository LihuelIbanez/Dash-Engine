# Acceptance Checklist - Escalado Editor + Juego

Usa este archivo para control de cierre por fase.

**Ultima actualizacion: Sprint 2 completado — tag v2.0-alpha**

## Fase A - Editor Foundations

- [x] Seleccion de entidad basada en EntityId estable. *(EntityData.id uint64_t, nextEntityId, allocateEntityId(), selectedEntityId_ en EditorApp, findEntityById())*
- [x] Undo/Redo global de escena (paint, place, erase, property edit). *(ICommand + CommandStack + PaintTileCommand/PlaceEnemyCommand/EraseCommand. Cmd+Z/Shift+Z. Menu Edit. 200 niveles max.)*
- [x] Escenas versionadas con validacion de carga. *(sceneVersion field, kCurrentVersion=1, validacion JSON, loadErrors en Build Log)*
- [x] Dirty state visible y protegido ante perdida de cambios. *(modal Unsaved Changes con Save/Discard/Cancel, titulo ventana con * en dirty)*

## Fase B - Asset Pipeline

- [x] Asset Database persistente con GUID por recurso. *(AssetDatabase.h/.cpp, asset_db.json con GUIDs, hash/mtime por asset — D08/D09)*
- [x] Import incremental por hash/mtime. *(ImportManager, FileWatcher con hash incremental — D09/D27)*
- [x] Asset browser con metadata y accion Reimport. *(AssetBrowserPanel con tabla tipo/guid/path, boton Reimport, hot-reload via menu — D09/D27)*
- [x] Integracion estable de assets en editor sin crasheos. *(11/11 test suites passing en Release, 0 warnings — D30)*

## Fase C - Runtime Systems

- [x] Game update migrado a scheduler + sistemas. *(SystemScheduler, RuntimeContext, MovementSystem/AISystem/CombatSystem/SpawnRewardSystem — D15)*
- [x] Gameplay configurable desde archivos de datos. *(GameplayDatabase carga player_classes.json, enemies.json, loot_tables.json; Enemy ctor data-driven — D16)*
- [x] AI con pathfinding A* usando costos de terreno. *(GridNav A* con costos por TileType, AISystem usa pathfinding — D14)*
- [x] Paridad funcional mantenida respecto al comportamiento previo. *(build standalone IsometricRPG funcional, sin regresiones — D30)*

## Fase D - Produccion y QA

- [x] Play mode con rollback exacto al estado de edicion. *(EditorApp guarda/restaura SceneSnapshot al entrar/salir de Play mode — D19)*
- [x] Save/load versionado con compatibilidad basica de versiones. *(SaveGame.h/.cpp + SaveVersioning con migrate() v0→v1, F5/F9 quicksave — D18)*
- [x] Tests automatizados minimos corriendo desde CMake. *(11 suites: scene_serialization, undo_redo, component_serialization, entity_registry, move_edit_commands, world_seed, pathfinding, prefab_system, content_validation, event_system, hot_reload — D20/D30)*
- [x] Panel de rendimiento con metricas de frame y subsistemas. *(Profiler singleton con scope RAII, panel en editor (Profiler tab), metricas Update/Render/AI por frame — D20)*

## Cierre de ciclo

- [x] Documentacion tecnica actualizada tras cada fase. *(README.md v2.0-alpha, SCALING_CHECKLIST.md Sprint 2 completado — D30)*
- [x] Deuda tecnica registrada y priorizada para el siguiente sprint. *(09_SPRINT3_POLISH_AND_SHIP.md cubre bundle paths, Game Over, loot runtime, tests faltantes)*
- [x] Version estable de DashEngine instalada y validada. *(packaging/install_app.sh ejecutado exitosamente, app en /Applications/DashEngine.app — D29)*

## Sprint 3 — Polish & Ship

- [ ] Bundle paths correctos en distribucion macOS (.app). *(AppPaths.h sin PROJECT_DIR — D31)*
- [ ] Tests SaveGame y GameplayDatabase. *(13 suites total — D32)*
- [ ] Game Over screen y pantalla de titulo con seleccion de clase. *(D33)*
- [ ] Loot runtime: drops al matar enemigos via loot_tables.json. *(D34)*

## Sprint 4 — Sprite Editor

- [x] Sprite Editor integrado al editor con canvas, herramientas y capas. *(D36-D41)*
- [x] Export/Load PNG y asignacion a entidades via RenderComponent con undo/redo. *(D42)*
- [x] Pipeline de SpriteImporter y render runtime con TextureCache en viewport. *(D43)*
- [x] Preview isometrico con anchor/pivot y metadatos .sprite.json usados por runtime. *(D44)*
- [x] Tests de logica de sprite editor y pulido UI final. *(D45, ctest 14/14)*

## Sprint 5 — Project Bundles

- [ ] `ProjectManifest` serializa/deserializa `.dashproject` sin perdida de datos. *(D46)*
- [ ] `ProjectManager` crea y abre proyectos; los paths de assets siguen al proyecto activo. *(D47)*
- [ ] `AppPaths` devuelve paths del proyecto activo cuando existe uno; backward compat con modo legacy. *(D48)*
- [ ] CMake: `game_runtime` compila sin ninguna dependencia de `src/editor/`. *(D49)*
- [ ] Build Game Pipeline: exportar directorio auto-contenido con ejecutable + assets del proyecto. *(D50)*
- [ ] WelcomePanel / Launcher con New Project, Open Project y Recent Projects. *(D51)*
- [ ] Aislamiento verificado: `check_isolation` target no detecta headers de editor en game_runtime. *(D52)*
- [ ] ≥ 18 tests (ctest 100% pass) incluyendo project_manifest, project_manager y build_pipeline. *(D53)*
- [ ] README y checklists actualizados; tag `v5.0-alpha`. *(D54)*

## Resumen de avance

| Fase | Completado | Total | % |
|------|-----------|-------|---|
| A - Editor Foundations | 4 | 4 | 100% |
| B - Asset Pipeline | 4 | 4 | 100% |
| C - Runtime Systems | 4 | 4 | 100% |
| D - Produccion y QA | 4 | 4 | 100% |
| Sprint 3 - Polish | 0 | 4 | 0% |
| Sprint 4 - Sprite Editor | 5 | 5 | 100% |
| Sprint 5 - Project Bundles | 0 | 9 | 0% |
| **Total S1+S2** | **16** | **16** | **100%** |
| **Total incluyendo S3-S5** | **21** | **34** | **62%** |
