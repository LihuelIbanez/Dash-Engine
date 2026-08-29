#version 450

// Depth-only billboard pass. Same procedural quad as billboard.vert, but the
// axes come from the light instead of the camera (see SceneRenderer.cpp) and
// the matrix is the cascade's light-view-projection.

layout(location = 0) out vec2 vUV;

layout(push_constant) uniform BillboardShadowPC {
    mat4 lightViewProj;  // 64
    vec4 center;         // xyz = world position
    vec4 size;           // xy = half extents
    vec4 axisRight;      // xyz = quad right axis, light aligned
    vec4 axisUp;         // xyz = quad up axis
} pc;                    // 128 bytes = the guaranteed push constant budget

void main()
{
    const vec2 corners[6] = vec2[6](
        vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2(-1.0,  1.0),
        vec2( 1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0)
    );

    vec2 c = corners[gl_VertexIndex];
    vec3 world = pc.center.xyz
               + pc.axisRight.xyz * (c.x * pc.size.x)
               + pc.axisUp.xyz    * (c.y * pc.size.y);

    gl_Position = pc.lightViewProj * vec4(world, 1.0);
    vUV = vec2(c.x, -c.y) * 0.5 + 0.5;
}
