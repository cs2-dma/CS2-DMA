#pragma once

#include <atomic>
#include <array>
#include <bitset>
#include <cstdint>
#include <string>
#include <vector>

struct ImFont;

namespace app::state {
    inline bool IsKnifeItemId(uint16_t id)
    {
        switch (id) {
        case 41:
        case 42:
        case 59:
        case 500:
        case 503:
        case 505:
        case 506:
        case 507:
        case 508:
        case 509:
        case 512:
        case 514:
        case 515:
        case 516:
        case 517:
        case 518:
        case 519:
        case 520:
        case 521:
        case 522:
        case 523:
        case 525:
        case 526:
            return true;
        default:
            return false;
        }
    }

    inline std::bitset<1200> CreateDefaultItemEspMask()
    {
        std::bitset<1200> mask;
        mask.set();
        mask.reset(0);
        mask.reset(41);
        mask.reset(42);
        mask.reset(59);
        for (uint16_t id = 500; id <= 526; ++id)
            mask.reset(id);
        return mask;
    }



    struct DisplaySettings {
        int width = 1920;
        int height = 1080;
        bool vsyncEnabled = true;
        int fpsLimit = 0;
    };

    struct RuntimeState {
        std::atomic<uintptr_t> clientBase{ 0 };
        std::atomic<uintptr_t> engine2Base{ 0 };
        std::atomic<bool> running{ true };
        std::atomic<bool> menuOpen{ true };
    };

