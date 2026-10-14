// GOW-Port: el consumidor raylib de producción, con GS procedural y ventana oculta.
#include "MiniTest.h"
#include <cstdlib>

#ifdef _WIN32
#include "runtime/gs/gs_host_present.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_gpu_backend.h"
#include "runtime/gs/gs_threaded_backend.h"
#include "runtime/gs/gs_swizzle.h"
#include "runtime/gs/ps2_gs_memory.h"
#include "runtime/ps2_memory.h"
#include "rlgl.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr uint32_t kSourceWidth = 64u;
    constexpr uint32_t kSourceHeight = 96u;

    class Setting
    {
        bool present = false;
        std::string previous;
    public:
        explicit Setting(bool shared)
        {
            char *value = nullptr;
            size_t size = 0u;
            if (_dupenv_s(&value, &size, "PS2X_GS_DIRECT_PRESENT") != 0)
                throw std::runtime_error("No se pudo leer el modo de presentación");
            const std::unique_ptr<char, decltype(&std::free)> owned(value, &std::free);
            present = value != nullptr;
            if (present)
                previous = value;
            if (_putenv_s("PS2X_GS_DIRECT_PRESENT", shared ? "1" : "0") != 0)
                throw std::runtime_error("No se pudo seleccionar el modo de presentación");
        }
        ~Setting() { _putenv_s("PS2X_GS_DIRECT_PRESENT", present ? previous.c_str() : ""); }
    };

    class HiddenWindow
    {
        bool owned = false;
    public:
        bool open(TestCase &t, int width, int height)
        {
            if (IsWindowReady())
            {
                t.Fail("El control necesita su propia ventana raylib, no una ventana ajena");
                return false;
            }
            SetConfigFlags(FLAG_WINDOW_HIDDEN);
            InitWindow(width, height, "GS host procedural");
            owned = IsWindowReady();
            if (!owned)
            {
                t.Fail("Ventana raylib oculta requerida");
                return false;
            }
            SetTargetFPS(60);
            t.Equals(rlGetVersion(), static_cast<int>(RL_OPENGL_33), "Host raylib con la API OpenGL 3.3 del juego");
            return true;
        }
        ~HiddenWindow()
        {
            if (owned)
                CloseWindow();
        }
    };

    struct Capture
    {
        std::vector<uint8_t> pixels;
        uint32_t width = 0u, height = 0u;
        bool operator==(const Capture &other) const
        {
            return width == other.width && height == other.height && pixels == other.pixels;
        }
    };

    class Source
    {
        bool started = false;
    public:
        GS gs;
        GSHostPresentation host;
        GSRegisters registers{};
        std::vector<uint8_t> memory = std::vector<uint8_t>(GSSwizzle::kMemorySize);
        uint64_t tick = 0u;

        void initialize(bool shared)
        {
            GSMem::InitLookupTables();
            for (uint32_t y = 0u; y < kSourceHeight; ++y)
                for (uint32_t x = 0u; x < kSourceWidth; ++x)
                {
                    uint32_t color = 0xff000000u | ((x * 13u + 17u) & 255u) |
                                     (((y * 7u + 29u) & 255u) << 8u) |
                                     ((((x + y) * 11u + 41u) & 255u) << 16u);
                    if ((y & 1u) == 0u)
                        color ^= 0x00804020u; // Líneas alternas que hacen visible LINEAR.
                    if (x < 4u && y < 4u) color = 0xff0037d0u;
                    if (x >= kSourceWidth - 4u && y < 4u) color = 0xffe03020u;
                    if (x < 4u && y >= kSourceHeight - 4u) color = 0xff60f030u;
                    if (x >= kSourceWidth - 4u && y >= kSourceHeight - 4u) color = 0xff3020e0u;
                    GSMem::WriteCT32(memory.data(), 0u, 1u, x, y, color);
                }
            host.captureHostContext();
            started = true;
            if (shared)
            {
                auto gpu = std::make_unique<GSGpuBackend>();
                gpu->SetHardwareRasterAllowed(false);
                gs.setRasterBackend(std::make_unique<GSThreadedBackend>(std::move(gpu)));
            }
            else
                gs.setRasterBackend(std::make_unique<GSCpuBackend>());
            gs.init(memory.data(), static_cast<uint32_t>(memory.size()), &registers);
            registers.pmode = 1u;
            registers.dispfb1 = 1ull << 9u;
            registers.display1 = (uint64_t{kSourceWidth - 1u} << 32u) | (uint64_t{kSourceHeight - 1u} << 44u);
            host.restoreHostContext();
        }

        GSHostPresentation::View render(TestCase &t, Capture &capture)
        {
            registers.vsyncTick.store(++tick, std::memory_order_release);
            return host.renderFrame(gs, tick, [&]
            {
                // GOW-Port: observar el backbuffer después del mismo DrawTexturePro.
                // EndDrawing de producción sigue cerrando el batch y el cuadro.
                rlDrawRenderBatchActive();
                Image image = LoadImageFromScreen();
                if (!image.data || image.width <= 0 || image.height <= 0)
                {
                    t.Fail("No se pudo leer el framebuffer real del host");
                    return;
                }
                ImageFormat(&image, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
                capture.width = static_cast<uint32_t>(image.width);
                capture.height = static_cast<uint32_t>(image.height);
                const auto *bytes = static_cast<const uint8_t *>(image.data);
                capture.pixels.assign(bytes, bytes + static_cast<size_t>(capture.width) * capture.height * 4u);
                UnloadImage(image);
            });
        }

        GSHostPresentation::View await(TestCase &t, Capture &capture, bool blank, uint64_t after = 0u)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
            GSHostPresentation::View view;
            do
            {
                view = render(t, capture);
                if (view.shared && view.frame.sequence > after && view.blank == blank)
                    return view;
            } while (std::chrono::steady_clock::now() < deadline);
            t.Fail("No se adquirió un frame compartido efectivo con el estado solicitado");
            return view;
        }

        void close()
        {
            if (!started)
                return;
            host.shutdown(gs); // Destruye y une el worker antes de cerrar raylib.
            started = false;
        }
        ~Source() { close(); }
    };

    Capture cpuReference(TestCase &t, int width, int height, bool blank)
    {
        Setting setting(false);
        HiddenWindow window;
        Capture capture;
        if (!window.open(t, width, height))
            return capture;
        Source source;
        source.initialize(false);
        t.IsFalse(source.gs.usesSharedPresentation(), "El control usa el compositor CPU/RAM");
        if (blank)
            source.registers.pmode = 0u;
        const auto view = source.render(t, capture);
        t.IsFalse(view.shared, "La UI de referencia no consume una textura compartida");
        t.Equals(view.blank, blank, "El placeholder CPU corresponde al display apagado");
        return capture;
    }

    void scaledHost(TestCase &t, int width, int height)
    {
        const Capture reference = cpuReference(t, width, height, false);
        if (reference.pixels.empty())
            return;
        Setting setting(true);
        HiddenWindow window;
        if (!window.open(t, width, height))
            return;
        Source source;
        source.initialize(true);
        t.IsTrue(source.gs.usesSharedPresentation(), "Backend GPU/worker efectivamente compartido");
        Capture actual;
        const auto view = source.await(t, actual, false);
        t.IsTrue(view.borrowed && view.texture.id == view.frame.texture && view.frame.texture != 0u,
                 "El helper dibuja la textura prestada del productor");
        t.Equals(view.width, kSourceWidth, "Usa el ancho efectivo publicado");
        t.Equals(view.height, kSourceHeight, "Usa el alto efectivo publicado");
        t.IsTrue(actual == reference, "Framebuffer escalado, filas, filtro y franjas coinciden exactamente con CPU");
        Capture retained;
        const auto again = source.render(t, retained);
        t.Equals(again.frame.sequence, view.frame.sequence, "Sin cambios conserva el mismo frame");
        t.IsTrue(again.borrowed && retained == reference, "Repetir el cuadro no hace readback ni pierde el slot");
        const uint32_t borrowedId = view.texture.id;
        source.close(); source.close(); // El segundo cierre no vuelve a descargar la textura.
        t.IsFalse(GSSharedPresent::Active(), "El cierre limpia el share group con el host vivo");
        t.IsFalse(source.gs.usesSharedPresentation(), "El backend de cierre es CPU y no conserva un contexto muerto");
        Image probe = GenImageColor(2, 2, RED);
        Texture2D owned = LoadTextureFromImage(probe);
        UnloadImage(probe);
        t.IsTrue(owned.id != 0u, "El contexto host sigue operativo después de destruir el worker");
        UnloadTexture(owned);
        t.IsTrue(borrowedId != 0u, "El control había utilizado una textura compartida real");
    }

    void blankTransition(TestCase &t)
    {
        constexpr int width = 160, height = 192;
        const Capture displayed = cpuReference(t, width, height, false);
        const Capture placeholder = cpuReference(t, width, height, true);
        t.IsFalse(displayed == placeholder, "El control distingue escena y placeholder magenta");
        Setting setting(true);
        HiddenWindow window;
        if (!window.open(t, width, height))
            return;
        Source source;
        source.initialize(true);
        Capture capture;
        const auto first = source.await(t, capture, false);
        t.IsTrue(capture == displayed, "Comienza con la imagen procedural");
        source.registers.pmode = 0u;
        const auto blank = source.await(t, capture, true, first.frame.sequence);
        t.IsTrue(blank.frame.blank && !blank.borrowed && blank.texture.id != blank.frame.texture,
                 "Display apagado usa placeholder propio, no la textura negra ni la escena anterior");
        t.Equals(blank.width, 640u, "Mantiene el ancho del placeholder RAM");
        t.Equals(blank.height, 448u, "Mantiene el alto del placeholder RAM");
        t.IsTrue(capture == placeholder, "La transición a magenta y las franjas coinciden con CPU");
        source.registers.pmode = 1u;
        const auto recovered = source.await(t, capture, false, blank.frame.sequence);
        t.IsTrue(recovered.borrowed && capture == displayed, "Reactivar PMODE recupera el frame correcto");
    }
}
#endif

void runGsHostPresentTests()
{
#ifdef _WIN32
    const char *setting = std::getenv("GOW_GS_GPU_TEST");
    if (!setting || *setting != '1')
        return;
    MiniTest::Case("GS consumidor host raylib real", [](TestCase &suite)
    {
        suite.Run("Escala 1 conserva filas y franjas", [](TestCase &t) { scaledHost(t, 96, 96); });
        suite.Run("Escala 2 conserva NEAREST y franjas", [](TestCase &t) { scaledHost(t, 160, 192); });
        suite.Run("Escala no entera conserva NEAREST y filas", [](TestCase &t) { scaledHost(t, 123, 157); });
        suite.Run("PMODE apagado muestra placeholder magenta y vuelve a la escena", blankTransition);
    });
#endif
}
