# Acceptance Checklist - Escalado Editor + Juego

Usa este archivo para control de cierre por fase.

**Ultima actualizacion: ver historial de Git/PR**

## Fase A - Editor Foundations

- [x] Seleccion de entidad basada en EntityId estable. *(Implementado: EntityData.id uint64_t, nextEntityId, allocateEntityId(), selectedEntityId_ en EditorApp, findEntityById())*
- [x] Undo/Redo global de escena (paint, place, erase, property edit). *(Implementado: ICommand + CommandStack + PaintTileCommand/PlaceEnemyCommand/EraseCommand. Cmd+Z/Shift+Z. Menu Edit. 200 niveles max.)*
- [x] Escenas versionadas con validacion de carga. *(Implementado: sceneVersion field, kCurrentVersion=1, validacion JSON parse/bounds/types/player, loadErrors con mensajes detallados en Build Log)*
- [x] Dirty state visible y protegido ante perdida de cambios. *(Implementado: modal Unsaved Changes con Save/Discard/Cancel, requestAction guard en New/Open/Exit/SDL_QUIT, titulo ventana con * en dirty)*

## Fase B - Asset Pipeline

- [ ] Asset Database persistente con GUID por recurso. *(No iniciado)*
- [ ] Import incremental por hash/mtime. *(No iniciado)*
- [ ] Asset browser con metadata y accion Reimport. *(No iniciado)*
- [ ] Integracion estable de assets en editor sin crasheos. *(No iniciado)*

## Fase C - Runtime Systems

- [ ] Game update migrado a scheduler + sistemas. *(No iniciado — logica monolitica en Game.cpp)*
- [ ] Gameplay configurable desde archivos de datos. *(No iniciado — stats hardcodeados en Character.cpp y Enemy.cpp)*
- [ ] AI con pathfinding A* usando costos de terreno. *(No iniciado — enemigos usan persecucion directa)*
- [ ] Paridad funcional mantenida respecto al comportamiento previo.

## Fase D - Produccion y QA

- [ ] Play mode con rollback exacto al estado de edicion. *(No iniciado)*
- [ ] Save/load versionado con compatibilidad basica de versiones. *(No iniciado)*
- [ ] Tests automatizados minimos corriendo desde CMake. *(No iniciado — no existe directorio tests/)*
- [ ] Panel de rendimiento con metricas de frame y subsistemas. *(No iniciado)*

## Cierre de ciclo

- [ ] Documentacion tecnica actualizada tras cada fase.
- [ ] Deuda tecnica registrada y priorizada para el siguiente sprint.
- [ ] Version estable de DashEngine instalada y validada.

## Resumen de avance

| Fase | Completado | Total | % |
|------|-----------|-------|---|
| A - Editor Foundations | 4 | 4 | 100% |
| B - Asset Pipeline | 0 | 4 | 0% |
| C - Runtime Systems | 0 | 4 | 0% |
| D - Produccion y QA | 0 | 4 | 0% |
| **Total** | **4** | **16** | **25%** |
