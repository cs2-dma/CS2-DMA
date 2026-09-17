#pragma once

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

namespace esp::diagnostics {
    inline constexpr unsigned kNarrowDebugWeaponChain = 1u << 0;
    inline constexpr unsigned kNarrowDebugBones = 1u << 1;
    inline constexpr unsigned kNarrowDebugC4 = 1u << 2;
    inline constexpr unsigned kNarrowDebugWorld = 1u << 3;
    inline constexpr unsigned kNarrowDebugAll =
        kNarrowDebugWeaponChain |
        kNarrowDebugBones |
        kNarrowDebugC4 |
        kNarrowDebugWorld;

    inline unsigned ReadNarrowDebugMask()
    {
        static const unsigned mask = [] {
            const char* raw = std::getenv("KEVQDMA_DEBUG_UPDATE");
            if (!raw)
                return 0u;

            std::string value(raw);
            if (value.empty() || value.size() >= 256)
                return 0u;
            for (char& ch : value) {
                if (ch >= 'A' && ch <= 'Z')
                    ch = static_cast<char>(ch - 'A' + 'a');
            }

            unsigned result = 0u;
            if (value == "1" || value.find("all") != std::string::npos)
                result = kNarrowDebugAll;
            if (value.find("weapon") != std::string::npos)
                result |= kNarrowDebugWeaponChain;
            if (value.find("bone") != std::string::npos)
                result |= kNarrowDebugBones;
            if (value.find("c4") != std::string::npos ||
                value.find("bomb") != std::string::npos) {
                result |= kNarrowDebugC4;
            }
            if (value.find("world") != std::string::npos ||
                value.find("utility") != std::string::npos) {
                result |= kNarrowDebugWorld;
            }
            return result;
        }();
        return mask;
    }

    inline bool ShouldEmitUpdateIssue(
        const char* reason,
        bool runtimeHealthy,
        uint32_t& counter,
        uint32_t period = 25u) noexcept
    {
        const std::string_view reasonView = reason ? reason : "";
        if (reasonView.starts_with("optional_failed_") ||
            reasonView.find("using_cached_snapshot") != std::string_view::npos ||
            !runtimeHealthy) {
            return false;
        }

        ++counter;
        return counter == 1u || (period > 0u && (counter % period) == 0u);
    }

    class NarrowDebugOptions {
    public:
        NarrowDebugOptions() noexcept
            : mask_(ReadNarrowDebugMask())
        {
        }

        bool Enabled(unsigned flag) const noexcept
        {
            return (mask_ & flag) != 0u;
        }

        bool Tick(unsigned flag, uint32_t& counter, uint32_t period) const noexcept
        {
            if (!Enabled(flag))
                return false;
            ++counter;
            return counter == 1u || (period > 0u && (counter % period) == 0u);
        }

    private:
        unsigned mask_ = 0u;
    };
}
