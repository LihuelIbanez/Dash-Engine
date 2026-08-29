#version 450

// Fullscreen triangle generated from gl_VertexIndex; no vertex buffer is bound
// (draw 3 vertices). uv (0,0) lands on the top-left texel, matching Vulkan's
// y-down NDC, so the HDR target is sampled without a flip.

layout(location = 0) out vec2 vUV;

void main()
{
    vUV = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(vUV * 2.0 - 1.0, 0.0, 1.0);
}
