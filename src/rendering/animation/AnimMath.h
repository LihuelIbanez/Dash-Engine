#pragma once

// Math for the skeletal animation runtime.
//
// Mat4/Vec3 mirror dash::vkexp (src/rendering/vulkan/VkMath.h) exactly: same
// column-major float[16] layout, same Vec3 field order, so a bone matrix array
// can be memcpy'd straight into a Vulkan buffer with no conversion. They are
// redeclared here instead of including VkMath.h because that header pulls in
// <vulkan/vulkan.h>, and this module is also compiled into ModelImporter, which
// several test targets build without the Vulkan SDK on their include path.

#include <cmath>
#include <cstdint>

namespace dash::anim {

struct Vec3 {
    float x;
    float y;
    float z;
};

struct Mat4 {
    float m[16]{};
};

struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

inline Mat4 identity()
{
    Mat4 out{};
    out.m[0] = 1.0f;
    out.m[5] = 1.0f;
    out.m[10] = 1.0f;
    out.m[15] = 1.0f;
    return out;
}

inline Mat4 multiply(const Mat4& a, const Mat4& b)
{
    Mat4 out{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k) {
                sum += a.m[k * 4 + row] * b.m[col * 4 + k];
            }
            out.m[col * 4 + row] = sum;
        }
    }
    return out;
}

inline Vec3 lerp(const Vec3& a, const Vec3& b, float t)
{
    return {a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t};
}

inline float dot(const Quat& a, const Quat& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

inline Quat normalize(const Quat& q)
{
    const float len = std::sqrt(dot(q, q));
    if (len <= 1e-8f) return Quat{};
    const float inv = 1.0f / len;
    return {q.x * inv, q.y * inv, q.z * inv, q.w * inv};
}

// Shortest-arc spherical interpolation; falls back to normalized lerp when the
// two rotations are nearly parallel and sin(theta) would blow up.
inline Quat slerp(const Quat& a, const Quat& b, float t)
{
    Quat end = b;
    float cosTheta = dot(a, b);
    if (cosTheta < 0.0f) {
        cosTheta = -cosTheta;
        end = {-b.x, -b.y, -b.z, -b.w};
    }

    if (cosTheta > 0.9995f) {
        return normalize(Quat{a.x + (end.x - a.x) * t,
                              a.y + (end.y - a.y) * t,
                              a.z + (end.z - a.z) * t,
                              a.w + (end.w - a.w) * t});
    }

    const float theta = std::acos(cosTheta);
    const float sinTheta = std::sin(theta);
    const float wa = std::sin((1.0f - t) * theta) / sinTheta;
    const float wb = std::sin(t * theta) / sinTheta;
    return normalize(Quat{a.x * wa + end.x * wb,
                          a.y * wa + end.y * wb,
                          a.z * wa + end.z * wb,
                          a.w * wa + end.w * wb});
}

// Column-major M = T * R * S.
inline Mat4 composeTRS(const Vec3& translation, const Quat& rotation, const Vec3& scale)
{
    const Quat q = normalize(rotation);
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

    Mat4 out{};
    out.m[0]  = (1.0f - 2.0f * (yy + zz)) * scale.x;
    out.m[1]  = (2.0f * (xy + wz)) * scale.x;
    out.m[2]  = (2.0f * (xz - wy)) * scale.x;
    out.m[4]  = (2.0f * (xy - wz)) * scale.y;
    out.m[5]  = (1.0f - 2.0f * (xx + zz)) * scale.y;
    out.m[6]  = (2.0f * (yz + wx)) * scale.y;
    out.m[8]  = (2.0f * (xz + wy)) * scale.z;
    out.m[9]  = (2.0f * (yz - wx)) * scale.z;
    out.m[10] = (1.0f - 2.0f * (xx + yy)) * scale.z;
    out.m[12] = translation.x;
    out.m[13] = translation.y;
    out.m[14] = translation.z;
    out.m[15] = 1.0f;
    return out;
}

// General 4x4 inverse; returns identity for singular matrices.
inline Mat4 inverse(const Mat4& in)
{
    const float* m = in.m;
    float inv[16];

    inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
    inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
    inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
    inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
    inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
    inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
    inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
    inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
    inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
    inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
    inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
    inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
    inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
    inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
    inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
    inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];

    const float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
    if (std::fabs(det) < 1e-12f) return identity();

    Mat4 out{};
    const float invDet = 1.0f / det;
    for (int i = 0; i < 16; ++i) out.m[i] = inv[i] * invDet;
    return out;
}

} // namespace dash::anim
