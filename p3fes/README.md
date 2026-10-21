# Persona 3 FES (USA, SLUS_216.21) native PC port

Built with PS2Recomp (static recompilation of the PS2 executable to C++). This
repository holds the modified PS2Recomp sources and the port's configuration.
It contains **no game files and no recompiled game code**. Both come from your
own disc and are generated locally.

## Contents

| File | Purpose |
| --- | --- |
| `p3fes.toml` | Recompiler config: input ELF, function map, stubs, `keep_guest` list |
| `p3fes_map.csv` | Function map (names/addresses) fed to the recompiler |
| `analyzer.toml`, `extra_starts.txt` | Analyzer config and extra function starts |
| `run.bat` | Windows launcher with the required environment variables |
| `scripts/` | Helper scripts (CVM listing, IRX import offsets, headless test runner) |

## Build outline

1. Build the tools (`ps2xRecomp`) with CMake.
2. Put `SLUS_216.21` from your disc next to `p3fes.toml` and run
   `ps2_recomp p3fes.toml`. The generated C++ goes to `./output/`.
3. Copy the generated `.cpp` files into `ps2xRuntime/src/runner/` and the
   `ps2_recompiled_functions.h` / `ps2_recompiled_stubs.h` headers into
   `ps2xRuntime/include/` (these paths are gitignored).
4. Build `ps2xRuntime` (Windows: llvm-mingw, `PLATFORM=Desktop`,
   `PS2X_ENABLE_LTO=OFF`).
5. Put `ps2EntryRunner.exe`, `run.bat`, `SLUS_216.21`, `SYSTEM.CNF`, `LIB31\` and
   the `.CVM` files from your disc in one folder and run `run.bat`.
6. Optional, about 3x faster 3D: run once with `PS2X_VU1_CAPTURE=<folder>` through the scenes you
   want, then reconfigure with `-DPS2X_VU1_MICROCODE_DIR=<folder>` and
   `-DPS2X_VU1_GENERATOR_SOURCE=p3fes/tools/vu1/generar_vu1.cpp` and rebuild. The VU1 microcode is
   compiled to C++ and linked in; microcode that wasn't captured is still interpreted. The captured
   micromemories and the generated C++ are game data: keep them out of the repository.

## Engine updates

The runtime is a shared library (`ps2runtime.dll` / `libps2_runtime.so`, CMake option `PS2X_SHARED_RUNTIME`, on by
default on desktop). The executable only holds the recompiled code, the compiled VU1 microcode and a small `main`
(`ps2xRuntime/src/game_main.cpp`) that hands the function table to the runtime. Fixes to the runtime therefore ship
as a new library without rebuilding the recompiled code.

`p3fes/scripts/publish_dev_build.sh <windows build dir> "<notes>"` pushes the Windows engine (no game code: the
script only publishes a fixed list of files) to the `dev-builds` branch, and `Update.bat` / `Update.ps1`
(`p3fes/launcher/`) download it next to an existing install. `ps2x_abi.h` holds a hash of the runtime headers the
recompiled code depends on: the executable passes its hash to the runtime, which warns on a mismatch, and the updater
refuses an engine whose hash differs from the install's `game_abi.txt`.

## Status

See the [project README](../README.md#status).

## License

PS2Recomp is GPL; the IPU and SIO2 code is adapted from PCSX2 (GPL-3.0+).
Keep this repository private, or publish the source alongside any binaries you share.
