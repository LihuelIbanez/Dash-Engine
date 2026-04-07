# Semana 1 - Fundaciones del Editor

## Meta de la semana

Construir una base escalable para edicion de escenas: modelo de datos limpio, sistema de comandos global para undo/redo y serializacion versionada.

---

## Tarea 1.1 - Introducir EntityId estable y modelo de entidades

### Objetivo
Desacoplar la seleccion y modificacion de entidades del indice de vector para evitar errores cuando se borra o reordena contenido.

### Archivos a modificar

1. src/editor/SceneData.h
- Seccion: struct EntityData y struct SceneData.
- Cambios:
  - agregar campo id (uint64_t) en EntityData.
  - agregar nextEntityId en SceneData para generar IDs.
  - agregar helper allocateEntityId().

2. src/editor/SceneData.cpp
- Seccion: serializacion to_json/from_json.
- Cambios:
  - persistir id y nextEntityId.
  - compatibilidad backward: si no existe id en scene vieja, asignar automaticamente.

3. src/editor/EditorApp.h
- Seccion: estado de seleccion.
- Cambios:
  - reemplazar selectedEntity_ (indice) por selectedEntityId_.
  - agregar helper findEntityById().

4. src/editor/EditorApp.cpp
- Secciones:
  - drawSceneHierarchy()
  - drawPropertiesPanel()
  - handleToolClick()
- Cambios:
  - adaptar seleccion por id.
  - al crear enemigo, usar allocateEntityId().

### Criterio de aceptacion

- Crear, borrar y reordenar entidades no rompe seleccion.
- Al guardar/cargar, IDs se mantienen estables.

---

## Tarea 1.2 - Sistema global de comandos (Undo/Redo de escena)

### Objetivo
Aplicar patron Command para que toda accion de escena sea reversible.

### Archivos a crear

1. src/editor/commands/ICommand.h
- Definir interfaz:
  - virtual void apply(SceneData&, World&)
  - virtual void undo(SceneData&, World&)
  - virtual const char* name() const

2. src/editor/commands/CommandStack.h
3. src/editor/commands/CommandStack.cpp
- Mantener stacks undo/redo y metodo execute(unique_ptr<ICommand>).

4. src/editor/commands/PaintTileCommand.h
5. src/editor/commands/PaintTileCommand.cpp

6. src/editor/commands/PlaceEnemyCommand.h
7. src/editor/commands/PlaceEnemyCommand.cpp

8. src/editor/commands/EraseCommand.h
9. src/editor/commands/EraseCommand.cpp

### Archivos a modificar

1. CMakeLists.txt
- Seccion: file(GLOB EDITOR_SOURCES ...)
- Cambio recomendado:
  - dejar de depender solo de src/editor/*.cpp.
  - incluir subcarpetas src/editor/**/*.cpp (o listado explicito para comandos).

2. src/editor/EditorApp.h
- Seccion: miembros privados.
- Agregar:
  - CommandStack commandStack_.
  - metodos performUndo() y performRedo().

3. src/editor/EditorApp.cpp
- Secciones:
  - run() para shortcuts globales Cmd+Z y Cmd+Shift+Z.
  - handleToolClick() para ejecutar comandos en vez de mutacion directa.

### Criterio de aceptacion

- Undo/redo funciona para paint tile, place enemy y erase.
- Redo se limpia al ejecutar nuevo comando.

---

## Tarea 1.3 - Serializacion versionada y validacion de escena

### Objetivo
Evitar corrupcion silenciosa y preparar migraciones de datos.

### Archivos a modificar

1. src/editor/SceneData.h
- Agregar campo sceneVersion.

2. src/editor/SceneData.cpp
- Seccion load/save.
- Cambios:
  - escribir sceneVersion.
  - validar campos requeridos.
  - loggear mensajes de error detallados.

3. src/editor/EditorApp.cpp
- Secciones openScene() y saveScene().
- Cambios:
  - mostrar errores con contexto en Build Log.
  - impedir sobreescritura si validacion falla.

### Criterio de aceptacion

- Escenas invalidas no crashean el editor.
- El usuario ve mensajes claros de por que fallo la carga.

---

## Tarea 1.4 - Dirty state y proteccion de cambios no guardados

### Objetivo
Evitar perdida de trabajo al cerrar/abrir escena.

### Archivos a modificar

1. src/editor/EditorApp.h
- Agregar bool sceneDirty_.

2. src/editor/EditorApp.cpp
- Secciones:
  - operaciones que mutan escena marcan sceneDirty_.
  - menu File (New/Open/Quit) muestra confirmacion si hay cambios.

### Criterio de aceptacion

- Antes de perder cambios, el editor pide confirmacion.

---

## Entregables de la semana

- Modelo de escena con IDs estables.
- Undo/Redo global de operaciones de escena.
- Carga/guardado versionado con validacion.
- Indicador de escena dirty y proteccion de perdida de datos.
