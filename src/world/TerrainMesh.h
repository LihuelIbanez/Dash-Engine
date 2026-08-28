#pragma once
#include "IsoRenderer.h"
#include "rendering/mesh/TerrainVertex.h"
#include <vector>
#include <array>
#include <cstdint>
#include <cmath>

// ─── WC3-style terrain constants ────────────────────────────────────────────
constexpr float CLIFF_STEP         = 12.0f;   // world units per cliff level
constexpr float INTRA_CLIFF_HEIGHT = 8.0f;    // smooth range within one level (matches old heightScale)
constexpr int   MAX_CLIFF_LEVEL    = 15;

// ─────────────────────────────────────────────────────────────────────────────
// WaterBody — configurable water plane
// ─────────────────────────────────────────────────────────────────────────────
struct WaterBody {
    uint8_t id       = 0;
    float   waterLevel = 0.3f;
    float   opacity    = 0.6f;
    std::array<float, 3> tint = {0.08f, 0.14f, 0.31f};
};

// ─────────────────────────────────────────────────────────────────────────────
// TerrainVertex – one corner of the heightmap grid
// ─────────────────────────────────────────────────────────────────────────────
struct TerrainVertex {
    float height = 0.0f;                          // continuous [0,1] within cliff level
    float nx = 0.0f, ny = 1.0f, nz = 0.0f;       // smooth normal (Y-up)
    float ao = 1.0f;                              // ambient occlusion [0.3..1]
    uint8_t cliffLevel = 0;                       // discrete cliff tier 0..15
    uint8_t texIndices[4] = {0, 0, 0, 0};         // terrain texture IDs
    uint8_t texWeights[4] = {255, 0, 0, 0};       // blend weights (sum to 255)
};

// ─────────────────────────────────────────────────────────────────────────────
// TerrainFace – one cell of the terrain (replaces old Tile in the mesh)
// ─────────────────────────────────────────────────────────────────────────────
struct TerrainFace {
    TileType type     = TileType::Grass;
    bool     walkable = true;
    uint8_t  waterRegionId = 0;                   // 0 = no water region

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

    // ── Cliff-aware world height ────────────────────────────────────────────
    float worldHeight(int vx, int vy) const;          // cliff + smooth
    float worldHeight(float wx, float wy) const;      // bilinear, cliff-aware

    // ── Cliff operations ────────────────────────────────────────────────────
    void    setCliffLevel(int vx, int vy, uint8_t level);
    uint8_t cliffLevel(int vx, int vy) const;
    uint8_t faceCliffLevel(int fx, int fy) const;  // min cliff of 4 corners

    // ── Texture painting ────────────────────────────────────────────────────
    void paintTexture(int vx, int vy, TerrainTextureId tex, float weight);

    // ── Water bodies ────────────────────────────────────────────────────────
    void addWaterBody(const WaterBody& body);
    void removeWaterBody(uint8_t id);
    const std::vector<WaterBody>& waterBodies() const { return waterBodies_; }
    std::vector<WaterBody>& waterBodies() { return waterBodies_; }

    // ── Vulkan mesh building ────────────────────────────────────────────────
    void buildVulkanMesh(std::vector<dash::vkexp::TerrainVkVertex>& outVerts,
                         std::vector<uint32_t>& outIndices) const;
    void buildCliffWalls(std::vector<dash::vkexp::TerrainVkVertex>& outVerts,
                         std::vector<uint32_t>& outIndices) const;
    void buildWaterMesh(std::vector<dash::vkexp::TerrainVkVertex>& outVerts,
                        std::vector<uint32_t>& outIndices) const;

    // ── Computed vertex data ──────────────────────────────────────────────
    void computeSmoothNormals();
    void assignTextureLayers();
    void computeAmbientOcclusion();

    // ── Dirty tracking ───────────────────────────────────────────────────────
    bool  dirty() const     { return dirty_; }
    void  markDirty()       { dirty_ = true; }
    void  clearDirty()      { dirty_ = false; }

private:
    std::vector<TerrainVertex> vertices_;   // VW * VH
    std::vector<TerrainFace>   faces_;      // FW * FH
    std::vector<WaterBody>     waterBodies_;
    bool dirty_ = true;
};
