# Sprint 12 — QA Checklist

## CMake Presets

- [ ] `cmake --preset macos-release` configures successfully
- [ ] `cmake --preset macos-debug` configures with BUILD_TESTING=ON
- [ ] Windows presets validate correctly (syntax check)

## Packaging

- [ ] `cmake/Packaging.cmake` generates correct install layout on macOS (`.app` bundle)
- [ ] `cmake/Packaging.cmake` generates correct install layout on Windows (`bin/` flat)
- [ ] `packaging/windows/install_app.bat` packages executables + resources

## Build Verification

- [ ] `cmake --build build --parallel` compiles without errors on macOS
- [ ] All existing tests still pass (no regressions)
- [ ] VulkanBootstrap renders correctly
- [ ] DashEngine editor opens without crashes

## CI/CD

- [ ] `.github/workflows/build-cross-platform.yml` syntax is valid
- [ ] macOS job: configure + build + test
- [ ] Windows job: vcpkg setup + configure + build + test
- [ ] Artifacts uploaded for both platforms

## Documentation

- [ ] CMakePresets.json documented in README
- [ ] SPRINT12_MSVC_FIXES.md covers all compatibility measures
- [ ] SPRINT12_VULKAN_PLATFORM_NOTES.md explains platform abstraction
- [ ] Planning docs updated (00_INDEX, 14, 19, SCALING_CHECKLIST)
- [ ] README progress bars reflect Sprint 10/11/12 completion

## Regression Checks

- [ ] All pre-existing tests still pass
- [ ] Sprint 11 tests pass (test_model_import_pipeline)
- [ ] Audio smoke tests still pass
- [ ] Physics determinism tests still pass
