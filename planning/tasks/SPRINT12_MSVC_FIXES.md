# Sprint 12 — MSVC Compatibility Fixes

## Overview

This document tracks the Windows/MSVC compatibility measures applied to the Dash-Engine codebase.

## Compiler Flags

**CMakeLists.txt (line 9-13):**
```cmake
if(MSVC)
    set(DASH_COMPILE_FLAGS /W4 /O2 /wd4996)
else()
    set(DASH_COMPILE_FLAGS -Wall -Wextra -O2)
endif()
```

- `/W4` — Warning level 4 (equivalent to `-Wall -Wextra`)
- `/O2` — Full optimization
- `/wd4996` — Suppress deprecation warnings for POSIX functions (e.g., `fopen`, `getenv`)

## Platform Guards

### AppPaths.h (`_WIN32` guards)
- Uses `%APPDATA%` for user data on Windows vs `~/Library/Application Support/` on macOS
- `std::filesystem` for path normalization (available in MSVC 2017 15.7+)
- Resource probing checks CWD and parent directory for `assets/asset_db.json`

### CMakeLists.txt
- `if(APPLE)` block for Cocoa framework linking on macOS
- `if(MSVC)` for compiler flag selection
- Homebrew assimp libz fix scoped to `if(APPLE)` — won't affect Windows builds

## Dependency Resolution (vcpkg)

Windows builds use vcpkg for all dependencies. The `vcpkg.json` manifest:

| Package | Purpose | MSVC Notes |
|---------|---------|------------|
| `sdl2` | Window/input for 2D editor | Links `SDL2.lib` + `SDL2main.lib` |
| `sqlite3` | Database persistence | Static link via vcpkg |
| `glfw3` | Window/input for Vulkan 3D | `glfw3.lib` |
| `assimp` | 3D model import (.obj/.gltf) | `assimp-vc14x-mt.lib` (MSVC) |

Vulkan SDK must be installed separately (LunarG SDK) and accessible via `find_package(Vulkan)`.

## Known MSVC Considerations

1. **std::filesystem**: Requires C++17 mode (`/std:c++17`), set via `CMAKE_CXX_STANDARD 17`
2. **POSIX functions**: `fopen`, `getenv`, `sprintf` trigger C4996 warnings — suppressed with `/wd4996`
3. **SDL2 entry point**: MSVC may need `SDL2main` linked before `SDL2` — handled by CMake's `find_package(SDL2)`
4. **Vulkan headers**: Compatible with MSVC 2017+; Vulkan SDK provides `vulkan-1.lib`
5. **glslc shader compiler**: Included in Vulkan SDK on both platforms — same SPIR-V output
6. **Objective-C (.mm files)**: `NativeFileDialogs_mac.mm` excluded on Windows builds (editor target is macOS-only for native dialogs)

## Build Scripts

- **macOS**: `dash.sh` (CLI wrapper) + `install_app.sh` (packaging)
- **Windows**: `build_windows.ps1` (configure + build) + `dash.ps1` (CLI wrapper) + `packaging/windows/install_app.bat` (packaging)

## Testing

All tests are platform-independent (no macOS-specific APIs in test code). Tests link against `game_core` which is pure C++17 with SQLite3 and nlohmann/json — both cross-platform.
