cmake_minimum_required(VERSION 3.16)
project(IsometricRPG VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
option(ENABLE_ISOLATION_CHECK "Enable game runtime isolation checks" OFF)

# ── Cross-platform compile flags ──────────────────────────────────────────────
if(MSVC)
    set(DASH_COMPILE_FLAGS /W4 /O2 /wd4996)
else()
    set(DASH_COMPILE_FLAGS -Wall -Wextra -O2)
endif()

include(FetchContent)

# ── SDL2 ─────────────────────────────────────────────────────────────────────
find_package(SDL2 REQUIRED)
# SQLite3: prefer vcpkg unofficial target, fall back to CMake's FindSQLite3
find_package(unofficial-sqlite3 CONFIG QUIET)
if(TARGET unofficial::sqlite3::sqlite3)
    if(NOT TARGET SQLite3::SQLite3)
        add_library(SQLite3::SQLite3 ALIAS unofficial::sqlite3::sqlite3)
    endif()
else()
    find_package(SQLite3 REQUIRED)
endif()
find_package(Vulkan QUIET)
find_package(glfw3 QUIET)
find_package(assimp CONFIG QUIET)
# Fix Homebrew assimp linking against wrong SDK path for zlib
if(TARGET assimp::assimp AND APPLE)
    get_target_property(_assimp_libs assimp::assimp INTERFACE_LINK_LIBRARIES)
    if(_assimp_libs)
        string(REGEX REPLACE "/Library/Developer/CommandLineTools/SDKs/[^/]+/usr/lib/libz\\.tbd" "-lz" _assimp_libs "${_assimp_libs}")
        set_target_properties(assimp::assimp PROPERTIES INTERFACE_LINK_LIBRARIES "${_assimp_libs}")
    endif()
endif()

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
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(imgui_lib PUBLIC ${SDL2_LIBRARIES} Vulkan::Vulkan)

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

# ── miniaudio (single-header audio library) ──────────────────────────────────
set(MINIAUDIO_DIR "${CMAKE_SOURCE_DIR}/src/game/audio")
if(NOT EXISTS "${MINIAUDIO_DIR}/miniaudio.h")
    message(STATUS "[miniaudio] Downloading miniaudio.h...")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/mackron/miniaudio/0.11.21/miniaudio.h"
        "${MINIAUDIO_DIR}/miniaudio.h"
        STATUS MINIAUDIO_DL_STATUS)
    list(GET MINIAUDIO_DL_STATUS 0 MINIAUDIO_DL_CODE)
    if(NOT MINIAUDIO_DL_CODE EQUAL 0)
        message(WARNING "[miniaudio] download failed — audio will be unavailable")
        file(REMOVE "${MINIAUDIO_DIR}/miniaudio.h")
    endif()
endif()

