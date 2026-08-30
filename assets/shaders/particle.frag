#version 450

// One shader for both blend modes: the output is premultiplied, so the alpha
// pipeline blends ONE / ONE_MINUS_SRC_ALPHA and the additive one ONE / ONE
// without needing a second fragment stage.
//
// The tint is not clamped: the additive presets emit well past 1.0 and the ACES
// pass in tonemap.frag is what brings them back into range.

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColor;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uAtlas;

void main()
{
    vec4 tex = texture(uAtlas, vUV);
    float a = tex.a * vColor.a;
    if (a < 0.004) discard;

    outColor = vec4(tex.rgb * vColor.rgb * a, a);
}
