// ═════════════════════════════════════════════════════════════════════════════
// test_state_machine_panel — logica pura del editor de maquinas de estado
//
// El dibujado con ImGui no se puede testear headless, asi que StateMachinePanel
// deja fuera de la UI todo lo que decide algo: la validacion contra los clips
// que el modelo realmente trae, la alcanzabilidad del grafo y los renombres y
// bajas que hay que propagar a transiciones y condiciones. Se cubre ademas el
// ida y vuelta a .animsm.json, que es el formato que el panel guarda.
// ═════════════════════════════════════════════════════════════════════════════
#include "panels/StateMachinePanel.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace sm = dash::editor::animsm;
namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

namespace {

fs::path tempDir()
{
    static const fs::path dir = fs::temp_directory_path() / "dash_test_state_machine_panel";
    fs::create_directories(dir);
    return dir;
}

const std::vector<std::string> kClips = {"Idle", "Run"};

// idle --(speed > 0.1)--> run --(speed < 0.1)--> idle
dash::anim::AnimationStateMachine makeMachine()
{
    dash::anim::AnimationStateMachine machine;
    machine.name = "wolf";
    machine.initialState = "idle";

    dash::anim::AnimationParameter speed;
    speed.name = "speed";
    speed.type = dash::anim::ParamType::Float;
    machine.parameters.push_back(speed);

    dash::anim::AnimationState idle;
    idle.name = "idle";
    idle.clip = "Idle";
    machine.states.push_back(idle);

    dash::anim::AnimationState run;
    run.name = "run";
    run.clip = "Run";
    machine.states.push_back(run);

    dash::anim::AnimationTransition toRun;
    toRun.from = "idle";
    toRun.to = "run";
    toRun.conditions.push_back({"speed", dash::anim::ConditionOp::Greater, 0.1f});
    machine.transitions.push_back(toRun);

    dash::anim::AnimationTransition toIdle;
    toIdle.from = "run";
    toIdle.to = "idle";
    toIdle.conditions.push_back({"speed", dash::anim::ConditionOp::Less, 0.1f});
    machine.transitions.push_back(toIdle);

    return machine;
}

bool hasErrorContaining(const std::vector<sm::Issue>& issues, const std::string& needle)
{
    for (const sm::Issue& issue : issues) {
        if (issue.severity != sm::IssueSeverity::Error) continue;
        if (issue.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

bool hasWarningContaining(const std::vector<sm::Issue>& issues, const std::string& needle)
{
    for (const sm::Issue& issue : issues) {
        if (issue.severity != sm::IssueSeverity::Warning) continue;
        if (issue.message.find(needle) != std::string::npos) return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
void testValidMachineIsClean()
{
    std::printf("[test] maquina valida sin problemas\n");
    const std::vector<sm::Issue> issues = sm::validate(makeMachine(), kClips);
    for (const sm::Issue& issue : issues)
        std::fprintf(stderr, "  unexpected: %s\n", issue.message.c_str());
    ASSERT(issues.empty(), "una maquina bien formada no deberia reportar nada");
}

void testMissingClip()
{
    std::printf("[test] estado que apunta a un clip inexistente\n");
    dash::anim::AnimationStateMachine machine = makeMachine();
    machine.states[1].clip = "Sprint";

    const std::vector<sm::Issue> issues = sm::validate(machine, kClips);
    ASSERT(hasErrorContaining(issues, "Sprint"), "el clip faltante deberia ser un error");
    ASSERT(sm::hasErrors(issues), "hasErrors deberia dar true");

    // Sin lista de clips no hay contra que validar, y no se reporta nada.
    const std::vector<sm::Issue> unchecked = sm::validate(machine, {});
    ASSERT(!hasErrorContaining(unchecked, "Sprint"),
           "sin lista de clips el nombre no se valida");
}

void testDanglingTransition()
{
    std::printf("[test] transicion a un estado que no existe\n");
    dash::anim::AnimationStateMachine machine = makeMachine();
    machine.transitions[0].to = "attack";

    const std::vector<sm::Issue> issues = sm::validate(machine, kClips);
    ASSERT(hasErrorContaining(issues, "attack"), "el destino colgado deberia ser un error");

    dash::anim::AnimationStateMachine orphanSource = makeMachine();
    orphanSource.transitions[0].from = "ghost";
    const std::vector<sm::Issue> sourceIssues = sm::validate(orphanSource, kClips);
    ASSERT(hasErrorContaining(sourceIssues, "ghost"), "el origen colgado deberia ser un error");
}

void testConditionOnUndeclaredParameter()
{
    std::printf("[test] condicion sobre un parametro no declarado\n");
    dash::anim::AnimationStateMachine machine = makeMachine();
    machine.transitions[0].conditions[0].parameter = "velocidad";

    const std::vector<sm::Issue> issues = sm::validate(machine, kClips);
    ASSERT(hasErrorContaining(issues, "velocidad"),
           "una condicion sobre un parametro inexistente deberia ser un error");
}

void testConditionOpTypeMismatch()
{
    std::printf("[test] operador que no corresponde al tipo del parametro\n");
    dash::anim::AnimationStateMachine machine = makeMachine();
    machine.transitions[0].conditions[0].op = dash::anim::ConditionOp::IsTrue;

    const std::vector<sm::Issue> issues = sm::validate(machine, kClips);
    ASSERT(hasWarningContaining(issues, "never pass"),
           "isTrue sobre un float nunca puede abrir la transicion");
    ASSERT(!sm::opMatchesType(dash::anim::ConditionOp::IsTrue, dash::anim::ParamType::Float),
           "isTrue no corresponde a un float");
    ASSERT(sm::opMatchesType(dash::anim::ConditionOp::Triggered, dash::anim::ParamType::Trigger),
           "triggered si corresponde a un trigger");
}

void testInitialState()
{
    std::printf("[test] estado inicial vacio o invalido\n");
    dash::anim::AnimationStateMachine empty = makeMachine();
    empty.initialState.clear();
    ASSERT(hasErrorContaining(sm::validate(empty, kClips), "no initial state"),
           "el estado inicial vacio deberia ser un error");

    dash::anim::AnimationStateMachine bogus = makeMachine();
    bogus.initialState = "nope";
    ASSERT(hasErrorContaining(sm::validate(bogus, kClips), "nope"),
           "un estado inicial inexistente deberia ser un error");
}

void testUnreachableState()
{
    std::printf("[test] estado inalcanzable\n");
    dash::anim::AnimationStateMachine machine = makeMachine();

    dash::anim::AnimationState attack;
    attack.name = "attack";
    attack.clip = "Run";
    machine.states.push_back(attack);

    const std::vector<sm::Issue> issues = sm::validate(machine, kClips);
    ASSERT(hasWarningContaining(issues, "'attack' is unreachable"),
           "un estado sin transiciones entrantes deberia avisarse");

    const std::vector<std::string> reached = sm::reachableStates(machine);
    ASSERT(reached.size() == 2, "solo idle y run deberian ser alcanzables");

    // Una transicion "Any State" alcanza a todos, asi que ya no queda aislado.
    dash::anim::AnimationTransition fromAny;
    fromAny.to = "attack";
    machine.transitions.push_back(fromAny);
    const std::vector<std::string> reachedWithAny = sm::reachableStates(machine);
    ASSERT(std::find(reachedWithAny.begin(), reachedWithAny.end(), "attack") !=
               reachedWithAny.end(),
           "una transicion desde cualquier estado vuelve alcanzable al destino");
    ASSERT(!hasWarningContaining(sm::validate(machine, kClips), "'attack' is unreachable"),
           "ya no deberia reportarse como inalcanzable");
}

void testDuplicateNames()
{
    std::printf("[test] nombres duplicados\n");
    dash::anim::AnimationStateMachine machine = makeMachine();
    machine.states[1].name = "idle";
    ASSERT(hasErrorContaining(sm::validate(machine, kClips), "duplicate state name"),
           "dos estados con el mismo nombre deberian ser un error");

    ASSERT(sm::uniqueName({"state", "state 2"}, "state") == "state 3",
           "uniqueName deberia saltar los nombres ya tomados");
}

void testRenamePropagates()
{
    std::printf("[test] renombrar propaga a transiciones y condiciones\n");
    dash::anim::AnimationStateMachine machine = makeMachine();

    sm::renameState(machine, 0, "reposo");
    ASSERT(machine.initialState == "reposo", "el estado inicial deberia seguir el renombre");
    ASSERT(machine.transitions[0].from == "reposo", "el origen deberia seguir el renombre");
    ASSERT(machine.transitions[1].to == "reposo", "el destino deberia seguir el renombre");
    ASSERT(sm::validate(machine, kClips).empty(), "el renombre no deberia romper nada");

    sm::renameParameter(machine, 0, "velocidad");
    ASSERT(machine.transitions[0].conditions[0].parameter == "velocidad",
           "la condicion deberia seguir el renombre del parametro");
    ASSERT(sm::validate(machine, kClips).empty(), "el renombre no deberia romper nada");
}

void testRemovalsKeepGraphConsistent()
{
    std::printf("[test] bajas sin dejar referencias colgadas\n");
    dash::anim::AnimationStateMachine machine = makeMachine();

    sm::removeState(machine, 1);   // run
    ASSERT(machine.states.size() == 1, "deberia quedar un solo estado");
    ASSERT(machine.transitions.empty(), "las transiciones que lo mencionaban deberian irse");
    ASSERT(machine.initialState == "idle", "el estado inicial no deberia haberse tocado");

    dash::anim::AnimationStateMachine other = makeMachine();
    sm::removeState(other, 0);     // idle, que era el inicial
    ASSERT(other.initialState == "run", "al borrar el inicial deberia reasignarse");

    dash::anim::AnimationStateMachine third = makeMachine();
    sm::removeParameter(third, 0);
    ASSERT(third.transitions[0].conditions.empty(),
           "las condiciones sobre el parametro borrado deberian irse");
    ASSERT(!hasErrorContaining(sm::validate(third, kClips), "not declared"),
           "no deberia quedar ninguna condicion colgada");
}

// ─────────────────────────────────────────────────────────────────────────────
void testRoundTrip()
{
    std::printf("[test] guardar y volver a leer un .animsm.json\n");
    const fs::path path = tempDir() / "wolf.animsm.json";

    dash::anim::AnimationStateMachine original = makeMachine();
    original.transitions[0].blendSeconds = 0.35f;
    original.transitions[0].hasExitTime = true;
    original.transitions[0].exitTime = 0.8f;

    dash::anim::AnimationParameter alive;
    alive.name = "alive";
    alive.type = dash::anim::ParamType::Bool;
    alive.defaultBool = true;
    original.parameters.push_back(alive);

    dash::anim::AnimationParameter attack;
    attack.name = "attack";
    attack.type = dash::anim::ParamType::Trigger;
    original.parameters.push_back(attack);

    dash::anim::AnimationTransition anyToIdle;
    anyToIdle.to = "idle";
    anyToIdle.conditions.push_back({"attack", dash::anim::ConditionOp::Triggered, 0.f});
    original.transitions.push_back(anyToIdle);

    std::string error;
    ASSERT(dash::anim::writeStateMachine(path.string(), original, error), error.c_str());

    dash::anim::AnimationStateMachine loaded;
    ASSERT(dash::anim::readStateMachine(path.string(), loaded, error), error.c_str());

    ASSERT(loaded.name == original.name, "el nombre deberia sobrevivir");
    ASSERT(loaded.initialState == original.initialState, "el estado inicial deberia sobrevivir");
    ASSERT(loaded.parameters.size() == original.parameters.size(), "deberian volver 3 parametros");
    ASSERT(loaded.states.size() == original.states.size(), "deberian volver 2 estados");
    ASSERT(loaded.transitions.size() == original.transitions.size(),
           "deberian volver 3 transiciones");

    ASSERT(loaded.findParameter("alive") != nullptr &&
               loaded.findParameter("alive")->type == dash::anim::ParamType::Bool &&
               loaded.findParameter("alive")->defaultBool,
           "el bool y su valor por defecto deberian sobrevivir");
    ASSERT(loaded.findParameter("attack") != nullptr &&
               loaded.findParameter("attack")->type == dash::anim::ParamType::Trigger,
           "el trigger deberia sobrevivir");

    const dash::anim::AnimationTransition& t = loaded.transitions[0];
    ASSERT(t.from == "idle" && t.to == "run", "los extremos deberian sobrevivir");
    ASSERT(t.hasExitTime && std::fabs(t.exitTime - 0.8f) < 1e-5f, "el exit time deberia sobrevivir");
    ASSERT(std::fabs(t.blendSeconds - 0.35f) < 1e-5f, "el blend deberia sobrevivir");
    ASSERT(t.conditions.size() == 1 && t.conditions[0].parameter == "speed" &&
               t.conditions[0].op == dash::anim::ConditionOp::Greater &&
               std::fabs(t.conditions[0].threshold - 0.1f) < 1e-5f,
           "la condicion deberia sobrevivir entera");

    ASSERT(loaded.transitions[2].fromAnyState(),
           "un 'from' vacio deberia releerse como Any State");

    // Segunda vuelta: el JSON tiene que ser un punto fijo.
    const fs::path second = tempDir() / "wolf_again.animsm.json";
    ASSERT(dash::anim::writeStateMachine(second.string(), loaded, error), error.c_str());
    ASSERT(dash::anim::toJson(loaded) == dash::anim::toJson(original),
           "reescribir lo leido deberia dar el mismo JSON");

    ASSERT(sm::validate(loaded, kClips).empty(), "lo releido deberia seguir siendo valido");
}

void testShippedWolfControllerLoads()
{
    std::printf("[test] el controller de ejemplo del lobo carga y valida\n");
#ifdef PROJECT_DIR
    const fs::path path =
        fs::path(PROJECT_DIR) / "assets/models/gltf/Wolf-Blender-2.82a.animsm.json";
    if (!fs::exists(path)) {
        std::printf("  (salteado: %s no esta presente)\n", path.string().c_str());
        return;
    }

    dash::anim::AnimationStateMachine machine;
    std::string error;
    ASSERT(dash::anim::readStateMachine(path.string(), machine, error), error.c_str());

    const std::vector<std::string> wolfClips = {"01_Run_Armature_0", "04_Idle_Armature_0",
                                                "05_site_Armature_0"};
    const std::vector<sm::Issue> issues = sm::validate(machine, wolfClips);
    for (const sm::Issue& issue : issues)
        std::fprintf(stderr, "  %s\n", issue.message.c_str());
    ASSERT(!sm::hasErrors(issues), "el controller versionado no deberia tener errores");
#else
    std::printf("  (salteado: PROJECT_DIR no definido)\n");
#endif
}

} // namespace

int main()
{
    std::printf("=== test_state_machine_panel ===\n");

    testValidMachineIsClean();
    testMissingClip();
    testDanglingTransition();
    testConditionOnUndeclaredParameter();
    testConditionOpTypeMismatch();
    testInitialState();
    testUnreachableState();
    testDuplicateNames();
    testRenamePropagates();
    testRemovalsKeepGraphConsistent();
    testRoundTrip();
    testShippedWolfControllerLoads();

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
