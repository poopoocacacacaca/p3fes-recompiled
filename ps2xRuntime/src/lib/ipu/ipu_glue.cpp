// Glue between the PCSX2-derived IPU core and the PS2Recomp runtime.
// SPDX-License-Identifier: GPL-3.0+  (IPU core: SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team)
//
// Model: the IPU runs synchronously on the EE thread. Every EE access to an IPU
// register, FIFO port or IPU DMA channel loads the channel registers, performs
// the access, then services pending IPU/DMA events until the IPU stalls waiting
// for input data or output space, and writes the channel registers back.

#include "ps2_ipu.h"

#include "IPU.h"
#include "IPU_MultiISA.h"
#include "IPUdma.h"

#include <cstdarg>
#include <cstdlib>

IpuCpuRegsShim cpuRegs;
DMACh ipu0ch;
DMACh ipu1ch;
IpuDmacRegsShim dmacRegs;
alignas(16) IPUregisters ipuRegs;

void ReadFIFO_IPUout(mem128_t *out);
void WriteFIFO_IPUin(const mem128_t *value);

namespace
{
    ps2ipu::Host g_host;
    bool g_attached = false;
    int g_depth = 0;

    void loadChannel(DMACh &ch, const ps2ipu::ChannelRegs &regs)
    {
        ch.chcr._u32 = regs.chcr ? *regs.chcr : 0u;
        ch.madr = regs.madr ? *regs.madr : 0u;
        ch.qwc = regs.qwc ? (*regs.qwc & 0xFFFFu) : 0u;
        ch.tadr = regs.tadr ? *regs.tadr : 0u;
        ch.asr0 = regs.asr0 ? *regs.asr0 : 0u;
        ch.asr1 = regs.asr1 ? *regs.asr1 : 0u;
    }

    void storeChannel(const DMACh &ch, const ps2ipu::ChannelRegs &regs)
    {
        if (regs.chcr)
            *regs.chcr = ch.chcr._u32;
        if (regs.madr)
            *regs.madr = ch.madr;
        if (regs.qwc)
            *regs.qwc = ch.qwc & 0xFFFFu;
        if (regs.tadr)
            *regs.tadr = ch.tadr;
        if (regs.asr0)
            *regs.asr0 = ch.asr0;
        if (regs.asr1)
            *regs.asr1 = ch.asr1;
    }

    void syncIn()
    {
        if (g_depth++ != 0)
            return;
        loadChannel(ipu0ch, g_host.fromIpu);
        loadChannel(ipu1ch, g_host.toIpu);
        dmacRegs.ctrl.STS = g_host.dmacCtrl ? ((*g_host.dmacCtrl >> 4) & 3u) : 0u;
        dmacRegs.stadr.ADDR = g_host.dmacStadr ? *g_host.dmacStadr : 0u;
    }

    void runPending()
    {
        // Each handler either makes progress or parks the IPU/DMA in a waiting
        // state without scheduling a new event, so this terminates; the guard
        // is only a safety net against a livelock bug.
        for (int guard = 0; guard < 1000000 && cpuRegs.interrupt != 0u; ++guard)
        {
            if (cpuRegs.interrupt & (1u << IPU_PROCESS))
            {
                cpuRegs.interrupt &= ~(1u << IPU_PROCESS);
                IPUProcessInterrupt();
                continue;
            }
            if (cpuRegs.interrupt & (1u << DMAC_TO_IPU))
            {
                cpuRegs.interrupt &= ~(1u << DMAC_TO_IPU);
                ipu1Interrupt();
                continue;
            }
            if (cpuRegs.interrupt & (1u << DMAC_FROM_IPU))
            {
                cpuRegs.interrupt &= ~(1u << DMAC_FROM_IPU);
                ipu0Interrupt();
                continue;
            }
            cpuRegs.interrupt = 0u;
        }
    }

    void syncOut()
    {
        if (--g_depth != 0)
            return;
        runPending();
        storeChannel(ipu0ch, g_host.fromIpu);
        storeChannel(ipu1ch, g_host.toIpu);
        if (g_host.dmacStadr && dmacRegs.ctrl.STS == STS_fromIPU)
            *g_host.dmacStadr = dmacRegs.stadr.ADDR;
    }

    struct Scope
    {
        Scope() { syncIn(); }
        ~Scope() { syncOut(); }
    };
}

namespace ps2ipu
{
    bool logEnabled()
    {
        static const bool enabled = std::getenv("PS2X_IPU_TRACE") != nullptr;
        return enabled;
    }

    void logf(const char *fmt, ...)
    {
        std::va_list args;
        va_start(args, fmt);
        std::fputs("[ipu] ", stderr);
        std::vfprintf(stderr, fmt, args);
        std::fputc('\n', stderr);
        va_end(args);
    }

    void attach(const Host &host)
    {
        g_host = host;
        g_attached = true;
        reset();
    }

    void reset()
    {
        cpuRegs = {};
        ipu0ch = {};
        ipu1ch = {};
        dmacRegs = {};
        ipuReset();
    }

    bool isRegister(uint32_t physAddr)
    {
        return physAddr >= 0x10002000u && physAddr < 0x10002040u;
    }

    uint32_t read32(uint32_t physAddr)
    {
        if (!g_attached)
            return 0u;
        Scope scope;
        return ipuRead32(physAddr);
    }

