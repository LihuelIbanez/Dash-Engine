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
    target_compile_options(${TEST_NAME} PRIVATE ${DASH_COMPILE_FLAGS})
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# ── Scene serialization needs editor sources ──────────────────────────────────
file(GLOB TEST_CMD_SOURCES "${CMAKE_SOURCE_DIR}/src/editor/commands/*.cpp")

add_executable(test_scene_serialization
    test_scene_serialization.cpp
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
target_compile_options(test_scene_serialization PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_scene_serialization COMMAND test_scene_serialization)

# ── Undo/redo needs editor command sources ────────────────────────────────────
add_executable(test_undo_redo_commands
    test_undo_redo_commands.cpp
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
target_compile_options(test_undo_redo_commands PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_undo_redo_commands COMMAND test_undo_redo_commands)

# ── Component serialization (D23) ────────────────────────────────────────────
dash_add_test(test_component_serialization test_component_serialization.cpp)

# ── EntityRegistry + scene migration (D24) ────────────────────────────────────
add_executable(test_entity_registry
    test_entity_registry.cpp
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
target_compile_options(test_entity_registry PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_entity_registry COMMAND test_entity_registry)

# ── MoveEntity and EditProperty commands ─────────────────────────────────────
add_executable(test_move_edit_commands
    test_move_edit_commands.cpp
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
target_compile_options(test_move_edit_commands PRIVATE ${DASH_COMPILE_FLAGS})
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
target_compile_options(test_prefab_system PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_prefab_system COMMAND test_prefab_system)

# ── Content validation (D28) ──────────────────────────────────────────────────
add_executable(test_content_validation
    test_content_validation.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/validation/ContentValidator.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/AssetDatabase.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/AssetRepositorySqlite.cpp
)
target_include_directories(test_content_validation PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/components
    ${CMAKE_SOURCE_DIR}/src/entities
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/editor/validation
    ${CMAKE_SOURCE_DIR}/src/game/nav
    ${CMAKE_SOURCE_DIR}/src/assets
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_content_validation PRIVATE
    game_core nlohmann_json::nlohmann_json SQLite3::SQLite3 ${SDL2_LIBRARIES}
)
target_compile_options(test_content_validation PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_content_validation COMMAND test_content_validation)

# ── Event system (D30) ────────────────────────────────────────────────────────
dash_add_test(test_event_system test_event_system.cpp)

# ── Hot-reload / FileWatcher (D30) ───────────────────────────────────────────
add_executable(test_hot_reload
    test_hot_reload.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/FileWatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/ImportManager.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/AssetDatabase.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/AssetRepositorySqlite.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/importers/SceneImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/importers/TileSetImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/importers/GameplayConfigImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/importers/PrefabImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/importers/SpriteImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/importers/ModelImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/importers/MaterialImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/MaterialAsset.cpp
)
target_include_directories(test_hot_reload PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/components
    ${CMAKE_SOURCE_DIR}/src/entities
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/assets
    ${CMAKE_SOURCE_DIR}/src/assets/importers
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_hot_reload PRIVATE
    game_core nlohmann_json::nlohmann_json SQLite3::SQLite3 ${SDL2_LIBRARIES}
)
if(TARGET assimp::assimp)
    target_link_libraries(test_hot_reload PRIVATE assimp::assimp)
endif()
target_compile_options(test_hot_reload PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_hot_reload COMMAND test_hot_reload)

# ── MaterialImporter + AssetType round-trip (backlog A3) ─────────────────────
add_executable(test_material_importer
    test_material_importer.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/ImportManager.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/AssetDatabase.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/AssetRepositorySqlite.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/MaterialAsset.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/importers/SceneImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/importers/TileSetImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/importers/GameplayConfigImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/importers/PrefabImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/importers/SpriteImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/importers/ModelImporter.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/importers/MaterialImporter.cpp
)
target_include_directories(test_material_importer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/components
    ${CMAKE_SOURCE_DIR}/src/entities
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/assets
    ${CMAKE_SOURCE_DIR}/src/assets/importers
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_material_importer PRIVATE
    game_core nlohmann_json::nlohmann_json SQLite3::SQLite3 ${SDL2_LIBRARIES}
)
if(TARGET assimp::assimp)
    target_link_libraries(test_material_importer PRIVATE assimp::assimp)
endif()
target_compile_options(test_material_importer PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_material_importer COMMAND test_material_importer)

# ── SaveGame round-trip (D32) ─────────────────────────────────────────────────
add_executable(test_save_game
    test_save_game.cpp
    ${CMAKE_SOURCE_DIR}/src/game/save/SaveGame.cpp
    ${CMAKE_SOURCE_DIR}/src/game/save/SaveVersioning.cpp
)
target_include_directories(test_save_game PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/entities
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${CMAKE_SOURCE_DIR}/src/game
    ${CMAKE_SOURCE_DIR}/src/game/runtime
    ${CMAKE_SOURCE_DIR}/src/game/systems
    ${CMAKE_SOURCE_DIR}/src/game/data
    ${CMAKE_SOURCE_DIR}/src/game/nav
    ${CMAKE_SOURCE_DIR}/src/game/save
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_save_game PRIVATE
    game_core nlohmann_json::nlohmann_json ${SDL2_LIBRARIES}
)
target_compile_definitions(test_save_game PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(test_save_game PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_save_game COMMAND test_save_game)

# ── GameplayDatabase loading (D32) ────────────────────────────────────────────
add_executable(test_gameplay_database
    test_gameplay_database.cpp
    ${CMAKE_SOURCE_DIR}/src/game/data/GameplayDatabase.cpp
)
target_include_directories(test_gameplay_database PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/entities
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${CMAKE_SOURCE_DIR}/src/game
    ${CMAKE_SOURCE_DIR}/src/game/runtime
    ${CMAKE_SOURCE_DIR}/src/game/systems
    ${CMAKE_SOURCE_DIR}/src/game/data
    ${CMAKE_SOURCE_DIR}/src/game/nav
    ${CMAKE_SOURCE_DIR}/src/game/save
    ${SDL2_INCLUDE_DIRS}
)
target_compile_definitions(test_gameplay_database PRIVATE
    ASSETS_DIR="${CMAKE_SOURCE_DIR}/assets"
)
target_link_libraries(test_gameplay_database PRIVATE
    game_core nlohmann_json::nlohmann_json ${SDL2_LIBRARIES}
)
target_compile_options(test_gameplay_database PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_gameplay_database COMMAND test_gameplay_database)

# ── Sprite editor pure logic + PNG roundtrip (D45) ──────────────────────────
add_executable(test_sprite_editor
    test_sprite_editor.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/stb_impl.cpp
)
target_include_directories(test_sprite_editor PRIVATE
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
target_link_libraries(test_sprite_editor PRIVATE
    game_core
    nlohmann_json::nlohmann_json
    ${SDL2_LIBRARIES}
)
target_compile_options(test_sprite_editor PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_sprite_editor COMMAND test_sprite_editor)

# ── ProjectManifest roundtrip (D53) ──────────────────────────────────────────
add_executable(test_project_manifest
    test_project_manifest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/project/ProjectManifest.cpp
)
target_include_directories(test_project_manifest PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/editor/project
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_project_manifest PRIVATE
    nlohmann_json::nlohmann_json
    ${SDL2_LIBRARIES}
)
target_compile_options(test_project_manifest PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_project_manifest COMMAND test_project_manifest)

# ── ProjectManager create/open workflow (D53) ───────────────────────────────
add_executable(test_project_manager
    test_project_manager.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/project/ProjectManifest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/project/ProjectManager.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/project/ProjectDataMigrator.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteDb.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteStatement.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SchemaManager.cpp
)
target_include_directories(test_project_manager PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/components
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/editor/project
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_project_manager PRIVATE
    game_core
    nlohmann_json::nlohmann_json
    SQLite3::SQLite3
    ${SDL2_LIBRARIES}
)
target_compile_definitions(test_project_manager PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(test_project_manager PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_project_manager COMMAND test_project_manager)

# ── ProjectDataMigrator JSON->SQLite migration (D58) ───────────────────────
add_executable(test_project_data_migrator
    test_project_data_migrator.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/project/ProjectManifest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/project/ProjectDataMigrator.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteDb.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteStatement.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SchemaManager.cpp
)
target_include_directories(test_project_data_migrator PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/components
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/editor/project
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_project_data_migrator PRIVATE
    game_core
    nlohmann_json::nlohmann_json
    SQLite3::SQLite3
    ${SDL2_LIBRARIES}
)
target_compile_definitions(test_project_data_migrator PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(test_project_data_migrator PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_project_data_migrator COMMAND test_project_data_migrator)

# ── GameBuildPipeline export bundle (D53) ────────────────────────────────────
add_executable(test_game_build_pipeline
    test_game_build_pipeline.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/project/ProjectManifest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/project/GameBuildPipeline.cpp
)
target_include_directories(test_game_build_pipeline PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/editor/project
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_game_build_pipeline PRIVATE
    nlohmann_json::nlohmann_json
    ${SDL2_LIBRARIES}
)
target_compile_options(test_game_build_pipeline PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_game_build_pipeline COMMAND test_game_build_pipeline)

# ── Runtime isolation source check (D53) ─────────────────────────────────────
add_executable(test_runtime_isolation
    test_runtime_isolation.cpp
)
target_include_directories(test_runtime_isolation PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_compile_definitions(test_runtime_isolation PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(test_runtime_isolation PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_runtime_isolation COMMAND test_runtime_isolation)

# ── SQLite schema migrations (D61) ─────────────────────────────────────────
add_executable(test_sqlite_schema
    test_sqlite_schema.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteDb.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteStatement.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SchemaManager.cpp
)
target_include_directories(test_sqlite_schema PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)
target_link_libraries(test_sqlite_schema PRIVATE
    SQLite3::SQLite3
)
target_compile_definitions(test_sqlite_schema PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(test_sqlite_schema PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_sqlite_schema COMMAND test_sqlite_schema)

# ── AssetDatabase SQLite backend (D61) ─────────────────────────────────────
add_executable(test_asset_db_sqlite
    test_asset_db_sqlite.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/AssetDatabase.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/AssetRepositorySqlite.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteDb.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteStatement.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SchemaManager.cpp
)
target_include_directories(test_asset_db_sqlite PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/assets
    ${CMAKE_SOURCE_DIR}/src/core
)
target_link_libraries(test_asset_db_sqlite PRIVATE
    nlohmann_json::nlohmann_json
    SQLite3::SQLite3
)
target_compile_definitions(test_asset_db_sqlite PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(test_asset_db_sqlite PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_asset_db_sqlite COMMAND test_asset_db_sqlite)

# ── GameplayDatabase SQLite backend (D61) ──────────────────────────────────
add_executable(test_gameplay_db_sqlite
    test_gameplay_db_sqlite.cpp
    ${CMAKE_SOURCE_DIR}/src/game/data/GameplayDatabase.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/project/ProjectManifest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/project/ProjectDataMigrator.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteDb.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteStatement.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SchemaManager.cpp
)
target_include_directories(test_gameplay_db_sqlite PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/components
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/editor/project
    ${CMAKE_SOURCE_DIR}/src/game
    ${CMAKE_SOURCE_DIR}/src/game/data
)
target_link_libraries(test_gameplay_db_sqlite PRIVATE
    game_core
    nlohmann_json::nlohmann_json
    SQLite3::SQLite3
)
target_compile_definitions(test_gameplay_db_sqlite PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(test_gameplay_db_sqlite PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_gameplay_db_sqlite COMMAND test_gameplay_db_sqlite)

# ── SQLite cutover performance benchmark (D69) ─────────────────────────────
add_executable(test_sqlite_cutover_performance
    test_sqlite_cutover_performance.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/project/ProjectManifest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/project/ProjectDataMigrator.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/scene/SceneRepositorySqlite.cpp
    ${CMAKE_SOURCE_DIR}/src/game/data/GameplayDatabase.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteDb.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteStatement.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SchemaManager.cpp
)
target_include_directories(test_sqlite_cutover_performance PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/components
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/editor/project
    ${CMAKE_SOURCE_DIR}/src/editor/scene
    ${CMAKE_SOURCE_DIR}/src/game
    ${CMAKE_SOURCE_DIR}/src/game/data
)
target_link_libraries(test_sqlite_cutover_performance PRIVATE
    game_core
    nlohmann_json::nlohmann_json
    SQLite3::SQLite3
)
target_compile_definitions(test_sqlite_cutover_performance PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(test_sqlite_cutover_performance PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_sqlite_cutover_performance COMMAND test_sqlite_cutover_performance)

# ── CLI tool: migrate an arbitrary project root to SQLite ───────────────────
add_executable(migrate_project_data
    migrate_project_data.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/project/ProjectManifest.cpp
    ${CMAKE_SOURCE_DIR}/src/editor/project/ProjectDataMigrator.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteDb.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteStatement.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SchemaManager.cpp
)
target_include_directories(migrate_project_data PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/components
    ${CMAKE_SOURCE_DIR}/src/editor
    ${CMAKE_SOURCE_DIR}/src/editor/project
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
)
target_link_libraries(migrate_project_data PRIVATE
    game_core
    nlohmann_json::nlohmann_json
    SQLite3::SQLite3
)
target_compile_definitions(migrate_project_data PRIVATE
    PROJECT_DIR="${CMAKE_SOURCE_DIR}"
)
target_compile_options(migrate_project_data PRIVATE ${DASH_COMPILE_FLAGS})

# ── Physics determinism baseline (D84) ─────────────────────────────────────
dash_add_test(test_physics_determinism test_physics_determinism.cpp)

# ── Audio smoke tests (D93) ──────────────────────────────────────────────────
add_executable(test_audio_smoke
    test_audio_smoke.cpp
    ${CMAKE_SOURCE_DIR}/src/game/audio/AudioEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/game/audio/AudioEventBindings.cpp
    ${CMAKE_SOURCE_DIR}/src/game/audio/AudioSettingsRepository.cpp
    ${CMAKE_SOURCE_DIR}/src/core/input/InputBindings3D.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteDb.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SqliteStatement.cpp
    ${CMAKE_SOURCE_DIR}/src/core/db/SchemaManager.cpp
)
target_include_directories(test_audio_smoke PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/events
    ${CMAKE_SOURCE_DIR}/src/game/audio
    ${CMAKE_SOURCE_DIR}/src/core/input
)
target_link_libraries(test_audio_smoke PRIVATE
    SQLite3::SQLite3
    nlohmann_json::nlohmann_json
)
if(APPLE)
    target_link_libraries(test_audio_smoke PRIVATE
        "-framework AudioToolbox"
        "-framework CoreFoundation"
        "-framework CoreAudio"
    )
endif()
target_compile_options(test_audio_smoke PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_audio_smoke COMMAND test_audio_smoke)

# ── Model import pipeline (D101) ────────────────────────────────────────────
if(TARGET assimp::assimp)
    add_executable(test_model_import_pipeline
        test_model_import_pipeline.cpp
        ${CMAKE_SOURCE_DIR}/src/assets/importers/ModelImporter.cpp
        ${CMAKE_SOURCE_DIR}/src/assets/importers/MaterialImporter.cpp
        ${CMAKE_SOURCE_DIR}/src/assets/MaterialAsset.cpp
    )
    target_include_directories(test_model_import_pipeline PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
        ${CMAKE_SOURCE_DIR}/src/assets
        ${CMAKE_SOURCE_DIR}/src/assets/importers
        ${CMAKE_SOURCE_DIR}/src/rendering
    )
    target_link_libraries(test_model_import_pipeline PRIVATE
        game_core
        assimp::assimp
        nlohmann_json::nlohmann_json
    )
    target_compile_options(test_model_import_pipeline PRIVATE ${DASH_COMPILE_FLAGS})
    add_test(NAME test_model_import_pipeline COMMAND test_model_import_pipeline)
endif()

# ── Scene components → RenderInstance mapping (Sprint 15) ───────────────────
# SceneLoader has no Vulkan dependency, so this runs headless in CI.
add_executable(test_scene_loader_3d
    test_scene_loader_3d.cpp
    ${CMAKE_SOURCE_DIR}/src/rendering/vulkan/SceneLoader.cpp
)
target_include_directories(test_scene_loader_3d PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/components
    ${CMAKE_SOURCE_DIR}/src/scene
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${SDL2_INCLUDE_DIRS}
)
target_link_libraries(test_scene_loader_3d PRIVATE
    game_core
    nlohmann_json::nlohmann_json
    ${SDL2_LIBRARIES}
)
target_compile_options(test_scene_loader_3d PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_scene_loader_3d COMMAND test_scene_loader_3d)

# ── MaterialAsset serialization (Sprint 15) ────────────────────────────────
add_executable(test_material_asset
    test_material_asset.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/MaterialAsset.cpp
)
target_include_directories(test_material_asset PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/assets
)
target_link_libraries(test_material_asset PRIVATE nlohmann_json::nlohmann_json)
target_compile_options(test_material_asset PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_material_asset COMMAND test_material_asset)

# ── Frustum culling math (Sprint 15) ───────────────────────────────────────
# Header-only and Vulkan-free, so this runs anywhere.
add_executable(test_frustum_culling test_frustum_culling.cpp)
target_include_directories(test_frustum_culling PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_options(test_frustum_culling PRIVATE ${DASH_COMPILE_FLAGS})
add_test(NAME test_frustum_culling COMMAND test_frustum_culling)

