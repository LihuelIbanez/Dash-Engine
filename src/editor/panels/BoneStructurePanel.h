#pragma once

// ─────────────────────────────────────────────────────────────────────────────
// BoneStructurePanel — inspect and edit the bone hierarchy of a .dashskel
//
// Everything that decides something (hierarchy walk, validation, the safe
// reparent with its topological re-sort, rename, name filter, TRS decomposition
// and the bone-index remap that keeps a .dashmesh valid) lives in
// dash::editor::bonestruct: header-only and ImGui-free, so the tests can
// exercise it headless.
// ─────────────────────────────────────────────────────────────────────────────

#include "rendering/animation/AnimationFile.h"
#include "rendering/animation/DashMeshFile.h"
#include "rendering/animation/Skeleton.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace dash::editor::bonestruct {

using dash::anim::Bone;
using dash::anim::Mat4;
using dash::anim::Quat;
using dash::anim::Vec3;

// ─────────────────────────────────────────────────────────────────────────────
// Hierarchy
// ─────────────────────────────────────────────────────────────────────────────

/// A parent reference is usable only when it points at another existing bone.
inline bool isValidParent(std::size_t boneCount, int index, int parent)
{
    return parent >= 0 && parent < static_cast<int>(boneCount) && parent != index;
}

inline int parentOf(const std::vector<Bone>& bones, int index)
{
    if (index < 0 || index >= static_cast<int>(bones.size())) return -1;
    const int p = bones[static_cast<std::size_t>(index)].parent;
    return isValidParent(bones.size(), index, p) ? p : -1;
}

struct Hierarchy {
    std::vector<std::vector<int>> children;  // per bone index, in ascending order
    std::vector<int>              roots;
};

/// Bones with a broken parent reference are surfaced as roots so a damaged file
/// still draws instead of losing branches.
inline Hierarchy buildHierarchy(const std::vector<Bone>& bones)
{
    Hierarchy h;
    h.children.assign(bones.size(), {});
    for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
        const int p = parentOf(bones, i);
        if (p >= 0) h.children[static_cast<std::size_t>(p)].push_back(i);
        else        h.roots.push_back(i);
    }
    return h;
}

/// False for every bone whose parent chain loops instead of ending at a root.
inline std::vector<bool> reachableFromRoots(const std::vector<Bone>& bones)
{
    const int n = static_cast<int>(bones.size());
    std::vector<int> state(bones.size(), 0);  // 0 unknown, 1 walking, 2 reachable, 3 looped
    std::vector<int> chain;

    for (int i = 0; i < n; ++i) {
        if (state[static_cast<std::size_t>(i)] != 0) continue;
        chain.clear();
        int cur = i;
        while (cur >= 0 && state[static_cast<std::size_t>(cur)] == 0) {
            state[static_cast<std::size_t>(cur)] = 1;
            chain.push_back(cur);
            cur = parentOf(bones, cur);
        }
        // Stopping on a bone of the current walk means we closed a loop.
        const int mark = (cur < 0 || state[static_cast<std::size_t>(cur)] == 2) ? 2 : 3;
        for (int b : chain) state[static_cast<std::size_t>(b)] = mark;
    }

    std::vector<bool> out(bones.size(), false);
    for (int i = 0; i < n; ++i) out[static_cast<std::size_t>(i)] = state[static_cast<std::size_t>(i)] == 2;
    return out;
}

/// True when `candidate` sits on the parent chain below `ancestor`; a bone is
/// its own descendant so a self-reparent is rejected by the same check.
inline bool isDescendantOf(const std::vector<Bone>& bones, int candidate, int ancestor)
{
    if (candidate < 0 || ancestor < 0) return false;
    int cur = candidate;
    for (std::size_t guard = 0; guard <= bones.size() && cur >= 0; ++guard) {
        if (cur == ancestor) return true;
        cur = parentOf(bones, cur);
    }
    return false;
}

inline int depthOf(const std::vector<Bone>& bones, int index)
{
    int depth = 0;
    int cur = parentOf(bones, index);
    for (std::size_t guard = 0; guard <= bones.size() && cur >= 0; ++guard) {
        ++depth;
        cur = parentOf(bones, cur);
    }
    return depth;
}

