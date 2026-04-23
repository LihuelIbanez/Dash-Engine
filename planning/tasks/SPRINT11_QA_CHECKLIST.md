# Sprint 11 — QA Checklist

## Automated Tests

- [ ] `test_model_import_pipeline` — 6 tests, all pass
  - test_vertex_layout_size (sizeof == 32, offsets correct)
  - test_asset_type_model (enum + string conversion)
  - test_mesh_data_struct (construction + field access)
  - test_assimp_loads_obj (parse .obj → DMSH binary)
  - test_assimp_invalid_file (graceful failure)
  - test_assimp_nonexistent_file (error, no crash)

## Build Verification

- [ ] `dash build` compiles without warnings
- [ ] VulkanBootstrap builds and links correctly
- [ ] DashEngine (editor) builds and links correctly
- [ ] IsometricRPG builds and links correctly
- [ ] Shader compilation (textured.vert.spv + textured.frag.spv) succeeds

## Manual Verification

- [ ] VulkanBootstrap renders cube with depth testing (no z-fighting)
- [ ] Textured pipeline is active (shader binds correctly)
- [ ] WASD camera movement still works
- [ ] Editor opens without crashes
- [ ] IsometricRPG launches without crashes

## Regression Checks

- [ ] All pre-existing tests still pass
- [ ] Audio smoke tests still pass
- [ ] Sprint 10 functionality intact (audio, input, settings)
