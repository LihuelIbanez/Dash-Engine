# CheckGameRuntimeIsolation.cmake
# Validates that game runtime source files do not include editor-only headers.

if(NOT DEFINED GAME_RUNTIME_SOURCES)
    message(FATAL_ERROR "GAME_RUNTIME_SOURCES is required")
endif()

set(_forbidden_patterns
    "#include[ \t]*[\"<].*src/editor/"
    "#include[ \t]*[\"<].*imgui"
    "#include[ \t]*[\"<].*stb_image_write"
)

set(_violations "")

foreach(_src IN LISTS GAME_RUNTIME_SOURCES)
    if(NOT EXISTS "${_src}")
        continue()
    endif()

    file(READ "${_src}" _content)
    foreach(_pat IN LISTS _forbidden_patterns)
        string(REGEX MATCH "${_pat}" _m "${_content}")
        if(_m)
            string(APPEND _violations "${_src}\n  -> ${_m}\n")
        endif()
    endforeach()
endforeach()

if(_violations)
    message(FATAL_ERROR "Game runtime isolation check failed:\n${_violations}")
endif()

message(STATUS "Game runtime isolation check passed.")
