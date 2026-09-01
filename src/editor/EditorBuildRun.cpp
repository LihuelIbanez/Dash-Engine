// ═════════════════════════════════════════════════════════════════════════════
// EditorApp — Build & Run and Export Game Bundle.
//
// Split out of EditorApp.cpp to keep that file navigable.
// ═════════════════════════════════════════════════════════════════════════════
#include "EditorApp.h"
#include "AppPaths.h"
#include "project/GameBuildPipeline.h"
#include "project/ProcessRunner.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <vector>

#ifdef _WIN32
#  define NOMINMAX
#  include <windows.h>
#else
#  include <spawn.h>
#  include <sys/wait.h>
#  include <signal.h>
extern char** environ;
#endif

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

bool spawnTrackedProcess(const fs::path& executable,
                        const std::vector<std::string>& args,
                        intptr_t& outPid,
                        std::string& error)
{
#ifdef _WIN32
    std::string cmdLine = "\"" + executable.string() + "\"";
    for (const auto& arg : args) cmdLine += " \"" + arg + "\"";
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
                        0, nullptr, nullptr, &si, &pi)) {
        error = "CreateProcess failed (" + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(pi.hThread);
    outPid = reinterpret_cast<intptr_t>(pi.hProcess);
    return true;
#else
    std::vector<char*> argv;
    argv.reserve(args.size() + 2);
    argv.push_back(const_cast<char*>(executable.c_str()));
    for (const auto& arg : args)
        argv.push_back(const_cast<char*>(arg.c_str()));
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int rc = posix_spawn(&pid, executable.c_str(), nullptr, nullptr, argv.data(), environ);
    if (rc != 0) { error = std::strerror(rc); return false; }
    outPid = static_cast<intptr_t>(pid);
    return true;
#endif
}

} // namespace

