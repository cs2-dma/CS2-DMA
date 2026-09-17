#include "app/Platform/overlay.h"
#include "app/Core/build_info.h"
#include "app/Core/globals.h"
#include "app/Config/config.h"
#include "app/Config/project_paths.h"
#include "app/Localization/localization.h"
#include "app/Input/input_device.h"
#include "app/Platform/win_handle.h"
#include "Features/ESP/esp.h"
#include "Features/ESP/Render/weapon_icon_atlas.h"
#include "Features/Target/target.h"
#include "app/UI/MenuShell/ui_icons.h"
#include "app/UI/MenuShell/ui_style.h"
#include "Features/WebRadar/webradar.h"
#include "fonts/weapons.hpp"

#include <Windows.h>
#include <wrl/client.h>
#include <d3d11.h>
#include <dxgi1_5.h>
#include <dwmapi.h>
#include <emmintrin.h>
#include <algorithm>
#include <cstdint>

#pragma comment(lib, "dwmapi.lib")

using Microsoft::WRL::ComPtr;
#include <filesystem>
#include <fstream>
#include <string>
#include <chrono>
#include <cstdio>
#include <thread>
#include <atomic>
#include <limits>
#include <mutex>
#include <vector>
#include <ShlObj.h>


#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static HWND                       s_hwnd = nullptr;
static ComPtr<ID3D11Device>       s_device;
static ComPtr<ID3D11DeviceContext> s_context;
static ComPtr<IDXGISwapChain1>    s_swapChain;
static ComPtr<IDXGISwapChain2>    s_swapChain2;
static ComPtr<ID3D11RenderTargetView> s_rtv;
static bool                    s_tearingSupported = false;
static bool                    s_waitableSwapChainEnabled = false;
static bool                    s_overlayVisible = true;
static bool                    s_windowClassRegistered = false;
static bool                    s_imguiContextCreated = false;
static bool                    s_imguiWin32Initialized = false;
static bool                    s_imguiDx11Initialized = false;
static app::platform::UniqueWinHandle s_frameLatencyWaitableObject;
static std::atomic<uint64_t>   s_overlayFrameUs = 0;
static std::atomic<uint64_t>   s_overlayMaxFrameUs = 0;
static std::atomic<uint64_t>   s_overlaySyncUs = 0;
static std::atomic<uint64_t>   s_overlayDrawUs = 0;
static std::atomic<uint64_t>   s_overlayPresentUs = 0;
static std::atomic<uint64_t>   s_overlayPacingWaitUs = 0;

class OverlayDeadlineTimer
{
public:
    OverlayDeadlineTimer() noexcept
    {
        constexpr DWORD kHighResolutionTimerFlag = 0x00000002u;
        timer_ = CreateWaitableTimerExW(
            nullptr,
            nullptr,
            kHighResolutionTimerFlag,
            TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (!timer_)
            timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }

    ~OverlayDeadlineTimer() noexcept
    {
        if (timer_)
            CloseHandle(timer_);
    }

    template <typename Duration>
    bool WaitFor(Duration duration) noexcept
    {
        if (!timer_ || duration <= Duration::zero())
            return false;
        using HundredNanoseconds =
            std::chrono::duration<LONGLONG, std::ratio<1, 10000000>>;
        LARGE_INTEGER dueTime = {};
        dueTime.QuadPart = -(std::max)(
            static_cast<LONGLONG>(1),
            std::chrono::duration_cast<HundredNanoseconds>(duration).count());
        if (!SetWaitableTimer(timer_, &dueTime, 0, nullptr, nullptr, FALSE))
            return false;
        return WaitForSingleObject(timer_, INFINITE) == WAIT_OBJECT_0;
    }

private:
    HANDLE timer_ = nullptr;
};

static void WaitForOverlayDeadline(
    const std::chrono::steady_clock::time_point deadline)
{
    using Clock = std::chrono::steady_clock;
    constexpr auto kSpinWindow = std::chrono::microseconds(150);
    const auto waitTarget = deadline - kSpinWindow;
    const auto now = Clock::now();
    if (waitTarget > now) {
        static thread_local OverlayDeadlineTimer timer;
        if (!timer.WaitFor(waitTarget - now))
            std::this_thread::sleep_until(waitTarget);
    }
    while (Clock::now() < deadline)
        _mm_pause();
}

static bool RecreateRenderTarget(UINT width, UINT height)
{
    if (!s_swapChain || !s_device || !s_context)
        return false;

    s_rtv.Reset();
    s_context->OMSetRenderTargets(0, nullptr, nullptr);

    const UINT flags =
        (s_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u) |
        (s_waitableSwapChainEnabled ? DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT : 0u);
    const HRESULT resizeResult =
        s_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, flags);
    if (FAILED(resizeResult))
        return false;

