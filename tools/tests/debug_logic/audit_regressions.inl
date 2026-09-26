#include "Features/WebRadar/web_remote.h"

void TestAuditRegressions()
{
    using esp::data::DeferredLane;
    for (uint64_t step : {4000u, 12000u, 24000u}) {
        esp::data::DeferredLaneFairness fairness;
        std::array<uint64_t,5> last{};
        std::array<int,5> serviced{};
        const std::array<uint64_t,5> intervals{20000,40000,10000,80000,5000};
        for (uint64_t now = 1000000; now < 25000000; now += step) {
            const auto forced = fairness.Select(now);
            std::array<bool,5> due{};
            for (size_t lane = 0; lane < last.size(); ++lane) {
                due[lane] = now-last[lane] >= intervals[lane];
                if (lane) fairness.Observe(static_cast<DeferredLane>(lane), due[lane], now);
            }
            size_t selected = 5;
            if (forced != DeferredLane::None) selected = static_cast<size_t>(forced);
            else for (size_t lane = 0; lane < due.size(); ++lane)
                if (due[lane]) { selected = lane; break; }
            if (selected < 5 && due[selected]) {
                if (serviced[selected]) CHECK(now-last[selected] <= 500000);
                last[selected] = now;
                ++serviced[selected];
                if (selected) fairness.Served(static_cast<DeferredLane>(selected));
            }
        }
        for (int count : serviced) CHECK(count > 20);
        fairness = {};
        CHECK(fairness.Select(1) == DeferredLane::None);
    }
    using webradar::remote::Settings;
    for (const auto& invalid : {nlohmann::json(nullptr), nlohmann::json("123"),
            nlohmann::json(-1), nlohmann::json(UINT64_MAX), nlohmann::json(1.25), nlohmann::json::array()}) {
        Settings value;
        webradar::remote::ParseSettingsJson({{"WebPort",invalid},{"SshPort",invalid},{"EnableWeb",invalid}},value);
        CHECK(value.webPort == 8080 && value.sshPort == 22 && !value.enabled);
    }
    Settings value;
    webradar::remote::ParseSettingsJson({{"WebPort",65535},{"EnableWeb",true},{"Host","example.test"}},value);
    CHECK(value.enabled && value.webPort == 65535 && value.host == "example.test");
}

void TestLostReleaseAndCircleCancellation(app::input::DeviceKind kind)
{
    using namespace app::input;
    WSADATA data{};
    CHECK(WSAStartup(MAKEWORD(2,2), &data) == 0);
    SOCKET server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    CHECK(server != INVALID_SOCKET);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(server,reinterpret_cast<sockaddr*>(&address),sizeof(address)) == 0);
    int length = sizeof(address);
    CHECK(getsockname(server,reinterpret_cast<sockaddr*>(&address),&length) == 0);
    u_long nonblocking = 1;
    ioctlsocket(server,FIONBIO,&nonblocking);
    std::atomic<bool> running{true}, down{false}, pauseFirstMove{false}, movePaused{false};
    std::atomic<int> ups{0}, downs{0}, moves{0};
    std::jthread simulation([&] {
        while (running) {
            std::array<char,1024> bytes{};
            sockaddr_in sender{};
            int senderLength = sizeof(sender);
            const int count = recvfrom(server,bytes.data(),static_cast<int>(bytes.size()),0,
                reinterpret_cast<sockaddr*>(&sender),&senderLength);
            if (count >= 16) {
                uint32_t command = 0;
                std::memcpy(&command,bytes.data()+12,4);
                if (command == 0x9823AE8Du && count >= 20) {
                    int32_t buttons = 0;
                    std::memcpy(&buttons,bytes.data()+16,4);
                    if (buttons & 1) { ++downs; down = true; }
                    else if (++ups == 1) continue;
                    else down = false;
                }
                if (command == 0xAEDE7345u) {
                    ++moves;
                    if (pauseFirstMove.exchange(false)) {
                        movePaused = true;
                        while (running && movePaused) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
                sendto(server,bytes.data(),16,0,reinterpret_cast<sockaddr*>(&sender),senderLength);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });
    const auto wait = [&](auto predicate) {
        const auto until = std::chrono::steady_clock::now()+std::chrono::seconds(4);
        while (!predicate() && std::chrono::steady_clock::now()<until)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        CHECK(predicate());
    };
    SetSelectedDevice(kind);
    SetKmBoxNetConfig({"127.0.0.1",ntohs(address.sin_port),"A1B2C3D4"});
    CHECK(RequestConnectAndTest());
    wait([] { return GetDeviceStatus().state == ConnectionState::Connected; });
    CHECK(TryRequestLeftClick(18,0) == ClickRequestResult::Unavailable);
    CHECK(RequestLeftClick(18));
    wait([&] { return ups >= 1; });
    CHECK(down && GetLeftClickStatus().outputDown);
    wait([&] { return ups >= 2 && !GetLeftClickStatus().active; });
    CHECK(!down && downs == 1 && !GetLeftClickStatus().outputDown);
    CHECK(RequestDisconnect());
    wait([] { return GetDeviceStatus().state == ConnectionState::Disconnected; });
    pauseFirstMove = true;
    CHECK(RequestConnectAndTest());
    wait([&] { return movePaused.load(); });
    const int before = moves;
    SetSelectedDevice(DeviceKind::None);
    movePaused = false;
    wait([] { return GetDeviceStatus().state == ConnectionState::Disconnected; });
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    CHECK(moves == before);
    running = false;
    simulation.join();
    closesocket(server);
    WSACleanup();
}
