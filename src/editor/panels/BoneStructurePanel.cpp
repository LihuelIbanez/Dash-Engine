#include "BoneStructurePanel.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;
namespace bs = dash::editor::bonestruct;

namespace {

const ImVec4 kErrorColor  (0.957f, 0.278f, 0.278f, 1.f); // #F44747
const ImVec4 kWarningColor(0.902f, 0.596f, 0.212f, 1.f); // #E69836
const ImVec4 kOkColor     (0.427f, 0.784f, 0.427f, 1.f); // #6DC86D

constexpr int kMaxScannedFiles = 256;

void copyToBuffer(char* dst, std::size_t size, const std::string& src)
{
    const std::size_t n = src.size() < size - 1 ? src.size() : size - 1;
    std::memcpy(dst, src.data(), n);
    dst[n] = '\0';
}

std::vector<std::string> scanForSkeletons(const std::string& root)
{
    std::vector<std::string> out;
    std::error_code ec;
    if (root.empty() || !fs::is_directory(root, ec)) return out;

    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    if (ec) return out;
    for (const fs::directory_entry& entry : it) {
        if (static_cast<int>(out.size()) >= kMaxScannedFiles) break;
        if (!entry.is_regular_file(ec)) continue;
        if (entry.path().extension() == ".dashskel") out.push_back(entry.path().string());
    }
    return out;
}

void matrixRows(const char* label, const dash::anim::Mat4& m)
{
    if (!ImGui::TreeNode(label)) return;
    for (int row = 0; row < 4; ++row) {
        ImGui::Text("% .4f  % .4f  % .4f  % .4f",
                    static_cast<double>(bs::matAt(m, row, 0)),
                    static_cast<double>(bs::matAt(m, row, 1)),
                    static_cast<double>(bs::matAt(m, row, 2)),
                    static_cast<double>(bs::matAt(m, row, 3)));
    }
    ImGui::TreePop();
}

void trsSummary(const char* label, const dash::anim::Mat4& m)
{
    const bs::Trs trs = bs::decomposeTrs(m);
    ImGui::SeparatorText(label);
    ImGui::Text("T   % .4f  % .4f  % .4f",
                static_cast<double>(trs.translation.x),
                static_cast<double>(trs.translation.y),
                static_cast<double>(trs.translation.z));
    ImGui::Text("R   % .4f  % .4f  % .4f  % .4f",
                static_cast<double>(trs.rotation.x), static_cast<double>(trs.rotation.y),
                static_cast<double>(trs.rotation.z), static_cast<double>(trs.rotation.w));
    ImGui::Text("S   % .4f  % .4f  % .4f",
                static_cast<double>(trs.scale.x),
                static_cast<double>(trs.scale.y),
                static_cast<double>(trs.scale.z));
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
bool BoneStructurePanel::load(const std::string& path, std::string& outError)
{
    bs::SkeletonDoc doc;
    if (!bs::loadSkeletonDoc(path, doc, outError)) return false;

    doc_  = std::move(doc);
    path_ = path;

    originalNames_.clear();
    originalNames_.reserve(doc_.bones.size());
    for (const bs::Bone& b : doc_.bones) originalNames_.push_back(b.name);

    origToCurrent_.resize(doc_.bones.size());
    for (std::size_t i = 0; i < origToCurrent_.size(); ++i)
        origToCurrent_[i] = static_cast<int>(i);

    dirty_ = false;
    selectBone(doc_.bones.empty() ? -1 : 0);
    copyToBuffer(pathBuf_, sizeof(pathBuf_), path);
    refreshIssues();

    boneUndo_.clear();
    previewPose_          = PreviewPose::Bind;
    previewAnimScanned_   = false;
    previewClips_.clear();
    previewClipIndex_     = -1;
    previewClipTimeTicks_ = 0.f;
    previewPlaying_       = false;
    previewDragMode_      = PreviewDragMode::None;
    resetPreviewCamera();
    return true;
}

void BoneStructurePanel::refreshIssues()
{
    issues_ = bs::validate(doc_.bones);
    const bs::Hierarchy h = bs::buildHierarchy(doc_.bones);
    children_ = h.children;
    roots_    = h.roots;
}

void BoneStructurePanel::selectBone(int index)
{
    selected_       = index;
    reparentTarget_ = index >= 0 && index < static_cast<int>(doc_.bones.size())
                          ? doc_.bones[static_cast<std::size_t>(index)].parent
                          : -1;
    copyToBuffer(nameBuf_, sizeof(nameBuf_),
                 index >= 0 && index < static_cast<int>(doc_.bones.size())
                     ? doc_.bones[static_cast<std::size_t>(index)].name
                     : std::string{});
}

void BoneStructurePanel::applyReorder(const bs::ReorderResult& result)
{
    const int selectedAfter = selected_ >= 0 && selected_ < static_cast<int>(result.oldToNew.size())
                                  ? result.oldToNew[static_cast<std::size_t>(selected_)]
                                  : -1;
    doc_.bones     = result.bones;
    origToCurrent_ = bs::composePermutation(origToCurrent_, result.oldToNew);
    dirty_         = true;
    refreshIssues();
    selectBone(selectedAfter);
}

std::vector<std::pair<std::string, std::string>> BoneStructurePanel::pendingRenames() const
{
    std::vector<std::pair<std::string, std::string>> out;
    for (std::size_t orig = 0; orig < originalNames_.size(); ++orig) {
        const int now = orig < origToCurrent_.size() ? origToCurrent_[orig] : -1;
        if (now < 0 || now >= static_cast<int>(doc_.bones.size())) continue;
        const std::string& current = doc_.bones[static_cast<std::size_t>(now)].name;
        if (current != originalNames_[orig]) out.emplace_back(originalNames_[orig], current);
    }
    return out;
}

std::string BoneStructurePanel::siblingPath(const char* extension) const
{
    if (path_.empty()) return {};
    fs::path p(path_);
    p.replace_extension(extension);
    return p.string();
}

// ─────────────────────────────────────────────────────────────────────────────
BoneStructurePanel::SaveReport BoneStructurePanel::save()
{
    SaveReport report;
    if (path_.empty()) {
        report.message = "no .dashskel loaded";
        return report;
    }
    if (bs::hasErrors(issues_)) {
        report.message = "fix the validation errors first";
        return report;
    }

    const bool        reordered = !bs::isIdentityPermutation(origToCurrent_);
    const std::string meshPath  = siblingPath(".dashmesh");
    std::error_code   ec;
    const bool        haveMesh  = fs::is_regular_file(meshPath, ec);

    // The remapped mesh is prepared before anything is written, so a mesh that
    // cannot be fixed aborts the save instead of leaving a stale pair on disk.
    std::string              error;
    dash::anim::DashMeshData remappedMesh;
    bool                     meshPending = false;
    if (reordered && haveMesh) {
        if (!remapMesh_) {
            report.message = "the topology moved: remapping the .dashmesh is not optional";
            return report;
        }
        if (!bs::loadRemappedMesh(meshPath, origToCurrent_, remappedMesh, error)) {
            report.message = "mesh remap: " + error + " (nothing was written)";
            return report;
        }
        meshPending = true;
    }

    if (!bs::saveSkeletonDoc(path_, doc_, error)) {
        report.message = "skeleton: " + error;
        return report;
    }

    std::string detail = "saved " + fs::path(path_).filename().string();

    if (meshPending) {
        if (dash::anim::writeDashMesh(meshPath, remappedMesh, error))
            detail += "; remapped skin indices in " + fs::path(meshPath).filename().string();
        else
            detail += "; MESH NOT REMAPPED (" + error + ")";
    } else if (reordered) {
        detail += "; no sibling .dashmesh to remap";
    }

    const std::vector<std::pair<std::string, std::string>> renames = pendingRenames();
    if (!renames.empty() && retargetAnim_) {
        const std::string animPath = siblingPath(".dashanim");
        std::error_code   animEc;
        if (fs::is_regular_file(animPath, animEc)) {
            const int touched = bs::renameAnimChannels(animPath, renames, error);
            if (touched < 0) detail += "; ANIM NOT RETARGETED (" + error + ")";
            else detail += "; retargeted " + std::to_string(touched) + " animation channel(s)";
        } else {
            detail += "; no sibling .dashanim to retarget";
        }
    }

    // The file on disk is the new baseline: renames and the permutation reset.
    originalNames_.clear();
    originalNames_.reserve(doc_.bones.size());
    for (const bs::Bone& b : doc_.bones) originalNames_.push_back(b.name);
    origToCurrent_.resize(doc_.bones.size());
    for (std::size_t i = 0; i < origToCurrent_.size(); ++i)
        origToCurrent_[i] = static_cast<int>(i);

    dirty_         = false;
    report.ok      = true;
    report.message = detail;
    return report;
}

// ─────────────────────────────────────────────────────────────────────────────
void BoneStructurePanel::drawSourceBar(const std::string& assetsRoot,
                                       const std::string& libraryRoot,
                                       LogCallback& logCb)
{
    ImGui::SetNextItemWidth(-260.f);
    ImGui::InputTextWithHint("##bs_path", "path to a .dashskel file", pathBuf_, sizeof(pathBuf_));

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_FOLDER_OPEN " Load")) {
        std::string error;
        if (load(pathBuf_, error)) {
            status_        = "loaded " + path_;
            statusIsError_ = false;
        } else {
            status_        = error;
            statusIsError_ = true;
        }
        if (logCb) logCb("[BoneStructure] " + status_);
    }

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_MAGNIFYING_GLASS " Scan")) {
        foundFiles_ = scanForSkeletons(libraryRoot);
        for (const std::string& p : scanForSkeletons(assetsRoot)) foundFiles_.push_back(p);
        scanned_ = true;
    }

    ImGui::SameLine();
    ImGui::BeginDisabled(path_.empty());
    if (ImGui::Button(ICON_FA_ARROWS_ROTATE " Revert")) {
        const std::string toReload = path_;
        std::string       error;
        if (load(toReload, error)) {
            status_        = "reloaded from disk, edits discarded";
            statusIsError_ = false;
        } else {
            status_        = error;
            statusIsError_ = true;
        }
        if (logCb) logCb("[BoneStructure] " + status_);
    }
    ImGui::EndDisabled();

    if (scanned_ && !foundFiles_.empty()) {
        if (ImGui::TreeNodeEx("##bs_found", ImGuiTreeNodeFlags_DefaultOpen, "%zu file(s) found",
                              foundFiles_.size())) {
            for (const std::string& p : foundFiles_) {
                if (!ImGui::Selectable(p.c_str(), p == path_)) continue;
                std::string error;
                if (load(p, error)) {
                    status_        = "loaded " + p;
                    statusIsError_ = false;
                } else {
                    status_        = error;
                    statusIsError_ = true;
                }
                if (logCb) logCb("[BoneStructure] " + status_);
            }
            ImGui::TreePop();
        }
    } else if (scanned_) {
        ImGui::TextDisabled("No .dashskel under the project. Import a skinned model first.");
    }

    if (!status_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, statusIsError_ ? kErrorColor : kOkColor);
        ImGui::TextWrapped("%s", status_.c_str());
        ImGui::PopStyleColor();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void BoneStructurePanel::drawBoneNode(int index, const std::vector<bool>& visible)
{
    const std::size_t i = static_cast<std::size_t>(index);
    if (i >= doc_.bones.size() || !visible[i]) return;

    const std::vector<int>& kids = children_[i];

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                               ImGuiTreeNodeFlags_SpanAvailWidth |
                               ImGuiTreeNodeFlags_DefaultOpen;
    if (kids.empty()) flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (index == selected_) flags |= ImGuiTreeNodeFlags_Selected;

    bool flagged = false;
    for (const bs::Issue& issue : issues_)
        if (issue.bone == index && issue.severity == bs::IssueSeverity::Error) flagged = true;

    ImGui::PushID(index);
    if (flagged) ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
    const bool open = ImGui::TreeNodeEx("##bone", flags, ICON_FA_BONE "  %s   #%d",
                                        doc_.bones[i].name.c_str(), index);
    if (flagged) ImGui::PopStyleColor();
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) selectBone(index);

    if (open && !kids.empty()) {
        for (int kid : kids) drawBoneNode(kid, visible);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void BoneStructurePanel::drawTree(float width)
{
    ImGui::BeginGroup();
    ImGui::SetNextItemWidth(width);
    ImGui::InputTextWithHint("##bs_filter", ICON_FA_MAGNIFYING_GLASS "  Filter bones by name...",
                             filterBuf_, sizeof(filterBuf_));

    const std::vector<bool> visible = bs::filterVisibility(doc_.bones, filterBuf_);

    ImGui::BeginChild("##bs_tree", ImVec2(width, 0.f), ImGuiChildFlags_Borders);
    if (doc_.bones.empty()) {
        ImGui::TextDisabled("The skeleton has no bones.");
    } else {
        for (int root : roots_) drawBoneNode(root, visible);

        // Bones on a parent cycle hang off no root, so the tree walk never sees them.
        const std::vector<bool> reachable = bs::reachableFromRoots(doc_.bones);
        bool                    header    = false;
        for (int i = 0; i < static_cast<int>(doc_.bones.size()); ++i) {
            if (reachable[static_cast<std::size_t>(i)]) continue;
            if (!header) {
                ImGui::SeparatorText(ICON_FA_TRIANGLE_EXCLAMATION " Unreachable (cycle)");
                header = true;
            }
            ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
            ImGui::PushID(1000000 + i);
            if (ImGui::Selectable(doc_.bones[static_cast<std::size_t>(i)].name.c_str(),
                                  i == selected_))
                selectBone(i);
            ImGui::PopID();
            ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();
    ImGui::EndGroup();
}

// ─────────────────────────────────────────────────────────────────────────────
void BoneStructurePanel::drawDetails(LogCallback& logCb)
{
    if (selected_ < 0 || selected_ >= static_cast<int>(doc_.bones.size())) {
        ImGui::TextDisabled("Select a bone in the tree.");
        return;
    }

    const std::size_t sel  = static_cast<std::size_t>(selected_);
    bs::Bone&         bone = doc_.bones[sel];

    ImGui::SeparatorText(ICON_FA_BONE "  Bone");
    ImGui::Text("Index   %d", selected_);
    ImGui::Text("Depth   %d", bs::depthOf(doc_.bones, selected_));
    ImGui::Text("Parent  %s", bone.parent < 0
                                  ? "(root)"
                                  : bs::boneLabel(doc_.bones, bone.parent).c_str());
    ImGui::Text("Children %zu", children_[sel].size());

    // ── Rename ───────────────────────────────────────────────────────────────
    ImGui::SetNextItemWidth(-90.f);
    ImGui::InputText("##bs_name", nameBuf_, sizeof(nameBuf_));
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PEN " Rename")) {
        const PanelSnapshot   before = {doc_, origToCurrent_};
        const bs::EditResult result = bs::renameBone(doc_.bones, selected_, nameBuf_);
        if (result.ok) {
            boneUndo_.push(before);
            dirty_         = true;
            statusIsError_ = false;
            status_        = "renamed bone #" + std::to_string(selected_);
            refreshIssues();
            selectBone(selected_);
        } else {
            statusIsError_ = true;
            status_        = result.error;
        }
        if (logCb) logCb("[BoneStructure] " + status_);
    }

    // ── Reparent ─────────────────────────────────────────────────────────────
    const std::string preview = reparentTarget_ < 0
                                    ? std::string("(root)")
                                    : bs::boneLabel(doc_.bones, reparentTarget_);
    ImGui::SetNextItemWidth(-90.f);
    if (ImGui::BeginCombo("##bs_parent", preview.c_str())) {
        if (ImGui::Selectable("(root)", reparentTarget_ < 0)) reparentTarget_ = -1;
        for (int i = 0; i < static_cast<int>(doc_.bones.size()); ++i) {
            if (i == selected_) continue;
            ImGui::PushID(i);
            if (ImGui::Selectable(doc_.bones[static_cast<std::size_t>(i)].name.c_str(),
                                  i == reparentTarget_))
                reparentTarget_ = i;
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_SITEMAP " Reparent")) {
        const PanelSnapshot     before = {doc_, origToCurrent_};
        const bs::ReorderResult result = bs::reparent(doc_.bones, selected_, reparentTarget_);
        if (result.ok) {
            boneUndo_.push(before);
            const bool reordered = result.reordered;
            applyReorder(result);
            statusIsError_ = false;
            status_ = reordered ? "reparented and re-sorted; bone indices moved"
                                : "reparented, indices unchanged";
        } else {
            statusIsError_ = true;
            status_        = result.error;
        }
        if (logCb) logCb("[BoneStructure] " + status_);
    }

    // ── Bind transform ───────────────────────────────────────────────────────
    trsSummary("localBind (relative to the parent)", bone.localBind);

    float t[3] = {bone.localBind.m[12], bone.localBind.m[13], bone.localBind.m[14]};
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool translationEdited = ImGui::DragFloat3("##bs_translation", t, 0.01f);
    if (ImGui::IsItemActivated()) pushUndoSnapshot();
    if (translationEdited) {
        bs::setTranslation(bone.localBind, dash::anim::Vec3{t[0], t[1], t[2]});
        if (autoRecomputeOffsets_) bs::recomputeOffsetsFromBindPose(doc_);
        dirty_ = true;
    }
    ImGui::TextDisabled("Editing the bind translation");
    ImGui::Checkbox("Recompute offset matrices from the bind pose", &autoRecomputeOffsets_);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("offsetMatrix is the inverse bind pose. Moving localBind without\n"
                          "rebuilding it makes the mesh render deformed at rest.");
    }

    trsSummary("offsetMatrix (inverse bind pose)", bone.offsetMatrix);
    matrixRows("localBind 4x4", bone.localBind);
    matrixRows("offsetMatrix 4x4", bone.offsetMatrix);
}

