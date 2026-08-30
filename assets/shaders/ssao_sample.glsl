// Screen-space ambient occlusion lookup, shared by scene_lights.glsl and
// terrain.frag. It declares set 0 binding 5, so every descriptor layout that
// binds one of those fragment shaders must carry that binding — the runtime and
// the editor viewport both do, unconditionally.
//
// The AO buffer is half the scene resolution (see dash::vkexp::SsaoPass), which
// is why the full-res pixel centre is divided by twice its size. When SSAO is
// off the binding holds a 1x1 white texel, so the lookup returns 1.0 and the
// ambient term comes out untouched.

layout(set = 0, binding = 5) uniform sampler2D dashSsaoTex;

float dashSsaoFactor()
{
    const vec2 fullRes = 2.0 * vec2(textureSize(dashSsaoTex, 0));
    return texture(dashSsaoTex, gl_FragCoord.xy / fullRes).r;
}
