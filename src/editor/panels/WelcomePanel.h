#pragma once

#include <functional>
#include <string>
#include <vector>

class WelcomePanel {
public:
    bool isOpen = true;

    // Reopen the panel (e.g. triggered from the File menu)
    void open() { isOpen = true; firstFrame_ = true; }

    void draw(const std::vector<std::string>& recentProjects,
              const std::function<bool(const std::string&)>& onOpenProject,
              const std::function<bool(const std::string&, const std::string&)>& onCreateProject,
              const std::function<void(const std::string&)>& logFn);

private:
    bool tryOpenSelectedProject(const std::function<bool(const std::string&)>& onOpenProject,
                                const std::function<void(const std::string&)>& logFn);

    char openPath_[512]  = {0};
    char createDir_[512] = {0};
    char createName_[128] = "MyGame";
    bool firstFrame_ = true;
};
