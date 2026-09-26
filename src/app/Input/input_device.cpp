#include "app/Input/input_device.h"
#include "app/Input/primary_keyboard.h"

#include "app/Platform/win_handle.h"

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <SetupAPI.h>
#include <devguid.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace
{
    using app::input::ConnectionState;
    using app::input::DeviceError;
    using app::input::DeviceKind;
    using app::input::DeviceStatus;
    using app::input::KmBoxNetConfig;

    constexpr DWORD kMakcuFastBaud = 4'000'000u;
    constexpr DWORD kSerialDefaultBaud = 115'200u;
    constexpr auto kHandshakeTimeout = std::chrono::milliseconds(500);
    constexpr auto kNetworkTimeout = std::chrono::milliseconds(250);
    constexpr auto kHealthProbeInterval = std::chrono::seconds(2);
    constexpr auto kPhysicalInputPollInterval = std::chrono::milliseconds(2);
    constexpr auto kLeftButtonWatchdog = std::chrono::milliseconds(100);
    constexpr uint32_t kMinimumLeftClickHoldMs = 8u;
    constexpr uint32_t kMaximumLeftClickHoldMs = 80u;
    constexpr std::array<uint8_t, 9> kMakcuBaudFrame = {
        0xDEu, 0xADu, 0x05u, 0x00u, 0xA5u,
        0x00u, 0x09u, 0x3Du, 0x00u
    };

    constexpr uint32_t kKmBoxNetConnect = 0xAF3C2828u;
    constexpr uint32_t kKmBoxNetMouseMove = 0xAEDE7345u;
    constexpr uint32_t kKmBoxNetMouseLeft = 0x9823AE8Du;
    constexpr uint32_t kKmBoxNetMonitor = 0x27388020u;

    uint64_t InputNowUs()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    struct ConnectionResult
    {
        bool success = false;
        DeviceError error = DeviceError::None;
        DWORD systemError = 0;
        std::string endpoint;
    };

    struct DeviceInfoSet
    {
        HDEVINFO value = INVALID_HANDLE_VALUE;

        DeviceInfoSet() = default;
        DeviceInfoSet(const DeviceInfoSet&) = delete;
        DeviceInfoSet& operator=(const DeviceInfoSet&) = delete;

        ~DeviceInfoSet()
        {
            if (value != INVALID_HANDLE_VALUE)
                SetupDiDestroyDeviceInfoList(value);
        }
    };

    std::vector<uint8_t> ReadDeviceProperty(
        HDEVINFO infoSet,
        SP_DEVINFO_DATA& device,
        DWORD property)
    {
        DWORD requiredBytes = 0;
        DWORD propertyType = 0;
        SetupDiGetDeviceRegistryPropertyW(
            infoSet,
            &device,
            property,
            &propertyType,
            nullptr,
            0,
            &requiredBytes);
        if (requiredBytes == 0 || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            return {};

        std::vector<uint8_t> buffer(
            static_cast<size_t>(requiredBytes) + sizeof(wchar_t),
            0u);
        if (!SetupDiGetDeviceRegistryPropertyW(
                infoSet,
                &device,
                property,
                &propertyType,
                buffer.data(),
                requiredBytes,
                nullptr)) {
            return {};
        }
        return buffer;
    }

    std::wstring ReadDeviceStringProperty(
        HDEVINFO infoSet,
        SP_DEVINFO_DATA& device,
        DWORD property)
    {
        const std::vector<uint8_t> bytes = ReadDeviceProperty(
            infoSet,
            device,
            property);
        if (bytes.empty())
            return {};
        return std::wstring(reinterpret_cast<const wchar_t*>(bytes.data()));
    }

    bool DeviceHardwareIdMatches(
        HDEVINFO infoSet,
        SP_DEVINFO_DATA& device,
        bool (*predicate)(std::wstring_view))
    {
        const std::vector<uint8_t> bytes = ReadDeviceProperty(
            infoSet,
            device,
            SPDRP_HARDWAREID);
        if (bytes.empty())
            return false;

        const auto* values = reinterpret_cast<const wchar_t*>(bytes.data());
        const size_t capacity = bytes.size() / sizeof(wchar_t);
        size_t offset = 0;
        while (offset < capacity && values[offset] != L'\0') {
            size_t length = 0;
            while (offset + length < capacity &&
                   values[offset + length] != L'\0') {
                ++length;
            }
            if (predicate(std::wstring_view(values + offset, length)))
                return true;
            offset += length + 1u;
        }
        return false;
    }

    std::wstring ReadDevicePortName(
        HDEVINFO infoSet,
        SP_DEVINFO_DATA& device)
    {
        HKEY rawKey = SetupDiOpenDevRegKey(
            infoSet,
            &device,
            DICS_FLAG_GLOBAL,
            0,
            DIREG_DEV,
            KEY_QUERY_VALUE);
        if (rawKey == INVALID_HANDLE_VALUE)
            return {};
        app::platform::UniqueRegKey key(rawKey);

        DWORD type = 0;
        DWORD byteCount = 0;
        if (RegQueryValueExW(
                key.Get(),
                L"PortName",
                nullptr,
                &type,
                nullptr,
                &byteCount) != ERROR_SUCCESS ||
            type != REG_SZ ||
            byteCount < sizeof(wchar_t)) {
            return {};
        }

        std::vector<wchar_t> value(
            static_cast<size_t>(byteCount) / sizeof(wchar_t) + 1u,
            L'\0');
        if (RegQueryValueExW(
                key.Get(),
                L"PortName",
                nullptr,
                &type,
                reinterpret_cast<BYTE*>(value.data()),
                &byteCount) != ERROR_SUCCESS) {
            return {};
        }
        return std::wstring(value.data());
    }

    int PortNumber(std::wstring_view port)
    {
        if (port.size() <= 3 ||
            app::input::LowerAscii(port[0]) != L'c' ||
            app::input::LowerAscii(port[1]) != L'o' ||
            app::input::LowerAscii(port[2]) != L'm') {
            return 10000;
        }
        int number = 0;
        for (size_t index = 3; index < port.size(); ++index) {
            if (port[index] < L'0' || port[index] > L'9')
                return 10000;
            number = number * 10 + static_cast<int>(port[index] - L'0');
        }
        return number;
    }

    std::vector<std::wstring> EnumerateSerialPorts(DeviceKind kind)
    {
        DeviceInfoSet infoSet;
        infoSet.value = SetupDiGetClassDevsW(
            &GUID_DEVCLASS_PORTS,
            nullptr,
            nullptr,
            DIGCF_PRESENT);
        if (infoSet.value == INVALID_HANDLE_VALUE)
            return {};

        std::vector<std::wstring> ports;
        for (DWORD index = 0;; ++index) {
            SP_DEVINFO_DATA device = {};
            device.cbSize = sizeof(device);
            if (!SetupDiEnumDeviceInfo(infoSet.value, index, &device)) {
                if (GetLastError() == ERROR_NO_MORE_ITEMS)
                    break;
                continue;
            }

            const bool isMakcu = DeviceHardwareIdMatches(
                infoSet.value,
                device,
                app::input::IsMakcuHardwareId);
            bool matches = isMakcu;
            if (kind == DeviceKind::KmBox) {
                const bool hasKmBoxId = DeviceHardwareIdMatches(
                    infoSet.value,
                    device,
                    app::input::IsKmBoxSerialHardwareId);
                const std::wstring friendlyName = ReadDeviceStringProperty(
                    infoSet.value,
                    device,
                    SPDRP_FRIENDLYNAME);
                matches = !isMakcu &&
                    (hasKmBoxId ||
                     app::input::ContainsAsciiCaseInsensitive(
                         friendlyName,
                         L"USB-SERIAL CH340"));
            }
            if (!matches)
                continue;

            std::wstring port = ReadDevicePortName(infoSet.value, device);
            if (!port.empty())
                ports.push_back(std::move(port));
        }

        std::sort(ports.begin(), ports.end(), [](const auto& left, const auto& right) {
            return PortNumber(left) < PortNumber(right);
        });
        ports.erase(std::unique(ports.begin(), ports.end()), ports.end());
        return ports;
    }

    std::string NarrowPortName(std::wstring_view port)
    {
        std::string result;
        result.reserve(port.size());
        for (const wchar_t value : port) {
            if (value <= 0x7F)
                result.push_back(static_cast<char>(value));
        }
        return result;
    }

    class SerialPort
    {
    public:
        bool Open(
            std::wstring_view port,
            DWORD baudRate,
            DeviceError* error,
            DWORD* systemError)
        {
            Disconnect();
            lastError_ = 0;
            if (error)
                *error = DeviceError::None;
            if (systemError)
                *systemError = 0;

            const std::wstring devicePath = L"\\\\.\\" + std::wstring(port);
            HANDLE rawHandle = CreateFileW(
                devicePath.c_str(),
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
            if (rawHandle == INVALID_HANDLE_VALUE) {
                lastError_ = GetLastError();
                if (error)
                    *error = DeviceError::PortUnavailable;
                if (systemError)
                    *systemError = lastError_;
                return false;
            }
            handle_.Reset(rawHandle);

            SetupComm(handle_.Get(), 4096, 4096);
            DCB dcb = {};
            dcb.DCBlength = sizeof(dcb);
            if (!GetCommState(handle_.Get(), &dcb)) {
                return ConfigureFailure(error, systemError);
            }
            dcb.BaudRate = baudRate;
            dcb.ByteSize = 8;
            dcb.Parity = NOPARITY;
            dcb.StopBits = ONESTOPBIT;
            dcb.fBinary = TRUE;
            dcb.fParity = FALSE;
            dcb.fOutxCtsFlow = FALSE;
            dcb.fOutxDsrFlow = FALSE;
            dcb.fDtrControl = DTR_CONTROL_DISABLE;
            dcb.fDsrSensitivity = FALSE;
            dcb.fTXContinueOnXoff = TRUE;
            dcb.fOutX = FALSE;
            dcb.fInX = FALSE;
            dcb.fErrorChar = FALSE;
            dcb.fNull = FALSE;
            dcb.fRtsControl = RTS_CONTROL_DISABLE;
            dcb.fAbortOnError = FALSE;
            if (!SetCommState(handle_.Get(), &dcb))
                return ConfigureFailure(error, systemError);

            COMMTIMEOUTS timeouts = {};
            timeouts.ReadIntervalTimeout = 20;
            timeouts.ReadTotalTimeoutMultiplier = 0;
            timeouts.ReadTotalTimeoutConstant = 25;
            timeouts.WriteTotalTimeoutMultiplier = 0;
            timeouts.WriteTotalTimeoutConstant = 250;
            if (!SetCommTimeouts(handle_.Get(), &timeouts))
                return ConfigureFailure(error, systemError);

            PurgeComm(
                handle_.Get(),
                PURGE_RXABORT | PURGE_RXCLEAR | PURGE_TXABORT | PURGE_TXCLEAR);
            return true;
        }

        void Disconnect()
        {
            handle_.Reset();
        }

        bool IsConnected() const
        {
            return static_cast<bool>(handle_);
        }

        bool IsAlive()
        {
            if (!handle_)
                return false;
            DCB state = {};
            state.DCBlength = sizeof(state);
            DWORD errors = 0;
            COMSTAT status = {};
            if (!GetCommState(handle_.Get(), &state) ||
                !ClearCommError(handle_.Get(), &errors, &status)) {
                lastError_ = GetLastError();
                return false;
            }
            return true;
        }

        bool WriteBytes(const void* bytes, size_t byteCount, bool drain = false)
        {
            if (!handle_ || !bytes || byteCount == 0)
                return false;
            const auto* source = static_cast<const uint8_t*>(bytes);
            size_t writtenTotal = 0;
            while (writtenTotal < byteCount) {
                DWORD written = 0;
                const DWORD request = static_cast<DWORD>((std::min)(
                    byteCount - writtenTotal,
                    static_cast<size_t>(MAXDWORD)));
                if (!WriteFile(
                        handle_.Get(),
                        source + writtenTotal,
                        request,
                        &written,
                        nullptr) ||
                    written == 0) {
                    lastError_ = GetLastError();
                    return false;
                }
                writtenTotal += written;
            }
            if (drain && !FlushFileBuffers(handle_.Get())) {
                lastError_ = GetLastError();
                return false;
            }
            return true;
        }

        bool DrainOutput()
        {
            if (!handle_ || !FlushFileBuffers(handle_.Get())) {
                lastError_ = GetLastError();
                return false;
            }
            return true;
        }

        bool ReadAvailable(std::vector<uint8_t>& output)
        {
            if (!handle_)
                return false;

            DWORD errors = 0;
            COMSTAT status = {};
            if (!ClearCommError(handle_.Get(), &errors, &status)) {
                lastError_ = GetLastError();
                return false;
            }

            std::array<uint8_t, 512> buffer = {};
            DWORD remaining = status.cbInQue;
            while (remaining != 0) {
                const DWORD request = (std::min)(
                    remaining,
                    static_cast<DWORD>(buffer.size()));
                DWORD bytesRead = 0;
                if (!ReadFile(
                        handle_.Get(),
                        buffer.data(),
                        request,
                        &bytesRead,
                        nullptr)) {
                    lastError_ = GetLastError();
                    return false;
                }
                if (bytesRead == 0)
                    break;
                output.insert(
                    output.end(),
                    buffer.begin(),
                    buffer.begin() + bytesRead);
                remaining -= bytesRead;
            }
            return true;
        }

        bool ExecuteCommand(
            std::string_view command,
            std::chrono::milliseconds timeout,
            std::string* response)
        {
            if (!handle_)
                return false;
            PurgeComm(handle_.Get(), PURGE_RXABORT | PURGE_RXCLEAR);
            if (!WriteBytes(command.data(), command.size(), true))
                return false;
            return ReadUntilPrompt(timeout, response);
        }

        DWORD LastError() const
        {
            return lastError_;
        }

    private:
        bool ConfigureFailure(DeviceError* error, DWORD* systemError)
        {
            lastError_ = GetLastError();
            if (error)
                *error = DeviceError::ConfigureFailed;
            if (systemError)
                *systemError = lastError_;
            Disconnect();
            return false;
        }

        bool ReadUntilPrompt(
            std::chrono::milliseconds timeout,
            std::string* response)
        {
            std::string received;
            received.reserve(256);
            const auto deadline = std::chrono::steady_clock::now() + timeout;
            std::array<char, 256> buffer = {};
            while (std::chrono::steady_clock::now() < deadline) {
                DWORD read = 0;
                if (!ReadFile(
                        handle_.Get(),
                        buffer.data(),
                        static_cast<DWORD>(buffer.size()),
                        &read,
                        nullptr)) {
                    lastError_ = GetLastError();
                    return false;
                }
                if (read > 0) {
                    received.append(buffer.data(), read);
                    if (received.find(">>>") != std::string::npos) {
                        if (response)
                            *response = std::move(received);
                        return true;
                    }
                } else {
                    Sleep(1);
                }
            }
            if (response)
                *response = std::move(received);
            return false;
        }

        app::platform::UniqueWinHandle handle_;
        DWORD lastError_ = 0;
    };

    class InputBackend
    {
    public:
        virtual ~InputBackend() = default;
        virtual ConnectionResult Connect(const KmBoxNetConfig& config) = 0;
        virtual void Disconnect() = 0;
        virtual bool IsAlive() = 0;
        virtual bool Move(int x, int y) = 0;
        virtual bool SetLeftButton(bool pressed) = 0;
        virtual bool PollPhysicalButtons(
            uint8_t* buttonMask,
            bool* available)
        {
            if (buttonMask)
                *buttonMask = 0;
            if (available)
                *available = false;
            return true;
        }
        virtual bool Probe() = 0;
        virtual app::input::PhysicalKeyboardState PhysicalKeyboard() const { return {}; }
        virtual uint64_t PhysicalInputUpdatedAtMs() const { return 0; }
        virtual uint16_t InputMonitorPort() const { return 0; }
        virtual void PopulateInputDiagnostics(DeviceStatus&) const {}
        virtual DWORD LastSystemError() const = 0;
        virtual std::chrono::milliseconds TestStepDelay() const = 0;
    };

    bool WriteSerialMove(SerialPort& serial, int x, int y)
    {
        char command[64] = {};
        const int length = std::snprintf(
            command,
            sizeof(command),
            "km.move(%d,%d)\r\n",
            x,
            y);
        return length > 0 &&
               static_cast<size_t>(length) < sizeof(command) &&
               serial.WriteBytes(command, static_cast<size_t>(length));
    }

    bool WriteSerialLeftButton(SerialPort& serial, bool pressed)
    {
        const char* command = pressed
            ? "km.left(1)\r\n"
            : "km.left(0)\r\n";
        return serial.WriteBytes(command, std::strlen(command));
    }

    class MakcuBackend final : public InputBackend
    {
    public:
        ConnectionResult Connect(const KmBoxNetConfig&) override
        {
            Disconnect();
            const std::vector<std::wstring> ports =
                EnumerateSerialPorts(DeviceKind::Makcu);
            if (ports.empty())
                return { false, DeviceError::NotFound, 0, {} };

            ConnectionResult result = {
                false,
                DeviceError::HandshakeFailed,
                0,
                NarrowPortName(ports.front())
            };
            for (const std::wstring& port : ports) {
                result.endpoint = NarrowPortName(port);
                DeviceError openError = DeviceError::None;
                DWORD systemError = 0;
                if (serial_.Open(port, kMakcuFastBaud, &openError, &systemError)) {
                    if (Probe() && EnableButtonMonitoring())
                        return { true, DeviceError::None, 0, result.endpoint };
                } else {
                    result.error = openError;
                    result.systemError = systemError;
                }
                serial_.Disconnect();

                if (!serial_.Open(
                        port,
                        kSerialDefaultBaud,
                        &openError,
                        &systemError)) {
                    result.error = openError;
                    result.systemError = systemError;
                    continue;
                }
                if (!serial_.WriteBytes(
                        kMakcuBaudFrame.data(),
                        kMakcuBaudFrame.size(),
                        true)) {
                    result.error = DeviceError::IoFailure;
                    result.systemError = serial_.LastError();
                    serial_.Disconnect();
                    continue;
                }
                Sleep(100);
                serial_.Disconnect();

                if (!serial_.Open(
                        port,
                        kMakcuFastBaud,
                        &openError,
                        &systemError)) {
                    result.error = openError;
                    result.systemError = systemError;
                    continue;
                }
                Sleep(50);
                if (Probe() && EnableButtonMonitoring())
                    return { true, DeviceError::None, 0, result.endpoint };
                result.error = DeviceError::HandshakeFailed;
                result.systemError = serial_.LastError();
                serial_.Disconnect();
            }
            return result;
        }

        void Disconnect() override
        {
            monitoringEnabled_ = false;
            buttonMask_ = 0;
            buttonParser_.Reset();
            monitorPackets_ = 0;
            monitorReceivedBytes_ = 0;
            monitorUpdatedAtMs_ = 0;
            monitorRetryAtMs_ = 0;
            serial_.Disconnect();
        }

        bool IsAlive() override
        {
            return serial_.IsAlive();
        }

        bool Move(int x, int y) override
        {
            return WriteSerialMove(serial_, x, y);
        }

        bool SetLeftButton(bool pressed) override
        {
            return WriteSerialLeftButton(serial_, pressed);
        }

        bool PollPhysicalButtons(
            uint8_t* buttonMask,
            bool* available) override
        {
            if (buttonMask)
                *buttonMask = buttonMask_;
            if (available)
                *available = monitoringEnabled_ && buttonParser_.HasSample();
            if (!monitoringEnabled_)
                return serial_.IsAlive();

            std::vector<uint8_t> bytes;
            if (!serial_.ReadAvailable(bytes))
                return false;
            ConsumeInput(bytes);
            const auto nowMs = GetTickCount64();
            if (!buttonParser_.HasSample() && nowMs >= monitorRetryAtMs_) {
                constexpr std::string_view command = "km.buttons(1)\r\n";
                if (!serial_.WriteBytes(command.data(), command.size())) return false;
                monitorRetryAtMs_ = nowMs + 2000;
            }
            if (available)
                *available = buttonParser_.HasSample();
            if (buttonMask)
                *buttonMask = buttonMask_;
            return true;
        }

        uint64_t PhysicalInputUpdatedAtMs() const override { return monitorUpdatedAtMs_; }

        void PopulateInputDiagnostics(DeviceStatus& status) const override
        {
            status.inputMonitorPackets = monitorPackets_;
            status.inputMonitorReceivedBytes = monitorReceivedBytes_;
            status.inputMonitorLastPacketBytes = buttonParser_.HasSample() ? buttonParser_.LastPacketBytes() : 0;
        }

        bool Probe() override
        {
            if (!serial_.IsConnected())
                return false;
            if (monitoringEnabled_)
                return serial_.IsAlive();
            if (!serial_.DrainOutput())
                return false;
            std::string response;
            return serial_.ExecuteCommand(
                       "km.version()\r\n",
                       kHandshakeTimeout,
                       &response) &&
                   app::input::IsMakcuVersionResponse(response);
        }

        DWORD LastSystemError() const override
        {
            return serial_.LastError();
        }

        std::chrono::milliseconds TestStepDelay() const override
        {
            return std::chrono::milliseconds(
                app::input::kMovementTestStepDelayMs);
        }

    private:
        void ConsumeInput(std::span<const uint8_t> bytes)
        {
            monitorReceivedBytes_ += bytes.size();
            for (const auto byte : bytes) {
                if (buttonParser_.Consume(byte, &buttonMask_)) {
                    ++monitorPackets_;
                    monitorUpdatedAtMs_ = GetTickCount64();
                }
            }
        }

        bool EnableButtonMonitoring()
        {
            buttonMask_ = 0;
            buttonParser_.Reset();
            std::string response;
            if (!serial_.ExecuteCommand(
                    "km.buttons(1)\r\n",
                    kHandshakeTimeout,
                    &response) ||
                !app::input::IsSuccessfulKmCommandResponse(response)) {
                return false;
            }
            monitoringEnabled_ = true;
            ConsumeInput(std::span(reinterpret_cast<const uint8_t*>(response.data()), response.size()));
            monitorRetryAtMs_ = GetTickCount64() + 2000;
            return true;
        }

        SerialPort serial_;
        app::input::MakcuButtonStreamParser buttonParser_;
        uint8_t buttonMask_ = 0;
        bool monitoringEnabled_ = false;
        uint64_t monitorPackets_ = 0;
        uint64_t monitorReceivedBytes_ = 0;
        uint64_t monitorUpdatedAtMs_ = 0;
        uint64_t monitorRetryAtMs_ = 0;
    };

    class KmBoxSerialBackend final : public InputBackend
    {
    public:
        ConnectionResult Connect(const KmBoxNetConfig&) override
        {
            Disconnect();
            const std::vector<std::wstring> ports =
                EnumerateSerialPorts(DeviceKind::KmBox);
            if (ports.empty())
                return { false, DeviceError::NotFound, 0, {} };

            ConnectionResult result = {
                false,
                DeviceError::HandshakeFailed,
                0,
                NarrowPortName(ports.front())
            };
            for (const std::wstring& port : ports) {
                result.endpoint = NarrowPortName(port);
                DeviceError openError = DeviceError::None;
                DWORD systemError = 0;
                if (!serial_.Open(
                        port,
                        kSerialDefaultBaud,
                        &openError,
                        &systemError)) {
                    result.error = openError;
                    result.systemError = systemError;
                    continue;
                }
                Sleep(20);
                if (Probe())
                    return { true, DeviceError::None, 0, result.endpoint };
                result.error = DeviceError::HandshakeFailed;
                result.systemError = serial_.LastError();
                serial_.Disconnect();
            }
            return result;
        }

        void Disconnect() override
        {
            buttonParser_.Reset();
            buttonMask_ = 0;
            buttonsUpdatedAtMs_ = 0;
            nextButtonsQueryAtMs_ = 0;
            buttonsQueryPending_ = false;
            serial_.Disconnect();
        }

        bool IsAlive() override
        {
            return serial_.IsAlive();
        }

        bool Move(int x, int y) override
        {
            return WriteSerialMove(serial_, x, y);
        }

        bool SetLeftButton(bool pressed) override
        {
            return WriteSerialLeftButton(serial_, pressed);
        }

        bool PollPhysicalButtons(uint8_t* buttonMask, bool* available) override
        {
            std::vector<uint8_t> bytes;
            if (!serial_.ReadAvailable(bytes)) return false;
            const auto nowMs = GetTickCount64();
            for (const auto byte : bytes) {
                uint8_t mask = 0;
                if (buttonParser_.Consume(byte, mask) && buttonsQueryPending_ &&
                    nowMs < nextButtonsQueryAtMs_) {
                    buttonMask_ = mask;
                    buttonsUpdatedAtMs_ = nowMs;
                    buttonsQueryPending_ = false;
                    nextButtonsQueryAtMs_ = nowMs + 25;
                }
            }
            if (nowMs >= nextButtonsQueryAtMs_) {
                constexpr std::string_view query =
                    "print('KVQB',km.left(),km.right(),km.middle(),km.side1(),km.side2())\r\n";
                if (!buttonsQueryPending_) {
                    if (!serial_.WriteBytes(query.data(), query.size())) return false;
                    buttonsQueryPending_ = true;
                    nextButtonsQueryAtMs_ = nowMs + 200;
                } else {
                    buttonsQueryPending_ = false;
                    nextButtonsQueryAtMs_ = nowMs + 2000;
                }
            }
            const bool fresh = app::input::IsNetworkInputFresh(buttonsUpdatedAtMs_, nowMs);
            if (buttonMask) *buttonMask = fresh ? buttonMask_ : 0;
            if (available) *available = fresh;
            return true;
        }

        uint64_t PhysicalInputUpdatedAtMs() const override { return buttonsUpdatedAtMs_; }

        bool Probe() override
        {
            if (!serial_.IsConnected())
                return false;
            if (app::input::IsNetworkInputFresh(buttonsUpdatedAtMs_, GetTickCount64()))
                return serial_.IsAlive();
            if (!serial_.DrainOutput())
                return false;
            std::string response;
            return serial_.ExecuteCommand(
                       "km.move(0,0)\r\n",
                       kHandshakeTimeout,
                       &response) &&
                   app::input::IsSuccessfulKmCommandResponse(response);
        }

        DWORD LastSystemError() const override
        {
            return serial_.LastError();
        }

        std::chrono::milliseconds TestStepDelay() const override
        {
            return std::chrono::milliseconds(
                app::input::kMovementTestStepDelayMs);
        }

    private:
        SerialPort serial_;
        app::input::KmBoxSerialButtonParser buttonParser_;
        uint8_t buttonMask_ = 0;
        uint64_t buttonsUpdatedAtMs_ = 0;
        uint64_t nextButtonsQueryAtMs_ = 0;
        bool buttonsQueryPending_ = false;
    };

    class WinsockSession
    {
    public:
        bool Start(DWORD* systemError)
        {
            Reset();
            WSADATA data = {};
            const int result = WSAStartup(MAKEWORD(2, 2), &data);
            if (result != 0) {
                if (systemError)
                    *systemError = static_cast<DWORD>(result);
                return false;
            }
            active_ = true;
            return true;
        }

        void Reset()
        {
            if (active_)
                WSACleanup();
            active_ = false;
        }

        ~WinsockSession()
        {
            Reset();
        }

    private:
        bool active_ = false;
    };

    class UniqueSocket
    {
    public:
        ~UniqueSocket()
        {
            Reset();
        }

        void Reset(SOCKET value = INVALID_SOCKET)
        {
            if (value_ != INVALID_SOCKET)
                closesocket(value_);
            value_ = value;
        }

        SOCKET Get() const
        {
            return value_;
        }

        explicit operator bool() const
        {
            return value_ != INVALID_SOCKET;
        }

    private:
        SOCKET value_ = INVALID_SOCKET;
    };

    struct KmBoxNetHeader
    {
        uint32_t hardwareKey = 0;
        uint32_t random = 0;
        uint32_t index = 0;
        uint32_t command = 0;
    };

    struct KmBoxNetMouse
    {
        int32_t buttons = 0;
        int32_t x = 0;
        int32_t y = 0;
        int32_t wheel = 0;
        int32_t points[10] = {};
    };

    static_assert(sizeof(KmBoxNetHeader) == 16);
    static_assert(sizeof(KmBoxNetMouse) == 56);

    class KmBoxNetBackend final : public InputBackend
    {
    public:
        ConnectionResult Connect(const KmBoxNetConfig& config) override
        {
            Disconnect();
            uint32_t hardwareKey = 0;
            if (!app::input::IsValidKmBoxNetworkConfig(
                    config.host,
                    config.port,
                    config.hardwareKey,
                    &hardwareKey)) {
                return {
                    false,
                    DeviceError::InvalidConfiguration,
                    0,
                    {}
                };
            }

            DWORD systemError = 0;
            if (!winsock_.Start(&systemError)) {
                return {
                    false,
                    DeviceError::IoFailure,
                    systemError,
                    {}
                };
            }

            socket_.Reset(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
            if (!socket_) {
                lastError_ = static_cast<DWORD>(WSAGetLastError());
                Disconnect();
                return {
                    false,
                    DeviceError::IoFailure,
                    lastError_,
                    {}
                };
            }

            const DWORD timeoutMs = static_cast<DWORD>(kNetworkTimeout.count());
            if (setsockopt(
                    socket_.Get(),
                    SOL_SOCKET,
                    SO_RCVTIMEO,
                    reinterpret_cast<const char*>(&timeoutMs),
                    sizeof(timeoutMs)) == SOCKET_ERROR ||
                setsockopt(
                    socket_.Get(),
                    SOL_SOCKET,
                    SO_SNDTIMEO,
                    reinterpret_cast<const char*>(&timeoutMs),
                    sizeof(timeoutMs)) == SOCKET_ERROR) {
                lastError_ = static_cast<DWORD>(WSAGetLastError());
                Disconnect();
                return {
                    false,
                    DeviceError::IoFailure,
                    lastError_,
                    {}
                };
            }

            destination_ = {};
            destination_.sin_family = AF_INET;
            destination_.sin_port = htons(config.port);
            if (InetPtonA(
                    AF_INET,
                    config.host.c_str(),
                    &destination_.sin_addr) != 1) {
                Disconnect();
                return {
                    false,
                    DeviceError::InvalidConfiguration,
                    0,
                    {}
                };
            }

            hardwareKey_ = hardwareKey;
            index_ = 0;
            endpoint_ = config.host + ":" + std::to_string(config.port);
            if (!SendPacket(kKmBoxNetConnect, nullptr, 0, false)) {
                const DWORD error = lastError_;
                const std::string failedEndpoint = endpoint_;
                const DeviceError kind = IsTimeoutError(error)
                    ? DeviceError::NetworkTimeout
                    : DeviceError::HandshakeFailed;
                Disconnect();
                return { false, kind, error, failedEndpoint };
            }
            StartMonitoring();
            return { true, DeviceError::None, 0, endpoint_ };
        }

        void Disconnect() override
        {
            if (monitorSocket_ && socket_)
                (void)SendPacket(kKmBoxNetMonitor, nullptr, 0, true, false, 0u);
            monitorSocket_.Reset();
            monitorPort_ = 0;
            monitorUpdatedAtMs_ = 0;
            monitorRetryAtMs_ = 0;
            monitorMouseButtons_ = 0;
            monitorKeyboard_ = {};
            monitorPackets_ = 0;
            monitorRejectedPackets_ = 0;
            monitorLastPacketBytes_ = 0;
            monitorError_ = 0;
            outputButtons_ = 0;
            socket_.Reset();
            winsock_.Reset();
            destination_ = {};
            hardwareKey_ = 0;
            index_ = 0;
            endpoint_.clear();
        }

        bool IsAlive() override
        {
            return static_cast<bool>(socket_);
        }

        bool Move(int x, int y) override
        {
            KmBoxNetMouse mouse = {};
            mouse.buttons = outputButtons_;
            mouse.x = x;
            mouse.y = y;
            return SendPacket(
                kKmBoxNetMouseMove,
                &mouse,
                sizeof(mouse),
                true);
        }

        bool SetLeftButton(bool pressed) override
        {
            KmBoxNetMouse mouse = {};
            mouse.buttons = pressed ? 1 : 0;
            outputButtons_ = mouse.buttons;
            for (int attempt = 0; attempt < (pressed ? 1 : 3); ++attempt) {
                if (SendPacket(kKmBoxNetMouseLeft, &mouse, sizeof(mouse), true, !pressed))
                    return true;
                if (pressed || !IsTimeoutError(lastError_))
                    break;
            }
            return false;
        }

        bool Probe() override
        {
            return Move(0, 0);
        }

        bool PollPhysicalButtons(uint8_t* buttonMask, bool* available) override
        {
            if (monitorSocket_) {
                std::array<uint8_t, 64> bytes = {};
                for (int attempt = 0; attempt < 256; ++attempt) {
                    sockaddr_in sender = {};
                    int senderLength = sizeof(sender);
                    const int received = recvfrom(monitorSocket_.Get(),
                        reinterpret_cast<char*>(bytes.data()), static_cast<int>(bytes.size()),
                        0, reinterpret_cast<sockaddr*>(&sender), &senderLength);
                    if (received == SOCKET_ERROR) {
                        const int error = WSAGetLastError();
                        if (error == WSAEMSGSIZE) {
                            ++monitorRejectedPackets_;
                            monitorError_ = error;
                            continue;
                        }
                        if (error != WSAEWOULDBLOCK) {
                            monitorUpdatedAtMs_ = 0;
                            monitorKeyboard_ = {};
                            monitorError_ = error;
                        }
                        break;
                    }
                    if (sender.sin_family != AF_INET ||
                        sender.sin_addr.s_addr != destination_.sin_addr.s_addr)
                        continue;
                    ++monitorPackets_;
                    monitorLastPacketBytes_ = static_cast<uint32_t>(received);
                    if (app::input::ParseNetworkInputReport(
                        std::span<const uint8_t>(bytes.data(), static_cast<size_t>(received)),
                        monitorMouseButtons_, monitorKeyboard_)) {
                        monitorUpdatedAtMs_ = GetTickCount64();
                        monitorError_ = 0;
                    } else {
                        ++monitorRejectedPackets_;
                    }
                }
                const auto nowMs = GetTickCount64();
                if (!app::input::IsNetworkInputFresh(monitorUpdatedAtMs_, nowMs) &&
                    nowMs >= monitorRetryAtMs_) {
                    if (!SendPacket(kKmBoxNetMonitor, nullptr, 0, true, false,
                            0xAA550000u | monitorPort_))
                        monitorError_ = lastError_;
                    monitorRetryAtMs_ = nowMs + 2000;
                }
            }
            const bool fresh = app::input::IsNetworkInputFresh(
                monitorUpdatedAtMs_, GetTickCount64());
            if (buttonMask) *buttonMask = fresh ? monitorMouseButtons_ : 0;
            if (available) *available = fresh;
            return true;
        }

        app::input::PhysicalKeyboardState PhysicalKeyboard() const override
        {
            return app::input::IsNetworkInputFresh(monitorUpdatedAtMs_, GetTickCount64())
                ? monitorKeyboard_ : app::input::PhysicalKeyboardState{};
        }

        uint64_t PhysicalInputUpdatedAtMs() const override { return monitorUpdatedAtMs_; }
        uint16_t InputMonitorPort() const override { return monitorPort_; }

        void PopulateInputDiagnostics(DeviceStatus& status) const override
        {
            status.inputMonitorPackets = monitorPackets_;
            status.inputMonitorRejectedPackets = monitorRejectedPackets_;
            status.inputMonitorLastPacketBytes = monitorLastPacketBytes_;
            status.inputMonitorError = monitorError_;
        }

        DWORD LastSystemError() const override
        {
            return lastError_;
        }

        std::chrono::milliseconds TestStepDelay() const override
        {
            return std::chrono::milliseconds(
                app::input::kMovementTestStepDelayMs);
        }

    private:
        void StartMonitoring()
        {
            monitorSocket_.Reset(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
            if (!monitorSocket_) {
                monitorError_ = WSAGetLastError();
                return;
            }
            const BOOL exclusive = TRUE;
            u_long nonBlocking = 1;
            if (setsockopt(monitorSocket_.Get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                    reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR ||
                ioctlsocket(monitorSocket_.Get(), FIONBIO, &nonBlocking) == SOCKET_ERROR) {
                monitorError_ = WSAGetLastError();
                monitorSocket_.Reset();
                return;
            }
            sockaddr_in local = {};
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = htonl(INADDR_ANY);
            for (int attempt = 0; attempt < 64; ++attempt) {
                const auto port = static_cast<uint16_t>(1024u + random_() % (32768u - 1024u));
                local.sin_port = htons(port);
                if (bind(monitorSocket_.Get(), reinterpret_cast<const sockaddr*>(&local),
                        sizeof(local)) == 0) {
                    monitorPort_ = port;
                    if (!SendPacket(kKmBoxNetMonitor, nullptr, 0, true, false,
                            0xAA550000u | monitorPort_))
                        monitorError_ = lastError_;
                    monitorRetryAtMs_ = GetTickCount64() + 2000;
                    return;
                }
                monitorError_ = WSAGetLastError();
            }
            monitorSocket_.Reset();
        }

        static bool IsTimeoutError(DWORD error)
        {
            return error == WSAETIMEDOUT || error == WSAEWOULDBLOCK;
        }

        bool SendPacket(
            uint32_t command,
            const void* payload,
            size_t payloadSize,
            bool incrementIndex,
            bool waitForReply = true,
            std::optional<uint32_t> headerRandom = std::nullopt)
        {
            if (!socket_ || payloadSize > 1024u)
                return false;
            if (incrementIndex)
                ++index_;

            KmBoxNetHeader header = {};
            header.hardwareKey = hardwareKey_;
            header.random = headerRandom.value_or(random_());
            header.index = index_;
            header.command = command;

            std::vector<uint8_t> packet(sizeof(header) + payloadSize);
            std::memcpy(packet.data(), &header, sizeof(header));
            if (payloadSize != 0)
                std::memcpy(packet.data() + sizeof(header), payload, payloadSize);

            const int sent = sendto(
                socket_.Get(),
                reinterpret_cast<const char*>(packet.data()),
                static_cast<int>(packet.size()),
                0,
                reinterpret_cast<const sockaddr*>(&destination_),
                sizeof(destination_));
            if (sent != static_cast<int>(packet.size())) {
                lastError_ = static_cast<DWORD>(WSAGetLastError());
                return false;
            }
            if (!waitForReply) {
                // Button timing is safety-critical: waiting for a UDP ACK here
                // would stretch LEFT DOWN beyond the requested hold interval.
                // The packet is emitted exactly once; any reply is filtered as
                // stale by the next acknowledged transaction.
                lastError_ = 0;
                return true;
            }

            std::array<uint8_t, 1024> response = {};
            sockaddr_in sender = {};
            int senderLength = sizeof(sender);
            // A command has already been emitted at this point, so never
            // resend it on a delayed ACK: that could duplicate a click.
            // Ignore stale/duplicated UDP replies and give one extra receive
            // window without repeating the output command.
            int datagramCount = 0;
            // Fire-and-forget button replies accumulate until the next move.
            // A 16-packet limit could disconnect a healthy device before its
            // matching ACK arrived. Allow bounded backlog/noise processing,
            // with an absolute deadline so traffic cannot extend this forever.
            const auto replyDeadline = std::chrono::steady_clock::now() +
                2 * kNetworkTimeout;
            while (datagramCount < 256) {
                const auto remaining = std::chrono::duration_cast<std::chrono::microseconds>(
                    replyDeadline - std::chrono::steady_clock::now()).count();
                if (remaining <= 0)
                    break;
                // A fixed SO_RCVTIMEO can overrun the deadline after a late
                // stale packet. Wait only for the remaining transaction time.
                fd_set readable;
                FD_ZERO(&readable);
                FD_SET(socket_.Get(), &readable);
                timeval wait = {};
                wait.tv_sec = static_cast<long>(remaining / 1'000'000);
                wait.tv_usec = static_cast<long>(remaining % 1'000'000);
                const int ready = select(0, &readable, nullptr, nullptr, &wait);
                if (ready == SOCKET_ERROR) {
                    lastError_ = static_cast<DWORD>(WSAGetLastError());
                    return false;
                }
                if (ready == 0 || std::chrono::steady_clock::now() >= replyDeadline)
                    break;
                sender = {};
                senderLength = sizeof(sender);
                const int received = recvfrom(
                    socket_.Get(),
                    reinterpret_cast<char*>(response.data()),
                    static_cast<int>(response.size()),
                    0,
                    reinterpret_cast<sockaddr*>(&sender),
                    &senderLength);
                if (received == SOCKET_ERROR) {
                    lastError_ = static_cast<DWORD>(WSAGetLastError());
                    if (!IsTimeoutError(lastError_))
                        return false;
                    continue;
                }
                ++datagramCount;
                if (received < static_cast<int>(sizeof(KmBoxNetHeader)) ||
                    sender.sin_addr.s_addr != destination_.sin_addr.s_addr ||
                    sender.sin_port != destination_.sin_port) {
                    lastError_ = WSAECONNRESET;
                    continue;
                }

                KmBoxNetHeader reply = {};
                std::memcpy(&reply, response.data(), sizeof(reply));
                if (reply.command != command || reply.index != index_) {
                    lastError_ = WSAECONNRESET;
                    continue;
                }
                lastError_ = 0;
                return true;
            }
            lastError_ = WSAETIMEDOUT;
            return false;
        }

        WinsockSession winsock_;
        UniqueSocket socket_;
        UniqueSocket monitorSocket_;
        uint16_t monitorPort_ = 0;
        uint64_t monitorUpdatedAtMs_ = 0;
        uint64_t monitorRetryAtMs_ = 0;
        uint8_t monitorMouseButtons_ = 0;
        uint64_t monitorPackets_ = 0;
        uint64_t monitorRejectedPackets_ = 0;
        uint32_t monitorLastPacketBytes_ = 0;
        DWORD monitorError_ = 0;
        int32_t outputButtons_ = 0;
        app::input::PhysicalKeyboardState monitorKeyboard_;
        sockaddr_in destination_ = {};
        uint32_t hardwareKey_ = 0;
        uint32_t index_ = 0;
        DWORD lastError_ = 0;
        std::string endpoint_;
        std::mt19937 random_ { std::random_device{}() };
    };

    std::unique_ptr<InputBackend> CreateBackend(DeviceKind kind)
    {
        switch (kind) {
        case DeviceKind::Makcu:
            return std::make_unique<MakcuBackend>();
        case DeviceKind::KmBox:
            return std::make_unique<KmBoxSerialBackend>();
        case DeviceKind::KmBoxNet:
        case DeviceKind::FerrumOne:
            // Ferrum App documents compatibility with these KMBox NET commands.
            // This is not the board's deprecated direct-serial Legacy API.
            return std::make_unique<KmBoxNetBackend>();
        default:
            return {};
        }
    }

    template<class IsCancelled, class Wait>
    bool RunCircularMovementTest(InputBackend& backend, IsCancelled cancelled, Wait wait)
    {
        if (!backend.IsAlive())
            return false;
        const std::vector<app::input::MouseDelta> path =
            app::input::BuildCircularTestPath();
        for (const auto& delta : path) {
            if (cancelled() || !backend.Move(delta.x, delta.y))
                return false;
            if (!wait(backend.TestStepDelay()))
                return false;
        }
        return !cancelled() && backend.Probe();
    }

    enum class CommandAction : uint8_t
    {
        Reconfigure,
        ConnectAndTest,
        Disconnect,
        TestMovement,
    };

    struct ServiceCommand
    {
        CommandAction action = CommandAction::Reconfigure;
        DeviceKind kind = DeviceKind::Makcu;
        uint64_t generation = 0;
        KmBoxNetConfig networkConfig;
    };

    enum class LeftClickPhase : uint8_t
    {
        Idle,
        Queued,
        Dispatching,
        Down,
        Releasing,
    };

    class InputDeviceService
    {
    public:
        InputDeviceService()
            : worker_(&InputDeviceService::WorkerLoop, this)
        {
            status_.selected = DeviceKind::Makcu;
        }

        ~InputDeviceService()
        {
            Shutdown();
        }

        void SetSelected(DeviceKind kind)
        {
            kind = app::input::SanitizeSelectableDeviceKind(
                static_cast<int>(kind));
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || status_.selected == kind)
                return;
            ++generation_;
            queue_.clear();
            ClearRealtimeLocked();
            queue_.push_back({
                CommandAction::Reconfigure,
                kind,
                generation_,
                networkConfig_
            });
            status_ = {};
            status_.selected = kind;
            condition_.notify_one();
        }

        void SetNetworkConfig(KmBoxNetConfig config)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || networkConfig_ == config)
                return;
            networkConfig_ = std::move(config);
            if (!app::input::IsNetworkDeviceKind(status_.selected))
                return;
            ++generation_;
            queue_.clear();
            ClearRealtimeLocked();
            queue_.push_back({
                CommandAction::Reconfigure,
                status_.selected,
                generation_,
                networkConfig_
            });
            status_.state = ConnectionState::Disconnected;
            status_.error = DeviceError::None;
            status_.port.clear();
            status_.systemError = 0;
            condition_.notify_one();
        }

        DeviceStatus GetStatus() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return GetStatusLocked();
        }

        app::input::KeyState ActivationKeyState(int key, app::input::KeyState primary)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return activationKeys_.Read(GetStatusLocked(), key, primary, GetTickCount64());
        }

        app::input::LeftClickTiming GetLeftClickTiming() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            app::input::LeftClickTiming timing;
            timing.calibrated = leftClickTimingSamples_ != 0u;
            timing.dispatchLatencyMs = static_cast<float>(
                leftClickDispatchMeanUs_ / 1000.0);
            timing.dispatchJitterMs = static_cast<float>(
                leftClickDispatchDeviationUs_ / 1000.0);
            timing.samples = leftClickTimingSamples_;
            return timing;
        }

        app::input::LeftClickStatus GetLeftClickStatus() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            app::input::LeftClickStatus status;
            status.active = leftClickPhase_ != LeftClickPhase::Idle;
            status.outputDown = leftButtonOutputDown_;
            status.token = status.active ? leftClickToken_ : 0u;
            return status;
        }

        bool HardwareKeyDown(int virtualKey) const
        {
            const uint8_t mask = app::input::VirtualKeyToMouseButtonMask(
                virtualKey);
            if (mask == 0)
                return false;
            const auto status = GetStatus();
            return status.state == ConnectionState::Connected &&
                   status.physicalButtonsAvailable &&
                   (status.physicalButtonMask & mask) != 0;
        }

        bool ConnectAndTest()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ ||
                !app::input::IsSelectableDeviceKind(
                    static_cast<int>(status_.selected))) {
                return false;
            }
            ++generation_;
            queue_.clear();
            ClearRealtimeLocked();
            queue_.push_back({
                CommandAction::ConnectAndTest,
                status_.selected,
                generation_,
                networkConfig_
            });
            status_.state = ConnectionState::Connecting;
            status_.error = DeviceError::None;
            status_.port.clear();
            status_.systemError = 0;
            condition_.notify_one();
            return true;
        }

        bool Disconnect()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_)
                return false;
            ++generation_;
            queue_.clear();
            ClearRealtimeLocked();
            queue_.push_back({
                CommandAction::Disconnect,
                status_.selected,
                generation_,
                networkConfig_
            });
            status_.state = ConnectionState::Disconnected;
            status_.error = DeviceError::None;
            status_.port.clear();
            status_.systemError = 0;
            condition_.notify_one();
            return true;
        }

        bool TestMovement()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || status_.state != ConnectionState::Connected ||
                leftClickPhase_ != LeftClickPhase::Idle ||
                leftButtonActionInFlight_ || leftButtonOutputDown_ ||
                pendingLeftButton_ >= 0) {
                return false;
            }
            queue_.push_back({
                CommandAction::TestMovement,
                status_.selected,
                generation_,
                networkConfig_
            });
            status_.state = ConnectionState::Testing;
            condition_.notify_one();
            return true;
        }

        bool Move(int deltaX, int deltaY, uint64_t validForUs)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || status_.state != ConnectionState::Connected)
                return false;
            if (deltaX == 0 && deltaY == 0) {
                CancelPendingMovesLocked();
                return true;
            }
            const bool replaced = pendingMove_.HasPending();
            if (!pendingMove_.Submit(deltaX, deltaY, InputNowUs(), validForUs))
                return false;
            if (replaced) ++status_.moveReplacements;
            ++status_.moveRequests;
            condition_.notify_one();
            return true;
        }

        void CancelPendingMoves()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            CancelPendingMovesLocked();
        }

        bool LeftButton(bool pressed)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ || status_.state != ConnectionState::Connected)
                return false;

            if (pressed &&
                (leftClickPhase_ != LeftClickPhase::Idle ||
                 leftButtonActionInFlight_)) {
                return false;
            }
            if (!pressed) {
                if (leftClickPhase_ == LeftClickPhase::Queued) {
                    ClearLeftClickLocked();
                    // No DOWN has left the worker yet, so emitting an UP here
                    // could release a physical button that this service does
                    // not own.
                    pendingLeftButton_ = -1;
                    condition_.notify_one();
                    return true;
                } else if (leftClickPhase_ == LeftClickPhase::Releasing) {
                    return true;
                } else if (leftClickPhase_ != LeftClickPhase::Idle) {
                    leftClickCancelRequested_ = true;
                } else if (!leftButtonOutputDown_ &&
                           !leftButtonActionInFlight_) {
                    // An idempotent release with no owned/pending DOWN must not
                    // generate a stray UP on the hardware device.
                    pendingLeftButton_ = -1;
                    return true;
                }
            }
            pendingLeftButton_ = pressed ? 1 : 0;
            pendingRealtimeGeneration_ = generation_;
            condition_.notify_one();
            return true;
        }

        app::input::ClickRequestResult LeftClick(uint32_t holdMs, bool requireIdleOutput,
            uint64_t validForUs = app::input::kMaximumQueuedMoveAgeUs)
        {
            const uint64_t requestedAtUs = InputNowUs();
            std::lock_guard<std::mutex> lock(mutex_);
            using Result = app::input::ClickRequestResult;
            if (stopping_ || status_.state != ConnectionState::Connected)
                return Result::Unavailable;
            validForUs = std::min(validForUs, app::input::kMaximumQueuedMoveAgeUs);
            if (validForUs == 0 || InputNowUs() - requestedAtUs >= validForUs)
                return Result::Unavailable;
            bool primaryLeftButtonDown = false;
#if !defined(KEVQ_INPUT_DEVICE_TESTING)
            primaryLeftButtonDown =
                app::input::IsPrimaryKeyDown(VK_LBUTTON);
#endif
            if ((status_.physicalButtonsAvailable &&
                 (status_.physicalButtonMask & 0x01u) != 0u) ||
                primaryLeftButtonDown)
                return Result::ManualInput;
            if ((requireIdleOutput && (pendingMove_.HasPending() || status_.moveInFlight ||
                    status_.probeInFlight)) || leftClickPhase_ != LeftClickPhase::Idle ||
                leftButtonActionInFlight_ || leftButtonOutputDown_ ||
                pendingLeftButton_ >= 0) {
                return Result::Busy;
            }

            CancelPendingMovesLocked();
            leftClickPhase_ = LeftClickPhase::Queued;
            leftClickCancelRequested_ = false;
            leftClickHoldMs_ = std::clamp(
                holdMs,
                kMinimumLeftClickHoldMs,
                kMaximumLeftClickHoldMs);
            leftClickGeneration_ = generation_;
            leftClickToken_ = nextLeftClickToken_++;
            if (leftClickToken_ == 0)
                leftClickToken_ = nextLeftClickToken_++;
            leftClickQueuedAt_ = std::chrono::steady_clock::now();
            leftClickExpiresAtUs_ = requestedAtUs + validForUs;
            condition_.notify_one();
            return Result::Queued;
        }

        void Shutdown()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_)
                    return;
                stopping_ = true;
                queue_.clear();
                ClearRealtimeLocked();
                condition_.notify_one();
            }
            if (worker_.joinable())
                worker_.join();
        }

    private:
        DeviceStatus GetStatusLocked() const
        {
            auto result = status_;
            result.movePending = pendingMove_.HasPending();
            if (stopping_) result.state = ConnectionState::Disconnected;
            if ((app::input::IsNetworkDeviceKind(result.selected) ||
                 result.selected == DeviceKind::KmBox) &&
                !app::input::IsNetworkInputFresh(result.physicalInputUpdatedAtMs, GetTickCount64())) {
                result.physicalButtonsAvailable = false;
                result.physicalKeyboard.available = false;
            }
            return result;
        }

        void ClearLeftClickLocked()
        {
            leftClickPhase_ = LeftClickPhase::Idle;
            leftClickCancelRequested_ = false;
            leftClickHoldMs_ = 0;
            leftClickGeneration_ = 0;
            leftClickToken_ = 0;
            leftClickQueuedAt_ = {};
            leftClickReleaseAt_ = {};
            leftClickExpiresAtUs_ = 0;
        }

        void CancelPendingMovesLocked()
        {
            if (pendingMove_.Cancel()) ++status_.moveCancellations;
        }

        void ClearRealtimeLocked()
        {
            pendingMove_.Cancel();
            pendingLeftButton_ = -1;
            pendingRealtimeGeneration_ = 0;
            ClearLeftClickLocked();
            leftClickDispatchMeanUs_ = 0.0;
            leftClickDispatchDeviationUs_ = 0.0;
            leftClickTimingSamples_ = 0;
            status_.physicalButtonMask = 0;
            status_.physicalButtonsAvailable = false;
            status_.physicalKeyboard = {};
            status_.physicalInputUpdatedAtMs = 0;
            status_.inputMonitorPort = 0;
            status_.inputMonitorPackets = 0;
            status_.inputMonitorRejectedPackets = 0;
            status_.inputMonitorReceivedBytes = 0;
            status_.inputMonitorLastPacketBytes = 0;
            status_.inputMonitorError = 0;
            status_.moveRequests = 0;
            status_.moveCompletions = 0;
            status_.moveReplacements = 0;
            status_.moveCancellations = 0;
            status_.moveExpirations = 0;
            status_.moveInFlight = false;
            status_.probeInFlight = false;
            status_.lastMoveQueueAgeUs = 0;
            activationKeys_ = {};
        }

        bool IsCurrent(const ServiceCommand& command) const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return !stopping_ &&
                   generation_ == command.generation &&
                   status_.selected == command.kind;
        }

        void Publish(
            const ServiceCommand& command,
            ConnectionState state,
            DeviceError error,
            std::string endpoint = {},
            DWORD systemError = 0)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_ ||
                generation_ != command.generation ||
                status_.selected != command.kind) {
                return;
            }
            status_.state = state;
            status_.error = error;
            status_.port = std::move(endpoint);
            status_.systemError = systemError;
        }

        void WorkerLoop()
        {
            // Mouse/button release deadlines are latency-sensitive. Keep the
            // worker responsive during short render/DMA CPU spikes without
            // using a real-time priority that could starve the system.
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
            std::unique_ptr<InputBackend> backend;
            ServiceCommand connectedCommand;
            std::string connectedEndpoint;
            auto nextHealthProbe = std::chrono::steady_clock::time_point::max();
            auto leftButtonWatchdogAt =
                std::chrono::steady_clock::time_point::max();

            const auto disconnectBackend = [&] {
                bool releaseLeftButton = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    releaseLeftButton = leftButtonOutputDown_;
                }
                if (backend && releaseLeftButton)
                    backend->SetLeftButton(false);
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    leftButtonOutputDown_ = false;
                    leftButtonActionInFlight_ = false;
                    ClearLeftClickLocked();
                }
                leftButtonWatchdogAt =
                    std::chrono::steady_clock::time_point::max();
                if (!backend)
                    return;
                backend->Disconnect();
                backend.reset();
            };

            const auto pollPhysicalButtons = [&]() -> bool {
                if (!backend)
                    return true;
                uint8_t buttonMask = 0;
                bool available = false;
                if (!backend->PollPhysicalButtons(&buttonMask, &available))
                    return false;
                std::lock_guard<std::mutex> lock(mutex_);
                if (!stopping_ &&
                    generation_ == connectedCommand.generation &&
                    status_.selected == connectedCommand.kind) {
                    status_.physicalButtonMask = buttonMask;
                    status_.physicalButtonsAvailable = available;
                    status_.physicalKeyboard = backend->PhysicalKeyboard();
                    status_.physicalInputUpdatedAtMs = backend->PhysicalInputUpdatedAtMs();
                    status_.inputMonitorPort = backend->InputMonitorPort();
                    backend->PopulateInputDiagnostics(status_);
                }
                return true;
            };

            for (;;) {
                ServiceCommand command;
                bool hasCommand = false;
                bool hasMove = false;
                bool hasLeftButton = false;
                bool hasClickStart = false;
                bool completesClick = false;
                app::input::QueuedMouseMove move;
                bool leftButtonPressed = false;
                uint64_t realtimeGeneration = 0;
                uint64_t clickToken = 0;
                uint32_t clickHoldMs = 0;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    const auto waitInterval = backend
                        ? kPhysicalInputPollInterval
                        : std::chrono::milliseconds(250);
                    condition_.wait_for(lock, waitInterval, [&] {
                        const auto now = std::chrono::steady_clock::now();
                        return stopping_ || !queue_.empty() ||
                               (pendingMove_.HasPending() && !leftButtonOutputDown_ &&
                                leftClickPhase_ == LeftClickPhase::Idle) ||
                               pendingLeftButton_ >= 0 ||
                               leftClickPhase_ == LeftClickPhase::Queued ||
                               (leftClickPhase_ == LeftClickPhase::Down &&
                                (leftClickCancelRequested_ ||
                                 now >= leftClickReleaseAt_)) ||
                               (leftButtonOutputDown_ &&
                                now >= leftButtonWatchdogAt);
                    });
                    if (stopping_)
                        break;

                    const auto now = std::chrono::steady_clock::now();
                    if (pendingLeftButton_ == 0) {
                        hasLeftButton = true;
                        leftButtonPressed = false;
                        realtimeGeneration = pendingRealtimeGeneration_;
                        pendingLeftButton_ = -1;
                        leftButtonActionInFlight_ = true;
                        if (leftClickPhase_ == LeftClickPhase::Queued) {
                            ClearLeftClickLocked();
                        } else if (leftClickPhase_ != LeftClickPhase::Idle) {
                            clickToken = leftClickToken_;
                            leftClickCancelRequested_ = true;
                            leftClickPhase_ = LeftClickPhase::Releasing;
                            completesClick = true;
                        }
                    } else if (leftClickPhase_ == LeftClickPhase::Down &&
                               (leftClickCancelRequested_ ||
                                now >= leftClickReleaseAt_)) {
                        hasLeftButton = true;
                        leftButtonPressed = false;
                        realtimeGeneration = leftClickGeneration_;
                        clickToken = leftClickToken_;
                        leftClickPhase_ = LeftClickPhase::Releasing;
                        leftButtonActionInFlight_ = true;
                        completesClick = true;
                    } else if (leftButtonOutputDown_ &&
                               now >= leftButtonWatchdogAt) {
                        hasLeftButton = true;
                        leftButtonPressed = false;
                        realtimeGeneration = connectedCommand.generation;
                        leftButtonActionInFlight_ = true;
                        if (leftClickPhase_ != LeftClickPhase::Idle) {
                            clickToken = leftClickToken_;
                            leftClickCancelRequested_ = true;
                            leftClickPhase_ = LeftClickPhase::Releasing;
                            completesClick = true;
                        }
                    } else if (!queue_.empty()) {
                        command = std::move(queue_.front());
                        queue_.pop_front();
                        hasCommand = true;
                    } else if (pendingLeftButton_ > 0) {
                        hasLeftButton = true;
                        leftButtonPressed = true;
                        pendingLeftButton_ = -1;
                        realtimeGeneration = pendingRealtimeGeneration_;
                        leftButtonActionInFlight_ = true;
                    } else if (leftClickPhase_ == LeftClickPhase::Queued) {
                        hasClickStart = true;
                        realtimeGeneration = leftClickGeneration_;
                        clickToken = leftClickToken_;
                        clickHoldMs = leftClickHoldMs_;
                        leftClickPhase_ = LeftClickPhase::Dispatching;
                    } else if (pendingMove_.HasPending() && !leftButtonOutputDown_ &&
                               leftClickPhase_ == LeftClickPhase::Idle) {
                        move = *pendingMove_.Take();
                        hasMove = true;
                        status_.moveInFlight = true;
                        realtimeGeneration = generation_;
                    }
                }

                if (hasClickStart) {
                    bool current =
                        backend &&
                        realtimeGeneration == connectedCommand.generation &&
                        IsCurrent(connectedCommand) &&
                        backend->IsAlive();
                    if (current) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        current = leftClickToken_ == clickToken &&
                            leftClickPhase_ == LeftClickPhase::Dispatching &&
                            !leftClickCancelRequested_ && generation_ == realtimeGeneration &&
                            InputNowUs() < leftClickExpiresAtUs_;
                    }
                    const bool sent = current && backend->SetLeftButton(true);
                    const auto sentAt = std::chrono::steady_clock::now();
                    bool releaseImmediately = false;
                    if (sent) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        leftButtonOutputDown_ = true;
                        const bool clickStillCurrent =
                            leftClickPhase_ == LeftClickPhase::Dispatching &&
                            leftClickToken_ == clickToken &&
                            leftClickGeneration_ == realtimeGeneration &&
                            generation_ == realtimeGeneration &&
                            !leftClickCancelRequested_;
                        if (clickStillCurrent) {
                            if (leftClickQueuedAt_ !=
                                std::chrono::steady_clock::time_point{}) {
                                const double sampleUs = static_cast<double>(
                                    std::chrono::duration_cast<
                                        std::chrono::microseconds>(
                                            sentAt - leftClickQueuedAt_)
                                        .count());
                                if (sampleUs >= 0.0 && sampleUs <= 250000.0) {
                                    constexpr double kTimingAlpha = 0.20;
                                    if (leftClickTimingSamples_ == 0u) {
                                        leftClickDispatchMeanUs_ = sampleUs;
                                        leftClickDispatchDeviationUs_ = 0.0;
                                    } else {
                                        const double delta =
                                            sampleUs - leftClickDispatchMeanUs_;
                                        leftClickDispatchMeanUs_ +=
                                            kTimingAlpha * delta;
                                        leftClickDispatchDeviationUs_ +=
                                            kTimingAlpha *
                                            (std::fabs(delta) -
                                             leftClickDispatchDeviationUs_);
                                    }
                                    ++leftClickTimingSamples_;
                                }
                            }
                            leftClickPhase_ = LeftClickPhase::Down;
                            leftClickReleaseAt_ = sentAt +
                                std::chrono::milliseconds(clickHoldMs);
                        } else {
                            releaseImmediately = true;
                            if (leftClickToken_ == clickToken)
                                leftClickPhase_ = LeftClickPhase::Releasing;
                        }
                    }
                    if (sent) {
                        leftButtonWatchdogAt = sentAt + kLeftButtonWatchdog;
                    } else {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (leftClickToken_ == clickToken)
                            ClearLeftClickLocked();
                    }

                    if (sent && releaseImmediately) {
                        const bool released = backend->SetLeftButton(false);
                        {
                            std::lock_guard<std::mutex> lock(mutex_);
                            if (released)
                                leftButtonOutputDown_ = false;
                            if (pendingLeftButton_ == 0)
                                pendingLeftButton_ = -1;
                            if (leftClickToken_ == clickToken)
                                ClearLeftClickLocked();
                        }
                        if (released) {
                            leftButtonWatchdogAt =
                                std::chrono::steady_clock::time_point::max();
                        } else {
                            const DWORD error = backend->LastSystemError();
                            disconnectBackend();
                            Publish(
                                connectedCommand,
                                ConnectionState::Error,
                                DeviceError::IoFailure,
                                connectedEndpoint,
                                error);
                            continue;
                        }
                    }

                    if (!sent && current) {
                        const DWORD error = backend->LastSystemError();
                        disconnectBackend();
                        Publish(
                            connectedCommand,
                            ConnectionState::Error,
                            DeviceError::IoFailure,
                            connectedEndpoint,
                            error);
                    } else if (sent && !pollPhysicalButtons()) {
                        const DWORD error = backend->LastSystemError();
                        disconnectBackend();
                        Publish(
                            connectedCommand,
                            ConnectionState::Error,
                            DeviceError::IoFailure,
                            connectedEndpoint,
                            error);
                    }
                    continue;
                }

                if (hasMove || hasLeftButton) {
                    if (hasMove) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        const auto nowUs = InputNowUs();
                        if (generation_ != realtimeGeneration || !pendingMove_.IsCurrent(move)) {
                            status_.moveInFlight = false;
                            continue;
                        }
                        if (!app::input::LatestMoveMailbox::IsFresh(move, nowUs)) {
                            ++status_.moveExpirations;
                            status_.moveInFlight = false;
                            continue;
                        }
                        status_.lastMoveQueueAgeUs = nowUs - move.submittedAtUs;
                    }
                    const bool current =
                        backend &&
                        realtimeGeneration == connectedCommand.generation &&
                        IsCurrent(connectedCommand) &&
                        backend->IsAlive();
                    const bool sent = current &&
                        (hasMove
                            ? backend->Move(move.x, move.y)
                            : backend->SetLeftButton(leftButtonPressed));
                    if (hasMove) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        status_.moveInFlight = false;
                        if (sent && generation_ == realtimeGeneration)
                            ++status_.moveCompletions;
                    }
                    if (sent && hasLeftButton) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        leftButtonOutputDown_ = leftButtonPressed;
                        leftButtonActionInFlight_ = false;
                        if (completesClick && leftClickToken_ == clickToken)
                            ClearLeftClickLocked();
                        leftButtonWatchdogAt = leftButtonPressed
                            ? std::chrono::steady_clock::now() +
                                kLeftButtonWatchdog
                            : std::chrono::steady_clock::time_point::max();
                    } else if (hasLeftButton) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        leftButtonActionInFlight_ = false;
                        if (completesClick && leftClickToken_ == clickToken)
                            ClearLeftClickLocked();
                    }
                    if (!sent && current) {
                        const DWORD error = backend->LastSystemError();
                        disconnectBackend();
                        Publish(
                            connectedCommand,
                            ConnectionState::Error,
                            DeviceError::IoFailure,
                            connectedEndpoint,
                            error);
                    } else if (sent && !pollPhysicalButtons()) {
                        const DWORD error = backend->LastSystemError();
                        disconnectBackend();
                        Publish(
                            connectedCommand,
                            ConnectionState::Error,
                            DeviceError::IoFailure,
                            connectedEndpoint,
                            error);
                    }
                    continue;
                }

                if (!hasCommand) {
                    if (!backend)
                        continue;
                    const auto now = std::chrono::steady_clock::now();
                    const bool locallyAlive = backend->IsAlive();
                    bool probeDue = now >= nextHealthProbe;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        probeDue = probeDue && !leftButtonOutputDown_ &&
                            leftClickPhase_ == LeftClickPhase::Idle;
                        status_.probeInFlight = probeDue;
                    }
                    const bool healthy = locallyAlive && pollPhysicalButtons() &&
                        (!probeDue || backend->Probe());
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        status_.probeInFlight = false;
                    }
                    if (!healthy) {
                        const DWORD error = backend->LastSystemError();
                        disconnectBackend();
                        Publish(
                            connectedCommand,
                            ConnectionState::Error,
                            DeviceError::Disconnected,
                            connectedEndpoint,
                            error);
                    } else if (probeDue) {
                        nextHealthProbe = now + kHealthProbeInterval;
                    }
                    continue;
                }

                if (command.action == CommandAction::Reconfigure ||
                    command.action == CommandAction::Disconnect) {
                    disconnectBackend();
                    nextHealthProbe = std::chrono::steady_clock::time_point::max();
                    Publish(
                        command,
                        ConnectionState::Disconnected,
                        DeviceError::None);
                    continue;
                }

                if (command.action == CommandAction::ConnectAndTest) {
                    disconnectBackend();
                    backend = CreateBackend(command.kind);
                    if (!backend) {
                        Publish(
                            command,
                            ConnectionState::Error,
                            DeviceError::Unsupported);
                        continue;
                    }

                    Publish(
                        command,
                        ConnectionState::Connecting,
                        DeviceError::None);
                    const ConnectionResult result = backend->Connect(
                        command.networkConfig);
                    if (!IsCurrent(command)) {
                        disconnectBackend();
                        continue;
                    }
                    if (!result.success) {
                        disconnectBackend();
                        Publish(
                            command,
                            ConnectionState::Error,
                            result.error,
                            result.endpoint,
                            result.systemError);
                        continue;
                    }

                    Publish(
                        command,
                        ConnectionState::Testing,
                        DeviceError::None,
                        result.endpoint);
                    if (!RunMovementTest(*backend, command)) {
                        const DWORD error = backend->LastSystemError();
                        disconnectBackend();
                        Publish(
                            command,
                            ConnectionState::Error,
                            DeviceError::IoFailure,
                            result.endpoint,
                            error);
                        continue;
                    }

                    connectedCommand = command;
                    connectedEndpoint = result.endpoint;
                    nextHealthProbe =
                        std::chrono::steady_clock::now() + kHealthProbeInterval;
                    Publish(
                        command,
                        ConnectionState::Connected,
                        DeviceError::None,
                        result.endpoint);
                    pollPhysicalButtons();
                    continue;
                }

                if (command.action == CommandAction::TestMovement) {
                    if (!backend ||
                        !IsCurrent(command) ||
                        !backend->IsAlive()) {
                        Publish(
                            command,
                            ConnectionState::Error,
                            DeviceError::Disconnected,
                            connectedEndpoint);
                        continue;
                    }
                    Publish(
                        command,
                        ConnectionState::Testing,
                        DeviceError::None,
                        connectedEndpoint);
                    if (!RunMovementTest(*backend, command)) {
                        const DWORD error = backend->LastSystemError();
                        disconnectBackend();
                        Publish(
                            command,
                            ConnectionState::Error,
                            DeviceError::IoFailure,
                            connectedEndpoint,
                            error);
                    } else {
                        nextHealthProbe =
                            std::chrono::steady_clock::now() + kHealthProbeInterval;
                        Publish(
                            command,
                            ConnectionState::Connected,
                            DeviceError::None,
                            connectedEndpoint);
                    }
                }
            }

            disconnectBackend();
        }

        bool RunMovementTest(InputBackend& backend, const ServiceCommand& command)
        {
            return RunCircularMovementTest(backend,
                [&] { return !IsCurrent(command); },
                [&](auto delay) {
                    std::unique_lock<std::mutex> lock(mutex_);
                    return !condition_.wait_for(lock, delay, [&] {
                        return stopping_ || generation_ != command.generation ||
                            status_.selected != command.kind;
                    });
                });
        }

        mutable std::mutex mutex_;
        std::condition_variable condition_;
        std::deque<ServiceCommand> queue_;
        DeviceStatus status_ = {};
        app::input::ActivationKeyRouter activationKeys_;
        KmBoxNetConfig networkConfig_;
        uint64_t generation_ = 0;
        app::input::LatestMoveMailbox pendingMove_;
        int pendingLeftButton_ = -1;
        uint64_t pendingRealtimeGeneration_ = 0;
        LeftClickPhase leftClickPhase_ = LeftClickPhase::Idle;
        bool leftClickCancelRequested_ = false;
        bool leftButtonActionInFlight_ = false;
        bool leftButtonOutputDown_ = false;
        uint32_t leftClickHoldMs_ = 0;
        uint64_t leftClickGeneration_ = 0;
        uint64_t leftClickToken_ = 0;
        uint64_t nextLeftClickToken_ = 1;
        uint64_t leftClickExpiresAtUs_ = 0;
        std::chrono::steady_clock::time_point leftClickQueuedAt_ = {};
        std::chrono::steady_clock::time_point leftClickReleaseAt_ = {};
        double leftClickDispatchMeanUs_ = 0.0;
        double leftClickDispatchDeviationUs_ = 0.0;
        uint64_t leftClickTimingSamples_ = 0;
        bool stopping_ = false;
        std::thread worker_;
    };

    InputDeviceService& Service()
    {
        static InputDeviceService service;
        return service;
    }
}

