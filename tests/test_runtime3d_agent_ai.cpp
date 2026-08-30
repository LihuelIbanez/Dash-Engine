// ═════════════════════════════════════════════════════════════════════════════
// test_runtime3d_agent_ai — capa pura de la simulacion de enemigos 3D
//
// Lo delicado es la histeresis: entrar a Chase/Attack usa un radio y salir usa
// otro mas grande, asi que un enemigo parado justo en el borde no oscila de
// estado todos los frames. Tambien se verifica que la separacion empuje solo
// cuando hay solapamiento y que el daño nunca baje de 1 ni la vida de 0.
//
// La parte de rodeo (surround slots) es lo que evita que todos converjan al
// mismo punto: se reparten posiciones en anillos alrededor del objetivo.
// ═════════════════════════════════════════════════════════════════════════════
#include "game/runtime3d/AgentAI.h"
#include "game/runtime3d/AgentAnimation.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace dash::runtime3d;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

static bool nearlyEqual(float a, float b, float eps = 1e-4f)
{
    return std::fabs(a - b) <= eps;
}

// ── Deteccion y persecucion ──────────────────────────────────────────────────
static void test_detection_and_chase()
{
    std::printf("  test_detection_and_chase\n");

    AgentStats stats;          // detection 9.0, attack 1.3
    AgentTimers timers;
    timers.idleRemaining = 5.0f;   // todavia no toca patrullar

    ASSERT(nextState(AgentState::Idle, 20.0f, stats, timers) == AgentState::Idle,
           "lejos y con timer vivo se queda quieto");
    ASSERT(nextState(AgentState::Idle, 5.0f, stats, timers) == AgentState::Chase,
           "dentro del radio de deteccion arranca a perseguir");
    ASSERT(nextState(AgentState::Chase, 1.0f, stats, timers) == AgentState::Attack,
           "dentro del radio de ataque pasa a atacar");
}

// ── Histeresis: salir cuesta mas que entrar ──────────────────────────────────
static void test_hysteresis()
{
    std::printf("  test_hysteresis\n");

    AgentStats stats;
    AgentTimers timers;
    timers.idleRemaining = 5.0f;

    // Justo por fuera del radio de ataque: sigue atacando, no vuelve a Chase.
    ASSERT(nextState(AgentState::Attack, stats.attackRadius * 1.1f, stats, timers)
               == AgentState::Attack,
           "un pelo fuera del radio de ataque no rompe el ataque");
    ASSERT(nextState(AgentState::Attack, stats.attackRadius * 1.6f, stats, timers)
               == AgentState::Chase,
           "bastante fuera del radio de ataque vuelve a perseguir");

    // Perder al jugador necesita 1.5x el radio de deteccion.
    ASSERT(nextState(AgentState::Chase, stats.detectionRadius * 1.2f, stats, timers)
               == AgentState::Chase,
           "salir apenas del radio de deteccion no cancela la persecucion");
    ASSERT(nextState(AgentState::Chase, stats.detectionRadius * 1.6f, stats, timers)
               == AgentState::Idle,
           "lo suficientemente lejos pierde al objetivo");
}

// ── Timers de Idle/Patrol ────────────────────────────────────────────────────
static void test_idle_patrol_cycle()
{
    std::printf("  test_idle_patrol_cycle\n");

    AgentStats stats;
    AgentTimers timers;

    timers.idleRemaining = 0.0f;
    ASSERT(nextState(AgentState::Idle, 50.0f, stats, timers) == AgentState::Patrol,
           "cuando se agota el timer de idle sale a patrullar");

    timers.patrolRemaining = 1.0f;
    ASSERT(nextState(AgentState::Patrol, 50.0f, stats, timers) == AgentState::Patrol,
           "con timer de patrulla vivo sigue patrullando");

    timers.patrolRemaining = 0.0f;
    ASSERT(nextState(AgentState::Patrol, 50.0f, stats, timers) == AgentState::Idle,
           "al agotarse la patrulla vuelve a idle");
}

