// VU1 on PCSX2's microVU recompiler (third_party/kzvu), in place of the built-in VU1Interpreter.
//
// microVU compiles each VU1 microprogram to x86-64 the first time it runs and caches it by content, so every program
// the game uploads runs as native code (the ahead-of-time compiled microcode in the game executable only covers the
// programs captured beforehand). PS2X_VU1=interp keeps the built-in interpreter.
//
// kzvu owns VU1 code and data memory (fixed buffers inside the runtime library: the recompiled code addresses them
// directly), so PS2Memory uses those buffers instead of allocating its own.
#pragma once

#include <cstdint>

class PS2Memory;

namespace ps2_host_vu1
{
    // True when VU1 runs on microVU (built with kzvu, not disabled, initialised). Initialises kzvu on first call.
    bool enabled();
    uint8_t *codeMem();
    uint8_t *dataMem();
    bool ownsBuffer(const uint8_t *p);
    // XGKICK packets go to this memory's GIF PATH1.
    void setMemory(PS2Memory *memory);
    // MSCAL / MSCNT: run until the E bit (or a D/T stop); vpuStat gets the VU1 D/T stop bits.
    void mscal(uint32_t startPcBytes, uint32_t top, uint32_t itop, uint32_t fbrst, uint64_t codeGeneration,
               uint32_t *vpuStat);
    void mscnt(uint32_t top, uint32_t itop, uint32_t fbrst, uint64_t codeGeneration, uint32_t *vpuStat);
}
