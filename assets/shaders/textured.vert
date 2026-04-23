#version 450

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec2 vTexCoord;
layout(location = 2) out vec3 vColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProj;
} ubo;

layout(push_constant) uniform InstancePC {
    vec4 offset;
    vec4 scale;
    vec4 color;
} pc;

void main()
{
    vec3 instanceScale = max(pc.scale.xyz, vec3(0.001));
    vec3 worldPos = (inPos * instanceScale) + pc.offset.xyz;
    gl_Position = ubo.viewProj * vec4(worldPos, 1.0);
    vNormal = inNormal;
    vTexCoord = inTexCoord;
    vColor = clamp(pc.color.xyz, 0.0, 1.0);
}
