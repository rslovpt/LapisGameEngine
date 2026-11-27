@echo off
rem ==========================================================================
rem  Lapis Engine - MakeRelease.bat
rem  Builds a clean, public, ready-to-run release of Lapis:
rem
rem      Release\LapisEngine-<version>\                 (the release folder)
rem      Release\LapisEngine-<version>-Windows-x64.zip  (the same, zipped)
rem
rem  The release has the editor, player, packager and diagnostics already
rem  built (C++ runtime linked in, so no Visual C++ Redistributable needed),
rem  plus templates, example projects, ready-to-play demos, docs and licenses.
rem  Anyone can unzip it and double-click "Lapis Editor.bat" - no compiler.
rem
rem  Options:   MakeRelease.bat nozip      skip the .zip
rem             MakeRelease.bat notests    skip the unit tests
rem  Needs Visual Studio 2022 (or 2019) with "Desktop development with C++".
rem  Full log: Release\release_log.txt
rem ==========================================================================
setlocal EnableDelayedExpansion
cd /d "%~dp0"
set "ROOT=%~dp0"
set "DOZIP=1"
set "DOTESTS=1"
for %%A in (%*) do (
    if /i "%%A"=="nozip" set "DOZIP=0"
    if /i "%%A"=="notests" set "DOTESTS=0"
)
if not exist "Release" mkdir "Release"
set "LOG=%ROOT%Release\release_log.txt"
echo Lapis release build %DATE% %TIME% > "%LOG%"

rem --- version: read EngineVersion from Engine\Source\Lapis\Lapis.h -----------
set "VER="
for /f tokens^=2^ delims^=^" %%v in ('findstr /c:"EngineVersion =" "Engine\Source\Lapis\Lapis.h"') do set "VER=%%v"
if not defined VER (
    echo  [Lapis] Could not read the engine version from Engine\Source\Lapis\Lapis.h
    pause
    exit /b 1
)
set "NAME=LapisEngine-%VER%"
set "OUT=%ROOT%Release\%NAME%"
echo.
echo  ============================================================
echo    Lapis Engine %VER%  -  making release "%NAME%"
echo  ============================================================
echo.

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
    echo  [Lapis] Could not find Visual Studio / CMake.
    echo  Install "Visual Studio 2022 Community" with "Desktop development with C++".
    pause
    exit /b 1
)

rem --- 1. build (separate folder, static runtime, Release) -------------------
echo  [1/5] Configuring...
"%CMAKE_EXE%" -S . -B build-release -A x64 -DLAPIS_STATIC_RUNTIME=ON >> "%LOG%" 2>&1
if errorlevel 1 goto :failed
echo  [1/5] Compiling (Release, first time takes a few minutes)...
"%CMAKE_EXE%" --build build-release --config Release --parallel >> "%LOG%" 2>&1
if errorlevel 1 goto :failed
set "BIN=%ROOT%build-release\bin\Release"
for %%E in (LapisEditor LapisPlayer LapisPackager LapisDiagnostics) do (
    if not exist "%BIN%\%%E.exe" (
        echo  [Lapis] Missing %%E.exe after the build.>> "%LOG%"
        goto :failed
    )
)

rem --- 2. unit tests (no window, no GPU needed) ------------------------------
if "%DOTESTS%"=="1" (
    echo  [2/5] Running unit tests...
    "%BIN%\LapisDiagnostics.exe" --unit-only --no-gpu-validation >> "%LOG%" 2>&1
    if errorlevel 1 (
        echo  [Lapis] Unit tests FAILED - not making a release. See Release\release_log.txt
        goto :failed
    )
) else (
    echo  [2/5] Unit tests skipped.
)

