// ═════════════════════════════════════════════════════════════════════════════
// EditorApp — 3D viewport camera matrix, skeletal animation preview and the
// Vulkan render of the offscreen viewport texture.
//
// Split out of EditorApp.cpp to keep that file navigable.
// ═════════════════════════════════════════════════════════════════════════════
#include "EditorApp.h"
#include "rendering/vulkan/SceneLoader.h"
#include "rendering/vulkan/SceneRenderer.h"

#include "imgui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

bool EditorApp::syncSceneRender3DSettingsFromUI()
{
    scene_.render3d.useVulkan3D = viewport3D_.useVulkan3D;
    scene_.render3d.embeddedPreview = viewport3D_.embeddedPreview;
    scene_.render3d.isoYawDeg = viewport3D_.isoYawDeg;
    scene_.render3d.isoPitchDeg = viewport3D_.isoPitchDeg;
    scene_.render3d.cameraDistance = viewport3D_.cameraDistance;
    scene_.render3d.cameraHeight = viewport3D_.cameraHeight;
    scene_.render3d.zoom = viewport3D_.zoom;
    scene_.render3d.heightScale = viewport3D_.heightScale;
    scene_.render3d.gridOpacity = viewport3D_.gridOpacity;
    return true;
}

void EditorApp::syncUIRender3DSettingsFromScene()
{
    viewport3D_.useVulkan3D = scene_.render3d.useVulkan3D;
    viewport3D_.embeddedPreview = scene_.render3d.embeddedPreview;
    viewport3D_.isoYawDeg = scene_.render3d.isoYawDeg;
    viewport3D_.isoPitchDeg = scene_.render3d.isoPitchDeg;
    viewport3D_.cameraDistance = scene_.render3d.cameraDistance;
    viewport3D_.cameraHeight = scene_.render3d.cameraHeight;
    viewport3D_.zoom = scene_.render3d.zoom;
    viewport3D_.heightScale = scene_.render3d.heightScale;
    viewport3D_.gridOpacity = scene_.render3d.gridOpacity;
}

