// Directional shadow map lookup, shared by every fragment shader compiled with
// -DDASH_SHADOWS. It declares set 0 binding 3, so only pipelines whose
// descriptor layout carries that binding may include it — the editor viewport
// still builds the "_lit" variants, which do not.
//
// The matrix and the tuning values travel inside the SceneLightsUBO block
// (set 0, binding 2) declared by scene_lights.glsl, so no extra buffer is needed.

layout(set = 0, binding = 3) uniform sampler2DShadow dashShadowMap;

// params.x = shadow-casting light index + 1 (0 disables the whole lookup)
// params.y = 1 / shadow map resolution
// params.z = depth bias in light clip units, measured at normal incidence
// params.w = normal offset in world units
float dashShadowFactor(mat4 lightViewProj, vec4 params, vec3 worldPos, vec3 N, float NdotL)
{
    if (params.x < 0.5) return 1.0;

    // Normal offset moves the lookup off the surface along the geometric
    // normal. It fixes the acne a depth bias alone cannot reach on curved
    // geometry, and costs no peter-panning because the offset shrinks to zero
    // where the surface faces the light.
    const float slope = clamp(1.0 - NdotL, 0.0, 1.0);
    const vec3 samplePos = worldPos + N * params.w * slope;

    const vec4 clip = lightViewProj * vec4(samplePos, 1.0);
    if (clip.w <= 0.0) return 1.0;

    const vec3 ndc = clip.xyz / clip.w;
    // Outside the light volume nothing was rendered, so nothing can occlude.
    if (any(lessThan(ndc.xy, vec2(-1.0))) || any(greaterThan(ndc.xy, vec2(1.0))) ||
        ndc.z < 0.0 || ndc.z > 1.0) {
        return 1.0;
    }

    const vec2 uv = ndc.xy * 0.5 + 0.5;
    // Slope-scaled: a surface edge-on to the light covers more depth per texel
    // than one facing it, so it needs a proportionally larger offset.
    const float ref = ndc.z - params.z * (1.0 + 4.0 * slope);

    // 3x3 taps on a comparison sampler: each tap is already bilinear-filtered
    // by the hardware, so the kernel covers roughly 4x4 texels of penumbra.
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y) {
        for (int x = -1; x <= 1; ++x) {
            sum += texture(dashShadowMap, vec3(uv + vec2(x, y) * params.y, ref));
        }
    }
    return sum * (1.0 / 9.0);
}
