# SQLite Migration Runbook

## Objetivo

Operar, validar y revertir la migracion SQLite en Dash-Engine con riesgo controlado.

## Modos de backend (D65)

Variable: DASH_DB_MODE

- json: fuerza backend legacy JSON.
- hybrid: preferencia SQLite con fallback JSON.
- sqlite: fuerza SQLite sin fallback.

Default operativo esperado: hybrid.

## Flujo recomendado de migracion

1. Abrir proyecto en editor.
2. Ejecutar Tools > Migrate Project Data to SQLite.
3. Revisar resumen y log detallado del modal.
4. Verificar que Scene Selector liste escenas correctamente.
5. Validar quicksave/quickload (F5/F9) en runtime.

## Hardening y seguridad de datos (D67)

- Antes de migrar, se crea backup automatico de la DB existente:
  - .library/dash_engine.db.bak.<timestamp>
- La migracion corre en transaccion.
- Al finalizar se ejecuta:
  - PRAGMA integrity_check
  - PRAGMA foreign_key_check

Si falla cualquiera, la migracion queda marcada como fallida.

## Validaciones post-migracion

1. Scene Selector carga escenas desde DB.
2. Guardar/abrir escena desde editor funciona.
3. GameplayDatabase carga clases/enemigos/loot desde SQLite.
4. SaveGame quicksave/quicload (F5/F9) funciona en sqlite/hybrid.
5. Build & Run y export no presentan regresiones.

## Rollback operativo

### Rollback inmediato

1. Exportar variable:
   - DASH_DB_MODE=json
2. Reiniciar editor/runtime.

### Rollback con restauracion de DB

1. Seleccionar backup mas reciente:
   - .library/dash_engine.db.bak.<timestamp>
2. Copiar como nueva base activa:
   - .library/dash_engine.db
3. Ejecutar en modo hybrid para validar.

## Diagnostico rapido

- Si migracion falla: revisar modal SQLite Migration Log.
- Si escenas no aparecen: ejecutar Refresh en Scene Selector y revisar log [SCENE].
- Si savegame falla en sqlite: validar permisos de .library/ o usar modo json temporal.

## Checklist de soporte

- Confirmar valor de DASH_DB_MODE.
- Confirmar existencia de .library/dash_engine.db.
- Confirmar backups .bak previos a incidente.
- Ejecutar tests SQLite clave en build-tests:
  - test_sqlite_schema
  - test_asset_db_sqlite
  - test_gameplay_db_sqlite
  - test_project_data_migrator
  - test_save_game
