# Semana 4 - Produccion, QA, Save/Load y Profiling

## Meta de la semana

Subir calidad de produccion: play mode seguro en editor, save/load versionado, tests y telemetria de rendimiento.

---

## Tarea 4.1 - Play Mode con snapshot y rollback

### Objetivo
Permitir probar gameplay dentro del editor sin ensuciar la escena editada.

### Archivos a crear

1. src/editor/playmode/PlaySession.h
2. src/editor/playmode/PlaySession.cpp
- Guardar snapshot de SceneData + World state.
- Entrar/salir de Play y restaurar estado.

### Archivos a modificar

1. src/editor/EditorApp.h
- Estado de modo actual: Edit o Play.

2. src/editor/EditorApp.cpp
- Toolbar: botones Play/Stop.
- run(): cambiar pipeline segun modo.

### Criterio de aceptacion

- Al salir de Play, la escena vuelve exactamente al estado anterior.

---

## Tarea 4.2 - Save/Load versionado del juego

### Objetivo
Persistir progreso de partida de manera robusta y migrable.

### Carpetas a crear

1. saves/

### Archivos a crear

1. src/game/save/SaveGame.h
2. src/game/save/SaveGame.cpp
3. src/game/save/SaveVersioning.h
4. src/game/save/SaveVersioning.cpp

### Archivos a modificar

1. src/game/Game.cpp
- Integrar guardado manual y carga.

2. src/game/Game.h
- Estado serializable minimo necesario.

### Criterio de aceptacion

- Save/load conserva posicion, stats, enemigos vivos y seed actual.
- Maneja versiones viejas con fallback o migracion.

---

## Tarea 4.3 - Testing automatizado minimo

### Objetivo
Detectar regresiones funcionales en sistemas criticos.

### Archivos a crear

1. tests/CMakeLists.txt
2. tests/test_scene_serialization.cpp
3. tests/test_undo_redo_commands.cpp
4. tests/test_world_seed_determinism.cpp
5. tests/test_pathfinding.cpp

### Archivos a modificar

1. CMakeLists.txt
- add_subdirectory(tests) condicional por opcion BUILD_TESTING.

### Criterio de aceptacion

- Suite de tests corre en local y pasa consistentemente.

---

## Tarea 4.4 - Profiling y observabilidad

### Objetivo
Exponer metricas utiles para optimizacion temprana.

### Archivos a crear

1. src/core/profiling/Profiler.h
2. src/core/profiling/Profiler.cpp
- Timers por subsistema.

### Archivos a modificar

1. src/game/Game.cpp
- Medir update, render y sistemas.

2. src/editor/EditorApp.cpp
- Panel de Performance con frame time, ms por sistema y picos.

### Criterio de aceptacion

- El editor muestra metricas en tiempo real.
- Quedan logs de picos de frame.

---

## Entregables de la semana

- Play mode seguro con rollback.
- Save/load versionado.
- Tests basicos automatizados.
- Panel de profiling en editor.
