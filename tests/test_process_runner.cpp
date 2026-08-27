// ═════════════════════════════════════════════════════════════════════════════
// test_process_runner — backlog C3
//
// Verifica que runProcessCapture() lance el subproceso con un array de
// argumentos y NO a traves de un shell: la propiedad que hace que una ruta con
// metacaracteres sea un dato y no sintaxis ejecutable.
// ═════════════════════════════════════════════════════════════════════════════
#include "ProcessRunner.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++g_fail; \
    } else { ++g_pass; } \
} while(0)

#ifndef _WIN32

// ── Captura incremental de salida y codigo de salida ────────────────────────
static void test_captures_output()
{
    std::printf("  test_captures_output\n");

    std::vector<std::string> lines;
    const int rc = runProcessCapture({"/bin/echo", "hola mundo"},
                                     [&lines](const std::string& l) { lines.push_back(l); });

    ASSERT(rc == 0, "codigo de salida 0");
    ASSERT(lines.size() == 1, "una linea capturada");
    ASSERT(!lines.empty() && lines[0] == "hola mundo", "la linea llega intacta");
}

// ── El codigo de salida distinto de cero se propaga ─────────────────────────
static void test_propagates_exit_code()
{
    std::printf("  test_propagates_exit_code\n");

    const int rc = runProcessCapture({"/bin/sh", "-c", "exit 3"}, nullptr);
    ASSERT(rc == 3, "el codigo de salida del hijo se propaga");
}

// ── Un ejecutable inexistente devuelve -1, no cuelga ────────────────────────
static void test_missing_executable()
{
    std::printf("  test_missing_executable\n");

    const int rc = runProcessCapture({"dash_no_existe_este_binario_xyz"}, nullptr);
    ASSERT(rc != 0, "un binario inexistente no reporta exito");
}

// ── Lo importante: los metacaracteres son datos, no sintaxis ────────────────
static void test_no_shell_injection(const fs::path& tmp)
{
    std::printf("  test_no_shell_injection\n");

    const fs::path canary = tmp / "canary.txt";
    std::error_code ec;
    fs::remove(canary, ec);

    // Con popen("echo " + arg) el shell ejecutaria el touch. Con un argv array
    // el argumento entero es un unico parametro de echo.
    const std::string hostile = "inofensivo; touch " + canary.string();

    std::vector<std::string> lines;
    const int rc = runProcessCapture({"/bin/echo", hostile},
                                     [&lines](const std::string& l) { lines.push_back(l); });

    ASSERT(rc == 0, "el proceso corrio");
    ASSERT(!fs::exists(canary), "el comando inyectado NO se ejecuto");
    ASSERT(lines.size() == 1 && lines[0] == hostile,
           "el argumento hostil se trato como texto literal");

    // Mismo caso, pero con la forma que usa el pipeline de build: un directorio
    // con comillas y punto y coma dentro del path.
    const fs::path canary2 = tmp / "canary2.txt";
    fs::remove(canary2, ec);
    const std::string hostileDir = "\"; touch " + canary2.string() + "; echo \"";

    lines.clear();
    runProcessCapture({"/bin/echo", hostileDir},
                      [&lines](const std::string& l) { lines.push_back(l); });
    ASSERT(!fs::exists(canary2), "el escape con comillas tampoco ejecuta nada");
}

int main()
{
    std::printf("=== test_process_runner ===\n");

    const fs::path tmp = fs::temp_directory_path() / "dash_process_runner_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp);

    test_captures_output();
    test_propagates_exit_code();
    test_missing_executable();
    test_no_shell_injection(tmp);

    fs::remove_all(tmp);

    std::printf("\nResults: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}

#else

int main()
{
    std::printf("=== test_process_runner ===\n");
    std::printf("  omitido: la ruta POSIX (posix_spawn) no aplica en Windows\n");
    return 0;
}

#endif
