#pragma once

#include "app/Bootstrap/version_update.h"
#include "app/Core/build_info.h"
#include "app/Localization/localization.h"
#include "app/UI/MenuShell/menu_utils.h"
#include <imgui.h>

namespace ui
{
    inline void RenderVersionNotice(const app::updates::Status& status, bool controls)
    {
        ImGui::PushID(controls ? "release_settings" : "release_banner");
        if (controls) {
            ImGui::TextDisabled("%s: %s", KEVQ_TR("Current version"), app::build_info::VersionTag().c_str());
            const char* message = "Release check not started";
            using State = app::updates::State;
            if (status.checking) message = "Checking releases...";
            else switch (status.state) {
            case State::Current: message = "No newer stable release"; break;
            case State::Available: message = "A new version is available"; break;
            case State::NoRelease: message = "No published stable release found"; break;
            case State::RateLimited: message = "GitHub rate limit; check will retry later"; break;
            case State::Unavailable: message = "Release check unavailable; application can continue"; break;
            case State::InvalidRelease: message = "Release version format is not supported"; break;
            default: break;
            }
            ImGui::TextWrapped("%s", app::localization::Get(message));
            if (!status.latestTag.empty())
                ImGui::TextDisabled("%s: %s", KEVQ_TR("Last verified release"), status.latestTag.c_str());
            if (status.httpStatus != 0 || status.systemError != 0)
                ImGui::TextDisabled("HTTP %u | OS %u | %llu ms", status.httpStatus, status.systemError,
                    static_cast<unsigned long long>(status.elapsedMs));
            ImGui::BeginDisabled(!status.canCheck);
            if (ImGui::Button(KEVQ_TR("Check releases"))) app::updates::RequestCheck();
            ImGui::EndDisabled();
            ImGui::TextWrapped("%s", KEVQ_TR("Checked in the background at startup and every 30 minutes. Manual checks have a 60-second cooldown."));
        } else if (status.updateAvailable) {
            ImGui::TextColored(ImVec4(0.95f, 0.77f, 0.35f, 1.0f), "%s: %s",
                KEVQ_TR("A new version is available"), status.latestTag.c_str());
        }
        if (status.updateAvailable && !status.releaseUrl.empty()) {
            if (ImGui::Button(KEVQ_TR("Open release page")))
                ui::menu_utils::OpenExternal(status.releaseUrl);
        }
        ImGui::PopID();
    }
}
