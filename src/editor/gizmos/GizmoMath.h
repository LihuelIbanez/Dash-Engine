#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// GizmoMath — ray/axis intersection helpers behind the viewport gizmos.
// Pure math: no ImGui, no Vulkan, no scene types, so it is unit-testable.
// Matrices use the same column-major layout as EditorApp::buildViewProjMatrix.
// ─────────────────────────────────────────────────────────────────────────────
namespace dash::gizmo {

struct Vec3 {
    float x = 0.f, y = 0.f, z = 0.f;
};

struct Ray {
    Vec3 origin;
    Vec3 dir;      // not required to be normalised
};

enum class Mode : int { None = 0, Translate = 1, Rotate = 2, Scale = 3 };
enum class Axis : int { None = 0, X = 1, Y = 2, Z = 3 };

Vec3  add(const Vec3& a, const Vec3& b);
Vec3  sub(const Vec3& a, const Vec3& b);
Vec3  mul(const Vec3& a, float s);
float dot(const Vec3& a, const Vec3& b);
Vec3  cross(const Vec3& a, const Vec3& b);
float length(const Vec3& a);
Vec3  normalize(const Vec3& a);

/// Unit vector for an axis in render-world space (Y is up).
Vec3 axisDirection(Axis axis);

bool invertMatrix4(const float m[16], float out[16]);

/// World point → normalised device coords. False when behind the camera.
bool projectToNdc(const float viewProj[16], const Vec3& p,
                  float& ndcX, float& ndcY, float& ndcZ);

/// NDC point → world-space picking ray (Vulkan depth range 0..1).
bool rayFromNdc(const float invViewProj[16], float ndcX, float ndcY, Ray& out);

/// Closest approach between a ray and an infinite line. False when parallel.
bool closestPointsRayLine(const Ray& ray, const Vec3& lineOrigin, const Vec3& lineDir,
                          float& tRay, float& tLine);

/// Distance from a ray to a segment; outT is the [0..1] position on the segment.
float distanceRayToSegment(const Ray& ray, const Vec3& a, const Vec3& b, float& outT);

bool rayPlaneIntersection(const Ray& ray, const Vec3& planePoint,
                          const Vec3& planeNormal, Vec3& outHit);

/// Nearest translate/scale handle under the ray, or Axis::None.
Axis pickAxisHandle(const Ray& ray, const Vec3& origin, float axisLength, float pickRadius);

/// Nearest rotate ring under the ray, or Axis::None.
Axis pickRotationRing(const Ray& ray, const Vec3& origin, float radius, float tolerance);

/// Signed world-space distance the pointer travelled along `axisDir`.
bool axisDragDelta(const Ray& from, const Ray& to, const Vec3& origin,
                   const Vec3& axisDir, float& outDelta);

/// Signed rotation in degrees the pointer swept around `axisDir`.
bool axisRotationDelta(const Ray& from, const Ray& to, const Vec3& origin,
                       const Vec3& axisDir, float& outDeg);

float snapTo(float value, float step);
float snapAngleDeg(float deg, float stepDeg);

} // namespace dash::gizmo
