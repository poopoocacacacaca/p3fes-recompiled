// One VU1 interpreter step (an instruction pair) and its latency accounting. Used by run() and by the compiled
// microprograms (tools/vu1/generar_vu1.cpp), which call it with each pair already decoded as a constant so the
// compiler can simplify the accounting.
#ifndef PS2_VU1_STEP_INL
#define PS2_VU1_STEP_INL

#include "runtime/ps2_vu1.h"
#include <algorithm>
#include <bit>
#include <cstring>
#if defined(_MSC_VER) || defined(__SSE4_1__)
#include <immintrin.h>
#endif

// VU arithmetic does not depend on /fp:fast. The runtime builds with /fp:fast, which lets the compiler fuse
// a*b+c into FMA depending on where the code is: the interpreter and the compiled microcode (or two copies of
// the interpreter) could give different results. Here each operation rounds separately, as in the PS2's FMACs
// (and in PCSX2).
#if defined(_MSC_VER)
#pragma float_control(precise, on, push)
#pragma fp_contract(off)
#endif

#if defined(_MSC_VER)
#define VU1_STEP_INLINE __forceinline
#else
#define VU1_STEP_INLINE inline __attribute__((always_inline))
#endif

static constexpr uint8_t vu1StepLane(uint32_t component)
{
    return static_cast<uint8_t>(1u << (3u - component));
}

// VF/ACC register writes with a single 16-byte store. If a register is written with several 4-byte stores and
// the next pair reads it with a 16-byte load (the norm for SSE FMACs), store->load forwarding fails and the load
// waits for the stores to reach the cache. Lanes are blended bitwise (no floating-point operations): the result is
// identical.
VU1_STEP_INLINE void vu1StoreVf0(float *vf0)
{
#if defined(_MSC_VER) || defined(__SSE4_1__)
    _mm_storeu_ps(vf0, _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f));
#else
    vf0[0] = 0.0f;
    vf0[1] = 0.0f;
    vf0[2] = 0.0f;
    vf0[3] = 1.0f;
#endif
}

// dest: VU mask (bit 3 = x ... bit 0 = w).
VU1_STEP_INLINE void vu1StoreLanes(float *dst, const float *src, uint32_t dest)
{
#if defined(_MSC_VER) || defined(__SSE4_1__)
    alignas(16) static constexpr int32_t kMasks[16][4] = {
        {0, 0, 0, 0}, {0, 0, 0, -1}, {0, 0, -1, 0}, {0, 0, -1, -1},
        {0, -1, 0, 0}, {0, -1, 0, -1}, {0, -1, -1, 0}, {0, -1, -1, -1},
        {-1, 0, 0, 0}, {-1, 0, 0, -1}, {-1, 0, -1, 0}, {-1, 0, -1, -1},
        {-1, -1, 0, 0}, {-1, -1, 0, -1}, {-1, -1, -1, 0}, {-1, -1, -1, -1}};
    dest &= 0xFu;
    if (dest == 0u)
        return;
    const __m128 mask = _mm_load_ps(reinterpret_cast<const float *>(kMasks[dest]));
    _mm_storeu_ps(dst, _mm_blendv_ps(_mm_loadu_ps(dst), _mm_loadu_ps(src), mask));
#else
    if (dest & 0x8u)
        dst[0] = src[0];
    if (dest & 0x4u)
        dst[1] = src[1];
    if (dest & 0x2u)
        dst[2] = src[2];
    if (dest & 0x1u)
        dst[3] = src[3];
#endif
}

