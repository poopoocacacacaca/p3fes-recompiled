#include "ps2_runtime.h"
#include "runtime/ee_scheduler.h"
#include "games_database.h"
#if defined(PS2X_ENABLE_DEBUG_UI) && !defined(PLATFORM_VITA)
#include "ps2_debug_panel.h"
#endif

#ifdef _DEBUG
#include "ps2_log.h"
#endif

#include <cstring>
#include <iostream>
#include <string>
#include <filesystem>
#include <exception>
#include <algorithm>
#include <cstdlib>
#include <chrono>
#include <cstdio>
#include <thread>

namespace
{
    // Debug aid: set PS2X_THREAD_DUMP=<seconds> to print every guest EE thread,
    // semaphore and event flag to stderr at that interval. Uses the same
    // best-effort snapshot the debug panel's Threads/Kernel tabs read.
    void startThreadDumpWatchdog(PS2Runtime &runtime)
    {
        const char *env = std::getenv("PS2X_THREAD_DUMP");
        if (!env || !*env)
            return;
        int seconds = std::atoi(env);
        if (seconds <= 0)
            seconds = 5;
        std::thread([&runtime, seconds]()
                    {
            for (;;)
            {
                std::this_thread::sleep_for(std::chrono::seconds(seconds));
                // PS2X_PEEK="addr[:words][*],..." dumps guest words each tick; a trailing
                // '*' dereferences the word at addr first (pointer to struct).
                if (const char *peek = std::getenv("PS2X_PEEK"); peek && *peek)
                {
                    uint8_t *ram = runtime.memory().getRDRAM();
                    std::string all(peek);
                    size_t pos = 0;
                    while (ram && pos < all.size())
                    {
                        size_t comma = all.find(',', pos);
                        std::string item = all.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                        bool deref = !item.empty() && item.back() == '*';
                        if (deref)
                            item.pop_back();
                        uint32_t addr = static_cast<uint32_t>(std::strtoul(item.c_str(), nullptr, 16));
                        uint32_t words = 8;
                        if (size_t colon = item.find(':'); colon != std::string::npos)
                            words = static_cast<uint32_t>(std::strtoul(item.c_str() + colon + 1, nullptr, 0));
                        auto rd = [&](uint32_t a) { uint32_t v = 0; std::memcpy(&v, ram + (a & 0x1FFFFFCu), 4); return v; };
                        uint32_t base = deref ? rd(addr) : addr;
                        std::fprintf(stderr, "[peek] %08X%s ->", addr, deref ? "*" : "");
                        for (uint32_t w = 0; w < words && w < 256; ++w)
                            std::fprintf(stderr, " %08X", rd(base + w * 4));
                        std::fprintf(stderr, "\n");
                        if (comma == std::string::npos)
                            break;
                        pos = comma + 1;
                    }
                }
                // PS2X_KWLN_HEAD=<addr of running-task list head>: game-specific (Persona 3
                // FES "kwln" task scheduler) walker; prints each task's name/state/timer/update.
                if (const char *head = std::getenv("PS2X_KWLN_HEAD"); head && *head)
                {
                    uint8_t *ram = runtime.memory().getRDRAM();
                    auto rd = [&](uint32_t a) { uint32_t v = 0; std::memcpy(&v, ram + (a & 0x1FFFFFCu), 4); return v; };
                    uint32_t task = rd(static_cast<uint32_t>(std::strtoul(head, nullptr, 16)));
                    std::fprintf(stderr, "[kwln]");
                    for (int n = 0; task != 0 && n < 64; ++n)
                    {
                        char name[25] = {};
                        std::memcpy(name, ram + (task & 0x1FFFFFFu), 24);
                        std::fprintf(stderr, " {%s st=%u t=%u upd=%08X w=%08X}", name, rd(task + 0x1C) & 0xF, rd(task + 0x28), rd(task + 0x34), rd(task + 0x3C));
                        if (const char *watch = std::getenv("PS2X_KWLN_WATCH"); watch && std::strcmp(watch, name) == 0 && rd(task + 0x3C))
                        {
                            const uint32_t w = rd(task + 0x3C);
                            std::fprintf(stderr, "\n[kwln-watch] %s work=%08X:", name, w);
                            for (uint32_t o = 0; o < 0x220; o += 4)
                                std::fprintf(stderr, "%s%08X", (o % 32) ? " " : "\n  ", rd(w + o));
                            std::fprintf(stderr, "\n");
                        }
                        task = rd(task + 0x40);
                    }
                    std::fprintf(stderr, "\n");
                }
                const EeKernelSnapshot s = runtime.eeScheduler().snapshot();
                std::fprintf(stderr, "[thread-dump] cycle=%llu running=%d threads=%zu semas=%zu flags=%zu\n",
                             static_cast<unsigned long long>(s.eeCycle), s.runningThreadId,
                             s.threads.size(), s.semaphores.size(), s.eventFlags.size());
                for (const EeThreadSnapshot &t : s.threads)
                {
                    std::fprintf(stderr,
                                 "[thread-dump]  thread id=%d status=%d wait=%d waitId=%d pc=0x%08X ra=0x%08X sp=0x%08X entry=0x%08X prio=%d/%d wake=%u susp=%d depth=%u\n",
                                 t.id, static_cast<int>(t.status), static_cast<int>(t.waitReason), t.waitId,
                                 t.pc, t.ra, t.sp, t.entry, t.currentPriority, t.initialPriority,
                                 t.wakeupCount, t.suspendCount, t.invocationDepth);
                }
                for (const EeSemaphoreSnapshot &m : s.semaphores)
                {
                    std::fprintf(stderr, "[thread-dump]  sema id=%d count=%d max=%d waiters=%u\n",
                                 m.id, m.count, m.maxCount, m.waiters);
                }
                for (const EeEventFlagSnapshot &e : s.eventFlags)
                {
                    std::fprintf(stderr, "[thread-dump]  flag id=%d bits=0x%08X init=0x%08X attr=0x%X waiters=%u\n",
                                 e.id, e.bits, e.initBits, e.attr, e.waiters);
                }
                std::fflush(stderr);
            } })
            .detach();
    }
}

