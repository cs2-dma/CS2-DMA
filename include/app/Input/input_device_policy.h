#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string_view>
#include <vector>

namespace app::input
{
    struct KeyState
    {
        bool available = false;
        bool down = false;
    };

    // A known physical release wins over a delayed OS/DMA sample.
    inline constexpr KeyState SelectActivationKeyState(KeyState hardware, KeyState primary)
    {
        return hardware.available ? hardware : primary;
    }

    inline constexpr bool IsLocalControlKeyDown(int key, short asyncState)
    {
        return key > 0 && key <= 0xFE && (static_cast<unsigned short>(asyncState) & 0x8000u) != 0;
    }

    enum class DeviceKind : int
    {
        None = 0,
        Makcu = 1,
        KmBox = 2,
        KmBoxNet = 3,
        FerrumOne = 4, // Ferrum App's KMBox-compatible Net API; keep persisted IDs stable.
    };

    struct MouseDelta
    {
        int16_t x = 0;
        int16_t y = 0;
    };

    inline constexpr uint8_t VirtualKeyToMouseButtonMask(int virtualKey)
    {
        switch (virtualKey) {
        case 0x01: return 0x01; // VK_LBUTTON
        case 0x02: return 0x02; // VK_RBUTTON
        case 0x04: return 0x04; // VK_MBUTTON
        case 0x05: return 0x08; // VK_XBUTTON1
        case 0x06: return 0x10; // VK_XBUTTON2
        default: return 0;
        }
    }

    inline constexpr bool IsHardwareMouseVirtualKey(int virtualKey)
    {
        return VirtualKeyToMouseButtonMask(virtualKey) != 0;
    }

    class MakcuButtonStreamParser
    {
    public:
        bool Consume(uint8_t value, uint8_t* buttonMask = nullptr) noexcept
        {
            switch (prefixState_) {
            case 0:
                prefixState_ = value == 'k' ? 1 : 0;
                return false;
            case 1:
                prefixState_ = value == 'm' ? 2 : (value == 'k' ? 1 : 0);
                return false;
            case 2:
                prefixState_ = value == '.' ? 3 : (value == 'k' ? 1 : 0);
                return false;
            default:
                prefixState_ = value == 'k' ? 1 : 0;
                if (value > 0x1Fu)
                    return false;
                mask_ = static_cast<uint8_t>(value & 0x1Fu);
                if (buttonMask)
                    *buttonMask = mask_;
                return true;
            }
        }

        void Reset() noexcept
        {
            prefixState_ = 0;
            mask_ = 0;
        }

        uint8_t Mask() const noexcept
        {
            return mask_;
        }

    private:
        uint8_t prefixState_ = 0;
        uint8_t mask_ = 0;
    };

    inline constexpr int kMovementTestRadius = 150;
    inline constexpr int kMovementTestSegments = 72;
    inline constexpr int kMovementTestStepDelayMs = 7;

    inline constexpr bool IsValidDeviceKind(int value)
    {
        return value >= static_cast<int>(DeviceKind::None) &&
               value <= static_cast<int>(DeviceKind::FerrumOne);
    }

    inline constexpr DeviceKind SanitizeDeviceKind(int value)
    {
        return IsValidDeviceKind(value)
            ? static_cast<DeviceKind>(value)
            : DeviceKind::None;
    }

    inline constexpr bool IsSelectableDeviceKind(int value)
    {
        return value >= static_cast<int>(DeviceKind::Makcu) &&
               value <= static_cast<int>(DeviceKind::FerrumOne);
    }

    inline constexpr DeviceKind SanitizeSelectableDeviceKind(int value)
    {
        return IsSelectableDeviceKind(value)
            ? static_cast<DeviceKind>(value)
            : DeviceKind::Makcu;
    }

    inline constexpr bool IsNetworkDeviceKind(DeviceKind kind)
    {
        return kind == DeviceKind::KmBoxNet || kind == DeviceKind::FerrumOne;
    }

    inline constexpr const char* DeviceKindLabel(DeviceKind kind)
    {
        switch (kind) {
        case DeviceKind::Makcu: return "MAKCU";
        case DeviceKind::KmBox: return "KMBox Serial";
        case DeviceKind::KmBoxNet: return "KMBox Network";
        case DeviceKind::FerrumOne: return "Ferrum One (App / NET)";
        default: return "None";
        }
    }

    inline constexpr const char* DeviceKindTransportLabel(DeviceKind kind)
    {
        switch (kind) {
        case DeviceKind::Makcu: return "USB serial | Automatic port detection";
        case DeviceKind::KmBox: return "USB serial | B / B+ / B Pro / NB (B mode)";
        case DeviceKind::KmBoxNet:
            return "UDP network | NET / NET+ / NetPro8K / NB / AI (NET mode)";
        case DeviceKind::FerrumOne:
            return "UDP network | Ferrum App required";
        default: return "";
        }
    }

