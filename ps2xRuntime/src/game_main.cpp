// Entry point of the game executable when the runtime is a shared library (PS2X_SHARED_RUNTIME): the executable holds
// the recompiled code (and compiled VU1 microcode), hands the function table to the runtime and runs it. Runtime fixes
// then ship as a new ps2runtime.dll / libps2_runtime.so without rebuilding the recompiled code.
#include "ps2_runtime.h"
#include "ps2x_abi.h"

int main(int argc, char *argv[])
{
    ps2SetRecompiledFunctionTable({g_ps2RecompiledFunctionTable, g_ps2RecompiledFunctionTableBase,
                                   g_ps2RecompiledFunctionTableEnd, g_ps2RecompiledFunctionTableSlotCount});
    return ps2xRuntimeMain(argc, argv, PS2X_RUNTIME_ABI_HASH);
}
