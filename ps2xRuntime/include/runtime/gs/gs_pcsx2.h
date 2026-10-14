// PCSX2's GS as the port's renderer (PS2X_GS_RENDERER=pcsx2).
//
// The library (third_party/pcsx2-gs: pcsx2gs.so / pcsx2gs.dll, next to the executable) is loaded at run time. A
// GSExternalObserver on the runtime's GS front end forwards every GIF packet (per PATH), direct register write, reset
// and vsync to a worker thread that owns the library and its OpenGL context. The worker reads back each presented
// field and the host window shows it in place of the built-in rasterizer's output.
#pragma once

#include <cstdint>
#include <vector>

class GS;

namespace ps2_pcsx2gs
{
    // PS2X_GS_RENDERER=pcsx2 (or pcsx2-sw for the software renderer).
    bool Requested();
    // Loads the library and attaches it to the GS front end. Returns false (and keeps the built-in renderer) when the
    // library or its renderer cannot be started.
    bool Install(GS &gs);
    bool Active();
    // Latest field presented by PCSX2, as RGBA8. Returns false when nothing new since `serial` (updated on success).
    bool CopyLatestFrame(std::vector<uint8_t> &rgba, uint32_t &width, uint32_t &height, uint64_t &serial);
    void Shutdown();
}
