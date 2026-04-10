# SPRINT 9 - DINÁMICA Y MUNDO FÍSICO
## Trabajo Registrado con Detalles y Criterios de Aceptación

**Sprint**: Sprint 9 - World Physics Integration  
**Versión**: 1.0  
**Fecha Inicio**: 2026-04-09  
**Estado General**: 🔴 NO INICIADO  

---

## 📋 META DEL SPRINT

Integrar sistema de físicas 3D determinista al renderer Vulkan experimental, permitiendo:
- Simulación de gravedad y colisiones reproducibles
- Sincronización entre estado físico y renderizado
- Herramientas de debug para visualizar colisiones
- Baseline para validar physics engine integration

**Valor de negocio**: Habilitar gameplay con restricciones físicas (caídas, colisiones, movimiento realista).

---

## 📊 DESGLOSE DE TAREAS

### TAREA 9.1 - Integración Physics Backend y PhysicsWorld
**Código**: D80  
**Estimación**: 4 días  
**Prioridad**: P0 (bloqueante)  
**Estado**: 🔴 NOT STARTED

#### Objetivo
Configurar motor de físicas (Jolt o Bullet) con:
- Inicialización desacoplada mediante interfaz backend
- World step determinista con timestep fijo
- Integración en main game loop

#### Descripción del trabajo

**Crear archivos:**
```
src/game/physics/PhysicsWorld.h         // Clase managers de simulación
src/game/physics/PhysicsWorld.cpp       // Implementación
src/game/physics/IPhysicsBackend.h      // Interfaz abstracta para backends
```

**Modificar archivos:**
```
CMakeLists.txt                          // Agregar linkage de physics library
src/main.cpp                            // Inicializar PhysicsWorld en main loop
```

#### Criterios de Aceptación

| # | Criterio | Aceptación |
|---|----------|-----------|
| 1 | PhysicsWorld inicializa sin crashes en constructor | ✓ Se instancia y destruye limpiamente |
| 2 | IPhysicsBackend define interfaz abstracta con step() | ✓ Clase base con métodos puros |
| 3 | World step estable a 30 FPS | ✓ dt=33.33ms aplicado correctamente |
| 4 | World step estable a 60 FPS | ✓ dt=16.67ms aplicado correctamente |
| 5 | World step estable a 120 FPS | ✓ dt=8.33ms aplicado correctamente |
| 6 | Acumulador delta previene overshooting | ✓ Substeps aplicados cuando dt > fixed_dt |
| 7 | Build DashEngine sin errores/warnings | ✓ CMake succeeds, zero warnings |
| 8 | No regresiones en tets existentes | ✓ Todos los tests pasan post-integración |

#### Detalles técnicos

**PhysicsWorld estructura:**
```cpp
class PhysicsWorld {
  private:
    std::unique_ptr<IPhysicsBackend> backend;
    float accumulator = 0.0f;
    const float FIXED_TIMESTEP = 1.0f / 60.0f;
  
  public:
    void initialize(const std::string& backendType);
    void step(float deltaTime);
    void shutdown();
    
    // Queries
    bool rayCast(const glm::vec3& from, const glm::vec3& to);
    std::vector<BodyHandle> getBodiesInRadius(const glm::vec3& center, float radius);
};
```

**IPhysicsBackend estructura:**
```cpp
class IPhysicsBackend {
  virtual void step(float dt) = 0;
  virtual BodyHandle createRigidBody(const RigidBodyDef& def) = 0;
  virtual void destroyRigidBody(BodyHandle h) = 0;
};
```

#### Testing
- [ ] Unit test: PhysicsWorld inicialización
- [ ] Unit test: Step acumulador a múltiples framerates
- [ ] Integration test: Inicialización completa del engine

---

### TAREA 9.2 - Transformaciones con GLM (Model*View*Projection)
**Código**: D81  
**Estimación**: 3 días  
**Prioridad**: P0 (bloqueante después de D80)  
**Estado**: 🔴 NOT STARTED

#### Objetivo
Sincronizar posición/rotación visual con estado físico, manteniendo:
- Fuente única de verdad (physics engine)
- Sin drift visible entre rendering y simulación
- Matrices MVP correctas para 3D isométrico

#### Descripción del trabajo

