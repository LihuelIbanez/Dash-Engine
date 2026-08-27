#include "FileEditorPanel.h"
#include "imgui.h"
#include "IconsFontAwesome6.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
void FileEditorPanel::init(const std::string& resourcesDir)
{
    fileBrowserRoot_ = resourcesDir;
    std::strncpy(fileBrowserNavBuf_, fileBrowserRoot_.c_str(), sizeof(fileBrowserNavBuf_) - 1);
}

void FileEditorPanel::openFile(const std::string& path, const LogCallback& logCb)
{
    // If already open, just focus it
    for (int i = 0; i < (int)openFiles_.size(); ++i) {
        if (openFiles_[i].path == path) { activeFileTab_ = i; return; }
    }
    // Read file content
    std::ifstream ifs(path);
    if (!ifs.is_open()) { if (logCb) logCb("Cannot open: " + path); return; }
    std::ostringstream ss;
    ss << ifs.rdbuf();

    OpenFile f;
    f.path         = path;
    f.content      = ss.str();
    f.modified     = false;
    f.lastSnapshot = f.content;
    openFiles_.push_back(std::move(f));
    activeFileTab_ = (int)openFiles_.size() - 1;
    if (logCb) logCb("Opened: " + path);
}

void FileEditorPanel::saveFile(int idx, const LogCallback& logCb)
{
    if (idx < 0 || idx >= (int)openFiles_.size()) return;
    auto& f = openFiles_[idx];
    std::ofstream ofs(f.path);
    if (!ofs.is_open()) { if (logCb) logCb("Cannot save: " + f.path); return; }
    ofs << f.content;
    f.modified = false;
    if (logCb) logCb("Saved: " + f.path);
}

void FileEditorPanel::snapshotForUndo(OpenFile& f)
{
    if (f.content != f.lastSnapshot) {
        f.undoStack.push_back(f.lastSnapshot);
        if (f.undoStack.size() > 200) f.undoStack.erase(f.undoStack.begin());
        f.redoStack.clear();
        f.lastSnapshot = f.content;
    }
}

void FileEditorPanel::undoFile(OpenFile& f)
{
    if (f.undoStack.empty()) return;
    f.redoStack.push_back(f.content);
    f.content      = f.undoStack.back();
    f.lastSnapshot = f.content;
    f.undoStack.pop_back();
    f.modified = true;
}

void FileEditorPanel::redoFile(OpenFile& f)
{
    if (f.redoStack.empty()) return;
    f.undoStack.push_back(f.content);
    f.content      = f.redoStack.back();
    f.lastSnapshot = f.content;
    f.redoStack.pop_back();
    f.modified = true;
}

// ═════════════════════════════════════════════════════════════════════════════
// File Browser panel — recursive directory tree
// ═════════════════════════════════════════════════════════════════════════════
static void drawDirectoryTree(const fs::path& dir,
                              const char* filter,
                              std::string& clickedFile,
                              std::string& copiedPath,
                              const fs::path& workspaceRoot)
{
    std::string filterStr(filter ? filter : "");
    for (auto& c : filterStr)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Collect entries and sort (dirs first, then files)
    std::vector<fs::directory_entry> entries;
    std::error_code iterEc;
    for (auto& e : fs::directory_iterator(dir,
            fs::directory_options::skip_permission_denied, iterEc))
        entries.push_back(e);
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        if (a.is_directory() != b.is_directory())
            return a.is_directory() > b.is_directory();
        return a.path().filename().string() < b.path().filename().string();
    });

    for (auto& entry : entries) {
        std::string name = entry.path().filename().string();
        if (name[0] == '.') continue; // skip hidden

        if (entry.is_directory()) {
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
                                    | ImGuiTreeNodeFlags_SpanAvailWidth;
            bool open = ImGui::TreeNodeEx(name.c_str(), flags);
            if (open) {
                drawDirectoryTree(entry.path(), filter, clickedFile, copiedPath, workspaceRoot);
                ImGui::TreePop();
            }
        } else {
            // Apply filter to leaf files
            if (!filterStr.empty()) {
                std::string nameLower = name;
                for (auto& c : nameLower)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (nameLower.find(filterStr) == std::string::npos)
                    continue;
            }

            ImGuiTreeNodeFlags leafFlags = ImGuiTreeNodeFlags_Leaf
                                        | ImGuiTreeNodeFlags_NoTreePushOnOpen
                                        | ImGuiTreeNodeFlags_SpanAvailWidth;
            ImGui::TreeNodeEx(name.c_str(), leafFlags);

            if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                clickedFile = entry.path().string();

            // Right-click context menu
            if (ImGui::BeginPopupContextItem()) {
                ImGui::TextDisabled("%s", name.c_str());
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open in Editor"))
                    clickedFile = entry.path().string();
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_COPY "  Copy Full Path"))
                    copiedPath = entry.path().string();
                if (ImGui::MenuItem(ICON_FA_COPY "  Copy Relative Path")) {
                    std::error_code ec;
                    fs::path rel = fs::relative(entry.path(), workspaceRoot, ec);
                    copiedPath = ec ? entry.path().string() : rel.string();
                }
                ImGui::EndPopup();
            }
        }
    }
}

