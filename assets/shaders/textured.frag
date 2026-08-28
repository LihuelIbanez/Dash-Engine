#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in vec3 vColor;
layout(location = 3) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D texSampler;

layout(push_constant) uniform InstancePC {
    mat4 model;
    vec4 color;
    vec4 lightDir;
    vec4 lightParams; // x = active scene lights, y = ambient, z = spec strength, w = shininess
} pc;

#ifdef DASH_SCENE_LIGHTS
#include "scene_lights.glsl"
#endif

void main()
{
    vec4 texColor = texture(texSampler, vTexCoord);
    vec3 normal = normalize(vNormal);

#ifdef DASH_SCENE_LIGHTS
    int lightCount = int(pc.lightParams.x);
    if (lightCount > 0) {
        vec3 lit = accumulateSceneLights(normal, vWorldPos, lightCount,
                                         pc.lightParams.y, pc.lightParams.z, pc.lightParams.w);
        outColor = vec4(texColor.rgb * vColor * lit, texColor.a);
        return;
    }
#endif

    // Simple directional light
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float light = max(dot(normal, lightDir), 0.3);
    outColor = vec4(texColor.rgb * vColor * light, texColor.a);
}
