# Sprint 4 — Sprite Editor (10 días)

Objetivo: implementar un editor de sprites integrado en el editor que permita crear, editar y asignar sprites a entidades via `RenderComponent`. Incluye herramientas básicas de imagen (lápiz, relleno, borrador, cuentagotas, línea, rectángulo, selección), paleta de colores, capas simples, preview isométrico en tiempo real y exportación PNG.

Prerequisito: Sprint 3 (D31-D35) completado. Font Awesome 6 integrado.

---

## Tablero del Sprint

### To Do

(vacío)

### Doing

(vacío)

### Done

- D36 — Infraestructura: stb + TextureCache + SpriteAsset ✅
- D37 — Canvas: buffer STREAMING + render + zoom ✅
- D38 — Herramientas 1: Lápiz, Borrador, Relleno (flood-fill), Cuentagotas ✅
- D39 — Herramientas 2: Línea, Rectángulo, Selección + copy/paste ✅
- D40 — Paleta de colores + historial de colores usados ✅
- D41 — Capas: add/delete/reorder/visibility/opacity ✅
- D42 — Exportación PNG + integración RenderComponent ✅
- D43 — SpriteImporter + AssetDatabase hook ✅
- D44 — Preview isométrico en viewport + alineación de tiles ✅
- D45 — Tests + pulido + cierre ✅

---

## D36 — Infraestructura: stb_image_write + TextureCache + SpriteAsset

- ID: D36
- Meta: Descargar `stb_image_write.h` vía CMake, crear `TextureCache` para manejar ciclo de vida de `SDL_Texture*`, definir `SpriteAsset` como tipo de asset.
- Horas estimadas: 5h
- Dependencias: ninguna

### Tareas

1. **CMakeLists.txt** — agregar `file(DOWNLOAD)` para `stb_image_write.h` y `stb_image.h` de nothings/stb:
   ```cmake
   set(STB_PATH "${CMAKE_SOURCE_DIR}/src/editor/stb_image_write.h")
   if(NOT EXISTS "${STB_PATH}")
       file(DOWNLOAD "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"
            "${STB_PATH}" STATUS dl_status)
   endif()
   set(STB_READ_PATH "${CMAKE_SOURCE_DIR}/src/editor/stb_image.h")
   if(NOT EXISTS "${STB_READ_PATH}")
       file(DOWNLOAD "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
            "${STB_READ_PATH}" STATUS dl_status)
   endif()
   ```

2. **`src/editor/TextureCache.h/.cpp`** — singleton que mantiene `std::unordered_map<std::string, SDL_Texture*>`. API:
   ```cpp
   class TextureCache {
   public:
       static TextureCache& instance();
       SDL_Texture* load(SDL_Renderer* r, const std::string& path);
       void         release(const std::string& path);
       void         clear(SDL_Renderer* r);   // llamar en destructor de EditorApp
   private:
       std::unordered_map<std::string, SDL_Texture*> cache_;
   };
   ```
   - `load()` devuelve el texture cacheado si existe, si no llama `stb_image` con `SDL_CreateTexture` + `SDL_UpdateTexture`.
   - `clear()` llama `SDL_DestroyTexture` en todos los valores y limpia el map.

3. **`src/core/assets/SpriteAsset.h`** — struct que describe el asset:
   ```cpp
   struct SpriteAsset {
       std::string guid;
       std::string sourcePath;   // assets/sprites/<nombre>.png
       int         width  = 16;
       int         height = 16;
   };
   ```

4. **`AssetTypes.h`** — agregar `Sprite` al enum `AssetType`.

5. **`assets/sprites/`** — crear `.gitkeep` para que el directorio exista en el repo.

6. **`src/editor/stb_impl.cpp`** — definir los `#define STB_IMAGE_WRITE_IMPLEMENTATION` y `STB_IMAGE_IMPLEMENTATION` en un TU propio para evitar símbolos duplicados:
   ```cpp
   #define STB_IMAGE_WRITE_IMPLEMENTATION
   #include "stb_image_write.h"
   #define STB_IMAGE_IMPLEMENTATION
   #include "stb_image.h"
   ```

7. **CMakeLists.txt** — agregar `src/editor/stb_impl.cpp` a `DashEngine` sources.

### Criterio de aceptación
- `cmake` descarga ambos headers si no existen.
- `TextureCache::load()` carga un PNG de `assets/sprites/` y devuelve un `SDL_Texture*` válido.
- `AssetType::Sprite` compila sin errores.

---

