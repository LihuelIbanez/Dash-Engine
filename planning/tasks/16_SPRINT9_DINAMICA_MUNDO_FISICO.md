# Sprint 9 - Dinamica y Mundo Fisico

## Meta del sprint

Integrar fisicas 3D al renderer para movimiento, gravedad y colisiones reproducibles.

**Estado: ✅ COMPLETADO**

Implementado en commit 2ad5d01 (2026-04-09).
PhysicsWorld con backend builtin (sin Jolt/Bullet), fixed timestep, AABB colliders, gravedad, y tests de determinismo pasando.

---

## Tarea 9.1 (D80) - Integracion Jolt/Bullet y PhysicsWorld

### Objetivo
Configurar motor de fisicas y world step determinista.

### Resultado esperado
- PhysicsWorld inicializable con backend desacoplado.
- Simulacion estable con fixed timestep.

### Archivos a crear
1. src/game/physics/PhysicsWorld.h
2. src/game/physics/PhysicsWorld.cpp
3. src/game/physics/IPhysicsBackend.h

### Archivos a modificar
1. CMakeLists.txt
2. src/main.cpp

### Criterio de aceptacion
- PhysicsWorld inicializa y simula sin crash.
- Step estable en 30/60/120 FPS.

---

## Tarea 9.2 (D81) - Transformaciones con GLM (P*V*M)

### Objetivo
Sincronizar transform visual con estado fisico.

### Resultado esperado
- Fuente de verdad unica para posicion/rotacion.
- Sin drift visible entre estado fisico y render.

### Archivos a crear
1. src/game/physics/TransformProxy.h
2. src/game/physics/TransformProxy.cpp

### Archivos a modificar
1. src/rendering/ (actualizacion de matrices)
2. src/game/physics/PhysicsWorld.cpp

### Criterio de aceptacion
- Objeto renderizado coincide con rigid body.

---

## Tarea 9.3 (D82) - Hitboxes y colliders

### Objetivo
Vincular objetos visuales con hitboxes de colision.

### Resultado esperado
- Colliders box/AABB operativos para entidades de prueba.
- Debug draw de colisiones habilitable desde herramientas.

### Archivos a crear
1. src/game/physics/Colliders.h
2. src/game/physics/DebugPhysicsDraw.cpp

### Archivos a modificar
1. src/editor/EditorApp.cpp
2. src/game/physics/PhysicsWorld.cpp

### Criterio de aceptacion
- Colision detectada y visible en debug overlay.

---

## Tarea 9.4 (D83) - Gravedad y colision con plano

### Objetivo
Implementar caida de cubo con respuesta fisica correcta.

### Resultado esperado
- Escena baseline (cubo + suelo) como referencia de simulacion.
- Estabilidad numerica con dt acotado y substeps.

### Archivos a crear
1. scenes/physics_baseline.json

### Archivos a modificar
1. src/game/physics/PhysicsWorld.cpp
2. src/editor/SceneData.cpp

### Criterio de aceptacion
- Cubo cae y se estabiliza sin penetracion persistente.

---

## Tarea 9.5 (D84) - QA de sprint

### Objetivo
Validar determinismo minimo y robustez de simulacion.

### Resultado esperado
- Prueba reproducible por seed/dt fijo.
- Reporte de tolerancias numericas para regresion.

### Archivos a crear
1. tests/test_physics_determinism.cpp
2. planning/tasks/SPRINT9_QA_CHECKLIST.md

### Archivos a modificar
1. tests/CMakeLists.txt
2. README.md

### Criterio de aceptacion
- Test determinista verde en CI local.