// ═════════════════════════════════════════════════════════════════════════════
// Shared camera matrix builder (used by renderWorldToTexture & viewportScreenToWorld)
// ═════════════════════════════════════════════════════════════════════════════
void EditorApp::buildViewProjMatrix(float vpW, float vpH, float viewProj[16],
                                     float* outEyeX, float* outEyeY, float* outEyeZ)
{
    const float yaw   = viewport3D_.isoYawDeg * 3.14159265f / 180.0f;
    const float pitch = viewport3D_.isoPitchDeg * 3.14159265f / 180.0f;
    const float dist  = viewport3D_.cameraDistance;

    float targetX = camX_ * TILE_SCALE;
    float targetZ = camY_ * TILE_SCALE;
    float targetY = viewport3D_.cameraHeight;

    float eyeX = targetX + dist * std::cos(yaw) * std::cos(pitch);
    float eyeY = targetY + dist * std::sin(pitch);
    float eyeZ = targetZ + dist * std::sin(yaw) * std::cos(pitch);

    if (outEyeX) *outEyeX = eyeX;
    if (outEyeY) *outEyeY = eyeY;
    if (outEyeZ) *outEyeZ = eyeZ;

    // Look-at matrix
    float fx = targetX - eyeX, fy = targetY - eyeY, fz = targetZ - eyeZ;
    float flen = std::sqrt(fx*fx + fy*fy + fz*fz);
    if (flen > 1e-6f) { fx /= flen; fy /= flen; fz /= flen; }

    float ux = 0.0f, uy = 1.0f, uz = 0.0f;
    float rx = fy * uz - fz * uy;
    float ry = fz * ux - fx * uz;
    float rz = fx * uy - fy * ux;
    float rlen = std::sqrt(rx*rx + ry*ry + rz*rz);
    if (rlen > 1e-6f) { rx /= rlen; ry /= rlen; rz /= rlen; }
    ux = ry * fz - rz * fy;
    uy = rz * fx - rx * fz;
    uz = rx * fy - ry * fx;

    float view[16] = {
         rx,  ux, -fx, 0,
         ry,  uy, -fy, 0,
         rz,  uz, -fz, 0,
        -(rx*eyeX + ry*eyeY + rz*eyeZ),
        -(ux*eyeX + uy*eyeY + uz*eyeZ),
        -(-fx*eyeX + -fy*eyeY + -fz*eyeZ),
         1
    };

    float aspect = vpW / vpH;
    float fov = 45.0f * 3.14159265f / 180.0f;
    float nearP = 0.1f, farP = 500.0f;
    float tanHalf = std::tan(fov * 0.5f);
    float proj[16] = {
        1.0f / (aspect * tanHalf), 0, 0, 0,
        0, -1.0f / tanHalf, 0, 0,
        0, 0, farP / (nearP - farP), -1,
        0, 0, (farP * nearP) / (nearP - farP), 0
    };

    // viewProj = proj * view (column-major)
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            viewProj[c * 4 + r] =
                proj[0 * 4 + r] * view[c * 4 + 0] +
                proj[1 * 4 + r] * view[c * 4 + 1] +
                proj[2 * 4 + r] * view[c * 4 + 2] +
                proj[3 * 4 + r] * view[c * 4 + 3];
        }
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Viewport rendering (Vulkan pipeline)
// ═════════════════════════════════════════════════════════════════════════════
// ─────────────────────────────────────────────────────────────────────────────
// Skeletal animation preview
//
// Runs in Edit mode as well as Play: the point of the viewport is to preview
// clips without launching the game. Players are keyed by entity id because the
// instance vector is rebuilt from the scene every frame.
// ─────────────────────────────────────────────────────────────────────────────
void EditorApp::updateViewportAnimators(float dt,
                                        const std::vector<dash::vkexp::RenderInstance>& instances,
                                        std::vector<dash::vkexp::InstanceResources>& resources)
{
    dash::anim::BonePalette& palette = vkCtx_.bonePalette();
    if (!palette.usable()) return;
    palette.beginFrame(vkCtx_.currentFrameIndex());

    for (size_t i = 0; i < instances.size() && i < resources.size(); ++i) {
        const dash::vkexp::RenderInstance& inst = instances[i];
        if (!inst.hasAnimation || inst.entityId == 0) continue;

        auto it = animators_.find(inst.entityId);
        if (it == animators_.end()) {
            const std::string meshPath = vkCtx_.resolveModelPath(inst.meshId);
            if (meshPath.empty()) continue;

            const dash::anim::AnimationSet& set = animationSets_.load(meshPath);
            if (!set.valid()) continue;

            dash::anim::AnimationPlayer player;
            player.setSkeleton(set.skeleton);
            for (const auto& clip : set.clips) player.addClip(clip);
            it = animators_.emplace(inst.entityId, std::move(player)).first;
        }

        dash::anim::AnimationPlayer& player = it->second;
        // Re-reading the component keeps Inspector edits (clip, pause, speed) live.
        player.syncWithComponent(inst.animation);
        player.update(dt);

        const std::vector<dash::anim::Mat4>& mats = player.boneMatrices();
        if (mats.empty()) continue;

        const int64_t offset = palette.writeSlot(mats.front().m,
                                                 static_cast<uint32_t>(mats.size()));
        if (offset < 0) break;  // frame ran out of slots

        resources[i].boneMatrices = mats.front().m;
        resources[i].boneCount    = static_cast<uint32_t>(mats.size());
        resources[i].boneOffset   = static_cast<uint32_t>(offset);
    }

    // Drop players whose entity left the scene. Their bone matrices are not
    // referenced by `resources`, precisely because the entity is gone.
    for (auto it = animators_.begin(); it != animators_.end(); ) {
        const bool present = std::any_of(
            instances.begin(), instances.end(),
            [&](const dash::vkexp::RenderInstance& in) {
                return in.hasAnimation && in.entityId == it->first;
            });
        it = present ? std::next(it) : animators_.erase(it);
    }

    if (animators_.size() != loggedAnimatorCount_) {
        loggedAnimatorCount_ = animators_.size();
        std::printf("[Anim] Viewport: %zu animated instance(s) over %zu model(s).\n",
                    animators_.size(), animationSets_.size());
        for (const auto& [entityId, player] : animators_) {
            std::printf("[Anim]   entity %llu clip='%s' bones=%zu\n",
                        static_cast<unsigned long long>(entityId),
                        player.currentClipName().c_str(),
                        player.boneMatrices().size());
        }
        std::fflush(stdout);
    }

    // Proof-of-motion trace: prints the same pose digest at intervals, so two
    // samples that match mean nothing is animating.
    static const bool trace = std::getenv("DASH_ANIM_TRACE") != nullptr;
    if (!trace) return;
    static float traceAccum = 0.0f;
    traceAccum += dt;
    if (traceAccum < 3.0f) return;
    traceAccum = 0.0f;

    for (const auto& [entityId, player] : animators_) {
        const std::vector<dash::anim::Mat4>& mats = player.boneMatrices();
        if (mats.empty()) continue;
        uint64_t hash = 1469598103934665603ull;
        for (const dash::anim::Mat4& m : mats) {
            for (float v : m.m) {
                uint32_t bits = 0;
                std::memcpy(&bits, &v, sizeof(bits));
                hash = (hash ^ bits) * 1099511628211ull;
            }
        }
        std::printf("[AnimTrace] entity=%llu clip='%s' bone0.col3=(%.4f, %.4f, %.4f) paletteHash=%016llx\n",
                    static_cast<unsigned long long>(entityId),
                    player.currentClipName().c_str(),
                    mats.front().m[12], mats.front().m[13], mats.front().m[14],
                    static_cast<unsigned long long>(hash));
    }
    std::fflush(stdout);
}

