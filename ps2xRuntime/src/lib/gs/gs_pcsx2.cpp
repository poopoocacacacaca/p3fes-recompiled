// PCSX2's GS as the port's renderer; see include/runtime/gs/gs_pcsx2.h.
#include "runtime/gs/gs_pcsx2.h"
#include "runtime/gs/gs_frontend.h"
#include "runtime/gs/gs_cpu_backend.h"
#include "runtime/gs/gs_threaded_backend.h"
#include "runtime/ps2_memory.h"
#include "pcsx2gs_api.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

namespace
{
    // Queue records: [type u8][path u8][pad u16][size u32][payload].
    enum class Rec : uint8_t
    {
        Gif = 1,
        Vsync = 2,
        Reset = 3,
    };
    struct RecHeader
    {
        Rec type;
        uint8_t path;
        uint16_t pad;
        uint32_t size;
    };
    static_assert(sizeof(RecHeader) == 8);

    std::string ExecutableDir()
    {
#if defined(_WIN32)
        wchar_t path[MAX_PATH] = {};
        const DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring w(path, n);
        const size_t slash = w.find_last_of(L"\\/");
        w = slash == std::wstring::npos ? L"." : w.substr(0, slash);
        const int bytes = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string out(bytes > 0 ? bytes - 1 : 0, '\0');
        if (bytes > 1)
            WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), bytes, nullptr, nullptr);
        return out;
#else
        char path[4096] = {};
        const ssize_t n = readlink("/proc/self/exe", path, sizeof(path) - 1);
        std::string s(path, n > 0 ? static_cast<size_t>(n) : 0u);
        const size_t slash = s.find_last_of('/');
        return slash == std::string::npos ? std::string(".") : s.substr(0, slash);
