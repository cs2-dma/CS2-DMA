#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <shared_mutex>

namespace dma
{
    class ReadPriority
    {
    public:
        static uint64_t NowUs() noexcept
        {
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
        }

        class Foreground
        {
        public:
            explicit Foreground(ReadPriority& owner) : owner_(owner), startedUs_(NowUs())
            {
                owner_.foreground_.fetch_add(1, std::memory_order_seq_cst);
                try {
                    lock_ = std::shared_lock<std::shared_mutex>(owner_.gate_);
                } catch (...) {
                    owner_.foreground_.fetch_sub(1, std::memory_order_seq_cst);
                    throw;
                }
            }
            ~Foreground()
            {
                const uint64_t finishedUs = NowUs();
                if (finishedUs >= startedUs_ && finishedUs - startedUs_ >= 2000u) {
                    auto last = owner_.lastSlowUs_.load(std::memory_order_relaxed);
                    while (last < finishedUs && !owner_.lastSlowUs_.compare_exchange_weak(
                        last, finishedUs, std::memory_order_relaxed)) {}
                }
                lock_.unlock();
                owner_.foreground_.fetch_sub(1, std::memory_order_seq_cst);
            }
            Foreground(const Foreground&) = delete;
            Foreground& operator=(const Foreground&) = delete;

        private:
            ReadPriority& owner_;
            uint64_t startedUs_;
            std::shared_lock<std::shared_mutex> lock_;
        };

        std::unique_lock<std::shared_mutex> TryBackground()
        {
            std::unique_lock<std::shared_mutex> lock(gate_, std::defer_lock);
            if (foreground_.load(std::memory_order_seq_cst) == 0 && lock.try_lock() &&
                foreground_.load(std::memory_order_seq_cst) != 0) lock.unlock();
            return lock;
        }

        bool UnderPressure(uint64_t nowUs) const noexcept
        {
            const auto last = lastSlowUs_.load(std::memory_order_relaxed);
            return last != 0 && nowUs >= last && nowUs - last < 200000u;
        }

        uint32_t ForegroundCount() const noexcept
        {
            return foreground_.load(std::memory_order_seq_cst);
        }

    private:
        std::shared_mutex gate_;
        std::atomic<uint32_t> foreground_{0};
        std::atomic<uint64_t> lastSlowUs_{0};
    };
}
