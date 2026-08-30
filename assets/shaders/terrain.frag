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
// Tangent-space normal (OpenGL Y+ convention) in rgb, roughness in a.
layout(set = 0, binding = 6) uniform sampler2DArray texNormalRough;

#ifdef DASH_SHADOWS
// Pulled in for the SceneLightsUBO block that carries the shadow matrix; the
// per-light accumulation it also declares is unused here.
#include "scene_lights.glsl"
#else
#include "pbr.glsl"
#include "ssao_sample.glsl"
#endif

// Push constants packed as vec4 to avoid alignment issues
layout(push_constant) uniform TerrainPC {
    vec4 data0;  // eyePos.xyz, time
    vec4 data1;  // fogStart, fogEnd, lightDir.x, lightDir.y
    vec4 data2;  // lightDir.z, lightIntensity, lightColor.r, lightColor.g
    vec4 data3;  // lightColor.b, ambientStrength, unused, unused
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

    // World-space tiled UVs
    vec2 uv = vWorldPos.xz * 0.5;

    // Sample and blend up to 4 texture layers. Normal and roughness ride the
    // same splat weights as the albedo, so a layer never shades with a
    // neighbour's value.
    vec3 baseColor = vec3(0.0);
    vec3 tangentNormal = vec3(0.0);
    float roughness = 0.0;
    float totalWeight = 0.0;

    if (vTexWeights.x > 0.001) {
        baseColor += texture(texArray, vec3(uv, float(vTexIndices.x))).rgb * vTexWeights.x;
        vec4 nr = texture(texNormalRough, vec3(uv, float(vTexIndices.x)));
        tangentNormal += (nr.xyz * 2.0 - 1.0) * vTexWeights.x;
        roughness += nr.a * vTexWeights.x;
        totalWeight += vTexWeights.x;
    }
    if (vTexWeights.y > 0.001) {
        baseColor += texture(texArray, vec3(uv, float(vTexIndices.y))).rgb * vTexWeights.y;
        vec4 nr = texture(texNormalRough, vec3(uv, float(vTexIndices.y)));
        tangentNormal += (nr.xyz * 2.0 - 1.0) * vTexWeights.y;
        roughness += nr.a * vTexWeights.y;
        totalWeight += vTexWeights.y;
    }
    if (vTexWeights.z > 0.001) {
        baseColor += texture(texArray, vec3(uv, float(vTexIndices.z))).rgb * vTexWeights.z;
        vec4 nr = texture(texNormalRough, vec3(uv, float(vTexIndices.z)));
        tangentNormal += (nr.xyz * 2.0 - 1.0) * vTexWeights.z;
        roughness += nr.a * vTexWeights.z;
        totalWeight += vTexWeights.z;
    }
    if (vTexWeights.w > 0.001) {
        baseColor += texture(texArray, vec3(uv, float(vTexIndices.w))).rgb * vTexWeights.w;
        vec4 nr = texture(texNormalRough, vec3(uv, float(vTexIndices.w)));
        tangentNormal += (nr.xyz * 2.0 - 1.0) * vTexWeights.w;
        roughness += nr.a * vTexWeights.w;
        totalWeight += vTexWeights.w;
    }

    vec3 Ngeo = normalize(vNormal);
    vec3 N = Ngeo;

    // Fallback to vertex color if no texture weights
    if (totalWeight < 0.01) {
        baseColor = vColor;
        roughness = 0.85;
    } else {
        roughness /= totalWeight;
        // Modulate with vertex color for subtle tint variation. Clamped because
        // an albedo above 1 would let the BRDF create energy.
        baseColor = clamp(baseColor * vColor * 1.5, 0.0, 1.0);

        // The layer UVs run along world X and Z, so the tangent frame is the
        // geometric normal with world X projected out of it.
        vec3 axis = abs(Ngeo.x) < 0.9 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0);
        vec3 T = normalize(axis - Ngeo * dot(Ngeo, axis));
        vec3 B = cross(T, Ngeo);
        vec3 nTs = tangentNormal / totalWeight;
        if (dot(nTs, nTs) > 1e-6)
            N = normalize(T * nTs.x + B * nTs.y + Ngeo * nTs.z);
    }

    vec3 L = normalize(lightDir);
    vec3 V = normalize(eyePos - vWorldPos);

    float NdotL = max(dot(N, L), 0.0);

    float shadow = 1.0;
#ifdef DASH_SHADOWS
    shadow = dashShadowFactor(vWorldPos, Ngeo, NdotL);
#endif

    // Terrain is dielectric everywhere: the splat table has no metal layers.
    const float kMetallic = 0.0;
    const vec3 radiance = lightColor * (intensity * kDashLightUnit) * shadow;
    vec3 lit = dashPbrDirect(N, V, L, radiance, baseColor, kMetallic, roughness);
    // Baked valley/cliff occlusion times the screen-space contact term; both
    // darken only the ambient, direct light is already handled by the cascades.
    lit += dashPbrAmbient(N, V, baseColor, kMetallic, roughness, ambientStr,
                          kDashSkyAmbient * lightColor, kDashGroundAmbient * lightColor)
           * clamp(vAo, 0.0, 1.0) * dashSsaoFactor();

    // Distance fog (sky color)
    float dist = length(eyePos - vWorldPos);
    float fogFactor = clamp((dist - fogStart) / (fogEnd - fogStart + 0.001), 0.0, 1.0);
    lit = mix(lit, vec3(0.72, 0.82, 0.95), fogFactor);

    outColor = vec4(lit, 1.0);
}
