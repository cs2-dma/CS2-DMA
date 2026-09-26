#pragma once

#include "Features/Target/target_convars.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace target::convars::detail
{
    class SnapshotWorker
    {
    public:
        using Cancellation = std::function<bool()>;
        using Sampler = std::function<Values(const Cancellation&)>;
        using Resetter = std::function<void()>;
        using Clock = std::function<uint64_t()>;
        static constexpr uint64_t kRefreshIntervalUs = 250000;
        static constexpr uint64_t kMaximumSampleAgeUs = 1000000;

        SnapshotWorker(Sampler sample, Resetter reset, Clock clock)
            : sample_(std::move(sample)), reset_(std::move(reset)), clock_(std::move(clock)) {}

        ~SnapshotWorker() { Stop(); }

        void Start()
        {
            std::lock_guard lifecycle(lifecycleMutex_);
            std::lock_guard lock(mutex_);
            if (running_) return;
            stopping_.store(false);
            running_ = true;
            try {
                worker_ = std::thread([this] { Run(); });
            } catch (...) {
                running_ = false;
                stopping_.store(true);
            }
        }

        Values Read(uint64_t scene)
        {
            std::lock_guard lock(mutex_);
            if (!running_ || stopping_.load()) return {};
            const uint64_t now = clock_();
            lastDemandUs_.store(now);
            if (!haveScene_ || scene_ != scene) {
                haveScene_ = true;
                scene_ = scene;
                generation_.fetch_add(1);
                published_ = {};
                nextRefreshUs_ = 0;
                pending_ = true;
            }
            if (!inFlight_ && (nextRefreshUs_ == 0 || now >= nextRefreshUs_ || now < lastRefreshUs_))
                pending_ = true;
            if (pending_) condition_.notify_one();
            if (published_.updatedAtUs == 0 || now < published_.updatedAtUs ||
                now - published_.updatedAtUs > kMaximumSampleAgeUs)
                return {};
            return published_;
        }

        void Stop()
        {
            std::lock_guard lifecycle(lifecycleMutex_);
            {
                std::lock_guard lock(mutex_);
                stopping_.store(true);
                generation_.fetch_add(1);
                pending_ = false;
                published_ = {};
            }
            condition_.notify_all();
            if (worker_.joinable()) worker_.join();
            std::lock_guard lock(mutex_);
            running_ = false;
            inFlight_ = false;
            haveScene_ = false;
            lastRefreshUs_ = 0;
            nextRefreshUs_ = 0;
        }

    private:
        void Run()
        {
            uint64_t sampledGeneration = 0;
            for (;;) {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] { return stopping_.load() || pending_; });
                if (stopping_.load()) break;
                const uint64_t generation = generation_.load();
                pending_ = false;
                inFlight_ = true;
                lock.unlock();
                const Cancellation canceled = [this, generation] {
                    const uint64_t demand = lastDemandUs_.load();
                    const uint64_t now = clock_();
                    return stopping_.load() || generation_.load() != generation ||
                        now < demand || now - demand > kMaximumSampleAgeUs;
                };
                Values next;
                try {
                    if (sampledGeneration != generation) reset_();
                    sampledGeneration = generation;
                    if (!canceled()) next = sample_(canceled);
                } catch (...) {
                    sampledGeneration = 0;
                }
                lock.lock();
                inFlight_ = false;
                if (!canceled()) {
                    published_ = next;
                    lastRefreshUs_ = clock_();
                    nextRefreshUs_ = lastRefreshUs_ + kRefreshIntervalUs;
                }
            }
            reset_();
        }

        Sampler sample_;
        Resetter reset_;
        Clock clock_;
        std::mutex lifecycleMutex_;
        std::mutex mutex_;
        std::condition_variable condition_;
        std::thread worker_;
        std::atomic<bool> stopping_{true};
        std::atomic<uint64_t> generation_{0};
        std::atomic<uint64_t> lastDemandUs_{0};
        bool running_ = false;
        bool pending_ = false;
        bool inFlight_ = false;
        bool haveScene_ = false;
        uint64_t scene_ = 0;
        uint64_t lastRefreshUs_ = 0;
        uint64_t nextRefreshUs_ = 0;
        Values published_;
    };
}
