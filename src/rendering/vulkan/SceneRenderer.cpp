#include "rendering/vulkan/SceneRenderer.h"

#include <algorithm>
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

// A skinned draw needs the palette, the pipeline and a mesh that actually
// carries the .dashmesh v2 skinning stream; anything else falls back to opaque.
bool wantsSkinning(const InstanceResources& res, const MeshBuffers* mesh,
                   const SceneDrawParams& params)
{
    return res.boneMatrices != nullptr
        && res.boneCount > 0
        && mesh != nullptr
        && mesh->isSkinned()
        && params.skinnedPipeline != VK_NULL_HANDLE
        && params.boneSet != VK_NULL_HANDLE
        && params.bonePalette != nullptr
        && params.bonePalette->usable();
}

} // namespace

void buildInstancePushConstants(const Mat4& model,
                                float r, float g, float b, float a,
                                const LightingParams& light,
                                float (&out)[kInstancePushConstantFloats],
                                int sceneLightCount)
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
    out[24] = static_cast<float>(sceneLightCount);
    out[25] = light.ambient;
    out[26] = light.specStr;
    out[27] = light.specShin;
}

int packSceneLights(const std::vector<SceneLight>* lights,
                    const Vec3& cameraPos,
                    SceneLightsUbo& out)
{
    out = SceneLightsUbo{};
    out.cameraPos[0] = cameraPos.x;
    out.cameraPos[1] = cameraPos.y;
    out.cameraPos[2] = cameraPos.z;

    if (lights == nullptr) return 0;

    const int count = static_cast<int>(std::min<std::size_t>(lights->size(), kMaxSceneLights));
    for (int i = 0; i < count; ++i) {
        const SceneLight& src = (*lights)[static_cast<std::size_t>(i)];
        SceneLightGpu& dst = out.lights[i];

        dst.posType[0] = src.posX;
        dst.posType[1] = src.posY;
        dst.posType[2] = src.posZ;
        dst.posType[3] = static_cast<float>(src.type);

        dst.dirRange[0] = src.dirX;
        dst.dirRange[1] = src.dirY;
        dst.dirRange[2] = src.dirZ;
        dst.dirRange[3] = src.range;

        dst.colorInt[0] = src.colorR;
        dst.colorInt[1] = src.colorG;
        dst.colorInt[2] = src.colorB;
        dst.colorInt[3] = src.intensity;

        dst.cone[0] = src.innerCos;
        dst.cone[1] = src.outerCos;
    }
    return count;
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

    // Zero keeps the fragment shaders on the single LightingParams directional.
    const int lightCount = params.lights
        ? static_cast<int>(std::min<std::size_t>(params.lights->size(), kMaxSceneLights))
        : 0;

    const MeshBuffers* boundMesh = params.fallbackMesh;
    VkDescriptorSet boundSet = params.defaultSet;
    bool hasBillboards = false;
    bool hasSkinned = false;

    // ── Opaque pass ─────────────────────────────────────────────────────────
    for (std::size_t i = 0; i < instances.size(); ++i) {
        const RenderInstance& inst = instances[i];
        if (!inst.visible) continue;
        if (isBillboard(inst)) {
            hasBillboards = true;
            continue;  // drawn in the transparent pass below
        }

        const InstanceResources& res = resourcesAt(resources, i);
        const MeshBuffers* mesh = res.mesh ? res.mesh : params.fallbackMesh;
        if (wantsSkinning(res, mesh, params)) {
            hasSkinned = true;
            continue;  // drawn in the skinned pass below
        }

        if (!frustum.intersectsAabb(inst.position.x * TILE_SCALE,
                                    inst.position.y,
                                    inst.position.z * TILE_SCALE,
                                    inst.scale.x, inst.scale.y, inst.scale.z)) {
            ++stats.culled;
            continue;
        }
        ++stats.drawn;

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
                                   1.0f, lighting, pc, lightCount);
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

    // ── Skinned meshes ──────────────────────────────────────────────────────────
    // Each instance addresses its own palette slot through the dynamic offset,
    // so one descriptor serves the whole frame.
    if (hasSkinned) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, params.skinnedPipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                params.skinnedLayout, 0, 1, &params.defaultSet, 0, nullptr);

        const MeshBuffers* boundSkinnedMesh = nullptr;
        for (std::size_t i = 0; i < instances.size(); ++i) {
            const RenderInstance& inst = instances[i];
            if (!inst.visible || isBillboard(inst)) continue;

            const InstanceResources& res = resourcesAt(resources, i);
            const MeshBuffers* mesh = res.mesh ? res.mesh : params.fallbackMesh;
            if (!wantsSkinning(res, mesh, params)) continue;

            if (!frustum.intersectsAabb(inst.position.x * TILE_SCALE,
                                        inst.position.y,
                                        inst.position.z * TILE_SCALE,
                                        inst.scale.x, inst.scale.y, inst.scale.z)) {
                ++stats.culled;
                continue;
            }
            ++stats.drawn;

            if (mesh != boundSkinnedMesh) {
                VkBuffer vb[] = { mesh->vertexBuffer() };
                VkDeviceSize offsets[] = { 0 };
                vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
                vkCmdBindIndexBuffer(cmd, mesh->indexBuffer(), 0, mesh->indexType());
                boundSkinnedMesh = mesh;
            }

            const uint32_t dynamicOffset = res.boneOffset;
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                    params.skinnedLayout, 1, 1, &params.boneSet,
                                    1, &dynamicOffset);

            const Mat4 model = trs(
                {inst.position.x * TILE_SCALE, inst.position.y, inst.position.z * TILE_SCALE},
                inst.yawDeg, inst.pitchDeg, inst.rollDeg,
                {inst.scale.x, inst.scale.y, inst.scale.z});

            float pc[kInstancePushConstantFloats];
            buildInstancePushConstants(model,
                                       inst.color.x * res.tint[0],
                                       inst.color.y * res.tint[1],
                                       inst.color.z * res.tint[2],
                                       1.0f, lighting, pc, lightCount);
            vkCmdPushConstants(cmd, params.skinnedLayout,
                               VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                               0, sizeof(pc), pc);
            vkCmdDrawIndexed(cmd, mesh->indexCount(), 1, 0, 0, 0);
        }
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
