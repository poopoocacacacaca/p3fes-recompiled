// Debug aid: a tiny sampling profiler for Linux test runs.
// PS2X_PROFILE=<file> samples every running thread's PC at ~1 kHz (SIGPROF) and
// rewrites <file> every 5 seconds with "tid comm pc count" lines (absolute PCs)
// plus <file>.maps, a copy of /proc/self/maps. scripts/prof_report.py
// turns that into a per-function report. No effect unless the variable is set.
#if defined(__linux__)

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <unordered_map>

#include <sys/syscall.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

namespace
{
    constexpr size_t kMaxSamples = 1u << 23;
    uint64_t *g_pcs = nullptr;
    uint32_t *g_tids = nullptr;
    std::atomic<size_t> g_count{0};

    void onProf(int, siginfo_t *, void *uctx)
    {
        const size_t index = g_count.fetch_add(1, std::memory_order_relaxed);
        if (index >= kMaxSamples)
            return;
        const auto *context = static_cast<ucontext_t *>(uctx);
#if defined(__x86_64__)
        g_pcs[index] = static_cast<uint64_t>(context->uc_mcontext.gregs[REG_RIP]);
#elif defined(__aarch64__)
        g_pcs[index] = static_cast<uint64_t>(context->uc_mcontext.pc);
#else
        g_pcs[index] = 0;
#endif
        g_tids[index] = static_cast<uint32_t>(syscall(SYS_gettid));
    }

    uint64_t exeBase()
    {
        std::ifstream maps("/proc/self/maps");
        char self[4096] = {};
        const ssize_t n = readlink("/proc/self/exe", self, sizeof(self) - 1);
        if (n <= 0)
            return 0;
        std::string line;
        while (std::getline(maps, line))
        {
            if (line.find(self) == std::string::npos)
                continue;
            return std::strtoull(line.c_str(), nullptr, 16);
        }
        return 0;
    }

    std::string threadName(uint32_t tid)
    {
        std::ifstream comm("/proc/self/task/" + std::to_string(tid) + "/comm");
        std::string name;
        std::getline(comm, name);
        for (char &c : name)
            if (c == ' ')
                c = '_';
        return name.empty() ? "?" : name;
    }

    void dump(const char *path, uint64_t base)
    {
        const size_t count = std::min(g_count.load(), kMaxSamples);
        std::map<std::pair<uint32_t, uint64_t>, uint32_t> histogram;
        for (size_t i = 0; i < count; ++i)
            ++histogram[{g_tids[i], g_pcs[i]}];
        (void)base;
        {
            std::ifstream maps("/proc/self/maps");
            std::ofstream copy(std::string(path) + ".maps");
            copy << maps.rdbuf();
        }
        std::unordered_map<uint32_t, std::string> names;
        FILE *out = std::fopen(path, "w");
        if (!out)
            return;
        for (const auto &[key, hits] : histogram)
        {
            auto it = names.find(key.first);
            if (it == names.end())
                it = names.emplace(key.first, threadName(key.first)).first;
            std::fprintf(out, "%u %s %llx %u\n", key.first, it->second.c_str(),
                         static_cast<unsigned long long>(key.second), hits);
        }
        std::fclose(out);
    }

    struct ProfilerStarter
    {
        ProfilerStarter()
        {
            const char *path = std::getenv("PS2X_PROFILE");
            if (!path || !*path)
                return;
            g_pcs = new uint64_t[kMaxSamples];
            g_tids = new uint32_t[kMaxSamples];
            struct sigaction action{};
            action.sa_sigaction = onProf;
            action.sa_flags = SA_SIGINFO | SA_RESTART;
            sigemptyset(&action.sa_mask);
            sigaction(SIGPROF, &action, nullptr);
            itimerval timer{};
            timer.it_interval.tv_usec = 1000;
            timer.it_value.tv_usec = 1000;
            setitimer(ITIMER_PROF, &timer, nullptr);
            const std::string file = path;
            std::thread([file]()
                        {
                            sigset_t block;
                            sigemptyset(&block);
                            sigaddset(&block, SIGPROF);
                            pthread_sigmask(SIG_BLOCK, &block, nullptr);
                            const uint64_t base = exeBase();
                            for (;;)
                            {
                                std::this_thread::sleep_for(std::chrono::seconds(5));
                                dump(file.c_str(), base);
                            } })
                .detach();
        }
    };

    ProfilerStarter g_starter;
}

#endif