void app::input::SetSelectedDevice(DeviceKind kind)
{
    Service().SetSelected(kind);
}

void app::input::SetKmBoxNetConfig(KmBoxNetConfig config)
{
    Service().SetNetworkConfig(std::move(config));
}

app::input::DeviceStatus app::input::GetDeviceStatus()
{
    return Service().GetStatus();
}

app::input::LeftClickTiming app::input::GetLeftClickTiming()
{
    return Service().GetLeftClickTiming();
}

app::input::LeftClickStatus app::input::GetLeftClickStatus()
{
    return Service().GetLeftClickStatus();
}

bool app::input::RequestConnectAndTest()
{
    return Service().ConnectAndTest();
}

bool app::input::RequestDisconnect()
{
    return Service().Disconnect();
}

bool app::input::RequestMovementTest()
{
    return Service().TestMovement();
}

bool app::input::RequestMove(int deltaX, int deltaY, uint64_t validForUs)
{
    return Service().Move(deltaX, deltaY, validForUs);
}

void app::input::CancelPendingMoves()
{
    Service().CancelPendingMoves();
}

bool app::input::RequestLeftButton(bool pressed)
{
    return Service().LeftButton(pressed);
}

bool app::input::RequestLeftClick(uint32_t holdMs)
{
    return Service().LeftClick(holdMs, false) == ClickRequestResult::Queued;
}

app::input::ClickRequestResult app::input::TryRequestLeftClick(uint32_t holdMs, uint64_t validForUs)
{
    return Service().LeftClick(holdMs, true, validForUs);
}

bool app::input::IsHardwareKeyDown(int virtualKey)
{
    return Service().HardwareKeyDown(virtualKey);
}

app::input::KeyState app::input::ReadActivationKeyState(int virtualKey)
{
    if (virtualKey < 1 || virtualKey > 0xFE)
        return {};
#if defined(KEVQ_INPUT_DEVICE_TESTING)
    const KeyState primary = {};
#else
    const KeyState primary = ReadPrimaryKeyState(virtualKey);
#endif
    return Service().ActivationKeyState(virtualKey, primary);
}

bool app::input::IsActivationKeyDown(int virtualKey)
{
    const auto key = ReadActivationKeyState(virtualKey);
    return key.available && key.down;
}

bool app::input::IsControlKeyDown(int virtualKey)
{
    return IsActivationKeyDown(virtualKey) ||
        (virtualKey >= 0 && virtualKey <= 0xFF &&
         (GetAsyncKeyState(virtualKey) & 0x8000) != 0);
}

void app::input::Shutdown()
{
    Service().Shutdown();
}
