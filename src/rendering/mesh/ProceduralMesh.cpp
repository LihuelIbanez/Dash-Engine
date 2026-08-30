#include "rendering/mesh/ProceduralMesh.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <map>
#include <utility>

namespace dash::procmesh {
namespace {

using dash::vkexp::Vertex;

constexpr float kPi = 3.14159265358979323846f;

// ── Vector helpers ───────────────────────────────────────────────────────────
struct V3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

V3 operator+(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(const V3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }

V3 cross(const V3& a, const V3& b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

float dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
float length(const V3& a) { return std::sqrt(dot(a, a)); }

V3 normalize(const V3& a)
{
    const float len = length(a);
    return len > 1e-8f ? a * (1.0f / len) : V3{0.0f, 1.0f, 0.0f};
}

// ── Deterministic RNG ────────────────────────────────────────────────────────
// xorshift32 seeded through splitmix32 so consecutive seeds (1, 2, 3...) still
// produce uncorrelated streams. std::random is avoided on purpose: its
// distributions are implementation defined and would break reproducibility
// across toolchains.
uint32_t splitmix32(uint32_t x)
{
    x += 0x9E3779B9u;
    x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
    x = (x ^ (x >> 13)) * 0xC2B2AE35u;
    return x ^ (x >> 16);
}

class Rng {
public:
    explicit Rng(uint32_t seed) : s_(splitmix32(seed) | 1u) {}

    uint32_t nextU32()
    {
        s_ ^= s_ << 13;
        s_ ^= s_ >> 17;
        s_ ^= s_ << 5;
        return s_;
    }
    float unit() { return static_cast<float>(nextU32() >> 8) * (1.0f / 16777216.0f); }
    float range(float a, float b) { return a + (b - a) * unit(); }
    int rangeI(int a, int b)
    {
        return a + static_cast<int>(nextU32() % static_cast<uint32_t>(b - a + 1));
    }

private:
    uint32_t s_;
};

// Stable value hash of a direction, so vertices shared by several faces get the
// same displacement and the surface stays watertight.
float hashDir01(const V3& d, uint32_t seed)
{
    const auto q = [](float v) { return static_cast<uint32_t>(static_cast<int32_t>(std::lround(v * 2048.0f))); };
    uint32_t h = splitmix32(seed ^ 0x27D4EB2Fu);
    h = splitmix32(h ^ q(d.x));
    h = splitmix32(h ^ (q(d.y) * 0x9E3779B9u));
    h = splitmix32(h ^ (q(d.z) * 0x85EBCA6Bu));
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

// ── Emission ─────────────────────────────────────────────────────────────────
std::array<float, 2> planarUv(const V3& p, const V3& n)
{
    const float ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
    if (ay >= ax && ay >= az) return {p.x * 0.5f, p.z * 0.5f};
    if (ax >= az) return {p.z * 0.5f, p.y * 0.5f};
    return {p.x * 0.5f, p.y * 0.5f};
}

// One triangle, three private vertices, one face normal. Never share vertices
// here: that is what keeps the shading faceted.
void tri(MeshData& m, const V3& a, const V3& b, const V3& c)
{
    const V3 raw = cross(b - a, c - a);
    if (length(raw) < 1e-9f) return;   // degenerate, skip

    const V3 n = normalize(raw);
    const uint32_t base = static_cast<uint32_t>(m.vertices.size());
    const V3 pts[3] = {a, b, c};
    for (const V3& p : pts) {
        Vertex v;
        v.position = {p.x, p.y, p.z};
        v.normal = {n.x, n.y, n.z};
        v.texCoord = planarUv(p, n);
        m.vertices.push_back(v);
    }
    m.indices.push_back(base);
    m.indices.push_back(base + 1);
    m.indices.push_back(base + 2);
}

void quad(MeshData& m, const V3& a, const V3& b, const V3& c, const V3& d)
{
    tri(m, a, b, c);
    tri(m, a, c, d);
}

// ── Rings and tubes ──────────────────────────────────────────────────────────
struct TubeNode {
    V3 center;
    float radius = 1.0f;
};

using Ring = std::vector<V3>;

// Parallel-transported frames along the polyline, so a curved trunk does not
// twist its cross-section.
std::vector<Ring> buildRings(const std::vector<TubeNode>& nodes, int segments,
                             const std::vector<float>& radial)
{
    std::vector<Ring> rings;
    if (nodes.size() < 2 || segments < 3) return rings;

    const size_t n = nodes.size();
    std::vector<V3> tangents(n);
    for (size_t i = 0; i < n; ++i) {
        if (i == 0) tangents[i] = normalize(nodes[1].center - nodes[0].center);
        else if (i + 1 == n) tangents[i] = normalize(nodes[n - 1].center - nodes[n - 2].center);
        else tangents[i] = normalize(nodes[i + 1].center - nodes[i - 1].center);
    }

    V3 u = normalize(cross(std::fabs(tangents[0].y) < 0.9f ? V3{0.0f, 1.0f, 0.0f} : V3{1.0f, 0.0f, 0.0f},
                           tangents[0]));
    rings.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) {
            V3 projected = u - tangents[i] * dot(u, tangents[i]);
            if (length(projected) < 1e-5f) {
                projected = cross(std::fabs(tangents[i].y) < 0.9f ? V3{0.0f, 1.0f, 0.0f}
                                                                  : V3{1.0f, 0.0f, 0.0f},
                                  tangents[i]);
            }
            u = normalize(projected);
        }
        const V3 v = cross(tangents[i], u);

        Ring ring;
        ring.reserve(static_cast<size_t>(segments));
        for (int s = 0; s < segments; ++s) {
            const float a = (static_cast<float>(s) / static_cast<float>(segments)) * 2.0f * kPi;
            const float r = nodes[i].radius * (radial.empty() ? 1.0f : radial[static_cast<size_t>(s)]);
            ring.push_back(nodes[i].center + u * (std::cos(a) * r) + v * (std::sin(a) * r));
        }
        rings.push_back(std::move(ring));
    }
    return rings;
}

void emitSkin(MeshData& m, const std::vector<Ring>& rings)
{
    for (size_t i = 0; i + 1 < rings.size(); ++i) {
        const Ring& lo = rings[i];
        const Ring& hi = rings[i + 1];
        const size_t segs = std::min(lo.size(), hi.size());
        for (size_t s = 0; s < segs; ++s) {
            const size_t t = (s + 1) % segs;
            quad(m, lo[s], lo[t], hi[t], hi[s]);
        }
    }
}

// `flip` = false when the apex sits on the +tangent side of the ring.
void emitFan(MeshData& m, const Ring& ring, const V3& apex, bool flip)
{
    const size_t segs = ring.size();
    for (size_t s = 0; s < segs; ++s) {
        const size_t t = (s + 1) % segs;
        if (flip) tri(m, ring[t], ring[s], apex);
        else tri(m, ring[s], ring[t], apex);
    }
}

V3 ringCenter(const Ring& ring)
{
    V3 c;
    for (const V3& p : ring) c = c + p;
    return ring.empty() ? c : c * (1.0f / static_cast<float>(ring.size()));
}

std::vector<float> radialJitter(Rng& rng, int segments, float lo, float hi)
{
    std::vector<float> out(static_cast<size_t>(segments));
    for (float& f : out) f = rng.range(lo, hi);
    return out;
}

// Winding matches what buildRings produces for a +Y tangent, so emitFan's
// `flip` convention holds for both.
Ring makeUprightRing(const V3& center, float radius, int segments,
                     const std::vector<float>& radial)
{
    Ring ring;
    ring.reserve(static_cast<size_t>(segments));
    for (int s = 0; s < segments; ++s) {
        const float a = (static_cast<float>(s) / static_cast<float>(segments)) * 2.0f * kPi;
        const float r = radius * (radial.empty() ? 1.0f : radial[static_cast<size_t>(s)]);
        ring.push_back(center + V3{std::sin(a) * r, 0.0f, std::cos(a) * r});
    }
    return ring;
}

// A cone with a closed base: the layered skirt that gives a conifer its
// unmistakable silhouette.
void emitCone(MeshData& m, const V3& base, float radius, float coneHeight,
              int segments, const std::vector<float>& radial)
{
    const Ring ring = makeUprightRing(base, radius, segments, radial);
    if (ring.size() < 3) return;
    emitFan(m, ring, base + V3{0.0f, coneHeight, 0.0f}, false);
    emitFan(m, ring, base, true);
}

// ── Deformed icosphere: rocks, canopies and bush blobs ───────────────────────
struct Poly {
    std::vector<V3> verts;
    std::vector<uint32_t> idx;
};

Poly icosahedron()
{
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    Poly p;
    p.verts = {{-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
               {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
               {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1}};
    for (V3& v : p.verts) v = normalize(v);
    p.idx = {0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11,
             1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
             3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9,
             4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1};
    return p;
}

Poly subdivide(const Poly& in)
{
    Poly out;
    out.verts = in.verts;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> midpoints;

    const auto midpoint = [&](uint32_t a, uint32_t b) {
        const auto key = a < b ? std::make_pair(a, b) : std::make_pair(b, a);
        const auto it = midpoints.find(key);
        if (it != midpoints.end()) return it->second;
        const uint32_t id = static_cast<uint32_t>(out.verts.size());
        out.verts.push_back(normalize((out.verts[a] + out.verts[b]) * 0.5f));
        midpoints.emplace(key, id);
        return id;
    };

    for (size_t i = 0; i + 2 < in.idx.size(); i += 3) {
        const uint32_t a = in.idx[i], b = in.idx[i + 1], c = in.idx[i + 2];
        const uint32_t ab = midpoint(a, b), bc = midpoint(b, c), ca = midpoint(c, a);
        const uint32_t tris[] = {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca};
        out.idx.insert(out.idx.end(), std::begin(tris), std::end(tris));
    }
    return out;
}

// Displacement is a function of the (shared) unit direction, so the blob stays
// closed while every face still gets its own normal on emission.
void emitBlob(MeshData& m, const V3& center, const V3& radii, int subdivisions,
              float amplitude, uint32_t seed)
{
    Poly poly = icosahedron();
    for (int i = 0; i < subdivisions; ++i) poly = subdivide(poly);

    std::vector<V3> pts(poly.verts.size());
    for (size_t i = 0; i < poly.verts.size(); ++i) {
        const V3& d = poly.verts[i];
        const float f = 1.0f + amplitude * (hashDir01(d, seed) * 2.0f - 1.0f);
        pts[i] = center + V3{d.x * radii.x * f, d.y * radii.y * f, d.z * radii.z * f};
    }
    for (size_t i = 0; i + 2 < poly.idx.size(); i += 3) {
        tri(m, pts[poly.idx[i]], pts[poly.idx[i + 1]], pts[poly.idx[i + 2]]);
    }
}

// A tapered spike (tetrahedron): one grass blade, one root flare, one branch stub.
void emitSpike(MeshData& m, const V3& base, float radius, const V3& tip, float rotation)
{
    V3 pts[3];
    for (int i = 0; i < 3; ++i) {
        const float a = rotation + static_cast<float>(i) * (2.0f * kPi / 3.0f);
        pts[i] = base + V3{std::cos(a) * radius, 0.0f, std::sin(a) * radius};
    }
    tri(m, pts[1], pts[0], tip);
    tri(m, pts[2], pts[1], tip);
    tri(m, pts[0], pts[2], tip);
    tri(m, pts[0], pts[1], pts[2]);
}

void computeBounds(MeshData& m)
{
    if (m.vertices.empty()) return;
    for (int a = 0; a < 3; ++a) {
        m.minBounds[a] = m.vertices[0].position[static_cast<size_t>(a)];
        m.maxBounds[a] = m.minBounds[a];
    }
    for (const Vertex& v : m.vertices) {
        for (int a = 0; a < 3; ++a) {
            const float c = v.position[static_cast<size_t>(a)];
            m.minBounds[a] = std::min(m.minBounds[a], c);
            m.maxBounds[a] = std::max(m.maxBounds[a], c);
        }
    }
}

void liftToGround(MeshData& m)
{
    computeBounds(m);
    const float dy = m.minBounds[1];
    if (std::fabs(dy) < 1e-6f) return;
    for (Vertex& v : m.vertices) v.position[1] -= dy;
}

// ── Generators ───────────────────────────────────────────────────────────────
// Every generator draws its whole random stream before emitting anything, so
// part=trunk and part=foliage of the same seed describe the same tree.

void genConifer(MeshData& m, const ModelParams& p, bool wantTrunk, bool wantFoliage)
{
    Rng rng(p.seed ^ 0x00C0FFEEu);

    const float h = p.height > 0.0f ? p.height : rng.range(5.0f, 8.5f);
    const int trunkSegs = rng.rangeI(6, 8);
    const float rBase = h * rng.range(0.038f, 0.058f);
    const float rTop = rBase * rng.range(0.16f, 0.30f);
    const float leanAngle = rng.range(0.0f, 2.0f * kPi);
    const float lean = h * rng.range(0.0f, 0.05f);
    const std::vector<float> trunkRadial = radialJitter(rng, trunkSegs, 0.86f, 1.14f);

    const int layers = rng.rangeI(3, 5);
    const float crownBottom = h * rng.range(0.20f, 0.34f);
    const float crownWidth = h * rng.range(0.24f, 0.36f);

    struct Layer {
        float baseY, radius, coneHeight;
        int segments;
        std::vector<float> radial;
    };
    std::vector<Layer> layerData;
    layerData.reserve(static_cast<size_t>(layers));
    for (int i = 0; i < layers; ++i) {
        const float t = layers > 1 ? static_cast<float>(i) / static_cast<float>(layers - 1) : 0.0f;
        Layer l;
        l.baseY = crownBottom + (h * 0.84f - crownBottom) * t;
        l.radius = crownWidth * (1.0f - 0.60f * t) * rng.range(0.88f, 1.08f);
        l.coneHeight = h * (0.30f - 0.12f * t) * rng.range(0.85f, 1.20f);
        l.segments = rng.rangeI(6, 9);
        l.radial = radialJitter(rng, l.segments, 0.76f, 1.16f);
        layerData.push_back(std::move(l));
    }
    const float spireHeight = h * rng.range(0.14f, 0.26f);
    const int spireSegs = rng.rangeI(5, 7);
    const std::vector<float> spireRadial = radialJitter(rng, spireSegs, 0.85f, 1.10f);

    const V3 leanDir{std::cos(leanAngle), 0.0f, std::sin(leanAngle)};
    const auto drift = [&](float y) { return leanDir * (lean * (y / h) * (y / h)); };

    if (wantTrunk) {
        std::vector<TubeNode> nodes;
        for (int i = 0; i <= 5; ++i) {
            const float t = static_cast<float>(i) / 5.0f;
            const float y = h * 0.96f * t;
            const float flare = 1.0f + 0.55f * std::exp(-t * 9.0f);
            TubeNode node;
            node.center = V3{0.0f, y, 0.0f} + drift(y);
            node.radius = (rBase + (rTop - rBase) * t) * flare;
            nodes.push_back(node);
        }
        const std::vector<Ring> rings = buildRings(nodes, trunkSegs, trunkRadial);
        emitSkin(m, rings);
        if (!rings.empty()) {
            emitFan(m, rings.front(), ringCenter(rings.front()), true);
            emitFan(m, rings.back(), ringCenter(rings.back()), false);
        }
    }

    if (wantFoliage) {
        for (const Layer& l : layerData) {
            emitCone(m, V3{0.0f, l.baseY, 0.0f} + drift(l.baseY), l.radius, l.coneHeight,
                     l.segments, l.radial);
        }
        const float spireBase = h * 0.84f;
        emitCone(m, V3{0.0f, spireBase, 0.0f} + drift(spireBase), crownWidth * 0.26f,
                 spireHeight, spireSegs, spireRadial);
    }
}

void genBroadleaf(MeshData& m, const ModelParams& p, bool wantTrunk, bool wantFoliage)
{
    Rng rng(p.seed ^ 0x0B10ADEFu);

    const float h = p.height > 0.0f ? p.height : rng.range(4.5f, 7.0f);
    const int trunkSegs = rng.rangeI(6, 8);
    const float rBase = h * rng.range(0.045f, 0.072f);
    const float rTop = rBase * rng.range(0.34f, 0.52f);
    const float bendAngle = rng.range(0.0f, 2.0f * kPi);
    const float bend = h * rng.range(0.05f, 0.17f);
    const float trunkTopY = h * rng.range(0.55f, 0.68f);
    const std::vector<float> trunkRadial = radialJitter(rng, trunkSegs, 0.88f, 1.12f);

    const int branches = rng.rangeI(2, 3);
    struct Branch {
        float startT, angle, length, lift;
    };
    std::vector<Branch> branchData;
    for (int i = 0; i < branches; ++i) {
        branchData.push_back({rng.range(0.45f, 0.72f), rng.range(0.0f, 2.0f * kPi),
                              h * rng.range(0.16f, 0.30f), rng.range(0.35f, 0.85f)});
    }

    const int blobs = rng.rangeI(2, 4);
    struct Blob {
        V3 offset, radii;
        float amp;
        uint32_t seed;
    };
    std::vector<Blob> blobData;
    for (int i = 0; i < blobs; ++i) {
        const float a = rng.range(0.0f, 2.0f * kPi);
        const float d = h * rng.range(0.0f, 0.18f);
        const float r = h * rng.range(0.19f, 0.30f);
        blobData.push_back({V3{std::cos(a) * d, h * rng.range(0.0f, 0.22f), std::sin(a) * d},
                            V3{r, r * rng.range(0.68f, 0.96f), r},
                            rng.range(0.14f, 0.30f), rng.nextU32()});
    }

    const V3 bendDir{std::cos(bendAngle), 0.0f, std::sin(bendAngle)};
    const auto drift = [&](float t) { return bendDir * (bend * std::sin(t * kPi * 0.5f)); };
    const V3 trunkTop = V3{0.0f, trunkTopY, 0.0f} + drift(1.0f);

    if (wantTrunk) {
        std::vector<TubeNode> nodes;
        for (int i = 0; i <= 5; ++i) {
            const float t = static_cast<float>(i) / 5.0f;
            const float flare = 1.0f + 0.60f * std::exp(-t * 8.0f);
            TubeNode node;
            node.center = V3{0.0f, trunkTopY * t, 0.0f} + drift(t);
            node.radius = (rBase + (rTop - rBase) * t) * flare;
            nodes.push_back(node);
        }
        const std::vector<Ring> rings = buildRings(nodes, trunkSegs, trunkRadial);
        emitSkin(m, rings);
        if (!rings.empty()) {
            emitFan(m, rings.front(), ringCenter(rings.front()), true);
            emitFan(m, rings.back(), ringCenter(rings.back()), false);
        }

        for (const Branch& b : branchData) {
            const V3 origin = V3{0.0f, trunkTopY * b.startT, 0.0f} + drift(b.startT);
            const V3 tip = origin + V3{std::cos(b.angle) * b.length, b.length * b.lift,
                                       std::sin(b.angle) * b.length};
            emitSpike(m, origin, rTop * 0.85f, tip, b.angle);
        }
    }

    if (wantFoliage) {
        for (const Blob& b : blobData) {
            emitBlob(m, trunkTop + b.offset + V3{0.0f, h * 0.16f, 0.0f}, b.radii, 1, b.amp, b.seed);
        }
    }
}

void genRock(MeshData& m, const ModelParams& p, bool, bool)
{
    Rng rng(p.seed ^ 0x0D0CAB1Eu);

    // Boulders read better wider than tall, which also cancels part of the
    // non-uniform per-instance scale the scene loader applies.
    const float h = p.height > 0.0f ? p.height : rng.range(0.7f, 1.8f);
    const int chunks = rng.rangeI(1, 3);

    for (int i = 0; i < chunks; ++i) {
        const float a = rng.range(0.0f, 2.0f * kPi);
        const float d = h * rng.range(0.0f, 0.42f) * (i == 0 ? 0.0f : 1.0f);
        const float rxz = h * rng.range(0.55f, 0.85f) * (i == 0 ? 1.0f : rng.range(0.45f, 0.80f));
        const V3 radii{rxz, h * rng.range(0.32f, 0.55f), rxz * rng.range(0.78f, 1.15f)};
        const V3 center{std::cos(a) * d, radii.y * rng.range(0.55f, 0.95f), std::sin(a) * d};
        emitBlob(m, center, radii, rng.rangeI(0, 1), rng.range(0.16f, 0.36f), rng.nextU32());
    }
}

void genBush(MeshData& m, const ModelParams& p, bool wantTrunk, bool wantFoliage)
{
    Rng rng(p.seed ^ 0x0B05C001u);

    const float h = p.height > 0.0f ? p.height : rng.range(0.6f, 1.2f);
    const int stems = rng.rangeI(2, 4);
    struct Stem {
        float angle, lean, height, radius;
    };
    std::vector<Stem> stemData;
    for (int i = 0; i < stems; ++i) {
        stemData.push_back({rng.range(0.0f, 2.0f * kPi), rng.range(0.10f, 0.32f),
                            h * rng.range(0.35f, 0.60f), h * rng.range(0.030f, 0.055f)});
    }

    const int blobs = rng.rangeI(3, 5);
    struct Blob {
        V3 center, radii;
        int subdiv;
        float amp;
        uint32_t seed;
    };
    std::vector<Blob> blobData;
    for (int i = 0; i < blobs; ++i) {
        const float a = rng.range(0.0f, 2.0f * kPi);
        const float d = h * rng.range(0.0f, 0.32f);
        const float r = h * rng.range(0.26f, 0.44f);
        blobData.push_back({V3{std::cos(a) * d, h * rng.range(0.34f, 0.62f), std::sin(a) * d},
                            V3{r, r * rng.range(0.66f, 0.92f), r},
                            rng.rangeI(0, 1), rng.range(0.16f, 0.34f), rng.nextU32()});
    }

    if (wantTrunk) {
        for (const Stem& s : stemData) {
            const V3 base{0.0f, 0.0f, 0.0f};
            const V3 tip{std::cos(s.angle) * s.height * s.lean, s.height,
                         std::sin(s.angle) * s.height * s.lean};
            emitSpike(m, base, s.radius, tip, s.angle);
        }
    }

    if (wantFoliage) {
        for (const Blob& b : blobData) {
            emitBlob(m, b.center, b.radii, b.subdiv, b.amp, b.seed);
        }
    }
}

void genGrass(MeshData& m, const ModelParams& p, bool, bool)
{
    Rng rng(p.seed ^ 0x0C7A55EDu);

    const float h = p.height > 0.0f ? p.height : rng.range(0.28f, 0.55f);
    const int blades = rng.rangeI(6, 11);
    for (int i = 0; i < blades; ++i) {
        const float a = rng.range(0.0f, 2.0f * kPi);
        const float d = h * rng.range(0.0f, 0.55f);
        const V3 base{std::cos(a) * d, 0.0f, std::sin(a) * d};
        const float bladeH = h * rng.range(0.55f, 1.0f);
        const float tilt = rng.range(0.12f, 0.42f);
        const float tiltAngle = rng.range(0.0f, 2.0f * kPi);
        const V3 tip = base + V3{std::cos(tiltAngle) * bladeH * tilt, bladeH,
                                 std::sin(tiltAngle) * bladeH * tilt};
        emitSpike(m, base, h * rng.range(0.022f, 0.042f), tip, a);
    }
}

void genStump(MeshData& m, const ModelParams& p, bool, bool)
{
    Rng rng(p.seed ^ 0x057A9900u);

    const float h = p.height > 0.0f ? p.height : rng.range(0.35f, 0.75f);
    const int segs = rng.rangeI(7, 10);
    const float rBase = h * rng.range(0.55f, 0.90f);
    const float rTop = rBase * rng.range(0.70f, 0.88f);
    const std::vector<float> radial = radialJitter(rng, segs, 0.90f, 1.10f);

    std::vector<TubeNode> nodes;
    for (int i = 0; i <= 3; ++i) {
        const float t = static_cast<float>(i) / 3.0f;
        const float flare = 1.0f + 0.50f * std::exp(-t * 7.0f);
        nodes.push_back({V3{0.0f, h * t, 0.0f}, (rBase + (rTop - rBase) * t) * flare});
    }

    std::vector<Ring> rings = buildRings(nodes, segs, radial);
    if (rings.empty()) return;

    // Splintered break: the top ring is jagged, and the skin uses the same
    // displaced points so the surface stays closed.
    for (V3& pt : rings.back()) pt.y += h * rng.range(-0.10f, 0.14f);

    emitSkin(m, rings);
    emitFan(m, rings.front(), ringCenter(rings.front()), true);
    emitFan(m, rings.back(), ringCenter(rings.back()) + V3{0.0f, h * rng.range(0.04f, 0.16f), 0.0f},
            false);

    const int roots = rng.rangeI(2, 4);
    for (int i = 0; i < roots; ++i) {
        const float a = rng.range(0.0f, 2.0f * kPi);
        const float len = rBase * rng.range(1.1f, 1.9f);
        emitSpike(m, V3{0.0f, h * rng.range(0.02f, 0.10f), 0.0f}, rBase * 0.55f,
                  V3{std::cos(a) * len, h * rng.range(0.02f, 0.12f), std::sin(a) * len}, a);
    }
}

void genLog(MeshData& m, const ModelParams& p, bool, bool)
{
    Rng rng(p.seed ^ 0x010900DDu);

    const float len = p.height > 0.0f ? p.height : rng.range(2.5f, 4.5f);
    const float radius = len * rng.range(0.055f, 0.095f);
    const int segs = rng.rangeI(6, 9);
    const float yaw = rng.range(0.0f, 2.0f * kPi);
    const float bend = len * rng.range(0.02f, 0.10f);
    const std::vector<float> radial = radialJitter(rng, segs, 0.88f, 1.12f);

    const V3 axis{std::cos(yaw), 0.0f, std::sin(yaw)};
    const V3 side{-std::sin(yaw), 0.0f, std::cos(yaw)};

    std::vector<TubeNode> nodes;
    for (int i = 0; i <= 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        const float s = (t - 0.5f) * len;
        TubeNode node;
        node.center = axis * s + side * (bend * std::sin(t * kPi)) + V3{0.0f, radius, 0.0f};
        node.radius = radius * (1.0f - 0.22f * t);
        nodes.push_back(node);
    }

    const std::vector<Ring> rings = buildRings(nodes, segs, radial);
    if (rings.empty()) return;
    emitSkin(m, rings);
    emitFan(m, rings.front(), ringCenter(rings.front()), true);
    emitFan(m, rings.back(), ringCenter(rings.back()), false);

    const int stubs = rng.rangeI(1, 3);
    for (int i = 0; i < stubs; ++i) {
        const float t = rng.range(0.2f, 0.8f);
        const V3 origin = axis * ((t - 0.5f) * len) + V3{0.0f, radius, 0.0f};
        const float a = rng.range(0.0f, 2.0f * kPi);
        const float stubLen = radius * rng.range(1.8f, 3.4f);
        emitSpike(m, origin, radius * 0.32f,
                  origin + V3{std::cos(a) * stubLen, stubLen * rng.range(0.4f, 1.1f),
                              std::sin(a) * stubLen},
                  a);
    }
}

// ── Id parsing helpers ───────────────────────────────────────────────────────
bool parseU32(const std::string& s, uint32_t& out)
{
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const unsigned long v = std::strtoul(s.c_str(), &end, 10);
    if (errno != 0 || end != s.c_str() + s.size() || v > 0xFFFFFFFFul) return false;
    out = static_cast<uint32_t>(v);
    return true;
}

bool parseFloat(const std::string& s, float& out)
{
    if (s.empty()) return false;
    errno = 0;
    char* end = nullptr;
    const float v = std::strtof(s.c_str(), &end);
    if (errno != 0 || end != s.c_str() + s.size() || !std::isfinite(v)) return false;
    out = v;
    return true;
}

bool kindFromName(const std::string& s, ModelKind& out)
{
    if (s == "conifer") { out = ModelKind::Conifer; return true; }
    if (s == "broadleaf") { out = ModelKind::Broadleaf; return true; }
    if (s == "rock") { out = ModelKind::Rock; return true; }
    if (s == "bush") { out = ModelKind::Bush; return true; }
    if (s == "grass") { out = ModelKind::Grass; return true; }
    if (s == "stump") { out = ModelKind::Stump; return true; }
    if (s == "log") { out = ModelKind::Log; return true; }
    return false;
}

constexpr const char* kPrefix = "proc:";
constexpr size_t kPrefixLen = 5;

} // namespace

// ─────────────────────────────────────────────────────────────────────────────

const char* kindName(ModelKind kind)
{
    switch (kind) {
        case ModelKind::Conifer: return "conifer";
        case ModelKind::Broadleaf: return "broadleaf";
        case ModelKind::Rock: return "rock";
        case ModelKind::Bush: return "bush";
        case ModelKind::Grass: return "grass";
        case ModelKind::Stump: return "stump";
        case ModelKind::Log: return "log";
    }
    return "unknown";
}

const char* partName(ModelPart part)
{
    switch (part) {
        case ModelPart::All: return "all";
        case ModelPart::Trunk: return "trunk";
        case ModelPart::Foliage: return "foliage";
    }
    return "unknown";
}

bool isProceduralMeshId(const std::string& meshId)
{
    return meshId.compare(0, kPrefixLen, kPrefix) == 0;
}

bool parseMeshId(const std::string& meshId, ModelParams& out)
{
    if (!isProceduralMeshId(meshId)) return false;

    const std::string body = meshId.substr(kPrefixLen);
    const size_t q = body.find('?');
    ModelParams parsed;
    if (!kindFromName(body.substr(0, q), parsed.kind)) return false;
    if (q == std::string::npos) {
        out = parsed;
        return true;
    }

    size_t pos = q + 1;
    while (pos < body.size()) {
        const size_t amp = body.find('&', pos);
        const std::string pair = body.substr(pos, amp == std::string::npos ? std::string::npos
                                                                           : amp - pos);
        pos = (amp == std::string::npos) ? body.size() : amp + 1;
        if (pair.empty()) continue;

        const size_t eq = pair.find('=');
        if (eq == std::string::npos) return false;
        const std::string key = pair.substr(0, eq);
        const std::string val = pair.substr(eq + 1);

        if (key == "seed") {
            if (!parseU32(val, parsed.seed)) return false;
        } else if (key == "height") {
            if (!parseFloat(val, parsed.height) || parsed.height <= 0.0f || parsed.height > 500.0f)
                return false;
        } else if (key == "part") {
            if (val == "all") parsed.part = ModelPart::All;
            else if (val == "trunk") parsed.part = ModelPart::Trunk;
            else if (val == "foliage") parsed.part = ModelPart::Foliage;
            else return false;
        }
        // Unknown keys are ignored so older builds still load newer scenes.
    }

    out = parsed;
    return true;
}

MeshData generate(const ModelParams& params)
{
    MeshData m;
    const bool wantTrunk = params.part != ModelPart::Foliage;
    const bool wantFoliage = params.part != ModelPart::Trunk;

    switch (params.kind) {
        case ModelKind::Conifer:   genConifer(m, params, wantTrunk, wantFoliage); break;
        case ModelKind::Broadleaf: genBroadleaf(m, params, wantTrunk, wantFoliage); break;
        case ModelKind::Rock:      genRock(m, params, true, true); break;
        case ModelKind::Bush:      genBush(m, params, wantTrunk, wantFoliage); break;
        case ModelKind::Grass:     genGrass(m, params, true, true); break;
        case ModelKind::Stump:     genStump(m, params, true, true); break;
        case ModelKind::Log:       genLog(m, params, true, true); break;
    }

    if (params.kind == ModelKind::Rock) liftToGround(m);
    computeBounds(m);
    return m;
}

} // namespace dash::procmesh
