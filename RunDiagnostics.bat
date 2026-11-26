@echo off
rem ==========================================================================
rem  Lapis Engine - runs LapisDiagnostics and opens the report.
rem  Report: bin\Logs\diagnostics_report.txt
rem ==========================================================================
cd /d "%~dp0"
call "%~dp0Build.bat"
if errorlevel 1 exit /b 1
"%~dp0bin\LapisDiagnostics.exe" --project "%~dp0Projects\Sandbox"
start "" notepad "%~dp0bin\Logs\diagnostics_report.txt"
