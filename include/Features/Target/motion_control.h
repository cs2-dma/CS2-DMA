#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace target::control
{
    struct Vec2 { float x = 0.0f, y = 0.0f; };

    inline float Length(Vec2 v) noexcept { return std::hypot(v.x, v.y); }
    inline Vec2 Limit(Vec2 v, float maximum) noexcept
    {
        const float length = Length(v);
        if (!std::isfinite(length) || !std::isfinite(maximum) || maximum < 0.0f) return {};
        const float scale = length > maximum && length > 0.0f ? maximum / length : 1.0f;
        return {v.x * scale, v.y * scale};
    }

    struct Trajectory
    {
        Vec2 velocity;
        Vec2 acceleration;

        Vec2 Advance(Vec2 requested, Vec2 remaining, float dt, float maxSpeed,
            float maxAcceleration, float maxJerk, bool curved = false) noexcept
        {
            if (!std::isfinite(dt) || dt <= 0.0f || dt > 0.1f ||
                !std::isfinite(Length(requested)) || !std::isfinite(Length(remaining))) {
                *this = {}; return {};
            }
            const Vec2 wanted = Limit({requested.x / dt, requested.y / dt}, maxSpeed);
            const Vec2 desiredAcceleration = Limit({(wanted.x - velocity.x) / dt,
                (wanted.y - velocity.y) / dt}, maxAcceleration);
            const Vec2 change = Limit({desiredAcceleration.x - acceleration.x,
                desiredAcceleration.y - acceleration.y}, maxJerk * dt);
            acceleration = Limit({acceleration.x + change.x, acceleration.y + change.y}, maxAcceleration);
            velocity = Limit({velocity.x + acceleration.x * dt, velocity.y + acceleration.y * dt}, maxSpeed);
            Vec2 step{velocity.x * dt, velocity.y * dt};
            const auto constrain = [](float value, float error) {
                return value * error <= 0.0f ? 0.0f : std::copysign(std::min(std::fabs(value), std::fabs(error)), error);
            };
            step = curved ? Limit(step, Length(remaining)) :
                Vec2{constrain(step.x, remaining.x), constrain(step.y, remaining.y)};
            if (step.x != velocity.x * dt) { velocity.x = step.x / dt; acceleration.x = 0.0f; }
            if (step.y != velocity.y * dt) { velocity.y = step.y / dt; acceleration.y = 0.0f; }
            return step;
        }
    };

    struct Intent
    {
        float gain = 1.0f;
        bool confident = false;
        float Advance(Vec2 residual, Vec2 error, float dt, float noiseFloor, bool valid) noexcept
        {
            confident = valid && std::isfinite(Length(residual)) && Length(residual) > noiseFloor &&
                Length(residual) < 15.0f && Length(error) > noiseFloor;
            float wanted = 1.0f;
            if (confident) {
                const float alignment = (residual.x * error.x + residual.y * error.y) /
                    (Length(residual) * Length(error));
                wanted = std::clamp((alignment + 0.35f) / 0.55f, 0.0f, 1.0f);
            }
            const float tau = wanted < gain ? 0.012f : 0.10f;
            gain += (wanted - gain) * -std::expm1(-std::clamp(dt, 0.0f, 0.05f) / tau);
            return std::clamp(gain, 0.0f, 1.0f);
        }
    };

    struct LatencyWindow
    {
        static constexpr std::array<uint64_t, 12> limits = {250, 500, 1000, 2000, 4000, 8000,
            16000, 24000, 35000, 50000, 75000, 100000};
        std::array<uint64_t, 128> values = {};
        std::array<uint16_t, 13> buckets = {};
        size_t count = 0, next = 0;
        uint64_t sum = 0;
        static size_t Bucket(uint64_t value) noexcept
        {
            return static_cast<size_t>(std::lower_bound(limits.begin(), limits.end(), value) - limits.begin());
        }
        void Add(uint64_t value) noexcept
        {
            value = std::min(value, uint64_t(1000000));
            if (count == values.size()) { sum -= values[next]; --buckets[Bucket(values[next])]; }
            else ++count;
            values[next] = value; next = (next + 1) % values.size(); sum += value; ++buckets[Bucket(value)];
        }
        uint64_t Mean() const noexcept { return count ? sum / count : 0; }
        uint64_t Percentile(unsigned percent) const noexcept
        {
            if (!count) return 0;
            const size_t required = (count * std::clamp(percent, 1u, 100u) + 99) / 100;
            size_t total = 0;
            for (size_t i = 0; i < buckets.size(); ++i) {
                total += buckets[i];
                if (total >= required) return i < limits.size() ? limits[i] : 1000000;
            }
            return 0;
        }
    };

    struct VelocityFilter
    {
        Vec2 value;
        Vec2 position;
        uint64_t sampledAtUs = 0;
        uintptr_t pawn = 0;
        uint32_t handle = 0;
        float confidence = 1.0f;

        Vec2 Update(Vec2 sample, Vec2 origin, uint64_t timestamp, uintptr_t identity, uint32_t serial) noexcept
        {
            if (timestamp == sampledAtUs && pawn == identity && handle == serial) return value;
            const float dt = timestamp > sampledAtUs ? static_cast<float>(timestamp - sampledAtUs) * 0.000001f : 0.0f;
            const bool reset = pawn != identity || handle != serial || sampledAtUs == 0 ||
                dt <= 0.0f || dt > 0.1f || Length({origin.x - position.x, origin.y - position.y}) > 128.0f;
            sample = Limit(sample, 4000.0f);
            if (reset) { value = sample; confidence = 1.0f; }
            else {
                const float change = Length({sample.x - value.x, sample.y - value.y});
                const float cutoff = std::clamp(12.0f + change * 0.10f, 12.0f, 80.0f);
                const float alpha = change < 2.0f ? 1.0f : 1.0f / (1.0f + 1.0f / (6.283185307f * cutoff * dt));
                value = {value.x + (sample.x - value.x) * alpha, value.y + (sample.y - value.y) * alpha};
                confidence = std::clamp(1.0f - change / std::max(100.0f, dt * 12000.0f), 0.25f, 1.0f);
            }
            position = origin; sampledAtUs = timestamp; pawn = identity; handle = serial;
            return value;
        }
    };
}
