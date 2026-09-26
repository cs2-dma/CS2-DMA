#pragma once

#include "app/Core/app_state.h"
#include <cstddef>
#include <cstdint>

namespace target::settings_policy
{
    inline int AimMotionStyle(const app::state::TargetSettings& settings,
        const app::state::TargetWeaponProfileSettings& profile) noexcept
    {
        if (profile.aimMotionStyle >= 0 && profile.aimMotionStyle <= 2) return profile.aimMotionStyle;
        return profile.aimWindMouse ? 2 : settings.aimHumanization ? 1 : 0;
    }

    inline uint64_t MotionKey(const app::state::TargetSettings& settings,
        const app::state::TargetWeaponProfileSettings& profile, bool trigger) noexcept
    {
        uint64_t hash = 14695981039346656037ull;
        const auto add = [&](const auto& value) {
            const auto* bytes = reinterpret_cast<const unsigned char*>(&value);
            for (std::size_t i = 0; i < sizeof(value); ++i) hash = (hash ^ bytes[i]) * 1099511628211ull;
        };
        add(settings.fovPerWeapon);
        add(settings.fovPerWeapon ? profile.fovRadius : settings.fovRadius);
        if (trigger) {
            add(settings.triggerAimAssist); add(settings.triggerAimBone);
            add(settings.triggerAimPredictive); add(settings.triggerAimRecoilControl);
            add(settings.triggerAimHumanization); add(settings.triggerVisibleOnly);
            add(settings.triggerTargetLock); add(profile.triggerSmoothing);
            add(profile.triggerAdaptiveSmoothing);
            add(profile.triggerForceCenter);
        } else {
            add(settings.aimBone); add(settings.aimPredictive); add(settings.aimRecoilControl);
            add(AimMotionStyle(settings, profile)); add(settings.aimVisibleOnly); add(settings.aimTargetLock);
            add(profile.aimReactionMs);
            add(profile.aimSmoothing); add(profile.aimAdaptiveSmoothing);
            add(profile.aimSoftAssist); add(profile.aimAssistStrength);
            add(profile.aimAssistMaxSpeed); add(profile.aimAssistDeadzone);
            add(profile.aimRecoilStrength);
            add(profile.aimWindGravity); add(profile.aimWindFluctuation);
            add(profile.aimWindMaxStep); add(profile.aimWindDistance);
            add(profile.aimDamageCheck); add(profile.aimMinimumDamage); add(profile.aimAutowall);
        }
        return hash;
    }

}
