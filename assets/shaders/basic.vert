#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProj;
} ubo;

layout(push_constant) uniform InstancePC {
    mat4 model;
    vec4 color;    // xyz = color, w = alpha
    vec4 lightDir; // xyz = light direction, w = intensity
} pc;

void main()
{
    vec4 worldPos = pc.model * vec4(inPos, 1.0);
    gl_Position = ubo.viewProj * worldPos;
    vColor = clamp(pc.color.xyz, 0.0, 1.0);
    vNormal = transpose(inverse(mat3(pc.model))) * inNormal;
    vWorldPos = worldPos.xyz;
}
