#include "WelcomePanel.h"
#include "imgui.h"
#include <filesystem>

namespace fs = std::filesystem;

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

        ImGui::TextUnformatted("Open Existing Project (.dashproject)");
        ImGui::SetNextItemWidth(520.f);
        ImGui::InputText("##openpath", openPath_, sizeof(openPath_));
        if (ImGui::Button("Open Project", ImVec2(150, 0))) {
            std::string p(openPath_);
            if (p.empty()) {
                logFn("[Welcome] Enter a .dashproject path first.");
            } else if (onOpenProject(p)) {
                isOpen = false;
                ImGui::CloseCurrentPopup();
            } else {
                logFn("[Welcome] Could not open project: " + p);
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Create New Project");
        ImGui::SetNextItemWidth(260.f);
        ImGui::InputText("Project Name", createName_, sizeof(createName_));
        ImGui::SetNextItemWidth(520.f);
        ImGui::InputText("Project Directory", createDir_, sizeof(createDir_));
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
