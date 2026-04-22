# build_windows.ps1
# Configura y compila DashEngine en Windows usando CMake + vcpkg.
#
# Uso:
#   .\build_windows.ps1                        # Release, sin tests
#   .\build_windows.ps1 -Config Debug          # Debug
#   .\build_windows.ps1 -Tests                 # Habilita ctest
#   .\build_windows.ps1 -VcpkgRoot "C:\vcpkg"  # Ruta custom de vcpkg

param(
    [string]$Config     = "Release",
    [string]$VcpkgRoot  = "C:\vcpkg",
    [string]$BuildDir   = "build",
    [switch]$Tests,
    [switch]$Vulkan
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# ── 1. Verificar vcpkg ────────────────────────────────────────────────────────
$toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $toolchain)) {
    Write-Error @"
vcpkg no encontrado en '$VcpkgRoot'.
Instala vcpkg y vuelve a ejecutar, o pasa -VcpkgRoot con la ruta correcta.

  git clone https://github.com/microsoft/vcpkg.git C:\vcpkg
  C:\vcpkg\bootstrap-vcpkg.bat
"@
}

# ── 2. Verificar CMake ────────────────────────────────────────────────────────
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "CMake no encontrado en PATH. Instala CMake >= 3.16 desde https://cmake.org/download/"
}

# ── 3. Configurar ─────────────────────────────────────────────────────────────
$cmakeArgs = @(
    "-B", $BuildDir,
    "-S", ".",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DCMAKE_BUILD_TYPE=$Config",
    "-DBUILD_TESTING=$(if ($Tests) { 'ON' } else { 'OFF' })",
    "-DENABLE_VULKAN_EXPERIMENTAL=$(if ($Vulkan) { 'ON' } else { 'OFF' })"
)

Write-Host "`n[1/2] Configurando CMake ($Config)..." -ForegroundColor Cyan
cmake @cmakeArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# ── 4. Compilar ───────────────────────────────────────────────────────────────
Write-Host "`n[2/2] Compilando..." -ForegroundColor Cyan
cmake --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# ── 5. Tests opcionales ───────────────────────────────────────────────────────
if ($Tests) {
    Write-Host "`n[3/3] Ejecutando tests..." -ForegroundColor Cyan
    Push-Location $BuildDir
    ctest --build-config $Config --output-on-failure
    Pop-Location
}

Write-Host "`nBuild completado. Binarios en: $BuildDir\$Config\" -ForegroundColor Green
Write-Host "  DashEngine.exe   — editor"
Write-Host "  IsometricRPG.exe — runtime standalone"
