#include "Features/ESP/UI/esp_sections.h"

#include "app/Core/globals.h"
#include "app/UI/MenuShell/ui_icons.h"
#include "app/UI/MenuShell/ui_widgets.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <string>

namespace
{
    using ui::widgets::AnimateFloat;
    using ui::widgets::ColorU32;
    using ui::widgets::LerpByte;
    using ui::widgets::Pixel;
    using ui::widgets::ToggleSwitch;
    using ui::icons::Icon;

    constexpr ImGuiColorEditFlags kPickerFlags =
        ImGuiColorEditFlags_AlphaBar |
        ImGuiColorEditFlags_AlphaPreviewHalf;

    constexpr ImGuiColorEditFlags kInlineColorFlags =
        kPickerFlags | ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel;

    constexpr float kSliderWidth = 160.0f;
    constexpr float kEspRowHeight = 50.0f;
    constexpr float kEspRowGap = 12.0f;
    constexpr float kEspColumnGap = 18.0f;
    constexpr float kEspRowRounding = 8.0f;

    std::string s_activeSettingsWindow = "";

    ImVec2 Add(const ImVec2& p, float x, float y)
    {
        return Pixel(p.x + x, p.y + y);
    }

    void DrawUiIcon(
        ImDrawList* drawList,
        Icon icon,
        const ImVec2& pos,
        float boxSize,
        ImU32 color,
        float glyphSize = 18.0f)
    {
        if (ui::icons::DrawCentered(
                drawList,
                g::fontUiIcons,
                icon,
                Pixel(pos.x, pos.y),
                boxSize,
                glyphSize,
                color)) {
            return;
        }

        const ImVec2 center = Pixel(pos.x + boxSize * 0.5f, pos.y + boxSize * 0.5f);
        drawList->AddCircle(center, std::max(2.0f, boxSize * 0.24f), color, 16, 1.35f);
        drawList->AddCircleFilled(center, 1.35f, color, 10);
    }

