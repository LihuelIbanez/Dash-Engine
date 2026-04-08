# Sprint 2 — Features Avanzadas (10 dias)

Objetivo: evolucionar el engine hacia una arquitectura de componentes, sistema de eventos desacoplado, prefabs reutilizables y herramientas de produccion (hot-reload, validacion, packaging).

Prerequisito: Sprint 1 (D01-D20) completado.

## Tablero del Sprint (To Do / Doing / Done)

### To Do

(vacio)

### Doing

(vacio)

### Done

- [x] D21 - Sistema de eventos desacoplado (6h)
- [x] D22 - Comando MoveEntity + EditProperty (6h)
- [x] D23 - Estructuras base de componentes (6h)
- [x] D24 - EntityRegistry + migracion de datos (6h)
- [x] D25 - Inspector generico con reflection (6h)
- [x] D26 - Sistema de prefabs/arquetipos (6h)
- [x] D27 - Hot-reload de assets (6h)
- [x] D28 - Herramientas de validacion de contenido (6h)
- [x] D29 - Paquete de build reproducible (6h)
- [x] D30 - Tests de regresion + cierre de sprint (6h)

## Registro Diario de Ejecucion

- [ ] Dia 21 | ID: D21 | Plan: 6h | Real: __h | Bloqueos: __ | Resultado: __
- [x] Dia 22 | ID: D22 | Plan: 6h | Real: 6h | Bloqueos: — | Resultado: MoveEntityCommand + EditPropertyCommand implementados, drag-to-move en viewport, properties panel via comandos, test_move_edit_commands 5/5 passing
- [x] Dia 23 | ID: D23 | Plan: 6h | Real: 6h | Bloqueos: — | Resultado: 7 componentes POD + ComponentSerialization JSON roundtrip, test_component_serialization 9/9 passing
- [x] Dia 24 | ID: D24 | Plan: 6h | Real: 6h | Bloqueos: — | Resultado: EntityRegistry, SceneData v2 con components, migracion v1→v2, test_entity_registry 9/9 passing
- [x] Dia 25 | ID: D25 | Plan: 6h | Real: 6h | Bloqueos: — | Resultado: Reflection.h/cpp con 7 metas, inspector genérico con DragFloat/DragInt/InputText/Checkbox/Combo, Add/Remove/EditComponentFieldCommand con undo/redo, 7/7 tests passing
- [x] Dia 26 | ID: D26 | Plan: 6h | Real: 6h | Bloqueos: — | Resultado: PrefabAsset load/save/instantiate/overrides, PrefabImporter, goblin_warrior.json ejemplo, test_prefab_system 4/4 passing
- [x] Dia 27 | ID: D27 | Plan: 6h | Real: 6h | Bloqueos: — | Resultado: FileWatcher scan/reset/hash incremental, reimportChanged, hot-reload loop en editor, menu Auto-Reload + Scan for Changes
- [x] Dia 28 | ID: D28 | Plan: 6h | Real: 6h | Bloqueos: — | Resultado: ContentValidator 10+ checks, ValidationPanel con tabla y click-to-navigate, toolbar Validate button, test_content_validation 8/8 passing
- [x] Dia 29 | ID: D29 | Plan: 6h | Real: 6h | Bloqueos: — | Resultado: cmake/Packaging.cmake, VersionInfo.h.in, CPack DragNDrop, About modal en editor (v2.0.0-alpha, commit, fecha), install_app.sh con recursos
- [x] Dia 30 | ID: D30 | Plan: 6h | Real: 6h | Bloqueos: — | Resultado: test_event_system 6/6, test_hot_reload 6/6, build Release 0 warnings, 11/11 suites passing, tag v2.0-alpha creado

## Reglas de ejecucion

- Jornada objetivo: 6 horas efectivas por dia.
- Cada dia cierra con build limpio y tests passing.
- No iniciar una tarea si su dependencia directa no esta completada.
- Si una tarea excede +25% de horas, dividir y mover remanente al siguiente dia.

