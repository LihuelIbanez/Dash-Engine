# Sprint 14 — Terreno 3D y Fisica de Personajes

## Meta del sprint

Refinar el pipeline de rendering del terreno con visual polish avanzado (smooth shading, iluminacion, niebla, ambient occlusion) y agregar gravedad/terrain-following a los personajes del juego.

**Estado: ✅ COMPLETADO**

### Implementacion
- Terreno heightmap con smooth normals per-vertex (acumulacion de face normals)
- Paleta de colores mejorada para 9 biomas (mas brillante y saturada)
- Blending de colores per-vertex en bordes de biomas
- Tinte de roca basado en pendiente (slope-based rock tint)
- Ambient Occlusion computado en CPU (radio 3 vertices, factor [0.3, 1.0])
- Shaders Vulkan terrain: Blinn-Phong, ambient hemisferico, niebla lineal, animacion de agua
- Push constants para terrain pipeline (eyePos, time, fogStart, fogEnd)
- SDL2 per-vertex Lambertian lighting en editor viewport
- TILE_SCALE configurable para escalar tiles y terreno
- Gravedad/terrain-following: personajes (Player, Enemy) siguen la superficie del heightmap
- UI controls: fog toggle y sliders en Tile Palette panel

---

## Tarea 14.1 — Escala configurable de tiles (TILE_SCALE)

### Objetivo
Agregar constante TILE_SCALE para escalar tiles y terreno, mejorando la visualizacion del mapa.

### Resultado esperado
- Terreno y entidades escalados uniformemente en editor y runtime.
- Logica de juego sin cambios (coordenadas internas en tiles).

### Archivos modificados
1. `src/rendering/IsoRenderer.h` — Constante TILE_SCALE, worldToScreen() escalado
2. `src/editor/EditorApp.cpp` — worldToScreenIso3D(), projectVert(), viewportScreenToWorld()
3. `src/world/World.cpp` — drawMesh() con TILE_SCALE
4. `src/game/Game.cpp` — screenToWorld() con TILE_SCALE

### Criterio de aceptacion
- Tiles y entidades visualmente mas grandes sin romper herramientas (paint, height brush, select).

---

## Tarea 14.2 — Smooth normals per-vertex

### Objetivo
Reemplazar flat shading con smooth normals calculados por acumulacion de face normals.

### Resultado esperado
- Terreno con transiciones suaves entre caras adyacentes.
- Normals correctos para iluminacion en SDL2 y Vulkan.

### Archivos modificados
1. `src/world/TerrainMesh.h` — Campos nx, ny, nz en TerrainVertex; declaracion computeSmoothNormals()
2. `src/world/TerrainMesh.cpp` — Implementacion computeSmoothNormals(), buildVulkanMesh() con normals per-vertex

### Criterio de aceptacion
- Terreno muestra shading suave en editor viewport (dot product con direccion solar).

---

## Tarea 14.3 — Paleta de colores mejorada y blending

### Objetivo
Actualizar colores de biomas y agregar blending per-vertex en bordes.

### Resultado esperado
- Colores mas vivos y saturados.
- Transiciones suaves entre biomas sin bordes duros.
- Tinte de roca en pendientes pronunciadas.

### Archivos modificados
1. `src/world/TerrainMesh.cpp` — topColor() actualizado, blending per-vertex en buildVulkanMesh()
2. `src/editor/EditorApp.cpp` — Colores de preview en drawTilePalette()

### Criterio de aceptacion
- Biomas claramente diferenciados con transiciones suaves.
- Pendientes muestran tinte rocoso.

---

## Tarea 14.4 — Iluminacion y shaders Vulkan

### Objetivo
Implementar Blinn-Phong con ambient hemisferico, niebla lineal y animacion de agua en shaders Vulkan.

### Resultado esperado
- Terreno con iluminacion direccional, especular sutil y ambient sky/ground.
- Niebla que desvanece terreno distante.
- Vertices de agua con animacion sinusoidal.

### Archivos creados
1. `assets/shaders/terrain.vert` — Push constants, water anim, vWorldPos output
2. `assets/shaders/terrain.frag` — Blinn-Phong, hemispheric ambient, distance fog

### Archivos modificados
1. `src/rendering/vulkan/PipelineBuilder.cpp` — Push constant range para terrain pipeline
2. `src/rendering/vulkan/Renderer.h` — elapsedSeconds_, fogStart_, fogEnd_
3. `src/rendering/vulkan/Renderer.cpp` — Push constants, time tracking, fog sync
4. `CMakeLists.txt` — Compilacion de terrain.vert/frag a SPIR-V

### Criterio de aceptacion
- Vulkan preview muestra highlights especulares en crestas, niebla en distancia, agua ondulante.