// ─────────────────────────────────────────────────────────────────────────────
// Validation
// ─────────────────────────────────────────────────────────────────────────────

enum class IssueSeverity { Warning, Error };

struct Issue {
    IssueSeverity severity = IssueSeverity::Error;
    int           bone     = -1;
    std::string   message;
};

inline std::string boneLabel(const std::vector<Bone>& bones, int index)
{
    if (index < 0 || index >= static_cast<int>(bones.size())) return "#" + std::to_string(index);
    return "'" + bones[static_cast<std::size_t>(index)].name + "' (#" + std::to_string(index) + ")";
}

/// Errors block saving; warnings are informational only.
inline std::vector<Issue> validate(const std::vector<Bone>& bones)
{
    std::vector<Issue> issues;
    const int n = static_cast<int>(bones.size());

    for (int i = 0; i < n; ++i) {
        const Bone& b = bones[static_cast<std::size_t>(i)];

        if (b.name.empty())
            issues.push_back({IssueSeverity::Error, i,
                              "bone #" + std::to_string(i) + " has an empty name"});

        for (int j = 0; j < i; ++j) {
            if (bones[static_cast<std::size_t>(j)].name != b.name) continue;
            issues.push_back({IssueSeverity::Error, i,
                              "duplicate name '" + b.name + "' on bones #" +
                                  std::to_string(j) + " and #" + std::to_string(i)});
            break;
        }

        if (b.parent == i)
            issues.push_back({IssueSeverity::Error, i,
                              boneLabel(bones, i) + " is its own parent"});
        else if (b.parent < -1 || b.parent >= n)
            issues.push_back({IssueSeverity::Error, i,
                              boneLabel(bones, i) + " points at parent index " +
                                  std::to_string(b.parent) + ", outside 0.." +
                                  std::to_string(n - 1)});
        else if (b.parent > i)
            issues.push_back({IssueSeverity::Error, i,
                              boneLabel(bones, i) + " has parent index " +
                                  std::to_string(b.parent) +
                                  " above its own: pose evaluation is a single forward pass "
                                  "and needs parent < child"});
    }

    const std::vector<bool> reachable = reachableFromRoots(bones);
    for (int i = 0; i < n; ++i) {
        if (reachable[static_cast<std::size_t>(i)]) continue;
        issues.push_back({IssueSeverity::Error, i,
                          boneLabel(bones, i) + " never reaches a root: its parent chain loops"});
    }

    if (bones.size() > dash::anim::Skeleton::kMaxBones)
        issues.push_back({IssueSeverity::Warning, -1,
                          std::to_string(bones.size()) + " bones, over the " +
                              std::to_string(dash::anim::Skeleton::kMaxBones) +
                              " the skinned pipeline can upload"});

    return issues;
}

