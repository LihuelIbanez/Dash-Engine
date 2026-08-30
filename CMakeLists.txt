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
# Optional: the terrain photo sets ship their normal/roughness maps as DWAA
# OpenEXR, a lossy DCT codec no single-header decoder implements. Without this
# the terrain falls back to displacement-derived normals and scalar roughness.
find_package(OpenEXR CONFIG QUIET)
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
set(STB_READ_PATH  "${CMAKE_SOURCE_DIR}/src/game/rendering/stb_image.h")

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
    "src/scene/*.cpp"
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
    src/scene
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
            ${CMAKE_SOURCE_DIR}/src/rendering/animation/AnimationPlayer.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/textures/TextureLoader.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/textures/TerrainTextureArray.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/textures/ExrImage.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/textures/stb_image_vk_impl.cpp
            ${CMAKE_SOURCE_DIR}/src/assets/cache/AssetCache3D.cpp
            ${CMAKE_SOURCE_DIR}/src/assets/MaterialAsset.cpp
            ${CMAKE_SOURCE_DIR}/src/assets/AssetDatabase.cpp
            ${CMAKE_SOURCE_DIR}/src/assets/AssetRepositorySqlite.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/VulkanDiagnostics.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/DeviceContext.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/CameraController.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/EditorBridge.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/PlayerController.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/SwapchainContext.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/FrameGraphLite.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/HdrTarget.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/ParticleRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/Renderer.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/SceneRenderer.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/ShadowMap.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/SsaoPass.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/PipelineBuilder.cpp
            ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/SceneLoader.cpp
            ${CMAKE_SOURCE_DIR}/src/game/runtime3d/EnemySimulation3D.cpp
        )

        add_library(vulkan_experimental STATIC ${VULKAN_EXPERIMENTAL_SOURCES})
        target_include_directories(vulkan_experimental PUBLIC
            ${CMAKE_SOURCE_DIR}/src
        )
        target_link_libraries(vulkan_experimental PUBLIC Vulkan::Vulkan ${GLFW_VK_TARGET} game_core)
        target_compile_options(vulkan_experimental PRIVATE ${DASH_COMPILE_FLAGS})

        if(TARGET OpenEXR::OpenEXR)
            target_link_libraries(vulkan_experimental PRIVATE OpenEXR::OpenEXR)
            target_compile_definitions(vulkan_experimental PRIVATE DASH_HAVE_OPENEXR=1)
            message(STATUS "OpenEXR found: terrain normal/roughness maps will be decoded from EXR")
        else()
            message(STATUS "OpenEXR not found: terrain falls back to displacement-derived normals")
        endif()

        set(VULKAN_SHADER_SRC_DIR "${CMAKE_SOURCE_DIR}/assets/shaders")
        set(VULKAN_SHADER_BIN_DIR "${CMAKE_BINARY_DIR}/generated/shaders")
        file(MAKE_DIRECTORY "${VULKAN_SHADER_BIN_DIR}")

        target_compile_definitions(vulkan_experimental PUBLIC
            VULKAN_SHADER_DIR="${VULKAN_SHADER_BIN_DIR}"
            VULKAN_MODEL_DIR="${CMAKE_SOURCE_DIR}/assets/models/gltf"
            VULKAN_ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
        )

        find_program(GLSLC_EXECUTABLE glslc)
        if(GLSLC_EXECUTABLE)
            set(VULKAN_SHADER_STAGES
                basic.vert basic.frag
                textured.vert textured.frag
                terrain.vert terrain.frag
                water.vert water.frag
                billboard.vert billboard.frag
                particle.vert particle.frag
                skinned.vert skinned.frag
                shadow_depth.vert shadow_depth_skinned.vert
                shadow_billboard.vert shadow_billboard.frag
                ssao.frag ssao_blur.frag
                tonemap.vert tonemap.frag)

            # Fragment shaders compiled twice: the plain variant targets descriptor
            # layouts without the scene-light binding (editor viewport), the "_lit"
            # variant adds set 0 binding 2 and consumes assets/shaders/scene_lights.glsl.
            set(VULKAN_SHADER_LIT_VARIANTS basic.frag textured.frag skinned.frag)

            # Third variant: "_lit" plus the shadow map at set 0 binding 3
            # (assets/shaders/shadow_sample.glsl). Only the runtime descriptor
            # layout declares that binding, so the editor keeps using "_lit".
            set(VULKAN_SHADER_SHADOW_VARIANTS basic.frag textured.frag skinned.frag terrain.frag)

            set(VULKAN_SHADER_OUTPUTS "")
            foreach(shader IN LISTS VULKAN_SHADER_STAGES)
                add_custom_command(
                    OUTPUT "${VULKAN_SHADER_BIN_DIR}/${shader}.spv"
                    COMMAND ${GLSLC_EXECUTABLE}
                            "-I${VULKAN_SHADER_SRC_DIR}"
                            "${VULKAN_SHADER_SRC_DIR}/${shader}"
                            -o "${VULKAN_SHADER_BIN_DIR}/${shader}.spv"
                    DEPENDS "${VULKAN_SHADER_SRC_DIR}/${shader}"
                    COMMENT "Compiling Vulkan shader ${shader} (GLSL -> SPIR-V)"
                    VERBATIM)
                list(APPEND VULKAN_SHADER_OUTPUTS "${VULKAN_SHADER_BIN_DIR}/${shader}.spv")
            endforeach()

            foreach(shader IN LISTS VULKAN_SHADER_LIT_VARIANTS)
                string(REPLACE "." "_lit." lit_name "${shader}")
                add_custom_command(
                    OUTPUT "${VULKAN_SHADER_BIN_DIR}/${lit_name}.spv"
                    COMMAND ${GLSLC_EXECUTABLE}
                            -DDASH_SCENE_LIGHTS
                            "-I${VULKAN_SHADER_SRC_DIR}"
                            "${VULKAN_SHADER_SRC_DIR}/${shader}"
                            -o "${VULKAN_SHADER_BIN_DIR}/${lit_name}.spv"
                    DEPENDS "${VULKAN_SHADER_SRC_DIR}/${shader}"
                            "${VULKAN_SHADER_SRC_DIR}/scene_lights.glsl"
                            "${VULKAN_SHADER_SRC_DIR}/ssao_sample.glsl"
                    COMMENT "Compiling Vulkan shader ${lit_name} (GLSL -> SPIR-V)"
                    VERBATIM)
                list(APPEND VULKAN_SHADER_OUTPUTS "${VULKAN_SHADER_BIN_DIR}/${lit_name}.spv")
            endforeach()

            foreach(shader IN LISTS VULKAN_SHADER_SHADOW_VARIANTS)
                string(REPLACE "." "_shadow." shadow_name "${shader}")
                add_custom_command(
                    OUTPUT "${VULKAN_SHADER_BIN_DIR}/${shadow_name}.spv"
                    COMMAND ${GLSLC_EXECUTABLE}
                            -DDASH_SCENE_LIGHTS
                            -DDASH_SHADOWS
                            "-I${VULKAN_SHADER_SRC_DIR}"
                            "${VULKAN_SHADER_SRC_DIR}/${shader}"
                            -o "${VULKAN_SHADER_BIN_DIR}/${shadow_name}.spv"
                    DEPENDS "${VULKAN_SHADER_SRC_DIR}/${shader}"
                            "${VULKAN_SHADER_SRC_DIR}/scene_lights.glsl"
                            "${VULKAN_SHADER_SRC_DIR}/shadow_sample.glsl"
                            "${VULKAN_SHADER_SRC_DIR}/ssao_sample.glsl"
                    COMMENT "Compiling Vulkan shader ${shadow_name} (GLSL -> SPIR-V)"
                    VERBATIM)
                list(APPEND VULKAN_SHADER_OUTPUTS "${VULKAN_SHADER_BIN_DIR}/${shadow_name}.spv")
            endforeach()

            add_custom_target(vulkan_shaders ALL DEPENDS ${VULKAN_SHADER_OUTPUTS})
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