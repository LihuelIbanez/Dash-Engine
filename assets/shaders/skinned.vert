#version 450

// Linear-blend skinning. Binding 0 carries the static Vertex layout used by
// every other pipeline; binding 1 carries the .dashmesh v2 skinning stream, so
// the same geometry can also be drawn by basic.vert without a second copy.
//
// kMaxBones must stay in sync with dash::vkexp::kMaxBonesPerSkeleton
// (src/rendering/mesh/SkinnedVertex.h). 128 mat4 = 8 KiB, well inside the
// 16 KiB minimum guaranteed uniform buffer range.
const int kMaxBones = 128;

layout(location = 0) in vec3  inPos;
layout(location = 1) in vec3  inNormal;
layout(location = 2) in vec2  inTexCoord;
layout(location = 3) in uvec4 inBoneIndices;
layout(location = 4) in vec4  inBoneWeights;

layout(location = 0) out vec3 vColor;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) out vec2 vTexCoord;

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 viewProj;
} ubo;

layout(set = 1, binding = 0) uniform BoneUBO {
    mat4 bones[kMaxBones];
} skin;

layout(push_constant) uniform InstancePC {
    mat4 model;
    vec4 color;    // xyz = color, w = alpha
    vec4 lightDir; // xyz = light direction, w = intensity
} pc;

void main()
{
    // Weights are normalized at import time, so a zero sum means the mesh was
    // bound to the skinned pipeline without a skinning stream: fall back to rigid.
    float weightSum = dot(inBoneWeights, vec4(1.0));

    mat4 skinMatrix = mat4(1.0);
    if (weightSum > 0.0001) {
        skinMatrix =
            inBoneWeights.x * skin.bones[min(inBoneIndices.x, uint(kMaxBones - 1))] +
            inBoneWeights.y * skin.bones[min(inBoneIndices.y, uint(kMaxBones - 1))] +
            inBoneWeights.z * skin.bones[min(inBoneIndices.z, uint(kMaxBones - 1))] +
            inBoneWeights.w * skin.bones[min(inBoneIndices.w, uint(kMaxBones - 1))];
    }

    vec4 skinnedPos = skinMatrix * vec4(inPos, 1.0);
    vec3 skinnedNormal = mat3(skinMatrix) * inNormal;

    vec4 worldPos = pc.model * skinnedPos;
    gl_Position = ubo.viewProj * worldPos;

    vColor = clamp(pc.color.xyz, 0.0, 1.0);
    vNormal = transpose(inverse(mat3(pc.model))) * skinnedNormal;
    vWorldPos = worldPos.xyz;
    vTexCoord = inTexCoord;
}