VU1_STEP_INLINE uint64_t VU1Interpreter::calculatePairReadyCycle(const DecodedInstructionPair &decoded) const
{
    uint64_t ready = m_cycle;
    const InstructionUsage *usages[2] = {
        &decoded.upperUsage,
        &decoded.lowerUsage};
    for (const InstructionUsage *usage : usages)
    {
        if (!usage)
            continue;
        for (uint32_t index = 0; index < usage->vfReadCount; ++index)
        {
            // walk only the set bits (bit 3 = x ... bit 0 = w).
            const VfAccess &access = usage->vfRead[index];
            const auto &vfReady = m_vfReady[access.reg];
            for (uint32_t lanes = access.lanes & 0xFu; lanes != 0u; lanes &= lanes - 1u)
                ready = std::max(ready, vfReady[3u - static_cast<uint32_t>(std::countr_zero(lanes))]);
        }
        for (uint32_t regs = usage->viRead & 0xFFFEu; regs != 0u; regs &= regs - 1u)
            ready = std::max(ready, m_viReady[static_cast<uint32_t>(std::countr_zero(regs))]);
        for (uint32_t lanes = usage->accRead & 0xFu; lanes != 0u; lanes &= lanes - 1u)
            ready = std::max(ready, m_accReady[3u - static_cast<uint32_t>(std::countr_zero(lanes))]);
    }

    if (decoded.lowerUsage.pipeline == PipelineFdiv && m_fdiv.valid)
        ready = std::max(ready, m_fdiv.readyCycle);
    if (decoded.lowerUsage.pipeline == PipelineEfu)
        ready = std::max(ready, m_efuResourceReady);
    if (decoded.lowerUsage.waitQ && m_fdiv.valid)
        ready = std::max(ready, m_fdiv.readyCycle);
    if (decoded.lowerUsage.waitP)
    {
        for (const ScalarPipelineEntry &entry : m_efu)
            if (entry.valid)
                ready = std::max(ready, entry.readyCycle);
    }
    if (decoded.lowerUsage.pipeline == PipelineXgkick && m_xgkick.active)
        ready = std::max(ready, m_cycle + 1u);
    return ready;
}

VU1_STEP_INLINE void VU1Interpreter::markPairWrites(const DecodedInstructionPair &decoded)
{
    const VfAccess lowerWrite = decoded.lowerUsage.vfWrite;
    if (lowerWrite.reg != 0u &&
        decoded.suppressedLowerVf != lowerWrite.reg)
    {
        const uint32_t latency = decoded.lowerUsage.vfLatency != 0u
                                     ? decoded.lowerUsage.vfLatency
                                     : decoded.lowerUsage.latency;
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((lowerWrite.lanes & vu1StepLane(component)) != 0u)
                m_vfReady[lowerWrite.reg][component] = m_cycle + latency;
        }
    }

    const VfAccess upperWrite = decoded.upperUsage.vfWrite;
    if (upperWrite.reg != 0u)
    {
        const uint32_t latency = decoded.upperUsage.vfLatency != 0u
                                     ? decoded.upperUsage.vfLatency
                                     : decoded.upperUsage.latency;
        for (uint32_t component = 0; component < 4u; ++component)
        {
            if ((upperWrite.lanes & vu1StepLane(component)) != 0u)
                m_vfReady[upperWrite.reg][component] = m_cycle + latency;
        }
    }

    for (uint32_t regs = decoded.lowerUsage.viWrite & 0xFFFEu; regs != 0u; regs &= regs - 1u)
        m_viReady[std::countr_zero(regs)] = m_cycle + (decoded.lowerUsage.viLatency != 0u ? decoded.lowerUsage.viLatency : decoded.lowerUsage.latency);
    for (uint32_t component = 0; component < 4u; ++component)
    {
        if ((decoded.upperUsage.accWrite & vu1StepLane(component)) != 0u)
            m_accReady[component] = m_cycle + kAccForwardLatency;
    }
}


