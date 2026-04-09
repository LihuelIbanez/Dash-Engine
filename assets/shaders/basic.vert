#version 450

layout(location = 0) in vec3 inPos;
layout(location = 0) out vec3 vColor;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 mvp;
} ubo;

void main()
{
    gl_Position = ubo.mvp * vec4(inPos, 1.0);
    vColor = inPos * 0.5 + 0.5;
}
