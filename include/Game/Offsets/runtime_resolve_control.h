#pragma once

#include <chrono>
#include <exception>
#include <functional>
#include <type_traits>
#include <utility>

namespace runtime_offsets
{
    class ResolveInterrupted final : public std::exception
    {
    public:
        explicit ResolveInterrupted(bool cancelled) noexcept : cancelled_(cancelled) {}
        bool Cancelled() const noexcept { return cancelled_; }
        const char* what() const noexcept override
        {
            return cancelled_ ? "Runtime offset resolution cancelled."
                : "Runtime offset resolution time budget exhausted.";
        }

    private:
        bool cancelled_;
    };

    struct ResolveControl
    {
        using Clock = std::chrono::steady_clock;
        Clock::time_point deadline = Clock::time_point::max();
        std::function<bool()> cancelled;

        void CheckAt(Clock::time_point now) const
        {
            if (cancelled && cancelled()) throw ResolveInterrupted(true);
            if (now >= deadline) throw ResolveInterrupted(false);
        }

        void Check() const { CheckAt(Clock::now()); }

        template <typename Operation>
        auto Run(Operation&& operation) const
        {
            Check();
            if constexpr (std::is_void_v<std::invoke_result_t<Operation>>) {
                std::invoke(std::forward<Operation>(operation));
                Check();
            } else {
                auto result = std::invoke(std::forward<Operation>(operation));
                Check();
                return result;
            }
        }
    };
}
