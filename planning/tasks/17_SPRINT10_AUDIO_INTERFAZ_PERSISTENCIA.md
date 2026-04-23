# Sprint 10 - Audio Espacial e Interfaz

## Meta del sprint

Agregar feedback de audio, triggers por eventos y persistencia de configuracion para mejorar respuesta del juego.

**Estado: ✅ COMPLETADO**

### Implementacion
- AudioEngine con miniaudio (single-header, procedural tones)
- AudioEventBindings: DamageEvent→220Hz, DeathEvent→110Hz, LevelUpEvent→880Hz, LootDropEvent→440Hz
- AudioSettingsRepository: volumenes persistidos en project_meta (SQLite)
- InputBindings3D: constantes de input extraidas de Renderer.cpp a struct configurable
- test_audio_smoke: 23 assertions, 0 fallos

---

## Tarea 10.1 (D89) - Integracion de motor de audio (miniaudio/SoLoud)

### Objetivo
Incorporar backend de audio para reproducir musica y SFX.

### Resultado esperado
- AudioEngine centralizado con lifecycle explicito.
- Canales master/music/sfx listos para configuracion.

### Archivos a crear
1. src/game/audio/AudioEngine.h
2. src/game/audio/AudioEngine.cpp

### Archivos a modificar
1. CMakeLists.txt
2. src/main.cpp

### Criterio de aceptacion
- Sonido reproducible en runtime sin glitch critico.

---

## Tarea 10.2 (D90) - Triggers de sonido por eventos de colision/ataque

### Objetivo
Disparar efectos sonoros desde eventos de gameplay reales.

### Resultado esperado
- Mapeo EventType -> AudioCue con control de spam.
- Sonido asociado a confirmacion de impacto.

### Archivos a crear
1. src/game/audio/AudioEventBindings.h
2. src/game/audio/AudioEventBindings.cpp

### Archivos a modificar
1. src/core/events/
2. src/game/audio/AudioEngine.cpp

### Criterio de aceptacion
- Sonido de ataque y colision funcional sin saturacion.

---

## Tarea 10.3 (D91) - Persistencia con SQLite de configuracion

### Objetivo
Guardar preferencias de audio y parametros base en SQLite.

### Resultado esperado
- Configuracion persistida por clave en project_meta.
- Guardado por cambio y no por polling continuo.

### Archivos a crear
1. src/game/audio/AudioSettingsRepository.h
2. src/game/audio/AudioSettingsRepository.cpp

### Archivos a modificar
1. src/core/db/ (acceso a project_meta)
2. src/game/audio/AudioEngine.cpp

### Criterio de aceptacion
- Volumenes se mantienen entre reinicios.

---

## Tarea 10.4 (D92) - Input mapping de camara (WASD)

### Objetivo
Definir controles consistentes para navegacion 3D.

### Resultado esperado
- Action map para movimiento y mouse look.
- Sensibilidad configurable y persistente.

### Archivos a crear
1. src/core/input/InputBindings3D.h
2. src/core/input/InputBindings3D.cpp

### Archivos a modificar
1. src/main.cpp
2. src/rendering/camera/

### Criterio de aceptacion
- Camara navegable con controles fluidos.

---

## Tarea 10.5 (D93) - QA de sprint

### Objetivo
Verificar estabilidad de audio + input + persistencia.

### Resultado esperado
- Smoke test combinado de init/play/stop/shutdown.
- Checklist operativa de salida del sprint.

### Archivos a crear
1. tests/test_audio_smoke.cpp
2. planning/tasks/SPRINT10_QA_CHECKLIST.md

### Archivos a modificar
1. tests/CMakeLists.txt
2. README.md

### Criterio de aceptacion
- No hay crash ni leak evidente en cierre.