    ComPtr<ID3D11Texture2D> backBuffer;
    if (FAILED(s_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) || !backBuffer)
        return false;

    const HRESULT viewResult =
        s_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &s_rtv);
    return SUCCEEDED(viewResult) && s_rtv;
}

static void ApplyOverlayWindowMode(bool show)
{
    if (!s_hwnd || !s_swapChain)
        return;

    if (show) {
        ShowWindow(s_hwnd, SW_SHOW);
        SetWindowPos(s_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
        ComPtr<IDXGISwapChain> baseSwapChain;
        if (SUCCEEDED(s_swapChain.As(&baseSwapChain))) {
            BOOL fullscreen = FALSE;
            if (SUCCEEDED(baseSwapChain->GetFullscreenState(&fullscreen, nullptr)) &&
                fullscreen) {
                baseSwapChain->SetFullscreenState(FALSE, nullptr);
            }
        }

        
        
        s_rtv.Reset();
        s_context->OMSetRenderTargets(0, nullptr, nullptr);
        ComPtr<ID3D11Texture2D> backBuffer;
        if (SUCCEEDED(s_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer))))
            s_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &s_rtv);
    } else {
        ComPtr<IDXGISwapChain> baseSwapChain;
        if (SUCCEEDED(s_swapChain.As(&baseSwapChain)))
            baseSwapChain->SetFullscreenState(FALSE, nullptr);
        ShowWindow(s_hwnd, SW_HIDE);
    }

    s_overlayVisible = show;
}

namespace
{
    struct MonitorData {
        HMONITOR handle = nullptr;
        RECT rect = {};
        bool isPrimary = false;
        std::wstring name;
        std::wstring friendlyName;
    };

    std::mutex s_monitorCacheMutex;
    std::vector<MonitorData> s_monitorCache;
    std::atomic<bool> s_monitorCacheDirty{true};

    std::wstring GetDriverKeyFromDeviceKey(const std::wstring& deviceKey)
    {
        size_t lastSlash = deviceKey.find_last_of(L'\\');
        if (lastSlash == std::wstring::npos || lastSlash == 0)
            return L"";
        size_t prevSlash = deviceKey.find_last_of(L'\\', lastSlash - 1);
        if (prevSlash == std::wstring::npos)
            return L"";
        return deviceKey.substr(prevSlash + 1);
    }

