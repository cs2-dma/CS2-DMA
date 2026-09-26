#include "Features/Target/UI/target_tab.h"

#include "Features/Target/physics_bvh.h"
#include "Features/Target/target.h"
#include "Features/Target/target_policy.h"
#include "Features/Target/target_settings_policy.h"
#include "app/Core/globals.h"
#include "app/Core/build_info.h"
#include "app/Input/input_device.h"
#include "app/Input/primary_keyboard.h"
#include "app/Localization/localization.h"
#include "app/UI/MenuShell/menu_utils.h"
#include "app/UI/MenuShell/ui_icons.h"
#include "app/UI/MenuShell/ui_widgets.h"

#include <imgui.h>
#if defined(KEVQ_UI_SMOKE_TESTS)
#include <imgui_internal.h>
#include <vector>
#endif
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
    constexpr float kRowHeight = 50.0f;
    constexpr float kRowGap = 12.0f;
    constexpr float kColumnGap = 18.0f;
    constexpr ImGuiColorEditFlags kColorFlags =
        ImGuiColorEditFlags_AlphaBar |
        ImGuiColorEditFlags_AlphaPreviewHalf |
        ImGuiColorEditFlags_NoInputs |
        ImGuiColorEditFlags_NoLabel;

    std::string s_activeSettings;
    std::string s_keyCaptureId;
    std::array<uint8_t, 256> s_keySnapshot = {};
    int s_weaponProfileCategory = 1;
    bool s_followWeaponProfile = false;
    bool s_weaponProfileAvailable = false;
    int s_aimPage = 0;
    int s_triggerPage = 0;
    std::string s_tooltipRow;
    double s_tooltipStartedAt = 0.0;
    int s_tooltipFrame = -2;
#if defined(KEVQ_UI_SMOKE_TESTS)
    bool s_testDiagnosticsOpen = false;
    bool s_testTuningOpen = false;
    int s_testSettingsPage = 0;
    struct TooltipRowState
    {
        std::string id;
        ImVec4 rectangle;
        bool disabled;
    };
    std::vector<TooltipRowState> s_testTooltipRows;
