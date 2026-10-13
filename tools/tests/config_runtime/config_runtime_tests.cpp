#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include "../../../src/app/Config/config.cpp"

namespace fixture {
    std::filesystem::path root;
    std::mutex remoteMutex;
    std::map<std::string, webradar::remote::Settings> remote;
}
std::filesystem::path app::paths::GetConfigDirectory() { return fixture::root / "profiles"; }
std::filesystem::path app::paths::GetLegacyProfilesDirectory() { return fixture::root / "legacy"; }
std::filesystem::path app::paths::GetSettingsDirectory() { return fixture::root / "settings"; }
webradar::remote::Settings webradar::remote::CaptureSettingsFromGlobals() {
    Settings value;
    value.host = g::webRadarRemoteHost;
    return value;
}
bool webradar::remote::SaveSettings(std::string_view name, const Settings& settings) {
    std::lock_guard<std::mutex> lock(fixture::remoteMutex);
    fixture::remote[std::string(name)] = settings;
    return true;
}
bool webradar::remote::LoadSettings(std::string_view name) {
    std::lock_guard<std::mutex> lock(fixture::remoteMutex);
    g::webRadarRemoteHost = fixture::remote[std::string(name)].host;
    return true;
}

int main() {
    fixture::root = std::filesystem::temp_directory_path() /
        ("kevq-config-regression-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
    std::filesystem::create_directories(fixture::root);
    int failures = 0;
    const auto check = [&](bool value) { if (!value) ++failures; };
    g::espHealth = false; g::webRadarRemoteHost = "profile-B";
    check(config::SaveNamed("B"));
    g::espHealth = true; g::webRadarRemoteHost = "profile-A";
    check(config::SaveNamed("A"));
    for (int repeat = 0; repeat < 40; ++repeat) {
        check(config::LoadNamed("A"));
        config::SaveAsync();
        check(config::LoadNamed("B"));
        config::FlushAsyncSaves();
        std::ifstream file(fixture::root / "profiles/A.json");
        const auto saved = nlohmann::json::parse(file);
        check(saved["ESP"]["Health"].get<bool>());
        check(fixture::remote["A"].host == "profile-A");
        check(config::GetActiveProfile() == "B" && !g::espHealth);
    }
    check(config::LoadNamed("A"));
    const auto immutableA = CaptureProfileSave("A");
    check(config::LoadNamed("B"));
    check(SaveProfileContents(immutableA));
    check(config::LoadNamed("A") && g::espHealth);
    const auto older = CaptureProfileSave("A");
    g::espHealth = false;
    const auto newer = CaptureProfileSave("A");
    check(SaveProfileContents(newer));
    check(SaveProfileContents(older));
    std::ifstream file(fixture::root / "profiles/A.json");
    check(!nlohmann::json::parse(file)["ESP"]["Health"].get<bool>());
    file.close();
    ApplyConfig(GetDefaultConfig());
    ApplyLoadedConfig(nlohmann::json{{"Target", {{"AimSmoothing",9.0},{"TriggerAimSmoothing",6.0},
        {"WeaponProfiles",nlohmann::json::array({{{"AimSmoothing",7.0}}})}}}});
    check(g::targetWeaponProfiles[0].aimSmoothing == 7.0f);
    check(g::targetWeaponProfiles[1].aimSmoothing == 9.0f);
    check(g::targetWeaponProfiles[0].triggerSmoothing == 6.0f);
    const auto migrated = CaptureProfileSave("migration");
    g::displaySettings.mapping.gameWidth = 1280;
    g::displaySettings.mapping.gameHeight = 960;
    g::displaySettings.mapping.autoAspect = true;
    g::displaySettings.mapping.scaling = 2;
    g::displaySettings.mapping.x = 0.1f;
    g::displaySettings.mapping.width = 0.8f;
    g::radarSettings.hud.manual = true;
    g::radarSettings.hud.shape = 1;
    g::radarSettings.hud.x = 14;
    g::radarSettings.hud.width = 380;
    const auto displayProfile = CaptureProfileSave("display");
    ApplyConfig(GetDefaultConfig());
    check(g::displaySettings.mapping.gameWidth == 1920 && g::displaySettings.mapping.gameHeight == 1080);
    ApplyLoadedConfig(displayProfile.root);
    check(g::displaySettings.mapping.gameWidth == 1280 && g::displaySettings.mapping.gameHeight == 960);
    check(g::displaySettings.mapping.autoAspect && g::displaySettings.mapping.scaling == 2);
    check(g::displaySettings.mapping.x == 0.1f && g::displaySettings.mapping.width == 0.8f);
    check(g::radarSettings.hud.manual && g::radarSettings.hud.shape == 1);
    check(g::radarSettings.hud.x == 14 && g::radarSettings.hud.width == 380);
    const auto mappingSnapshot = CaptureCurrentConfig();
    g::espWorldSmokeTimerColor[0] = 0.25f;
    g::espWorldSmokeTimerColor[3] = 0.75f;
    g::espWorldInfernoTimerColor[1] = 0.125f;
    const auto timerProfile = CaptureProfileSave("timer-colors");
    ApplyConfig(GetDefaultConfig());
    check(g::espWorldSmokeTimerColor[3] == 1.0f);
    ApplyLoadedConfig(timerProfile.root);
    check(g::espWorldSmokeTimerColor[0] == 0.25f && g::espWorldSmokeTimerColor[3] == 0.75f);
    check(g::espWorldInfernoTimerColor[1] == 0.125f);
    const auto timerSnapshot = CaptureCurrentConfig();
    ApplyConfig(GetDefaultConfig());
    ApplyConfig(timerSnapshot);
    check(g::espWorldSmokeTimerColor[0] == 0.25f && g::espWorldInfernoTimerColor[1] == 0.125f);
    g::displaySettings.mapping = {}; g::radarSettings.hud = {};
    ApplyConfig(mappingSnapshot);
    check(g::displaySettings.mapping.gameWidth == 1280 && g::radarSettings.hud.shape == 1);
    check(!migrated.root["Target"].contains("AimSmoothing"));
    check(!migrated.root["Target"].contains("TriggerAimSmoothing"));
    check(!migrated.root["Target"]["WeaponProfiles"][0].contains("AimRecoilVariation"));
    g::targetSettings.fovUnit = 1; g::targetSettings.fovDegrees = 7.5f;
    auto& modern = g::targetWeaponProfiles[1];
    modern.aimTrajectoryControl = false; modern.aimRespectInput = false;
    modern.aimMaxAcceleration = 650; modern.aimMaxJerk = 32000; modern.fovDegrees = 3.5f;
    const auto modernSnapshot = CaptureCurrentConfig();
    ApplyConfig(GetDefaultConfig());
    ApplyConfig(modernSnapshot);
    check(g::targetSettings.fovUnit == 1 && g::targetSettings.fovDegrees == 7.5f);
    check(!modern.aimTrajectoryControl && !modern.aimRespectInput && modern.aimMaxAcceleration == 650);
    check(modern.aimMaxJerk == 32000 && modern.fovDegrees == 3.5f);
    modern.aimMaxAcceleration = NAN; modern.aimMaxJerk = -1; modern.fovDegrees = 90;
    g::targetSettings.fovUnit = 9; g::targetSettings.fovDegrees = NAN;
    ValidateLoadedValues();
    check(g::targetSettings.fovUnit == 0 && std::isfinite(g::targetSettings.fovDegrees));
    check(std::isfinite(modern.aimMaxAcceleration) && modern.aimMaxJerk >= 1000 && modern.fovDegrees <= 30);
    const auto guardedPath = fixture::root / "profiles/guarded.json";
    const auto writeGuarded = [&](const nlohmann::json& document) {
        std::ofstream output(guardedPath, std::ios::binary | std::ios::trunc);
        output << document.dump();
    };
    const auto readGuarded = [&]() {
        std::ifstream input(guardedPath, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(input), {});
    };
    for (const auto& version : {nlohmann::json(2), nlohmann::json(-1),
            nlohmann::json("1"), nlohmann::json(1.5), nlohmann::json(nullptr),
            nlohmann::json(18446744073709551615ull)}) {
        writeGuarded({{"configVersion",version},{"ESP",{{"Health",false}}}});
        const auto original = readGuarded();
        g::espHealth = true;
        check(!LoadJsonConfig(guardedPath.string()) && g::espHealth);
        check(!WriteJsonConfig(guardedPath.string(),timerProfile.root));
        check(original == readGuarded());
    }
    writeGuarded({{"configVersion",1},{"ESP",false}});
    check(!LoadJsonConfig(guardedPath.string()));
    writeGuarded({{"configVersion",1},{"customExtension",{{"keep",42}}}});
    check(WriteJsonConfig(guardedPath.string(),timerProfile.root));
    check(nlohmann::json::parse(readGuarded())["customExtension"]["keep"] == 42);
    check(std::filesystem::exists(guardedPath.string()+".bak"));
    {
        std::ofstream oversized(guardedPath, std::ios::binary | std::ios::trunc);
        oversized << std::string(static_cast<size_t>(kMaxConfigBytes)+1,' ');
    }
    check(!LoadJsonConfig(guardedPath.string()));
    std::filesystem::create_directories(fixture::root/"legacy");
    {
        std::ofstream legacy(fixture::root/"legacy/recover.ini");
        legacy << "[ESP]\nHealth=1\n";
    }
    check(MigrateLegacyProfileToJson("recover"));
    check(std::filesystem::exists(fixture::root/"legacy/recover.ini.bak"));
    check(!std::filesystem::exists(fixture::root/"legacy/recover.ini"));
    std::cout << "Config snapshot/ordering/restart/guard/backup checks: " << failures << " failures; fixture " << fixture::root << '\n';
    return failures ? 1 : 0;
}
