// GOW-Port: el mismo consumidor raylib para el juego y los controles sin ELF.
#pragma once

#include "raylib.h"
#include "runtime/gs/gs_shared_present.h"
#include "runtime/gs/gs_host_acquired_capture.h" // GOW-Port: captura independiente y opt-in.

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

    // GOW-Port: InitWindow precede a capture; Initialize del GS precede a restore.
    void captureHostContext();
    void restoreHostContext();
    View renderFrame(GS &gs, uint64_t vsyncTick, const std::function<void()> &overlay = {});
    // GOW-Port: unir primero el hilo del juego; mantiene el host vivo al destruir GS.
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
    uint32_t diagnosticSharedFrames = 0u; // GOW-Port: salida opcional acotada.
    GSHostAcquiredCapture acquiredCapture; // GOW-Port: sin opción no descarga píxeles.
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
