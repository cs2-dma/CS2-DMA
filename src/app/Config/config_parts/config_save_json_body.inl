        json root = json::object();
        root["configVersion"] = 1;

        json& esp = EnsureSection(root, "ESP");
        esp["Enabled"] = g::espEnabled;
        esp["Box"] = g::espBox;
        esp["Health"] = g::espHealth;
        esp["HealthText"] = g::espHealthText;
        esp["Armor"] = g::espArmor;
        esp["ArmorText"] = g::espArmorText;
        esp["Name"] = g::espName;
        esp["NameFontSize"] = g::espNameFontSize;
        esp["Weapon"] = g::espWeapon;
        esp["WeaponText"] = g::espWeaponText;
        esp["WeaponTextSize"] = g::espWeaponTextSize;
        SaveColor(esp, "WeaponTextColor", g::espWeaponTextColor);
        esp["WeaponIcon"] = g::espWeaponIcon;
        esp["WeaponIconNoKnife"] = g::espWeaponIconNoKnife;
        esp["WeaponIconSize"] = g::espWeaponIconSize;
        SaveColor(esp, "WeaponIconColor", g::espWeaponIconColor);
        esp["WeaponAmmo"] = g::espWeaponAmmo;
        esp["WeaponAmmoSize"] = g::espWeaponAmmoSize;
        SaveColor(esp, "WeaponAmmoColor", g::espWeaponAmmoColor);
        esp["Distance"] = g::espDistance;
        esp["DistanceSize"] = g::espDistanceSize;
        esp["Skeleton"] = g::espSkeleton;
        esp["SkeletonDots"] = g::espSkeletonDots;
        esp["Snaplines"] = g::espSnaplines;
        esp["SnapFromTop"] = g::espSnaplineFromTop;
        esp["VisibilityColoring"] = g::espVisibilityColoring;
        esp["ShowTeammates"] = g::espShowTeammates;
        esp["OffscreenArrows"] = g::espOffscreenArrows;
        esp["Flags"] = g::espFlags;
        esp["Item"] = g::espItem;
        esp["ItemText"] = g::espItemText;
        esp["ItemIcon"] = g::espItemIcon;
        esp["ItemHighlight"] = g::espItemHighlight;
        esp["FlagBlind"] = g::espFlagBlind;
        SaveColor(esp, "FlagBlindColor", g::espFlagBlindColor);
        esp["FlagBlindSize"] = g::espFlagBlindSize;
        esp["FlagScoped"] = g::espFlagScoped;
        SaveColor(esp, "FlagScopedColor", g::espFlagScopedColor);
        esp["FlagScopedSize"] = g::espFlagScopedSize;
        esp["FlagDefusing"] = g::espFlagDefusing;
        SaveColor(esp, "FlagDefusingColor", g::espFlagDefusingColor);
        esp["FlagDefusingSize"] = g::espFlagDefusingSize;
        esp["FlagKit"] = g::espFlagKit;
        SaveColor(esp, "FlagKitColor", g::espFlagKitColor);
        esp["FlagKitSize"] = g::espFlagKitSize;
        esp["FlagMoney"] = g::espFlagMoney;
        SaveColor(esp, "FlagMoneyColor", g::espFlagMoneyColor);
        esp["FlagMoneySize"] = g::espFlagMoneySize;
        esp["World"] = g::espWorld;
        esp["WorldProjectiles"] = g::espWorldProjectiles;
        esp["WorldSmokeTimer"] = g::espWorldSmokeTimer;
        esp["WorldInfernoTimer"] = g::espWorldInfernoTimer;
        esp["WorldDecoyTimer"] = g::espWorldDecoyTimer;
        esp["WorldExplosiveTimer"] = g::espWorldExplosiveTimer;
        json& world = EnsureSection(root, "World");
        world["GrenadeHelperEnabled"] = g::grenadeHelperEnabled;
        world["GrenadeHelperVisible"] = g::grenadeHelperVisible;
        world["FilterSmoke"] = g::grenadeHelperSmoke;
        world["FilterMolotov"] = g::grenadeHelperMolotov;
        world["FilterHE"] = g::grenadeHelperHe;
        world["FilterFlash"] = g::grenadeHelperFlash;
        world["ToggleKey"] = g::grenadeHelperToggleKey;
        world["AddKey"] = g::grenadeHelperAddKey;
        world["DeleteKey"] = g::grenadeHelperDeleteKey;
        world["CircleRadius"] = g::grenadeHelperRadius;
        world["CircleThickness"] = g::grenadeHelperThickness;
        world["TextSize"] = g::grenadeHelperTextSize;
        world["MaxDistance"] = g::grenadeHelperMaxDistance;
        SaveColor(world, "CircleColor", g::grenadeHelperCircleColor);
        SaveColor(world, "ActiveColor", g::grenadeHelperActiveColor);
        SaveColor(world, "AimColor", g::grenadeHelperAimColor);
        SaveColor(world, "TextColor", g::grenadeHelperTextColor);
        esp["BombInfo"] = g::espBombInfo;
        esp["BombText"] = g::espBombText;
        esp["BombTime"] = g::espBombTime;
        esp["BombTimerShowWithMenu"] = g::espBombTimerShowWithMenu;
        esp["BombTextSize"] = g::espBombTextSize;
        esp["BombTimerX"] = g::espBombTimerX;
        esp["BombTimerY"] = g::espBombTimerY;
        esp["OffscreenSize"] = g::espOffscreenSize;
        esp["BoxThickness"] = g::espBoxThickness;
        esp["BoxStyle"] = g::espBoxStyle;
        esp["BoxCornerPercent"] = g::espBoxCornerPercent;
        esp["SkeletonThickness"] = g::espSkeletonThickness;
        SaveColor(esp, "BoxColor", g::espBoxColor);
        SaveColor(esp, "HealthColor", g::espHealthColor);
        SaveColor(esp, "HealthLowColor", g::espHealthLowColor);
        esp["HealthColorMode"] = g::espHealthColorMode;
        SaveColor(esp, "VisibleColor", g::espVisibleColor);
        SaveColor(esp, "HiddenColor", g::espHiddenColor);
        SaveColor(esp, "ArmorColor", g::espArmorColor);
        SaveColor(esp, "ArmorLowColor", g::espArmorLowColor);
        esp["ArmorColorMode"] = g::espArmorColorMode;
        SaveColor(esp, "NameColor", g::espNameColor);
        SaveColor(esp, "DistanceColor", g::espDistanceColor);
        SaveColor(esp, "SkeletonColor", g::espSkeletonColor);
        SaveColor(esp, "SnaplineColor", g::espSnaplineColor);
        SaveColor(esp, "OffscreenColor", g::espOffscreenColor);
        SaveColor(esp, "FlagColor", g::espFlagColor);
        SaveColor(esp, "WorldColor", g::espWorldColor);
        SaveColor(esp, "ItemColor", g::espItemColor);
        SaveColor(esp, "BombColor", g::espBombColor);

        {
            json hiddenIds = json::array();
            for (size_t itemId = 1; itemId < 1200; ++itemId) {
                if (!g::espItemEnabledMask.test(itemId))
                    hiddenIds.push_back(static_cast<int>(itemId));
            }
            esp["ItemHiddenIds"] = std::move(hiddenIds);
        }

        json& target = EnsureSection(root, "Target");
        target["Enabled"] = g::targetEnabled;
        target["FovEnabled"] = g::targetFovEnabled;
        target["FovPerWeapon"] = g::targetFovPerWeapon;
        target["FovRadius"] = g::targetFovRadius;
        SaveColor(target, "FovColor", g::targetFovColor);
        target["AimbotEnabled"] = g::targetAimbotEnabled;
        target["AimKey"] = g::targetAimKey;
        target["AimActivationMode"] = g::targetAimActivationMode;
        target["AimBone"] = g::targetAimBone;
        target["AimSmoothing"] = g::targetAimSmoothing;
        target["AimVisibleOnly"] = g::targetAimVisibleOnly;
        target["AimPredictive"] = g::targetAimPredictive;
        target["AimRecoilControl"] = g::targetAimRecoilControl;
        target["AimHumanization"] = g::targetAimHumanization;
        target["TriggerbotEnabled"] = g::targetTriggerbotEnabled;
        target["TriggerKey"] = g::targetTriggerKey;
        target["TriggerActivationMode"] = g::targetTriggerActivationMode;
        target["TriggerAimAssist"] = g::targetTriggerAimAssist;
        target["TriggerAimBone"] = g::targetTriggerAimBone;
        target["TriggerAimSmoothing"] = g::targetTriggerAimSmoothing;
        target["TriggerAimPredictive"] = g::targetTriggerAimPredictive;
        target["TriggerAimRecoilControl"] = g::targetTriggerAimRecoilControl;
        target["TriggerAimHumanization"] = g::targetTriggerAimHumanization;
        target["TriggerDelayMs"] = g::targetTriggerDelayMs;
        target["TriggerVisibleOnly"] = g::targetTriggerVisibleOnly;
        target["TriggerAutoShot"] = g::targetTriggerAutoShot;
        target["WeaponProfiles"] = json::array();
        for (const auto& profile : g::targetWeaponProfiles) {
            target["WeaponProfiles"].push_back({
                {"FovRadius", profile.fovRadius},
                {"AimSmoothing", profile.aimSmoothing},
                {"AimMinimumDamage", profile.aimMinimumDamage},
                {"AimAutowall", profile.aimAutowall},
                {"TriggerSmoothing", profile.triggerSmoothing},
                {"TriggerHitchance", profile.hitchance},
                {"TriggerHitchanceEnabled", profile.hitchanceEnabled},
                {"TriggerSeedWindowEnabled", profile.seedWindowEnabled},
                {"TriggerMinimumDamage", profile.minimumDamage},
                {"TriggerAutowall", profile.autowall},
            });
        }

        json& radar = EnsureSection(root, "Radar");
        radar["Enabled"] = g::radarEnabled;
        radar["Mode"] = g::radarMode;
        radar["ShowLocalDot"] = g::radarShowLocalDot;
        radar["ShowAngles"] = g::radarShowAngles;
        radar["ShowCrosshair"] = g::radarShowCrosshair;
        radar["ShowBomb"] = g::radarShowBomb;
        radar["Size"] = g::radarSize;
        radar["DotSize"] = g::radarDotSize;
        radar["WorldRotationDeg"] = g::radarWorldRotationDeg;
        radar["WorldScale"] = g::radarWorldScale;
        radar["WorldOffsetX"] = g::radarWorldOffsetX;
        radar["WorldOffsetY"] = g::radarWorldOffsetY;
        radar["StaticFlipX"] = g::radarStaticFlipX;
        SaveColor(radar, "BgColor", g::radarBgColor);
        SaveColor(radar, "BorderColor", g::radarBorderColor);
        SaveColor(radar, "DotColor", g::radarDotColor);
        SaveColor(radar, "BombColor", g::radarBombColor);
        SaveColor(radar, "AngleColor", g::radarAngleColor);
        radar["SpectatorList"] = g::radarSpectatorList;
        radar["SpectatorListShowWithMenu"] = g::radarSpectatorListShowWithMenu;
        radar["SpectatorListX"] = g::radarSpectatorListX;
        radar["SpectatorListY"] = g::radarSpectatorListY;

        json& webRadar = EnsureSection(root, "WEBRadar");
        webRadar["Enabled"] = g::webRadarEnabled;
        webRadar["Port"] = g::webRadarPort;
        webRadar["MapOverride"] = g::webRadarMapOverride;
        webRadar["BindLan"] = g::webRadarBindLan;
        webRadar["IntervalMs"] = g::webRadarIntervalMs;
        {
            json originList = json::array();
            for (const auto& origin : g::webRadarOriginAllowlist)
                originList.push_back(origin);
            webRadar["OriginAllowlist"] = std::move(originList);
        }

        json& screen = EnsureSection(root, "Screen");
        screen["VSync"] = g::vsyncEnabled;
        screen["FPSLimit"] = g::fpsLimit;

        json& ui = EnsureSection(root, "UI");
        ui["EspPreviewOpen"] = g::espPreviewOpen;
        ui["RadarCalibrationOpen"] = g::radarCalibrationOpen;
        ui["WebRadarQrOpen"] = g::webRadarQrOpen;
        ui["WebRadarDebugOpen"] = g::webRadarDebugOpen;
        ui["MenuToggleKey"] = g::menuToggleKey;
        ui["OverlayToggleKey"] = g::overlayToggleKey;
        ui["OverlayMonitorIndex"] = g::overlayMonitorIndex;
        ui["InputDeviceKind"] = g::inputDeviceKind;
        ui["InputDeviceNetHost"] = g::inputDeviceNetHost;
        ui["InputDeviceNetPort"] = g::inputDeviceNetPort;
        ui["InputDeviceNetKey"] = g::inputDeviceNetKey;
