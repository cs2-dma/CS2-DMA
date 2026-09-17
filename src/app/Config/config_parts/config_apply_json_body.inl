        int configVersion = 0;
        if (root.contains("configVersion") && root["configVersion"].is_number_integer()) {
            configVersion = root["configVersion"].get<int>();
        }

        if (configVersion < 1) {
            g::espWeaponIconNoKnife = false;
            g::espBoxThickness = 2.0f;
            g::espSkeletonThickness = 1.6f;
            g::espOffscreenSize = 14.0f;
            g::radarDotSize = 4.0f;
            g::radarSize = 290.0f;
        }

        LoadBool(root, "ESP", "Enabled", g::espEnabled);
        LoadBool(root, "ESP", "Box", g::espBox);
        LoadBool(root, "ESP", "Health", g::espHealth);
        LoadBool(root, "ESP", "HealthText", g::espHealthText);
        LoadBool(root, "ESP", "Armor", g::espArmor);
        LoadBool(root, "ESP", "ArmorText", g::espArmorText);
        LoadBool(root, "ESP", "Name", g::espName);
        LoadFloat(root, "ESP", "NameFontSize", g::espNameFontSize);
        LoadBool(root, "ESP", "Weapon", g::espWeapon);
        LoadBool(root, "ESP", "WeaponText", g::espWeaponText);
        LoadFloat(root, "ESP", "WeaponTextSize", g::espWeaponTextSize);
        LoadColor(root, "ESP", "WeaponTextColor", g::espWeaponTextColor);
        LoadBool(root, "ESP", "WeaponIcon", g::espWeaponIcon);
        LoadBool(root, "ESP", "WeaponIconNoKnife", g::espWeaponIconNoKnife);
        LoadFloat(root, "ESP", "WeaponIconSize", g::espWeaponIconSize);
        LoadColor(root, "ESP", "WeaponIconColor", g::espWeaponIconColor);
        LoadBool(root, "ESP", "WeaponAmmo", g::espWeaponAmmo);
        LoadFloat(root, "ESP", "WeaponAmmoSize", g::espWeaponAmmoSize);
        LoadColor(root, "ESP", "WeaponAmmoColor", g::espWeaponAmmoColor);
        LoadBool(root, "ESP", "Distance", g::espDistance);
        LoadFloat(root, "ESP", "DistanceSize", g::espDistanceSize);
        LoadBool(root, "ESP", "Skeleton", g::espSkeleton);
        LoadBool(root, "ESP", "SkeletonDots", g::espSkeletonDots);
        LoadBool(root, "ESP", "Snaplines", g::espSnaplines);
        LoadBool(root, "ESP", "SnapFromTop", g::espSnaplineFromTop);
        LoadBool(root, "ESP", "VisibilityColoring", g::espVisibilityColoring);
        LoadBool(root, "ESP", "ShowTeammates", g::espShowTeammates);
        LoadBool(root, "ESP", "OffscreenArrows", g::espOffscreenArrows);
        LoadBool(root, "ESP", "Flags", g::espFlags);
        LoadBool(root, "ESP", "Item", g::espItem);
        LoadBool(root, "ESP", "ItemText", g::espItemText);
        LoadBool(root, "ESP", "ItemIcon", g::espItemIcon);
        LoadBool(root, "ESP", "ItemHighlight", g::espItemHighlight);
        LoadBool(root, "ESP", "FlagBlind", g::espFlagBlind);
        LoadColor(root, "ESP", "FlagBlindColor", g::espFlagBlindColor);
        LoadFloat(root, "ESP", "FlagBlindSize", g::espFlagBlindSize);
        LoadBool(root, "ESP", "FlagScoped", g::espFlagScoped);
        LoadColor(root, "ESP", "FlagScopedColor", g::espFlagScopedColor);
        LoadFloat(root, "ESP", "FlagScopedSize", g::espFlagScopedSize);
        LoadBool(root, "ESP", "FlagDefusing", g::espFlagDefusing);
        LoadColor(root, "ESP", "FlagDefusingColor", g::espFlagDefusingColor);
        LoadFloat(root, "ESP", "FlagDefusingSize", g::espFlagDefusingSize);
        LoadBool(root, "ESP", "FlagKit", g::espFlagKit);
        LoadColor(root, "ESP", "FlagKitColor", g::espFlagKitColor);
        LoadFloat(root, "ESP", "FlagKitSize", g::espFlagKitSize);
        LoadBool(root, "ESP", "FlagMoney", g::espFlagMoney);
        LoadColor(root, "ESP", "FlagMoneyColor", g::espFlagMoneyColor);
        LoadFloat(root, "ESP", "FlagMoneySize", g::espFlagMoneySize);
        LoadBool(root, "ESP", "World", g::espWorld);
        LoadBool(root, "ESP", "WorldProjectiles", g::espWorldProjectiles);
        LoadBool(root, "ESP", "WorldSmokeTimer", g::espWorldSmokeTimer);
        LoadBool(root, "ESP", "WorldInfernoTimer", g::espWorldInfernoTimer);
        LoadBool(root, "ESP", "WorldDecoyTimer", g::espWorldDecoyTimer);
        LoadBool(root, "ESP", "WorldExplosiveTimer", g::espWorldExplosiveTimer);
        LoadBool(root, "World", "GrenadeHelperEnabled", g::grenadeHelperEnabled);
        LoadBool(root, "World", "GrenadeHelperVisible", g::grenadeHelperVisible);
        LoadBool(root, "World", "FilterSmoke", g::grenadeHelperSmoke);
        LoadBool(root, "World", "FilterMolotov", g::grenadeHelperMolotov);
        LoadBool(root, "World", "FilterHE", g::grenadeHelperHe);
        LoadBool(root, "World", "FilterFlash", g::grenadeHelperFlash);
        LoadInt(root, "World", "ToggleKey", g::grenadeHelperToggleKey);
        LoadInt(root, "World", "AddKey", g::grenadeHelperAddKey);
        LoadInt(root, "World", "DeleteKey", g::grenadeHelperDeleteKey);
        LoadFloat(root, "World", "CircleRadius", g::grenadeHelperRadius);
        LoadFloat(root, "World", "CircleThickness", g::grenadeHelperThickness);
        LoadFloat(root, "World", "TextSize", g::grenadeHelperTextSize);
        LoadFloat(root, "World", "MaxDistance", g::grenadeHelperMaxDistance);
        LoadColor(root, "World", "CircleColor", g::grenadeHelperCircleColor);
        LoadColor(root, "World", "ActiveColor", g::grenadeHelperActiveColor);
        LoadColor(root, "World", "AimColor", g::grenadeHelperAimColor);
        LoadColor(root, "World", "TextColor", g::grenadeHelperTextColor);
        LoadBool(root, "ESP", "BombInfo", g::espBombInfo);
        LoadBool(root, "ESP", "BombText", g::espBombText);
        LoadBool(root, "ESP", "BombTime", g::espBombTime);
        LoadBool(root, "ESP", "BombTimerShowWithMenu", g::espBombTimerShowWithMenu);
        LoadFloat(root, "ESP", "BombTextSize", g::espBombTextSize);
        LoadFloat(root, "ESP", "BombTimerX", g::espBombTimerX);
        LoadFloat(root, "ESP", "BombTimerY", g::espBombTimerY);
        LoadDisabledItemIds(root, "ESP", "ItemHiddenIds", g::espItemEnabledMask);
        LoadFloat(root, "ESP", "OffscreenSize", g::espOffscreenSize);
        LoadFloat(root, "ESP", "BoxThickness", g::espBoxThickness);
        LoadInt(root, "ESP", "BoxStyle", g::espBoxStyle);
        LoadInt(root, "ESP", "BoxCornerPercent", g::espBoxCornerPercent);
        LoadFloat(root, "ESP", "SkeletonThickness", g::espSkeletonThickness);
        LoadColor(root, "ESP", "BoxColor", g::espBoxColor);
        LoadColor(root, "ESP", "HealthColor", g::espHealthColor);
        LoadColor(root, "ESP", "HealthLowColor", g::espHealthLowColor);
        LoadInt(root, "ESP", "HealthColorMode", g::espHealthColorMode);
        LoadColor(root, "ESP", "VisibleColor", g::espVisibleColor);
        LoadColor(root, "ESP", "HiddenColor", g::espHiddenColor);
        LoadColor(root, "ESP", "ArmorColor", g::espArmorColor);
        LoadColor(root, "ESP", "ArmorLowColor", g::espArmorLowColor);
        LoadInt(root, "ESP", "ArmorColorMode", g::espArmorColorMode);
        LoadColor(root, "ESP", "NameColor", g::espNameColor);
        LoadColor(root, "ESP", "DistanceColor", g::espDistanceColor);
        LoadColor(root, "ESP", "SkeletonColor", g::espSkeletonColor);
        LoadColor(root, "ESP", "SnaplineColor", g::espSnaplineColor);
        LoadColor(root, "ESP", "OffscreenColor", g::espOffscreenColor);
        LoadColor(root, "ESP", "FlagColor", g::espFlagColor);
        LoadColor(root, "ESP", "WorldColor", g::espWorldColor);
        LoadColor(root, "ESP", "ItemColor", g::espItemColor);
        LoadColor(root, "ESP", "BombColor", g::espBombColor);

        LoadBool(root, "Target", "Enabled", g::targetEnabled);
        LoadBool(root, "Target", "FovEnabled", g::targetFovEnabled);
        LoadBool(root, "Target", "FovPerWeapon", g::targetFovPerWeapon);
        LoadFloat(root, "Target", "FovRadius", g::targetFovRadius);
        LoadColor(root, "Target", "FovColor", g::targetFovColor);
        LoadBool(root, "Target", "AimbotEnabled", g::targetAimbotEnabled);
        LoadInt(root, "Target", "AimKey", g::targetAimKey);
        LoadInt(root, "Target", "AimActivationMode", g::targetAimActivationMode);
        LoadInt(root, "Target", "AimBone", g::targetAimBone);
        LoadFloat(root, "Target", "AimSmoothing", g::targetAimSmoothing);
        LoadBool(root, "Target", "AimVisibleOnly", g::targetAimVisibleOnly);
        LoadBool(root, "Target", "AimPredictive", g::targetAimPredictive);
        LoadBool(root, "Target", "AimRecoilControl", g::targetAimRecoilControl);
        LoadBool(root, "Target", "AimHumanization", g::targetAimHumanization);
        LoadBool(root, "Target", "TriggerbotEnabled", g::targetTriggerbotEnabled);
        LoadInt(root, "Target", "TriggerKey", g::targetTriggerKey);
        LoadInt(root, "Target", "TriggerActivationMode", g::targetTriggerActivationMode);
        LoadBool(root, "Target", "TriggerAimAssist", g::targetTriggerAimAssist);
        LoadInt(root, "Target", "TriggerAimBone", g::targetTriggerAimBone);
        LoadFloat(root, "Target", "TriggerAimSmoothing", g::targetTriggerAimSmoothing);
        LoadBool(root, "Target", "TriggerAimPredictive", g::targetTriggerAimPredictive);
        LoadBool(root, "Target", "TriggerAimRecoilControl", g::targetTriggerAimRecoilControl);
        LoadBool(root, "Target", "TriggerAimHumanization", g::targetTriggerAimHumanization);
        LoadInt(root, "Target", "TriggerDelayMs", g::targetTriggerDelayMs);
        LoadBool(root, "Target", "TriggerVisibleOnly", g::targetTriggerVisibleOnly);
        LoadBool(root, "Target", "TriggerAutoShot", g::targetTriggerAutoShot);
        if (const auto targetIt = root.find("Target");
            targetIt != root.end() && targetIt->is_object()) {
            const auto profilesIt = targetIt->find("WeaponProfiles");
            if (profilesIt != targetIt->end() && profilesIt->is_array()) {
                const size_t count = std::min(
                    profilesIt->size(),
                    g::targetWeaponProfiles.size());
                for (size_t index = 0; index < count; ++index) {
                    const auto& source = (*profilesIt)[index];
                    if (!source.is_object())
                        continue;
                    auto& profile = g::targetWeaponProfiles[index];
                    if (const auto value = source.find("FovRadius");
                        value != source.end() && value->is_number())
                        profile.fovRadius = value->get<float>();
                    if (const auto value = source.find("AimSmoothing");
                        value != source.end() && value->is_number())
                        profile.aimSmoothing = value->get<float>();
                    if (const auto value = source.find("AimMinimumDamage");
                        value != source.end() && value->is_number()) {
                        profile.aimMinimumDamage = value->get<float>();
                    } else if (const auto legacy = source.find("MinimumDamage");
                        legacy != source.end() && legacy->is_number()) {
                        profile.aimMinimumDamage = legacy->get<float>();
                    }
                    if (const auto value = source.find("AimAutowall");
                        value != source.end() && value->is_boolean()) {
                        profile.aimAutowall = value->get<bool>();
                    } else if (const auto legacy = source.find("Autowall");
                        legacy != source.end() && legacy->is_boolean()) {
                        profile.aimAutowall = legacy->get<bool>();
                    }
                    if (const auto value = source.find("TriggerSmoothing");
                        value != source.end() && value->is_number())
                        profile.triggerSmoothing = value->get<float>();
                    if (const auto value = source.find("TriggerHitchance");
                        value != source.end() && value->is_number()) {
                        profile.hitchance = value->get<float>();
                    } else if (const auto legacy = source.find("Hitchance");
                        legacy != source.end() && legacy->is_number()) {
                        profile.hitchance = legacy->get<float>();
                    }
                    if (const auto value = source.find("TriggerHitchanceEnabled");
                        value != source.end() && value->is_boolean())
                        profile.hitchanceEnabled = value->get<bool>();
                    if (const auto value = source.find("TriggerSeedWindowEnabled");
                        value != source.end() && value->is_boolean())
                        profile.seedWindowEnabled = value->get<bool>();
                    if (const auto value = source.find("TriggerMinimumDamage");
                        value != source.end() && value->is_number()) {
                        profile.minimumDamage = value->get<float>();
                    } else if (const auto legacy = source.find("MinimumDamage");
                        legacy != source.end() && legacy->is_number()) {
                        profile.minimumDamage = legacy->get<float>();
                    }
                    if (const auto value = source.find("TriggerAutowall");
                        value != source.end() && value->is_boolean()) {
                        profile.autowall = value->get<bool>();
                    } else if (const auto legacy = source.find("Autowall");
                        legacy != source.end() && legacy->is_boolean()) {
                        profile.autowall = legacy->get<bool>();
                    }
                }
            }
        }

        LoadBool(root, "Radar", "Enabled", g::radarEnabled);
        LoadInt(root, "Radar", "Mode", g::radarMode);
        LoadBool(root, "Radar", "ShowLocalDot", g::radarShowLocalDot);
        LoadBool(root, "Radar", "ShowAngles", g::radarShowAngles);
        LoadBool(root, "Radar", "ShowCrosshair", g::radarShowCrosshair);
        LoadBool(root, "Radar", "ShowBomb", g::radarShowBomb);
        LoadFloat(root, "Radar", "Size", g::radarSize);
        LoadFloat(root, "Radar", "DotSize", g::radarDotSize);
        LoadFloat(root, "Radar", "WorldRotationDeg", g::radarWorldRotationDeg);
        LoadFloat(root, "Radar", "WorldScale", g::radarWorldScale);
        LoadFloat(root, "Radar", "WorldOffsetX", g::radarWorldOffsetX);
        LoadFloat(root, "Radar", "WorldOffsetY", g::radarWorldOffsetY);
        LoadBool(root, "Radar", "StaticFlipX", g::radarStaticFlipX);
        LoadColor(root, "Radar", "BgColor", g::radarBgColor);
        LoadColor(root, "Radar", "BorderColor", g::radarBorderColor);
        LoadColor(root, "Radar", "DotColor", g::radarDotColor);
        LoadColor(root, "Radar", "BombColor", g::radarBombColor);
        LoadColor(root, "Radar", "AngleColor", g::radarAngleColor);
        LoadBool(root, "Radar", "SpectatorList", g::radarSpectatorList);
        LoadBool(root, "Radar", "SpectatorListShowWithMenu", g::radarSpectatorListShowWithMenu);
        LoadFloat(root, "Radar", "SpectatorListX", g::radarSpectatorListX);
        LoadFloat(root, "Radar", "SpectatorListY", g::radarSpectatorListY);

        LoadBool(root, "WEBRadar", "Enabled", g::webRadarEnabled);
        LoadInt(root, "WEBRadar", "Port", g::webRadarPort);
        LoadString(root, "WEBRadar", "MapOverride", g::webRadarMapOverride);
        LoadBool(root, "WEBRadar", "BindLan", g::webRadarBindLan);
        {
            int loadedIntervalMs = g::webRadarIntervalMs;
            LoadInt(root, "WEBRadar", "IntervalMs", loadedIntervalMs);
            g::webRadarIntervalMs = std::clamp(
                loadedIntervalMs,
                webradar::cfg::kMinRealtimeIntervalMs,
                webradar::cfg::kMaxRealtimeIntervalMs);
        }
        {
            const auto webRadarIt = root.find("WEBRadar");
            if (webRadarIt != root.end() && webRadarIt->is_object()) {
                const auto originIt = webRadarIt->find("OriginAllowlist");
                if (originIt != webRadarIt->end() && originIt->is_array()) {
                    g::webRadarOriginAllowlist.clear();
                    for (const auto& entry : *originIt) {
                        if (entry.is_string())
                            g::webRadarOriginAllowlist.push_back(entry.get<std::string>());
                    }
                }
            }
        }

        LoadBool(root, "Screen", "VSync", g::vsyncEnabled);
        LoadInt(root, "Screen", "FPSLimit", g::fpsLimit);

        LoadBool(root, "UI", "EspPreviewOpen", g::espPreviewOpen);
        LoadBool(root, "UI", "RadarCalibrationOpen", g::radarCalibrationOpen);
        LoadBool(root, "UI", "WebRadarQrOpen", g::webRadarQrOpen);
        LoadBool(root, "UI", "WebRadarDebugOpen", g::webRadarDebugOpen);
        LoadInt(root, "UI", "MenuToggleKey", g::menuToggleKey);
        LoadInt(root, "UI", "OverlayToggleKey", g::overlayToggleKey);
        LoadInt(root, "UI", "OverlayMonitorIndex", g::overlayMonitorIndex);
        LoadInt(root, "UI", "InputDeviceKind", g::inputDeviceKind);
        LoadString(root, "UI", "InputDeviceNetHost", g::inputDeviceNetHost);
        LoadInt(root, "UI", "InputDeviceNetPort", g::inputDeviceNetPort);
        LoadString(root, "UI", "InputDeviceNetKey", g::inputDeviceNetKey);
