// VU1 on PCSX2's microVU recompiler (third_party/kzvu); see ps2_host_vu1.h.
// Integration modelled on KillzoneRecomp's src/kz_vu.cpp (GPL-3.0).
#include "ps2_host_vu1.h"

#include "runtime/ps2_memory.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(PS2X_HAVE_KZVU)
#include "kzvu.h"

namespace
{
    PS2Memory *g_memory = nullptr;
    uint64_t g_codeGeneration = ~0ull;
    // The runtime's VIF1 is synchronous: programs run to their E bit.
    constexpr uint32_t kBudget = 1u << 24;

    void onXgkick(void *, const uint8_t *packet, uint32_t bytes, uint32_t)
    {
        if (g_memory)
            g_memory->submitGifPacket(GifPathId::Path1, packet, bytes);
    }

    bool init()
    {
        const char *mode = std::getenv("PS2X_VU1");
        if (mode && (std::strcmp(mode, "interp") == 0 || std::strcmp(mode, "builtin") == 0))
        {
            std::fprintf(stderr, "[vu1] built-in interpreter (PS2X_VU1=%s)\n", mode);
            return false;
        }
        KzvuConfig cfg;
        // PCSX2's defaults for a game with no GameIndex fixes (Persona 3 FES has none): clamp mode 1 ("normal"),
        // mVU flag hack on, I-bit hack off.
        cfg.useJit = !(mode && std::strcmp(mode, "pcsx2interp") == 0);
        cfg.clampMode = 1;
        cfg.iBitHack = false;
        cfg.flagHack = true;
        cfg.vu0UseJit = false; // VU0 micro mode stays on the runtime's own path
        if (const char *clamp = std::getenv("PS2X_VU1_CLAMP"))
            cfg.clampMode = std::atoi(clamp);
        cfg.xgkick = &onXgkick;
        std::string error;
        if (!kzvuInit(cfg, &error))
        {
            std::fprintf(stderr, "[vu1] microVU unavailable (%s): built-in interpreter\n", error.c_str());
            return false;
        }
        std::fprintf(stderr, "[vu1] %s (PS2X_VU1=interp for the built-in interpreter), clamp mode %d\n",
                     cfg.useJit ? "PCSX2 microVU recompiler" : "PCSX2 VU interpreter", cfg.clampMode);
        return true;
    }

    void prepare(uint32_t top, uint32_t itop, uint32_t fbrst, uint64_t codeGeneration)
    {
        if (codeGeneration != g_codeGeneration)
        {
            // Micro memory changed since the last program: microVU re-validates its cached programs by content.
            kzvuMicroWritten(0, kKzvuCodeSize);
            g_codeGeneration = codeGeneration;
        }
        kzvuSetFBRST(fbrst);
        kzvuSetTop(top, itop);
    }

    void finish(uint32_t *vpuStat)
    {
        for (int i = 0; i < 16 && kzvuRunning() && (kzvuVpuStat() & 0x0600u) == 0u; ++i)
            kzvuContinue(kBudget);
        if (vpuStat)
            *vpuStat = (*vpuStat & ~0x0600u) | (kzvuVpuStat() & 0x0600u);
    }
}

namespace ps2_host_vu1
{
    bool enabled()
    {
        static const bool on = init();
        return on;
    }
    uint8_t *codeMem() { return enabled() ? kzvuCodeMem() : nullptr; }
    uint8_t *dataMem() { return enabled() ? kzvuDataMem() : nullptr; }
    bool ownsBuffer(const uint8_t *p) { return enabled() && p && (p == kzvuCodeMem() || p == kzvuDataMem()); }
    void setMemory(PS2Memory *memory) { g_memory = memory; }

    void mscal(uint32_t startPcBytes, uint32_t top, uint32_t itop, uint32_t fbrst, uint64_t codeGeneration,
               uint32_t *vpuStat)
    {
        prepare(top, itop, fbrst, codeGeneration);
        kzvuExecute(startPcBytes, kBudget);
        finish(vpuStat);
    }

    void mscnt(uint32_t top, uint32_t itop, uint32_t fbrst, uint64_t codeGeneration, uint32_t *vpuStat)
    {
        prepare(top, itop, fbrst, codeGeneration);
        kzvuExecute(0xFFFFFFFFu, kBudget);
        finish(vpuStat);
    }
}

#else // built without kzvu

namespace ps2_host_vu1
{
    bool enabled() { return false; }
    uint8_t *codeMem() { return nullptr; }
    uint8_t *dataMem() { return nullptr; }
    bool ownsBuffer(const uint8_t *) { return false; }
    void setMemory(PS2Memory *) {}
    void mscal(uint32_t, uint32_t, uint32_t, uint32_t, uint64_t, uint32_t *) {}
    void mscnt(uint32_t, uint32_t, uint32_t, uint64_t, uint32_t *) {}
}

#endif