    bool SettingsButton(const char* id, bool selected, bool parentHovered)
    {
        ImGui::PushID(id);
        const ImVec2 size(26.0f, 26.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##settings", size);
        const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
        const bool hovered = ImGui::IsItemHovered();
        if (hovered)
            ImGui::SetItemTooltip("%s", KEVQ_TR("Feature settings"));

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const float hoverAmount = AnimateFloat(
            ImGui::GetID("##hover_animation"),
            hovered ? 1.0f : 0.0f,
            22.0f);
        const float selectedAmount = AnimateFloat(
            ImGui::GetID("##selected_animation"),
            selected ? 1.0f : 0.0f,
            18.0f);
        const float revealAmount = AnimateFloat(
            ImGui::GetID("##reveal_animation"),
            (hovered || selected) ? 1.0f : (parentHovered ? 0.72f : 0.32f),
            18.0f);
        const float surfaceAmount = std::max(hoverAmount, selectedAmount);
        const ImVec2 buttonMin = Pixel(pos.x, pos.y);
        const ImVec2 buttonMax = Pixel(pos.x + size.x, pos.y + size.y);

        if (surfaceAmount > 0.001f) {
            drawList->AddRectFilled(
                buttonMin,
                buttonMax,
                ColorU32(
                    LerpByte(17, 22, selectedAmount),
                    LerpByte(27, 58, selectedAmount),
                    LerpByte(41, 108, selectedAmount),
                    static_cast<int>(std::round(210.0f * surfaceAmount))),
                7.0f);
            drawList->AddRect(
                buttonMin,
                buttonMax,
                ColorU32(
                    LerpByte(72, 65, selectedAmount),
                    LerpByte(91, 132, selectedAmount),
                    LerpByte(116, 238, selectedAmount),
                    static_cast<int>(std::round(170.0f * surfaceAmount))),
                7.0f,
                0.8f,
                0);
        }

        DrawUiIcon(
            drawList,
            Icon::Sliders,
            pos,
            size.x,
            ColorU32(
                LerpByte(158, 205, selectedAmount),
                LerpByte(174, 220, selectedAmount),
                LerpByte(197, 248, selectedAmount),
                LerpByte(142, 255, revealAmount)),
            16.0f);

        ImGui::PopID();
        return clicked;
    }

    void ColorRow(const char* id, const char* label, float* color);

    template <typename ExtraFn>
    void DrawOptionRow(const char* id,
                       Icon icon,
                       const char* label,
                       bool* enabled,
                       float* color,
                       ExtraFn&& extraFn,
                       bool showSettings = true,
                       float forcedWidth = 0.0f)
    {
        ImGui::PushID(id);

        const float availableWidth = forcedWidth > 0.0f ? forcedWidth : ImGui::GetContentRegionAvail().x - 2.0f;
        const float rowWidth = std::max(1.0f, availableWidth);
        const ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        const ImVec2 rowPos = Pixel(cursorPos.x, cursorPos.y);
        const ImVec2 rowMax = Pixel(rowPos.x + rowWidth, rowPos.y + kEspRowHeight);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const bool isEnabled = enabled && *enabled;
        const bool rowHovered = ImGui::IsMouseHoveringRect(rowPos, rowMax);
        const bool settingsOpen = showSettings && s_activeSettingsWindow == id;
        const float enabledAmount = AnimateFloat(
            ImGui::GetID("##enabled_animation"),
            isEnabled ? 1.0f : 0.0f,
            16.0f);
        const float hoverAmount = AnimateFloat(
            ImGui::GetID("##hover_animation"),
            rowHovered ? 1.0f : 0.0f,
            20.0f);
        const float settingsAmount = AnimateFloat(
            ImGui::GetID("##settings_animation"),
            settingsOpen ? 1.0f : 0.0f,
            18.0f);

        const int backgroundHoverLift = static_cast<int>(std::round(4.0f * hoverAmount));
        const int backgroundSettingsLift = static_cast<int>(std::round(2.0f * settingsAmount));

        drawList->AddRectFilled(
            rowPos,
            rowMax,
            ColorU32(
                std::clamp(LerpByte(8, 9, enabledAmount) + backgroundHoverLift, 0, 255),
                std::clamp(LerpByte(16, 21, enabledAmount) + backgroundHoverLift + backgroundSettingsLift, 0, 255),
                std::clamp(LerpByte(26, 35, enabledAmount) + backgroundHoverLift * 2 + backgroundSettingsLift, 0, 255),
                LerpByte(244, 251, hoverAmount)),
            kEspRowRounding);
        drawList->AddRect(
            rowPos,
            rowMax,
            ColorU32(
                LerpByte(32, 37, enabledAmount) + static_cast<int>(std::round(8.0f * hoverAmount)),
                LerpByte(44, 53, enabledAmount) + static_cast<int>(std::round(11.0f * hoverAmount)),
                LerpByte(59, 72, enabledAmount) + static_cast<int>(std::round(15.0f * hoverAmount)),
                std::clamp(
                    88 +
                        static_cast<int>(std::round(12.0f * enabledAmount)) +
                        static_cast<int>(std::round(36.0f * hoverAmount)) +
                        static_cast<int>(std::round(10.0f * settingsAmount)),
                    0,
                    255)),
            kEspRowRounding,
            0.75f,
            0);

        if (enabledAmount > 0.001f) {
            drawList->AddRectFilled(
                ImVec2(rowPos.x + 1.0f, rowPos.y + 10.0f),
                ImVec2(rowPos.x + 3.0f, rowMax.y - 10.0f),
                ColorU32(
                    47,
                    124,
                    246,
                    static_cast<int>(std::round(218.0f * enabledAmount))),
                2.0f);
        }

        const float contentAmount = std::max(
            enabledAmount,
            std::max(hoverAmount * 0.45f, settingsAmount * 0.35f));
        constexpr float iconSize = 20.0f;
        const ImVec2 iconPos(rowPos.x + 17.0f, rowPos.y + (kEspRowHeight - iconSize) * 0.5f);
        DrawUiIcon(
            drawList,
            icon,
            iconPos,
            iconSize,
            ColorU32(
                LerpByte(151, 207, contentAmount),
                LerpByte(166, 220, contentAmount),
                LerpByte(188, 239, contentAmount),
                LerpByte(184, 250, contentAmount)));

        const float rightPad = 12.0f;
        const float settingsWidth = showSettings ? 32.0f : 0.0f;
        const float toggleX = rowMax.x - rightPad - settingsWidth - 38.0f - (showSettings ? 8.0f : 0.0f);
        const ImVec2 labelPos(rowPos.x + 55.0f, rowPos.y + (kEspRowHeight - ImGui::GetFontSize()) * 0.5f);
        const ImVec4 labelClip(labelPos.x, rowPos.y, std::max(labelPos.x, toggleX - 8.0f), rowMax.y);
        drawList->AddText(
            nullptr, 0.0f,
            labelPos,
            ColorU32(
                LerpByte(172, 228, contentAmount),
                LerpByte(185, 236, contentAmount),
                LerpByte(203, 248, contentAmount),
                LerpByte(208, 255, contentAmount)),
            app::localization::Get(label), nullptr, 0.0f, &labelClip);
        if (rowHovered && ImGui::GetMousePos().x < toggleX)
            ImGui::SetTooltip("%s", app::localization::Get(label));
        ImGui::SetCursorScreenPos(ImVec2(toggleX, rowPos.y + (kEspRowHeight - 20.0f) * 0.5f));
        ToggleSwitch("toggle", enabled);

        if (showSettings) {
            ImGui::SetCursorScreenPos(ImVec2(rowMax.x - rightPad - 26.0f, rowPos.y + (kEspRowHeight - 26.0f) * 0.5f));
            if (SettingsButton("settings", settingsOpen, rowHovered)) {
                if (s_activeSettingsWindow == id) {
                    s_activeSettingsWindow = "";
                } else {
                    s_activeSettingsWindow = id;
                }
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(rowPos.x, rowMax.y + kEspRowGap));
        ImGui::Dummy(ImVec2(rowWidth, 1.0f));

        if (s_activeSettingsWindow == id) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 12.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 8.0f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.035f, 0.055f, 0.085f, 0.98f));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, ImVec4(0.035f, 0.055f, 0.085f, 0.98f));
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.16f, 0.24f, 0.36f, 0.82f));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.07f, 0.10f, 0.15f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.10f, 0.14f, 0.21f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.20f, 0.48f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_CheckMark, ImVec4(0.20f, 0.48f, 1.0f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_TitleBg, ImVec4(0.035f, 0.055f, 0.085f, 0.98f));
            ImGui::PushStyleColor(ImGuiCol_TitleBgActive, ImVec4(0.055f, 0.085f, 0.125f, 0.98f));
            ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, ImVec4(0.035f, 0.055f, 0.085f, 0.98f));

            char windowTitle[128];
            std::snprintf(
                windowTitle,
                sizeof(windowTitle),
                KEVQ_TR("%s Settings###%s_settings_win"),
                app::localization::Get(label),
                id);

            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            const float popupWidth = std::min(520.0f, std::max(1.0f, viewport->WorkSize.x - 24.0f));
            const float popupHeight = std::max(1.0f, viewport->WorkSize.y - 24.0f);
            ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.5f,
                viewport->WorkPos.y + 12.0f), ImGuiCond_Appearing, ImVec2(0.5f, 0.0f));
            ImGui::SetNextWindowSizeConstraints(ImVec2(popupWidth, 0.0f), ImVec2(popupWidth, popupHeight));

            bool open = true;
            if (ImGui::Begin(windowTitle, &open, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
                ImDrawList* popupDrawList = ImGui::GetWindowDrawList();
                const ImVec2 linePos = ImGui::GetCursorScreenPos();
                popupDrawList->AddRectFilled(
                    linePos,
                    ImVec2(linePos.x + ImGui::GetContentRegionAvail().x, linePos.y + 1.0f),
                    ColorU32(52, 105, 186, 138),
                    1.0f);
                ImGui::Spacing();
                ImGui::Spacing();

                if (color)
                    ColorRow("accent", "Accent Color", color);

                extraFn();
            }
            ImGui::End();

            if (!open) {
                s_activeSettingsWindow = "";
            }

            ImGui::PopStyleColor(10);
            ImGui::PopStyleVar(4);
        }

        ImGui::PopID();
    }

    struct SettingsRowLayout {
        float width = 0.0f;
        float height = 0.0f;
        ImVec2 min = {};
        ImVec2 max = {};
    };

    SettingsRowLayout BeginSettingsRow(
        const char* id,
        const char* label,
        float minimumWidth,
        float height,
        bool active,
        int inactiveBorderAlpha,
        int inactiveLabelAlpha)
    {
        ImGui::PushID(id);
        SettingsRowLayout row;
        row.width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        row.height = height;
        row.min = Pixel(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y);
        row.max = Pixel(row.min.x + row.width, row.min.y + row.height);
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(row.min, row.max, ColorU32(8, 17, 29, 238), 7.0f);
        drawList->AddRect(
            row.min,
            row.max,
            active ? ColorU32(38, 92, 162, 102) : ColorU32(41, 58, 82, inactiveBorderAlpha),
            7.0f,
            0.65f,
            0);
        // Reserve the controls' portion even with long translations/high DPI.
        const float controlsWidth = minimumWidth >= 430.0f ? 272.0f :
            minimumWidth >= 340.0f ? kSliderWidth + 24.0f : minimumWidth >= 320.0f ? 104.0f : 62.0f;
        const ImVec4 labelClip(row.min.x, row.min.y, std::max(row.min.x, row.max.x - controlsWidth), row.max.y);
        drawList->AddText(
            nullptr, 0.0f,
            ImVec2(row.min.x + 12.0f, row.min.y + (row.height - ImGui::GetFontSize()) * 0.5f),
            ColorU32(226, 234, 246, active ? 255 : inactiveLabelAlpha),
            app::localization::Get(label), nullptr, 0.0f, &labelClip);
        if (ImGui::IsMouseHoveringRect(row.min, ImVec2(row.max.x - controlsWidth, row.max.y)))
            ImGui::SetTooltip("%s", app::localization::Get(label));
        return row;
    }

    void EndSettingsRow(const SettingsRowLayout& row)
    {
        ImGui::SetCursorScreenPos(ImVec2(row.min.x, row.max.y + 7.0f));
        ImGui::Dummy(ImVec2(row.width, 1.0f));
        ImGui::PopID();
    }

    void ColorRow(const char* id, const char* label, float* color)
    {
        const SettingsRowLayout row =
            BeginSettingsRow(id, label, 300.0f, 34.0f, false, 120, 235);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - 40.0f, row.min.y + 5.0f));
        ImGui::SetNextItemWidth(26.0f);
        ImGui::ColorEdit4("##c", color, kInlineColorFlags);
        EndSettingsRow(row);
    }

    bool ToggleSetting(const char* id, const char* label, bool* toggle)
    {
        const bool isOn = toggle && *toggle;
        const SettingsRowLayout row =
            BeginSettingsRow(id, label, 300.0f, 34.0f, isOn, 116, 218);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - 50.0f, row.min.y + 7.0f));
        const bool changed = ToggleSwitch("toggle", toggle);
        EndSettingsRow(row);
        return changed;
    }

    void ToggleColorRow(const char* id, const char* label, bool* toggle, float* color)
    {
        const bool isOn = toggle && *toggle;
        const SettingsRowLayout row =
            BeginSettingsRow(id, label, 320.0f, 34.0f, isOn, 116, 218);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - 92.0f, row.min.y + 7.0f));
        ToggleSwitch("toggle", toggle);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - 40.0f, row.min.y + 5.0f));
        ImGui::SetNextItemWidth(26.0f);
        ImGui::ColorEdit4("##c", color, kInlineColorFlags);
        EndSettingsRow(row);
    }

    void SliderSettingsRow(
        const char* id,
        const char* label,
        float* value,
        float minValue,
        float maxValue,
        const char* format)
    {
        const SettingsRowLayout row =
            BeginSettingsRow(id, label, 340.0f, 38.0f, false, 116, 230);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - kSliderWidth - 12.0f, row.min.y + 8.0f));
        ImGui::SetNextItemWidth(kSliderWidth);
        ImGui::SliderFloat(
            "##s",
            value,
            minValue,
            maxValue,
            app::localization::Get(format));
        EndSettingsRow(row);
    }

    void SizeRow(const char* id, const char* label, float* size, float minValue, float maxValue)
    {
        SliderSettingsRow(id, label, size, minValue, maxValue, *size == 0.0f ? "Default" : "%.0f");
    }

    void ThicknessRow(const char* id, const char* label, float* value, float minValue, float maxValue)
    {
        SliderSettingsRow(id, label, value, minValue, maxValue, "%.2f");
    }

    void IntSliderSettingsRow(
        const char* id,
        const char* label,
        int* value,
        int minValue,
        int maxValue,
        const char* format)
    {
        const SettingsRowLayout row =
            BeginSettingsRow(id, label, 340.0f, 38.0f, false, 116, 230);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - kSliderWidth - 12.0f, row.min.y + 8.0f));
        ImGui::SetNextItemWidth(kSliderWidth);
        ImGui::SliderInt("##s", value, minValue, maxValue, format);
        EndSettingsRow(row);
    }

    void ChoiceSettingsRow(
        const char* id,
        const char* label,
        int* value,
        const char* const* options,
        int optionCount)
    {
        const int selected = std::clamp(value ? *value : 0, 0, optionCount - 1);
        const SettingsRowLayout row =
            BeginSettingsRow(id, label, 340.0f, 38.0f, false, 116, 230);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - kSliderWidth - 12.0f, row.min.y + 7.0f));
        ImGui::SetNextItemWidth(kSliderWidth);
        if (ImGui::BeginCombo("##choice", app::localization::Get(options[selected]))) {
            for (int i = 0; i < optionCount; ++i) {
                const bool isSelected = i == selected;
                if (ImGui::Selectable(app::localization::Get(options[i]), isSelected) && value)
                    *value = i;
                if (isSelected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        EndSettingsRow(row);
    }

    void FlagSettingsRow(const char* id, const char* label, bool* toggle, float* color, float* size)
    {
        const bool isOn = toggle && *toggle;
        const SettingsRowLayout row =
            BeginSettingsRow(id, label, 430.0f, 38.0f, isOn, 116, 218);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - 260.0f, row.min.y + 9.0f));
        ToggleSwitch("toggle", toggle);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - 210.0f, row.min.y + 7.0f));
        ImGui::SetNextItemWidth(26.0f);
        ImGui::ColorEdit4("##c", color, kInlineColorFlags);
        ImGui::SetCursorScreenPos(ImVec2(row.max.x - 170.0f, row.min.y + 9.0f));
        ImGui::SetNextItemWidth(158.0f);
        ImGui::SliderFloat(
            "##s",
            size,
            0.0f,
            24.0f,
            *size == 0.0f ? KEVQ_TR("Default") : "%.0f");
        EndSettingsRow(row);
    }

    float EspGridColumnWidth()
    {
        const float availableWidth = std::max(1.0f, ImGui::GetContentRegionAvail().x);
        const float minimumColumnWidth = std::max(280.0f, ImGui::GetFontSize() * 19.0f);
        return availableWidth >= minimumColumnWidth * 2.0f + kEspColumnGap
            ? std::floor((availableWidth - kEspColumnGap) * 0.5f) : availableWidth;
    }

    template <typename LeftFn, typename RightFn>
    void RenderEspGridPair(float columnWidth, LeftFn&& leftFn, RightFn&& rightFn)
    {
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const bool twoColumns = ImGui::GetContentRegionAvail().x >= columnWidth * 2.0f + kEspColumnGap - 1.0f;
        leftFn(columnWidth);
        ImGui::SetCursorScreenPos(twoColumns ? ImVec2(start.x + columnWidth + kEspColumnGap, start.y)
            : ImVec2(start.x, start.y + kEspRowHeight + kEspRowGap));
        rightFn(columnWidth);
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + (kEspRowHeight + kEspRowGap) * (twoColumns ? 1.0f : 2.0f)));
    }
}

