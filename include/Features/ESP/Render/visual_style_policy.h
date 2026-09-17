#pragma once

#include <algorithm>
#include <cstdint>

namespace esp::render
{
    enum class BoxStyle : std::uint8_t {
        Corners = 0,
        Full = 1,
        Dashed = 2
    };

    enum class BarColorMode : std::uint8_t {
        Dynamic = 0,
        Solid = 1,
        Gradient = 2
    };

    struct BarColor {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    inline BoxStyle NormalizeBoxStyle(int value) noexcept
    {
        if (value < static_cast<int>(BoxStyle::Corners) ||
            value > static_cast<int>(BoxStyle::Dashed)) {
            return BoxStyle::Corners;
        }
        return static_cast<BoxStyle>(value);
    }

    inline BarColorMode NormalizeBarColorMode(int value) noexcept
    {
        if (value < static_cast<int>(BarColorMode::Dynamic) ||
            value > static_cast<int>(BarColorMode::Gradient)) {
            return BarColorMode::Dynamic;
        }
        return static_cast<BarColorMode>(value);
    }

    inline float ResolveCornerLength(
        float width,
        float height,
        int cornerPercent) noexcept
    {
        const float shortSide = std::max(0.0f, std::min(width, height));
        if (shortSide <= 0.0f)
            return 0.0f;
        const float fraction = std::clamp(cornerPercent, 10, 45) * 0.01f;
        const float maximum = shortSide * 0.5f;
        const float minimum = std::min(3.0f, maximum);
        return std::clamp(shortSide * fraction, minimum, maximum);
    }

    inline BarColor ReadBarColor(const float* color) noexcept
    {
        if (!color)
            return {};
        return {
            std::clamp(color[0], 0.0f, 1.0f),
            std::clamp(color[1], 0.0f, 1.0f),
            std::clamp(color[2], 0.0f, 1.0f),
            std::clamp(color[3], 0.0f, 1.0f)
        };
    }

    inline BarColor InterpolateBarColor(
        const BarColor& from,
        const BarColor& to,
        float amount) noexcept
    {
        const float t = std::clamp(amount, 0.0f, 1.0f);
        return {
            from.r + (to.r - from.r) * t,
            from.g + (to.g - from.g) * t,
            from.b + (to.b - from.b) * t,
            from.a + (to.a - from.a) * t
        };
    }

    inline BarColor ResolveDynamicBarColor(float fraction) noexcept
    {
        constexpr BarColor low{ 1.0f, 0.12f, 0.08f, 1.0f };
        constexpr BarColor medium{ 1.0f, 0.72f, 0.08f, 1.0f };
        constexpr BarColor high{ 0.25f, 0.95f, 0.35f, 1.0f };
        const float t = std::clamp(fraction, 0.0f, 1.0f);
        return t <= 0.5f
            ? InterpolateBarColor(low, medium, t * 2.0f)
            : InterpolateBarColor(medium, high, (t - 0.5f) * 2.0f);
    }

    inline BarColor ResolveDynamicArmorBarColor() noexcept
    {
        return { 0.35f, 0.65f, 1.0f, 1.0f };
    }

    inline BarColor ResolveBarColor(
        int modeValue,
        float fraction,
        const float* primary,
        const float* low) noexcept
    {
        (void)low;
        if (NormalizeBarColorMode(modeValue) == BarColorMode::Dynamic)
            return ResolveDynamicBarColor(fraction);
        return ReadBarColor(primary);
    }

    inline BarColor ResolveArmorBarColor(
        int modeValue,
        float fraction,
        const float* primary,
        const float* low) noexcept
    {
        if (NormalizeBarColorMode(modeValue) == BarColorMode::Dynamic)
            return ResolveDynamicArmorBarColor();
        return ResolveBarColor(modeValue, fraction, primary, low);
    }
}
