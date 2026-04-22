# dash.ps1 — CLI de Dash-Engine para Windows
# Uso: .\dash.ps1 <comando> [opciones]
#   o: dash <comando>  (si agregaste la función al perfil de PowerShell)
#
# Comandos:
#   build    Compila todo (editor + runtime)
#   editor   Compila y abre el editor DashEngine
#   run      Abre el editor sin recompilar
#   game     Abre el runtime IsometricRPG sin recompilar
#   clean    Elimina el directorio build/
#   update   git pull + recompila
#   test     Compila y ejecuta los tests
#   config   Solo configura CMake (sin compilar)
#   help     Muestra esta ayuda

param(
    [Parameter(Position = 0)]
    [string]$Command = "help",

    [string]$Config    = "Release",
    [string]$BuildDir  = "build",
    [string]$VcpkgRoot = "C:\vcpkg",
    [switch]$Vulkan
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── Resolver cmake ────────────────────────────────────────────────────────────
$cmake = $null
if (Get-Command cmake -ErrorAction SilentlyContinue) {
    $cmake = "cmake"
} else {
    $vsCmake = "C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $vsCmake) { $cmake = $vsCmake }
}
if (-not $cmake) {
    Write-Error "cmake no encontrado. Agrega CMake al PATH o instala Visual Studio con CMake."
    exit 1
}

# ── Rutas de ejecutables ──────────────────────────────────────────────────────
$editorExe = Join-Path $BuildDir "src\editor\$Config\DashEngine.exe"
$gameExe   = Join-Path $BuildDir "src\game\$Config\IsometricRPG.exe"
$toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"

# ── Funciones internas ────────────────────────────────────────────────────────
function Invoke-Config {
    Write-Host "`n[dash] Configurando CMake ($Config)..." -ForegroundColor Cyan
    $cmakeArgs = @(
        "-B", $BuildDir, "-S", ".",
        "-DCMAKE_BUILD_TYPE=$Config",
        "-DBUILD_TESTING=OFF",
        "-DENABLE_VULKAN_EXPERIMENTAL=$(if ($Vulkan) { 'ON' } else { 'OFF' })"
    )
    if (Test-Path $toolchain) {
        $cmakeArgs += @("-DCMAKE_TOOLCHAIN_FILE=$toolchain")
    } else {
        Write-Warning "vcpkg no encontrado en '$VcpkgRoot'. Continuando sin toolchain..."
    }
    & $cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Invoke-Build ([string]$Target = "") {
    # Always reconfigure when -Vulkan is requested (the cache may have ENABLE_VULKAN_EXPERIMENTAL=OFF)
    if (-not (Test-Path $BuildDir) -or $Vulkan) { Invoke-Config }
    Write-Host "`n[dash] Compilando$(if ($Target) { " $Target" } else { " todo" }) ($Config)..." -ForegroundColor Cyan
    $cmakeArgs = @("--build", $BuildDir, "--config", $Config, "--parallel")
    if ($Target) { $cmakeArgs += @("--target", $Target) }
    & $cmake @cmakeArgs
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

function Start-Exe ([string]$Exe) {
    if (-not (Test-Path $Exe)) {
        Write-Error "Ejecutable no encontrado: $Exe`nEjecuta primero: dash build"
        exit 1
    }
    Write-Host "`n[dash] Iniciando $Exe..." -ForegroundColor Green
    & $Exe
}

# ── Dispatcher ────────────────────────────────────────────────────────────────
switch ($Command.ToLower()) {

    "build" {
        Invoke-Build
        Write-Host "`n[dash] Listo. Binarios en $BuildDir\$Config\" -ForegroundColor Green
    }

    "editor" {
        Invoke-Build "DashEngine"
        Start-Exe $editorExe
    }

    "run" {
        Start-Exe $editorExe
    }

    "game" {
        Start-Exe $gameExe
    }

    "clean" {
        if (Test-Path $BuildDir) {
            Write-Host "`n[dash] Eliminando $BuildDir\..." -ForegroundColor Yellow
            Remove-Item -Recurse -Force $BuildDir
            Write-Host "[dash] Limpio." -ForegroundColor Green
        } else {
            Write-Host "[dash] $BuildDir no existe, nada que limpiar." -ForegroundColor Gray
        }
    }

    "update" {
        Write-Host "`n[dash] Actualizando repositorio..." -ForegroundColor Cyan
        git pull
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        Invoke-Build
        Write-Host "`n[dash] Actualizado y compilado." -ForegroundColor Green
    }

    "test" {
        Write-Host "`n[dash] Compilando con tests..." -ForegroundColor Cyan
        if (-not (Test-Path $BuildDir)) { Invoke-Config }
        & $cmake --build $BuildDir --config $Config --parallel
        if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
        Push-Location $BuildDir
        ctest --build-config $Config --output-on-failure
        $code = $LASTEXITCODE
        Pop-Location
        exit $code
    }

    "config" {
        Invoke-Config
        Write-Host "`n[dash] Configuración completa." -ForegroundColor Green
    }

    { $_ -in "help", "--help", "-h", "" } {
        Write-Host @"

  Dash-Engine CLI — Windows

  COMANDOS
    dash build            Compila todo (editor + runtime)
    dash editor           Compila y abre el editor DashEngine
    dash run              Abre el editor sin recompilar
    dash game             Abre el runtime IsometricRPG sin recompilar
    dash clean            Elimina el directorio build/
    dash update           git pull + recompila
    dash test             Compila y ejecuta los tests
    dash config           Solo configura CMake (sin compilar)
    dash help             Muestra esta ayuda

  OPCIONES
    -Config Release|Debug     Configuración de build  (default: Release)
    -BuildDir <ruta>          Directorio de build     (default: build)
    -VcpkgRoot <ruta>         Ruta de vcpkg           (default: C:\vcpkg)
    -Vulkan                   Habilita VulkanBootstrap

  CONFIGURACIÓN DE ALIAS (una sola vez):
    Add-Content `$PROFILE 'function dash { powershell -ExecutionPolicy Bypass -File "E:\Develop\Proyects\Dash-Engine\dash.ps1" @args }'
    . `$PROFILE

"@ -ForegroundColor White
    }

    default {
        Write-Host "[dash] Comando desconocido: '$Command'. Usa 'dash help' para ver los comandos." -ForegroundColor Red
        exit 1
    }
}