#endif

    void SetControlRowTooltip(const char* id, const char* text)
    {
        const ImVec2 itemMin = ImGui::GetItemRectMin();
        const ImVec2 itemMax = ImGui::GetItemRectMax();
        const ImVec2 rowMin(itemMin.x, itemMin.y - 50.0f);
        const ImVec2 rowMax(itemMax.x, itemMin.y - 8.0f);
#if defined(KEVQ_UI_SMOKE_TESTS)
        s_testTooltipRows.push_back({id, ImVec4(rowMin.x, rowMin.y, rowMax.x, rowMax.y),
            (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0});
#endif
        if (!ImGui::IsWindowHovered() || ImGui::IsAnyItemActive() ||
            !ImGui::IsMouseHoveringRect(rowMin, rowMax))
            return;
        const int frame = ImGui::GetFrameCount();
        if (s_tooltipRow != id || (s_tooltipFrame != frame && s_tooltipFrame != frame - 1)) {
            s_tooltipRow = id;
            s_tooltipStartedAt = ImGui::GetTime();
        }
        s_tooltipFrame = frame;
        if (ImGui::GetTime() - s_tooltipStartedAt >= ImGui::GetStyle().HoverDelayNormal)
            ImGui::SetTooltip("%s", KEVQ_TR(text));
    }

    template <typename Body>
    void RenderProfileControls(Body&& body)
    {
        ImGui::BeginDisabled(s_followWeaponProfile && !s_weaponProfileAvailable);
        body();
        ImGui::EndDisabled();
    }

    bool TuningHeader(const char* label)
    {
#if defined(KEVQ_UI_SMOKE_TESTS)
        ImGui::SetNextItemOpen(s_testTuningOpen, ImGuiCond_Always);
#endif
        return ImGui::CollapsingHeader(KEVQ_TR(label));
    }

    bool RenderChoiceRow(const char* id, const char* label, int& selected,
        const char* const* choices, int count)
    {
        const auto row = ui::widgets::BeginControlRow(id, label,
            std::max(320.0f, ImGui::GetContentRegionAvail().x - 2.0f), 42.0f, false, 225);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - 162.0f, row.min.y + 7.0f));
        ImGui::SetNextItemWidth(150.0f);
        bool changed = false;
        ImGui::PushID(id);
        if (ImGui::BeginCombo("##value", KEVQ_TR(choices[std::clamp(selected, 0, count - 1)]))) {
            for (int i = 0; i < count; ++i) {
                if (ImGui::Selectable(KEVQ_TR(choices[i]), selected == i)) { selected = i; changed = true; }
                if (selected == i) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
        ui::widgets::EndControlRow(row);
        return changed;
    }

    void RenderSettingsPages(const char* id, int& selected, const char* const* labels, int count)
    {
        if (!ImGui::BeginTabBar(id, ImGuiTabBarFlags_FittingPolicyScroll)) return;
        for (int i = 0; i < count; ++i) {
            ImGuiTabItemFlags flags = ImGuiTabItemFlags_None;
#if defined(KEVQ_UI_SMOKE_TESTS)
            if (i == s_testSettingsPage) flags |= ImGuiTabItemFlags_SetSelected;
#endif
            if (ImGui::BeginTabItem(KEVQ_TR(labels[i]), nullptr, flags)) {
                selected = i;
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
        ImGui::Spacing();
    }

    float GridColumnWidth()
    {
        const float available = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        return available >= 520.0f + kColumnGap
            ? std::floor((available - kColumnGap) * 0.5f) : available;
    }

    template <typename LeftFn, typename RightFn>
    void RenderGridPair(float width, LeftFn&& left, RightFn&& right)
    {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const bool twoColumns = ImGui::GetContentRegionAvail().x >= width * 2.0f + kColumnGap - 1.0f;
        left(width);
        ImGui::SetCursorScreenPos(twoColumns ? ImVec2(start.x + width + kColumnGap, start.y)
            : ImVec2(start.x, start.y + kRowHeight + kRowGap));
        right(width);
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + (kRowHeight + kRowGap) * (twoColumns ? 1.0f : 2.0f)));
        ImGui::Dummy(ImVec2(0.0f, 0.0f));
    }

    bool DrawFeatureRow(
        const char* id,
        const char* label,
        ui::icons::Icon icon,
        bool* enabled,
        bool showSettings,
        float width)
    {
        ImGui::PushID(id);
        const bool active = enabled && *enabled;
        const bool selected = s_activeSettings == id;
        const auto row = ui::widgets::BeginControlRow(
            "row",
            "",
            std::max(260.0f, width),
            kRowHeight,
            active,
            active ? 255 : 210);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        (void)ui::icons::DrawCentered(
            drawList,
            g::fontUiIcons,
            icon,
            ImVec2(row.min.x + 14.0f, row.min.y + 15.0f),
            20.0f,
            18.0f,
            ui::widgets::ColorU32(174, 196, 225, 238));
        drawList->AddText(
            ImVec2(
                row.min.x + 48.0f,
                row.min.y + (row.height - ImGui::GetFontSize()) * 0.5f),
            ui::widgets::ColorU32(226, 234, 246, active ? 255 : 210),
            app::localization::Get(label));

        ImGui::SetCursorScreenPos(ImVec2(
            row.max.x - (showSettings ? 88.0f : 50.0f),
            row.min.y + 15.0f));
        ui::widgets::ToggleSwitch("toggle", enabled);

        bool settingsClicked = false;
        if (showSettings) {
            ImGui::SetCursorScreenPos(ImVec2(row.max.x - 38.0f, row.min.y + 12.0f));
            ImGui::InvisibleButton("##settings", ImVec2(26.0f, 26.0f));
            settingsClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
            const bool hovered = ImGui::IsItemHovered();
            if (hovered)
                ImGui::SetItemTooltip("%s", KEVQ_TR("Feature settings"));
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            if (selected || hovered) {
                drawList->AddRectFilled(
                    min,
                    max,
                    selected
                        ? ui::widgets::ColorU32(17, 45, 83, 235)
                        : ui::widgets::ColorU32(15, 27, 42, 225),
                    7.0f);
                drawList->AddRect(
                    min,
                    max,
                    selected
                        ? ui::widgets::ColorU32(65, 132, 238, 205)
                        : ui::widgets::ColorU32(72, 91, 116, 145),
                    7.0f,
                    0,
                    0.8f);
            }
            (void)ui::icons::DrawCentered(
                drawList,
                g::fontUiIcons,
                ui::icons::Icon::Sliders,
                min,
                max.x - min.x,
                15.0f,
                ui::widgets::ColorU32(174, 196, 225, 235));
        }

        ui::widgets::EndControlRow(row);
        ImGui::PopID();
        if (settingsClicked)
            s_activeSettings = selected ? "" : id;
        return settingsClicked;
    }

    void RenderKeyRow(
        const char* id,
        const char* label,
        int* key)
    {
        if (*key < 1 || *key > 0xFE)
            *key = 0x06;
        const auto row = ui::widgets::BeginControlRow(
            id,
            label,
            std::max(320.0f, ImGui::GetContentRegionAvail().x - 2.0f),
            42.0f,
            false,
            225);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - 162.0f, row.min.y + 7.0f));
        const bool capturing = s_keyCaptureId == id;
        const std::string current = capturing
            ? KEVQ_TR("Press a key...")
            : key_names::ToDisplayName(*key);
        if (ImGui::Button(current.c_str(), ImVec2(150.0f, 28.0f))) {
            s_keyCaptureId = id;
            g::targetKeyCaptureUntilMs.store(GetTickCount64() + 500, std::memory_order_release);
            for (int vk = 0; vk < 256; ++vk) {
                s_keySnapshot[vk] =
                    app::input::IsControlKeyDown(vk) ? 1u : 0u;
            }
        }
        if (capturing) {
            g::targetKeyCaptureUntilMs.store(GetTickCount64() + 500, std::memory_order_release);
            for (int vk = 1; vk <= 0xFE; ++vk) {
                const bool down = app::input::IsControlKeyDown(vk);
                const bool wasDown = s_keySnapshot[vk] != 0;
                s_keySnapshot[vk] = down ? 1u : 0u;
                if (!down || wasDown)
                    continue;
                if (vk != VK_ESCAPE)
                    *key = vk;
                s_keyCaptureId.clear();
                g::targetKeyCaptureUntilMs.store(0, std::memory_order_release);
                break;
            }
        }
        ui::widgets::EndControlRow(row);
    }

    void RenderActivationModeRow(const char* id, int* mode)
    {
        static constexpr const char* kModes[] = {"Hold", "Toggle"};
        *mode = target::policy::SanitizeActivationMode(*mode);
        const auto row = ui::widgets::BeginControlRow(
            id,
            "Activation Mode",
            std::max(320.0f, ImGui::GetContentRegionAvail().x - 2.0f),
            42.0f,
            false,
            225);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - 162.0f, row.min.y + 7.0f));
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo("##mode", KEVQ_TR(kModes[*mode]))) {
            for (int index = 0; index < 2; ++index) {
                const bool selected = index == *mode;
                if (ImGui::Selectable(KEVQ_TR(kModes[index]), selected))
                    *mode = index;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ui::widgets::EndControlRow(row);
    }

    void RenderAimPointRow(const char* id, int* point)
    {
        static constexpr const char* kPoints[] = {
            "Head",
            "Neck",
            "Chest",
            "Pelvis",
            "Closest",
            "Auto",
        };
        if (!point)
            return;
        const int selected = target::policy::SanitizeAimBone(*point);
        const auto row = ui::widgets::BeginControlRow(
            id,
            "Aim Point",
            std::max(320.0f, ImGui::GetContentRegionAvail().x - 2.0f),
            42.0f,
            false,
            225);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - 162.0f, row.min.y + 7.0f));
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo("##point", KEVQ_TR(kPoints[selected]))) {
            for (int i = 0; i < static_cast<int>(std::size(kPoints)); ++i) {
                const bool isSelected = i == selected;
                if (ImGui::Selectable(KEVQ_TR(kPoints[i]), isSelected))
                    *point = i;
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ui::widgets::EndControlRow(row);
    }

    void RenderWeaponProfileRow(const char* id)
    {
        static constexpr const char* kProfiles[] = {
            "Pistols",
            "Rifles",
            "Sniper Rifles",
            "SMGs",
            "Shotguns",
            "Machine Guns",
        };
        const auto runtime = target::GetRuntimeStatus();
        s_weaponProfileAvailable = runtime.weaponId != 0 && runtime.weaponProfile >= 0 && runtime.weaponProfile < 6 &&
            runtime.phase != target::RuntimePhase::Disabled && runtime.phase != target::RuntimePhase::DataUnavailable &&
            runtime.phase != target::RuntimePhase::InputUnavailable;
        if (s_followWeaponProfile && s_weaponProfileAvailable && !ImGui::IsAnyItemActive())
            s_weaponProfileCategory = runtime.weaponProfile;
        s_weaponProfileCategory = std::clamp(s_weaponProfileCategory, 0, 5);
        const std::string preview = s_followWeaponProfile
            ? std::string(KEVQ_TR("Auto")) + ": " + KEVQ_TR(s_weaponProfileAvailable ? kProfiles[s_weaponProfileCategory] : "Unavailable")
            : KEVQ_TR(kProfiles[s_weaponProfileCategory]);
        const auto row = ui::widgets::BeginControlRow(
            id,
            "Weapon Profile",
            std::max(320.0f, ImGui::GetContentRegionAvail().x - 2.0f),
            42.0f,
            false,
            225);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - 162.0f, row.min.y + 7.0f));
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::BeginCombo(
                "##weapon_profile",
                preview.c_str())) {
            if (ImGui::Selectable(KEVQ_TR("Current weapon"), s_followWeaponProfile)) {
                s_followWeaponProfile = true;
                if (s_weaponProfileAvailable) s_weaponProfileCategory = runtime.weaponProfile;
            }
            for (int index = 0; index < 6; ++index) {
                const bool selected = !s_followWeaponProfile && index == s_weaponProfileCategory;
                if (ImGui::Selectable(KEVQ_TR(kProfiles[index]), selected)) {
                    s_weaponProfileCategory = index;
                    s_followWeaponProfile = false;
                }
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ui::widgets::EndControlRow(row);
        SetControlRowTooltip(id, "Current weapon follows the active category. Select a category manually to edit it without a live weapon. Runtime always selects the held weapon automatically.");
    }

    void RenderRuntimeDiagnostics()
    {
        using app::input::ConnectionState;
        using target::physics::BuildState;

        const app::input::DeviceStatus input = app::input::GetDeviceStatus();
        const app::input::PrimaryKeyboardStatus primaryKeyboard =
            app::input::GetPrimaryKeyboardStatus();
        const target::physics::Stats geometry = target::physics::GetStats();
        const target::RuntimeStatus runtime = target::GetRuntimeStatus();
        const bool inputReady =
            input.state == ConnectionState::Connected;
        const auto aimKey = app::input::ReadActivationKeyState(g::targetAimKey);
        const bool aimPressed = inputReady && aimKey.available && aimKey.down;

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        const ImVec4 readyColor(0.28f, 0.85f, 0.42f, 1.0f);
        const ImVec4 waitingColor(0.95f, 0.72f, 0.25f, 1.0f);
        const ImVec4 unavailableColor(0.96f, 0.36f, 0.31f, 1.0f);
        if (runtime.pausedByMenu) {
            ImGui::PushStyleColor(ImGuiCol_Text, waitingColor);
            ImGui::TextWrapped("%s", KEVQ_TR("Target is paused while assigning a key. Finish or cancel key capture to resume."));
            ImGui::PopStyleColor();
        } else {
            ImGui::TextWrapped("%s", KEVQ_TR("Opening this menu does not pause activation from the game PC. Use the activation key or disable Target to stop."));
        }

        ImGui::TextDisabled("%s", KEVQ_TR("Movement output"));
        ImGui::SameLine();
        ImGui::TextColored(
            inputReady ? readyColor : unavailableColor,
            "%s",
            KEVQ_TR(inputReady ? "Ready" : "Unavailable"));
        if (inputReady) {
            ImGui::TextColored(
                !aimKey.available ? unavailableColor :
                    (aimPressed ? readyColor : ImVec4(0.65f, 0.72f, 0.82f, 1.0f)),
                "%s",
                KEVQ_TR(!aimKey.available ? "Activation key unavailable" :
                    (aimPressed ? "Pressed" : "Released")));
        }
        if (input.physicalButtonsAvailable || input.physicalKeyboard.available) {
            ImGui::TextDisabled("%s", KEVQ_TR("Device input monitor"));
        } else if (primaryKeyboard.ready) {
            ImGui::TextDisabled("%s", KEVQ_TR("Primary keyboard"));
        }
        if (inputReady && !aimKey.available) {
            ImGui::TextWrapped("%s", KEVQ_TR("Movement output is connected, but activation input is unavailable. Check the device monitor, UDP firewall, or game-PC keyboard reader."));
        }
        if (inputReady && input.inputMonitorPort != 0)
            ImGui::TextDisabled("UDP: %u", static_cast<unsigned>(input.inputMonitorPort));

        const auto keyText = [](app::input::KeyState value) {
            return app::localization::Get(!value.available ? "Unavailable" :
                (value.down ? "Pressed" : "Released"));
        };
        const auto aimDevice = app::input::ReadDeviceActivationKeyState(input, g::targetAimKey);
        const auto triggerDevice = app::input::ReadDeviceActivationKeyState(input, g::targetTriggerKey);
        const auto aimPrimary = app::input::ReadPrimaryKeyState(g::targetAimKey);
        const auto triggerPrimary = app::input::ReadPrimaryKeyState(g::targetTriggerKey);
        const auto triggerKey = app::input::ReadActivationKeyState(g::targetTriggerKey);
        ImGui::TextWrapped(KEVQ_TR("%s key: %s | device: %s | game PC: %s"),
            KEVQ_TR("Aimbot"), keyText(aimKey), keyText(aimDevice), keyText(aimPrimary));
        ImGui::TextWrapped(KEVQ_TR("%s key: %s | device: %s | game PC: %s"),
            KEVQ_TR("Triggerbot"), keyText(triggerKey), keyText(triggerDevice), keyText(triggerPrimary));
        if (app::input::IsNetworkDeviceKind(input.selected)) {
            ImGui::TextWrapped(KEVQ_TR("Monitor: packets %llu | rejected %llu | last %u bytes | error %u"),
                static_cast<unsigned long long>(input.inputMonitorPackets),
                static_cast<unsigned long long>(input.inputMonitorRejectedPackets),
                input.inputMonitorLastPacketBytes, input.inputMonitorError);
        }
        ImGui::TextWrapped(KEVQ_TR("Movement: queued %llu | sent batches %llu"),
            static_cast<unsigned long long>(input.moveRequests),
            static_cast<unsigned long long>(input.moveCompletions));
        ImGui::TextWrapped(KEVQ_TR("Movement queue: replaced %llu | cancelled %llu | expired %llu"),
            static_cast<unsigned long long>(input.moveReplacements),
            static_cast<unsigned long long>(input.moveCancellations),
            static_cast<unsigned long long>(input.moveExpirations));
        if (ImGui::SmallButton(KEVQ_TR("Copy input diagnostics"))) {
            char text[4096] = {};
            const auto nowMs = GetTickCount64();
            const auto sampleAgeMs = input.physicalInputUpdatedAtMs != 0 &&
                nowMs >= input.physicalInputUpdatedAtMs
                ? static_cast<long long>(nowMs - input.physicalInputUpdatedAtMs) : -1LL;
            std::snprintf(text, sizeof(text),
                "build=%s version=%s device=%s state=%u error=%u system=%u endpoint=%s\n"
                "primary ready=%u pid=%u failures=%u samples=%llu resolving=%u attempts=%llu unreadable_pages=%llu\n"
                "aim key=%d raw=%u/%u device=%u/%u primary=%u/%u active=%u\n"
                "trigger key=%d raw=%u/%u device=%u/%u primary=%u/%u active=%u\n"
                "monitor port=%u packets=%llu rejected=%llu bytes=%u error=%u age_ms=%lld serial_rx_bytes=%llu\n"
                "moves queued=%llu batches=%llu phase=%u menu=%u target=%d fire=%s pause=%s\n"
                "motion pending=%u in_flight=%u probe=%u replaced=%llu cancelled=%llu expired=%llu queue_age_us=%llu\n"
                "aim_motion=%s view_age_us=%lld ttl_us=%llu trigger_motion=%s view_age_us=%lld ttl_us=%llu\n"
                "aim_eye=%u eye_age_us=%lld error_deg=%.4f trigger_eye=%u eye_age_us=%lld error_deg=%.4f delta=%d/%d\n"
                "geometry evaluated=%u ready=%u aim_visibility_deferred=%u trigger_visibility_deferred=%u aim_damage_deferred=%u aim_autowall=%u trigger_autowall=%u\n"
                "aim_recoil weapon=%u available=%u sample_age_us=%lld step_deg=%.4f/%.4f assist_resting=%u scale=%.3f source=predictable_base_angle\n"
                "aim_point=%.2f/%.2f/%.2f hitbox=%d lead_z=%.2f eye_z=%.2f shots=%d raw_punch=%.4f/%.4f\n"
                "trigger_recoil weapon=%u available=%u sample_age_us=%lld step_deg=%.4f/%.4f scale=%.3f source=predictable_base_angle\n"
                "trigger_point=%.2f/%.2f/%.2f hitbox=%d lead_z=%.2f eye_z=%.2f shots=%d raw_punch=%.4f/%.4f\n"
                "feedback pending=%u age_us=%lld expected_deg=%.4f/%.4f observed_deg=%.4f/%.4f timeouts=%llu\n"
                "target_work last_us=%llu peak_us=%llu samples=%llu over_budget=%llu budget_us=7812 window_us=%llu\n",
                app::build_info::DisplayStamp().c_str(), app::build_info::VersionTag().c_str(),
                app::input::DeviceKindLabel(input.selected), static_cast<unsigned>(input.state),
                static_cast<unsigned>(input.error), input.systemError, input.port.c_str(),
                primaryKeyboard.ready, primaryKeyboard.sourcePid, primaryKeyboard.consecutiveReadFailures,
                static_cast<unsigned long long>(primaryKeyboard.publishGeneration),
                primaryKeyboard.resolving, static_cast<unsigned long long>(primaryKeyboard.resolveAttempts),
                static_cast<unsigned long long>(primaryKeyboard.unreadablePages),
                g::targetAimKey, aimKey.available, aimKey.down, aimDevice.available,
                aimDevice.down, aimPrimary.available, aimPrimary.down, runtime.aimKeyDown,
                g::targetTriggerKey, triggerKey.available, triggerKey.down, triggerDevice.available,
                triggerDevice.down, triggerPrimary.available, triggerPrimary.down, runtime.triggerKeyDown,
                static_cast<unsigned>(input.inputMonitorPort),
                static_cast<unsigned long long>(input.inputMonitorPackets),
                static_cast<unsigned long long>(input.inputMonitorRejectedPackets),
                input.inputMonitorLastPacketBytes, input.inputMonitorError,
                sampleAgeMs,
                static_cast<unsigned long long>(input.inputMonitorReceivedBytes),
                static_cast<unsigned long long>(input.moveRequests),
                static_cast<unsigned long long>(input.moveCompletions),
                static_cast<unsigned>(runtime.phase), g::menuOpen.load(std::memory_order_relaxed),
                runtime.targetSlot, target::FireBlockReasonName(runtime.fire.reason),
                runtime.pausedByMenu ? "key_capture" : "none",
                input.movePending, input.moveInFlight, input.probeInFlight,
                static_cast<unsigned long long>(input.moveReplacements),
                static_cast<unsigned long long>(input.moveCancellations),
                static_cast<unsigned long long>(input.moveExpirations),
                static_cast<unsigned long long>(input.lastMoveQueueAgeUs),
                target::MoveBlockReasonName(runtime.aimMove.reason),
                static_cast<long long>(runtime.aimMove.viewAgeUs),
                static_cast<unsigned long long>(runtime.aimMove.validForUs),
                target::MoveBlockReasonName(runtime.triggerMove.reason),
                static_cast<long long>(runtime.triggerMove.viewAgeUs),
                static_cast<unsigned long long>(runtime.triggerMove.validForUs),
                runtime.aimMove.eyeValid, static_cast<long long>(runtime.aimMove.eyeAgeUs),
                runtime.aimMove.angularErrorDegrees, runtime.triggerMove.eyeValid,
                static_cast<long long>(runtime.triggerMove.eyeAgeUs), runtime.triggerMove.angularErrorDegrees,
                runtime.moveX, runtime.moveY,
                runtime.geometry.evaluated, runtime.geometry.ready, runtime.geometry.aimVisibilityDeferred,
                runtime.geometry.triggerVisibilityDeferred, runtime.geometry.aimDamageDeferred,
                runtime.geometry.aimAutowallActive, runtime.geometry.triggerAutowallActive,
                static_cast<unsigned>(runtime.aimMove.weaponId), runtime.aimMove.recoilAvailable,
                static_cast<long long>(runtime.aimMove.recoilAgeUs), runtime.aimMove.recoilPitchStep,
                runtime.aimMove.recoilYawStep, runtime.aimMove.assistanceResting, runtime.aimMove.recoilScale,
                runtime.aimMove.pointX, runtime.aimMove.pointY, runtime.aimMove.pointZ,
                runtime.aimMove.pointHitbox, runtime.aimMove.predictionZ, runtime.aimMove.eyeZ,
                runtime.aimMove.shotsFired, runtime.aimMove.rawPunchPitch, runtime.aimMove.rawPunchYaw,
                static_cast<unsigned>(runtime.triggerMove.weaponId), runtime.triggerMove.recoilAvailable,
                static_cast<long long>(runtime.triggerMove.recoilAgeUs), runtime.triggerMove.recoilPitchStep,
                runtime.triggerMove.recoilYawStep, runtime.triggerMove.recoilScale,
                runtime.triggerMove.pointX, runtime.triggerMove.pointY, runtime.triggerMove.pointZ,
                runtime.triggerMove.pointHitbox, runtime.triggerMove.predictionZ, runtime.triggerMove.eyeZ,
                runtime.triggerMove.shotsFired, runtime.triggerMove.rawPunchPitch, runtime.triggerMove.rawPunchYaw,
                runtime.outputFeedback.pending, static_cast<long long>(runtime.outputFeedback.ageUs),
                runtime.outputFeedback.expectedPitch, runtime.outputFeedback.expectedYaw,
                runtime.outputFeedback.observedPitch, runtime.outputFeedback.observedYaw,
                static_cast<unsigned long long>(runtime.outputFeedback.timeouts),
                static_cast<unsigned long long>(runtime.workUs),
                static_cast<unsigned long long>(runtime.recentPeakWorkUs),
                static_cast<unsigned long long>(runtime.workSamples),
                static_cast<unsigned long long>(runtime.overBudgetSamples),
                static_cast<unsigned long long>(runtime.workWindowUs));
            ImGui::SetClipboardText(text);
        }

        ImGui::Spacing();
        ImGui::TextDisabled("%s", KEVQ_TR("World Geometry"));
        ImGui::SameLine();
        const char* geometryText = "Waiting for map";
        ImVec4 geometryColor = waitingColor;
        switch (geometry.state) {
        case BuildState::Queued: geometryText = "Queued"; break;
        case BuildState::Building: geometryText = "Building..."; break;
        case BuildState::Ready:
            geometryText = "Ready";
            geometryColor = readyColor;
            break;
        case BuildState::Failed:
            geometryText = "Failed";
            geometryColor = unavailableColor;
            break;
        default:
            break;
        }
        if (runtime.geometry.evaluated && !runtime.geometry.ready && geometry.state == BuildState::Ready) {
            geometryText = "Waiting for current map";
            geometryColor = waitingColor;
        }
        ImGui::TextColored(geometryColor, "%s", KEVQ_TR(geometryText));
        if (geometry.state == BuildState::Ready && runtime.geometry.ready) {
            ImGui::SameLine();
            ImGui::TextDisabled(
                "| %zu %s | %.0f ms",
                geometry.triangles,
                KEVQ_TR("triangles"),
                static_cast<double>(geometry.buildTimeUs) / 1000.0);
        }

        if (runtime.geometry.evaluated && !runtime.geometry.ready) {
            ImGui::TextWrapped("%s", KEVQ_TR("Basic aim and pull remain available. Wall tracing and penetration resume automatically when geometry is ready."));
            if (runtime.geometry.aimVisibilityDeferred || runtime.geometry.triggerVisibilityDeferred)
                ImGui::TextWrapped("%s", KEVQ_TR("Visible Only is temporarily suspended for movement; aiming through walls is possible. Automatic fire still requires confirmed visibility."));
            if (runtime.geometry.aimDamageDeferred)
                ImGui::TextWrapped("%s", KEVQ_TR("Aimbot damage filter is waiting for geometry. Saved settings are unchanged."));
        }

        if (runtime.phase == target::RuntimePhase::DataUnavailable && runtime.snapshotAgeUs >= 0) {
            ImGui::TextDisabled("snapshot %.1f ms | view %.1f ms | eye %.1f ms",
                runtime.snapshotAgeUs / 1000.0, runtime.viewAgeUs / 1000.0, runtime.eyeAgeUs / 1000.0);
        }
        ImGui::TextDisabled("%s: %s | %s: %s", KEVQ_TR("Aim assistance"),
            KEVQ_TR(runtime.aimKeyDown ? "Active" : "Inactive"), KEVQ_TR("Triggerbot"),
            KEVQ_TR(runtime.triggerKeyDown ? "Active" : "Inactive"));
        const auto showSelection = [](const char* label, const target::SelectionDiagnostics& checks) {
            if (checks.enemies == 0) return;
            ImGui::TextWrapped(KEVQ_TR("%s checks: enemies %d | stale %d | FOV %d | visibility %d | missing data %d | damage %d"),
                app::localization::Get(label), checks.enemies, checks.stale, checks.outsideFov,
                checks.visibilityRejected, checks.missingBallistics, checks.damageRejected);
        };
        showSelection("Aimbot", runtime.aimSelection);
        showSelection("Triggerbot", runtime.triggerSelection);
        if (runtime.fire.reason != target::FireBlockReason::Inactive) {
            ImGui::TextWrapped(KEVQ_TR("Fire gate: %s"),
                target::FireBlockReasonName(runtime.fire.reason));
            ImGui::TextWrapped(KEVQ_TR("Hitchance check: %s | Seed forecast check: %s"),
                KEVQ_TR(runtime.fire.hitchanceEnabled ? "On" : "Off"),
                KEVQ_TR(runtime.fire.seedWindowEnabled ? "On" : "Off"));
            if (runtime.fire.hitchancePercent >= 0.0f)
                ImGui::TextWrapped(KEVQ_TR("Hitchance: %.1f / %.1f%% | Inaccuracy: %.6f | Spread: %.6f"),
                    runtime.fire.hitchancePercent, runtime.fire.requiredHitchancePercent,
                    runtime.fire.inaccuracy, runtime.fire.spread);
            if (runtime.fire.centeredHitchancePercent >= 0.0f)
                ImGui::TextWrapped(KEVQ_TR("At selected point center: %.1f%% (threshold is not reduced)"),
                    runtime.fire.centeredHitchancePercent);
            if (runtime.fire.damage >= 0.0f)
                ImGui::TextWrapped(KEVQ_TR("Damage: %.1f / %.1f"),
                    runtime.fire.damage, runtime.fire.requiredDamage);
            else
                ImGui::TextDisabled("%s", KEVQ_TR("Damage: not evaluated"));
        }
        if (runtime.shot.targetSlot >= 0) {
            ImGui::TextWrapped(KEVQ_TR("Observed HP: %d -> %d | Weapon shot confirmed: %s"),
                runtime.shot.healthBefore, runtime.shot.healthAfter,
                KEVQ_TR(runtime.shot.weaponConfirmed ? "Yes" : "No"));
        }
        if (!runtime.pausedByMenu &&
            runtime.phase == target::RuntimePhase::Tracking &&
            runtime.targetSlot >= 0) {
            ImGui::TextDisabled(
                "| #%d | %.0f px | %d, %d",
                runtime.targetSlot,
                runtime.targetDistancePx,
                runtime.moveX,
                runtime.moveY);
        }
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
    }

    void RenderRuntimeStatus()
    {
        const auto runtime = target::GetRuntimeStatus();
        const ImVec4 waitingColor(0.9f, 0.73f, 0.35f, 1.0f);
        const ImVec4 readyColor(0.45f, 0.85f, 0.6f, 1.0f);
        const ImVec4 unavailableColor(0.95f, 0.45f, 0.4f, 1.0f);
        const char* runtimeText = "Ready";
        ImVec4 runtimeColor = ImVec4(0.65f, 0.72f, 0.82f, 1.0f);
        if (runtime.pausedByMenu) {
            runtimeText = "Paused while assigning a key";
            runtimeColor = waitingColor;
        } else {
            switch (runtime.phase) {
            case target::RuntimePhase::Disabled:
                runtimeText = "Target disabled";
                break;
            case target::RuntimePhase::InputUnavailable:
                runtimeText = "Input unavailable";
                runtimeColor = unavailableColor;
                break;
            case target::RuntimePhase::DataUnavailable:
                runtimeText = "Game data unavailable";
                runtimeColor = unavailableColor;
                break;
            case target::RuntimePhase::WaitingForKey:
                runtimeText = "Waiting for activation";
                break;
            case target::RuntimePhase::NoTarget:
                runtimeText = "No eligible target";
                runtimeColor = waitingColor;
                break;
            case target::RuntimePhase::Tracking:
                runtimeText = "Tracking";
                runtimeColor = readyColor;
                break;
            case target::RuntimePhase::Reacting:
                runtimeText = "Reaction delay";
                runtimeColor = waitingColor;
                break;
            case target::RuntimePhase::OutputFailed:
                runtimeText = "Mouse output failed";
                runtimeColor = unavailableColor;
                break;
            default:
                break;
            }
        }
        ImGui::TextDisabled("%s", KEVQ_TR("Target State"));
        ImGui::SameLine();
        ImGui::TextColored(runtimeColor, "%s", KEVQ_TR(runtimeText));
        ImGui::TextDisabled("%s: %s | %s: %s", KEVQ_TR("Aimbot"),
            KEVQ_TR(runtime.aimKeyDown ? "Active" : "Inactive"), KEVQ_TR("Triggerbot"),
            KEVQ_TR(runtime.triggerKeyDown ? "Active" : "Inactive"));
#if defined(KEVQ_UI_SMOKE_TESTS)
        ImGui::SetNextItemOpen(s_testDiagnosticsOpen, ImGuiCond_Always);
#endif
        if (ImGui::CollapsingHeader(KEVQ_TR("Debug")))
            RenderRuntimeDiagnostics();
    }

    template <typename Body>
    void RenderSettingsWindow(
        const char* id,
        const char* title,
        Body&& body,
        bool compact = false)
    {
        if (s_activeSettings != id)
            return;
        char windowTitle[128] = {};
        std::snprintf(
            windowTitle,
            sizeof(windowTitle),
            KEVQ_TR("%s Settings###%s_settings_win"),
            app::localization::Get(title),
            id);
        const ImVec2 available = ImGui::GetMainViewport()->WorkSize;
        const ImVec2 workPos = ImGui::GetMainViewport()->WorkPos;
        ImGui::SetNextWindowPos(ImVec2(workPos.x + available.x * 0.5f,
            workPos.y + available.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        const float maxWidth = std::max(320.0f, available.x - 24.0f);
        if (!compact)
            ImGui::SetNextWindowSize(ImVec2(std::min(920.0f, maxWidth), std::min(480.0f, std::max(120.0f, available.y - 24.0f))), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(1.0f);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(std::min(compact ? 390.0f : 360.0f, maxWidth), 120.0f),
            ImVec2(std::min(compact ? 560.0f : 1040.0f, maxWidth),
                std::max(120.0f, std::min(compact ? 720.0f : 680.0f, available.y - 24.0f))));
        bool open = true;
        if (ImGui::Begin(
                windowTitle,
                &open,
                ImGuiWindowFlags_NoCollapse | (compact ? ImGuiWindowFlags_AlwaysAutoResize : ImGuiWindowFlags_HorizontalScrollbar))) {
            body();
        }
        ImGui::End();
        if (!open) {
            s_activeSettings.clear();
            s_keyCaptureId.clear();
            g::targetKeyCaptureUntilMs.store(0, std::memory_order_release);
        }
    }

    template <typename LeftBody, typename RightBody>
    void RenderSettingsColumns(
        const char* id,
        LeftBody&& leftBody,
        RightBody&& rightBody)
    {
        if (ImGui::GetContentRegionAvail().x < 700.0f) {
            ImGui::PushID(id);
            ImGui::PushID("left");
            leftBody();
            ImGui::PopID();
            ImGui::PushID("right");
            rightBody();
            ImGui::PopID();
            ImGui::PopID();
            return;
        }
        const ImGuiTableFlags flags =
            ImGuiTableFlags_SizingStretchSame |
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_PadOuterX;
        if (!ImGui::BeginTable(id, 2, flags))
            return;
        ImGui::TableSetupColumn("##left", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##right", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::PushID("left");
        leftBody();
        ImGui::PopID();
        ImGui::TableSetColumnIndex(1);
        ImGui::PushID("right");
        rightBody();
        ImGui::PopID();
        ImGui::EndTable();
    }
}

#if defined(KEVQ_UI_SMOKE_TESTS)
namespace ui::tabs::testing {
    void OpenTargetDiagnostics(bool open) { s_testDiagnosticsOpen = open; }
    void OpenTargetTuning(bool open) { s_testTuningOpen = open; }
    void OpenTargetPage(int page) { s_testSettingsPage = page; }
    void FollowTargetWeapon(bool follow) { s_followWeaponProfile = follow; }
    void OpenTargetSettings(const char* id) { s_activeSettings = id; }
    bool TargetTooltipRow(const char* id, ImVec4& rectangle, bool& disabled)
    {
        for (const auto& row : s_testTooltipRows) {
            if (row.id == id) {
                rectangle = row.rectangle;
                disabled = row.disabled;
                return true;
            }
        }
        return false;
    }
}
#endif

const char* ui::tabs::TargetTab::Label() const
{
    return "Target";
}

void ui::tabs::TargetTab::Render(MenuState& state, IStatusSink& statusSink)
{
    (void)state;
    (void)statusSink;
#if defined(KEVQ_UI_SMOKE_TESTS)
    s_testTooltipRows.clear();
#endif
    if (GetTickCount64() >= g::targetKeyCaptureUntilMs.load(std::memory_order_acquire))
        s_keyCaptureId.clear();

    if (!ImGui::BeginChild(
        "##target_child",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_Borders)) {
        ImGui::EndChild();
        return;
    }

    const float width = GridColumnWidth();
    if (!g::targetEnabled) {
        DrawFeatureRow(
            "target_enabled",
            "Enable Target",
            ui::icons::Icon::Crosshair,
            &g::targetEnabled,
            false,
            width);
        s_activeSettings.clear();
        s_keyCaptureId.clear();
        g::targetKeyCaptureUntilMs.store(0, std::memory_order_release);
        ImGui::EndChild();
        return;
    }

    RenderGridPair(
        width,
        [](float rowWidth) {
            DrawFeatureRow(
                "target_enabled",
                "Enable Target",
                ui::icons::Icon::Crosshair,
                &g::targetEnabled,
                false,
                rowWidth);
        },
        [](float rowWidth) {
            DrawFeatureRow(
                "target_fov",
                "FOV circle",
                ui::icons::Icon::Radar,
                &g::targetFovEnabled,
                true,
                rowWidth);
        });
    RenderGridPair(
        width,
        [](float rowWidth) {
            DrawFeatureRow(
                "target_aimbot",
                "Aim assistance",
                ui::icons::Icon::Crosshair,
                &g::targetAimbotEnabled,
                true,
                rowWidth);
        },
        [](float rowWidth) {
            DrawFeatureRow(
                "target_triggerbot",
                "Triggerbot",
                ui::icons::Icon::Stopwatch,
                &g::targetTriggerbotEnabled,
                true,
                rowWidth);
        });
    RenderRuntimeStatus();
    ImGui::Dummy(ImVec2(0.0f, 1.0f));
    ImGui::EndChild();

    RenderSettingsWindow("target_fov", "FOV", [] {
        ui::widgets::ToggleRow("fov_per_weapon", "Weapon-specific FOV", &g::targetFovPerWeapon);
        SetControlRowTooltip("fov_per_weapon", "Radius always limits aim selection. The FOV circle switch only changes drawing; Target lock cannot extend the radius.");
        if (g::targetFovPerWeapon) {
            RenderWeaponProfileRow("fov_weapon_profile");
            ImGui::BeginDisabled(s_followWeaponProfile && !s_weaponProfileAvailable);
            ui::widgets::SliderFloatRow("profile_fov_radius", "FOV Radius",
                &g::targetWeaponProfiles[s_weaponProfileCategory].fovRadius,
                target::policy::kMinimumFovRadius, target::policy::kMaximumFovRadius, "%.0f px");
            ImGui::EndDisabled();
        } else {
            ui::widgets::SliderFloatRow("global_fov_radius", "FOV Radius", &g::targetFovRadius,
                target::policy::kMinimumFovRadius, target::policy::kMaximumFovRadius, "%.0f px");
        }
        ui::widgets::ColorRow(
            "target_fov_color",
            "FOV Color",
            g::targetFovColor,
            kColorFlags);
    }, true);

    RenderSettingsWindow("target_aimbot", "Aim assistance", [] {
        RenderKeyRow("aim_key", "Activation Key", &g::targetAimKey);
        RenderSettingsColumns("aim_top_columns",
            [&] { RenderActivationModeRow("aim_activation_mode", &g::targetAimActivationMode); },
            [&] { RenderWeaponProfileRow("aim_weapon_profile"); });
        static constexpr const char* pages[] = {"Aim", "Motion", "Filters"};
        RenderSettingsPages("aim_pages", s_aimPage, pages, 3);
        auto& profile = g::targetWeaponProfiles[s_weaponProfileCategory];
        RenderSettingsColumns("aim_settings_columns", [&] {
            if (s_aimPage == 0) {
                RenderAimPointRow("aim_point", &g::targetAimBone);
                SetControlRowTooltip("aim_point", "Auto selects the nearest eligible head or torso point inside FOV and retains it through small changes. Closest also considers limbs.");
                ui::widgets::ToggleRow("aim_target_lock", "Target lock", &g::targetAimTargetLock);
                RenderProfileControls([&] {
                    ui::widgets::SliderIntRow("aim_reaction", "Reaction time", &profile.aimReactionMs, 0, 500, "%d ms");
                    SetControlRowTooltip("aim_reaction", "Wait after acquiring a new target. Resets when the target is lost; never delays a manual shot.");
                });
            } else if (s_aimPage == 1) {
                RenderProfileControls([&] {
                    int style = target::settings_policy::AimMotionStyle(g::targetSettings, profile);
                    static constexpr const char* styles[] = {"Smooth", "Curved", "WindMouse"};
                    if (RenderChoiceRow("aim_motion_style", "Motion style", style, styles, 3)) {
                        profile.aimMotionStyle = style;
                        profile.aimWindMouse = style == 2;
                    }
                    ui::widgets::SliderFloatRow("aim_smoothing", "Smoothing", &profile.aimSmoothing,
                        target::policy::kMinimumSmoothing, target::policy::kMaximumSmoothing, "%.1f");
                    ui::widgets::ToggleRow("aim_adaptive", "Adaptive smoothing", &profile.aimAdaptiveSmoothing);
                    if (style == 2 && TuningHeader("Wind tuning")) {
                        ui::widgets::SliderFloatRow("wind_gravity", "Wind gravity", &profile.aimWindGravity, 4.0f, 40.0f, "%.1f");
                        ui::widgets::SliderFloatRow("wind_force", "Wind strength", &profile.aimWindFluctuation, 0.0f, 40.0f, "%.1f");
                        ui::widgets::SliderFloatRow("wind_step", "Wind max step", &profile.aimWindMaxStep, 1.0f, 40.0f, "%.1f");
                        ui::widgets::SliderFloatRow("wind_distance", "Wind fade distance", &profile.aimWindDistance, 1.0f, 40.0f, "%.1f");
                    }
                });
            } else {
                ui::widgets::ToggleRow("aim_visible", "Visible Only", &g::targetAimVisibleOnly);
                SetControlRowTooltip("aim_visible", "Movement visibility filtering resumes with World Geometry. Until then, aim/pull can move through walls; automatic fire keeps its visibility checks.");
                ui::widgets::ToggleRow("aim_predictive", "Predictive", &g::targetAimPredictive);
                SetControlRowTooltip("aim_predictive", "Bounded horizontal lead only. Never adds height above the sampled model.");
            }
        }, [&] {
            if (s_aimPage == 0) {
                RenderProfileControls([&] {
                    int mode = profile.aimSoftAssist ? 0 : 1;
                    static constexpr const char* modes[] = {"Soft", "Precise"};
                    if (RenderChoiceRow("aim_assistance", "Assistance mode", mode, modes, 2))
                        profile.aimSoftAssist = mode == 0;
                    if (profile.aimSoftAssist) {
                        ui::widgets::SliderFloatRow("aim_assist_strength", "Assistance strength", &profile.aimAssistStrength, 0.0f, 100.0f, "%.0f%%");
                        if (TuningHeader("Assistance tuning")) {
                            ui::widgets::SliderFloatRow("aim_assist_speed", "Assistance speed", &profile.aimAssistMaxSpeed, 1.0f, 180.0f, "%.0f deg/s");
                            if (target::policy::SanitizeAimBone(g::targetAimBone) >= 4)
                                ui::widgets::SliderFloatRow("aim_assist_deadzone", "Resting radius", &profile.aimAssistDeadzone, 0.0f, 1.0f, "%.2f deg");
                        }
                    }
                });
            } else if (s_aimPage == 1) {
                ui::widgets::ToggleRow("aim_recoil_control", "Recoil Control", &g::targetAimRecoilControl);
                if (g::targetAimRecoilControl) {
                    RenderProfileControls([&] {
                        ui::widgets::SliderFloatRow("aim_recoil_strength", "Recoil strength", &profile.aimRecoilStrength, 0.0f, 100.0f, "%.0f%%");
                        SetControlRowTooltip("aim_recoil_strength", "Measured recoil compensation is separate from smoothing and WindMouse. No random variation is added to recoil correction.");
                    });
                }
            } else {
                RenderProfileControls([&] {
                    ui::widgets::ToggleRow("aim_damage_check", "Damage check", &profile.aimDamageCheck);
                    SetControlRowTooltip("aim_damage_check", "Optional Aimbot damage and penetration filter. Automatically suspended without World Geometry and restored when ready. Visible Only remains independent.");
                    if (profile.aimDamageCheck) {
                        ui::widgets::SliderFloatRow("aim_minimum_damage", "Minimum Damage", &profile.aimMinimumDamage, 1.0f, 200.0f, "%.0f");
                        ui::widgets::ToggleRow("aim_autowall", "Autowall", &profile.aimAutowall);
                    }
                });
            }
        });
    });

    RenderSettingsWindow("target_triggerbot", "Triggerbot", [] {
        RenderKeyRow("trigger_key", "Activation Key", &g::targetTriggerKey);
        RenderSettingsColumns("trigger_top_columns",
            [&] { RenderActivationModeRow("trigger_activation_mode", &g::targetTriggerActivationMode); },
            [&] { RenderWeaponProfileRow("trigger_weapon_profile"); });
        static constexpr const char* pages[] = {"Fire", "Aim Assist", "Filters"};
        RenderSettingsPages("trigger_pages", s_triggerPage, pages, 3);
        auto& profile = g::targetWeaponProfiles[s_weaponProfileCategory];
        RenderSettingsColumns("trigger_settings_columns", [&] {
            if (s_triggerPage == 0) {
                RenderAimPointRow("trigger_aim_point", &g::targetTriggerAimBone);
                ui::widgets::ToggleRow("trigger_auto_shot", "Repeat shots", &g::targetTriggerAutoShot);
                SetControlRowTooltip("trigger_auto_shot", "Activation key is required. Disable Repeat shots for one shot per activation.");
                ui::widgets::SliderIntRow("trigger_delay", "Capture delay", &g::targetTriggerDelayMs, 0, 500, "%d ms");
            } else if (s_triggerPage == 1) {
                ui::widgets::ToggleRow("trigger_aim_assist", "Aim Assist", &g::targetTriggerAimAssist);
                if (g::targetTriggerAimAssist) {
                    ui::widgets::ToggleRow("trigger_target_lock", "Target lock", &g::targetTriggerTargetLock);
                    ui::widgets::ToggleRow("trigger_aim_recoil_control", "Recoil Control", &g::targetTriggerAimRecoilControl);
                }
            } else {
                RenderProfileControls([&] {
                    ui::widgets::SliderFloatRow("trigger_minimum_damage", "Minimum Damage", &profile.minimumDamage, 1.0f, 200.0f, "%.0f");
                    ui::widgets::ToggleRow("trigger_autowall", "Autowall", &profile.autowall);
                });
                ui::widgets::ToggleRow("trigger_visible", "Visible Only", &g::targetTriggerVisibleOnly);
            }
        }, [&] {
            if (s_triggerPage == 0) {
                RenderProfileControls([&] {
                    ui::widgets::ToggleRow("trigger_hitchance_enabled", "Hitchance check", &profile.hitchanceEnabled);
                    if (profile.hitchanceEnabled)
                        ui::widgets::SliderFloatRow("trigger_hitchance", "Hitchance", &profile.hitchance, 1.0f, 100.0f, "%.0f%%");
                    ui::widgets::ToggleRow("trigger_force_center", "Force center", &profile.triggerForceCenter);
                    SetControlRowTooltip("trigger_force_center", "Require the firing ray to cross the inner capsule, not just its outer edge. Does not replace hitchance or visibility checks.");
                });
            } else if (s_triggerPage == 1 && g::targetTriggerAimAssist) {
                RenderProfileControls([&] {
                    ui::widgets::SliderFloatRow("trigger_aim_smoothing", "Smoothing", &profile.triggerSmoothing,
                        target::policy::kMinimumSmoothing, target::policy::kMaximumSmoothing, "%.1f");
                    ui::widgets::ToggleRow("trigger_adaptive", "Adaptive smoothing", &profile.triggerAdaptiveSmoothing);
                });
                int style = g::targetTriggerAimHumanization ? 1 : 0;
                static constexpr const char* styles[] = {"Smooth", "Curved"};
                if (RenderChoiceRow("trigger_motion_style", "Motion style", style, styles, 2))
                    g::targetTriggerAimHumanization = style == 1;
                ui::widgets::ToggleRow("trigger_aim_predictive", "Predictive", &g::targetTriggerAimPredictive);
            } else if (s_triggerPage == 2) {
                RenderProfileControls([&] {
                    ui::widgets::ToggleRow("trigger_seed_window_enabled", "Seed forecast check", &profile.seedWindowEnabled);
                    SetControlRowTooltip("trigger_seed_window_enabled", "Uses a tick estimated from game time, not the actual shot command seed. This forecast can delay a shot even when Hitchance is off.");
                });
            }
        });
    });
}