// ── Los muertos no vuelven ───────────────────────────────────────────────────
static void test_dead_is_absorbing()
{
    std::printf("  test_dead_is_absorbing\n");

    AgentStats stats;
    AgentTimers timers;
    ASSERT(nextState(AgentState::Dead, 0.1f, stats, timers) == AgentState::Dead,
           "un agente muerto no vuelve a atacar aunque tenga al jugador encima");
}

// ── Daño ─────────────────────────────────────────────────────────────────────
static void test_damage_maths()
{
    std::printf("  test_damage_maths\n");

    ASSERT(rollDamage(10, 4) == 8, "la defensa mitiga la mitad de su valor");
    ASSERT(rollDamage(1, 100) == 1, "el daño nunca baja de 1");
    ASSERT(applyDamage(10, 3) == 7, "la vida baja por el daño recibido");
    ASSERT(applyDamage(2, 50) == 0, "la vida se satura en 0, no va a negativo");
    ASSERT(applyDamage(10, -5) == 10, "un daño negativo no cura");
}

// ── Separacion entre enemigos ────────────────────────────────────────────────
static void test_separation()
{
    std::printf("  test_separation\n");

    float px = 9.f, pz = 9.f;
    separationPush(0.0f, 0.0f, 5.0f, 0.0f, px, pz);
    ASSERT(nearlyEqual(px, 0.0f) && nearlyEqual(pz, 0.0f),
           "lejos no hay empuje");

    separationPush(0.0f, 0.0f, 0.4f, 0.0f, px, pz);
    ASSERT(px < 0.0f, "solapados, el empuje aleja en -X del vecino");
    ASSERT(nearlyEqual(pz, 0.0f), "sin componente en Z si el vecino esta en X puro");

    // Cuanto mas cerca, mas fuerte.
    float nearX = 0.f, nearZ = 0.f, farX = 0.f, farZ = 0.f;
    separationPush(0.0f, 0.0f, 0.1f, 0.0f, nearX, nearZ);
    separationPush(0.0f, 0.0f, 0.8f, 0.0f, farX, farZ);
    ASSERT(std::fabs(nearX) > std::fabs(farX), "mas cerca empuja mas fuerte");

    separationPush(3.0f, 3.0f, 3.0f, 3.0f, px, pz);
    ASSERT(!nearlyEqual(px, 0.0f) || !nearlyEqual(pz, 0.0f),
           "dos agentes exactamente encima igual se separan");
}

// ── Orientacion ──────────────────────────────────────────────────────────────
static void test_heading()
{
    std::printf("  test_heading\n");

    ASSERT(nearlyEqual(headingYawDeg(1.0f, 0.0f, 123.0f), 0.0f, 1e-3f),
           "+X mira a 0 grados");
    ASSERT(nearlyEqual(headingYawDeg(0.0f, 1.0f, 123.0f), 90.0f, 1e-3f),
           "+Z mira a 90 grados");
    ASSERT(nearlyEqual(headingYawDeg(0.0f, 0.0f, 123.0f), 123.0f),
           "sin movimiento conserva la orientacion previa");
}

// ── Retirada por vida baja ───────────────────────────────────────────────────
static void test_flee_on_low_health()
{
    std::printf("  test_flee_on_low_health\n");

    AgentStats stats;              // fleeHealthFraction 0.25, deteccion 9.0
    AgentTimers timers;
    timers.idleRemaining = 5.0f;

    ASSERT(nextState(AgentState::Chase, 3.0f, stats, timers, 0.9f) == AgentState::Chase,
           "con la vida alta sigue persiguiendo");
    ASSERT(nextState(AgentState::Chase, 3.0f, stats, timers, 0.2f) == AgentState::Flee,
           "por debajo del umbral de vida se retira");
    ASSERT(nextState(AgentState::Attack, 1.0f, stats, timers, 0.1f) == AgentState::Flee,
           "la retirada tambien corta un ataque en curso");

    // La retirada solo termina saliendo del radio de amenaza: sin eso, un agente
    // herido entraria a Chase y volveria a Flee un frame despues, para siempre.
    ASSERT(nextState(AgentState::Flee, 5.0f, stats, timers, 0.1f) == AgentState::Flee,
           "mientras el jugador este cerca sigue huyendo");
    ASSERT(nextState(AgentState::Flee, stats.detectionRadius * 1.6f, stats, timers, 0.1f)
               == AgentState::Idle,
           "una vez lejos deja de huir");
    ASSERT(nextState(AgentState::Idle, 3.0f, stats, timers, 0.1f) == AgentState::Flee,
           "herido y con el jugador cerca no vuelve a atacar: huye");

    stats.fleeHealthFraction = 0.0f;
    ASSERT(nextState(AgentState::Chase, 3.0f, stats, timers, 0.01f) == AgentState::Chase,
           "un arquetipo sin moral (esqueleto) nunca se retira");

    AgentStats dead;
    ASSERT(nextState(AgentState::Dead, 1.0f, dead, timers, 0.0f) == AgentState::Dead,
           "un muerto no huye");
}

