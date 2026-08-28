#version 450

// Skinned variant of shadow_depth.vert: same push constants, plus the bone
// palette at set 1 so animated casters are skinned before being depth-tested.
// Set 0 is left unused on purpose, to keep the layout compatible with the
// opaque skinned pipeline and reuse the same descriptor binding calls.
const int kMaxBones = 128;

layout(location = 0) in vec3  inPos;
layout(location = 3) in uvec4 inBoneIndices;
layout(location = 4) in vec4  inBoneWeights;

layout(set = 1, binding = 0) uniform BoneUBO {
    mat4 bones[kMaxBones];
} skin;

layout(push_constant) uniform ShadowPC {
    mat4 model;
    mat4 lightViewProj;
} pc;

void main()
{
    float weightSum = dot(inBoneWeights, vec4(1.0));

    mat4 skinMatrix = mat4(1.0);
    if (weightSum > 0.0001) {
        skinMatrix =
            inBoneWeights.x * skin.bones[min(inBoneIndices.x, uint(kMaxBones - 1))] +
            inBoneWeights.y * skin.bones[min(inBoneIndices.y, uint(kMaxBones - 1))] +
            inBoneWeights.z * skin.bones[min(inBoneIndices.z, uint(kMaxBones - 1))] +
            inBoneWeights.w * skin.bones[min(inBoneIndices.w, uint(kMaxBones - 1))];
    }

    gl_Position = pc.lightViewProj * pc.model * skinMatrix * vec4(inPos, 1.0);
}
