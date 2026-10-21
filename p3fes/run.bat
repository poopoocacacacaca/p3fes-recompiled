@echo off
rem Persona 3 FES native port - test launcher
rem Put this next to ps2EntryRunner.exe, SLUS_216.21, SYSTEM.CNF, LIB31\ and the .CVM files.
cd /d "%~dp0"

rem Required: keeps the game's interrupt/callback stacks clear of the main stack.
set PS2X_CALLBACK_STACK_TOP=0x100000

rem Memory card: memcards\Mcd001.ps2 (slot 1) is created on first run as an
rem unformatted 8 MB card, same format as PCSX2's .ps2 cards. To use an existing
rem PCSX2 card, copy it over memcards\Mcd001.ps2 (or point PS2X_MEMCARD1 at it).
rem set PS2X_MEMCARD1=C:\path\to\Mcd001.ps2
rem set PS2X_MEMCARD1=none   (boot with no card in slot 1)

rem Controller: first connected gamepad (XInput / DualShock / DualSense via SDL mappings).
rem Stick deadzone, 0.0 - 0.9 (default 0.15):
rem set PS2X_PAD_DEADZONE=0.15

rem Renderer: PCSX2's GS (pcsx2gs.dll and pcsx2gs_resources\ next to the exe) is the default.
rem   pcsx2-sw = PCSX2 software renderer, builtin = the port's own CPU rasterizer.
rem set PS2X_GS_RENDERER=pcsx2-sw
rem Internal resolution multiplier for PCSX2's hardware renderer (1 = native):
rem set PS2X_PCSX2_SCALE=2

rem Hide the developer overlay:
set PS2X_HIDE_DEBUG_UI=1

ps2EntryRunner.exe "%~dp0SLUS_216.21" > run_log.txt 2>&1
