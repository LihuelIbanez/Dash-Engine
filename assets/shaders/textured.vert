#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vTexCoord;
layout(location = 2) out vec3 vColor;
layout(location = 3) out vec3 vWorldPos;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProj;
} ubo;

layout(push_constant) uniform InstancePC {
    mat4 model;
    vec4 color;
    vec4 lightDir;
} pc;

void main()
{
    vec4 worldPos = pc.model * vec4(inPos, 1.0);
    gl_Position = ubo.viewProj * worldPos;
    vNormal = transpose(inverse(mat3(pc.model))) * inNormal;
    vTexCoord = inTexCoord;
    vColor = clamp(pc.color.xyz, 0.0, 1.0);
    vWorldPos = worldPos.xyz;
}
