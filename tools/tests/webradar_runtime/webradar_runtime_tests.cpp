#include <winsock2.h>
#include <ws2tcpip.h>
#include "Features/WebRadar/webradar.h"
#include "Features/WebRadar/web_remote.h"
#include "Features/WebRadar/runtime_utils.h"
#include "app/Localization/localization.h"
#include "app/Config/project_paths.h"
#include <json/json.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <stdexcept>

using Json = nlohmann::json;
using namespace std::chrono_literals;
namespace {
    std::mutex payloadMutex;
    Json payload;
    void Check(bool condition, const char* label) {
        if (!condition) throw std::runtime_error(label);
    }
    template<class Predicate> void Wait(Predicate predicate, const char* label,
        std::chrono::milliseconds timeout = 3000ms) {
        const auto until = std::chrono::steady_clock::now() + timeout;
        while (!predicate()) {
            Check(std::chrono::steady_clock::now() < until, label);
            std::this_thread::sleep_for(10ms);
        }
    }
    struct Socket {
        SOCKET value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        ~Socket() { if (value != INVALID_SOCKET) closesocket(value); }
    };
    uint16_t FreePort() {
        Socket s;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        Check(bind(s.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "bind probe");
        int length = sizeof(address);
        Check(getsockname(s.value, reinterpret_cast<sockaddr*>(&address), &length) == 0, "port probe");
        return ntohs(address.sin_port);
    }
    Json Live(uint16_t port) {
        Socket s;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        const DWORD timeout = 2000;
        setsockopt(s.value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        Check(connect(s.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "connect HTTP");
        const std::string request = "GET /api/live?v=2 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
        Check(webradar::runtime::SendAll(s.value, request.data(), request.size()), "send HTTP");
        std::string response;
        char buffer[4096];
        for (;;) {
            const int bytes = recv(s.value, buffer, sizeof(buffer), 0);
            Check(bytes >= 0, "receive HTTP");
            if (bytes == 0) break;
            response.append(buffer, bytes);
        }
        Check(response.starts_with("HTTP/1.1 200"), "HTTP status");
        const auto body = response.find("\r\n\r\n");
        Check(body != std::string::npos, "HTTP body");
        return Json::parse(response.substr(body + 4));
    }
    bool HasEntities(bool present) {
        std::lock_guard lock(payloadMutex);
        return payload.is_object() && !payload.at("p").empty() == present;
    }
}
namespace esp {
    uint64_t GetPublishCount() { return 0; }
    bool GetWebRadarSnapshot(WebRadarSnapshot*) { return false; }
}
namespace app::paths {
    std::filesystem::path ResolveWebRadarAssetDirectory() {
        return std::filesystem::current_path() / "src/Features/WebRadar/Assets";
    }
    std::filesystem::path ResolveWebRadarAssetPath(const std::filesystem::path& relative) {
        return ResolveWebRadarAssetDirectory() / relative;
    }
}
namespace app::localization {
    Language GetLanguage() noexcept { return Language::English; }
    const char* LanguageCode(Language) noexcept { return "en"; }
    const char* Get(const char* text) noexcept { return text; }
}
namespace webradar::remote {
    void Start() {}
    void Stop() {}
    void Configure(const Settings&) {}
    bool HasActiveConsumerDemand() { return true; }
    void Publish(const std::string& value) {
        std::lock_guard lock(payloadMutex);
        payload = Json::parse(value);
    }
}
int main()
{
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 1;
    int result = 0;
    try {
        Check(!webradar::runtime::IsSnapshotReceiptFresh(0, 0), "missing receipt");
        Check(!webradar::runtime::IsSnapshotReceiptFresh(1000, 999), "future receipt");
        Check(webradar::runtime::IsSnapshotReceiptFresh(1000, 1500), "receipt boundary");
        Check(!webradar::runtime::IsSnapshotReceiptFresh(1000, 1501), "expired receipt");
        webradar::WEBRadar server;
        const auto port = FreePort();
        server.Configure(true, 17, port, "", false, {});
        server.Start();
        Wait([&] { return server.GetStats().serverListening; }, "HTTP startup");
        auto snapshot = std::make_unique<esp::WebRadarSnapshot>();
        snapshot->localPos = {NAN, NAN, NAN};
        strcpy_s(snapshot->mapKey, "de_inferno");
        snapshot->captureTickMs = 1000;
        auto& player = snapshot->players[0];
        player.valid = true;
        player.pawn = 0x10000;
        player.health = 100;
        player.team = 3;
        player.position = {100, 200, 30};
        strcpy_s(player.name, "Regression");
        snapshot->bomb.dropped = true;
        snapshot->bomb.position = {50, 60, 70};
        snapshot->worldMarkerCount = 1;
        snapshot->worldMarkers[0] = {2, {60, 70, 80}, 46, 5.0f};
        server.UpdateSnapshot(*snapshot);
        Wait([] { return HasEntities(true); }, "first snapshot");
        const auto first = Live(port);
        Check(first.at("p").size() == 1 && first.at("w").size() == 1, "live entities");
        Wait([] { return HasEntities(false); }, "producer stall clears snapshot", 1500ms);
        const auto expired = Live(port);
        Check(expired.at("p").empty() && expired.at("w").empty() && expired.at("b")[0] == 0,
            "expired HTTP data");
        Check(first.at("sid") == expired.at("sid") && expired.at("seq") > first.at("seq") &&
            expired.at("ts") >= first.at("ts"), "ordered clear frame");
        const auto sent = server.GetStats().sentPackets;
        std::this_thread::sleep_for(200ms);
        Check(server.GetStats().sentPackets == sent, "expiry publishes once");
        snapshot->captureTickMs = 3000;
        server.UpdateSnapshot(*snapshot);
        Wait([] { return HasEntities(true); }, "producer recovery");
        const auto recovered = Live(port);
        Check(recovered.at("seq") > expired.at("seq") && recovered.at("ts") > expired.at("ts"),
            "ordered recovery");
        server.Configure(true, 2000, port, "", false, {});
        Wait([] { return HasEntities(false); }, "expiry independent of slow publish rate", 1500ms);
        server.Stop();
        server.Start();
        Wait([&] { return server.GetStats().serverListening; }, "server restart");
        Check(Live(port).at("p").empty(), "no old entities after restart");
        server.Stop();
        std::cout << "Native WebRadar passed: HTTP, producer stall/expiry, recovery, ordered frames, slow cadence, restart.\n";
    } catch (const std::exception& error) {
        std::cerr << "Native WebRadar failure: " << error.what() << '\n';
        result = 1;
    }
    WSACleanup();
    return result;
}
