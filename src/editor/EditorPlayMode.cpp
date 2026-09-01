// ═════════════════════════════════════════════════════════════════════════════
// EditorApp — Play mode transport (pause/step/time scale), audit log and the
// editor↔runtime playback state file.
//
// Split out of EditorApp.cpp to keep that file navigable.
// ═════════════════════════════════════════════════════════════════════════════
#include "EditorApp.h"
#include "GameEvents.h"
#include "IconsFontAwesome6.h"
#include "rendering/vulkan/SceneLoader.h"
#include "imgui.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

std::string nowIso8601Local()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tmLocal{};
#if defined(_WIN32)
    localtime_s(&tmLocal, &t);
#else
    localtime_r(&t, &tmLocal);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tmLocal, "%Y-%m-%dT%H:%M:%S");
    return oss.str();
}

constexpr float       kPlaybackSpeeds[]      = { 0.25f, 0.5f, 1.0f, 2.0f };
constexpr const char* kPlaybackSpeedLabels[] = { "0.25x", "0.5x", "1x", "2x" };
constexpr int         kPlaybackSpeedCount    = 4;

} // namespace

std::string EditorApp::playAuditFilePath() const
{
    fs::path root;
    if (projectManager_.hasActiveProject() && !projectManager_.manifest().projectRoot.empty()) {
        root = projectManager_.manifest().projectRoot;
    } else {
        root = fs::path(BUILD_DIR).parent_path();
    }

    return (root / "audit" / "play_sessions_audit.json").string();
}

void EditorApp::beginPlayAuditSession()
{
    playAuditActive_ = true;
    playAuditSessionStartedAt_ = nowIso8601Local();
    playAuditCurrentSessionLogs_.clear();
    playAuditCurrentSessionLogs_.push_back("[AUDIT] Play session started at " + playAuditSessionStartedAt_);
}

void EditorApp::flushPlayAuditSessionToFile(const std::string& reason)
{
    if (!playAuditActive_) return;

    const std::string endedAt = nowIso8601Local();
    if (!reason.empty()) {
        playAuditCurrentSessionLogs_.push_back("[AUDIT] Session end reason: " + reason);
    }

    const std::string auditPath = playAuditFilePath();
    fs::create_directories(fs::path(auditPath).parent_path());

    json root;
    root["sessions"] = json::array();

    std::ifstream in(auditPath);
    if (in.is_open()) {
        try {
            in >> root;
        } catch (...) {
            root = json{};
            root["sessions"] = json::array();
        }
    }

    if (!root.contains("sessions") || !root["sessions"].is_array()) {
        root["sessions"] = json::array();
    }

    json session;
    session["startedAt"] = playAuditSessionStartedAt_;
    session["endedAt"] = endedAt;
    session["reason"] = reason;
    session["logs"] = playAuditCurrentSessionLogs_;

    root["sessions"].push_back(session);
    while (root["sessions"].size() > 2) {
        root["sessions"].erase(root["sessions"].begin());
    }

    std::ofstream out(auditPath);
    out << root.dump(2);

    playAuditActive_ = false;
    playAuditSessionStartedAt_.clear();
    playAuditCurrentSessionLogs_.clear();
}

// ═════════════════════════════════════════════════════════════════════════════
// Play-mode transport – pause / step / time scale
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::drawPlaybackControls()
{
    const bool paused = playback_.paused();

    if (ImGui::Button(paused ? ICON_FA_PLAY "  Resume  " : ICON_FA_PAUSE "  Pause  ", {130, 34}))
        playback_.togglePause();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s the running game (F6)", paused ? "Resume" : "Pause");

    ImGui::SameLine();
    ImGui::BeginDisabled(!paused);
    if (ImGui::Button(ICON_FA_FORWARD_STEP "  Step  ", {110, 34}))
        playback_.requestStep();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Advance a single frame (F10)");

    ImGui::SameLine();
    int speedIndex = 2;
    for (int i = 0; i < kPlaybackSpeedCount; ++i) {
        if (std::fabs(playback_.timeScale() - kPlaybackSpeeds[i]) < 0.001f) {
            speedIndex = i;
            break;
        }
    }
    ImGui::SetNextItemWidth(90);
    if (ImGui::Combo("##playbackSpeed", &speedIndex, kPlaybackSpeedLabels, kPlaybackSpeedCount))
        playback_.setTimeScale(kPlaybackSpeeds[speedIndex]);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Simulation speed");
}

