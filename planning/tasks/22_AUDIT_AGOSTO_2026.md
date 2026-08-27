# Auditoria Tecnica — Agosto 2026

**Estado: ✅ COMPLETADA** — los 3 bugs y las 6 mejoras fueron implementados y verificados (`ctest`: 26/26 en verde).

## Contexto

Analisis completo del proyecto realizado el 2026-08-27 sobre la rama `feat/vulkan` (HEAD `06a15b6`). Incluye ejecucion real de la suite `ctest`, revision de arquitectura, seguridad y deuda tecnica acumulada.

## Resumen

| Item                    | Resultado                                                                                       |
| ----------------------- | ----------------------------------------------------------------------------------------------- |
| Tests (`ctest`)       | **26/26 pasan (100%)** tras los fixes — antes 24/26                                            |
| Codigo propio           | ~26.700 lineas (sin contar vendored)                                                            |
| Seguridad               | Sin hallazgos criticos (SQL siempre parametrizado, sin command injection con input de usuario)  |
| Documentacion de estado | Sincronizada con la realidad de QA                                                              |

---

## Bugs a resolver

### BUG-1 — Posicion de entidad se pierde en save/load si el TransformComponent no esta sincronizado con EntityData::x/y

- **Estado:** ✅ Resuelto
- **Prioridad:** Alta
- **Sintoma:** `test_entity_registry` (subtest `test_scene_components_roundtrip`) falla en las aserciones `player transform.x` / `player transform.y`.
- **Causa raiz:** en `SceneData::loadFromJson` ([src/editor/SceneData.cpp](../../src/editor/SceneData.cpp)), el bucle de post-procesado tras cargar entidades sobreescribe incondicionalmente `TransformComponent::x/y` con los campos legacy `EntityData::x/y`:
  ```cpp
  if (auto* tf = std::get_if<TransformComponent>(&comp)) {
      tf->x = ed.x;
      tf->y = ed.y;
      ...
  }
  ```

  `toJson()` serializa `ej["x"]/ej["y"]` desde `EntityData::x/y`, no desde el `TransformComponent`. Si algo construye o edita `components` sin mantener `EntityData::x/y` sincronizados (el test lo hace; tambien podria pasar en importers o generacion programatica de escenas), la posicion real del componente se descarta al recargar. Los comandos del editor (`MoveEntityCommand`, `EditComponentFieldCommand`) sincronizan ambos campos manualmente, pero es un parche por callsite, no una garantia estructural.
- **Archivos involucrados:**
  - [src/editor/SceneData.cpp](../../src/editor/SceneData.cpp)
  - [src/editor/commands/MoveEntityCommand.cpp](../../src/editor/commands/MoveEntityCommand.cpp)
  - [src/editor/commands/EditComponentFieldCommand.cpp](../../src/editor/commands/EditComponentFieldCommand.cpp)
  - [tests/test_entity_registry.cpp](../../tests/test_entity_registry.cpp)
- **Fix sugerido:** elegir una unica fuente de verdad para la posicion. Opcion recomendada: eliminar el paso que sobreescribe `tf->x/y` desde `ed.x/y` al cargar, y en su lugar sincronizar `ed.x/y` desde el `TransformComponent` (si existe) tanto al guardar (`toJson`) como despues de cargar. Mantener `EntityData::x/y` solo como cache derivada para codigo legacy que no usa componentes.
- **Fix aplicado:** en `toJson()`, `ej["x"]/ej["y"]` ahora se derivan del `TransformComponent` cuando existe. En `loadFromJson()` se invirtio la direccion de sincronizacion: el `TransformComponent` es autoritativo y se copia hacia `ed.x/ed.y` (con clamp a los limites del mundo), en vez de ser sobreescrito.
- **Criterio de aceptacion:**
  - `test_scene_components_roundtrip` pasa sin modificar los valores esperados.
  - Guardar y recargar una escena preserva la posicion del `TransformComponent` incluso si se edito sin pasar por `MoveEntityCommand`.

### BUG-2 — Test de validacion de contenido acoplado a generacion procedural real

- **Estado:** ✅ Resuelto
- **Prioridad:** Media
- **Sintoma:** `test_content_validation` (subtest `test_valid_scene`) espera 0 errores, pero `ContentValidator` reporta al menos un error (probablemente "Player is on a non-walkable tile").
- **Causa raiz:** el test genera un `World` real con `world.generate(42)` y asume que el tile `(8,8)` es caminable. Cambios recientes de generacion de terreno (Sprint 14: TILE_SCALE, water bodies, cliffs) probablemente movieron que tile fuera de tierra firme para ese seed. El test es fragil porque depende de un detalle de implementacion de la generacion procedural en lugar de un `World` fijo/controlado.
- **Archivos involucrados:**
  - [tests/test_content_validation.cpp](../../tests/test_content_validation.cpp)
  - [src/editor/validation/ContentValidator.cpp](../../src/editor/validation/ContentValidator.cpp)
  - [src/world/World.cpp](../../src/world/World.cpp) (`isWalkable`, generacion procedural)
- **Fix sugerido:** reemplazar `world.generate(42)` por un `World` con tiles seteadas manualmente a `Grass`/`walkable = true` en las posiciones usadas por el test (como ya hace `test_pathfinding.cpp` en `test_straight_path`), para desacoplar el test de la generacion procedural real.
- **Fix aplicado:** `test_valid_scene` ahora construye un `World` totalmente caminable seteando cada tile a `Grass`/`walkable = true`, en vez de llamar a `world.generate(42)`.
- **Criterio de aceptacion:** `test_valid_scene` pasa de forma determinista independientemente de cambios futuros en los parametros de generacion de terreno.

