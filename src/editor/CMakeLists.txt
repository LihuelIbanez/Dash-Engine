# ── Editor executable (DashEngine) ───────────────────────────────────────────

file(GLOB EDITOR_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB EDITOR_CMD_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/commands/*.cpp")
file(GLOB EDITOR_PLAYMODE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/playmode/*.cpp")
file(GLOB EDITOR_VALIDATION_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/validation/*.cpp")
file(GLOB EDITOR_PROJECT_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/project/*.cpp")
file(GLOB EDITOR_SCENE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/scene/*.cpp")
file(GLOB ASSET_SOURCES "${CMAKE_SOURCE_DIR}/src/assets/*.cpp")
file(GLOB IMPORTER_SOURCES "${CMAKE_SOURCE_DIR}/src/assets/importers/*.cpp")
file(GLOB PANEL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/panels/*.cpp")

set(EDITOR_PLATFORM_SOURCES)
if(APPLE)
    list(APPEND EDITOR_PLATFORM_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/NativeFileDialogs_mac.mm"
    )
else()
    list(APPEND EDITOR_PLATFORM_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/NativeFileDialogs_stub.cpp"
    )
endif()

add_executable(DashEngine
    ${EDITOR_SOURCES}
    ${EDITOR_CMD_SOURCES}
    ${EDITOR_PLAYMODE_SOURCES}
    ${EDITOR_VALIDATION_SOURCES}
    ${EDITOR_PROJECT_SOURCES}
    ${EDITOR_SCENE_SOURCES}
    ${ASSET_SOURCES}
    ${IMPORTER_SOURCES}
    ${PANEL_SOURCES}
    ${EDITOR_PLATFORM_SOURCES}
)

target_include_directories(DashEngine PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/components
    ${CMAKE_SOURCE_DIR}/src/entities
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/editor/commands
    ${CMAKE_SOURCE_DIR}/src/editor/panels
    ${CMAKE_SOURCE_DIR}/src/editor/playmode
    ${CMAKE_SOURCE_DIR}/src/editor/validation
    ${CMAKE_SOURCE_DIR}/src/editor/project
    ${CMAKE_SOURCE_DIR}/src/editor/scene
    ${CMAKE_SOURCE_DIR}/src/assets
    ${CMAKE_SOURCE_DIR}/src/assets/importers
    ${CMAKE_SOURCE_DIR}/src/game
    ${CMAKE_SOURCE_DIR}/src/game/runtime
    ${CMAKE_SOURCE_DIR}/src/game/systems
    ${CMAKE_SOURCE_DIR}/src/game/data
    ${CMAKE_SOURCE_DIR}/src/game/save
    ${CMAKE_SOURCE_DIR}/src/game/nav
    ${CMAKE_BINARY_DIR}/generated
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(DashEngine PRIVATE
    game_runtime
    game_core
    imgui_lib
    nlohmann_json::nlohmann_json
)

target_compile_definitions(DashEngine PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
    BUILD_DIR="${CMAKE_BINARY_DIR}"
    DASH_VERSION="${PROJECT_VERSION}"
)

if(APPLE)
    target_link_libraries(DashEngine PRIVATE "-framework Cocoa" "-framework UniformTypeIdentifiers")
endif()

target_compile_options(DashEngine PRIVATE -Wall -Wextra -O2)
