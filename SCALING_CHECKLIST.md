# Isometric RPG - Scaling Checklist (30 dias)

Este plan prioriza primero el Editor (como Unity/Unreal/Godot) y luego el Runtime del juego.
Marca cada item con [x] cuando este completado.

## Objetivos de Escalado

- [ ] Reducir acoplamiento entre editor, datos y runtime.
- [ ] Soportar crecimiento de contenido (assets, escenas, entidades).
- [ ] Mejorar mantenibilidad y velocidad de iteracion.
- [ ] Preparar base para trabajo en equipo y features de produccion.

## Semana 1 - Fundaciones del Editor (Arquitectura)

### 1) Modelo de datos de escena y componentes
- [ ] Definir `EntityId` estable (int/uint64) para editor y runtime.
- [ ] Separar datos de escena de la logica visual del editor.
- [ ] Crear estructuras base de componentes (`Transform`, `Render`, `Combat`, etc.).
- [ ] Versionar formato de escena (`scene_version`).

### 2) Command System global (Undo/Redo real)
- [ ] Crear interfaz `ICommand` (`apply()`, `undo()`, `name()`).
- [ ] Implementar `CommandStack` global del editor.
- [ ] Migrar acciones actuales a comandos:
- [ ] Pintar tile.
- [ ] Colocar enemigo.
- [ ] Borrar entidad/tile.
- [ ] Mover/editar propiedades de entidad.
- [ ] Añadir atajos globales: Cmd+Z (undo), Cmd+Shift+Z (redo).

### 3) Persistencia robusta
- [ ] Guardado canonico (orden estable en JSON).
- [ ] Validacion basica al cargar escenas (campos requeridos).
- [ ] Manejo de errores de parseo con mensajes claros en Build Log.

## Semana 2 - Asset Pipeline minimo (estilo Unity/Godot)

### 4) Asset Database
- [ ] Crear `assets/` como origen y `library/` como cache/importados.
- [ ] Definir GUID por asset (persistente).
- [ ] Crear indice de assets (`asset_db.json`).
- [ ] Evitar depender solo de path absoluto para referencias.

### 5) Importers iniciales
- [ ] Importer de texturas/tilesets.
- [ ] Importer de escenas.
- [ ] Deteccion de cambios por hash/mtime.
- [ ] Reimport solo de lo modificado.

### 6) File Browser / Inspector mejorados
- [ ] Mostrar metadata de asset (GUID, tipo, dependencias).
- [ ] Boton Reimport en assets seleccionados.
- [ ] Mostrar estado dirty/no guardado en escena y assets.

## Semana 3 - Runtime escalable (Juego)

### 7) Separacion por sistemas
- [ ] Crear modulos por sistema:
- [ ] `MovementSystem`.
- [ ] `CombatSystem`.
- [ ] `AISystem`.
- [ ] `SpawnSystem`.
- [ ] `InteractionSystem`.
- [ ] Evitar logica grande centralizada en `Game.cpp`.

### 8) Data-driven gameplay
- [ ] Mover stats de enemigos/jugador a archivos de datos.
- [ ] Definir tablas para loot/exp/dificultad.
- [ ] Cargar configuraciones por semilla y por biome.

### 9) Navegacion/pathfinding
- [ ] Implementar A* sobre grid walkable.
- [ ] Soportar costo por tipo de terreno.
- [ ] Enemigos usan navegacion en lugar de persecucion directa simple.

## Semana 4 - Produccion, QA y rendimiento

### 10) Play Mode estable (Editor)
- [ ] Snapshot del estado de escena al entrar en Play.
- [ ] Restaurar estado al salir de Play (sin contaminar datos).
- [ ] Separar claramente modo Edit vs Play en UI.

### 11) Guardado/carga de partida
- [ ] Definir formato de savegame versionado.
- [ ] Cargar/guardar estado completo del mundo + entidades.
- [ ] Estrategia de migracion de versiones de save.

### 12) Testing y profiling minimo
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

- [ ] Editor con undo/redo global para operaciones de escena.
- [ ] Asset DB con GUID + import/cache incremental.
- [ ] Runtime separado por sistemas principales.
- [ ] Save/load versionado funcionando.
- [ ] Suite minima de tests automatizados.
- [ ] Metricas de rendimiento visibles en editor.

## Notas

- Prioridad recomendada: Editor -> Asset Pipeline -> Runtime -> QA.
- Regla de arquitectura: toda feature nueva debe entrar como sistema/modulo, no como bloque monolitico.
- Regla de datos: evitar hardcode de contenido jugable cuando sea posible.
