#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// .dashmesh — binary mesh container produced by ModelImporter
//
// All fields are little-endian, tightly packed, no padding between blocks.
//
//   char     magic[4]        "DMSH"
//   uint32   version         1 = static only, 2 = optional skinning block
//   uint32   vertexCount
//   uint32   indexCount
//   uint16   texPathLen
//   ── v2 only ──────────────────────────────────────────────────────────────
//   uint16   flags           bit 0 (kDashMeshFlagSkinned) = skinning block present
//   uint32   boneCount       bones in the companion .dashskel, 0 if none
//   ─────────────────────────────────────────────────────────────────────────
//   Vertex        vertices[vertexCount]   32 B each: pos[3] normal[3] uv[2] (f32)
//   SkinnedVertex skin[vertexCount]       v2 + kDashMeshFlagSkinned only. 24 B
//                                         each: boneIndices[4] (u16) + weights[4] (f32)
//   uint32        indices[indexCount]
//   char          texPath[texPathLen]     not NUL-terminated
//
// The v1 vertex block is byte-identical in v2, so the skinning stream can be a
// separate vertex binding at draw time and readers of the static layout keep
// working. Meshes without bones are still written as v1 to stay compatible with
// everything already on disk.
//
// Header-only so ModelImporter.cpp can use it without new link dependencies.
// ─────────────────────────────────────────────────────────────────────────────

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "rendering/mesh/SkinnedVertex.h"
#include "rendering/mesh/Vertex.h"

namespace dash::anim {

inline constexpr uint32_t kDashMeshVersionStatic = 1;
inline constexpr uint32_t kDashMeshVersionSkinned = 2;
inline constexpr uint16_t kDashMeshFlagSkinned = 0x0001;

struct DashMeshData {
    std::vector<dash::vkexp::Vertex>        vertices;
    std::vector<dash::vkexp::SkinnedVertex> skin;   // empty => static mesh
    std::vector<uint32_t>                   indices;
    std::string                             diffuseTexturePath;
    uint32_t                                boneCount = 0;
    uint32_t                                version = kDashMeshVersionStatic;

    bool isSkinned() const { return !skin.empty(); }
};

namespace detail {

inline constexpr char kDashMeshMagic[4] = {'D', 'M', 'S', 'H'};

template <typename T>
inline void writePod(std::ostream& out, const T& value)
{
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

template <typename T>
inline bool readPod(std::istream& in, T& value)
{
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return in.good();
}

} // namespace detail

// Writes v2 when `data.skin` is populated, v1 otherwise.
inline bool writeDashMesh(const std::string& path, const DashMeshData& data, std::string& outError)
{
    if (data.isSkinned() && data.skin.size() != data.vertices.size()) {
        outError = "skin stream size does not match vertex count";
        return false;
    }

    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) {
        outError = "cannot open for writing: " + path;
        return false;
    }

    const bool skinned = data.isSkinned();
    const uint32_t version = skinned ? kDashMeshVersionSkinned : kDashMeshVersionStatic;
    const uint32_t vertexCount = static_cast<uint32_t>(data.vertices.size());
    const uint32_t indexCount = static_cast<uint32_t>(data.indices.size());
    const uint16_t texPathLen = static_cast<uint16_t>(
        std::min<size_t>(data.diffuseTexturePath.size(), 0xFFFFu));

    out.write(detail::kDashMeshMagic, 4);
    detail::writePod(out, version);
    detail::writePod(out, vertexCount);
    detail::writePod(out, indexCount);
    detail::writePod(out, texPathLen);

    if (version >= kDashMeshVersionSkinned) {
        const uint16_t flags = skinned ? kDashMeshFlagSkinned : static_cast<uint16_t>(0);
        detail::writePod(out, flags);
        detail::writePod(out, data.boneCount);
    }

