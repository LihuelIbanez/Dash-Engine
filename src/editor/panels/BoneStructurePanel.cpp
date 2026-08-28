#include "BoneStructurePanel.h"

#include "IconsFontAwesome6.h"
#include "imgui.h"

#include <cfloat>
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
        const bs::EditResult result = bs::renameBone(doc_.bones, selected_, nameBuf_);
        if (result.ok) {
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
        const bs::ReorderResult result = bs::reparent(doc_.bones, selected_, reparentTarget_);
        if (result.ok) {
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
    if (ImGui::DragFloat3("##bs_translation", t, 0.01f)) {
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
        const bs::ReorderResult result = bs::topologicalSort(doc_.bones);
        if (result.ok) {
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

    ImGui::Separator();
    ImGui::Text("%zu bone(s)%s", doc_.bones.size(), dirty_ ? "  *modified*" : "");

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