#endif
    }

    const char *EnvOr(const char *name, const char *fallback)
    {
        const char *v = std::getenv(name);
        return v && *v ? v : fallback;
    }

    class Pcsx2Renderer final : public GSExternalObserver
    {
    public:
        bool Start(const Pcsx2GsApi *api, uint32_t renderer, float scale, std::string resources, std::string cache,
                   std::string options)
        {
            m_api = api;
            m_resources = std::move(resources);
            m_cache = std::move(cache);
            m_options = std::move(options);
            m_renderer = renderer;
            m_scale = scale;
            if (const char *v = std::getenv("PS2X_PCSX2_FRAMES_IN_FLIGHT"))
                m_maxInFlight = static_cast<uint32_t>(std::max(1, std::atoi(v)));
            m_thread = std::thread([this] { Run(); });
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_openResult != 0; });
            return m_openResult > 0;
        }

        void Stop()
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_quit = true;
            }
            m_cv.notify_all();
            if (m_thread.joinable())
                m_thread.join();
        }

        // --- GSExternalObserver (called with the GS front end's lock held, on the EE/VU1/GS-front thread) ---
        void gif(GifPathId path, const uint8_t *data, uint32_t size) noexcept override
        {
            const uint32_t bytes = size & ~15u;
            if (!data || !bytes)
                return;
            Append(Rec::Gif, static_cast<uint8_t>(path), data, bytes);
        }

        void reg(uint8_t address, uint64_t value) noexcept override
        {
            // A one-register A+D packet on PCSX2's otherwise unused merged-stream path.
            uint64_t packet[4];
            packet[0] = 1ull | (1ull << 15) | (0ull << 58) | (1ull << 60); // NLOOP 1, EOP, PACKED, NREG 1
            packet[1] = 0xEull;                                              // REGS: A+D
            packet[2] = value;
            packet[3] = address;
            Append(Rec::Gif, 0u, packet, sizeof(packet));
        }

        void vsync(const GSRegisters &r, uint64_t, uint32_t) noexcept override
        {
            Pcsx2GsPrivRegs regs{};
            const uint64_t set1[15] = {r.pmode, r.smode1, r.smode2, r.srfsh, r.synch1, r.synch2, r.syncv, r.dispfb1,
                                       r.display1, r.dispfb2, r.display2, r.extbuf, r.extdata, r.extwrite, r.bgcolor};
            std::memcpy(regs.regset1, set1, sizeof(set1));
            regs.csr = r.csr.load(std::memory_order_relaxed);
            regs.imr = r.imr;
            regs.busdir = r.busdir;
            regs.siglblid = r.siglblid;
            Append(Rec::Vsync, 0u, &regs, sizeof(regs));
            // Back-pressure: the game may run at most m_maxInFlight fields ahead of the renderer.
            std::unique_lock<std::mutex> lock(m_mutex);
            ++m_vsyncsQueued;
            m_cv.notify_all();
            m_cv.wait(lock, [this] { return m_quit || m_vsyncsQueued - m_vsyncsDone <= m_maxInFlight; });
        }

        void reset() noexcept override
        {
            Append(Rec::Reset, 0u, nullptr, 0u);
        }

        bool CopyLatest(std::vector<uint8_t> &rgba, uint32_t &width, uint32_t &height, uint64_t &serial)
        {
            std::lock_guard<std::mutex> lock(m_frameMutex);
            if (m_frameSerial == serial || m_frame.empty())
                return false;
            rgba = m_frame;
            width = m_frameWidth;
            height = m_frameHeight;
            serial = m_frameSerial;
            return true;
        }

    private:
        void Append(Rec type, uint8_t path, const void *data, uint32_t size)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            RecHeader h{type, path, 0u, size};
            const size_t at = m_pending.size();
            m_pending.resize(at + sizeof(h) + size);
            std::memcpy(m_pending.data() + at, &h, sizeof(h));
            if (size)
                std::memcpy(m_pending.data() + at + sizeof(h), data, size);
            // Large uploads (movie frames) start rendering before the vsync arrives.
            if (m_pending.size() >= 256u * 1024u || type != Rec::Gif)
                m_cv.notify_all();
        }

        void Run()
        {
            Pcsx2GsOpenParams p{};
            p.hwnd = nullptr; // headless: the host window shows the read-back field
            p.width = 640;
            p.height = 448;
            p.scale = m_scale;
            p.resources = m_resources.c_str();
            p.cache = m_cache.c_str();
            p.vsync = 0;
            p.stretch = 1;
            p.options = m_options.c_str();
            p.renderer = m_renderer;
            const int ok = m_api->open(&p);
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_openResult = ok ? 1 : -1;
            }
            m_cv.notify_all();
            if (!ok)
                return;

            std::vector<uint8_t> work;
            for (;;)
            {
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_cv.wait_for(lock, std::chrono::milliseconds(2), [this] { return m_quit || !m_pending.empty(); });
                    if (m_quit)
                        break;
                    work.swap(m_pending);
                    m_pending.clear();
                }
                uint32_t vsyncs = 0;
                size_t at = 0;
                while (at + sizeof(RecHeader) <= work.size())
                {
                    RecHeader h;
                    std::memcpy(&h, work.data() + at, sizeof(h));
                    const uint8_t *payload = work.data() + at + sizeof(h);
                    at += sizeof(h) + h.size;
                    switch (h.type)
                    {
                    case Rec::Gif:
                        m_api->gif_path(h.path, payload, h.size / 16u);
                        break;
                    case Rec::Vsync:
                    {
                        Pcsx2GsPrivRegs regs;
                        std::memcpy(&regs, payload, sizeof(regs));
                        m_api->vsync(&regs);
                        PublishFrame();
                        ++vsyncs;
                        break;
                    }
                    case Rec::Reset:
                        m_api->reset();
                        break;
                    }
                    if (vsyncs)
                    {
                        // Release the game as soon as each field is out, not after the whole batch.
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_vsyncsDone += vsyncs;
                        vsyncs = 0;
                        m_cv.notify_all();
                    }
                }
                work.clear();
                if (work.capacity() > 64u * 1024u * 1024u)
                    std::vector<uint8_t>().swap(work);
            }
            m_api->close();
        }

        void PublishFrame()
        {
            uint32_t w = 0, h = 0;
            const uint32_t *pixels = nullptr;
            if (!m_api->frame(&w, &h, &pixels) || !pixels || !w || !h)
                return;
            if (w != m_frameWidth || h != m_frameHeight)
                std::fprintf(stderr, "[pcsx2-gs] presented field %ux%u\n", w, h);
            if (m_statsEnabled)
            {
                ++m_statFields;
                const auto now = std::chrono::steady_clock::now();
                const double secs = std::chrono::duration<double>(now - m_statStart).count();
                if (secs >= 5.0)
                {
                    std::fprintf(stderr, "[pcsx2-gs] %.1f fields/s\n", m_statFields / secs);
                    m_statFields = 0;
                    m_statStart = now;
                }
            }
            std::lock_guard<std::mutex> lock(m_frameMutex);
            m_frame.resize(static_cast<size_t>(w) * h * 4u);
            std::memcpy(m_frame.data(), pixels, m_frame.size());
            for (size_t i = 3; i < m_frame.size(); i += 4)
                m_frame[i] = 0xFF; // PCSX2 keeps the framebuffer alpha; the host window shows it opaque
            m_frameWidth = w;
            m_frameHeight = h;
            ++m_frameSerial;
        }

        const Pcsx2GsApi *m_api = nullptr;
        std::string m_resources, m_cache, m_options;
        uint32_t m_renderer = 0;
        float m_scale = 1.0f;
        std::thread m_thread;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::vector<uint8_t> m_pending;
        uint64_t m_vsyncsQueued = 0, m_vsyncsDone = 0;
        uint32_t m_maxInFlight = 2;
        int m_openResult = 0;
        bool m_quit = false;

        std::mutex m_frameMutex;
        std::vector<uint8_t> m_frame;
        uint32_t m_frameWidth = 0, m_frameHeight = 0;
        uint64_t m_frameSerial = 0;
        bool m_statsEnabled = std::getenv("PS2X_PCSX2_STATS") != nullptr;
        uint32_t m_statFields = 0;
        std::chrono::steady_clock::time_point m_statStart = std::chrono::steady_clock::now();
    };

    std::shared_ptr<Pcsx2Renderer> g_renderer;
    GS *g_gs = nullptr;
    std::atomic<bool> g_active{false};
}

