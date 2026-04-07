# Isometric RPG - Scaling Checklist (30 dias)

Este plan prioriza primero el Editor (como Unity/Unreal/Godot) y luego el Runtime del juego.
Marca cada item con [x] cuando este completado.

**Ultima actualizacion: 2026-04-07**

## Objetivos de Escalado

- [x] Reducir acoplamiento entre editor, datos y runtime.
- [x] Soportar crecimiento de contenido (assets, escenas, entidades).
- [ ] Mejorar mantenibilidad y velocidad de iteracion.
- [ ] Preparar base para trabajo en equipo y features de produccion.

## Semana 1 - Fundaciones del Editor (Arquitectura) ✅ COMPLETADA

### 1) Modelo de datos de escena y componentes
- [x] Definir `EntityId` estable (int/uint64) para editor y runtime.
- [x] Separar datos de escena de la logica visual del editor.
- [ ] Crear estructuras base de componentes (`Transform`, `Render`, `Combat`, etc.).
- [x] Versionar formato de escena (`scene_version`).

### 2) Command System global (Undo/Redo real)
- [x] Crear interfaz `ICommand` (`apply()`, `undo()`, `name()`).
- [x] Implementar `CommandStack` global del editor.
- [x] Migrar acciones actuales a comandos:
  - [x] Pintar tile.
  - [x] Colocar enemigo.
  - [x] Borrar entidad/tile.
  - [ ] Mover/editar propiedades de entidad.
- [x] Añadir atajos globales: Cmd+Z (undo), Cmd+Shift+Z (redo).

### 3) Persistencia robusta
- [x] Guardado canonico (orden estable en JSON).
- [x] Validacion basica al cargar escenas (campos requeridos).
- [x] Manejo de errores de parseo con mensajes claros en Build Log.

### 4) Dirty state y proteccion de cambios *(agregado)*
- [x] Modal "Unsaved Changes" (Save/Discard/Cancel) en New/Open/Exit.
- [x] Titulo de ventana con indicador `*` de cambios sin guardar.
- [x] SDL_QUIT interceptado por guard de confirmacion.

## Semana 2 - Asset Pipeline minimo (estilo Unity/Godot) ✅ COMPLETADA

### 5) Asset Database
- [x] Crear `assets/` como origen y `library/` como cache/importados.
- [x] Definir GUID por asset (persistente).
- [x] Crear indice de assets (`asset_db.json`).
- [x] Evitar depender solo de path absoluto para referencias.

### 6) Importers iniciales
- [x] Importer de texturas/tilesets.
- [x] Importer de escenas.
- [x] Deteccion de cambios por hash/mtime.
- [x] Reimport solo de lo modificado.

### 7) File Browser / Inspector mejorados
- [x] Mostrar metadata de asset (GUID, tipo, dependencias).
- [x] Boton Reimport en assets seleccionados.
- [x] Mostrar estado dirty/no guardado en escena y assets.

## Semana 3 - Runtime escalable (Juego)

### 8) Separacion por sistemas
- [ ] Crear modulos por sistema:
- [ ] `MovementSystem`.
- [ ] `CombatSystem`.
- [ ] `AISystem`.
- [ ] `SpawnSystem`.
- [ ] `InteractionSystem`.
- [ ] Evitar logica grande centralizada en `Game.cpp`.

### 9) Data-driven gameplay
- [ ] Mover stats de enemigos/jugador a archivos de datos.
- [ ] Definir tablas para loot/exp/dificultad.
- [ ] Cargar configuraciones por semilla y por biome.

### 10) Navegacion/pathfinding
- [ ] Implementar A* sobre grid walkable.
- [ ] Soportar costo por tipo de terreno.
- [ ] Enemigos usan navegacion en lugar de persecucion directa simple.

## Semana 4 - Produccion, QA y rendimiento

### 11) Play Mode estable (Editor)
- [ ] Snapshot del estado de escena al entrar en Play.
- [ ] Restaurar estado al salir de Play (sin contaminar datos).
- [ ] Separar claramente modo Edit vs Play en UI.

### 12) Guardado/carga de partida
- [ ] Definir formato de savegame versionado.
- [ ] Cargar/guardar estado completo del mundo + entidades.
- [ ] Estrategia de migracion de versiones de save.

### 13) Testing y profiling minimo
- [ ] Test de determinismo procedural por seed.
- [ ] Test de carga de escenas invalidas/validas.
- [ ] Test basico de comandos undo/redo del editor.
- [ ] Medir tiempos por frame: update, render, AI, pathfinding.
- [ ] Registrar picos de frame time en log.

## Backlog de Alto Impacto (siguiente mes)

- [ ] Sistema de prefabs/arquetipos con overrides por instancia.
- [ ] Hot-reload de assets sin reiniciar editor.
- [ ] Inspector generico con metadata/reflection.
- [ ] Sistema de eventos desacoplado (`OnDamage`, `OnDeath`, etc.).
- [ ] Herramientas de validacion de contenido (map checks).
- [ ] Paquete de build reproducible para editor y juego.

## Definition of Done (Escalado inicial)

Marca como completado cuando se cumpla todo:

- [x] Editor con undo/redo global para operaciones de escena.
- [x] Asset DB con GUID + import/cache incremental.
- [ ] Runtime separado por sistemas principales.
- [ ] Save/load versionado funcionando.
- [ ] Suite minima de tests automatizados.
- [ ] Metricas de rendimiento visibles en editor.

## Resumen de Progreso

| Semana | Estado | Avance |
|--------|--------|--------|
| 1 - Editor Foundations | ✅ Completada | 100% |
| 2 - Asset Pipeline | ✅ Completada | 100% |
| 3 - Runtime Systems | ⬜ No iniciada | 0% |
| 4 - Produccion/QA | ⬜ No iniciada | 0% |

## Notas

- Prioridad recomendada: Editor -> Asset Pipeline -> Runtime -> QA.
- Regla de arquitectura: toda feature nueva debe entrar como sistema/modulo, no como bloque monolitico.
- Regla de datos: evitar hardcode de contenido jugable cuando sea posible.
