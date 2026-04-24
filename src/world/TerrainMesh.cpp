#include "TerrainMesh.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

// ═════════════════════════════════════════════════════════════════════════════
// Perlin Noise – self-contained 2D implementation (same as World.cpp)
// ═════════════════════════════════════════════════════════════════════════════
namespace {

static int tmPerm_[512];

void tmInitPerm(unsigned int seed)
{
    for (int i = 0; i < 256; ++i) tmPerm_[i] = i;
    unsigned int s = seed;
    for (int i = 255; i > 0; --i) {
        s = s * 1664525u + 1013904223u;
        int j = static_cast<int>((s >> 16) % (i + 1));
        int tmp     = tmPerm_[i];
        tmPerm_[i]  = tmPerm_[j];
        tmPerm_[j]  = tmp;
    }
    for (int i = 0; i < 256; ++i) tmPerm_[256 + i] = tmPerm_[i];
}

inline float tmFade(float t) { return t * t * t * (t * (t * 6.f - 15.f) + 10.f); }
inline float tmLerp(float a, float b, float t) { return a + t * (b - a); }

float tmGrad(int hash, float x, float y)
{
    switch (hash & 3) {
    case 0: return  x + y;
    case 1: return -x + y;
    case 2: return  x - y;
    default: return -x - y;
    }
}

float tmPerlin(float x, float y)
{
    int xi = static_cast<int>(std::floor(x)) & 255;
    int yi = static_cast<int>(std::floor(y)) & 255;
    float xf = x - std::floor(x);
    float yf = y - std::floor(y);
    float u = tmFade(xf);
    float v = tmFade(yf);
    int aa = tmPerm_[tmPerm_[xi]     + yi];
    int ab = tmPerm_[tmPerm_[xi]     + yi + 1];
    int ba = tmPerm_[tmPerm_[xi + 1] + yi];
    int bb = tmPerm_[tmPerm_[xi + 1] + yi + 1];
    float x1 = tmLerp(tmGrad(aa, xf,       yf),
                       tmGrad(ba, xf - 1.f, yf),       u);
    float x2 = tmLerp(tmGrad(ab, xf,       yf - 1.f),
                       tmGrad(bb, xf - 1.f, yf - 1.f), u);
    return tmLerp(x1, x2, v);
}

float tmFbm(float x, float y, int octaves, float lacunarity, float gain)
{
    float sum = 0.f, amp = 1.f, freq = 1.f, maxAmp = 0.f;
    for (int i = 0; i < octaves; ++i) {
        sum    += tmPerlin(x * freq, y * freq) * amp;
        maxAmp += amp;
        amp    *= gain;
        freq   *= lacunarity;
    }
    return sum / maxAmp;
}

inline float tmNorm01(float v) { return (v + 1.f) * 0.5f; }

float tmIslandMask(float x, float y, int w, int h)
{
    float nx = 2.f * x / (w - 1) - 1.f;
    float ny = 2.f * y / (h - 1) - 1.f;
    float d  = std::max(std::abs(nx), std::abs(ny));
    float t  = std::max(0.f, (d - 0.55f) / 0.45f);
    return 1.f - t * t;
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════════
// TerrainFace colours – same palette as Tile
// ═════════════════════════════════════════════════════════════════════════════
SDL_Color TerrainFace::topColor() const
{
    switch (type) {
    case TileType::DeepWater: return {  15,  40, 100, 255 };
    case TileType::Water:     return {  30,  70, 140, 255 };
    case TileType::Sand:      return { 180, 160,  90, 255 };
    case TileType::Grass:     return {  60, 120,  40, 255 };
    case TileType::Forest:    return {  30,  75,  25, 255 };
    case TileType::Dirt:      return { 110,  75,  40, 255 };
    case TileType::Stone:     return { 120, 115, 105, 255 };
    case TileType::Mountain:  return {  95,  85,  80, 255 };
    case TileType::Snow:      return { 220, 225, 235, 255 };
    }
    return { 60, 120, 40, 255 };
}

SDL_Color TerrainFace::sideColor() const
{
    SDL_Color t = topColor();
    return colDarken(t, 45);
}

// ═════════════════════════════════════════════════════════════════════════════
// TerrainMesh
// ═════════════════════════════════════════════════════════════════════════════
TerrainMesh::TerrainMesh()
{
    vertices_.resize(VW * VH);
    faces_.resize(FW * FH);
}

void TerrainMesh::generate(unsigned int seed)
{
    const float scale = 0.07f;

    // ── Phase 1: per-vertex elevation ────────────────────────────────────────
    tmInitPerm(seed);

    for (int vy = 0; vy < VH; ++vy) {
        for (int vx = 0; vx < VW; ++vx) {
            float e = tmFbm(vx * scale, vy * scale, 6, 2.0f, 0.5f);
            e = tmNorm01(e);
            e *= tmIslandMask(static_cast<float>(vx), static_cast<float>(vy), VW, VH);
            vert(vx, vy).height = e;
        }
    }

    // ── Phase 2: per-face moisture + biome assignment ────────────────────────
    tmInitPerm(seed + 31337u);
    std::vector<float> moist(FW * FH);
    for (int fy = 0; fy < FH; ++fy) {
        for (int fx = 0; fx < FW; ++fx) {
            float cx = fx + 0.5f, cy = fy + 0.5f;
            float m = tmFbm(cx * 0.09f + 100.f, cy * 0.09f + 100.f, 4, 2.0f, 0.5f);
            moist[fy * FW + fx] = tmNorm01(m);
        }
    }

    tmInitPerm(seed + 77777u);

    for (int fy = 0; fy < FH; ++fy) {
        for (int fx = 0; fx < FW; ++fx) {
            float e = faceAverageHeight(fx, fy);
            float m = moist[fy * FW + fx];
            float cx = fx + 0.5f, cy = fy + 0.5f;
            float detail = tmNorm01(tmPerlin(cx * 0.25f, cy * 0.25f));

            TerrainFace& f = face(fx, fy);
            f.walkable = true;

            // Water border
            if (fx == 0 || fx == FW - 1 || fy == 0 || fy == FH - 1) {
                f.type = TileType::Water;
                f.walkable = false;
                continue;
            }

            // Elevation thresholds (same as World::generate)
            if (e < 0.22f) {
                f.type = TileType::DeepWater;
                f.walkable = false;
            } else if (e < 0.32f) {
                f.type = TileType::Water;
                f.walkable = false;
            } else if (e < 0.37f) {
                f.type = TileType::Sand;
            } else if (e < 0.60f) {
                if (m > 0.60f)      f.type = TileType::Forest;
                else if (m > 0.40f) f.type = TileType::Grass;
                else if (m > 0.25f) f.type = (detail > 0.5f) ? TileType::Dirt : TileType::Grass;
                else                f.type = (detail > 0.4f) ? TileType::Sand : TileType::Dirt;
            } else if (e < 0.72f) {
                if (m > 0.55f)      f.type = TileType::Forest;
                else if (m > 0.35f) f.type = (detail > 0.6f) ? TileType::Dirt : TileType::Grass;
                else                f.type = TileType::Stone;
            } else if (e < 0.82f) {
                f.type = TileType::Mountain;
                f.walkable = (detail > 0.4f);
            } else if (e < 0.88f) {
                f.type = TileType::Stone;
                f.walkable = false;
            } else {
                f.type = TileType::Snow;
                f.walkable = false;
            }
        }
    }

    // ── Phase 3: post-processing ─────────────────────────────────────────────
    // Remove isolated water/land
    for (int pass = 0; pass < 2; ++pass) {
        for (int fy = 1; fy < FH - 1; ++fy) {
            for (int fx = 1; fx < FW - 1; ++fx) {
                bool isWater = (face(fx, fy).type == TileType::Water ||
                                face(fx, fy).type == TileType::DeepWater);
                int wn = 0;
                if (face(fx, fy-1).type == TileType::Water ||
                    face(fx, fy-1).type == TileType::DeepWater) ++wn;
                if (face(fx, fy+1).type == TileType::Water ||
                    face(fx, fy+1).type == TileType::DeepWater) ++wn;
                if (face(fx-1, fy).type == TileType::Water ||
                    face(fx-1, fy).type == TileType::DeepWater) ++wn;
                if (face(fx+1, fy).type == TileType::Water ||
                    face(fx+1, fy).type == TileType::DeepWater) ++wn;

                if (isWater && wn <= 1) {
                    face(fx, fy).type = TileType::Sand;
                    face(fx, fy).walkable = true;
                }
                if (!isWater && wn >= 3) {
                    face(fx, fy).type = TileType::Water;
                    face(fx, fy).walkable = false;
                }
            }
        }
    }

    // Ensure sand strip along coastlines
    for (int fy = 1; fy < FH - 1; ++fy) {
        for (int fx = 1; fx < FW - 1; ++fx) {
            if (face(fx, fy).type != TileType::Water &&
                face(fx, fy).type != TileType::DeepWater &&
                face(fx, fy).type != TileType::Sand)
            {
                bool nearWater = false;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        if (face(fx+dx, fy+dy).type == TileType::Water ||
                            face(fx+dx, fy+dy).type == TileType::DeepWater)
                            nearWater = true;
                if (nearWater)
                    face(fx, fy).type = TileType::Sand;
            }
        }
    }

    computeSmoothNormals();
    computeAmbientOcclusion();

    dirty_ = true;
}

// ═════════════════════════════════════════════════════════════════════════════
// Smooth normals & AO
// ═════════════════════════════════════════════════════════════════════════════
void TerrainMesh::computeSmoothNormals()
{
    // Zero all vertex normals
    for (int i = 0; i < VW * VH; ++i) {
        vertices_[i].nx = 0.0f;
        vertices_[i].ny = 0.0f;
        vertices_[i].nz = 0.0f;
    }

    // Accumulate face normals to corner vertices using cliff-aware heights
    for (int fy = 0; fy < FH; ++fy) {
        for (int fx = 0; fx < FW; ++fx) {
            float x0 = static_cast<float>(fx)     * TILE_SCALE;
            float x1 = static_cast<float>(fx + 1) * TILE_SCALE;
            float z0 = static_cast<float>(fy)     * TILE_SCALE;
            float z1 = static_cast<float>(fy + 1) * TILE_SCALE;
            float h00 = worldHeight(fx,   fy  );
            float h10 = worldHeight(fx+1, fy  );
            float h01 = worldHeight(fx,   fy+1);

            // Edge vectors
            float e1x = x1 - x0, e1y = h10 - h00, e1z = 0.0f;
            float e2x = 0.0f,    e2y = h01 - h00, e2z = z1 - z0;

            // Cross product (negated for Y-up convention)
            float fnx = -(e1y * e2z - e1z * e2y);
            float fny = -(e1z * e2x - e1x * e2z);
            float fnz = -(e1x * e2y - e1y * e2x);

            // Accumulate to 4 corners
            auto accum = [&](int vx, int vy) {
                auto& v = vert(vx, vy);
                v.nx += fnx; v.ny += fny; v.nz += fnz;
            };
            accum(fx,   fy);
            accum(fx+1, fy);
            accum(fx,   fy+1);
            accum(fx+1, fy+1);
        }
    }

    // Normalize
    for (int i = 0; i < VW * VH; ++i) {
        auto& v = vertices_[i];
        float len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
        if (len > 1e-6f) {
            v.nx /= len; v.ny /= len; v.nz /= len;
        } else {
            v.nx = 0.0f; v.ny = 1.0f; v.nz = 0.0f;
        }
    }
}

void TerrainMesh::computeAmbientOcclusion()
{
    constexpr int radius = 3;
    for (int vy = 0; vy < VH; ++vy) {
        for (int vx = 0; vx < VW; ++vx) {
            float myH = vert(vx, vy).height;
            float higher = 0.0f;
            int samples = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (dx == 0 && dy == 0) continue;
                    int sx = std::max(0, std::min(VW - 1, vx + dx));
                    int sy = std::max(0, std::min(VH - 1, vy + dy));
                    float diff = vert(sx, sy).height - myH;
                    if (diff > 0.0f) higher += diff;
                    ++samples;
                }
            }
            float occlusion = higher / (samples * 0.15f);
            vert(vx, vy).ao = std::max(0.3f, std::min(1.0f, 1.0f - occlusion));
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Queries
// ═════════════════════════════════════════════════════════════════════════════
float TerrainMesh::faceAverageHeight(int fx, int fy) const
{
    return (vert(fx, fy).height + vert(fx+1, fy).height +
            vert(fx, fy+1).height + vert(fx+1, fy+1).height) * 0.25f;
}

// ── Cliff-aware world height ────────────────────────────────────────────────
float TerrainMesh::worldHeight(int vx, int vy) const
{
    const auto& v = vert(vx, vy);
    return v.cliffLevel * CLIFF_STEP + v.height * INTRA_CLIFF_HEIGHT;
}

float TerrainMesh::worldHeight(float wx, float wy) const
{
    int ix = static_cast<int>(std::floor(wx));
    int iy = static_cast<int>(std::floor(wy));
    ix = std::max(0, std::min(ix, FW - 1));
    iy = std::max(0, std::min(iy, FH - 1));

    float fx = wx - ix;
    float fy = wy - iy;
    fx = std::max(0.f, std::min(1.f, fx));
    fy = std::max(0.f, std::min(1.f, fy));

    float h00 = worldHeight(ix,     iy    );
    float h10 = worldHeight(ix + 1, iy    );
    float h01 = worldHeight(ix,     iy + 1);
    float h11 = worldHeight(ix + 1, iy + 1);

    float h0 = h00 + (h10 - h00) * fx;
    float h1 = h01 + (h11 - h01) * fx;
    return h0 + (h1 - h0) * fy;
}

void TerrainMesh::setCliffLevel(int vx, int vy, uint8_t level)
{
    if (vx < 0 || vx >= VW || vy < 0 || vy >= VH) return;
    vert(vx, vy).cliffLevel = std::min(level, static_cast<uint8_t>(MAX_CLIFF_LEVEL));
    dirty_ = true;
}

uint8_t TerrainMesh::cliffLevel(int vx, int vy) const
{
    if (vx < 0 || vx >= VW || vy < 0 || vy >= VH) return 0;
    return vert(vx, vy).cliffLevel;
}

uint8_t TerrainMesh::faceCliffLevel(int fx, int fy) const
{
    if (fx < 0 || fx >= FW || fy < 0 || fy >= FH) return 0;
    uint8_t a = vert(fx,     fy    ).cliffLevel;
    uint8_t b = vert(fx + 1, fy    ).cliffLevel;
    uint8_t c = vert(fx,     fy + 1).cliffLevel;
    uint8_t d = vert(fx + 1, fy + 1).cliffLevel;
    return std::min({a, b, c, d});
}

// ── Texture painting ────────────────────────────────────────────────────────
void TerrainMesh::paintTexture(int vx, int vy, TerrainTextureId tex, float weight)
{
    if (vx < 0 || vx >= VW || vy < 0 || vy >= VH) return;
    auto& v = vert(vx, vy);
    uint8_t texId = static_cast<uint8_t>(tex);
    int addW = static_cast<int>(weight * 255.0f);
    if (addW <= 0) return;

    // Find if texture already exists in a slot
    int existingSlot = -1;
    int minSlot = 0;
    uint8_t minWeight = v.texWeights[0];
    for (int i = 0; i < 4; ++i) {
        if (v.texIndices[i] == texId) { existingSlot = i; break; }
        if (v.texWeights[i] < minWeight) { minWeight = v.texWeights[i]; minSlot = i; }
    }

    if (existingSlot >= 0) {
        // Increase existing slot
        int cur = v.texWeights[existingSlot];
        v.texWeights[existingSlot] = static_cast<uint8_t>(std::min(255, cur + addW));
    } else {
        // Replace lowest-weight slot
        v.texIndices[minSlot] = texId;
        v.texWeights[minSlot] = static_cast<uint8_t>(std::min(255, addW));
    }

    // Renormalize weights to sum to 255
    int total = 0;
    for (int i = 0; i < 4; ++i) total += v.texWeights[i];
    if (total > 0 && total != 255) {
        float scale = 255.0f / total;
        int sum = 0;
        for (int i = 0; i < 3; ++i) {
            v.texWeights[i] = static_cast<uint8_t>(v.texWeights[i] * scale);
            sum += v.texWeights[i];
        }
        v.texWeights[3] = static_cast<uint8_t>(255 - sum);
    }
    dirty_ = true;
}

// ── Water bodies ────────────────────────────────────────────────────────────
void TerrainMesh::addWaterBody(const WaterBody& body)
{
    // Replace if same ID exists
    for (auto& wb : waterBodies_) {
        if (wb.id == body.id) { wb = body; dirty_ = true; return; }
    }
    waterBodies_.push_back(body);
    dirty_ = true;
}

void TerrainMesh::removeWaterBody(uint8_t id)
{
    waterBodies_.erase(
        std::remove_if(waterBodies_.begin(), waterBodies_.end(),
                        [id](const WaterBody& wb) { return wb.id == id; }),
        waterBodies_.end());
    dirty_ = true;
}

float TerrainMesh::sampleHeight(float wx, float wy) const
{
    // Cliff-aware bilinear interpolation via worldHeight()
    return worldHeight(wx, wy);
}

bool TerrainMesh::isWalkable(float wx, float wy) const
{
    int fx = static_cast<int>(wx);
    int fy = static_cast<int>(wy);
    if (fx < 0 || fx >= FW || fy < 0 || fy >= FH) return false;
    return face(fx, fy).walkable;
}

float TerrainMesh::terrainCost(int tx, int ty) const
{
    if (tx < 0 || tx >= FW || ty < 0 || ty >= FH) return 1e6f;
    const TerrainFace& f = face(tx, ty);
    if (!f.walkable) return 1e6f;

    // Block faces where corners span multiple cliff levels
    uint8_t a = vert(tx,     ty    ).cliffLevel;
    uint8_t b = vert(tx + 1, ty    ).cliffLevel;
    uint8_t c = vert(tx,     ty + 1).cliffLevel;
    uint8_t d = vert(tx + 1, ty + 1).cliffLevel;
    if (a != b || a != c || a != d) return 1e6f;

    switch (f.type) {
    case TileType::Grass:    return 1.0f;
    case TileType::Dirt:     return 1.0f;
    case TileType::Sand:     return 1.3f;
    case TileType::Forest:   return 1.5f;
    case TileType::Stone:    return 1.1f;
    case TileType::Mountain: return 2.0f;
    case TileType::Snow:     return 1.8f;
    default:                 return 1e6f;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Vulkan mesh building
// ═════════════════════════════════════════════════════════════════════════════

// Helper: pack 4 uint8 values into a uint32
static uint32_t packU8x4(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    return static_cast<uint32_t>(a)
         | (static_cast<uint32_t>(b) << 8)
         | (static_cast<uint32_t>(c) << 16)
         | (static_cast<uint32_t>(d) << 24);
}

void TerrainMesh::buildVulkanMesh(
    std::vector<dash::vkexp::TerrainVkVertex>& outVerts,
    std::vector<uint32_t>& outIndices) const
{
    const int totalFaces = FW * FH;
    outVerts.clear();
    outVerts.reserve(totalFaces * 4);
    outIndices.clear();
    outIndices.reserve(totalFaces * 6);

    // Helper: compute per-vertex blended color from up to 4 adjacent faces
    auto vertexColor = [&](int vx, int vy) -> std::array<float, 3> {
        float r = 0, g = 0, b = 0;
        int count = 0;
        for (int dy = -1; dy <= 0; ++dy) {
            for (int dx = -1; dx <= 0; ++dx) {
                int ffx = vx + dx, ffy = vy + dy;
                if (ffx >= 0 && ffx < FW && ffy >= 0 && ffy < FH) {
                    SDL_Color c = face(ffx, ffy).topColor();
                    r += c.r; g += c.g; b += c.b;
                    ++count;
                }
            }
        }
        if (count == 0) return {0.5f, 0.5f, 0.5f};
        return { r / (count * 255.0f), g / (count * 255.0f), b / (count * 255.0f) };
    };

    // Helper: apply slope rock tint to a vertex color (AO left to the shader)
    auto applyVertexEffects = [&](std::array<float, 3> col, int vx, int vy) -> std::array<float, 3> {
        const auto& v = vert(vx, vy);

        // Slope-based rock tint: steep slopes blend toward rock color
        float slopeFactor = 1.0f - v.ny;
        float rockBlend = std::max(0.0f, std::min(1.0f, slopeFactor * 3.0f - 0.3f));
        col[0] = col[0] * (1.0f - rockBlend) + 0.45f * rockBlend;
        col[1] = col[1] * (1.0f - rockBlend) + 0.40f * rockBlend;
        col[2] = col[2] * (1.0f - rockBlend) + 0.35f * rockBlend;

        return col;
    };

    // Helper: pack texture data for a vertex
    auto packTexData = [&](int vx, int vy, uint32_t& outIndicesPacked, uint32_t& outWeightsPacked) {
        const auto& v = vert(vx, vy);
        outIndicesPacked = packU8x4(v.texIndices[0], v.texIndices[1], v.texIndices[2], v.texIndices[3]);
        outWeightsPacked = packU8x4(v.texWeights[0], v.texWeights[1], v.texWeights[2], v.texWeights[3]);
    };

    for (int fy = 0; fy < FH; ++fy) {
        for (int fx = 0; fx < FW; ++fx) {
            // Use cliff-aware world height
            float h00 = worldHeight(fx,     fy    );
            float h10 = worldHeight(fx + 1, fy    );
            float h01 = worldHeight(fx,     fy + 1);
            float h11 = worldHeight(fx + 1, fy + 1);

            std::array<float, 3> p00 = { static_cast<float>(fx)     * TILE_SCALE, h00, static_cast<float>(fy)     * TILE_SCALE };
            std::array<float, 3> p10 = { static_cast<float>(fx + 1) * TILE_SCALE, h10, static_cast<float>(fy)     * TILE_SCALE };
            std::array<float, 3> p01 = { static_cast<float>(fx)     * TILE_SCALE, h01, static_cast<float>(fy + 1) * TILE_SCALE };
            std::array<float, 3> p11 = { static_cast<float>(fx + 1) * TILE_SCALE, h11, static_cast<float>(fy + 1) * TILE_SCALE };

            // Smooth per-vertex normals
            std::array<float, 3> n00 = { vert(fx,   fy  ).nx, vert(fx,   fy  ).ny, vert(fx,   fy  ).nz };
            std::array<float, 3> n10 = { vert(fx+1, fy  ).nx, vert(fx+1, fy  ).ny, vert(fx+1, fy  ).nz };
            std::array<float, 3> n01 = { vert(fx,   fy+1).nx, vert(fx,   fy+1).ny, vert(fx,   fy+1).nz };
            std::array<float, 3> n11 = { vert(fx+1, fy+1).nx, vert(fx+1, fy+1).ny, vert(fx+1, fy+1).nz };

            // Per-vertex blended color with slope tint and AO
            auto c00 = applyVertexEffects(vertexColor(fx,   fy  ), fx,   fy  );
            auto c10 = applyVertexEffects(vertexColor(fx+1, fy  ), fx+1, fy  );
            auto c01 = applyVertexEffects(vertexColor(fx,   fy+1), fx,   fy+1);
            auto c11 = applyVertexEffects(vertexColor(fx+1, fy+1), fx+1, fy+1);

            // Pack texture blend data
            uint32_t ti00, tw00, ti10, tw10, ti01, tw01, ti11, tw11;
            packTexData(fx,   fy,   ti00, tw00);
            packTexData(fx+1, fy,   ti10, tw10);
            packTexData(fx,   fy+1, ti01, tw01);
            packTexData(fx+1, fy+1, ti11, tw11);

            uint32_t baseIdx = static_cast<uint32_t>(outVerts.size());
            outVerts.push_back({ p00, n00, c00, ti00, tw00, 0, 0 });
            outVerts.push_back({ p10, n10, c10, ti10, tw10, 0, 0 });
            outVerts.push_back({ p01, n01, c01, ti01, tw01, 0, 0 });
            outVerts.push_back({ p11, n11, c11, ti11, tw11, 0, 0 });

            // Triangle 1: TL, TR, BL
            outIndices.push_back(baseIdx + 0);
            outIndices.push_back(baseIdx + 1);
            outIndices.push_back(baseIdx + 2);
            // Triangle 2: TR, BR, BL
            outIndices.push_back(baseIdx + 1);
            outIndices.push_back(baseIdx + 3);
            outIndices.push_back(baseIdx + 2);
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Cliff wall generation — vertical quads between differing cliff levels
// ═════════════════════════════════════════════════════════════════════════════
void TerrainMesh::buildCliffWalls(
    std::vector<dash::vkexp::TerrainVkVertex>& outVerts,
    std::vector<uint32_t>& outIndices) const
{
    // Rock color for cliff walls
    const std::array<float, 3> rockCol = {0.45f, 0.40f, 0.35f};
    const uint32_t rockTexIdx = packU8x4(
        static_cast<uint8_t>(TerrainTextureId::Rock), 0, 0, 0);
    const uint32_t rockTexWt = packU8x4(255, 0, 0, 0);

    auto addCliffQuad = [&](float x0, float z0, float topY0, float botY0,
                            float x1, float z1, float topY1, float botY1,
                            float nx, float nz) {
        // Wall normal (horizontal, pointing outward)
        std::array<float, 3> n = {nx, 0.0f, nz};
        uint32_t baseIdx = static_cast<uint32_t>(outVerts.size());

        // Four corners: topLeft, topRight, botLeft, botRight
        outVerts.push_back({{x0, topY0, z0}, n, rockCol, rockTexIdx, rockTexWt, 1, 0});
        outVerts.push_back({{x1, topY1, z1}, n, rockCol, rockTexIdx, rockTexWt, 1, 0});
        outVerts.push_back({{x0, botY0, z0}, n, rockCol, rockTexIdx, rockTexWt, 1, 0});
        outVerts.push_back({{x1, botY1, z1}, n, rockCol, rockTexIdx, rockTexWt, 1, 0});

        outIndices.push_back(baseIdx + 0);
        outIndices.push_back(baseIdx + 1);
        outIndices.push_back(baseIdx + 2);
        outIndices.push_back(baseIdx + 1);
        outIndices.push_back(baseIdx + 3);
        outIndices.push_back(baseIdx + 2);
    };

    // Check horizontal edges (between vx,vy and vx+1,vy)
    for (int vy = 0; vy < VH; ++vy) {
        for (int vx = 0; vx < VW - 1; ++vx) {
            int clA = vert(vx, vy).cliffLevel;
            int clB = vert(vx + 1, vy).cliffLevel;
            if (clA == clB) continue;

            float xA = vx * TILE_SCALE;
            float xB = (vx + 1) * TILE_SCALE;
            float z  = vy * TILE_SCALE;

            float hA = worldHeight(vx, vy);
            float hB = worldHeight(vx + 1, vy);

            if (clA > clB) {
                // Wall faces +Z direction from B side looking at A
                float botY = clB * CLIFF_STEP + vert(vx + 1, vy).height * INTRA_CLIFF_HEIGHT;
                addCliffQuad(xA, z, hA, botY, xB, z, hB, botY, 0.0f, -1.0f);
            } else {
                float botY = clA * CLIFF_STEP + vert(vx, vy).height * INTRA_CLIFF_HEIGHT;
                addCliffQuad(xA, z, hA, botY, xB, z, hB, botY, 0.0f, 1.0f);
            }
        }
    }

    // Check vertical edges (between vx,vy and vx,vy+1)
    for (int vy = 0; vy < VH - 1; ++vy) {
        for (int vx = 0; vx < VW; ++vx) {
            int clA = vert(vx, vy).cliffLevel;
            int clB = vert(vx, vy + 1).cliffLevel;
            if (clA == clB) continue;

            float x  = vx * TILE_SCALE;
            float zA = vy * TILE_SCALE;
            float zB = (vy + 1) * TILE_SCALE;

            float hA = worldHeight(vx, vy);
            float hB = worldHeight(vx, vy + 1);

            if (clA > clB) {
                float botY = clB * CLIFF_STEP + vert(vx, vy + 1).height * INTRA_CLIFF_HEIGHT;
                addCliffQuad(x, zA, hA, botY, x, zB, hB, botY, -1.0f, 0.0f);
            } else {
                float botY = clA * CLIFF_STEP + vert(vx, vy).height * INTRA_CLIFF_HEIGHT;
                addCliffQuad(x, zA, hA, botY, x, zB, hB, botY, 1.0f, 0.0f);
            }
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Water mesh — flat planes at water body levels
// ═════════════════════════════════════════════════════════════════════════════
void TerrainMesh::buildWaterMesh(
    std::vector<dash::vkexp::TerrainVkVertex>& outVerts,
    std::vector<uint32_t>& outIndices) const
{
    for (const auto& wb : waterBodies_) {
        float wl = wb.waterLevel;
        std::array<float, 3> waterCol = {wb.tint[0], wb.tint[1], wb.tint[2]};
        std::array<float, 3> waterNorm = {0.0f, 1.0f, 0.0f};
        uint32_t waterTexIdx = packU8x4(0, 0, 0, 0);
        uint32_t waterTexWt  = packU8x4(255, 0, 0, 0);
        // flags bit 1 = water (value 2)
        uint16_t waterFlags = 2;

        for (int fy = 0; fy < FH; ++fy) {
            for (int fx = 0; fx < FW; ++fx) {
                // Check if any corner is below water level
                float h00 = worldHeight(fx,     fy    );
                float h10 = worldHeight(fx + 1, fy    );
                float h01 = worldHeight(fx,     fy + 1);
                float h11 = worldHeight(fx + 1, fy + 1);

                if (h00 >= wl && h10 >= wl && h01 >= wl && h11 >= wl) continue;

                float x0 = fx * TILE_SCALE;
                float x1 = (fx + 1) * TILE_SCALE;
                float z0 = fy * TILE_SCALE;
                float z1 = (fy + 1) * TILE_SCALE;

                uint32_t baseIdx = static_cast<uint32_t>(outVerts.size());
                outVerts.push_back({{x0, wl, z0}, waterNorm, waterCol, waterTexIdx, waterTexWt, waterFlags, 0});
                outVerts.push_back({{x1, wl, z0}, waterNorm, waterCol, waterTexIdx, waterTexWt, waterFlags, 0});
                outVerts.push_back({{x0, wl, z1}, waterNorm, waterCol, waterTexIdx, waterTexWt, waterFlags, 0});
                outVerts.push_back({{x1, wl, z1}, waterNorm, waterCol, waterTexIdx, waterTexWt, waterFlags, 0});

                outIndices.push_back(baseIdx + 0);
                outIndices.push_back(baseIdx + 1);
                outIndices.push_back(baseIdx + 2);
                outIndices.push_back(baseIdx + 1);
                outIndices.push_back(baseIdx + 3);
                outIndices.push_back(baseIdx + 2);
            }
        }
    }
}
