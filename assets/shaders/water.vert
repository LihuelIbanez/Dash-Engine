#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;
layout(location = 3) in uint inTexIndicesPacked;
layout(location = 4) in uint inTexWeightsPacked;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out float vOpacity;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProj;
} ubo;

layout(push_constant) uniform TerrainPC {
    vec4 data0;  // eyePos.xyz, time
    vec4 data1;
    vec4 data2;
    vec4 data3;
} pc;

void main() {
    vec3 pos = inPos;
    float time = pc.data0.w;

    // Water wave animation
    pos.y += sin(pos.x * 2.0 + time * 1.5) * 0.03
           + sin(pos.z * 1.7 + time * 1.2) * 0.02;

    gl_Position = ubo.viewProj * vec4(pos, 1.0);
    vColor    = inColor;
    vWorldPos = pos;

    // Decode opacity from texWeightsPacked (stored as first weight / 255)
    float w0 = float((inTexWeightsPacked >> 0) & 0xFFu) / 255.0;
    vOpacity = max(w0, 0.4);
}
