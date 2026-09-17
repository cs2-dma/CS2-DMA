#include "Features/ESP/Render/visual_style_policy.h"

#include <cmath>

static void DrawCornerBox(ImDrawList* dl, float x, float y, float w, float h,
                          ImU32 color, ImU32 outline, float cornerLen, float thickness)
{
    float cl = cornerLen;
    if (cl > w * 0.5f) cl = w * 0.5f;
    if (cl > h * 0.5f) cl = h * 0.5f;

    float ot = thickness + 2.0f;

    dl->AddLine(ImVec2(x - 1, y), ImVec2(x + cl, y), outline, ot);
    dl->AddLine(ImVec2(x, y - 1), ImVec2(x, y + cl), outline, ot);
    dl->AddLine(ImVec2(x + w - cl, y), ImVec2(x + w + 1, y), outline, ot);
    dl->AddLine(ImVec2(x + w, y - 1), ImVec2(x + w, y + cl), outline, ot);
    dl->AddLine(ImVec2(x - 1, y + h), ImVec2(x + cl, y + h), outline, ot);
    dl->AddLine(ImVec2(x, y + h - cl), ImVec2(x, y + h + 1), outline, ot);
    dl->AddLine(ImVec2(x + w - cl, y + h), ImVec2(x + w + 1, y + h), outline, ot);
    dl->AddLine(ImVec2(x + w, y + h - cl), ImVec2(x + w, y + h + 1), outline, ot);

    dl->AddLine(ImVec2(x, y), ImVec2(x + cl, y), color, thickness);
    dl->AddLine(ImVec2(x, y), ImVec2(x, y + cl), color, thickness);
    dl->AddLine(ImVec2(x + w - cl, y), ImVec2(x + w, y), color, thickness);
    dl->AddLine(ImVec2(x + w, y), ImVec2(x + w, y + cl), color, thickness);
    dl->AddLine(ImVec2(x, y + h), ImVec2(x + cl, y + h), color, thickness);
    dl->AddLine(ImVec2(x, y + h - cl), ImVec2(x, y + h), color, thickness);
    dl->AddLine(ImVec2(x + w - cl, y + h), ImVec2(x + w, y + h), color, thickness);
    dl->AddLine(ImVec2(x + w, y + h - cl), ImVec2(x + w, y + h), color, thickness);
}

static void DrawDashedLine(
    ImDrawList* dl,
    const ImVec2& from,
    const ImVec2& to,
    ImU32 color,
    float thickness,
    float dashLength,
    float gapLength)
{
    const ImVec2 delta(to.x - from.x, to.y - from.y);
    const float length = std::sqrt(delta.x * delta.x + delta.y * delta.y);
    if (length <= 0.5f)
        return;

    const ImVec2 direction(delta.x / length, delta.y / length);
    for (float offset = 0.0f; offset < length; offset += dashLength + gapLength) {
        const float endOffset = std::min(offset + dashLength, length);
        dl->AddLine(
            ImVec2(from.x + direction.x * offset, from.y + direction.y * offset),
            ImVec2(from.x + direction.x * endOffset, from.y + direction.y * endOffset),
            color,
            thickness);
    }
}

static void DrawDashedBoxLayer(
    ImDrawList* dl,
    float x,
    float y,
    float w,
    float h,
    ImU32 color,
    float thickness,
    float dashLength,
    float gapLength)
{
    DrawDashedLine(dl, ImVec2(x, y), ImVec2(x + w, y), color, thickness, dashLength, gapLength);
    DrawDashedLine(dl, ImVec2(x + w, y), ImVec2(x + w, y + h), color, thickness, dashLength, gapLength);
    DrawDashedLine(dl, ImVec2(x + w, y + h), ImVec2(x, y + h), color, thickness, dashLength, gapLength);
    DrawDashedLine(dl, ImVec2(x, y + h), ImVec2(x, y), color, thickness, dashLength, gapLength);
}

static void DrawStyledBox(
    ImDrawList* dl,
    float x,
    float y,
    float w,
    float h,
    ImU32 color,
    ImU32 outline,
    int styleValue,
    int cornerPercent,
    float thickness)
{
    using esp::render::BoxStyle;
    switch (esp::render::NormalizeBoxStyle(styleValue)) {
    case BoxStyle::Full:
        dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), outline, 0.0f, 0, thickness + 2.0f);
        dl->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), color, 0.0f, 0, thickness);
        break;
    case BoxStyle::Dashed: {
        const float shortSide = std::max(1.0f, std::min(w, h));
        const float dashLength = std::clamp(shortSide * 0.16f, 5.0f, 10.0f);
        const float gapLength = std::clamp(dashLength * 0.58f, 3.0f, 6.0f);
        DrawDashedBoxLayer(dl, x, y, w, h, outline, thickness + 2.0f, dashLength, gapLength);
        DrawDashedBoxLayer(dl, x, y, w, h, color, thickness, dashLength, gapLength);
        break;
    }
    case BoxStyle::Corners:
    default:
        DrawCornerBox(
            dl,
            x,
            y,
            w,
            h,
            color,
            outline,
            esp::render::ResolveCornerLength(w, h, cornerPercent),
            thickness);
        break;
    }
}
