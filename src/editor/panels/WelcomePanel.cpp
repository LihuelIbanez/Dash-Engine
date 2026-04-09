#include "WelcomePanel.h"
#include "platform/NativeFileDialogs.h"
#include "imgui.h"

#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

void copyToBuffer(char* dest, size_t size, const std::string& value)
{
    if (size == 0) return;
    std::strncpy(dest, value.c_str(), size - 1);
    dest[size - 1] = '\0';
}

}

bool WelcomePanel::tryOpenSelectedProject(const std::function<bool(const std::string&)>& onOpenProject,
                                          const std::function<void(const std::string&)>& logFn)
{
    std::string path(openPath_);
    if (path.empty()) {
        logFn("[Welcome] Select a project file or folder first.");
        return false;
    }

    if (onOpenProject(path)) {
        isOpen = false;
        ImGui::CloseCurrentPopup();
        return true;
    }

    logFn("[Welcome] Could not open project: " + path);
    return false;
}

void WelcomePanel::draw(const std::vector<std::string>& recentProjects,
                        const std::function<bool(const std::string&)>& onOpenProject,
                        const std::function<bool(const std::string&, const std::string&)>& onCreateProject,
                        const std::function<void(const std::string&)>& logFn)
{
    if (!isOpen) return;

    if (firstFrame_) {
        ImGui::OpenPopup("Welcome to DashEngine");
        firstFrame_ = false;
    }

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Welcome to DashEngine", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
        ImGui::TextUnformatted("Create or open a project to start editing.");
        ImGui::Separator();

        ImGui::TextUnformatted("Open Existing Project");
        ImGui::SetNextItemWidth(430.f);
        ImGui::InputText("##openpath", openPath_, sizeof(openPath_));
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(90, 0))) {
            const std::string selectedPath = NativeFileDialogs::pickProjectPath(openPath_);
            if (!selectedPath.empty()) {
                copyToBuffer(openPath_, sizeof(openPath_), selectedPath);
                tryOpenSelectedProject(onOpenProject, logFn);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Open", ImVec2(90, 0))) {
            tryOpenSelectedProject(onOpenProject, logFn);
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Create New Project");
        ImGui::SetNextItemWidth(260.f);
        ImGui::InputText("Project Name", createName_, sizeof(createName_));
        ImGui::SetNextItemWidth(430.f);
        ImGui::InputText("Project Directory", createDir_, sizeof(createDir_));
        ImGui::SameLine();
        if (ImGui::Button("Choose...", ImVec2(90, 0))) {
            const std::string selectedDir = NativeFileDialogs::pickProjectDirectory(createDir_);
            if (!selectedDir.empty()) {
                copyToBuffer(createDir_, sizeof(createDir_), selectedDir);
            }
        }
        if (ImGui::Button("Create Project", ImVec2(150, 0))) {
            std::string dir(createDir_);
            std::string name(createName_);
            if (dir.empty() || name.empty()) {
                logFn("[Welcome] Enter project name and directory.");
            } else if (onCreateProject(dir, name)) {
                isOpen = false;
                ImGui::CloseCurrentPopup();
            } else {
                logFn("[Welcome] Could not create project at: " + dir);
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Recent Projects");
        if (ImGui::BeginListBox("##recent_projects", ImVec2(620, 180))) {
            if (recentProjects.empty()) {
                ImGui::Selectable("(No recent projects)", false, ImGuiSelectableFlags_Disabled);
            } else {
                for (const auto& p : recentProjects) {
                    bool exists = fs::exists(p);
                    std::string label = exists ? p : (p + "  [missing]");
                    if (ImGui::Selectable(label.c_str(), false) && exists) {
                        if (onOpenProject(p)) {
                            isOpen = false;
                            ImGui::CloseCurrentPopup();
                        } else {
                            logFn("[Welcome] Could not open recent project: " + p);
                        }
                    }
                }
            }
            ImGui::EndListBox();
        }

        ImGui::EndPopup();
    }
}