// ── Patrullaje alrededor del spawn ───────────────────────────────────────────
static void test_patrol_points()
{
    std::printf("  test_patrol_points\n");

    const float spawnX = 20.0f, spawnZ = 40.0f;
    float prevX = 0.0f, prevZ = 0.0f;
    for (uint32_t i = 0; i < 8; ++i) {
        float px = 0.0f, pz = 0.0f;
        patrolPoint(spawnX, spawnZ, kPatrolRadius, i, px, pz);

        const float r = std::sqrt((px - spawnX) * (px - spawnX) +
                                  (pz - spawnZ) * (pz - spawnZ));
        ASSERT(nearlyEqual(r, kPatrolRadius, 1e-3f),
               "cada punto de patrulla cae sobre el anillo del spawn");
        if (i > 0) {
            ASSERT(!nearlyEqual(px, prevX, 1e-2f) || !nearlyEqual(pz, prevZ, 1e-2f),
                   "dos waypoints seguidos nunca son el mismo");
        }
        prevX = px;
        prevZ = pz;
    }

    float ax = 0.0f, az = 0.0f, bx = 0.0f, bz = 0.0f;
    patrolPoint(spawnX, spawnZ, kPatrolRadius, 3, ax, az);
    patrolPoint(spawnX, spawnZ, kPatrolRadius, 3, bx, bz);
    ASSERT(nearlyEqual(ax, bx) && nearlyEqual(az, bz),
           "la ruta es determinista: mismo indice, mismo punto");
}

// ── Rodeo: reparto de posiciones alrededor del objetivo ──────────────────────
static void test_surround_slot_offsets()
{
    std::printf("  test_surround_slot_offsets\n");

    SurroundRings rings;
    float ox = 9.0f, oz = 9.0f;

    ASSERT(!surroundSlotOffset(rings, -1, ox, oz) && nearlyEqual(ox, 0.0f) && nearlyEqual(oz, 0.0f),
           "sin slot asignado el offset es cero (se camina al objetivo)");
    ASSERT(!surroundSlotOffset(rings, rings.totalSlots(), ox, oz),
           "un indice fuera de rango tampoco produce offset");

    for (int s = 0; s < rings.innerSlots; ++s) {
        ASSERT(surroundSlotOffset(rings, s, ox, oz), "slot interno valido");
        ASSERT(nearlyEqual(std::sqrt(ox * ox + oz * oz), rings.innerRadius, 1e-3f),
               "los slots internos caen sobre el anillo de melee");
    }
    for (int s = rings.innerSlots; s < rings.totalSlots(); ++s) {
        ASSERT(surroundSlotOffset(rings, s, ox, oz), "slot externo valido");
        ASSERT(nearlyEqual(std::sqrt(ox * ox + oz * oz), rings.outerRadius, 1e-3f),
               "los slots externos caen sobre el anillo de espera");
    }

    // Adyacentes en el anillo interno tienen que quedar mas separados que el
    // radio de separacion, o la propia formacion generaria empujones.
    float x0 = 0.0f, z0 = 0.0f, x1 = 0.0f, z1 = 0.0f;
    surroundSlotOffset(rings, 0, x0, z0);
    surroundSlotOffset(rings, 1, x1, z1);
    const float chord = std::sqrt((x1 - x0) * (x1 - x0) + (z1 - z0) * (z1 - z0));
    ASSERT(chord > kMinSeparation, "dos slots contiguos no se pisan entre si");

    // Y tienen que estar dentro del alcance de ataque, si no nadie pega nunca.
    AgentStats stats;
    ASSERT(rings.innerRadius < stats.attackRadius,
           "el anillo de melee esta dentro del radio de ataque");
}

