#version 450

// Same lighting model as basic.frag; kept separate because skinned.vert also
// forwards UVs for a future textured skinned pass.

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in vec2 vTexCoord;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform InstancePC {
    mat4 model;
    vec4 color;
    vec4 lightDir; // xyz = direction, w = intensity
} pc;

void main()
{
    vec3 N = normalize(vNormal);
    vec3 L = normalize(pc.lightDir.xyz);
    float NdotL = max(dot(N, L), 0.0);

    float light = 0.4 + NdotL * pc.lightDir.w * 0.6;

    outColor = vec4(vColor * light, 1.0);
}
