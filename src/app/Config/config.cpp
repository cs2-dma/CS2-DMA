#include "app/Config/config.h"
#include "app/Config/config_parse_utils.h"
#include "app/Config/profile_name_utils.h"
#include "app/Core/fallback_log.h"
#include "app/Core/globals.h"
#include "app/Config/project_paths.h"
#include "app/Config/user_state.h"
#include "app/Input/input_device_policy.h"
#include "app/Platform/file_replace.h"
#include "Features/Target/target_policy.h"
#include "Features/WebRadar/webradar.h"
#include "Features/WebRadar/web_remote.h"

#include <json/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <limits>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace {
    using json = nlohmann::json;
    using IniSection = std::unordered_map<std::string, std::string>;
    using IniDocument = std::unordered_map<std::string, IniSection>;

    std::string s_activeProfile = "KevqDefault";
    std::mutex s_profileStateMutex;
    std::string s_lastSavedJson;
    std::string s_lastQueuedJson;
    bool s_asyncSaveInitialized = false;
    std::mutex s_saveStateMutex;
    std::chrono::steady_clock::time_point s_nextSaveRetryAt = {};
    std::atomic<int64_t> s_nextDirtyCheckMs{0};

    std::string CopyActiveProfile()
    {
        std::lock_guard<std::mutex> lock(s_profileStateMutex);
        return s_activeProfile;
    }

    void SetActiveProfile(std::string profileName)
    {
        std::lock_guard<std::mutex> lock(s_profileStateMutex);
        s_activeProfile = std::move(profileName);
    }

    void InitializeSaveState(std::string serialized)
    {
        std::lock_guard<std::mutex> lock(s_saveStateMutex);
        s_lastSavedJson = serialized;
        s_lastQueuedJson = std::move(serialized);
        s_asyncSaveInitialized = true;
        s_nextSaveRetryAt = {};
    }

    void MarkSaveSucceeded(std::string serialized)
    {
        std::lock_guard<std::mutex> lock(s_saveStateMutex);
        s_lastSavedJson = std::move(serialized);
        s_asyncSaveInitialized = true;
        s_nextSaveRetryAt = {};
    }

    void MarkSaveFailed()
    {
        std::lock_guard<std::mutex> lock(s_saveStateMutex);
        s_lastQueuedJson.clear();
        s_nextSaveRetryAt = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    }

    using app::config_parse::ParseBoolString;
    using app::config_parse::ParseFloatString;
    using app::config_parse::ParseIntString;
    using app::config_parse::ToLower;
    using app::config_parse::Trim;
    using app::profile_name::IsReservedProfileName;
    using app::profile_name::IsUsableProfileName;
    using app::profile_name::SanitizeProfileName;

    using app::paths::GetConfigDirectory;
    using app::paths::GetLegacyProfilesDirectory;
    using app::paths::GetSettingsDirectory;

    std::filesystem::path BuildProfilePathObject(const std::string& profileName)
    {
        const std::string safeName = SanitizeProfileName(profileName);
        return GetConfigDirectory() / (safeName + ".json");
    }

    std::vector<std::filesystem::path> CollectLegacyProfilePathCandidates(const std::string& profileName)
    {
        const std::string safeName = SanitizeProfileName(profileName);
        return {
            GetConfigDirectory() / (safeName + ".ini"),
            GetLegacyProfilesDirectory() / (safeName + ".ini"),
        };
    }

    std::string BuildProfilePath(const std::string& profileName)
    {
        return BuildProfilePathObject(profileName).string();
    }

    std::array<float, 4> CopyColor(const float src[4])
    {
        return { src[0], src[1], src[2], src[3] };
    }

    void CopyColor(float dst[4], const std::array<float, 4>& src)
    {
        for (size_t i = 0; i < src.size(); ++i)
            dst[i] = src[i];
    }

    struct ScreenConfigSnapshot {
        bool vsyncEnabled = true;
        int fpsLimit = 0;
    };

    struct ConfigSnapshot {
        app::state::EspSettings esp = {};
        app::state::TargetSettings target = {};
        app::state::RadarSettings radar = {};
        app::state::WebRadarSettings webRadar = {};
        app::state::UiSettings ui = {};
        ScreenConfigSnapshot screen = {};
    };

    ConfigSnapshot CaptureCurrentConfig()
    {
        std::lock_guard<std::recursive_mutex> lock(g::settingsMutex);
        return ConfigSnapshot {
            g::espSettings,
            g::targetSettings,
            g::radarSettings,
            g::webRadarSettings,
            g::uiSettings,
            { g::vsyncEnabled, g::fpsLimit }
        };
    }

    void ApplyConfig(const ConfigSnapshot& snapshot)
    {
        std::lock_guard<std::recursive_mutex> lock(g::settingsMutex);
        g::espSettings = snapshot.esp;
        g::targetSettings = snapshot.target;
        g::radarSettings = snapshot.radar;
        g::webRadarSettings = snapshot.webRadar;
        g::uiSettings = snapshot.ui;
        g::webRadarIntervalMs = std::clamp(
            g::webRadarIntervalMs,
            webradar::cfg::kMinRealtimeIntervalMs,
            webradar::cfg::kMaxRealtimeIntervalMs);
        g::vsyncEnabled = snapshot.screen.vsyncEnabled;
        g::fpsLimit = snapshot.screen.fpsLimit;
    }

    const ConfigSnapshot& GetDefaultConfig()
    {
        static const ConfigSnapshot defaults = CaptureCurrentConfig();
        return defaults;
    }

    bool SaveJsonConfig(const std::string& jsonPath);
    bool LoadJsonConfig(const std::string& jsonPath);
    bool LoadFromLegacyPath(const std::filesystem::path& path);

    void DeleteLegacyProfileFiles(const std::string& profileName)
    {
        std::unordered_set<std::string> seen;
        for (const auto& path : CollectLegacyProfilePathCandidates(profileName)) {
            const std::string key = path.lexically_normal().generic_string();
            if (!seen.insert(key).second)
                continue;
            app::platform::RemoveFileIfExists(path);
        }
    }

    bool SaveToPath(const std::string& iniPath)
    {
        return SaveJsonConfig(iniPath);
    }

    bool LoadFromPath(const std::string& iniPath)
    {
        return LoadJsonConfig(iniPath);
    }

    void PersistActiveProfileName(const std::string& profileName)
    {
        app::user_state::SaveActiveProfile(SanitizeProfileName(profileName));
    }

    std::string LoadPersistedActiveProfileName()
    {
        std::string profileName;
        if (!app::user_state::LoadActiveProfile(&profileName))
            return {};

        const std::string name = SanitizeProfileName(profileName);
        return name.empty() ? std::string{} : name;
    }

    json& EnsureSection(json& root, std::string_view sectionName)
    {
        json& section = root[std::string(sectionName)];
        if (!section.is_object())
            section = json::object();
        return section;
    }

    const json* FindSection(const json& root, std::string_view sectionName)
    {
        const auto it = root.find(std::string(sectionName));
        if (it == root.end() || !it->is_object())
            return nullptr;
        return &(*it);
    }

    void SaveColor(json& section, std::string_view key, const float color[4])
    {
        section[std::string(key)] = json::array({ color[0], color[1], color[2], color[3] });
    }

    void LoadBool(const json& root, std::string_view sectionName, std::string_view key, bool& value)
    {
        const json* section = FindSection(root, sectionName);
        if (!section)
            return;

        const auto it = section->find(std::string(key));
        if (it == section->end())
            return;

        if (it->is_boolean())
            value = it->get<bool>();
        else if (it->is_number_integer())
            value = it->get<int>() != 0;
        else if (it->is_string())
            value = ParseBoolString(it->get_ref<const std::string&>(), value);
    }

    void LoadInt(const json& root, std::string_view sectionName, std::string_view key, int& value)
    {
        const json* section = FindSection(root, sectionName);
        if (!section)
            return;

        const auto it = section->find(std::string(key));
        if (it == section->end())
            return;

        if (it->is_number_unsigned()) {
            const uint64_t candidate = it->get<uint64_t>();
            if (candidate <= static_cast<uint64_t>(std::numeric_limits<int>::max()))
                value = static_cast<int>(candidate);
        }
        else if (it->is_number_integer()) {
            const int64_t candidate = it->get<int64_t>();
            if (candidate >= static_cast<int64_t>(std::numeric_limits<int>::min()) &&
                candidate <= static_cast<int64_t>(std::numeric_limits<int>::max())) {
                value = static_cast<int>(candidate);
            }
        }
        else if (it->is_boolean())
            value = it->get<bool>() ? 1 : 0;
        else if (it->is_string())
            value = ParseIntString(it->get_ref<const std::string&>(), value);
    }

    void LoadFloat(const json& root, std::string_view sectionName, std::string_view key, float& value)
    {
        const json* section = FindSection(root, sectionName);
        if (!section)
            return;

        const auto it = section->find(std::string(key));
        if (it == section->end())
            return;

        if (it->is_number()) {
            const double candidate = it->get<double>();
            if (std::isfinite(candidate) &&
                std::fabs(candidate) <= static_cast<double>(std::numeric_limits<float>::max())) {
                value = static_cast<float>(candidate);
            }
        }
        else if (it->is_string())
            value = ParseFloatString(it->get_ref<const std::string&>(), value);
    }

    void LoadString(const json& root, std::string_view sectionName, std::string_view key, std::string& value)
    {
        const json* section = FindSection(root, sectionName);
        if (!section)
            return;

        const auto it = section->find(std::string(key));
        if (it != section->end() && it->is_string())
            value = it->get<std::string>();
    }

    void LoadColor(const json& root, std::string_view sectionName, std::string_view key, float color[4])
    {
        const json* section = FindSection(root, sectionName);
        if (!section)
            return;

        const auto it = section->find(std::string(key));
        if (it == section->end() || !it->is_array() || it->size() != 4)
            return;

        std::array<float, 4> parsed = CopyColor(color);
        for (size_t i = 0; i < parsed.size(); ++i) {
            if (!(*it)[i].is_number())
                return;
            const double candidate = (*it)[i].get<double>();
            if (!std::isfinite(candidate))
                return;
            parsed[i] = static_cast<float>(std::clamp(candidate, 0.0, 1.0));
        }
        CopyColor(color, parsed);
    }

    void LoadDisabledItemIds(const json& root, std::string_view sectionName, std::string_view key, std::bitset<1200>& mask)
    {
        const json* section = FindSection(root, sectionName);
        if (!section)
            return;

        const auto it = section->find(std::string(key));
        if (it == section->end() || !it->is_array())
            return;

        mask = app::state::CreateDefaultItemEspMask();
        for (const auto& value : *it) {
            if (!value.is_number_unsigned() && !value.is_number_integer())
                continue;
            uint64_t itemId = 0;
            if (value.is_number_unsigned()) {
                itemId = value.get<uint64_t>();
            } else {
                const int64_t signedItemId = value.get<int64_t>();
                if (signedItemId <= 0)
                    continue;
                itemId = static_cast<uint64_t>(signedItemId);
            }
            if (itemId > 0 && itemId < mask.size())
                mask.reset(static_cast<size_t>(itemId));
        }
    }

    const std::string* FindIniValue(const IniDocument& ini, std::string_view sectionName, std::string_view key)
    {
        const auto secIt = ini.find(std::string(sectionName));
        if (secIt == ini.end())
            return nullptr;

        const auto keyIt = secIt->second.find(std::string(key));
        if (keyIt == secIt->second.end())
            return nullptr;

        return &keyIt->second;
    }

    void LoadBool(const IniDocument& ini, std::string_view sectionName, std::string_view key, bool& value)
    {
        const std::string* raw = FindIniValue(ini, sectionName, key);
        if (raw != nullptr)
            value = ParseBoolString(*raw, value);
    }

    void LoadInt(const IniDocument& ini, std::string_view sectionName, std::string_view key, int& value)
    {
        const std::string* raw = FindIniValue(ini, sectionName, key);
        if (raw != nullptr)
            value = ParseIntString(*raw, value);
    }

    void LoadFloat(const IniDocument& ini, std::string_view sectionName, std::string_view key, float& value)
    {
        const std::string* raw = FindIniValue(ini, sectionName, key);
        if (raw != nullptr)
            value = ParseFloatString(*raw, value);
    }

    void LoadString(const IniDocument& ini, std::string_view sectionName, std::string_view key, std::string& value)
    {
        const std::string* raw = FindIniValue(ini, sectionName, key);
        if (raw != nullptr)
            value = *raw;
    }

    void LoadColor(const IniDocument& ini, std::string_view sectionName, std::string_view key, float color[4])
    {
        const std::string* raw = FindIniValue(ini, sectionName, key);
        if (raw == nullptr)
            return;

        const std::string_view value = *raw;
        std::array<float, 4> parsed = CopyColor(color);
        size_t start = 0;
        for (size_t i = 0; i < parsed.size(); ++i) {
            const size_t comma = value.find(',', start);
            if ((i + 1u < parsed.size() && comma == std::string_view::npos) ||
                (i + 1u == parsed.size() && comma != std::string_view::npos)) {
                return;
            }
            const size_t len =
                comma == std::string_view::npos ? value.size() - start : comma - start;
            const float candidate = ParseFloatString(
                value.substr(start, len),
                std::numeric_limits<float>::quiet_NaN());
            if (!std::isfinite(candidate))
                return;
            parsed[i] = std::clamp(candidate, 0.0f, 1.0f);
            start = comma == std::string_view::npos ? value.size() : comma + 1u;
        }
        CopyColor(color, parsed);
    }

    void LoadDisabledItemIds(const IniDocument& ini, std::string_view sectionName, std::string_view key, std::bitset<1200>& mask)
    {
        const std::string* raw = FindIniValue(ini, sectionName, key);
        if (raw == nullptr)
            return;

        mask = app::state::CreateDefaultItemEspMask();
        const std::string_view value = *raw;
        size_t start = 0;
        while (start < value.size()) {
            const size_t comma = value.find(',', start);
            const size_t len = (comma == std::string_view::npos) ? (value.size() - start) : (comma - start);
            const int itemId = ParseIntString(value.substr(start, len), -1);
            if (itemId > 0 && itemId < 1200)
                mask.reset(static_cast<size_t>(itemId));
            if (comma == std::string_view::npos)
                break;
            start = comma + 1;
        }
    }

    void ValidateLoadedValues()
    {
        constexpr int kVkEnd = 0x23;
        constexpr int kVkInsert = 0x2D;
        const ConfigSnapshot& defaults = GetDefaultConfig();

        const auto restoreOutsideRange = [](
            float& value,
            float minimum,
            float maximum,
            float fallback) {
            if (!std::isfinite(value) || value < minimum || value > maximum)
                value = fallback;
        };

        restoreOutsideRange(g::espNameFontSize, 0.0f, 24.0f, defaults.esp.nameFontSize);
        restoreOutsideRange(g::espWeaponTextSize, 0.0f, 24.0f, defaults.esp.weaponTextSize);
        restoreOutsideRange(g::espWeaponIconSize, 10.0f, 30.0f, defaults.esp.weaponIconSize);
        restoreOutsideRange(g::espWeaponAmmoSize, 0.0f, 24.0f, defaults.esp.weaponAmmoSize);
        restoreOutsideRange(g::espDistanceSize, 0.0f, 24.0f, defaults.esp.distanceSize);
        restoreOutsideRange(g::espFlagBlindSize, 0.0f, 24.0f, defaults.esp.flagBlindSize);
        restoreOutsideRange(g::espFlagScopedSize, 0.0f, 24.0f, defaults.esp.flagScopedSize);
        restoreOutsideRange(g::espFlagDefusingSize, 0.0f, 24.0f, defaults.esp.flagDefusingSize);
        restoreOutsideRange(g::espFlagKitSize, 0.0f, 24.0f, defaults.esp.flagKitSize);
        restoreOutsideRange(g::espFlagMoneySize, 0.0f, 24.0f, defaults.esp.flagMoneySize);
        restoreOutsideRange(g::espBombTextSize, 0.0f, 24.0f, defaults.esp.bombTextSize);
        restoreOutsideRange(g::espBoxThickness, 0.5f, 4.0f, defaults.esp.boxThickness);
        if (g::espBoxStyle < 0 || g::espBoxStyle > 2)
            g::espBoxStyle = defaults.esp.boxStyle;
        if (g::espBoxCornerPercent < 10 || g::espBoxCornerPercent > 45)
            g::espBoxCornerPercent = defaults.esp.boxCornerPercent;
        if (g::espHealthColorMode < 0 || g::espHealthColorMode > 2)
            g::espHealthColorMode = defaults.esp.healthColorMode;
        if (g::espArmorColorMode < 0 || g::espArmorColorMode > 2)
            g::espArmorColorMode = defaults.esp.armorColorMode;
        restoreOutsideRange(
            g::espSkeletonThickness,
            0.5f,
            4.0f,
            defaults.esp.skeletonThickness);
        g::targetFovRadius = target::policy::SanitizeFovRadius(
            g::targetFovRadius,
            defaults.target.fovRadius);
        g::targetAimSmoothing = target::policy::SanitizeSmoothing(
            g::targetAimSmoothing,
            defaults.target.aimSmoothing);
        g::targetAimBone = target::policy::SanitizeAimBone(
            g::targetAimBone,
            defaults.target.aimBone);
        g::targetTriggerAimSmoothing = target::policy::SanitizeSmoothing(
            g::targetTriggerAimSmoothing,
            defaults.target.triggerAimSmoothing);
        g::targetTriggerAimBone = target::policy::SanitizeAimBone(
            g::targetTriggerAimBone,
            defaults.target.triggerAimBone);
        g::targetTriggerDelayMs = target::policy::SanitizeDelayMs(
            g::targetTriggerDelayMs,
            defaults.target.triggerDelayMs);
        for (size_t index = 0; index < g::targetWeaponProfiles.size(); ++index) {
            auto& profile = g::targetWeaponProfiles[index];
            const auto& fallback = defaults.target.weaponProfiles[index];
            profile.fovRadius = target::policy::SanitizeFovRadius(
                profile.fovRadius,
                fallback.fovRadius);
            profile.aimSmoothing = target::policy::SanitizeSmoothing(
                profile.aimSmoothing,
                fallback.aimSmoothing);
            restoreOutsideRange(
                profile.aimMinimumDamage,
                1.0f,
                200.0f,
                fallback.aimMinimumDamage);
            profile.triggerSmoothing = target::policy::SanitizeSmoothing(
                profile.triggerSmoothing,
                fallback.triggerSmoothing);
            restoreOutsideRange(
                profile.hitchance,
                1.0f,
                100.0f,
                fallback.hitchance);
            restoreOutsideRange(
                profile.minimumDamage,
                1.0f,
                200.0f,
                fallback.minimumDamage);
        }
        g::targetAimActivationMode = target::policy::SanitizeActivationMode(
            g::targetAimActivationMode,
            defaults.target.aimActivationMode);
        g::targetTriggerActivationMode = target::policy::SanitizeActivationMode(
            g::targetTriggerActivationMode,
            defaults.target.triggerActivationMode);
        const auto validateTargetKey = [](int& key, int fallback) {
            if (key < 1 || key > 0xFE)
                key = fallback;
        };
        validateTargetKey(g::targetAimKey, defaults.target.aimKey);
        validateTargetKey(g::targetTriggerKey, defaults.target.triggerKey);
        restoreOutsideRange(g::grenadeHelperRadius, 4.0f, 64.0f, defaults.esp.grenadeHelperRadius);
        restoreOutsideRange(g::grenadeHelperThickness, 0.5f, 5.0f, defaults.esp.grenadeHelperThickness);
        restoreOutsideRange(g::grenadeHelperTextSize, 10.0f, 24.0f, defaults.esp.grenadeHelperTextSize);
        restoreOutsideRange(g::grenadeHelperMaxDistance, 500.0f, 6000.0f, defaults.esp.grenadeHelperMaxDistance);
        const auto validateGrenadeKey = [](int& key, int fallback) {
            if (key < 0x08 || key > 0xFE)
                key = fallback;
        };
        validateGrenadeKey(g::grenadeHelperToggleKey, defaults.esp.grenadeHelperToggleKey);
        validateGrenadeKey(g::grenadeHelperAddKey, defaults.esp.grenadeHelperAddKey);
        validateGrenadeKey(g::grenadeHelperDeleteKey, defaults.esp.grenadeHelperDeleteKey);

        if (g::radarMode < 0 || g::radarMode > 1)
            g::radarMode = defaults.radar.mode;
        restoreOutsideRange(g::radarSize, 100.0f, 400.0f, defaults.radar.size);
        restoreOutsideRange(g::radarDotSize, 2.0f, 8.0f, defaults.radar.dotSize);
        restoreOutsideRange(g::espOffscreenSize, 6.0f, 36.0f, defaults.esp.offscreenSize);
        restoreOutsideRange(
            g::radarWorldRotationDeg,
            -180.0f,
            180.0f,
            defaults.radar.worldRotationDeg);
        restoreOutsideRange(g::radarWorldScale, 0.50f, 1.50f, defaults.radar.worldScale);
        restoreOutsideRange(g::radarWorldOffsetX, -0.25f, 0.25f, defaults.radar.worldOffsetX);
        restoreOutsideRange(g::radarWorldOffsetY, -0.25f, 0.25f, defaults.radar.worldOffsetY);
        if (g::webRadarPort < 1025 || g::webRadarPort > 65535)
            g::webRadarPort = static_cast<int>(webradar::cfg::kDefaultListenPort);
        if (g::fpsLimit < 0)
            g::fpsLimit = 0;
        if (g::fpsLimit > 500)
            g::fpsLimit = 500;
        if (g::overlayMonitorIndex < 0)
            g::overlayMonitorIndex = defaults.ui.overlayMonitorIndex;
        if (!app::input::IsSelectableDeviceKind(g::inputDeviceKind))
            g::inputDeviceKind = defaults.ui.inputDeviceKind;
        if (g::inputDeviceNetPort < 0 || g::inputDeviceNetPort > 65535)
            g::inputDeviceNetPort = defaults.ui.inputDeviceNetPort;
        if (g::inputDeviceNetHost.size() > 64)
            g::inputDeviceNetHost.resize(64);
        if (g::inputDeviceNetKey.size() > 32)
            g::inputDeviceNetKey.resize(32);
        auto sanitizeOverlayPos = [](float& x, float& y, float defaultX, float defaultY) {
            constexpr float kMaxCoord = 12000.0f;
            if (!std::isfinite(x) || std::fabs(x) > kMaxCoord)
                x = defaultX;
            if (!std::isfinite(y) || std::fabs(y) > kMaxCoord)
                y = defaultY;
        };
        sanitizeOverlayPos(g::radarSpectatorListX, g::radarSpectatorListY, defaults.radar.spectatorListX, defaults.radar.spectatorListY);
        sanitizeOverlayPos(g::espBombTimerX, g::espBombTimerY, defaults.esp.bombTimerX, defaults.esp.bombTimerY);
        if (g::menuToggleKey < 0x08 || g::menuToggleKey > 0xFE || g::menuToggleKey == kVkEnd || g::menuToggleKey == kVkInsert)
            g::menuToggleKey = defaults.ui.menuToggleKey;
        if (g::overlayToggleKey < 0x08 || g::overlayToggleKey > 0xFE || g::overlayToggleKey == kVkEnd || g::overlayToggleKey == kVkInsert)
            g::overlayToggleKey = defaults.ui.overlayToggleKey;
        g::espItemEnabledMask.set(0, false);
        for (uint16_t id = 1; id < 1200; ++id) {
            if (app::state::IsKnifeItemId(id))
                g::espItemEnabledMask.set(id, false);
        }
        g::webRadarIntervalMs = std::clamp(
            g::webRadarIntervalMs,
            webradar::cfg::kMinRealtimeIntervalMs,
            webradar::cfg::kMaxRealtimeIntervalMs);
    }

    void ApplyLoadedConfig(const json& root)
    {
        #include "config_parts/config_apply_json_body.inl"
    }

    void ApplyLoadedConfig(const IniDocument& ini)
    {
        LoadBool(ini, "ESP", "Enabled", g::espEnabled);
        LoadBool(ini, "ESP", "Box", g::espBox);
        LoadBool(ini, "ESP", "Health", g::espHealth);
        LoadBool(ini, "ESP", "HealthText", g::espHealthText);
        LoadBool(ini, "ESP", "Armor", g::espArmor);
        LoadBool(ini, "ESP", "ArmorText", g::espArmorText);
        LoadBool(ini, "ESP", "Name", g::espName);
        LoadFloat(ini, "ESP", "NameFontSize", g::espNameFontSize);
        LoadBool(ini, "ESP", "Weapon", g::espWeapon);
        LoadBool(ini, "ESP", "WeaponText", g::espWeaponText);
        LoadFloat(ini, "ESP", "WeaponTextSize", g::espWeaponTextSize);
        LoadColor(ini, "ESP", "WeaponTextColor", g::espWeaponTextColor);
        LoadBool(ini, "ESP", "WeaponIcon", g::espWeaponIcon);
        LoadBool(ini, "ESP", "WeaponIconNoKnife", g::espWeaponIconNoKnife);
        LoadFloat(ini, "ESP", "WeaponIconSize", g::espWeaponIconSize);
        LoadColor(ini, "ESP", "WeaponIconColor", g::espWeaponIconColor);
        LoadBool(ini, "ESP", "WeaponAmmo", g::espWeaponAmmo);
        LoadFloat(ini, "ESP", "WeaponAmmoSize", g::espWeaponAmmoSize);
        LoadColor(ini, "ESP", "WeaponAmmoColor", g::espWeaponAmmoColor);
        LoadBool(ini, "ESP", "Distance", g::espDistance);
        LoadFloat(ini, "ESP", "DistanceSize", g::espDistanceSize);
        LoadBool(ini, "ESP", "Skeleton", g::espSkeleton);
        LoadBool(ini, "ESP", "SkeletonDots", g::espSkeletonDots);
        LoadBool(ini, "ESP", "Snaplines", g::espSnaplines);
        LoadBool(ini, "ESP", "SnapFromTop", g::espSnaplineFromTop);
        LoadBool(ini, "ESP", "VisibilityColoring", g::espVisibilityColoring);
        LoadBool(ini, "ESP", "ShowTeammates", g::espShowTeammates);
        LoadBool(ini, "ESP", "OffscreenArrows", g::espOffscreenArrows);
        LoadBool(ini, "ESP", "Flags", g::espFlags);
        LoadBool(ini, "ESP", "Item", g::espItem);
        LoadBool(ini, "ESP", "ItemText", g::espItemText);
        LoadBool(ini, "ESP", "ItemIcon", g::espItemIcon);
        LoadBool(ini, "ESP", "ItemHighlight", g::espItemHighlight);
        LoadBool(ini, "ESP", "FlagBlind", g::espFlagBlind);
        LoadColor(ini, "ESP", "FlagBlindColor", g::espFlagBlindColor);
        LoadFloat(ini, "ESP", "FlagBlindSize", g::espFlagBlindSize);
        LoadBool(ini, "ESP", "FlagScoped", g::espFlagScoped);
        LoadColor(ini, "ESP", "FlagScopedColor", g::espFlagScopedColor);
        LoadFloat(ini, "ESP", "FlagScopedSize", g::espFlagScopedSize);
        LoadBool(ini, "ESP", "FlagDefusing", g::espFlagDefusing);
        LoadColor(ini, "ESP", "FlagDefusingColor", g::espFlagDefusingColor);
        LoadFloat(ini, "ESP", "FlagDefusingSize", g::espFlagDefusingSize);
        LoadBool(ini, "ESP", "FlagKit", g::espFlagKit);
        LoadColor(ini, "ESP", "FlagKitColor", g::espFlagKitColor);
        LoadFloat(ini, "ESP", "FlagKitSize", g::espFlagKitSize);
        LoadBool(ini, "ESP", "FlagMoney", g::espFlagMoney);
        LoadColor(ini, "ESP", "FlagMoneyColor", g::espFlagMoneyColor);
        LoadFloat(ini, "ESP", "FlagMoneySize", g::espFlagMoneySize);
        LoadBool(ini, "ESP", "World", g::espWorld);
        LoadBool(ini, "ESP", "WorldProjectiles", g::espWorldProjectiles);
        LoadBool(ini, "ESP", "WorldSmokeTimer", g::espWorldSmokeTimer);
        LoadBool(ini, "ESP", "WorldInfernoTimer", g::espWorldInfernoTimer);
        LoadBool(ini, "ESP", "WorldDecoyTimer", g::espWorldDecoyTimer);
        LoadBool(ini, "ESP", "WorldExplosiveTimer", g::espWorldExplosiveTimer);
        LoadBool(ini, "World", "GrenadeHelperEnabled", g::grenadeHelperEnabled);
        LoadBool(ini, "World", "GrenadeHelperVisible", g::grenadeHelperVisible);
        LoadBool(ini, "World", "FilterSmoke", g::grenadeHelperSmoke);
        LoadBool(ini, "World", "FilterMolotov", g::grenadeHelperMolotov);
        LoadBool(ini, "World", "FilterHE", g::grenadeHelperHe);
        LoadBool(ini, "World", "FilterFlash", g::grenadeHelperFlash);
        LoadInt(ini, "World", "ToggleKey", g::grenadeHelperToggleKey);
        LoadInt(ini, "World", "AddKey", g::grenadeHelperAddKey);
        LoadInt(ini, "World", "DeleteKey", g::grenadeHelperDeleteKey);
        LoadFloat(ini, "World", "CircleRadius", g::grenadeHelperRadius);
        LoadFloat(ini, "World", "CircleThickness", g::grenadeHelperThickness);
        LoadFloat(ini, "World", "TextSize", g::grenadeHelperTextSize);
        LoadFloat(ini, "World", "MaxDistance", g::grenadeHelperMaxDistance);
        LoadColor(ini, "World", "CircleColor", g::grenadeHelperCircleColor);
        LoadColor(ini, "World", "ActiveColor", g::grenadeHelperActiveColor);
        LoadColor(ini, "World", "AimColor", g::grenadeHelperAimColor);
        LoadColor(ini, "World", "TextColor", g::grenadeHelperTextColor);
        LoadBool(ini, "ESP", "BombInfo", g::espBombInfo);
        LoadBool(ini, "ESP", "BombText", g::espBombText);
        LoadBool(ini, "ESP", "BombTime", g::espBombTime);
        LoadBool(ini, "ESP", "BombTimerShowWithMenu", g::espBombTimerShowWithMenu);
        LoadFloat(ini, "ESP", "BombTextSize", g::espBombTextSize);
        LoadFloat(ini, "ESP", "BombTimerX", g::espBombTimerX);
        LoadFloat(ini, "ESP", "BombTimerY", g::espBombTimerY);
        LoadDisabledItemIds(ini, "ESP", "ItemHiddenIds", g::espItemEnabledMask);
        LoadFloat(ini, "ESP", "OffscreenSize", g::espOffscreenSize);
        LoadFloat(ini, "ESP", "BoxThickness", g::espBoxThickness);
        LoadInt(ini, "ESP", "BoxStyle", g::espBoxStyle);
        LoadInt(ini, "ESP", "BoxCornerPercent", g::espBoxCornerPercent);
        LoadFloat(ini, "ESP", "SkeletonThickness", g::espSkeletonThickness);
        LoadColor(ini, "ESP", "BoxColor", g::espBoxColor);
        LoadColor(ini, "ESP", "HealthColor", g::espHealthColor);
        LoadColor(ini, "ESP", "HealthLowColor", g::espHealthLowColor);
        LoadInt(ini, "ESP", "HealthColorMode", g::espHealthColorMode);
        LoadColor(ini, "ESP", "VisibleColor", g::espVisibleColor);
        LoadColor(ini, "ESP", "HiddenColor", g::espHiddenColor);
        LoadColor(ini, "ESP", "ArmorColor", g::espArmorColor);
        LoadColor(ini, "ESP", "ArmorLowColor", g::espArmorLowColor);
        LoadInt(ini, "ESP", "ArmorColorMode", g::espArmorColorMode);
        LoadColor(ini, "ESP", "NameColor", g::espNameColor);
        LoadColor(ini, "ESP", "DistanceColor", g::espDistanceColor);
        LoadColor(ini, "ESP", "SkeletonColor", g::espSkeletonColor);
        LoadColor(ini, "ESP", "SnaplineColor", g::espSnaplineColor);
        LoadColor(ini, "ESP", "OffscreenColor", g::espOffscreenColor);
        LoadColor(ini, "ESP", "FlagColor", g::espFlagColor);
        LoadColor(ini, "ESP", "WorldColor", g::espWorldColor);
        LoadColor(ini, "ESP", "ItemColor", g::espItemColor);
        LoadColor(ini, "ESP", "BombColor", g::espBombColor);

        LoadBool(ini, "Target", "Enabled", g::targetEnabled);
        LoadBool(ini, "Target", "FovEnabled", g::targetFovEnabled);
        LoadBool(ini, "Target", "FovPerWeapon", g::targetFovPerWeapon);
        LoadFloat(ini, "Target", "FovRadius", g::targetFovRadius);
        LoadColor(ini, "Target", "FovColor", g::targetFovColor);
        LoadBool(ini, "Target", "AimbotEnabled", g::targetAimbotEnabled);
        LoadInt(ini, "Target", "AimKey", g::targetAimKey);
        LoadInt(ini, "Target", "AimActivationMode", g::targetAimActivationMode);
        LoadInt(ini, "Target", "AimBone", g::targetAimBone);
        LoadFloat(ini, "Target", "AimSmoothing", g::targetAimSmoothing);
        LoadBool(ini, "Target", "AimVisibleOnly", g::targetAimVisibleOnly);
        LoadBool(ini, "Target", "AimPredictive", g::targetAimPredictive);
        LoadBool(ini, "Target", "AimRecoilControl", g::targetAimRecoilControl);
        LoadBool(ini, "Target", "AimHumanization", g::targetAimHumanization);
        LoadBool(ini, "Target", "TriggerbotEnabled", g::targetTriggerbotEnabled);
        LoadInt(ini, "Target", "TriggerKey", g::targetTriggerKey);
        LoadInt(ini, "Target", "TriggerActivationMode", g::targetTriggerActivationMode);
        LoadBool(ini, "Target", "TriggerAimAssist", g::targetTriggerAimAssist);
        LoadInt(ini, "Target", "TriggerAimBone", g::targetTriggerAimBone);
        LoadFloat(ini, "Target", "TriggerAimSmoothing", g::targetTriggerAimSmoothing);
        LoadBool(ini, "Target", "TriggerAimPredictive", g::targetTriggerAimPredictive);
        LoadBool(ini, "Target", "TriggerAimRecoilControl", g::targetTriggerAimRecoilControl);
        LoadBool(ini, "Target", "TriggerAimHumanization", g::targetTriggerAimHumanization);
        LoadInt(ini, "Target", "TriggerDelayMs", g::targetTriggerDelayMs);
        LoadBool(ini, "Target", "TriggerVisibleOnly", g::targetTriggerVisibleOnly);
        LoadBool(ini, "Target", "TriggerAutoShot", g::targetTriggerAutoShot);
        for (size_t index = 0; index < g::targetWeaponProfiles.size(); ++index) {
            const std::string prefix = "WeaponProfile" +
                std::to_string(index);
            auto& profile = g::targetWeaponProfiles[index];
            LoadFloat(ini, "Target", (prefix + "FovRadius").c_str(), profile.fovRadius);
            LoadFloat(ini, "Target", (prefix + "AimSmoothing").c_str(), profile.aimSmoothing);
            LoadFloat(ini, "Target", (prefix + "TriggerSmoothing").c_str(), profile.triggerSmoothing);
            LoadFloat(ini, "Target", (prefix + "Hitchance").c_str(), profile.hitchance);
            LoadFloat(ini, "Target", (prefix + "MinimumDamage").c_str(), profile.minimumDamage);
            LoadBool(ini, "Target", (prefix + "Autowall").c_str(), profile.autowall);
            profile.aimMinimumDamage = profile.minimumDamage;
            profile.aimAutowall = profile.autowall;
            LoadFloat(ini, "Target", (prefix + "AimMinimumDamage").c_str(), profile.aimMinimumDamage);
            LoadBool(ini, "Target", (prefix + "AimAutowall").c_str(), profile.aimAutowall);
            LoadFloat(ini, "Target", (prefix + "TriggerHitchance").c_str(), profile.hitchance);
            LoadBool(ini, "Target", (prefix + "TriggerHitchanceEnabled").c_str(), profile.hitchanceEnabled);
            LoadBool(ini, "Target", (prefix + "TriggerSeedWindowEnabled").c_str(), profile.seedWindowEnabled);
            LoadFloat(ini, "Target", (prefix + "TriggerMinimumDamage").c_str(), profile.minimumDamage);
            LoadBool(ini, "Target", (prefix + "TriggerAutowall").c_str(), profile.autowall);
        }

        LoadBool(ini, "Radar", "Enabled", g::radarEnabled);
        LoadInt(ini, "Radar", "Mode", g::radarMode);
        LoadBool(ini, "Radar", "ShowLocalDot", g::radarShowLocalDot);
        LoadBool(ini, "Radar", "ShowAngles", g::radarShowAngles);
        LoadBool(ini, "Radar", "ShowCrosshair", g::radarShowCrosshair);
        LoadBool(ini, "Radar", "ShowBomb", g::radarShowBomb);
        LoadFloat(ini, "Radar", "Size", g::radarSize);
        LoadFloat(ini, "Radar", "DotSize", g::radarDotSize);
        LoadFloat(ini, "Radar", "WorldRotationDeg", g::radarWorldRotationDeg);
        LoadFloat(ini, "Radar", "WorldScale", g::radarWorldScale);
        LoadFloat(ini, "Radar", "WorldOffsetX", g::radarWorldOffsetX);
        LoadFloat(ini, "Radar", "WorldOffsetY", g::radarWorldOffsetY);
        LoadBool(ini, "Radar", "StaticFlipX", g::radarStaticFlipX);
        LoadColor(ini, "Radar", "BgColor", g::radarBgColor);
        LoadColor(ini, "Radar", "BorderColor", g::radarBorderColor);
        LoadColor(ini, "Radar", "DotColor", g::radarDotColor);
        LoadColor(ini, "Radar", "BombColor", g::radarBombColor);
        LoadColor(ini, "Radar", "AngleColor", g::radarAngleColor);
        LoadBool(ini, "Radar", "SpectatorList", g::radarSpectatorList);
        LoadBool(ini, "Radar", "SpectatorListShowWithMenu", g::radarSpectatorListShowWithMenu);
        LoadFloat(ini, "Radar", "SpectatorListX", g::radarSpectatorListX);
        LoadFloat(ini, "Radar", "SpectatorListY", g::radarSpectatorListY);

        LoadBool(ini, "WEBRadar", "Enabled", g::webRadarEnabled);
        LoadInt(ini, "WEBRadar", "Port", g::webRadarPort);
        LoadString(ini, "WEBRadar", "MapOverride", g::webRadarMapOverride);

        LoadBool(ini, "Screen", "VSync", g::vsyncEnabled);
        LoadInt(ini, "Screen", "FPSLimit", g::fpsLimit);

        LoadBool(ini, "UI", "EspPreviewOpen", g::espPreviewOpen);
        LoadBool(ini, "UI", "RadarCalibrationOpen", g::radarCalibrationOpen);
        LoadBool(ini, "UI", "WebRadarQrOpen", g::webRadarQrOpen);
        LoadBool(ini, "UI", "WebRadarDebugOpen", g::webRadarDebugOpen);
        LoadInt(ini, "UI", "MenuToggleKey", g::menuToggleKey);
        LoadInt(ini, "UI", "OverlayToggleKey", g::overlayToggleKey);
        LoadInt(ini, "UI", "InputDeviceKind", g::inputDeviceKind);
        LoadString(ini, "UI", "InputDeviceNetHost", g::inputDeviceNetHost);
        LoadInt(ini, "UI", "InputDeviceNetPort", g::inputDeviceNetPort);
        LoadString(ini, "UI", "InputDeviceNetKey", g::inputDeviceNetKey);
    }

    IniDocument ParseIniFile(const std::filesystem::path& path)
    {
        IniDocument ini;
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return ini;

        std::string currentSection;
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF) {
                line.erase(0, 3);
            }

            const std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
                continue;

            if (trimmed.front() == '[' && trimmed.back() == ']') {
                currentSection = Trim(std::string_view(trimmed).substr(1, trimmed.size() - 2));
                continue;
            }

            const size_t eq = trimmed.find('=');
            if (eq == std::string::npos)
                continue;

            const std::string key = Trim(std::string_view(trimmed).substr(0, eq));
            const std::string value = Trim(std::string_view(trimmed).substr(eq + 1));
            if (!key.empty())
                ini[currentSection][key] = value;
        }

        return ini;
    }

    bool SaveJsonConfig(const std::string& jsonPath)
    {
        json finalRoot;
        {
            std::lock_guard<std::recursive_mutex> lock(g::settingsMutex);
            #include "config_parts/config_save_json_body.inl"
            finalRoot = std::move(root);
        }

        std::filesystem::path path(jsonPath);
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);

        std::filesystem::path tmpPath = path;
        tmpPath += ".tmp";
        {
            std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
                return false;
            file << finalRoot.dump(4);
            file.flush();
            if (!file.good())
                return false;
        }
        if (!app::platform::ReplaceFileWithTemp(tmpPath, path, ec)) {
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
        MarkSaveSucceeded(finalRoot.dump());
        return true;
    }

    bool LoadJsonConfig(const std::string& jsonPath)
    {
        std::ifstream file(jsonPath, std::ios::binary);
        if (!file.is_open())
            return false;

        json loadedRoot = json::parse(file, nullptr, false);
        if (loadedRoot.is_discarded() || !loadedRoot.is_object())
            return false;

        {
            std::lock_guard<std::recursive_mutex> lock(g::settingsMutex);
            ApplyConfig(GetDefaultConfig());
            ApplyLoadedConfig(loadedRoot);
            ValidateLoadedValues();
            g::configJustLoaded = true;

            json saveRoot;
            {
                #include "config_parts/config_save_json_body.inl"
                saveRoot = std::move(root);
            }
            InitializeSaveState(saveRoot.dump());
        }
        return true;
    }

    bool LoadFromLegacyPath(const std::filesystem::path& path)
    {
        const IniDocument ini = ParseIniFile(path);
        if (ini.empty())
            return false;

        {
            std::lock_guard<std::recursive_mutex> lock(g::settingsMutex);
            ApplyConfig(GetDefaultConfig());
            ApplyLoadedConfig(ini);
            ValidateLoadedValues();
            g::configJustLoaded = true;

            json saveRoot;
            {
                #include "config_parts/config_save_json_body.inl"
                saveRoot = std::move(root);
            }
            InitializeSaveState(saveRoot.dump());
        }
        return true;
    }

    bool MigrateLegacyProfileToJson(const std::string& profileName)
    {
        const std::string cleanName = SanitizeProfileName(profileName);
        const std::filesystem::path jsonPath = BuildProfilePathObject(cleanName);

        std::error_code ec;
        if (std::filesystem::exists(jsonPath, ec)) {
            DeleteLegacyProfileFiles(cleanName);
            return true;
        }

        std::filesystem::path legacySource;
        std::unordered_set<std::string> seen;
        for (const auto& candidate : CollectLegacyProfilePathCandidates(cleanName)) {
            const std::string key = candidate.lexically_normal().generic_string();
            if (!seen.insert(key).second)
                continue;

            ec.clear();
            if (std::filesystem::exists(candidate, ec)) {
                legacySource = candidate;
                break;
            }
        }

        if (legacySource.empty())
            return false;

        const ConfigSnapshot savedSnapshot = CaptureCurrentConfig();
        const std::string savedActiveProfile = CopyActiveProfile();

        const bool migrated = LoadFromLegacyPath(legacySource) && SaveToPath(jsonPath.string());

        ApplyConfig(savedSnapshot);
        SetActiveProfile(savedActiveProfile);

        if (migrated)
            DeleteLegacyProfileFiles(cleanName);

        return migrated;
    }

    void MigrateAllLegacyProfilesToJson()
    {
        std::unordered_set<std::string> profileNames;
        std::error_code ec;

        const std::filesystem::path directories[] = {
            GetConfigDirectory(),
            GetLegacyProfilesDirectory(),
        };

        for (const auto& directory : directories) {
            ec.clear();
            if (directory.empty() || !std::filesystem::exists(directory, ec))
                continue;

            for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
                if (ec || !entry.is_regular_file())
                    continue;

                const std::filesystem::path& path = entry.path();
                if (ToLower(path.extension().string()) != ".ini")
                    continue;

                const std::string name = path.stem().string();
                if (!name.empty() && !IsReservedProfileName(name))
                    profileNames.insert(name);
            }
        }

        for (const auto& name : profileNames)
            MigrateLegacyProfileToJson(name);
    }
}

