// Cook-Torrance metallic/roughness BRDF, shared by the scene shaders (through
// scene_lights.glsl) and by terrain.frag, which has its own light plumbing.
//
// Everything here is linear and unbounded: the scene renders into the HDR
// target and assets/shaders/tonemap.frag applies exposure + ACES afterwards.

const float kDashPi = 3.14159265359;

// Authored light intensity is normalised so 1.0 lights a white Lambertian
// surface to 1.0 at normal incidence; turning it into radiance costs exactly the
// pi the diffuse lobe divides by. Keeps every scene's authored value meaningful.
const float kDashLightUnit = kDashPi;

// Trowbridge-Reitz GGX normal distribution.
float dashDistributionGGX(float NdotH, float roughness)
{
    const float a  = roughness * roughness;
    const float a2 = a * a;
    const float d  = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(kDashPi * d * d, 1e-7);
}

float dashSchlickGGX(float NdotX, float k)
{
    return NdotX / max(NdotX * (1.0 - k) + k, 1e-7);
}

// Smith geometry term with the direct-lighting remap of k.
float dashGeometrySmith(float NdotV, float NdotL, float roughness)
{
    const float r = roughness + 1.0;
    const float k = (r * r) / 8.0;
    return dashSchlickGGX(NdotV, k) * dashSchlickGGX(NdotL, k);
}

vec3 dashFresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Fresnel that stops rough surfaces from getting a hard rim in the ambient term.
vec3 dashFresnelRoughness(float cosTheta, vec3 F0, float roughness)
{
    const vec3 ceiling = max(vec3(1.0 - roughness), F0);
    return F0 + (ceiling - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Lazarov's analytic fit of the split-sum environment BRDF: x scales F0,
// y is the additive bias. Saves the LUT an IBL pass would otherwise need.
vec2 dashEnvBRDF(float NdotV, float roughness)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572,  0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.040, -0.040);
    const vec4 r  = roughness * c0 + c1;
    const float a004 = min(r.x * r.x, exp2(-9.28 * NdotV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

vec3 dashF0(vec3 albedo, float metallic)
{
    return mix(vec3(0.04), albedo, metallic);
}

// Outgoing radiance from one light. `radiance` already carries
// colour * intensity * attenuation * shadow.
vec3 dashPbrDirect(vec3 N, vec3 V, vec3 L, vec3 radiance,
                   vec3 albedo, float metallic, float roughness)
{
    const float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    const float NdotV = max(dot(N, V), 1e-4);
    const vec3  H     = normalize(L + V);
    const float NdotH = max(dot(N, H), 0.0);
    const float VdotH = max(dot(V, H), 0.0);

    const vec3  F0 = dashF0(albedo, metallic);
    const float D  = dashDistributionGGX(NdotH, roughness);
    const float G  = dashGeometrySmith(NdotV, NdotL, roughness);
    const vec3  F  = dashFresnelSchlick(VdotH, F0);

    const vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 1e-4);
    const vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

    return (kD * albedo / kDashPi + specular) * radiance * NdotL;
}

// Stand-in for IBL: a sky/ground gradient sampled along N for irradiance and
// along the reflection vector for the specular probe, weighted by dashEnvBRDF.
vec3 dashPbrAmbient(vec3 N, vec3 V, vec3 albedo, float metallic, float roughness,
                    float strength, vec3 skyColor, vec3 groundColor)
{
    const float NdotV = max(dot(N, V), 1e-4);
    const vec3  F0    = dashF0(albedo, metallic);
    const vec3  F     = dashFresnelRoughness(NdotV, F0, roughness);

    const vec3 irradiance = mix(groundColor, skyColor, N.y * 0.5 + 0.5) * strength;
    const vec3 diffuse    = (vec3(1.0) - F) * (1.0 - metallic) * albedo * irradiance;

    const vec3 R = reflect(-V, N);
    // A rough surface averages the whole environment, so its probe flattens
    // towards the horizon mix instead of tracking R.
    const float lobe = mix(R.y, 0.0, roughness * roughness);
    const vec3 probe = mix(groundColor, skyColor, lobe * 0.5 + 0.5) * strength;

    const vec2 ab = dashEnvBRDF(NdotV, roughness);
    const vec3 specular = probe * (F0 * ab.x + ab.y);

    return diffuse + specular;
}

// Default environment tint used when a shader has nothing better: cool sky over
// warm earth, which is what the grading in ColorGrading.h is balanced against.
const vec3 kDashSkyAmbient    = vec3(0.58, 0.64, 0.78);
const vec3 kDashGroundAmbient = vec3(0.26, 0.22, 0.18);
