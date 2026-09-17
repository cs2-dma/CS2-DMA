#pragma once

#include "app/Localization/localization.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace ui::widgets {

inline ImU32 ColorU32(int r, int g, int b, int a)
{
    return IM_COL32(r, g, b, a);
}

inline ImVec2 Pixel(float x, float y)
{
    return ImVec2(std::floor(x) + 0.5f, std::floor(y) + 0.5f);
}

inline float AnimateFloat(ImGuiID id, float target, float response = 18.0f)
{
    target = std::clamp(target, 0.0f, 1.0f);
    ImGuiStorage* storage = ImGui::GetStateStorage();
    if (!storage)
        return target;

    float current = storage->GetFloat(id, target);
    if (!std::isfinite(current))
        current = target;

    const float deltaTime = std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f);
    const float blend = 1.0f - std::exp(-std::max(1.0f, response) * deltaTime);
    current += (target - current) * blend;
    if (std::abs(target - current) < 0.001f)
        current = target;

    storage->SetFloat(id, current);
    return current;
}

inline int LerpByte(int from, int to, float amount)
{
    const float value =
        static_cast<float>(from) +
        static_cast<float>(to - from) * std::clamp(amount, 0.0f, 1.0f);
    return std::clamp(static_cast<int>(std::round(value)), 0, 255);
}

inline void SectionTitle(const char* label)
{
    ImGui::Dummy(ImVec2(0.0f, 2.0f));
    ImGui::TextColored(
        ImVec4(0.86f, 0.92f, 1.0f, 1.0f),
        "%s",
        app::localization::Get(label));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(
        ImVec2(p.x, p.y + 2.0f),
        ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y + 3.0f),
        ColorU32(43, 84, 148, 98),
        1.0f);
    ImGui::Dummy(ImVec2(0.0f, 9.0f));
}

inline bool ToggleSwitch(const char* id, bool* value)
{
    if (!value)
        return false;

    ImGui::PushID(id);
    const ImVec2 size(38.0f, 20.0f);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##toggle", size);
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    if (clicked)
        *value = !*value;

    const bool hovered = ImGui::IsItemHovered();
    const float valueAmount = AnimateFloat(
        ImGui::GetID("##value_animation"),
        *value ? 1.0f : 0.0f,
        18.0f);
    const float hoverAmount = AnimateFloat(
        ImGui::GetID("##hover_animation"),
        hovered ? 1.0f : 0.0f,
        22.0f);

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const int hoverLift = static_cast<int>(std::round(4.0f * hoverAmount));
    const ImU32 trackColor = ColorU32(
        std::clamp(LerpByte(50, 45, valueAmount) + hoverLift, 0, 255),
        std::clamp(LerpByte(60, 124, valueAmount) + hoverLift, 0, 255),
        std::clamp(LerpByte(74, 246, valueAmount) + hoverLift / 2, 0, 255),
        LerpByte(226, 250, hoverAmount));
    const ImVec2 min = Pixel(pos.x, pos.y);
    const ImVec2 max = Pixel(pos.x + size.x, pos.y + size.y);
    drawList->AddRectFilled(min, max, trackColor, size.y * 0.5f);
    drawList->AddRect(
        min,
        max,
        ColorU32(
            LerpByte(76, 91, valueAmount),
            LerpByte(88, 151, valueAmount),
            LerpByte(105, 255, valueAmount),
            LerpByte(68, 122, std::max(valueAmount, hoverAmount))),
        size.y * 0.5f,
        1.0f,
        0);

    const float thumbX = pos.x + 10.0f + valueAmount * 18.0f;
    drawList->AddCircleFilled(
        Pixel(thumbX, pos.y + 10.7f),
        7.0f,
        ColorU32(0, 0, 0, LerpByte(36, 56, valueAmount)));
    drawList->AddCircleFilled(
        Pixel(thumbX, pos.y + 10.0f),
        6.8f,
        ColorU32(240, 246, 255, 255));

    ImGui::PopID();
    return clicked;
}

inline void DrawRowFrame(const ImVec2& min, const ImVec2& max, bool active, bool hovered)
{
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(
        min,
        max,
        active
            ? (hovered ? ColorU32(12, 25, 41, 250) : ColorU32(9, 20, 33, 246))
            : (hovered ? ColorU32(12, 22, 35, 250) : ColorU32(8, 16, 26, 244)),
        8.0f);
    drawList->AddRect(
        min,
        max,
        active
            ? ColorU32(38, 53, 72, hovered ? 148 : 112)
            : ColorU32(34, 46, 61, hovered ? 136 : 96),
        8.0f,
        0.75f,
        0);
    if (active) {
        drawList->AddRectFilled(
            ImVec2(min.x + 1.0f, min.y + 10.0f),
            ImVec2(min.x + 3.0f, max.y - 10.0f),
            ColorU32(47, 124, 246, 212),
            2.0f);
    }
}

struct ControlRowLayout {
    float width = 0.0f;
    float height = 0.0f;
    ImVec2 min = {};
    ImVec2 max = {};
};

