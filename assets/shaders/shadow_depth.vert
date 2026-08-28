#version 450

// Depth-only pass for the directional shadow map. There is no fragment stage:
// the pipeline exists purely to fill the depth attachment.

layout(location = 0) in vec3 inPos;

layout(push_constant) uniform ShadowPC {
    mat4 model;
    mat4 lightViewProj;
} pc;

void main()
{
    gl_Position = pc.lightViewProj * pc.model * vec4(inPos, 1.0);
}
