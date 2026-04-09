# Sprint 9 QA Checklist (D84)

## Objetivo

Validar estabilidad y determinismo minimo de la base fisica integrada al renderer Vulkan experimental.

## Evidencia minima

- [ ] Build verde de DashEngine.
- [ ] Build verde de VulkanBootstrap.
- [ ] Smoke run de VulkanBootstrap con logs D80-D83.
- [ ] `test_physics_determinism` en verde.
- [ ] `ctest --output-on-failure` sin regresiones en pruebas existentes relevantes.

## Criterios tecnicos

- [ ] Step fijo de fisica activo a 60 Hz con acumulador.
- [ ] Escena baseline cubo + plano reproducible (`scenes/physics_baseline.json`).
- [ ] Sin penetracion persistente del cubo bajo tolerancia numerica.
- [ ] Evento de colision `Enter` observable en logs.

## Notas de ejecucion local

1. Configurar y compilar con CMake.
2. Ejecutar `VulkanBootstrap` y verificar logs de inicializacion fisica y posicion final del cubo.
3. Ejecutar `ctest -R physics_determinism --output-on-failure`.
