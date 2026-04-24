#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) flat in uvec4 vTexIndices;
layout(location = 4) in vec4 vTexWeights;

layout(location = 0) out vec4 outColor;

// Push constants packed as vec4 to avoid alignment issues
layout(push_constant) uniform TerrainPC {
    vec4 data0;  // eyePos.xyz, time
    vec4 data1;  // fogStart, fogEnd, lightDir.x, lightDir.y
    vec4 data2;  // lightDir.z, lightIntensity, lightColor.r, lightColor.g
    vec4 data3;  // lightColor.b, ambientStrength, specularStrength, specularShininess
} pc;

void main() {
    // Unpack push constants
    vec3  eyePos     = pc.data0.xyz;
    float fogStart   = pc.data1.x;
    float fogEnd     = pc.data1.y;
    vec3  lightDir   = vec3(pc.data1.z, pc.data1.w, pc.data2.x);
    float intensity  = pc.data2.y;
    vec3  lightColor = vec3(pc.data2.z, pc.data2.w, pc.data3.x);
    float ambientStr = pc.data3.y;

    // Use vertex color as base (texture array sampling can be added later)
    vec3 baseColor = vColor;

    vec3 N = normalize(vNormal);
    vec3 L = normalize(lightDir);

    // Daylight: ambient + Lambertian diffuse
    float NdotL = max(dot(N, L), 0.0);
    float light = ambientStr + NdotL * intensity;
    vec3 lit = baseColor * light * lightColor;

    // Distance fog (daylight sky)
    float dist = length(eyePos - vWorldPos);
    float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart + 0.001), 0.0, 1.0);
    lit = mix(lit, vec3(0.72, 0.82, 0.95), fogFactor);

    outColor = vec4(lit, 1.0);
}