    uint64_t read64(uint32_t physAddr)
    {
        if (!g_attached)
            return 0u;
        Scope scope;
        return ipuRead64(physAddr);
    }

    void write32(uint32_t physAddr, uint32_t value)
    {
        if (!g_attached)
            return;
        Scope scope;
        (void)ipuWrite32(physAddr, value);
    }

    void write64(uint32_t physAddr, uint64_t value)
    {
        if (!g_attached)
            return;
        Scope scope;
        if (ipuWrite64(physAddr, value))
            (void)ipuWrite32(physAddr, static_cast<uint32_t>(value));
    }

    void readOutFifo(void *quadword)
    {
        std::memset(quadword, 0, 16);
        if (!g_attached)
            return;
        Scope scope;
        ReadFIFO_IPUout(static_cast<mem128_t *>(quadword));
        // Space was freed in the output FIFO: let a stalled decode continue.
        if (ipuRegs.ctrl.BUSY && IPUCoreStatus.WaitingOnIPUFrom)
        {
            IPUCoreStatus.WaitingOnIPUFrom = false;
            CPU_INT(IPU_PROCESS, 1);
        }
    }

    void writeInFifo(const void *quadword)
    {
        if (!g_attached)
            return;
        Scope scope;
        alignas(16) mem128_t value;
        std::memcpy(&value, quadword, 16);
        WriteFIFO_IPUin(&value);
    }

    void dmaStart(int channel)
    {
        if (!g_attached)
            return;
        Scope scope;
        if (channel == 3)
        {
            if (ipu0ch.chcr.MOD == NORMAL_MODE && ipu0ch.qwc == 0u)
                ipu0ch.qwc = 0x10000u;
            dmaIPU0();
        }
        else if (channel == 4)
        {
            dmaIPU1();
        }
    }
}

// ---------------------------------------------------------------------------
// Hooks required by the PCSX2 IPU core
// ---------------------------------------------------------------------------

void CPU_INT(int eventType, int cycles)
{
    cpuRegs.interrupt |= 1u << eventType;
    cpuRegs.eCycle[eventType] = static_cast<u32>(cycles);
}

void hwIntcIrq(int cause)
{
    if (cause == INTC_IPU && g_host.intcIrq)
        g_host.intcIrq();
}

void hwDmacIrq(int channelCause)
{
    if (g_host.dmacIrq)
        g_host.dmacIrq(static_cast<uint32_t>(channelCause));
}

bool DMACh::transfer(const char *, tDMA_TAG *ptag)
{
    if (!ptag)
    {
        chcr.STR = 0;
        return false;
    }
    chcr.TAG = ptag[0]._u32 >> 16;
    qwc = ptag[0].QWC;
    return true;
}

tDMA_TAG *dmaGetAddr(u32 addr, bool)
{
    if (addr & 0x80000000u)
    {
        if (!g_host.scratchpad)
            return nullptr;
        return reinterpret_cast<tDMA_TAG *>(g_host.scratchpad + (addr & 0x3FF0u));
    }
    const u32 phys = addr & 0x0FFFFFF0u;
    if (!g_host.rdram || phys + 16u > g_host.rdramSize)
        return nullptr;
    return reinterpret_cast<tDMA_TAG *>(g_host.rdram + phys);
}

bool hwDmacSrcChain(DMACh &dma, int id)
{
    u32 temp;
    switch (id)
    {
    case TAG_REFE:
        dma.tadr += 16;
        return true;
    case TAG_CNT:
        dma.madr = dma.tadr + 16;
        dma.tadr = dma.madr;
        return false;
    case TAG_NEXT:
        temp = dma.madr;
        dma.madr = dma.tadr + 16;
        dma.tadr = temp;
        return false;
    case TAG_REF:
    case TAG_REFS:
        dma.tadr += 16;
        return false;
    case TAG_CALL:
        temp = dma.madr;
        dma.madr = dma.tadr + 16;
        if (((dma.chcr.ASP) & 3u) == 0u)
            dma.asr0 = dma.madr + (dma.qwc << 4);
        else if (((dma.chcr.ASP) & 3u) == 1u)
            dma.asr1 = dma.madr + (dma.qwc << 4);
        else
            return true; // call stack overflow
        dma.chcr.ASP = dma.chcr.ASP + 1u;
        dma.tadr = temp;
        return false;
    case TAG_RET:
        dma.madr = dma.tadr + 16;
        if (dma.chcr.ASP == 2u)
        {
            dma.tadr = dma.asr1;
            dma.asr1 = 0;
            dma.chcr.ASP = 1u;
        }
        else if (dma.chcr.ASP == 1u)
        {
            dma.tadr = dma.asr0;
            dma.asr0 = 0;
            dma.chcr.ASP = 0u;
        }
        else
        {
            return true;
        }
        return false;
    case TAG_END:
        dma.madr = dma.tadr + 16;
        return true;
    default:
        return true;
    }
}

void hwDmacSrcTadrInc(DMACh &dma)
{
    if (dma.chcr.STR == 0)
        return;
    if (dma.chcr.MOD != CHAIN_MODE)
        return;
    const u16 tagid = (dma.chcr.TAG >> 12) & 0x7;
    if (tagid == TAG_CNT)
        dma.tadr = dma.madr;
}
