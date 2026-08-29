#version 450

layout(location = 0) in vec3 vColor;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) flat in uvec4 vTexIndices;
layout(location = 4) in vec4 vTexWeights;
layout(location = 6) in float vAo;

layout(location = 0) out vec4 outColor;

// Binding 1 stays a plain sampler2D for the other pipelines sharing this set.
layout(set = 0, binding = 4) uniform sampler2DArray texArray;

#ifdef DASH_SHADOWS
// Pulled in for the SceneLightsUBO block that carries the shadow matrix; the
// per-light accumulation it also declares is unused here.
#include "scene_lights.glsl"
#else
#include "pbr.glsl"
#endif

// Push constants packed as vec4 to avoid alignment issues
layout(push_constant) uniform TerrainPC {
    vec4 data0;  // eyePos.xyz, time
    vec4 data1;  // fogStart, fogEnd, lightDir.x, lightDir.y
    vec4 data2;  // lightDir.z, lightIntensity, lightColor.r, lightColor.g
    vec4 data3;  // lightColor.b, ambientStrength, unused, unused
    vec4 rough0; // scalar roughness of terrain layers 0..3
    vec4 rough1; // layers 4..7
    vec4 rough2; // x = layer 8
} pc;

// Mirrors dash::vkexp::packTerrainLayerRoughness.
float layerRoughness(uint layer)
{
    if (layer < 4u) return pc.rough0[layer];
    if (layer < 8u) return pc.rough1[layer - 4u];
    return pc.rough2[min(layer - 8u, 3u)];
}

void main() {
    // Unpack push constants
    vec3  eyePos     = pc.data0.xyz;
    float fogStart   = pc.data1.x;
    float fogEnd     = pc.data1.y;
    vec3  lightDir   = vec3(pc.data1.z, pc.data1.w, pc.data2.x);
    float intensity  = pc.data2.y;
    vec3  lightColor = vec3(pc.data2.z, pc.data2.w, pc.data3.x);
    float ambientStr = pc.data3.y;

    // World-space tiled UVs
    vec2 uv = vWorldPos.xz * 0.5;

    // Sample and blend up to 4 texture layers. Roughness rides the same splat
    // weights as the albedo, so a layer never shades with a neighbour's value.
    vec3 baseColor = vec3(0.0);
    float roughness = 0.0;
    float totalWeight = 0.0;

    if (vTexWeights.x > 0.001) {
        baseColor += texture(texArray, vec3(uv, float(vTexIndices.x))).rgb * vTexWeights.x;
        roughness += layerRoughness(vTexIndices.x) * vTexWeights.x;
        totalWeight += vTexWeights.x;
    }
    if (vTexWeights.y > 0.001) {
        baseColor += texture(texArray, vec3(uv, float(vTexIndices.y))).rgb * vTexWeights.y;
        roughness += layerRoughness(vTexIndices.y) * vTexWeights.y;
        totalWeight += vTexWeights.y;
    }
    if (vTexWeights.z > 0.001) {
        baseColor += texture(texArray, vec3(uv, float(vTexIndices.z))).rgb * vTexWeights.z;
        roughness += layerRoughness(vTexIndices.z) * vTexWeights.z;
        totalWeight += vTexWeights.z;
    }
    if (vTexWeights.w > 0.001) {
        baseColor += texture(texArray, vec3(uv, float(vTexIndices.w))).rgb * vTexWeights.w;
        roughness += layerRoughness(vTexIndices.w) * vTexWeights.w;
        totalWeight += vTexWeights.w;
    }

    // Fallback to vertex color if no texture weights
    if (totalWeight < 0.01) {
        baseColor = vColor;
        roughness = 0.85;
    } else {
        roughness /= totalWeight;
        // Modulate with vertex color for subtle tint variation. Clamped because
        // an albedo above 1 would let the BRDF create energy.
        baseColor = clamp(baseColor * vColor * 1.5, 0.0, 1.0);
    }

    vec3 N = normalize(vNormal);
    vec3 L = normalize(lightDir);
    vec3 V = normalize(eyePos - vWorldPos);

    float NdotL = max(dot(N, L), 0.0);

    float shadow = 1.0;
#ifdef DASH_SHADOWS
    shadow = dashShadowFactor(vWorldPos, N, NdotL);
#endif

    // Terrain is dielectric everywhere: the splat table has no metal layers.
    const float kMetallic = 0.0;
    const vec3 radiance = lightColor * (intensity * kDashLightUnit) * shadow;
    vec3 lit = dashPbrDirect(N, V, L, radiance, baseColor, kMetallic, roughness);
    // Baked valley/cliff occlusion darkens only the ambient term; direct light
    // is already handled by the cascades.
    lit += dashPbrAmbient(N, V, baseColor, kMetallic, roughness, ambientStr,
                          kDashSkyAmbient * lightColor, kDashGroundAmbient * lightColor)
           * clamp(vAo, 0.0, 1.0);

    // Distance fog (sky color)
    float dist = length(eyePos - vWorldPos);
    float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart + 0.001), 0.0, 1.0);
    lit = mix(lit, vec3(0.72, 0.82, 0.95), fogFactor);

    outColor = vec4(lit, 1.0);
}
