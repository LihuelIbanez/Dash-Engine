#include "ModelImporter.h"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

#include "rendering/animation/AnimationFile.h"
#include "rendering/animation/DashMeshFile.h"

namespace fs = std::filesystem;

using dash::anim::AnimationChannel;
using dash::anim::AnimationClip;
using dash::anim::DashMeshData;
using dash::anim::Mat4;
using dash::anim::QuatKey;
using dash::anim::Skeleton;
using dash::anim::VecKey;
using dash::vkexp::SkinnedVertex;
using dash::vkexp::Vertex;

namespace {

// Assimp stores matrices row-major; the engine is column-major.
Mat4 toMat4(const aiMatrix4x4& in)
{
    Mat4 out{};
    out.m[0]  = in.a1; out.m[1]  = in.b1; out.m[2]  = in.c1; out.m[3]  = in.d1;
    out.m[4]  = in.a2; out.m[5]  = in.b2; out.m[6]  = in.c2; out.m[7]  = in.d2;
    out.m[8]  = in.a3; out.m[9]  = in.b3; out.m[10] = in.c3; out.m[11] = in.d3;
    out.m[12] = in.a4; out.m[13] = in.b4; out.m[14] = in.c4; out.m[15] = in.d4;
    return out;
}

bool sceneHasBones(const aiScene* scene)
{
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        if (scene->mMeshes[m]->mNumBones > 0) return true;
    }
    return false;
}

// A node belongs in the skeleton when it is a bone or an ancestor of one:
// intermediate nodes carry transforms the bones depend on.
bool markSkeletonNodes(const aiNode* node,
                       const std::unordered_set<std::string>& boneNames,
                       std::unordered_set<const aiNode*>& outKeep)
{
    bool keep = boneNames.count(node->mName.C_Str()) > 0;
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        if (markSkeletonNodes(node->mChildren[i], boneNames, outKeep)) keep = true;
    }
    if (keep) outKeep.insert(node);
    return keep;
}

// Pre-order walk, so a parent is always added before its children and the bone
// array comes out topologically sorted.
void buildSkeletonRecursive(const aiNode* node,
                            int parentIndex,
                            const std::unordered_set<const aiNode*>& keep,
                            Skeleton& skeleton)
{
    int myIndex = parentIndex;
    if (keep.count(node) > 0) {
        myIndex = skeleton.addBone(node->mName.C_Str(), parentIndex,
                                   dash::anim::identity(), toMat4(node->mTransformation));
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        buildSkeletonRecursive(node->mChildren[i], myIndex, keep, skeleton);
    }
}

// Keeps the four strongest influences per vertex.
void addInfluence(SkinnedVertex& vertex, uint16_t boneIndex, float weight)
{
    if (weight <= 0.0f) return;

    int weakest = 0;
    for (int i = 1; i < 4; ++i) {
        if (vertex.boneWeights[i] < vertex.boneWeights[weakest]) weakest = i;
    }
    if (weight > vertex.boneWeights[weakest]) {
        vertex.boneIndices[weakest] = boneIndex;
        vertex.boneWeights[weakest] = weight;
    }
}

AnimationClip toClip(const aiAnimation* anim, unsigned int fallbackIndex)
{
    AnimationClip clip;
    clip.name = anim->mName.length > 0
        ? std::string(anim->mName.C_Str())
        : "clip_" + std::to_string(fallbackIndex);
    clip.duration = static_cast<float>(anim->mDuration);
    clip.ticksPerSecond = anim->mTicksPerSecond > 0.0
        ? static_cast<float>(anim->mTicksPerSecond)
        : 25.0f;

    clip.channels.reserve(anim->mNumChannels);
    for (unsigned int c = 0; c < anim->mNumChannels; ++c) {
        const aiNodeAnim* src = anim->mChannels[c];
        AnimationChannel channel;
        channel.boneName = src->mNodeName.C_Str();

        channel.positions.reserve(src->mNumPositionKeys);
        for (unsigned int k = 0; k < src->mNumPositionKeys; ++k) {
            const aiVectorKey& key = src->mPositionKeys[k];
            channel.positions.push_back(VecKey{
                static_cast<float>(key.mTime),
                {key.mValue.x, key.mValue.y, key.mValue.z}});
        }

        channel.rotations.reserve(src->mNumRotationKeys);
        for (unsigned int k = 0; k < src->mNumRotationKeys; ++k) {
            const aiQuatKey& key = src->mRotationKeys[k];
            channel.rotations.push_back(QuatKey{
                static_cast<float>(key.mTime),
                {key.mValue.x, key.mValue.y, key.mValue.z, key.mValue.w}});
        }

        channel.scales.reserve(src->mNumScalingKeys);
        for (unsigned int k = 0; k < src->mNumScalingKeys; ++k) {
            const aiVectorKey& key = src->mScalingKeys[k];
            channel.scales.push_back(VecKey{
                static_cast<float>(key.mTime),
                {key.mValue.x, key.mValue.y, key.mValue.z}});
        }

        clip.channels.push_back(std::move(channel));
    }

    return clip;
}

} // namespace

