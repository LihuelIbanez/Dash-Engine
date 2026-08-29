#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in uint inTexIndicesPacked;
layout(location = 4) in uint inTexWeightsPacked;
layout(location = 5) in uint inFlags;
layout(location = 6) in float inAo;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) flat out uvec4 vTexIndices;
layout(location = 4) out vec4 vTexWeights;
layout(location = 5) flat out uint vFlags;
layout(location = 6) out float vAo;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProj;
} ubo;

// Push constants: packed as vec4 to avoid alignment issues
// [0] = vec4(eyePos.xyz, time)
// [1] = vec4(fogStart, fogEnd, lightDir.x, lightDir.y)
// [2] = vec4(lightDir.z, lightIntensity, lightColor.r, lightColor.g)
// [3] = vec4(lightColor.b, ambientStrength, unused, unused)
layout(push_constant) uniform TerrainPC {
    vec4 data0;
    vec4 data1;
    vec4 data2;
    vec4 data3;
} pc;

void main() {
    vec3 pos = inPos;
    float time = pc.data0.w;

    // Water animation: detect water by blue-dominant vertex color
    if (inColor.b > inColor.r * 2.0 && inColor.b > inColor.g * 1.5) {
        pos.y += sin(pos.x * 2.0 + time * 1.5) * 0.03
               + sin(pos.z * 1.7 + time * 1.2) * 0.02;
    }

    gl_Position = ubo.viewProj * vec4(pos, 1.0);
    vColor    = inColor;
    vNormal   = inNormal;
    vWorldPos = pos;
    vFlags    = inFlags;
    vAo       = inAo;

    // Unpack texture blend data
    vTexIndices = uvec4(
        (inTexIndicesPacked >>  0) & 0xFFu,
        (inTexIndicesPacked >>  8) & 0xFFu,
        (inTexIndicesPacked >> 16) & 0xFFu,
        (inTexIndicesPacked >> 24) & 0xFFu
    );
    vTexWeights = vec4(
        float((inTexWeightsPacked >>  0) & 0xFFu) / 255.0,
        float((inTexWeightsPacked >>  8) & 0xFFu) / 255.0,
        float((inTexWeightsPacked >> 16) & 0xFFu) / 255.0,
        float((inTexWeightsPacked >> 24) & 0xFFu) / 255.0
    );
}
