// ═════════════════════════════════════════════════════════════════════════════
// test_playback_controller — transporte de Play mode (pausa / step / velocidad)
//
// Lo delicado es el contador de step: el runtime avanza un frame cuando ve un
// valor que todavia no consumio, asi que el editor tiene que incrementarlo una
// vez por pedido y no tocarlo al pausar o reanudar.
// ═════════════════════════════════════════════════════════════════════════════
#include "editor/playmode/PlaybackController.h"

#include <cmath>
#include <cstdio>

using dash::playmode::PlaybackController;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

static bool nearlyEqual(float a, float b, float eps = 1e-5f)
{
    return std::fabs(a - b) <= eps;
}

// ── Estado inicial ───────────────────────────────────────────────────────────
static void test_defaults()
{
    std::printf("  test_defaults\n");

    PlaybackController pc;
    ASSERT(!pc.paused(), "arranca corriendo");
    ASSERT(nearlyEqual(pc.timeScale(), 1.0f), "arranca a velocidad normal");
    ASSERT(pc.stepSerial() == 0u, "el contador de step arranca en 0");
    ASSERT(!pc.consumeDirty(), "sin cambios no hay nada que escribir");
}

// ── El contador de step ──────────────────────────────────────────────────────
static void test_step_increments_serial()
{
    std::printf("  test_step_increments_serial\n");

    PlaybackController pc;
    pc.setPaused(true);
    (void)pc.consumeDirty();

    pc.requestStep();
    ASSERT(pc.stepSerial() == 1u, "un step incrementa el contador");
    ASSERT(pc.consumeDirty(), "el step marca el estado como pendiente de escritura");

    pc.requestStep();
    pc.requestStep();
    ASSERT(pc.stepSerial() == 3u, "cada pedido suma exactamente uno");
    ASSERT(pc.paused(), "pedir step no reanuda la ejecucion");
}

// ── Pausa / reanudacion ──────────────────────────────────────────────────────
static void test_pause_does_not_touch_serial()
{
    std::printf("  test_pause_does_not_touch_serial\n");

    PlaybackController pc;
    pc.requestStep();
    pc.requestStep();
    const uint32_t serial = pc.stepSerial();

    pc.setPaused(true);
    ASSERT(pc.paused(), "setPaused(true) pausa");
    ASSERT(pc.stepSerial() == serial, "pausar no mueve el contador");

    pc.togglePause();
    ASSERT(!pc.paused(), "togglePause reanuda");
    ASSERT(pc.stepSerial() == serial, "reanudar no mueve el contador");

    pc.togglePause();
    ASSERT(pc.paused(), "togglePause vuelve a pausar");
    ASSERT(pc.stepSerial() == serial, "el ciclo completo deja el contador intacto");

    // Un setPaused redundante no ensucia el estado: evita reescribir el archivo.
    (void)pc.consumeDirty();
    pc.setPaused(true);
    ASSERT(!pc.consumeDirty(), "pausar dos veces no genera una escritura nueva");
}

// ── timeScale ────────────────────────────────────────────────────────────────
static void test_time_scale_clamped()
{
    std::printf("  test_time_scale_clamped\n");

    PlaybackController pc;
    pc.setTimeScale(0.25f);
    ASSERT(nearlyEqual(pc.timeScale(), 0.25f), "camara lenta a 0.25x");

    pc.setTimeScale(2.0f);
    ASSERT(nearlyEqual(pc.timeScale(), 2.0f), "acelerado a 2x");

    pc.setTimeScale(0.0f);
    ASSERT(nearlyEqual(pc.timeScale(), 0.0f), "0x es un valor valido (congelado)");

    // Un dt negativo correria la simulacion hacia atras: se clampea a 0.
    pc.setTimeScale(-1.0f);
    ASSERT(nearlyEqual(pc.timeScale(), 0.0f), "una velocidad negativa se clampea a 0");

    pc.setTimeScale(-0.001f);
    ASSERT(pc.timeScale() >= 0.0f, "nunca queda negativo");

    const uint32_t serial = pc.stepSerial();
    ASSERT(serial == 0u, "cambiar la velocidad no pide steps");
}

// ── reset ────────────────────────────────────────────────────────────────────
static void test_reset_restores_defaults()
{
    std::printf("  test_reset_restores_defaults\n");

    PlaybackController pc;
    pc.setPaused(true);
    pc.setTimeScale(0.25f);
    pc.requestStep();
    pc.requestStep();

    pc.reset();
    ASSERT(!pc.paused(), "reset despausa");
    ASSERT(nearlyEqual(pc.timeScale(), 1.0f), "reset vuelve a 1x");
    ASSERT(pc.stepSerial() == 0u, "reset limpia el contador de step");
    ASSERT(pc.consumeDirty(), "el reset se tiene que propagar al runtime");

    PlaybackController fresh;
    ASSERT(pc.paused() == fresh.paused(), "queda igual que uno recien creado (pausa)");
    ASSERT(nearlyEqual(pc.timeScale(), fresh.timeScale()), "queda igual que uno recien creado (velocidad)");
    ASSERT(pc.stepSerial() == fresh.stepSerial(), "queda igual que uno recien creado (contador)");
}

// ── Bandera de escritura ─────────────────────────────────────────────────────
static void test_dirty_flag_consumed_once()
{
    std::printf("  test_dirty_flag_consumed_once\n");

    PlaybackController pc;
    pc.setTimeScale(0.5f);
    ASSERT(pc.consumeDirty(), "el cambio de velocidad marca dirty");
    ASSERT(!pc.consumeDirty(), "la bandera se consume una sola vez");
}

int main()
{
    std::printf("=== test_playback_controller ===\n");

    test_defaults();
    test_step_increments_serial();
    test_pause_does_not_touch_serial();
    test_time_scale_clamped();
    test_reset_restores_defaults();
    test_dirty_flag_consumed_once();

    std::printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
