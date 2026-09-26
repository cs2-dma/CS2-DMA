#pragma once

#include <cstdint>
#include <string>

namespace app::updates
{
    enum class State : uint8_t { Idle, Current, Available, NoRelease, Unavailable, RateLimited, InvalidRelease };

    struct Status {
        State state = State::Idle;
        bool checking = false;
        bool updateAvailable = false;
        bool canCheck = false;
        std::string latestTag;
        std::string releaseUrl;
        std::string archiveName;
        std::string archiveUrl;
        uint64_t checkedAtMs = 0;
        uint64_t elapsedMs = 0;
        uint32_t httpStatus = 0;
        uint32_t systemError = 0;
    };

    void Start() noexcept;
    void Shutdown() noexcept;
    bool RequestCheck() noexcept;
    Status GetStatus();
    Status WaitForInitialCheck(uint32_t timeoutMs = 1200);
}
