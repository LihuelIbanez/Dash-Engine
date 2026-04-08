# Semana 5 - Arquitectura de Componentes y Eventos

## Meta de la semana

Evolucionar el modelo de entidades de herencia (Entity→Character→Player/Enemy) a una arquitectura basada en componentes con registry centralizado, sistema de eventos desacoplado, comandos de edicion avanzados e inspector generico con reflection.

---

## Tarea 5.1 - Sistema de eventos desacoplado (D21)

### Objetivo
Crear un bus de eventos tipado para desacoplar la comunicacion entre sistemas de runtime. Actualmente CombatSystem muta directamente el health de las entidades y SpawnRewardSystem verifica `health == 0` por polling. El objetivo es que CombatSystem emita DamageEvent/DeathEvent y SpawnRewardSystem se suscriba a DeathEvent.

### Archivos a crear

1. src/core/events/EventDispatcher.h
- Template class EventDispatcher:
  - `subscribe<EventT>(std::function<void(const EventT&)>)` — registra listener.
  - `emit<EventT>(const EventT& event)` — encola evento en buffer del frame.
  - `flush()` — procesa cola FIFO y notifica suscriptores.
  - `clear()` — limpia suscripciones (para reset en Play mode).
- Almacenamiento interno:
  - `std::unordered_map<std::type_index, std::vector<std::function<void(const void*)>>>` para handlers.
  - `std::vector<std::pair<std::type_index, std::shared_ptr<void>>>` como cola de eventos pendientes.
- Los eventos se procesan al final del frame, no inmediatamente.

2. src/core/events/GameEvents.h
- Definir structs de eventos (POD sin logica):
  - `DamageEvent { uint64_t attackerId; uint64_t targetId; int damage; int finalHealth; }`.
  - `DeathEvent { uint64_t entityId; float x; float y; std::string entityName; }`.
  - `LevelUpEvent { int oldLevel; int newLevel; int totalExp; }`.
  - `HealthChangeEvent { uint64_t entityId; int oldHealth; int newHealth; int maxHealth; }`.

### Archivos a modificar

1. src/game/runtime/RuntimeContext.h
- Seccion: struct RuntimeContext.
- Cambios:
  - Agregar `EventDispatcher* events = nullptr;`.
  - El dispatcher vive en Game y se inyecta por puntero.

2. src/game/systems/CombatSystem.cpp
- Seccion: resolucion de ataques (player attacks y enemy attacks).
- Cambios:
  - Despues de calcular damage y aplicar a health, emitir `ctx.events->emit(DamageEvent{...})`.
  - Si health <= 0, emitir `ctx.events->emit(DeathEvent{...})`.
  - Emitir `HealthChangeEvent` en cada cambio de HP.

3. src/game/systems/SpawnRewardSystem.cpp
- Seccion: deteccion de enemies muertos (`health == 0`).
- Cambios:
  - En lugar de polling, suscribirse a DeathEvent en un metodo init o al registrar el sistema.
  - Acumular DeathEvents del frame y procesar XP/score/cleanup al recibir cada uno.
  - Alternativa viable: mantener polling actual pero emitir DeathEvent extra para futuros suscriptores.

4. src/game/Game.cpp
- Seccion: inicializacion de sistemas y game loop.
- Cambios:
  - Crear `EventDispatcher dispatcher_` como miembro de Game.
  - Asignar `ctx.events = &dispatcher_` en setup del contexto.
  - Llamar `dispatcher_.flush()` al final de cada frame, despues de todos los systems.
  - En `initEmbedded()` tambien inicializar el dispatcher.

5. src/game/Game.h
- Seccion: miembros privados.
- Cambios:
  - Agregar `#include "core/events/EventDispatcher.h"`.
  - Agregar `EventDispatcher dispatcher_;`.

### Criterio de aceptacion

- CombatSystem emite DamageEvent y DeathEvent.
- SpawnRewardSystem reacciona a DeathEvent para otorgar XP/score.
- El dispatcher procesa eventos en orden FIFO al final del frame.
- El juego funciona identico al comportamiento previo (no hay cambio de gameplay).

---

## Tarea 5.2 - Comandos MoveEntity y EditProperty (D22)