#if defined(__ANDROID__)
#include <android/log.h>
#include <unistd.h>
#include <thread>
#include <cstdio>
#include <cstring>
#endif

namespace
{
#if defined(__ANDROID__)
    int g_logcatPipeFds[2]{-1, -1};
    std::thread g_logcatThread;

    void stopLogcatRedirect()
    {
        std::fflush(stdout);
        std::fflush(stderr);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
        if (g_logcatPipeFds[1] >= 0)
        {
            close(g_logcatPipeFds[1]);
            g_logcatPipeFds[1] = -1;
        }
        if (g_logcatThread.joinable())
        {
            g_logcatThread.join();
        }
    }

    void redirectStdioToLogcat()
    {
        if (pipe(g_logcatPipeFds) != 0)
        {
            return;
        }

        setvbuf(stdout, nullptr, _IOLBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        dup2(g_logcatPipeFds[1], STDOUT_FILENO);
        dup2(g_logcatPipeFds[1], STDERR_FILENO);

        g_logcatThread = std::thread([]()
                                     {
                                         FILE *reader = fdopen(g_logcatPipeFds[0], "r");
                                         if (!reader)
                                         {
                                             return;
                                         }
                                         char line[1024];
                                         while (fgets(line, sizeof(line), reader))
                                         {
                                             size_t len = std::strlen(line);
                                             if (len > 0 && line[len - 1] == '\n')
                                             {
                                                 line[len - 1] = '\0';
                                             }
                                             __android_log_write(ANDROID_LOG_INFO, "ps2x", line);
                                         }
                                         fclose(reader);
                                         g_logcatPipeFds[0] = -1;
                                     });
        if (std::atexit(stopLogcatRedirect) != 0)
        {
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
            close(g_logcatPipeFds[1]);
            g_logcatPipeFds[1] = -1;
            if (g_logcatThread.joinable())
            {
                g_logcatThread.join();
            }
        }
    }
#endif

    void setupTerminateLogger() // to help on release build crashs
    {
        std::set_terminate([]()
                           {
                               std::cerr << "[terminate] unhandled exception" << std::endl;
                               const std::exception_ptr ep = std::current_exception();
                               if (ep)
                               {
                                   try
                                   {
                                       std::rethrow_exception(ep);
                                   }
                                   catch (const std::system_error &e)
                                   {
                                       std::cerr << "[terminate] std::system_error code=" << e.code().value()
                                                 << " category=" << e.code().category().name()
                                                 << " message=" << e.what() << std::endl;
                                   }
                                   catch (const std::exception &e)
                                   {
                                       std::cerr << "[terminate] std::exception: " << e.what() << std::endl;
                                   }
                                   catch (...)
                                   {
                                       std::cerr << "[terminate] non-std exception" << std::endl;
                                   }
                               }
                               std::abort(); });
    }

    std::string normalizeGameId(const std::string &folderName)
    {
        std::string result = folderName;

        size_t underscore = result.find('_');
        if (underscore != std::string::npos)
            result[underscore] = '-';

        size_t dot = result.find('.');
        if (dot != std::string::npos)
            result.erase(dot, 1);

        std::ranges::transform(result, result.begin(), [](unsigned char character)
                               { return static_cast<char>(std::toupper(character)); });

        return result;
    }

    std::filesystem::path getExecutablePath(int argc, char *argv[])
    {
        if (argc >= 2 && argv[1] && argv[1][0] != '\0')
        {
            std::cout << "Using argv boot path" << std::endl;
            return std::filesystem::path(argv[1]);
        }
#if defined(PS2X_DEFAULT_BOOT_ELF)
        std::cout << "Using default boot file" << std::endl;
        const std::filesystem::path configuredPath = std::filesystem::path(PS2X_DEFAULT_BOOT_ELF);
#if defined(PLATFORM_VITA)
        return configuredPath;
#endif
        if (configuredPath.is_absolute())
        {
            return configuredPath;
        }
        return (std::filesystem::current_path() / configuredPath).lexically_normal();
#else
        throw std::runtime_error("Unable to determine executable path. Pass the guest ELF as argv[1] or define PS2X_DEFAULT_BOOT_ELF.");
#endif
    }
}

#if defined(PS2X_RUNTIME_LIBRARY_ENTRY)
#include "ps2x_abi.h"
#if defined(_WIN32)
// Declared by hand: windows.h clashes with raylib's names.
extern "C" __declspec(dllimport) int __stdcall MessageBoxA(void *, const char *, const char *, unsigned int);
#endif

int ps2xRuntimeMain(int argc, char *argv[], const char *abiHash)
{
    setupTerminateLogger();
    // The game executable and this runtime library must be built from the same runtime headers (layout of the CPU
    // context, inline helpers). Otherwise the executable needs rebuilding.
    if (!abiHash || std::strcmp(abiHash, PS2X_RUNTIME_ABI_HASH) != 0)
    {
        std::cerr << "[abi] game executable built against runtime headers " << (abiHash ? abiHash : "?")
                  << ", runtime library is " << PS2X_RUNTIME_ABI_HASH << ": a newer game executable is needed" << std::endl;
#if defined(_WIN32)
        const int answer = MessageBoxA(nullptr,
                                       "This update of the engine (ps2runtime.dll) needs a newer game executable "
                                       "(ps2EntryRunner.exe).\n\nIt may crash or misbehave. Continue anyway?",
                                       "P3FES Recompiled", 0x4u /* MB_YESNO */ | 0x30u /* MB_ICONWARNING */);
        if (answer != 6 /* IDYES */)
            return 2;
#endif
    }
#else
int main(int argc, char *argv[])
{
#if defined(__ANDROID__)
    redirectStdioToLogcat();
#endif
    setupTerminateLogger();
    ps2SetRecompiledFunctionTable({g_ps2RecompiledFunctionTable, g_ps2RecompiledFunctionTableBase,
                                   g_ps2RecompiledFunctionTableEnd, g_ps2RecompiledFunctionTableSlotCount});
#endif

    try
    {
        std::filesystem::path pathObj = getExecutablePath(argc, argv);

        std::string filePathStr = pathObj.string();
        std::string elfName = pathObj.filename().string();
        std::string normalizedId = normalizeGameId(elfName);

        std::string windowTitle = "PS2-Recomp | ";
        const char *gameName = getGameName(normalizedId);

#if !defined(PLATFORM_VITA)
        if (gameName)
        {
            windowTitle += std::string(gameName) + " | " + elfName;
        }
        else
#endif
        {
            windowTitle += elfName;
        }

        PS2Runtime runtime;
#if defined(PS2X_ENABLE_DEBUG_UI) && !defined(PLATFORM_VITA)
        // This hook is to prevent leak rlimgui deps to recompiler etc
        PS2DebugPanel debugPanel;
        runtime.setDebugUiCallbacks(
            [](PS2Runtime &rt, void *userData)
            {
                (void)rt;
                static_cast<PS2DebugPanel *>(userData)->initialize();
            },
            [](PS2Runtime &rt, void *userData)
            {
                static_cast<PS2DebugPanel *>(userData)->draw(rt);
            },
            [](PS2Runtime &rt, void *userData)
            {
                (void)rt;
                static_cast<PS2DebugPanel *>(userData)->shutdown();
            },
            &debugPanel);
#endif
        if (!runtime.initialize(windowTitle.c_str()))
        {
            std::cerr << "Failed to initialize PS2 runtime" << std::endl;
            return 1;
        }

        if (!runtime.loadELF(filePathStr))
        {
            std::cerr << "Failed to load ELF file: " << filePathStr << std::endl;
            return 1;
        }

        startThreadDumpWatchdog(runtime);
        runtime.run();

#ifdef _DEBUG
        ps2_log::print_saved_location();
#endif
        std::cout.flush();
        std::cerr.flush();
        std::_Exit(0);
    }
    catch (const std::exception &e)
    {
        std::cerr << "[main] fatal exception: " << e.what() << std::endl;
    }
    catch (...)
    {
        std::cerr << "[main] fatal exception: unknown" << std::endl;
    }

    std::cout.flush();
    std::cerr.flush();
    std::_Exit(1);
}
