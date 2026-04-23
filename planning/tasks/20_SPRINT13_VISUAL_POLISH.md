# Sprint 13 — Visual Polish del Editor (Estilo VS Code)

## Meta del sprint

Restyling completo del editor al estilo VS Code Dark+ y verificacion funcional de todos los controles.

**Estado: ✅ COMPLETADO**

### Implementacion
- Tema VS Code Dark+ completo (~40 colores ImGui + style vars)
- Paleta: #1E1E1E fondo, #007ACC acento azul, #F44747 errores, #CCA700 warnings
- Toolbar: separadores verticales nativos en lugar de TextDisabled("|")
- Status Bar inferior estilo VS Code (azul edit, naranja play) con modo, escena, entidades, FPS
- Colores de botones actualizados: Build&Run verde #388A34, Play azul #007ACC, Stop rojo #F44747, Validate purpura #C586C0
- Scene Selector: botones con colores VS Code (azul, verde, naranja)
- ValidationPanel: colores de error/warning actualizados a paleta VS Code
- Viewport letterbox: color actualizado a #1E1E1E
- Titulo de ventana: "Dash Engine" en lugar de "Isometric RPG Editor"
- Fix: Export Bundle ahora verifica retorno de saveToFile()

---

## Tarea 13.1 — Tema VS Code completo

### Objetivo
Aplicar paleta de colores VS Code Dark+ a todo el editor.

### Resultado esperado
- Todos los paneles, botones, tabs, scrollbars y controles con colores coherentes.
- Sin colores verdes "Unreal-like" residuales.

### Archivos modificados
1. src/editor/EditorApp.cpp — Tema global, botones toolbar, scene selector, viewport, status bar
2. src/editor/panels/ValidationPanel.cpp — Colores de error/warning

### Criterio de aceptacion
- Editor visualmente coherente con VS Code Dark+.

---

## Tarea 13.2 — Mejoras de layout

### Objetivo
Separadores limpios y barra de estado.

### Resultado esperado
- Separadores verticales nativos en toolbar.
- Status bar con modo, escena, entidades y FPS.

### Archivos modificados
1. src/editor/EditorApp.cpp — SeparatorEx, status bar con ImDrawList

### Criterio de aceptacion
- Status bar visible y actualizada en tiempo real.

---

## Tarea 13.3 — Verificacion funcional

### Objetivo
Auditar todos los controles interactivos del editor.

### Resultado esperado
- Todos los botones, menus, dialogs y paneles funcionan correctamente.
- Bugs encontrados corregidos.

### Archivos modificados
1. src/editor/EditorApp.cpp — Fix Export Bundle save return check

### Criterio de aceptacion
- Audit funcional completo, sin bugs criticos.

---

## Tarea 13.4 — Documentacion

### Archivos creados
1. planning/tasks/20_SPRINT13_VISUAL_POLISH.md

### Archivos modificados
1. planning/tasks/00_INDEX.md
2. SCALING_CHECKLIST.md
3. README.md
