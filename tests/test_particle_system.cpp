// ═════════════════════════════════════════════════════════════════════════════
// test_particle_system — capa pura de los efectos de combate
//
// Lo que se verifica es lo que no se ve desde el renderer: que las particulas
// realmente se integren (gravedad, drag, piso), que MUERAN al llegar a su vida
// (si el contador de vivas fuera constante o creciera para siempre, el sistema
// estaria roto aunque dibujara), y que el pool nunca pase su capacidad.
//
// Tambien entra la parte de presentacion que decide COMO se ven: la seleccion
// de frame del atlas a lo largo de la vida, el reparto entre las dos colas de
// blending, y el criterio de prioridad de las luces de vida corta cuando hay
// mas flashes que slots libres en el UBO de 8 luces.
// ═════════════════════════════════════════════════════════════════════════════
#include "rendering/vfx/CameraFeedback.h"
#include "rendering/vfx/CombatVfx.h"
#include "rendering/vfx/ParticleSystem.h"
#include "rendering/vfx/TransientLights.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace dash::vfx;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

static EmitParams simpleBurst(int count, float life)
{
    EmitParams p;
    p.x = 0.f; p.y = 10.f; p.z = 0.f;
    p.radius = 0.f;
    p.dirX = 0.f; p.dirY = 1.f; p.dirZ = 0.f;
    p.spread = 0.f;
    p.speedMin = p.speedMax = 0.f;
    p.lifeMin = p.lifeMax = life;
    p.gravity = 0.f;
    p.drag = 0.f;
    p.spinMax = 0.f;
    p.count = count;
    return p;
}

// ── Nacimiento y muerte ──────────────────────────────────────────────────────
static void test_life_cycle()
{
    std::printf("  test_life_cycle\n");

    ParticleSystem ps(64);
    ASSERT(ps.aliveCount() == 0, "arranca vacio");

    const int spawned = ps.emit(simpleBurst(10, 1.0f));
    ASSERT(spawned == 10, "emite lo pedido");
    ASSERT(ps.aliveCount() == 10, "las 10 quedan vivas");
    ASSERT(ps.totalEmitted() == 10, "el contador acumulado sube");

    for (int i = 0; i < 9; ++i) ps.update(0.1f);
    ASSERT(ps.aliveCount() == 10, "a 0.9s de vida 1.0s todavia no muere ninguna");

    ps.update(0.2f);
    ASSERT(ps.aliveCount() == 0, "pasada la vida el pool se vacia");
    ASSERT(ps.totalRetired() == 10, "todas se contabilizan como retiradas");
}

// ── El pool tiene techo ──────────────────────────────────────────────────────
static void test_capacity_clamp()
{
    std::printf("  test_capacity_clamp\n");

    ParticleSystem ps(16);
    const int spawned = ps.emit(simpleBurst(100, 1.0f));
    ASSERT(spawned == 16, "la rafaga se trunca en la capacidad");
    ASSERT(ps.aliveCount() == 16, "no se pasa del pool");

    ASSERT(ps.emit(simpleBurst(5, 1.0f)) == 0, "con el pool lleno no entra nada");
}

// ── Integracion: gravedad, drag y piso ───────────────────────────────────────
static void test_integration()
{
    std::printf("  test_integration\n");

    ParticleSystem ps(8);
    EmitParams p = simpleBurst(1, 5.0f);
    p.y = 4.0f;
    p.speedMin = p.speedMax = 0.f;
    p.gravity = -10.0f;
    p.drag = 0.f;
    ps.emit(p);

    ps.update(0.5f);
    const Particle& q = ps.particles()[0];
    // Euler semi-implicito: v = -5 tras 0.5s, y = 4 - 5*0.5 = 1.5
    ASSERT(std::fabs(q.vy + 5.0f) < 1e-4f, "la gravedad integra la velocidad");
    ASSERT(std::fabs(q.py - 1.5f) < 1e-4f, "la velocidad integra la posicion");

    // El drag frena: con drag alto la velocidad tiene que caer contra el caso libre.
    ParticleSystem dragged(8);
    EmitParams d = p;
    d.drag = 4.0f;
    dragged.emit(d);
    dragged.update(0.5f);
    ASSERT(std::fabs(dragged.particles()[0].vy) < std::fabs(q.vy),
           "el drag deja la particula mas lenta que sin drag");
}

