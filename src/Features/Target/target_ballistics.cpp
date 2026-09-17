#include "Features/Target/target_ballistics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <cstring>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    class ValveRandom
    {
    public:
        void Seed(int seed) noexcept
        {
            // Avoid signed overflow for INT_MIN and the degenerate modulus
            // seed. Preserve the full signed seed, not just its low byte.
            const int64_t magnitude = seed < 0 ? -static_cast<int64_t>(seed) : seed;
            state_ = -static_cast<int>(magnitude >= 2147483647LL ? 1 : magnitude);
            index_ = 0;
            seeded_ = false;
            table_.fill(0);
        }

        float Float(float minimum = 0.0f, float maximum = 1.0f) noexcept
        {
            const int raw = Generate();
            const float normalized = std::fmin(
                0.99999988f,
                static_cast<float>(raw) * 4.6566129e-10f);
            return minimum + normalized * (maximum - minimum);
        }

    private:
        static int Lcg(int state) noexcept
        {
            const int quotient = state / 127773;
            int result = 16807 * (state - quotient * 127773) -
                2836 * quotient;
            if (result < 0)
                result += 2147483647;
            return result;
        }

        int Generate() noexcept
        {
            if (!seeded_) {
                int value = -state_;
                if (value < 1)
                    value = 1;
                for (int index = 39; index >= 0; --index) {
                    value = Lcg(value);
                    if (index < 32)
                        table_[index] = value;
                }
                state_ = value;
                index_ = table_[0];
                seeded_ = true;
            }
            state_ = Lcg(state_);
            const int tableIndex = index_ / 0x4000000;
            index_ = table_[tableIndex];
            table_[tableIndex] = state_;
            return index_;
        }

        int state_ = 0;
        int index_ = 0;
        std::array<int, 32> table_ = {};
        bool seeded_ = false;
    };

    struct SpreadOffset
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    struct SpreadSample
    {
        float inaccuracyRadius, inaccuracyCos, inaccuracySin;
        float spreadRadius, spreadCos, spreadSin;
    };

    SpreadSample GenerateSpreadSample(int seed) noexcept
    {
        ValveRandom random;
        random.Seed(seed);
        const float inaccuracyRadius = random.Float();
        const float inaccuracyAngle = random.Float(0.0f, kPi * 2.0f);
        const float spreadRadius = random.Float();
        const float spreadAngle = random.Float(0.0f, kPi * 2.0f);
        return {inaccuracyRadius, std::cos(inaccuracyAngle), std::sin(inaccuracyAngle),
            spreadRadius, std::cos(spreadAngle), std::sin(spreadAngle)};
    }

    const std::array<SpreadSample, target::ballistics::kHitchanceSamples>& HitchanceSamples() noexcept
    {
        // The 256 probability samples do not depend on the frame/weapon.
        // Cache ONLY the RNG/trigonometry; live weapon penalties stay dynamic.
        static const auto samples = [] {
            std::array<SpreadSample, target::ballistics::kHitchanceSamples> result{};
            for (int seed = 0; seed < target::ballistics::kHitchanceSamples; ++seed)
                result[seed] = GenerateSpreadSample(seed);
            return result;
        }();
        return samples;
    }

    class Sha1
    {
    public:
        void Reset() noexcept
        {
            state_[0] = 0x67452301u;
            state_[1] = 0xEFCDAB89u;
            state_[2] = 0x98BADCFEu;
            state_[3] = 0x10325476u;
            state_[4] = 0xC3D2E1F0u;
            count_ = 0;
            buffer_.fill(0);
            digest_.fill(0);
        }

        void Update(const void* input, size_t length) noexcept
        {
            if (!input || length == 0)
                return;
            const auto* bytes = static_cast<const uint8_t*>(input);
            size_t index = static_cast<size_t>(count_ & 63u);
            count_ += length;
            size_t cursor = 0;
            if (index != 0) {
                const size_t part = 64u - index;
                if (length < part) {
                    std::memcpy(buffer_.data() + index, bytes, length);
                    return;
                }
                std::memcpy(buffer_.data() + index, bytes, part);
                Transform(buffer_.data());
                cursor = part;
            }
            while (cursor + 64u <= length) {
                Transform(bytes + cursor);
                cursor += 64u;
            }
            if (cursor < length) {
                std::memcpy(
                    buffer_.data(),
                    bytes + cursor,
                    length - cursor);
            }
        }

        void Final() noexcept
        {
            std::array<uint8_t, 64> padding = {};
            padding[0] = 0x80u;
            const size_t index = static_cast<size_t>(count_ & 63u);
            const size_t paddingLength = index < 56u
                ? 56u - index
                : 120u - index;
            const uint64_t bitCount = count_ * 8u;
            Update(padding.data(), paddingLength);
            std::array<uint8_t, 8> bits = {};
            for (int byteIndex = 0; byteIndex < 8; ++byteIndex) {
                bits[7 - byteIndex] = static_cast<uint8_t>(
                    bitCount >> (byteIndex * 8));
            }
            Update(bits.data(), bits.size());
            for (int stateIndex = 0; stateIndex < 5; ++stateIndex) {
                digest_[stateIndex * 4 + 0] =
                    static_cast<uint8_t>(state_[stateIndex] >> 24);
                digest_[stateIndex * 4 + 1] =
                    static_cast<uint8_t>(state_[stateIndex] >> 16);
                digest_[stateIndex * 4 + 2] =
                    static_cast<uint8_t>(state_[stateIndex] >> 8);
                digest_[stateIndex * 4 + 3] =
                    static_cast<uint8_t>(state_[stateIndex]);
            }
        }

        uint32_t FirstUint32() const noexcept
        {
            uint32_t result = 0;
            std::memcpy(&result, digest_.data(), sizeof(result));
            return result;
        }

    private:
        static uint32_t RotateLeft(uint32_t value, int bits) noexcept
        {
            return (value << bits) | (value >> (32 - bits));
        }

        void Transform(const uint8_t* block) noexcept
        {
            uint32_t words[80] = {};
            for (int index = 0; index < 16; ++index) {
                words[index] =
                    (static_cast<uint32_t>(block[index * 4 + 0]) << 24) |
                    (static_cast<uint32_t>(block[index * 4 + 1]) << 16) |
                    (static_cast<uint32_t>(block[index * 4 + 2]) << 8) |
                    static_cast<uint32_t>(block[index * 4 + 3]);
            }
            for (int index = 16; index < 80; ++index) {
                words[index] = RotateLeft(
                    words[index - 3] ^ words[index - 8] ^
                    words[index - 14] ^ words[index - 16],
                    1);
            }
            uint32_t a = state_[0];
            uint32_t b = state_[1];
            uint32_t c = state_[2];
            uint32_t d = state_[3];
            uint32_t e = state_[4];
            for (int index = 0; index < 80; ++index) {
                uint32_t function = 0;
                uint32_t constant = 0;
                if (index < 20) {
                    function = (b & c) | ((~b) & d);
                    constant = 0x5A827999u;
                } else if (index < 40) {
                    function = b ^ c ^ d;
                    constant = 0x6ED9EBA1u;
                } else if (index < 60) {
                    function = (b & c) | (b & d) | (c & d);
                    constant = 0x8F1BBCDCu;
                } else {
                    function = b ^ c ^ d;
                    constant = 0xCA62C1D6u;
                }
                const uint32_t temporary = RotateLeft(a, 5) + function +
                    e + constant + words[index];
                e = d;
                d = c;
                c = RotateLeft(b, 30);
                b = a;
                a = temporary;
            }
            state_[0] += a;
            state_[1] += b;
            state_[2] += c;
            state_[3] += d;
            state_[4] += e;
        }

        std::array<uint32_t, 5> state_ = {};
        uint64_t count_ = 0;
        std::array<uint8_t, 64> buffer_ = {};
        std::array<uint8_t, 20> digest_ = {};
    };

    SpreadOffset CalculateSpread(
        int seed,
        const target::ballistics::WeaponSpread& weapon) noexcept
    {
        constexpr uint16_t kRevolver = 64;
        constexpr uint16_t kNegev = 28;
        const SpreadSample sample = seed >= 0 && seed < target::ballistics::kHitchanceSamples
            ? HitchanceSamples()[seed] : GenerateSpreadSample(seed);
        float inaccuracyRadius = sample.inaccuracyRadius;
        if (weapon.itemDefinitionIndex == kRevolver && weapon.bullets == 1) {
            inaccuracyRadius = 1.0f - inaccuracyRadius * inaccuracyRadius;
        } else if (weapon.itemDefinitionIndex == kNegev &&
                   weapon.recoilIndex < 3.0f) {
            float value = inaccuracyRadius;
            int count = 3;
            do {
                --count;
                value *= value;
            } while (static_cast<float>(count) > weapon.recoilIndex);
            inaccuracyRadius = 1.0f - value;
        }
        inaccuracyRadius *= weapon.inaccuracy;

        float spreadRadius = sample.spreadRadius;
        if (weapon.itemDefinitionIndex == kRevolver && weapon.bullets == 1) {
            spreadRadius = 1.0f - spreadRadius * spreadRadius;
        } else if (weapon.itemDefinitionIndex == kNegev &&
                   weapon.recoilIndex < 3.0f) {
            float value = spreadRadius;
            int count = 3;
            do {
                --count;
                value *= value;
            } while (static_cast<float>(count) > weapon.recoilIndex);
            spreadRadius = 1.0f - value;
        }
        spreadRadius *= weapon.spread;
        return {
            sample.spreadCos * spreadRadius + sample.inaccuracyCos * inaccuracyRadius,
            sample.spreadSin * spreadRadius + sample.inaccuracySin * inaccuracyRadius,
        };
    }

    bool RaySphereIntersection(
        const Vector3& origin,
        const Vector3& direction,
        const Vector3& center,
        float radius,
        float maximumDistance,
        float& distance) noexcept
    {
        const Vector3 offset = origin - center;
        const float b = target::ballistics::Dot(offset, direction);
        const float c = target::ballistics::Dot(offset, offset) - radius * radius;
        const float discriminant = b * b - c;
        if (discriminant < 0.0f)
            return false;
        const float root = std::sqrt(discriminant);
        float candidate = -b - root;
        if (candidate < 0.0f)
            candidate = -b + root;
        if (candidate < 0.0f || candidate > maximumDistance)
            return false;
        distance = candidate;
        return true;
    }

    Vector3 Cross(const Vector3& first, const Vector3& second) noexcept
    {
        return {
            first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x,
        };
    }

    float PointSegmentDistanceSquared(
        const Vector3& point,
        const Vector3& start,
        const Vector3& end) noexcept
    {
        const Vector3 axis = end - start;
        const float axisLengthSquared = target::ballistics::Dot(axis, axis);
        if (axisLengthSquared <= 0.0000001f)
            return target::ballistics::Dot(point - start, point - start);
        const float along = std::clamp(
            target::ballistics::Dot(point - start, axis) /
                axisLengthSquared,
            0.0f,
            1.0f);
        const Vector3 nearest = start + axis * along;
        return target::ballistics::Dot(point - nearest, point - nearest);
    }

    float SegmentSegmentDistanceSquared(
        const Vector3& firstStart,
        const Vector3& firstEnd,
        const Vector3& secondStart,
        const Vector3& secondEnd) noexcept
    {
        constexpr float kEpsilon = 0.0000001f;
        const Vector3 first = firstEnd - firstStart;
        const Vector3 second = secondEnd - secondStart;
        const Vector3 offset = firstStart - secondStart;
        const float firstLengthSquared =
            target::ballistics::Dot(first, first);
        const float secondLengthSquared =
            target::ballistics::Dot(second, second);
        const float secondOffset =
            target::ballistics::Dot(second, offset);
        float firstParameter = 0.0f;
        float secondParameter = 0.0f;

        if (firstLengthSquared <= kEpsilon &&
            secondLengthSquared <= kEpsilon) {
            return target::ballistics::Dot(offset, offset);
        }
        if (firstLengthSquared <= kEpsilon) {
            secondParameter = std::clamp(
                secondOffset / secondLengthSquared,
                0.0f,
                1.0f);
        } else {
            const float firstOffset =
                target::ballistics::Dot(first, offset);
            if (secondLengthSquared <= kEpsilon) {
                firstParameter = std::clamp(
                    -firstOffset / firstLengthSquared,
                    0.0f,
                    1.0f);
            } else {
                const float axes = target::ballistics::Dot(first, second);
                const float denominator = firstLengthSquared *
                    secondLengthSquared - axes * axes;
                if (std::fabs(denominator) > kEpsilon) {
                    firstParameter = std::clamp(
                        (axes * secondOffset - firstOffset *
                            secondLengthSquared) / denominator,
                        0.0f,
                        1.0f);
                }
                secondParameter =
                    (axes * firstParameter + secondOffset) /
                    secondLengthSquared;
                if (secondParameter < 0.0f) {
                    secondParameter = 0.0f;
                    firstParameter = std::clamp(
                        -firstOffset / firstLengthSquared,
                        0.0f,
                        1.0f);
                } else if (secondParameter > 1.0f) {
                    secondParameter = 1.0f;
                    firstParameter = std::clamp(
                        (axes - firstOffset) / firstLengthSquared,
                        0.0f,
                        1.0f);
                }
            }
        }
        const Vector3 separation = offset + first * firstParameter -
            second * secondParameter;
        return target::ballistics::Dot(separation, separation);
    }

    Vector3 PointToAngles(
        const Vector3& origin,
        const Vector3& point) noexcept
    {
        const Vector3 delta = point - origin;
        const float horizontal = std::sqrt(
            delta.x * delta.x + delta.y * delta.y);
        if (!IsFiniteVec(delta) ||
            horizontal + std::fabs(delta.z) < 0.00001f) {
            return {};
        }
        constexpr float kRadiansToDegrees = 180.0f / kPi;
        return {
            -std::atan2(delta.z, horizontal) * kRadiansToDegrees,
            std::atan2(delta.y, delta.x) * kRadiansToDegrees,
            0.0f,
        };
    }

    bool TraceSpreadSeedDetailed(
        const Vector3& eyePosition,
        const Vector3& aimAngles,
        const esp::PlayerData& player,
        const target::ballistics::WeaponSpread& weapon,
        uint32_t spreadSeed,
        int requiredHitgroup,
        float& geometricSafety) noexcept
    {
        geometricSafety = 0.0f;
        if (!player.hasHitboxes || !IsFiniteVec(eyePosition) ||
            !IsFiniteVec(aimAngles) || !std::isfinite(weapon.inaccuracy) ||
            !std::isfinite(weapon.spread) || weapon.inaccuracy < 0.0f ||
            weapon.spread < 0.0f || !std::isfinite(weapon.range) ||
            weapon.range <= 0.0f) {
            return false;
        }
        Vector3 direction = {};
        if (!target::ballistics::ResolveSpreadDirection(
                aimAngles,
                weapon,
                spreadSeed,
                direction)) {
            return false;
        }

        bool hit = false;
        const Vector3 rayEnd = eyePosition + direction * weapon.range;
        const int count = std::min<int>(
            player.hitboxCount,
            esp::kMaximumPlayerHitboxes);
        for (int index = 0; index < count; ++index) {
            const esp::HitboxCapsule& capsule = player.hitboxes[index];
            if (!capsule.valid || !std::isfinite(capsule.radius) ||
                capsule.radius <= 0.0f ||
                (requiredHitgroup > 0 &&
                 capsule.hitgroup != requiredHitgroup)) {
                continue;
            }
            if (!target::ballistics::RayCapsuleIntersection(
                    eyePosition,
                    direction,
                    capsule.start,
                    capsule.end,
                    capsule.radius,
                    weapon.range)) {
                continue;
            }
            hit = true;
            const float distanceSquared = SegmentSegmentDistanceSquared(
                eyePosition,
                rayEnd,
                capsule.start,
                capsule.end);
            if (!std::isfinite(distanceSquared))
                continue;
            const float normalizedDistance = std::sqrt(std::max(
                0.0f,
                distanceSquared)) / capsule.radius;
            geometricSafety = std::max(
                geometricSafety,
                std::clamp(1.0f - normalizedDistance, 0.0f, 1.0f));
        }
        return hit;
    }
}

