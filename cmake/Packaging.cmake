# ─────────────────────────────────────────────────────────────────────────────
# cmake/Packaging.cmake
# DashEngine packaging: VersionInfo generation, install rules, CPack config.
# Included from the top-level CMakeLists.txt AFTER all targets are defined.
# ─────────────────────────────────────────────────────────────────────────────

# ── Version info ──────────────────────────────────────────────────────────────
# Version comes from project(IsometricRPG VERSION 2.0.0) in CMakeLists.txt.

# Get git commit hash
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
)
if(NOT GIT_COMMIT)
    set(GIT_COMMIT "unknown")
endif()

# Build date
string(TIMESTAMP BUILD_DATE "%Y-%m-%d" UTC)

# Generate VersionInfo.h into the build tree so targets can include it.
configure_file(
    "${CMAKE_SOURCE_DIR}/src/core/VersionInfo.h.in"
    "${CMAKE_BINARY_DIR}/generated/VersionInfo.h"
    @ONLY
)

# ── Install rules ─────────────────────────────────────────────────────────────
set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install prefix" FORCE)

install(TARGETS DashEngine
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION .
)
install(TARGETS IsometricRPG
    RUNTIME DESTINATION .
)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/assets/"
    DESTINATION "DashEngine.app/Contents/Resources/assets"
)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/library/"
    OPTIONAL
    DESTINATION "DashEngine.app/Contents/Resources/library"
)
install(DIRECTORY "${CMAKE_SOURCE_DIR}/scenes/"
    DESTINATION "DashEngine.app/Contents/Resources/scenes"
)

# ── CPack (macOS DragNDrop → .dmg) ───────────────────────────────────────────
set(CPACK_GENERATOR "DragNDrop")
set(CPACK_PACKAGE_NAME "DashEngine")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_FILE_NAME "DashEngine-${PROJECT_VERSION}-macOS")
set(CPACK_DMG_VOLUME_NAME "DashEngine ${PROJECT_VERSION}")
set(CPACK_BUNDLE_NAME "DashEngine")
set(CPACK_BUNDLE_PLIST "${CMAKE_SOURCE_DIR}/packaging/Info.plist")
set(CPACK_BUNDLE_ICON "${CMAKE_SOURCE_DIR}/packaging/DashEngine.icns")

include(CPack)