static void test_surround_slot_assignment()
{
    std::printf("  test_surround_slot_assignment\n");

    SurroundRings rings;
    const float tx = 32.0f, tz = 32.0f;

    // Cinco agentes llegando todos desde el mismo lado: el caso que antes los
    // apilaba sobre el jugador.
    std::vector<SurroundActor> actors = {
        {36.0f, 32.0f, -1}, {36.5f, 32.4f, -1}, {37.0f, 31.6f, -1},
        {36.2f, 33.0f, -1}, {35.8f, 31.0f, -1},
    };

    const std::vector<int> slots = assignSurroundSlots(actors, tx, tz, rings);
    ASSERT(slots.size() == actors.size(), "un slot por agente");

    bool allInner = true, allDistinct = true;
    for (size_t i = 0; i < slots.size(); ++i) {
        if (slots[i] < 0 || slots[i] >= rings.innerSlots) allInner = false;
        for (size_t j = i + 1; j < slots.size(); ++j) {
            if (slots[i] == slots[j]) allDistinct = false;
        }
    }
    ASSERT(allDistinct, "dos agentes nunca comparten la misma posicion");
    ASSERT(allInner, "con anillo interno libre nadie se queda esperando afuera");

    // La distancia minima entre puestos ocupados tiene que superar el radio de
    // separacion: eso es literalmente "no se apilan".
    float closest = 1e9f;
    for (size_t i = 0; i < slots.size(); ++i) {
        float ax = 0.0f, az = 0.0f;
        surroundSlotOffset(rings, slots[i], ax, az);
        for (size_t j = i + 1; j < slots.size(); ++j) {
            float bx = 0.0f, bz = 0.0f;
            surroundSlotOffset(rings, slots[j], bx, bz);
            closest = std::min(closest, std::sqrt((ax - bx) * (ax - bx) +
                                                  (az - bz) * (az - bz)));
        }
    }
    ASSERT(closest > kMinSeparation, "los destinos asignados no se solapan");

    // Mas agentes que cupos en el anillo interno: los que sobran esperan afuera.
    std::vector<SurroundActor> crowd;
    for (int i = 0; i < rings.innerSlots + 3; ++i) {
        crowd.push_back({36.0f + static_cast<float>(i) * 0.1f, 32.0f, -1});
    }
    const std::vector<int> crowdSlots = assignSurroundSlots(crowd, tx, tz, rings);
    int inner = 0, outer = 0;
    for (int s : crowdSlots) {
        if (s < 0) continue;
        (s < rings.innerSlots ? inner : outer)++;
    }
    ASSERT(inner == rings.innerSlots, "el anillo de melee se llena primero");
    ASSERT(outer == 3, "los que sobran van al anillo externo");

    // Sin cupos, el agente queda sin slot y vuelve al comportamiento simple.
    std::vector<SurroundActor> horde(static_cast<size_t>(rings.totalSlots()) + 2,
                                     SurroundActor{36.0f, 32.0f, -1});
    const std::vector<int> hordeSlots = assignSurroundSlots(horde, tx, tz, rings);
    int unassigned = 0;
    for (int s : hordeSlots) if (s < 0) ++unassigned;
    ASSERT(unassigned == 2, "con los anillos llenos los ultimos no reciben puesto");
}

static void test_surround_slot_stickiness()
{
    std::printf("  test_surround_slot_stickiness\n");

    SurroundRings rings;
    std::vector<SurroundActor> actors = {
        {33.1f, 32.0f, 0},   // ya parado en su slot 0
        {32.55f, 32.95f, 1}, // ya parado en su slot 1
    };
    const std::vector<int> slots = assignSurroundSlots(actors, 32.0f, 32.0f, rings);
    ASSERT(slots[0] == 0 && slots[1] == 1,
           "un reparto nuevo no le saca el puesto a quien ya lo ocupa");

    // Si el puesto preferido ya no existe, se reasigna sin romperse.
    std::vector<SurroundActor> moved = {{33.1f, 32.0f, 999}};
    const std::vector<int> movedSlots = assignSurroundSlots(moved, 32.0f, 32.0f, rings);
    ASSERT(movedSlots[0] >= 0 && movedSlots[0] < rings.innerSlots,
           "un slot preferido invalido cae al mas cercano libre");
}

