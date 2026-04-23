#pragma once
#include "IsoRenderer.h"
#include "rendering/mesh/TerrainVertex.h"
#include <vector>
#include <cstdint>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// TerrainVertex – one corner of the heightmap grid
// ─────────────────────────────────────────────────────────────────────────────
struct TerrainVertex {
    float height = 0.0f;
    float nx = 0.0f, ny = 1.0f, nz = 0.0f;  // smooth normal (Y-up)
    float ao = 1.0f;                          // ambient occlusion [0.3..1]
};

// ─────────────────────────────────────────────────────────────────────────────
// TerrainFace – one cell of the terrain (replaces old Tile in the mesh)
// ─────────────────────────────────────────────────────────────────────────────
struct TerrainFace {
    TileType type     = TileType::Grass;
    bool     walkable = true;

    SDL_Color topColor()  const;
    SDL_Color sideColor() const;
};

// ─────────────────────────────────────────────────────────────────────────────
// TerrainMesh – heightmap polygon mesh terrain
//
//   Vertex grid: (WORLD_W+1) × (WORLD_H+1) = 257×257 vertices
//   Face grid:   WORLD_W × WORLD_H          = 256×256 faces
//
//   Each face (fx, fy) has corners:
//     TL = vert(fx,   fy  )     TR = vert(fx+1, fy  )
//     BL = vert(fx,   fy+1)     BR = vert(fx+1, fy+1)
//
//   Each face produces 2 triangles for rendering.
// ─────────────────────────────────────────────────────────────────────────────
class TerrainMesh {
public:
    static constexpr int VW = WORLD_W + 1;   // vertex grid width
    static constexpr int VH = WORLD_H + 1;   // vertex grid height
    static constexpr int FW = WORLD_W;        // face grid width
    static constexpr int FH = WORLD_H;        // face grid height

    TerrainMesh();

    // Procedural generation (same algorithm as World::generate)
    void generate(unsigned int seed = 42);

    // ── Accessors ────────────────────────────────────────────────────────────
    TerrainVertex&       vert(int vx, int vy)       { return vertices_[vy * VW + vx]; }
    const TerrainVertex& vert(int vx, int vy) const { return vertices_[vy * VW + vx]; }

    TerrainFace&       face(int fx, int fy)       { return faces_[fy * FW + fx]; }
    const TerrainFace& face(int fx, int fy) const { return faces_[fy * FW + fx]; }

    // ── Derived queries ──────────────────────────────────────────────────────
    float faceAverageHeight(int fx, int fy) const;
    float sampleHeight(float wx, float wy) const;   // bilinear interpolation
    bool  isWalkable(float wx, float wy) const;
    float terrainCost(int tx, int ty) const;

    // ── Vulkan mesh building ────────────────────────────────────────────────
    void buildVulkanMesh(std::vector<dash::vkexp::TerrainVkVertex>& outVerts,
                         std::vector<uint32_t>& outIndices) const;

    // ── Computed vertex data ──────────────────────────────────────────────
    void computeSmoothNormals();
    void computeAmbientOcclusion();

    // ── Dirty tracking ───────────────────────────────────────────────────────
    bool  dirty() const     { return dirty_; }
    void  markDirty()       { dirty_ = true; }
    void  clearDirty()      { dirty_ = false; }

private:
    std::vector<TerrainVertex> vertices_;   // VW * VH
    std::vector<TerrainFace>   faces_;      // FW * FH
    bool dirty_ = true;
};