static void test_floor()
{
    std::printf("  test_floor\n");

    ParticleSystem ps(8);
    EmitParams p = simpleBurst(1, 5.0f);
    p.y = 1.0f;
    p.gravity = -20.0f;
    p.floorY = 0.25f;
    ps.emit(p);

    for (int i = 0; i < 20; ++i) ps.update(0.05f);

    const Particle& q = ps.particles()[0];
    ASSERT(q.py >= 0.25f - 1e-4f, "no atraviesa el piso");
    ASSERT(std::fabs(q.py - 0.25f) < 1e-4f, "se queda apoyada exactamente en el piso");
    ASSERT(std::fabs(q.vy) < 1e-6f, "y sin velocidad, para que se desvanezca en el lugar");
    ASSERT(ps.aliveCount() == 1, "seguir viva es lo que la deja terminar su fade");
}

// ── Atlas: el frame avanza con la vida y no se sale de la fila ───────────────
static void test_atlas_frames()
{
    std::printf("  test_atlas_frames\n");

    Particle q;
    q.life = 1.0f;
    q.frameFirst = kFrameSplat;   // fila 3
    q.frameCount = 4;
    q.frameRate = 0.f;            // reparte los 4 cuadros en toda la vida

    ASSERT(ParticleSystem::atlasFrame(q, 0.0f) == kFrameSplat, "arranca en el primer cuadro");
    ASSERT(ParticleSystem::atlasFrame(q, 0.30f) == kFrameSplat + 1, "avanza a un cuarto de vida");
    ASSERT(ParticleSystem::atlasFrame(q, 0.99f) == kFrameSplat + 3, "termina en el ultimo");
    ASSERT(ParticleSystem::atlasFrame(q, 1.0f) == kFrameSplat + 3,
           "en t=1 no se pasa de la fila (seria otro sprite)");

    Particle single;
    single.frameCount = 1;
    single.frameFirst = kFrameRing;
    ASSERT(ParticleSystem::atlasFrame(single, 0.7f) == kFrameRing,
           "sin animacion siempre es el mismo cuadro");
}

// ── Las dos colas de blending y el rect UV ───────────────────────────────────
static void test_instance_batches()
{
    std::printf("  test_instance_batches\n");

    ParticleSystem ps(64);
    ps.emit(bloodSplatter(1.f, 2.f, 3.f, 1.f, 0.f, /*damage=*/12, /*groundY=*/0.f));
    ps.emit(impactSparks(1.f, 2.f, 3.f, 1.f, 0.f, /*damage=*/12));

    std::vector<ParticleInstance> alpha, additive;
    ps.buildInstances(alpha, additive, kAtlasCols, kAtlasRows);

    ASSERT(!alpha.empty(), "la sangre va por la cola alpha");
    ASSERT(!additive.empty(), "las chispas van por la cola aditiva");
    ASSERT(alpha.size() + additive.size() == ps.aliveCount(),
           "toda particula viva termina en exactamente una cola");

    // El rect tiene que caber dentro de su celda del atlas, o el filtrado
    // bilineal trae pixeles del cuadro vecino.
    const float cellU = 1.0f / static_cast<float>(kAtlasCols);
    const float cellV = 1.0f / static_cast<float>(kAtlasRows);
    for (const ParticleInstance& inst : alpha) {
        ASSERT(inst.uvRect[2] > 0.f && inst.uvRect[2] < cellU, "el ancho UV entra en la celda");
        ASSERT(inst.uvRect[3] > 0.f && inst.uvRect[3] < cellV, "el alto UV entra en la celda");
        ASSERT(inst.uvRect[0] >= 0.f && inst.uvRect[0] + inst.uvRect[2] <= 1.0f,
               "el rect no se sale del atlas en U");
        ASSERT(inst.uvRect[1] >= 0.f && inst.uvRect[1] + inst.uvRect[3] <= 1.0f,
               "el rect no se sale del atlas en V");
    }

    // Las chispas emiten por encima de 1.0 a proposito: el ACES del tonemap es
    // el que las trae de vuelta. Si alguien las clampeara, dejan de brillar.
    bool anyHdr = false;
    for (const ParticleInstance& inst : additive) {
        if (inst.color[0] > 1.0f) anyHdr = true;
    }
    ASSERT(anyHdr, "el preset aditivo conserva valores HDR");
}

