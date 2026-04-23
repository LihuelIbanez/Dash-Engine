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

    const float hs = 8.0f * TILE_SCALE;  // match buildVulkanMesh heightScale * TILE_SCALE

    // Accumulate face normals to corner vertices
    for (int fy = 0; fy < FH; ++fy) {
        for (int fx = 0; fx < FW; ++fx) {
            float x0 = static_cast<float>(fx)     * TILE_SCALE;
            float x1 = static_cast<float>(fx + 1) * TILE_SCALE;
            float z0 = static_cast<float>(fy)     * TILE_SCALE;
            float z1 = static_cast<float>(fy + 1) * TILE_SCALE;
            float h00 = vert(fx,   fy  ).height * hs;
            float h10 = vert(fx+1, fy  ).height * hs;
            float h01 = vert(fx,   fy+1).height * hs;

            // Edge vectors
            float e1x = x1 - x0, e1y = h10 - h00, e1z = 0.0f;
            float e2x = 0.0f,    e2y = h01 - h00, e2z = z1 - z0;

            // Cross product
            float fnx = e1y * e2z - e1z * e2y;
            float fny = e1z * e2x - e1x * e2z;
            float fnz = e1x * e2y - e1y * e2x;

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

float TerrainMesh::sampleHeight(float wx, float wy) const
{
    // Bilinear interpolation of vertex heights
    int ix = static_cast<int>(std::floor(wx));
    int iy = static_cast<int>(std::floor(wy));
    ix = std::max(0, std::min(ix, FW - 1));
    iy = std::max(0, std::min(iy, FH - 1));

    float fx = wx - ix;
    float fy = wy - iy;
    fx = std::max(0.f, std::min(1.f, fx));
    fy = std::max(0.f, std::min(1.f, fy));

    float h00 = vert(ix,     iy    ).height;
    float h10 = vert(ix + 1, iy    ).height;
    float h01 = vert(ix,     iy + 1).height;
    float h11 = vert(ix + 1, iy + 1).height;

    float h0 = h00 + (h10 - h00) * fx;
    float h1 = h01 + (h11 - h01) * fx;
    return h0 + (h1 - h0) * fy;
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
void TerrainMesh::buildVulkanMesh(
    std::vector<dash::vkexp::TerrainVkVertex>& outVerts,
    std::vector<uint32_t>& outIndices) const
{
    const int totalFaces = FW * FH;
    outVerts.clear();
    outVerts.reserve(totalFaces * 4);
    outIndices.clear();
    outIndices.reserve(totalFaces * 6);

    constexpr float heightScale = 8.0f;

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

    // Helper: apply slope rock tint and AO to a vertex color
    auto applyVertexEffects = [&](std::array<float, 3> col, int vx, int vy) -> std::array<float, 3> {
        const auto& v = vert(vx, vy);

        // Slope-based rock tint: steep slopes blend toward rock color
        float slopeFactor = 1.0f - v.ny;
        float rockBlend = std::max(0.0f, std::min(1.0f, slopeFactor * 3.0f - 0.3f));
        col[0] = col[0] * (1.0f - rockBlend) + 0.45f * rockBlend;
        col[1] = col[1] * (1.0f - rockBlend) + 0.40f * rockBlend;
        col[2] = col[2] * (1.0f - rockBlend) + 0.35f * rockBlend;

        // Ambient occlusion
        col[0] *= v.ao;
        col[1] *= v.ao;
        col[2] *= v.ao;

        return col;
    };

    for (int fy = 0; fy < FH; ++fy) {
        for (int fx = 0; fx < FW; ++fx) {
            float h00 = vert(fx,     fy    ).height * heightScale;
            float h10 = vert(fx + 1, fy    ).height * heightScale;
            float h01 = vert(fx,     fy + 1).height * heightScale;
            float h11 = vert(fx + 1, fy + 1).height * heightScale;

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

            uint32_t baseIdx = static_cast<uint32_t>(outVerts.size());
            outVerts.push_back({ p00, n00, c00 });
            outVerts.push_back({ p10, n10, c10 });
            outVerts.push_back({ p01, n01, c01 });
            outVerts.push_back({ p11, n11, c11 });

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