**Crear archivos:**
```
src/game/physics/TransformProxy.h       // Proxy entre RigidBody y Transform visual
src/game/physics/TransformProxy.cpp     // Sincronización bidireccional
```

**Modificar archivos:**
```
src/components/Components.h             // Agregar PhysicsBodyComponent
src/core/Reflection.cpp                 // Registrar metadatos de PhysicsBodyComponent
src/game/physics/PhysicsWorld.cpp       // Agregar métodos de query para posición
src/rendering/Renderer.cpp              // Aplicar matrices MVP desde physics state
```

#### Criterios de Aceptación

| # | Criterio | Aceptación |
|---|----------|-----------|
| 1 | RigidBody posición feed a Transform.position | ✓ Sincronización cada frame |
| 2 | RigidBody rotación feed a Transform.rotation | ✓ Cuaterniones convertidos a Euler |
| 3 | Matriz modelo computed desde Transform physics | ✓ GLM::translate(glm::rotate(...)) correcto |
| 4 | No drift visual >0.01 unidades sobre 300 frames | ✓ Error acumulado <= tolerancia |
| 5 | Renderizado isométrico respeta posición 3D | ✓ Worldspace → screenspace projection correcto |
| 6 | PhysicsBodyComponent serializable/deserializable | ✓ JSON round-trip para scenes |

#### Detalles técnicos

**PhysicsBodyComponent:**
```cpp
struct PhysicsBodyComponent : public Component {
  uint64_t bodyHandle = 0;              // Identificador en physics engine
  glm::vec3 linearVelocity = {0,0,0};
  glm::vec3 angularVelocity = {0,0,0};
  float mass = 1.0f;
  bool isStatic = false;
};
```

**TransformProxy:**
- Recibe RigidBody handle
- Cada frame: lee posición/rotación de physics engine
- Actualiza TransformComponent del entity
- Computa MVP matrices para renderer

#### Testing
- [ ] Unit test: Matriz modelo correcta de posición + rotación
- [ ] Integration test: Entity con body físico se renderiza correctamente
- [ ] Regression test: No breaking changes en Transform existing

---

### TAREA 9.3 - Hitboxes y Colliders
**Código**: D82  
**Estimación**: 3 días  
**Prioridad**: P1  
**Estado**: 🔴 NOT STARTED

#### Objetivo
Vincular objetos visuales con geometría de colisión:
- Colliders box/AABB operativos
- Debug draw habilitable desde herramientas
- Visualización de colisiones en viewport

#### Descripción del trabajo

**Crear archivos:**
```
src/game/physics/Colliders.h            // Definiciones de geometría (Box, Sphere, etc)
src/game/physics/DebugPhysicsDraw.cpp   // Renderizado de debug de colisiones
```

**Modificar archivos:**
```
src/components/Components.h             // Agregar ColliderComponent
src/editor/EditorApp.cpp                // Toggle debug draw en viewport
src/game/physics/PhysicsWorld.cpp       // Registrar colliders en RigidBodyDef
```

#### Criterios de Aceptación

| # | Criterio | Aceptación |
|---|----------|-----------|
| 1 | ColliderComponent define box/sphere/cylinder | ✓ Struct con type enum + propiedades |
| 2 | Collider se vincula a RigidBody en PhysicsWorld | ✓ createRigidBody acepta ColliderComponent |
| 3 | Debug overlay dibuja AABB wireframe | ✓ Líneas 3D en screenspace |
| 4 | Toggle debug draw en World Settings panel | ✓ Checkbox enable/disable |
| 5 | Collider visible matchea geometría visual ± 5% | ✓ Referencia visual vs debug draw |
| 6 | Múltiples colliders por entity soportados | ✓ Vector<Collider> en component |

#### Detalles técnicos

**ColliderComponent:**
```cpp
struct ColliderComponent : public Component {
  enum Type { Box, Sphere, Cylinder, Capsule };
  
  Type type = Box;
  glm::vec3 size = {1,1,1};             // Para Box: half-extents
  float radius = 0.5f;                  // Para Sphere/Cylinder
  float height = 2.0f;                  // Para Cylinder/Capsule
  glm::vec3 center = {0,0,0};           // Offset local
  bool isTrigger = false;               // Si es sensor (no physical)
};
```