### BUG-3 — Asercion de version de escena desactualizada

- **Estado:** ✅ Resuelto
- **Prioridad:** Baja
- **Sintoma:** dentro del mismo `test_scene_components_roundtrip`, `ASSERT_EQ(loaded.sceneVersion, 2, "loaded version is 2")` falla.
- **Causa raiz:** `SceneData::kCurrentVersion` ([src/editor/SceneData.h](../../src/editor/SceneData.h)) ya vale `5`; el test quedo con el valor hardcodeado de cuando se escribio (v2).
- **Fix sugerido:** comparar contra `SceneData::kCurrentVersion` en vez de un literal, para que el test no se rompa en cada bump de version.
- **Fix aplicado:** la asercion ahora compara `loaded.sceneVersion` contra `SceneData::kCurrentVersion`.
- **Criterio de aceptacion:** el test sigue siendo valido aunque `kCurrentVersion` vuelva a incrementarse.

---

## Puntos de mejora (deuda tecnica, no bloqueante)

- [x] **MEJORA-1 — Dividir `EditorApp.cpp`** ([src/editor/EditorApp.cpp](../../src/editor/EditorApp.cpp), ~4.160 lineas). Candidato a god-object; ya existen `panels/`, `commands/`, `project/` como destinos naturales para extraer logica (build & run, manejo de proyecto, dibujo de paneles sueltos).
  - **Hecho:** el File Browser + File Editor (estado `OpenFile`, undo/redo de texto, arbol de directorios, ~310 lineas) se extrajeron a [src/editor/panels/FileEditorPanel.h](../../src/editor/panels/FileEditorPanel.h) / [.cpp](../../src/editor/panels/FileEditorPanel.cpp), siguiendo el patron de callback de log que ya usaban `AssetBrowserPanel` y `ValidationPanel`. `EditorApp.cpp` bajo de 4.159 a ~3.850 lineas. Queda margen para seguir extrayendo (viewport, build & run) en futuras iteraciones.
- [x] **MEJORA-2 — Deduplicar dependencias vendored.** `stb_image.h` / `stb_image_write.h` estan descargados y versionados por separado en [src/editor/](../../src/editor/) y [src/game/rendering/](../../src/game/rendering/stb_image.h). Consolidar en un unico directorio referenciado por ambos targets.
  - **Hecho:** se verifico que ambas copias de `stb_image.h` eran identicas byte a byte. Se elimino `src/editor/stb_image.h` y todos los consumidores (`SpriteEditorPanel.cpp`, `stb_impl.cpp`, `TextureCache.cpp`, `test_sprite_editor.cpp`) apuntan ahora a `game/rendering/stb_image.h`, que ya era la ruta usada por el lado Vulkan. Se actualizo `STB_READ_PATH` en [CMakeLists.txt](../../CMakeLists.txt) para que la descarga automatica escriba en la ruta canonica.
- [x] **MEJORA-3 — Sacar `editor.logcd` del repo.** Era un log de ejecucion commiteado en la raiz, no cubierto por `.gitignore`.
  - **Hecho:** archivo eliminado del indice de git y del working tree; se agrego el patron `*.logcd` bajo una seccion "Runtime logs" en [.gitignore](../../.gitignore).
- [x] **MEJORA-4 — Clarificar el target `IsometricRPG`.** Sigue compilando desde [src/main.cpp](../../src/main.cpp) pero no aparecia documentado en el README ni lo usa `GameBuildPipeline`.
  - **Hecho:** se documento en el README como runtime 2D legado accesible via `dash game` (comando que ya existia en [dash.sh](../../dash.sh)), aclarando que no participa del pipeline Build & Run. Se opto por documentarlo en vez de removerlo porque el CLI ya lo expone como modo alternativo.
- [x] **MEJORA-5 — Sincronizar documentacion de estado con la realidad de QA.**
  - **Hecho:** [README.md](../../README.md) ahora indica explicitamente 26/26 tests en verde con fecha de verificacion, y [SCALING_CHECKLIST.md](../../SCALING_CHECKLIST.md) incluye una fila para esta auditoria enlazando a este documento.
- [x] **MEJORA-6 (hardening) — Evitar construccion de comandos de shell via concatenacion de strings.**
  - **Hecho:** la invocacion de `osascript` que traia la ventana del juego al frente en macOS usaba `std::system()` con un comando de shell embebido (incluyendo comillas anidadas y `&`). Se reemplazo por `spawnTrackedProcess("/usr/bin/osascript", ...)` con los argumentos en un vector, eliminando el intermediario de shell.
  - **Pendiente deliberado:** los `popen()` de [EditorApp.cpp](../../src/editor/EditorApp.cpp) y [GameBuildPipeline.cpp](../../src/editor/project/GameBuildPipeline.cpp) se mantienen porque necesitan capturar stdout del build de forma incremental para el Build Log. Solo interpolan `BUILD_DIR` (constante de compilacion), por lo que no hay superficie de inyeccion con input de usuario. Migrarlos requeriria pipes manuales con `posix_spawn`, fuera del alcance de esta auditoria.