std::string EditorApp::playbackSpeedLabel() const
{
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%gx", static_cast<double>(playback_.timeScale()));
    return buf;
}

void EditorApp::handlePlaybackShortcut(SDL_Keycode key)
{
    switch (key) {
    case SDLK_F5:
        if (editorMode_ == EditorMode::Edit) enterPlayMode();
        else                                 exitPlayMode();
        break;
    case SDLK_F6:
        if (editorMode_ == EditorMode::Play) {
            playback_.togglePause();
            addLog(playback_.paused() ? "[Play] Paused." : "[Play] Resumed.");
        }
        break;
    case SDLK_F10:
        // Stepping only makes sense while paused, so pause first if needed.
        if (editorMode_ == EditorMode::Play) {
            playback_.setPaused(true);
            playback_.requestStep();
        }
        break;
    default:
        break;
    }
}

std::string EditorApp::playbackStatePath() const
{
    return std::string(BUILD_DIR) + "/generated/vulkan_viewport_state.json";
}

void EditorApp::syncPlaybackStateFile(bool force)
{
    const bool dirty = playback_.consumeDirty();
    if (!dirty && !force) return;

    const fs::path path = fs::path(playbackStatePath());
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    // Merge instead of overwrite: other blocks (camera, viewport…) may live here.
    json state = json::object();
    {
        std::ifstream in(path);
        if (in.is_open()) {
            try { in >> state; } catch (...) { state = json::object(); }
        }
    }
    if (!state.is_object()) state = json::object();

    state["playback"] = {
        {"paused",     playback_.paused()},
        {"timeScale",  playback_.timeScale()},
        {"stepSerial", playback_.stepSerial()},
    };

    // Write-then-rename so the polling runtime never reads a half-written file.
    const fs::path tmp = fs::path(path).concat(".tmp");
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out.is_open()) return;
        out << state.dump(2);
    }
    fs::rename(tmp, path, ec);
    if (ec) fs::remove(tmp, ec);
}

