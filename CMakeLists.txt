cmake_minimum_required(VERSION 3.16)
project(IsometricRPG CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ── SDL2 ─────────────────────────────────────────────────────────────────────
find_package(SDL2 REQUIRED)

# ── Dear ImGui (downloaded on first build) ───────────────────────────────────
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        origin/docking
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdlrenderer2.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(imgui_lib PUBLIC ${SDL2_LIBRARIES})

# ── nlohmann/json (header-only, downloaded on first build) ────────────────────
FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES ""
)
FetchContent_MakeAvailable(json)

# ── Core game library (shared between game & editor) ─────────────────────────
file(GLOB CORE_SOURCES
    "src/core/*.cpp"
    "src/core/profiling/*.cpp"
    "src/core/components/*.cpp"
    "src/entities/*.cpp"
    "src/world/*.cpp"
    "src/game/nav/*.cpp"
)
add_library(game_core STATIC ${CORE_SOURCES})
target_include_directories(game_core PUBLIC
    src
    src/core
    src/core/events
    src/core/components
    src/core/profiling
    src/entities
    src/world
    src/rendering
    src/game/data
    src/game/nav
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(game_core PUBLIC ${SDL2_LIBRARIES} nlohmann_json::nlohmann_json)

# ── Game executable ──────────────────────────────────────────────────────────
file(GLOB RUNTIME_SOURCES "src/game/runtime/*.cpp")
file(GLOB SYSTEM_SOURCES  "src/game/systems/*.cpp")
file(GLOB DATA_SOURCES    "src/game/data/*.cpp")
file(GLOB SAVE_SOURCES    "src/game/save/*.cpp")
file(GLOB NAV_SOURCES     "src/game/nav/*.cpp")
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/game/Game.cpp
    ${RUNTIME_SOURCES}
    ${SYSTEM_SOURCES}
    ${DATA_SOURCES}
    ${SAVE_SOURCES}
)
target_include_directories(${PROJECT_NAME} PRIVATE
    src src/core src/entities src/world src/rendering src/game
    src/game/runtime src/game/systems src/game/data src/game/nav
    src/game/save
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(${PROJECT_NAME} PRIVATE game_core nlohmann_json::nlohmann_json ${SDL2_LIBRARIES})

# ── Editor executable (DashEngine) ───────────────────────────────────────────
file(GLOB EDITOR_SOURCES "src/editor/*.cpp")
file(GLOB EDITOR_CMD_SOURCES "src/editor/commands/*.cpp")
file(GLOB EDITOR_PLAYMODE_SOURCES "src/editor/playmode/*.cpp")
file(GLOB EDITOR_VALIDATION_SOURCES "src/editor/validation/*.cpp")
file(GLOB ASSET_SOURCES "src/assets/*.cpp")
file(GLOB IMPORTER_SOURCES "src/assets/importers/*.cpp")
file(GLOB PANEL_SOURCES "src/editor/panels/*.cpp")
add_executable(DashEngine
    ${EDITOR_SOURCES} ${EDITOR_CMD_SOURCES} ${EDITOR_PLAYMODE_SOURCES}
    ${EDITOR_VALIDATION_SOURCES}
    ${ASSET_SOURCES} ${IMPORTER_SOURCES} ${PANEL_SOURCES}
    src/game/Game.cpp
    ${RUNTIME_SOURCES} ${SYSTEM_SOURCES} ${DATA_SOURCES} ${SAVE_SOURCES}
)
target_include_directories(DashEngine PRIVATE
    src src/core src/core/components src/entities src/world src/rendering src/editor
    src/editor/commands src/editor/panels src/editor/playmode src/editor/validation
    src/assets src/assets/importers
    src/game src/game/runtime src/game/systems src/game/data src/game/save src/game/nav
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(DashEngine PRIVATE
    game_core
    imgui_lib
    nlohmann_json::nlohmann_json
)
target_compile_definitions(DashEngine PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
    BUILD_DIR="${CMAKE_BINARY_DIR}"
)

# ── macOS: Cocoa framework ───────────────────────────────────────────────────
if(APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE "-framework Cocoa")
    target_link_libraries(DashEngine PRIVATE "-framework Cocoa")
endif()

# ── Compile flags ─────────────────────────────────────────────────────────────
target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -O2)
target_compile_options(DashEngine PRIVATE -Wall -Wextra -O2)
target_compile_options(game_core PRIVATE -Wall -Wextra -O2)

# ── Tests (opt-in via -DBUILD_TESTING=ON) ─────────────────────────────────────
option(BUILD_TESTING "Build unit tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()