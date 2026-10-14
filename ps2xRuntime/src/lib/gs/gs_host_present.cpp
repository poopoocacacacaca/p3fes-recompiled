// GOW-Port: contexto host, selección de frames y dibujo raylib usados por el port.
#include "runtime/gs/gs_host_present.h"
#include "runtime/gs/gs_host_present_diag.h" // GOW-Port: ventana sin lecturas GPU adicionales.
#include "runtime/gs/gs_frontend.h"
#include "runtime/ps2_perf.h"
#include "ps2_log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace
{
    constexpr uint32_t kWidth = 640u;
    constexpr uint32_t kHeight = 512u;
    constexpr uint32_t kDisplayHeight = 448u;
}

void GSHostPresentation::captureHostContext()
{
    if (started)
        return;
    started = true;
    const char *setting = std::getenv("PS2X_GS_DIRECT_PRESENT");
    captureRequested = setting && *setting == '1';
    if (captureRequested)
        GSSharedPresent::CaptureHostContext();
}

void GSHostPresentation::restoreHostContext()
{
    if (captureRequested)
        GSSharedPresent::RestoreHostContext();
}

void GSHostPresentation::createFallback()
{
    if (fallback.id != 0u)
        return;
    Image blank = GenImageColor(kWidth, kHeight, BLANK);
    fallback = LoadTextureFromImage(blank);
    UnloadImage(blank);
    SetTextureFilter(fallback, TEXTURE_FILTER_POINT); // Coincide con la ruta RAM original.
    upload.resize(static_cast<size_t>(kWidth) * kHeight * 4u, 0u);
}

void GSHostPresentation::blankFallback()
{
    if (!uploadedBlank)
    {
        for (size_t offset = 0u; offset < upload.size(); offset += 4u)
        {
            upload[offset] = MAGENTA.r;
            upload[offset + 1u] = MAGENTA.g;
            upload[offset + 2u] = MAGENTA.b;
            upload[offset + 3u] = MAGENTA.a;
        }
        UpdateTexture(fallback, upload.data());
        uploadedBlank = true;
    }
    current.texture = fallback;
    current.width = kWidth;
    current.height = kDisplayHeight;
    current.borrowed = false;
    current.blank = true;
    uploaded = true;
}