## D37 — Canvas: buffer STREAMING + render + zoom + cuadrícula

- ID: D37
- Meta: Crear `SpriteEditorPanel` con canvas editable basado en `SDL_TEXTUREACCESS_STREAMING`, renderizado vía `ImGui::Image` con zoom variable y cuadrícula superpuesta con `ImDrawList`.
- Horas estimadas: 6h
- Dependencias: D36

### Tareas

1. **`src/editor/panels/SpriteEditorPanel.h`** — declaración del panel:
   ```cpp
   class SpriteEditorPanel {
   public:
       SpriteEditorPanel() = default;
       ~SpriteEditorPanel();   // destruye canvasTex_
       void init(SDL_Renderer* r);
       void draw();
       bool isOpen = true;
   private:
       SDL_Renderer* renderer_     = nullptr;
       SDL_Texture*  canvasTex_    = nullptr;
       int           canvasW_      = 16;
       int           canvasH_      = 16;
       float         zoom_         = 16.f;   // píxeles por celda en pantalla
       std::vector<uint32_t> pixels_;         // RGBA, tamaño = canvasW_ * canvasH_
       bool          dirty_        = false;  // ¿canvas modificado sin subir a GPU?

       void rebuildTexture();
       void uploadToGPU();
       void drawCanvas(ImVec2 origin, ImVec2 size);
       void drawGrid(ImDrawList* dl, ImVec2 origin, float cellSize, int cols, int rows);
   };
   ```

2. **`src/editor/panels/SpriteEditorPanel.cpp`** — implementación:
   - `init()`: llama `rebuildTexture()` con el tamaño inicial (16×16), rellena `pixels_` con blanco transparente `0x00000000`.
   - `rebuildTexture()`: destruye `canvasTex_` si existe, crea nuevo con `SDL_TEXTUREACCESS_STREAMING` + `SDL_PIXELFORMAT_ABGR8888`, llama `uploadToGPU()`.
   - `uploadToGPU()`: `SDL_UpdateTexture(canvasTex_, nullptr, pixels_.data(), canvasW_ * 4)`.
   - `draw()`:
     - Barra superior: botones **New** (modal de tamaño), **Open**, **Save**, combo de zoom (2×, 4×, 8×, 12×, 16×, 24×, 32×).
     - Panel central dividido (splitter): izquierda = canvas, derecha = herramientas + paleta (D38-D40).
     - `ImGui::Image((ImTextureID)canvasTex_, {canvasW_ * zoom_, canvasH_ * zoom_})` con `SDL_BLENDMODE_BLEND`.
   - `drawGrid()`: usa `ImDrawList::AddLine` para dibujar la cuadrícula con color `IM_COL32(80,80,80,120)`.
   - Interacción: detectar `ImGui::IsItemHovered()` + `ImGui::GetMousePos()` → calcular celda `(x, y)` = `(mousePos - origin) / zoom_`, clampear a `[0, w-1]×[0, h-1]`.

3. **`EditorApp.h`** — agregar `SpriteEditorPanel spriteEditor_` como miembro y `bool showSpriteEditor_ = false`.

4. **`EditorApp.cpp`** — conectar el panel:
   - `init()`: llamar `spriteEditor_.init(renderer_)`.
   - Menú **Assets** → agregar `ImGui::MenuItem(ICON_FA_PAINTBRUSH " Sprite Editor", nullptr, &showSpriteEditor_)`.
   - En `draw()`: `if (showSpriteEditor_) spriteEditor_.draw()`.

5. **CMakeLists.txt** — agregar `SpriteEditorPanel.cpp` a `PANEL_SOURCES`.

### Criterio de aceptación
- El menú Assets tiene "Sprite Editor" que abre el panel.
- El panel muestra un canvas vacío con cuadrícula.
- El zoom cambia el tamaño visible del canvas correctamente.

---

## D38 — Herramientas 1: Lápiz, Borrador, Relleno flood-fill, Cuentagotas

- ID: D38
- Meta: Implementar las 4 herramientas básicas de dibujo con interacción de mouse por píxel.
- Horas estimadas: 6h
- Dependencias: D37

### Diseño interno

```cpp
enum class SpriteTool { Pencil, Eraser, Fill, Eyedropper };
```

El color activo se guarda como `uint32_t fgColor_` (ABGR8888). Las herramientas solo modifican `pixels_` y marcan `dirty_ = true`; `uploadToGPU()` se invoca al final de cada frame que tuvo cambios.

### Tareas

