// ═════════════════════════════════════════════════════════════════════════════
// EditorApp — project/scene lifecycle: open/create project, new/open/save
// scene, scene file listing and the world <-> scene sync helpers.
//
// Split out of EditorApp.cpp to keep that file navigable.
// ═════════════════════════════════════════════════════════════════════════════
#include "EditorApp.h"
#include "AppPaths.h"
#include "IconsFontAwesome6.h"
#include "db/DbMode.h"
#include "scene/SceneRepositorySqlite.h"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

bool sqliteModeEnabled()
{
    return DbMode::usesSqliteRead(DbMode::current());
}

fs::path projectSqlitePath(const ProjectManifest& manifest)
{
    return fs::path(manifest.absoluteLibraryDir()) / "dash_engine.db";
}

} // namespace

// ═════════════════════════════════════════════════════════════════════════════
// Project management
// ═════════════════════════════════════════════════════════════════════════════

void EditorApp::refreshProjectPaths()
{
    if (projectManager_.hasActiveProject()) {
        const auto& m = projectManager_.manifest();
        assetsRoot_  = m.absoluteAssetsDir();
        libraryRoot_ = m.absoluteLibraryDir();
        scenesDir_   = m.absoluteScenesDir();
    } else {
        // Legacy mode: fall back to AppPaths-relative directories.
        assetsRoot_  = AppPaths::getAssetsDir();
        libraryRoot_ = AppPaths::getLibraryDir();
        scenesDir_   = AppPaths::getScenesDir();
    }
    fs::create_directories(scenesDir_);
}

void EditorApp::reinitAssetPipeline()
{
    // Save current DB before switching context.
    if (!assetDbPath_.empty())
        assetDb_.save(assetDbPath_);

    assetDbPath_ = assetsRoot_ + "/asset_db.json";
    assetDb_     = AssetDatabase{};
    if (fs::exists(assetDbPath_)) {
        if (assetDb_.load(assetDbPath_))
            addLog("Asset DB loaded (" + std::to_string(assetDb_.records().size()) + " records).");
        else
            addLog("[WARN] Failed to reload asset DB.");
    }

    // Re-import assets for the new root.
    std::vector<std::string> errors;
    int count = importManager_.importAll(assetsRoot_, libraryRoot_, assetDb_, errors);
    if (count > 0) {
        addLog("Imported " + std::to_string(count) + " asset(s).");
        assetDb_.save(assetDbPath_);
    }
    for (auto& e : errors) addLog("[IMPORT] " + e);

    // Reset file watcher to new assets directory.
    fileWatcher_ = FileWatcher(assetsRoot_, 1.0f);
    fileWatcher_.reset();

    // Refresh pointers held by the sprite editor.
    spriteEditor_.assetsRoot  = &assetsRoot_;
    spriteEditor_.libraryRoot = &libraryRoot_;
}

bool EditorApp::openProject(const std::string& manifestPath)
{
    addLog("[PROJ] openProject called with: " + manifestPath);
    if (manifestPath.empty()) {
        addLog("[PROJ] manifestPath is empty, skipping");
        return false;
    }
    
    std::fprintf(stdout, "[EditorApp::openProject] Starting migration and project load...\n");
    
    if (!projectManager_.openProject(manifestPath)) {
        addLog("[ERROR] Failed to open project: " + manifestPath);
        return false;
    }
    addLog("[PROJ] Project opened: " + projectManager_.manifest().name);
    addLog("[PROJ] Project root: " + projectManager_.manifest().projectRoot);
    addLog("[PROJ] Default scene: " + projectManager_.manifest().defaultScene);
    addLog("[PROJ] Absolute default scene: " + projectManager_.manifest().absoluteDefaultScene());
    
    // Log migration status
    const auto& migration = projectManager_.lastMigrationStatus();
    if (migration.attempted) {
        if (migration.success) {
            addLog("[PROJ:MIGRATION] SQLite migration succeeded: " + migration.dbPath);
        } else {
            addLog("[PROJ:MIGRATION] SQLite migration attempted but failed");
        }
    }
    
    refreshProjectPaths();

    const auto& migrationAgain = projectManager_.lastMigrationStatus();
    if (migrationAgain.attempted) {
        if (migrationAgain.success) {
            addLog("[MIGRATION] SQLite migration completed: " + migrationAgain.dbPath);
        } else {
            addLog("[MIGRATION] SQLite migration failed - using JSON fallback.");
        }
        for (const auto& line : migrationAgain.log)
            addLog("[MIGRATION] " + line);
    }

    const auto& sceneSync = projectManager_.lastSceneSyncStatus();
    if (sceneSync.attempted) {
        for (const auto& line : sceneSync.log)
            addLog(line);
        if (!sceneSync.success)
            addLog("[SceneSync] Failed - SQLite scene rows may be stale.");
    }

    refreshSceneFiles();
    addLog("[PROJ] Available scenes after refresh: " + std::to_string(sceneFiles_.size()));
    for (const auto& f : sceneFiles_) {
        addLog("[PROJ]   Scene: " + f);
    }
    
    loadInitialProjectScene();
    reinitAssetPipeline();
    projectManager_.saveRecents();
    return true;
}