rem --- 3. assemble the release folder ----------------------------------------
echo  [3/5] Assembling Release\%NAME% ...
if exist "%OUT%" rmdir /s /q "%OUT%"
if exist "%OUT%" (
    echo  [Lapis] Could not delete the old "%OUT%" - is a program from it still open?
    goto :failed
)
mkdir "%OUT%\bin" "%OUT%\bin\Logs" "%OUT%\Licenses" "%OUT%\Demos"
for %%E in (LapisEditor LapisPlayer LapisPackager LapisDiagnostics) do copy /y "%BIN%\%%E.exe" "%OUT%\bin\" >nul
rem robocopy: exit codes below 8 mean success. Saved/Logs/caches are left out.
robocopy "Templates" "%OUT%\Templates" /E /XD Saved Logs Export /NFL /NDL /NJH /NJS /NP >> "%LOG%"
if errorlevel 8 goto :failed
robocopy "Projects" "%OUT%\Projects" /E /XD Saved Logs Export /NFL /NDL /NJH /NJS /NP >> "%LOG%"
if errorlevel 8 goto :failed
robocopy "Docs" "%OUT%\Docs" /E /NFL /NDL /NJH /NJS /NP >> "%LOG%"
if errorlevel 8 goto :failed
copy /y "README.md" "%OUT%\README.md" >nul
copy /y "ThirdParty\luau\LICENSE.txt" "%OUT%\Licenses\Luau-LICENSE.txt" >nul
copy /y "ThirdParty\luau\lua_LICENSE.txt" "%OUT%\Licenses\Lua-LICENSE.txt" >nul
copy /y "ThirdParty\fonts\LICENSE-DejaVu.txt" "%OUT%\Licenses\DejaVu-LICENSE.txt" >nul
if exist "LICENSE" copy /y "LICENSE" "%OUT%\LICENSE.txt" >nul

rem Launchers (relative paths, so the folder can be moved or unzipped anywhere).
> "%OUT%\Lapis Editor.bat" (
    echo @echo off
    echo rem Opens the Lapis Editor.
    echo start "" "%%~dp0bin\LapisEditor.exe" %%*
)
> "%OUT%\Run Diagnostics.bat" (
    echo @echo off
    echo rem Checks your GPU and the engine; the report is saved to bin\Logs\diagnostics_report.txt
    echo "%%~dp0bin\LapisDiagnostics.exe" %%*
    echo pause
)

rem Version info.
set "COMMIT=unknown"
for /f "delims=" %%c in ('git rev-parse --short HEAD 2^>nul') do set "COMMIT=%%c"
> "%OUT%\VERSION.txt" (
    echo Lapis Engine %VER%
    echo Build:    %DATE% %TIME%
    echo Commit:   !COMMIT!
    echo Platform: Windows x64 ^(DirectX 12 default, OpenGL 4.5 fallback^)
    echo.
    echo Start:    double-click "Lapis Editor.bat"
    echo Demos:    Demos\^<name^>\ - ready-to-play exported games
    echo Problems: run "Run Diagnostics.bat" and send bin\Logs\diagnostics_report.txt
    echo Python:   optional. Install Python 3.8+ ^(64-bit^) from python.org to use .py scripts.
)

rem --- 4. ready-to-play demos (exported with the packager we just built) ------
echo  [4/5] Exporting demos...
for %%T in (Physics Rendering Scripting) do (
    "%OUT%\bin\LapisPackager.exe" "Projects\StressTests\%%T" "%OUT%\Demos\StressTest_%%T" >> "%LOG%" 2>&1
    if errorlevel 1 echo  [Lapis] warning: could not export StressTest_%%T ^(see log^)
)
for /d %%P in (Templates\*) do (
    "%OUT%\bin\LapisPackager.exe" "%%P" "%OUT%\Demos\%%~nxP" >> "%LOG%" 2>&1
    if errorlevel 1 echo  [Lapis] warning: could not export %%~nxP ^(see log^)
)
rem The packager may write logs into bin\Logs; keep the release clean.
del /q "%OUT%\bin\Logs\*" >nul 2>&1

rem --- 5. zip -----------------------------------------------------------------
if "%DOZIP%"=="1" (
    echo  [5/5] Zipping...
    set "ZIP=%ROOT%Release\%NAME%-Windows-x64.zip"
    if exist "!ZIP!" del /q "!ZIP!"
    powershell -NoProfile -Command "Compress-Archive -Path '%OUT%' -DestinationPath '!ZIP!' -CompressionLevel Optimal" >> "%LOG%" 2>&1
    if errorlevel 1 echo  [Lapis] warning: zipping failed ^(the folder is still fine^)
) else (
    echo  [5/5] Zip skipped.
)

echo.
echo  ============================================================
echo    Done:  Release\%NAME%
if "%DOZIP%"=="1" echo           Release\%NAME%-Windows-x64.zip
echo  ============================================================
echo.
explorer "%ROOT%Release"
pause
exit /b 0

:failed
echo.
echo  [Lapis] RELEASE FAILED. Last lines of the log:
echo  ------------------------------------------------------------------------
powershell -NoProfile -Command "Get-Content -Tail 30 '%LOG%'"
echo  ------------------------------------------------------------------------
echo  Full log: Release\release_log.txt  (Claude can read it from your folder)
echo.
pause
exit /b 1
