#include "project/ProjectManager.h"

#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

int main()
{
    std::printf("=== test_project_manager ===\n");

    fs::path tmpRoot = fs::temp_directory_path() / "dash_test_project_manager";
    std::error_code ec;
    fs::remove_all(tmpRoot, ec);
    fs::create_directories(tmpRoot, ec);

    ProjectManager pm;
    ASSERT(pm.createProject(tmpRoot.string(), "ManagerTest"), "createProject succeeds");
    ASSERT(pm.hasActiveProject(), "project is active");

    fs::path manifestPath = tmpRoot / "ManagerTest.dashproject";
    ASSERT(fs::exists(manifestPath), ".dashproject exists");
    ASSERT(fs::exists(tmpRoot / "assets"), "assets dir exists");
    ASSERT(fs::exists(tmpRoot / "scenes"), "scenes dir exists");
    ASSERT(fs::exists(tmpRoot / ".library"), "library dir exists");
    ASSERT(fs::exists(tmpRoot / "assets" / "sprites"), "sprites dir exists");
    ASSERT(fs::exists(tmpRoot / "assets" / "fonts"), "fonts dir exists");
    ASSERT(fs::exists(tmpRoot / "assets" / "data"), "data dir exists");

    pm.closeProject();
    ASSERT(!pm.hasActiveProject(), "project closed");

    ASSERT(pm.openProject(manifestPath.string()), "openProject succeeds");
    ASSERT(pm.hasActiveProject(), "project active after open");
    ASSERT(pm.manifest().name == "ManagerTest", "manifest name loaded");

    pm.closeProject();
    ASSERT(pm.openProject(tmpRoot.string()), "openProject accepts a project directory");
    ASSERT(pm.hasActiveProject(), "project active after opening directory");
    ASSERT(pm.manifest().name == "ManagerTest", "manifest name loaded from directory");

    ASSERT(!pm.recentProjects().empty(), "recents has entries");
    ASSERT(pm.recentProjects().front().find("ManagerTest.dashproject") != std::string::npos,
           "recent contains manifest path");

    fs::remove_all(tmpRoot, ec);

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