1. **Lápiz** (`SpriteTool::Pencil`):
   - `ImGui::IsMouseDown(0)` sobre el canvas → pintar la celda bajo el cursor con `fgColor_`.
   - Interpolación lineal entre el píxel del frame anterior y el actual (bresenham) para evitar huecos al mover rápido.
   - Soportar tamaño de pincel 1×1, 2×2, 3×3 (selector en la barra de herramientas).

2. **Borrador** (`SpriteTool::Eraser`):
   - Igual que lápiz pero escribe `0x00000000` (transparente).
   - Mismo selector de tamaño de pincel.

3. **Relleno flood-fill** (`SpriteTool::Fill`):
   - Click → BFS/DFS desde la celda clickeada, expande a vecinos 4-conectados con el mismo color original.
   - Reemplaza con `fgColor_`.
   - Límite de 16×16 = 256 píxeles → no necesita optimización.

4. **Cuentagotas** (`SpriteTool::Eyedropper`):
   - Click sobre una celda → `fgColor_ = pixels_[y * canvasW_ + x]`.
   - Restaurar la herramienta anterior automáticamente (toggle temporal, como Photoshop con Alt).

5. **Barra de herramientas vertical** — botones con iconos FA:
   ```
   ICON_FA_PENCIL      → Pencil
   ICON_FA_ERASER      → Eraser
   ICON_FA_FILL_DRIP   → Fill
   ICON_FA_EYE_DROPPER → Eyedropper
   ```
   Botón activo resaltado con `ImGui::PushStyleColor(ImGuiCol_Button, activeColor)`.

6. **Selector de tamaño de pincel** — `ImGui::SliderInt("Brush", &brushSize_, 1, 5)`.

7. **Tick de `dirty_`** — al final de `draw()`:
   ```cpp
   if (dirty_) { uploadToGPU(); dirty_ = false; }
   ```

### Criterio de aceptación
- Lápiz dibuja píxeles individuales con arrastre continuo sin huecos.
- Borrador limpia a transparente.
- Flood-fill rellena una región cerrada correctamente.
- Cuentagotas captura el color de la celda y lo pone como color activo.

---

## D39 — Herramientas 2: Línea, Rectángulo, Selección + copy/paste

- ID: D39
- Meta: Añadir herramientas geométricas y selección rectangular con portapapeles interno.
- Horas estimadas: 6h
- Dependencias: D38

### Tareas

1. **Línea** (`SpriteTool::Line`):
   - `MouseDown` → fijar punto origen.
   - Mientras arrastra → renderizar preview (overlay con `ImDrawList::AddLine` en coordenadas de pantalla, sin tocar `pixels_`).
   - `MouseUp` → aplicar Bresenham sobre `pixels_` con `fgColor_`.

2. **Rectángulo** (`SpriteTool::Rect`):
   - `MouseDown` → fijar esquina A.
   - Mientras arrastra → preview con `ImDrawList::AddRect`.
   - `MouseUp` → rellenar borde del rectángulo en `pixels_` (modo outline).
   - Modificador `Shift` held → rellenar interior también (modo fill).

3. **Selección rectangular** (`SpriteTool::Select`):
   - `MouseDown` + drag → dibuja marquee animado (`ImDrawList::AddRect` con dashes via `AddPolyline`).
   - `MouseUp` → almacena `selRect_` (`SDL_Rect` en coordenadas de canvas).
   - Indicador visual de selección persistente sobre el canvas.

4. **Copy/Paste interno**:
   ```cpp
   std::vector<uint32_t> clipboard_;  // píxeles copiados
   int clipW_, clipH_;                // dimensiones del clipboard
   ```
   - `Ctrl+C` → copiar píxeles de `selRect_` a `clipboard_`.
   - `Ctrl+V` → pegar en la posición del cursor (sin salirse de los límites, clamp).
   - `Escape` → deseleccionar (`selRect_ = {-1,-1,0,0}`).

5. **Delete/Backspace** sobre selección activa → borrar la región a `0x00000000`.

6. **Atajos de teclado** para herramientas:
   ```
   P → Pencil
   E → Eraser
   G → Fill (bucket)
   I → Eyedropper
   L → Line
   R → Rect
   S → Select
   ```
   Solo activos cuando el panel SpriteEditor tiene foco.

7. **Barra de herramientas** — agregar los 3 nuevos botones con iconos FA:
   ```
   ICON_FA_MINUS      → Line
   ICON_FA_SQUARE     → Rect
   ICON_FA_OBJECT_GROUP → Select
   ```

