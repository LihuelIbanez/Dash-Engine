// Scene light block shared by the *_lit fragment variants. It declares set 0
// binding 2, so only pipelines whose descriptor layout has that binding may use
// it; the plain variants of the same shaders stay compatible with 2-binding
// layouts (the editor viewport still uses those).
//
// Must stay in sync with dash::vkexp::SceneLightsUbo (src/rendering/vulkan/SceneRenderer.h)
// and dash::vkexp::kMaxSceneLights (src/rendering/vulkan/RenderTypes.h).

const int kMaxSceneLights = 8;

struct SceneLight {
    vec4 posType;   // xyz = world position, w = type (0 directional, 1 point, 2 spot)
    vec4 dirRange;  // xyz = emission direction, w = range in world units
    vec4 colorInt;  // rgb = color, a = intensity
    vec4 cone;      // x = inner cone cosine, y = outer cone cosine
};

layout(set = 0, binding = 2) uniform SceneLightsUBO {
    vec4 cameraPos;                     // xyz = eye position
    SceneLight lights[kMaxSceneLights];
} scene;

vec3 hemisphericAmbient(vec3 N, float strength)
{
    const vec3 kSky    = vec3(0.58, 0.64, 0.78);
    const vec3 kGround = vec3(0.26, 0.22, 0.18);
    return mix(kGround, kSky, N.y * 0.5 + 0.5) * strength;
}

// Blinn-Phong accumulation over the first `count` scene lights.
vec3 accumulateSceneLights(vec3 N, vec3 worldPos, int count,
                           float ambient, float specStrength, float shininess)
{
    const vec3 V = normalize(scene.cameraPos.xyz - worldPos);
    vec3 acc = hemisphericAmbient(N, ambient);

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

        const vec3 radiance = l.colorInt.rgb * l.colorInt.a * attenuation;
        const float NdotL = max(dot(N, L), 0.0);
        acc += radiance * NdotL;

        const vec3 H = normalize(L + V);
        const float spec = pow(max(dot(N, H), 0.0), max(shininess, 1.0));
        acc += radiance * spec * specStrength * step(1e-4, NdotL);
    }

    return acc;
}
