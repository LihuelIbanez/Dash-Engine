#include "rendering/vulkan/SceneRenderer.h"

#include <cstring>

#include "rendering/Frustum.h"
#include "rendering/IsoRenderer.h"

namespace dash::vkexp {

namespace {

const InstanceResources& resourcesAt(const std::vector<InstanceResources>& res, std::size_t i)
{
    static const InstanceResources kDefault{};
    return i < res.size() ? res[i] : kDefault;
}

bool isBillboard(const RenderInstance& inst)
{
    return inst.renderMode == static_cast<int>(InstanceRenderMode::BillboardSprite);
}

} // namespace

void buildInstancePushConstants(const Mat4& model,
                                float r, float g, float b, float a,
                                const LightingParams& light,
                                float (&out)[kInstancePushConstantFloats])
{
    std::memcpy(out, model.m, sizeof(model.m));
    out[16] = r;
    out[17] = g;
    out[18] = b;
    out[19] = a;
    out[20] = light.dirX;
    out[21] = light.dirY;
    out[22] = light.dirZ;
    out[23] = light.intensity;
}

SceneDrawStats drawSceneInstances(VkCommandBuffer cmd,
                                  const std::vector<RenderInstance>& instances,
                                  const std::vector<InstanceResources>& resources,
                                  const LightingParams& lighting,
                                  const SceneDrawParams& params)
{
    SceneDrawStats stats;
    if (cmd == VK_NULL_HANDLE || params.fallbackMesh == nullptr) return stats;

    const dash::Frustum frustum = dash::Frustum::fromViewProj(params.viewProj.m);

    const MeshBuffers* boundMesh = params.fallbackMesh;
    VkDescriptorSet boundSet = params.defaultSet;
    bool hasBillboards = false;

    // ── Opaque pass ─────────────────────────────────────────────────────────
    for (std::size_t i = 0; i < instances.size(); ++i) {
        const RenderInstance& inst = instances[i];
        if (!inst.visible) continue;
        if (isBillboard(inst)) {
            hasBillboards = true;
            continue;  // drawn in the transparent pass below
        }

        if (!frustum.intersectsAabb(inst.position.x * TILE_SCALE,
                                    inst.position.y,
                                    inst.position.z * TILE_SCALE,
                                    inst.scale.x, inst.scale.y, inst.scale.z)) {
            ++stats.culled;
            continue;
        }
        ++stats.drawn;

        const InstanceResources& res = resourcesAt(resources, i);
        const MeshBuffers* mesh = res.mesh ? res.mesh : params.fallbackMesh;
        if (mesh != boundMesh) {
            VkBuffer vb[] = { mesh->vertexBuffer() };
            VkDeviceSize offsets[] = { 0 };
            vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
            vkCmdBindIndexBuffer(cmd, mesh->indexBuffer(), 0, mesh->indexType());
            boundMesh = mesh;
        }

        VkDescriptorSet set = res.materialSet != VK_NULL_HANDLE ? res.materialSet
                                                                : params.defaultSet;
        if (set != boundSet) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    params.opaqueLayout, 0, 1, &set, 0, nullptr);
            boundSet = set;
        }

        const Mat4 model = trs(
            {inst.position.x * TILE_SCALE, inst.position.y, inst.position.z * TILE_SCALE},
            inst.yawDeg, inst.pitchDeg, inst.rollDeg,
            {inst.scale.x, inst.scale.y, inst.scale.z});

        float pc[kInstancePushConstantFloats];
        buildInstancePushConstants(model,
                                   inst.color.x * res.tint[0],
                                   inst.color.y * res.tint[1],
                                   inst.color.z * res.tint[2],
                                   1.0f, lighting, pc);
        vkCmdPushConstants(cmd, params.opaqueLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), pc);
        vkCmdDrawIndexed(cmd, mesh->indexCount(), 1, 0, 0, 0);
    }

    // Leave the default set bound for whatever the caller draws next frame.
    if (boundSet != params.defaultSet && params.defaultSet != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                params.opaqueLayout, 0, 1, &params.defaultSet, 0, nullptr);
    }

    // ── Billboards (transparent, after the opaque pass) ──────────────────────
    if (!hasBillboards || params.billboardPipeline == VK_NULL_HANDLE) return stats;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, params.billboardPipeline);
    VkDescriptorSet boundBillboardSet = VK_NULL_HANDLE;

    for (std::size_t i = 0; i < instances.size(); ++i) {
        const RenderInstance& inst = instances[i];
        if (!inst.visible || !isBillboard(inst)) continue;

        // Billboards face the camera, so the quad is as wide as it is tall on X.
        if (!frustum.intersectsAabb(inst.position.x * TILE_SCALE,
                                    inst.position.y,
                                    inst.position.z * TILE_SCALE,
                                    inst.scale.x, inst.scale.y, inst.scale.x)) {
            ++stats.culled;
            continue;
        }
        ++stats.drawn;

        const InstanceResources& res = resourcesAt(resources, i);
        VkDescriptorSet set = res.materialSet != VK_NULL_HANDLE ? res.materialSet
                                                                : params.defaultSet;
        if (set != boundBillboardSet) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    params.billboardLayout, 0, 1, &set, 0, nullptr);
            boundBillboardSet = set;
        }

        const float pc[20] = {
            inst.position.x * TILE_SCALE, inst.position.y, inst.position.z * TILE_SCALE, 0.0f,
            inst.scale.x, inst.scale.y, 0.0f, 0.0f,
            inst.color.x * res.tint[0], inst.color.y * res.tint[1], inst.color.z * res.tint[2], 1.0f,
            params.cameraRight.x, params.cameraRight.y, params.cameraRight.z, 0.0f,
            params.cameraUp.x, params.cameraUp.y, params.cameraUp.z, 0.0f
        };
        vkCmdPushConstants(cmd, params.billboardLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(pc), pc);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }

    return stats;
}

} // namespace dash::vkexp
