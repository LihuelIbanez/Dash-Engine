#include "GizmoMath.h"

#include <cmath>

namespace dash::gizmo {

namespace {

constexpr float kPi = 3.14159265358979f;
constexpr float kEps = 1e-6f;

// Perpendicular basis for a ring/plane around `axis`, stable for any axis.
void axisBasis(const Vec3& axis, Vec3& u, Vec3& v)
{
    const Vec3 ref = (std::fabs(axis.x) < 0.9f) ? Vec3{1.f, 0.f, 0.f} : Vec3{0.f, 1.f, 0.f};
    u = normalize(cross(ref, axis));
    v = cross(axis, u);
}

} // namespace

Vec3  add(const Vec3& a, const Vec3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3  sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3  mul(const Vec3& a, float s)       { return {a.x * s, a.y * s, a.z * s}; }
float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

Vec3 cross(const Vec3& a, const Vec3& b)
{
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

float length(const Vec3& a) { return std::sqrt(dot(a, a)); }

Vec3 normalize(const Vec3& a)
{
    const float len = length(a);
    return (len > kEps) ? mul(a, 1.f / len) : Vec3{};
}

Vec3 axisDirection(Axis axis)
{
    switch (axis) {
    case Axis::X: return {1.f, 0.f, 0.f};
    case Axis::Y: return {0.f, 1.f, 0.f};
    case Axis::Z: return {0.f, 0.f, 1.f};
    case Axis::None: break;
    }
    return {};
}

bool invertMatrix4(const float m[16], float out[16])
{
    const float a00 = m[0],  a01 = m[1],  a02 = m[2],  a03 = m[3];
    const float a10 = m[4],  a11 = m[5],  a12 = m[6],  a13 = m[7];
    const float a20 = m[8],  a21 = m[9],  a22 = m[10], a23 = m[11];
    const float a30 = m[12], a31 = m[13], a32 = m[14], a33 = m[15];

    const float b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10;
    const float b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11;
    const float b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12;
    const float b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30;
    const float b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31;
    const float b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;

    const float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (std::fabs(det) < 1e-12f) return false;
    const float invDet = 1.f / det;

    out[0]  = ( a11 * b11 - a12 * b10 + a13 * b09) * invDet;
    out[1]  = (-a01 * b11 + a02 * b10 - a03 * b09) * invDet;
    out[2]  = ( a31 * b05 - a32 * b04 + a33 * b03) * invDet;
    out[3]  = (-a21 * b05 + a22 * b04 - a23 * b03) * invDet;
    out[4]  = (-a10 * b11 + a12 * b08 - a13 * b07) * invDet;
    out[5]  = ( a00 * b11 - a02 * b08 + a03 * b07) * invDet;
    out[6]  = (-a30 * b05 + a32 * b02 - a33 * b01) * invDet;
    out[7]  = ( a20 * b05 - a22 * b02 + a23 * b01) * invDet;
    out[8]  = ( a10 * b10 - a11 * b08 + a13 * b06) * invDet;
    out[9]  = (-a00 * b10 + a01 * b08 - a03 * b06) * invDet;
    out[10] = ( a30 * b04 - a31 * b02 + a33 * b00) * invDet;
    out[11] = (-a20 * b04 + a21 * b02 - a23 * b00) * invDet;
    out[12] = (-a10 * b09 + a11 * b07 - a12 * b06) * invDet;
    out[13] = ( a00 * b09 - a01 * b07 + a02 * b06) * invDet;
    out[14] = (-a30 * b03 + a31 * b01 - a32 * b00) * invDet;
    out[15] = ( a20 * b03 - a21 * b01 + a22 * b00) * invDet;
    return true;
}

bool projectToNdc(const float viewProj[16], const Vec3& p,
                  float& ndcX, float& ndcY, float& ndcZ)
{
    const float x = viewProj[0] * p.x + viewProj[4] * p.y + viewProj[8]  * p.z + viewProj[12];
    const float y = viewProj[1] * p.x + viewProj[5] * p.y + viewProj[9]  * p.z + viewProj[13];
    const float z = viewProj[2] * p.x + viewProj[6] * p.y + viewProj[10] * p.z + viewProj[14];
    const float w = viewProj[3] * p.x + viewProj[7] * p.y + viewProj[11] * p.z + viewProj[15];
    if (w <= kEps) return false;
    ndcX = x / w;
    ndcY = y / w;
    ndcZ = z / w;
    return true;
}

bool rayFromNdc(const float invViewProj[16], float ndcX, float ndcY, Ray& out)
{
    auto unproject = [&](float ndcZ, Vec3& p) {
        const float x = invViewProj[0] * ndcX + invViewProj[4] * ndcY + invViewProj[8]  * ndcZ + invViewProj[12];
        const float y = invViewProj[1] * ndcX + invViewProj[5] * ndcY + invViewProj[9]  * ndcZ + invViewProj[13];
        const float z = invViewProj[2] * ndcX + invViewProj[6] * ndcY + invViewProj[10] * ndcZ + invViewProj[14];
        const float w = invViewProj[3] * ndcX + invViewProj[7] * ndcY + invViewProj[11] * ndcZ + invViewProj[15];
        if (std::fabs(w) < 1e-12f) return false;
        p = {x / w, y / w, z / w};
        return true;
    };

    Vec3 nearP, farP;
    if (!unproject(0.f, nearP)) return false;
    if (!unproject(1.f, farP))  return false;

    out.origin = nearP;
    out.dir = sub(farP, nearP);
    return length(out.dir) > kEps;
}

bool closestPointsRayLine(const Ray& ray, const Vec3& lineOrigin, const Vec3& lineDir,
                          float& tRay, float& tLine)
{
    const Vec3 d1 = ray.dir;
    const Vec3 d2 = lineDir;
    const Vec3 r  = sub(ray.origin, lineOrigin);

    const float a = dot(d1, d1);
    const float b = dot(d1, d2);
    const float c = dot(d2, d2);
    const float d = dot(d1, r);
    const float e = dot(d2, r);

    const float denom = a * c - b * b;
    if (std::fabs(denom) < kEps || a < kEps || c < kEps) return false;

    tRay  = (b * e - c * d) / denom;
    tLine = (a * e - b * d) / denom;
    return true;
}

float distanceRayToSegment(const Ray& ray, const Vec3& a, const Vec3& b, float& outT)
{
    const Vec3 segDir = sub(b, a);
    const float segLen = length(segDir);
    if (segLen < kEps) {
        outT = 0.f;
        const Vec3 diff = sub(a, ray.origin);
        const Vec3 dir = normalize(ray.dir);
        const float proj = dot(diff, dir);
        return length(sub(diff, mul(dir, proj)));
    }

    float tRay = 0.f, tLine = 0.f;
    if (!closestPointsRayLine(ray, a, segDir, tRay, tLine)) {
        tLine = 0.f;
    }
    if (tLine < 0.f) tLine = 0.f;
    if (tLine > 1.f) tLine = 1.f;
    outT = tLine;

    // Distance from the (clamped) segment point to the ray, ray clamped forward.
    const Vec3 segPoint = add(a, mul(segDir, tLine));
    const Vec3 rayDir = normalize(ray.dir);
    float proj = dot(sub(segPoint, ray.origin), rayDir);
    if (proj < 0.f) proj = 0.f;
    const Vec3 rayPoint = add(ray.origin, mul(rayDir, proj));
    return length(sub(segPoint, rayPoint));
}

bool rayPlaneIntersection(const Ray& ray, const Vec3& planePoint,
                          const Vec3& planeNormal, Vec3& outHit)
{
    const float denom = dot(ray.dir, planeNormal);
    if (std::fabs(denom) < kEps) return false;
    const float t = dot(sub(planePoint, ray.origin), planeNormal) / denom;
    if (t < 0.f) return false;
    outHit = add(ray.origin, mul(ray.dir, t));
    return true;
}

Axis pickAxisHandle(const Ray& ray, const Vec3& origin, float axisLength, float pickRadius)
{
    Axis best = Axis::None;
    float bestDist = pickRadius;

    for (int i = 1; i <= 3; ++i) {
        const Axis axis = static_cast<Axis>(i);
        const Vec3 tip = add(origin, mul(axisDirection(axis), axisLength));
        float t = 0.f;
        const float dist = distanceRayToSegment(ray, origin, tip, t);
        if (dist < bestDist) {
            bestDist = dist;
            best = axis;
        }
    }
    return best;
}

Axis pickRotationRing(const Ray& ray, const Vec3& origin, float radius, float tolerance)
{
    Axis best = Axis::None;
    float bestDelta = tolerance;

    for (int i = 1; i <= 3; ++i) {
        const Axis axis = static_cast<Axis>(i);
        Vec3 hit;
        if (!rayPlaneIntersection(ray, origin, axisDirection(axis), hit)) continue;
        const float delta = std::fabs(length(sub(hit, origin)) - radius);
        if (delta < bestDelta) {
            bestDelta = delta;
            best = axis;
        }
    }
    return best;
}

bool axisDragDelta(const Ray& from, const Ray& to, const Vec3& origin,
                   const Vec3& axisDir, float& outDelta)
{
    const Vec3 dir = normalize(axisDir);
    if (length(dir) < kEps) return false;

    float tRayA = 0.f, tLineA = 0.f, tRayB = 0.f, tLineB = 0.f;
    if (!closestPointsRayLine(from, origin, dir, tRayA, tLineA)) return false;
    if (!closestPointsRayLine(to,   origin, dir, tRayB, tLineB)) return false;

    outDelta = tLineB - tLineA;
    return true;
}

bool axisRotationDelta(const Ray& from, const Ray& to, const Vec3& origin,
                       const Vec3& axisDir, float& outDeg)
{
    const Vec3 n = normalize(axisDir);
    if (length(n) < kEps) return false;

    Vec3 hitA, hitB;
    if (!rayPlaneIntersection(from, origin, n, hitA)) return false;
    if (!rayPlaneIntersection(to,   origin, n, hitB)) return false;

    Vec3 u, v;
    axisBasis(n, u, v);

    const Vec3 pa = sub(hitA, origin);
    const Vec3 pb = sub(hitB, origin);
    if (length(pa) < kEps || length(pb) < kEps) return false;

    const float angA = std::atan2(dot(pa, v), dot(pa, u));
    const float angB = std::atan2(dot(pb, v), dot(pb, u));

    float delta = (angB - angA) * 180.f / kPi;
    while (delta > 180.f)  delta -= 360.f;
    while (delta < -180.f) delta += 360.f;
    outDeg = delta;
    return true;
}

float snapTo(float value, float step)
{
    if (step <= kEps) return value;
    return std::round(value / step) * step;
}

float snapAngleDeg(float deg, float stepDeg)
{
    return snapTo(deg, stepDeg);
}

} // namespace dash::gizmo