    std::wstring GetMonitorFriendlyNameFromRegistry(const std::wstring& driverKey)
    {
        if (driverKey.empty())
            return L"";

        std::wstring friendlyName = L"";
        app::platform::UniqueRegKey hDisplayKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY", 0, KEY_READ, hDisplayKey.Put()) == ERROR_SUCCESS) {
            DWORD subkeyCount = 0;
            if (RegQueryInfoKeyW(hDisplayKey.Get(), nullptr, nullptr, nullptr, &subkeyCount, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                for (DWORD i = 0; i < subkeyCount; ++i) {
                    wchar_t subkeyName[256];
                    DWORD subkeyNameSize = 256;
                    if (RegEnumKeyExW(hDisplayKey.Get(), i, subkeyName, &subkeyNameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                        std::wstring hardwarePath = L"SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\" + std::wstring(subkeyName);
                        app::platform::UniqueRegKey hHardwareKey;
                        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, hardwarePath.c_str(), 0, KEY_READ, hHardwareKey.Put()) == ERROR_SUCCESS) {
                            DWORD instanceCount = 0;
                            if (RegQueryInfoKeyW(hHardwareKey.Get(), nullptr, nullptr, nullptr, &instanceCount, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                                for (DWORD j = 0; j < instanceCount; ++j) {
                                    wchar_t instanceName[256];
                                    DWORD instanceNameSize = 256;
                                    if (RegEnumKeyExW(hHardwareKey.Get(), j, instanceName, &instanceNameSize, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                                        std::wstring instancePath = hardwarePath + L"\\" + std::wstring(instanceName);
                                        app::platform::UniqueRegKey hInstanceKey;
                                        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, instancePath.c_str(), 0, KEY_READ, hInstanceKey.Put()) == ERROR_SUCCESS) {
                                            wchar_t driverValue[256] = {};
                                            DWORD driverValueSize = sizeof(driverValue);
                                            DWORD valueType = 0;
                                            if (RegQueryValueExW(
                                                    hInstanceKey.Get(),
                                                    L"Driver",
                                                    nullptr,
                                                    &valueType,
                                                    reinterpret_cast<LPBYTE>(driverValue),
                                                    &driverValueSize) == ERROR_SUCCESS &&
                                                (valueType == REG_SZ || valueType == REG_EXPAND_SZ)) {
                                                driverValue[_countof(driverValue) - 1] = L'\0';
                                                if (_wcsicmp(driverValue, driverKey.c_str()) == 0) {
                                                    std::wstring paramsPath = instancePath + L"\\Device Parameters";
                                                    app::platform::UniqueRegKey hParamsKey;
                                                    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, paramsPath.c_str(), 0, KEY_READ, hParamsKey.Put()) == ERROR_SUCCESS) {
                                                        unsigned char edidData[1024];
                                                        DWORD edidDataSize = sizeof(edidData);
                                                        if (RegQueryValueExW(hParamsKey.Get(), L"EDID", nullptr, nullptr, edidData, &edidDataSize) == ERROR_SUCCESS) {
                                                            if (edidDataSize >= 128) {
                                                                for (int offset = 54; offset <= 108; offset += 18) {
                                                                    if (edidData[offset] == 0x00 && edidData[offset + 1] == 0x00 && edidData[offset + 2] == 0x00 &&
                                                                        edidData[offset + 3] == 0xFC && edidData[offset + 4] == 0x00) {
                                                                        
                                                                        std::string name;
                                                                        for (int k = 0; k < 13; ++k) {
                                                                            char c = (char)edidData[offset + 5 + k];
                                                                            if (c == 0x0A || c == 0x00)
                                                                                break;
                                                                            name += c;
                                                                        }
                                                                        while (!name.empty() && isspace((unsigned char)name.back()))
                                                                            name.pop_back();
                                                                        size_t start = 0;
                                                                        while (start < name.size() && isspace((unsigned char)name[start]))
                                                                            ++start;
                                                                        if (start > 0)
                                                                            name = name.substr(start);
                                                                        
                                                                        if (!name.empty()) {
                                                                            friendlyName = std::wstring(name.begin(), name.end());
                                                                        }
                                                                        break;
                                                                    }
                                                                }
                                                            }
                                                        }
                                                    }
                                                }
                                            }
                                        }
                                        if (!friendlyName.empty())
                                            break;
                                    }
                                }
                            }
                        }
                        if (!friendlyName.empty())
                            break;
                    }
                }
            }
        }
        return friendlyName;
    }

    BOOL CALLBACK EnumMonitorsProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM dwData)
    {
        auto* monitors = reinterpret_cast<std::vector<MonitorData>*>(dwData);
        MONITORINFOEXW mi = {};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMonitor, &mi)) {
            MonitorData data;
            data.handle = hMonitor;
            data.rect = mi.rcMonitor;
            data.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            data.name = mi.szDevice;
            
            std::wstring friendlyName = L"Generic Monitor";
            DISPLAY_DEVICEW ddMonitor = {};
            ddMonitor.cb = sizeof(ddMonitor);
            if (EnumDisplayDevicesW(mi.szDevice, 0, &ddMonitor, 0)) {
                std::wstring deviceKey = ddMonitor.DeviceKey;
                std::wstring driverKey = GetDriverKeyFromDeviceKey(deviceKey);
                std::wstring regFriendlyName = GetMonitorFriendlyNameFromRegistry(driverKey);
                if (!regFriendlyName.empty()) {
                    friendlyName = regFriendlyName;
                } else if (ddMonitor.DeviceString[0] != L'\0') {
                    friendlyName = ddMonitor.DeviceString;
                }
            }
            data.friendlyName = friendlyName;
            monitors->push_back(data);
        }
        return TRUE;
    }

    std::vector<MonitorData> EnumerateMonitors()
    {
        std::vector<MonitorData> monitors;
        EnumDisplayMonitors(nullptr, nullptr, EnumMonitorsProc, reinterpret_cast<LPARAM>(&monitors));
        std::sort(monitors.begin(), monitors.end(), [](const MonitorData& a, const MonitorData& b) {
            if (a.rect.left != b.rect.left)
                return a.rect.left < b.rect.left;
            return a.rect.top < b.rect.top;
        });
        return monitors;
    }

    void InvalidateMonitorCache()
    {
        s_monitorCacheDirty.store(true, std::memory_order_release);
    }

    std::vector<MonitorData> GetMonitors()
    {
        std::lock_guard<std::mutex> lock(s_monitorCacheMutex);
        if (s_monitorCache.empty() ||
            s_monitorCacheDirty.exchange(false, std::memory_order_acq_rel)) {
            s_monitorCache = EnumerateMonitors();
        }
        return s_monitorCache;
    }

    struct OverlayTargetBounds {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    OverlayTargetBounds ResolveOverlayTargetBounds()
    {
        const std::vector<MonitorData> monitors = GetMonitors();
        int selectedIndex = g::overlayMonitorIndex;
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(monitors.size())) {
            selectedIndex = 0;
            for (int i = 0; i < static_cast<int>(monitors.size()); ++i) {
                if (monitors[i].isPrimary) {
                    selectedIndex = i;
                    break;
                }
            }
        }

        OverlayTargetBounds bounds;
        if (!monitors.empty()) {
            const MonitorData& monitor = monitors[selectedIndex];
            bounds.x = monitor.rect.left;
            bounds.y = monitor.rect.top;
            bounds.width = monitor.rect.right - monitor.rect.left;
            bounds.height = monitor.rect.bottom - monitor.rect.top;
        } else {
            bounds.width = GetSystemMetrics(SM_CXSCREEN);
            bounds.height = GetSystemMetrics(SM_CYSCREEN);
        }
        return bounds;
    }

    std::wstring Utf8ToWide(const std::string& text)
    {
        if (text.empty())
            return {};

        const int required = MultiByteToWideChar(
            CP_UTF8,
            0,
            text.data(),
            static_cast<int>(text.size()),
            nullptr,
            0);
        if (required <= 0)
            return std::wstring(text.begin(), text.end());

        std::wstring wide(static_cast<size_t>(required), L'\0');
        MultiByteToWideChar(
            CP_UTF8,
            0,
            text.data(),
            static_cast<int>(text.size()),
            wide.data(),
            required);
        return wide;
    }

    void PruneLegacyImGuiIniEntries(const std::filesystem::path& iniPath)
    {
        if (iniPath.empty())
            return;

        std::ifstream input(iniPath, std::ios::binary);
        if (!input.is_open())
            return;

        std::vector<std::string> keptLines;
        keptLines.reserve(256);
        bool skipSection = false;
        bool changed = false;
        std::string line;
        while (std::getline(input, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            if (!line.empty() && line.front() == '[') {
                const bool isLegacyMainWindow =
                    line.rfind("[Window][KevqDMA ", 0) == 0 &&
                    line.find("###main_menu]") == std::string::npos;
                skipSection = isLegacyMainWindow;
                changed = changed || isLegacyMainWindow;
                if (skipSection)
                    continue;
            }

            if (skipSection)
                continue;

            keptLines.push_back(line);
        }

        if (!changed)
            return;

        std::ofstream output(iniPath, std::ios::binary | std::ios::trunc);
        if (!output.is_open())
            return;

        for (const std::string& keptLine : keptLines)
            output << keptLine << "\r\n";
    }
}

