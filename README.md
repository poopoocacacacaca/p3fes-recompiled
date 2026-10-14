<div align="center">

# Persona 3 FES: PS2 Static Recompilation

**A native PC port of *Shin Megami Tensei: Persona 3 FES* (PS2, NTSC-U `SLUS-216.21`), built by statically recompiling the game's MIPS R5900 code to C++.**

![Platform](https://img.shields.io/badge/platform-Windows%20x64%20%7C%20Linux-0078D6)
![Language](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)
![Build](https://img.shields.io/badge/build-CMake%20%2B%20Ninja-064F8C?logo=cmake&logoColor=white)
![Based on](https://img.shields.io/badge/based%20on-PS2Recomp-8A2BE2)
![Status](https://img.shields.io/badge/status-experimental-orange)

</div>

---

> [!IMPORTANT]
> This repository **does not contain any game files** (ELF, IRX modules, `.CVM` archives) or the C++
> generated from them. You need **your own copy** of Persona 3 FES (NTSC-U) and must generate the code locally.

<p align="center">
  <img src="p3fes/status/progress.svg" alt="Port progress: weighted phases toward a fully playable port, EE code translated, C decompilation progress and component status" width="100%">
</p>

The chart is generated from [`p3fes/status/status.toml`](p3fes/status/status.toml) by
`p3fes/scripts/status_chart.py`. The overall figure is a weighted estimate of the phases listed (weights are effort
guesses, not measurements). "EE code translated" is exact: every function of the executable is either recompiled to
C++ or bound to a runtime implementation. The C decompilation bar is a different project (readable source code);
this port does not depend on it.

## Goal

A **fully playable static recompilation** of Persona 3 FES: *The Journey* and *The Answer*, from the opening
movie to the credits, running as a native executable with no emulator in the loop. Accuracy comes first,
with PCSX2 as the reference implementation. Once the game is playable, the port adds widescreen, higher
internal resolution and 60 FPS with frame interpolation.

## How it works

The PS2 has three processors that matter here. The **Emotion Engine** (EE, MIPS R5900 with 128-bit MMI,
the FPU and the VU0 macro unit) runs the game. The **I/O processor** (IOP, MIPS R3000A) runs the sound,
disc, controller and memory card drivers. The **Graphics Synthesizer** (GS) rasterizes primitives that
reach it through DMA, VIF1/VU1 and the GIF.

```
SLUS_216.21 (EE ELF) ─┬─► function map (p3fes_map.csv, 14,628 functions)
                      │        + analyzer output (analyzer.toml, extra_starts.txt)
                      ▼
                 ps2_recomp (p3fes.toml) ──► C++ (one file per function) ──┐
                                                                           ▼
   ps2xRuntime: EE kernel HLE · DMAC · GIF/GS · VIF1/VU1 · IPU · timers ─► ps2EntryRunner
   ps2xIOP:     R3000A interpreter running the game's own IRX modules + SPU2 + SIO2
```

**EE: static recompilation.** `ps2xRecomp` decodes every function in the map and emits C++ that operates on
an `R5900Context` (128-bit GPRs, HI/LO pairs, FPU and VU0 state). Branch delay slots, 64-bit compares
and MMI lane semantics are translated explicitly. Calls between functions are direct C++ calls. Indirect
jumps (`jr`/`jalr`) go through a dispatcher that maps a guest PC to its compiled function or to a
mid-function entry point. For this ELF:

| | |
|---|---|
| Functions recompiled | 14,423 |
| Functions bound to runtime HLE stubs | 205 (EE kernel syscalls, SIF, CD, pad, memory card libraries) |
| Functions kept as guest code (`keep_guest`) | Sony `libmpeg` and `libipu`, so the game's own MPEG player runs on the emulated IPU |
| Indirect-jump fallback entries | 3,910 functions (453,264 entries) |
| Recovered jump tables | 14 |

**EE runtime.** The EE kernel is implemented at a high level: threads, semaphores, event flags, alarms,
interrupt handlers and the VBlank/HBlank timing that drives them. Hardware is modelled where the game
touches it directly:

- **DMAC**: all channels, including source/destination chain and interleave modes for scratchpad DMA.
  Sofdec's motion compensation pulls reference blocks with toSPR chain DMA.
- **IPU**: the MPEG-1/2 decoder adapted from PCSX2 (BDEC/IDEC/VDEC/FDEC/CSC, FIFOs, DMA ch3/ch4).
- **GS**: a CPU rasterizer and an OpenGL 4.6 compute/graphics backend, with an optional render thread.
- **VIF1/VU1**: VU1 microcode is interpreted, or compiled ahead of time from captured microprograms.
- **Timers, INTC, COP0 Count, SIF** and the SIF RPC transport to the IOP.

**IOP.** Instead of re-implementing the game's drivers, the IOP runs the original IRX modules from
`LIB31/` (`SIO2MAN`, `PADMAN`, `MC2_S1`, `LIBSD`, `SDRDRV`, `CRI_ADXI`, `SKSOUND`, `MODHSYN`, …) on an
R3000A interpreter. The IOP kernel and libraries (`sysmem`, `loadcore`, `intrman`, `thbase`, `sifman`,
`sifcmd`, `cdvdman`, `timrman`, …) are HLE. The hardware the drivers program is emulated:

- **SPU2**: voices and ADMA streaming, with 48 kHz host audio output
- **SIO2**: PS2 memory cards in PCSX2's 8 MB `.ps2` format
- **IOP DMA** and **interrupts**

## Middleware in this game

| Library | Role | Status in the port |
|---|---|---|
| CRI Sofdec (`libmwsfde`) + Sony `libmpeg` | FMV playback (`.SFD`, MPEG-1 640×448) through the IPU | Decodes correctly |
| CRI ADX (`CRI_ADXI.IRX`) | Streamed music and movie audio through SPU2 AutoDMA | Working (verified from the emulated SPU2 output) |
| Atlus sound driver (`SKSOUND`/`MODHSYN`) | Sound effects and sequenced audio | Loads |
| `libmc2` + `MC2_S1.IRX` | Memory card | Card detected (8 MB) |
| `libpad` + `PADMAN.IRX` | DualShock 2 | Working |
| RMD models | 3D characters and fields | Not reached yet |

## Current status

| Milestone | Status |
|---|:---:|
| Recompile `SLUS_216.21` to C++ (14,628 functions) | ✅ |
| Boot: EE kernel, IOP modules, CVM archives (`DATA.CVM`, `BGM.CVM`, `BTL.CVM`) | ✅ |
| Memory card detection (8 MB card) | ✅ |
| Controller and keyboard input | ✅ |
| ATLUS and CRI logos | ✅ |
| Opening movie decoded through the IPU | ✅ |
| Movies at full speed | 🔧 ~0.4× in the 2-core test VM: the CPU GS rasterizer (alpha-blended textured fans, twice per frame) is the bottleneck while the EE thread idles ~75%. Integrating PCSX2's GS. |
| Opening movie audio (CRI ADX over SPU2 AutoDMA) | ✅ |
| Title screen | 🔧 next |
| New Game, field and dungeon rendering | ⏳ |
| Battles, social links, events | ⏳ |
| Full playthrough of *The Journey* and *The Answer* | ⏳ |
| Widescreen, internal resolution, 60 FPS with interpolation | ⏳ planned after playability |

## Repository layout

```
.
├── p3fes/
│   ├── p3fes.toml          # Recompiler config: stubs, keep_guest, MMIO, jump tables, patches
│   ├── p3fes_map.csv       # Function map (name, start, end, size)
│   ├── analyzer.toml       # Analyzer output used to seed the config
│   ├── extra_starts.txt    # Additional function entry points
│   ├── run.bat             # Windows launcher (environment variables documented inside)
│   ├── scripts/            # CVM listing, IRX import offsets, headless test runner, profiler report
│   └── README.md           # Build steps
├── ps2xRecomp/             # Recompiler: R5900 decoder (rabbitizer) and C++ emitter
├── ps2xAnalyzer/           # ELF analyzer that proposes stubs/entry points
├── ps2xRuntime/            # EE runtime: kernel, DMAC, GS, VIF/VU, IPU, input, host window
├── third_party/pcsx2/      # Subset of PCSX2 (GS, common/, libraries), commit 2954ebe
├── third_party/pcsx2-gs/   # PCSX2's GS as a shared library with a C API (OpenGL + software renderers)
├── ps2xIOP/                # IOP: R3000A interpreter, HLE kernel/libraries, SPU2, SIO2
└── PS2RECOMP_README.md     # Upstream PS2Recomp README
```

## Building

See [`p3fes/README.md`](p3fes/README.md). In short:

1. Build `ps2xRecomp` (`-DPS2X_BUILD_RECOMP=ON`).
2. Copy `SLUS_216.21` from your disc next to `p3fes/p3fes.toml` and run `ps2_recomp p3fes.toml`.
3. Copy the generated C++ into `ps2xRuntime/src/runner/` and its two headers into `ps2xRuntime/include/`.
4. Build `ps2xRuntime` (`-DPS2X_BUILD_RUNTIME=ON`). Windows uses llvm-mingw or MSVC.
5. Put `ps2EntryRunner.exe`, `run.bat`, `SLUS_216.21`, `SYSTEM.CNF`, `LIB31\` and the `.CVM` files in one folder.

## Runtime options

| Variable | Effect |
|---|---|
| `PS2X_CALLBACK_STACK_TOP=0x100000` | Required: keeps interrupt/callback stacks clear of the main stack |
| `PS2X_GS_GPU=1` | Use the OpenGL GS backend instead of the CPU rasterizer |
| `PS2X_GS_THREAD=1` | Run the GS backend on its own thread |
| `PS2X_MEMCARD1=<path>` / `none` | Memory card image for slot 1 (default `memcards/Mcd001.ps2`) |
| `PS2X_PAD_DEADZONE=0.15` | Analog stick deadzone |
| `PS2X_HIDE_DEBUG_UI=1` | Hide the developer overlay |
| `PS2X_AUDIO=0` | Disable host audio output |
| `PS2X_AUDIO_DUMP=<file>` | Dump emulated SPU2 output (s16le stereo 48 kHz) |
| `PS2X_PROFILE=<file>` | Linux sampling profiler; summarise with `p3fes/scripts/prof_report.py` |

Debug traces: `PS2X_SPU2_DIAG`, `PS2X_GS_DIAG`, `PS2X_IPU_TRACE`, `PS2X_DMA_LOG`, `PS2X_CD_LOG`, `PS2X_IOP_CD_LOG`, `PS2X_EE_TRACE_CALLS`,
`PS2X_LOG_SUSPEND`.

## Credits

- [PS2Recomp](https://github.com/ran-j/PS2Recomp) by ran-j and contributors: recompiler and runtime.
- [god-of-war-recomp](https://github.com/KIexster/god-of-war-recomp) by KIexster: 108 PS2Recomp patches
  (SPU2, SIO2, IOP and VU1 work, many EE/GS fixes), merged into this tree with comments translated and
  environment variables renamed to `PS2X_*`.
- Taylor N. Albarnaz / LightVelox's Shadow of the Colossus fork of PS2Recomp: OpenGL GS backend, command queue,
  FPU/VU0 semantics (via god-of-war-recomp).
- [urban-reign-recomp](https://github.com/thiagordsfx/urban-reign-recomp) by thiagordsfx: the PCSX2 GS bridge
  design (`third_party/pcsx2-gs`) this port adapts to OpenGL.
- [PCSX2](https://github.com/PCSX2/pcsx2): the IPU is adapted from it, and it is the accuracy reference.
- [ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp): reference for SPU2, IPU and a GPU-compute GS.
- The snaku Persona 3 FES decompilation, for function names.

*Persona 3 FES* is © ATLUS / SEGA. This is an unofficial, non-commercial fan project, not affiliated with
or endorsed by ATLUS or SEGA.

## License

GPL-3.0. PS2Recomp is GPL, and the PCSX2 and god-of-war-recomp code is GPL-3.0+. If you distribute a build,
you must also provide the corresponding source.
