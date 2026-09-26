#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <json/json.hpp>

namespace webradar::remote {

struct Settings {
    bool enabled = false;
    std::string host;
    int webPort = 8080;
    int sshPort = 22;
    std::string login = "root";
    std::string password;
    std::string remotePath = "/opt/kevqdma-webradar";
};

inline void ParseSettingsJson(const nlohmann::json& root, Settings& settings)
{
    if (!root.is_object()) return;
    const auto text = [&](const char* key, std::string& value) {
        const auto it = root.find(key);
        if (it != root.end() && it->is_string() && it->get_ref<const std::string&>().size() <= 4096)
            value = it->get<std::string>();
    };
    const auto port = [&](const char* key, int& value) {
        const auto it = root.find(key);
        if (it == root.end() || !it->is_number_integer()) return;
        if (it->is_number_unsigned()) {
            const auto candidate = it->get<uint64_t>();
            if (candidate >= 1 && candidate <= 65535) value = static_cast<int>(candidate);
        } else {
            const auto candidate = it->get<int64_t>();
            if (candidate >= 1 && candidate <= 65535) value = static_cast<int>(candidate);
        }
    };
    if (const auto it = root.find("EnableWeb"); it != root.end() && it->is_boolean())
        settings.enabled = it->get<bool>();
    text("Host", settings.host);
    text("Login", settings.login);
    text("Password", settings.password);
    text("RemotePath", settings.remotePath);
    port("WebPort", settings.webPort);
    port("SshPort", settings.sshPort);
}

struct Stats {
    bool enabled = false;
    bool configured = false;
    bool connected = false;
    uint64_t sentPackets = 0;
    uint64_t sentBytes = 0;
    uint64_t queueDrops = 0;
    uint64_t reconnects = 0;
    uint64_t lastSendUnixMs = 0;
    int lastPingMs = -1;
    std::string lastError;
};

void Start();
void Stop();
void Configure(const Settings& settings);
void Publish(const std::string& compactPayload);
Stats GetStats();
bool HasActiveConsumerDemand();

bool LoadSettings(std::string_view profileName);
bool SaveSettings(std::string_view profileName, const Settings& settings);

bool TestPing(const Settings& settings, int* outMs, std::string* outError);
bool TestHttpStatus(const Settings& settings, std::string* outError);
bool TestSshConnection(const Settings& settings, std::string* outError);
bool CheckServerReady(const Settings& settings, std::string* outError);
bool ExportDeployPackage(const Settings& settings, std::filesystem::path* outPath, std::string* outError);
using DeployProgressCallback = std::function<void(std::string_view)>;
bool DeployToServer(
    const Settings& settings,
    std::filesystem::path* outPath,
    std::string* outError,
    const DeployProgressCallback& progress = {});
Settings CaptureSettingsFromGlobals();
void ApplySettingsToGlobals(const Settings& settings);

}
