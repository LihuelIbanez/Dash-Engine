#include "SettlementPanel.h"

#include "SceneData.h"
#include "World.h"
#include "commands/CommandStack.h"
#include "commands/GenerateSettlementsCommand.h"
#include "core/components/PrefabAsset.h"
#include "world/SettlementGenerator.h"

#include "imgui.h"

#include <filesystem>
#include <memory>
#include <random>

namespace {

namespace fs = std::filesystem;

bool isStaticProp(const PrefabAsset& prefab)
{
    for (const ComponentVariant& c : prefab.defaultComponents) {
        if (const auto* pc = std::get_if<PhysicsComponent>(&c))
            return pc->isStatic;
    }
    return false;
}

// Any prefab directly under `prefabsDir` whose default components include a
// static PhysicsComponent is eligible scenery for a settlement — the same
// marker EnemySimulation3D uses to skip props during AI simulation. This way
// dropping a new prefab (e.g. "well.json") into assets/prefabs/ makes it
// available here with no code change.
std::vector<PrefabAsset> findBuildingPrefabs(const std::string& prefabsDir)
{
    std::vector<PrefabAsset> out;
    std::error_code ec;
    if (!fs::is_directory(prefabsDir, ec)) return out;

    for (const auto& entry : fs::directory_iterator(prefabsDir, fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (entry.path().extension() != ".json") continue;
        PrefabAsset p = loadPrefab(entry.path().string());
        if (!p.guid.empty() && isStaticProp(p)) out.push_back(std::move(p));
    }
    return out;
}

} // namespace

void SettlementPanel::draw(SceneData& scene, World& world, CommandStack& commandStack,
                           const std::string& assetsRoot, const LogCallback& log)
{
    ImGui::Begin("Settlements");

    ImGui::TextWrapped("Procedurally places settlements (buildings) and roads "
                       "connecting them across the current terrain.");
    ImGui::SliderInt("Settlements", &count_, 1, 12);
    ImGui::SliderFloat("Min spacing", &minSpacing_, 8.f, 80.f, "%.0f tiles");
    ImGui::InputInt("Seed", &seed_);

    if (ImGui::Button("Generate")) {
        SettlementGenerationParams params;
        params.count      = count_;
        params.minSpacing = minSpacing_;
        params.seed       = static_cast<unsigned int>(std::max(0, seed_));

        SettlementGenerationResult result = generateSettlements(world, params);
        if (result.settlements.empty()) {
            if (log) log("[Settlements] No valid spot found (try a smaller spacing or a different seed).");
        } else {
            const std::string prefabsDir = assetsRoot + "/prefabs";
            const std::vector<PrefabAsset> buildingPrefabs = findBuildingPrefabs(prefabsDir);

            if (buildingPrefabs.empty()) {
                if (log) log("[Settlements] No static-prop prefab found under " + prefabsDir + ", aborting.");
            } else {
                std::mt19937 rng(params.seed);
                std::uniform_real_distribution<float> scaleDist(0.8f, 1.4f);
                std::uniform_int_distribution<int>    buildingCountDist(2, 4);
                std::uniform_real_distribution<float> offsetDist(-4.f, 4.f);
                std::uniform_int_distribution<std::size_t> prefabDist(0, buildingPrefabs.size() - 1);

                std::vector<GenerateSettlementsCommand::Building> buildings;
                uint64_t nextId = scene.nextEntityId;

                for (const Settlement& s : result.settlements) {
                    const int buildingsHere = buildingCountDist(rng);
                    for (int i = 0; i < buildingsHere; ++i) {
                        const PrefabAsset& prefab = buildingPrefabs[prefabDist(rng)];

                        GenerateSettlementsCommand::Building b;
                        b.entityId   = nextId++;
                        b.name       = s.name + " " + prefab.name + " " + std::to_string(i + 1);
                        b.x          = s.x + (i == 0 ? 0.f : offsetDist(rng));
                        b.y          = s.y + (i == 0 ? 0.f : offsetDist(rng));
                        b.prefabGuid = prefab.guid;
                        b.components = instantiate(prefab);
                        for (ComponentVariant& c : b.components) {
                            if (auto* t = std::get_if<TransformComponent>(&c)) {
                                t->x = b.x;
                                t->y = b.y;
                                t->scale *= scaleDist(rng);
                            }
                        }
                        buildings.push_back(std::move(b));
                    }
                }

                std::vector<TileOverride> roadTiles;
                for (const SettlementRoad& road : result.roads) {
                    for (const NavPoint& wp : road.waypoints) {
                        if (wp.x < 0 || wp.x >= WORLD_W || wp.y < 0 || wp.y >= WORLD_H) continue;
                        roadTiles.push_back(TileOverride{wp.x, wp.y, static_cast<int>(TileType::Dirt), true});
                    }
                }

                scene.nextEntityId = nextId;
                commandStack.execute(
                    std::make_unique<GenerateSettlementsCommand>(std::move(buildings), std::move(roadTiles)),
                    scene, world);

                if (log) {
                    log("[Settlements] Generated " + std::to_string(result.settlements.size())
                      + " settlement(s) from " + std::to_string(buildingPrefabs.size()) + " building prefab(s), "
                      + std::to_string(result.roads.size()) + " road(s).");
                }
            }
        }
    }

    ImGui::End();
}
