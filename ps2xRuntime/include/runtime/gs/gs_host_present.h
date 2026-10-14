// the same raylib consumer for the game and the ELF-less controls.
#pragma once

#include "raylib.h"
#include "runtime/gs/gs_shared_present.h"
#include "runtime/gs/gs_host_acquired_capture.h" // independent, opt-in capture.

#include <cstdint>
#include <functional>
#include <vector>

class GS;

class GSHostPresentation
{
public:
    struct View
    {
        Texture2D texture{};
        uint32_t width = 640u;
        uint32_t height = 448u;
        bool shared = false;
        bool borrowed = false;
        bool blank = true;
        GSSharedPresent::Frame frame{};
    };

    // InitWindow precedes capture; the GS's Initialize precedes restore.
    void captureHostContext();
    void restoreHostContext();
    View renderFrame(GS &gs, uint64_t vsyncTick, const std::function<void()> &overlay = {});
    // join the game thread first; keeps the host alive while destroying the GS.
    void shutdown(GS &gs);

private:
    void createFallback();
    void blankFallback();
    View prepare(GS &gs, uint64_t vsyncTick);
    Texture2D fallback{}; // Propia; las texturas de View compartidas son prestadas.
    std::vector<uint8_t> scratch;
    std::vector<uint8_t> upload;
    View current;
    uint64_t lastTick = 0u;
    uint64_t lastSharedSequence = 0u;
    uint32_t diagnosticSharedFrames = 0u; // optional bounded output.
    GSHostAcquiredCapture acquiredCapture; // without the option no pixels are downloaded.
    uint32_t lastDisplayFbp = UINT32_MAX;
    uint32_t lastSourceFbp = UINT32_MAX;
    uint32_t lastWidth = 0u;
    uint32_t lastHeight = 0u;
    uint32_t uploadDebugCount = 0u;
    bool lastPreferred = false;
    bool started = false;
    bool captureRequested = false;
    bool hasLatched = false;
    bool uploaded = false;
    bool uploadedBlank = false;
    bool modeKnown = false;
    bool sharedMode = false;
};