inline ControlRowLayout BeginControlRow(
    const char* id,
    const char* label,
    float width,
    float height,
    bool active,
    int labelAlpha)
{
    ImGui::PushID(id);
    ControlRowLayout row;
    row.width = width;
    row.height = height;
    row.min = Pixel(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().y);
    row.max = Pixel(row.min.x + row.width, row.min.y + row.height);
    DrawRowFrame(row.min, row.max, active, ImGui::IsMouseHoveringRect(row.min, row.max));
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(row.min.x + 14.0f, row.min.y + (row.height - ImGui::GetFontSize()) * 0.5f),
        ColorU32(226, 234, 246, labelAlpha),
        app::localization::Get(label));
    return row;
}

inline void EndControlRow(const ControlRowLayout& row)
{
    ImGui::SetCursorScreenPos(ImVec2(row.min.x, row.max.y + 8.0f));
    ImGui::Dummy(ImVec2(row.width, 1.0f));
    ImGui::PopID();
}

inline bool ToggleRow(const char* id, const char* label, bool* value)
{
    const float rowWidth = std::max(260.0f, ImGui::GetContentRegionAvail().x - 2.0f);
    const bool active = value && *value;
    const ControlRowLayout row =
        BeginControlRow(id, label, rowWidth, 42.0f, active, active ? 255 : 210);
    ImGui::SetCursorScreenPos(ImVec2(row.max.x - 52.0f, row.min.y + (row.height - 20.0f) * 0.5f));
    const bool changed = ToggleSwitch("toggle", value);
    EndControlRow(row);
    return changed;
}

template <typename LeftFn, typename RightFn>
inline void TwoColumnRows(const char* id, LeftFn&& leftFn, RightFn&& rightFn)
{
    if (ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        leftFn();
        ImGui::TableSetColumnIndex(1);
        rightFn();
        ImGui::EndTable();
    }
}

inline bool SliderFloatRow(const char* id, const char* label, float* value, float minValue, float maxValue, const char* format)
{
    const float rowWidth = std::max(320.0f, ImGui::GetContentRegionAvail().x - 2.0f);
    const ControlRowLayout row = BeginControlRow(id, label, rowWidth, 42.0f, false, 225);
    const float sliderWidth = std::min(190.0f, std::max(120.0f, rowWidth - 170.0f));
    ImGui::SetCursorScreenPos(ImVec2(row.max.x - sliderWidth - 12.0f, row.min.y + 10.0f));
    ImGui::SetNextItemWidth(sliderWidth);
    const bool changed = ImGui::SliderFloat(
        "##slider",
        value,
        minValue,
        maxValue,
        app::localization::Get(format));
    EndControlRow(row);
    return changed;
}

inline bool SliderIntRow(const char* id, const char* label, int* value, int minValue, int maxValue, const char* format)
{
    const float rowWidth = std::max(320.0f, ImGui::GetContentRegionAvail().x - 2.0f);
    const ControlRowLayout row = BeginControlRow(id, label, rowWidth, 42.0f, false, 225);
    const float sliderWidth = std::min(190.0f, std::max(120.0f, rowWidth - 170.0f));
    ImGui::SetCursorScreenPos(ImVec2(row.max.x - sliderWidth - 12.0f, row.min.y + 10.0f));
    ImGui::SetNextItemWidth(sliderWidth);
    const bool changed = ImGui::SliderInt(
        "##slider",
        value,
        minValue,
        maxValue,
        app::localization::Get(format));
    EndControlRow(row);
    return changed;
}

inline bool CompactInputIntRowAt(const char* id, const char* label, int* value, float rowWidth, float inputX)
{
    rowWidth = std::max(260.0f, rowWidth);
    inputX = std::clamp(inputX, 80.0f, rowWidth - 126.0f);
    const ControlRowLayout row = BeginControlRow(id, label, rowWidth, 42.0f, false, 225);
    ImGui::SetCursorScreenPos(ImVec2(row.min.x + inputX, row.min.y + 9.0f));
    ImGui::SetNextItemWidth(114.0f);
    const bool changed = ImGui::InputInt("##input", value, 0, 0);
    EndControlRow(row);
    return changed;
}

inline void ColorRow(const char* id, const char* label, float* color, ImGuiColorEditFlags flags)
{
    const float rowWidth = std::max(260.0f, ImGui::GetContentRegionAvail().x - 2.0f);
    const ControlRowLayout row = BeginControlRow(id, label, rowWidth, 42.0f, false, 225);
    ImGui::SetCursorScreenPos(ImVec2(row.max.x - 42.0f, row.min.y + 8.0f));
    ImGui::SetNextItemWidth(28.0f);
    ImGui::ColorEdit4("##color", color, flags);
    EndControlRow(row);
}

inline bool FullButton(const char* label, float height = 32.0f)
{
    const float width = ImGui::GetContentRegionAvail().x;
    return ImGui::Button(app::localization::Get(label), ImVec2(width, height));
}

inline bool CompactButton(const char* label, float width, float height = 32.0f)
{
    width = std::max(120.0f, width);
    return ImGui::Button(app::localization::Get(label), ImVec2(width, height));
}

} 
