# ── Test suite ────────────────────────────────────────────────────────────────
# Lightweight tests using a minimal assertion framework (no external deps).
# Run with: cd build && ctest --output-on-failure
# ─────────────────────────────────────────────────────────────────────────────

# Helper: add a test executable linked to common libs
function(dash_add_test TEST_NAME TEST_SRC)
    add_executable(${TEST_NAME} ${TEST_SRC})
    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/entities
        ${CMAKE_SOURCE_DIR}/src/world
        ${CMAKE_SOURCE_DIR}/src/rendering
        ${CMAKE_SOURCE_DIR}/src/editor
        ${CMAKE_SOURCE_DIR}/src/editor/commands
        ${CMAKE_SOURCE_DIR}/src/game
        ${CMAKE_SOURCE_DIR}/src/game/runtime
        ${CMAKE_SOURCE_DIR}/src/game/systems
        ${CMAKE_SOURCE_DIR}/src/game/data
        ${CMAKE_SOURCE_DIR}/src/game/nav
        ${CMAKE_SOURCE_DIR}/src/game/save
        ${SDL2_INCLUDE_DIRS}
    )
    target_link_libraries(${TEST_NAME} PRIVATE
        game_core
        nlohmann_json::nlohmann_json
        ${SDL2_LIBRARIES}
    )
    target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -O2)
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# ── Scene serialization needs editor sources ──────────────────────────────────
file(GLOB TEST_CMD_SOURCES "${CMAKE_SOURCE_DIR}/src/editor/commands/*.cpp")

add_executable(test_scene_serialization
    test_scene_serialization.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/SceneData.cpp
)
target_include_directories(test_scene_serialization PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/entities
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${CMAKE_SOURCE_DIR}/src/editor
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_scene_serialization PRIVATE
    game_core nlohmann_json::nlohmann_json ${SDL2_LIBRARIES}
)
target_compile_options(test_scene_serialization PRIVATE -Wall -Wextra -O2)
add_test(NAME test_scene_serialization COMMAND test_scene_serialization)

# ── Undo/redo needs editor command sources ────────────────────────────────────
add_executable(test_undo_redo_commands
    test_undo_redo_commands.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/SceneData.cpp
    ${TEST_CMD_SOURCES}
)
target_include_directories(test_undo_redo_commands PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/entities
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/editor/commands
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_undo_redo_commands PRIVATE
    game_core nlohmann_json::nlohmann_json ${SDL2_LIBRARIES}
)
target_compile_options(test_undo_redo_commands PRIVATE -Wall -Wextra -O2)
add_test(NAME test_undo_redo_commands COMMAND test_undo_redo_commands)

# ── Component serialization (D23) ────────────────────────────────────────────
dash_add_test(test_component_serialization test_component_serialization.cpp)

# ── EntityRegistry + scene migration (D24) ────────────────────────────────────
add_executable(test_entity_registry
    test_entity_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/SceneData.cpp
)
target_include_directories(test_entity_registry PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/components
    ${CMAKE_SOURCE_DIR}/src/entities
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${CMAKE_SOURCE_DIR}/src/editor
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_entity_registry PRIVATE
    game_core nlohmann_json::nlohmann_json ${SDL2_LIBRARIES}
)
target_compile_options(test_entity_registry PRIVATE -Wall -Wextra -O2)
add_test(NAME test_entity_registry COMMAND test_entity_registry)

# ── MoveEntity and EditProperty commands ─────────────────────────────────────
add_executable(test_move_edit_commands
    test_move_edit_commands.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/SceneData.cpp
    ${TEST_CMD_SOURCES}
)
target_include_directories(test_move_edit_commands PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/entities
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/editor/commands
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_move_edit_commands PRIVATE
    game_core nlohmann_json::nlohmann_json ${SDL2_LIBRARIES}
)
target_compile_options(test_move_edit_commands PRIVATE -Wall -Wextra -O2)
add_test(NAME test_move_edit_commands COMMAND test_move_edit_commands)

# ── World seed determinism ────────────────────────────────────────────────────
dash_add_test(test_world_seed_determinism test_world_seed_determinism.cpp)

# ── Pathfinding ───────────────────────────────────────────────────────────────
dash_add_test(test_pathfinding test_pathfinding.cpp)

# ── Prefab system (D26) ───────────────────────────────────────────────────────
add_executable(test_prefab_system
    test_prefab_system.cpp
    ${CMAKE_SOURCE_DIR}/src/core/components/PrefabAsset.cpp
)
target_include_directories(test_prefab_system PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/components
    ${CMAKE_SOURCE_DIR}/src/entities
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${CMAKE_SOURCE_DIR}/src/editor
    ${SDL2_INCLUDE_DIRS}
)
target_compile_definitions(test_prefab_system PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)
target_link_libraries(test_prefab_system PRIVATE
    game_core nlohmann_json::nlohmann_json ${SDL2_LIBRARIES}
)
target_compile_options(test_prefab_system PRIVATE -Wall -Wextra -O2)
add_test(NAME test_prefab_system COMMAND test_prefab_system)
