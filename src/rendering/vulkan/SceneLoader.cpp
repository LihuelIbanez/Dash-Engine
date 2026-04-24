#include "rendering/vulkan/SceneLoader.h"

#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace dash::vkexp {

using json = nlohmann::json;

bool SceneLoader::loadSpawnPoint(const std::string& scenePath, dash::physics::Vec3& outSpawn)
{
    std::ifstream in(scenePath);
    if (!in.is_open()) return false;

    json j;
    try {
        in >> j;
    } catch (...) {
        return false;
    }

    if (!j.contains("entities") || !j["entities"].is_array()) return false;

    const auto& entities = j["entities"];
    const json* selected = nullptr;
    for (const auto& e : entities) {
        if (!e.is_object()) continue;
        if (e.value("type", std::string{}) == "Player") {
            selected = &e;
            break;
        }
    }
    if (!selected && !entities.empty() && entities[0].is_object()) {
        selected = &entities[0];
    }
    if (!selected) return false;

    const float sx = selected->value("x", 0.0f);
    const float sy = selected->value("y", 0.0f);

    // Dash scenes store horizontal position in (x, y). Vulkan baseline uses y-up,
    // so map scene y to z and keep a default spawn height on physics y.
    outSpawn = {sx, 0.8f, sy};
    return true;
}

std::vector<RenderInstance> SceneLoader::loadInstances(const std::string& scenePath)
{
    std::vector<RenderInstance> out;
    std::ifstream in(scenePath);
    if (!in.is_open()) return out;

    json j;
    try {
        in >> j;
    } catch (...) {
        return out;
    }

    if (!j.contains("entities") || !j["entities"].is_array()) return out;

    for (const auto& e : j["entities"]) {
        if (!e.is_object()) continue;
        const float ex = e.value("x", 0.0f);
        const float ez = e.value("y", 0.0f);
        float ey = 0.6f;
        dash::physics::Vec3 color{0.82f, 0.34f, 0.34f};
        dash::physics::Vec3 scale{0.22f, 0.40f, 0.22f};
        bool isPlayer = false;
        if (e.contains("type") && e["type"].is_string()) {
            const std::string t = e["type"].get<std::string>();
            if (t == "Player") {
                ey = 1.0f;
                color = {0.30f, 0.58f, 0.95f};
                scale = {0.26f, 0.52f, 0.26f};
                isPlayer = true;
            }
        }
        out.push_back({{ex, ey, ez}, scale, color, isPlayer});
    }
    return out;
}

dash::physics::Vec3 SceneLoader::getTileColor(int tileType)
{
    switch (tileType) {
        case 0:  return {0.04f, 0.07f, 0.22f};   // DeepWater
        case 1:  return {0.08f, 0.14f, 0.31f};   // Water
        case 2:  return {0.43f, 0.35f, 0.20f};   // Sand
        case 3:  return {0.14f, 0.22f, 0.10f};   // Grass
        case 4:  return {0.08f, 0.16f, 0.06f};   // Forest
        case 5:  return {0.25f, 0.16f, 0.10f};   // Dirt
        case 6:  return {0.27f, 0.25f, 0.24f};   // Stone
        case 7:  return {0.22f, 0.20f, 0.19f};   // Mountain
        case 8:  return {0.63f, 0.65f, 0.69f};   // Snow
        default: return {0.14f, 0.22f, 0.10f};   // Default to Grass
    }
}

float SceneLoader::getTileHeight(int tileType)
{
    switch (tileType) {
        case 0: return -0.30f; // DeepWater
        case 1: return -0.16f; // Water
        case 2: return  0.00f; // Sand
        case 3: return  0.05f; // Grass
        case 4: return  0.14f; // Forest
        case 5: return  0.08f; // Dirt
        case 6: return  0.22f; // Stone
        case 7: return  0.42f; // Mountain
        case 8: return  0.50f; // Snow
        default: return 0.05f;
    }
}

float SceneLoader::sampleTerrainHeight(const std::vector<float>& mapHeights, int mapWidth, int mapHeight, float x, float z)
{
    if (mapHeights.empty() || mapWidth <= 0 || mapHeight <= 0) return 0.0f;
    int tx = static_cast<int>(std::round(x));
    int tz = static_cast<int>(std::round(z));
    tx = std::max(0, std::min(mapWidth - 1, tx));
    tz = std::max(0, std::min(mapHeight - 1, tz));
    return mapHeights[static_cast<size_t>(tz * mapWidth + tx)];
}