namespace {
    bool SaveProfileContents(const std::string& profileName)
    {
        const std::string cleanName = SanitizeProfileName(profileName);
        if (!SaveToPath(BuildProfilePath(cleanName)))
            return false;

        webradar::remote::SaveSettings(cleanName);
        DeleteLegacyProfileFiles(cleanName);
        return true;
    }

    bool SaveDirtyProfileIfNeeded()
    {
        json currentRoot;
        {
            std::lock_guard<std::recursive_mutex> lock(g::settingsMutex);
            #include "config_parts/config_save_json_body.inl"
            currentRoot = std::move(root);
        }

        std::string currentDump = currentRoot.dump();
        bool shouldSave = false;
        {
            std::lock_guard<std::mutex> lock(s_saveStateMutex);
            if (!s_asyncSaveInitialized) {
                s_lastSavedJson = currentDump;
                s_lastQueuedJson = std::move(currentDump);
                s_asyncSaveInitialized = true;
                s_nextSaveRetryAt = {};
                return true;
            }

            const auto now = std::chrono::steady_clock::now();
            const bool retryAllowed =
                s_nextSaveRetryAt.time_since_epoch().count() == 0 ||
                now >= s_nextSaveRetryAt;
            if (retryAllowed &&
                currentDump != s_lastSavedJson &&
                currentDump != s_lastQueuedJson) {
                s_lastQueuedJson = std::move(currentDump);
                shouldSave = true;
            }
        }

        return !shouldSave || SaveProfileContents(CopyActiveProfile());
    }
}

