#pragma once

#include <algorithm>
#include <cmath>

namespace target::wind
{
    struct Settings {
        float gravity = 18.0f;
        float wind = 3.0f;
        float maxStep = 5.0f;
        float distance = 12.0f;
    };

    struct State {
        float velocityX = 0.0f;
        float velocityY = 0.0f;
        float windX = 0.0f;
        float windY = 0.0f;
    };

    struct Step { float x = 0.0f; float y = 0.0f; };

    inline float FiniteClamp(float value, float fallback, float low, float high) noexcept
    {
        return std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    }

    inline Settings Sanitize(Settings settings) noexcept
    {
        return {FiniteClamp(settings.gravity, 18.0f, 4.0f, 40.0f),
            FiniteClamp(settings.wind, 3.0f, 0.0f, 40.0f),
            FiniteClamp(settings.maxStep, 5.0f, 1.0f, 40.0f),
            FiniteClamp(settings.distance, 12.0f, 1.0f, 40.0f)};
    }

    inline Step Advance(State& state, Step desired, Step remaining, float elapsedSeconds,
        float precisionCounts, Settings settings, float noiseX, float noiseY) noexcept
    {
        const float distance = std::hypot(remaining.x, remaining.y);
        const float desiredLength = std::hypot(desired.x, desired.y);
        if (!std::isfinite(distance) || !std::isfinite(desiredLength) ||
            !std::isfinite(elapsedSeconds) || elapsedSeconds <= 0.0f ||
            distance <= 0.0001f || desiredLength <= 0.0001f) {
            state = {};
            return {};
        }
        settings = Sanitize(settings);
        const float dt = std::clamp(elapsedSeconds, 0.0005f, 0.05f);
        const float ticks = dt * 128.0f;
        precisionCounts = FiniteClamp(precisionCounts, 1.0f, 0.0f, 10000.0f);
        if (distance <= precisionCounts) {
            state = {};
            const float scale = std::min({1.0f, distance / desiredLength, settings.maxStep * ticks / desiredLength});
            return {desired.x * scale, desired.y * scale};
        }
        if (!std::isfinite(state.velocityX) || !std::isfinite(state.velocityY) ||
            !std::isfinite(state.windX) || !std::isfinite(state.windY) ||
            state.velocityX * remaining.x + state.velocityY * remaining.y < 0.0f ||
            elapsedSeconds > 0.05f)
            state = {};
        const float t = std::clamp((distance - precisionCounts) / settings.distance, 0.0f, 1.0f);
        const float envelope = t * t * (3.0f - 2.0f * t);
        const float correlation = std::exp(-dt / 0.045f);
        const float innovation = std::sqrt(1.0f - correlation * correlation);
        state.windX = state.windX * correlation +
            FiniteClamp(noiseX, 0.0f, -1.0f, 1.0f) * innovation;
        state.windY = state.windY * correlation +
            FiniteClamp(noiseY, 0.0f, -1.0f, 1.0f) * innovation;
        const float blend = -std::expm1(-dt * settings.gravity * 3.0f);
        const float desiredPerTick = desiredLength / ticks;
        const float windGain = std::min(settings.wind * 0.25f, desiredPerTick * 0.35f) * envelope;
        state.velocityX += (desired.x / ticks + state.windX * windGain - state.velocityX) * blend;
        state.velocityY += (desired.y / ticks + state.windY * windGain - state.velocityY) * blend;
        const float ux = remaining.x / distance;
        const float uy = remaining.y / distance;
        const float along = std::clamp(state.velocityX * ux + state.velocityY * uy,
            desiredPerTick * 0.25f, desiredPerTick);
        const float sideLimit = along * 0.35f * envelope;
        const float side = std::clamp(-state.velocityX * uy + state.velocityY * ux,
            -sideLimit, sideLimit);
        Step step{(ux * along - uy * side) * ticks, (uy * along + ux * side) * ticks};
        const float length = std::hypot(step.x, step.y);
        const float limit = std::min({desiredLength, settings.maxStep * ticks, distance});
        const float scale = length > limit ? limit / length : 1.0f;
        step.x *= scale;
        step.y *= scale;
        state.velocityX = step.x / ticks;
        state.velocityY = step.y / ticks;
        return step;
    }
}
