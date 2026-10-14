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

## Status

See the [project README](../README.md#status).

## License

PS2Recomp is GPL; the IPU and SIO2 code is adapted from PCSX2 (GPL-3.0+).
Keep this repository private, or publish the source alongside any binaries you share.