VU1_STEP_INLINE bool VU1Interpreter::stepPair(const DecodedInstructionPair &decoded, StepContext &context)
{
    if (m_xgkick.active)
        xgkickAdvanceTo(m_cycle); // PATH1 up to date after compiled code (by events)
    uint64_t readyCycle = calculatePairReadyCycle(decoded);
    while (readyCycle > m_cycle)
    {
        if (readyCycle >= context.budgetEnd)
        {
            advanceTo(context.budgetEnd);
            break;
        }
        advanceTo(readyCycle);
        readyCycle = calculatePairReadyCycle(decoded);
    }
    if (m_cycle >= context.budgetEnd)
        return false;

    uint8_t writtenVi = 0u;
    int32_t oldVi = 0;
    if (const uint32_t viWrites = decoded.lowerUsage.viWrite & 0xFFFEu; viWrites != 0u)
    {
        writtenVi = static_cast<uint8_t>(std::countr_zero(viWrites));
        oldVi = m_state.vi[writtenVi];
    }

    const bool direct = m_directRegisterWrites;
    const VfAccess upperWrite = decoded.upperUsage.vfWrite;
    const VfAccess lowerWrite = decoded.lowerUsage.vfWrite;
    const bool hasUpperWrite = !direct && upperWrite.reg != 0u;
    const bool hasLowerWrite = !direct && lowerWrite.reg != 0u && decoded.suppressedLowerVf != lowerWrite.reg;
    const bool hasDistinctLowerWrite = hasLowerWrite && (!hasUpperWrite || lowerWrite.reg != upperWrite.reg);
    float oldUpperVf[4]{};
    float newUpperVf[4]{};
    float oldLowerVf[4]{};
    float newLowerVf[4]{};
    float oldAcc[4]{};
    float newAcc[4]{};
    if (hasUpperWrite)
        std::memcpy(oldUpperVf, m_state.vf[upperWrite.reg], sizeof(oldUpperVf));
    if (hasDistinctLowerWrite)
        std::memcpy(oldLowerVf, m_state.vf[lowerWrite.reg], sizeof(oldLowerVf));
    if (!direct && decoded.upperUsage.accWrite != 0u)
        std::memcpy(oldAcc, m_state.acc, sizeof(oldAcc));

    if (decoded.iBit)
    {
        if (decoded.upperNop)
            m_currentUpperInstruction = decoded.upper;
        else
            execUpper(decoded.upper);
        float immediate = 0.0f;
        std::memcpy(&immediate, &decoded.lower, sizeof(immediate));
        m_state.i = normalizeOperand(immediate);
    }
    else if (decoded.upperVfShadowReg != 0u)
    {
        float oldVf[4]{};
        float upperVf[4]{};
        std::memcpy(oldVf,
                    m_state.vf[decoded.upperVfShadowReg],
                    sizeof(oldVf));
        if (decoded.upperNop)
            m_currentUpperInstruction = decoded.upper;
        else
            execUpper(decoded.upper);
        std::memcpy(upperVf,
                    m_state.vf[decoded.upperVfShadowReg],
                    sizeof(upperVf));
        std::memcpy(m_state.vf[decoded.upperVfShadowReg],
                    oldVf,
                    sizeof(oldVf));
        execLower(decoded.lower, context.vuData, context.dataSize, *context.gs, context.memory, decoded.upper);
        std::memcpy(m_state.vf[decoded.upperVfShadowReg],
                    upperVf,
                    sizeof(upperVf));
    }
    else
    {
        if (decoded.upperNop)
            m_currentUpperInstruction = decoded.upper;
        else
            execUpper(decoded.upper);
        execLower(decoded.lower, context.vuData, context.dataSize, *context.gs, context.memory, decoded.upper);
    }

    m_viBranchBackupValid = false;

    if (hasUpperWrite)
    {
        std::memcpy(newUpperVf, m_state.vf[upperWrite.reg], sizeof(newUpperVf));
        std::memcpy(m_state.vf[upperWrite.reg], oldUpperVf, sizeof(oldUpperVf));
        const uint32_t latency =
            decoded.upperUsage.vfLatency != 0u
                ? decoded.upperUsage.vfLatency
                : decoded.upperUsage.latency;
        queueVfWrite(upperWrite.reg, upperWrite.lanes, newUpperVf, latency);
    }
    if (hasDistinctLowerWrite)
    {
        std::memcpy(newLowerVf, m_state.vf[lowerWrite.reg], sizeof(newLowerVf));
        std::memcpy(m_state.vf[lowerWrite.reg], oldLowerVf, sizeof(oldLowerVf));
        const uint32_t latency = decoded.lowerUsage.vfLatency != 0u
                                     ? decoded.lowerUsage.vfLatency
                                     : decoded.lowerUsage.latency;
        queueVfWrite(lowerWrite.reg, lowerWrite.lanes, newLowerVf, latency);
    }
    if (!direct && decoded.upperUsage.accWrite != 0u)
    {
        std::memcpy(newAcc, m_state.acc, sizeof(newAcc));
        std::memcpy(m_state.acc, oldAcc, sizeof(oldAcc));
        // ACC is forwarded to the next upper instruction. Its arithmetic
        // flags still use the normal four-cycle FMAC timeline.
        queueAccWrite(decoded.upperUsage.accWrite, newAcc,
                      kAccForwardLatency);
    }
    if (writtenVi != 0u && direct)
    {
        m_state.vi[writtenVi] = static_cast<int16_t>(m_state.vi[writtenVi]);
    }
    else if (writtenVi != 0u)
    {
        const int32_t newVi = m_state.vi[writtenVi];
        m_state.vi[writtenVi] = oldVi;
        const uint32_t latency =
            decoded.lowerUsage.viLatency != 0u
                ? decoded.lowerUsage.viLatency
                : decoded.lowerUsage.latency;
        queueViWrite(writtenVi, newVi, latency);
    }

    markPairWrites(decoded);
    if (writtenVi != 0u && decoded.lowerUsage.delaysNextBranchRead)
        recordViWriteForBranch(writtenVi, oldVi);

    // VF0 with a single 16-byte store: the 16-byte read of VF0 in the next pair (vf00w = 1.0 is very common) cannot be
    // forwarded from four 4-byte stores.
    vu1StoreVf0(m_state.vf[0]);
    m_state.vi[0] = 0;

    uint32_t nextPc = m_state.pc + 8u;
    if (nextPc >= context.codeSize)
        nextPc = 0u;
    m_state.pc = nextPc;

    if (m_state.branchPending)
    {
        if (m_state.branchDelay == 0u)
        {
            m_state.pc = m_state.branchTarget & microAddressMask();
            m_state.branchPending = false;
        }
        else
        {
            --m_state.branchDelay;
        }
    }

    const bool dHalt = decoded.dBit && m_state.dBitEnabled;
    const bool tHalt = decoded.tBit && m_state.tBitEnabled;
    const bool haltBit = dHalt || tHalt;
    const bool haltBranch = haltBit && decoded.lowerUsage.pipeline == PipelineBranch;

    if (m_state.haltAfterDelaySlot)
    {
        m_state.stoppedByD = m_pendingHaltD;
        m_state.stoppedByT = m_pendingHaltT;
        context.programEnded = true;
    }
    else if (m_state.ebit)
        context.programEnded = true;
    else if (haltBit && !haltBranch)
    {
        m_state.stoppedByD = dHalt;
        m_state.stoppedByT = tHalt;
        context.programEnded = true;
    }
    else if (decoded.eBit)
        m_state.ebit = true;
    else if (haltBranch)
    {
        m_state.haltAfterDelaySlot = true;
        m_pendingHaltD = dHalt;
        m_pendingHaltT = tHalt;
    }

    advanceOneCycle();
    return !context.programEnded;
}

VU1_STEP_INLINE bool VU1Interpreter::compiledPrologue(StepContext &context)
{
    if (m_cycle >= context.budgetEnd || m_stopRequested)
        return false;
    commitReadyPipelines();
    return true;
}

#if defined(_MSC_VER)
#pragma float_control(pop)
#endif

#endif
