#!/usr/bin/env bash
# dash.sh — CLI de Dash-Engine para macOS / Linux
# Uso: ./dash.sh <comando> [opciones]
#   o: dash <comando>  (si agregaste el alias al perfil de tu shell)
#
# Comandos:
#   build    Compila todo (editor + runtime + vulkan)
#   editor   Compila y abre el editor DashEngine
#   run      Abre el editor sin recompilar
#   game     Abre el runtime IsometricRPG sin recompilar
#   vulkan   Compila y abre VulkanBootstrap
#   clean    Elimina el directorio build/
#   update   git pull + recompila
#   test     Compila y ejecuta los tests
#   config   Solo configura CMake (sin compilar)
#   help     Muestra esta ayuda

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)"

# ── Colores ───────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
CYAN='\033[0;36m'
YELLOW='\033[1;33m'
NC='\033[0m'

info()  { echo -e "${CYAN}[dash]${NC} $*"; }
ok()    { echo -e "${GREEN}[dash]${NC} $*"; }
warn()  { echo -e "${YELLOW}[dash]${NC} $*"; }
err()   { echo -e "${RED}[dash]${NC} $*" >&2; }

# ── Rutas de ejecutables ─────────────────────────────────────────────────────
EDITOR_EXE="${BUILD_DIR}/DashEngine"
GAME_EXE="${BUILD_DIR}/IsometricRPG"
VULKAN_EXE="${BUILD_DIR}/VulkanBootstrap"

# ── Funciones internas ───────────────────────────────────────────────────────
dash_config() {
    info "Configurando CMake..."
    cmake -B "${BUILD_DIR}" -S "${SCRIPT_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        -DENABLE_VULKAN_EXPERIMENTAL=ON
}

dash_build() {
    local target="${1:-}"
    [[ -d "${BUILD_DIR}" ]] || dash_config
    if [[ -n "${target}" ]]; then
        info "Compilando ${target}..."
        cmake --build "${BUILD_DIR}" --target "${target}" --parallel "${JOBS}"
    else
        info "Compilando todo..."
        cmake --build "${BUILD_DIR}" --parallel "${JOBS}"
    fi
}

dash_run_exe() {
    local exe="$1"
    local name="$2"
    if [[ ! -x "${exe}" ]]; then
        err "Ejecutable no encontrado: ${exe}"
        err "Ejecuta primero: dash build"
        exit 1
    fi
    ok "Iniciando ${name}..."
    "${exe}"
}

# ── Dispatcher ───────────────────────────────────────────────────────────────
CMD="${1:-help}"
shift || true

case "${CMD}" in

    build)
        dash_build
        ok "Listo. Binarios en ${BUILD_DIR}/"
        ;;

    editor)
        dash_build "DashEngine"
        dash_run_exe "${EDITOR_EXE}" "DashEngine"
        ;;

    run)
        dash_run_exe "${EDITOR_EXE}" "DashEngine"
        ;;

    game)
        dash_run_exe "${GAME_EXE}" "IsometricRPG"
        ;;

    vulkan)
        dash_build "VulkanBootstrap"
        dash_run_exe "${VULKAN_EXE}" "VulkanBootstrap"
        ;;

    clean)
        if [[ -d "${BUILD_DIR}" ]]; then
            warn "Eliminando ${BUILD_DIR}/..."
            rm -rf "${BUILD_DIR}"
            ok "Limpio."
        else
            info "build/ no existe, nada que limpiar."
        fi
        ;;

    update)
        info "Actualizando repositorio..."
        git -C "${SCRIPT_DIR}" pull
        dash_build
        ok "Actualizado y compilado."
        ;;

    test)
        info "Compilando con tests..."
        cmake -B "${BUILD_DIR}" -S "${SCRIPT_DIR}" \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_TESTING=ON \
            -DENABLE_VULKAN_EXPERIMENTAL=ON
        cmake --build "${BUILD_DIR}" --parallel "${JOBS}"
        info "Ejecutando tests..."
        cd "${BUILD_DIR}" && ctest --output-on-failure
        ;;

    config)
        dash_config
        ok "Configuración completa."
        ;;

    help|--help|-h)
        cat <<'HELP'

  Dash-Engine CLI — macOS / Linux

  COMANDOS
    dash build            Compila todo (editor + runtime + vulkan)
    dash editor           Compila y abre el editor DashEngine
    dash run              Abre el editor sin recompilar
    dash game             Abre el runtime IsometricRPG sin recompilar
    dash vulkan           Compila y abre VulkanBootstrap
    dash clean            Elimina el directorio build/
    dash update           git pull + recompila
    dash test             Compila y ejecuta los tests
    dash config           Solo configura CMake (sin compilar)
    dash help             Muestra esta ayuda

  CONFIGURACIÓN DE ALIAS (una sola vez):
    echo 'alias dash="/ruta/a/Dash-Engine/dash.sh"' >> ~/.zshrc
    source ~/.zshrc

HELP
        ;;

    *)
        err "Comando desconocido: '${CMD}'. Usa 'dash help' para ver los comandos."
        exit 1
        ;;
esac
