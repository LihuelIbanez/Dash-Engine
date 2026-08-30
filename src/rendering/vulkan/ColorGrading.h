#pragma once

#include <cstddef>

namespace dash::vkexp {

// Tuning for the tonemap pass (assets/shaders/tonemap.frag). Defaults aim at
// the Diablo 4 look: dark, cold shadows against a warm key, desaturated and
// high contrast.
//
// Colour balance is lift/gamma/gain rather than temperature+tint because that
// look needs shadows and highlights pulled in *opposite* directions; a white
// balance shift is a single global transform and cannot do that.
struct GradingParams {
    // ACES saturates around 1.0, so this is what decides where mid grey lands.
    float exposure = 1.15f;
    // Around 0.5 in the gamma-2.2 working space: keeps black and white pinned.
    float contrast = 1.16f;
    // Below 1.0 pulls colour out; the reference look is close to monochrome.
    float saturation = 0.82f;

    // Shadows to cold blue, with red pulled under zero so they crush cleanly.
    float lift[3] = {-0.018f, 0.004f, 0.042f};
    // Midtones a touch cooler than neutral.
    float gamma[3] = {0.97f, 1.0f, 1.03f};
    // Highlights warm, so torchlight and sky read against the cold shadows.
    float gain[3] = {1.06f, 1.0f, 0.93f};
};

// vec4 tuning + vec4 lift + vec4 gamma + vec4 gain, matching the push constant
// block in tonemap.frag.
inline constexpr std::size_t kTonemapPushConstantFloats = 16;

// `flashPremulRgb` is the combat hit tint already multiplied by its strength.
// It travels in the three .w slots the grading vectors leave spare, which is
// why the block never had to grow — and why the range shared with the SSAO
// fullscreen pipelines is untouched.
inline void packTonemapPushConstants(const GradingParams& g,
                                     bool encodeSrgb,
                                     float (&out)[kTonemapPushConstantFloats],
                                     const float* flashPremulRgb = nullptr)
{
    out[0] = g.exposure;
    out[1] = g.contrast;
    out[2] = g.saturation;
    out[3] = encodeSrgb ? 1.0f : 0.0f;

    for (int i = 0; i < 3; ++i) {
        out[4 + i] = g.lift[i];
        out[8 + i] = g.gamma[i];
        out[12 + i] = g.gain[i];
    }
    out[7]  = flashPremulRgb ? flashPremulRgb[0] : 0.0f;
    out[11] = flashPremulRgb ? flashPremulRgb[1] : 0.0f;
    out[15] = flashPremulRgb ? flashPremulRgb[2] : 0.0f;
}

} // namespace dash::vkexp
