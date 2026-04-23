# Sprint 10 — QA Checklist

## Automated Tests

- [x] `test_audio_smoke` — all assertions pass (`ctest --output-on-failure -R test_audio_smoke`)
  - [x] AudioEngine init/shutdown lifecycle
  - [x] AudioEventBindings cooldown prevents spam
  - [x] All four event types bound without crash
  - [x] AudioSettingsRepository float round-trip (in-memory SQLite)
  - [x] InputBindings3D defaults match expected GLFW key codes

## Build Verification

- [x] `IsometricRPG` — builds and links cleanly
- [x] `DashEngine` — builds and links cleanly
- [x] `VulkanBootstrap` — builds and links cleanly
- [x] `test_audio_smoke` — builds and links cleanly (including Apple audio frameworks)

## Manual Verification

- [ ] Launch `IsometricRPG` → select class → enter combat → hear damage tones (220 Hz)
- [ ] Kill enemy → hear death tone (110 Hz low boom)
- [ ] Level up → hear chime (880 Hz)
- [ ] Loot drop → hear ding (440 Hz)
- [ ] Rapid attack spam → no audio distortion (cooldown active)
- [ ] Close and reopen game → volume settings preserved in SQLite
- [ ] Launch `VulkanBootstrap` → WASD movement works identically to before
- [ ] Right-click mouse look works with configurable sensitivity
- [ ] DashEngine editor → Press Play → audio triggers on combat events
- [ ] DashEngine editor → Edit mode → viewport unchanged (no audio interference)

## No Regressions

- [ ] Existing tests (`ctest --output-on-failure`) — no new failures
- [ ] Editor undo/redo, scene load/save — unchanged
- [ ] Vulkan camera presets — still correct after InputBindings3D refactor