---

## Dia 21

- ID: D21
- Meta: Sistema de eventos desacoplado entre sistemas de runtime.
- Horas: 6h
- Dependencias: D20
- Tareas:
  - Crear EventDispatcher con subscribe/emit tipado por template.
  - Definir eventos core: DamageEvent, DeathEvent, LevelUpEvent, HealthChangeEvent.
  - Cola de eventos por frame (FIFO) con flush al final del update.
  - Integrar dispatcher en RuntimeContext.
  - Migrar CombatSystem para emitir DamageEvent/DeathEvent en lugar de mutacion directa.
  - Migrar SpawnRewardSystem para suscribirse a DeathEvent.
- Archivos:
  - Crear: src/core/events/EventDispatcher.h
  - Crear: src/core/events/GameEvents.h
  - Modificar: src/game/runtime/RuntimeContext.h
  - Modificar: src/game/systems/CombatSystem.cpp
  - Modificar: src/game/systems/SpawnRewardSystem.cpp
  - Modificar: src/game/Game.cpp

## Dia 22

- ID: D22
- Meta: Comandos de mover entidad y editar propiedades en editor.
- Horas: 6h
- Dependencias: D20
- Tareas:
  - Crear MoveEntityCommand (drag & drop en viewport con undo/redo).
  - Crear EditPropertyCommand generico (nombre, clase, posicion x/y con undo/redo).
  - Integrar drag de entidades en drawViewport() (solo modo Edit).
  - Integrar edicion de propiedades en drawPropertiesPanel() via comandos.
  - Tests: mover entidad undo/redo, editar propiedad undo/redo.
- Archivos:
  - Crear: src/editor/commands/MoveEntityCommand.h
  - Crear: src/editor/commands/MoveEntityCommand.cpp
  - Crear: src/editor/commands/EditPropertyCommand.h
  - Crear: src/editor/commands/EditPropertyCommand.cpp
  - Modificar: src/editor/EditorApp.cpp (viewport drag + properties panel)
  - Crear: tests/test_move_edit_commands.cpp
  - Modificar: tests/CMakeLists.txt

## Dia 23

- ID: D23
- Meta: Definir estructuras base de componentes.
- Horas: 6h
- Dependencias: D21
- Tareas:
  - Crear structs de componentes: TransformComponent, RenderComponent, StatsComponent, HealthComponent, ManaComponent, CombatComponent, AIComponent.
  - Cada componente es un POD/struct simple, sin logica.
  - Crear ComponentType enum con IDs estables para serialization.
  - Crear serializacion JSON to_json/from_json para cada componente.
  - Crear ComponentVariant (std::variant de todos los tipos de componentes).
  - Tests: roundtrip JSON de cada componente.
- Archivos:
  - Crear: src/core/components/Components.h
  - Crear: src/core/components/ComponentSerialization.h
  - Crear: src/core/components/ComponentSerialization.cpp
  - Crear: tests/test_component_serialization.cpp
  - Modificar: tests/CMakeLists.txt

## Dia 24

- ID: D24
- Meta: EntityRegistry + migracion del modelo de entidades.
- Horas: 6h
- Dependencias: D23
- Tareas:
  - Crear EntityRegistry: almacena entidades como ID + vector de componentes.
  - API: createEntity(), destroyEntity(), addComponent<T>(), getComponent<T>(), hasComponent<T>().
  - Extender EntityData en SceneData para serializar componentes en JSON.
  - Migracion: scene_version 1→2, convertir entidades antiguas (Player/Enemy) a representacion por componentes.
  - Backward compat: cargar escenas v1 y convertir automaticamente.
  - Tests: crear entidad con componentes, serializar/deserializar, migracion v1→v2.
