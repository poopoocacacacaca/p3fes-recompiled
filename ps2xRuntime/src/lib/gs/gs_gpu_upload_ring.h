// persistent uploads protected by fences, with no OpenGL dependency.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

namespace GSGpuUpload
{
    enum WaitResult { Signaled, Timeout, Failed };
    enum class Failure { None, NullFence, Timeout, WaitFailed };

    template <class Fence, size_t ChunkBytes, size_t ChunkCount, size_t Alignment>
    class Ring
    {
        static_assert(ChunkBytes >= 16u && ChunkCount != 0u);
        static_assert(Alignment != 0u && (Alignment & (Alignment - 1u)) == 0u);
        static_assert(ChunkBytes % Alignment == 0u);
        static_assert(ChunkBytes <= (std::numeric_limits<size_t>::max)() / ChunkCount);

    public:
        void setStorage(uint8_t *mapped) { storage = mapped; }
        bool available() const { return storage != nullptr && !disabled(); }
        bool disabled() const { return failed != Failure::None; }
        Failure failure() const { return failed; }

        // The wait callback must represent the full bounded wait. Timeout/Failed block all later uploads, even in
        // another chunk.
        template <class CreateFence, class WaitFence, class DeleteFence>
        size_t put(const void *data, size_t payloadBytes, CreateFence createFence,
                   WaitFence waitFence, DeleteFence deleteFence)
        {
            if (!available() || payloadBytes > ChunkBytes)
                return SIZE_MAX;
            const size_t bytes = payloadBytes < 16u ? 16u : payloadBytes;
            const size_t padding = (Alignment - offset % Alignment) % Alignment;
            size_t aligned = offset + padding; // ChunkBytes is aligned too.
            if (bytes > ChunkBytes - aligned)
            {
                const Fence submitted = createFence();
                if (submitted == Fence{})
                {
                    failed = Failure::NullFence;
                    return SIZE_MAX;
                }
                fences[chunk] = submitted;
                const size_t next = (chunk + 1u) % ChunkCount;
                if (fences[next] != Fence{})
                {
                    const WaitResult result = waitFence(fences[next]);
                    if (result != WaitResult::Signaled)
                    {
                        failed = result == WaitResult::Timeout ? Failure::Timeout : Failure::WaitFailed;
                        // keep both fences for shutdown; do not copy or advance.
                        return SIZE_MAX;
                    }
                    const Fence complete = fences[next];
                    fences[next] = Fence{};
                    deleteFence(complete);
                }
                chunk = next;
                aligned = 0u;
            }
            const size_t result = chunk * ChunkBytes + aligned;
            if (data != nullptr)
            {
                std::memcpy(storage + result, data, payloadBytes);
                if (payloadBytes < bytes)
                    std::memset(storage + result + payloadBytes, 0, bytes - payloadBytes);
            }
            offset = aligned + bytes;
            return result;
        }

        // the owner calls this during shutdown, before tearing down the context.
        template <class DeleteFence>
        void releaseFences(DeleteFence deleteFence)
        {
            for (Fence &fence : fences)
                if (fence != Fence{})
                {
                    const Fence owned = fence;
                    fence = Fence{};
                    deleteFence(owned);
                }
        }

    private:
        uint8_t *storage = nullptr;
        std::array<Fence, ChunkCount> fences{};
        size_t chunk = 0u;
        size_t offset = 0u;
        Failure failed = Failure::None;
    };
}
