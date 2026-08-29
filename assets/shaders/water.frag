#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in float vOpacity;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform TerrainPC {
    vec4 data0;  // eyePos.xyz, time
    vec4 data1;  // fogStart, fogEnd, lightDir.x, lightDir.y
    vec4 data2;  // lightDir.z, lightIntensity, lightColor.r, lightColor.g
    vec4 data3;  // lightColor.b, ambientStrength, unused, unused
} pc;

void main() {
    vec3 eyePos   = pc.data0.xyz;
    float fogStart = pc.data1.x;
    float fogEnd   = pc.data1.y;
    vec3 lightDir  = vec3(pc.data1.z, pc.data1.w, pc.data2.x);
    float intensity = pc.data2.y;
    vec3 lightColor = vec3(pc.data2.z, pc.data2.w, pc.data3.x);
    float ambientStr = pc.data3.y;

    // Water surface normal (flat plane pointing up)
    vec3 N = vec3(0.0, 1.0, 0.0);
    vec3 L = normalize(lightDir);
    vec3 V = normalize(eyePos - vWorldPos);
    vec3 H = normalize(L + V);

    // Ambient + diffuse
    float NdotL = max(dot(N, L), 0.0);
    float light = ambientStr + NdotL * intensity;

    // Specular highlight for water surface
    float spec = pow(max(dot(N, H), 0.0), 64.0) * 0.4;

    vec3 lit = vColor * light * lightColor + vec3(spec);

    // Distance fog
    float dist = length(eyePos - vWorldPos);
    float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart + 0.001), 0.0, 1.0);
    lit = mix(lit, vec3(0.72, 0.82, 0.95), fogFactor);

    outColor = vec4(lit, vOpacity);
}