- Archivos:
  - Crear: src/core/components/EntityRegistry.h
  - Crear: src/core/components/EntityRegistry.cpp
  - Modificar: src/editor/SceneData.h
  - Modificar: src/editor/SceneData.cpp
  - Crear: tests/test_entity_registry.cpp
  - Modificar: tests/CMakeLists.txt

## Dia 25

- ID: D25
- Meta: Inspector generico con reflection para componentes.
- Horas: 6h
- Dependencias: D24
- Tareas:
  - Crear sistema de reflection minimo: PropertyInfo (nombre, tipo, offset/getter/setter).
  - Registrar propiedades de cada componente con macros o funciones helper.
  - Refactorizar drawPropertiesPanel() para iterar componentes del EntityRegistry.
  - Renderizar automaticamente: float → DragFloat, int → DragInt, string → InputText, bool → Checkbox, enum → Combo.
  - Edicion de propiedades via EditPropertyCommand (undo/redo).
  - Boton "+ Add Component" y "- Remove Component" en inspector.
- Archivos:
  - Crear: src/core/components/Reflection.h
  - Crear: src/core/components/Reflection.cpp
  - Modificar: src/editor/EditorApp.cpp (drawPropertiesPanel refactor)
  - Modificar: src/editor/EditorApp.h

## Dia 26

- ID: D26
- Meta: Sistema de prefabs/arquetipos con overrides por instancia.
- Horas: 6h
- Dependencias: D25
- Tareas:
  - Crear PrefabAsset: JSON con lista de componentes default (archetype).
  - Crear PrefabImporter para el asset pipeline.
  - Extender EntityData con campo prefabGuid (opcional).
  - Al instanciar un prefab, copiar componentes base y permitir overrides por instancia.
  - Inspector muestra valores override en negrita, boton "Reset to Prefab".
  - Drag de prefab desde Asset Browser al viewport para instanciar.
  - Guardar solo overrides en la escena (no datos repetidos del prefab).
- Archivos:
  - Crear: src/core/components/PrefabAsset.h
  - Crear: src/core/components/PrefabAsset.cpp
  - Crear: src/assets/importers/PrefabImporter.h
  - Crear: src/assets/importers/PrefabImporter.cpp
  - Crear: assets/prefabs/.gitkeep
  - Modificar: src/editor/SceneData.h
  - Modificar: src/editor/SceneData.cpp
  - Modificar: src/editor/EditorApp.cpp (inspector + viewport drag)
  - Crear: tests/test_prefab_system.cpp
  - Modificar: tests/CMakeLists.txt

## Dia 27

