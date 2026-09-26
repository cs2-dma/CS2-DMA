#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace app::input
{
    struct KeyState
    {
        bool available = false;
        bool down = false;
    };

    struct PhysicalKeyboardState
    {
        bool available = false;
        uint8_t modifiers = 0;
        std::array<uint8_t, 10> keys = {};
    };

    inline constexpr uint64_t kNetworkInputMaxAgeMs = 500;

    inline constexpr bool IsNetworkInputFresh(uint64_t sampledAtMs, uint64_t nowMs)
    {
        return sampledAtMs != 0 && nowMs >= sampledAtMs &&
            nowMs - sampledAtMs <= kNetworkInputMaxAgeMs;
    }

    inline bool ParseNetworkInputReport(std::span<const uint8_t> bytes,
        uint8_t& mouseButtons, PhysicalKeyboardState& keyboard)
    {
        if (bytes.size() != 20)
            return false;
        mouseButtons = bytes[1] & 0x1F;
        keyboard = {};
        keyboard.modifiers = bytes[9];
        std::copy_n(bytes.begin() + 10, keyboard.keys.size(), keyboard.keys.begin());
        keyboard.available = std::none_of(keyboard.keys.begin(), keyboard.keys.end(),
            [](uint8_t key) { return key >= 1 && key <= 3; });
        return true;
    }

    inline constexpr uint8_t VirtualKeyToHidUsage(int key)
    {
        if (key >= 0x41 && key <= 0x5A) return static_cast<uint8_t>(key - 0x41 + 4);
        if (key >= 0x31 && key <= 0x39) return static_cast<uint8_t>(key - 0x31 + 0x1E);
        if (key >= 0x70 && key <= 0x7B) return static_cast<uint8_t>(key - 0x70 + 0x3A);
        if (key >= 0x7C && key <= 0x87) return static_cast<uint8_t>(key - 0x7C + 0x68);
        if (key >= 0x61 && key <= 0x69) return static_cast<uint8_t>(key - 0x61 + 0x59);
        switch (key) {
        case 0x30: return 0x27;
        case 0x0D: return 0x28; case 0x1B: return 0x29;
        case 0x08: return 0x2A; case 0x09: return 0x2B; case 0x20: return 0x2C;
        case 0xBD: return 0x2D; case 0xBB: return 0x2E;
        case 0xDB: return 0x2F; case 0xDD: return 0x30; case 0xDC: return 0x31;
        case 0xBA: return 0x33; case 0xDE: return 0x34; case 0xC0: return 0x35;
        case 0xBC: return 0x36; case 0xBE: return 0x37; case 0xBF: return 0x38;
        case 0x14: return 0x39; case 0x2C: return 0x46; case 0x91: return 0x47;
        case 0x13: return 0x48; case 0x2D: return 0x49; case 0x24: return 0x4A;
        case 0x21: return 0x4B; case 0x2E: return 0x4C; case 0x23: return 0x4D;
        case 0x22: return 0x4E; case 0x27: return 0x4F; case 0x25: return 0x50;
        case 0x28: return 0x51; case 0x26: return 0x52; case 0x90: return 0x53;
        case 0x6F: return 0x54; case 0x6A: return 0x55; case 0x6D: return 0x56;
        case 0x6B: return 0x57; case 0x60: return 0x62; case 0x6E: return 0x63;
        case 0xE2: return 0x64; case 0x5D: return 0x65;
        default: return 0;
        }
    }

    inline KeyState ReadPhysicalKeyboardKey(const PhysicalKeyboardState& state, int key)
    {
        if (!state.available) return {};
        uint8_t modifierMask = 0;
        switch (key) {
        case 0x10: modifierMask = 0x22; break;
        case 0x11: modifierMask = 0x11; break;
        case 0x12: modifierMask = 0x44; break;
        case 0xA0: modifierMask = 0x02; break; case 0xA1: modifierMask = 0x20; break;
        case 0xA2: modifierMask = 0x01; break; case 0xA3: modifierMask = 0x10; break;
        case 0xA4: modifierMask = 0x04; break; case 0xA5: modifierMask = 0x40; break;
        case 0x5B: modifierMask = 0x08; break; case 0x5C: modifierMask = 0x80; break;
        default: break;
        }
        if (modifierMask) return {true, (state.modifiers & modifierMask) != 0};
        const auto usage = VirtualKeyToHidUsage(key);
        if (!usage) return {};
        const bool down = std::find(state.keys.begin(), state.keys.end(), usage) != state.keys.end();
        return {true, down || (key == 0x0D &&
            std::find(state.keys.begin(), state.keys.end(), 0x58) != state.keys.end())};
    }

    // A known physical release wins over a delayed OS/DMA sample.
    inline constexpr KeyState SelectActivationKeyState(KeyState hardware, KeyState primary)
    {
        return hardware.available ? hardware : primary;
    }

    inline constexpr KeyState SelectKeyboardActivationKeyState(KeyState hardware, KeyState primary)
    {
        return {hardware.available || primary.available,
            (hardware.available && hardware.down) || (primary.available && primary.down)};
    }

    class MouseActivationRouter
    {
    public:
        KeyState Read(KeyState hardware, KeyState primary, uint64_t nowMs)
        {
            if (hardware.available && hardware.down) {
                hardwareGesture_ = true;
                primaryGesture_ = false;
                primaryBlocked_ = true;
                primaryResumeAtMs_ = nowMs + 250;
                return hardware;
            }
            if (hardwareGesture_) {
                if (!hardware.available) {
                    if (primary.available && !primary.down) {
                        hardwareGesture_ = false;
                        primaryBlocked_ = true;
                        primaryResumeAtMs_ = nowMs + 250;
                    }
                    return {};
                }
                hardwareGesture_ = false;
                primaryResumeAtMs_ = nowMs + 250;
                return hardware;
            }
            if (primaryBlocked_) {
                if (nowMs >= primaryResumeAtMs_ && primary.available && !primary.down)
                    primaryBlocked_ = false;
                else
                    return hardware.available ? hardware : KeyState{};
            }
            if (primaryGesture_) {
                if (!primary.available) return {};
                primaryGesture_ = primary.down;
                return primary;
            }
            if (primary.available && primary.down) {
                primaryGesture_ = true;
                return primary;
            }
            return {hardware.available || primary.available, false};
        }

    private:
        bool hardwareGesture_ = false;
        bool primaryGesture_ = false;
        bool primaryBlocked_ = false;
        uint64_t primaryResumeAtMs_ = 0;
    };

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

    inline constexpr uint64_t kMaximumQueuedMoveAgeUs = 35000;

    struct QueuedMouseMove
    {
        int x = 0;
        int y = 0;
        uint64_t revision = 0;
        uint64_t submittedAtUs = 0;
        uint64_t expiresAtUs = 0;
    };

    class LatestMoveMailbox
    {
    public:
        bool Submit(int x, int y, uint64_t nowUs, uint64_t validForUs)
        {
            if ((x == 0 && y == 0) || validForUs == 0) return false;
            const auto ageUs = std::min(validForUs, kMaximumQueuedMoveAgeUs);
            if (nowUs > UINT64_MAX - ageUs) return false;
            pending_ = QueuedMouseMove{std::clamp(x, -2048, 2048), std::clamp(y, -2048, 2048),
                ++revision_, nowUs, nowUs + ageUs};
            return true;
        }

        bool HasPending() const { return pending_.has_value(); }

        std::optional<QueuedMouseMove> Take()
        {
            const auto result = pending_;
            pending_.reset();
            return result;
        }

        bool IsCurrent(const QueuedMouseMove& move) const { return move.revision == revision_; }

        static bool IsFresh(const QueuedMouseMove& move, uint64_t nowUs)
        {
            return nowUs >= move.submittedAtUs && nowUs < move.expiresAtUs;
        }

        bool Cancel()
        {
            const bool hadPending = HasPending();
            pending_.reset();
            ++revision_;
            return hadPending;
        }

    private:
        std::optional<QueuedMouseMove> pending_;
        uint64_t revision_ = 0;
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
            constexpr std::string_view namedPrefix = "km.buttons";
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
            case 3:
                if (value == 'b') {
                    prefixState_ = 4;
                    return false;
                }
                break;
            default:
                if (prefixState_ < namedPrefix.size()) {
                    prefixState_ = value == namedPrefix[prefixState_]
                        ? static_cast<uint8_t>(prefixState_ + 1) : (value == 'k' ? 1 : 0);
                    return false;
                }
                break;
            }
            const uint32_t packetBytes = prefixState_ == namedPrefix.size() ? 11 : 4;
            prefixState_ = value == 'k' ? 1 : 0;
            if (value > 0x1Fu)
                return false;
            lastPacketBytes_ = packetBytes;
            mask_ = static_cast<uint8_t>(value & 0x1Fu);
            hasSample_ = true;
            if (buttonMask)
                *buttonMask = mask_;
            return true;
        }

        void Reset() noexcept
        {
            prefixState_ = 0;
            mask_ = 0;
            hasSample_ = false;
            lastPacketBytes_ = 0;
        }

        uint8_t Mask() const noexcept
        {
            return mask_;
        }

        bool HasSample() const noexcept { return hasSample_; }
        uint32_t LastPacketBytes() const noexcept { return lastPacketBytes_; }

    private:
        uint8_t prefixState_ = 0;
        uint8_t mask_ = 0;
        bool hasSample_ = false;
        uint32_t lastPacketBytes_ = 0;
    };

    class KmBoxSerialButtonParser
    {
    public:
        bool Consume(uint8_t value, uint8_t& mask)
        {
            if (value == '\r' || value == '\n') {
                const bool parsed = !overflow_ && ParseLine(line_, mask);
                line_.clear();
                overflow_ = false;
                return parsed;
            }
            if (line_.size() < 192) line_.push_back(static_cast<char>(value));
            else overflow_ = true;
            return false;
        }

        void Reset() { line_.clear(); overflow_ = false; }

    private:
        static bool ParseLine(std::string_view line, uint8_t& mask)
        {
            if (!line.starts_with("KVQB ")) return false;
            line.remove_prefix(5);
            uint8_t result = 0;
            for (int button = 0; button < 5; ++button) {
                while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
                if (line.empty() || line.front() < '0' || line.front() > '3') return false;
                if (((line.front() - '0') & 1) != 0)
                    result |= static_cast<uint8_t>(1u << button);
                line.remove_prefix(1);
                if (button != 4 && (line.empty() || line.front() != ' ')) return false;
            }
            while (!line.empty() && line.front() == ' ') line.remove_prefix(1);
            if (!line.empty()) return false;
            mask = result;
            return true;
        }

        std::string line_;
        bool overflow_ = false;
    };

    inline constexpr int kMovementTestRadius = 150;
    inline constexpr int kMovementTestSegments = 72;
    inline constexpr int kMovementTestStepDelayMs = 7;

    inline constexpr bool IsValidDeviceKind(int value)
    {
        return value >= static_cast<int>(DeviceKind::None) &&
               value <= static_cast<int>(DeviceKind::FerrumOne);
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
