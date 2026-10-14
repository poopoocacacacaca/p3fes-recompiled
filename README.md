# Persona 3 FES: Native PC Port

The goal of this project is a **fully playable static recompilation of Persona 3 FES**
(USA, `SLUS_216.21`) that runs natively on Windows: the complete game, from the
opening movie to the ending of *The Answer*, playable from start to finish with no
emulator involved.

## What "static recompilation" means here

Emulators like PCSX2 interpret or JIT-compile the PS2's machine code while the game runs.
This project instead translates the game's executable **ahead of time** into C++,
which is then compiled into a normal PC program. Only the parts of the PS2 that game code
talks to directly are recreated around it: the graphics chip (GS), the MPEG decoder (IPU),
the I/O processor (IOP), DMA, memory cards and controllers.

Translation is done by [PS2Recomp](https://github.com/ran-j/PS2Recomp). This repository
holds a modified copy of it plus everything specific to Persona 3 FES.

## Goals

1. **Fully playable.** Every part of the game works: menus, the dorm, school and city,
   Tartarus, battles, social links, events, movies, saving and loading, and both
   *The Journey* and *The Answer*.
2. **Native Windows build.** A single executable, modern controllers (XInput,
   DualShock, DualSense), keyboard support, and saves compatible with PCSX2's
   memory card format.
3. **Faithful.** Behaviour, audio and visuals match the original. PCSX2 is the
   reference for anything uncertain.
4. **Enhancements, once the game is playable:**
   - Widescreen
   - 60 FPS with frame interpolation, in the spirit of Ship of Harkinian
   - Higher internal resolution

## You need your own copy of the game

This repository contains **no game files and no recompiled game code**. Both are
derived from copyrighted material. To build the port you dump your own disc and generate
the C++ locally; see [`p3fes/README.md`](p3fes/README.md) for the steps.

## Status

| Area | State |
| --- | --- |
| Boot, kernel, threads | Working |
| Memory cards (8 MB, PCSX2 `.ps2` format) | Working |
| Controller and keyboard input | Working |
| Movies (IPU / Sofdec MPEG decoding) | Decodes correctly, plays below full speed |
| Rendering performance | The software GS renderer is the main bottleneck |
| Title screen and menus | Next |
| In-game 3D rendering | Not yet verified |
| Exploration, battles, social links, events | Not started |
| Full playthrough | Not started |
| Widescreen / 60 FPS | Planned, after the game is playable |

## Repository layout

| Path | Contents |
| --- | --- |
| `p3fes/` | Port configuration, function map, Windows launcher, helper scripts, build steps |
| `ps2xRecomp/` | The recompiler (PS2 executable → C++) |
| `ps2xRuntime/` | Runtime the generated code runs on: memory, kernel, GS, IPU, DMA, input |
| `ps2xIOP/` | I/O processor emulation: sound, disc, memory cards, controllers |
| `PS2RECOMP_README.md` | The original PS2Recomp README |

## Credits

- [PS2Recomp](https://github.com/ran-j/PS2Recomp) by ran-j and contributors: the recompiler and runtime
- [PCSX2](https://github.com/PCSX2/pcsx2): the IPU and SIO2/memory card code is adapted from it, and it is the accuracy reference
- The snaku Persona 3 FES decompilation project, for function names and structure

Persona 3 FES is © ATLUS / SEGA. This is an unofficial fan project and is not affiliated
with or endorsed by them.

## License

PS2Recomp is GPL, and the code adapted from PCSX2 is GPL-3.0+. If builds are ever shared,
the matching source must be shared with them.