// ─────────────────────────────────────────────────────────────────────────────
void BoneStructurePanel::drawIssues()
{
    int errors = 0;
    for (const bs::Issue& issue : issues_)
        if (issue.severity == bs::IssueSeverity::Error) ++errors;

    ImGui::SeparatorText(ICON_FA_TRIANGLE_EXCLAMATION "  Validation");
    if (issues_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kOkColor);
        ImGui::TextUnformatted(ICON_FA_CIRCLE_CHECK " The hierarchy is consistent.");
        ImGui::PopStyleColor();
        return;
    }

    ImGui::Text("%d error(s), %d warning(s)", errors,
                static_cast<int>(issues_.size()) - errors);

    ImGui::BeginChild("##bs_issues", ImVec2(0.f, 110.f), ImGuiChildFlags_Borders);
    for (int i = 0; i < static_cast<int>(issues_.size()); ++i) {
        const bs::Issue& issue = issues_[static_cast<std::size_t>(i)];
        const bool       err   = issue.severity == bs::IssueSeverity::Error;
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_Text, err ? kErrorColor : kWarningColor);
        if (ImGui::Selectable(issue.message.c_str()) && issue.bone >= 0) selectBone(issue.bone);
        ImGui::PopStyleColor();
        ImGui::PopID();
    }
    ImGui::EndChild();

    const bool sortable = errors > 0;
    ImGui::BeginDisabled(!sortable);
    if (ImGui::Button(ICON_FA_SITEMAP " Sort topologically")) {
        const PanelSnapshot     before = {doc_, origToCurrent_};
        const bs::ReorderResult result = bs::topologicalSort(doc_.bones);
        if (result.ok) {
            boneUndo_.push(before);
            applyReorder(result);
            statusIsError_ = false;
            status_        = "re-sorted so every parent comes before its children";
        } else {
            statusIsError_ = true;
            status_        = result.error;
        }
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered() && sortable)
        ImGui::SetTooltip("Fixes parent-after-child ordering. Cannot fix cycles.");
}

