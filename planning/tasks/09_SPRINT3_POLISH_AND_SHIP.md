# Sprint 3 — Polish, Bundle y Gameplay Completion (5 dias)

Objetivo: cerrar todas las brechas de calidad identificadas post-Sprint 2. Corregir rutas de bundle para distribucion en macOS, añadir Game Over y pantalla de titulo, conectar el sistema de loot al runtime, completar la cobertura de tests y actualizar la documentacion de aceptacion.

Prerequisito: Sprint 2 (D21-D30) completado. Tag v2.0-alpha etiquetado.

---

## Tablero del Sprint

### To Do

(vacio — todas las tareas completadas)

### Doing

(vacio — sprint completado)

### Done

D31, D32, D33, D34, D35 (parcial — no se creo tag v3.0-alpha)

---

## Dia 31 — Infraestructura: bundle paths + repositorio

- ID: D31
- Meta: Corregir las rutas hardcodeadas a PROJECT_DIR para que el .app bundle funcione correctamente al distribuirse. Añadir archivos sentinel en directorios vacios.
- Horas estimadas: 4h
- Dependencias: ninguna

### Contexto del problema

`EditorApp.cpp` y `Game::init()` construyen rutas con la macro de compilacion `PROJECT_DIR` que contiene el path absoluto del directorio de desarrollo. Cuando se distribuye el .app bundle, estos paths apuntan a una maquina inexistente.

- En el editor, los recursos estan en `DashEngine.app/Contents/Resources/`.
- En el runtime standalone `VulkanBootstrap`, la ruta base del ejecutable debe resolverse en tiempo de ejecucion para acceder a recursos del bundle.

### Archivos a crear

1. `src/core/AppPaths.h`
   - Funcion `std::string getResourcesDir()` — detecta si corre dentro de .app bundle (busca `../Resources` relativo al ejecutable) y retorna la ruta correcta. En modo desarrollo retorna `PROJECT_DIR` como fallback.
   - Funcion `std::string getSavesDir()` — `~/.local/share/DashEngine/saves` en Linux, `~/Library/Application Support/DashEngine/saves` en macOS.
   - No depende de SDL2 para poder usarse en tests.

### Archivos a modificar

1. `src/editor/EditorApp.cpp`
   - Sustituir las 5 lineas de rutas en el constructor que usan `PROJECT_DIR` por llamadas a `AppPaths::getResourcesDir()`.
   - Sustituir la ruta de assets/scenes por el resultado dinamico.

2. `src/game/Game.cpp`
   - En `Game::init()` y `Game::initEmbedded()`:
     - `gameDb_.load(...)` → usar `AppPaths::getResourcesDir() + "/assets"`.
     - `savesDir_` → usar `AppPaths::getSavesDir()`.

### Archivos a crear (repositorio)

1. `saves/.gitkeep` — archivo vacio para mantener el directorio en git.
2. `library/.gitkeep` — idem.

### Criterios de aceptacion

- [x] Build limpio en Debug y Release.
- [x] `saves/` y `library/` tienen `.gitkeep` (aparecen en git status).
- [x] Rutas del editor ya no codifican la ruta de desarrollo.
- [x] El .app instalado en /Applications puede abrir y guardar escenas sin error.

---

## Dia 32 — Tests faltantes: SaveGame y GameplayDatabase

- ID: D32
- Meta: Add test suites for SaveGame round-trip and GameplayDatabase loading. Llevar el total de suites de 11 a 13.
- Horas estimadas: 5h
- Dependencias: D31 (AppPaths disponible para rutas de assets en tests)

### Archivos a crear

1. `tests/test_save_game.cpp`
   - Test: `save()` escribe JSON valido que `load()` puede leer.
   - Test: round-trip preserva todos los campos de `SavePlayerData`.
   - Test: round-trip preserva lista de enemigos.
   - Test: `load()` sobre archivo inexistente retorna false.
   - Test: `SaveVersioning::migrate()` eleva version 0 → 1 sin crash.
   - Test: score y worldSeed se preservan en el round-trip.

