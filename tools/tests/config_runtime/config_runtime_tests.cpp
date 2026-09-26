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
    check(!migrated.root["Target"].contains("AimSmoothing"));
    check(!migrated.root["Target"].contains("TriggerAimSmoothing"));
    check(!migrated.root["Target"]["WeaponProfiles"][0].contains("AimRecoilVariation"));
    std::cout << "Config snapshot/ordering/restart checks: " << failures << " failures; fixture " << fixture::root << '\n';
    return failures ? 1 : 0;
}