static void SyncOverlayBounds()
{
    if (!s_hwnd || !s_swapChain)
        return;

    const OverlayTargetBounds bounds = ResolveOverlayTargetBounds();
    const int targetX = bounds.x;
    const int targetY = bounds.y;
    const int targetWidth = bounds.width;
    const int targetHeight = bounds.height;

    if (targetWidth <= 0 || targetHeight <= 0)
        return;

    RECT windowRect = {};
    GetWindowRect(s_hwnd, &windowRect);
    const int currentWidth = windowRect.right - windowRect.left;
    const int currentHeight = windowRect.bottom - windowRect.top;

    if (currentWidth != targetWidth || currentHeight != targetHeight ||
        windowRect.left != targetX || windowRect.top != targetY) {
        SetWindowPos(s_hwnd, HWND_TOPMOST,
            targetX, targetY, targetWidth, targetHeight,
            SWP_NOACTIVATE | SWP_SHOWWINDOW);
        RecreateRenderTarget(
            static_cast<UINT>(targetWidth),
            static_cast<UINT>(targetHeight));
    } else if (!s_rtv) {
        RecreateRenderTarget(
            static_cast<UINT>(targetWidth),
            static_cast<UINT>(targetHeight));
    }

    g::screenWidth = targetWidth;
    g::screenHeight = targetHeight;
}