bool SceneLoader::loadPlayerPosition(const std::string& scenePath, float& outX, float& outZ)
{
    std::ifstream in(scenePath);
    if (!in.is_open()) return false;

    json j;
    try {
        in >> j;
    } catch (...) {
        return false;
    }

    if (!j.contains("entities") || !j["entities"].is_array()) return false;

    for (const auto& e : j["entities"]) {
        if (!e.is_object()) continue;
        if (e.contains("type") && e["type"].is_string()) {
            const std::string t = e["type"].get<std::string>();
            if (t == "Player") {
                outX = e.value("x", 32.0f);
                outZ = e.value("y", 32.0f);
                return true;
            }
        }
    }
    return false;
}

std::vector<RenderInstance> SceneLoader::loadTerrainInstances(
    const std::string& scenePath,
    std::vector<float>* outHeightMap,
    int* outMapWidth,
    int* outMapHeight)
{
    std::vector<RenderInstance> out;
    if (outHeightMap) outHeightMap->clear();
    if (outMapWidth) *outMapWidth = 0;
    if (outMapHeight) *outMapHeight = 0;

    std::ifstream in(scenePath);
    if (!in.is_open()) return out;

    json j;
    try {
        in >> j;
    } catch (...) {
        return out;
    }

    // Try to load tilemap from JSON
    if (j.contains("tilemap") && j["tilemap"].is_array()) {
        const auto& tilemap = j["tilemap"];
        const int worldWidth = j.value("worldWidth", 64);
        const int worldHeight = j.value("worldHeight", worldWidth > 0 ? static_cast<int>(tilemap.size()) / worldWidth : 0);

        if (outHeightMap && worldWidth > 0 && worldHeight > 0) {
            outHeightMap->assign(static_cast<size_t>(worldWidth * worldHeight), 0.0f);
            if (outMapWidth) *outMapWidth = worldWidth;
            if (outMapHeight) *outMapHeight = worldHeight;
        }

        for (int idx = 0; idx < static_cast<int>(tilemap.size()); ++idx) {
            const int y = idx / worldWidth;
            const int x = idx % worldWidth;

            const int tileType = tilemap[idx].is_number() ? tilemap[idx].get<int>() : 3;
            const dash::physics::Vec3 color = getTileColor(tileType);
            const float h = getTileHeight(tileType);

            if (outHeightMap && idx < static_cast<int>(outHeightMap->size())) {
                (*outHeightMap)[static_cast<size_t>(idx)] = h;
            }

            out.push_back({
                {static_cast<float>(x), h, static_cast<float>(y)},
                {0.48f, 0.03f, 0.48f},
                color
            });
        }
        return out;
    }

    // Fallback: Generate checkerboard around entities if no tilemap
    int minX = 28;
    int maxX = 36;
    int minZ = 28;
    int maxZ = 36;

    if (j.contains("entities") && j["entities"].is_array() && !j["entities"].empty()) {
        bool first = true;
        for (const auto& e : j["entities"]) {
            if (!e.is_object()) continue;
            const int ex = static_cast<int>(std::round(e.value("x", 0.0f)));
            const int ez = static_cast<int>(std::round(e.value("y", 0.0f)));
            if (first) {
                minX = maxX = ex;
                minZ = maxZ = ez;
                first = false;
            } else {
                minX = std::min(minX, ex);
                maxX = std::max(maxX, ex);
                minZ = std::min(minZ, ez);
                maxZ = std::max(maxZ, ez);
            }
        }
    }

    minX -= 3;
    maxX += 3;
    minZ -= 3;
    maxZ += 3;
    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            const bool checker = ((x + z) & 1) == 0;
            out.push_back({
                {static_cast<float>(x), -1.05f, static_cast<float>(z)},
                {0.48f, 0.03f, 0.48f},
                checker ? dash::physics::Vec3{0.24f, 0.34f, 0.24f}
                        : dash::physics::Vec3{0.18f, 0.28f, 0.18f}
            });
        }
    }
    return out;
}

} // namespace dash::vkexp