### Objetivo
Completar el sistema de comandos del editor con operaciones de mover entidades (drag & drop en viewport) y editar propiedades genéricas, ambos con undo/redo.

### Archivos a crear

1. src/editor/commands/MoveEntityCommand.h
2. src/editor/commands/MoveEntityCommand.cpp
- Hereda de `ICommand`.
- Constructor: `MoveEntityCommand(uint64_t entityId, float oldX, float oldY, float newX, float newY)`.
- `apply()`: busca entidad por ID en `scene.entities`, setea (newX, newY), actualiza World si es necesario.
- `undo()`: restaura (oldX, oldY).
- `name()`: retorna "Move Entity".

3. src/editor/commands/EditPropertyCommand.h
4. src/editor/commands/EditPropertyCommand.cpp
- Hereda de `ICommand`.
- Usa `std::variant<int, float, std::string, bool>` para old/new value.
- Enum `PropertyTarget`: Name, CharClass, PosX, PosY.
- Constructor: `EditPropertyCommand(uint64_t entityId, PropertyTarget prop, ValueVariant oldVal, ValueVariant newVal)`.
- `apply()`: busca entidad por ID, setea propiedad segun target.
- `undo()`: restaura valor anterior.
- `name()`: retorna "Edit [Property]" con nombre descriptivo.

5. tests/test_move_edit_commands.cpp
- Test MoveEntityCommand: crear scene con entidad, apply mueve, undo restaura, redo mueve de nuevo.
- Test EditPropertyCommand: cambiar nombre, undo restaura, cambiar clase, undo restaura.
- Test drag sequence: multiples moves fusionados en un solo comando (opcional).

### Archivos a modificar

1. src/editor/EditorApp.cpp
- Seccion: drawViewport() en modo Edit.
- Cambios:
  - Detectar click sobre entidad seleccionada + drag (ImGui::IsMouseDragging).
  - Al iniciar drag: capturar posicion original.
  - Al soltar (mouse released): ejecutar `MoveEntityCommand(id, oldX, oldY, newX, newY)` via commandStack_.
  - Feedback visual: dibujar outline punteado en posicion de drag.

- Seccion: drawPropertiesPanel().
- Cambios:
  - Reemplazar mutacion directa de EntityData por creacion de EditPropertyCommand.
  - ImGui::InputText para nombre: al perder foco (IsItemDeactivatedAfterEdit), crear comando con old/new.
  - ImGui::DragFloat para x,y: misma logica con valor pre/post edit.
  - Usar `ImGui::IsItemActivated()` para capturar valor previo.

2. tests/CMakeLists.txt
- Seccion: lista de test sources.
- Cambios:
  - Agregar test_move_edit_commands.cpp a la lista de tests.

### Criterio de aceptacion

- Drag & drop de entidades en viewport con undo/redo funcional.
- Edicion de nombre, clase y posicion desde Properties con undo/redo.
- Tests pasan: apply/undo/redo de ambos comandos.

---

## Tarea 5.3 - Estructuras base de componentes (D23)

### Objetivo
Definir los componentes como structs POD simples que reemplacen los campos dispersos de Entity, Character, EntityData. Cada componente es data pura, sin logica. Serializacion JSON roundtrip para cada uno.

### Archivos a crear

1. src/core/components/Components.h
- Enum class ComponentType con IDs numericos estables (para serialization):
  ```
  Transform = 0,
  Render    = 1,
  Health    = 2,
  Mana      = 3,
  Stats     = 4,
  Combat    = 5,
  AI        = 6,
  ```
- Structs:
  - `TransformComponent { float x = 0.f; float y = 0.f; }`.
  - `RenderComponent { std::string sprite = "default"; int layer = 0; bool visible = true; }`.
  - `HealthComponent { int health = 100; int maxHealth = 100; }`.
  - `ManaComponent { int mana = 50; int maxMana = 50; }`.
  - `StatsComponent { int attack = 10; int defense = 5; int magicAttack = 0; int speed = 3; float critChance = 0.05f; int level = 1; int experience = 0; int expToNextLevel = 100; }`.
  - `CombatComponent { float attackRange = 1.8f; float attackCooldown = 0.8f; float cooldownTimer = 0.f; bool isAttacking = false; }`.
  - `AIComponent { enum class Behavior { Idle, Patrol, Chase, Flee }; Behavior behavior = Behavior::Idle; float detectionRange = 5.f; float patrolRadius = 3.f; }`.
