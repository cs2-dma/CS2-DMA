#pragma once

#include "app/Bootstrap/version_update.h"
#include <functional>
#include <optional>
#include <string>

namespace bootstrap
{
    struct StartupUpdateHooks {
        std::function<void()> connectionOk;
        std::function<void()> connectionQuestion;
        std::function<void(const std::string&)> info;
        std::function<std::optional<std::string>()> readReply;
        std::function<bool(const std::string&)> openUrl;
        std::function<void()> exitCountdown;
    };

    enum class StartupUpdateResult { Continue, Exit };
    StartupUpdateResult HandleStartupUpdate(const app::updates::Status& status, const StartupUpdateHooks& hooks);
}
