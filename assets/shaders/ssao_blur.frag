#version 450

// One half of the separable bilateral blur that cleans up ssao.frag. The taps
// are weighted by how close they are in view-space depth, so the AO does not
// bleed across a silhouette and leave a halo around the object.
//
// Recorded twice with the same pipeline: horizontally into the ping image, then
// vertically into the image the scene pass samples. `dir` picks the axis.

layout(location = 0) in vec2 vUV;
layout(location = 0) out float outAo;

layout(set = 0, binding = 0) uniform sampler2D srcTex;
layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(push_constant) uniform BlurPC {
    vec4 dir;   // xy = texel step along the blur axis, z = depth tolerance in world units
    vec4 proj;  // z = zNear, w = zFar (x,y unused, kept for one shared push range)
} pc;

const float kWeight[5] = float[5](0.2270270, 0.1945946, 0.1216216, 0.0540541, 0.0162162);

float linearDepth(float d)
{
    const float zn = pc.proj.z;
    const float zf = pc.proj.w;
    return (zn * zf) / max(zf - d * (zf - zn), 1e-6);
}

void main()
{
    const float centerZ = linearDepth(texture(depthTex, vUV).r);
    const float tolerance = max(pc.dir.z, 1e-4);

    float sum = texture(srcTex, vUV).r * kWeight[0];
    float weightSum = kWeight[0];

    for (int i = 1; i < 5; ++i) {
        const vec2 offset = pc.dir.xy * float(i);

        const vec2 uvA = vUV + offset;
        const float zA = linearDepth(texture(depthTex, uvA).r);
        const float wA = kWeight[i] * max(0.0, 1.0 - abs(zA - centerZ) / tolerance);
        sum += texture(srcTex, uvA).r * wA;
        weightSum += wA;

        const vec2 uvB = vUV - offset;
        const float zB = linearDepth(texture(depthTex, uvB).r);
        const float wB = kWeight[i] * max(0.0, 1.0 - abs(zB - centerZ) / tolerance);
        sum += texture(srcTex, uvB).r * wB;
        weightSum += wB;
    }

    outAo = sum / max(weightSum, 1e-5);
}