### Criterio de aceptación
- Línea y Rectángulo muestran preview durante el arrastre y aplican al soltar.
- Copy/paste dentro del mismo canvas funciona sin corrupción de píxeles.
- Los atajos de teclado cambian la herramienta activa.

---

## D40 — Paleta de colores + historial de colores usados

- ID: D40
- Meta: Implementar la UI de color: `ImGui::ColorPicker4` para selección precisa, paleta de 32 colores editables, historial de los últimos 16 colores usados, muestras de foreground/background.
- Horas estimadas: 4h
- Dependencias: D38

### Tareas

1. **Color FG/BG** — dos cuadrados superpuestos clicables (como Photoshop):
   - Cuadrado grande = FG (`fgColor_`). Click → abre `ColorPicker4`.
   - Cuadrado pequeño desplazado = BG (`bgColor_`). Click → abre `ColorPicker4` para BG.
   - Tecla `X` → intercambiar FG y BG.
   - Tecla `D` → resetear a negro (FG) y transparente (BG).

2. **Paleta fija de 32 colores** (2 filas de 16):
   - Array `uint32_t palette_[32]` inicializado con paleta NES/PICO-8.
   - Click izquierdo sobre una muestra → `fgColor_ = palette_[i]`.
   - Click derecho → `bgColor_ = palette_[i]`.
   - Doble click → abre `ColorPicker4` para editar ese slot de paleta.
   - Botones `Import Palette` / `Export Palette` → JSON `{"colors": [0xRRGGBBAA, ...]}`.

3. **Historial de colores usados** (últimos 16):
   ```cpp
   std::deque<uint32_t> colorHistory_;  // máx 16 elementos
   ```
   - Cada vez que se aplica un pincel/línea/rect/fill → si el color no está ya en el historial, push_front y pop_back si size > 16.
   - Renderizar como fila de cuadrados 14×14 debajo de la paleta. Click → `fgColor_ = colorHistory_[i]`.

4. **`ImGui::ColorPicker4`** —  inline, colapsable con `ImGui::CollapsingHeader("Color Picker")`.

5. **Alpha slider** adicional debajo del picker para controlar la transparencia del FG directamente.

### Criterio de aceptación
- FG/BG visibles, intercambiables y conectados a todas las herramientas.
- La paleta de 32 colores es editable y persiste en `assets/sprites/default_palette.json`.
- El historial acumula colores usados correctamente.

---

## D41 — Capas: add/delete/reorder/toggle visibilidad/opacidad

- ID: D41
- Meta: Sistema de capas simple (máx 8) con fusión alpha-over para composición final. El canvas edita siempre la capa activa.
- Horas estimadas: 7h
- Dependencias: D37

### Diseño de datos

```cpp
struct SpriteLayer {
    std::string          name     = "Layer";
    std::vector<uint32_t> pixels;   // canvasW * canvasH, ABGR8888
    float                opacity  = 1.f;  // 0.0 – 1.0
    bool                 visible  = true;
};
```

`pixels_` (el buffer plano que se sube a GPU) se convierte en el resultado de la composición de todas las capas visibles → recalcular en `compositeLayers()`.

### Tareas

1. **Refactor `SpriteEditorPanel`**:
   - Renombrar `pixels_` → `composite_` (solo lectura, resultado final).
   - Agregar `std::vector<SpriteLayer> layers_` y `int activeLayer_ = 0`.
   - Toda escritura de herramientas va a `layers_[activeLayer_].pixels`.

2. **`compositeLayers()`**:
   ```cpp
   void SpriteEditorPanel::compositeLayers() {
       std::fill(composite_.begin(), composite_.end(), 0u);
       for (auto& layer : layers_) {
           if (!layer.visible) continue;
           for (int i = 0; i < canvasW_ * canvasH_; ++i)
               composite_[i] = alphaOver(composite_[i], layer.pixels[i], layer.opacity);
       }
       dirty_ = true;
   }
   ```
   - `alphaOver()` implementa Porter-Duff src-over en ABGR8888.

