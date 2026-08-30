#version 450

// Instanced camera-facing quad. The corners come from gl_VertexIndex, the
// per-particle data from a per-instance vertex buffer the CPU refills each
// frame, so a whole burst is one vkCmdDraw instead of one draw per particle.
//
// Every location here has a matching VkVertexInputAttributeDescription in
// PipelineBuilder::createParticlePipeline. Adding one without the other makes
// the pipeline fail silently and the particles vanish.

layout(location = 0) in vec4 iCenterSize;  // xyz world position, w half extent
layout(location = 1) in vec4 iColor;       // rgba, straight alpha
layout(location = 2) in vec4 iUvRect;      // u0, v0, uSpan, vSpan
layout(location = 3) in vec4 iParams;      // x = rotation radians

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColor;

layout(push_constant) uniform ParticlePC {
    mat4 viewProj;
    vec4 camRight;
    vec4 camUp;
} pc;

void main()
{
    const vec2 corners[6] = vec2[6](
        vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2(-1.0,  1.0),
        vec2( 1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0)
    );

    vec2 c = corners[gl_VertexIndex];

    float s = sin(iParams.x);
    float co = cos(iParams.x);
    vec2 r = vec2(c.x * co - c.y * s, c.x * s + c.y * co);

    vec3 world = iCenterSize.xyz
               + pc.camRight.xyz * (r.x * iCenterSize.w)
               + pc.camUp.xyz    * (r.y * iCenterSize.w);

    gl_Position = pc.viewProj * vec4(world, 1.0);

    vec2 cell = vec2(c.x, -c.y) * 0.5 + 0.5;
    vUV = iUvRect.xy + cell * iUvRect.zw;
    vColor = iColor;
}
