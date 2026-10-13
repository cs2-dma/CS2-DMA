#pragma once

#include <algorithm>
#include <cstdint>

namespace app::platform {
    class GraphicsRecovery {
    public:
        void Request() noexcept
        {
            if (!pending_) {
                pending_ = true;
                failures_ = 0;
                retryAtMs_ = 0;
            }
        }

        bool Pending() const noexcept { return pending_; }
        bool Ready(uint64_t nowMs) const noexcept { return pending_ && nowMs >= retryAtMs_; }

        void Failed(uint64_t nowMs) noexcept
        {
            pending_ = true;
            failures_ = std::min(failures_ + 1u, 6u);
            retryAtMs_ = nowMs + std::min(2000u, 100u << (failures_ - 1u));
        }

        void Recovered() noexcept
        {
            pending_ = false;
            failures_ = 0;
            retryAtMs_ = 0;
        }

    private:
        bool pending_ = false;
        unsigned failures_ = 0;
        uint64_t retryAtMs_ = 0;
    };
}