3. **Panel de capas** (columna derecha del panel):
   ```
   ┌─ Layers ──────────────────┐
   │ [+] [-] [↑] [↓]          │
   │ ▶ ● Layer 2  opacity: 80%│  ← activa, resaltada
   │   ● Layer 1  opacity:100%│
   │   ○ BG       opacity:100%│  ○ = oculta
   └───────────────────────────┘
   ```
   - `[+]` → `addLayer()` (copia dimensiones, rellena 0), inserta encima de la activa.
   - `[-]` → `deleteLayer()` (si quedan ≥ 2 capas).
   - `[↑]/[↓]` → `std::swap` de la capa activa con la anterior/siguiente, recomponer.
   - Click en nombre → la activa cambia, recomponer.
   - Doble click en nombre → editar texto inline (`ImGui::InputText`).
   - `●/○` → toggle `layer.visible`, recomponer.
   - `ImGui::SliderFloat("##op", &layer.opacity, 0.f, 1.f)` → recomponer al cambiar.

4. **Merge down** (botón contextual con click derecho sobre una capa):
   - Fusionar la capa activa sobre la capa inferior y eliminarla.

5. **Aplanar todo** (`Flatten All`) → fusiona todas las capas en una sola.

6. **Rendimiento**: `compositeLayers()` solo se invoca cuando hay cambio real (herramienta aplica píxel, toggle visibilidad, cambio de opacidad). No recalcular cada frame.

### Criterio de aceptación
- Panel muestra lista de capas con nombre, visibilidad, opacidad.
- Herramientas pintan solo en la capa activa.
- La composición final es correcta (alpha-over) y el canvas muestra el resultado.
- Add/delete/reorder de capas funciona con ≤ 8 capas.

---

## D42 — Exportación PNG + integración con RenderComponent

- ID: D42
- Meta: Guardar el sprite como PNG en `assets/sprites/`, actualizar `RenderComponent.sprite` de la entidad seleccionada en el editor y notificar al `AssetBrowserPanel`.
- Horas estimadas: 5h
- Dependencias: D41, D36

### Tareas

1. **`SpriteEditorPanel::saveAsPNG(const std::string& path)`**:
   ```cpp
   bool SpriteEditorPanel::saveAsPNG(const std::string& path) {
       compositeLayers();
       // stb_image_write espera RGBA, el compositor usa ABGR → convertir
       std::vector<uint8_t> rgba(canvasW_ * canvasH_ * 4);
       for (int i = 0; i < canvasW_ * canvasH_; ++i) {
           uint32_t px = composite_[i]; // ABGR8888
           rgba[i*4+0] = (px >>  0) & 0xFF; // R
           rgba[i*4+1] = (px >>  8) & 0xFF; // G
           rgba[i*4+2] = (px >> 16) & 0xFF; // B
           rgba[i*4+3] = (px >> 24) & 0xFF; // A
       }
       return stbi_write_png(path.c_str(), canvasW_, canvasH_, 4, rgba.data(), canvasW_ * 4) != 0;
   }
   ```

2. **Botón Save** (`ICON_FA_FLOPPY_DISK`) en la barra superior:
   - Si `currentPath_` está definido → guardar directamente.
   - Si no → mostrar `ImGui::InputText` modal para pedir nombre → guarda en `assets/sprites/<nombre>.png`.

3. **Botón Open** (`ICON_FA_FOLDER_OPEN`):
   - Lista los archivos en `assets/sprites/` → `ImGui::ListBox` para seleccionar.
   - Al seleccionar → carga con `stb_image`, recrea el canvas con las dimensiones del PNG, aplana en la capa 0 (o crea capa nueva).

4. **Botón New** (`ICON_FA_FILE`):
   - Modal con `ImGui::InputInt` para ancho/alto (8, 16, 32, 48, 64).
   - Resetea capas, rellena con transparente, limpia `currentPath_`.

5. **Panel "Assign to Entity"** — colapsable al pie del SpriteEditorPanel:
   - Muestra el nombre de la entidad seleccionada en el editor (`editorSelectedEntityId_`).
   - Botón **Assign** → si `currentPath_` existe, emite un `EditComponentFieldCommand` que actualiza `RenderComponent.sprite = <nombre_relativo>`.
   - Si la entidad no tiene `RenderComponent`, primero lo añade via `AddComponentCommand` (nuevo comando a crear en D42).

6. **`AddComponentCommand`** — nuevo comando:
   ```cpp
   class AddComponentCommand : public ICommand {
   public:
       AddComponentCommand(uint64_t entityId, ComponentVariant comp);
       void apply(SceneData& scene, World& world) override;
       void undo (SceneData& scene, World& world) override;
       const char* name() const override { return "Add Component"; }
   };
   ```

7. **Notificación a AssetBrowserPanel**: tras guardar PNG, llamar `importManager_.importAll()` para actualizar `AssetDatabase`. (Patrón ya establecido en D09.)