**DebugPhysicsDraw:**
```cpp
class DebugPhysicsDraw {
  void drawBox(const glm::vec3& center, const glm::vec3& halfExtents, const glm::mat4& transform);
  void drawSphere(const glm::vec3& center, float radius);
  void drawCylinder(const glm::vec3& center, float radius, float height);
  
  void render(const Renderer& renderer);  // Batch renderizado
};
```

#### Testing
- [ ] Unit test: ColliderComponent JSON serialization
- [ ] Integration test: Collider registrado correctamente en physics engine
- [ ] Visual test: Debug draw activo/inactivo toggle works

---

### TAREA 9.4 - Gravedad y Colisión con Plano Base
**Código**: D83  
**Estimación**: 2 días  
**Prioridad**: P0 (after D80, D82)  
**Estado**: 🔴 NOT STARTED

#### Objetivo
Crear escena baseline de validación:
- Cubo cayendo sobre plano bajo gravedad
- Sin penetración persistente
- Reproducible por seed

#### Descripción del trabajo

**Crear archivos:**
```
scenes/physics_baseline.json            // Escena con cubo + plano
```

**Modificar archivos:**
```
src/game/physics/PhysicsWorld.cpp       // Integrar gravedad (setGravity)
src/editor/SceneData.cpp                // Cargar physics_baseline.json
src/main.cpp                            // Opción CLI para cargar baseline
```

#### Criterios de Aceptación

| # | Criterio | Aceptación |
|---|----------|-----------|
| 1 | physics_baseline.json define cubo + plano | ✓ Entities con Transform + Collider + Physics |
| 2 | Gravedad configurada a 9.81 m/s² | ✓ setGravity(0, -9.81, 0) aplicada |
| 3 | Cubo cae visualmente | ✓ Posición Z decrece cada frame |
| 4 | Cubo se estabiliza en plano | ✓ Termina velocidad ≈ 0 en t=2-3 seg |
| 5 | No penetración >0.01 unidades | ✓ Cubo resting sobre plano, no hundido |
| 6 | Reproducible: seed fijo → mismo resultado | ✓ 3 runs idénticas en posición final |

#### Detalles técnicos

**physics_baseline.json estructura:**
```json
{
  "version": 3,
  "entities": [
    {
      "id": 1,
      "name": "plano_base",
      "transform": { "x": 0, "y": -1, "z": 0, "scale": [10, 0.5, 10] },
      "collider": { "type": "Box", "size": [10, 0.5, 10] },
      "physics": { "mass": 0, "isStatic": true }
    },
    {
      "id": 2,
      "name": "cubo_caida",
      "transform": { "x": 0, "y": 5, "z": 0, "scale": [1, 1, 1] },
      "collider": { "type": "Box", "size": [1, 1, 1] },
      "physics": { "mass": 1.0, "isStatic": false }
    }
  ]
}
```

**Baseline run:**
- t=0: cubo en (0, 5, 0)
- t=1s: cubo aproximadamente (0, 0, 0)
- t=3s: cubo en (0, -0.5, 0), resting on plano

#### Testing
- [ ] Smoke test: VulkanBootstrap --scene scenes/physics_baseline.json runs
- [ ] Visual verification: Cubo cae y se estabiliza
- [ ] Determinism test: 3 consecutive runs identical results

---

### TAREA 9.5 - QA y Validación de Sprint
**Código**: D84  
**Estimación**: 2 días  
**Prioridad**: P1  
**Estado**: 🔴 NOT STARTED

#### Objetivo
Validar que todas las tareas precedentes funcionan integradas:
- Build sin errores
- Todos los tests pasan
- Determinismo verificado
- Performance baseline registrado

#### Descripción del trabajo

**Crear archivos:**
```
tests/test_physics_determinism.cpp      // Reproducibilidad de física
planning/SPRINT9_QA_RESULTS.md          // Reporte final de validación
```

**Modificar archivos:**
```
tests/CMakeLists.txt                    // Registrar test_physics_determinism
README.md                               // Sección de Physics Engine status
```

#### Criterios de Aceptación (Acceptance Criteria Finales del Sprint)