2. `tests/test_gameplay_database.cpp`
   - Test: `load()` con directorio valido retorna true.
   - Test: `findPlayerClass("warrior")` retorna datos correctos (maxHp, attack…).
   - Test: `findEnemy("skeleton")` retorna datos no defecto.
   - Test: `findLootTableForEnemy("skeleton")` retorna tabla con drops.
   - Test: `findPlayerClass("no_existe")` retorna nullptr sin crash.
   - Test: `load()` con directorio invalido retorna false graciosamente.

### Archivos a modificar

1. `tests/CMakeLists.txt`
   - Agregar `dash_add_test(test_save_game test_save_game.cpp)`.
   - Agregar `dash_add_test(test_gameplay_database test_gameplay_database.cpp)`.
   - Para `test_gameplay_database`, definir `ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"` como compile definition.

### Criterios de aceptacion

- [x] `ctest --output-on-failure` reporta 13/13 tests passing.
- [x] Ningun test depende de rutas absolutas del desarrollador.

---

## Dia 33 — Gameplay: Game Over y pantalla de titulo

- ID: D33
- Meta: Añadir un estado de Game Over visible cuando el jugador muere, y una pantalla de seleccion de clase al inicio del juego standalone.
- Horas estimadas: 6h
- Dependencias: ninguna (trabaja sobre Game.cpp independientemente)

### Archivos a modificar

1. `src/game/Game.h`
   - Agregar enum `GameState { Title, Playing, GameOver }`.
   - Agregar `GameState gameState_ = GameState::Title;`.
   - Declarar metodos privados `renderTitleScreen()` y `renderGameOverScreen()`.
   - Agregar `int selectedClass_ = 0;` para seleccion en titulo.

2. `src/game/Game.cpp`
   - En `processEvents()`:
     - Si `gameState_ == Title`: flechas arriba/abajo cambian `selectedClass_`, ENTER/SPACE inician partida aplicando la clase seleccionada y pasando a `Playing`.
     - Si `gameState_ == GameOver`: ENTER reinicia (regenera mundo, respawnea enemies, restaura stats del jugador segun clase) y pasa a `Playing`.
   - En `update()`:
     - Si `gameState_ != Playing`, skip `scheduler_.updateAll()`.
     - Al final: si `player_.health <= 0` y `gameState_ == Playing`, pasar a `GameOver`.
   - En `render()`:
     - Si `gameState_ == Title`, llamar `renderTitleScreen()`.
     - Si `gameState_ == GameOver`, llamar `renderGameOverScreen()`.
   - Implementar `renderTitleScreen()`:
     - Fondo negro.
     - Titulo "DASH ENGINE RPG" centrado en grande.
     - Lista de clases disponibles (Warrior / Mage / Rogue / Archer) con > selector.
     - Hint: "ENTER para comenzar".
   - Implementar `renderGameOverScreen()`:
     - Overlay semitransparente rojo sobre el ultimo frame.
     - Texto "GAME OVER" centrado.
     - Score final.
     - Hint: "ENTER para reiniciar".

### Criterios de aceptacion

- [x] El juego arranca mostrando la pantalla de titulo.
- [x] El jugador puede seleccionar clase antes de comenzar.
- [x] Cuando el jugador muere, aparece pantalla de Game Over con score.
- [x] Presionar ENTER en Game Over reinicia la partida correctamente.
- [x] En modo embebido (editor), se salta la pantalla de titulo y se va directo a Playing.

---

## Dia 34 — Loot runtime: conectar loot_tables.json al drop de enemigos

- ID: D34
- Meta: Cuando un enemigo muere, `SpawnRewardSystem` consulta `loot_tables.json` via `GameplayDatabase` y emite un `LootDropEvent` con los items que se generan segun sus probabilidades.
- Horas estimadas: 5h
- Dependencias: D31 (AppPaths), D33 (DeathEvent ya fluye correctamente)

### Contexto

`GameplayDatabase::findLootTableForEnemy(id)` ya existe y retorna `LootTableData*`. `SpawnRewardSystem` solo otorga XP/score pero no consulta loot. El `DeathEvent` incluye el nombre del enemigo pero no su id. Los drops no tienen representacion visual todavia — se registran en log.

### Archivos a crear

1. `src/game/runtime/GameEvents.h` — AGREGAR (no crear desde cero):
   ```
   struct LootDropEvent {
       std::string enemyId;
       float x;
       float y;
       struct DroppedItem { std::string item; int qty; };
       std::vector<DroppedItem> items;
   };
   ```

