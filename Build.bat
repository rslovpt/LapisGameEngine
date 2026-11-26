@echo off
rem ==========================================================================
rem  Lapis Engine - Build.bat
rem  Compiles the engine, editor, player and tools (Release) into Binaries\.
rem  Needs Visual Studio 2022 (or 2019) with "Desktop development with C++".
rem  Full build output: Binaries\Logs\build_log.txt
rem ==========================================================================
setlocal EnableDelayedExpansion
cd /d "%~dp0"
if not exist "Binaries\Logs" mkdir "Binaries\Logs"
set "LOG=%~dp0Binaries\Logs\build_log.txt"

rem --- find CMake (PATH first, then the copy bundled with Visual Studio) -----
set "CMAKE_EXE="
where cmake >nul 2>nul && set "CMAKE_EXE=cmake"
if not defined CMAKE_EXE (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "!VSWHERE!" (
        for /f "usebackq delims=" %%i in (`"!VSWHERE!" -latest -products * -property installationPath`) do set "VSPATH=%%i"
        set "VSCMAKE=!VSPATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if exist "!VSCMAKE!" set "CMAKE_EXE=!VSCMAKE!"
    )
)
if not defined CMAKE_EXE (
    echo.
    echo  [Lapis] Could not find Visual Studio / CMake.
    echo.
    echo  Install "Visual Studio 2022 Community" from https://visualstudio.microsoft.com/
    echo  and tick the workload "Desktop development with C++". Then run this again.
    echo.
    pause
    exit /b 1
)

echo [Lapis] Configuring...   (log: Binaries\Logs\build_log.txt)
"%CMAKE_EXE%" -S . -B build -A x64 > "%LOG%" 2>&1
if errorlevel 1 goto :failed

echo [Lapis] Compiling...     (first build takes a few minutes)
"%CMAKE_EXE%" --build build --config Release --parallel >> "%LOG%" 2>&1
if errorlevel 1 goto :failed

copy /y "build\bin\Release\*.exe" "Binaries\" >nul
if errorlevel 1 goto :failed
echo [Lapis] Build succeeded. Programs are in the Binaries folder.
exit /b 0

:failed
echo.
echo  [Lapis] BUILD FAILED. Last lines of the log:
echo  ------------------------------------------------------------------------
powershell -NoProfile -Command "Get-Content -Tail 30 '%LOG%'"
echo  ------------------------------------------------------------------------
echo  Full log: Binaries\Logs\build_log.txt  (Claude can read it from your folder)
echo.
pause
exit /b 1