    struct EspSettings {
        bool enabled = true;
        bool box = true;
        bool health = true;
        bool healthText = true;
        bool armor = false;
        bool armorText = true;
        bool name = false;
        bool weapon = true;
        bool weaponText = false;
        float weaponTextSize = 0.0f;
        float weaponTextColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        bool weaponIcon = true;
        bool weaponIconNoKnife = false;
        float weaponIconSize = 10.0f;
        float weaponIconColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        bool weaponAmmo = false;
        float weaponAmmoSize = 0.0f;
        float weaponAmmoColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        bool distance = false;
        bool flagBlind = true;
        float flagBlindColor[4] = { 0.80f, 0.92f, 1.0f, 1.0f };
        float flagBlindSize = 0.0f;
        bool flagScoped = true;
        float flagScopedColor[4] = { 0.80f, 0.92f, 1.0f, 1.0f };
        float flagScopedSize = 0.0f;
        bool flagDefusing = true;
        float flagDefusingColor[4] = { 0.80f, 0.92f, 1.0f, 1.0f };
        float flagDefusingSize = 0.0f;
        bool flagKit = true;
        float flagKitColor[4] = { 0.80f, 0.92f, 1.0f, 1.0f };
        float flagKitSize = 0.0f;
        bool flagMoney = false;
        float flagMoneyColor[4] = { 0.80f, 0.92f, 1.0f, 1.0f };
        float flagMoneySize = 0.0f;
        float distanceSize = 0.0f;
        bool skeleton = true;
        bool skeletonDots = false;
        bool snaplines = false;
        bool snaplineFromTop = false;
        bool visibilityColoring = true;
        bool showTeammates = false;
        bool offscreenArrows = false;
        bool flags = true;
        bool item = false;
        bool itemText = true;
        bool itemIcon = true;
        bool itemHighlight = true;
        bool world = false;
        bool worldProjectiles = false;
        bool worldSmokeTimer = true;
        bool worldInfernoTimer = true;
        bool worldDecoyTimer = true;
        bool worldExplosiveTimer = true;
        bool grenadeHelperEnabled = false;
        bool grenadeHelperVisible = true;
        bool grenadeHelperSmoke = true;
        bool grenadeHelperMolotov = true;
        bool grenadeHelperHe = true;
        bool grenadeHelperFlash = true;
        int grenadeHelperToggleKey = 0x72;
        int grenadeHelperAddKey = 0x73;
        int grenadeHelperDeleteKey = 0x2E;
        float grenadeHelperRadius = 10.0f;
        float grenadeHelperThickness = 2.0f;
        float grenadeHelperTextSize = 12.0f;
        float grenadeHelperMaxDistance = 2500.0f;
        float grenadeHelperCircleColor[4] = { 0.30f, 0.55f, 1.00f, 0.75f };
        float grenadeHelperActiveColor[4] = { 0.20f, 1.00f, 0.45f, 0.95f };
        float grenadeHelperAimColor[4] = { 1.00f, 1.00f, 1.00f, 0.65f };
        float grenadeHelperTextColor[4] = { 1.00f, 1.00f, 1.00f, 0.95f };
        bool bombInfo = true;
        std::bitset<1200> itemEnabledMask = CreateDefaultItemEspMask();
        float itemColor[4] = { 0.35f, 0.65f, 1.0f, 1.0f };
        float boxColor[4] = { 1.0f, 0.20f, 0.20f, 1.0f };
        float healthColor[4] = { 0.25f, 0.95f, 0.35f, 1.0f };
        float healthLowColor[4] = { 1.0f, 0.12f, 0.08f, 1.0f };
        int healthColorMode = 0;
        float visibleColor[4] = { 1.0f, 0.20f, 0.20f, 1.0f };
        float hiddenColor[4] = { 0.65f, 0.65f, 0.65f, 1.0f };
        float armorColor[4] = { 0.35f, 0.65f, 1.0f, 1.0f };
        float armorLowColor[4] = { 0.18f, 0.30f, 0.58f, 1.0f };
        int armorColorMode = 1;
        float nameColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        float distanceColor[4] = { 0.80f, 0.92f, 1.0f, 1.0f };
        float skeletonColor[4] = { 0.0f, 1.0f, 0.5f, 1.0f };
        float snaplineColor[4] = { 1.0f, 1.0f, 0.0f, 0.80f };
        float offscreenColor[4] = { 1.0f, 0.30f, 0.20f, 1.0f };
        float flagColor[4] = { 0.80f, 0.92f, 1.0f, 1.0f };
        float worldColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        float bombColor[4] = { 1.0f, 0.65f, 0.20f, 1.0f };
        float bombTextSize = 0.0f;
        bool bombText = false;
        bool bombTime = false;
        bool bombTimerShowWithMenu = true;
        float bombTimerX = 780.0f;
        float bombTimerY = 86.0f;
        float offscreenSize = 14.0f;
        bool previewOpen = false;
        float nameFontSize = 16.0f;
        float boxThickness = 2.0f;
        int boxStyle = 0;
        int boxCornerPercent = 25;
        float skeletonThickness = 1.6f;
    };

    struct RadarSettings {
        bool enabled = true;
        int mode = 1;
        bool showLocalDot = false;
        bool showAngles = true;
        bool showCrosshair = false;
        bool showBomb = true;
        float size = 290.0f;
        float dotSize = 4.0f;
        float worldRotationDeg = 0.0f;
        float worldScale = 1.0f;
        float worldOffsetX = 0.0f;
        float worldOffsetY = 0.0f;
        bool staticFlipX = false;
        float bgColor[4] = { 0.0f, 0.0f, 0.0f, 0.75f };
        float borderColor[4] = { 0.0f, 0.0f, 0.0f, 0.35f };
        float dotColor[4] = { 1.0f, 0.13207549f, 0.13207549f, 1.0f };
        float bombColor[4] = { 1.0f, 0.74716979f, 0.0f, 1.0f };
        float angleColor[4] = { 1.0f, 1.0f, 1.0f, 0.95f };
        bool calibrationOpen = false;
        bool spectatorList = false;
        bool spectatorListShowWithMenu = true;
        float spectatorListX = 24.0f;
        float spectatorListY = 300.0f;
    };

