#include "Features/Target/UI/target_tab.h"

#include "Features/Target/physics_bvh.h"
#include "Features/Target/target.h"
#include "Features/Target/target_policy.h"
#include "app/Core/globals.h"
#include "app/Input/input_device.h"
#include "app/Input/primary_keyboard.h"
#include "app/Localization/localization.h"
#include "app/UI/MenuShell/menu_utils.h"
#include "app/UI/MenuShell/ui_icons.h"
#include "app/UI/MenuShell/ui_widgets.h"

#include <imgui.h>
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

    float GridColumnWidth()
    {
        const float available = std::max(520.0f, ImGui::GetContentRegionAvail().x);
        return std::floor((available - kColumnGap) * 0.5f);
    }

    template <typename LeftFn, typename RightFn>
    void RenderGridPair(float width, LeftFn&& left, RightFn&& right)
    {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        left(width);
        ImGui::SetCursorScreenPos(ImVec2(start.x + width + kColumnGap, start.y));
        right(width);
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + kRowHeight + kRowGap));
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
            for (int vk = 0; vk < 256; ++vk) {
                s_keySnapshot[vk] =
                    app::input::IsControlKeyDown(vk) ? 1u : 0u;
            }
        }
        if (capturing) {
            for (int vk = 1; vk <= 0xFE; ++vk) {
                const bool down = app::input::IsControlKeyDown(vk);
                const bool wasDown = s_keySnapshot[vk] != 0;
                s_keySnapshot[vk] = down ? 1u : 0u;
                if (!down || wasDown)
                    continue;
                if (vk != VK_ESCAPE)
                    *key = vk;
                s_keyCaptureId.clear();
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
        s_weaponProfileCategory = std::clamp(s_weaponProfileCategory, 0, 5);
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
                KEVQ_TR(kProfiles[s_weaponProfileCategory]))) {
            for (int index = 0; index < 6; ++index) {
                const bool selected = index == s_weaponProfileCategory;
                if (ImGui::Selectable(KEVQ_TR(kProfiles[index]), selected))
                    s_weaponProfileCategory = index;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ui::widgets::EndControlRow(row);
    }

    void RenderRuntimeStatus()
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
        const bool aimPressed = inputReady &&
            app::input::IsActivationKeyDown(g::targetAimKey);

        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        const ImVec4 readyColor(0.28f, 0.85f, 0.42f, 1.0f);
        const ImVec4 waitingColor(0.95f, 0.72f, 0.25f, 1.0f);
        const ImVec4 unavailableColor(0.96f, 0.36f, 0.31f, 1.0f);

        ImGui::TextDisabled("%s", KEVQ_TR("Hardware Input"));
        ImGui::SameLine();
        ImGui::TextColored(
            inputReady ? readyColor : unavailableColor,
            "%s",
            KEVQ_TR(inputReady ? "Ready" : "Unavailable"));
        if (inputReady) {
            ImGui::SameLine();
            ImGui::TextDisabled("|");
            ImGui::SameLine();
            ImGui::TextColored(
                aimPressed ? readyColor : ImVec4(0.65f, 0.72f, 0.82f, 1.0f),
                "%s",
                KEVQ_TR(aimPressed ? "Pressed" : "Released"));
        }
        if (primaryKeyboard.ready) {
            ImGui::SameLine();
            ImGui::TextDisabled("| %s", KEVQ_TR("Primary keyboard"));
        }

        ImGui::SameLine(0.0f, 24.0f);
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
        ImGui::TextColored(geometryColor, "%s", KEVQ_TR(geometryText));
        if (geometry.state == BuildState::Ready) {
            ImGui::SameLine();
            ImGui::TextDisabled(
                "| %zu %s | %.0f ms",
                geometry.triangles,
                KEVQ_TR("triangles"),
                static_cast<double>(geometry.buildTimeUs) / 1000.0);
        }

        const char* runtimeText = "Ready";
        ImVec4 runtimeColor = ImVec4(0.65f, 0.72f, 0.82f, 1.0f);
        if (runtime.pausedByMenu) {
            runtimeText = "Paused while menu is open";
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
        if (runtime.phase == target::RuntimePhase::DataUnavailable && runtime.snapshotAgeUs >= 0) {
            ImGui::TextDisabled("snapshot %.1f ms | view %.1f ms | eye %.1f ms",
                runtime.snapshotAgeUs / 1000.0, runtime.viewAgeUs / 1000.0, runtime.eyeAgeUs / 1000.0);
        }
        ImGui::TextDisabled("%s: %s | %s: %s", KEVQ_TR("Aimbot"),
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
            ImGui::TextWrapped(KEVQ_TR("Hitchance check: %s | Spread-seed check: %s"),
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
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(std::min(compact ? 390.0f : 860.0f, maxWidth), 0.0f),
            ImVec2(std::min(compact ? 560.0f : 1040.0f, maxWidth),
                std::max(120.0f, std::min(compact ? 720.0f : 680.0f, available.y - 24.0f))));
        bool open = true;
        if (ImGui::Begin(
                windowTitle,
                &open,
                ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
            body();
        }
        ImGui::End();
        if (!open) {
            s_activeSettings.clear();
            s_keyCaptureId.clear();
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
    void OpenTargetSettings(const char* id) { s_activeSettings = id; }
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

    ImGui::BeginChild(
        "##target_child",
        ImVec2(0.0f, 0.0f),
        ImGuiChildFlags_Borders);

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
                "FOV",
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
                "Aimbot",
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
    ImGui::Dummy(ImVec2(width * 2.0f + kColumnGap, 1.0f));
    ImGui::EndChild();

    RenderSettingsWindow("target_fov", "FOV", [] {
        ui::widgets::ToggleRow("fov_per_weapon", "Weapon-specific FOV", &g::targetFovPerWeapon);
        if (g::targetFovPerWeapon) {
            RenderWeaponProfileRow("fov_weapon_profile");
            ui::widgets::SliderFloatRow("profile_fov_radius", "FOV Radius",
                &g::targetWeaponProfiles[s_weaponProfileCategory].fovRadius,
                target::policy::kMinimumFovRadius, target::policy::kMaximumFovRadius, "%.0f px");
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

    RenderSettingsWindow("target_aimbot", "Aimbot", [] {
        // Activation is intentionally the first control in every Target
        // settings window so the primary action is never buried below tuning.
        RenderKeyRow("aim_key", "Activation Key", &g::targetAimKey);
        RenderSettingsColumns(
            "aim_top_columns",
            [&] {
                RenderActivationModeRow(
                    "aim_activation_mode",
                    &g::targetAimActivationMode);
            },
            [&] { RenderWeaponProfileRow("aim_weapon_profile"); });
        auto& profile = g::targetWeaponProfiles[s_weaponProfileCategory];
        ImGui::Separator();
        RenderSettingsColumns(
            "aim_settings_columns",
            [&] {
                RenderAimPointRow("aim_point", &g::targetAimBone);
                ui::widgets::ToggleRow(
                    "aim_visible",
                    "Visible Only",
                    &g::targetAimVisibleOnly);
                ui::widgets::ToggleRow(
                    "aim_predictive",
                    "Predictive",
                    &g::targetAimPredictive);
            },
            [&] {
                ui::widgets::SliderFloatRow(
                    "aim_smoothing",
                    "Smoothing",
                    &profile.aimSmoothing,
                    target::policy::kMinimumSmoothing,
                    target::policy::kMaximumSmoothing,
                    "%.1f");
                ui::widgets::SliderFloatRow(
                    "aim_minimum_damage",
                    "Minimum Damage",
                    &profile.aimMinimumDamage,
                    1.0f,
                    200.0f,
                    "%.0f");
                ui::widgets::ToggleRow(
                    "aim_autowall",
                    "Autowall",
                    &profile.aimAutowall);
                ui::widgets::ToggleRow(
                    "aim_recoil_control",
                    "Recoil Control",
                    &g::targetAimRecoilControl);
                ui::widgets::ToggleRow(
                    "aim_humanization",
                    "Humanization",
                    &g::targetAimHumanization);
            });
    });

    RenderSettingsWindow("target_triggerbot", "Triggerbot", [] {
        RenderKeyRow(
            "trigger_key",
            "Activation Key",
            &g::targetTriggerKey);
        RenderSettingsColumns(
            "trigger_top_columns",
            [&] {
                RenderActivationModeRow(
                    "trigger_activation_mode",
                    &g::targetTriggerActivationMode);
            },
            [&] { RenderWeaponProfileRow("trigger_weapon_profile"); });
        auto& profile = g::targetWeaponProfiles[s_weaponProfileCategory];
        ImGui::Separator();
        RenderSettingsColumns(
            "trigger_settings_columns",
            [&] {
                ui::widgets::ToggleRow(
                    "trigger_aim_assist",
                    "Aim Assist",
                    &g::targetTriggerAimAssist);
                RenderAimPointRow(
                    "trigger_aim_point",
                    &g::targetTriggerAimBone);
                ImGui::BeginDisabled(!g::targetTriggerAimAssist);
                ui::widgets::SliderFloatRow(
                    "trigger_aim_smoothing",
                    "Smoothing",
                    &profile.triggerSmoothing,
                    target::policy::kMinimumSmoothing,
                    target::policy::kMaximumSmoothing,
                    "%.1f");
                ui::widgets::ToggleRow(
                    "trigger_aim_predictive",
                    "Predictive",
                    &g::targetTriggerAimPredictive);
                ui::widgets::ToggleRow(
                    "trigger_aim_recoil_control",
                    "Recoil Control",
                    &g::targetTriggerAimRecoilControl);
                ui::widgets::ToggleRow(
                    "trigger_aim_humanization",
                    "Humanization",
                    &g::targetTriggerAimHumanization);
                ImGui::EndDisabled();
            },
            [&] {
                ui::widgets::ToggleRow("trigger_hitchance_enabled", "Hitchance check",
                    &profile.hitchanceEnabled);
                if (profile.hitchanceEnabled) {
                    ui::widgets::SliderFloatRow(
                        "trigger_hitchance", "Hitchance", &profile.hitchance,
                        1.0f, 100.0f, "%.0f%%");
                }
                ui::widgets::ToggleRow("trigger_seed_window_enabled", "Spread-seed check",
                    &profile.seedWindowEnabled);
                if (ImGui::IsItemHovered())
                    ImGui::SetItemTooltip("%s", KEVQ_TR("Independent check of predicted spread across future ticks. It can delay a shot even when Hitchance is off."));
                if (!profile.hitchanceEnabled) {
                    ImGui::TextWrapped("%s", KEVQ_TR(profile.seedWindowEnabled
                        ? "Hitchance is off. Spread-seed check can still delay a shot."
                        : "Spread checks are off. Misses are possible; target and weapon checks remain active."));
                }
                ui::widgets::SliderFloatRow(
                    "trigger_minimum_damage",
                    "Minimum Damage",
                    &profile.minimumDamage,
                    1.0f,
                    200.0f,
                    "%.0f");
                ui::widgets::ToggleRow(
                    "trigger_autowall",
                    "Autowall",
                    &profile.autowall);
                ui::widgets::SliderIntRow(
                    "trigger_delay",
                    "Delay",
                    &g::targetTriggerDelayMs,
                    0,
                    500,
                    "%d ms");
                ui::widgets::ToggleRow(
                    "trigger_visible",
                    "Visible Only",
                    &g::targetTriggerVisibleOnly);
                ui::widgets::ToggleRow(
                    "trigger_auto_shot",
                    "Repeat shots",
                    &g::targetTriggerAutoShot);
                ImGui::TextWrapped("%s", KEVQ_TR("Activation key is required. Disable Repeat shots for one shot per activation."));
            });
    });
}