    inline constexpr wchar_t LowerAscii(wchar_t value)
    {
        return value >= L'A' && value <= L'Z'
            ? static_cast<wchar_t>(value + (L'a' - L'A'))
            : value;
    }

    inline bool ContainsAsciiCaseInsensitive(
        std::wstring_view value,
        std::wstring_view needle)
    {
        if (needle.empty())
            return true;
        if (needle.size() > value.size())
            return false;
        for (size_t start = 0; start + needle.size() <= value.size(); ++start) {
            bool match = true;
            for (size_t i = 0; i < needle.size(); ++i) {
                if (LowerAscii(value[start + i]) != LowerAscii(needle[i])) {
                    match = false;
                    break;
                }
            }
            if (match)
                return true;
        }
        return false;
    }

    inline bool IsMakcuHardwareId(std::wstring_view hardwareId)
    {
        return ContainsAsciiCaseInsensitive(
            hardwareId,
            L"vid_1a86&pid_55d3");
    }

    inline bool IsKmBoxSerialHardwareId(std::wstring_view hardwareId)
    {
        return ContainsAsciiCaseInsensitive(
            hardwareId,
            L"vid_1a86&pid_7523");
    }

    inline bool IsMakcuVersionResponse(std::string_view response)
    {
        return response.find("km.MAKCU") != std::string_view::npos;
    }

    inline bool IsSuccessfulKmCommandResponse(std::string_view response)
    {
        return response.find(">>>") != std::string_view::npos &&
               response.find("Traceback") == std::string_view::npos &&
               response.find("Error") == std::string_view::npos;
    }

    inline bool ParseKmBoxHardwareKey(
        std::string_view text,
        uint32_t* value)
    {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
            text.remove_prefix(1);
        while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
            text.remove_suffix(1);
        if (text.starts_with("0x") || text.starts_with("0X"))
            text.remove_prefix(2);
        if (text.empty() || text.size() > 8)
            return false;

        uint32_t parsed = 0;
        const auto result = std::from_chars(
            text.data(),
            text.data() + text.size(),
            parsed,
            16);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
            return false;
        if (value)
            *value = parsed;
        return true;
    }

    inline bool IsValidIpv4Address(std::string_view text)
    {
        int componentCount = 0;
        size_t start = 0;
        while (start < text.size()) {
            const size_t end = text.find('.', start);
            const size_t length =
                (end == std::string_view::npos ? text.size() : end) - start;
            if (length == 0 || length > 3)
                return false;
            int component = 0;
            for (size_t index = start; index < start + length; ++index) {
                if (text[index] < '0' || text[index] > '9')
                    return false;
                component = component * 10 + (text[index] - '0');
            }
            if (component > 255)
                return false;
            ++componentCount;
            if (end == std::string_view::npos)
                break;
            start = end + 1;
        }
        return componentCount == 4;
    }

    inline bool IsValidKmBoxNetworkConfig(
        std::string_view host,
        uint32_t port,
        std::string_view hardwareKey,
        uint32_t* parsedHardwareKey = nullptr)
    {
        uint32_t parsed = 0;
        if (!IsValidIpv4Address(host) ||
            port == 0 ||
            port > 65535 ||
            !ParseKmBoxHardwareKey(hardwareKey, &parsed)) {
            return false;
        }
        if (parsedHardwareKey)
            *parsedHardwareKey = parsed;
        return true;
    }

    inline std::vector<MouseDelta> BuildCircularTestPath(
        int radius = kMovementTestRadius,
        int segments = kMovementTestSegments)
    {
        radius = std::clamp(radius, 4, 640);
        segments = std::clamp(segments, 12, 128);

        std::vector<MouseDelta> path;
        path.reserve(static_cast<size_t>(segments) + 2u);

        int previousX = 0;
        int previousY = 0;
        constexpr double kTau = 2.0 * std::numbers::pi_v<double>;
        for (int i = 0; i <= segments; ++i) {
            const double angle = kTau * static_cast<double>(i) /
                                 static_cast<double>(segments);
            const int x = static_cast<int>(std::lround(
                static_cast<double>(radius) * std::cos(angle)));
            const int y = static_cast<int>(std::lround(
                static_cast<double>(radius) * std::sin(angle)));
            path.push_back(MouseDelta {
                static_cast<int16_t>(x - previousX),
                static_cast<int16_t>(y - previousY)
            });
            previousX = x;
            previousY = y;
        }
        path.push_back(MouseDelta {
            static_cast<int16_t>(-previousX),
            static_cast<int16_t>(-previousY)
        });
        return path;
    }
}