GSHostPresentation::View GSHostPresentation::prepare(GS &gs, uint64_t vsyncTick)
{
    createFallback();
    const bool shared = gs.usesSharedPresentation() && GSSharedPresent::Active();
    if (!modeKnown || shared != sharedMode)
    {
        std::fprintf(stderr, "[gs-present] host mode: %s\n", shared ? "shared texture (opt-in)" : "RAM frames");
        std::fflush(stderr);
        modeKnown = true;
        sharedMode = shared;
        hasLatched = uploaded = uploadedBlank = false;
        lastSharedSequence = 0u;
        current = {};
    }
    const bool needsLatch = !hasLatched || vsyncTick != lastTick;
    if (needsLatch)
    {
        gs.latchHostPresentationFrame();
        lastTick = vsyncTick;
        hasLatched = true;
    }
    current.shared = shared;
    if (shared)
    {
        // GOW-Port: la llamada anterior ya terminó EndDrawing, incluido el batch.
        // Acquire puede conservar el slot previo; la ruta normal no descarga píxeles.
        GSSharedPresent::Frame frame;
        if (GSSharedPresent::Acquire(frame))
        {
            current.frame = frame;
            if (frame.sequence != lastSharedSequence)
            {
                // GOW-Port: observar la textura adquirida sin otra lectura ni otra barrera.
                static const bool diagnostic = [] {
                    const char *v = std::getenv("PS2X_GS_PRESENT_DIAG");
                    return v && std::strcmp(v, "1") == 0;
                }();
                // GOW-Port: desplazar el registro sin elevar su cupo ni cambiar la presentación.
                static const uint64_t diagnosticFromTick = GSHostPresentDiag::parseFromTick(
                    std::getenv("PS2X_GS_PRESENT_DIAG_FROM_TICK"));
                if (diagnostic && GSHostPresentDiag::consume(vsyncTick, diagnosticFromTick, diagnosticSharedFrames))
                {
                    // GOW-Port: sourceFbp son páginas DISPFB o bloques TEX0 según la fuente adquirida.
                    std::fprintf(stderr, "[gs-present:shared] seq=%llu render=%llu hostTick=%llu frameTick=%llu displayFbp=%u sourceFbp=%u preferred=%u smode2=%llu width=%u height=%u hashValid=%u hash=%016llx blank=%u\n",
                        (unsigned long long)frame.sequence, (unsigned long long)frame.renderSequence,
                        (unsigned long long)vsyncTick, (unsigned long long)frame.vsyncTick,
                        frame.displayFbp, frame.sourceFbp, unsigned(frame.usedPreferred), (unsigned long long)frame.smode2,
                        frame.width, frame.height,
                        unsigned(frame.hashValid), (unsigned long long)frame.contentHash, unsigned(frame.blank));
                }
                gs.recordAcquiredSharedFrame(frame);
                lastSharedSequence = frame.sequence;
                // GOW-Port: captura separada explícita; usa esta misma reserva/textura adquirida.
                acquiredCapture.capture(frame, vsyncTick);
            }
            if (!frame.blank && frame.texture != 0u && frame.width != 0u && frame.height != 0u)
            {
                current.texture = Texture2D{frame.texture, static_cast<int>(frame.width), static_cast<int>(frame.height),
                                            1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
                current.width = frame.width;
                current.height = frame.height;
                current.borrowed = true;
                current.blank = false;
                return current;
            }
        }
        blankFallback(); // PMODE apagado conserva el placeholder magenta de RAM.
        return current;
    }
    if (!needsLatch && uploaded)
        return current;

    scratch.clear();
    uint32_t width = 0u, height = 0u, displayFbp = 0u, sourceFbp = 0u;
    bool preferred = false;
    if (!gs.copyLatchedHostPresentationFrame(scratch, width, height, &displayFbp, &sourceFbp, &preferred))
    {
        blankFallback();
        lastWidth = current.width;
        lastHeight = current.height;
        return current;
    }
    PS2_IF_AGRESSIVE_LOGS({
        if (uploadDebugCount < 128u || displayFbp != lastDisplayFbp || sourceFbp != lastSourceFbp ||
            preferred != lastPreferred || width != lastWidth || height != lastHeight)
            std::cout << "[frame:upload] idx=" << uploadDebugCount << " tick=" << vsyncTick
                      << " displayFbp=" << displayFbp << " sourceFbp=" << sourceFbp
                      << " size=" << width << "x" << height
                      << " preferred=" << static_cast<uint32_t>(preferred ? 1u : 0u) << std::endl;
        ++uploadDebugCount;
    });
    lastDisplayFbp = displayFbp;
    lastSourceFbp = sourceFbp;
    lastPreferred = preferred;
    lastWidth = width;
    lastHeight = height;
    std::fill(upload.begin(), upload.end(), 0u);
    const uint32_t copyWidth = std::min(width, kWidth);
    const uint32_t copyHeight = std::min(height, kHeight);
    const size_t sourceRow = static_cast<size_t>(width) * 4u;
    const size_t destinationRow = static_cast<size_t>(kWidth) * 4u;
    const size_t copyRow = static_cast<size_t>(copyWidth) * 4u;
    for (uint32_t y = 0u; y < copyHeight; ++y)
    {
        const size_t sourceOffset = static_cast<size_t>(y) * sourceRow;
        const size_t destinationOffset = static_cast<size_t>(y) * destinationRow;
        if (sourceOffset + copyRow > scratch.size() || destinationOffset + copyRow > upload.size())
            break;
        std::memcpy(upload.data() + destinationOffset, scratch.data() + sourceOffset, copyRow);
    }
    UpdateTexture(fallback, upload.data());
    uploaded = true;
    uploadedBlank = false;
    current.texture = fallback;
    current.width = width;
    current.height = height;
    current.borrowed = current.blank = false;
    current.frame = {};
    return current;
}

GSHostPresentation::View GSHostPresentation::renderFrame(GS &gs, uint64_t vsyncTick, const std::function<void()> &overlay)
{
    View selected;
    {
        ps2_perf::Scope perf(ps2_perf::Bucket::Upload);
        selected = prepare(gs, vsyncTick);
    }
    BeginDrawing();
    ClearBackground(BLACK);
    const float sourceWidth = static_cast<float>(std::max<uint32_t>(1u, selected.width));
    const float sourceHeight = static_cast<float>(std::max<uint32_t>(1u, selected.height));
    const float screenWidth = static_cast<float>(GetScreenWidth());
    const float screenHeight = static_cast<float>(GetScreenHeight());
    const float scale = std::min(screenWidth / sourceWidth, screenHeight / sourceHeight);
    const float destinationWidth = sourceWidth * scale;
    const float destinationHeight = sourceHeight * scale;
    const Rectangle source{0.0f, 0.0f, sourceWidth, sourceHeight};
    const Rectangle destination{(screenWidth - destinationWidth) * 0.5f,
                                (screenHeight - destinationHeight) * 0.5f,
                                destinationWidth, destinationHeight};
    // GOW-Port: textura del PBO con filas en orden; no invertir source.height.
    DrawTexturePro(selected.texture, source, destination, Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    if (overlay)
        overlay();
    {
        ps2_perf::Scope perf(ps2_perf::Bucket::HostWait);
        EndDrawing();
    }
    return selected;
}

void GSHostPresentation::shutdown(GS &gs)
{
    if (!started && fallback.id == 0u)
        return;
    // GOW-Port: nullptr crea CPU; drena y destruye el worker/contexto compartido.
    gs.setRasterBackend(nullptr);
    restoreHostContext();
    if (fallback.id != 0u)
        UnloadTexture(fallback); // Nunca descargar la Texture2D prestada del productor.
    fallback = {};
    if (captureRequested)
        GSSharedPresent::ShutdownHost();
    started = captureRequested = hasLatched = uploaded = uploadedBlank = modeKnown = sharedMode = false;
    lastTick = lastSharedSequence = 0u;
    current = {};
    scratch.clear();
    upload.clear();
}
