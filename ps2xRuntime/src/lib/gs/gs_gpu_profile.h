// Own collector of OpenGL timestamps, with no driver dependencies.
// Inspired by nathanialf/ico-pc, rd_perf.c @ 46de16d393b817290e1bbf5de63a6de10cf9575c (no code copied).
// Windows keep counters and samples together.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>

namespace GSGpuProfile
{
    constexpr size_t kMaxSamplesPerWindow = 4096u;
    constexpr size_t kMaxWaitingWindows = 3u;
    constexpr uint64_t kFramesPerWindow = 4u;

    struct Pending
    {
        uint32_t kind;
        uint32_t begin;
        uint32_t end;
        std::array<uint32_t, 8> info;
    };

    struct Window
    {
        std::vector<Pending> pending;
        std::vector<std::pair<uint64_t, std::array<uint32_t, 8>>> rasterTimes;
        std::array<uint64_t, 8> ns{};
        std::array<uint64_t, 8> count{};
        uint64_t pairs = 0u;
        uint64_t tiles = 0u;
        uint64_t maxBin = 0u;
        uint64_t sumMaxBin = 0u;
        uint64_t prims = 0u;
        uint64_t frames = 0u;
        uint64_t skippedQueries = 0u;
        std::array<uint64_t, 6> reasons{};
        std::array<uint64_t, 3> uploadHazards{};
    };

    class Queue : public Window
    {
    public:
        bool enabled = false;
        std::vector<uint32_t> freeQueries;
        std::array<uint32_t, 8> nextInfo{};

        // under pressure measuring stops, without stopping rendering.
        bool sampleAllowed()
        {
            if (pending.size() < kMaxSamplesPerWindow)
                return true;
            ++skippedQueries;
            return false;
        }

        void endFrame()
        {
            ++frames;
            if (frames >= kFramesPerWindow && waiting.size() < kMaxWaitingWindows)
                seal();
        }

        size_t waitingWindows() const { return waiting.size(); }

        // only shutdown may collect results still pending.
        void finish()
        {
            if (frames != 0u || !pending.empty() || skippedQueries != 0u ||
                prims != 0u || pairs != 0u || tiles != 0u || maxBin != 0u || sumMaxBin != 0u ||
                reasons != std::array<uint64_t, 6>{} || uploadHazards != std::array<uint64_t, 3>{})
                seal();
        }

        template <class Available, class Read, class Report>
        void collect(Available available, Read read, Report report, bool wait = false)
        {
            while (!waiting.empty())
            {
                Window &window = waiting.front();
                // They are all TIMESTAMPs from the same context. OpenGL guarantees that the last one being available implies
                // the earlier ones are.
                if (!wait && !window.pending.empty() && !available(window.pending.back().end))
                    break;
                for (const Pending &item : window.pending)
                {
                    const uint64_t begin = read(item.begin);
                    const uint64_t end = read(item.end);
                    window.ns[item.kind] += end - begin;
                    ++window.count[item.kind];
                    if (item.kind == 0u)
                        window.rasterTimes.push_back({end - begin, item.info});
                    freeQueries.push_back(item.begin);
                    freeQueries.push_back(item.end);
                }
                window.pending.clear();
                report(window);
                waiting.pop_front();
            }
        }

    private:
        std::deque<Window> waiting;

        void seal()
        {
            waiting.emplace_back(std::move(static_cast<Window &>(*this)));
            static_cast<Window &>(*this) = Window{};
        }
    };
}
