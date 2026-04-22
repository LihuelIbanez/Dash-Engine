# Instrucciones — Dash-Engine

## Requisitos previos

### Windows
- Visual Studio 2022+ con soporte C++ y CMake
- [vcpkg](https://github.com/microsoft/vcpkg) en `C:\vcpkg`
- Git

### macOS
- Homebrew, CMake ≥ 3.16
- SDL2 (`brew install sdl2`)
- Xcode Command Line Tools (`xcode-select --install`)

---

## Comandos `dash` — Windows (PowerShell)

El script `dash.ps1` en la raíz del proyecto expone todos los flujos como `dash <comando>`.

### Configuración del alias (una sola vez)

```powershell
Add-Content $PROFILE 'function dash { powershell -ExecutionPolicy Bypass -File "E:\Develop\Proyects\Dash-Engine\dash.ps1" @args }'
. $PROFILE
```

> Ya aplicado en este equipo. En una nueva máquina, ejecutar el bloque de arriba desde la raíz del proyecto.

### Referencia de comandos

| Comando | Acción |
|---|---|
| `dash build` | Compila todo (editor + runtime) |
| `dash editor` | Compila y abre el **editor DashEngine** |
| `dash run` | Abre el editor **sin recompilar** |
| `dash game` | Abre el runtime **IsometricRPG** sin recompilar |
| `dash clean` | Elimina `build/` para recompilar desde cero |
| `dash update` | `git pull` + recompila |
| `dash test` | Compila y ejecuta los tests |
| `dash config` | Solo configura CMake (sin compilar) |
| `dash help` | Muestra ayuda y opciones disponibles |

### Opciones

| Opción | Default | Descripción |
|---|---|---|
| `-Config` | `Release` | `Release` o `Debug` |
| `-BuildDir` | `build` | Directorio de build |
| `-VcpkgRoot` | `C:\vcpkg` | Ruta de instalación de vcpkg |
| `-Vulkan` | off | Habilita el target `VulkanBootstrap` |

### Ejemplos

```powershell
# Primer uso
dash build

# Abrir el editor (compila si hace falta)
dash editor

# Solo abrir el editor ya compilado
dash run

# Recompilar desde cero
dash clean
dash build

# Actualizar y recompilar
dash update

# Build debug
dash build -Config Debug

# Con soporte Vulkan
dash build -Vulkan
```

---

## Comandos `dash` — macOS (zsh)

Los aliases se agregan al `.zshrc`:

| Alias | Acción |
|---|---|
| `dash-build` | Configura CMake y compila todos los targets |
| `dash-editor` | Compila y abre el **editor DashEngine** |
| `dash-game` | Compila y ejecuta el runtime standalone |
| `dash-update` | `git pull` + recompila |
| `dash-clean` | Elimina `build/` para recompilar desde cero |

```bash
# Activar aliases
source ~/.zshrc

dash-build          # compilar
dash-editor         # editor
dash-clean && dash-build  # build limpio
```

---

## Ejecutables generados

| Archivo | Descripción |
|---|---|
| `build\src\editor\Release\DashEngine.exe` | Editor de niveles (Dear ImGui, estilo Unreal) |
| `build\src\game\Release\IsometricRPG.exe` | Runtime standalone del juego |

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