- `using ComponentVariant = std::variant<TransformComponent, RenderComponent, HealthComponent, ManaComponent, StatsComponent, CombatComponent, AIComponent>;`

2. src/core/components/ComponentSerialization.h
3. src/core/components/ComponentSerialization.cpp
- `nlohmann::json componentToJson(const ComponentVariant& comp)` — serializa componente con campo `"type"` (string) + campos.
- `ComponentVariant componentFromJson(const nlohmann::json& j)` — deserializa por tipo.
- `to_json`/`from_json` overloads para cada struct individual.
- `std::string componentTypeName(ComponentType type)` — mapa type↔string.
- `ComponentType componentTypeFromName(const std::string& name)` — inverso.

4. tests/test_component_serialization.cpp
- Crear instancia de cada componente con valores custom.
- Serializar a JSON.
- Deserializar de vuelta.
- Verificar todos los campos iguales (roundtrip).
- Verificar que tipo desconocido lanza/retorna error controlado.

### Archivos a modificar

1. CMakeLists.txt
- Seccion: game_core SOURCES o DashEngine SOURCES.
- Cambios:
  - Agregar `src/core/components/ComponentSerialization.cpp`.
  - Agregar include directory `src/core` si no existe.

2. tests/CMakeLists.txt
- Cambios:
  - Agregar test_component_serialization.cpp.

### Criterio de aceptacion

- 7 componentes definidos como structs POD con valores default sensatos.
- Serialization roundtrip JSON funciona para cada uno.
- ComponentType enum tiene IDs numericos estables.
- Tests pasan.

---

## Tarea 5.4 - EntityRegistry y migracion de datos (D24)

### Objetivo
Crear un registry centralizado que almacene entidades como (ID + coleccion de componentes) y migrar EntityData de SceneData para soportar serializar componentes en la escena.

### Archivos a crear

1. src/core/components/EntityRegistry.h
2. src/core/components/EntityRegistry.cpp
- Clase EntityRegistry:
  - `uint64_t createEntity()` — asigna nuevo ID, retorna.
  - `void destroyEntity(uint64_t id)` — elimina entidad y todos sus componentes.
  - `template<typename T> T& addComponent(uint64_t id)` — agrega componente, retorna referencia.
  - `template<typename T> T* getComponent(uint64_t id)` — retorna puntero o nullptr.
  - `template<typename T> bool hasComponent(uint64_t id) const` — test de existencia.
  - `template<typename T> void removeComponent(uint64_t id)` — quita componente.
  - `std::vector<ComponentVariant>& getComponents(uint64_t id)` — acceso directo a la lista.
  - `const std::vector<uint64_t>& allEntities() const` — lista de IDs activos.
  - `void clear()` — limpia todo.
- Almacenamiento interno: `std::unordered_map<uint64_t, std::vector<ComponentVariant>> storage_`.
- `uint64_t nextId_ = 1` — generador monotono (igual que SceneData).

### Archivos a modificar

1. src/editor/SceneData.h
- Seccion: struct EntityData.
- Cambios:
  - Agregar campo `std::vector<ComponentVariant> components;` (opcional, puede estar vacio para escenas v1).
  - Mantener campos legacy (type, name, x, y, charClass) para backward compat.

2. src/editor/SceneData.cpp
- Seccion: to_json / from_json de EntityData.
- Cambios:
  - Si `!entity.components.empty()`, serializar array `"components"` usando componentToJson.
  - En from_json: si existe `"components"`, deserializar. Si no (v1), construir components desde campos legacy:
    - `TransformComponent{x, y}`, `HealthComponent{100, 100}`, `StatsComponent{defaults by charClass}`.
  - Incrementar `kCurrentVersion` a 2.
  - `sceneVersion 1→2`: migrar automaticamente al cargar.

3. tests/CMakeLists.txt
- Cambios:
  - Agregar test_entity_registry.cpp.

### Archivos a crear (tests)

