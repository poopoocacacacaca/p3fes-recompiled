// GOW-Port: fallback real por tamaño y capacidad del buffer de entrada/salida.
// API pública y píxeles procedurales; sin hooks del driver ni getters del backend.
#include "MiniTest.h"

#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_gpu_backend.h"
#include "runtime/gs/gs_shared_present.h"
#include "runtime/gs/gs_swizzle.h"
#include "gs/gpu/gs_gl.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr GSGL::GLenum kStorageBinding = 0x90D3;
    constexpr GSGL::GLenum kBufferSize = 0x8764;
    constexpr uint32_t kWidth = 2048u;
    constexpr uint32_t kReadHeight = 4095u;
    constexpr uint32_t kUploadHeight = 2049u;
    constexpr uint32_t kReadBytes = kWidth * kReadHeight * 4u;
    constexpr uint32_t kUploadBytes = kWidth * kUploadHeight * 4u;
    constexpr uint32_t kBeforeColor = 0x40201080u;
    constexpr uint32_t kAfterColor = 0x90d06030u;

    using GetIntegerIndexed = void (APIENTRY *)(GSGL::GLenum, GSGL::GLuint, GSGL::GLint *);
    using GetBufferParameter = void (APIENTRY *)(GSGL::GLenum, GSGL::GLenum, GSGL::GLint *);
    using BindBuffer = void (APIENTRY *)(GSGL::GLenum, GSGL::GLuint);

    class DirectPresentSetting
    {
        bool present = false;
        std::string previous;
    public:
        DirectPresentSetting()
        {
            char *value = nullptr;
            size_t size = 0u;
            if (_dupenv_s(&value, &size, "PS2X_GS_DIRECT_PRESENT") != 0)
                throw std::runtime_error("No se pudo leer PS2X_GS_DIRECT_PRESENT");
            const std::unique_ptr<char, decltype(&std::free)> owned(value, &std::free);
            present = value != nullptr;
            if (present)
                previous = value;
            if (_putenv_s("PS2X_GS_DIRECT_PRESENT", "0") != 0)
                throw std::runtime_error("No se pudo seleccionar presentación por RAM");
        }
        ~DirectPresentSetting()
        {
            _putenv_s("PS2X_GS_DIRECT_PRESENT", present ? previous.c_str() : "");
        }
    };

    struct BufferObservation
    {
        GetIntegerIndexed indexed = reinterpret_cast<GetIntegerIndexed>(wglGetProcAddress("glGetIntegeri_v"));
        GetBufferParameter parameter = reinterpret_cast<GetBufferParameter>(wglGetProcAddress("glGetBufferParameteriv"));
        BindBuffer bind = reinterpret_cast<BindBuffer>(wglGetProcAddress("glBindBuffer"));

        bool ready() const { return indexed && parameter && bind; }
        GSGL::GLint size(GSGL::GLint &id) const
        {
            indexed(kStorageBinding, 7u, &id);
            if (id <= 0)
                return -1;
            bind(GSGL::kShaderStorageBuffer, static_cast<GSGL::GLuint>(id));
            GSGL::GLint bytes = 0;
            parameter(GSGL::kShaderStorageBuffer, kBufferSize, &bytes);
            return bytes;
        }
    };

    GSTransferCommand readCommand()
    {
        GSTransferCommand cmd{};
        cmd.direction = 1u;
        cmd.bitbltbuf.spsm = GS_PSM_CT32;
        cmd.bitbltbuf.sbw = 32u;
        cmd.trxreg.rrw = static_cast<uint16_t>(kWidth);
        cmd.trxreg.rrh = static_cast<uint16_t>(kReadHeight);
        return cmd;
    }

    GSTransferCommand uploadCommand()
    {
        GSTransferCommand cmd{};
        cmd.direction = 0u;
        cmd.bitbltbuf.dpsm = GS_PSM_CT32;
        cmd.bitbltbuf.dbw = 32u;
        cmd.trxreg.rrw = static_cast<uint16_t>(kWidth);
        cmd.trxreg.rrh = static_cast<uint16_t>(kUploadHeight);
        return cmd;
    }

    void compareReadback(TestCase &t, GSCpuBackend &cpu, GSGpuBackend &gpu, bool changed)
    {
        t.Equals(cpu.GetTransferSnapshot().localToHostPendingBytes, uint64_t{kReadBytes}, "CPU prepara toda la lectura grande");
        t.Equals(gpu.GetTransferSnapshot().localToHostPendingBytes, uint64_t{kReadBytes}, "OpenGL prepara toda la lectura grande");
        std::array<uint8_t, 65536u> expected{}, actual{};
        std::array<uint8_t, 4u> beforePixel{}, expectedPixel{};
        const uint32_t color = changed ? kAfterColor : kBeforeColor;
        std::memcpy(beforePixel.data(), &kBeforeColor, sizeof(kBeforeColor));
        std::memcpy(expectedPixel.data(), &color, sizeof(color));
        uint32_t consumed = 0u;
        uint64_t differentBytes = 0u;
        bool reportedMismatch = false;
        while (consumed < kReadBytes)
        {
            const uint32_t request = std::min<uint32_t>(static_cast<uint32_t>(expected.size()), kReadBytes - consumed);
            const uint32_t expectedCount = cpu.ConsumeLocalToHostBytes(expected.data(), request);
            const uint32_t actualCount = gpu.ConsumeLocalToHostBytes(actual.data(), request);
            if (expectedCount != request || actualCount != request)
            {
                t.Fail("La lectura grande terminó antes de recuperar todos los bytes");
                break;
            }
            if (!reportedMismatch && !std::equal(expected.begin(), expected.begin() + request, actual.begin()))
            {
                t.Fail("OpenGL difiere del CPU en la lectura grande, desde byte " + std::to_string(consumed));
                reportedMismatch = true;
            }
            for (uint32_t i = 0u; i < request; ++i)
            {
                if (actual[i] != beforePixel[(consumed + i) & 3u])
                    ++differentBytes;
                if (!reportedMismatch && expected[i] != expectedPixel[(consumed + i) & 3u])
                {
                    t.Fail("El control CPU no contiene el color procedural esperado");
                    reportedMismatch = true;
                }
            }
            consumed += request;
        }
        t.Equals(consumed, kReadBytes, "Se comparan todos los bytes, no una muestra de la lectura");
        t.Equals(differentBytes, changed ? uint64_t{kReadBytes} : uint64_t{0},
                 "La segunda lectura debe cambiar cada byte respecto al color anterior");
        t.Equals(cpu.GetTransferSnapshot().localToHostPendingBytes, uint64_t{0}, "Se vacía la lectura CPU");
        t.Equals(gpu.GetTransferSnapshot().localToHostPendingBytes, uint64_t{0}, "Se vacía la lectura OpenGL");
    }

    void streamingCapacity(TestCase &t)
    {
        DirectPresentSetting setting;
        if (GSSharedPresent::Active())
        {
            t.Fail("El control requiere un contexto propio sin otro host compartido activo");
            return;
        }
        std::vector<uint8_t> cpuRam(GSSwizzle::kMemorySize), gpuRam(GSSwizzle::kMemorySize);
        for (size_t offset = 0u; offset < cpuRam.size(); offset += sizeof(kBeforeColor))
            std::memcpy(cpuRam.data() + offset, &kBeforeColor, sizeof(kBeforeColor));
        gpuRam = cpuRam;
        GSCpuBackend cpu;
        cpu.Initialize(cpuRam.data(), static_cast<uint32_t>(cpuRam.size()));
        GSGpuBackend gpu;
        gpu.SetHardwareRasterAllowed(false);
        gpu.Initialize(gpuRam.data(), static_cast<uint32_t>(gpuRam.size()));
        if (!gpu.IsReady())
        {
            t.Fail("OpenGL efectivo requerido; no se permite fallback CPU");
            return;
        }
        BufferObservation observed;
        if (!observed.ready())
        {
            t.Fail("Funciones OpenGL requeridas para acreditar la capacidad real del buffer");
            return;
        }

        const GSTransferCommand read = readCommand();
        cpu.BeginTransfer(read); gpu.BeginTransfer(read);
        GSGL::GLint dataId = 0;
        const GSGL::GLint initialSize = observed.size(dataId);
        t.IsTrue(initialSize >= static_cast<GSGL::GLint>(kReadBytes), "La primera lectura reserva casi 32 MiB efectivos");
        compareReadback(t, cpu, gpu, false);

        // GOW-Port: una entrada mayor que el chunk de 16 MiB obliga al fallback real.
        // Los píxeles repetidos hacen idempotentes los aliases de la VRAM de 4 MiB.
        std::vector<uint32_t> payload(kUploadBytes / 4u, kAfterColor);
        const GSTransferCommand upload = uploadCommand();
        cpu.BeginTransfer(upload); gpu.BeginTransfer(upload);
        cpu.UploadImage(reinterpret_cast<const uint8_t *>(payload.data()), kUploadBytes);
        gpu.UploadImage(reinterpret_cast<const uint8_t *>(payload.data()), kUploadBytes);
        gpu.Flush();
        GSGL::GLint uploadId = 0;
        const GSGL::GLint uploadSize = observed.size(uploadId);
        t.Equals(uploadId, dataId, "La entrada oversized usa el mismo buffer de entrada/salida");
        t.IsTrue(uploadSize > 16 * 1024 * 1024 && uploadSize < initialSize,
                 "Streaming reduce de verdad el buffer; no pasó por el ring ni conservó el tamaño anterior");
        std::vector<uint8_t> expectedVram, actualVram;
        cpu.SnapshotVram(expectedVram); gpu.SnapshotVram(actualVram);
        t.IsTrue(expectedVram == actualVram, "Upload oversized produce 4 MiB exactamente iguales al CPU");
        t.IsTrue(expectedVram != gpuRam, "El upload cambia la VRAM inicial");

        cpu.BeginTransfer(read); gpu.BeginTransfer(read);
        GSGL::GLint finalId = 0;
        const GSGL::GLint finalSize = observed.size(finalId);
        t.Equals(finalId, dataId, "La segunda lectura usa el mismo objeto OpenGL");
        t.IsTrue(finalSize >= static_cast<GSGL::GLint>(kReadBytes) && finalSize > uploadSize,
                 "La lectura posterior realoca después de achicar el buffer en streaming");
        compareReadback(t, cpu, gpu, true);
    }
}
#endif

void runGpuStreamingGlTests()
{
#ifdef _WIN32
    const char *setting = std::getenv("GOW_GS_GPU_TEST");
    if (!setting || *setting != '1')
        return;
    MiniTest::Case("GS GPU streaming capacidad real", [](TestCase &suite)
    {
        suite.Run("Entrada oversized no deja obsoleta la capacidad de la siguiente lectura", streamingCapacity);
    });
#endif
}

// GOW-Port: sonda aislada de regresión; la suite normal registra la función anterior.
#ifdef GOW_STREAMING_PROBE_MAIN
int main()
{
    runGpuStreamingGlTests();
    return MiniTest::Run();
}
#endif