float target::ballistics::Dot(
    const Vector3& first,
    const Vector3& second) noexcept
{
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

Vector3 target::ballistics::Normalize(const Vector3& value) noexcept
{
    const float lengthSquared = Dot(value, value);
    if (!std::isfinite(lengthSquared) || lengthSquared < 0.00000001f)
        return {};
    return value * (1.0f / std::sqrt(lengthSquared));
}

void target::ballistics::AnglesToDirections(
    const Vector3& angles,
    Vector3& forward,
    Vector3& right,
    Vector3& up) noexcept
{
    const float pitch = angles.x * (kPi / 180.0f);
    const float yaw = angles.y * (kPi / 180.0f);
    const float roll = angles.z * (kPi / 180.0f);
    const float sp = std::sin(pitch);
    const float cp = std::cos(pitch);
    const float sy = std::sin(yaw);
    const float cy = std::cos(yaw);
    const float sr = std::sin(roll);
    const float cr = std::cos(roll);
    forward = {cp * cy, cp * sy, -sp};
    right = {
        -sr * sp * cy + -cr * -sy,
        -sr * sp * sy + -cr * cy,
        -sr * cp,
    };
    up = {
        cr * sp * cy + -sr * -sy,
        cr * sp * sy + -sr * cy,
        cr * cp,
    };
}

bool target::ballistics::RayCapsuleIntersection(
    const Vector3& origin,
    const Vector3& directionInput,
    const Vector3& capsuleStart,
    const Vector3& capsuleEnd,
    float radius,
    float maximumDistance,
    float* hitDistance) noexcept
{
    if (!IsFiniteVec(origin) || !IsFiniteVec(directionInput) ||
        !IsFiniteVec(capsuleStart) || !IsFiniteVec(capsuleEnd) ||
        !std::isfinite(radius) || radius <= 0.0f ||
        !std::isfinite(maximumDistance) || maximumDistance <= 0.0f) {
        return false;
    }
    const Vector3 direction = Normalize(directionInput);
    if (Dot(direction, direction) < 0.5f)
        return false;
    const Vector3 axis = capsuleEnd - capsuleStart;
    const float axisLengthSquared = Dot(axis, axis);
    float closest = (std::numeric_limits<float>::max)();
    if (axisLengthSquared < 0.000001f) {
        if (!RaySphereIntersection(
                origin,
                direction,
                capsuleStart,
                radius,
                maximumDistance,
                closest)) {
            return false;
        }
    } else {
        const Vector3 offset = origin - capsuleStart;
        const float axisRay = Dot(axis, direction);
        const float axisOffset = Dot(axis, offset);
        const float rayOffset = Dot(direction, offset);
        const float offsetSquared = Dot(offset, offset);
        const float a = axisLengthSquared - axisRay * axisRay;
        const float b = axisLengthSquared * rayOffset - axisOffset * axisRay;
        const float c = axisLengthSquared * offsetSquared -
            axisOffset * axisOffset - radius * radius * axisLengthSquared;
        const float discriminant = b * b - a * c;
        if (std::fabs(a) > 0.0000001f && discriminant >= 0.0f) {
            const float candidate = (-b - std::sqrt(discriminant)) / a;
            const float alongAxis = axisOffset + candidate * axisRay;
            if (candidate >= 0.0f && candidate <= maximumDistance &&
                alongAxis > 0.0f && alongAxis < axisLengthSquared) {
                closest = candidate;
            }
        }
        float capDistance = 0.0f;
        if (RaySphereIntersection(
                origin,
                direction,
                capsuleStart,
                radius,
                maximumDistance,
                capDistance)) {
            closest = std::min(closest, capDistance);
        }
        if (RaySphereIntersection(
                origin,
                direction,
                capsuleEnd,
                radius,
                maximumDistance,
                capDistance)) {
            closest = std::min(closest, capDistance);
        }
        if (closest == (std::numeric_limits<float>::max)())
            return false;
    }
    if (hitDistance)
        *hitDistance = closest;
    return true;
}

bool target::ballistics::IsPointInsideCapsule(
    const Vector3& point,
    const esp::HitboxCapsule& capsule,
    float tolerance) noexcept
{
    if (!capsule.valid || !IsFiniteVec(point) ||
        !IsFiniteVec(capsule.start) || !IsFiniteVec(capsule.end) ||
        !std::isfinite(capsule.radius) || capsule.radius <= 0.0f) {
        return false;
    }
    if (!std::isfinite(tolerance))
        tolerance = 0.0f;
    const float safeRadius = capsule.radius + std::max(tolerance, 0.0f);
    return PointSegmentDistanceSquared(
        point,
        capsule.start,
        capsule.end) <= safeRadius * safeRadius;
}

target::ballistics::CapsuleMultipoints
target::ballistics::GenerateCapsuleMultipoints(
    const esp::HitboxCapsule& capsule,
    const Vector3& eyePosition,
    const WeaponSpread& weapon,
    float requestedScale) noexcept
{
    CapsuleMultipoints result;
    if (!capsule.valid || !IsFiniteVec(capsule.start) ||
        !IsFiniteVec(capsule.end) || !IsFiniteVec(eyePosition) ||
        !std::isfinite(capsule.radius) || capsule.radius <= 0.0f ||
        !std::isfinite(weapon.inaccuracy) || weapon.inaccuracy < 0.0f ||
        !std::isfinite(weapon.spread) || weapon.spread < 0.0f ||
        !std::isfinite(requestedScale)) {
        return result;
    }

    const Vector3 axis = capsule.end - capsule.start;
    const float axisLengthSquared = Dot(axis, axis);
    if (!IsFiniteVec(axis) || !std::isfinite(axisLengthSquared))
        return result;
    const float axisLength = std::sqrt(std::max(0.0f, axisLengthSquared));
    const Vector3 center = capsule.start + axis * 0.5f;
    const Vector3 eyeDelta = center - eyePosition;
    if (!IsFiniteVec(center) || !IsFiniteVec(eyeDelta))
        return result;
    result.distance = eyeDelta.Length();
    if (!std::isfinite(result.distance))
        return result;

    // Spread offsets are tangent-space slopes. Their sum is the conservative
    // outer envelope used here; converting it at target distance makes outer
    // points move toward the capsule axis as range or weapon uncertainty
    // grows. This scale affects point generation only -- hitchance still uses
    // the complete deterministic 256-seed model.
    const float spreadEnvelope = std::clamp(
        weapon.inaccuracy + weapon.spread,
        0.0f,
        0.5f);
    result.projectedSpreadRadius = result.distance * spreadEnvelope;
    const float uncertaintyRatio = result.projectedSpreadRadius /
        std::max(capsule.radius, 0.001f);
    const bool head = capsule.hitgroup == 1;
    const float hitgroupScale = head ? 0.72f : 0.62f;
    const float accuracyScale = 1.0f / (1.0f + uncertaintyRatio * 0.28f);
    result.appliedScale = std::clamp(requestedScale, 0.0f, 1.0f) *
        hitgroupScale * accuracyScale;

    const auto append = [&](const Vector3& position, MultipointRole role) {
        if (result.count >= kMaximumCapsuleMultipoints ||
            !IsPointInsideCapsule(position, capsule, 0.0f)) {
            return;
        }
        for (int index = 0; index < result.count; ++index) {
            const Vector3 difference =
                result.points[index].position - position;
            if (Dot(difference, difference) < 0.000001f)
                return;
        }
        CapsuleAimPoint& point = result.points[result.count++];
        point.position = position;
        point.role = role;
        if (role == MultipointRole::Center ||
            role == MultipointRole::AxisStart ||
            role == MultipointRole::AxisEnd) {
            point.normalizedRadialOffset = 0.0f;
        } else {
            point.normalizedRadialOffset = std::clamp(
                std::sqrt(std::max(
                    0.0f,
                    PointSegmentDistanceSquared(
                        position,
                        capsule.start,
                        capsule.end))) / capsule.radius,
                0.0f,
                1.0f);
        }
    };

    append(center, MultipointRole::Center);
    if (axisLength > 0.001f) {
        // Interior axis points remain valid even when radial scale collapses
        // because of poor accuracy. They also cover long, rotated body boxes
        // without aiming at either spherical end cap.
        append(capsule.start + axis * 0.30f, MultipointRole::AxisStart);
        append(capsule.start + axis * 0.70f, MultipointRole::AxisEnd);
    }

    const float radialDistance = capsule.radius * result.appliedScale;
    if (radialDistance < 0.0001f)
        return result;

    Vector3 top = {};
    Vector3 side = {};
    const Vector3 worldUp = {0.0f, 0.0f, 1.0f};
    if (axisLength > 0.001f) {
        const Vector3 axisDirection = axis * (1.0f / axisLength);
        top = Normalize(worldUp - axisDirection * Dot(worldUp, axisDirection));
        if (Dot(top, top) < 0.5f) {
            const Vector3 view = Normalize(center - eyePosition);
            top = Normalize(view - axisDirection * Dot(view, axisDirection));
        }
        if (Dot(top, top) < 0.5f) {
            const Vector3 fallback = std::fabs(axisDirection.x) < 0.8f
                ? Vector3{1.0f, 0.0f, 0.0f}
                : Vector3{0.0f, 1.0f, 0.0f};
            top = Normalize(
                fallback - axisDirection * Dot(fallback, axisDirection));
        }
        side = Normalize(Cross(axisDirection, top));
    } else {
        Vector3 view = Normalize(center - eyePosition);
        if (Dot(view, view) < 0.5f)
            view = {1.0f, 0.0f, 0.0f};
        top = Normalize(worldUp - view * Dot(worldUp, view));
        if (Dot(top, top) < 0.5f)
            top = {0.0f, 1.0f, 0.0f};
        side = Normalize(Cross(view, top));
    }
    if (Dot(top, top) < 0.5f || Dot(side, side) < 0.5f)
        return result;

    const float topFactor = head ? 0.82f : 0.62f;
    const float bottomFactor = head ? 0.52f : 0.62f;
    append(center + top * (radialDistance * topFactor),
        MultipointRole::Top);
    append(center - top * (radialDistance * bottomFactor),
        MultipointRole::Bottom);
    append(center - side * radialDistance, MultipointRole::Left);
    append(center + side * radialDistance, MultipointRole::Right);

    const Vector3 topLeft = Normalize(top - side);
    const Vector3 topRight = Normalize(top + side);
    const float diagonalDistance = radialDistance * (head ? 0.78f : 0.64f);
    append(center + topLeft * diagonalDistance, MultipointRole::TopLeft);
    append(center + topRight * diagonalDistance, MultipointRole::TopRight);
    return result;
}

bool target::ballistics::TracePlayerCapsules(
    const Vector3& origin,
    const Vector3& direction,
    const esp::PlayerData& player,
    float maximumDistance,
    int requiredHitgroup,
    CapsuleHit* output) noexcept
{
    CapsuleHit result;
    float nearest = maximumDistance;
    if (!player.hasHitboxes || player.hitboxCount == 0) {
        if (output)
            *output = result;
        return false;
    }
    const int count = std::min<int>(
        player.hitboxCount,
        esp::kMaximumPlayerHitboxes);
    for (int index = 0; index < count; ++index) {
        const esp::HitboxCapsule& capsule = player.hitboxes[index];
        if (!capsule.valid || capsule.radius <= 0.0f ||
            (requiredHitgroup > 0 && capsule.hitgroup != requiredHitgroup)) {
            continue;
        }
        float distance = 0.0f;
        if (!RayCapsuleIntersection(
                origin,
                direction,
                capsule.start,
                capsule.end,
                capsule.radius,
                nearest,
                &distance)) {
            continue;
        }
        nearest = distance;
        result.hit = true;
        result.distance = distance;
        result.hitbox = capsule.index;
        result.hitgroup = capsule.hitgroup;
        result.position = origin + Normalize(direction) * distance;
    }
    if (output)
        *output = result;
    return result.hit;
}

float target::ballistics::CalculateHitchance(
    const Vector3& eyePosition,
    const Vector3& aimAngles,
    const esp::PlayerData& player,
    const WeaponSpread& weapon,
    int requiredHitgroup) noexcept
{
    if (!player.hasHitboxes || !IsFiniteVec(eyePosition) ||
        !IsFiniteVec(aimAngles) || !std::isfinite(weapon.inaccuracy) ||
        !std::isfinite(weapon.spread) || weapon.inaccuracy < 0.0f ||
        weapon.spread < 0.0f || weapon.range <= 0.0f) {
        return 0.0f;
    }
    Vector3 forward = {};
    Vector3 right = {};
    Vector3 up = {};
    AnglesToDirections(aimAngles, forward, right, up);
    if (weapon.inaccuracy + weapon.spread < 0.000001f) {
        return TracePlayerCapsules(
            eyePosition,
            forward,
            player,
            weapon.range,
            requiredHitgroup)
            ? 1.0f
            : 0.0f;
    }

    int hits = 0;
    for (int seed = 0; seed < kHitchanceSamples; ++seed) {
        const SpreadOffset spread = CalculateSpread(seed, weapon);
        const Vector3 direction = Normalize(
            forward + right * spread.x + up * spread.y);
        if (TracePlayerCapsules(
                eyePosition,
                direction,
                player,
                weapon.range,
                requiredHitgroup)) {
            ++hits;
        }
    }
    return static_cast<float>(hits) /
        static_cast<float>(kHitchanceSamples);
}

uint32_t target::ballistics::CalculateSpreadSeed(
    const Vector3& aimAngles,
    int renderTick) noexcept
{
    if (!IsFiniteVec(aimAngles) || renderTick < 0)
        return 0u;
    const auto normalizeAngle = [](float angle) {
        return angle - std::floor(angle * (1.0f / 360.0f) + 0.5f) *
            360.0f;
    };
    const auto quantizeAngle = [&](float angle) {
        return std::floor(normalizeAngle(angle) * 2.0f) * 0.5f;
    };
    struct SeedInput
    {
        float pitch;
        float yaw;
        int playerRenderTick;
    } input = {
        quantizeAngle(aimAngles.x),
        quantizeAngle(aimAngles.y),
        renderTick,
    };
    static_assert(sizeof(SeedInput) == 12u);
    Sha1 hash;
    hash.Reset();
    hash.Update(&input, sizeof(input));
    hash.Final();
    return hash.FirstUint32();
}

bool target::ballistics::ResolveSpreadDirection(
    const Vector3& aimAngles,
    const WeaponSpread& weapon,
    uint32_t spreadSeed,
    Vector3& direction) noexcept
{
    direction = {};
    if (!IsFiniteVec(aimAngles) || !std::isfinite(weapon.inaccuracy) ||
        !std::isfinite(weapon.spread) || weapon.inaccuracy < 0.0f ||
        weapon.spread < 0.0f) {
        return false;
    }
    Vector3 forward = {};
    Vector3 right = {};
    Vector3 up = {};
    AnglesToDirections(aimAngles, forward, right, up);
    int32_t signedSeed = 0;
    static_assert(sizeof(signedSeed) == sizeof(spreadSeed));
    std::memcpy(&signedSeed, &spreadSeed, sizeof(signedSeed));
    const SpreadOffset offset = CalculateSpread(signedSeed, weapon);
    direction = Normalize(forward + right * offset.x + up * offset.y);
    return IsFiniteVec(direction) && Dot(direction, direction) > 0.5f;
}

bool target::ballistics::TraceSpreadSeed(
    const Vector3& eyePosition,
    const Vector3& aimAngles,
    const esp::PlayerData& player,
    const WeaponSpread& weapon,
    uint32_t spreadSeed,
    int requiredHitgroup) noexcept
{
    if (!player.hasHitboxes || !IsFiniteVec(eyePosition) ||
        !IsFiniteVec(aimAngles) || !std::isfinite(weapon.inaccuracy) ||
        !std::isfinite(weapon.spread) || weapon.inaccuracy < 0.0f ||
        weapon.spread < 0.0f || weapon.range <= 0.0f) {
        return false;
    }
    Vector3 direction = {};
    if (!ResolveSpreadDirection(
            aimAngles,
            weapon,
            spreadSeed,
            direction)) {
        return false;
    }
    return TracePlayerCapsules(
        eyePosition,
        direction,
        player,
        weapon.range,
        requiredHitgroup);
}

target::ballistics::SeedTickSelection
target::ballistics::SelectSpreadSeedTick(
    const Vector3& eyePosition,
    const Vector3& aimAngles,
    const esp::PlayerData& player,
    const WeaponSpread& weapon,
    int earliestDeliveryTick,
    int renderTickCount,
    int requiredHitgroup) noexcept
{
    SeedTickSelection result;
    if (earliestDeliveryTick < 0 || renderTickCount <= 0)
        return result;
    result.earliestDeliveryTick = earliestDeliveryTick;
    renderTickCount = std::clamp(
        renderTickCount,
        1,
        kMaximumSeedWindowTicks);

    for (int offset = 0; offset < renderTickCount; ++offset) {
        if (earliestDeliveryTick >
            (std::numeric_limits<int>::max)() - offset) {
            break;
        }
        SeedTickCandidate& candidate = result.candidates[result.tested];
        candidate.renderTick = earliestDeliveryTick + offset;
        candidate.seed = CalculateSpreadSeed(
            aimAngles,
            candidate.renderTick);
        candidate.hit = TraceSpreadSeedDetailed(
            eyePosition,
            aimAngles,
            player,
            weapon,
            candidate.seed,
            requiredHitgroup,
            candidate.geometricSafety);
        ++result.tested;
        if (!candidate.hit)
            continue;
        ++result.hits;
        if (result.earliestHitTick < 0)
            result.earliestHitTick = candidate.renderTick;

        // The selected tick maximizes how deeply its ray crosses a capsule.
        // Equal margins deliberately resolve to the earliest deliverable tick
        // to avoid adding latency for no geometric benefit.
        if (result.selectedRenderTick < 0 ||
            candidate.geometricSafety >
                result.selectedGeometricSafety + 0.000001f) {
            result.selectedRenderTick = candidate.renderTick;
            result.selectedSeed = candidate.seed;
            result.selectedGeometricSafety = candidate.geometricSafety;
        }
    }
    return result;
}

target::ballistics::SeedWindowResult
target::ballistics::TraceSpreadSeedWindow(
    const Vector3& eyePosition,
    const Vector3& aimAngles,
    const esp::PlayerData& player,
    const WeaponSpread& weapon,
    int firstRenderTick,
    int renderTickCount,
    int requiredHitgroup) noexcept
{
    const SeedTickSelection selection = SelectSpreadSeedTick(
        eyePosition,
        aimAngles,
        player,
        weapon,
        firstRenderTick,
        renderTickCount,
        requiredHitgroup);
    return {selection.tested, selection.hits};
}

target::ballistics::MultipointTickSelection
target::ballistics::SelectMultipointSpreadTick(
    const Vector3& eyePosition,
    const esp::PlayerData& player,
    const WeaponSpread& weapon,
    const CapsuleMultipoints& multipoints,
    int earliestDeliveryTick,
    int renderTickCount,
    int requiredHitgroup) noexcept
{
    MultipointTickSelection result;
    if (!IsFiniteVec(eyePosition) || multipoints.count <= 0 ||
        earliestDeliveryTick < 0 || renderTickCount <= 0) {
        return result;
    }
    const int pointCount = std::clamp(
        multipoints.count,
        0,
        kMaximumCapsuleMultipoints);
    float bestConfidence = -1.0f;
    for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex) {
        const Vector3 point = multipoints.points[pointIndex].position;
        const Vector3 delta = point - eyePosition;
        const float pointDistanceSquared = Dot(delta, delta);
        if (!IsFiniteVec(point) || !IsFiniteVec(delta) ||
            !std::isfinite(pointDistanceSquared) ||
            pointDistanceSquared < 0.000001f) {
            continue;
        }
        const Vector3 aimAngles = PointToAngles(eyePosition, point);
        const SeedTickSelection selection = SelectSpreadSeedTick(
            eyePosition,
            aimAngles,
            player,
            weapon,
            earliestDeliveryTick,
            renderTickCount,
            requiredHitgroup);
        if (!selection.HasSelection())
            continue;
        const float confidence = selection.Confidence();
        const bool betterConfidence = confidence > bestConfidence + 0.000001f;
        const bool equalConfidence =
            std::fabs(confidence - bestConfidence) <= 0.000001f;
        const bool betterSafety = !result.valid ||
            selection.selectedGeometricSafety >
                result.seed.selectedGeometricSafety + 0.000001f;
        const bool equalSafety = result.valid &&
            std::fabs(selection.selectedGeometricSafety -
                result.seed.selectedGeometricSafety) <= 0.000001f;
        const bool earlier = !result.valid ||
            selection.selectedRenderTick < result.seed.selectedRenderTick;
        if (!betterConfidence &&
            !(equalConfidence && (betterSafety ||
                (equalSafety && earlier)))) {
            continue;
        }
        result.valid = true;
        result.pointIndex = pointIndex;
        result.point = point;
        result.aimAngles = aimAngles;
        result.seed = selection;
        bestConfidence = confidence;
    }
    return result;
}