1. tests/test_entity_registry.cpp
- Test crear entidad y agregar componentes.
- Test getComponent retorna nullptr si no existe.
- Test destroyEntity limpia todo.
- Test serializar SceneData con componentes (roundtrip).
- Test cargar escena v1 y verificar migracion automatica a componentes.

### Criterio de aceptacion

- EntityRegistry funciona como store de componentes por ID.
- Escenas v1 (sin componentes) cargan correctamente y se convierten a v2 con componentes.
- Escenas v2 persisten componentes en JSON.
- Backward compat: escenas guardadas antes siguen cargando sin errores.
- Tests pasan.

---

## Tarea 5.5 - Inspector generico con reflection (D25)

### Objetivo
Reemplazar el inspector hardcodeado de propiedades (que lee campos directos de EntityData) por un inspector generico que itera los componentes de una entidad y renderiza controles ImGui automaticamente segun el tipo de cada campo.

### Archivos a crear

1. src/core/components/Reflection.h
2. src/core/components/Reflection.cpp
- Structs de reflection:
  - Enum class `PropertyType { Float, Int, String, Bool, Enum }`.
  - `struct PropertyInfo { std::string name; PropertyType type; size_t offset; /* para enums: */ std::vector<std::string> enumValues; }`.
  - `struct ComponentMeta { std::string name; ComponentType type; std::vector<PropertyInfo> properties; }`.
- Registro global:
  - `const ComponentMeta& getComponentMeta(ComponentType type)`.
  - Inicializacion estatica o funcion `initReflection()` que registra cada componente:
    - TransformComponent: {Float "x", Float "y"}.
    - HealthComponent: {Int "health", Int "maxHealth"}.
    - StatsComponent: {Int "attack", Int "defense", Int "magicAttack", Int "speed", Float "critChance", Int "level"}.
    - AIComponent: {Enum "behavior" con valores {"Idle","Patrol","Chase","Flee"}, Float "detectionRange", Float "patrolRadius"}.
    - (etc para cada componente).
- Helper para acceso por offset: `void* fieldPtr(ComponentVariant& comp, const PropertyInfo& prop)`.

### Archivos a modificar

1. src/editor/EditorApp.cpp
- Seccion: drawPropertiesPanel().
- Cambios:
  - Si hay entidad seleccionada, obtener sus componentes del EntityRegistry (o de EntityData.components).
  - Para cada componente:
    - Obtener ComponentMeta via getComponentMeta(type).
    - ImGui::CollapsingHeader(meta.name).
    - Para cada PropertyInfo en meta.properties:
      - Float → `ImGui::DragFloat(name, ptr)`.
      - Int → `ImGui::DragInt(name, ptr)`.
      - String → `ImGui::InputText(name, ptr)`.
      - Bool → `ImGui::Checkbox(name, ptr)`.
      - Enum → `ImGui::Combo(name, ptr, enumValues)`.
    - Detectar cambios (IsItemDeactivatedAfterEdit) y crear EditPropertyCommand.
  - Boton "+ Add Component": Combo con tipos disponibles + boton Add.
  - Boton "X" en header de componente: removeComponent con undo.

2. src/editor/EditorApp.h
- Seccion: includes.
- Cambios:
  - Agregar `#include "core/components/Reflection.h"`.
  - Agregar `#include "core/components/EntityRegistry.h"` (si no esta ya).

3. CMakeLists.txt
- Seccion: sources.
- Cambios:
  - Agregar `src/core/components/Reflection.cpp`.

### Criterio de aceptacion

- El inspector genera controles automaticos para cualquier componente registrado.
- Agregar un componente nuevo en Components.h + registrar en Reflection solo requiere esas 2 lineas, el inspector lo muestra automaticamente.
- Edicion de propiedades crea EditPropertyCommand (undo/redo).
- Se puede agregar/quitar componentes de una entidad desde el inspector.

---

## Entregables de la semana

- Sistema de eventos desacoplado con dispatcher por frame.
- Comandos MoveEntity y EditProperty con undo/redo.
- 7 structs de componentes con serializacion JSON.
- EntityRegistry con almacenamiento por componentes.
- Migracion automatica de escenas v1→v2.
- Inspector generico con reflection que renderiza cualquier componente.