// ─────────────────────────────────────────────────────────────────────────────
void BoneStructurePanel::drawSaveSection(LogCallback& logCb)
{
    ImGui::SeparatorText(ICON_FA_FLOPPY_DISK "  Save");

    const bool reordered = !bs::isIdentityPermutation(origToCurrent_);
    const std::vector<std::pair<std::string, std::string>> renames = pendingRenames();

    const std::string meshPath = siblingPath(".dashmesh");
    const std::string animPath = siblingPath(".dashanim");
    std::error_code   ec;
    const bool        haveMesh = !meshPath.empty() && fs::is_regular_file(meshPath, ec);
    const bool        haveAnim = !animPath.empty() && fs::is_regular_file(animPath, ec);

    bool blocked = bs::hasErrors(issues_);

    if (reordered) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
        ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION
                           " The topology moved. SkinnedVertex references bones by INDEX, so "
                           "the baked .dashmesh is stale unless its skin stream is remapped "
                           "through the same permutation.");
        ImGui::PopStyleColor();
        if (haveMesh) {
            ImGui::Checkbox("Remap bone indices in the sibling .dashmesh", &remapMesh_);
            if (!remapMesh_) {
                blocked = true;
                ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
                ImGui::TextWrapped("Saving is blocked: writing the reordered skeleton without "
                                   "remapping the mesh would break the skinning.");
                ImGui::PopStyleColor();
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
            ImGui::TextWrapped("No sibling .dashmesh next to this .dashskel: any mesh baked "
                               "against the old order stays stale and cannot be fixed here.");
            ImGui::PopStyleColor();
        }
    }

    if (!renames.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kWarningColor);
        ImGui::TextWrapped(ICON_FA_TRIANGLE_EXCLAMATION
                           " %zu bone(s) renamed. Animation channels bind to bones by NAME, so "
                           "every .dashanim and .animsm.json that mentions the old name stops "
                           "matching.", renames.size());
        ImGui::PopStyleColor();
        if (haveAnim) {
            ImGui::Checkbox("Retarget the sibling .dashanim channels", &retargetAnim_);
        } else {
            ImGui::TextDisabled("No sibling .dashanim to retarget.");
        }
        for (const auto& rename : renames)
            ImGui::BulletText("%s  \xe2\x86\x92  %s", rename.first.c_str(), rename.second.c_str());
    }

    ImGui::BeginDisabled(blocked || !dirty_);
    if (ImGui::Button(ICON_FA_FLOPPY_DISK " Save .dashskel", ImVec2(-FLT_MIN, 0.f))) {
        const SaveReport report = save();
        status_                 = report.message;
        statusIsError_          = !report.ok;
        if (logCb) logCb("[BoneStructure] " + status_);
    }
    ImGui::EndDisabled();

    if (!dirty_) ImGui::TextDisabled("No unsaved edits.");
    else if (blocked) ImGui::TextDisabled("Saving is blocked, see above.");
}