bool EditorApp::createProject(const std::string& dirPath, const std::string& name)
{
    if (!projectManager_.createProject(dirPath, name)) {
        addLog("[ERROR] Failed to create project at: " + dirPath);
        return false;
    }
    addLog("Created project: " + name + "  (" + dirPath + ")");
    refreshProjectPaths();
    refreshSceneFiles();
    loadInitialProjectScene();
    reinitAssetPipeline();
    projectManager_.saveRecents();
    return true;
}

// ═════════════════════════════════════════════════════════════════════════════
// File Dialogs
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawOpenDialog()
{
    ImGui::OpenPopup("Open Scene");
    if (ImGui::BeginPopupModal("Open Scene", &showOpenDialog_,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Scenes in project:");
        ImGui::Separator();

        for (auto& f : sceneFiles_) {
            if (ImGui::Selectable(f.c_str())) {
                openScene(scenesDir_ + "/" + f);
                showOpenDialog_ = false;
            }
        }
        if (sceneFiles_.empty())
            ImGui::TextDisabled("No .json files in scenes/");

        ImGui::Separator();
        if (ImGui::Button("Cancel", {120, 0})) {
            showOpenDialog_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::drawSaveDialog()
{
    ImGui::OpenPopup("Save Scene");
    if (ImGui::BeginPopupModal("Save Scene", &showSaveDialog_,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        char sceneName[128];
        std::strncpy(sceneName, scene_.sceneName.c_str(), sizeof(sceneName));
        sceneName[sizeof(sceneName) - 1] = '\0';
        if (ImGui::InputText("Scene Name", sceneName, sizeof(sceneName)))
            scene_.sceneName = sceneName;

        ImGui::InputText("File Name", saveFileName_, sizeof(saveFileName_));

        ImGui::Separator();
        if (ImGui::Button("Save", {120, 0})) {
            saveScene(scenesDir_ + "/" + saveFileName_);
            showSaveDialog_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", {120, 0})) {
            showSaveDialog_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorApp::drawCreateSceneDialog()
{
    ImGui::OpenPopup("Create Scene");
    if (ImGui::BeginPopupModal("Create Scene", &showCreateSceneDialog_,
                               ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Create a new scene file in scenes/");
        ImGui::Separator();

        ImGui::InputText("File Name", createSceneFileName_, sizeof(createSceneFileName_));

        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.17f, 0.55f, 0.24f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.24f, 0.68f, 0.31f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.14f, 0.45f, 0.21f, 1.f));
        if (ImGui::Button(ICON_FA_FILE " Create", {120, 0})) {
            std::string fileName(createSceneFileName_);
            if (fileName.empty()) {
                addLog("ERROR: Scene file name cannot be empty.");
            } else {
                if (fs::path(fileName).extension() != ".json")
                    fileName += ".json";

                const fs::path scenePath = fs::path(scenesDir_) / fileName;
                const bool sqliteScenes = projectManager_.hasActiveProject() && sqliteModeEnabled();
                const bool existsInSelection = std::find(sceneFiles_.begin(), sceneFiles_.end(), fileName) != sceneFiles_.end();
                if ((sqliteScenes && existsInSelection) || (!sqliteScenes && fs::exists(scenePath))) {
                    addLog("ERROR: Scene already exists: " + fileName);
                } else {
                    newScene();
                    scene_.sceneName = fs::path(fileName).stem().string();
                    saveScene(scenePath.string());
                    openScene(fileName);
                    showCreateSceneDialog_ = false;
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.62f, 0.20f, 0.20f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.76f, 0.25f, 0.25f, 1.f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.52f, 0.16f, 0.16f, 1.f));
        if (ImGui::Button("Cancel", {120, 0})) {
            showCreateSceneDialog_ = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor(3);

        ImGui::EndPopup();
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Actions
// ═════════════════════════════════════════════════════════════════════════════

// Center camera on the Player entity, or the centroid of all entities,
// falling back to the world center if no entities exist.
void EditorApp::focusCameraOnEntities()
{
    // Try Player first
    for (const auto& e : scene_.entities) {
        if (e.type == EntityData::Type::Player) {
            camX_ = e.x;
            camY_ = e.y;
            return;
        }
    }
    // Centroid of all entities
    if (!scene_.entities.empty()) {
        float sx = 0.f, sy = 0.f;
        for (const auto& e : scene_.entities) { sx += e.x; sy += e.y; }
        camX_ = sx / static_cast<float>(scene_.entities.size());
        camY_ = sy / static_cast<float>(scene_.entities.size());
        return;
    }
    // Fallback: world center
    camX_ = WORLD_W / 2.f;
    camY_ = WORLD_H / 2.f;
}

void EditorApp::regenerateWorld()
{
    world_.generate(scene_.worldSeed, biomeTable_.empty() ? nullptr : &biomeTable_);
    applySceneToWorld();
    // The viewport draws an uploaded copy of the mesh, so it has to be refreshed
    // or the panel keeps showing the previous world.
    vkCtx_.updateTerrainMesh(world_.terrain());
}

void EditorApp::newScene()
{
    scene_.createDefault();
    syncUIRender3DSettingsFromScene();
    regenerateWorld();
    clearSelection();
    commandStack_.clear();
    camX_ = WORLD_W / 2.f;
    camY_ = WORLD_H / 2.f;
    addLog("New scene created.");
}

void EditorApp::refreshSceneFiles()
{
    const std::string previousSelection = selectedSceneFile_;
    sceneFiles_.clear();

    if (projectManager_.hasActiveProject() && sqliteModeEnabled()) {
        const fs::path dbPath = projectSqlitePath(projectManager_.manifest());
        if (fs::exists(dbPath)) {
            SceneRepositorySqlite repo(dbPath.string());
            std::string error;
            if (repo.listSceneFiles(sceneFiles_, &error)) {
                std::sort(sceneFiles_.begin(), sceneFiles_.end());
            } else {
                addLog("[SCENE] SQLite scene listing failed, fallback to files: " + error);
                sceneFiles_.clear();
            }
        }
    }

    if (sceneFiles_.empty()) {
        if (!fs::exists(scenesDir_)) return;
        for (auto& entry : fs::directory_iterator(scenesDir_)) {
            if (entry.path().extension() == ".json")
                sceneFiles_.push_back(entry.path().filename().string());
        }
        std::sort(sceneFiles_.begin(), sceneFiles_.end());
    }

    if (sceneFiles_.empty()) {
        selectedSceneFile_.clear();
        return;
    }

    if (std::find(sceneFiles_.begin(), sceneFiles_.end(), previousSelection) != sceneFiles_.end()) {
        selectedSceneFile_ = previousSelection;
    } else {
        selectedSceneFile_ = sceneFiles_.front();
    }
}

void EditorApp::loadInitialProjectScene()
{
    addLog("[SCENE:LOAD] loadInitialProjectScene begin");
    if (!projectManager_.hasActiveProject()) {
        addLog("[SCENE:LOAD] NO ACTIVE PROJECT - creating new scene");
        return;
    }

    const auto& manifest = projectManager_.manifest();
    const fs::path defaultScenePath = manifest.absoluteDefaultScene();
    const std::string defaultSceneFile = defaultScenePath.filename().string();

    addLog("[SCENE:LOAD] Default scene from manifest: " + defaultSceneFile);
    addLog("[SCENE:LOAD] Absolute path: " + defaultScenePath.string());

    // Always prefer the project's declared default scene when it exists on disk.
    // In SQLite mode, scene listings may not contain JSON-backed scenes yet.
    if (fs::exists(defaultScenePath)) {
        addLog("[SCENE:LOAD] Default scene path EXISTS on disk");
        selectedSceneFile_ = defaultSceneFile;
        openScene(defaultScenePath.string());
        if (scene_.filePath.empty()) {
            addLog("[SCENE:LOAD] ERROR: Could not open default scene path: " + defaultScenePath.string());
        } else {
            addLog("[SCENE:LOAD] SUCCESS: Loaded default scene from disk: " + defaultScenePath.string());
            addLog("[SCENE:LOAD] Scene has " + std::to_string(scene_.entities.size()) + " entities");
            refreshSceneFiles();
            return;
        }
    } else {
        addLog("[SCENE:LOAD] Default scene path DOES NOT EXIST: " + defaultScenePath.string());
    }

    refreshSceneFiles();
    addLog("[SCENE:LOAD] Available scenes: " + std::to_string(sceneFiles_.size()));
    for (const auto& f : sceneFiles_) {
        addLog("[SCENE:LOAD]   - " + f);
    }
    
    if (!sceneFiles_.empty()) {
        if (std::find(sceneFiles_.begin(), sceneFiles_.end(), defaultSceneFile) != sceneFiles_.end()) {
            selectedSceneFile_ = defaultSceneFile;
            openScene(defaultSceneFile);
            addLog("[SCENE:LOAD] Auto-loaded default scene from list: " + defaultSceneFile);
            return;
        }

        selectedSceneFile_ = sceneFiles_.front();
        openScene(selectedSceneFile_);
        addLog("[SCENE:LOAD] Auto-loaded first available scene: " + selectedSceneFile_);
        addLog("[SCENE:LOAD] Scene has " + std::to_string(scene_.entities.size()) + " entities");
        return;
    }

    newScene();
    addLog("[SCENE:LOAD] No project scenes found; started a new scene.");
}


void EditorApp::saveScene(const std::string& path)
{
    // Validate before save
    bool hasPlayer = false;
    for (auto& e : scene_.entities)
        if (e.type == EntityData::Type::Player) { hasPlayer = true; break; }
    if (!hasPlayer) {
        addLog("WARNING: Scene has no Player entity. Saving anyway.");
    }

    // Capture current terrain state into scene overrides
    {
        TerrainMesh& tm = world_.terrain();

        // Cliff overrides: vertices with non-zero cliff level
        scene_.cliffOverrides.clear();
        for (int vy = 0; vy < TerrainMesh::VH; ++vy)
            for (int vx = 0; vx < TerrainMesh::VW; ++vx) {
                uint8_t cl = tm.vert(vx, vy).cliffLevel;
                if (cl != 0)
                    scene_.cliffOverrides.push_back({vx, vy, cl});
            }

        // Texture overrides: vertices with non-default texture blend
        scene_.textureOverrides.clear();
        for (int vy = 0; vy < TerrainMesh::VH; ++vy)
            for (int vx = 0; vx < TerrainMesh::VW; ++vx) {
                const auto& v = tm.vert(vx, vy);
                if (v.texWeights[1] != 0 || v.texWeights[2] != 0 || v.texWeights[3] != 0
                    || v.texIndices[0] != 0) {
                    TextureOverride to;
                    to.vx = vx; to.vy = vy;
                    std::memcpy(to.texIndices, v.texIndices, 4);
                    std::memcpy(to.texWeights, v.texWeights, 4);
                    scene_.textureOverrides.push_back(to);
                }
            }

        // Water bodies
        scene_.waterBodies = tm.waterBodies();
    }

    const std::string fileName = fs::path(path).filename().string();
    const bool sqliteScenes = projectManager_.hasActiveProject() && sqliteModeEnabled();

    // Disk first: the .json is the versioned artifact the runtime loads, SQLite
    // is only a cache. Writing it before the row keeps `updated_at` >= mtime, so
    // the next project open does not mistake this save for an external edit.
    if (!scene_.saveToFile(path)) {
        addLog("ERROR: Could not write scene file: " + path);
        return;
    }
    addLog("Saved: " + path + " (v" + std::to_string(SceneData::kCurrentVersion) + ")");

    if (sqliteScenes) {
        const fs::path dbPath = projectSqlitePath(projectManager_.manifest());
        SceneRepositorySqlite repo(dbPath.string());
        std::string error;
        if (repo.saveScene(fileName, scene_, &error))
            addLog("Cached (SQLite): " + fileName);
        else
            addLog("[SCENE] SQLite cache update failed: " + error);
    }

    refreshSceneFiles();
    selectedSceneFile_ = fileName;
}

void EditorApp::openScene(const std::string& path)
{
    const std::string fileName = fs::path(path).filename().string();
    const bool sqliteScenes = projectManager_.hasActiveProject() && sqliteModeEnabled();

    if (sqliteScenes) {
        const fs::path dbPath = projectSqlitePath(projectManager_.manifest());
        if (fs::exists(dbPath)) {
            SceneRepositorySqlite repo(dbPath.string());
            std::string error;
            if (repo.loadScene(fileName, scene_, assetsRoot_, &error)) {
                scene_.filePath = (fs::path(scenesDir_) / fileName).string();
                syncUIRender3DSettingsFromScene();

                for (auto& err : scene_.loadErrors)
                    addLog("  [load] " + err);

                world_.generate(scene_.worldSeed, biomeTable_.empty() ? nullptr : &biomeTable_);
                applySceneToWorld();
                clearSelection();
                commandStack_.clear();
                focusCameraOnEntities();
                selectedSceneFile_ = fileName;
                addLog("Loaded (SQLite): " + fileName + " (v" + std::to_string(scene_.sceneVersion) + ")");
                return;
            }
            addLog("[SCENE] SQLite load failed, fallback to JSON: " + error);
        }
    }

    if (scene_.loadFromFile(path, assetsRoot_)) {
        syncUIRender3DSettingsFromScene();
        // Report any warnings collected during load
        for (auto& err : scene_.loadErrors)
            addLog("  [load] " + err);

        world_.generate(scene_.worldSeed, biomeTable_.empty() ? nullptr : &biomeTable_);
        applySceneToWorld();
        clearSelection();
        commandStack_.clear();
        focusCameraOnEntities();
        selectedSceneFile_ = fileName;
        addLog("Loaded: " + path + " (v" + std::to_string(scene_.sceneVersion) + ")");

        if (sqliteScenes) {
            const fs::path dbPath = projectSqlitePath(projectManager_.manifest());
            SceneRepositorySqlite repo(dbPath.string());
            std::string sqliteError;
            if (repo.saveScene(fileName, scene_, &sqliteError)) {
                addLog("[SCENE] Synced JSON scene into SQLite: " + fileName);
                refreshSceneFiles();
            } else {
                addLog("[SCENE] SQLite sync after JSON load failed: " + sqliteError);
            }
        }
    } else {
        addLog("ERROR: Could not load scene: " + path);
        for (auto& err : scene_.loadErrors)
            addLog("  [load] " + err);
    }
}

void EditorApp::applySceneToWorld()
{
    for (auto& ovr : scene_.tileOverrides) {
        if (ovr.x >= 0 && ovr.x < WORLD_W && ovr.y >= 0 && ovr.y < WORLD_H) {
            // Apply to terrain mesh face
            TerrainFace& f = world_.terrain().face(ovr.x, ovr.y);
            f.type     = static_cast<TileType>(ovr.tileType);
            f.walkable = ovr.walkable;

            // Keep legacy grid in sync
            world_.grid[ovr.y][ovr.x].type     = static_cast<TileType>(ovr.tileType);
            world_.grid[ovr.y][ovr.x].walkable  = ovr.walkable;
        }
    }

    // Apply vertex height overrides
    for (auto& vh : scene_.vertexHeightOverrides) {
        if (vh.vx >= 0 && vh.vx < TerrainMesh::VW &&
            vh.vy >= 0 && vh.vy < TerrainMesh::VH) {
            world_.terrain().vert(vh.vx, vh.vy).height = vh.height;
        }
    }

    // Apply cliff overrides (v5+)
    for (auto& co : scene_.cliffOverrides) {
        if (co.vx >= 0 && co.vx < TerrainMesh::VW &&
            co.vy >= 0 && co.vy < TerrainMesh::VH) {
            world_.terrain().setCliffLevel(co.vx, co.vy, co.cliffLevel);
        }
    }

    // Apply texture overrides (v5+)
    for (auto& to : scene_.textureOverrides) {
        if (to.vx >= 0 && to.vx < TerrainMesh::VW &&
            to.vy >= 0 && to.vy < TerrainMesh::VH) {
            auto& v = world_.terrain().vert(to.vx, to.vy);
            std::memcpy(v.texIndices, to.texIndices, 4);
            std::memcpy(v.texWeights, to.texWeights, 4);
        }
    }

    // Apply water bodies (v5+)
    auto& wbs = world_.terrain().waterBodies();
    wbs.clear();
    for (auto& wb : scene_.waterBodies) {
        world_.terrain().addWaterBody(wb);
    }

    world_.terrain().markDirty();
}
