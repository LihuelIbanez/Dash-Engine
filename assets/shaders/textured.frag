#version 450

layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec2 vTexCoord;
layout(location = 2) in vec3 vColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2D texSampler;

void main()
{
    vec4 texColor = texture(texSampler, vTexCoord);
    // Simple directional light
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    vec3 normal = normalize(vNormal);
    float light = max(dot(normal, lightDir), 0.3);
    outColor = vec4(texColor.rgb * vColor * light, texColor.a);
}