namespace ps2_pcsx2gs
{
    bool Requested()
    {
        const char *v = std::getenv("PS2X_GS_RENDERER");
        return v && std::strncmp(v, "pcsx2", 5) == 0;
    }

    bool Install(GS &gs)
    {
        if (g_renderer)
            return true;
        const std::string dir = ExecutableDir();
#if defined(_WIN32)
        const std::string libPath = dir + "\\pcsx2gs.dll";
        const int wlen = MultiByteToWideChar(CP_UTF8, 0, libPath.c_str(), -1, nullptr, 0);
        std::wstring wpath(wlen > 0 ? wlen - 1 : 0, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, libPath.c_str(), -1, wpath.data(), wlen);
        HMODULE lib = LoadLibraryW(wpath.c_str());
        auto getApi = lib ? reinterpret_cast<Pcsx2GsGetApiFn>(GetProcAddress(lib, "pcsx2gs_get_api")) : nullptr;
        const char sep = '\\';
#else
        const std::string libPath = dir + "/pcsx2gs.so";
        void *lib = dlopen(libPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!lib)
            std::fprintf(stderr, "[pcsx2-gs] %s\n", dlerror());
        auto getApi = lib ? reinterpret_cast<Pcsx2GsGetApiFn>(dlsym(lib, "pcsx2gs_get_api")) : nullptr;
        const char sep = '/';
#endif
        const Pcsx2GsApi *api = getApi ? getApi(PCSX2GS_API_VERSION) : nullptr;
        if (!api)
        {
            std::fprintf(stderr, "[pcsx2-gs] cannot load %s (API %u); using the built-in renderer\n", libPath.c_str(),
                         PCSX2GS_API_VERSION);
            return false;
        }
        const char *mode = std::getenv("PS2X_GS_RENDERER");
        const uint32_t renderer = (mode && std::strcmp(mode, "pcsx2-sw") == 0) ? 1u : 0u;
        const float scale = static_cast<float>(std::atof(EnvOr("PS2X_PCSX2_SCALE", "1")));
        auto r = std::make_shared<Pcsx2Renderer>();
        if (!r->Start(api, renderer, scale, dir + sep + "pcsx2gs_resources", dir + sep + "pcsx2gs_cache",
                      EnvOr("PS2X_PCSX2_OPTIONS", "")))
        {
            std::fprintf(stderr, "[pcsx2-gs] renderer did not start; using the built-in renderer\n");
            r->Stop();
            return false;
        }
        g_renderer = r;
        g_gs = &gs;
        // The built-in rasterizer keeps tracking GS state and local memory (for local -> host reads) but stops
        // drawing, which was the movie's bottleneck. PS2X_PCSX2_KEEP_INTERNAL=1 keeps it drawing (comparisons).
        const char *keep = std::getenv("PS2X_PCSX2_KEEP_INTERNAL");
        if (!(keep && *keep == '1'))
        {
#if defined(_WIN32)
            _putenv_s("PS2X_GS_DISCARD_DRAWS", "1");
#else
            setenv("PS2X_GS_DISCARD_DRAWS", "1", 1);
#endif
            gs.setRasterBackend(std::make_unique<GSThreadedBackend>(std::make_unique<GSCpuBackend>()));
        }
        gs.setExternalObserver(r);
        g_active = true;
        std::fprintf(stderr, "[pcsx2-gs] attached (%s renderer)\n", renderer ? "software" : "hardware");
        return true;
    }

    bool Active()
    {
        return g_active.load(std::memory_order_relaxed);
    }

    bool CopyLatestFrame(std::vector<uint8_t> &rgba, uint32_t &width, uint32_t &height, uint64_t &serial)
    {
        auto r = g_renderer;
        return r && r->CopyLatest(rgba, width, height, serial);
    }

    void Shutdown()
    {
        if (!g_renderer)
            return;
        g_active = false;
        if (g_gs)
            g_gs->setExternalObserver(nullptr);
        g_renderer->Stop();
        g_renderer.reset();
        g_gs = nullptr;
    }
}
