cmake_minimum_required(VERSION 3.16)
project(IsometricRPG VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(ENABLE_ISOLATION_CHECK "Enable game runtime isolation checks" OFF)

include(FetchContent)

# ── SDL2 ─────────────────────────────────────────────────────────────────────
find_package(SDL2 REQUIRED)
find_package(SQLite3 REQUIRED)

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

# ── Font Awesome 6 — icon font for the editor UI ─────────────────────────────
# Downloads only the two files we need (TTF + C++ header); no full repo clone.
set(FA_VERSION "6.5.2")
set(FA_TTF_PATH    "${CMAKE_SOURCE_DIR}/assets/fonts/fa-solid-900.ttf")
set(FA_HEADER_PATH "${CMAKE_SOURCE_DIR}/src/editor/IconsFontAwesome6.h")

file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/assets/fonts")

if(NOT EXISTS "${FA_TTF_PATH}")
    message(STATUS "[FA] Downloading fa-solid-900.ttf (Font Awesome ${FA_VERSION})...")
    file(DOWNLOAD
        "https://github.com/FortAwesome/Font-Awesome/raw/${FA_VERSION}/webfonts/fa-solid-900.ttf"
        "${FA_TTF_PATH}"
        SHOW_PROGRESS
        STATUS _fa_status
    )
    list(GET _fa_status 0 _fa_code)
    if(NOT _fa_code EQUAL 0)
        message(WARNING "[FA] TTF download failed — icons will not appear.")
        file(REMOVE "${FA_TTF_PATH}")
    endif()
endif()

if(NOT EXISTS "${FA_HEADER_PATH}")
    message(STATUS "[FA] Downloading IconsFontAwesome6.h...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/juliettef/IconFontCppHeaders/main/IconsFontAwesome6.h"
        "${FA_HEADER_PATH}"
        SHOW_PROGRESS
        STATUS _fah_status
    )
    list(GET _fah_status 0 _fah_code)
    if(NOT _fah_code EQUAL 0)
        message(WARNING "[FA] Header download failed — icons will not appear.")
        file(REMOVE "${FA_HEADER_PATH}")
    endif()
endif()

# ── stb (header-only image read/write for the sprite editor) ─────────────────
set(STB_WRITE_PATH "${CMAKE_SOURCE_DIR}/src/editor/stb_image_write.h")
set(STB_READ_PATH  "${CMAKE_SOURCE_DIR}/src/editor/stb_image.h")

if(NOT EXISTS "${STB_WRITE_PATH}")
    message(STATUS "[stb] Downloading stb_image_write.h...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h"
        "${STB_WRITE_PATH}"
        SHOW_PROGRESS STATUS _stbw_status)
    list(GET _stbw_status 0 _stbw_code)
    if(NOT _stbw_code EQUAL 0)
        message(WARNING "[stb] stb_image_write.h download failed.")
        file(REMOVE "${STB_WRITE_PATH}")
    endif()
endif()

if(NOT EXISTS "${STB_READ_PATH}")
    message(STATUS "[stb] Downloading stb_image.h...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/nothings/stb/master/stb_image.h"
        "${STB_READ_PATH}"
        SHOW_PROGRESS STATUS _stbr_status)
    list(GET _stbr_status 0 _stbr_code)
    if(NOT _stbr_code EQUAL 0)
        message(WARNING "[stb] stb_image.h download failed.")
        file(REMOVE "${STB_READ_PATH}")
    endif()
endif()

# ── Core game library (shared between game & editor) ─────────────────────────
file(GLOB CORE_SOURCES
    "src/core/*.cpp"
    "src/core/db/*.cpp"
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
target_link_libraries(game_core PUBLIC SQLite3::SQLite3)

add_subdirectory(src/game)
add_subdirectory(src/editor)

# ── Compile flags ─────────────────────────────────────────────────────────────
target_compile_options(game_core PRIVATE -Wall -Wextra -O2)

# ── Tests (opt-in via -DBUILD_TESTING=ON) ─────────────────────────────────────
option(BUILD_TESTING "Build unit tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Packaging (install rules, CPack, VersionInfo) ─────────────────────────────
include(cmake/Packaging.cmake)