void ui::tabs::esp_sections::RenderCoreSection()
{
    if (!g::espEnabled) {
        s_activeSettingsWindow.clear();
        DrawOptionRow("enable", Icon::Radar, "Enable ESP", &g::espEnabled, nullptr, [] {}, false);
        return;
    }

    const float columnWidth = EspGridColumnWidth();
    RenderEspGridPair(
        columnWidth,
        [](float width) { DrawOptionRow("enable", Icon::Radar, "Enable ESP", &g::espEnabled, nullptr, [] {}, false, width); },
        [](float width) { DrawOptionRow("preview", Icon::Display, "ESP Preview", &g::espPreviewOpen, nullptr, [] {}, false, width); });
}

void ui::tabs::esp_sections::RenderOptionsGrid()
{
    if (!g::espEnabled)
        return;
    const float columnWidth = EspGridColumnWidth();
    const auto renderPair = [columnWidth](auto&& leftFn, auto&& rightFn) {
        RenderEspGridPair(columnWidth, std::forward<decltype(leftFn)>(leftFn), std::forward<decltype(rightFn)>(rightFn));
    };

    renderPair(
        [] (float width) { DrawOptionRow("box", Icon::BoundingBoxCircles, "ESP Box", &g::espBox, g::espBoxColor, [] {
                static constexpr const char* kBoxStyles[] = { "Corners", "Full", "Dashed" };
                ChoiceSettingsRow("style", "Box Style", &g::espBoxStyle, kBoxStyles, 3);
                if (g::espBoxStyle == 0)
                    IntSliderSettingsRow("corner", "Corner Length", &g::espBoxCornerPercent, 10, 45, "%d%%");
                ThicknessRow("thick", "Thickness", &g::espBoxThickness, 0.5f, 4.0f);
            }, true, width); },
        [] (float width) { DrawOptionRow("skeleton", Icon::PersonArmsUp, "Skeleton", &g::espSkeleton, g::espSkeletonColor, [] {
                ToggleSetting("dots", "Show Dots", &g::espSkeletonDots);
                ThicknessRow("thick", "Thickness", &g::espSkeletonThickness, 0.5f, 4.0f);
            }, true, width); });

    renderPair(
        [] (float width) { DrawOptionRow("health", Icon::HeartPulse, "Health Bar", &g::espHealth, nullptr, [] {
                static constexpr const char* kColorModes[] = { "Dynamic", "Solid", "Gradient" };
                ChoiceSettingsRow("mode", "Color Mode", &g::espHealthColorMode, kColorModes, 3);
                if (g::espHealthColorMode != 0)
                    ColorRow("accent", "Accent Color", g::espHealthColor);
                if (g::espHealthColorMode == 2)
                    ColorRow("low", "Low Color", g::espHealthLowColor);
                ToggleSetting("value", "Show Value", &g::espHealthText);
            }, true, width); },
        [] (float width) { DrawOptionRow("teammates", Icon::People, "Show Teammates", &g::espShowTeammates, nullptr, [] {
            ImGui::TextDisabled(
                "%s",
                KEVQ_TR("Uses the same ESP options as enemies."));
            }, true, width); });

    renderPair(
        [] (float width) { DrawOptionRow("armor", Icon::Shield, "Armor Bar", &g::espArmor, nullptr, [] {
                static constexpr const char* kColorModes[] = { "Dynamic", "Solid", "Gradient" };
                ChoiceSettingsRow("mode", "Color Mode", &g::espArmorColorMode, kColorModes, 3);
                if (g::espArmorColorMode != 0)
                    ColorRow("accent", "Accent Color", g::espArmorColor);
                if (g::espArmorColorMode == 2)
                    ColorRow("low", "Low Color", g::espArmorLowColor);
                ToggleSetting("value", "Show Value", &g::espArmorText);
            }, true, width); },
        [] (float width) { DrawOptionRow("flags", Icon::Flag, "Player Flags", &g::espFlags, nullptr, [] {
                FlagSettingsRow("name", "Name", &g::espName, g::espNameColor, &g::espNameFontSize);
                FlagSettingsRow("distance", "Distance", &g::espDistance, g::espDistanceColor, &g::espDistanceSize);
                FlagSettingsRow("blind", "Blind", &g::espFlagBlind, g::espFlagBlindColor, &g::espFlagBlindSize);
                FlagSettingsRow("scoped", "Scoped", &g::espFlagScoped, g::espFlagScopedColor, &g::espFlagScopedSize);
                FlagSettingsRow("defusing", "Defusing", &g::espFlagDefusing, g::espFlagDefusingColor, &g::espFlagDefusingSize);
                FlagSettingsRow("kit", "Kit", &g::espFlagKit, g::espFlagKitColor, &g::espFlagKitSize);
                FlagSettingsRow("money", "Money", &g::espFlagMoney, g::espFlagMoneyColor, &g::espFlagMoneySize);
            }, true, width); });

    renderPair(
        [] (float width) { DrawOptionRow("vis", Icon::Eye, "Visibility Colors", &g::espVisibilityColoring, g::espVisibleColor, [] {
                ColorRow("occ", "Occluded", g::espHiddenColor);
            }, true, width); },
        [] (float width) { DrawOptionRow("weapon", Icon::Crosshair, "Weapon Label", &g::espWeapon, nullptr, [] {
                ToggleColorRow("txt", "Label Text", &g::espWeaponText, g::espWeaponTextColor);
                SizeRow("txtsz", "Text Size", &g::espWeaponTextSize, 0.0f, 24.0f);
                ImGui::Separator();
                ToggleColorRow("icon", "Icon Weapon", &g::espWeaponIcon, g::espWeaponIconColor);
                ToggleSetting("knife", "No Knife", &g::espWeaponIconNoKnife);
                SizeRow("iconsz", "Icon Size", &g::espWeaponIconSize, 10.0f, 30.0f);
                ImGui::Separator();
                ToggleColorRow("ammo", "Weapon Ammo", &g::espWeaponAmmo, g::espWeaponAmmoColor);
                SizeRow("ammosz", "Ammo Size", &g::espWeaponAmmoSize, 0.0f, 24.0f);
            }, true, width); });

    renderPair(
        [] (float width) { DrawOptionRow("bomb", Icon::Stopwatch, "Bomb ESP", &g::espBombInfo, g::espBombColor, [] {
                ToggleSetting("text", "Show Text", &g::espBombText);
                ToggleSetting("timer", "Bomb Time", &g::espBombTime);
                ImGui::BeginDisabled(!g::espBombTime);
                ToggleSetting("timer_menu", "Show With Menu", &g::espBombTimerShowWithMenu);
                ImGui::EndDisabled();
                SizeRow("bmbtxtsz", "Text Size", &g::espBombTextSize, 0.0f, 24.0f);
            }, true, width); },
        [] (float width) { DrawOptionRow("snap", Icon::Rulers, "Snap Lines", &g::espSnaplines, g::espSnaplineColor, [] {
                ToggleSetting("top", "Snap From Top", &g::espSnaplineFromTop);
            }, true, width); });

    renderPair(
        [] (float width) { DrawOptionRow("arrows", Icon::ArrowsFullscreen, "Screen Arrows", &g::espOffscreenArrows, g::espOffscreenColor, [] {
                SizeRow("sz", "Arrow Size", &g::espOffscreenSize, 6.0f, 36.0f);
            }, true, width); },
        [] (float) {});


}

#ifdef KEVQ_UI_SMOKE_TESTS
namespace ui::tabs::esp_sections::testing {
    void OpenSettings(const char* id) { s_activeSettingsWindow = id ? id : ""; }
}
#endif
