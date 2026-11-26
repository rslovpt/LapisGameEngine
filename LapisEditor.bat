@echo off
rem ==========================================================================
rem  Lapis Engine - double-click to open the Lapis Editor.
rem  Builds (or updates) the engine first; the first build takes a few minutes.
rem ==========================================================================
cd /d "%~dp0"
call "%~dp0Build.bat"
if errorlevel 1 exit /b 1
start "" "%~dp0Binaries\LapisEditor.exe"