void FileEditorPanel::drawFileBrowser(const std::string& resourcesDir,
                                     const std::string& assetsRoot,
                                     const std::string& scenesDir,
                                     const LogCallback& logCb)
{
    ImGui::Begin("File Browser");

    const std::string& resDir = resourcesDir;

    // ── Navigation bar ────────────────────────────────────────────────────
    ImGui::SetNextItemWidth(-52.f);
    bool navEnter = ImGui::InputText("##navpath", fileBrowserNavBuf_,
                                     sizeof(fileBrowserNavBuf_),
                                     ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    bool navGo = ImGui::SmallButton("Go");
    if (navEnter || navGo) {
        fs::path p(fileBrowserNavBuf_);
        if (fs::is_directory(p))
            fileBrowserRoot_ = p.string();
        // Sync nav buf back to resolved root
        std::strncpy(fileBrowserNavBuf_, fileBrowserRoot_.c_str(),
                     sizeof(fileBrowserNavBuf_) - 1);
    }

    // ── Bookmarks ─────────────────────────────────────────────────────────
    auto bookmark = [&](const char* icon, const char* tip, const std::string& dir) {
        if (ImGui::SmallButton(icon)) {
            if (fs::is_directory(dir)) {
                fileBrowserRoot_ = dir;
                std::strncpy(fileBrowserNavBuf_, fileBrowserRoot_.c_str(),
                             sizeof(fileBrowserNavBuf_) - 1);
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
    };

    bookmark(ICON_FA_HOUSE,        resDir.c_str(),         resDir);
    bookmark(ICON_FA_IMAGE,        assetsRoot.c_str(),    assetsRoot);
    bookmark(ICON_FA_MAP,          scenesDir.c_str(),     scenesDir);
    bookmark(ICON_FA_CODE,         (resDir+"/src").c_str(), resDir + "/src");
    {
        std::string savesDir = resDir + "/saves";
        bookmark(ICON_FA_FLOPPY_DISK, savesDir.c_str(), savesDir);
    }
    // Up one level
    if (ImGui::SmallButton(ICON_FA_ARROW_UP)) {
        fs::path parent = fs::path(fileBrowserRoot_).parent_path();
        if (fs::is_directory(parent)) {
            fileBrowserRoot_ = parent.string();
            std::strncpy(fileBrowserNavBuf_, fileBrowserRoot_.c_str(),
                         sizeof(fileBrowserNavBuf_) - 1);
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Up one level");

    ImGui::NewLine();
    ImGui::Separator();

    // ── Filter bar ────────────────────────────────────────────────────────
    ImGui::SetNextItemWidth(-1.f);
    ImGui::InputTextWithHint("##fbfilter", ICON_FA_MAGNIFYING_GLASS "  Filter files...",
                              fileBrowserFilter_, sizeof(fileBrowserFilter_));
    ImGui::Separator();

    // ── Directory tree ────────────────────────────────────────────────────
    std::string clickedFile, copiedPath;
    if (fs::is_directory(fileBrowserRoot_)) {
        drawDirectoryTree(fileBrowserRoot_, fileBrowserFilter_,
                          clickedFile, copiedPath, fs::path(resDir));
    } else {
        ImGui::TextColored({0.957f,0.278f,0.278f,1.f}, "Path not found: %s",
                           fileBrowserRoot_.c_str());
    }

    if (!clickedFile.empty())
        openFile(clickedFile, logCb);

    if (!copiedPath.empty()) {
        ImGui::SetClipboardText(copiedPath.c_str());
        if (logCb) logCb("[Browser] Copied to clipboard: " + copiedPath);
    }

    ImGui::End();
}

// ═════════════════════════════════════════════════════════════════════════════
// File Editor panel — tabbed text editor
// ═════════════════════════════════════════════════════════════════════════════
void FileEditorPanel::drawFileEditor(const LogCallback& logCb)
{
    ImGui::Begin("File Editor");

    if (openFiles_.empty()) {
        ImGui::TextDisabled("Open a file from the File Browser.");
        ImGui::End();
        return;
    }

    // Tab bar
    if (ImGui::BeginTabBar("##FileTabs", ImGuiTabBarFlags_Reorderable
                                        | ImGuiTabBarFlags_AutoSelectNewTabs)) {
        for (int i = 0; i < (int)openFiles_.size(); ++i) {
            auto& f = openFiles_[i];
            std::string tabLabel = fs::path(f.path).filename().string();
            if (f.modified) tabLabel += " *";
            tabLabel += "###tab" + std::to_string(i);

            bool open = true;
            ImGuiTabItemFlags tabFlags = 0;
            if (ImGui::BeginTabItem(tabLabel.c_str(), &open, tabFlags)) {
                activeFileTab_ = i;

                // Cmd+Z = undo, Cmd+Shift+Z = redo
                {
                    ImGuiIO& io = ImGui::GetIO();
                    bool cmdHeld = io.KeySuper; // Cmd on macOS
                    if (cmdHeld && ImGui::IsKeyPressed(ImGuiKey_Z)) {
                        if (io.KeyShift)
                            redoFile(f);
                        else
                            undoFile(f);
                    }
                }

                // Save button  (also Cmd+S)
                {
                    ImGuiIO& io = ImGui::GetIO();
                    if (io.KeySuper && ImGui::IsKeyPressed(ImGuiKey_S))
                        saveFile(i, logCb);
                }
                if (f.modified) {
                    if (ImGui::Button("Save")) saveFile(i, logCb);
                    ImGui::SameLine();
                }
                ImGui::TextDisabled("%s", f.path.c_str());

                // Text editing area
                ImVec2 avail = ImGui::GetContentRegionAvail();
                // Ensure buffer is large enough for editing
                if (f.content.capacity() < f.content.size() + 65536)
                    f.content.reserve(f.content.size() + 65536);
                f.content.resize(f.content.capacity());

                std::string editorId = "##editor" + std::to_string(i);
                if (ImGui::InputTextMultiline(editorId.c_str(),
                        f.content.data(), f.content.capacity(), avail,
                        ImGuiInputTextFlags_AllowTabInput
                        | ImGuiInputTextFlags_CallbackResize,
                        [](ImGuiInputTextCallbackData* data) -> int {
                            if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
                                auto* s = (std::string*)data->UserData;
                                s->resize(data->BufTextLen);
                                data->Buf = s->data();
                            }
                            return 0;
                        }, &f.content)) {
                    f.content.resize(std::strlen(f.content.c_str()));
                    snapshotForUndo(f);
                    f.modified = true;
                }

                ImGui::EndTabItem();
            }
            if (!open) {
                // Tab closed
                openFiles_.erase(openFiles_.begin() + i);
                if (activeFileTab_ >= (int)openFiles_.size())
                    activeFileTab_ = (int)openFiles_.size() - 1;
                --i;
            }
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}