// ─────────────────────────────────────────────────────────────────────────────
// Undo/redo — local to this panel; never touches the scene's command stack.
// ─────────────────────────────────────────────────────────────────────────────
void BoneStructurePanel::pushUndoSnapshot()
{
    boneUndo_.push(PanelSnapshot{doc_, origToCurrent_});
}

bool BoneStructurePanel::undo()
{
    PanelSnapshot current{doc_, origToCurrent_};
    if (!boneUndo_.undoTo(current)) return false;
    doc_           = std::move(current.doc);
    origToCurrent_ = std::move(current.origToCurrent);
    dirty_         = true;
    refreshIssues();
    selectBone(std::min(selected_, static_cast<int>(doc_.bones.size()) - 1));
    return true;
}

bool BoneStructurePanel::redo()
{
    PanelSnapshot current{doc_, origToCurrent_};
    if (!boneUndo_.redoTo(current)) return false;
    doc_           = std::move(current.doc);
    origToCurrent_ = std::move(current.origToCurrent);
    dirty_         = true;
    refreshIssues();
    selectBone(std::min(selected_, static_cast<int>(doc_.bones.size()) - 1));
    return true;
}

void BoneStructurePanel::handleUndoRedoShortcuts(LogCallback& logCb)
{
    if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) return;
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput || !io.KeyCtrl) return;

    if (io.KeyShift) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && redo()) {
            status_        = "redo";
            statusIsError_ = false;
            if (logCb) logCb("[BoneStructure] " + status_);
        }
    } else if (ImGui::IsKeyPressed(ImGuiKey_Z, false) && undo()) {
        status_        = "undo";
        statusIsError_ = false;
        if (logCb) logCb("[BoneStructure] " + status_);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// 3D preview
// ─────────────────────────────────────────────────────────────────────────────
void BoneStructurePanel::resetPreviewCamera()
{
    previewCam_ = bs::OrbitCamera{};
    if (doc_.bones.empty()) {
        previewGridSpacing_ = 1.f;
        previewGridY_       = 0.f;
        return;
    }

    const std::vector<dash::anim::Mat4> globals = bs::bindGlobals(doc_);
    dash::anim::Vec3                    mn{globals[0].m[12], globals[0].m[13], globals[0].m[14]};
    dash::anim::Vec3                    mx = mn;
    for (const dash::anim::Mat4& g : globals) {
        mn.x = std::min(mn.x, g.m[12]); mx.x = std::max(mx.x, g.m[12]);
        mn.y = std::min(mn.y, g.m[13]); mx.y = std::max(mx.y, g.m[13]);
        mn.z = std::min(mn.z, g.m[14]); mx.z = std::max(mx.z, g.m[14]);
    }

    const dash::anim::Vec3 center{(mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, (mn.z + mx.z) * 0.5f};
    const float extent = std::max({mx.x - mn.x, mx.y - mn.y, mx.z - mn.z});
    const float radius = extent > 1e-4f ? extent * 0.5f : 1.f;

    previewCam_.focus   = center;
    previewCam_.distance = radius * 2.6f;
    previewCam_.nearZ   = std::max(0.01f, radius * 0.01f);
    previewCam_.farZ    = radius * 50.f;
    previewGridSpacing_ = radius / 5.f;
    previewGridY_       = mn.y;
}

void BoneStructurePanel::ensurePreviewAnimLoaded()
{
    if (previewAnimScanned_) return;
    previewAnimScanned_ = true;
    previewClips_.clear();
    previewClipIndex_ = -1;

    const std::string animPath = siblingPath(".dashanim");
    std::error_code   ec;
    if (animPath.empty() || !fs::is_regular_file(animPath, ec)) return;

    std::string                            error;
    std::vector<dash::anim::AnimationClip> clips;
    if (dash::anim::readAnimationClips(animPath, clips, error)) {
        previewClips_ = std::move(clips);
        if (!previewClips_.empty()) previewClipIndex_ = 0;
    }
}

void BoneStructurePanel::drawPreviewToolbar()
{
    ensurePreviewAnimLoaded();

    if (ImGui::Button(ICON_FA_LOCATION_CROSSHAIRS " Reset camera")) resetPreviewCamera();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.f);
    ImGui::SliderFloat("##bs_preview_h", &previewHeight_, 220.f, 800.f, "size %.0f");

    ImGui::SameLine();
    ImGui::BeginDisabled(!boneUndo_.canUndo());
    if (ImGui::Button(ICON_FA_ROTATE_LEFT " Undo")) undo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!boneUndo_.canRedo());
    if (ImGui::Button(ICON_FA_ROTATE_RIGHT " Redo")) redo();
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(Ctrl+Z / Ctrl+Shift+Z)");

    const bool hasClips     = !previewClips_.empty();
    const bool bindSelected = previewPose_ == PreviewPose::Bind;
    if (ImGui::RadioButton("Bind pose", bindSelected)) previewPose_ = PreviewPose::Bind;
    ImGui::SameLine();
    ImGui::BeginDisabled(!hasClips);
    if (ImGui::RadioButton("Animated", !bindSelected)) previewPose_ = PreviewPose::Animated;
    ImGui::EndDisabled();
    if (!hasClips) {
        ImGui::SameLine();
        ImGui::TextDisabled("(no sibling .dashanim clips)");
    }

    if (previewPose_ == PreviewPose::Animated && hasClips) {
        if (previewClipIndex_ < 0 || previewClipIndex_ >= static_cast<int>(previewClips_.size()))
            previewClipIndex_ = 0;
        const dash::anim::AnimationClip& clip =
            previewClips_[static_cast<std::size_t>(previewClipIndex_)];

        ImGui::SetNextItemWidth(180.f);
        if (ImGui::BeginCombo("##bs_preview_clip", clip.name.c_str())) {
            for (int i = 0; i < static_cast<int>(previewClips_.size()); ++i) {
                const bool isSel = i == previewClipIndex_;
                if (ImGui::Selectable(previewClips_[static_cast<std::size_t>(i)].name.c_str(), isSel)) {
                    previewClipIndex_     = i;
                    previewClipTimeTicks_ = 0.f;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button(previewPlaying_ ? ICON_FA_PAUSE : ICON_FA_PLAY))
            previewPlaying_ = !previewPlaying_;

        const float durationSeconds = std::max(clip.durationSeconds(), 0.001f);
        float       timeSeconds     = clip.ticksPerSecond > 0.f
                                         ? previewClipTimeTicks_ / clip.ticksPerSecond
                                         : previewClipTimeTicks_;
        ImGui::SetNextItemWidth(220.f);
        if (ImGui::SliderFloat("##bs_preview_scrub", &timeSeconds, 0.f, durationSeconds, "%.2f s")) {
            previewClipTimeTicks_ = timeSeconds * clip.ticksPerSecond;
            previewPlaying_       = false;
        }

        if (previewPlaying_) {
            previewClipTimeTicks_ += ImGui::GetIO().DeltaTime * clip.ticksPerSecond;
            previewClipTimeTicks_ = clip.normalizeTime(previewClipTimeTicks_, true);
        }

        ImGui::TextDisabled("Bone edits apply to the bind pose; switch back to move the gizmo.");
    }
}

void BoneStructurePanel::drawPreviewCanvas(LogCallback& logCb)
{
    ImGui::BeginChild("##bs_preview3d", ImVec2(0.f, previewHeight_), ImGuiChildFlags_Borders);

    const ImVec2 canvasPos  = ImGui::GetCursorScreenPos();
    const ImVec2 canvasSize = ImGui::GetContentRegionAvail();

    if (canvasSize.x < 4.f || canvasSize.y < 4.f || doc_.bones.empty()) {
        ImGui::Dummy(canvasSize);
        ImGui::EndChild();
        return;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (gpuPreviewTex_ != 0) {
        // Real render from last frame (see EditorApp::renderWorldToTexture()):
        // one frame of latency, imperceptible at interactive rates and the
        // same pipelining every offscreen viewport in this editor already has.
        dl->AddImage(reinterpret_cast<ImTextureID>(gpuPreviewTex_), canvasPos,
                     ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y));
    } else {
        dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
                          IM_COL32(28, 28, 34, 255));
    }

    const bs::Mat4 viewOnly = bs::lookAtMatrix(bs::orbitEye(previewCam_), previewCam_.focus,
                                               dash::anim::Vec3{0.f, 1.f, 0.f});
    const bs::Mat4 viewProj = bs::viewProjection(previewCam_, canvasSize.x, canvasSize.y);

    const std::vector<bs::Mat4> globals =
        (previewPose_ == PreviewPose::Animated && previewClipIndex_ >= 0 &&
         previewClipIndex_ < static_cast<int>(previewClips_.size()))
            ? bs::animatedGlobals(doc_, previewClips_[static_cast<std::size_t>(previewClipIndex_)],
                                  previewClipTimeTicks_)
            : bs::bindGlobals(doc_);

    // Cached for EditorApp to render the actual skinned mesh next frame; see
    // the "GPU mesh preview" getters on this class.
    previewSkinningMatrices_ = bs::skinningMatricesFromGlobals(doc_, globals);
    std::memcpy(previewViewProjFlat_, viewProj.m, sizeof(previewViewProjFlat_));
    previewCanvasW_ = canvasSize.x;
    previewCanvasH_ = canvasSize.y;

    std::vector<dash::anim::Vec3> jointWorld(globals.size());
    std::vector<bs::ScreenPoint>  jointScreen(globals.size());
    for (std::size_t i = 0; i < globals.size(); ++i) {
        jointWorld[i]  = {globals[i].m[12], globals[i].m[13], globals[i].m[14]};
        jointScreen[i] = bs::project(viewProj, canvasSize.x, canvasSize.y, jointWorld[i]);
    }

    auto toScreen = [&](const bs::ScreenPoint& p) {
        return ImVec2(canvasPos.x + p.x, canvasPos.y + p.y);
    };

    // ── Floor grid ───────────────────────────────────────────────────────────
    const float spacing = previewGridSpacing_ > 1e-4f ? previewGridSpacing_ : 1.f;
    constexpr int kGridLines = 10;
    for (int i = -kGridLines; i <= kGridLines; ++i) {
        const float offset = static_cast<float>(i) * spacing;
        const float extent = static_cast<float>(kGridLines) * spacing;
        const ImU32 col    = (i == 0) ? IM_COL32(150, 150, 165, 170) : IM_COL32(80, 80, 92, 110);

        const bs::ScreenPoint za = bs::project(viewProj, canvasSize.x, canvasSize.y,
                                               {previewCam_.focus.x + offset, previewGridY_,
                                                previewCam_.focus.z - extent});
        const bs::ScreenPoint zb = bs::project(viewProj, canvasSize.x, canvasSize.y,
                                               {previewCam_.focus.x + offset, previewGridY_,
                                                previewCam_.focus.z + extent});
        if (za.visible && zb.visible) dl->AddLine(toScreen(za), toScreen(zb), col);

        const bs::ScreenPoint xa = bs::project(viewProj, canvasSize.x, canvasSize.y,
                                               {previewCam_.focus.x - extent, previewGridY_,
                                                previewCam_.focus.z + offset});
        const bs::ScreenPoint xb = bs::project(viewProj, canvasSize.x, canvasSize.y,
                                               {previewCam_.focus.x + extent, previewGridY_,
                                                previewCam_.focus.z + offset});
        if (xa.visible && xb.visible) dl->AddLine(toScreen(xa), toScreen(xb), col);
    }

    // ── Bones: a segment per parent/child pair, a circle per joint ──────────
    int drawnSegments = 0;
    for (int i = 0; i < static_cast<int>(doc_.bones.size()); ++i) {
        const int parent = bs::parentOf(doc_.bones, i);
        if (parent < 0) continue;
        const bs::ScreenPoint& pa = jointScreen[static_cast<std::size_t>(parent)];
        const bs::ScreenPoint& pb = jointScreen[static_cast<std::size_t>(i)];
        if (!pa.visible || !pb.visible) continue;

        ImU32 col = IM_COL32(205, 205, 215, 220);
        if (i == selected_) col = IM_COL32(255, 200, 60, 255);
        else if (parent == selected_) col = IM_COL32(255, 230, 150, 200);
        dl->AddLine(toScreen(pa), toScreen(pb), col, i == selected_ ? 2.6f : 1.6f);
        ++drawnSegments;
    }

    int drawnJoints = 0;
    for (int i = 0; i < static_cast<int>(doc_.bones.size()); ++i) {
        const bs::ScreenPoint& p = jointScreen[static_cast<std::size_t>(i)];
        if (!p.visible) continue;
        ++drawnJoints;

        ImU32 col = IM_COL32(230, 230, 238, 255);
        float r   = 3.2f;
        if (i == selected_) { col = IM_COL32(255, 200, 60, 255); r = 5.f; }
        else if (bs::parentOf(doc_.bones, i) == selected_) { col = IM_COL32(255, 230, 150, 255); r = 4.f; }
        dl->AddCircleFilled(toScreen(p), r, col);
    }

    if (const char* trace = std::getenv("DASH_BONE_PREVIEW_TRACE")) {
        (void)trace;
        static float accum = 0.f;
        accum += ImGui::GetIO().DeltaTime;
        if (accum > 1.0f) {
            accum = 0.f;
            std::fprintf(stdout,
                         "[BoneStructurePreview] %d/%zu joint(s) visible, %d bone segment(s) drawn, "
                         "selected=%d, pose=%s\n",
                         drawnJoints, doc_.bones.size(), drawnSegments, selected_,
                         previewPose_ == PreviewPose::Animated ? "animated" : "bind");
        }
    }

    // ── Interaction: gizmo first (left button), then orbit/pick/zoom ────────
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("##bs_preview3d_canvas", canvasSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool   hovered  = ImGui::IsItemHovered();
    const ImVec2 mousePos = ImGui::GetIO().MousePos;

    const bool gizmoEligible = previewPose_ == PreviewPose::Bind && selected_ >= 0 &&
                               selected_ < static_cast<int>(doc_.bones.size());

    dash::gizmo::GizmoInput  gizmoIn;
    dash::gizmo::GizmoOutput gizmoOut;
    if (gizmoEligible) {
        const dash::anim::Vec3& pivot = jointWorld[static_cast<std::size_t>(selected_)];
        gizmoIn.viewProj      = viewProj.m;
        gizmoIn.rect          = {canvasPos.x, canvasPos.y, canvasSize.x, canvasSize.y};
        gizmoIn.pivot         = {pivot.x, pivot.y, pivot.z};
        gizmoIn.mouseX        = mousePos.x;
        gizmoIn.mouseY        = mousePos.y;
        gizmoIn.hovered       = hovered;
        gizmoIn.mouseClicked  = hovered && previewDragMode_ == PreviewDragMode::None &&
                               ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        gizmoIn.mouseDown     = ImGui::IsMouseDown(ImGuiMouseButton_Left);
        gizmoIn.mouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
        gizmoIn.snap          = ImGui::GetIO().KeyShift;

        previewGizmo_.setMode(dash::gizmo::Mode::Translate);
        gizmoOut = previewGizmo_.update(gizmoIn);

        if (gizmoOut.dragStarted) {
            previewDragMode_        = PreviewDragMode::Gizmo;
            previewGizmoUndoPushed_ = false;
            const bs::Bone& bone    = doc_.bones[static_cast<std::size_t>(selected_)];
            previewGizmoStartLocalT_ = {bone.localBind.m[12], bone.localBind.m[13],
                                        bone.localBind.m[14]};
            const int parent          = bs::parentOf(doc_.bones, selected_);
            previewGizmoParentGlobal_ = parent >= 0 ? globals[static_cast<std::size_t>(parent)]
                                                     : dash::anim::identity();
        }

        if (gizmoOut.dragging) {
            const dash::anim::Vec3 worldDelta{gizmoOut.translation.x, gizmoOut.translation.y,
                                              gizmoOut.translation.z};
            const dash::anim::Vec3 localDelta =
                bs::worldDeltaToLocal(previewGizmoParentGlobal_, worldDelta);
            if (!previewGizmoUndoPushed_ &&
                (localDelta.x != 0.f || localDelta.y != 0.f || localDelta.z != 0.f)) {
                pushUndoSnapshot();
                previewGizmoUndoPushed_ = true;
            }
            bs::Bone& bone = doc_.bones[static_cast<std::size_t>(selected_)];
            bs::setTranslation(bone.localBind,
                               dash::anim::Vec3{previewGizmoStartLocalT_.x + localDelta.x,
                                                previewGizmoStartLocalT_.y + localDelta.y,
                                                previewGizmoStartLocalT_.z + localDelta.z});
        }

        if (gizmoOut.dragEnded) {
            if (previewGizmoUndoPushed_) {
                bs::recomputeOffsetsFromBindPose(doc_);
                dirty_ = true;
                refreshIssues();
                status_        = "moved '" + doc_.bones[static_cast<std::size_t>(selected_)].name +
                          "' with the gizmo";
                statusIsError_ = false;
                if (logCb) logCb("[BoneStructure] " + status_);
            }
            previewDragMode_ = PreviewDragMode::None;
        }
    } else {
        previewGizmo_.cancel();
    }

    if (previewDragMode_ == PreviewDragMode::None && hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        previewDragMode_ = PreviewDragMode::Orbit;
    }
    if (previewDragMode_ == PreviewDragMode::Orbit) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            const ImVec2 d = ImGui::GetIO().MouseDelta;
            previewCam_.yawDeg += d.x * 0.4f;
            previewCam_.pitchDeg =
                std::clamp(previewCam_.pitchDeg - d.y * 0.4f, -85.f, 85.f);
        } else {
            previewDragMode_ = PreviewDragMode::None;
        }
    }

    if (previewDragMode_ == PreviewDragMode::None && hovered && !gizmoOut.handleHovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const int picked = bs::pickJoint(jointScreen, mousePos.x - canvasPos.x,
                                         mousePos.y - canvasPos.y, 10.f);
        if (picked >= 0) selectBone(picked);
    }

    if (hovered && ImGui::GetIO().MouseWheel != 0.f) {
        previewCam_.distance *= std::pow(0.9f, ImGui::GetIO().MouseWheel);
        previewCam_.distance =
            std::clamp(previewCam_.distance, spacing * 0.1f, spacing * 500.f);
    }

    if (gizmoEligible) previewGizmo_.draw(dl, gizmoIn, gizmoOut);

    // ── Corner axis gizmo: screen-space rotation only, no perspective ───────
    {
        const dash::anim::Vec3 rgt{bs::matAt(viewOnly, 0, 0), bs::matAt(viewOnly, 0, 1),
                                   bs::matAt(viewOnly, 0, 2)};
        const dash::anim::Vec3 up{bs::matAt(viewOnly, 1, 0), bs::matAt(viewOnly, 1, 1),
                                  bs::matAt(viewOnly, 1, 2)};
        const ImVec2 center(canvasPos.x + 28.f, canvasPos.y + 28.f);
        auto         axisTip = [&](const dash::anim::Vec3& axis, ImU32 col, const char* label) {
            const float sx = rgt.x * axis.x + rgt.y * axis.y + rgt.z * axis.z;
            const float sy = up.x * axis.x + up.y * axis.y + up.z * axis.z;
            const ImVec2 tip(center.x + sx * 20.f, center.y - sy * 20.f);
            dl->AddLine(center, tip, col, 2.f);
            dl->AddCircleFilled(tip, 3.f, col);
            dl->AddText(ImVec2(tip.x + 4.f, tip.y - 6.f), col, label);
        };
        axisTip({1.f, 0.f, 0.f}, IM_COL32(226, 74, 74, 255), "X");
        axisTip({0.f, 1.f, 0.f}, IM_COL32(112, 209, 96, 255), "Y");
        axisTip({0.f, 0.f, 1.f}, IM_COL32(72, 140, 236, 255), "Z");
    }

    ImGui::EndChild();
    ImGui::TextDisabled("Right-drag: orbit   Wheel: zoom   Left-click: select joint   "
                        "Drag an arrow: move the selected bone (bind pose)");
}