    struct TargetWeaponProfileSettings {
        float fovRadius = 150.0f;
        float aimSmoothing = 5.0f;
        float aimMinimumDamage = 25.0f;
        bool aimAutowall = true;
        float triggerSmoothing = 4.0f;
        float hitchance = 80.0f;
        float minimumDamage = 25.0f;
        bool autowall = true;
        // Appended so legacy aggregate profiles keep their values and checks.
        bool hitchanceEnabled = true;
        bool seedWindowEnabled = true;
    };

    struct TargetSettings {
        bool enabled = false;
        bool fovEnabled = false;
        bool fovPerWeapon = false;
        float fovRadius = 150.0f;
        float fovColor[4] = { 0.3137255f, 0.6f, 1.0f, 0.9f };
        bool aimbotEnabled = false;
        int aimKey = 0x06;
        int aimActivationMode = 0;
        int aimBone = 0;
        float aimSmoothing = 5.0f;
        bool aimVisibleOnly = true;
        bool aimPredictive = true;
        bool aimRecoilControl = true;
        bool aimHumanization = true;
        bool triggerbotEnabled = false;
        int triggerKey = 0x06;
        int triggerActivationMode = 0;
        bool triggerAimAssist = true;
        int triggerAimBone = 0;
        float triggerAimSmoothing = 4.0f;
        bool triggerAimPredictive = false;
        bool triggerAimRecoilControl = true;
        bool triggerAimHumanization = false;
        int triggerDelayMs = 10;
        bool triggerVisibleOnly = true;
        bool triggerAutoShot = false;
        std::array<TargetWeaponProfileSettings, 6> weaponProfiles = {{
            {140.0f, 5.0f, 20.0f, false, 4.0f, 78.0f, 20.0f, false},
            {160.0f, 5.0f, 30.0f, true, 4.0f, 75.0f, 30.0f, true},
            {120.0f, 6.0f, 70.0f, true, 5.0f, 88.0f, 70.0f, true},
            {180.0f, 4.0f, 20.0f, true, 3.5f, 68.0f, 20.0f, true},
            {150.0f, 5.0f, 35.0f, true, 4.0f, 72.0f, 35.0f, true},
            {170.0f, 5.5f, 25.0f, true, 4.5f, 68.0f, 25.0f, true},
        }};
    };

    struct WebRadarSettings {
        bool enabled = true;
        int intervalMs = 33;
        int port = 22006;
        std::string mapOverride;
        bool qrOpen = true;
        bool debugOpen = false;
        bool bindLan = true;
        std::vector<std::string> originAllowlist;
    };

    struct WebRadarRemoteSettings {
        bool enabled = false;
        bool settingsOpen = false;
        std::string host;
        int webPort = 8080;
        int sshPort = 22;
        std::string login = "root";
        std::string password;
        std::string remotePath = "/opt/kevqdma-webradar";
    };

    struct UiSettings {
        int menuToggleKey = 'P';
        int overlayToggleKey = 0x71; 
        int overlayMonitorIndex = 0;
        int inputDeviceKind = 1;
        std::string inputDeviceNetHost;
        int inputDeviceNetPort = 0;
        std::string inputDeviceNetKey;
    };

    struct FontState {
        ImFont* fontDefault = nullptr;
        ImFont* fontUiSemibold = nullptr;
        ImFont* fontUiTitle = nullptr;
        ImFont* fontEspName = nullptr;
        ImFont* fontOverlayText = nullptr;
        ImFont* fontUiIcons = nullptr;
        ImFont* fontWeaponIcons = nullptr;
        ImFont* fontWeaponIconsSmall = nullptr;
        ImFont* fontWeaponIconsLarge = nullptr;
    };

    struct AppState {
        DisplaySettings display = {};
        RuntimeState runtime = {};
        EspSettings esp = {};
        TargetSettings target = {};
        RadarSettings radar = {};
        WebRadarSettings webRadar = {};
        WebRadarRemoteSettings webRadarRemote = {};
        UiSettings ui = {};
        FontState fonts = {};
    };

    inline AppState globalState = {};
}
