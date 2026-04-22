# ── Game runtime + standalone executable ─────────────────────────────────────

file(GLOB GAME_RUNTIME_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/runtime/*.cpp")
file(GLOB GAME_SYSTEM_SOURCES  "${CMAKE_CURRENT_SOURCE_DIR}/systems/*.cpp")
file(GLOB GAME_DATA_SOURCES    "${CMAKE_CURRENT_SOURCE_DIR}/data/*.cpp")
file(GLOB GAME_SAVE_SOURCES    "${CMAKE_CURRENT_SOURCE_DIR}/save/*.cpp")
file(GLOB GAME_RENDERING_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/rendering/*.cpp")

add_library(game_runtime STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/Game.cpp
    ${GAME_RUNTIME_SOURCES}
    ${GAME_SYSTEM_SOURCES}
    ${GAME_DATA_SOURCES}
    ${GAME_SAVE_SOURCES}
    ${GAME_RENDERING_SOURCES}
)

target_include_directories(game_runtime PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/core/components
    ${CMAKE_SOURCE_DIR}/src/entities
    ${CMAKE_SOURCE_DIR}/src/world
    ${CMAKE_SOURCE_DIR}/src/rendering
    ${CMAKE_SOURCE_DIR}/src/game
    ${CMAKE_SOURCE_DIR}/src/game/runtime
    ${CMAKE_SOURCE_DIR}/src/game/systems
    ${CMAKE_SOURCE_DIR}/src/game/data
    ${CMAKE_SOURCE_DIR}/src/game/save
    ${CMAKE_SOURCE_DIR}/src/game/nav
    ${CMAKE_SOURCE_DIR}/src/game/rendering
    ${SDL2_INCLUDE_DIRS}
)

target_link_libraries(game_runtime PUBLIC
    game_core
    nlohmann_json::nlohmann_json
    ${SDL2_LIBRARIES}
)

target_compile_options(game_runtime PRIVATE -Wall -Wextra -O2)

if(ENABLE_ISOLATION_CHECK)
    target_compile_definitions(game_runtime PRIVATE GAME_RUNTIME_ONLY)
    get_target_property(GAME_RUNTIME_ALL_SOURCES game_runtime SOURCES)
    add_custom_target(check_isolation
        COMMAND ${CMAKE_COMMAND}
            -DGAME_RUNTIME_SOURCES="${GAME_RUNTIME_ALL_SOURCES}"
            -P "${CMAKE_SOURCE_DIR}/cmake/CheckGameRuntimeIsolation.cmake"
        COMMENT "Checking game runtime isolation"
        VERBATIM
    )
endif()