void config::Save()
{
    SaveNamed(CopyActiveProfile());
}

namespace {
    std::mutex s_asyncSaveMutex;
    std::condition_variable s_asyncSaveCv;
    std::jthread s_asyncSaveThread;
    std::string s_asyncSaveProfile = "KevqDefault";
    std::atomic<bool> s_asyncSavePending{false};
    std::atomic<bool> s_asyncDirtyCheckPending{false};
    std::atomic<bool> s_asyncSaveActive{false};
    std::atomic<bool> s_asyncSaveStarted{false};

    void AsyncSaveLoop(const std::stop_token& stopToken) noexcept
    {
        while (!stopToken.stop_requested()) {
            try {
                std::unique_lock<std::mutex> lock(s_asyncSaveMutex);
                s_asyncSaveCv.wait_for(lock, std::chrono::milliseconds(250), [&stopToken] {
                    return s_asyncSavePending.load(std::memory_order_acquire) ||
                           s_asyncDirtyCheckPending.load(std::memory_order_acquire) ||
                           stopToken.stop_requested();
                });
                if (stopToken.stop_requested())
                    break;
                const bool saveRequested =
                    s_asyncSavePending.exchange(false, std::memory_order_acq_rel);
                const bool dirtyCheckRequested =
                    s_asyncDirtyCheckPending.exchange(false, std::memory_order_acq_rel);
                if (!saveRequested && !dirtyCheckRequested)
                    continue;
                const std::string profileToSave =
                    saveRequested ? s_asyncSaveProfile : std::string{};
                lock.unlock();
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                s_asyncSaveActive.store(true, std::memory_order_release);
                const bool saveSucceeded = saveRequested
                    ? SaveProfileContents(profileToSave)
                    : SaveDirtyProfileIfNeeded();
                if (!saveSucceeded)
                    MarkSaveFailed();
            } catch (...) {
                try {
                    MarkSaveFailed();
                } catch (...) {
                    app::diagnostics::WriteFallbackError(
                        "Config async-save failure handler threw an exception");
                }
            }
            s_asyncSaveActive.store(false, std::memory_order_release);
            s_asyncSaveCv.notify_all();
        }
    }

