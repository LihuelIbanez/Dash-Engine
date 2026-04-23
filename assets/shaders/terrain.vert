#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec3 inColor;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProj;
} ubo;

layout(push_constant) uniform TerrainPC {
    vec3  eyePos;
    float time;
    float fogStart;
    float fogEnd;
    float pad0;
    float pad1;
} pc;

void main() {
    vec3 pos = inPos;

    // Water animation: detect water by blue-dominant vertex color
    if (inColor.b > inColor.r * 2.0 && inColor.b > inColor.g * 1.5) {
        pos.y += sin(pos.x * 2.0 + pc.time * 1.5) * 0.03
               + sin(pos.z * 1.7 + pc.time * 1.2) * 0.02;
    }

    gl_Position = ubo.viewProj * vec4(pos, 1.0);
    vColor    = inColor;
    vNormal   = inNormal;
    vWorldPos = pos;
}