// ═════════════════════════════════════════════════════════════════════════════
// Play Mode – snapshot & rollback
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::enterPlayMode()
{
    if (editorMode_ == EditorMode::Play) return;

    beginPlayAuditSession();

    addLog("[VSTEP] enterPlayMode begin");
    addLog("[VSTEP] Current scene name: " + scene_.sceneName);
    addLog("[VSTEP] Current scene entities: " + std::to_string(scene_.entities.size()));
    for (size_t i = 0; i < scene_.entities.size(); ++i) {
        const auto& e = scene_.entities[i];
        addLog("[VSTEP]   Entity[" + std::to_string(i) + "]: " + e.name + " (type=" + 
               (e.type == EntityData::Type::Player ? "Player" : "Enemy") + ")");
    }

    playSession_.capture(scene_, world_);

    // Export current scene to temp file for Vulkan to load
    std::string tempScene = std::string(BUILD_DIR) + "/_play_scene.json";
    std::string prevPath = scene_.filePath;
    bool prevMod = scene_.modified;
    
    const bool saved = scene_.saveToFile(tempScene);
    
    // Append tilemap (world grid) to the exported scene JSON
    if (saved) {
        std::ifstream in(tempScene);
        json j;
        if (in >> j) {
            in.close();
            // Export the entire world.grid as a flat tilemap array
            // tilemap[y*WORLD_W + x] = tileType (0-8)
            std::vector<int> tilemap;
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
    }
    
    scene_.filePath = prevPath;
    scene_.modified = prevMod;
    addLog(std::string("[Play] Scene exported: ") + (saved ? "ok" : "failed"));

    // ── Enemy AI + melee simulation: same runtime3d system VulkanBootstrap uses,
    // driven every frame from EditorApp::renderWorldToTexture() while Play is active.
    {
        const SceneData flatScene = dash::editor::flattenHierarchy(scene_);
        const auto instances = dash::vkexp::SceneLoader::buildInstances(flatScene);
        enemySim_.build(flatScene, instances, biomeTable_.empty() ? nullptr : &biomeTable_);

        events_.clear(); // drop subscriptions from any previous Play session
        particleSim_.clear();
        particleSim_.setSeed(0xC0FFEEu);
        if (!enemySim_.empty()) {
            // Target position for the splatter: the player if it's the target,
            // else whichever live agent owns that entity id. No attacker lookup
            // (yet), so particles fly in a fixed direction rather than "away
            // from the hit" — a cosmetic simplification, not a correctness gap.
            auto locateTarget = [this](uint64_t targetId, const std::string& targetName,
                                       float& tx, float& tz) -> bool {
                if (targetName == "Player") {
                    for (const auto& e : scene_.entities) {
                        if (e.type == EntityData::Type::Player) { tx = e.x; tz = e.y; return true; }
                    }
                    return false;
                }
                for (const auto& agent : enemySim_.agents()) {
                    if (agent.entityId == targetId) { tx = agent.x; tz = agent.z; return true; }
                }
                return false;
            };

            events_.subscribe<DamageEvent>([this, locateTarget](const DamageEvent& e) {
                addLog("[Combat] hit " + e.targetName + " for " + std::to_string(e.damage) +
                       " (hp left " + std::to_string(e.finalHealth) + ")");
                float tx = 0.f, tz = 0.f;
                if (!locateTarget(e.targetId, e.targetName, tx, tz)) return;
                const float ground = world_.terrain().sampleHeight(tx, tz);
                const float wx = tx * TILE_SCALE, wz = tz * TILE_SCALE, wy = ground + 0.35f;
                particleSim_.emit(dash::vfx::bloodSplatter(wx, wy, wz, 0.f, 1.f, e.damage, ground + 0.02f));
                particleSim_.emit(dash::vfx::impactSparks(wx, wy, wz, 0.f, 1.f, e.damage));
            });
            events_.subscribe<DeathEvent>([this](const DeathEvent& e) {
                addLog("[Combat] " + e.entityName + " died (+" + std::to_string(e.expReward) + " xp)");
                const float ground = world_.terrain().sampleHeight(e.x, e.y);
                const float wx = e.x * TILE_SCALE, wz = e.y * TILE_SCALE, wy = ground + 0.55f;
                particleSim_.emit(dash::vfx::deathShockwave(wx, wy, wz));
                particleSim_.emit(dash::vfx::deathGibs(wx, wy, wz, ground + 0.02f));
                particleSim_.emit(dash::vfx::deathSmoke(wx, wy, wz));
            });
            events_.subscribe<LootDropEvent>([this](const LootDropEvent& e) {
                std::string items;
                for (const auto& item : e.items) items += " " + item.item + " x" + std::to_string(item.qty);
                addLog("[Loot] " + e.enemyId + " dropped:" + items);
            });
            addLog("[Play] Enemy simulation armed: " + std::to_string(enemySim_.agentCount()) + " agent(s).");
        }
    }

    editorMode_ = EditorMode::Play;

    // Never inherit pause/speed from a previous session.
    playback_.reset();
    syncPlaybackStateFile(true);

    // Center camera on player entity
    for (const auto& e : scene_.entities) {
        if (e.type == EntityData::Type::Player) {
            camX_ = e.x;
            camY_ = e.y;
            break;
        }
    }

    addLog("Entered Play mode.");
}

void EditorApp::exitPlayMode()
{
    if (editorMode_ != EditorMode::Play) return;

    flushPlayAuditSessionToFile("play_stopped");

    playSession_.restore(scene_, world_);
    clearSelection();
    editorMode_ = EditorMode::Edit;
    playback_.reset();
    syncPlaybackStateFile(true);
    addLog("Exited Play mode (scene restored).");

    // ── Apply hot-reload changes that were deferred during Play ──────────────
    if (!deferredReloads_.empty()) {
        std::vector<std::string> reloadErrors;
        bool dbChanged = importManager_.reimportChanged(
            deferredReloads_, assetsRoot_, libraryRoot_, assetDb_, reloadErrors);
        for (const auto& ch : deferredReloads_)
            addLog("[Hot-Reload] Reimported: " + ch.relativePath);
        for (const auto& err : reloadErrors)
            addLog("[IMPORT] " + err);
        if (dbChanged) {
            assetDb_.save(assetDbPath_);
        }
        deferredReloads_.clear();
    }
}
