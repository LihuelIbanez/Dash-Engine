#include "WaterLevelCommand.h"

WaterLevelCommand::WaterLevelCommand(uint8_t bodyId, float oldLevel, float newLevel)
    : bodyId_(bodyId), oldLevel_(oldLevel), newLevel_(newLevel)
{}

void WaterLevelCommand::apply(SceneData& scene, World& world)
{
    TerrainMesh& tm = world.terrain();
    for (auto& wb : tm.waterBodies()) {
        if (wb.id == bodyId_) {
            wb.waterLevel = newLevel_;
            break;
        }
    }
    tm.markDirty();
    scene.modified = true;
}

void WaterLevelCommand::undo(SceneData& scene, World& world)
{
    TerrainMesh& tm = world.terrain();
    for (auto& wb : tm.waterBodies()) {
        if (wb.id == bodyId_) {
            wb.waterLevel = oldLevel_;
            break;
        }
    }
    tm.markDirty();
    scene.modified = true;
}
