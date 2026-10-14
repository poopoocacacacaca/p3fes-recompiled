// GOW-Port: comprobar la liberacion del ring en un share group que sigue vivo.
// Sin assets ni getters de produccion: se observan objetos del driver OpenGL.
#include "MiniTest.h"

#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_gpu_backend.h"
#include "runtime/gs/gs_shared_present.h"
#include "runtime/gs/gs_swizzle.h"
#include "gs/gpu/gs_gl.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    constexpr GSGL::GLenum kShaderStorageBufferBinding = 0x90D3;
    constexpr GSGL::GLenum kBufferSize = 0x8764;
    constexpr GSGL::GLenum kBufferMapped = 0x88BC;
    using GetIntegerIndexed = void (APIENTRY *)(GSGL::GLenum, GSGL::GLuint, GSGL::GLint *);
    using GetBufferParameter = void (APIENTRY *)(GSGL::GLenum, GSGL::GLenum, GSGL::GLint *);
    using IsBuffer = GSGL::GLboolean (APIENTRY *)(GSGL::GLuint);

    std::string readSetting(const char *name, bool &present)
    {
        char *value = nullptr;
        size_t size = 0u;
        if (_dupenv_s(&value, &size, name) != 0)
            throw std::runtime_error(std::string("No se pudo leer ") + name);
        const std::unique_ptr<char, decltype(&std::free)> owned(value, &std::free);
        present = value != nullptr;
        return present ? std::string(value) : std::string();
    }

    class SharedHost
    {
        GSGL::Context context;
        HDC previousDc = wglGetCurrentDC();
        HGLRC previousContext = wglGetCurrentContext();
        bool hadSetting = false;
        bool captured = false;
        std::string oldSetting;

    public:
        SharedHost()
        {
            oldSetting = readSetting("PS2X_GS_DIRECT_PRESENT", hadSetting);
        }

        ~SharedHost()
        {
            if (captured)
            {
                GSSharedPresent::RestoreHostContext();
                GSSharedPresent::ShutdownHost();
            }
            context.Destroy();
            wglMakeCurrent(previousDc, previousContext);
            _putenv_s("PS2X_GS_DIRECT_PRESENT", hadSetting ? oldSetting.c_str() : "");
            // GOW-Port: reponer tambien la captura del host anterior si la tenia.
            if (captured && previousContext && hadSetting && oldSetting == "1")
            {
                GSSharedPresent::CaptureHostContext();
                GSSharedPresent::RestoreHostContext();
            }
        }

        bool initialize(TestCase &t)
        {
            if (GSSharedPresent::Active())
            {
                t.Fail("El control requiere un host compartido propio, sin otro backend activo");
                return false;
            }
            if (_putenv_s("PS2X_GS_DIRECT_PRESENT", "1") != 0)
            {
                t.Fail("No se pudo configurar la presentacion compartida del control");
                return false;
            }
            std::string error;
            if (!context.Create(error))
            {
                t.Fail("Contexto host OpenGL requerido: " + error);
                return false;
            }
            GSSharedPresent::CaptureHostContext();
            captured = true;
            GSSharedPresent::RestoreHostContext();
            if (wglGetCurrentContext() == nullptr)
            {
                t.Fail("No se pudo restaurar el host oculto despues de capturarlo");
                return false;
            }
            return true;
        }
    };

    GSPrimitiveBatch ringSprite()
    {
        GSPrimitiveBatch draw{};
        draw.vertexCount = 2u;
        draw.state.prim.type = GS_PRIM_SPRITE;
        draw.state.colclamp = 1u;
        draw.state.context.frame.fbp = 2u;
        draw.state.context.frame.fbw = 1u;
        draw.state.context.frame.psm = GS_PSM_CT32;
        draw.state.context.zbuf.zbp = 68u;
        draw.state.context.zbuf.zmask = true;
        draw.state.context.scissor = {0u, 63u, 0u, 31u};
        draw.state.context.test = 1ull << 17u; // ZTST=ALWAYS, como los controles GS.
        for (GSVertex &vertex : draw.vertices)
        {
            vertex.r = 32u;
            vertex.g = 64u;
            vertex.b = 96u;
            vertex.a = 128u;
            vertex.q = 1.0f;
        }
        draw.vertices[0].x = 1.0f;
        draw.vertices[0].y = 1.0f;
        draw.vertices[1].x = 17.0f;
        draw.vertices[1].y = 17.0f;
        return draw;
    }

    void sharedRingDeleted(TestCase &t)
    {
        SharedHost host;
        if (!host.initialize(t))
            return;

        std::vector<uint8_t> cpuRam(GSSwizzle::kMemorySize);
        std::vector<uint8_t> gpuRam(GSSwizzle::kMemorySize);
        GSCpuBackend cpu;
        cpu.Initialize(cpuRam.data(), static_cast<uint32_t>(cpuRam.size()));
        const GSPrimitiveBatch draw = ringSprite();
        cpu.Submit(draw);
        std::vector<uint8_t> expected;
        cpu.SnapshotVram(expected);
        t.Equals(cpu.ReadVram(GS_PSM_CT32, 2u << 5u, 1u, 5u, 7u),
                 0x80604020u, "El control CPU dibuja color no nulo");

        auto gpu = std::make_unique<GSGpuBackend>();
        gpu->SetHardwareRasterAllowed(false);
        gpu->Initialize(gpuRam.data(), static_cast<uint32_t>(gpuRam.size()));
        if (!gpu->IsReady())
        {
            t.Fail("Backend OpenGL requerido; no se admite fallback CPU");
            return;
        }
        if (!GSSharedPresent::Active())
        {
            t.Fail("El contexto del backend debe compartir objetos con el host vivo");
            return;
        }
        const auto getIntegerIndexed = reinterpret_cast<GetIntegerIndexed>(wglGetProcAddress("glGetIntegeri_v"));
        const auto getBufferParameter = reinterpret_cast<GetBufferParameter>(wglGetProcAddress("glGetBufferParameteriv"));
        const auto isBuffer = reinterpret_cast<IsBuffer>(wglGetProcAddress("glIsBuffer"));
        if (!getIntegerIndexed || !getBufferParameter || !isBuffer)
        {
            t.Fail("glGetIntegeri_v y glIsBuffer requeridos para observar objetos reales");
            return;
        }

        gpu->Submit(draw);
        gpu->Flush();
        t.Equals(gpu->GetStats().prims, uint64_t{1}, "OpenGL procesa el sprite real");
        GSGL::GLint ringId = 0;
        getIntegerIndexed(kShaderStorageBufferBinding, 3u, &ringId);
        if (ringId <= 0 || !isBuffer(static_cast<GSGL::GLuint>(ringId)))
        {
            t.Fail("El upload de primitivas debe enlazar un buffer OpenGL valido");
            return;
        }
        // GOW-Port: binding 3 tambien tiene fallback; exigir el ring persistente real.
        auto bindBuffer = reinterpret_cast<void (APIENTRY *)(GSGL::GLenum, GSGL::GLuint)>(wglGetProcAddress("glBindBuffer"));
        if (!bindBuffer)
        {
            t.Fail("glBindBuffer requerido para comprobar el ring persistente");
            return;
        }
        bindBuffer(GSGL::kShaderStorageBuffer, static_cast<GSGL::GLuint>(ringId));
        GSGL::GLint size = 0, mapped = 0;
        getBufferParameter(GSGL::kShaderStorageBuffer, kBufferSize, &size);
        getBufferParameter(GSGL::kShaderStorageBuffer, kBufferMapped, &mapped);
        if (size != 64 * 1024 * 1024 || !mapped)
        {
            t.Fail("El control requiere el ring mapeado de 64 MiB, no el buffer fallback");
            return;
        }
        // GOW-Port: descartar una sonda vacia: el upload debe producir VRAM exacta.
        std::vector<uint8_t> actual;
        gpu->SnapshotVram(actual);
        t.IsTrue(actual == expected, "Los 4 MiB de VRAM coinciden exactamente con CPU");

        // El host queda vivo: borrar solo el contexto del backend no libera el ring.
        gpu.reset();
        GSSharedPresent::RestoreHostContext();
        if (!wglGetCurrentContext())
        {
            t.Fail("Host requerido despues de destruir el backend");
            return;
        }
        t.IsFalse(isBuffer(static_cast<GSGL::GLuint>(ringId)) != 0u,
                  "El destructor elimina el ring de uploads aunque siga vivo el share group");
    }
}
#endif

void runGpuRingGlTests()
{
#ifdef _WIN32
    bool present = false;
    const std::string setting = readSetting("GOW_GS_GPU_TEST", present);
    if (!present || setting != "1")
        return;
    MiniTest::Case("GS GPU ring compartido", [](TestCase &suite)
    {
        suite.Run("Destruir backend elimina el ring sin cerrar el host", sharedRingDeleted);
    });
#endif
}