static std::filesystem::path ResolveImGuiIniPath()
{
    const auto settingsDir = app::paths::GetSettingsDirectory();
    if (settingsDir.empty())
        return {};

    const auto targetPath = settingsDir / "imgui.ini";
    std::error_code ec;
    std::filesystem::create_directories(targetPath.parent_path(), ec);
    std::filesystem::remove(settingsDir / "imgui.build", ec);
    PruneLegacyImGuiIniEntries(targetPath);

    return targetPath;
}

static bool CheckTearingSupport()
{
    ComPtr<IDXGIFactory4> factory4;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory4));
    if (FAILED(hr) || !factory4)
        return false;

    ComPtr<IDXGIFactory5> factory5;
    hr = factory4.As(&factory5);
    if (FAILED(hr) || !factory5)
        return false;

    BOOL allowTearing = FALSE;
    hr = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
    return SUCCEEDED(hr) && allowTearing;
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_DISPLAYCHANGE:
    case WM_DEVICECHANGE:
        InvalidateMonitorCache();
        return 0;
    case WM_DESTROY:
        g::running = false;
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool overlay::Create(int width, int height, std::string* outErrorMessage)
{
    #include "overlay_parts/overlay_create_body.inl"
}

void overlay::Run()
{
    #include "overlay_parts/overlay_run_body.inl"
}

