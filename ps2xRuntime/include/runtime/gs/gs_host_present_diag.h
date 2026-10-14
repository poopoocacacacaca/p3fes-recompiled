// optional log window; does not control rendering, fences or Acquire.
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
        // earlier acquisitions do not use up the per-instance quota of 4096.
        if (hostTick < fromTick || records >= 4096u) return false;
        ++records;
        return true;
    }
}