| Criterio | Aceptación | Verificación |
|----------|-----------|--------------|
| **Build DashEngine** | Zero errors, zero warnings | `cmake --build build --target DashEngine` |
| **Build VulkanBootstrap** | Zero errors, zero warnings | `cmake --build build --target VulkanBootstrap` |
| **Smoke run VulkanBootstrap** | Executes without crash, shows logs D80-D83 initialized | `./build/bin/VulkanBootstrap --scene scenes/physics_baseline.json` 120s |
| **test_physics_determinism** | 100% pass rate (3 runs, identical results) | `ctest -R physics_determinism -V` |
| **All existing tests** | No regressions in: test_scene_serialization, test_component_serialization | `ctest --output-on-failure` |
| **Step timing stability** | 60 FPS physics step within ±2% | Profile log shows dt variance |
| **Physics penetration** | Cubo no penetra plano > 0.01 units | Visual + log verification |
| **Collision event logging** | "Collision Enter: ID1-ID2" appears in logs | Run baseline, grep logs |

#### Detalles técnicos

**test_physics_determinism.cpp estructura:**
```cpp
TEST(PhysicsDeterminism, CuboFallWithFixedSeed) {
  // 1. Load physics_baseline.json
  // 2. Set random seed = 12345
  // 3. Simulate 300 frames (5 seconds @ 60 FPS)
  // 4. Record cubo final position
  // 5. Repeat 3 times, expect identical positions within 1e-6f precision
}

TEST(PhysicsDeterminism, StepTimingConsistency) {
  // 1. Run 600 frames (10 seconds @ 60 FPS)
  // 2. Measure delta time per step
  // 3. Assert: mean_dt = 16.67ms, stdev_dt < 0.5ms
}
```

**QA Coverage:**
- Functional testing: All tasks work end-to-end
- Determinism testing: Physics reproducible
- Performance testing: Frame timing stable
- Regression testing: No broken existing features

#### Testing
- [ ] Unit: test_physics_determinism passes 3x
- [ ] Integration: Full baseline scene loads and simulates
- [ ] Build: Zero warnings/errors on both targets
- [ ] Regression: All pre-existing test suite passes

---

## 🎯 MAPA DE DEPENDENCIAS

```
D80 (PhysicsWorld, Backend)
  ↓
D81 (Transform Sync, MVP)  ← depends on D80
D82 (Colliders, Debug)      ← depends on D80
  ↓
D83 (Gravity, Baseline)     ← depends on D80, D81, D82
  ↓
D84 (QA, Validation)        ← depends on D80-D83
```

**Ruta crítica**: D80 → D81 → D83 → D84 (11 días de bloqueantes)  
**Paralelizable**: D82 puede correr con D80 (comenzar día 2)

---

## 📈 TRACKING DE TRABAJO

### Timeline Estimado

| Semana | Tarea | Estado | % Completado | Blockers |
|--------|-------|--------|--------------|----------|
| Sem 1 (9-13 Abr) | D80 | 🔴 No iniciado | 0% | - |
| Sem 1 (9-13 Abr) | D82 (paralelo) | 🔴 No iniciado | 0% | - |
| Sem 2 (14-18 Abr) | D81 | 🔴 Planned | 0% | D80 |
| Sem 2 (14-18 Abr) | D83 | 🔴 Planned | 0% | D80+D81+D82 |
| Sem 3 (21-23 Abr) | D84 | 🔴 Planned | 0% | D83 |

### Registro de Cambios Implementados

#### Fase 1: Preparación (Pre-Sprint)
**Fecha**: 2026-04-09  
- [x] Sprint 9 planning document created
- [x] Task breakdown finalized
- [x] Acceptance criteria defined
- [x] Dependencies mapped
- [ ] Physics backend selection (Jolt vs Bullet) - PENDING

#### Fase 2: Implementación D80 (PENDIENTE)
**Fecha**: TBD  
- [ ] src/game/physics/PhysicsWorld.h created
- [ ] src/game/physics/PhysicsWorld.cpp implemented
- [ ] src/game/physics/IPhysicsBackend.h defined
- [ ] CMakeLists.txt updated with physics linkage
- [ ] src/main.cpp integrated with PhysicsWorld initialization
- [ ] Unit tests: PhysicsWorld initialization
- [ ] Integration tests: Multi-framerate step timing

#### Fase 3: Implementación D81 (PENDIENTE)
**Fecha**: TBD  
- [ ] src/game/physics/TransformProxy.h created
- [ ] src/game/physics/TransformProxy.cpp implemented
- [ ] PhysicsBodyComponent added to Components.h
- [ ] Reflection metadata registered
- [ ] Synchronization logic: RigidBody → Transform
- [ ] MVP matrix computation from physics state
- [ ] Integration tests: Rendering matches physics position