inline bool hasErrors(const std::vector<Issue>& issues)
{
    for (const Issue& i : issues)
        if (i.severity == IssueSeverity::Error) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Topological re-sort and reparenting
// ─────────────────────────────────────────────────────────────────────────────

struct ReorderResult {
    bool              ok = false;
    std::string       error;
    std::vector<Bone> bones;      // reordered copy, parents already remapped
    std::vector<int>  oldToNew;   // oldToNew[oldIndex] = newIndex
    bool              reordered = false;
};

inline bool isIdentityPermutation(const std::vector<int>& perm)
{
    for (std::size_t i = 0; i < perm.size(); ++i)
        if (perm[i] != static_cast<int>(i)) return false;
    return true;
}

/// Depth-first from the roots in index order, so a bone always lands after its
/// parent and siblings keep their relative order. Broken parent references are
/// normalised to -1 on the way out.
inline ReorderResult topologicalSort(const std::vector<Bone>& bones)
{
    ReorderResult r;
    const int       n = static_cast<int>(bones.size());
    const Hierarchy h = buildHierarchy(bones);

    std::vector<int>  order;
    std::vector<bool> queued(bones.size(), false);
    std::vector<int>  stack(h.roots.rbegin(), h.roots.rend());
    order.reserve(bones.size());
    for (int root : h.roots) queued[static_cast<std::size_t>(root)] = true;

    while (!stack.empty()) {
        const int cur = stack.back();
        stack.pop_back();
        order.push_back(cur);
        const std::vector<int>& kids = h.children[static_cast<std::size_t>(cur)];
        for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
            if (queued[static_cast<std::size_t>(*it)]) continue;
            queued[static_cast<std::size_t>(*it)] = true;
            stack.push_back(*it);
        }
    }

    if (order.size() != bones.size()) {
        r.error = "the hierarchy has a cycle: " +
                  std::to_string(bones.size() - order.size()) + " bone(s) never reach a root";
        return r;
    }

    r.oldToNew.assign(bones.size(), -1);
    for (int newIdx = 0; newIdx < n; ++newIdx)
        r.oldToNew[static_cast<std::size_t>(order[static_cast<std::size_t>(newIdx)])] = newIdx;

    r.bones.reserve(bones.size());
    for (int newIdx = 0; newIdx < n; ++newIdx) {
        const int oldIdx = order[static_cast<std::size_t>(newIdx)];
        Bone      b      = bones[static_cast<std::size_t>(oldIdx)];
        const int oldParent = parentOf(bones, oldIdx);
        b.parent = oldParent < 0 ? -1 : r.oldToNew[static_cast<std::size_t>(oldParent)];
        r.bones.push_back(std::move(b));
    }

    r.reordered = !isIdentityPermutation(r.oldToNew);
    r.ok        = true;
    return r;
}

/// Moves `bone` under `newParent` (-1 makes it a root) and re-sorts so the
/// parent < child invariant holds. Rejected when the new parent hangs below the
/// bone, which would close a cycle.
inline ReorderResult reparent(const std::vector<Bone>& bones, int bone, int newParent)
{
    ReorderResult r;
    const int n = static_cast<int>(bones.size());

    if (bone < 0 || bone >= n) {
        r.error = "bone index " + std::to_string(bone) + " is out of range";
        return r;
    }
    if (newParent < -1 || newParent >= n) {
        r.error = "parent index " + std::to_string(newParent) + " is out of range";
        return r;
    }
    if (newParent == bone) {
        r.error = "a bone cannot be its own parent";
        return r;
    }
    if (newParent >= 0 && isDescendantOf(bones, newParent, bone)) {
        r.error = boneLabel(bones, newParent) + " hangs below " + boneLabel(bones, bone) +
                  ": reparenting there would create a cycle";
        return r;
    }

    std::vector<Bone> next = bones;
    next[static_cast<std::size_t>(bone)].parent = newParent;
    return topologicalSort(next);
}

/// `first` then `second`, so a chain of edits collapses into one permutation
/// from the on-disk order to the current one.
inline std::vector<int> composePermutation(const std::vector<int>& first,
                                           const std::vector<int>& second)
{
    std::vector<int> out(first.size(), -1);
    for (std::size_t i = 0; i < first.size(); ++i) {
        const int mid = first[i];
        if (mid < 0 || mid >= static_cast<int>(second.size())) continue;
        out[i] = second[static_cast<std::size_t>(mid)];
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rename and filter
// ─────────────────────────────────────────────────────────────────────────────

struct EditResult {
    bool        ok = false;
    std::string error;
};

inline std::string trim(const std::string& s)
{
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

inline EditResult renameBone(std::vector<Bone>& bones, int index, const std::string& newName)
{
    if (index < 0 || index >= static_cast<int>(bones.size()))
        return {false, "bone index " + std::to_string(index) + " is out of range"};

    const std::string name = trim(newName);
    if (name.empty()) return {false, "a bone name cannot be empty"};

    for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
        if (i == index) continue;
        if (bones[static_cast<std::size_t>(i)].name == name)
            return {false, "bone #" + std::to_string(i) + " is already called '" + name + "'"};
    }

    bones[static_cast<std::size_t>(index)].name = name;
    return {true, {}};
}

inline bool containsCaseInsensitive(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) return true;
    if (needle.size() > haystack.size()) return false;
    auto lower = [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    };
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        std::size_t j = 0;
        while (j < needle.size() && lower(haystack[i + j]) == lower(needle[j])) ++j;
        if (j == needle.size()) return true;
    }
    return false;
}

