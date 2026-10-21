@echo off
rem Persona 3 FES Recompiled - one-click engine update (see Update.ps1).
rem Everything runs on one line so cmd is not confused when the update replaces this file.
cd /d "%~dp0" & powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Update.ps1" & exit /b
