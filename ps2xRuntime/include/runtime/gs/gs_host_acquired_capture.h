// explicit, bounded capture of the acquired texture; off by default.
#pragma once
#include "runtime/gs/gs_shared_present.h"
#include <cstdint>
#include <string>

class GSHostAcquiredCapture
{
public:
    struct Options
    {
        std::string directory;
        uint64_t fromTick = 0u;
        uint32_t limit = 6u;
    };
    GSHostAcquiredCapture();
    explicit GSHostAcquiredCapture(Options options);
    // The caller keeps the Acquire reservation and the current host context.
    // It does not acquire another slot or query the GS/VRAM; failures do not interrupt the game.
    bool capture(const GSSharedPresent::Frame &frame, uint64_t hostTick) noexcept;

private:
    Options options;
    uint32_t attempts = 0u;
    uint64_t lastSequence = 0u;
};
