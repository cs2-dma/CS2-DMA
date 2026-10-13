#pragma once

#include <algorithm>
#include <cmath>

namespace app::display {
    struct MappingSettings {
        int gameWidth = 1920;
        int gameHeight = 1080;
        bool autoAspect = false;
        int scaling = 0;
        float x = 0.0f;
        float y = 0.0f;
        float width = 1.0f;
        float height = 1.0f;
    };

    struct HudSettings {
        bool manual = false;
        int shape = 0;
        float x = 5.0f;
        float y = 7.0f;
        float width = 290.0f;
        float height = 290.0f;
        float scale = 1.0f;
        float safeX = 1.0f;
        float safeY = 1.0f;
    };

    struct Rect {
        float x = 0.0f;
        float y = 0.0f;
        float width = 1920.0f;
        float height = 1080.0f;
    };

    struct Viewport : Rect {
        float sourceAspect = 16.0f / 9.0f;
        bool detectedAspect = false;
    };

    inline float Bounded(float value, float low, float high, float fallback) noexcept
    {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    }

    inline float PlaceOverlayLabel(float position, float extent, float viewport,
        float padding = 2.0f) noexcept
    {
        if (!std::isfinite(extent) || !std::isfinite(viewport) || viewport <= 0.0f)
            return 0.0f;
        const float available = std::max(0.0f, viewport - std::max(0.0f, extent));
        const float margin = std::isfinite(padding)
            ? std::clamp(padding, 0.0f, available * 0.5f) : 0.0f;
        return std::isfinite(position) ? std::clamp(position, margin, available - margin) : margin;
    }

    inline MappingSettings Sanitize(MappingSettings s) noexcept
    {
        if (s.gameWidth < 320 || s.gameWidth > 16384) s.gameWidth = 1920;
        if (s.gameHeight < 240 || s.gameHeight > 16384) s.gameHeight = 1080;
        if (s.scaling < 0 || s.scaling > 2) s.scaling = 0;
        s.x = Bounded(s.x, 0.0f, 0.95f, 0.0f);
        s.y = Bounded(s.y, 0.0f, 0.95f, 0.0f);
        s.width = Bounded(s.width, 0.05f, 1.0f - s.x, 1.0f - s.x);
        s.height = Bounded(s.height, 0.05f, 1.0f - s.y, 1.0f - s.y);
        return s;
    }

    inline HudSettings Sanitize(HudSettings s) noexcept
    {
        if (s.shape < 0 || s.shape > 1) s.shape = 0;
        s.x = Bounded(s.x, 0.0f, 16384.0f, 5.0f);
        s.y = Bounded(s.y, 0.0f, 16384.0f, 7.0f);
        s.width = Bounded(s.width, 32.0f, 8192.0f, 290.0f);
        s.height = Bounded(s.height, 32.0f, 8192.0f, 290.0f);
        s.scale = Bounded(s.scale, 0.5f, 2.0f, 1.0f);
        s.safeX = Bounded(s.safeX, 0.5f, 1.0f, 1.0f);
        s.safeY = Bounded(s.safeY, 0.5f, 1.0f, 1.0f);
        return s;
    }

    inline float CameraAspect(const float (&matrix)[4][4]) noexcept
    {
        float horizontal = 0.0f;
        float vertical = 0.0f;
        for (int i = 0; i < 3; ++i) {
            horizontal += matrix[0][i] * matrix[0][i];
            vertical += matrix[1][i] * matrix[1][i];
        }
        if (!std::isfinite(horizontal) || !std::isfinite(vertical) || horizontal < 0.0001f)
            return 0.0f;
        const float aspect = std::sqrt(vertical / horizontal);
        return aspect >= 0.5f && aspect <= 4.0f ? aspect : 0.0f;
    }

    inline Viewport Resolve(MappingSettings settings, float outputWidth,
        float outputHeight, float cameraAspect = 0.0f) noexcept
    {
        const auto s = Sanitize(settings);
        Viewport v;
        v.width = Bounded(outputWidth, 1.0f, 32768.0f, 1920.0f);
        v.height = Bounded(outputHeight, 1.0f, 32768.0f, 1080.0f);
        v.sourceAspect = static_cast<float>(s.gameWidth) / static_cast<float>(s.gameHeight);
        v.detectedAspect = s.autoAspect && std::isfinite(cameraAspect) &&
            cameraAspect >= 0.5f && cameraAspect <= 4.0f;
        if (v.detectedAspect) v.sourceAspect = cameraAspect;
        if (s.scaling == 1) {
            const float width = (std::min)(v.width, v.height * v.sourceAspect);
            const float height = width / v.sourceAspect;
            v.x = (v.width - width) * 0.5f;
            v.y = (v.height - height) * 0.5f;
            v.width = width;
            v.height = height;
        } else if (s.scaling == 2) {
            v.x = v.width * s.x;
            v.y = v.height * s.y;
            v.width *= s.width;
            v.height *= s.height;
        }
        return v;
    }

    inline Rect RadarRect(const Viewport& v, HudSettings settings, float size) noexcept
    {
        const auto s = Sanitize(settings);
        Rect r;
        if (s.manual) {
            r = {s.x, s.y, s.width, s.height};
        } else {
            const float unitX = v.width / (v.sourceAspect * 1080.0f);
            const float unitY = v.height / 1080.0f;
            const float span = Bounded(size, 100.0f, 400.0f, 290.0f) * s.scale;
            r = {(1.0f - s.safeX) * v.width * 0.5f + 5.0f * unitX,
                 (1.0f - s.safeY) * v.height * 0.5f + 7.0f * unitY,
                 span * unitX, span * unitY};
        }
        r.width = (std::min)(r.width, v.width);
        r.height = (std::min)(r.height, v.height);
        r.x = std::clamp(r.x, 0.0f, v.width - r.width);
        r.y = std::clamp(r.y, 0.0f, v.height - r.height);
        return r;
    }

    inline void ClampRadarPoint(const Rect& r, bool circle, float padding,
        float& x, float& y) noexcept
    {
        const float cx = r.x + r.width * 0.5f;
        const float cy = r.y + r.height * 0.5f;
        const float rx = (std::max)(r.width * 0.5f - padding, 0.5f);
        const float ry = (std::max)(r.height * 0.5f - padding, 0.5f);
        if (!std::isfinite(x) || !std::isfinite(y)) { x = cx; y = cy; return; }
        float nx = (x - cx) / rx;
        float ny = (y - cy) / ry;
        if (circle) {
            const float length = std::hypot(nx, ny);
            if (length > 1.0f) { nx /= length; ny /= length; }
        } else {
            nx = std::clamp(nx, -1.0f, 1.0f);
            ny = std::clamp(ny, -1.0f, 1.0f);
        }
        x = cx + nx * rx;
        y = cy + ny * ry;
    }
}