#### Fase 4: Implementación D82 (PENDIENTE)
**Fecha**: TBD  
- [ ] src/game/physics/Colliders.h created
- [ ] src/game/physics/DebugPhysicsDraw.cpp implemented
- [ ] ColliderComponent added to Components.h
- [ ] Debug overlay toggle in EditorApp UI
- [ ] Collider visualization in viewport
- [ ] Visual verification tests

#### Fase 5: Implementación D83 (PENDIENTE)
**Fecha**: TBD  
- [ ] scenes/physics_baseline.json created
- [ ] Gravity integration in PhysicsWorld
- [ ] Baseline scene loader in SceneData
- [ ] CLI option for baseline loading
- [ ] Smoke tests: Baseline execution
- [ ] Determinism verification

#### Fase 6: QA y Cierre D84 (PENDIENTE)
**Fecha**: TBD  
- [ ] tests/test_physics_determinism.cpp implemented
- [ ] Build validation (DashEngine, VulkanBootstrap)
- [ ] Test suite execution (100% pass rate)
- [ ] Performance profiling complete
- [ ] planning/SPRINT9_QA_RESULTS.md documented
- [ ] README.md updated with physics engine status

---

## ✅ CRITERIOS DE ACEPTACIÓN GLOBALES (DEFINICIÓN DONE)

Un sprint se considera completo cuando:

1. **Implementación Técnica**: Todas las tareas D80-D84 tienen código implementado, compilado y sin warnings
2. **Testing**: 100% de tests pasan (nuevos + existentes), cero regresiones
3. **Documentation**: Cada tarea tiene README inline en código, SPRINT9_QA_RESULTS.md completado
4. **Determinism**: test_physics_determinism pasa 3 ejecuciones consecutivas con resultados idénticos
5. **Performance**: Physics step timing establemente dentro de **margin de framerate actual**
6. **Integration**: Escena baseline cargable, cubo visible cayendo, se estabiliza sin penetración
7. **CI Validation**: `ctest --output-on-failure` verde en macOS con Clang

---

## 📝 NOTAS Y OBSERVACIONES

### Consideraciones técnicas
- **Physics engine**: Jolt Physics preferido (deterministic, open-source, Rust bindings disponibles)
- **Fixed timestep**: 60 FPS canonical (16.67ms), acumulador para variable framerate
- **Floating point precision**: Usar `float` (32-bit), tolerancia de colisión ±0.01 units
- **Debug visualization**: Implementar en 3D isometric, no romper viewport 2D/legacy

### Riesgos identificados
| Riesgo | Probabilidad | Impacto | Mitigación |
|--------|-------------|--------|-----------|
| Physics library linkage complexity | Media | Alto | Proto con header-only physics |
| Determinism broken by random floats | Media | Medio | Use seeded PRNG, avoid `rand()` |
| Performance regression in rendering | Baja | Medio | Profile before/after D81 |
| Scene versioning conflicts | Baja | Alto | Migrate v2→v3 tested before D83 |

### Próximos sprints dependent
- **Sprint 10**: Audio + Persistence (necesita physics state serializable)
- **Sprint 11**: 3D Asset Import (necesita colliders para assets)
- **Sprint 12**: Windows Portability (physics library cross-platform linkage)

---

## 📞 COMANDO RAPIDO DE EJECUCIÓN

Una vez lista la implementación:

```bash
# Build all
cd /Users/lihuelibanez/Development/proyects/Dash-Engine
cmake --build build --target DashEngine VulkanBootstrap --parallel

# Run baseline
./build/bin/VulkanBootstrap --scene scenes/physics_baseline.json

# Run tests
cd build && ctest -R "physics_determinism|scene_serialization|component_serialization" -V --output-on-failure

# Full QA report
ctest --output-on-failure > SPRINT9_QA_RESULTS.log 2>&1 && cat SPRINT9_QA_RESULTS.log
```

---

**Documento preparado**: 2026-04-09 | **Estado**: Listo para ejecución  
**Responsable de ejecución**: GitHub Copilot Agent  
**Reviewer**: Usuario (LihueLíbanez)