- ID: D27
- Meta: Hot-reload de assets sin reiniciar editor.
- Horas: 6h
- Dependencias: D22
- Tareas:
  - Crear FileWatcher: polling periodico (cada 1s) de hash de archivos en assets/.
  - Detectar archivos nuevos, modificados o eliminados.
  - Trigger automatico de ImportManager::importAsset() para archivos cambiados.
  - Notificar al editor via callback (log + actualizar Asset Browser).
  - Reload de GameplayDatabase en caliente si gameplay/*.json cambia.
  - Toggle "Auto-Reload" en menu View del editor.
  - Proteger contra reload durante Play mode (encolar y aplicar al salir).
- Archivos:
  - Crear: src/assets/FileWatcher.h
  - Crear: src/assets/FileWatcher.cpp
  - Modificar: src/assets/ImportManager.h
  - Modificar: src/assets/ImportManager.cpp
  - Modificar: src/editor/EditorApp.h
  - Modificar: src/editor/EditorApp.cpp

## Dia 28

- ID: D28
- Meta: Herramientas de validacion de contenido.
- Horas: 6h
- Dependencias: D26
- Tareas:
  - Crear ContentValidator: recorre escena y reporta problemas.
  - Validaciones de mapa: entidades fuera de bounds, tiles no-walkable bajo player, zonas inaccesibles.
  - Validaciones de datos: prefabs rotos (GUID no encontrado), enemigos sin stats validos, entidades duplicadas.
  - Validaciones de gameplay: player ausente, spawn en agua, enemies sin pathfinding posible al player.
  - Panel "Validation" en editor con lista de warnings/errors clicables (navegar a la entidad/tile problemática).
  - Boton "Validate Scene" en menu y toolbar.
  - Tests: escena valida sin errores, escena con errores conocidos detectados.
- Archivos:
  - Crear: src/editor/validation/ContentValidator.h
  - Crear: src/editor/validation/ContentValidator.cpp
  - Crear: src/editor/panels/ValidationPanel.h
  - Crear: src/editor/panels/ValidationPanel.cpp
  - Modificar: src/editor/EditorApp.h
  - Modificar: src/editor/EditorApp.cpp
  - Crear: tests/test_content_validation.cpp
  - Modificar: tests/CMakeLists.txt

## Dia 29

- ID: D29
- Meta: Paquete de build reproducible para editor y juego.
- Horas: 6h
- Dependencias: D27
- Tareas:
  - Crear script de packaging multiplataforma (CMake install + CPack).
  - macOS: generar .app bundle completo con assets embebidos y scene default.
  - Copiar assets/, library/, scenes/ al bundle.
  - Embedir versión y commit hash en binario (compile_definitions).
  - Crear target "package" en CMake que genera distribuible.
  - Verificar que el juego standalone arranca correctamente desde el bundle.
  - Panel "About" en editor mostrando version, commit, fecha de build.
- Archivos:
  - Crear: cmake/Packaging.cmake
  - Modificar: CMakeLists.txt (include Packaging, version defines)
  - Modificar: packaging/Info.plist
  - Modificar: packaging/install_app.sh
  - Modificar: src/editor/EditorApp.cpp (About dialog)

## Dia 30

- ID: D30
- Meta: Tests de regresion y cierre de sprint.
- Horas: 6h
- Dependencias: D29
- Tareas:
  - Agregar tests para sistema de eventos (subscribe/emit/queue).
  - Agregar tests para hot-reload (simular cambio de archivo y verificar reimport).
  - Agregar tests para prefab instanciacion + override + reset.
  - Verificar todos los tests anteriores (D01-D29) siguen passing.
  - Build completo release con 0 warnings.
  - Actualizar README.md con features de Sprint 2.
  - Actualizar SCALING_CHECKLIST.md con items nuevos completados.
  - Crear tag v2.0-alpha.
- Archivos:
  - Crear: tests/test_event_system.cpp
  - Crear: tests/test_hot_reload.cpp
  - Modificar: tests/CMakeLists.txt
  - Modificar: README.md
  - Modificar: SCALING_CHECKLIST.md

---

## Dependencias globales (resumen)

- Bloque Eventos + Comandos: D21, D22 (independientes, solo dependen de D20)
- Bloque ECS: D23 -> D24 -> D25 -> D26 (cadena estricta)
- Bloque Produccion: D27, D28, D29 (D27 depende de D22, D28 depende de D26, D29 depende de D27)
- Cierre: D30 (depende de todo)

```
D20 ──┬── D21 (Eventos) ── D23 (Componentes) ── D24 (Registry) ── D25 (Inspector) ── D26 (Prefabs) ── D28 (Validacion) ──┐
      │                                                                                                                     ├── D30 (Cierre)
      └── D22 (Comandos) ── D27 (Hot-reload) ── D29 (Packaging) ──────────────────────────────────────────────────────────┘
```

## Estimacion total

- Horas totales: 60h
- Promedio diario: 6h
- Dias: 10 (D21-D30)

## Buffer recomendado

- Reservar 1 dia extra (fuera de los 10) para integracion de ECS con prefabs.
- El bloque ECS (D23-D26) es el de mayor riesgo. Si se complica, priorizar tener componentes + registry sin inspector generico.
