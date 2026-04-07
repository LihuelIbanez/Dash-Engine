# Semana 2 - Asset Pipeline y Asset Database

## Meta de la semana

Implementar una base de assets similar al flujo de motores maduros: origen en assets, cache en library, GUID estable y reimport incremental.

---

## Tarea 2.1 - Estructura de carpetas y contratos de asset

### Objetivo
Definir donde vive el contenido fuente y donde vive el contenido procesado.

### Carpetas a crear

1. assets/
- Contenido fuente editable (imagenes, tilesets, json de gameplay).

2. library/
- Cache generado por importadores.

3. src/assets/
- Codigo del asset system.

### Archivos a crear

1. src/assets/AssetTypes.h
- enum AssetType (Texture, TileSet, Scene, GameplayConfig, Unknown).

2. src/assets/AssetRecord.h
- struct AssetRecord:
  - guid
  - sourcePath
  - importPath
  - assetType
  - hash
  - lastImportTime
  - dependencies

### Criterio de aceptacion

- Existen carpetas y tipos base para operar pipeline.

---

## Tarea 2.2 - Asset Database persistente

### Objetivo
Registrar todos los assets con GUID y detectar cambios.

### Archivos a crear

1. src/assets/AssetDatabase.h
2. src/assets/AssetDatabase.cpp
- API minima:
  - load(path)
  - save(path)
  - upsertRecord(...)
  - findByGuid(...)
  - findBySourcePath(...)
  - removeMissingAssets(...)

3. assets/asset_db.json (generado por herramienta/editor)

### Archivos a modificar

1. CMakeLists.txt
- Incluir nuevos cpp de src/assets en game_core o en una lib dedicada.

2. src/editor/EditorApp.cpp
- init(): cargar asset database al iniciar.
- shutdown/destructor: persistir asset database.

### Criterio de aceptacion

- Cada asset importado tiene GUID estable.
- Reiniciar editor no pierde el indice.

---

## Tarea 2.3 - Importadores iniciales

### Objetivo
Transformar assets fuente en representaciones listas para runtime/editor.

### Archivos a crear

1. src/assets/importers/IImporter.h
- Interfaz comun import(source, output, metadata).

2. src/assets/importers/SceneImporter.h
3. src/assets/importers/SceneImporter.cpp

4. src/assets/importers/TileSetImporter.h
5. src/assets/importers/TileSetImporter.cpp

6. src/assets/importers/GameplayConfigImporter.h
7. src/assets/importers/GameplayConfigImporter.cpp

8. src/assets/ImportManager.h
9. src/assets/ImportManager.cpp
- Resolver importer por tipo.
- Ejecutar import incremental por hash.

### Archivos a modificar

1. src/editor/EditorApp.cpp
- File Browser/Asset panel: boton Reimport para asset seleccionado.

### Criterio de aceptacion

- Reimport solo corre cuando el hash cambia.
- Errores de import quedan en log de editor.

---

## Tarea 2.4 - Integracion en UI de editor

### Objetivo
Mostrar metadata de assets en panel dedicado.

### Archivos a crear

1. src/editor/panels/AssetBrowserPanel.h
2. src/editor/panels/AssetBrowserPanel.cpp

3. src/editor/panels/AssetInspectorPanel.h
4. src/editor/panels/AssetInspectorPanel.cpp

### Archivos a modificar

1. src/editor/EditorApp.h
- Agregar estado de seleccion de asset GUID.

2. src/editor/EditorApp.cpp
- buildDefaultLayout(): dock de Asset Browser y Asset Inspector.
- run(): draw de ambos paneles.

### Criterio de aceptacion

- Se puede seleccionar asset y ver GUID, tipo, hash y dependencias.
- Reimport desde UI actualiza metadata en caliente.

---

## Entregables de la semana

- Asset database persistente con GUID.
- Import manager con import incremental.
- Paneles de Asset Browser e inspector de metadata.
- Integracion inicial de pipeline en ciclo de editor.
