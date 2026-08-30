#version 450

// Exposure -> ACES tonemap -> lift/gamma/gain + contrast + saturation.
// Shared by the runtime and the editor viewport: tuning.w picks whether the
// result is written linear (the runtime swapchain is _SRGB and the hardware
// encodes) or sRGB-encoded by hand (the editor hands the image to ImGui, which
// samples it raw onto a _UNORM swapchain).

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uHdr;

layout(push_constant) uniform TonemapPC {
    vec4 tuning;  // x = exposure, y = contrast, z = saturation, w = encode sRGB
    vec4 lift;    // rgb = shadow offset
    vec4 gammaC;  // rgb = midtone balance
    vec4 gain;    // rgb = highlight scale
} pc;

// Narkowicz 2015, "ACES Filmic Tone Mapping Curve".
vec3 acesFilmic(vec3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 linearToSrgb(vec3 c)
{
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(max(c, vec3(1e-5)), vec3(1.0 / 2.4)) - 0.055;
    return mix(hi, lo, step(c, vec3(0.0031308)));
}

// Grading runs in a gamma-2.2 working space, where an offset reads as a uniform
// shift and a multiply as a uniform gain — in linear both crush the shadows.
vec3 grade(vec3 c)
{
    vec3 g = pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));

    g = pc.gain.rgb * (g + pc.lift.rgb * (1.0 - g));
    g = pow(max(g, vec3(1e-4)), vec3(1.0) / max(pc.gammaC.rgb, vec3(1e-3)));
    g = (g - 0.5) * pc.tuning.y + 0.5;

    float luma = dot(g, vec3(0.2126, 0.7152, 0.0722));
    g = mix(vec3(luma), g, pc.tuning.z);

    return pow(clamp(g, 0.0, 1.0), vec3(2.2));
}

void main()
{
    vec3 hdr = texture(uHdr, vUV).rgb;

    // Combat hit flash. The tint rides in the three .w slots the grading
    // vectors leave spare, already multiplied by its strength — which is why
    // the strength can be read back as the max component.
    vec3 flash = vec3(pc.lift.w, pc.gammaC.w, pc.gain.w);
    float hit = clamp(max(flash.r, max(flash.g, flash.b)), 0.0, 1.0);
    if (hit > 0.0) {
        // Weighted toward the edges so the centre of the screen stays readable
        // while the player is being hit.
        float vig = smoothstep(0.10, 0.80, length(vUV - vec2(0.5)) * 1.42);
        float w = hit * mix(0.45, 1.0, vig);
        // Mixed before ACES so the tint saturates on the curve instead of
        // flat-topping, and so the grade treats it like any other light.
        hdr = mix(hdr, flash * 2.5 + hdr * 0.35, w);
    }

    vec3 color = grade(acesFilmic(hdr * pc.tuning.x));

    outColor = vec4(pc.tuning.w > 0.5 ? linearToSrgb(color) : color, 1.0);
}
