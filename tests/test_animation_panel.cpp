// ═════════════════════════════════════════════════════════════════════════════
// test_animation_panel — el scrub del reproductor sobre el lobo real
//
// La previsualizacion en vivo del panel se apoya en dos cosas: que escribir el
// AnimationComponent llegue al AnimationPlayer del viewport (eso lo hace
// syncWithComponent) y que el scrub deje al player en el tiempo pedido sin
// alterar el resto del estado de reproduccion. Lo segundo es logica pura del
// panel y se verifica aca contra el .dashskel/.dashanim versionados.
// ═════════════════════════════════════════════════════════════════════════════
#include "panels/AnimationPanel.h"

#include "core/components/Components.h"
#include "rendering/animation/AnimationWiring.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace ap = dash::editor::animpanel;
namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

namespace {

std::string wolfMeshPath()
{
#ifdef PROJECT_DIR
    return (fs::path(PROJECT_DIR) / "assets/models/gltf/Wolf-Blender-2.82a.dashmesh").string();
#else
    return {};
#endif
}

// Digest of the whole palette: two equal digests mean the pose did not move.
uint64_t poseDigest(const std::vector<dash::anim::Mat4>& matrices)
{
    uint64_t hash = 1469598103934665603ull;
    for (const dash::anim::Mat4& m : matrices) {
        for (float v : m.m) {
            uint32_t bits = 0;
            std::memcpy(&bits, &v, sizeof(bits));
            hash = (hash ^ bits) * 1099511628211ull;
        }
    }
    return hash;
}

void testScrubOnRealWolf()
{
    const std::string mesh = wolfMeshPath();
    if (mesh.empty() || !fs::exists(mesh)) {
        std::printf("[test] (salteado: no esta %s)\n", mesh.c_str());
        return;
    }

    dash::anim::AnimationSetCache cache;
    const dash::anim::AnimationSet& set = cache.load(mesh);
    ASSERT(set.valid(), "el lobo deberia traer esqueleto");
    ASSERT(!set.clips.empty(), "el lobo deberia traer clips");
    if (!set.valid() || set.clips.empty()) return;

    dash::anim::AnimationPlayer player;
    player.setSkeleton(set.skeleton);
    for (const auto& clip : set.clips) player.addClip(clip);

    const dash::anim::AnimationClip& clip = *set.clips.front();
    const float duration = clip.durationSeconds();
    std::printf("[test] clip '%s', %.3f s, %zu huesos\n", clip.name.c_str(),
                static_cast<double>(duration), set.skeleton->boneCount());
    ASSERT(duration > 0.0f, "el clip deberia durar algo");

    // El panel deja el player pausado y a velocidad propia; el scrub no puede
    // pisar ninguna de las dos.
    player.setPaused(true);
    player.setSpeed(2.5f);

    ASSERT(ap::scrubTo(player, clip.name, 0.0f), "el scrub deberia encontrar el clip");
    ASSERT(player.paused(), "el scrub no deberia despausar");
    ASSERT(std::fabs(player.speed() - 2.5f) < 1e-5f, "el scrub no deberia cambiar la velocidad");
    ASSERT(std::fabs(player.currentTimeSeconds()) < 1e-4f, "scrub a 0 deberia dar t=0");

    const uint64_t atZero = poseDigest(player.boneMatrices());
    ASSERT(!player.boneMatrices().empty(), "deberia haber matrices de hueso");

    const float mid = duration * 0.5f;
    ap::scrubTo(player, clip.name, mid);
    ASSERT(std::fabs(player.currentTimeSeconds() - mid) < 1e-3f,
           "el scrub deberia aterrizar en el tiempo pedido");
    const uint64_t atMid = poseDigest(player.boneMatrices());
    ASSERT(atMid != atZero, "la pose a mitad del clip deberia diferir de la del inicio");

    // Volver atras: es lo que un slider hace todo el tiempo.
    ap::scrubTo(player, clip.name, 0.0f);
    ASSERT(poseDigest(player.boneMatrices()) == atZero,
           "volver a 0 deberia reproducir exactamente la misma pose");

    ASSERT(!ap::scrubTo(player, "clip_que_no_existe", 0.5f),
           "un clip inexistente deberia fallar sin romper nada");
    ASSERT(player.paused() && std::fabs(player.speed() - 2.5f) < 1e-5f,
           "un scrub fallido tampoco deberia tocar el estado de reproduccion");
}

// El camino real de la previsualizacion: el panel escribe el componente y
// updateViewportAnimators lo reinyecta con syncWithComponent en el frame siguiente.
void testComponentDrivesPlayer()
{
    const std::string mesh = wolfMeshPath();
    if (mesh.empty() || !fs::exists(mesh)) return;

    dash::anim::AnimationSetCache cache;
    const dash::anim::AnimationSet& set = cache.load(mesh);
    if (!set.valid() || set.clips.size() < 2) {
        std::printf("[test] (salteado: hacen falta dos clips)\n");
        return;
    }

    dash::anim::AnimationPlayer player;
    player.setSkeleton(set.skeleton);
    for (const auto& clip : set.clips) player.addClip(clip);

    AnimationComponent component;
    component.clip = set.clips[0]->name;
    component.blendSeconds = 0.0f;
    player.syncWithComponent(component);
    ASSERT(player.currentClipName() == component.clip, "el player deberia tomar el clip");

    // Pause desde el panel: el pose no puede seguir avanzando.
    component.playing = false;
    player.syncWithComponent(component);
    player.update(0.5f);
    const uint64_t frozen = poseDigest(player.boneMatrices());
    player.update(0.5f);
    ASSERT(poseDigest(player.boneMatrices()) == frozen, "en pausa la pose no deberia moverse");

    // Play: vuelve a avanzar.
    component.playing = true;
    player.syncWithComponent(component);
    player.update(0.25f);
    ASSERT(poseDigest(player.boneMatrices()) != frozen, "al despausar la pose deberia avanzar");

    // Cambio de clip desde el panel.
    component.clip = set.clips[1]->name;
    player.syncWithComponent(component);
    ASSERT(player.currentClipName() == component.clip, "el player deberia seguir el cambio de clip");
}

} // namespace

int main()
{
    std::printf("=== test_animation_panel ===\n");
    testScrubOnRealWolf();
    testComponentDrivesPlayer();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
