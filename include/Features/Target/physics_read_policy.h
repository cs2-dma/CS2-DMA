#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace target::physics::read_policy
{
    inline constexpr size_t kMaximumChunkBytes = 8192;

    class BackgroundBudget
    {
    public:
        uint64_t DelayUs(uint64_t nowUs) const noexcept
        {
            return resumeAtUs_ > nowUs ? resumeAtUs_ - nowUs : 0;
        }

        void Account(uint64_t startedAtUs, uint64_t finishedAtUs, bool foregroundPressure = false) noexcept
        {
            if (finishedAtUs < startedAtUs) {
                activeUs_ = resumeAtUs_ = 0;
                return;
            }
            activeUs_ += (std::min)(finishedAtUs - startedAtUs, uint64_t{50000});
            if (activeUs_ >= 500) {
                const uint64_t cooldown = (std::max)(uint64_t{2000},
                    (std::min)(activeUs_ * (foregroundPressure ? 15u : 7u), uint64_t{100000}));
                resumeAtUs_ = finishedAtUs + cooldown;
                activeUs_ = 0;
            }
        }

    private:
        uint64_t activeUs_ = 0;
        uint64_t resumeAtUs_ = 0;
    };
}