ImportResult ModelImporter::import(const std::string& sourcePath,
                                   const std::string& outputPath,
                                   AssetRecord& record)
{
    ImportResult result;

    Assimp::Importer importer;
    importer.SetPropertyInteger(AI_CONFIG_PP_LBW_MAX_WEIGHTS,
                                static_cast<int>(dash::vkexp::kMaxBoneInfluences));

    const aiScene* scene = importer.ReadFile(sourcePath,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_FlipUVs |
        aiProcess_JoinIdenticalVertices |
        aiProcess_LimitBoneWeights);

    if (!scene || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || !scene->mRootNode) {
        result.success = false;
        result.errors.push_back(std::string("Assimp error: ") + importer.GetErrorString());
        return result;
    }

    if (scene->mNumMeshes == 0) {
        result.success = false;
        result.errors.push_back("No meshes found in model file");
        return result;
    }

    const bool skinned = sceneHasBones(scene);

    // ── Skeleton ─────────────────────────────────────────────────────────────
    Skeleton skeleton;
    if (skinned) {
        std::unordered_set<std::string> boneNames;
        for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
            const aiMesh* mesh = scene->mMeshes[m];
            for (unsigned int b = 0; b < mesh->mNumBones; ++b) {
                boneNames.insert(mesh->mBones[b]->mName.C_Str());
            }
        }

        std::unordered_set<const aiNode*> keep;
        markSkeletonNodes(scene->mRootNode, boneNames, keep);
        buildSkeletonRecursive(scene->mRootNode, -1, keep, skeleton);
        skeleton.setGlobalInverseTransform(
            dash::anim::inverse(toMat4(scene->mRootNode->mTransformation)));
    }

    // ── Geometry (and skinning stream) ───────────────────────────────────────
    DashMeshData mesh;
    std::string diffuseTexPath;
    uint32_t vertexOffset = 0;

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* src = scene->mMeshes[m];

        for (unsigned int i = 0; i < src->mNumVertices; ++i) {
            Vertex v{};
            v.position = {src->mVertices[i].x, src->mVertices[i].y, src->mVertices[i].z};
            v.normal = src->mNormals
                ? std::array<float, 3>{src->mNormals[i].x, src->mNormals[i].y, src->mNormals[i].z}
                : std::array<float, 3>{0.0f, 1.0f, 0.0f};
            v.texCoord = src->mTextureCoords[0]
                ? std::array<float, 2>{src->mTextureCoords[0][i].x, src->mTextureCoords[0][i].y}
                : std::array<float, 2>{0.0f, 0.0f};
            mesh.vertices.push_back(v);
            if (skinned) mesh.skin.push_back(SkinnedVertex{});
        }

        for (unsigned int b = 0; b < src->mNumBones; ++b) {
            const aiBone* bone = src->mBones[b];
            const int boneIndex = skeleton.findBone(bone->mName.C_Str());
            if (boneIndex < 0) continue;

            skeleton.setBoneOffset(boneIndex, toMat4(bone->mOffsetMatrix));

            for (unsigned int w = 0; w < bone->mNumWeights; ++w) {
                const aiVertexWeight& vw = bone->mWeights[w];
                const size_t target = vertexOffset + vw.mVertexId;
                if (target >= mesh.skin.size()) continue;
                addInfluence(mesh.skin[target], static_cast<uint16_t>(boneIndex), vw.mWeight);
            }
        }

        for (unsigned int f = 0; f < src->mNumFaces; ++f) {
            const aiFace& face = src->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; ++j) {
                mesh.indices.push_back(vertexOffset + face.mIndices[j]);
            }
        }

        if (m == 0 && src->mMaterialIndex < scene->mNumMaterials) {
            aiMaterial* mat = scene->mMaterials[src->mMaterialIndex];
            if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
                aiString texPath;
                mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath);
                diffuseTexPath = texPath.C_Str();
            }
        }

        vertexOffset += src->mNumVertices;
    }

    mesh.diffuseTexturePath = diffuseTexPath;
    mesh.boneCount = static_cast<uint32_t>(skeleton.boneCount());

    if (skinned) {
        for (SkinnedVertex& sv : mesh.skin) dash::anim::normalizeBoneWeights(sv);
        if (skeleton.exceedsGpuLimit()) {
            std::fprintf(stderr,
                         "[ModelImporter] %s has %u bones, over the %u the skinned pipeline supports\n",
                         sourcePath.c_str(), mesh.boneCount,
                         static_cast<uint32_t>(Skeleton::kMaxBones));
        }
    }

    // ── Output ───────────────────────────────────────────────────────────────
    fs::path outPath(outputPath);
    outPath.replace_extension(".dashmesh");
    fs::create_directories(outPath.parent_path());

    std::string error;
    if (!dash::anim::writeDashMesh(outPath.string(), mesh, error)) {
        result.success = false;
        result.errors.push_back(error);
        return result;
    }

    if (skinned) {
        fs::path skelPath = outPath;
        skelPath.replace_extension(".dashskel");
        if (!dash::anim::writeSkeleton(skelPath.string(), skeleton, error)) {
            result.success = false;
            result.errors.push_back(error);
            return result;
        }

        std::vector<AnimationClip> clips;
        clips.reserve(scene->mNumAnimations);
        for (unsigned int a = 0; a < scene->mNumAnimations; ++a) {
            clips.push_back(toClip(scene->mAnimations[a], a));
        }

        fs::path animPath = outPath;
        animPath.replace_extension(".dashanim");
        if (!dash::anim::writeAnimationClips(animPath.string(), clips, error)) {
            result.success = false;
            result.errors.push_back(error);
            return result;
        }

        std::fprintf(stdout, "[ModelImporter] Skeleton: %u bones, %u clips\n",
                     mesh.boneCount, static_cast<uint32_t>(clips.size()));
    }

    record.assetType = AssetType::Model;
    record.importPath = fs::relative(outPath, outPath.parent_path().parent_path()).string();

    result.success = true;
    std::fprintf(stdout, "[ModelImporter] Imported %u vertices, %u indices from %s\n",
                 static_cast<uint32_t>(mesh.vertices.size()),
                 static_cast<uint32_t>(mesh.indices.size()),
                 sourcePath.c_str());
    return result;
}
