#include "ModelImporter.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <filesystem>
#include <fstream>
#include <cstring>
#include <cstdio>

namespace fs = std::filesystem;

ImportResult ModelImporter::import(const std::string& sourcePath,
                                   const std::string& outputPath,
                                   AssetRecord& record)
{
    ImportResult result;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(sourcePath,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_FlipUVs |
        aiProcess_JoinIdenticalVertices);

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

    // Collect all meshes into a single vertex/index buffer
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> texCoords;
    std::vector<uint32_t> indices;
    std::string diffuseTexPath;

    uint32_t vertexOffset = 0;

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];

        for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
            positions.push_back(mesh->mVertices[i].x);
            positions.push_back(mesh->mVertices[i].y);
            positions.push_back(mesh->mVertices[i].z);

            if (mesh->mNormals) {
                normals.push_back(mesh->mNormals[i].x);
                normals.push_back(mesh->mNormals[i].y);
                normals.push_back(mesh->mNormals[i].z);
            } else {
                normals.push_back(0.0f);
                normals.push_back(1.0f);
                normals.push_back(0.0f);
            }

            if (mesh->mTextureCoords[0]) {
                texCoords.push_back(mesh->mTextureCoords[0][i].x);
                texCoords.push_back(mesh->mTextureCoords[0][i].y);
            } else {
                texCoords.push_back(0.0f);
                texCoords.push_back(0.0f);
            }
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            for (unsigned int j = 0; j < face.mNumIndices; ++j) {
                indices.push_back(vertexOffset + face.mIndices[j]);
            }
        }

        // Extract diffuse texture path from first mesh's material
        if (m == 0 && mesh->mMaterialIndex < scene->mNumMaterials) {
            aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
            if (mat->GetTextureCount(aiTextureType_DIFFUSE) > 0) {
                aiString texPath;
                mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath);
                diffuseTexPath = texPath.C_Str();
            }
        }

        vertexOffset += mesh->mNumVertices;
    }

    // Write binary .dashmesh format
    fs::path outPath(outputPath);
    outPath.replace_extension(".dashmesh");
    fs::create_directories(outPath.parent_path());

    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open()) {
        result.success = false;
        result.errors.push_back("Cannot write output: " + outPath.string());
        return result;
    }

    // Header: "DMSH" magic + version
    const char magic[4] = {'D', 'M', 'S', 'H'};
    const uint32_t version = 1;
    uint32_t vertexCount = static_cast<uint32_t>(positions.size() / 3);
    uint32_t indexCount = static_cast<uint32_t>(indices.size());
    uint16_t texPathLen = static_cast<uint16_t>(diffuseTexPath.size());

    out.write(magic, 4);
    out.write(reinterpret_cast<const char*>(&version), sizeof(version));
    out.write(reinterpret_cast<const char*>(&vertexCount), sizeof(vertexCount));
    out.write(reinterpret_cast<const char*>(&indexCount), sizeof(indexCount));
    out.write(reinterpret_cast<const char*>(&texPathLen), sizeof(texPathLen));

    // Interleaved vertex data: pos(3) + normal(3) + uv(2) per vertex
    for (uint32_t i = 0; i < vertexCount; ++i) {
        out.write(reinterpret_cast<const char*>(&positions[i * 3]), sizeof(float) * 3);
        out.write(reinterpret_cast<const char*>(&normals[i * 3]), sizeof(float) * 3);
        out.write(reinterpret_cast<const char*>(&texCoords[i * 2]), sizeof(float) * 2);
    }

    // Index data
    out.write(reinterpret_cast<const char*>(indices.data()), sizeof(uint32_t) * indexCount);

    // Texture path
    if (texPathLen > 0) {
        out.write(diffuseTexPath.data(), texPathLen);
    }

    out.close();

    record.assetType = AssetType::Model;
    record.importPath = fs::relative(outPath, outPath.parent_path().parent_path()).string();

    result.success = true;
    std::fprintf(stdout, "[ModelImporter] Imported %u vertices, %u indices from %s\n",
                 vertexCount, indexCount, sourcePath.c_str());
    return result;
}