overlay::PerfStats overlay::GetPerfStats()
{
    PerfStats stats = {};
    stats.frameUs = s_overlayFrameUs.load(std::memory_order_relaxed);
    stats.maxFrameUs = s_overlayMaxFrameUs.load(std::memory_order_relaxed);
    stats.syncUs = s_overlaySyncUs.load(std::memory_order_relaxed);
    stats.drawUs = s_overlayDrawUs.load(std::memory_order_relaxed);
    stats.presentUs = s_overlayPresentUs.load(std::memory_order_relaxed);
    stats.pacingWaitUs = s_overlayPacingWaitUs.load(std::memory_order_relaxed);
    return stats;
}

void overlay::Destroy()
{
    esp::render::weapon_icons::Shutdown();

    if (s_imguiDx11Initialized) {
        ImGui_ImplDX11_Shutdown();
        s_imguiDx11Initialized = false;
    }
    if (s_imguiWin32Initialized) {
        ImGui_ImplWin32_Shutdown();
        s_imguiWin32Initialized = false;
    }
    if (s_imguiContextCreated) {
        ImGui::DestroyContext();
        s_imguiContextCreated = false;
    }

    s_frameLatencyWaitableObject.Reset();
    s_waitableSwapChainEnabled = false;

    if (s_swapChain) {
        ComPtr<IDXGISwapChain> baseSwapChain;
        if (SUCCEEDED(s_swapChain.As(&baseSwapChain)))
            baseSwapChain->SetFullscreenState(FALSE, nullptr);
    }

    s_rtv.Reset();
    s_swapChain.Reset();
    s_swapChain2.Reset();
    s_context.Reset();
    s_device.Reset();

    if (s_hwnd) {
        DestroyWindow(s_hwnd);
        s_hwnd = nullptr;
    }
    if (s_windowClassRegistered) {
        UnregisterClassW(L"KevqDMA_Overlay", GetModuleHandleW(nullptr));
        s_windowClassRegistered = false;
    }
    s_tearingSupported = false;
    s_overlayVisible = false;
}

std::vector<std::string> overlay::GetMonitorNames()
{
    auto monitors = GetMonitors();
    std::vector<std::string> names;
    names.reserve(monitors.size());
    for (size_t i = 0; i < monitors.size(); ++i) {
        const auto& m = monitors[i];
        
        std::string nameUtf8;
        if (!m.name.empty()) {
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, m.name.c_str(), (int)m.name.size(), NULL, 0, NULL, NULL);
            nameUtf8.resize(size_needed);
            WideCharToMultiByte(CP_UTF8, 0, m.name.c_str(), (int)m.name.size(), &nameUtf8[0], size_needed, NULL, NULL);
        }
        
        std::string friendlyUtf8;
        if (!m.friendlyName.empty()) {
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, m.friendlyName.c_str(), (int)m.friendlyName.size(), NULL, 0, NULL, NULL);
            friendlyUtf8.resize(size_needed);
            WideCharToMultiByte(CP_UTF8, 0, m.friendlyName.c_str(), (int)m.friendlyName.size(), &friendlyUtf8[0], size_needed, NULL, NULL);
        }
        
        std::string displayName = nameUtf8;
        size_t lastBackslash = displayName.find_last_of('\\');
        if (lastBackslash != std::string::npos) {
            displayName = displayName.substr(lastBackslash + 1);
        }
        
        char buf[256];
        if (m.isPrimary) {
            std::snprintf(buf, sizeof(buf), KEVQ_TR("Monitor %d [%s] (%s) (Primary) - %dx%d"),
                static_cast<int>(i + 1), friendlyUtf8.c_str(), displayName.c_str(),
                static_cast<int>(m.rect.right - m.rect.left), 
                static_cast<int>(m.rect.bottom - m.rect.top));
        } else {
            std::snprintf(buf, sizeof(buf), KEVQ_TR("Monitor %d [%s] (%s) - %dx%d"),
                static_cast<int>(i + 1), friendlyUtf8.c_str(), displayName.c_str(),
                static_cast<int>(m.rect.right - m.rect.left), 
                static_cast<int>(m.rect.bottom - m.rect.top));
        }
        names.push_back(buf);
    }
    return names;
}
