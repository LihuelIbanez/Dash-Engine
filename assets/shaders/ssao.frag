#version 450

// Half-resolution SSAO over the depth-only prepass recorded immediately before
// this pass. The renderer is forward, so there is no normal buffer: the normal
// is rebuilt from the derivatives of the reconstructed view-space position.
//
// Vertex stage is assets/shaders/tonemap.vert (fullscreen triangle, vUV at
// location 0). Output feeds the separable bilateral blur in ssao_blur.frag.

layout(location = 0) in vec2 vUV;
layout(location = 0) out float outAo;

layout(set = 0, binding = 1) uniform sampler2D depthTex;

layout(push_constant) uniform SsaoPC {
    vec4 proj;    // x = P[0][0], y = P[1][1], z = zNear, w = zFar
    vec4 params;  // x = radius (world units), y = intensity, z = bias, w = power
    vec4 texel;   // xy = 1 / aoResolution, zw = aoResolution
} pc;

const int kSampleCount = 16;

// Golden-angle spiral over the unit hemisphere: even coverage with no noise
// texture to upload. The per-sample radius below is driven by a decorrelated
// golden-ratio sequence so near and far taps are not tied to the polar angle.
const vec3 kKernel[16] = vec3[16](
    vec3( 0.999512,  0.000000, 0.031250),
    vec3(-0.734121,  0.672515, 0.093750),
    vec3( 0.086490, -0.983946, 0.156250),
    vec3( 0.593565,  0.774488, 0.218750),
    vec3(-0.944918, -0.167153, 0.281250),
    vec3( 0.791443, -0.504442, 0.343750),
    vec3(-0.237868,  0.882264, 0.406250),
    vec3(-0.407035, -0.784636, 0.468750),
    vec3( 0.795861,  0.290523, 0.531250),
    vec3(-0.743907,  0.306693, 0.593750),
    vec3( 0.319795, -0.683418, 0.656250),
    vec3( 0.208269,  0.663362, 0.718750),
    vec3(-0.539774, -0.313548, 0.781250),
    vec3( 0.524148, -0.115581, 0.843750),
    vec3(-0.242927,  0.345976, 0.906250),
    vec3(-0.031893, -0.245980, 0.968750)
);

// Distance from the eye along -Z, from the [0,1] depth VkMath::perspective writes.
float linearDepth(float d)
{
    const float zn = pc.proj.z;
    const float zf = pc.proj.w;
    return (zn * zf) / max(zf - d * (zf - zn), 1e-6);
}

vec3 viewPosAt(vec2 uv, out float rawDepth)
{
    rawDepth = texture(depthTex, uv).r;
    const float vz = linearDepth(rawDepth);
    const vec2 ndc = uv * 2.0 - 1.0;
    return vec3(ndc.x * vz / pc.proj.x, ndc.y * vz / pc.proj.y, -vz);
}

void main()
{
    float centerDepth;
    const vec3 P = viewPosAt(vUV, centerDepth);

    // Nothing was drawn here: the sky must not receive occlusion.
    if (centerDepth >= 0.999999) {
        outAo = 1.0;
        return;
    }

    // Pick the closer neighbour on each axis so the normal does not smear
    // across a silhouette and darken the object in front of it.
    float dummy;
    const vec3 pR = viewPosAt(vUV + vec2(pc.texel.x, 0.0), dummy);
    const vec3 pL = viewPosAt(vUV - vec2(pc.texel.x, 0.0), dummy);
    const vec3 pD = viewPosAt(vUV + vec2(0.0, pc.texel.y), dummy);
    const vec3 pU = viewPosAt(vUV - vec2(0.0, pc.texel.y), dummy);

    const vec3 dx = abs(pR.z - P.z) < abs(P.z - pL.z) ? (pR - P) : (P - pL);
    const vec3 dy = abs(pD.z - P.z) < abs(P.z - pU.z) ? (pD - P) : (P - pU);

    vec3 N = cross(dx, dy);
    const float nLen = length(N);
    if (nLen < 1e-8) {
        outAo = 1.0;
        return;
    }
    N /= nLen;
    if (N.z < 0.0) N = -N;

    // Per-pixel rotation of the kernel; the blur that follows turns the
    // resulting interleaved noise back into a smooth gradient.
    const float rnd = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    const float angle = rnd * 6.2831853;
    const vec3 rvec = vec3(cos(angle), sin(angle), 0.0);

    const vec3 tangentRaw = rvec - N * dot(rvec, N);
    const float tLen = length(tangentRaw);
    const vec3 T = tLen > 1e-4 ? tangentRaw / tLen : normalize(cross(N, vec3(0.0, 0.0, 1.0)));
    const mat3 TBN = mat3(T, cross(N, T), N);

    const float radius = max(pc.params.x, 1e-3);
    float occlusion = 0.0;

    for (int i = 0; i < kSampleCount; ++i) {
        const float f = fract((float(i) + 0.5) * 0.6180339887);
        const vec3 samplePos = P + TBN * kKernel[i] * (radius * mix(0.2, 1.0, f * f));
        if (samplePos.z > -1e-3) continue;

        const vec2 sndc = vec2(pc.proj.x * samplePos.x, pc.proj.y * samplePos.y) / (-samplePos.z);
        const vec2 suv = sndc * 0.5 + 0.5;
        if (any(lessThan(suv, vec2(0.0))) || any(greaterThan(suv, vec2(1.0)))) continue;

        const float sceneVz = linearDepth(texture(depthTex, suv).r);
        const float sampleVz = -samplePos.z;

        // Only occluders inside the radius count, so a wall far behind a thin
        // object cannot swallow it whole.
        const float rangeCheck = smoothstep(0.0, 1.0, radius / max(abs(sampleVz - sceneVz), 1e-4));
        occlusion += (sceneVz <= sampleVz - pc.params.z) ? rangeCheck : 0.0;
    }

    const float ao = 1.0 - (occlusion / float(kSampleCount)) * pc.params.y;
    outAo = pow(clamp(ao, 0.0, 1.0), max(pc.params.w, 1e-3));
}