void EditorApp::renderWorldToTexture()
{
    // Determine viewport size from the ImGui panel
    uint32_t vpW = static_cast<uint32_t>(std::max(1.0f, vpDisplayW_));
    uint32_t vpH = static_cast<uint32_t>(std::max(1.0f, vpDisplayH_));

    // ── Update terrain mesh if dirty ────────────────────────────────────────
    vkCtx_.updateTerrainMesh(world_.terrain());

    // ── Build view-projection matrix (isometric 3D camera) ──────────────────
    float eyeX, eyeY, eyeZ;
    float viewProj[16];
    buildViewProjMatrix(static_cast<float>(vpW), static_cast<float>(vpH),
                        viewProj, &eyeX, &eyeY, &eyeZ);

    vkCtx_.updateCamera(viewProj);

    // ── Scene instances and lights ──────────────────────────────────────────
    // Built up front because the shadow depth pass draws the very same casters,
    // and it has to be recorded before the viewport pass: passes cannot nest.
    const SceneData flatScene = dash::editor::flattenHierarchy(scene_);
    std::vector<dash::vkexp::RenderInstance> instances =
        dash::vkexp::SceneLoader::buildInstances(flatScene);
    std::vector<dash::vkexp::SceneLight> sceneLights =
        dash::vkexp::SceneLoader::buildLights(flatScene);

    std::vector<dash::vkexp::InstanceResources> resources(instances.size());
    for (size_t i = 0; i < instances.size(); ++i) {
        auto& inst = instances[i];
        // The placeholder cube's pivot sits at its centre and needs the lift; a
        // custom mesh is base-anchored (matches the same rule in
        // Renderer::snapInstancesToTerrain, so the editor doesn't show enemies
        // floating or sinking relative to the runtime).
        const bool isCubePlaceholder = inst.meshId.empty() || inst.meshId == "cube";
        inst.position.y += world_.terrain().sampleHeight(inst.position.x, inst.position.z)
                         + (isCubePlaceholder ? inst.scale.y : 0.0f);
        resources[i].mesh = vkCtx_.resolveMesh(inst.meshId);
    }

    // ── Enemy AI + combat (Play mode only; enemySim_ is armed in enterPlayMode) ──
    // Runs after the grounding loop above: syncToInstances() writes each agent's
    // already-grounded absolute position, which must be the last word on it.
    if (editorMode_ == EditorMode::Play && !enemySim_.empty()) {
        float playerX = 0.0f, playerZ = 0.0f;
        for (const auto& e : flatScene.entities) {
            if (e.type == EntityData::Type::Player) { playerX = e.x; playerZ = e.y; break; }
        }
        const float simDt = playback_.paused() ? 0.0f
            : std::min(ImGui::GetIO().DeltaTime, 0.1f) * playback_.timeScale();
        const bool attackInput = ImGui::IsKeyDown(ImGuiKey_Space);
        enemySim_.update(simDt, playerX, playerZ, attackInput, &world_.terrain(), true, events_);
        enemySim_.syncToInstances(instances);
        events_.flush();

        particleSim_.update(simDt);
        particleSim_.buildInstances(particleAlphaBatch_, particleAdditiveBatch_,
                                    dash::vfx::kAtlasCols, dash::vfx::kAtlasRows);
    }

    dash::vkexp::LightingParams lighting;
    lighting.dirX = viewport3D_.lightDirX;
    lighting.dirY = viewport3D_.lightDirY;
    lighting.dirZ = viewport3D_.lightDirZ;
    lighting.intensity = viewport3D_.lightIntensity;
    lighting.colorR = viewport3D_.lightColorR;
    lighting.colorG = viewport3D_.lightColorG;
    lighting.colorB = viewport3D_.lightColorB;
    lighting.ambient = viewport3D_.ambientStrength;

    // Same sun Renderer::init synthesizes when a scene declares no light, so the
    // Lighting panel drives the viewport shadows live. LightingParams.dir is the
    // surface-to-light vector, SceneLight.dir the emission direction.
    if (sceneLights.empty()) {
        dash::vkexp::SceneLight sun;
        sun.type = 0;
        sun.dirX = -lighting.dirX;
        sun.dirY = -lighting.dirY;
        sun.dirZ = -lighting.dirZ;
        sun.colorR = lighting.colorR;
        sun.colorG = lighting.colorG;
        sun.colorB = lighting.colorB;
        sun.intensity = lighting.intensity;
        sun.castsShadows = true;
        sceneLights.push_back(sun);
    }

    // ── Shadow cascades ─────────────────────────────────────────────────────
    int shadowLight = -1;
    for (size_t i = 0; i < sceneLights.size(); ++i) {
        if (sceneLights[i].type == 0 && sceneLights[i].castsShadows) {
            shadowLight = static_cast<int>(i);
            break;
        }
    }

    // Same basis buildViewProjMatrix derives, reused for the frustum slices and
    // for the billboard axes further down.
    const dash::vkexp::Vec3 forward = dash::vkexp::normalize(
        {camX_ * TILE_SCALE - eyeX, viewport3D_.cameraHeight - eyeY, camY_ * TILE_SCALE - eyeZ});
    const dash::vkexp::Vec3 camRight = dash::vkexp::normalize(
        dash::vkexp::cross(forward, {0.0f, 1.0f, 0.0f}));
    const dash::vkexp::Vec3 camUp = dash::vkexp::cross(camRight, forward);

    dash::vkexp::Vec3 shadowDir{-lighting.dirX, -lighting.dirY, -lighting.dirZ};
    if (shadowLight >= 0) {
        const dash::vkexp::SceneLight& l = sceneLights[static_cast<size_t>(shadowLight)];
        shadowDir = {l.dirX, l.dirY, l.dirZ};
    }
    vkCtx_.updateShadowCascades({eyeX, eyeY, eyeZ}, forward, camRight, camUp,
                                45.0f * 3.14159265f / 180.0f,
                                static_cast<float>(vpW) / static_cast<float>(vpH),
                                shadowDir, shadowLight);

    // Scene lights need the "_lit"/"_shadow" pipeline; without it the viewport
    // keeps the flat directional shading it always had.
    const bool useSceneLights = vkCtx_.basicLitPipeline() != VK_NULL_HANDLE;

    // Uploaded even when the lit pipeline is missing: the terrain shader reads
    // the camera position and the cascade block out of the same buffer.
    dash::vkexp::SceneLightsUbo lightUbo;
    const int lightCount = dash::vkexp::packSceneLights(
        &sceneLights, {eyeX, eyeY, eyeZ}, lightUbo);
    vkCtx_.fillShadowUbo(lightUbo);
    vkCtx_.updateSceneLights(lightUbo, lightCount);

    // ── Shadow depth pass (outside the viewport render pass) ────────────────
    // Sized before both depth passes: the SSAO target follows the viewport, and
    // resizing it once the render pass is open is not possible.
    vkCtx_.ensureViewportSize(vpW, vpH);

    // Before every pass that draws the instances: all three read the same slots.
    updateViewportAnimators(std::min(ImGui::GetIO().DeltaTime, 0.1f), instances, resources);

    vkCtx_.recordShadowPass(instances, resources);

    dash::vkexp::Mat4 ssaoViewProj{};
    std::memcpy(ssaoViewProj.m, viewProj, sizeof(ssaoViewProj.m));
    vkCtx_.recordSsaoPass(instances, resources, ssaoViewProj,
                          static_cast<float>(vpW) / static_cast<float>(vpH));

    // ── Begin offscreen viewport render pass ────────────────────────────────
    vkCtx_.beginViewportRender(vpW, vpH);
    VkCommandBuffer cmd = vkCtx_.currentCmd();

    // ── Terrain ─────────────────────────────────────────────────────────────
    if (vkCtx_.terrainMesh().indexCount() > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkCtx_.terrainPipeline());
        VkDescriptorSet ds = vkCtx_.sceneDescriptorSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                vkCtx_.terrainPipelineLayout(), 0, 1, &ds, 0, nullptr);

        VkBuffer vb[] = { vkCtx_.terrainMesh().vertexBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, vkCtx_.terrainMesh().indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        // Push constants: eyePos(3) + time(1) + fogStart(1) + fogEnd(1) + lightDir(3) + intensity(1) + lightColor(3) + ambient(1) + 2 spare
        static auto startTime = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime).count();

        float terrainPC[dash::vkexp::kTerrainPushConstantFloats] = {
            eyeX, eyeY, eyeZ, elapsed,
            viewport3D_.fogStart, viewport3D_.fogEnd, viewport3D_.lightDirX, viewport3D_.lightDirY,
            viewport3D_.lightDirZ, viewport3D_.lightIntensity, viewport3D_.lightColorR, viewport3D_.lightColorG,
            viewport3D_.lightColorB, viewport3D_.ambientStrength, 0.0f, 0.0f
        };
        vkCmdPushConstants(cmd, vkCtx_.terrainPipelineLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(terrainPC), terrainPC);

        vkCmdDrawIndexed(cmd, vkCtx_.terrainMesh().indexCount(), 1, 0, 0, 0);
    }

    // ── Water ───────────────────────────────────────────────────────────────
    if (vkCtx_.waterMesh().indexCount() > 0) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, vkCtx_.waterPipeline());
        VkDescriptorSet ds = vkCtx_.sceneDescriptorSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                vkCtx_.waterPipelineLayout(), 0, 1, &ds, 0, nullptr);

        VkBuffer vb[] = { vkCtx_.waterMesh().vertexBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, offsets);
        vkCmdBindIndexBuffer(cmd, vkCtx_.waterMesh().indexBuffer(), 0, VK_INDEX_TYPE_UINT32);

        static auto startTime2 = std::chrono::high_resolution_clock::now();
        float elapsed2 = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - startTime2).count();
        float waterPC[dash::vkexp::kTerrainPushConstantFloats] = {
            eyeX, eyeY, eyeZ, elapsed2,
            viewport3D_.fogStart, viewport3D_.fogEnd, viewport3D_.lightDirX, viewport3D_.lightDirY,
            viewport3D_.lightDirZ, viewport3D_.lightIntensity, viewport3D_.lightColorR, viewport3D_.lightColorG,
            viewport3D_.lightColorB, viewport3D_.ambientStrength, 0.0f, 0.0f
        };
        vkCmdPushConstants(cmd, vkCtx_.waterPipelineLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(waterPC), waterPC);

        vkCmdDrawIndexed(cmd, vkCtx_.waterMesh().indexCount(), 1, 0, 0, 0);
    }

    // ── Entity rendering ─────────────────────────────────────────────────────
    if (vkCtx_.cubeMesh().indexCount() > 0) {
        VkPipeline       opaquePipeline = useSceneLights ? vkCtx_.basicLitPipeline()
                                                         : vkCtx_.basicPipeline();
        VkPipelineLayout opaqueLayout   = useSceneLights ? vkCtx_.basicLitPipelineLayout()
                                                         : vkCtx_.basicPipelineLayout();

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, opaquePipeline);
        VkDescriptorSet ds = vkCtx_.sceneDescriptorSet();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                opaqueLayout, 0, 1, &ds, 0, nullptr);

        VkBuffer vb[] = { vkCtx_.cubeMesh().vertexBuffer() };
        VkDeviceSize vbOffsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, vb, vbOffsets);
        vkCmdBindIndexBuffer(cmd, vkCtx_.cubeMesh().indexBuffer(), 0, vkCtx_.cubeMesh().indexType());

        dash::vkexp::SceneDrawParams params;
        params.opaquePipeline    = opaquePipeline;
        params.opaqueLayout      = opaqueLayout;
        params.billboardPipeline = vkCtx_.billboardPipeline();
        params.billboardLayout   = vkCtx_.billboardPipelineLayout();
        params.skinnedPipeline   = vkCtx_.skinnedPipeline();
        params.skinnedLayout     = vkCtx_.skinnedPipelineLayout();
        params.defaultSet        = ds;
        params.fallbackMesh      = &vkCtx_.cubeMesh();
        params.boneSet           = vkCtx_.boneDescriptorSet();
        params.bonePalette       = &vkCtx_.bonePalette();
        params.lights            = useSceneLights ? &sceneLights : nullptr;
        params.cameraRight       = camRight;
        params.cameraUp          = camUp;
        std::memcpy(params.viewProj.m, viewProj, sizeof(params.viewProj.m));

        dash::vkexp::drawSceneInstances(cmd, instances, resources, lighting, params);
    }

    // Drawn last, still inside the HDR pass, same as Renderer::particles_.record().
    if (!particleAlphaBatch_.empty() || !particleAdditiveBatch_.empty()) {
        dash::vkexp::Mat4 particleViewProj{};
        std::memcpy(particleViewProj.m, viewProj, sizeof(particleViewProj.m));
        vkCtx_.recordParticles(particleViewProj, camRight, camUp,
                              particleAlphaBatch_, particleAdditiveBatch_);
    }

    vkCtx_.endViewportRender();
}
