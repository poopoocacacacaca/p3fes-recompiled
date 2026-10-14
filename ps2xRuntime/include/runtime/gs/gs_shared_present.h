// Adapted from Taylor N. Albarnaz / LightVelox (GPL-3.0).
// Source: PS2Recomp sotc-port, ac9efa070638ad3b3accd284de6f898d5ab271d1.
#pragma once

#include <cstdint>

namespace GSSharedPresent
{
    struct Frame
    {
        uint32_t texture = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint64_t sequence = 0;
        uint64_t renderSequence = 0;
        uint64_t contentHash = 0;
        bool hashValid = false;
        // keep the CRTC selection without downloading the image to the host.
        uint32_t displayFbp = 0;
        uint32_t sourceFbp = 0;
        bool usedPreferred = false;
        // diagnostics use this image's tick, not a later request's.
        uint64_t vsyncTick = 0;
        // PMODE off is an empty PresentationFrame for the host.
        bool blank = false;
        // mode of the request that produced this texture, not of the later CRTC.
        uint64_t smode2 = 0;
    };

    void CaptureHostContext();
    void RestoreHostContext();
    bool Active();
    bool Acquire(Frame &frame);
    void ShutdownHost();
}
