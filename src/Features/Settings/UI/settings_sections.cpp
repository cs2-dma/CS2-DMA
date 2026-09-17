#include "Features/Settings/UI/settings_sections.h"

#include "app/Core/globals.h"
#include "app/UI/MenuShell/menu_utils.h"
#include "app/UI/MenuShell/ui_widgets.h"
#include "app/Config/config.h"
#include "app/Input/input_device.h"
#include "app/Localization/localization.h"
#include "Features/ESP/DataReader/bone_read_policy.h"
#include "Features/ESP/DataReader/bone_plausibility.h"
#include "Features/ESP/DataReader/bomb_policy.h"
#include "Features/ESP/DataReader/intervals.h"
#include "Features/ESP/DataReader/player_core_policy.h"
#include "Features/ESP/DataReader/visibility_policy.h"
#include "Features/ESP/esp.h"
#include "Features/ESP/Recovery/dma_cache_profile.h"
#include "app/Platform/overlay.h"
#include <DMALibrary/Memory/Memory.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <imgui.h>

namespace
{
    void SaveProfile(const char* profileName, ui::IStatusSink& statusSink)
    {
        if (::config::SaveNamed(profileName ? profileName : "default"))
            statusSink.SetStatus("Profile saved.");
        else
            statusSink.SetStatus("Profile could not be saved.");
    }

    void LoadProfile(const char* profileName, ui::IStatusSink& statusSink)
    {
        if (::config::LoadNamed(profileName ? profileName : "default")) {
            statusSink.SetStatus("Profile loaded.");
            esp::RequestCacheRefresh();
        }
        else
            statusSink.SetStatus("Profile not found.");
    }

    void StartMenuKeyCapture(ui::MenuState& state, ui::IStatusSink& statusSink)
    {
        state.waitingForMenuKey = true;
        statusSink.SetStatus("Press any key to bind menu toggle.");
    }

    void CancelMenuKeyCapture(ui::MenuState& state, ui::IStatusSink& statusSink)
    {
        state.waitingForMenuKey = false;
        statusSink.SetStatus("Key capture canceled.");
    }

    void ResetMenuKey(ui::MenuState& state, ui::IStatusSink& statusSink)
    {
        g::menuToggleKey = 'P';
        state.waitingForMenuKey = false;
        statusSink.SetStatus("Menu key reset to P.");
    }

    void StartOverlayKeyCapture(ui::MenuState& state, ui::IStatusSink& statusSink)
    {
        state.waitingForOverlayKey = true;
        statusSink.SetStatus("Press any key to bind overlay toggle.");
    }

    void CancelOverlayKeyCapture(ui::MenuState& state, ui::IStatusSink& statusSink)
    {
        state.waitingForOverlayKey = false;
        statusSink.SetStatus("Key capture canceled.");
    }

    void ResetOverlayKey(ui::MenuState& state, ui::IStatusSink& statusSink)
    {
        g::overlayToggleKey = 0x71; 
        state.waitingForOverlayKey = false;
        statusSink.SetStatus("Overlay key reset to F2.");
    }
}

