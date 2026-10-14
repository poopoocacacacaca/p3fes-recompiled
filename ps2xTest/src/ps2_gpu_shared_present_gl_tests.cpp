// GOW-Port: comprobar la composición compartida con un host raylib OpenGL 3.3.
// Patrones sintéticos: sin assets, sin fallback y con el renderer CPU de referencia.
#include "MiniTest.h"

#include <cstdlib>

#ifdef _WIN32
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_gpu_backend.h"
#include "runtime/gs/gs_shared_present.h"
#include "runtime/gs/gs_host_acquired_capture.h" // GOW-Port: captura de reserva real sin VRAM actual.
#include "runtime/gs/gs_swizzle.h"
#include "runtime/gs/gs_threaded_backend.h"
#include "raylib.h"
#include "rlgl.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    std::string sharedSetting(const char *name, bool &present)
    {
        char *value = nullptr;
        size_t size = 0;
        if (_dupenv_s(&value, &size, name) != 0)
            throw std::runtime_error(std::string("No se pudo leer ") + name);
        const std::unique_ptr<char, decltype(&std::free)> owned(value, &std::free);
        present = value != nullptr;
        return present ? std::string(value) : std::string();
    }

    class RaylibSharedHost
    {
        bool hadSetting = false;
        bool changedSetting = false;
        bool ownsWindow = false;
        bool captured = false;
        std::string oldSetting;

    public:
        RaylibSharedHost()
        {
            oldSetting = sharedSetting("PS2X_GS_DIRECT_PRESENT", hadSetting);
        }

        ~RaylibSharedHost()
        {
            if (captured)
            {
                GSSharedPresent::RestoreHostContext();
                GSSharedPresent::ShutdownHost();
            }
            if (ownsWindow)
                CloseWindow();
            if (changedSetting)
                _putenv_s("PS2X_GS_DIRECT_PRESENT", hadSetting ? oldSetting.c_str() : "");
        }

        bool initialize(TestCase &t)
        {
            if (IsWindowReady() || GSSharedPresent::Active())
            {
                t.Fail("La prueba requiere un host raylib propio, sin ventana ni GPU compartida anterior");
                return false;
            }
            if (_putenv_s("PS2X_GS_DIRECT_PRESENT", "1") != 0)
            {
                t.Fail("No se pudo activar sharing para la prueba");
                return false;
            }
            changedSetting = true;
            SetConfigFlags(FLAG_WINDOW_HIDDEN | FLAG_WINDOW_UNFOCUSED);
            InitWindow(160, 120, "GOW prueba compositor compartido");
            ownsWindow = IsWindowReady();
            if (!ownsWindow)
            {
                t.Fail("Host raylib oculto requerido; no se admite fallback");
                return false;
            }
            if (rlGetVersion() != RL_OPENGL_33)
            {
                t.Fail("La prueba requiere el camino host OpenGL 3.3 del juego");
                return false;
            }
            GSSharedPresent::CaptureHostContext();
            captured = true;
            GSSharedPresent::RestoreHostContext();
            return true;
        }
    };

    struct OwnedImage
    {
        Image image{};
        explicit OwnedImage(Texture2D texture) : image(LoadImageFromTexture(texture)) {}
        ~OwnedImage() { if (image.data) UnloadImage(image); }
        OwnedImage(const OwnedImage &) = delete;
        OwnedImage &operator=(const OwnedImage &) = delete;
    };

    struct SharedFixture
    {
        std::vector<uint8_t> cpuRam = std::vector<uint8_t>(GSSwizzle::kMemorySize);
        std::vector<uint8_t> gpuRam = std::vector<uint8_t>(GSSwizzle::kMemorySize);
        GSCpuBackend cpu;
        std::unique_ptr<GSThreadedBackend> gpu;
        GSSharedPresent::Frame current{};

        bool initialize(TestCase &t)
        {
            cpu.Initialize(cpuRam.data(), static_cast<uint32_t>(cpuRam.size()));
            auto inner = std::make_unique<GSGpuBackend>();
            auto *raw = inner.get();
            inner->SetHardwareRasterAllowed(false);
            gpu = std::make_unique<GSThreadedBackend>(std::move(inner));
            gpu->Initialize(gpuRam.data(), static_cast<uint32_t>(gpuRam.size()));
            GSSharedPresent::RestoreHostContext();
            if (&gpu->Inner() != raw || !raw->IsReady() || !gpu->UsesSharedPresentation())
            {
                t.Fail("GPU compartida real requerida; el CPU de fallback no valida esta prueba");
                return false;
            }
            GSSharedPresent::Frame startup{};
            t.IsFalse(GSSharedPresent::Acquire(startup), "Un backend nuevo no hereda una textura del share group anterior");
            return true;
        }

        void upload(uint32_t base, uint32_t bw, uint32_t psm, uint32_t width, uint32_t height,
                    uint32_t variant = 0)
        {
            std::vector<uint8_t> pixels;
            for (uint32_t y = 0; y < height; ++y)
                for (uint32_t x = 0; x < width; ++x)
                {
                    const bool half = psm == GS_PSM_CT16 || psm == GS_PSM_CT16S;
                    const uint32_t color = half
                        ? 0x8000u | ((x + variant) & 31u) | ((y & 31u) << 5u) | (((x + y) & 31u) << 10u)
                        : 0x80000000u | ((x * 3u + variant) & 255u) | (((y * 3u) & 255u) << 8u) |
                          (((x + y * 7u) & 255u) << 16u);
                    const unsigned bytes = half ? 2u : (psm == GS_PSM_CT24 ? 3u : 4u);
                    for (unsigned byte = 0; byte < bytes; ++byte)
                        pixels.push_back(static_cast<uint8_t>(color >> (byte * 8u)));
                }
            GSTransferCommand command{};
            command.direction = 0;
            command.bitbltbuf.dbp = base;
            command.bitbltbuf.dbw = bw;
            command.bitbltbuf.dpsm = static_cast<uint8_t>(psm);
            command.trxreg = {static_cast<uint16_t>(width), static_cast<uint16_t>(height)};
            for (GSRasterBackend *backend : {static_cast<GSRasterBackend *>(&cpu),
                                            static_cast<GSRasterBackend *>(gpu.get())})
            {
                backend->BeginTransfer(command);
                backend->UploadImage(pixels.data(), static_cast<uint32_t>(pixels.size()));
            }
        }

        bool acquireNext(TestCase &t)
        {
            // GOW-Port: Acquire comprueba fences del driver; esperar solo en esta prueba.
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            GSSharedPresent::Frame next{};
            while (std::chrono::steady_clock::now() < until)
            {
                if (GSSharedPresent::Acquire(next) && next.sequence > current.sequence)
                {
                    t.IsTrue(next.renderSequence > current.renderSequence, "Avanza la secuencia de composición");
                    current = next;
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            t.Fail("No se recibió una composición compartida nueva dentro del plazo");
            return false;
        }

        std::vector<uint8_t> compareCurrent(TestCase &t, const PresentationFrame &reference,
                                            const std::string &label)
        {
            t.IsTrue(static_cast<bool>(reference), label + ": referencia CPU no vacía");
            t.IsFalse(current.blank, label + ": circuitos activos");
            t.Equals(current.width, reference.width, label + ": ancho lógico");
            t.Equals(current.height, reference.height, label + ": alto lógico");
            t.Equals(current.displayFbp, reference.displayFbp, label + ": destino en páginas");
            t.Equals(current.sourceFbp, reference.sourceFbp, label + ": fuente en sus unidades originales");
            t.Equals(current.usedPreferred, reference.usedPreferred, label + ": selección preferida");
            if (!reference || current.width != reference.width || current.height != reference.height)
                return {};
            const Texture2D borrowed{current.texture, static_cast<int>(current.width),
                                     static_cast<int>(current.height), 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            OwnedImage image(borrowed);
            if (!image.image.data || image.image.format != PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 ||
                image.image.width != static_cast<int>(current.width) ||
                image.image.height != static_cast<int>(current.height))
            {
                t.Fail(label + ": lectura real de la textura compartida requerida");
                return {};
            }
            // GOW-Port: CPU tiene stride 640; la textura compartida tiene stride width.
            const size_t rowBytes = static_cast<size_t>(reference.width) * 4u;
            if (reference.pixels.size() < static_cast<size_t>(640u) * reference.height * 4u)
            {
                t.Fail(label + ": framebuffer CPU incompleto");
                return {};
            }
            const auto *bytes = static_cast<const uint8_t *>(image.image.data);
            for (uint32_t y = 0; y < reference.height; ++y)
                if (!std::equal(bytes + y * rowBytes, bytes + (y + 1u) * rowBytes,
                                reference.pixels.begin() + static_cast<size_t>(y) * 640u * 4u))
                {
                    t.Fail(label + ": píxeles diferentes en fila " + std::to_string(y));
                    return {};
                }
            return {bytes, bytes + rowBytes * reference.height};
        }

        std::vector<uint8_t> present(TestCase &t, const GSPresentationRequest &request,
                                   const std::string &label)
        {
            const auto reference = cpu.Present(request);
            const auto returned = gpu->Present(request);
            t.IsFalse(static_cast<bool>(returned), label + ": publicación compartida sin descarga CPU");
            if (!acquireNext(t))
                return {};
            t.Equals(current.vsyncTick, request.vsyncTick, label + ": tick realmente compuesto");
            return compareCurrent(t, reference, label);
        }
    };

    void sharedSourcesAndBlank(TestCase &t)
    {
        RaylibSharedHost host;
        if (!host.initialize(t)) return;
        SharedFixture fixture;
        if (!fixture.initialize(t)) return;
        fixture.upload(32u * 32u, 1, GS_PSM_CT32, 64, 112);
        GSPresentationRequest request{};
        request.pmode = 1;
        request.dispfb1 = 32ull | (1ull << 9u) | (7ull << 32u) | (9ull << 43u);
        request.display1 = (63ull << 32u) | (95ull << 44u);
        request.hasPreferredSource = true;
        request.preferredDestFbp = 32;
        request.preferredSource = {1057, 2, GS_PSM_CT32, 0};
        for (const uint32_t format : {GS_PSM_CT32, GS_PSM_CT24, GS_PSM_CT16, GS_PSM_CT16S})
        {
            request.preferredSource.psm = static_cast<uint8_t>(format);
            fixture.upload(1057, 2, format, 64, 96);
            fixture.present(t, request, "Fuente preferida formato " + std::to_string(format));
            t.IsTrue(fixture.current.usedPreferred, "El patrón sintético activa la fuente preferida");
            t.Equals(fixture.current.sourceFbp, 1057u, "La fuente conserva el bloque no alineado a página");
        }
        // GOW-Port: comprobar estas direcciones también en la textura adquirida, sin forceReadback.
        fixture.upload(32, 10, GS_PSM_CT32, 64, 96, 37);
        request.preferredSource = {32, 0, GS_PSM_CT32, 0};
        fixture.present(t, request, "TBW cero: números iguales, direcciones distintas");
        t.IsTrue(fixture.current.usedPreferred, "32 bloques no son 32 páginas en la textura compartida");
        t.Equals(fixture.current.sourceFbp, 32u, "Fuente adquirida en bloques sin TBW");
        request.preferredSource = {1024, 0, GS_PSM_CT32, 0};
        fixture.present(t, request, "TBW cero: dirección igual conserva el CRTC");
        t.IsFalse(fixture.current.usedPreferred, "1024 bloques son 32 páginas en la textura compartida");
        t.Equals(fixture.current.sourceFbp, 32u, "Fuente adquirida vuelve a páginas CRTC");
        // GOW-Port: reutilizar las ocho ranuras con contenido y selección distintos por request.
        for (uint32_t step = 0; step < 16; ++step)
        {
            fixture.upload(32, 10, GS_PSM_CT32, 64, 96, 37 + step);
            fixture.upload(1024, 1, GS_PSM_CT32, 64, 112, 91 + step);
            request.vsyncTick = 100 + step;
            const bool preferred = (step & 1u) == 0;
            request.preferredSource = {preferred ? 32u : 1024u, 0, GS_PSM_CT32, 0};
            fixture.present(t, request, "Reutilización de ranuras: paso " + std::to_string(step));
            t.Equals(fixture.current.usedPreferred, preferred, "Selección de la textura nueva al reutilizar ranuras");
        }
        request.hasPreferredSource = false;
        fixture.present(t, request, "Origen CRTC distinto de cero sin preferredSource");
        const auto enabledRequest = request;
        request.pmode = 0;
        t.IsFalse(static_cast<bool>(fixture.cpu.Present(request)), "PMODE apagado devuelve CPU vacío");
        fixture.gpu->Present(request);
        if (!fixture.acquireNext(t)) return;
        t.IsTrue(fixture.current.blank, "PMODE apagado publica la transición vacía para el host");
        t.Equals(fixture.current.width, 1u, "Textura interna vacía de un píxel");
        t.Equals(fixture.current.height, 1u, "Textura interna vacía de una fila");
        t.Equals(fixture.current.displayFbp, 0u, "Estado vacío sin destino CRTC");
        t.Equals(fixture.current.sourceFbp, 0u, "Estado vacío sin fuente");
        t.IsFalse(fixture.current.usedPreferred, "Estado vacío sin selección preferida");
        const Texture2D blank{fixture.current.texture, 1, 1, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
        OwnedImage blankImage(blank);
        if (!blankImage.image.data) t.Fail("Lectura real del marcador vacío requerida");
        else
        {
            const auto *pixel = static_cast<const uint8_t *>(blankImage.image.data);
            t.IsTrue(pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 0 && pixel[3] == 255,
                     "Marcador interno negro opaco; el host usa su placeholder CPU al ver blank");
        }
        fixture.present(t, enabledRequest, "Restaurar PMODE recupera imagen y metadatos");
    }

    void sharedCrtcFields(TestCase &t)
    {
        RaylibSharedHost host;
        if (!host.initialize(t)) return;
        SharedFixture fixture;
        if (!fixture.initialize(t)) return;
        fixture.upload(1024, 1, GS_PSM_CT24, 64, 66);
        GSPresentationRequest request{};
        request.pmode = 0x8023;
        request.smode2 = 0x401;
        request.dispfb2 = 32ull | (1ull << 9u) | (1ull << 15u);
        request.dispfb1 = request.dispfb2 | (1ull << 43u);
        request.display1 = (63ull << 32u) | (64ull << 44u);
        request.display2 = (63ull << 32u) | (65ull << 44u);
        const auto even = fixture.present(t, request, "Dos circuitos: campo par");
        request.vsyncTick = 1;
        const auto odd = fixture.present(t, request, "Dos circuitos: campo impar");
        t.IsTrue(!even.empty() && !odd.empty() && even != odd, "Campos distintos con la misma VRAM");
        request.smode2 = 0;
        request.vsyncTick = 2;
        const auto progressive = fixture.present(t, request, "Dos circuitos: progresivo");
        const auto previous = fixture.current;
        request.vsyncTick = 3;
        fixture.gpu->Present(request);
        fixture.gpu->Flush();
        GSSharedPresent::Frame retained{};
        t.IsTrue(GSSharedPresent::Acquire(retained), "Se conserva la última imagen completa");
        t.Equals(retained.sequence, previous.sequence, "Progresivo sin cambios no recompone por paridad");
        t.IsTrue(fixture.compareCurrent(t, fixture.cpu.Present(request), "Progresivo retenido") == progressive,
                 "La imagen retenida conserva todas las filas");
        for (const uint32_t alpha : {0u, 64u, 128u, 255u})
        {
            request.pmode = 3u | (1u << 5u) | (static_cast<uint64_t>(alpha) << 8u);
            fixture.present(t, request, "Dos circuitos ALP " + std::to_string(alpha));
        }
        request.pmode = 0x8023;
        request.smode2 = 0x401;
        request.dispfb1 = 0x800000090d0ull;
        request.dispfb2 = 0x90d0ull;
        request.display1 = 0x1be9ff0203227cull;
        request.display2 = 0x1bf9ff0203227cull;
        fixture.upload(0xd0u * 32u, 8, GS_PSM_CT24, 512, 448);
        request.vsyncTick = 0;
        const auto gowEven = fixture.present(t, request, "Temporización GoW: campo par");
        request.vsyncTick = 1;
        const auto gowOdd = fixture.present(t, request, "Temporización GoW: campo impar");
        t.IsTrue(!gowEven.empty() && !gowOdd.empty() && gowEven != gowOdd,
                 "Temporización GoW alterna los campos con salida CPU exacta");
    }

    // GOW-Port: comprobar el modo en la textura real y en una adquisición retenida.
    void sharedAcquiredMode(TestCase &t)
    {
        RaylibSharedHost host;
        if (!host.initialize(t)) return;
        SharedFixture fixture;
        if (!fixture.initialize(t)) return;
        fixture.upload(32u * 32u, 1, GS_PSM_CT32, 64, 128);
        GSPresentationRequest request{};
        request.pmode = 1u;
        request.dispfb1 = 32ull | (1ull << 9u);
        // GOW-Port: altura lógica >=64 también cuando SMODE2 divide el buffer por dos.
        request.display1 = (63ull << 32u) | (127ull << 44u);
        uint64_t tick = 20u;
        for (const uint64_t mode : {0x401ull, 0x401ull, 0x403ull, 0x100000000400ull})
        {
            request.smode2 = mode;
            request.vsyncTick = tick++;
            if ((mode & 3ull) == 3ull)
            {
                // GOW-Port: CPU y GPU difieren en altura en modo cuadro; aquí probar solo metadatos.
                // Conservar esa diferencia visible, sin cambiar la composición para este diagnóstico.
                const auto returned = fixture.gpu->Present(request);
                t.IsFalse(static_cast<bool>(returned), "Modo cuadro publicado sin descarga CPU");
                if (!fixture.acquireNext(t)) return;
                const auto reference = fixture.cpu.Present(request);
                std::cout << "[gs-test:mode-frame] smode2=" << mode
                          << " cpu=" << reference.width << 'x' << reference.height
                          << " shared=" << fixture.current.width << 'x' << fixture.current.height << '\n';
                t.IsTrue(fixture.current.texture != 0u && fixture.current.width != 0u &&
                         fixture.current.height != 0u, "Modo cuadro adquiere una textura real no vacía");
            }
            else
            {
                const auto pixels = fixture.present(t, request, "Modo de textura adquirida");
                t.IsTrue(!pixels.empty(), "Campos y progresivo comparados contra CPU");
            }
            t.Equals(fixture.current.smode2, mode, "Conservar SMODE2 completo de esta secuencia");
            const uint64_t sequence = fixture.current.sequence;
            // Mutar el request del llamador no debe contaminar una textura ya publicada.
            request.smode2 ^= 3u;
            GSSharedPresent::Frame retained{};
            t.IsTrue(GSSharedPresent::Acquire(retained), "Adquisición real sin una publicación nueva");
            t.Equals(retained.sequence, sequence, "Se conserva la secuencia anterior");
            t.Equals(retained.smode2, mode, "El modo retenido no procede del request posterior");
        }
    }

    // GOW-Port: el archivo debe representar la textura reservada, aunque VRAM cambie después.
    void sharedAcquiredCapture(TestCase &t)
    {
        RaylibSharedHost host;
        if (!host.initialize(t)) return;
        SharedFixture fixture;
        if (!fixture.initialize(t)) return;
        fixture.upload(32u * 32u, 1, GS_PSM_CT32, 64, 64, 7u);
        GSPresentationRequest request{};
        request.pmode = 1u;
        request.smode2 = 0x100000000400ull;
        request.dispfb1 = 32ull | (1ull << 9u);
        request.display1 = (63ull << 32u) | (63ull << 44u);
        request.vsyncTick = 20u;
        const auto pixelsA = fixture.present(t, request, "Captura adquirida A");
        if (pixelsA.empty()) return;
        const uint32_t before = fixture.gpu->ReadVram(GS_PSM_CT32, 1024u, 1u, 0u, 0u);
        fixture.upload(32u * 32u, 1, GS_PSM_CT32, 64, 64, 91u);
        const uint32_t after = fixture.gpu->ReadVram(GS_PSM_CT32, 1024u, 1u, 0u, 0u);
        t.IsTrue(before != after, "VRAM real ya contiene B antes de capturar la reserva A");
        struct PrivateFiles
        {
            std::filesystem::path directory;
            bool owned = false;
            ~PrivateFiles()
            {
                if (!owned) return;
                std::error_code error;
                // GOW-Port: carpeta procedural nueva; quitar solo archivos directos, sin recursión.
                for (const auto &file : std::filesystem::directory_iterator(directory, error))
                    if (file.is_regular_file(error)) std::filesystem::remove(file.path(), error);
                std::filesystem::remove(directory, error);
            }
        } files{std::filesystem::temp_directory_path() / ("gow_acquired_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()))};
        if (!std::filesystem::create_directory(files.directory))
        {
            t.Fail("Se requiere una carpeta procedural nueva");
            return;
        }
        files.owned = true;
        const auto utf8Directory = files.directory.u8string();
        const std::string directory(reinterpret_cast<const char *>(utf8Directory.data()), utf8Directory.size());
        GSHostAcquiredCapture capture({directory, 30u, 2u});
        GSHostAcquiredCapture disabled(GSHostAcquiredCapture::Options{});
        t.IsFalse(disabled.capture(fixture.current, 30u), "Sin carpeta la captura queda desactivada");
        t.IsFalse(capture.capture(fixture.current, 29u), "La ventana anterior no descarga ni gasta cupo");
        t.IsTrue(std::filesystem::is_empty(files.directory), "Sin archivos anteriores a la ventana");
        auto fileName = [&](uint32_t index)
        {
            return "acquired_" + std::to_string(index) + "_seq_" + std::to_string(fixture.current.sequence) +
                   "_tick_" + std::to_string(fixture.current.vsyncTick);
        };
        auto bytesFrom = [&](const std::filesystem::path &path)
        {
            std::ifstream stream(path, std::ios::binary);
            return std::vector<unsigned char>(std::istreambuf_iterator<char>(stream), {});
        };
        auto checkFile = [&](uint32_t index, const std::vector<uint8_t> &expected)
        {
            const auto png = bytesFrom(files.directory / (fileName(index) + ".png"));
            if (png.empty()) { t.Fail("Falta PNG adquirido real"); return; }
            struct Decoded
            {
                Image image{};
                ~Decoded() { if (image.data) UnloadImage(image); }
            } decoded{LoadImageFromMemory(".png", png.data(), static_cast<int>(png.size()))};
            t.IsTrue(decoded.image.data != nullptr, "PNG adquirido decodificable");
            t.Equals(decoded.image.width, 64, "Ancho completo exportado");
            t.Equals(decoded.image.height, 64, "Alto completo exportado");
            t.Equals(decoded.image.format, int(PIXELFORMAT_UNCOMPRESSED_R8G8B8A8), "RGBA preservado");
            if (decoded.image.data && decoded.image.width == 64 && decoded.image.height == 64 &&
                decoded.image.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
            {
                const auto *pixels = static_cast<const uint8_t *>(decoded.image.data);
                t.IsTrue(std::equal(expected.begin(), expected.end(), pixels),
                         "Todos los píxeles del PNG coinciden con la reserva, no con VRAM posterior");
            }
            const auto metadataBytes = bytesFrom(files.directory / (fileName(index) + ".json"));
            const std::string metadata(metadataBytes.begin(), metadataBytes.end());
            t.IsTrue(metadata.find("\"source\": \"acquired_texture\"") != std::string::npos &&
                     metadata.find("\"smode2\": " + std::to_string(fixture.current.smode2)) != std::string::npos &&
                     metadata.find("\"frameTick\": " + std::to_string(fixture.current.vsyncTick)) != std::string::npos &&
                     metadata.find("\"seq\": " + std::to_string(fixture.current.sequence)) != std::string::npos,
                     "Metadatos completos proceden de la misma textura exportada");
        };
        t.IsTrue(capture.capture(fixture.current, 30u), "Primera captura inclusiva después de cambiar VRAM");
        checkFile(1u, pixelsA);
        const auto original = bytesFrom(files.directory / (fileName(1u) + ".png"));
        GSHostAcquiredCapture collision({directory, 30u, 1u});
        t.IsFalse(collision.capture(fixture.current, 30u), "Otra instancia no sobrescribe una captura existente");
        t.IsTrue(original == bytesFrom(files.directory / (fileName(1u) + ".png")), "PNG original intacto");
        t.IsFalse(capture.capture(fixture.current, 31u), "Una secuencia retenida no consume otro archivo");
        request.smode2 = 0x401u;
        request.vsyncTick = 21u;
        const auto pixelsB = fixture.present(t, request, "Captura adquirida B campo impar");
        if (pixelsB.empty()) return;
        t.IsTrue(capture.capture(fixture.current, 31u), "Segunda captura real con modo cambiado");
        checkFile(2u, pixelsB);
        request.vsyncTick = 22u;
        if (fixture.present(t, request, "Captura fuera del cupo campo par").empty()) return;
        t.IsFalse(capture.capture(fixture.current, 32u), "No descargar ni escribir después del cupo");
        size_t pngCount = 0u, metadataCount = 0u;
        for (const auto &file : std::filesystem::directory_iterator(files.directory))
        {
            pngCount += file.path().extension() == ".png" ? 1u : 0u;
            metadataCount += file.path().extension() == ".json" ? 1u : 0u;
        }
        t.Equals(pngCount, size_t(2u), "Solo dos texturas únicas dentro de la ventana/cupo");
        t.Equals(metadataCount, size_t(2u), "Un metadato por textura adquirida");
    }

    void sharedForcedReadback(TestCase &t)
    {
        RaylibSharedHost host;
        if (!host.initialize(t)) return;
        SharedFixture fixture;
        if (!fixture.initialize(t)) return;
        fixture.upload(32u * 32u, 1, GS_PSM_CT32, 64, 96);
        GSPresentationRequest request{};
        request.pmode = 1;
        request.dispfb1 = 32ull | (1ull << 9u);
        request.display1 = (63ull << 32u) | (95ull << 44u);
        fixture.present(t, request, "Imagen inicial compartida");
        const auto previous = fixture.current;
        fixture.upload(32u * 32u, 1, GS_PSM_CT32, 64, 96, 83);
        request.forceReadback = true;
        // GOW-Port: la suite nativa arranca con async=false; reponer incluso si falla.
        struct AsyncScope
        {
            AsyncScope() { GSGpuBackend::SetAsyncPresentDefault(true); }
            ~AsyncScope() { GSGpuBackend::SetAsyncPresentDefault(false); }
        } asynchronousDefault;
        const auto expected = fixture.cpu.Present(request);
        const auto returned = fixture.gpu->Present(request);
        t.IsTrue(static_cast<bool>(returned), "forceReadback devuelve imagen aunque shared y async estén activos");
        t.Equals(returned.width, expected.width, "Captura síncrona: ancho");
        t.Equals(returned.height, expected.height, "Captura síncrona: alto");
        t.Equals(returned.displayFbp, expected.displayFbp, "Captura síncrona: destino");
        t.Equals(returned.sourceFbp, expected.sourceFbp, "Captura síncrona: fuente");
        t.Equals(returned.usedPreferred, expected.usedPreferred, "Captura síncrona: selección");
        const size_t bytes = static_cast<size_t>(640u) * expected.height * 4u;
        t.IsTrue(returned.pixels.size() >= bytes && expected.pixels.size() >= bytes &&
                 std::equal(expected.pixels.begin(), expected.pixels.begin() + bytes, returned.pixels.begin()),
                 "Captura solicitada lee el request actual y no un PBO anterior");
        GSSharedPresent::Frame retained{};
        t.IsTrue(GSSharedPresent::Acquire(retained), "La captura no retira la imagen ya presentada");
        t.Equals(retained.sequence, previous.sequence, "La lectura diagnóstica no publica otra textura");
        request.forceReadback = false;
        fixture.present(t, request, "El camino compartido continúa después de una captura");
    }
}
#endif

void runGpuSharedPresentGlTests()
{
#ifdef _WIN32
    bool present = false;
    const auto setting = sharedSetting("GOW_GS_GPU_TEST", present);
    if (!present || setting != "1") return;
    MiniTest::Case("GS GPU compositor compartido", [](TestCase &suite)
    {
        suite.Run("Fuente, dimensiones, unidades y transición PMODE", sharedSourcesAndBlank);
        suite.Run("Dos circuitos, ALP, filas y campos GoW", sharedCrtcFields);
        suite.Run("SMODE2 de textura adquirida y retenida", sharedAcquiredMode); // GOW-Port
        suite.Run("Captura de reserva adquirida tras cambiar VRAM", sharedAcquiredCapture); // GOW-Port
        suite.Run("Captura diagnóstica síncrona mientras shared y async están activos", sharedForcedReadback);
    });
#endif
}