/// A hit keeps its ancestors visible too, otherwise the tree path to it breaks.
inline std::vector<bool> filterVisibility(const std::vector<Bone>& bones,
                                          const std::string& filter)
{
    std::vector<bool> visible(bones.size(), filter.empty());
    if (filter.empty()) return visible;

    for (int i = 0; i < static_cast<int>(bones.size()); ++i) {
        if (!containsCaseInsensitive(bones[static_cast<std::size_t>(i)].name, filter)) continue;
        int cur = i;
        for (std::size_t guard = 0; guard <= bones.size() && cur >= 0; ++guard) {
            visible[static_cast<std::size_t>(cur)] = true;
            cur = parentOf(bones, cur);
        }
    }
    return visible;
}

// ─────────────────────────────────────────────────────────────────────────────
// Matrix helpers
// ─────────────────────────────────────────────────────────────────────────────

struct Trs {
    Vec3 translation{0.f, 0.f, 0.f};
    Quat rotation{};
    Vec3 scale{1.f, 1.f, 1.f};
};

inline float matAt(const Mat4& m, int row, int col) { return m.m[col * 4 + row]; }

/// Column-major T*R*S split. Skew is not representable, so a sheared matrix
/// comes back approximated by the closest rigid basis.
inline Trs decomposeTrs(const Mat4& m)
{
    Trs out;
    out.translation = {m.m[12], m.m[13], m.m[14]};

    float len[3];
    for (int c = 0; c < 3; ++c) {
        len[c] = std::sqrt(matAt(m, 0, c) * matAt(m, 0, c) +
                           matAt(m, 1, c) * matAt(m, 1, c) +
                           matAt(m, 2, c) * matAt(m, 2, c));
    }

    // A negative determinant means one axis is mirrored; by convention it goes on X.
    const float det =
        matAt(m, 0, 0) * (matAt(m, 1, 1) * matAt(m, 2, 2) - matAt(m, 2, 1) * matAt(m, 1, 2)) -
        matAt(m, 0, 1) * (matAt(m, 1, 0) * matAt(m, 2, 2) - matAt(m, 2, 0) * matAt(m, 1, 2)) +
        matAt(m, 0, 2) * (matAt(m, 1, 0) * matAt(m, 2, 1) - matAt(m, 2, 0) * matAt(m, 1, 1));
    if (det < 0.f) len[0] = -len[0];

    out.scale = {len[0], len[1], len[2]};

    float r[3][3];
    for (int c = 0; c < 3; ++c) {
        const float inv = std::fabs(len[c]) < 1e-8f ? 0.f : 1.f / len[c];
        for (int row = 0; row < 3; ++row) r[row][c] = matAt(m, row, c) * inv;
    }

    const float trace = r[0][0] + r[1][1] + r[2][2];
    if (trace > 0.f) {
        const float s = std::sqrt(trace + 1.f) * 2.f;
        out.rotation = {(r[2][1] - r[1][2]) / s, (r[0][2] - r[2][0]) / s,
                        (r[1][0] - r[0][1]) / s, 0.25f * s};
    } else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
        const float s = std::sqrt(1.f + r[0][0] - r[1][1] - r[2][2]) * 2.f;
        out.rotation = {0.25f * s, (r[0][1] + r[1][0]) / s, (r[0][2] + r[2][0]) / s,
                        (r[2][1] - r[1][2]) / s};
    } else if (r[1][1] > r[2][2]) {
        const float s = std::sqrt(1.f + r[1][1] - r[0][0] - r[2][2]) * 2.f;
        out.rotation = {(r[0][1] + r[1][0]) / s, 0.25f * s, (r[1][2] + r[2][1]) / s,
                        (r[0][2] - r[2][0]) / s};
    } else {
        const float s = std::sqrt(1.f + r[2][2] - r[0][0] - r[1][1]) * 2.f;
        out.rotation = {(r[0][2] + r[2][0]) / s, (r[1][2] + r[2][1]) / s, 0.25f * s,
                        (r[1][0] - r[0][1]) / s};
    }
    out.rotation = dash::anim::normalize(out.rotation);
    return out;
}