8. **`TextureCache::invalidate(path)`** — agregar método para forzar recarga de una textura específica tras guardar.

### Criterio de aceptación
- Save genera un PNG legible en `assets/sprites/`.
- Open carga el PNG correctamente en el canvas.
- Assign actualiza `RenderComponent.sprite` con undo/redo funcionales.
- El AssetBrowser muestra el sprite recién guardado tras el save.

---

## D43 — SpriteImporter + carga en runtime

- ID: D43
- Meta: Crear `SpriteImporter` (para el pipeline de assets del editor) y conectar `TextureCache` al renderer del viewport para que las entidades muestren sus sprites en lugar del diamante de color.
- Horas estimadas: 5h
- Dependencias: D42

### Tareas

1. **`src/editor/importers/SpriteImporter.h/.cpp`** — implementa `IImporter`:
   - `AssetType` aceptado: `AssetType::Sprite`.
   - `import()` → copia el PNG a `library/sprites/<guid>.png`, registra en `AssetDatabase`.
   - `canImport(path)` → `path.extension() == ".png"`.

2. **Registrar en `ImportManager`** — agregar `registerImporter(std::make_unique<SpriteImporter>())`.

3. **Conectar `TextureCache` al viewport**:
   - En `EditorApp::renderWorldToTexture()` (Edit mode), cuando se dibuja una entidad:
     - Obtener `RenderComponent* rc` de la entidad.
     - Si `rc->sprite != "default"` → `SDL_Texture* tex = TextureCache::instance().load(renderer_, assetsRoot + "/sprites/" + rc->sprite + ".png")`.
     - Si `tex` es válido → `SDL_RenderCopy` de la textura en la posición isométrica en lugar del diamante de color.
     - Si no → fallback al diamante existente.

4. **`EditorApp::shutdown()`** — llamar `TextureCache::instance().clear(renderer_)` antes de `SDL_DestroyRenderer`.

5. **Preview en `SpriteEditorPanel`** — miniatura `64×64` de la composición actual en el panel, renderizada como `ImGui::Image` cada frame (sin zoom).

### Criterio de aceptación
- El importer registra sprites PNG en AssetDatabase correctamente.
- Una entidad con `RenderComponent.sprite = "hero"` muestra el PNG `assets/sprites/hero.png` en el viewport.
- Entidades sin sprite asignado siguen mostrando el diamante de color.

---

## D44 — Preview isométrico + alineación de tiles

- ID: D44
- Meta: Añadir en el SpriteEditorPanel una ventana de preview que muestra cómo queda el sprite "montado" sobre un tile isométrico, con opciones de alineación (anchor point).
- Horas estimadas: 5h
- Dependencias: D43

### Tareas

1. **Sub-panel "Iso Preview"** (colapsable, lado derecho del SpriteEditorPanel):
   - Dibuja un tile isométrico de referencia usando `ImDrawList` (el mismo `drawDiamond` de `IsoRenderer.h`).
   - Superpone `ImGui::Image` del sprite actual posicionado según el anchor.
   - Muestra el resultado a escala 2× y 4×.

2. **Anchor point** — enum + selector visual:
   ```
   enum class SpriteAnchor {
       TopLeft, TopCenter, TopRight,
       MiddleLeft, Center, MiddleRight,
       BottomLeft, BottomCenter, BottomRight,
       Custom   // pixel exacto en canvasW_/canvasH_
   };
   ```
   - UI: grid de 3×3 botones pequeños como en Unity/Aseprite.
   - `Custom` → `ImGui::InputFloat2("Pivot", &pivotX_, &pivotY_)`.

3. **Persitir anchor en metadatos**:
   - Agregar `SpriteAnchor anchor` y `float pivotX, pivotY` a `SpriteAsset`.
   - Al guardar PNG → guardar también `assets/sprites/<nombre>.sprite.json` con los metadatos:
     ```json
     { "anchor": "BottomCenter", "pivotX": 0.5, "pivotY": 1.0 }
     ```

4. **Usar anchor en el viewport**: en `renderWorldToTexture()`, al hacer `SDL_RenderCopy`, calcular el `SDL_Rect dst` usando el pivot para centrar correctamente el sprite sobre el tile.

5. **Botón "Snap to tile bottom"** — atajo que setea automáticamente `BottomCenter` y calcula el pivot para que el sprite quede apoyado en el vértice inferior del tile.

6. **Fondo del preview** — toggle entre: transparente (tablero de ajedrez), negro, blanco.