void EditorApp::buildAndRun()
{
    addLog("[VSTEP] Build & Run begin");
    addLog("--- Building Vulkan runtime ---");
    showBuildLog_ = true;

#ifdef _WIN32
    // Locate cmake: check PATH first, then common VS install locations.
    std::string cmakeExe;
    if (std::system("where cmake >nul 2>&1") == 0) {
        cmakeExe = "cmake";
    } else {
        const char* vsCandidates[] = {
            "C:\\Program Files\\Microsoft Visual Studio\\18\\Insiders\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Professional\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe",
            "C:\\Program Files\\Microsoft Visual Studio\\2022\\Enterprise\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\CMake\\bin\\cmake.exe",
        };
        for (const char* p : vsCandidates) {
            if (fs::exists(p)) { cmakeExe = p; break; }
        }
    }
    if (cmakeExe.empty()) {
        addLog("[VFAIL] cmake not found. Make sure Visual Studio or CMake is installed.");
        addLog("        To build VulkanBootstrap manually: dash build -Vulkan");
        return;
    }
    const std::vector<std::string> buildArgv = {
        cmakeExe, "--build", BUILD_DIR, "--target", "VulkanBootstrap", "--config", "Release"
    };
#else
    const std::vector<std::string> buildArgv = {
        "cmake", "--build", BUILD_DIR, "--target", "VulkanBootstrap", "--parallel"
    };
#endif

    const int ret = runProcessCapture(buildArgv, [this](const std::string& line) {
        addLog(line);
    });
    if (ret < 0) {
        addLog("ERROR: Could not start build.");
        return;
    }

    if (ret == 0) {
        addLog("[VOK] Build OK. Launching Vulkan runtime...");

        // Export scene + tilemap to temp file so Vulkan has full terrain data.
        std::string tempScene = std::string(BUILD_DIR) + "/_play_scene.json";
        std::string prevPath = scene_.filePath;
        bool prevMod = scene_.modified;
        if (scene_.saveToFile(tempScene)) {
            std::ifstream in(tempScene);
            json j;
            if (in >> j) {
                in.close();
                std::vector<int> tilemap;
                tilemap.reserve(WORLD_W * WORLD_H);
                for (int y = 0; y < WORLD_H; ++y) {
                    for (int x = 0; x < WORLD_W; ++x) {
                        tilemap.push_back(static_cast<int>(world_.grid[y][x].type));
                    }
                }
                j["tilemap"] = tilemap;
                j["worldWidth"] = WORLD_W;
                j["worldHeight"] = WORLD_H;
                std::ofstream out(tempScene);
                out << j.dump(2);
                out.close();
            }

            scene_.filePath = prevPath;
            scene_.modified = prevMod;
            addLog("[VOK] Scene exported to " + tempScene);
            addLog("[VSTEP] Tilemap exported (" + std::to_string(WORLD_W * WORLD_H) + " tiles)");
        } else {
            addLog("[VFAIL] Could not export scene, launching with defaults.");
            tempScene.clear();
        }

        const fs::path buildDir = fs::path(BUILD_DIR);
        std::array<fs::path, 6> candidates = {
            buildDir / "Release" / "VulkanBootstrap.exe",
            buildDir / "Release" / "VulkanBootstrap",
            buildDir / "VulkanBootstrap.exe",
            buildDir / "VulkanBootstrap",
            buildDir / "src" / "tools" / "Release" / "VulkanBootstrap.exe",
            buildDir / "Debug" / "VulkanBootstrap.exe",
        };

        fs::path executablePath;
        std::error_code ec;
        for (const auto& c : candidates) {
            if (fs::exists(c, ec) && fs::is_regular_file(c, ec)) {
                executablePath = c;
                break;
            }
            ec.clear();
        }

        if (executablePath.empty()) {
            addLog("[VFAIL] VulkanBootstrap executable not found under: " + std::string(BUILD_DIR));
            return;
        }

        std::vector<std::string> runArgs;
        runArgs.emplace_back("--persistent");
        if (!tempScene.empty()) {
            runArgs.emplace_back("--scene");
            runArgs.push_back(tempScene);
        }
        // Transport channel: the runtime polls this file for pause/step/timeScale.
        playback_.reset();
        syncPlaybackStateFile(true);
        runArgs.emplace_back("--state");
        runArgs.push_back(playbackStatePath());

        {
            std::string argsLog = "[VSTEP] Build&Run launch args:";
            for (const auto& a : runArgs) argsLog += " " + a;
            addLog(argsLog);
        }

        std::string launchError;
        intptr_t pid = -1;
        if (!spawnTrackedProcess(executablePath, runArgs, pid, launchError)) {
            addLog("[VFAIL] Could not launch Vulkan runtime: " + launchError);
            return;
        }

        // Bring the game window to front on macOS
#ifdef __APPLE__
        {
            std::vector<std::string> osaArgs = {
                "-e", "delay 0.3",
                "-e", "tell application \"System Events\"",
                "-e", "  set frontmost of (first process whose name is \"VulkanBootstrap\") to true",
                "-e", "end tell"
            };
            intptr_t osaPid = -1;
            std::string osaError;
            spawnTrackedProcess("/usr/bin/osascript", osaArgs, osaPid, osaError);
        }
#endif
        addLog("Game launched: " + executablePath.string());

        // The launched runtime is a play session: without this the transport
        // controls stay hidden while the process that consumes them is alive.
        editorMode_ = EditorMode::Play;
    } else {
        addLog("[VFAIL] Build FAILED (exit " + std::to_string(ret) + ").");
    }
}

void EditorApp::exportGameBundle()
{
    showBuildLog_ = true;
    addLog("--- Export Game Bundle ---");

    if (!projectManager_.hasActiveProject()) {
        addLog("[ERROR] No active project. Open a .dashproject before exporting.");
        return;
    }

    // Preserve scene state and export current editor scene into project scenes dir.
    if (!scene_.filePath.empty()) {
        const std::string prevPath = scene_.filePath;
        const bool prevModified = scene_.modified;
        if (!scene_.saveToFile(scene_.filePath)) {
            addLog("[ERROR] Failed to save scene before export: " + scene_.filePath);
        }
        scene_.filePath = prevPath;
        scene_.modified = prevModified;
    }

    const auto& manifest = projectManager_.manifest();
    std::string outDir = manifest.absoluteBuildDir();
    if (outDir.empty()) outDir = AppPaths::getBuildOutputDir();

    auto result = GameBuildPipeline::build(manifest, outDir, BUILD_DIR);
    for (const auto& line : result.log)
        addLog(line);

    if (result.success)
        addLog("Export OK: " + result.outputPath);
    else
        addLog("Export FAILED.");
}