    if (vertexCount > 0) {
        out.write(reinterpret_cast<const char*>(data.vertices.data()),
                  static_cast<std::streamsize>(sizeof(dash::vkexp::Vertex) * vertexCount));
    }
    if (skinned) {
        out.write(reinterpret_cast<const char*>(data.skin.data()),
                  static_cast<std::streamsize>(sizeof(dash::vkexp::SkinnedVertex) * vertexCount));
    }
    if (indexCount > 0) {
        out.write(reinterpret_cast<const char*>(data.indices.data()),
                  static_cast<std::streamsize>(sizeof(uint32_t) * indexCount));
    }
    if (texPathLen > 0) {
        out.write(data.diffuseTexturePath.data(), texPathLen);
    }

    if (!out.good()) {
        outError = "write failed: " + path;
        return false;
    }
    return true;
}

// Accepts both v1 and v2; v1 files come back with an empty skin vector.
inline bool readDashMesh(const std::string& path, DashMeshData& outData, std::string& outError)
{
    outData = DashMeshData{};

    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        outError = "cannot open for reading: " + path;
        return false;
    }

    char magic[4] = {};
    in.read(magic, 4);
    if (!in.good() || std::memcmp(magic, detail::kDashMeshMagic, 4) != 0) {
        outError = "bad magic, not a .dashmesh: " + path;
        return false;
    }

    uint32_t version = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    uint16_t texPathLen = 0;
    if (!detail::readPod(in, version) || !detail::readPod(in, vertexCount) ||
        !detail::readPod(in, indexCount) || !detail::readPod(in, texPathLen)) {
        outError = "truncated header: " + path;
        return false;
    }

    if (version < kDashMeshVersionStatic || version > kDashMeshVersionSkinned) {
        outError = "unsupported .dashmesh version " + std::to_string(version);
        return false;
    }

    uint16_t flags = 0;
    uint32_t boneCount = 0;
    if (version >= kDashMeshVersionSkinned) {
        if (!detail::readPod(in, flags) || !detail::readPod(in, boneCount)) {
            outError = "truncated v2 header: " + path;
            return false;
        }
    }

    outData.version = version;
    outData.boneCount = boneCount;

    outData.vertices.resize(vertexCount);
    if (vertexCount > 0) {
        in.read(reinterpret_cast<char*>(outData.vertices.data()),
                static_cast<std::streamsize>(sizeof(dash::vkexp::Vertex) * vertexCount));
        if (!in.good()) {
            outError = "truncated vertex block: " + path;
            return false;
        }
    }

    if ((flags & kDashMeshFlagSkinned) != 0) {
        outData.skin.resize(vertexCount);
        if (vertexCount > 0) {
            in.read(reinterpret_cast<char*>(outData.skin.data()),
                    static_cast<std::streamsize>(sizeof(dash::vkexp::SkinnedVertex) * vertexCount));
            if (!in.good()) {
                outError = "truncated skin block: " + path;
                return false;
            }
        }
    }

    outData.indices.resize(indexCount);
    if (indexCount > 0) {
        in.read(reinterpret_cast<char*>(outData.indices.data()),
                static_cast<std::streamsize>(sizeof(uint32_t) * indexCount));
        if (!in.good()) {
            outError = "truncated index block: " + path;
            return false;
        }
    }

    if (texPathLen > 0) {
        outData.diffuseTexturePath.resize(texPathLen);
        in.read(&outData.diffuseTexturePath[0], texPathLen);
        if (!in.good()) {
            outError = "truncated texture path: " + path;
            return false;
        }
    }

    return true;
}

// Rescales the four influences so they sum to 1. A vertex the exporter left
// without any influence is pinned to bone 0 so it still follows the skeleton.
inline void normalizeBoneWeights(dash::vkexp::SkinnedVertex& vertex)
{
    float sum = 0.0f;
    for (float w : vertex.boneWeights) sum += w;

    if (sum <= 1e-6f) {
        vertex.boneIndices = {{0, 0, 0, 0}};
        vertex.boneWeights = {{1.0f, 0.0f, 0.0f, 0.0f}};
        return;
    }

    const float inv = 1.0f / sum;
    for (float& w : vertex.boneWeights) w *= inv;
}

} // namespace dash::anim
