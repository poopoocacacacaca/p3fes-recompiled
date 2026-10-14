// GOW-Port: captura explícita y acotada de la textura adquirida; desactivada por defecto.
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
    // GOW-Port: el llamador mantiene la reserva de Acquire y el contexto host actual.
    // No adquiere otra ranura ni consulta el GS/VRAM; los fallos no interrumpen el juego.
    bool capture(const GSSharedPresent::Frame &frame, uint64_t hostTick) noexcept;

private:
    Options options;
    uint32_t attempts = 0u;
    uint64_t lastSequence = 0u;
};
