// Scene light block shared by the *_lit fragment variants. It declares set 0
// binding 2, so only pipelines whose descriptor layout has that binding may use
// it; the plain variants of the same shaders stay compatible with 2-binding
// layouts (the editor viewport still uses those).
//
// Must stay in sync with dash::vkexp::SceneLightsUbo (src/rendering/vulkan/SceneRenderer.h)
// and dash::vkexp::kMaxSceneLights (src/rendering/vulkan/RenderTypes.h).

const int kMaxSceneLights = 8;
const int kShadowCascades = 3;

struct SceneLight {
    vec4 posType;   // xyz = world position, w = type (0 directional, 1 point, 2 spot)
    vec4 dirRange;  // xyz = emission direction, w = range in world units
    vec4 colorInt;  // rgb = color, a = intensity
    vec4 cone;      // x = inner cone cosine, y = outer cone cosine
};

layout(set = 0, binding = 2) uniform SceneLightsUBO {
    vec4 cameraPos;                          // xyz = eye position
    mat4 shadowMatrices[kShadowCascades];    // world -> light clip space, per cascade
    vec4 shadowSplits;                       // xyz = far camera distance of each cascade
    vec4 shadowTexels;                       // xyz = world texel size, per cascade
    vec4 shadowDepthBias;                    // xyz = depth bias in clip units, per cascade
    vec4 shadowParams;                       // see shadow_sample.glsl
    SceneLight lights[kMaxSceneLights];
} scene;

#ifdef DASH_SHADOWS
#include "shadow_sample.glsl"
#endif

#include "pbr.glsl"

// Cook-Torrance accumulation over the first `count` scene lights. Returns the
// final surface colour, not a multiplier: albedo enters inside the BRDF.
vec3 accumulateSceneLights(vec3 N, vec3 worldPos, int count, float ambient,
                           vec3 albedo, float metallic, float roughness)
{
    const vec3 V = normalize(scene.cameraPos.xyz - worldPos);
    // GGX blows up as roughness reaches zero; this keeps the highlight finite.
    roughness = clamp(roughness, 0.045, 1.0);
    metallic  = clamp(metallic, 0.0, 1.0);

    vec3 acc = dashPbrAmbient(N, V, albedo, metallic, roughness, ambient,
                              kDashSkyAmbient, kDashGroundAmbient);

    for (int i = 0; i < min(count, kMaxSceneLights); ++i) {
        const SceneLight l = scene.lights[i];
        const int type = int(l.posType.w);

        vec3 L;
        float attenuation = 1.0;
        if (type == 0) {
            L = normalize(-l.dirRange.xyz);
        } else {
            const vec3 toLight = l.posType.xyz - worldPos;
            const float distance = length(toLight);
            L = distance > 1e-4 ? toLight / distance : vec3(0.0, 1.0, 0.0);

            const float falloff = clamp(1.0 - distance / max(l.dirRange.w, 1e-4), 0.0, 1.0);
            attenuation = falloff * falloff;

            if (type == 2) {
                const float cosAngle = dot(normalize(-l.dirRange.xyz), L);
                attenuation *= smoothstep(l.cone.y, l.cone.x, cosAngle);
            }
        }
        if (attenuation <= 0.0) continue;

        vec3 radiance = l.colorInt.rgb * (l.colorInt.a * kDashLightUnit) * attenuation;
        const float NdotL = max(dot(N, L), 0.0);

#ifdef DASH_SHADOWS
        // Only one light owns the shadow map; the rest stay unshadowed.
        if (i == int(scene.shadowParams.x) - 1) {
            radiance *= dashShadowFactor(worldPos, N, NdotL);
        }
#endif

        acc += dashPbrDirect(N, V, L, radiance, albedo, metallic, roughness);
    }

    return acc;
}
