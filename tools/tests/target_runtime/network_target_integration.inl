struct NetworkTargetFixture
{
    SOCKET socket = INVALID_SOCKET;
    uint16_t port = 0;
    std::atomic<bool> running{true};
    std::atomic<bool> monitorEnabled{true};
    std::atomic<uint8_t> buttons{0};
    std::atomic<uint8_t> key{0};
    std::atomic<uint8_t> modifiers{0};
    std::atomic<int> moves{0};
    std::atomic<int> downs{0};
    std::atomic<int> ups{0};
    std::atomic<int> inconsistentButtons{0};
    std::atomic<int> lastMoveX{0};
    std::atomic<int> lastMoveY{0};
    std::atomic<bool> delayNextMoveAck{false};
    std::atomic<bool> delayNextProbeAck{false};
    std::atomic<bool> releaseMoveAck{false};
    std::atomic<bool> moveAckBlocked{false};
    std::thread worker;

    NetworkTargetFixture()
    {
        socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        CHECK(socket != INVALID_SOCKET);
        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        CHECK(bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
        int size = sizeof(address);
        CHECK(getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size) == 0);
        port = ntohs(address.sin_port);
        u_long nonblocking = 1;
        CHECK(ioctlsocket(socket, FIONBIO, &nonblocking) == 0);
        worker = std::thread([this] {
            uint16_t monitorPort = 0;
            int32_t outputButtons = 0;
            std::array<uint8_t, 16> delayedAck = {};
            sockaddr_in delayedSender = {};
            while (running.load()) {
                std::array<uint8_t, 1024> bytes = {};
                sockaddr_in sender = {};
                int senderSize = sizeof(sender);
                const int received = recvfrom(socket, reinterpret_cast<char*>(bytes.data()),
                    static_cast<int>(bytes.size()), 0, reinterpret_cast<sockaddr*>(&sender), &senderSize);
                if (received >= 16) {
                    uint32_t command = 0;
                    bool delayAck = false;
                    std::memcpy(&command, bytes.data() + 12, 4);
                    if (command == 0x27388020u) {
                        uint32_t random = 0;
                        std::memcpy(&random, bytes.data() + 4, 4);
                        monitorPort = (random >> 16) == 0xAA55 ? static_cast<uint16_t>(random) : 0;
                    } else if (received >= 28) {
                        int32_t fields[3] = {};
                        std::memcpy(fields, bytes.data() + 16, sizeof(fields));
                        if (command == 0x9823AE8Du) {
                            outputButtons = fields[0];
                            if (outputButtons & 1) ++downs; else ++ups;
                        } else if (command == 0xAEDE7345u) {
                            if (fields[0] != outputButtons) ++inconsistentButtons;
                            if (fields[1] != 0 || fields[2] != 0) {
                                lastMoveX = fields[1]; lastMoveY = fields[2];
                                ++moves;
                                delayAck = delayNextMoveAck.exchange(false);
                            } else delayAck = delayNextProbeAck.exchange(false);
                        }
                    }
                    if (delayAck) {
                        std::copy_n(bytes.begin(), delayedAck.size(), delayedAck.begin());
                        delayedSender = sender;
                        moveAckBlocked = true;
                    } else {
                        sendto(socket, reinterpret_cast<const char*>(bytes.data()), 16, 0,
                            reinterpret_cast<const sockaddr*>(&sender), senderSize);
                    }
                }
                if (moveAckBlocked.load() && releaseMoveAck.load()) {
                    sendto(socket, reinterpret_cast<const char*>(delayedAck.data()), 16, 0,
                        reinterpret_cast<const sockaddr*>(&delayedSender), sizeof(delayedSender));
                    moveAckBlocked = false;
                }
                if (monitorPort != 0 && monitorEnabled.load()) {
                    std::array<uint8_t, 20> report = {};
                    report[0] = 1; report[1] = buttons.load();
                    report[8] = 2; report[9] = modifiers.load(); report[10] = key.load();
                    sockaddr_in destination = {};
                    destination.sin_family = AF_INET;
                    destination.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                    destination.sin_port = htons(monitorPort);
                    sendto(socket, reinterpret_cast<const char*>(report.data()),
                        static_cast<int>(report.size()), 0,
                        reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    ~NetworkTargetFixture()
    {
        running = false;
        if (worker.joinable()) worker.join();
        closesocket(socket);
    }
};

int RunNetworkTargetIntegration()
{
    using namespace app::input;
    WSADATA data = {};
    CHECK(WSAStartup(MAKEWORD(2, 2), &data) == 0);
    const auto wait = [](auto condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!condition() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        CHECK(condition());
    };
    {
        NetworkTargetFixture server;
        for (const auto kind : {DeviceKind::KmBoxNet, DeviceKind::FerrumOne}) {
            SetSelectedDevice(kind);
            SetKmBoxNetConfig({"127.0.0.1", server.port, "A1B2C3D4"});
            CHECK(RequestConnectAndTest());
            wait([] { return GetDeviceStatus().state == ConnectionState::Connected; });
            wait([] { return GetDeviceStatus().physicalButtonsAvailable; });
            CHECK(GetDeviceStatus().inputMonitorPackets > 0);
            CHECK(GetDeviceStatus().inputMonitorLastPacketBytes == 20);
            CHECK(GetDeviceStatus().inputMonitorPort < 32768);
            CHECK(GetDeviceStatus().moveRequests == 0);
            for (int source = 0; source < 4; ++source) for (int mode = 0; mode < 2; ++mode) {
                auto settings = ResetFixture();
                g::menuOpen = true;
                settings.weaponProfiles[1].aimDamageCheck = false;
                fixture::geometryAvailable = false;
                const int key = source == 1 ? 0x75 : source == 2 ? 0x10 : 0x06;
                settings.weaponProfiles[1].aimWindMouse = true;
                settings.aimKey = key; settings.aimActivationMode = mode;
                fixture::keyboardAvailable = source == 3;
                const auto press = [&](bool down) {
                    if (source == 3) fixture::keyDown = down;
                    else if (source == 0) server.buttons = down ? 0x10 : 0;
                    else if (source == 1) server.key = down ? 0x3F : 0;
                    else server.modifiers = down ? 0x20 : 0;
                    wait([&] {
                        const auto value = ReadActivationKeyState(key);
                        return value.available && value.down == down;
                    });
                };
                const auto frame = [&] { fixture::snapshot.sampledAtUs = fixture::DataNowUs(); Tick(settings); };
                press(false);
                if (source == 3) std::this_thread::sleep_for(std::chrono::milliseconds(270));
                frame();
                const int movesBefore = server.moves.load();
                press(true); frame();
                CHECK(s_status.aimKeyDown);
                CHECK(s_status.phase == target::RuntimePhase::Tracking);
                wait([&] { return server.moves.load() > movesBefore; });
                press(false); frame();
                if (mode == 1) {
                    CHECK(s_status.aimKeyDown);
                    press(true); frame();
                    CHECK(!s_status.aimKeyDown);
                    press(false); frame();
                } else CHECK(!s_status.aimKeyDown);
                const auto queued = GetDeviceStatus().moveRequests;
                for (int i = 0; i < 5; ++i) frame();
                CHECK(GetDeviceStatus().moveRequests == queued);
            }

            auto settings = ResetFixture();
            settings.aimbotEnabled = false; settings.triggerbotEnabled = true;
            settings.weaponProfiles[1].aimWindMouse = true;
            settings.triggerActivationMode = 0; settings.triggerKey = 0x06;
            settings.triggerDelayMs = 0; settings.triggerAutoShot = false;
            fixture::keyboardAvailable = false;
            auto& head = fixture::snapshot.players[1].hitboxes[0];
            head.start.y = head.center.y = head.end.y = 0;
            const int downsBefore = server.downs.load();
            const int upsBefore = server.ups.load();
            server.buttons = 0x10;
            wait([] { return ReadActivationKeyState(0x06).down; });
            for (int i = 0; i < 50 && server.downs.load() == downsBefore; ++i) {
                fixture::snapshot.sampledAtUs = fixture::DataNowUs(); Tick(settings);
                s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            CHECK(server.downs.load() == downsBefore + 1);
            CHECK(s_runtime.trigger.assist.windUpdatedAtUs == 0);
            wait([&] { return server.ups.load() > upsBefore; });
            for (int i = 0; i < 30; ++i) {
                fixture::snapshot.sampledAtUs = fixture::DataNowUs(); Tick(settings);
            }
            CHECK(server.downs.load() == downsBefore + 1);
            server.buttons = 0;
            wait([] { return !ReadActivationKeyState(0x06).down; });

            CHECK(RequestLeftButton(true));
            wait([] { return GetLeftClickStatus().outputDown; });
            const auto batches = GetDeviceStatus().moveCompletions;
            CHECK(RequestMove(3, -2));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            CHECK(GetDeviceStatus().moveCompletions == batches);
            CHECK(RequestLeftButton(false));
            wait([] { return !GetLeftClickStatus().outputDown; });
            wait([&] { return GetDeviceStatus().moveCompletions > batches; });
            CHECK(server.inconsistentButtons.load() == 0);

            server.monitorEnabled = false;
            wait([] { return !GetDeviceStatus().physicalButtonsAvailable; });
            settings = ResetFixture();
            settings.aimKey = 0x06; settings.aimActivationMode = 0;
            fixture::keyDown = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(270));
            Tick(settings);
            const auto beforeFallback = GetDeviceStatus().moveRequests;
            fixture::keyDown = true; Tick(settings);
            CHECK(s_status.aimKeyDown);
            CHECK(GetDeviceStatus().moveRequests > beforeFallback);
            fixture::keyboardAvailable = false; Tick(settings);
            CHECK(!s_status.aimKeyDown);
            CHECK(GetDeviceStatus().state == ConnectionState::Connected);

            const auto drain = [&] {
                wait([] {
                    const auto status = GetDeviceStatus();
                    return !status.moveInFlight && !status.movePending;
                });
            };
            const auto blockMove = [&] {
                drain();
                server.releaseMoveAck = false;
                server.delayNextMoveAck = true;
                CHECK(RequestMove(11, -12));
                wait([&] { return server.moveAckBlocked.load(); });
            };
            blockMove();
            const int beforeLatest = server.moves.load();
            const auto replacedBefore = GetDeviceStatus().moveReplacements;
            for (int i = 1; i <= 100; ++i) CHECK(RequestMove(i, -i));
            server.releaseMoveAck = true;
            drain();
            CHECK(server.moves.load() == beforeLatest + 1);
            CHECK(server.lastMoveX.load() == 100 && server.lastMoveY.load() == -100);
            CHECK(GetDeviceStatus().moveReplacements == replacedBefore + 99);

            blockMove();
            const int beforeExpiry = server.moves.load();
            const auto expiredBefore = GetDeviceStatus().moveExpirations;
            CHECK(RequestMove(79, -80, 1000));
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            server.releaseMoveAck = true;
            drain();
            CHECK(server.moves.load() == beforeExpiry);
            CHECK(GetDeviceStatus().moveExpirations == expiredBefore + 1);

            blockMove();
            const int beforeBusyDowns = server.downs.load();
            CHECK(TryRequestLeftClick(18) == ClickRequestResult::Busy);
            CHECK(!GetLeftClickStatus().active);
            server.releaseMoveAck = true;
            drain();
            CHECK(server.downs.load() == beforeBusyDowns);

            server.releaseMoveAck = false;
            server.delayNextProbeAck = true;
            wait([&] { return server.moveAckBlocked.load() && GetDeviceStatus().probeInFlight; });
            CHECK(TryRequestLeftClick(18) == ClickRequestResult::Busy);
            CHECK(!GetLeftClickStatus().active);
            server.releaseMoveAck = true;
            wait([] { return !GetDeviceStatus().probeInFlight; });

            for (int cancellation = 0; cancellation < 7; ++cancellation) {
                settings = ResetFixture();
                if (cancellation == 6) fixture::geometryAvailable = false;
                settings.weaponProfiles[1].aimWindMouse = true;
                settings.aimKey = 0x06;
                fixture::keyDown = false;
                Tick(settings);
                fixture::keyDown = true;
                Tick(settings);
                drain();
                fixture::keyDown = false;
                Tick(settings);
                blockMove();
                fixture::snapshot.sampledAtUs = fixture::DataNowUs();
                const auto beforeBlocked = GetDeviceStatus().moveRequests;
                Tick(settings);
                CHECK(!GetDeviceStatus().movePending);
                CHECK(GetDeviceStatus().moveRequests == beforeBlocked);
                CHECK(s_status.aimMove.reason == target::MoveBlockReason::OutputBusy);
                CHECK(RequestMove(13, -14));
                CHECK(GetDeviceStatus().movePending);
                const int beforeCancel = server.moves.load();
                const auto cancelledBefore = GetDeviceStatus().moveCancellations;
                if (cancellation == 0) fixture::keyDown = true;
                if (cancellation == 2) settings.enabled = false;
                if (cancellation == 3) fixture::haveSnapshot = false;
                if (cancellation == 4) fixture::snapshot.players[1].health = 0;
                if (cancellation == 5) fixture::snapshot.sceneSerial++;
                if (cancellation == 6) {
                    fixture::geometryAvailable = true;
                    fixture::wallBlocks = true;
                }
                Tick(settings, cancellation == 1);
                CHECK(!GetDeviceStatus().movePending);
                CHECK(GetDeviceStatus().moveCancellations > cancelledBefore);
                server.releaseMoveAck = true;
                drain();
                CHECK(server.moves.load() == beforeCancel);
            }
            CHECK(RequestDisconnect());
            CHECK(TryRequestLeftClick(18) == ClickRequestResult::Unavailable);
            fixture::keyDown = false;
            server.monitorEnabled = true;
        }
        Shutdown();
    }
    WSACleanup();
    if (failures) { std::cerr << failures << " Target/input integration failures\n"; return 1; }
    std::cout << "Target/input integration passed: KMBox/Ferrum UDP, Hold/Toggle, aim/trigger, 100-to-1 coalescing, expiry, 7 cancellation paths including geometry readiness, busy movement/probe, release priority, missing monitor.\n";
    return 0;
}
