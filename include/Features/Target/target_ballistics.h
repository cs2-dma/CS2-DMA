#pragma once

#include "Features/ESP/esp.h"

#include <array>
#include <cstdint>

namespace target::ballistics
{
    inline constexpr int kHitchanceSamples = 256;
    inline constexpr int kMaximumCapsuleMultipoints = 9;
    inline constexpr int kMaximumSeedWindowTicks = 8;

    struct CapsuleHit
    {
        bool hit = false;
        float distance = 0.0f;
        int hitbox = -1;
        int hitgroup = 0;
        Vector3 position = {};
    };

    struct WeaponSpread
    {
        float inaccuracy = 0.0f;
        float spread = 0.0f;
        float recoilIndex = 0.0f;
        uint16_t itemDefinitionIndex = 0;
        int bullets = 1;
        float range = 8192.0f;
    };

    struct WeaponDamage
    {
        float damage = 0.0f;
        float penetration = 0.0f;
        float range = 0.0f;
        float rangeModifier = 1.0f;
        float armorRatio = 1.0f;
        float headshotMultiplier = 4.0f;
    };

    enum class MultipointRole : uint8_t
    {
        Center,
        AxisStart,
        AxisEnd,
        Top,
        Bottom,
        Left,
        Right,
        TopLeft,
        TopRight,
    };

    struct CapsuleAimPoint
    {
        Vector3 position = {};
        MultipointRole role = MultipointRole::Center;
        // Distance from the capsule axis divided by its radius. This is kept
        // below one by GenerateCapsuleMultipoints so a point never relies on
        // floating-point contact with the outer surface.
        float normalizedRadialOffset = 0.0f;
    };

    struct CapsuleMultipoints
    {
        std::array<CapsuleAimPoint, kMaximumCapsuleMultipoints> points = {};
        int count = 0;
        // Final radial scale after the hitgroup, distance and weapon accuracy
        // safety margins have been applied.
        float appliedScale = 0.0f;
        float distance = 0.0f;
        float projectedSpreadRadius = 0.0f;
    };

    struct DamageResult
    {
        bool hit = false;
        bool penetrated = false;
        float damage = 0.0f;
        int hitbox = -1;
        int hitgroup = 0;
    };

    struct SeedWindowResult
    {
        int tested = 0;
        int hits = 0;

        float Fraction() const noexcept
        {
            return tested > 0
                ? static_cast<float>(hits) / static_cast<float>(tested)
                : 0.0f;
        }
    };

    struct SeedTickCandidate
    {
        int renderTick = -1;
        uint32_t seed = 0;
        bool hit = false;
        // 0 is a tangent hit, 1 is a ray through the capsule axis. This is a
        // geometric margin, not a probability that input delivery is atomic.
        float geometricSafety = 0.0f;
    };

    struct SeedTickSelection
    {
        std::array<SeedTickCandidate, kMaximumSeedWindowTicks> candidates = {};
        int tested = 0;
        int hits = 0;
        int earliestDeliveryTick = -1;
        int earliestHitTick = -1;
        int selectedRenderTick = -1;
        uint32_t selectedSeed = 0;
        float selectedGeometricSafety = 0.0f;

        bool HasSelection() const noexcept
        {
            return selectedRenderTick >= earliestDeliveryTick && hits > 0;
        }

        // Honest confidence for an uncertain delivery window: the fraction
        // of tested future ticks whose deterministic seed intersects a valid
        // capsule. It deliberately does not claim that an external click can
        // be delivered atomically on selectedRenderTick.
        float Confidence() const noexcept
        {
            return tested > 0
                ? static_cast<float>(hits) / static_cast<float>(tested)
                : 0.0f;
        }
    };

    struct MultipointTickSelection
    {
        bool valid = false;
        int pointIndex = -1;
        Vector3 point = {};
        Vector3 aimAngles = {};
        SeedTickSelection seed = {};
    };

    Vector3 Normalize(const Vector3& value) noexcept;
    float Dot(const Vector3& first, const Vector3& second) noexcept;
    void AnglesToDirections(
        const Vector3& angles,
        Vector3& forward,
        Vector3& right,
        Vector3& up) noexcept;
    bool RayCapsuleIntersection(
        const Vector3& origin,
        const Vector3& direction,
        const Vector3& capsuleStart,
        const Vector3& capsuleEnd,
        float radius,
        float maximumDistance,
        float* hitDistance = nullptr) noexcept;
    bool IsPointInsideCapsule(
        const Vector3& point,
        const esp::HitboxCapsule& capsule,
        float tolerance = 0.0001f) noexcept;
    CapsuleMultipoints GenerateCapsuleMultipoints(
        const esp::HitboxCapsule& capsule,
        const Vector3& eyePosition,
        const WeaponSpread& weapon,
        float requestedScale = 1.0f) noexcept;
    bool TracePlayerCapsules(
        const Vector3& origin,
        const Vector3& direction,
        const esp::PlayerData& player,
        float maximumDistance,
        int requiredHitgroup,
        CapsuleHit* result = nullptr) noexcept;
    float CalculateHitchance(
        const Vector3& eyePosition,
        const Vector3& aimAngles,
        const esp::PlayerData& player,
        const WeaponSpread& weapon,
        int requiredHitgroup = 0) noexcept;
    uint32_t CalculateSpreadSeed(
        const Vector3& aimAngles,
        int renderTick) noexcept;
    bool ResolveSpreadDirection(
        const Vector3& aimAngles,
        const WeaponSpread& weapon,
        uint32_t spreadSeed,
        Vector3& direction) noexcept;
    bool TraceSpreadSeed(
        const Vector3& eyePosition,
        const Vector3& aimAngles,
        const esp::PlayerData& player,
        const WeaponSpread& weapon,
        uint32_t spreadSeed,
        int requiredHitgroup = 0) noexcept;
    SeedTickSelection SelectSpreadSeedTick(
        const Vector3& eyePosition,
        const Vector3& aimAngles,
        const esp::PlayerData& player,
        const WeaponSpread& weapon,
        int earliestDeliveryTick,
        int renderTickCount,
        int requiredHitgroup = 0) noexcept;
    SeedWindowResult TraceSpreadSeedWindow(
        const Vector3& eyePosition,
        const Vector3& aimAngles,
        const esp::PlayerData& player,
        const WeaponSpread& weapon,
        int firstRenderTick,
        int renderTickCount,
        int requiredHitgroup = 0) noexcept;
    MultipointTickSelection SelectMultipointSpreadTick(
        const Vector3& eyePosition,
        const esp::PlayerData& player,
        const WeaponSpread& weapon,
        const CapsuleMultipoints& multipoints,
        int earliestDeliveryTick,
        int renderTickCount,
        int requiredHitgroup = 0) noexcept;
    float ScaleDamage(
        float damage,
        int hitgroup,
        int armor,
        bool hasHelmet,
        int targetTeam,
        float armorRatio,
        float headshotMultiplier,
        float ctHeadScale = 1.0f,
        float tHeadScale = 1.0f,
        float ctBodyScale = 1.0f,
        float tBodyScale = 1.0f) noexcept;
}
