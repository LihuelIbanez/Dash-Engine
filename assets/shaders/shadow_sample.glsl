// Cascaded directional shadow lookup, shared by every fragment shader compiled
// with -DDASH_SHADOWS. It declares set 0 binding 3, so only pipelines whose
// descriptor layout carries that binding may include it — the editor viewport
// still builds the "_lit" variants, which do not.
//
// The matrices and the tuning values travel inside the SceneLightsUBO block
// (set 0, binding 2) declared by scene_lights.glsl, so no extra buffer is
// needed. This file must be included after that declaration.

layout(set = 0, binding = 3) uniform sampler2DArrayShadow dashShadowMap;

// One cascade's worth of lookup. `inside` reports whether the point actually
// landed in this cascade, which is what lets the caller fall through.
float dashSampleCascade(int cascade, vec3 worldPos, vec3 N, float NdotL, out bool inside)
{
    inside = false;

    // Normal offset moves the lookup off the surface along the geometric
    // normal. It fixes the acne a depth bias alone cannot reach on curved
    // geometry, and costs no peter-panning because the offset shrinks to zero
    // where the surface faces the light.
    float slope = clamp(1.0 - NdotL, 0.0, 1.0);
    float normalOffset = scene.shadowTexels[cascade] * 1.5;
    vec3 samplePos = worldPos + N * normalOffset * slope;

    vec4 clip = scene.shadowMatrices[cascade] * vec4(samplePos, 1.0);
    if (clip.w <= 0.0) return 1.0;

    vec3 ndc = clip.xyz / clip.w;
    if (any(lessThan(ndc.xy, vec2(-1.0))) || any(greaterThan(ndc.xy, vec2(1.0))) ||
        ndc.z < 0.0 || ndc.z > 1.0) {
        return 1.0;
    }
    inside = true;

    vec2 uv = ndc.xy * 0.5 + 0.5;
    // Slope-scaled: a surface edge-on to the light covers more depth per texel
    // than one facing it, so it needs a proportionally larger offset.
    float ref = ndc.z - scene.shadowDepthBias[cascade] * (1.0 + 4.0 * slope);

    // 3x3 taps on a comparison sampler: each tap is already bilinear-filtered
    // by the hardware, so the kernel covers roughly 4x4 texels of penumbra.
    float texelStep = scene.shadowParams.y;
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            sum += texture(dashShadowMap,
                           vec4(uv + vec2(x, y) * texelStep, float(cascade), ref));
        }
    }
    return sum * (1.0 / 9.0);
}

// params.x = shadow-casting light index + 1 (0 disables the whole lookup)
// params.y = 1 / shadow map resolution
// params.z = width of the cross-fade band, as a fraction of each cascade's range
float dashShadowFactor(vec3 worldPos, vec3 N, float NdotL)
{
    if (scene.shadowParams.x < 0.5) return 1.0;

    float viewDist = length(worldPos - scene.cameraPos.xyz);

    // Past the last split nothing was rendered into any cascade.
    if (viewDist >= scene.shadowSplits[kShadowCascades - 1]) return 1.0;

    int cascade = kShadowCascades - 1;
    for (int i = 0; i < kShadowCascades; ++i) {
        if (viewDist < scene.shadowSplits[i]) { cascade = i; break; }
    }

    bool inside = false;
    float factor = dashSampleCascade(cascade, worldPos, N, NdotL, inside);

    // A point can miss its cascade near the rim of the slice sphere; the next
    // one out always contains it, so fall through rather than lighting it up.
    if (!inside && cascade + 1 < kShadowCascades) {
        factor = dashSampleCascade(cascade + 1, worldPos, N, NdotL, inside);
    }

    // Cross-fade into the next cascade over the tail of this one, or the change
    // in resolution draws a visible ring on the ground.
    if (cascade + 1 < kShadowCascades) {
        float nearSplit = cascade == 0 ? 0.0 : scene.shadowSplits[cascade - 1];
        float farSplit = scene.shadowSplits[cascade];
        float band = (farSplit - nearSplit) * scene.shadowParams.z;
        if (band > 0.0 && viewDist > farSplit - band) {
            bool nextInside = false;
            float nextFactor = dashSampleCascade(cascade + 1, worldPos, N, NdotL, nextInside);
            if (nextInside) {
                factor = mix(factor, nextFactor, (viewDist - (farSplit - band)) / band);
            }
        }
    }

    return factor;
}