    void EnsureAsyncSaveThread()
    {
        bool expected = false;
        if (!s_asyncSaveStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
            return;
        s_asyncSaveThread = std::jthread(AsyncSaveLoop);
    }
}

void config::SaveAsync()
{
    const std::string activeProfile = CopyActiveProfile();
    {
        std::lock_guard<std::mutex> lock(s_asyncSaveMutex);
        EnsureAsyncSaveThread();
        s_asyncSaveProfile = activeProfile;
        s_asyncSavePending.store(true, std::memory_order_release);
    }
    s_asyncSaveCv.notify_one();
}

void config::SaveIfDirty()
{
    constexpr int64_t kDirtyCheckIntervalMs = 200;
    const int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    int64_t nextCheckMs = s_nextDirtyCheckMs.load(std::memory_order_relaxed);
    if (nowMs < nextCheckMs)
        return;
    if (!s_nextDirtyCheckMs.compare_exchange_strong(
            nextCheckMs,
            nowMs + kDirtyCheckIntervalMs,
            std::memory_order_relaxed,
            std::memory_order_relaxed)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(s_asyncSaveMutex);
        EnsureAsyncSaveThread();
        s_asyncDirtyCheckPending.store(true, std::memory_order_release);
    }
    s_asyncSaveCv.notify_one();
}

void config::FlushAsyncSaves()
{
    if (!s_asyncSaveStarted.load(std::memory_order_acquire))
        return;

    const std::string activeProfile = CopyActiveProfile();
    {
        std::lock_guard<std::mutex> lock(s_asyncSaveMutex);
        s_asyncSaveProfile = activeProfile;
        s_asyncSavePending.store(true, std::memory_order_release);
    }
    s_asyncSaveCv.notify_one();

    
    {
        std::unique_lock<std::mutex> lock(s_asyncSaveMutex);
        s_asyncSaveCv.wait_for(lock, std::chrono::seconds(2), [] {
            return !s_asyncSavePending.load(std::memory_order_acquire) &&
                   !s_asyncDirtyCheckPending.load(std::memory_order_acquire) &&
                   !s_asyncSaveActive.load(std::memory_order_acquire);
        });
    }

    
    s_asyncSaveThread.request_stop();
    s_asyncSaveCv.notify_one();
    if (s_asyncSaveThread.joinable())
        s_asyncSaveThread.join();
}

void config::Load()
{
    MigrateAllLegacyProfilesToJson();

    const std::string persistedProfile = LoadPersistedActiveProfileName();
    if (!persistedProfile.empty())
        SetActiveProfile(persistedProfile);

    const std::string activeProfile = CopyActiveProfile();
    if (!LoadNamed(activeProfile) && activeProfile != "KevqDefault")
        LoadNamed("KevqDefault");
}

bool config::SaveNamed(const std::string& profileName)
{
    if (!IsUsableProfileName(profileName))
        return false;
    const std::string cleanName = SanitizeProfileName(profileName);
    if (!SaveProfileContents(cleanName))
        return false;

    SetActiveProfile(cleanName);
    PersistActiveProfileName(cleanName);
    return true;
}

bool config::LoadNamed(const std::string& profileName)
{
    if (!IsUsableProfileName(profileName))
        return false;
    const std::string cleanName = SanitizeProfileName(profileName);
    MigrateLegacyProfileToJson(cleanName);
    const std::string path = BuildProfilePath(cleanName);
    if (!LoadFromPath(path))
        return false;

    SetActiveProfile(cleanName);
    webradar::remote::LoadSettings(cleanName);
    PersistActiveProfileName(cleanName);
    return true;
}

std::vector<std::string> config::ListProfiles()
{
    MigrateAllLegacyProfilesToJson();

    std::vector<std::string> result;
    std::error_code ec;

    const std::filesystem::path configDir(GetConfigDirectory());
    if (std::filesystem::exists(configDir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(configDir, ec)) {
            if (ec || !entry.is_regular_file())
                continue;

            const std::filesystem::path& path = entry.path();
            if (ToLower(path.extension().string()) != ".json")
                continue;

            const std::string name = path.stem().string();
            if (!name.empty() && !IsReservedProfileName(name))
                result.push_back(name);
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

std::string config::GetActiveProfile()
{
    return CopyActiveProfile();
}