// ── El tamano y el color interpolan a lo largo de la vida ────────────────────
static void test_size_and_color_ramp()
{
    std::printf("  test_size_and_color_ramp\n");

    ParticleSystem ps(8);
    EmitParams p = simpleBurst(1, 1.0f);
    p.sizeBegin = 1.0f;
    p.sizeEnd = 0.0f;
    p.colorBegin[3] = 1.0f;
    p.colorEnd[3] = 0.0f;
    ps.emit(p);

    std::vector<ParticleInstance> a, b;
    ps.buildInstances(a, b, kAtlasCols, kAtlasRows);
    const float size0 = a[0].centerSize[3];
    const float alpha0 = a[0].color[3];

    ps.update(0.5f);
    ps.buildInstances(a, b, kAtlasCols, kAtlasRows);
    ASSERT(a[0].centerSize[3] < size0, "el tamano cae hacia sizeEnd");
    ASSERT(a[0].color[3] < alpha0, "el alpha cae hacia colorEnd");
    ASSERT(std::fabs(a[0].color[3] - 0.5f) < 1e-4f, "a mitad de vida el alpha es la mitad");
}

// ── Luces de vida corta: decaimiento y prioridad ─────────────────────────────
static void test_transient_lights()
{
    std::printf("  test_transient_lights\n");

    TransientLights lights(4);
    TransientLight l;
    l.peakIntensity = 10.0f;
    l.life = 0.2f;
    l.x = 0.f; l.y = 0.f; l.z = 0.f;
    lights.spawn(l);

    ASSERT(lights.liveCount() == 1, "la luz entra al pool");
    ASSERT(std::fabs(TransientLights::currentIntensity(lights.lights()[0]) - 10.0f) < 1e-4f,
           "arranca en su pico");

    lights.update(0.1f);
    const float half = TransientLights::currentIntensity(lights.lights()[0]);
    ASSERT(half < 10.0f && half > 0.0f, "a mitad de vida ya decayo");
    ASSERT(std::fabs(half - 2.5f) < 1e-3f, "el decaimiento es cuadratico, no lineal");

    lights.update(0.2f);
    ASSERT(lights.liveCount() == 0, "pasada la vida se libera el slot");

    // Prioridad: con menos slots que flashes gana la mas cercana a la camara.
    TransientLights many(8);
    TransientLight near = l; near.x = 1.0f;  near.peakIntensity = 5.0f;
    TransientLight far  = l; far.x  = 50.0f; far.peakIntensity  = 5.0f;
    many.spawn(far);
    many.spawn(near);

    std::vector<std::size_t> pick;
    ASSERT(many.select(0.f, 0.f, 0.f, 1, pick) == 1, "solo entra una en el slot libre");
    ASSERT(std::fabs(many.lights()[pick[0]].x - 1.0f) < 1e-4f,
           "la elegida es la cercana a la camara");

    // A igual distancia manda la intensidad.
    TransientLights tie(8);
    TransientLight dim = l;    dim.x = 3.0f;   dim.peakIntensity = 1.0f;
    TransientLight bright = l; bright.x = 3.0f; bright.peakIntensity = 20.0f;
    tie.spawn(dim);
    tie.spawn(bright);
    tie.select(0.f, 0.f, 0.f, 1, pick);
    ASSERT(std::fabs(tie.lights()[pick[0]].peakIntensity - 20.0f) < 1e-4f,
           "a igual distancia gana la mas intensa");

    // El pool lleno recicla la mas vieja, no la mas nueva.
    TransientLights full(2);
    full.spawn(l);
    full.update(0.05f);
    full.spawn(l);
    full.spawn(l);
    ASSERT(full.liveCount() == 2, "el pool no crece");
    ASSERT(full.totalSpawned() == 3, "pero registra los tres pedidos");
}

