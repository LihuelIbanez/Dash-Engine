# Semana 3 - Runtime por Sistemas y Gameplay Data-Driven

## Meta de la semana

Romper la logica monolitica de runtime en sistemas desacoplados para escalar gameplay y rendimiento.

---

## Tarea 3.1 - RuntimeContext y bucle por sistemas

### Objetivo
Crear una orquestacion central donde cada sistema actualiza su dominio.

### Archivos a crear

1. src/game/runtime/RuntimeContext.h
- Referencias compartidas: World, player, enemies, deltaTime, etc.

2. src/game/runtime/ISystem.h
- Interfaz:
  - virtual void update(RuntimeContext&)
  - virtual const char* name() const

3. src/game/runtime/SystemScheduler.h
4. src/game/runtime/SystemScheduler.cpp
- Lista ordenada de sistemas y update secuencial.

### Archivos a modificar

1. src/game/Game.h
- Sustituir logica directa por scheduler + context.

2. src/game/Game.cpp
- Secciones update() y posiblemente processEvents().
- Delegar en sistemas.

### Criterio de aceptacion

- Game.cpp reduce responsabilidad y deja de concentrar reglas de gameplay.

---

## Tarea 3.2 - Migrar sistemas principales

### Objetivo
Separar las reglas en modulos testeables.

### Archivos a crear

1. src/game/systems/MovementSystem.h
2. src/game/systems/MovementSystem.cpp

3. src/game/systems/CombatSystem.h
4. src/game/systems/CombatSystem.cpp

5. src/game/systems/AISystem.h
6. src/game/systems/AISystem.cpp

7. src/game/systems/SpawnRewardSystem.h
8. src/game/systems/SpawnRewardSystem.cpp

### Archivos a modificar

1. src/game/Game.cpp
- Quitar logica duplicada de update y moverla a sistemas.

2. src/entities/Player.cpp y src/entities/Enemy.cpp (si aplica)
- Mantener entidades livianas y sin reglas globales de juego.

### Criterio de aceptacion

- Cada sistema tiene responsabilidad unica.
- Se mantiene comportamiento jugable previo.

---

## Tarea 3.3 - Gameplay data-driven

### Objetivo
Sacar stats hardcodeados a datos configurables.

### Carpetas a crear

1. assets/gameplay/

### Archivos a crear

1. assets/gameplay/player_classes.json
2. assets/gameplay/enemies.json
3. assets/gameplay/loot_tables.json

4. src/game/data/GameplayDatabase.h
5. src/game/data/GameplayDatabase.cpp
- Carga de json y validaciones.

### Archivos a modificar

1. src/game/Game.cpp
- Construccion de player/enemies desde GameplayDatabase.

2. src/entities/Enemy.h / src/entities/Enemy.cpp
- Soportar stats inicializados externamente.

### Criterio de aceptacion

- Cambiar JSON modifica balance sin recompilar.

---

## Tarea 3.4 - Navegacion con A*

### Objetivo
Reemplazar persecucion directa por pathfinding estable.

### Archivos a crear

1. src/game/nav/GridNav.h
2. src/game/nav/GridNav.cpp
- API:
  - findPath(start, goal, world)
  - costo por tipo de tile

### Archivos a modificar

1. src/world/World.h
2. src/world/World.cpp
- Exponer costo por terreno (no solo walkable).

3. src/game/systems/AISystem.cpp
- Usar GridNav para perseguir al player.

### Criterio de aceptacion

- Enemigos rodean obstaculos y llegan al objetivo de forma consistente.

---

## Entregables de la semana

- Runtime organizado por scheduler + sistemas.
- Gameplay configurable por datos.
- Navegacion A* integrada en IA.
