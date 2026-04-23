# Sprint 11 - Importacion de Activos 3D

## Meta del sprint

Cargar modelos 3D complejos y texturas externas (.obj/.gltf) y renderizarlos correctamente en Vulkan.

**Estado: ✅ COMPLETADO**

### Implementacion
- Assimp via vcpkg para .obj/.gltf/.glb
- ModelImporter con salida binaria .dashmesh (DMSH header + interleaved vertex data)
- Vertex extendido: position(3) + normal(3) + texCoord(2) = 32 bytes
- Depth buffer (VK_FORMAT_D32_SFLOAT) en SwapchainContext
- TextureLoader: stb_image → staging buffer → VkImage con transiciones de layout
- Textured pipeline con shaders textured.vert/frag + iluminacion direccional basica
- AssetCache3D: cache por path con ownership de recursos GPU
- MeshBuffers movable + initFromData para meshes importados
- MeshUploader con staging buffer para upload a GPU
- test_model_import_pipeline: 6 tests, 0 fallos

---

## Tarea 11.1 (D97) - Integracion Assimp

### Objetivo
Anadir cargador de modelos para .obj/.gltf.

### Resultado esperado
- ModelImporter modular con salida neutral para runtime.
- Parseo estable de nodos y mallas en formatos objetivo.

### Archivos a crear
1. src/assets/importers/ModelImporter.h
2. src/assets/importers/ModelImporter.cpp

### Archivos a modificar
1. CMakeLists.txt
2. src/assets/

### Criterio de aceptacion
- Se parsea al menos un .obj y un .gltf.

---

## Tarea 11.2 (D98) - Mesh loading a buffers Vulkan

### Objetivo
Convertir nodos/mallas importadas a buffers GPU.

### Resultado esperado
- Layout de Vertex unificado para assets importados.
- Upload por staging buffer con ownership claro.

### Archivos a crear
1. src/rendering/mesh/ImportedMeshUploader.h
2. src/rendering/mesh/ImportedMeshUploader.cpp

### Archivos a modificar
1. src/rendering/mesh/Vertex.h
2. src/rendering/vulkan/

### Criterio de aceptacion
- Malla compleja visible correctamente.

---

## Tarea 11.3 (D99) - Texture mapping (stb_image)

### Objetivo
Cargar texturas y aplicarlas a materiales.

### Resultado esperado
- TextureLoader con transiciones VkImage correctas.
- Material basico con albedo estable en runtime.

### Archivos a crear
1. src/assets/textures/TextureLoader.h
2. src/assets/textures/TextureLoader.cpp

### Archivos a modificar
1. src/rendering/materials/
2. CMakeLists.txt

### Criterio de aceptacion
- Modelo texturizado visible sin artefactos graves.

---

## Tarea 11.4 (D100) - Organizacion scene/model cache

### Objetivo
Evitar recargas redundantes de modelos y texturas.

### Resultado esperado
- Cache por path+hash para recursos importados.
- Invalidador basico ante cambios de archivo.

### Archivos a crear
1. src/assets/cache/AssetCache3D.h
2. src/assets/cache/AssetCache3D.cpp

### Archivos a modificar
1. src/assets/importers/ModelImporter.cpp
2. src/assets/textures/TextureLoader.cpp

### Criterio de aceptacion
- Segunda carga de mismo asset reutiliza recursos.

---

## Tarea 11.5 (D101) - QA de sprint

### Objetivo
Validar robustez del import para assets reales y defectuosos.

### Resultado esperado
- Suite de smoke con casos validos e invalidos.
- Falla controlada sin crash ante archivos corruptos.

### Archivos a crear
1. tests/test_model_import_pipeline.cpp
2. planning/tasks/SPRINT11_QA_CHECKLIST.md

### Archivos a modificar
1. tests/CMakeLists.txt
2. README.md

### Criterio de aceptacion
- Import robusto sin crash ante archivos defectuosos.
