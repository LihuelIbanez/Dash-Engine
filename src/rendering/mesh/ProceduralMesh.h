#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "rendering/mesh/Vertex.h"

// ─────────────────────────────────────────────────────────────────────────────
// ProceduralMesh — CPU-only generators for low-poly, faceted props (the
// Valheim silhouette: strong shapes, flat shading, no textures). Deliberately
// free of Vulkan so the whole module is unit-testable; the caller uploads the
// result with MeshBuffers::initFromData.
//
// FACETED BY CONSTRUCTION: every triangle owns its three vertices and carries
// the face normal. Vertices are never shared across faces, because sharing is
// exactly what would average the normals and smooth the shading away.
//
// ── Mesh id scheme ───────────────────────────────────────────────────────────
//   proc:<kind>[?key=value[&key=value...]]
//
//   proc:conifer
//   proc:conifer?seed=42
//   proc:broadleaf?seed=7&height=6.5&part=foliage
//
// Keys: seed (uint32), height (metres, >0), part (all|trunk|foliage).
// Unknown keys are ignored; an unknown kind or a malformed value fails the
// parse so the caller can warn and fall back.
//
// Why this shape:
//  * It is one string, so it drops straight into RenderComponent::mesh with no
//    schema change to scenes, to the SQLite cache or to the editor UI.
//  * The "proc:" prefix cannot collide with a path: a Windows drive letter puts
//    its colon at index 1, never at index 0.
//  * Query syntax is order independent and extensible, and the *whole id* is
//    the cache key, so two entities asking for the same id share one GPU mesh
//    while two seeds are naturally two entries.
//
// ── Colour ───────────────────────────────────────────────────────────────────
// dash::vkexp::Vertex has no colour channel, and adding one would mean a new
// VkVertexInputAttributeDescription in PipelineBuilder for every pipeline that
// consumes it. So colour comes from materials instead: `part` splits a model
// into bark and foliage sub-meshes, and the scene points each at its own
// .mat.json. Note the renderer multiplies MaterialAsset::baseColor by the
// hardcoded per-instance tint from SceneLoader (0.82, 0.34, 0.34 for non-player
// entities), so the shipped material files store pre-divided values.
// ─────────────────────────────────────────────────────────────────────────────

namespace dash::procmesh {

struct MeshData {
    std::vector<dash::vkexp::Vertex> vertices;
    std::vector<uint32_t> indices;
    float minBounds[3] = {0.0f, 0.0f, 0.0f};
    float maxBounds[3] = {0.0f, 0.0f, 0.0f};

    bool empty() const { return vertices.empty() || indices.empty(); }
    float extent(int axis) const { return maxBounds[axis] - minBounds[axis]; }
    std::size_t triangleCount() const { return indices.size() / 3; }
};

enum class ModelKind { Conifer, Broadleaf, Rock, Bush, Grass, Stump, Log };

// Kinds without a bark/leaf split (rock, grass, stump, log) ignore this and
// always return the whole model.
enum class ModelPart { All, Trunk, Foliage };

struct ModelParams {
    ModelKind kind = ModelKind::Conifer;
    uint32_t  seed = 0;
    float     height = 0.0f;   // <= 0 → seeded default for the kind
    ModelPart part = ModelPart::All;
};

bool isProceduralMeshId(const std::string& meshId);
bool parseMeshId(const std::string& meshId, ModelParams& out);

// Deterministic: the same params always yield byte-identical geometry.
MeshData generate(const ModelParams& params);

const char* kindName(ModelKind kind);
const char* partName(ModelPart part);

} // namespace dash::procmesh