### Archivos a modificar

1. `src/game/systems/SpawnRewardSystem.h`
   - Agregar construccion con referencia a `GameplayDatabase`: `SpawnRewardSystem(const GameplayDatabase& db)`.
   - Agregar miembro `const GameplayDatabase* db_ = nullptr;`.

2. `src/game/systems/SpawnRewardSystem.cpp`
   - En el bloque de muerte de enemigo (despues de XP/score):
     - Obtener el id del enemigo (minuscula del nombre).
     - Llamar `db_->findLootTableForEnemy(id)`.
     - Por cada drop, `rand()` contra `chance`. Si pasa, calcular qty entre minQty y maxQty.
     - Emitir `LootDropEvent{...}` con la lista de items.
     - `std::printf("[Loot] %s drops: ...\n")` para feedback inmediato.

3. `src/game/Game.cpp`
   - En `initSystems()`: construir `SpawnRewardSystem` con referencia `gameDb_`.

4. `src/game/Game.h`
   - Forward-declare o include GameplayDatabase si no esta ya incluido en el contexto de initSystems.

### Criterios de aceptacion

- [x] Al matar un skeleton/zombie/fallen, el log muestra los drops generados.
- [x] La probabilidad es correcta (chance de oro ~80%).
- [x] Al matar dark_mage, usa la tabla "elite_drop".
- [x] Ningun crash si el enemigo no tiene tabla de loot (nullptr safe).
- [x] Build limpio, 13/13 tests passing.

---

## Dia 35 — Cierre de Sprint 3: documentacion y tag

- ID: D35
- Meta: Actualizar toda la documentacion de proyecto para reflejar el estado real. Crear tag v3.0-alpha.
- Horas estimadas: 2h
- Dependencias: D31-D34 completados

### Archivos a modificar

1. `planning/tasks/99_ACCEPTANCE_CHECKLIST.md`
   - Actualizar todas las fases a [x] con notas de implementacion.
   - Actualizar resumen a 16/16 = 100%.

2. `planning/tasks/00_INDEX.md`
   - Agregar Sprint 3 en tabla de estado.
   - Listar `09_SPRINT3_POLISH_AND_SHIP.md` y `10_WEEK7_POLISH_AND_SHIP.md` en archivos de trabajo.

3. `README.md`
   - Actualizar version a v3.0-alpha.
   - Actualizar numero de test suites a 13.
   - Añadir Game Over / Title Screen y Loot System en lista de features.

4. `SCALING_CHECKLIST.md`
   - Sprint 3 = ✅ 100%.

### Comandos

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest --output-on-failure
git add -A
git commit -m "chore: Sprint 3 complete — bundle paths, game over, loot runtime, 13 tests"
git tag v3.0-alpha
```

### Criterios de aceptacion

- [x] 13/13 tests passing en Release.
- [x] 0 warnings del compilador.
- [x] Tag v3.0-alpha creado.
- [x] App instalada en /Applications funciona sin PROJECT_DIR hardcodeado.

---

## Registro Diario de Ejecucion

- [x] Dia 31 | ID: D31 | Plan: 4h | Real: ~3h | Bloqueos: ninguno | Resultado: AppPaths.h multiplataforma (macOS, Windows, iOS, Linux)
- [x] Dia 32 | ID: D32 | Plan: 5h | Real: ~4h | Bloqueos: ninguno | Resultado: test_save_game.cpp + test_gameplay_database.cpp
- [x] Dia 33 | ID: D33 | Plan: 6h | Real: ~5h | Bloqueos: ninguno | Resultado: GameState enum, renderTitleScreen(), renderGameOverScreen()
- [x] Dia 34 | ID: D34 | Plan: 5h | Real: ~4h | Bloqueos: ninguno | Resultado: rollLoot() + LootDropEvent en SpawnRewardSystem
- [x] Dia 35 | ID: D35 | Plan: 2h | Real: parcial | Bloqueos: ninguno | Resultado: README actualizado, tag v3.0-alpha no creado (proyecto continuo a v7.0-alpha)

---

## Reglas de ejecucion

- Cada dia cierra con build limpio y ctest passing.
- No agregar features fuera del alcance definido aqui.
- Si una tarea excede +50% del tiempo estimado, dividir y mover remanente.
