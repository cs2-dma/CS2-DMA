#include <source_location>

struct NetworkTargetFixture
{
    SOCKET socket = INVALID_SOCKET;
    uint16_t port = 0;
    std::atomic<bool> running{true};
    std::atomic<bool> monitorEnabled{true};
    std::atomic<bool> eventOnly{true};
    std::atomic<bool> replyEnabled{true};
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
            bool reportSent = false;
            std::array<uint8_t, 20> lastReport = {};
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
                        const auto nextPort = (random >> 16) == 0xAA55 ? static_cast<uint16_t>(random) : 0;
                        if (monitorPort != nextPort) reportSent = false;
                        monitorPort = nextPort;
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
                    } else if (replyEnabled.load()) {
                        sendto(socket, reinterpret_cast<const char*>(bytes.data()), 16, 0,
                            reinterpret_cast<const sockaddr*>(&sender), senderSize);
                    }
                }
                if (moveAckBlocked.load() && releaseMoveAck.load() && replyEnabled.load()) {
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
                    if (!eventOnly.load() || !reportSent || report != lastReport) {
                        sendto(socket, reinterpret_cast<const char*>(report.data()),
                            static_cast<int>(report.size()), 0,
                            reinterpret_cast<const sockaddr*>(&destination), sizeof(destination));
                        reportSent = true;
                        lastReport = report;
                    }
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
    const auto wait = [](auto condition, int timeoutSeconds = 3,
        std::source_location location = std::source_location::current()) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
        while (!condition() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        const bool ready = condition();
        if (!ready) std::cerr << "Network wait timed out at " << location.line() << '\n';
        CHECK(ready);
    };
    const auto tick = [](const g::TargetSettings& settings, bool uiBlocked = false) {
        FreshFrame();
        Tick(settings, uiBlocked);
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
            CHECK(RequestDisconnect());
            server.monitorEnabled = false;
            wait([] { return GetDeviceStatus().state == ConnectionState::Disconnected; });
            CHECK(RequestConnectAndTest());
            wait([] { return GetDeviceStatus().state == ConnectionState::Connected; });
            const auto firstMonitorPort = GetDeviceStatus().inputMonitorPort;
            CHECK(!GetDeviceStatus().physicalButtonsAvailable);
            wait([&] {
                const auto status = GetDeviceStatus();
                return status.inputMonitorPort != 0 && status.inputMonitorPort != firstMonitorPort;
            }, 8);
            CHECK(GetDeviceStatus().state == ConnectionState::Connected);
            server.monitorEnabled = true;
            wait([] { return GetDeviceStatus().physicalButtonsAvailable; });
            {
                auto held = ResetFixture();
                held.aimKey = 0x06;
                held.aimActivationMode = 0;
                fixture::geometryAvailable = false;
                fixture::keyboardAvailable = false;
                server.buttons = 0x10;
                wait([] { return ReadActivationKeyState(0x06).down; });
                const auto pressed = GetDeviceStatus();
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
                while (std::chrono::steady_clock::now() < deadline) {
                    tick(held);
                    CHECK(s_status.aimKeyDown);
                    CHECK(GetDeviceStatus().physicalButtonsAvailable);
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                }
                const auto quiet = GetDeviceStatus();
                CHECK(quiet.inputMonitorPort == pressed.inputMonitorPort);
                CHECK(quiet.inputMonitorPackets == pressed.inputMonitorPackets);
                CHECK(quiet.physicalInputUpdatedAtMs == pressed.physicalInputUpdatedAtMs);
                CHECK(quiet.physicalInputValidatedAtMs > pressed.physicalInputValidatedAtMs);
                CHECK(quiet.moveRequests > pressed.moveRequests);
                server.buttons = 0;
                wait([] { const auto key = ReadActivationKeyState(0x06); return key.available && !key.down; });
                tick(held);
                CHECK(!s_status.aimKeyDown);
            }
            {
                auto toggled = ResetFixture();
                toggled.aimKey = 0x06;
                toggled.aimActivationMode = 1;
                fixture::keyboardAvailable = false;
                tick(toggled);
                server.buttons = 0x10;
                wait([] { return ReadActivationKeyState(0x06).down; });
                tick(toggled);
                CHECK(s_status.aimKeyDown);
                server.buttons = 0;
                wait([] { const auto key = ReadActivationKeyState(0x06); return key.available && !key.down; });
                tick(toggled);
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                tick(toggled);
                CHECK(s_status.aimKeyDown);
                KeyCaptureState capture;
                capture.Begin(ReadControlKeySnapshot());
                CHECK(capture.Observe(ReadControlKeySnapshot()) == 0);
                server.buttons = 0x10;
                wait([] { return ReadActivationKeyState(0x06).down; });
                CHECK(capture.Observe(ReadControlKeySnapshot()) == 0x06);
                CHECK(capture.Observe(ReadControlKeySnapshot()) == 0);
                tick(toggled);
                CHECK(!s_status.aimKeyDown);
                server.buttons = 0;
                wait([] { const auto key = ReadActivationKeyState(0x06); return key.available && !key.down; });
                tick(toggled);
            }
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
                const auto frame = [&] { tick(settings); };
                press(false);
                if (source == 3) std::this_thread::sleep_for(std::chrono::milliseconds(270));
                frame();
                const int movesBefore = server.moves.load();
                press(true); frame();
                CHECK(s_status.aimKeyDown);
                CHECK(s_status.phase == target::RuntimePhase::Tracking);
                wait([&] { frame(); return server.moves.load() > movesBefore; });
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

            {
                wait([] {
                    const auto status = GetDeviceStatus();
                    return !status.moveInFlight && !status.movePending;
                });
                auto continuous = ResetFixture();
                continuous.aimKey = 0x06;
                continuous.aimActivationMode = 0;
                fixture::geometryAvailable = false;
                fixture::keyboardAvailable = false;
                server.buttons = 0x10;
                wait([] { return ReadActivationKeyState(0x06).down; });
                const auto beforeContinuous = GetDeviceStatus().moveCompletions;
                uint64_t appliedToken = 0;
                bool targetMoved = false;
                for (int frame = 0; frame < 128 && GetDeviceStatus().moveCompletions < beforeContinuous + 12; ++frame) {
                    tick(continuous);
                    wait([] {
                        const auto status = GetDeviceStatus();
                        return !status.moveInFlight && !status.movePending;
                    });
                    const auto receipt = GetMoveReceipt(s_outputFeedback.token);
                    if (receipt.state == MoveDeliveryState::Sent && receipt.token != appliedToken) {
                        CHECK(receipt.sampledAtUs >= receipt.sentAtUs);
                        CHECK(receipt.sentAtUs > fixture::snapshot.sampledAtUs + 1000000);
                        const float scale = fixture::snapshot.sensitivity *
                            fixture::snapshot.fovSensitivityAdjust * target::policy::kMouseYawDegrees;
                        fixture::snapshot.viewAngles.x += receipt.y * scale;
                        fixture::snapshot.viewAngles.y -= receipt.x * scale;
                        appliedToken = receipt.token;
                    }
                    if (!targetMoved && frame > 0 && !s_outputFeedback.diagnostics.pending &&
                        s_status.aimMove.angularErrorDegrees < 0.01f) {
                        auto& movingPlayer = fixture::snapshot.players[1];
                        auto& movingHead = movingPlayer.hitboxes[0];
                        movingHead.start.y += 40;
                        movingHead.center.y += 40;
                        movingHead.end.y += 40;
                        for (auto& bone : movingPlayer.bones) bone.y += 40;
                        targetMoved = true;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(4));
                }
                CHECK(targetMoved);
                if (GetDeviceStatus().moveCompletions <= beforeContinuous + 8) {
                    std::cerr << "Continuous feedback: moves=" << GetDeviceStatus().moveCompletions - beforeContinuous
                        << " samples=" << s_outputFeedback.diagnostics.latencySamples
                        << " pending=" << s_outputFeedback.diagnostics.pending
                        << " age=" << s_outputFeedback.diagnostics.ageUs
                        << " phase=" << static_cast<int>(s_status.phase)
                        << " reason=" << static_cast<int>(s_status.aimMove.reason)
                        << " expected=" << s_outputFeedback.diagnostics.expectedYaw
                        << " observed=" << s_outputFeedback.diagnostics.observedYaw
                        << " view=" << fixture::snapshot.viewAngles.y
                        << " angular_error=" << s_status.aimMove.angularErrorDegrees
                        << " gain=" << s_status.aimMove.assistanceGain << '\n';
                }
                CHECK(GetDeviceStatus().moveCompletions > beforeContinuous + 8);
                CHECK(s_outputFeedback.diagnostics.latencySamples > 8);
                CHECK(s_outputFeedback.diagnostics.timeouts == 0);
                server.buttons = 0;
                wait([] { return !ReadActivationKeyState(0x06).down; });
                tick(continuous);
            }

            for (const int mode : {0, 1}) {
                wait([] { const auto value = GetDeviceStatus(); return !value.movePending && !value.moveInFlight; });
                server.buttons = 0;
                wait([] { const auto value = ReadActivationKeyState(0x06); return value.available && !value.down; });
                auto handoff = ResetFixture();
                handoff.aimKey = 0x06;
                handoff.aimActivationMode = mode;
                handoff.aimVisibleOnly = false;
                handoff.weaponProfiles[1].aimDamageCheck = false;
                handoff.weaponProfiles[1].aimSmoothing = 50;
                fixture::geometryAvailable = false;
                fixture::keyboardAvailable = false;
                auto& snap = fixture::snapshot;
                snap.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
                tick(handoff);
                server.buttons = 0x11;
                wait([] { return ReadActivationKeyState(0x06).down; });
                tick(handoff);
                CHECK(s_status.aimKeyDown);
                const auto heldPacketCount = GetDeviceStatus().inputMonitorPackets;
                if (mode == 1) {
                    server.buttons = 1;
                    wait([] { return !ReadActivationKeyState(0x06).down; });
                    tick(handoff);
                    CHECK(s_status.aimKeyDown);
                }
                uint64_t applied = 0;
                const auto apply = [&] {
                    wait([] { const auto value = GetDeviceStatus(); return !value.movePending && !value.moveInFlight; });
                    const auto receipt = GetMoveReceipt(s_outputFeedback.token);
                    if (receipt.state == MoveDeliveryState::Sent && receipt.token != applied) {
                        snap.viewAngles.x += receipt.y * 0.022f;
                        snap.viewAngles.y -= receipt.x * 0.022f;
                        applied = receipt.token;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(4));
                };
                snap.localShotsFired = 3;
                snap.localAimPunch = {-0.5f, 0.25f, 0};
                tick(handoff); apply();
                const auto original = snap.players[1];
                snap.players[1].health = 0;
                snap.localShotsFired = 4;
                snap.localAimPunch = {-1, 0.3f, 0};
                const auto beforeGap = GetDeviceStatus().moveRequests;
                for (int frame = 0; frame < 4; ++frame) {
                    tick(handoff); apply();
                    CHECK(s_status.aimKeyDown && s_status.targetSlot == -1);
                }
                CHECK(GetDeviceStatus().moveRequests > beforeGap);
                auto& second = snap.players[2];
                second = original;
                second.pawn += 0x1000;
                second.pawnHandle += 1;
                for (auto& bone : second.bones) bone.y += 20;
                second.hitboxes[0].start.y += 20;
                second.hitboxes[0].center.y += 20;
                second.hitboxes[0].end.y += 20;
                const auto beforeSecond = GetDeviceStatus().moveRequests;
                for (int frame = 0; frame < 64; ++frame) {
                    tick(handoff); apply();
                    CHECK(s_status.aimKeyDown && s_runtime.aim.targetSlot == 2);
                    CHECK(std::fabs(snap.viewAngles.x + snap.localAimPunch.x * 2) < 0.04f);
                }
                CHECK(GetDeviceStatus().moveRequests > beforeSecond);
                CHECK(s_outputFeedback.diagnostics.timeouts == 0);
                if (mode == 0) CHECK(GetDeviceStatus().inputMonitorPackets == heldPacketCount);
                server.buttons = mode == 1 ? 0x11 : 0;
                wait([&] { return ReadActivationKeyState(0x06).down == (mode == 1); });
                tick(handoff);
                CHECK(!s_status.aimKeyDown);
                server.buttons = 0;
                wait([] { return !ReadActivationKeyState(0x06).down; });
                tick(handoff);
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
                tick(settings);
                s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            CHECK(server.downs.load() == downsBefore + 1);
            CHECK(s_runtime.trigger.assist.windUpdatedAtUs == 0);
            wait([&] { return server.ups.load() > upsBefore; });
            for (int i = 0; i < 30; ++i) {
                tick(settings);
            }
            CHECK(server.downs.load() == downsBefore + 1);
            server.buttons = 0;
            wait([] { return !ReadActivationKeyState(0x06).down; });

            wait([] {
                const auto output = GetDeviceStatus();
                const auto click = GetLeftClickStatus();
                return !output.movePending && !output.moveInFlight && !output.probeInFlight &&
                    !click.outputDown && !click.active;
            });
            CHECK(RequestLeftButton(true));
            wait([] { return GetLeftClickStatus().outputDown; });
            const auto batches = GetDeviceStatus().moveCompletions;
            const auto heldMove = RequestTrackedMove(3, -2);
            CHECK(heldMove != 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            CHECK(GetDeviceStatus().moveCompletions == batches);
            CHECK(RequestLeftButton(false));
            wait([] { return !GetLeftClickStatus().outputDown; });
            wait([&] {
                const auto state = GetMoveReceipt(heldMove).state;
                return state != MoveDeliveryState::Queued && state != MoveDeliveryState::Dispatching;
            });
            CHECK(GetMoveReceipt(heldMove).state == MoveDeliveryState::Sent);
            CHECK(GetDeviceStatus().moveCompletions > batches);
            CHECK(server.inconsistentButtons.load() == 0);

            settings = ResetFixture();
            settings.aimKey = 0x06;
            settings.aimActivationMode = 1;
            fixture::primaryKey = 0x06;
            fixture::keyboardAvailable = true;
            fixture::keyDown = false;
            server.buttons = 0;
            wait([] { const auto key = ReadActivationKeyState(0x06); return key.available && !key.down; });
            tick(settings);
            fixture::keyDown = true;
            server.buttons = 0x10;
            wait([] { return GetDeviceStatus().physicalButtonMask == 0x10; });
            tick(settings);
            CHECK(s_status.aimKeyDown);
            server.buttons = 0;
            wait([] { return GetDeviceStatus().physicalButtonMask == 0; });
            tick(settings);
            CHECK(s_status.aimKeyDown);
            wait([] { const auto status = GetDeviceStatus(); return !status.movePending && !status.moveInFlight; });
            CHECK(RequestDisconnect());
            wait([] { return GetDeviceStatus().state == ConnectionState::Disconnected; });
            server.monitorEnabled = false;
            CHECK(RequestConnectAndTest());
            wait([] { return GetDeviceStatus().state == ConnectionState::Connected; });
            wait([] { return !GetDeviceStatus().physicalButtonsAvailable; });
            settings = ResetFixture();
            settings.aimKey = 0x06;
            settings.aimActivationMode = 1;
            fixture::keyboardAvailable = true;
            fixture::keyDown = false;
            tick(settings);
            CHECK(!s_status.aimKeyDown);
            fixture::keyDown = true;
            tick(settings);
            CHECK(s_status.aimKeyDown);
            fixture::keyDown = false;
            tick(settings);
            CHECK(s_status.aimKeyDown);
            fixture::keyDown = true;
            tick(settings);
            CHECK(!s_status.aimKeyDown);
            settings = ResetFixture();
            settings.aimKey = 0x06; settings.aimActivationMode = 0;
            fixture::keyDown = false;
            std::this_thread::sleep_for(std::chrono::milliseconds(270));
            tick(settings);
            const auto beforeFallback = GetDeviceStatus().moveRequests;
            fixture::keyDown = true; tick(settings);
            CHECK(s_status.aimKeyDown);
            CHECK(GetDeviceStatus().moveRequests > beforeFallback);
            fixture::keyboardAvailable = false; tick(settings);
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
            server.monitorEnabled = true;
            server.buttons = 0x10;
            fixture::keyboardAvailable = false;
            wait([] { return GetDeviceStatus().physicalButtonsAvailable && ReadActivationKeyState(0x06).down; });
            blockMove();
            const auto releaseAt = std::chrono::steady_clock::now();
            server.buttons = 0;
            wait([] { const auto key = ReadActivationKeyState(0x06); return key.available && !key.down; });
            CHECK(std::chrono::steady_clock::now() - releaseAt < std::chrono::milliseconds(100));
            CHECK(GetDeviceStatus().moveInFlight);
            server.buttons = 1;
            wait([] { return GetDeviceStatus().physicalButtonMask == 1; });
            CHECK(TryRequestLeftClick(18) == ClickRequestResult::ManualInput);
            CHECK(GetDeviceStatus().moveInFlight);
            server.buttons = 0;
            wait([] { return GetDeviceStatus().physicalButtonMask == 0; });
            server.releaseMoveAck = true;
            drain();
            blockMove();
            const int beforeLatest = server.moves.load();
            const auto replacedBefore = GetDeviceStatus().moveReplacements;
            uint64_t previous = 0, last = 0;
            for (int i = 1; i <= 100; ++i) {
                last = RequestTrackedMove(i, -i);
                CHECK(last != 0 && GetMoveReceipt(last).state == MoveDeliveryState::Queued);
                if (previous) CHECK(GetMoveReceipt(previous).state == MoveDeliveryState::Replaced);
                previous = last;
            }
            server.releaseMoveAck = true;
            drain();
            const auto delivered = GetMoveReceipt(last);
            CHECK(delivered.state == MoveDeliveryState::Sent && delivered.sentAtUs >= delivered.dispatchAtUs);
            CHECK(delivered.dispatchAtUs >= delivered.queuedAtUs && delivered.x == 100 && delivered.y == -100);
            CHECK(server.moves.load() == beforeLatest + 1);
            CHECK(server.lastMoveX.load() == 100 && server.lastMoveY.load() == -100);
            CHECK(GetDeviceStatus().moveReplacements == replacedBefore + 99);

            blockMove();
            const int beforeExpiry = server.moves.load();
            const auto expiredBefore = GetDeviceStatus().moveExpirations;
            const auto expiredToken = RequestTrackedMove(79, -80, 1000);
            CHECK(expiredToken != 0);
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            server.releaseMoveAck = true;
            drain();
            CHECK(server.moves.load() == beforeExpiry);
            CHECK(GetDeviceStatus().moveExpirations == expiredBefore + 1);
            CHECK(GetMoveReceipt(expiredToken).state == MoveDeliveryState::Expired);
            blockMove();
            const auto cancelledToken = RequestTrackedMove(10, -10);
            CancelPendingMoves();
            CHECK(GetMoveReceipt(cancelledToken).state == MoveDeliveryState::Cancelled);
            server.releaseMoveAck = true;
            drain();

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
                tick(settings);
                fixture::keyDown = true;
                tick(settings);
                drain();
                fixture::keyDown = false;
                tick(settings);
                blockMove();
                const auto beforeBlocked = GetDeviceStatus().moveRequests;
                tick(settings);
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
                tick(settings, cancellation == 1);
                CHECK(!GetDeviceStatus().movePending);
                CHECK(GetDeviceStatus().moveCancellations > cancelledBefore);
                server.releaseMoveAck = true;
                drain();
                CHECK(server.moves.load() == beforeCancel);
            }
            server.monitorEnabled = false;
            server.replyEnabled = false;
            fixture::keyboardAvailable = false;
            CHECK(RequestMove(3, 1));
            wait([] { return GetDeviceStatus().state == ConnectionState::Error; }, 4);
            CHECK(!ReadActivationKeyState(0x06).available);
            CHECK(!GetDeviceStatus().physicalButtonsAvailable);
            CHECK(RequestDisconnect());
            CHECK(TryRequestLeftClick(18) == ClickRequestResult::Unavailable);
            fixture::keyDown = false;
            server.monitorEnabled = true;
            server.replyEnabled = true;
        }
        Shutdown();
    }
    WSACleanup();
    if (failures) { std::cerr << failures << " Target/input integration failures\n"; return 1; }
    std::cout << "Target/input integration passed: KMBox/Ferrum event-only UDP, 6s quiet Hold, release during blocked ACK, connection-loss reset, continuous feedback across independent clocks, Hold/Toggle, aim/trigger, 100-to-1 coalescing, expiry, 7 cancellation paths including geometry readiness, busy movement/probe, release priority, missing monitor.\n";
    return 0;
}