---

## Tarea 14.5 — Ambient Occlusion

### Objetivo
Computar AO en CPU y bakearlo en vertex colors para oscurecer valles.

### Resultado esperado
- Valles y zonas bajas notablemente mas oscuras.
- Factor AO en rango [0.3, 1.0].

### Archivos modificados
1. `src/world/TerrainMesh.h` — Campo ao en TerrainVertex; declaracion computeAmbientOcclusion()
2. `src/world/TerrainMesh.cpp` — Implementacion computeAmbientOcclusion(), aplicacion en buildVulkanMesh()

### Criterio de aceptacion
- Valles visiblemente mas oscuros que crestas en editor y Vulkan preview.

---

## Tarea 14.6 — Controles UI de niebla

### Objetivo
Agregar controles de niebla al panel Tile Palette y sincronizar con Vulkan preview.

### Resultado esperado
- Toggle fog on/off y sliders start/end en editor.
- Parametros sincronizados via viewport state file.

### Archivos modificados
1. `src/editor/EditorApp.h` — fogEnabled, fogStart, fogEnd en Viewport3DState
2. `src/editor/EditorApp.cpp` — Collapsing header "Terrain Rendering", writeVulkanViewportStateFile()

### Criterio de aceptacion
- Toggle y sliders funcionales; cambios reflejados en Vulkan preview.

---

## Tarea 14.7 — Gravedad y terrain-following de personajes

### Objetivo
Los personajes (Player, Enemy) siguen la superficie del heightmap en lugar de flotar en un plano fijo.

### Resultado esperado
- Personajes suben y bajan con el terreno.
- Rendering ajustado para reflejar la altura del terreno.

### Archivos modificados
1. `src/core/Entity.h` — Campo z (altura del terreno)
2. `src/core/Character.h` — Campo vz (velocidad vertical para futura mecanica de salto)
3. `src/entities/Player.h` — Puntero world_, metodo setWorld()
4. `src/entities/Player.cpp` — Terrain snap en update(), offset de altura en draw()
5. `src/entities/Enemy.cpp` — Terrain snap en update(), offset de altura en draw()
6. `src/game/Game.h` — drawSpriteAtWorld() acepta z
7. `src/game/Game.cpp` — setWorld() en initSystems(), rendering con offset de altura

### Criterio de aceptacion
- Personajes suben visiblemente al caminar cuesta arriba y bajan cuesta abajo.
- Health bars y efectos de ataque siguen la altura del personaje.
- Editor no afectado (usa entityWorldZ de componentes).

---

## Resumen de archivos

| Archivo | Cambios |
|---------|---------|
| `src/rendering/IsoRenderer.h` | TILE_SCALE, worldToScreen escalado |
| `src/world/TerrainMesh.h` | Normals, AO, nuevos metodos |
| `src/world/TerrainMesh.cpp` | Smooth normals, AO, paleta, buildVulkanMesh, blending |
| `assets/shaders/terrain.vert` | Push constants, water anim, vWorldPos |
| `assets/shaders/terrain.frag` | Blinn-Phong, hemispheric ambient, fog |
| `src/editor/EditorApp.h` | Fog params en Viewport3DState |
| `src/editor/EditorApp.cpp` | SDL2 lighting, UI controls, TILE_SCALE projection |
| `src/rendering/vulkan/PipelineBuilder.cpp` | Push constant range terrain |
| `src/rendering/vulkan/Renderer.h` | elapsedSeconds_, fog params |
| `src/rendering/vulkan/Renderer.cpp` | Push constants, time, fog sync |
| `src/world/World.cpp` | TILE_SCALE en drawMesh |
| `src/game/Game.cpp` | TILE_SCALE, setWorld(), height rendering |
| `src/game/Game.h` | drawSpriteAtWorld con z |
| `src/core/Entity.h` | float z |
| `src/core/Character.h` | float vz |
| `src/entities/Player.h` | world_ pointer, setWorld() |
| `src/entities/Player.cpp` | Terrain snap, height draw |
| `src/entities/Enemy.cpp` | Terrain snap, height draw |
| `CMakeLists.txt` | Shader compilation targets |

## Verificacion

1. `cmake --build build --parallel` — compila sin errores
2. `ctest --test-dir build` — 24/26 tests pasan (2 pre-existentes)
3. Editor viewport: terreno con shading suave, AO en valles, transiciones de bioma
4. Vulkan preview: specular en crestas, niebla, agua animada
5. Game runtime: personajes siguen el heightmap
6. Herramientas (paint, height brush, undo/redo) sin regresiones
7. UI fog toggle/sliders funcionales