inline void setTranslation(Mat4& m, const Vec3& t)
{
    m.m[12] = t.x;
    m.m[13] = t.y;
    m.m[14] = t.z;
}

// ─────────────────────────────────────────────────────────────────────────────
// Skeleton document: what the panel keeps in memory
// ─────────────────────────────────────────────────────────────────────────────

struct SkeletonDoc {
    std::vector<Bone> bones;
    Mat4              globalInverse = dash::anim::identity();
};

/// Model-space bind transform of every bone, parents first.
inline std::vector<Mat4> bindGlobals(const SkeletonDoc& doc)
{
    std::vector<Mat4> globals(doc.bones.size(), dash::anim::identity());
    for (std::size_t i = 0; i < doc.bones.size(); ++i) {
        const int p = parentOf(doc.bones, static_cast<int>(i));
        globals[i] = p >= 0 && p < static_cast<int>(i)
                         ? dash::anim::multiply(globals[static_cast<std::size_t>(p)],
                                                doc.bones[i].localBind)
                         : doc.bones[i].localBind;
    }
    return globals;
}

/// Rebuilds every offset matrix as the inverse of the current bind pose, which
/// is what makes Skeleton::bindPoseMatrices() come back as identities. Needed
/// after moving a localBind, otherwise the mesh renders deformed at rest.
inline void recomputeOffsetsFromBindPose(SkeletonDoc& doc)
{
    const std::vector<Mat4> globals = bindGlobals(doc);
    for (std::size_t i = 0; i < doc.bones.size(); ++i)
        doc.bones[i].offsetMatrix =
            dash::anim::inverse(dash::anim::multiply(doc.globalInverse, globals[i]));
}

// ─────────────────────────────────────────────────────────────────────────────
// File operations
// ─────────────────────────────────────────────────────────────────────────────

inline bool loadSkeletonDoc(const std::string& path, SkeletonDoc& out, std::string& outError)
{
    dash::anim::Skeleton skeleton;
    if (!dash::anim::readSkeleton(path, skeleton, outError)) return false;
    out.bones         = skeleton.bones();
    out.globalInverse = skeleton.globalInverseTransform();
    return true;
}

inline bool saveSkeletonDoc(const std::string& path, const SkeletonDoc& doc, std::string& outError)
{
    dash::anim::Skeleton skeleton;
    skeleton.setGlobalInverseTransform(doc.globalInverse);
    for (const Bone& b : doc.bones)
        skeleton.addBone(b.name, b.parent, b.offsetMatrix, b.localBind);

    // addBone() collapses repeated names, so a short skeleton means duplicates.
    if (skeleton.boneCount() != doc.bones.size()) {
        outError = "duplicate bone names would silently drop " +
                   std::to_string(doc.bones.size() - skeleton.boneCount()) + " bone(s)";
        return false;
    }
    return dash::anim::writeSkeleton(path, skeleton, outError);
}

/// SkinnedVertex stores bone references as indices, so a reorder invalidates the
/// mesh unless its skin stream travels through the same permutation.
inline bool remapSkinIndices(std::vector<dash::vkexp::SkinnedVertex>& skin,
                             const std::vector<int>& oldToNew, std::string& outError)
{
    for (dash::vkexp::SkinnedVertex& v : skin) {
        for (std::size_t i = 0; i < v.boneIndices.size(); ++i) {
            const std::size_t old = v.boneIndices[i];
            if (old >= oldToNew.size()) {
                outError = "skin references bone index " + std::to_string(old) +
                           " but the skeleton only has " + std::to_string(oldToNew.size());
                return false;
            }
            const int mapped = oldToNew[old];
            if (mapped < 0) {
                outError = "bone index " + std::to_string(old) + " has no place in the new order";
                return false;
            }
            v.boneIndices[i] = static_cast<uint16_t>(mapped);
        }
    }
    return true;
}