### Criterio de aceptación
- El preview iso muestra el sprite sobre un tile de referencia con la alineación correcta.
- Cambiar el anchor actualiza el preview en tiempo real.
- El JSON de metadatos se guarda/carga correctamente.
- El viewport usa el pivot del sprite al renderizar entidades.

---

## D45 — Tests + pulido + cierre sprint

- ID: D45
- Meta: Tests de la lógica de edición (flood-fill, Bresenham, composición de capas, serialización PNG), corrección de bugs tras testing integrado, actualización del índice y checklist.
- Horas estimadas: 5h
- Dependencias: D44

### Tareas

1. **`tests/test_sprite_editor.cpp`** — suite de tests sin SDL (solo lógica pura):
   - `test_floodFill_fillsConnectedRegion`: buffer 8×8, fill desde center, verifica píxeles.
   - `test_floodFill_doesNotLeakThroughBorders`: región cerrada, fill no sale.
   - `test_bresenham_line`: línea (0,0)→(7,7), verifica 8 píxeles diagonales.
   - `test_bresenham_horizontal`: línea horizontal, sin huecos.
   - `test_alphaOver_fullyOpaque`: src opaco → resultado = src.
   - `test_alphaOver_fullyTransparent`: src transparente → resultado = dst.
   - `test_compositeLayers_visibilityRespected`: capa invisible no aporta al composite.
   - `test_compositeLayers_opacityBlend`: capa al 50% opacidad mezcla correctamente.
   - `test_saveLoad_png_roundtrip`: guardar PNG → cargar con stb_image → pixeles iguales.
   - `test_palette_import_export`: serializar/deserializar paleta JSON.

2. **Extraer lógica pura** de `SpriteEditorPanel` a funciones libres en `src/editor/SpriteOps.h`:
   ```cpp
   namespace SpriteOps {
       void floodFill(std::vector<uint32_t>& pixels, int w, int h,
                      int x, int y, uint32_t newColor);
       void drawLine(std::vector<uint32_t>& pixels, int w, int h,
                     int x0, int y0, int x1, int y1, uint32_t color);
       uint32_t alphaOver(uint32_t dst, uint32_t src, float opacity);
   }
   ```
   `SpriteEditorPanel` llama `SpriteOps::*` internamente.

3. **CMakeLists.txt** — agregar `tests/test_sprite_editor.cpp` a la suite de tests.

4. **Smoke test integrado**: correr el editor, crear un sprite 16×16, dibujar con cada herramienta, guardar, asignar a Hero, verificar que aparece en el viewport.

5. **Pulido UI**:
   - Tooltip en cada herramienta con nombre + shortcut.
   - Cursor del mouse cambia según herramienta (`ImGui::SetMouseCursor`).
   - Título del panel incluye el nombre del sprite y `*` si hay cambios sin guardar.
   - Confirmación de "¿Guardar antes de cerrar?" al cerrar el panel con cambios.

6. **Actualizar `00_INDEX.md`** con Sprint 4 entrada.

7. **Actualizar `99_ACCEPTANCE_CHECKLIST.md`** con ítems de Sprint 4.

8. **`git tag v4.0-alpha`** tras build limpio y tests OK.

### Criterio de aceptación
- `ctest` pasa todos los tests del sprite editor sin SDL.
- 0 warnings en Release mode.
- El flujo completo (crear → dibujar → guardar → asignar → ver en viewport) funciona end-to-end.

---

## Registro Diario de Ejecución

