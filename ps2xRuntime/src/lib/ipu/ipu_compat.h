// Compatibility layer that lets the PCSX2 IPU sources (pcsx2/IPU, GPL-3.0+,
// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team) build inside the PS2Recomp
// runtime. It replaces PCSX2's Common.h / MultiISA plumbing with the minimal
// types, the DMA channel model and the event hooks those files use.
// Needs proper testing beyond the Sofdec titles it was written against.

#pragma once

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#include <emmintrin.h>
#include <smmintrin.h>

using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;
using uint = unsigned int;
using uptr = uintptr_t;
using sptr = intptr_t;

union alignas(16) u128
{
    u8 _u8[16];
    u16 _u16[8];
    u32 _u32[4];
    u64 _u64[2];
    s8 _s8[16];
    s16 _s16[8];
    s32 _s32[4];
    s64 _s64[2];
    struct
    {
        u64 lo;
        u64 hi;
    };
};
using mem128_t = u128;

#ifndef ARCH_X86
#define ARCH_X86 1
#endif
#ifndef _M_X86
#define _M_X86 1
#endif

// PCSX2 marks out-of-line definitions in .cpp files with __fi; they must stay
// externally visible here, so __fi is a no-op (in-class bodies are inline anyway).
#define __fi
#if defined(_MSC_VER) && !defined(__clang__)
#define __noinline __declspec(noinline)
#else
#define __noinline __attribute__((noinline))
#endif
#define __ri

#define pxAssert(cond) ((void)0)
#define pxAssume(cond) ((void)0)
#define pxAssertMsg(cond, msg) ((void)0)
#define pxFail(msg) ((void)0)
#define jNO_DEFAULT \
    default:        \
        break;

namespace ps2ipu
{
    bool logEnabled();
    void logf(const char *fmt, ...);
}

#define IPU_LOG(...)                       \
    do                                     \
    {                                      \
        if (ps2ipu::logEnabled())          \
            ps2ipu::logf(__VA_ARGS__);     \
    } while (0)
#define DMA_LOG(...) IPU_LOG(__VA_ARGS__)

struct IpuConsoleShim
{
    template <typename... Args>
    void WriteLn(const char *fmt = "", Args... args) const
    {
        if (ps2ipu::logEnabled())
            ps2ipu::logf(fmt, args...);
    }
    template <typename... Args>
    void Warning(const char *fmt, Args... args) const
    {
        if (ps2ipu::logEnabled())
            ps2ipu::logf(fmt, args...);
    }
    template <typename... Args>
    void Error(const char *fmt, Args... args) const
    {
        if (ps2ipu::logEnabled())
            ps2ipu::logf(fmt, args...);
    }
};
inline constexpr IpuConsoleShim Console{};
inline constexpr IpuConsoleShim DevCon{};

static constexpr bool IsDebugBuild = false;

#define MULTI_ISA_DEF(...) __VA_ARGS__
#define MULTI_ISA_UNSHARED_START
#define MULTI_ISA_UNSHARED_END
#define MULTI_ISA_COMPILE_ONCE 1
#define MULTI_ISA_SELECT(name) name

namespace StringUtil
{
    template <typename... Args>
    std::string StdStringFromFormat(const char *fmt, Args... args)
    {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), fmt, args...);
        return buffer;
    }
}

inline void CopyQWC(void *dest, const void *src)
{
    std::memcpy(dest, src, 16);
}

using r128 = __m128i;
inline r128 r128_load(const void *p) { return _mm_loadu_si128(static_cast<const __m128i *>(p)); }
inline void r128_store(void *p, r128 v) { _mm_storeu_si128(static_cast<__m128i *>(p), v); }
inline r128 r128_zero() { return _mm_setzero_si128(); }
inline r128 r128_from_u32_dup(u32 v) { return _mm_set1_epi32(static_cast<int>(v)); }

// ---------------------------------------------------------------------------
// EE event model. PCSX2 schedules IPU work and the IPU DMA channels through
// cpuRegs.interrupt/eCycle; here those become pending flags that
// ps2ipu::runPending() services until the IPU stalls on input or output.
// ---------------------------------------------------------------------------
enum IpuEventType
{
    DMAC_FROM_IPU = 3,
    DMAC_TO_IPU = 4,
    IPU_PROCESS = 14,
};

enum
{
    INTC_IPU = 8,
};

struct IpuCpuRegsShim
{
    u32 interrupt = 0;
    u32 eCycle[32] = {};
    u64 cycle = 0;
    u32 pc = 0;
};
extern IpuCpuRegsShim cpuRegs;

#define BIAS 2
void CPU_INT(int eventType, int cycles);
#define CPU_SET_DMASTALL(eventType, stalled) ((void)0)

void hwIntcIrq(int cause);
void hwDmacIrq(int channelCause);

// ---------------------------------------------------------------------------
// DMA channel model (EE DMAC chain tags, CHCR layout).
// ---------------------------------------------------------------------------
enum TagID
{
    TAG_REFE = 0,
    TAG_CNT,
    TAG_NEXT,
    TAG_REF,
    TAG_REFS,
    TAG_CALL,
    TAG_RET,
    TAG_END,
};

enum DMAModes
{
    NORMAL_MODE = 0,
    CHAIN_MODE = 1,
    INTERLEAVE_MODE = 2,
};

enum
{
    STS_fromIPU = 3,
};

union tDMA_TAG
{
    struct
    {
        u32 QWC : 16;
        u32 _reserved : 10;
        u32 PCE : 2;
        u32 ID : 3;
        u32 IRQ : 1;
    };
    u32 _u32;

    tDMA_TAG() : _u32(0) {}
    tDMA_TAG(u32 value) : _u32(value) {}
};

union tDMA_CHCR
{
    struct
    {
        u32 DIR : 1;
        u32 _reserved1 : 1;
        u32 MOD : 2;
        u32 ASP : 2;
        u32 TTE : 1;
        u32 TIE : 1;
        u32 STR : 1;
        u32 _reserved2 : 7;
        u32 TAG : 16;
    };
    u32 _u32;

    tDMA_TAG tag() const { return tDMA_TAG(static_cast<u32>(TAG) << 16); }
};

struct DMACh
{
    tDMA_CHCR chcr{};
    u32 madr = 0;
    u32 qwc = 0;
    u32 tadr = 0;
    u32 asr0 = 0;
    u32 asr1 = 0;

    bool transfer(const char *name, tDMA_TAG *ptag);
};

extern DMACh ipu0ch; // FROM_IPU, channel 3
extern DMACh ipu1ch; // TO_IPU, channel 4

struct IpuDmacRegsShim
{
    struct
    {
        u32 STS = 0;
    } ctrl;
    struct
    {
        u32 ADDR = 0;
    } stadr;
};
extern IpuDmacRegsShim dmacRegs;

tDMA_TAG *dmaGetAddr(u32 addr, bool write);
bool hwDmacSrcChain(DMACh &dma, int id);
void hwDmacSrcTadrInc(DMACh &dma);