// ── Sacudida de camara y tinte de golpe ──────────────────────────────────────
static void test_camera_feedback()
{
    std::printf("  test_camera_feedback\n");

    ScreenShake shake;
    float off[3];
    shake.offset(1.0f, off);
    ASSERT(off[0] == 0.f && off[1] == 0.f && off[2] == 0.f, "sin trauma no hay sacudida");

    shake.addTrauma(0.5f);
    ASSERT(shake.active(), "un golpe la activa");
    shake.update(0.016f);
    shake.offset(1.0f, off);
    const float mag = std::fabs(off[0]) + std::fabs(off[1]) + std::fabs(off[2]);
    ASSERT(mag > 0.f, "con trauma hay desplazamiento");

    shake.addTrauma(10.0f);
    ASSERT(shake.trauma() <= 1.0f, "el trauma se satura en 1");

    for (int i = 0; i < 120; ++i) shake.update(0.016f);
    ASSERT(!shake.active(), "el trauma se apaga solo");

    HitFlash flash;
    ASSERT(!flash.active(), "sin danio no hay tinte");
    flash.trigger(0.5f);
    float rgb[3];
    flash.packPremultiplied(rgb);
    // El shader recupera la fuerza como la componente maxima: el tinte tiene
    // que estar normalizado a 1 o el tonemap lee otra intensidad.
    const float maxc = std::max(rgb[0], std::max(rgb[1], rgb[2]));
    ASSERT(std::fabs(maxc - flash.strength()) < 1e-5f,
           "la componente maxima del premultiplicado es la fuerza");

    flash.trigger(0.1f);
    ASSERT(std::fabs(flash.strength() - 0.5f) < 1e-5f,
           "un golpe menor no pisa uno mayor todavia vigente");

    for (int i = 0; i < 120; ++i) flash.update(0.016f);
    ASSERT(!flash.active(), "el tinte se desvanece solo");
}

// ── Los presets producen algo dibujable ──────────────────────────────────────
static void test_presets()
{
    std::printf("  test_presets\n");

    ParticleSystem ps(2048);
    ps.emit(deathShockwave(0.f, 1.f, 0.f));
    ps.emit(deathGibs(0.f, 1.f, 0.f, 0.f));
    ps.emit(deathSmoke(0.f, 1.f, 0.f));
    ASSERT(ps.aliveCount() > 30, "una muerte llena la pantalla");

    // Con 200 frames de 16 ms todo lo de una muerte tiene que haber muerto.
    for (int i = 0; i < 200; ++i) ps.update(0.016f);
    ASSERT(ps.aliveCount() == 0, "nada queda vivo para siempre");

    const EmitParams weak = bloodSplatter(0.f, 0.f, 0.f, 1.f, 0.f, 2, 0.f);
    const EmitParams heavy = bloodSplatter(0.f, 0.f, 0.f, 1.f, 0.f, 40, 0.f);
    ASSERT(heavy.count > weak.count, "un golpe mas fuerte salpica mas");
    ASSERT(heavy.speedMax > weak.speedMax, "y salpica mas lejos");
}

int main()
{
    std::printf("[test_particle_system]\n");

    test_life_cycle();
    test_capacity_clamp();
    test_integration();
    test_floor();
    test_atlas_frames();
    test_instance_batches();
    test_size_and_color_ramp();
    test_transient_lights();
    test_camera_feedback();
    test_presets();

    std::printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
