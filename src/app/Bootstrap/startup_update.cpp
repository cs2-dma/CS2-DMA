#include "app/Bootstrap/startup_update.h"
#include "app/Bootstrap/release_policy.h"
#include "app/Core/build_info.h"
#include "app/Localization/localization.h"

bootstrap::StartupUpdateResult bootstrap::HandleStartupUpdate(
    const app::updates::Status& status, const StartupUpdateHooks& hooks)
{
    using namespace app::updates;
    const auto current = policy::ParseVersion(app::build_info::kVersionTag);
    const auto latest = policy::ParseVersion(status.latestTag);
    const bool verified = current && latest && !status.checking &&
        (status.state == State::Current || status.state == State::Available) &&
        policy::IsReleasePageUrl(status.releaseUrl, status.latestTag);
    if (verified && *latest <= *current) {
        hooks.connectionOk();
        return StartupUpdateResult::Continue;
    }
    if (!verified) {
        hooks.connectionOk();
        return StartupUpdateResult::Continue;
    }
    hooks.connectionQuestion();
    hooks.info(app::localization::Format("A new version ({}) is available. Update now? y/n (Enter = y)", status.latestTag));
    for (;;) {
        const auto reply = hooks.readReply();
        if (!reply) {
            hooks.info(KEVQ_TR("No interactive input. Update skipped."));
            hooks.connectionOk();
            return StartupUpdateResult::Continue;
        }
        const auto choice = policy::ParseUpdateReply(*reply);
        if (!choice) {
            hooks.info(KEVQ_TR("Please enter y or n (Enter = y)."));
            continue;
        }
        if (!*choice) {
            hooks.connectionOk();
            return StartupUpdateResult::Continue;
        }
        if (!hooks.openUrl(status.releaseUrl)) {
            hooks.info(KEVQ_TR("Could not open the release page. Continuing the current version."));
            hooks.info(status.releaseUrl);
            hooks.connectionOk();
            return StartupUpdateResult::Continue;
        }
        if (policy::IsReleaseArchiveUrl(status.archiveUrl, status.latestTag)) {
            if (hooks.openUrl(status.archiveUrl))
                hooks.info(KEVQ_TR("ZIP download requested in your browser. Extract the archive and run the new version."));
            else
                hooks.info(KEVQ_TR("Could not start the ZIP download. Download it from the opened release page."));
        } else {
            hooks.info(KEVQ_TR("No unique ZIP package found. Choose the download on the opened release page."));
        }
        hooks.exitCountdown();
        return StartupUpdateResult::Exit;
    }
}
