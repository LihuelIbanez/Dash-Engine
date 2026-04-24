#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) flat in uvec4 vTexIndices;
layout(location = 4) in vec4 vTexWeights;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 1) uniform sampler2DArray texArray;

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
    float specStr    = pc.data3.z;
    float specShin   = pc.data3.w;

    // World-space tiled UVs
    vec2 uv = vWorldPos.xz * 0.5;

    // Sample and blend up to 4 texture layers
    vec3 baseColor = vec3(0.0);
    float totalWeight = 0.0;

    if (vTexWeights.x > 0.001) {
        baseColor += texture(texArray, vec3(uv, float(vTexIndices.x))).rgb * vTexWeights.x;
        totalWeight += vTexWeights.x;
    }
    if (vTexWeights.y > 0.001) {
        baseColor += texture(texArray, vec3(uv, float(vTexIndices.y))).rgb * vTexWeights.y;
        totalWeight += vTexWeights.y;
    }
    if (vTexWeights.z > 0.001) {
        baseColor += texture(texArray, vec3(uv, float(vTexIndices.z))).rgb * vTexWeights.z;
        totalWeight += vTexWeights.z;
    }
    if (vTexWeights.w > 0.001) {
        baseColor += texture(texArray, vec3(uv, float(vTexIndices.w))).rgb * vTexWeights.w;
        totalWeight += vTexWeights.w;
    }

    // Fallback to vertex color if no texture weights
    if (totalWeight < 0.01) {
        baseColor = vColor;
    } else {
        // Modulate with vertex color for subtle tint variation
        baseColor *= vColor * 1.5;
    }

    vec3 N = normalize(vNormal);
    vec3 L = normalize(lightDir);
    vec3 V = normalize(eyePos - vWorldPos);
    vec3 H = normalize(L + V);

    // Half-Lambert diffuse for softer shadows
    float NdotL = dot(N, L);
    float diffuse = NdotL * 0.5 + 0.5;
    diffuse = diffuse * diffuse;

    // Hemisphere ambient (sky tint top, earth tint bottom)
    vec3 skyAmbient    = lightColor * 1.1;
    vec3 groundAmbient = lightColor * vec3(0.4, 0.35, 0.3);
    vec3 ambient = mix(groundAmbient, skyAmbient, N.y * 0.5 + 0.5) * ambientStr;

    // Blinn-Phong specular
    float spec = pow(max(dot(N, H), 0.0), specShin) * specStr;

    // Combine
    vec3 lit = baseColor * (ambient + diffuse * intensity * lightColor) + spec * lightColor;

    // Distance fog (sky color)
    float dist = length(eyePos - vWorldPos);
    float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart + 0.001), 0.0, 1.0);
    lit = mix(lit, vec3(0.72, 0.82, 0.95), fogFactor);

    outColor = vec4(lit, 1.0);
}
