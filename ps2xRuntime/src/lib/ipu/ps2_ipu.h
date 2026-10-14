// Public interface of the IPU (MPEG decoder) emulation used by PS2Memory.
// The decoder itself is adapted from PCSX2 (GPL-3.0+); see ipu_compat.h.
#pragma once

#include <cstdint>
#include <functional>

namespace ps2ipu
{
    struct ChannelRegs
    {
        uint32_t *chcr = nullptr;
        uint32_t *madr = nullptr;
        uint32_t *qwc = nullptr;
        uint32_t *tadr = nullptr;
        uint32_t *asr0 = nullptr;
        uint32_t *asr1 = nullptr;
    };

    struct Host
    {
        uint8_t *rdram = nullptr;
        uint32_t rdramSize = 0;
        uint8_t *scratchpad = nullptr;
        ChannelRegs fromIpu; // DMA channel 3
        ChannelRegs toIpu;   // DMA channel 4
        uint32_t *dmacCtrl = nullptr;  // D_CTRL
        uint32_t *dmacStadr = nullptr; // D_STADR
        // Channel finished: clear STR already done; raise D_STAT cause (3 or 4).
        std::function<void(uint32_t cause)> dmacIrq;
        // INTC cause 8 (IPU).
        std::function<void()> intcIrq;
    };

    void attach(const Host &host);
    void reset();

    bool isRegister(uint32_t physAddr);
    uint32_t read32(uint32_t physAddr);
    uint64_t read64(uint32_t physAddr);
    void write32(uint32_t physAddr, uint32_t value);
    void write64(uint32_t physAddr, uint64_t value);

    // EE-side FIFO ports: IPU_OUT_FIFO (0x10007000) and IPU_IN_FIFO (0x10007010).
    void readOutFifo(void *quadword);
    void writeInFifo(const void *quadword);

    // A CHCR write with STR set on channel 3 (FROM_IPU) or 4 (TO_IPU).
    void dmaStart(int channel);
}