# ── Core game library (shared between game & editor) ─────────────────────────
file(GLOB CORE_SOURCES
    "src/core/*.cpp"
    "src/core/db/*.cpp"
    "src/core/profiling/*.cpp"
    "src/core/components/*.cpp"
    "src/core/input/*.cpp"
    "src/entities/*.cpp"
    "src/world/*.cpp"
    "src/game/nav/*.cpp"
    "src/game/physics/*.cpp"
)
add_library(game_core STATIC ${CORE_SOURCES})
target_include_directories(game_core PUBLIC
    src
    src/core
    src/core/events
    src/core/components
    src/core/profiling
    src/core/input
    src/entities
    src/world
    src/rendering
    src/game/data
    src/game/nav
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(game_core PUBLIC ${SDL2_LIBRARIES} nlohmann_json::nlohmann_json)
target_link_libraries(game_core PUBLIC SQLite3::SQLite3)
if(TARGET assimp::assimp)
    target_link_libraries(game_core PUBLIC assimp::assimp)
endif()

add_subdirectory(src/game)
# The editor is a desktop-only tool; skip it when cross-compiling for iOS.
if(NOT IOS)
    add_subdirectory(src/editor)
endif()

option(ENABLE_VULKAN_EXPERIMENTAL "Build experimental Vulkan bootstrap (D70-D74)" ON)

if(ENABLE_VULKAN_EXPERIMENTAL AND NOT IOS)
    set(GLFW_VK_TARGET "")
    if(TARGET glfw)
        set(GLFW_VK_TARGET glfw)
    elseif(TARGET glfw3::glfw)
        set(GLFW_VK_TARGET glfw3::glfw)
    endif()

    if(Vulkan_FOUND AND NOT "${GLFW_VK_TARGET}" STREQUAL "")
        set(VULKAN_EXPERIMENTAL_SOURCES
            ${CMAKE_SOURCE_DIR}/src/rendering/platform/WindowContext.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/mesh/MeshBuffers.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/mesh/MeshUploader.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/textures/TextureLoader.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/textures/stb_image_vk_impl.cpp
            ${CMAKE_SOURCE_DIR}/src/assets/cache/AssetCache3D.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/VulkanDiagnostics.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/DeviceContext.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/CameraController.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/EditorBridge.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/PlayerController.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/SwapchainContext.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/FrameGraphLite.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/Renderer.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/PipelineBuilder.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/SceneLoader.cpp
        )

        add_library(vulkan_experimental STATIC ${VULKAN_EXPERIMENTAL_SOURCES})
        target_include_directories(vulkan_experimental PUBLIC
            ${CMAKE_SOURCE_DIR}/src
        )
        target_link_libraries(vulkan_experimental PUBLIC Vulkan::Vulkan ${GLFW_VK_TARGET} game_core)
        target_compile_options(vulkan_experimental PRIVATE ${DASH_COMPILE_FLAGS})

        set(VULKAN_SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/assets/shaders")
        set(VULKAN_SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
        file(MAKE_DIRECTORY "${VULKAN_SHADER_BIN_DIR}")

        target_compile_definitions(vulkan_experimental PUBLIC
            VULKAN_SHADER_DIR="${VULKAN_SHADER_BIN_DIR}"
            VULKAN_MODEL_DIR="${CMAKE_SOURCE_DIR}/assets/models/gltf"
        )

        find_program(GLSLC_EXECUTABLE glslc)
        if(GLSLC_EXECUTABLE)
            add_custom_command(
                OUTPUT
                    "${VULKAN_SHADER_BIN_DIR}/basic.vert.spv"
                    "${VULKAN_SHADER_BIN_DIR}/basic.frag.spv"
                    "${VULKAN_SHADER_BIN_DIR}/textured.vert.spv"
                    "${VULKAN_SHADER_BIN_DIR}/textured.frag.spv"
                    "${VULKAN_SHADER_BIN_DIR}/terrain.vert.spv"
                    "${VULKAN_SHADER_BIN_DIR}/terrain.frag.spv"
                    "${VULKAN_SHADER_BIN_DIR}/water.vert.spv"
                    "${VULKAN_SHADER_BIN_DIR}/water.frag.spv"
                COMMAND ${GLSLC_EXECUTABLE} "${VULKAN_SHADER_SRC_DIR}/basic.vert" -o "${VULKAN_SHADER_BIN_DIR}/basic.vert.spv"
                COMMAND ${GLSLC_EXECUTABLE} "${VULKAN_SHADER_SRC_DIR}/basic.frag" -o "${VULKAN_SHADER_BIN_DIR}/basic.frag.spv"
                COMMAND ${GLSLC_EXECUTABLE} "${VULKAN_SHADER_SRC_DIR}/textured.vert" -o "${VULKAN_SHADER_BIN_DIR}/textured.vert.spv"
                COMMAND ${GLSLC_EXECUTABLE} "${VULKAN_SHADER_SRC_DIR}/textured.frag" -o "${VULKAN_SHADER_BIN_DIR}/textured.frag.spv"
                COMMAND ${GLSLC_EXECUTABLE} "${VULKAN_SHADER_SRC_DIR}/terrain.vert" -o "${VULKAN_SHADER_BIN_DIR}/terrain.vert.spv"
                COMMAND ${GLSLC_EXECUTABLE} "${VULKAN_SHADER_SRC_DIR}/terrain.frag" -o "${VULKAN_SHADER_BIN_DIR}/terrain.frag.spv"
                COMMAND ${GLSLC_EXECUTABLE} "${VULKAN_SHADER_SRC_DIR}/water.vert" -o "${VULKAN_SHADER_BIN_DIR}/water.vert.spv"
                COMMAND ${GLSLC_EXECUTABLE} "${VULKAN_SHADER_SRC_DIR}/water.frag" -o "${VULKAN_SHADER_BIN_DIR}/water.frag.spv"
                DEPENDS
                    "${VULKAN_SHADER_SRC_DIR}/basic.vert"
                    "${VULKAN_SHADER_SRC_DIR}/basic.frag"
                    "${VULKAN_SHADER_SRC_DIR}/textured.vert"
                    "${VULKAN_SHADER_SRC_DIR}/textured.frag"
                    "${VULKAN_SHADER_SRC_DIR}/terrain.vert"
                    "${VULKAN_SHADER_SRC_DIR}/terrain.frag"
                    "${VULKAN_SHADER_SRC_DIR}/water.vert"
                    "${VULKAN_SHADER_SRC_DIR}/water.frag"
                COMMENT "Compiling Vulkan shaders (GLSL -> SPIR-V)"
                VERBATIM
            )
            add_custom_target(vulkan_shaders ALL
                DEPENDS
                    "${VULKAN_SHADER_BIN_DIR}/basic.vert.spv"
                    "${VULKAN_SHADER_BIN_DIR}/basic.frag.spv"
                    "${VULKAN_SHADER_BIN_DIR}/textured.vert.spv"
                    "${VULKAN_SHADER_BIN_DIR}/textured.frag.spv"
                    "${VULKAN_SHADER_BIN_DIR}/terrain.vert.spv"
                    "${VULKAN_SHADER_BIN_DIR}/terrain.frag.spv"
                    "${VULKAN_SHADER_BIN_DIR}/water.vert.spv"
                    "${VULKAN_SHADER_BIN_DIR}/water.frag.spv"
            )
        else()
            message(WARNING "glslc not found. VulkanBootstrap will build, but pipeline creation (D74) will fail at runtime.")
        endif()

        add_executable(VulkanBootstrap
            ${CMAKE_SOURCE_DIR}/src/tools/vulkan_bootstrap_main.cpp
        )
        target_link_libraries(VulkanBootstrap PRIVATE vulkan_experimental)
        target_compile_options(VulkanBootstrap PRIVATE ${DASH_COMPILE_FLAGS})

        if(TARGET vulkan_shaders)
            add_dependencies(VulkanBootstrap vulkan_shaders)
        endif()

        if(APPLE)
            target_link_libraries(VulkanBootstrap PRIVATE "-framework Cocoa")
        endif()
    else()
        message(WARNING "ENABLE_VULKAN_EXPERIMENTAL is ON but Vulkan or glfw was not found. Skipping VulkanBootstrap target.")
    endif()
endif()

# ── Compile flags ─────────────────────────────────────────────────────────────
target_compile_options(game_core PRIVATE ${DASH_COMPILE_FLAGS})

# ── Tests (opt-in via -DBUILD_TESTING=ON) ─────────────────────────────────────
option(BUILD_TESTING "Build unit tests" OFF)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Packaging (install rules, CPack, VersionInfo) ─────────────────────────────
include(cmake/Packaging.cmake)