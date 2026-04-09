# Sprint 6-7 - Migracion a SQLite (analisis, tareas y ejecucion)

## Objetivo

Migrar el almacenamiento principal del proyecto desde archivos JSON sueltos a una base de datos SQLite, manteniendo compatibilidad retroactiva durante la transicion y evitando regresiones en editor, runtime y pipeline de assets.

Resultado esperado al finalizar:

1. El proyecto usa un archivo SQLite como fuente principal de datos de proyecto.
2. Existe migracion automatica desde JSON legacy.
3. Editor y runtime pueden leer desde SQLite con fallback controlado.
4. Tests y herramientas validan integridad, rendimiento y rollback.

---

## Analisis del estado actual

### Fuentes de datos actuales (JSON)

1. Asset index:
- assets/asset_db.json
- Consumidor principal: AssetDatabase + ImportManager + paneles Asset Browser/Inspector

2. Gameplay data:
- assets/gameplay/player_classes.json
- assets/gameplay/enemies.json
- assets/gameplay/loot_tables.json
- Consumidor principal: GameplayDatabase en runtime

3. Escenas:
- scenes/*.json
- Consumidor principal: SceneData (editor y runtime)

4. Proyecto:
- *.dashproject (ProjectManifest)
- Consumidor principal: ProjectManager y pipeline de export

5. Savegames:
- saves/*.json
- Consumidor principal: SaveGame

### Hallazgos clave

1. El ecosistema ya esta desacoplado por modulos (AssetDatabase, GameplayDatabase, SceneData, SaveGame), lo cual facilita introducir un backend alternativo sin reescribir todo de una vez.
2. El proyecto ya tiene versionado en algunas partes (sceneVersion, save versioning), aprovechable para migraciones SQL.
3. El pipeline de importacion y hot-reload depende de hash/timestamp, ideal para persistir metadata en tablas.
4. Hay cobertura de tests que permite introducir una estrategia de migracion incremental con verificacion continua.

### Riesgos principales

1. Cortar JSON de golpe romperia flujo de trabajo y contenido existente.
2. Riesgo de duplicidad/inconsistencia durante coexistencia si no se define fuente de verdad.
3. Riesgo de corrupcion si no se usan transacciones en migracion.
4. Riesgo de degradacion de performance en editor si se hacen queries sin indices.

---

## Estrategia tecnica recomendada

### Enfoque

Migracion incremental por fases con dual-read y migracion one-shot por proyecto.

### Ubicacion DB

- Por proyecto: <projectRoot>/.library/dash_engine.db
- Mantener JSON como fallback hasta fin de Sprint 7.

### Principios

1. Fuente de verdad por fase:
- Sprint 6: JSON (DB en espejo/migracion)
- Sprint 7: SQLite (JSON opcional export/debug)

2. Migraciones idempotentes:
- Tabla schema_migrations
- Versionado estricto de esquema

3. Todo cambio de lote en transaccion:
- BEGIN IMMEDIATE ... COMMIT

4. Compatibilidad:
- Si no hay DB, intentar migrar JSON
- Si falla migracion, fallback a JSON con warning visible

---

## Diseno de esquema inicial (v1)

### Metadatos

1. schema_migrations
- version INTEGER PRIMARY KEY
- applied_at TEXT NOT NULL

2. project_meta
- key TEXT PRIMARY KEY
- value TEXT NOT NULL

### Assets

1. assets
- guid TEXT PRIMARY KEY
- source_path TEXT NOT NULL
- import_path TEXT NOT NULL
- asset_type TEXT NOT NULL
- hash TEXT NOT NULL
- last_import_time INTEGER NOT NULL

2. asset_dependencies
- asset_guid TEXT NOT NULL
- dependency_path TEXT NOT NULL
- PRIMARY KEY(asset_guid, dependency_path)

Indices:
- idx_assets_source_path(source_path)
- idx_assets_type(asset_type)

### Gameplay data

1. player_classes
- id TEXT PRIMARY KEY
- name TEXT NOT NULL
- description TEXT NOT NULL
- max_hp INTEGER NOT NULL
- max_mana INTEGER NOT NULL
- attack_cooldown REAL NOT NULL
- attack INTEGER NOT NULL
- defense INTEGER NOT NULL
- magic_attack INTEGER NOT NULL
- speed REAL NOT NULL
- crit_chance REAL NOT NULL

2. enemies
- id TEXT PRIMARY KEY
- name TEXT NOT NULL
- max_hp INTEGER NOT NULL
- detection_radius REAL NOT NULL
- attack_radius REAL NOT NULL
- exp_reward INTEGER NOT NULL
- attack_cooldown REAL NOT NULL
- attack INTEGER NOT NULL
- defense INTEGER NOT NULL
- magic_attack INTEGER NOT NULL
- speed REAL NOT NULL
- crit_chance REAL NOT NULL

3. loot_tables
- id TEXT PRIMARY KEY

4. loot_table_enemies
- loot_id TEXT NOT NULL
- enemy_id TEXT NOT NULL
- PRIMARY KEY(loot_id, enemy_id)

5. loot_drops
- loot_id TEXT NOT NULL
- item TEXT NOT NULL
- chance REAL NOT NULL
- min_qty INTEGER NOT NULL
- max_qty INTEGER NOT NULL
- PRIMARY KEY(loot_id, item)

### Scenes

1. scenes
- scene_id TEXT PRIMARY KEY
- file_name TEXT NOT NULL UNIQUE
- scene_name TEXT NOT NULL
- world_seed INTEGER NOT NULL
- next_entity_id INTEGER NOT NULL
- scene_version INTEGER NOT NULL
- raw_json TEXT NOT NULL
- updated_at INTEGER NOT NULL

Nota: en v1 se almacena raw_json para minimizar riesgo. Normalizacion de entidades/componentes se puede hacer en v2.

### Savegames

1. savegames
- save_id TEXT PRIMARY KEY
- slot_name TEXT NOT NULL UNIQUE
- save_version INTEGER NOT NULL
- raw_json TEXT NOT NULL
- updated_at INTEGER NOT NULL

---

## Sprint 6 - SQLite Foundation (D55-D61)

Meta: introducir infraestructura SQLite + migracion inicial sin romper JSON.

### D55 - Infraestructura SQLite base

Objetivo:
- Agregar modulo db/sqlite con wrapper RAII y manejo de errores.

Archivos a crear:
- src/core/db/SqliteDb.h
- src/core/db/SqliteDb.cpp
- src/core/db/SqliteStatement.h
- src/core/db/SqliteStatement.cpp

Cambios CMake:
- Enlazar sqlite3 (preferible sistema o amalgamado controlado).

Criterio de aceptacion:
- Abrir/cerrar DB, ejecutar SQL y prepared statements en tests.

### D56 - Schema manager y migraciones

Objetivo:
- Crear SchemaManager con aplicacion incremental de migraciones.

Archivos a crear:
- src/core/db/SchemaManager.h
- src/core/db/SchemaManager.cpp
- src/core/db/migrations/001_init.sql

Criterio de aceptacion:
- DB nueva crea esquema v1.
- Re-ejecutar no rompe (idempotencia).

### D57 - Repositorio de AssetDatabase (dual read)

Objetivo:
- Implementar AssetRepositorySqlite y conectarlo en paralelo a AssetDatabase.

Archivos a crear/modificar:
- src/assets/AssetRepositorySqlite.h/.cpp
- src/assets/AssetDatabase.cpp (feature flag backend)

Criterio de aceptacion:
- Load/save de assets desde SQLite.
- Si no existe DB o falla, fallback a JSON con log.

### D58 - Migrador JSON -> SQLite (assets + gameplay)

Objetivo:
- Herramienta de migracion inicial por proyecto.

Archivos a crear:
- src/editor/project/ProjectDataMigrator.h/.cpp

Flujo:
- Leer JSON existentes.
- Escribir en DB en transaccion unica.
- Registrar resultado en Build Log.

Criterio de aceptacion:
- Migracion completa para assets + gameplay sin perdida de registros.

### D59 - GameplayDatabase con backend SQLite

Objetivo:
- GameplayDatabase primero intenta SQLite; fallback a JSON.

Archivos a modificar:
- src/game/data/GameplayDatabase.h/.cpp

Criterio de aceptacion:
- Runtime carga player_classes, enemies, loot desde DB.
- Tests actuales siguen pasando con fallback.

### D60 - Integracion editor: accion de migracion

Objetivo:
- Agregar comando/menu: Migrate Project Data to SQLite.

Archivos a modificar:
- src/editor/EditorApp.cpp

Criterio de aceptacion:
- Migracion ejecutable desde UI y con feedback detallado.

### D61 - Tests de fundacion

Objetivo:
- Cubrir wrapper DB, schema, migrador y equivalencia JSON/SQLite.

Archivos a crear:
- tests/test_sqlite_schema.cpp
- tests/test_asset_db_sqlite.cpp
- tests/test_gameplay_db_sqlite.cpp
- tests/test_project_data_migrator.cpp

Criterio de aceptacion:
- Todos verdes en CI/local.

---

## Sprint 7 - SQLite Cutover (D62-D68)

Meta: mover escenas y savegames a DB y dejar SQLite como backend principal.

### D62 - Scene repository SQLite

Objetivo:
- Introducir SceneRepositorySqlite usando tabla scenes (raw_json v1).

Archivos a crear/modificar:
- src/editor/scene/SceneRepositorySqlite.h/.cpp
- src/editor/SceneData.cpp (adaptador a repositorio)

Criterio de aceptacion:
- Crear/abrir/guardar escenas desde DB con compatibilidad.

### D63 - Migracion de scenes/*.json

Objetivo:
- Migrar todas las escenas del proyecto a tabla scenes.

Criterio de aceptacion:
- Scene Selector funciona leyendo desde DB.
- Export opcional de escena a JSON para debug.

### D64 - SaveGame en SQLite

Estado: ✅ Implementado

Objetivo:
- Persistir savegames en tabla savegames.

Archivos a modificar:
- src/game/save/SaveGame.cpp

Criterio de aceptacion:
- F5/F9 funcionales con SQLite.
- Fallback a JSON solo si flag legacy activo.

### D65 - Cutover por feature flags

Estado: ✅ Implementado

Objetivo:
- Flags de backend:
  - DASH_DB_MODE=json|hybrid|sqlite

Reglas:
- default en Sprint 7: hybrid -> sqlite preferido.

Criterio de aceptacion:
- Se puede forzar modo legacy para rollback controlado.

### D66 - Performance tuning e indices

Estado: ✅ Implementado

Objetivo:
- Medir y ajustar queries criticas (scene list, asset lookups, gameplay load).

Criterio de aceptacion:
- Apertura de proyecto y carga de escena no empeoran vs baseline JSON.

### D67 - Hardening de integridad

Estado: ✅ Implementado

Objetivo:
- PRAGMA foreign_keys=ON
- checks de integridad y backup automatico previo a migracion.

Criterio de aceptacion:
- Fallos simulados no dejan DB en estado parcial.

### D68 - QA final y documentacion operativa

Estado: ✅ Implementado

Objetivo:
- Documentar operaciones de migracion/rollback.
- Cerrar checklist de aceptacion.

Entregables:
- planning/tasks/99_ACCEPTANCE_CHECKLIST.md actualizado.
- Documento runbook de soporte.

---

## Criterios de aceptacion global (fin Sprint 7)

1. Proyecto abre y funciona con SQLite sin requerir JSON fuente.
2. Migracion de un proyecto real completa en un paso, con logs claros.
3. Feature flag permite rollback a JSON en caso de incidente.
4. No hay regresiones en tests existentes + nuevos tests SQLite.
5. Export Build Pipeline y Runtime siguen operativos.

---

## Estimacion de esfuerzo

- Sprint 6: 7 dias habiles (infraestructura + dual-read + migrador base)
- Sprint 7: 7 dias habiles (cutover escenas/saves + hardening + QA)
- Total: 14 dias habiles

Equipo recomendado:
- 1 dev core/runtime
- 1 dev tools/editor
- 1 QA parcial (o rotativo)

---

## Backlog post-migracion (opcional)

1. Normalizar entities/components en tablas (eliminar raw_json gradual).
2. Query tooling para analytics de contenido.
3. Sincronizacion remota (si luego se requiere multi-user).
4. Compactacion y mantenimiento DB (VACUUM/ANALYZE en pipeline).
