#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace dma {
    struct ReadBatchQuality {
        uint64_t requestedBytes = 0;
        uint64_t completedBytes = 0;
        uint64_t requests = 0;
        uint64_t incompleteRequests = 0;
    };

    // Scatter_PrepareEx retains pointers until Clear/Close. Chunk storage keeps
    // counters stable during growth and reuses allocations across hot batches.
    template <typename ByteCount, size_t ChunkSize = 128, size_t MaxChunks = 256>
    class ScatterReadTracker {
    public:
        static_assert(sizeof(ByteCount) == 4);
        static_assert(ChunkSize > 0 && MaxChunks > 0);

        struct Request {
            ByteCount completed = 0;
            ByteCount requested = 0;
            ByteCount* callerOutput = nullptr;
            bool prepared = false;
        };

        Request* Add(ByteCount requested, ByteCount* callerOutput) noexcept
        {
            if (callerOutput)
                *callerOutput = 0;
            if (!requested || count_ == ChunkSize * MaxChunks)
                return nullptr;
            const size_t chunk = count_ / ChunkSize;
            if (!chunks_[chunk]) {
                chunks_[chunk].reset(new (std::nothrow) Request[ChunkSize]);
                if (!chunks_[chunk])
                    return nullptr;
            }
            Request* request = &chunks_[chunk][count_++ % ChunkSize];
            *request = {};
            request->requested = requested;
            request->callerOutput = callerOutput;
            return request;
        }

        ReadBatchQuality Complete(bool executed) noexcept
        {
            ReadBatchQuality result;
            for (size_t i = 0; i < count_; ++i) {
                Request& request = chunks_[i / ChunkSize][i % ChunkSize];
                const ByteCount completed = executed && request.prepared &&
                    request.completed <= request.requested ? request.completed : 0;
                if (request.callerOutput)
                    *request.callerOutput = completed;
                if (!request.prepared)
                    continue;
                ++result.requests;
                result.requestedBytes += request.requested;
                result.completedBytes += completed;
                result.incompleteRequests += completed != request.requested;
            }
            return result;
        }

        // Only after the upstream handle has been cleared/closed. Reset does
        // not dereference caller outputs, which may already be out of scope.
        void Reset() noexcept { count_ = 0; }

    private:
        std::array<std::unique_ptr<Request[]>, MaxChunks> chunks_{};
        size_t count_ = 0;
    };
}
