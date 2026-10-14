// GOW-Port: ventana opcional del registro; no controla render, fences ni Acquire.
#pragma once
#include <cstdint>
#include <limits>

namespace GSHostPresentDiag
{
    inline uint64_t parseFromTick(const char *text) noexcept
    {
        if (!text || !*text) return 0u;
        uint64_t value = 0u;
        for (const char *at = text; *at; ++at)
        {
            if (*at < '0' || *at > '9') return 0u;
            const uint64_t digit = static_cast<uint64_t>(*at - '0');
            if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10u) return 0u;
            value = value * 10u + digit;
        }
        return value;
    }

    inline bool consume(uint64_t hostTick, uint64_t fromTick, uint32_t &records) noexcept
    {
        // GOW-Port: las adquisiciones anteriores no gastan el cupo de 4096 por instancia.
        if (hostTick < fromTick || records >= 4096u) return false;
        ++records;
        return true;
    }
}
