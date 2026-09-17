#pragma once

#include <imgui.h>

#include <array>

namespace ui::icons
{
    // Bootstrap Icons v1.13.1 subset. See the bundled MIT license.
    inline constexpr wchar_t kFontResourceName[] = L"BOOTSTRAP_ICONS_TTF";

    enum class Icon : ImWchar
    {
        ArrowsFullscreen = 0xF14D,
        BoundingBoxCircles = 0xF1B5,
        Crosshair = 0xF794,
        Display = 0xF302,
        Eye = 0xF341,
        Flag = 0xF3CC,
        Globe = 0xF3EF,
        HeartPulse = 0xF76F,
        People = 0xF4D0,
        PersonArmsUp = 0xF8F6,
        Radar = 0xF8CF,
        Rulers = 0xF523,
        Shield = 0xF53F,
        Sliders = 0xF56B,
        Stopwatch = 0xF597
    };

    [[nodiscard]] constexpr ImWchar Codepoint(Icon icon) noexcept
    {
        return static_cast<ImWchar>(icon);
    }

    inline constexpr std::array<Icon, 15> kAllIcons = {
        Icon::ArrowsFullscreen,
        Icon::BoundingBoxCircles,
        Icon::Crosshair,
        Icon::Display,
        Icon::Eye,
        Icon::Flag,
        Icon::Globe,
        Icon::HeartPulse,
        Icon::People,
        Icon::PersonArmsUp,
        Icon::Radar,
        Icon::Rulers,
        Icon::Shield,
        Icon::Sliders,
        Icon::Stopwatch
    };

    inline constexpr ImWchar kGlyphRanges[] = {
        Codepoint(Icon::ArrowsFullscreen), Codepoint(Icon::ArrowsFullscreen),
        Codepoint(Icon::BoundingBoxCircles), Codepoint(Icon::BoundingBoxCircles),
        Codepoint(Icon::Display), Codepoint(Icon::Display),
        Codepoint(Icon::Eye), Codepoint(Icon::Eye),
        Codepoint(Icon::Flag), Codepoint(Icon::Flag),
        Codepoint(Icon::Globe), Codepoint(Icon::Globe),
        Codepoint(Icon::People), Codepoint(Icon::People),
        Codepoint(Icon::Rulers), Codepoint(Icon::Rulers),
        Codepoint(Icon::Shield), Codepoint(Icon::Shield),
        Codepoint(Icon::Sliders), Codepoint(Icon::Sliders),
        Codepoint(Icon::Stopwatch), Codepoint(Icon::Stopwatch),
        Codepoint(Icon::HeartPulse), Codepoint(Icon::HeartPulse),
        Codepoint(Icon::Crosshair), Codepoint(Icon::Crosshair),
        Codepoint(Icon::Radar), Codepoint(Icon::Radar),
        Codepoint(Icon::PersonArmsUp), Codepoint(Icon::PersonArmsUp),
        0
    };

    [[nodiscard]] bool DrawCentered(
        ImDrawList* drawList,
        ImFont* font,
        Icon icon,
        const ImVec2& boxMin,
        float boxSize,
        float glyphSize,
        ImU32 color) noexcept;
}
