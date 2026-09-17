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

    void SetSelectedDevice(DeviceKind kind);
    void SetKmBoxNetConfig(KmBoxNetConfig config);
    DeviceStatus GetDeviceStatus();
    LeftClickTiming GetLeftClickTiming();
    LeftClickStatus GetLeftClickStatus();
    bool RequestConnectAndTest();
    bool RequestDisconnect();
    bool RequestMovementTest();
    bool RequestMove(int deltaX, int deltaY);
    bool RequestLeftButton(bool pressed);
    // Reserves one worker-owned DOWN/UP pulse. Returns false while another
    // pulse or a left-button transition is queued or in flight.
    bool RequestLeftClick(uint32_t holdMs);
    bool IsHardwareKeyDown(int virtualKey);
    KeyState ReadActivationKeyState(int virtualKey);
    bool IsActivationKeyDown(int virtualKey);
    bool IsControlKeyDown(int virtualKey);
    void Shutdown();
}
