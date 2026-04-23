#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;

layout(location = 0) out vec4 outColor;

layout(push_constant) uniform TerrainPC {
    vec3  eyePos;
    float time;
    float fogStart;
    float fogEnd;
    float pad0;
    float pad1;
} pc;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(vec3(0.5, 1.0, 0.3));
    vec3 V = normalize(pc.eyePos - vWorldPos);
    vec3 H = normalize(L + V);

    // Hemispheric ambient: sky vs ground
    vec3 skyColor    = vec3(0.6, 0.7, 0.85);
    vec3 groundColor = vec3(0.15, 0.12, 0.10);
    float hemi = N.y * 0.5 + 0.5;
    vec3 ambient = mix(groundColor, skyColor, hemi) * 0.35;

    // Diffuse (Lambertian)
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = vColor * NdotL * 0.55;

    // Specular (Blinn-Phong) — subtle, terrain is matte
    float NdotH = max(dot(N, H), 0.0);
    float spec = pow(NdotH, 32.0) * 0.15;

    vec3 lit = ambient * vColor + diffuse + vec3(spec);

    // Distance fog
    float dist = length(pc.eyePos - vWorldPos);
    float fogFactor = clamp((dist - pc.fogStart) / (pc.fogEnd - pc.fogStart + 0.001), 0.0, 1.0);
    vec3 fogColor = vec3(0.55, 0.62, 0.75);
    lit = mix(lit, fogColor, fogFactor);

    outColor = vec4(lit, 1.0);
}
