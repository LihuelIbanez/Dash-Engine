# Acceptance Checklist - Escalado Editor + Juego

Usa este archivo para control de cierre por fase.

## Fase A - Editor Foundations

- [ ] Seleccion de entidad basada en EntityId estable.
- [ ] Undo/Redo global de escena (paint, place, erase, property edit).
- [ ] Escenas versionadas con validacion de carga.
- [ ] Dirty state visible y protegido ante perdida de cambios.

## Fase B - Asset Pipeline

- [ ] Asset Database persistente con GUID por recurso.
- [ ] Import incremental por hash/mtime.
- [ ] Asset browser con metadata y accion Reimport.
- [ ] Integracion estable de assets en editor sin crasheos.

## Fase C - Runtime Systems

- [ ] Game update migrado a scheduler + sistemas.
- [ ] Gameplay configurable desde archivos de datos.
- [ ] AI con pathfinding A* usando costos de terreno.
- [ ] Paridad funcional mantenida respecto al comportamiento previo.

## Fase D - Produccion y QA

- [ ] Play mode con rollback exacto al estado de edicion.
- [ ] Save/load versionado con compatibilidad basica de versiones.
- [ ] Tests automatizados minimos corriendo desde CMake.
- [ ] Panel de rendimiento con metricas de frame y subsistemas.

## Cierre de ciclo

- [ ] Documentacion tecnica actualizada tras cada fase.
- [ ] Deuda tecnica registrada y priorizada para el siguiente sprint.
- [ ] Version estable de DashEngine instalada y validada.