void ui::tabs::settings_sections::RenderProfilesSection(MenuState& state, IStatusSink& statusSink)
{
    const float innerGap = 10.0f;
    const float contentWidth = ImGui::GetContentRegionAvail().x;
    const float fieldWidth = contentWidth;
    const float dualButtonWidth = (contentWidth - innerGap) * 0.5f;

    ui::widgets::SectionTitle("Profiles");
    ImGui::TextDisabled("%s", KEVQ_TR("Active Profile"));
    ImGui::TextUnformatted(config::GetActiveProfile().c_str());
    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    ImGui::TextDisabled("%s", KEVQ_TR("Profile Name"));
    ImGui::SetNextItemWidth(fieldWidth);
    ImGui::InputText("##profile_name", state.profileName, sizeof(state.profileName));

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ImGui::TextDisabled("%s", KEVQ_TR("Saved Profiles"));
    ImGui::SetNextItemWidth(fieldWidth);
    if (ImGui::BeginCombo("##saved_profiles", config::GetActiveProfile().c_str())) {
        const std::vector<std::string> profiles = config::ListProfiles();
        for (const std::string& profile : profiles) {
            const bool selected = (profile == config::GetActiveProfile());
            if (ImGui::Selectable(profile.c_str(), selected)) {
                ui::menu_utils::CopyToBuffer(state.profileName, sizeof(state.profileName), profile);
                if (::config::LoadNamed(profile)) {
                    statusSink.SetStatus("Profile loaded.");
                    esp::RequestCacheRefresh();
                } else {
                    statusSink.SetStatus("Failed to load profile.");
                }
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    if (ImGui::Button(KEVQ_TR("Save Profile"), ImVec2(dualButtonWidth, 32.0f))) {
        SaveProfile(state.profileName, statusSink);
    }
    ImGui::SameLine(0.0f, innerGap);
    if (ImGui::Button(KEVQ_TR("Load Profile"), ImVec2(dualButtonWidth, 32.0f))) {
        LoadProfile(state.profileName, statusSink);
    }
}

void ui::tabs::settings_sections::RenderControlsSection(MenuState& state, IStatusSink& statusSink)
{
    const float contentWidth = ImGui::GetContentRegionAvail().x;

    ui::widgets::SectionTitle("Controls");
    ImGui::TextDisabled("%s", KEVQ_TR("Menu Toggle Key"));
    ImGui::TextUnformatted(key_names::ToDisplayName(g::menuToggleKey).c_str());
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    if (!state.waitingForMenuKey) {
        if (ImGui::Button(KEVQ_TR("Change Menu Key"), ImVec2(contentWidth, 32.0f))) {
            StartMenuKeyCapture(state, statusSink);
        }
    }
    else {
        if (ImGui::Button(KEVQ_TR("Cancel Key Capture"), ImVec2(contentWidth, 32.0f))) {
            CancelMenuKeyCapture(state, statusSink);
        }
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::TextDisabled("%s", KEVQ_TR("Waiting for key press..."));
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (ImGui::Button(KEVQ_TR("Reset to P"), ImVec2(contentWidth, 32.0f))) {
        ResetMenuKey(state, statusSink);
    }

    ImGui::Dummy(ImVec2(0.0f, 14.0f));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    ImGui::TextDisabled("%s", KEVQ_TR("Overlay Toggle Key"));
    ImGui::TextUnformatted(key_names::ToDisplayName(g::overlayToggleKey).c_str());
    ImGui::Dummy(ImVec2(0.0f, 10.0f));

    if (!state.waitingForOverlayKey) {
        if (ImGui::Button(KEVQ_TR("Change Overlay Key"), ImVec2(contentWidth, 32.0f))) {
            StartOverlayKeyCapture(state, statusSink);
        }
    } else {
        if (ImGui::Button(KEVQ_TR("Cancel Key Capture##Overlay"), ImVec2(contentWidth, 32.0f))) {
            CancelOverlayKeyCapture(state, statusSink);
        }
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::TextDisabled("%s", KEVQ_TR("Waiting for key press..."));
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (ImGui::Button(KEVQ_TR("Reset to F2"), ImVec2(contentWidth, 32.0f))) {
        ResetOverlayKey(state, statusSink);
    }
}

void ui::tabs::settings_sections::RenderLanguageSection(IStatusSink& statusSink)
{
    using app::localization::Language;

    ui::widgets::SectionTitle("Language");
    ImGui::TextDisabled("%s", app::localization::Get("Interface Language"));

    const Language current = app::localization::GetLanguage();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo(
            "##interface_language",
            app::localization::NativeLanguageName(current))) {
        constexpr Language languages[] = {
            Language::English,
            Language::SimplifiedChinese
        };
        for (const Language language : languages) {
            const bool selected = language == current;
            if (ImGui::Selectable(
                    app::localization::NativeLanguageName(language),
                    selected)) {
                app::localization::SetLanguage(language);
                statusSink.SetStatus("Language updated.");
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void ui::tabs::settings_sections::RenderInputDeviceSection(IStatusSink& statusSink)
{
    using app::input::ConnectionState;
    using app::input::DeviceError;
    using app::input::DeviceKind;

    DeviceKind selected = app::input::SanitizeSelectableDeviceKind(
        g::inputDeviceKind);
    g::inputDeviceKind = static_cast<int>(selected);
    app::input::SetSelectedDevice(selected);

    ui::widgets::SectionTitle("Input Device");
    ImGui::TextDisabled("%s", KEVQ_TR("Mouse Controller"));
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo(
            "##input_device_kind",
            KEVQ_TR(app::input::DeviceKindLabel(selected)))) {
        constexpr DeviceKind devices[] = {
            DeviceKind::Makcu,
            DeviceKind::KmBox,
            DeviceKind::KmBoxNet,
            DeviceKind::FerrumOne,
        };
        for (const DeviceKind device : devices) {
            const bool isSelected = device == selected;
            if (ImGui::Selectable(
                    KEVQ_TR(app::input::DeviceKindLabel(device)),
                    isSelected)) {
                g::inputDeviceKind = static_cast<int>(device);
                app::input::SetSelectedDevice(device);
                selected = device;
                statusSink.SetStatus("Input device updated.");
            }
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::Dummy(ImVec2(0.0f, 3.0f));
    ImGui::TextDisabled(
        "%s",
        KEVQ_TR(app::input::DeviceKindTransportLabel(selected)));

    static std::array<char, 65> netHost = {};
    static std::array<char, 33> netKey = {};
    static std::string bufferedHost;
    static std::string bufferedKey;
    const auto syncBuffer = [](auto& buffer, std::string& buffered, const std::string& value) {
        if (buffered == value)
            return;
        std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
        buffered = value;
    };
    syncBuffer(netHost, bufferedHost, g::inputDeviceNetHost);
    syncBuffer(netKey, bufferedKey, g::inputDeviceNetKey);

    const bool networkDevice = app::input::IsNetworkDeviceKind(selected);
    if (selected == DeviceKind::FerrumOne) {
        ImGui::TextWrapped("%s", KEVQ_TR("Keep Ferrum App running. Copy IP, port, and UUID from its Net API tab. Network settings are shared with KMBox NET."));
    }
    if (networkDevice) {
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        if (ImGui::BeginTable(
                "##kmbox_network_endpoint",
                2,
                ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("##host", ImGuiTableColumnFlags_WidthStretch, 3.0f);
            ImGui::TableSetupColumn("##port", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("%s", KEVQ_TR("Device IP Address"));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("%s", KEVQ_TR("Device Port"));
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::InputTextWithHint(
                    "##kmbox_net_host",
                    "192.168.2.188",
                    netHost.data(),
                    netHost.size())) {
                g::inputDeviceNetHost = netHost.data();
                bufferedHost = g::inputDeviceNetHost;
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            if (ImGui::InputInt(
                    "##kmbox_net_port",
                    &g::inputDeviceNetPort,
                    0,
                    0)) {
                g::inputDeviceNetPort = std::clamp(
                    g::inputDeviceNetPort,
                    0,
                    65535);
            }
            ImGui::EndTable();
        }

        ImGui::TextDisabled("%s", KEVQ_TR("Device UUID"));
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::InputTextWithHint(
                "##kmbox_net_key",
                "A1B2C3D4",
                netKey.data(),
                netKey.size(),
                ImGuiInputTextFlags_CharsHexadecimal)) {
            g::inputDeviceNetKey = netKey.data();
            bufferedKey = g::inputDeviceNetKey;
        }
    }

    const uint16_t networkPort = static_cast<uint16_t>(
        std::clamp(g::inputDeviceNetPort, 0, 65535));
    const bool networkConfigValid =
        !networkDevice ||
        app::input::IsValidKmBoxNetworkConfig(
            g::inputDeviceNetHost,
            networkPort,
            g::inputDeviceNetKey);
    app::input::SetKmBoxNetConfig({
        g::inputDeviceNetHost,
        networkPort,
        g::inputDeviceNetKey
    });

    if (networkDevice) {
        const ImVec4 validationColor = networkConfigValid
            ? ImVec4(0.28f, 0.85f, 0.42f, 1.0f)
            : ImVec4(0.96f, 0.36f, 0.31f, 1.0f);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::TextColored(
            validationColor,
            "%s",
            KEVQ_TR(networkConfigValid
                ? "Network settings ready"
                : "Enter a valid IP, port, and UUID"));
    }

    const app::input::DeviceStatus status = app::input::GetDeviceStatus();
    const char* stateText = "Not connected";
    ImVec4 stateColor(0.96f, 0.36f, 0.31f, 1.0f);
    switch (status.state) {
    case ConnectionState::Connecting:
        stateText = "Connecting...";
        stateColor = ImVec4(0.95f, 0.72f, 0.25f, 1.0f);
        break;
    case ConnectionState::Testing:
        stateText = "Testing movement...";
        stateColor = ImVec4(0.25f, 0.66f, 0.98f, 1.0f);
        break;
    case ConnectionState::Connected:
        stateText = "Connected";
        stateColor = ImVec4(0.28f, 0.85f, 0.42f, 1.0f);
        break;
    case ConnectionState::Error:
        stateColor = ImVec4(0.96f, 0.36f, 0.31f, 1.0f);
        switch (status.error) {
        case DeviceError::NotFound: stateText = "Input device was not found"; break;
        case DeviceError::PortUnavailable: stateText = "Serial port is unavailable"; break;
        case DeviceError::ConfigureFailed: stateText = "Serial port setup failed"; break;
        case DeviceError::HandshakeFailed: stateText = "Device handshake failed"; break;
        case DeviceError::IoFailure: stateText = "Device communication failed"; break;
        case DeviceError::Disconnected: stateText = "Device disconnected"; break;
        case DeviceError::InvalidConfiguration: stateText = "Invalid network settings"; break;
        case DeviceError::NetworkTimeout: stateText = "Network device did not respond"; break;
        case DeviceError::Unsupported: stateText = "Backend is not available yet"; break;
        default: stateText = "Connection failed"; break;
        }
        break;
    default:
        break;
    }

    ImGui::Dummy(ImVec2(0.0f, 3.0f));
    ImGui::TextDisabled("%s", KEVQ_TR("Status"));
    ImGui::SameLine();
    const ImVec2 dotCursor = ImGui::GetCursorScreenPos();
    const float dotRadius = 4.0f;
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(dotCursor.x + dotRadius, dotCursor.y + ImGui::GetTextLineHeight() * 0.5f),
        dotRadius,
        ImGui::GetColorU32(stateColor));
    ImGui::Dummy(ImVec2(dotRadius * 2.0f + 3.0f, ImGui::GetTextLineHeight()));
    ImGui::SameLine();
    ImGui::TextColored(stateColor, "%s", KEVQ_TR(stateText));

    if (!status.port.empty()) {
        ImGui::TextDisabled(
            "%s: %s",
            KEVQ_TR(networkDevice
                ? "Network Endpoint"
                : "Detected Port"),
            status.port.c_str());
    }
    if (status.systemError != 0) {
        ImGui::TextDisabled(
            "%s: %u",
            KEVQ_TR("System Error"),
            status.systemError);
    }

    ImGui::Dummy(ImVec2(0.0f, 5.0f));
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const bool busy =
        status.state == ConnectionState::Connecting ||
        status.state == ConnectionState::Testing;
    if (status.state == ConnectionState::Connected) {
        constexpr float kButtonGap = 8.0f;
        const float buttonWidth = (availableWidth - kButtonGap) * 0.5f;
        if (ImGui::Button(KEVQ_TR("Disconnect Device"), ImVec2(buttonWidth, 32.0f))) {
            if (app::input::RequestDisconnect())
                statusSink.SetStatus("Input device disconnected.");
        }
        ImGui::SameLine(0.0f, kButtonGap);
        if (ImGui::Button(KEVQ_TR("Test Movement"), ImVec2(buttonWidth, 32.0f))) {
            if (app::input::RequestMovementTest())
                statusSink.SetStatus("Movement test started.");
        }
    } else {
        ImGui::BeginDisabled(busy || !networkConfigValid);
        const char* buttonText = busy ? "Please wait..." : "Connect";
        if (ImGui::Button(KEVQ_TR(buttonText), ImVec2(availableWidth, 32.0f))) {
            if (app::input::RequestConnectAndTest())
                statusSink.SetStatus("Input device connection requested.");
        }
        ImGui::EndDisabled();
    }
}

void ui::tabs::settings_sections::RenderScreenSection()
{
    ui::widgets::SectionTitle("Screen");
    ImGui::TextDisabled("%s", KEVQ_TR("Current Overlay Size"));
    ImGui::Text("%d x %d", g::screenWidth, g::screenHeight);
    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    ImGui::TextDisabled(
        "%s",
        KEVQ_TR("Overlay size is detected automatically from the game window."));

    ImGui::Dummy(ImVec2(0.0f, 8.0f));

    ImGui::TextDisabled("%s", KEVQ_TR("Overlay Monitor"));
    std::vector<std::string> monitors = overlay::GetMonitorNames();
    if (monitors.empty()) {
        monitors.push_back(KEVQ_TR("Primary Monitor (Default)"));
    }
    std::string currentMonitorName = KEVQ_TR("Primary Monitor (Default)");
    if (g::overlayMonitorIndex >= 0 && g::overlayMonitorIndex < static_cast<int>(monitors.size())) {
        currentMonitorName = monitors[g::overlayMonitorIndex];
    }
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    if (ImGui::BeginCombo("##overlay_monitor", currentMonitorName.c_str())) {
        for (int i = 0; i < static_cast<int>(monitors.size()); ++i) {
            const bool selected = (i == g::overlayMonitorIndex);
            if (ImGui::Selectable(monitors[i].c_str(), selected)) {
                g::overlayMonitorIndex = i;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    ui::widgets::ToggleRow("vsync", "VSync", &g::vsyncEnabled);
    if (!g::vsyncEnabled) {
        ui::widgets::SliderIntRow("fps_limit", "FPS Limit", &g::fpsLimit, 0, 500, g::fpsLimit == 0 ? "Unlimited" : "%d");
    }
}

void ui::tabs::settings_sections::RenderDebugWindow(bool* open)
{
    ImGui::SetNextWindowSize(ImVec2(430.0f, 500.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(KEVQ_TR("Debug Statistics"), open)) {
        ImGui::End();
        return;
    }

    const auto health = esp::GetDmaHealthStats();
    const auto debug = esp::GetDebugStats();
    const bool idleScene = debug.engineResolved && debug.engineMenu && !debug.engineInGame;
    const auto& st = debug.stages;
    static double s_lastCopyTime = 0.0;

    auto msFromUs = [](uint64_t valueUs) -> float {
        return static_cast<float>(valueUs) / 1000.0f;
    };
    auto percentOf = [](uint64_t value, uint64_t total) -> double {
        return total == 0
            ? 0.0
            : (100.0 * static_cast<double>(value)) / static_cast<double>(total);
    };
    auto statusBadge = [](bool value, ImVec4 okColor, ImVec4 badColor) -> ImVec4 {
        return value ? okColor : badColor;
    };
    auto subsystemStateLabel = [](esp::SubsystemHealthState state) -> const char* {
        switch (state) {
        case esp::SubsystemHealthState::Healthy: return "healthy";
        case esp::SubsystemHealthState::Degraded: return "degraded";
        case esp::SubsystemHealthState::Failed: return "failed";
        default: return "unknown";
        }
    };
    auto subsystemStateColor = [](esp::SubsystemHealthState state) -> ImVec4 {
        switch (state) {
        case esp::SubsystemHealthState::Healthy: return ImVec4(0.30f, 0.86f, 0.30f, 1.0f);
        case esp::SubsystemHealthState::Degraded: return ImVec4(0.95f, 0.76f, 0.24f, 1.0f);
        case esp::SubsystemHealthState::Failed: return ImVec4(1.0f, 0.40f, 0.30f, 1.0f);
        default: return ImVec4(0.62f, 0.62f, 0.66f, 1.0f);
        }
    };
    auto stageColorForUs = [](uint64_t valueUs) -> ImVec4 {
        if (valueUs == 0)
            return ImVec4(0.32f, 0.32f, 0.34f, 0.0f);
        if (valueUs <= 500)
            return ImVec4(0.30f, 0.80f, 0.34f, 0.92f);
        if (valueUs <= 1500)
            return ImVec4(0.86f, 0.76f, 0.24f, 0.92f);
        if (valueUs <=
            (1000000u / static_cast<uint64_t>(esp::kDataWorkerLiveHz)))
            return ImVec4(0.95f, 0.62f, 0.24f, 0.92f);
        return ImVec4(0.98f, 0.32f, 0.24f, 0.92f);
    };
    auto renderMetricBar = [&](const char* label, float displayUs, const char* detail) {
        constexpr uint64_t kMetricScaleUs =
            1000000u / static_cast<uint64_t>(esp::kDataWorkerLiveHz);
        const float windowRight = ImGui::GetWindowContentRegionMax().x;
        const float barStartX = 168.0f;
        const float barMargin = 12.0f;
        const float barMaxWidth = std::max(windowRight - barStartX - barMargin, 20.0f);
        const float displayMs = displayUs / 1000.0f;
        const uint64_t clampedDisplayUs = displayUs > 0.0f ? static_cast<uint64_t>(displayUs) : 0u;

        ImGui::Text("%-11s", app::localization::Get(label));
        ImGui::SameLine(88.0f);
        ImGui::Text("%6.2f ms", displayMs);
        ImGui::SameLine(barStartX);

        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float barHeight = ImGui::GetTextLineHeight() - 2.0f;
        dl->AddRectFilled(
            cursor,
            ImVec2(cursor.x + barMaxWidth, cursor.y + barHeight),
            ImGui::GetColorU32(ImVec4(0.12f, 0.12f, 0.14f, 0.9f)),
            2.0f);
        if (clampedDisplayUs > 0) {
            const float fillFraction =
                static_cast<float>(std::min<uint64_t>(clampedDisplayUs, kMetricScaleUs)) /
                static_cast<float>(kMetricScaleUs);
            const float fillWidth = fillFraction * barMaxWidth;
            if (fillWidth > 0.0f) {
                dl->AddRectFilled(
                    cursor,
                    ImVec2(cursor.x + fillWidth, cursor.y + barHeight),
                    ImGui::ColorConvertFloat4ToU32(stageColorForUs(clampedDisplayUs)),
                    2.0f);
            }
            if (clampedDisplayUs > kMetricScaleUs) {
                dl->AddRectFilled(
                    ImVec2(cursor.x + barMaxWidth - 6.0f, cursor.y),
                    ImVec2(cursor.x + barMaxWidth, cursor.y + barHeight),
                    ImGui::GetColorU32(ImVec4(0.98f, 0.24f, 0.20f, 0.95f)),
                    2.0f);
            }
        }
        ImGui::Dummy(ImVec2(barMaxWidth, barHeight));
        if (detail && detail[0] != '\0') {
            if (std::strcmp(label, "Bomb") == 0) {
                ImGui::Text("%s", KEVQ_TR("Status:"));
                ImGui::SameLine();
                const bool bombHidden =
                    !debug.bombPlanted &&
                    !debug.bombDropped &&
                    !debug.bombCarried;
                if (bombHidden) {
                    ImGui::TextDisabled("%s", detail);
                } else {
                    ImGui::TextColored(ImVec4(0.3f, 0.86f, 0.3f, 1.0f), "%s", detail);
                }
            } else {
                ImGui::TextDisabled("%s", detail);
            }
        }
    };
    const uint64_t sessionUptimeUs = debug.sessionUptimeUs > 0 ? debug.sessionUptimeUs : debug.uptimeUs;
    const char* statusLabel = "Unknown";
    ImVec4 statusColor(0.7f, 0.7f, 0.7f, 1.0f);

    
    {
        if (health.gameStatus == esp::GameStatus::Ok) {
            statusLabel = "OK";
            statusColor = ImVec4(0.3f, 0.86f, 0.3f, 1.0f);
        } else if (health.gameStatus == esp::GameStatus::WaitCs2) {
            statusLabel = "Waiting for CS2";
            statusColor = ImVec4(0.86f, 0.7f, 0.24f, 1.0f);
        }
        ImGui::Text("%s", KEVQ_TR("Status:"));
        ImGui::SameLine();
        ImGui::TextColored(statusColor, "%s", app::localization::Get(statusLabel));

        ImGui::SameLine(110.0f);
        ImGui::Text("%s", KEVQ_TR("Data:"));
        ImGui::SameLine();
        ImGui::TextColored(
            statusBadge(health.workerRunning, ImVec4(0.3f, 0.86f, 0.3f, 1.0f), ImVec4(1.0f, 0.4f, 0.3f, 1.0f)),
            "%s",
            KEVQ_TR(health.workerRunning ? "Running" : "Stopped"));

        ImGui::SameLine(205.0f);
        ImGui::Text("%s", KEVQ_TR("Camera:"));
        ImGui::SameLine();
        ImGui::TextColored(
            statusBadge(health.cameraWorkerRunning, ImVec4(0.3f, 0.86f, 0.3f, 1.0f), ImVec4(1.0f, 0.4f, 0.3f, 1.0f)),
            "%s",
            KEVQ_TR(health.cameraWorkerRunning ? "Running" : "Stopped"));
    }

    ImGui::Separator();

    
    const float rawSnapshotAgeMs =
        (debug.lastPublishUs > 0 && debug.uptimeUs > debug.lastPublishUs)
        ? msFromUs(debug.uptimeUs - debug.lastPublishUs)
        : 0.0f;
    const float rawCameraViewAgeMs = msFromUs(debug.cameraViewAgeUs);
    const float rawCameraLocalAgeMs = msFromUs(debug.cameraLocalPosAgeUs);
    const float rawWorldAgeMs = msFromUs(debug.worldScanAgeUs);
    const float worldTargetMs = msFromUs(debug.worldScanTargetIntervalUs);
    const float worldTickMs = msFromUs(st.worldScanUs);
    const float worldLastMs = msFromUs(st.worldScanLastUs);
    const float workerLoopAgeMs = static_cast<float>(health.dataWorkerLoopAgeMs);
    const float workerInFlightAgeMs = static_cast<float>(health.dataWorkerInFlightAgeMs);
    const float uiDeltaSeconds = std::clamp(ImGui::GetIO().DeltaTime, 1.0f / 240.0f, 0.25f);
    auto smoothToward = [&](float current, float target, float riseTauSeconds, float fallTauSeconds) -> float {
        const float tauSeconds = target >= current ? riseTauSeconds : fallTauSeconds;
        if (tauSeconds <= 0.0f)
            return target;
        const float alpha = 1.0f - std::exp(-uiDeltaSeconds / tauSeconds);
        return current + (target - current) * alpha;
    };
    constexpr float kHealthRiseTauSeconds = 2.50f;
    constexpr float kHealthFallTauSeconds = 4.00f;
    static float s_smoothedSnapshotAgeMs = 0.0f;
    static float s_smoothedCameraViewAgeMs = 0.0f;
    static float s_smoothedCameraLocalAgeMs = 0.0f;
    static float s_smoothedWorldAgeMs = 0.0f;
    static bool s_smoothedHealthInit = false;
    static uint64_t s_smoothedHealthScene = 0;
    if (!s_smoothedHealthInit || s_smoothedHealthScene != debug.sceneEpoch) {
        s_smoothedHealthScene = debug.sceneEpoch;
        s_smoothedSnapshotAgeMs = rawSnapshotAgeMs;
        s_smoothedCameraViewAgeMs = rawCameraViewAgeMs;
        s_smoothedCameraLocalAgeMs = rawCameraLocalAgeMs;
        s_smoothedWorldAgeMs = rawWorldAgeMs;
        s_smoothedHealthInit = true;
    } else {
        s_smoothedSnapshotAgeMs = smoothToward(
            s_smoothedSnapshotAgeMs,
            rawSnapshotAgeMs,
            kHealthRiseTauSeconds,
            kHealthFallTauSeconds);
        s_smoothedCameraViewAgeMs = smoothToward(
            s_smoothedCameraViewAgeMs,
            rawCameraViewAgeMs,
            kHealthRiseTauSeconds,
            kHealthFallTauSeconds);
        s_smoothedCameraLocalAgeMs = smoothToward(
            s_smoothedCameraLocalAgeMs,
            rawCameraLocalAgeMs,
            kHealthRiseTauSeconds,
            kHealthFallTauSeconds);
    s_smoothedWorldAgeMs = smoothToward(
            s_smoothedWorldAgeMs,
            rawWorldAgeMs,
            0.55f,
            0.85f);
    }
    const float snapshotAgeMs = s_smoothedSnapshotAgeMs;
    const float cameraViewAgeMs = s_smoothedCameraViewAgeMs;
    const float worldAgeMs = s_smoothedWorldAgeMs;

    static float s_smoothedCycleTopUs = 0.0f;
    constexpr float kPrimaryRiseTauSeconds = 0.55f;
    constexpr float kPrimaryFallTauSeconds = 0.50f;
    s_smoothedCycleTopUs = smoothToward(
        s_smoothedCycleTopUs,
        static_cast<float>(debug.cycleUs),
        kPrimaryRiseTauSeconds,
        kPrimaryFallTauSeconds);
    const float cycleMsSmoothed = s_smoothedCycleTopUs / 1000.0f;
    constexpr float budgetMs =
        1000.0f / static_cast<float>(esp::kDataWorkerLiveHz);

    ImGui::Text(
        KEVQ_TR("Pipeline: %.2f / %.2f ms"),
        cycleMsSmoothed,
        budgetMs);
    ImGui::SameLine();
    if (cycleMsSmoothed > budgetMs)
        ImGui::TextColored(
            ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
            "%s",
            KEVQ_TR("(over)"));
    else
        ImGui::TextColored(
            ImVec4(0.4f, 0.8f, 0.4f, 1.0f),
            "%s",
            KEVQ_TR("(ok)"));

    ImGui::TextDisabled(
        KEVQ_TR("Freshness: Snap %.1f | Cam %.1f | World %.1f ms"),
        snapshotAgeMs,
        cameraViewAgeMs,
        worldAgeMs);
    ImGui::Text("%s", KEVQ_TR("Subsystems:"));
    ImGui::SameLine();
    ImGui::TextDisabled("P");
    ImGui::SameLine();
    ImGui::TextColored(subsystemStateColor(debug.playersCore.state), "%s", app::localization::Get(subsystemStateLabel(debug.playersCore.state)));
    ImGui::SameLine();
    ImGui::TextDisabled("C");
    ImGui::SameLine();
    ImGui::TextColored(subsystemStateColor(debug.cameraView.state), "%s", app::localization::Get(subsystemStateLabel(debug.cameraView.state)));
    ImGui::SameLine();
    ImGui::TextDisabled("G");
    ImGui::SameLine();
    ImGui::TextColored(subsystemStateColor(debug.gamerulesMap.state), "%s", app::localization::Get(subsystemStateLabel(debug.gamerulesMap.state)));
    ImGui::SameLine();
    ImGui::TextDisabled("B");
    ImGui::SameLine();
    ImGui::TextColored(subsystemStateColor(debug.bones.state), "%s", app::localization::Get(subsystemStateLabel(debug.bones.state)));
    ImGui::SameLine();
    ImGui::TextDisabled("W");
    ImGui::SameLine();
    ImGui::TextColored(subsystemStateColor(debug.world.state), "%s", app::localization::Get(subsystemStateLabel(debug.world.state)));

    ImGui::Separator();

    constexpr float kSecondaryRiseTauSeconds = 0.70f;
    constexpr float kSecondaryFallTauSeconds = 1.10f;
    static float s_smoothedHeldAuxUs = 0.0f;
    static float s_smoothedHeldInvUs = 0.0f;
    static float s_smoothedHeldBonesUs = 0.0f;
    static float s_smoothedAuxUs = 0.0f;
    static float s_smoothedInvUs = 0.0f;
    static float s_smoothedBonesUs = 0.0f;
    static float s_smoothedCameraCycleUs = 0.0f;
    static float s_smoothedOverlayFrameUs = 0.0f;
    static float s_smoothedOverlayPacingUs = 0.0f;
    static float s_smoothedCoreUs = 0.0f;
    static float s_smoothedDeferredCurrentUs = 0.0f;
    static float s_smoothedDeferredHeldUs = 0.0f;
    static float s_smoothedBombUs = 0.0f;
    static float s_smoothedWorldCurrentUs = 0.0f;
    static float s_smoothedWorldHeldUs = 0.0f;
    static float s_smoothedBaseUs = 0.0f;
    static float s_smoothedPlayerReadsUs = 0.0f;
    static float s_smoothedEngineUs = 0.0f;
    static float s_smoothedCommitUs = 0.0f;
    static float s_smoothedWorldTickUs = 0.0f;
    static float s_smoothedWorldLastUs = 0.0f;
    s_smoothedHeldAuxUs = smoothToward(
        s_smoothedHeldAuxUs,
        static_cast<float>(st.playerAuxLastUs),
        kSecondaryRiseTauSeconds,
        kSecondaryFallTauSeconds);
    s_smoothedHeldInvUs = smoothToward(
        s_smoothedHeldInvUs,
        static_cast<float>(st.inventoryLastUs),
        kSecondaryRiseTauSeconds,
        kSecondaryFallTauSeconds);
    s_smoothedHeldBonesUs = smoothToward(
        s_smoothedHeldBonesUs,
        static_cast<float>(st.boneReadsLastUs),
        kSecondaryRiseTauSeconds,
        kSecondaryFallTauSeconds);
    s_smoothedCameraCycleUs = smoothToward(
        s_smoothedCameraCycleUs,
        static_cast<float>(debug.camera.cycleUs),
        kSecondaryRiseTauSeconds,
        kSecondaryFallTauSeconds);
    s_smoothedOverlayFrameUs = smoothToward(
        s_smoothedOverlayFrameUs,
        static_cast<float>(debug.overlay.frameUs),
        kSecondaryRiseTauSeconds,
        kSecondaryFallTauSeconds);
    s_smoothedOverlayPacingUs = smoothToward(
        s_smoothedOverlayPacingUs,
        static_cast<float>(debug.overlay.pacingWaitUs),
        kSecondaryRiseTauSeconds,
        kSecondaryFallTauSeconds);
    uint64_t heldWorldUs = st.worldScanLastUs;
    if (debug.worldScanAgeUs > 0) {
        const uint64_t staleWorldThresholdUs =
            std::max<uint64_t>(debug.worldScanTargetIntervalUs * 2u, 120000u);
        if (debug.worldScanAgeUs > staleWorldThresholdUs)
            heldWorldUs = 0;
    }
    const uint64_t coreUs =
        st.engineUs + st.baseReadsUs + st.playerReadsUs + st.commitStateUs + st.commitEnrichUs;
    const uint64_t commitUs = st.commitStateUs + st.commitEnrichUs;
    const uint64_t deferredCurrentUs =
        st.playerAuxUs +
        st.inventoryUs +
        st.boneReadsUs;
    const uint64_t deferredHeldUs =
        std::max(st.playerAuxUs, st.playerAuxLastUs) +
        std::max(st.inventoryUs, st.inventoryLastUs) +
        std::max(st.boneReadsUs, st.boneReadsLastUs);
    const uint64_t worldCurrentUs = st.worldScanUs;
    const uint64_t worldHeldUs = heldWorldUs;
    s_smoothedCoreUs = smoothToward(
        s_smoothedCoreUs,
        static_cast<float>(coreUs),
        kPrimaryRiseTauSeconds,
        kPrimaryFallTauSeconds);
    s_smoothedBaseUs = smoothToward(
        s_smoothedBaseUs,
        static_cast<float>(st.baseReadsUs),
        kSecondaryRiseTauSeconds,
        kSecondaryFallTauSeconds);
    s_smoothedPlayerReadsUs = smoothToward(
        s_smoothedPlayerReadsUs,
        static_cast<float>(st.playerReadsUs),
        kSecondaryRiseTauSeconds,
        kSecondaryFallTauSeconds);
    s_smoothedEngineUs = smoothToward(
        s_smoothedEngineUs,
        static_cast<float>(st.engineUs),
        kSecondaryRiseTauSeconds,
        kSecondaryFallTauSeconds);
    s_smoothedCommitUs = smoothToward(
        s_smoothedCommitUs,
        static_cast<float>(commitUs),
        kSecondaryRiseTauSeconds,
        kSecondaryFallTauSeconds);
    s_smoothedDeferredCurrentUs = smoothToward(
        s_smoothedDeferredCurrentUs,
        static_cast<float>(deferredCurrentUs),
        kPrimaryRiseTauSeconds,
        kPrimaryFallTauSeconds);
    s_smoothedDeferredHeldUs = smoothToward(
        s_smoothedDeferredHeldUs,
        static_cast<float>(deferredHeldUs),
        kPrimaryRiseTauSeconds,
        kPrimaryFallTauSeconds);
    s_smoothedBombUs = smoothToward(
        s_smoothedBombUs,
        static_cast<float>(st.bombScanUs),
        kPrimaryRiseTauSeconds,
        kPrimaryFallTauSeconds);
    s_smoothedWorldCurrentUs = smoothToward(
        s_smoothedWorldCurrentUs,
        static_cast<float>(worldCurrentUs),
        0.65f,
        1.00f);
    s_smoothedWorldHeldUs = smoothToward(
        s_smoothedWorldHeldUs,
        static_cast<float>(worldHeldUs),
        0.65f,
        1.00f);
    s_smoothedAuxUs = smoothToward(
        s_smoothedAuxUs,
        static_cast<float>(st.playerAuxUs),
        kSecondaryRiseTauSeconds,
        kSecondaryFallTauSeconds);
    s_smoothedInvUs = smoothToward(
        s_smoothedInvUs,
        static_cast<float>(st.inventoryUs),
        kSecondaryRiseTauSeconds,
        kSecondaryFallTauSeconds);
    s_smoothedBonesUs = smoothToward(
        s_smoothedBonesUs,
        static_cast<float>(st.boneReadsUs),
        kSecondaryRiseTauSeconds,
        kSecondaryFallTauSeconds);
    s_smoothedWorldTickUs = smoothToward(
        s_smoothedWorldTickUs,
        static_cast<float>(st.worldScanUs),
        0.45f,
        0.70f);
    s_smoothedWorldLastUs = smoothToward(
        s_smoothedWorldLastUs,
        static_cast<float>(st.worldScanLastUs),
        0.50f,
        0.80f);
    const char* bombStateLabel = "Hidden";
    if (debug.bombPlanted)
        bombStateLabel = debug.bombTicking ? "Ticking" : "Planted";
    else if (debug.bombDropped)
        bombStateLabel = "Dropped";
    else if (debug.bombCarried)
        bombStateLabel = "Carried";

    auto warmupStateLabel = [](esp::SceneWarmupState state) -> const char* {
        switch (state) {
        case esp::SceneWarmupState::ColdAttach: return "cold_attach";
        case esp::SceneWarmupState::SceneTransition: return "scene_transition";
        case esp::SceneWarmupState::HierarchyWarming: return "hierarchy_warming";
        case esp::SceneWarmupState::Stable: return "stable";
        case esp::SceneWarmupState::Recovery: return "recovery";
        default: return "unknown";
        }
    };

    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 2.0f));

    char detail[192] = {};
    std::snprintf(
        detail, sizeof(detail),
        KEVQ_TR("Engine %.2f | Base %.2f | Player %.2f | Commit %.2f"),
        s_smoothedEngineUs / 1000.0f,
        s_smoothedBaseUs / 1000.0f,
        s_smoothedPlayerReadsUs / 1000.0f,
        s_smoothedCommitUs / 1000.0f);
    renderMetricBar("Core Data", s_smoothedCoreUs, detail);

    std::snprintf(
        detail, sizeof(detail),
        KEVQ_TR("Aux %.2f | Inv %.2f | Bones %.2f"),
        s_smoothedAuxUs / 1000.0f,
        s_smoothedInvUs / 1000.0f,
        s_smoothedBonesUs / 1000.0f);
    renderMetricBar("Deferred", s_smoothedDeferredCurrentUs, detail);

    std::snprintf(
        detail, sizeof(detail),
        KEVQ_TR("[ %s%s | src 0x%X | q %u ]"),
        app::localization::Get(bombStateLabel),
        debug.bombBeingDefused ? KEVQ_TR(" | Defusing") : "",
        debug.bombSourceFlags,
        static_cast<unsigned>(debug.bombConfidence));
    renderMetricBar("Bomb", s_smoothedBombUs, detail);

    std::snprintf(
        detail, sizeof(detail),
        KEVQ_TR("Tick %.2f | Age %.0f ms | Markers %d"),
        s_smoothedWorldCurrentUs / 1000.0f,
        worldAgeMs,
        debug.worldMarkerCount);
    renderMetricBar("World", s_smoothedWorldCurrentUs, detail);

    std::snprintf(
        detail, sizeof(detail),
        KEVQ_TR("Cycle %.2f"),
        s_smoothedCameraCycleUs / 1000.0f);
    renderMetricBar("Camera", s_smoothedCameraCycleUs, detail);

    ImGui::Separator();

    ImGui::Text(KEVQ_TR("Players: %d | Budget: %d | Entities: %d"),
        debug.activePlayers, debug.playerSlotBudget, debug.highestEntityIdx);

    if (health.recovering || health.recoveryRequested || health.dataWorkerStalled) {
        ImGui::Separator();
        if (health.dataWorkerStalled)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.25f, 1.0f), "%s", KEVQ_TR("Worker stalled"));
        else if (health.recovering)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", KEVQ_TR("DMA recovering"));
        else if (health.recoveryRequested)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", KEVQ_TR("DMA recovery requested"));
    }
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (ImGui::Button(KEVQ_TR("Copy Diagnostics"), ImVec2(ImGui::GetContentRegionAvail().x, 30.0f))) {
        char line[1024] = {};
        std::string diagnostics;
        diagnostics.reserve(2048);

        const char* dataQuality = "healthy";
        for (const auto* subsystem : { &debug.playersCore, &debug.cameraView,
                                      &debug.gamerulesMap, &debug.bones, &debug.world }) {
            if (subsystem->state == esp::SubsystemHealthState::Failed) {
                dataQuality = "failed";
                break;
            }
            if (subsystem->state == esp::SubsystemHealthState::Degraded)
                dataQuality = "degraded";
        }
        // Healthy reads of a single resolved pawn do not mean roster warmup
        // is complete. Idle scenes intentionally have no world/player data.
        if (debug.engineResolved && debug.engineMenu && !debug.engineInGame)
            dataQuality = "idle";
        else if (!debug.engineResolved)
            dataQuality = "unknown";
        else if (std::strcmp(dataQuality, "healthy") == 0 &&
                 debug.warmupState != esp::SceneWarmupState::Stable)
            dataQuality = "warming";

        std::snprintf(line, sizeof(line),
            "status=%s data_quality=%s data_worker=%s camera_worker=%s game_status=%d\n",
            statusLabel,
            dataQuality,
            health.workerRunning ? "running" : "stopped",
            health.cameraWorkerRunning ? "running" : "stopped",
            static_cast<int>(health.gameStatus));
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "worker loop_age_ms=%.1f in_flight=%d in_flight_age_ms=%.1f stalled=%d\n",
            workerLoopAgeMs,
            health.dataWorkerInFlight ? 1 : 0,
            workerInFlightAgeMs,
            health.dataWorkerStalled ? 1 : 0);
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "publishes=%llu snapshot_age_ms=%.1f camera_view=%s(%.1f) camera_local=%s(%.1f) snapshot_state=%s\n",
            static_cast<unsigned long long>(debug.publishCount),
            rawSnapshotAgeMs,
            idleScene ? "idle" : (debug.liveViewValid ? (debug.liveViewFresh ? "fresh" : "stale") : "invalid"),
            rawCameraViewAgeMs,
            idleScene ? "idle" : (debug.liveLocalPosValid ? (debug.liveLocalPosFresh ? "fresh" : "stale") : "invalid"),
            rawCameraLocalAgeMs,
            idleScene ? "idle_clear" : (!debug.engineResolved ? "unresolved" : "active"));
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "engine menu=%d ingame=%d max_clients=%d slot_budget=%d resolved=%d signon=%d background=%d\n",
            debug.engineMenu ? 1 : 0,
            debug.engineInGame ? 1 : 0,
            debug.engineMaxClients,
            debug.playerSlotBudget,
            debug.engineResolved ? 1 : 0,
            debug.engineSignOnState,
            debug.engineBackgroundMap ? 1 : 0);
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "epochs scene=%llu map=%llu bomb=%llu warmup=%s warmup_age_ms=%.1f\n",
            static_cast<unsigned long long>(debug.sceneEpoch),
            static_cast<unsigned long long>(debug.mapEpoch),
            static_cast<unsigned long long>(debug.bombEpoch),
            warmupStateLabel(debug.warmupState),
            msFromUs(debug.warmupAgeUs));
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "publication_drops snapshot=%llu visibility=%llu camera=%llu settings_snapshot_reuse=%llu\n",
            static_cast<unsigned long long>(debug.publishDropCount),
            static_cast<unsigned long long>(debug.visibilityPublishDropCount),
            static_cast<unsigned long long>(debug.cameraPublishDropCount),
            static_cast<unsigned long long>(debug.settingsSnapshotReuseCount));
        diagnostics += line;

        auto resetKindLabel = [](esp::RuntimeResetKind kind) -> const char* {
            switch (kind) {
            case esp::RuntimeResetKind::None: return "none";
            case esp::RuntimeResetKind::Soft: return "soft";
            case esp::RuntimeResetKind::Hard: return "hard";
            default: return "unknown";
            }
        };

        std::snprintf(line, sizeof(line),
            "last_reset kind=%s published_clear=%d age_ms=%.1f reason=%s\n",
            resetKindLabel(debug.lastResetKind),
            debug.lastResetPublishedClearedSnapshot ? 1 : 0,
            msFromUs(debug.lastResetAgeUs),
            debug.lastResetReason[0] ? debug.lastResetReason : "none");
        diagnostics += line;

        double avgHandleCreateMs = 0.0;
        if (debug.dma.scatterHandleCreateCount > 0) {
            avgHandleCreateMs =
                (static_cast<double>(debug.dma.scatterHandleCreateTotalUs) / 1000.0) /
                static_cast<double>(debug.dma.scatterHandleCreateCount);
        }
        double avgHandleCloseMs = 0.0;
        if (debug.dma.scatterHandleCloseCount > 0) {
            avgHandleCloseMs =
                (static_cast<double>(debug.dma.scatterHandleCloseTotalUs) / 1000.0) /
                static_cast<double>(debug.dma.scatterHandleCloseCount);
        }
        std::snprintf(line, sizeof(line),
            "dma_handles session=%llu pid=%lu create_avg_ms=%.3f create_peak_ms=%.3f creates=%llu close_avg_ms=%.3f close_peak_ms=%.3f closes=%llu\n",
            static_cast<unsigned long long>(debug.dma.sessionGeneration),
            static_cast<unsigned long>(debug.dma.attachedProcessId),
            avgHandleCreateMs,
            static_cast<double>(debug.dma.scatterHandleCreatePeakUs) / 1000.0,
            static_cast<unsigned long long>(debug.dma.scatterHandleCreateCount),
            avgHandleCloseMs,
            static_cast<double>(debug.dma.scatterHandleClosePeakUs) / 1000.0,
            static_cast<unsigned long long>(debug.dma.scatterHandleCloseCount));
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "subsystems players=%s@%.1f/%u camera=%s@%.1f/%u gamerules=%s@%.1f/%u bones=%s@%.1f/%u world=%s@%.1f/%u mode=%s\n",
            subsystemStateLabel(debug.playersCore.state),
            msFromUs(debug.playersCore.lastGoodAgeUs),
            debug.playersCore.failureStreak,
            subsystemStateLabel(debug.cameraView.state),
            msFromUs(debug.cameraView.lastGoodAgeUs),
            debug.cameraView.failureStreak,
            subsystemStateLabel(debug.gamerulesMap.state),
            msFromUs(debug.gamerulesMap.lastGoodAgeUs),
            debug.gamerulesMap.failureStreak,
            subsystemStateLabel(debug.bones.state),
            msFromUs(debug.bones.lastGoodAgeUs),
            debug.bones.failureStreak,
            subsystemStateLabel(debug.world.state),
            msFromUs(debug.world.lastGoodAgeUs),
            debug.world.failureStreak,
            idleScene ? "idle" : (debug.engineResolved ? "active" : "unresolved"));
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "cycle_ms=%.2f recent_peak_ms=%.2f lifetime_peak_ms=%.2f schedule_skips_window=%llu current_stage_sum_ms=%.2f estimated_lane_peak_ms=%.2f budget_ms=%.2f data_hz=%d camera_hz=%d camera_reads=%d budget_kind=live_reference\n",
            msFromUs(debug.cycleUs),
            msFromUs(debug.recentMaxCycleUs),
            msFromUs(debug.maxCycleUs),
            static_cast<unsigned long long>(debug.deadlineMissCount),
            msFromUs(st.totalUs),
            msFromUs(st.totalHeldUs),
            budgetMs,
            debug.dataWorkerTargetHz,
            debug.cameraWorkerTargetHz,
            debug.cameraReadsEnabled ? 1 : 0);
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "cycle_freq window_ms=%.0f samples=%llu p50_ms=%.2f p95_ms=%.2f p99_ms=%.2f over_budget=%llu(%.1f%%) over_5ms=%llu(%.1f%%) over_16ms=%llu(%.1f%%) percentiles=bucket_upper_bounds observed_hz=%.1f\n",
            msFromUs(debug.cycleWindowAgeUs),
            static_cast<unsigned long long>(debug.cycleSampleCount10s),
            msFromUs(debug.cycleP50Us),
            msFromUs(debug.cycleP95Us),
            msFromUs(debug.cycleP99Us),
            static_cast<unsigned long long>(debug.cycleOverBudgetCount10s),
            percentOf(debug.cycleOverBudgetCount10s, debug.cycleSampleCount10s),
            static_cast<unsigned long long>(debug.cycleOver5msCount10s),
            percentOf(debug.cycleOver5msCount10s, debug.cycleSampleCount10s),
            static_cast<unsigned long long>(debug.cycleOver16msCount10s),
            percentOf(debug.cycleOver16msCount10s, debug.cycleSampleCount10s),
            debug.cycleWindowAgeUs > 0 ? 1000000.0 *
                static_cast<double>(debug.cycleSampleCount10s) / debug.cycleWindowAgeUs : 0.0);
        diagnostics += line;

        const char* playerCoreBatchQuality =
            debug.playerCoreBatchQuality == static_cast<uint8_t>(
                esp::data::PlayerCoreBatchDecision::Coherent)
                ? "coherent"
                : (debug.playerCoreBatchQuality == static_cast<uint8_t>(
                       esp::data::PlayerCoreBatchDecision::Degraded)
                       ? "degraded"
                       : "none");
        std::snprintf(line, sizeof(line),
            "player_pipeline controller_candidates=%d resolved=%d core_plausible=%d core_gen=%llu core_age_ms=%.1f core_batch=%s batch_holds_window=%llu identity_dedup=%d backlink_mismatch=%d stride=0x%X held_hierarchy=%d held_zero_pawn=%d held_core=%d core_anomaly_window=%llu recovered_window=%llu global_refresh_avoided_window=%llu unexpected_evictions_window=%llu expected_evictions_window=%llu\n",
            debug.playerControllerSlots,
            debug.playerResolvedSlots,
            debug.playerPlausibleCoreSlots,
            static_cast<unsigned long long>(debug.playerCoreGeneration),
            msFromUs(debug.playerCoreGenerationAgeUs),
            playerCoreBatchQuality,
            static_cast<unsigned long long>(debug.playerCoreBatchHoldCount10s),
            debug.playerDuplicateIdentityFiltered,
            debug.playerBacklinkMismatchCount,
            debug.entitySlotStride,
            debug.playerHierarchyHeldSlots,
            debug.playerZeroPawnHeldSlots,
            debug.playerCoreHeldSlots,
            static_cast<unsigned long long>(debug.playerCoreAnomalyCount10s),
            static_cast<unsigned long long>(debug.playerCoreRecoveredCount10s),
            static_cast<unsigned long long>(debug.playerCoreGlobalRefreshAvoidedCount10s),
            static_cast<unsigned long long>(debug.playerUnexpectedEvictionCount10s),
            static_cast<unsigned long long>(debug.playerExpectedEvictionCount10s));
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "stages_ms engine=%.2f base=%.2f player_hierarchy=%.2f player_core=%.2f player_repair=%.2f player_total=%.2f commit=%.2f player_aux=%.2f inventory=%.2f bones=%.2f bomb=%.2f world_tick=%.2f world_last=%.2f enrich=%.2f\n",
            msFromUs(st.engineUs),
            msFromUs(st.baseReadsUs),
            msFromUs(st.playerHierarchyUs),
            msFromUs(st.playerCoreUs),
            msFromUs(st.playerRepairUs),
            msFromUs(st.playerReadsUs),
            msFromUs(st.commitStateUs),
            msFromUs(st.playerAuxUs),
            msFromUs(st.inventoryUs),
            msFromUs(st.boneReadsUs),
            msFromUs(st.bombScanUs),
            worldTickMs,
            worldLastMs,
            msFromUs(st.commitEnrichUs));
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "lane_last_ms player_aux=%.2f@%.1f inventory=%.2f@%.1f bones=%.2f@%.1f\n",
            msFromUs(st.playerAuxLastUs), msFromUs(st.playerAuxAgeUs),
            msFromUs(st.inventoryLastUs), msFromUs(st.inventoryAgeUs),
            msFromUs(st.boneReadsLastUs), msFromUs(st.boneReadsAgeUs));
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "bone_batch slots=%u pointer_slots=%u pointer_validate_ms=%.1f pose_ranges=%u bytes=%u joints=%d target_ms=%.1f spread_ticks=%d\n",
            st.bonePoseSlots,
            st.bonePointerValidationSlots,
            static_cast<double>(esp::data::kBonePointerValidationUs) / 1000.0,
            st.bonePoseRanges,
            st.bonePoseBytes,
            esp::data::kBoneReadTransformCount,
            static_cast<double>(esp::intervals::kBoneReadsUs) / 1000.0,
            esp::data::kBoneReadSpreadTicks);
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "world_scan age_ms=%.1f target_ms=%.1f markers=%d tracked=%d candidates=%d utility=%d classified=%d identity_pending=%d active_players=%d entity_range=%d\n",
            rawWorldAgeMs,
            worldTargetMs,
            debug.worldMarkerCount,
            debug.worldTrackedEntityCount,
            debug.worldCandidateCount,
            debug.worldUtilityCandidateCount,
            debug.worldClassifiedCandidateCount,
            debug.worldIdentityPendingCount,
            debug.activePlayers,
            debug.highestEntityIdx);
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "world_quality totals_since_reset capacity_drops=%llu read_gap_holds=%llu position_read_misses=%llu\n",
            static_cast<unsigned long long>(debug.worldMarkerCapacityDrops),
            static_cast<unsigned long long>(debug.worldMarkerReadGapHolds),
            static_cast<unsigned long long>(debug.worldPositionReadMisses));
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "player_core_rejections incomplete_mask=0x%llX invalid_values_mask=0x%llX\n",
            static_cast<unsigned long long>(debug.playerCoreIncompleteMask),
            static_cast<unsigned long long>(debug.playerCoreInvalidMask));
        diagnostics += line;

        const auto visibilityState = esp::data::ResolveVisibilityDiagnosticState(
            debug.visibilityEnabled,
            debug.activePlayers,
            debug.visibilityLocalMaskResolved,
            debug.visibilityFreshSlots,
            debug.visibilityCommitAgeUs);
        const char* visibilityStateName =
            visibilityState == esp::data::VisibilityDiagnosticState::Disabled ? "disabled" :
            visibilityState == esp::data::VisibilityDiagnosticState::Healthy ? "healthy" :
            visibilityState == esp::data::VisibilityDiagnosticState::Unavailable ? "unavailable" :
            visibilityState == esp::data::VisibilityDiagnosticState::Stale ? "stale" :
            "waiting";
        std::snprintf(line, sizeof(line),
            "visibility state=%s fresh=%d visible=%d mask=%d crosshair=%d age_ms=%.1f local_mask=%d\n",
            visibilityStateName,
            debug.visibilityFreshSlots,
            debug.visibilityVisibleSlots,
            debug.visibilityMaskSlots,
            debug.visibilityCrosshairSlot,
            msFromUs(debug.visibilityCommitAgeUs),
            debug.visibilityLocalMaskResolved ? 1 : 0);
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "camera_ms cycle=%.2f recent_peak=%.2f lifetime_peak=%.2f window_ms=%.0f p50=%.2f p95=%.2f p99=%.2f schedule_skips_window=%llu samples_window=%llu over_budget=%llu(%.1f%%) over_5ms=%llu(%.1f%%) over_16ms=%llu(%.1f%%) overlay_ms frame=%.2f lifetime_peak=%.2f sync=%.2f draw=%.2f present=%.2f pacing_wait=%.2f\n",
            msFromUs(debug.camera.cycleUs),
            msFromUs(debug.camera.recentMaxCycleUs),
            msFromUs(debug.camera.maxCycleUs),
            msFromUs(debug.camera.recentWindowAgeUs),
            msFromUs(debug.camera.p50Us),
            msFromUs(debug.camera.p95Us),
            msFromUs(debug.camera.p99Us),
            static_cast<unsigned long long>(debug.camera.deadlineMissCount),
            static_cast<unsigned long long>(debug.camera.sampleCount10s),
            static_cast<unsigned long long>(debug.camera.overBudgetCount10s),
            percentOf(debug.camera.overBudgetCount10s, debug.camera.sampleCount10s),
            static_cast<unsigned long long>(debug.camera.over5msCount10s),
            percentOf(debug.camera.over5msCount10s, debug.camera.sampleCount10s),
            static_cast<unsigned long long>(debug.camera.over16msCount10s),
            percentOf(debug.camera.over16msCount10s, debug.camera.sampleCount10s),
            msFromUs(debug.overlay.frameUs),
            msFromUs(debug.overlay.maxFrameUs),
            msFromUs(debug.overlay.syncUs),
            msFromUs(debug.overlay.drawUs),
            msFromUs(debug.overlay.presentUs),
            msFromUs(debug.overlay.pacingWaitUs));
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "bomb state=%s planted=%d ticking=%d dropped=%d drop_confirmed=%d carried=%d defusing=%d bounds=%d source=0x%X raw=0x%X meta_due=%d meta_ok=%d meta_fresh=%d confidence=%u defuser_slot=%d blow_left_ms=%d defuse_left_ms=%d\n",
            bombStateLabel,
            debug.bombPlanted ? 1 : 0,
            debug.bombTicking ? 1 : 0,
            debug.bombDropped ? 1 : 0,
            debug.bombDropped &&
                    debug.bombConfidence >=
                        esp::data::kDroppedC4ConfirmedScore
                ? 1
                : 0,
            debug.bombCarried ? 1 : 0,
            debug.bombBeingDefused ? 1 : 0,
            debug.bombBoundsValid ? 1 : 0,
            debug.bombSourceFlags,
            debug.bombRawFlags,
            (debug.bombRawFlags & (1u << 11)) != 0u ? 1 : 0,
            (debug.bombRawFlags & (1u << 12)) != 0u ? 1 : 0,
            (debug.bombRawFlags & (1u << 18)) != 0u ? 1 : 0,
            static_cast<unsigned>(debug.bombConfidence),
            debug.bombDefuserSlot,
            debug.bombBlowLeftMs,
            debug.bombDefuseLeftMs);
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "bomb_position valid=%d age_ms=%.1f cached=%d\n",
            debug.bombPositionValid ? 1 : 0,
            debug.bombPositionValid ? msFromUs(debug.bombPositionAgeUs) : -1.0,
            (debug.bombSourceFlags & esp::data::kBombCachedPositionSources) != 0u ? 1 : 0);
        diagnostics += line;

        const uint64_t dropPublication = debug.bombDropPublicationDebug;
        std::snprintf(line, sizeof(line),
            "bomb_drop_gate reason=%s samples=%u/%d lookup=%u ordinal=%u candidate_valid=%d identity_valid=%d sample_fresh=%d\n",
            esp::data::DroppedC4PublicationStatusName(
                static_cast<esp::data::DroppedC4PublicationStatus>(dropPublication & 0xFFu)),
            static_cast<unsigned>((dropPublication >> 8) & 0xFFu),
            esp::data::kWeaponC4CandidateConfirmSamples,
            static_cast<unsigned>((dropPublication >> 16) & 0xFFu),
            static_cast<unsigned>(dropPublication >> 32),
            (dropPublication & (1ull << 24)) != 0 ? 1 : 0,
            (dropPublication & (1ull << 25)) != 0 ? 1 : 0,
            (dropPublication & (1ull << 26)) != 0 ? 1 : 0);
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "dma ok=%llu degraded=%llu fail=%llu consec_degraded=%u consec_fail=%u recoveries=%llu recovering=%d recovery_requested=%d recovery_age_ms=%llu last_success_age_ms=%llu\n",
            static_cast<unsigned long long>(health.totalSuccesses),
            static_cast<unsigned long long>(health.totalDegraded),
            static_cast<unsigned long long>(health.totalFailures),
            health.consecutiveDegraded,
            health.consecutiveFailures,
            static_cast<unsigned long long>(health.totalRecoveries),
            health.recovering ? 1 : 0,
            health.recoveryRequested ? 1 : 0,
            static_cast<unsigned long long>(health.recoveryRequestAgeMs),
            static_cast<unsigned long long>(health.lastSuccessAgeMs));
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "dma_cache mode=%s verified=%d background_refresh=%d nocache_reads=1 library_read_cache_ms=%llu tlb_ms=%llu process_partial_ms=%llu process_full_ms=%llu slow_estimate_ms=%llu\n",
            esp::recovery::DmaCacheModeName(static_cast<esp::DmaCacheMode>(debug.dma.cacheMode)),
            debug.dma.cacheProfileVerified ? 1 : 0,
            debug.dma.backgroundRefreshEnabled ? 1 : 0,
            static_cast<unsigned long long>(debug.dma.readCacheIntervalMs),
            static_cast<unsigned long long>(debug.dma.tlbCacheIntervalMs),
            static_cast<unsigned long long>(debug.dma.processPartialIntervalMs),
            static_cast<unsigned long long>(debug.dma.processFullIntervalMs),
            static_cast<unsigned long long>(debug.dma.estimatedSlowIntervalMs));
        diagnostics += line;

        double avgScatterMs = 0.0;
        if (debug.dma.executeScatterCount > 0) {
            avgScatterMs = (static_cast<double>(debug.dma.executeScatterTotalUs) / 1000.0) / static_cast<double>(debug.dma.executeScatterCount);
        }
        std::snprintf(line, sizeof(line),
            "dma_scatter_api avg_ms=%.3f recent_peak_ms=%.3f lifetime_peak_ms=%.3f count=%llu window_ms=%.0f samples_window=%llu over_1ms=%llu(%.1f%%) over_budget=%llu(%.1f%%) over_5ms=%llu(%.1f%%) over_16ms=%llu(%.1f%%) budget_ms=%.2f\n",
            avgScatterMs,
            static_cast<double>(debug.dma.executeScatterRecentPeakUs) / 1000.0,
            static_cast<double>(debug.dma.executeScatterPeakUs) / 1000.0,
            static_cast<unsigned long long>(debug.dma.executeScatterCount),
            msFromUs(debug.dma.executeScatterRecentWindowAgeUs),
            static_cast<unsigned long long>(debug.dma.executeScatterRecentCount),
            static_cast<unsigned long long>(debug.dma.executeScatterOver1msRecentCount),
            percentOf(debug.dma.executeScatterOver1msRecentCount, debug.dma.executeScatterRecentCount),
            static_cast<unsigned long long>(debug.dma.executeScatterOverBudgetRecentCount),
            percentOf(debug.dma.executeScatterOverBudgetRecentCount, debug.dma.executeScatterRecentCount),
            static_cast<unsigned long long>(debug.dma.executeScatterOver5msRecentCount),
            percentOf(debug.dma.executeScatterOver5msRecentCount, debug.dma.executeScatterRecentCount),
            static_cast<unsigned long long>(debug.dma.executeScatterOver16msRecentCount),
            percentOf(debug.dma.executeScatterOver16msRecentCount, debug.dma.executeScatterRecentCount),
            msFromUs(debug.dma.scatterBudgetUs));
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "dma_read_quality totals_since_start requests=%llu requested_bytes=%llu completed_bytes=%llu incomplete_requests=%llu partial_batches=%llu setup_failures=%llu\n",
            static_cast<unsigned long long>(debug.dma.scatterRequests),
            static_cast<unsigned long long>(debug.dma.scatterRequestedBytes),
            static_cast<unsigned long long>(debug.dma.scatterCompletedBytes),
            static_cast<unsigned long long>(debug.dma.scatterIncompleteRequests),
            static_cast<unsigned long long>(debug.dma.scatterPartialBatches),
            static_cast<unsigned long long>(debug.dma.scatterSetupFailures));
        diagnostics += line;

        const auto& qualityInterval = debug.dma.readQualityInterval;
        std::snprintf(line, sizeof(line),
            "dma_read_quality_interval ready=%d window_ms=%.1f sample_age_ms=%.1f requests=%llu incomplete_requests=%llu partial_batches=%llu setup_failures=%llu scope=all_scatter_threads boundary=approximate\n",
            qualityInterval.valid ? 1 : 0,
            msFromUs(qualityInterval.durationUs),
            msFromUs(debug.dma.readQualityIntervalAgeUs),
            static_cast<unsigned long long>(qualityInterval.counts[0]),
            static_cast<unsigned long long>(qualityInterval.counts[1]),
            static_cast<unsigned long long>(qualityInterval.counts[2]),
            static_cast<unsigned long long>(qualityInterval.counts[3]));
        diagnostics += line;

        std::snprintf(line, sizeof(line),
            "dma_manual_refresh active=%d window_ms=%.0f last_ms=%.3f recent_peak_ms=%.3f lifetime_peak_ms=%.3f executed=%llu executed_window=%llu queued=%llu queued_window=%llu suppressed=%llu suppressed_window=%llu avoided=%llu avoided_window=%llu coalesced=%llu camera_pause=%u paused=%d admin=%d orphan_recovered=%llu release_imbalances=%llu\n",
            debug.dma.manualRefreshInProgress ? 1 : 0,
            msFromUs(debug.cycleWindowAgeUs),
            static_cast<double>(debug.dma.manualRefreshLastUs) / 1000.0,
            static_cast<double>(debug.dma.manualRefreshRecentPeakUs) / 1000.0,
            static_cast<double>(debug.dma.manualRefreshPeakUs) / 1000.0,
            static_cast<unsigned long long>(debug.dma.manualRefreshCount),
            static_cast<unsigned long long>(debug.dma.manualRefreshRecentCount),
            static_cast<unsigned long long>(debug.dma.manualRefreshQueuedCount),
            static_cast<unsigned long long>(debug.dma.manualRefreshQueuedRecentCount),
            static_cast<unsigned long long>(debug.dma.manualRefreshSuppressedCount),
            static_cast<unsigned long long>(debug.dma.manualRefreshSuppressedRecentCount),
            static_cast<unsigned long long>(debug.dma.manualRefreshAvoidedCount),
            static_cast<unsigned long long>(debug.dma.manualRefreshAvoidedRecentCount),
            static_cast<unsigned long long>(debug.dma.manualRefreshCoalescedCount),
            debug.dma.cameraPauseRequests,
            debug.dma.cameraWorkerPaused ? 1 : 0,
            debug.dma.adminPauseActive ? 1 : 0,
            static_cast<unsigned long long>(debug.dma.cameraPauseOrphanRecoveryCount),
            static_cast<unsigned long long>(debug.dma.cameraPauseReleaseImbalanceCount));
        diagnostics += line;

        if (health.eventCount > 0) {
            diagnostics += "dma_events";
            for (int i = 0; i < health.eventCount; ++i) {
                const auto& event = health.events[i];
                std::snprintf(line, sizeof(line),
                    " [%d]=%s:%s@%llums",
                    i,
                    event.action,
                    event.reason,
                    static_cast<unsigned long long>(event.ageMs));
                diagnostics += line;
            }
            diagnostics += "\n";
        }

        if (debug.espEventCount > 0) {
            diagnostics += "esp_trace";
            for (int i = 0; i < debug.espEventCount; ++i) {
                const auto& event = debug.espEvents[i];
                if (std::strcmp(event.type, "bone_rejected") == 0) {
                    const auto reason = static_cast<esp::data::BonePlausibilityRejectReason>(event.param);
                    std::snprintf(line, sizeof(line),
                        " [%d]=%s:slot=%u:reason=%s@%llums",
                        i,
                        event.type,
                        static_cast<unsigned>(event.slot),
                        esp::data::BonePlausibilityRejectReasonName(reason),
                        static_cast<unsigned long long>(event.ageMs));
                }
                else {
                    std::snprintf(line, sizeof(line),
                        " [%d]=%s:slot=%u:param=%u@%llums",
                        i,
                        event.type,
                        static_cast<unsigned>(event.slot),
                        static_cast<unsigned>(event.param),
                        static_cast<unsigned long long>(event.ageMs));
                }
                diagnostics += line;
            }
            diagnostics += "\n";
        }

        const uint64_t uptimeSec = sessionUptimeUs / 1000000u;
        std::snprintf(line, sizeof(line),
            "uptime=%02d:%02d:%02d",
            static_cast<int>(uptimeSec / 3600),
            static_cast<int>((uptimeSec % 3600) / 60),
            static_cast<int>(uptimeSec % 60));
        diagnostics += line;
        ImGui::SetClipboardText(diagnostics.c_str());
        s_lastCopyTime = ImGui::GetTime();
    }
    if (s_lastCopyTime > 0.0 && (ImGui::GetTime() - s_lastCopyTime) < 1.5)
        ImGui::TextColored(
            ImVec4(0.35f, 0.85f, 0.45f, 1.0f),
            "%s",
            KEVQ_TR("Diagnostics copied to clipboard."));

    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (ImGui::Button(
            KEVQ_TR("Refresh Cache Data"),
            ImVec2(ImGui::GetContentRegionAvail().x, 30.0f))) {
        esp::RequestCacheRefresh();
    }

    ImGui::End();
}
