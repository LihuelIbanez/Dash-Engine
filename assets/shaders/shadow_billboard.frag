#version 450

// No colour attachment: this stage exists only to discard the transparent part
// of the sprite so the caster is its silhouette and not a solid rectangle.

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 1) uniform sampler2D texSampler;

void main()
{
    // Half coverage, the usual alpha-to-shadow cut: the soft antialiased rim of
    // a sprite contributes almost nothing visually but would cast at full black.
    if (texture(texSampler, vUV).a < 0.5) discard;
}
