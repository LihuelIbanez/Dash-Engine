#version 450

// Camera-facing quad generated from gl_VertexIndex; no vertex buffer is bound.

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec3 vColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProj;
} ubo;

layout(push_constant) uniform BillboardPC {
    vec4 center;   // xyz = world position
    vec4 size;     // xy = half extents
    vec4 color;    // xyz = tint, w = alpha
    vec4 camRight; // xyz = camera right axis
    vec4 camUp;    // xyz = camera up axis
} pc;

void main()
{
    const vec2 corners[6] = vec2[6](
        vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2(-1.0,  1.0),
        vec2( 1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0)
    );

    vec2 c = corners[gl_VertexIndex];
    vec3 world = pc.center.xyz
               + pc.camRight.xyz * (c.x * pc.size.x)
               + pc.camUp.xyz    * (c.y * pc.size.y);

    gl_Position = ubo.viewProj * vec4(world, 1.0);
    vUV = vec2(c.x, -c.y) * 0.5 + 0.5;
    vColor = clamp(pc.color.xyz, 0.0, 1.0);
}
