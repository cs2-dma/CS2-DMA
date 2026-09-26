#pragma once

#include <imgui.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cfloat>

namespace esp::render
{
    inline constexpr float kBarValueFontSize = 12.0f;

    inline ImFont* LoadBarValueFont(ImFontAtlas& atlas, const char* path)
    {
        ImFontConfig config = {};
        config.SizePixels = kBarValueFontSize;
        config.OversampleH = 1;
        config.OversampleV = 1;
        config.PixelSnapH = true;
        static constexpr ImWchar ranges[] = {'0', '9', 0};
        ImFont* font = path && path[0]
            ? atlas.AddFontFromFileTTF(path, kBarValueFontSize, &config, ranges)
            : nullptr;
        return font ? font : atlas.AddFontDefault(&config);
    }

    struct BarValueLabel {
        bool active = false;
        char text[8] = {};
        ImVec2 bgMin = {};
        ImVec2 bgMax = {};
        ImU32 accentColor = 0;
    };

    inline BarValueLabel MakeBarValueLabel(ImFont* font, int value, ImU32 accent)
    {
        BarValueLabel label;
        if (!font || value < 0 || value > 999999)
            return label;
        const auto converted = std::to_chars(label.text, label.text + sizeof(label.text) - 1, value);
        if (converted.ec != std::errc{})
            return label;
        *converted.ptr = '\0';
        const ImVec2 size = font->CalcTextSizeA(kBarValueFontSize, FLT_MAX, 0.0f, label.text);
        label.active = true;
        label.bgMax = ImVec2(std::ceil(size.x) + 4.0f, std::ceil(size.y) + 4.0f);
        label.accentColor = accent;
        return label;
    }

    inline bool BarLabelsOverlap(const BarValueLabel& a, const BarValueLabel& b)
    {
        return a.active && b.active && a.bgMin.x < b.bgMax.x && a.bgMax.x > b.bgMin.x &&
            a.bgMin.y < b.bgMax.y && a.bgMax.y > b.bgMin.y;
    }



    inline void DrawBarValueLabel(ImDrawList& draw, ImFont* font, const BarValueLabel& label)
    {
        if (!font || !label.active)
            return;
        const ImVec2 textPos(label.bgMin.x + 2.0f, label.bgMin.y + 1.0f);
        draw.AddRectFilled(label.bgMin, label.bgMax, IM_COL32_BLACK);
        draw.AddRectFilled(ImVec2(label.bgMin.x + 1.0f, label.bgMax.y - 2.0f),
            ImVec2(label.bgMax.x - 1.0f, label.bgMax.y - 1.0f), label.accentColor);
        for (const ImVec2 offset : {ImVec2(-1, 0), ImVec2(1, 0), ImVec2(0, -1), ImVec2(0, 1)})
            draw.AddText(font, kBarValueFontSize, ImVec2(textPos.x + offset.x, textPos.y + offset.y),
                IM_COL32_BLACK, label.text);
        draw.AddText(font, kBarValueFontSize, textPos, IM_COL32_WHITE, label.text);
    }
}
