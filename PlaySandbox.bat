@echo off
rem  Plays the Sandbox sample project directly (no editor).
cd /d "%~dp0"
call "%~dp0Build.bat"
if errorlevel 1 exit /b 1
start "" "%~dp0Binaries\LapisPlayer.exe" --project "%~dp0Projects\Sandbox"