// ── Acantilados ──────────────────────────────────────────────────────────────
static void test_cliff_step_rule()
{
    std::printf("  test_cliff_step_rule\n");

    ASSERT(cliffStepAllowed(3, 3), "moverse dentro del mismo nivel siempre se puede");
    ASSERT(!cliffStepAllowed(0, 1), "por defecto no se escala un nivel de acantilado");
    ASSERT(!cliffStepAllowed(1, 0), "por defecto tampoco se salta hacia abajo");
    ASSERT(!cliffStepAllowed(0, 5), "menos todavia varios niveles de una");

    ASSERT(cliffStepAllowed(0, 1, /*maxClimb=*/1, /*maxDrop=*/0),
           "un arquetipo trepador si puede subir un nivel");
    ASSERT(!cliffStepAllowed(0, 2, 1, 0), "pero no dos");
    ASSERT(cliffStepAllowed(2, 1, 0, 1), "y una caida permitida se acepta");
}

// ── Cableado de animacion ────────────────────────────────────────────────────
static void test_animation_parameters()
{
    std::printf("  test_animation_parameters\n");

    dash::anim::StateMachineRuntime runtime;
    runtime.setMachine(sharedEnemyStateMachine());
    ASSERT(runtime.active(), "la maquina por defecto de enemigos es valida");

    runtime.step(0.0f);   // paso de entrada
    ASSERT(runtime.currentState() == "Idle", "arranca quieto");

    AgentAnimSignals moving;
    moving.speed = 2.5f;
    applyAgentAnimation(moving, runtime.parameters());
    ASSERT(nearlyEqual(runtime.parameters().getFloat(kAnimParamSpeed), 2.5f),
           "la velocidad del agente llega como parametro float");
    runtime.step(0.0f);
    ASSERT(runtime.currentState() == "Walk", "moverse cambia el estado de animacion");

    AgentAnimSignals swing;
    swing.speed = 2.5f;
    swing.attackStarted = true;
    applyAgentAnimation(swing, runtime.parameters());
    ASSERT(runtime.parameters().isTriggerSet(kAnimParamAttack), "el golpe deja el trigger");
    runtime.step(0.0f);
    ASSERT(runtime.currentState() == "Attack", "el trigger de ataque cambia la animacion");
    ASSERT(!runtime.parameters().isTriggerSet(kAnimParamAttack),
           "la transicion consume el trigger, no se repite sola");

    AgentAnimSignals death;
    death.died = true;
    applyAgentAnimation(death, runtime.parameters());
    runtime.step(0.0f);
    ASSERT(runtime.currentState() == "Death", "morir manda a la animacion de muerte");

    // Un cadaver no vuelve a caminar aunque queden parametros dando vueltas.
    AgentAnimSignals ghost;
    ghost.speed = 3.0f;
    ghost.attackStarted = true;
    applyAgentAnimation(ghost, runtime.parameters());
    runtime.step(1.0f);
    ASSERT(runtime.currentState() == "Death", "de la muerte no se sale");

    ASSERT(stateWalks(AgentState::Chase) && stateWalks(AgentState::Flee) &&
           stateWalks(AgentState::Patrol),
           "perseguir, huir y patrullar son estados con locomocion");
    ASSERT(!stateWalks(AgentState::Attack) && !stateWalks(AgentState::Idle) &&
           !stateWalks(AgentState::Dead),
           "atacar, esperar y morir no");
}

int main()
{
    std::printf("test_runtime3d_agent_ai\n");

    test_detection_and_chase();
    test_hysteresis();
    test_idle_patrol_cycle();
    test_dead_is_absorbing();
    test_damage_maths();
    test_separation();
    test_heading();
    test_flee_on_low_health();
    test_patrol_points();
    test_surround_slot_offsets();
    test_surround_slot_assignment();
    test_surround_slot_stickiness();
    test_cliff_step_rule();
    test_animation_parameters();

    std::printf("  %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
