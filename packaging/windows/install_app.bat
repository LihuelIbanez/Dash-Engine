@echo off
REM ─────────────────────────────────────────────────────────────────────────────
REM install_app.bat — Package DashEngine for Windows
REM Creates a portable directory with executables and resources.
REM ─────────────────────────────────────────────────────────────────────────────

setlocal enabledelayedexpansion

set "PROJECT_DIR=%~dp0..\.."
set "BUILD_DIR=%PROJECT_DIR%\build\Release"
set "INSTALL_DIR=%PROJECT_DIR%\build\DashEngine-Windows"

REM ── 1. Verify build exists ─────────────────────────────────────────────────
if not exist "%BUILD_DIR%\DashEngine.exe" (
    echo ERROR: DashEngine.exe not found in %BUILD_DIR%
    echo Run build_windows.ps1 first.
    exit /b 1
)

echo === Packaging DashEngine for Windows ===

REM ── 2. Clean previous install ──────────────────────────────────────────────
if exist "%INSTALL_DIR%" (
    echo Cleaning previous package...
    rmdir /s /q "%INSTALL_DIR%"
)

REM ── 3. Create directory structure ──────────────────────────────────────────
mkdir "%INSTALL_DIR%\bin"

REM ── 4. Copy executables ────────────────────────────────────────────────────
echo Copying executables...
copy "%BUILD_DIR%\DashEngine.exe" "%INSTALL_DIR%\bin\" >nul
if exist "%BUILD_DIR%\IsometricRPG.exe" (
    copy "%BUILD_DIR%\IsometricRPG.exe" "%INSTALL_DIR%\bin\" >nul
)
if exist "%BUILD_DIR%\VulkanBootstrap.exe" (
    copy "%BUILD_DIR%\VulkanBootstrap.exe" "%INSTALL_DIR%\bin\" >nul
)

REM Copy DLLs that may be needed
for %%f in ("%BUILD_DIR%\*.dll") do (
    copy "%%f" "%INSTALL_DIR%\bin\" >nul 2>nul
)

REM ── 5. Copy resources ──────────────────────────────────────────────────────
echo Copying resources...
if exist "%PROJECT_DIR%\assets" (
    xcopy "%PROJECT_DIR%\assets" "%INSTALL_DIR%\bin\assets\" /s /e /q /y >nul
)
if exist "%PROJECT_DIR%\library" (
    xcopy "%PROJECT_DIR%\library" "%INSTALL_DIR%\bin\library\" /s /e /q /y >nul
)
if exist "%PROJECT_DIR%\scenes" (
    xcopy "%PROJECT_DIR%\scenes" "%INSTALL_DIR%\bin\scenes\" /s /e /q /y >nul
)

REM ── 6. Create launcher script ──────────────────────────────────────────────
echo @echo off > "%INSTALL_DIR%\DashEngine.bat"
echo cd /d "%%~dp0bin" >> "%INSTALL_DIR%\DashEngine.bat"
echo start DashEngine.exe %%* >> "%INSTALL_DIR%\DashEngine.bat"

echo.
echo === Package created: %INSTALL_DIR% ===
echo.
echo Contents:
dir /b "%INSTALL_DIR%\bin\*.exe" 2>nul
echo.
echo Run DashEngine.bat to launch the editor.