float target::ballistics::ScaleDamage(
    float damage,
    int hitgroup,
    int armor,
    bool hasHelmet,
    int targetTeam,
    float armorRatio,
    float headshotMultiplier,
    float ctHeadScale,
    float tHeadScale,
    float ctBodyScale,
    float tBodyScale) noexcept
{
    if (!std::isfinite(damage) || damage <= 0.0f)
        return 0.0f;
    const bool counterTerrorist = targetTeam == 3;
    const float headScale = counterTerrorist ? ctHeadScale : tHeadScale;
    const float bodyScale = counterTerrorist ? ctBodyScale : tBodyScale;
    switch (hitgroup) {
    case 1: damage *= headshotMultiplier * headScale; break;
    case 3: damage *= 1.25f * bodyScale; break;
    case 6:
    case 7: damage *= 0.75f * bodyScale; break;
    case 2:
    case 4:
    case 5:
    case 8: damage *= bodyScale; break;
    default: break;
    }

    const bool armoredHitgroup =
        (hitgroup >= 1 && hitgroup <= 5) || hitgroup == 8;
    if (armor <= 0 || !armoredHitgroup ||
        (hitgroup == 1 && !hasHelmet)) {
        return std::floor(std::max(0.0f, damage));
    }
    const float healthDamage = damage * armorRatio * 0.5f;
    const float armorDamage = (damage - healthDamage) * 0.5f;
    const float adjusted = armorDamage > static_cast<float>(armor)
        ? damage - static_cast<float>(armor) / 0.5f
        : healthDamage;
    return std::floor(std::max(0.0f, adjusted));
}