/// Kept separate from the write so a caller can validate the whole remap before
/// anything touches the disk.
inline bool loadRemappedMesh(const std::string& meshPath, const std::vector<int>& oldToNew,
                             dash::anim::DashMeshData& outMesh, std::string& outError)
{
    if (!dash::anim::readDashMesh(meshPath, outMesh, outError)) return false;
    if (!outMesh.isSkinned()) {
        outError = "not a skinned mesh, nothing to remap: " + meshPath;
        return false;
    }
    if (outMesh.boneCount != oldToNew.size()) {
        outError = "the mesh was baked against " + std::to_string(outMesh.boneCount) +
                   " bones but the skeleton has " + std::to_string(oldToNew.size());
        return false;
    }
    return remapSkinIndices(outMesh.skin, oldToNew, outError);
}

inline bool remapMeshBoneIndices(const std::string& meshPath, const std::vector<int>& oldToNew,
                                 std::string& outError)
{
    dash::anim::DashMeshData mesh;
    if (!loadRemappedMesh(meshPath, oldToNew, mesh, outError)) return false;
    return dash::anim::writeDashMesh(meshPath, mesh, outError);
}

/// Animation channels bind to bones by name, so a rename orphans them unless the
/// clips are rewritten. Returns the number of channels retargeted, -1 on error.
inline int renameAnimChannels(const std::string& animPath,
                              const std::vector<std::pair<std::string, std::string>>& renames,
                              std::string& outError)
{
    std::vector<dash::anim::AnimationClip> clips;
    if (!dash::anim::readAnimationClips(animPath, clips, outError)) return -1;

    int touched = 0;
    for (dash::anim::AnimationClip& clip : clips) {
        for (dash::anim::AnimationChannel& channel : clip.channels) {
            for (const auto& rename : renames) {
                if (channel.boneName != rename.first) continue;
                channel.boneName = rename.second;
                ++touched;
                break;
            }
        }
    }

    if (touched > 0 && !dash::anim::writeAnimationClips(animPath, clips, outError)) return -1;
    return touched;
}

} // namespace dash::editor::bonestruct

// ─────────────────────────────────────────────────────────────────────────────
// BoneStructurePanel — the ImGui window itself
//
// Autonomous: draw() opens and closes its own window and owns the loaded
// skeleton. It never touches the scene, only the .dashskel on disk and, when
// asked to, its .dashmesh / .dashanim siblings.
// ─────────────────────────────────────────────────────────────────────────────
class BoneStructurePanel {
public:
    using LogCallback = std::function<void(const std::string&)>;

    void draw(const std::string& assetsRoot, const std::string& libraryRoot,
              LogCallback logCb = nullptr);

    bool load(const std::string& path, std::string& outError);

    const std::string& loadedPath() const { return path_; }
    bool               hasSkeleton() const { return !path_.empty(); }

private:
    struct SaveReport {
        bool        ok = false;
        std::string message;
    };

    void       refreshIssues();
    void       applyReorder(const dash::editor::bonestruct::ReorderResult& result);
    void       selectBone(int index);
    SaveReport save();

    std::vector<std::pair<std::string, std::string>> pendingRenames() const;
    std::string siblingPath(const char* extension) const;

    void drawSourceBar(const std::string& assetsRoot, const std::string& libraryRoot,
                       LogCallback& logCb);
    void drawTree(float width);
    void drawBoneNode(int index, const std::vector<bool>& visible);
    void drawDetails(LogCallback& logCb);
    void drawIssues();
    void drawSaveSection(LogCallback& logCb);

    dash::editor::bonestruct::SkeletonDoc doc_;
    std::string                           path_;
    std::vector<std::string>              originalNames_;  // in the on-disk order
    std::vector<int>                      origToCurrent_;  // on-disk index -> current index
    bool                                  dirty_ = false;

    std::vector<dash::editor::bonestruct::Issue> issues_;
    std::vector<std::vector<int>>                children_;
    std::vector<int>                             roots_;

    int  selected_ = -1;
    char filterBuf_[128] = {0};
    char nameBuf_[128]   = {0};
    char pathBuf_[512]   = {0};

    int  reparentTarget_  = -1;
    bool remapMesh_       = true;
    bool retargetAnim_    = true;
    bool autoRecomputeOffsets_ = false;

    std::vector<std::string> foundFiles_;
    bool                     scanned_ = false;

    std::string status_;
    bool        statusIsError_ = false;
};
