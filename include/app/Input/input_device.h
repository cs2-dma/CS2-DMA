#pragma once

#include "app/Input/input_device_policy.h"

#include <cstdint>
#include <string>

namespace app::input
{
    enum class ConnectionState : uint8_t
    {
        Disconnected,
        Connecting,
        Testing,
        Connected,
        Error,
    };

    enum class DeviceError : uint8_t
    {
        None,
        NotFound,
        PortUnavailable,
        ConfigureFailed,
        HandshakeFailed,
        IoFailure,
        Disconnected,
        InvalidConfiguration,
        NetworkTimeout,
        Unsupported,
    };

    struct KmBoxNetConfig
    {
        std::string host;
        uint16_t port = 0;
        std::string hardwareKey;

        bool operator==(const KmBoxNetConfig&) const = default;
    };

    struct DeviceStatus
    {
        DeviceKind selected = DeviceKind::None;
        ConnectionState state = ConnectionState::Disconnected;
        DeviceError error = DeviceError::None;
        std::string port;
        uint32_t systemError = 0;
        uint8_t physicalButtonMask = 0;
        bool physicalButtonsAvailable = false;
        PhysicalKeyboardState physicalKeyboard;
        uint64_t physicalInputUpdatedAtMs = 0;
        uint16_t inputMonitorPort = 0;
        uint64_t inputMonitorPackets = 0;
        uint64_t inputMonitorRejectedPackets = 0;
        uint64_t inputMonitorReceivedBytes = 0;
        uint32_t inputMonitorLastPacketBytes = 0;
        uint32_t inputMonitorError = 0;
        uint64_t moveRequests = 0;
        uint64_t moveCompletions = 0;
        uint64_t moveReplacements = 0;
        uint64_t moveCancellations = 0;
        uint64_t moveExpirations = 0;
        bool movePending = false;
        bool moveInFlight = false;
        bool probeInFlight = false;
        uint64_t lastMoveQueueAgeUs = 0;
    };

    inline KeyState ReadDeviceActivationKeyState(const DeviceStatus& device, int virtualKey)
    {
        if (device.state != ConnectionState::Connected) return {};
        const uint8_t mask = VirtualKeyToMouseButtonMask(virtualKey);
        if (mask != 0)
            return {device.physicalButtonsAvailable, (device.physicalButtonMask & mask) != 0};
        return ReadPhysicalKeyboardKey(device.physicalKeyboard, virtualKey);
    }

    class ActivationKeyRouter
    {
    public:
        KeyState Read(const DeviceStatus& device, int key, KeyState primary, uint64_t nowMs)
        {
            if (key < 1 || key > 0xFE) return {};
            const auto hardware = ReadDeviceActivationKeyState(device, key);
            if (!IsHardwareMouseVirtualKey(key))
                return SelectKeyboardActivationKeyState(hardware, primary);
            if (key == 0x01 || device.state != ConnectionState::Connected)
                return SelectActivationKeyState(hardware, primary);
            return mouse_[key].Read(hardware, primary, nowMs);
        }

    private:
        std::array<MouseActivationRouter, 7> mouse_ = {};
    };

    struct LeftClickTiming
    {
        bool calibrated = false;
        float dispatchLatencyMs = 0.0f;
        float dispatchJitterMs = 0.0f;
        uint64_t samples = 0;
    };

    struct LeftClickStatus
    {
        // True from reservation until the worker has either completed UP or
        // cancelled/failed the pulse. This is the authoritative lifetime;
        // callers must not infer completion from their own wall-clock timer.
        bool active = false;
        bool outputDown = false;
        uint64_t token = 0;
    };

    enum class ClickRequestResult : uint8_t
    {
        Queued,
        Busy,
        ManualInput,
        Unavailable,
    };

    void SetSelectedDevice(DeviceKind kind);
    void SetKmBoxNetConfig(KmBoxNetConfig config);
    DeviceStatus GetDeviceStatus();
    LeftClickTiming GetLeftClickTiming();
    LeftClickStatus GetLeftClickStatus();
    bool RequestConnectAndTest();
    bool RequestDisconnect();
    bool RequestMovementTest();
    bool RequestMove(int deltaX, int deltaY, uint64_t validForUs = kMaximumQueuedMoveAgeUs);
    void CancelPendingMoves();
    bool RequestLeftButton(bool pressed);
    // Reserves one worker-owned DOWN/UP pulse. Returns false while another
    // pulse or a left-button transition is queued or in flight.
    bool RequestLeftClick(uint32_t holdMs);
    ClickRequestResult TryRequestLeftClick(uint32_t holdMs, uint64_t validForUs = kMaximumQueuedMoveAgeUs);
    bool IsHardwareKeyDown(int virtualKey);
    KeyState ReadActivationKeyState(int virtualKey);
    bool IsActivationKeyDown(int virtualKey);
    bool IsControlKeyDown(int virtualKey);
    void Shutdown();
}
