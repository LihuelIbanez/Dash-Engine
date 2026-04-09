#include "GameBuildPipeline.h"

#include <nlohmann/json.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

bool runCommandCapture(const std::string& cmd, std::vector<std::string>& out)
{
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        out.push_back("[ERROR] Could not start command: " + cmd);
        return false;
    }

    std::array<char, 512> buf{};
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
        std::string line(buf.data());
        if (!line.empty() && line.back() == '\n') line.pop_back();
        if (!line.empty()) out.push_back(line);
    }

    int rc = pclose(pipe);
    return rc == 0;
}

void copyTreeIfExists(const fs::path& src, const fs::path& dst, std::vector<std::string>& log)
{
    std::error_code ec;
    if (!fs::exists(src, ec)) {
        log.push_back("[WARN] Missing optional directory: " + src.string());
        return;
    }

    fs::create_directories(dst, ec);
    if (ec) {
        log.push_back("[ERROR] Could not create directory: " + dst.string());
        return;
    }

    fs::copy(src, dst,
             fs::copy_options::recursive | fs::copy_options::overwrite_existing,
             ec);
    if (ec)
        log.push_back("[ERROR] Copy failed: " + src.string() + " -> " + dst.string() + " (" + ec.message() + ")");
    else
        log.push_back("[OK] Copied: " + src.string() + " -> " + dst.string());
}

} // namespace

GameBuildPipeline::BuildResult GameBuildPipeline::build(const ProjectManifest& manifest,
                                                        const std::string& outputDir,
                                                        const std::string& buildDir)
{
    BuildResult res;
    res.log.push_back("[BuildPipeline] Start export for project: " + manifest.name);

    fs::path outRoot = fs::path(outputDir);
    if (outRoot.empty()) {
        outRoot = fs::path(manifest.absoluteBuildDir());
    }

    const std::string safeName = manifest.name.empty() ? std::string("Game") : manifest.name;
    fs::path bundleRoot = outRoot / (safeName + "_bundle");

    std::error_code ec;
    fs::remove_all(bundleRoot, ec);
    ec.clear();
    fs::create_directories(bundleRoot, ec);
    if (ec) {
        res.log.push_back("[ERROR] Could not create output directory: " + bundleRoot.string());
        return res;
    }

    // 1) Build runtime executable
    res.log.push_back("[BuildPipeline] Building target IsometricRPG...");
    std::string cmd = "cd \"" + buildDir + "\" && cmake --build . --target IsometricRPG --parallel 2>&1";
    if (!runCommandCapture(cmd, res.log)) {
        res.log.push_back("[ERROR] Build failed.");
        return res;
    }

    fs::path exePath = fs::path(buildDir) / "IsometricRPG";
    if (!fs::exists(exePath, ec)) {
        res.log.push_back("[ERROR] Built executable not found at: " + exePath.string());
        return res;
    }

#if defined(__APPLE__)
    // 2) Create .app bundle
    fs::path appRoot = bundleRoot / (safeName + std::string(".app"));
    fs::path appMacOS = appRoot / "Contents" / "MacOS";
    fs::path appRes = appRoot / "Contents" / "Resources";
    fs::create_directories(appMacOS, ec);
    fs::create_directories(appRes, ec);
    if (ec) {
        res.log.push_back("[ERROR] Could not create .app structure.");
        return res;
    }

    fs::path appExe = appMacOS / safeName;
    fs::copy_file(exePath, appExe, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        res.log.push_back("[ERROR] Failed to copy executable into .app: " + ec.message());
        return res;
    }

    // Ensure executable permissions.
    fs::permissions(appExe,
                    fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add,
                    ec);

    // Minimal plist.
    {
        std::ofstream plist((appRoot / "Contents" / "Info.plist").string());
        plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
              << "<plist version=\"1.0\">\n"
              << "<dict>\n"
              << "  <key>CFBundleExecutable</key><string>" << safeName << "</string>\n"
              << "  <key>CFBundleIdentifier</key><string>com.dashengine." << safeName << "</string>\n"
              << "  <key>CFBundleName</key><string>" << safeName << "</string>\n"
              << "  <key>CFBundlePackageType</key><string>APPL</string>\n"
              << "  <key>CFBundleVersion</key><string>1</string>\n"
              << "  <key>CFBundleShortVersionString</key><string>1.0</string>\n"
              << "</dict>\n"
              << "</plist>\n";
    }

    copyTreeIfExists(manifest.absoluteAssetsDir(), appRes / "assets", res.log);
    copyTreeIfExists(manifest.absoluteScenesDir(), appRes / "scenes", res.log);

    json pj;
    pj["name"] = manifest.name;
    pj["defaultScene"] = manifest.defaultScene;
    pj["gameConfig"]["screenWidth"] = manifest.gameConfig.screenWidth;
    pj["gameConfig"]["screenHeight"] = manifest.gameConfig.screenHeight;
    pj["gameConfig"]["targetFps"] = manifest.gameConfig.targetFps;

    std::ofstream pof((appRes / "project.json").string());
    pof << pj.dump(2);

    res.outputPath = appRoot.string();
#else
    fs::path binDir = bundleRoot / "bin";
    fs::path assetsDir = bundleRoot / "assets";
    fs::path scenesDir = bundleRoot / "scenes";
    fs::create_directories(binDir, ec);
    fs::create_directories(assetsDir, ec);
    fs::create_directories(scenesDir, ec);

    fs::path outExe = binDir / "IsometricRPG";
    fs::copy_file(exePath, outExe, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        res.log.push_back("[ERROR] Could not copy executable: " + ec.message());
        return res;
    }

    copyTreeIfExists(manifest.absoluteAssetsDir(), assetsDir, res.log);
    copyTreeIfExists(manifest.absoluteScenesDir(), scenesDir, res.log);

    json pj;
    pj["name"] = manifest.name;
    pj["defaultScene"] = manifest.defaultScene;
    pj["gameConfig"]["screenWidth"] = manifest.gameConfig.screenWidth;
    pj["gameConfig"]["screenHeight"] = manifest.gameConfig.screenHeight;
    pj["gameConfig"]["targetFps"] = manifest.gameConfig.targetFps;

    std::ofstream pof((bundleRoot / "project.json").string());
    pof << pj.dump(2);

    res.outputPath = bundleRoot.string();
#endif

    res.success = true;
    res.log.push_back("[BuildPipeline] Export completed: " + res.outputPath);
    return res;
}