void BoneStructurePanel::drawPreview3D(LogCallback& logCb)
{
    ImGui::SeparatorText(ICON_FA_CUBE "  3D Preview");
    drawPreviewToolbar();
    drawPreviewCanvas(logCb);
}

// ─────────────────────────────────────────────────────────────────────────────
void BoneStructurePanel::drawAssignedModel()
{
    const std::string meshPath = siblingPath(".dashmesh");
    if (meshPath.empty()) return;

    std::error_code ec;
    ImGui::SeparatorText(ICON_FA_CUBE "  Assigned Model");

    const bool haveMesh = fs::is_regular_file(meshPath, ec);
    ImGui::TextWrapped("Mesh: %s", fs::path(meshPath).filename().string().c_str());
    if (haveMesh) {
        dash::anim::DashMeshData meshData;
        std::string              error;
        if (dash::anim::readDashMesh(meshPath, meshData, error)) {
            ImGui::TextColored(kOkColor, "  %zu vertices, %zu indices, %s, %u bone(s) expected",
                               meshData.vertices.size(), meshData.indices.size(),
                               meshData.isSkinned() ? "skinned" : "static", meshData.boneCount);
            if (meshData.isSkinned() && meshData.boneCount != doc_.bones.size()) {
                ImGui::TextColored(kWarningColor,
                                   "  Warning: mesh expects %u bone(s) but this skeleton has %zu.",
                                   meshData.boneCount, doc_.bones.size());
            }
        } else {
            ImGui::TextColored(kErrorColor, "  Could not read mesh: %s", error.c_str());
        }
    } else {
        ImGui::TextColored(kWarningColor, "  No sibling .dashmesh next to this .dashskel yet.");
    }

    const std::string animPath = siblingPath(".dashanim");
    const bool        haveAnim = fs::is_regular_file(animPath, ec);
    ImGui::TextWrapped("Animation: %s", fs::path(animPath).filename().string().c_str());
    if (haveAnim) {
        ensurePreviewAnimLoaded();
        ImGui::TextColored(kOkColor, "  %zu clip(s) — see the 3D Preview below to play them",
                           previewClips_.size());
    } else {
        ImGui::TextDisabled("  No sibling .dashanim found.");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
void BoneStructurePanel::draw(const std::string& assetsRoot, const std::string& libraryRoot,
                              LogCallback logCb)
{
    if (!ImGui::Begin("Bone Structure")) {
        ImGui::End();
        return;
    }

    drawSourceBar(assetsRoot, libraryRoot, logCb);

    if (path_.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("Load a .dashskel to inspect its bone hierarchy.");
        ImGui::End();
        return;
    }

    handleUndoRedoShortcuts(logCb);

    ImGui::Separator();
    ImGui::Text("%zu bone(s)%s", doc_.bones.size(), dirty_ ? "  *modified*" : "");

    drawAssignedModel();
    drawPreview3D(logCb);

    if (ImGui::BeginTable("##bs_layout", 2,
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("##tree", ImGuiTableColumnFlags_WidthStretch, 0.42f);
        ImGui::TableSetupColumn("##details", ImGuiTableColumnFlags_WidthStretch, 0.58f);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        drawTree(ImGui::GetContentRegionAvail().x);

        ImGui::TableNextColumn();
        ImGui::BeginChild("##bs_details", ImVec2(0.f, 0.f), ImGuiChildFlags_Borders);
        drawDetails(logCb);
        drawIssues();
        drawSaveSection(logCb);
        ImGui::EndChild();

        ImGui::EndTable();
    }

    ImGui::End();
}
