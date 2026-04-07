# Isometric RPG - Plan de Implementacion Detallado

Este directorio separa las tareas por semana para ejecutar el escalado del editor y del juego.

## Orden recomendado

1. Semana 1: Fundaciones del editor y modelo de datos.
2. Semana 2: Asset pipeline y base de importacion/caching.
3. Semana 3: Runtime por sistemas y gameplay data-driven.
4. Semana 4: Produccion, QA, testing y profiling.

## Archivos de trabajo

- 01_WEEK1_EDITOR_FOUNDATIONS.md
- 02_WEEK2_ASSET_PIPELINE.md
- 03_WEEK3_RUNTIME_SYSTEMS.md
- 04_WEEK4_PRODUCTION_QA.md
- 99_ACCEPTANCE_CHECKLIST.md

## Convenciones para ejecutar tareas

- Cada tarea define:
  - objetivo tecnico
  - alcance
  - cambios concretos por archivo
  - seccion de codigo a modificar o crear
  - criterio de aceptacion
- No mezclar tareas de distintas semanas salvo dependencias criticas.
- Mantener cambios pequenos y verificables en commits separados.

## Regla de arquitectura

- Editor y runtime deben compartir datos por contratos (estructuras serializables), no por acceso directo a UI.
- Toda funcionalidad nueva debe llegar como modulo/sistema y no como bloque monolitico en un solo cpp.