- [x] Dia 36 | ID: D36 | Plan: 5h | Real: ~1h | Bloqueos: warnings stb (resueltos con pragma) | Resultado: stb_image+write descargados, stb_impl.cpp, TextureCache singleton, SpriteAsset.h, AssetType::Sprite, assets/sprites/ creado. Build 0 warnings.
- [x] Dia 37 | ID: D37 | Plan: 6h | Real: ~2h | Bloqueos: API ImGui KeysDown removida + modal fuera de scope (resuelto) | Resultado: SpriteEditorPanel integrado en EditorApp, canvas+grid+zoom, build limpio.
- [x] Dia 38 | ID: D38 | Plan: 6h | Real: ~2h | Bloqueos: — | Resultado: Lápiz/Borrador/Fill/Cuentagotas con interacción por píxel, arrastre continuo con Bresenham.
- [x] Dia 39 | ID: D39 | Plan: 6h | Real: ~1h | Bloqueos: — | Resultado: Line/Rect/Select con preview, copy/paste interno, delete y shortcuts de herramientas.
- [x] Dia 40 | ID: D40 | Plan: 4h | Real: ~2h | Bloqueos: — | Resultado: picker inline + alpha, paleta editable de 32, historial de 16, import/export `default_palette.json`.
- [x] Dia 41 | ID: D41 | Plan: 7h | Real: 7h | Bloqueos: — | Resultado: Sistema de capas completo; SpriteLayer struct, Porter-Duff alphaOver, compositeLayers, add/delete/reorder/merge/flatten, UI panel con visibility+opacity, build 0 warnings ✅
- [x] Dia 42 | ID: D42 | Plan: 5h | Real: ~2h | Bloqueos: forward decl class/struct mismatch (resuelto) | Resultado: saveAsPNG, loadFromPNG, Open modal, Assign panel (AddComponent+EditField undoable), TextureCache invalidate post-save. Build 0 warnings ✅
- [x] Dia 43 | ID: D43 | Plan: 5h | Real: ~2h | Bloqueos: linker por glob de CMake (resuelto con reconfigure) | Resultado: SpriteImporter registrado, inferAssetType detecta sprites/, render en viewport con TextureCache + fallback, clear en shutdown, preview 64x64. Build 0 warnings ✅
- [x] Dia 44 | ID: D44 | Plan: 5h | Real: ~3h | Bloqueos: Build_CMakeTools no disponible en entorno (resuelto con cmake+make) | Resultado: Iso Preview 2x/4x con tile de referencia, grid de anchor 3x3 + Custom pivot + snap bottom + fondo selectable, guardado/carga de metadata .sprite.json, viewport usa pivot al renderizar sprites. Build 0 warnings ✅
- [x] Dia 45 | ID: D45 | Plan: 5h | Real: ~3h | Bloqueos: warning linkage de stb en test nuevo (resuelto vinculando stb_impl.cpp) | Resultado: SpriteOps extraído (floodFill/drawLine/alphaOver), test_sprite_editor con 10 casos, pulido UI (tooltips+shortcuts, cursor por herramienta, confirmación al cerrar con cambios), ctest 14/14 passing, build limpio ✅

**Total estimado: 54 horas (10 días a ~5-6h/día)**

---

## Dependencias entre tareas

```
D36 (infra)
 └─ D37 (canvas)
     ├─ D38 (herr. básicas)
     │   ├─ D39 (herr. geométricas)
     │   └─ D40 (paleta)
     └─ D41 (capas)
         └─ D42 (export + assign)   ←── también depende D36
             └─ D43 (importer + runtime)
                 └─ D44 (preview iso)
                     └─ D45 (tests + cierre)
```

---

## Archivos nuevos a crear

| Archivo | Día |
|---|---|
| `src/editor/stb_image_write.h` | D36 (descarga) |
| `src/editor/stb_image.h` | D36 (descarga) |
| `src/editor/stb_impl.cpp` | D36 |
| `src/editor/TextureCache.h` | D36 |
| `src/editor/TextureCache.cpp` | D36 |
| `src/core/assets/SpriteAsset.h` | D36 |
| `assets/sprites/.gitkeep` | D36 |
| `src/editor/panels/SpriteEditorPanel.h` | D37 |
| `src/editor/panels/SpriteEditorPanel.cpp` | D37 |
| `src/editor/SpriteOps.h` | D45 |
| `src/editor/importers/SpriteImporter.h` | D43 |
| `src/editor/importers/SpriteImporter.cpp` | D43 |
| `tests/test_sprite_editor.cpp` | D45 |

## Archivos a modificar

| Archivo | Qué cambia | Día |
|---|---|---|
| `CMakeLists.txt` | Descargas stb, `stb_impl.cpp`, `SpriteEditorPanel.cpp`, `SpriteImporter.cpp`, test suite | D36, D37, D43, D45 |
| `src/core/assets/AssetTypes.h` | Agregar `Sprite` al enum | D36 |
| `src/editor/EditorApp.h` | Miembro `SpriteEditorPanel`, `TextureCache init` | D37 |
| `src/editor/EditorApp.cpp` | Menú, draw call, shutdown, renderWorldToTexture con sprites | D37, D43 |
| `src/editor/importers/ImportManager.cpp` | Registrar `SpriteImporter` | D43 |
| `src/editor/commands/` | Nuevo `AddComponentCommand` | D42 |
| `planning/tasks/00_INDEX.md` | Nueva entrada Sprint 4 | D45 |
| `planning/tasks/99_ACCEPTANCE_CHECKLIST.md` | Ítems Sprint 4 | D45 |
