# Instrucciones — Dash-Engine

## Requisitos previos

- macOS con Homebrew
- CMake ≥ 3.16
- SDL2 (`brew install sdl2`)
- Clang / Xcode Command Line Tools (`xcode-select --install`)

---

## Aliases de terminal

Los aliases se agregan automáticamente al `.zshrc`. Después de cada nueva terminal o de ejecutar `source ~/.zshrc`, están disponibles:

| Alias | Acción |
|---|---|
| `dash-build` | Configura CMake (si no existe el build) y compila todos los targets |
| `dash-editor` | Compila y abre el **editor DashEngine** |
| `dash-game` | Compila y ejecuta el **runtime VulkanBootstrap** standalone |
| `dash-update` | Hace `git pull` y recompila con los últimos cambios |
| `dash-clean` | Elimina el directorio `build/` para hacer un build desde cero |

---

## Flujos comunes

### Primer uso (clonar y compilar)

```bash
git clone https://github.com/LihuelIbanez/Dash-Engine.git
cd Dash-Engine
source ~/.zshrc   # cargar aliases si recién clonaste
dash-build
```

### Abrir el editor

```bash
dash-editor
```

### Jugar

```bash
dash-game
```

### Actualizar con los últimos cambios del repositorio

```bash
dash-update
```

Este comando:
1. Descarga los últimos cambios con `git pull`
2. Recompila solo los archivos modificados (incremental)

### Build limpio desde cero

```bash
dash-clean && dash-build
```

---

## Build manual (sin aliases)

```bash
# Configurar
cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# Compilar todo
cmake --build build --parallel

# Solo el editor
cmake --build build --target DashEngine --parallel

# Solo runtime standalone
cmake --build build --target VulkanBootstrap --parallel
```

---

## Ejecutables generados

| Archivo | Descripción |
|---|---|
| `build/DashEngine` | Editor de niveles (Dear ImGui, estilo Unreal) |
| `build/VulkanBootstrap` | Runtime standalone (Vulkan 3D, sin UI de editor) |

---

## Estructura del proyecto

```
Dash-Engine/
├── src/
│   ├── core/        # Entity, Character, Stats RPG
│   ├── entities/    # Player, Enemy
│   ├── world/       # World: grid 64×64, generación procedural
│   ├── rendering/   # IsoRenderer, Font5x7
│   ├── game/        # Game loop, HUD, combate
│   └── editor/      # EditorApp (ImGui panels, herramientas)
├── scenes/          # Escenas .json
├── build/           # Binarios compilados (generado por CMake)
├── planning/        # Roadmap y sprint diario
├── CMakeLists.txt
└── instrucciones.md
```

---

## Solución de problemas

### "SDL2 not found"
```bash
brew install sdl2
```

### IntelliSense sin funcionar en VS Code
El archivo `compile_commands.json` en la raíz del proyecto se genera automáticamente al hacer `dash-build`. VS Code lo lee para resolver includes.

### Build corrompido o errores raros
```bash
dash-clean && dash-build
```
