#include "Features/Target/target.h"

#include "Features/ESP/esp.h"
#include "Features/ESP/weapon_catalog.h"
#include "Features/Target/physics_bvh.h"
#include "Features/Target/target_ballistics.h"
#include "Features/Target/target_convars.h"
#include "Features/Target/target_policy.h"
#include "app/Core/globals.h"
#include "app/Input/input_device.h"
#include "app/Input/primary_keyboard.h"
#include "vendor/DMALibrary/pch.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <mutex>
#include <random>
#include <thread>
#include <vector>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr auto kTriggerHoldTime = std::chrono::milliseconds(18);
    constexpr auto kTriggerMinimumStableTime = std::chrono::milliseconds(12);
    constexpr auto kTriggerRetryBackoff = std::chrono::milliseconds(25);
    constexpr auto kRuntimeTickInterval = std::chrono::microseconds(7812);
    constexpr uint64_t kMaximumTargetSnapshotAgeUs = 100000u;

    struct WeaponControlProfile
    {
        float recoilPitchScale = 2.0f;
        float recoilYawScale = 2.0f;
        float maximumPrecisionSpeed = 30.0f;
        uint8_t triggerStableTicks = 3;
        std::chrono::milliseconds shotCooldown{95};
        std::chrono::milliseconds accuracySettleTime{0};
    };

    struct Candidate
    {
        const esp::PlayerData* player = nullptr;
        Vector3 point = {};
        float screenDistance = (std::numeric_limits<float>::max)();
        int slot = -1;
        int requiredHitgroup = 0;
        Vector3 predictionOffset = {};
    };

    bool ResolvePlannedAimShot(
        const esp::TargetSnapshot& snapshot,
        Candidate& candidate,
        float leadSeconds,
        bool visibleOnly,
        float minimumDamage,
        bool autowall,
        const target::convars::Values& convars,
        float screenWidth, float screenHeight, float radius);

    target::policy::ActivationState s_aimActivation;
    target::policy::ActivationState s_triggerActivation;
    bool s_triggerActivationShotIssued = false;
    uint64_t s_activationScene = 0;
    uintptr_t s_activationPawn = 0;

    struct MotionRuntimeState
    {
        float mouseRemainderX = 0.0f;
        float mouseRemainderY = 0.0f;
        uintptr_t targetPawn = 0;
        uint64_t lastStepAtUs = 0;
        uint64_t lastViewIssuedAtUs = 0;
        float humanSmoothNoise = 0.0f;
        float humanPitchNoise = 0.0f;
        float humanYawNoise = 0.0f;
        float humanInitialDistance = 0.0f;
        float humanCurveStrength = 0.0f;
        float humanCurveSign = 1.0f;
        uint64_t humanNoiseUpdatedAtUs = 0;
    };

    struct AimRuntimeState : MotionRuntimeState
    {
    };

    struct TriggerRuntimeState
    {
        bool waiting = false;
        bool held = false;
        bool clickReserved = false;
        bool shotLatched = false;
        bool shotObserved = false;
        bool requireCrosshairExit = false;
        uintptr_t targetPawn = 0;
        uintptr_t alignedPawn = 0;
        uintptr_t latchedPawn = 0;
        int latchedSlot = -1;
        uint32_t latchedNameHash = 0;
        uint32_t latchedPawnHandle = 0;
        int latchedHealthBefore = 0;
        uint64_t latchedCoreUpdatedAtBefore = 0;
        uint64_t lastPostShotCoreUpdatedAtUs = 0;
        uint64_t lastMissingSnapshotGeneration = 0;
        uint8_t postShotCoreSamples = 0;
        uint8_t missingTargetSamples = 0;
        float predictedDamage = 0.0f;
        bool predictedLethal = false;
        uintptr_t blockedUnobservedPawn = 0;
        bool blockedAfterConfirmedDeath = false;
        uint64_t blockedPawnCoreUpdatedAtUs = 0;
        uint64_t blockedRevivalCoreUpdatedAtUs = 0;
        uint8_t blockedRevivalSamples = 0;
        uintptr_t clickSeriesPawn = 0;
        uint32_t clickSeriesNameHash = 0;
        uint8_t unobservedRetryCount = 0;
        uint8_t alignedTicks = 0;
        uint64_t lastCrosshairGeneration = 0;
        uint64_t movementIssuedGeneration = 0;
        uint64_t lastLatchCrosshairGeneration = 0;
        uint8_t crosshairExitSamples = 0;
        int ammoBefore = -1;
        uint64_t ammoUpdatedAtBefore = 0;
        int shotsBefore = 0;
        bool shotsBeforeValid = false;
        uint64_t shotsUpdatedAtBefore = 0;
        float lastShotTimeBefore = 0.0f;
        bool lastShotTimeBeforeValid = false;
        uint64_t weaponTelemetryUpdatedAtBefore = 0;
        Vector3 previousPostShotAimPunch = {};
        uint64_t previousPostShotAimPunchUpdatedAtUs = 0;
        uint8_t stablePostShotRecoilSamples = 0;
        MotionRuntimeState assist = {};
        std::chrono::steady_clock::time_point triggerAt = {};
        std::chrono::steady_clock::time_point stableSince = {};
        std::chrono::steady_clock::time_point releaseAt = {};
        std::chrono::steady_clock::time_point nextAutoShotAt = {};
        std::chrono::steady_clock::time_point triggerShotIssuedAt = {};
        std::chrono::steady_clock::time_point shotObservationDeadline = {};
        std::chrono::steady_clock::time_point targetOutcomeNotBefore = {};
        std::chrono::steady_clock::time_point accuracyStableSince = {};
    };

    struct RuntimeState
    {
        bool contextInitialized = false;
        uint64_t sceneSerial = 0;
        uintptr_t localPawn = 0;
        uint16_t localWeaponId = 0;
        uint32_t localWeaponHandle = 0;
        uintptr_t localWeaponEntity = 0;
        int observedShotsFired = -1;
        AimRuntimeState aim = {};
        TriggerRuntimeState trigger = {};
    };

    RuntimeState s_runtime;
    target::RuntimeStatus s_status;
    target::SelectionDiagnostics s_aimSelection;
    target::SelectionDiagnostics s_triggerSelection;
    target::ShotDiagnostics s_shotDiagnostics;
    target::FireDiagnostics s_fireDiagnostics;
    target::RuntimeStatus s_publishedStatus;
    std::mutex s_statusMutex;
    std::mutex s_runtimeMutex;
    std::mutex s_workerMutex;
    std::condition_variable s_workerCondition;
    std::thread s_worker;
    bool s_workerStarted = false;
    bool s_workerStopping = false;
    float s_viewportWidth = 0.0f;
    float s_viewportHeight = 0.0f;
    std::atomic<int> s_activeWeaponProfile{0};

    void PublishCompletedStatus()
    {
        // Copy only: the UI must never wait for ballistics, DMA or input I/O.
        std::lock_guard<std::mutex> lock(s_statusMutex);
        s_publishedStatus = s_status;
    }

    uint64_t NowUs()
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    void PublishStatus(
        target::RuntimePhase phase,
        bool aimKeyDown = false,
        bool triggerKeyDown = false,
        const Candidate* candidate = nullptr,
        int moveX = 0,
        int moveY = 0)
    {
        s_status = {};
        s_status.phase = phase;
        s_status.aimKeyDown = aimKeyDown;
        s_status.triggerKeyDown = triggerKeyDown;
        s_status.updatedAtUs = NowUs();
        s_status.aimSelection = s_aimSelection;
        s_status.triggerSelection = s_triggerSelection;
        s_status.shot = s_shotDiagnostics;
        s_status.fire = s_fireDiagnostics;
        s_status.moveX = moveX;
        s_status.moveY = moveY;
        if (candidate) {
            s_status.targetSlot = candidate->slot;
            s_status.targetDistancePx = candidate->screenDistance;
        }
    }

    bool IsUsablePoint(const Vector3& point)
    {
        return IsFiniteVec(point) &&
               (std::fabs(point.x) + std::fabs(point.y) +
                std::fabs(point.z)) > 1.0f;
    }

    Vector3 PlayerBone(const esp::PlayerData& player, int boneId)
    {
        const int index = esp::PlayerStoredBoneIndex(boneId);
        if (index < 0 || index >= esp::kPlayerStoredBoneCount)
            return {};
        return player.bones[index];
    }

    bool IsTargetVisible(
        const esp::TargetSnapshot& snapshot,
        const esp::PlayerData& player,
        const Vector3& targetPoint)
    {
        // Workshop maps are often unavailable to the remote BVH builder. The
        // live spotted/visibility sample remains authoritative when geometry
        // has not been built for such a map.
        if (player.visible && target::policy::IsFreshSample(
                player.visibilityUpdatedAtUs, snapshot.sampledAtUs,
                target::policy::kMaximumTriggerTargetAgeUs))
            return true;
        if (!IsUsablePoint(snapshot.localEyePos) ||
            !IsUsablePoint(targetPoint)) {
            return false;
        }
        return target::physics::IsLineVisible(
            snapshot.mapKey,
            snapshot.localEyePos,
            targetPoint);
    }

    bool IsPlayerCoreFresh(
        const esp::TargetSnapshot& snapshot,
        const esp::PlayerData& player)
    {
        return target::policy::IsFreshSample(
            player.coreUpdatedAtUs,
            snapshot.sampledAtUs,
            target::policy::kMaximumPlayerCoreAgeUs);
    }

    bool ArePlayerBonesFresh(
        const esp::TargetSnapshot& snapshot,
        const esp::PlayerData& player)
    {
        return player.hasBones && target::policy::IsFreshSample(
            player.bonesUpdatedAtUs,
            snapshot.sampledAtUs,
            target::policy::kMaximumPlayerBoneAgeUs);
    }

    int ConfiguredBoneId(int aimBone)
    {
        switch (target::policy::SanitizeAimBone(aimBone)) {
        case 1: return esp::NECK;
        case 2: return esp::SPINE2;
        case 3: return esp::PELVIS;
        default: return esp::HEAD;
        }
    }

    Vector3 SelectNamedAimPoint(
        const esp::PlayerData& player,
        int aimBone)
    {
        const int sanitized = target::policy::SanitizeAimBone(aimBone);
        const int desiredHitgroup = sanitized == 0
            ? 1
            : (sanitized == 2 ? 2 : (sanitized == 3 ? 3 : 0));
        if (desiredHitgroup != 0 && player.hasHitboxes) {
            const esp::HitboxCapsule* fallback = nullptr;
            const int count = std::min<int>(
                player.hitboxCount,
                esp::kMaximumPlayerHitboxes);
            for (int index = 0; index < count; ++index) {
                const esp::HitboxCapsule& capsule = player.hitboxes[index];
                if (!capsule.valid || capsule.hitgroup != desiredHitgroup ||
                    !IsUsablePoint(capsule.center)) {
                    continue;
                }
                if (!fallback)
                    fallback = &capsule;
                if ((desiredHitgroup == 1 && capsule.index == 0) ||
                    (desiredHitgroup == 2 && capsule.index == 4) ||
                    (desiredHitgroup == 3 && capsule.index == 6)) {
                    return capsule.center;
                }
            }
            if (fallback)
                return fallback->center;
        }
        // Neck remains an explicit transform because Source's hitbox set has
        // no separate neck hitgroup. Named body points otherwise use the true
        // rotated capsule center and only fall back when model data is absent.
        return PlayerBone(player, ConfiguredBoneId(sanitized));
    }

    int RequiredHitgroupForAimPoint(int aimBone)
    {
        switch (target::policy::SanitizeAimBone(aimBone)) {
        case 0: return 1;
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        default: return 0;
        }
    }

    Vector3 PredictAimPoint(
        const esp::PlayerData& player,
        Vector3 point,
        float leadSeconds)
    {
        if (!player.velocityValid || leadSeconds <= 0.0f)
            return point;
        // The horizon is sampled age + ping/2 + interpolation. Horizontal
        // movement is fully projected. Airborne Z follows Source gravity and
        // is capped to the capsule scale, preventing the old above-head lead.
        float verticalLead = 0.0f;
        if (std::fabs(player.velocity.z) > 20.0f) {
            constexpr float kSourceGravity = 800.0f;
            verticalLead = player.velocity.z * leadSeconds -
                0.5f * kSourceGravity * leadSeconds * leadSeconds;
            verticalLead = std::clamp(verticalLead, -12.0f, 12.0f);
        }
        return point + Vector3(
            player.velocity.x * leadSeconds,
            player.velocity.y * leadSeconds,
            verticalLead);
    }

    Vector3 SelectAimPoint(
        const esp::PlayerData& player,
        int aimBone,
        const view_matrix_t& matrix,
        float screenWidth,
        float screenHeight,
        float leadSeconds)
    {
        const auto predicted = [&](Vector3 point) {
            return PredictAimPoint(player, point, leadSeconds);
        };

        if (target::policy::SanitizeAimBone(aimBone) == 4) {
            if (player.hasHitboxes) {
                const float centerX = screenWidth * 0.5f;
                const float centerY = screenHeight * 0.5f;
                Vector3 best = {};
                float bestDistanceSquared =
                    (std::numeric_limits<float>::max)();
                const int count = std::min<int>(
                    player.hitboxCount,
                    esp::kMaximumPlayerHitboxes);
                for (int index = 0; index < count; ++index) {
                    const esp::HitboxCapsule& capsule = player.hitboxes[index];
                    if (!capsule.valid)
                        continue;
                    const Vector3 point = predicted(capsule.center);
                    const ScreenPos screen = WorldToScreen(
                        point,
                        matrix,
                        screenWidth,
                        screenHeight);
                    if (!screen.onScreen)
                        continue;
                    const float dx = screen.x - centerX;
                    const float dy = screen.y - centerY;
                    const float distanceSquared = dx * dx + dy * dy;
                    if (distanceSquared < bestDistanceSquared) {
                        bestDistanceSquared = distanceSquared;
                        best = point;
                    }
                }
                if (IsUsablePoint(best))
                    return best;
            }
            struct BoneSegment
            {
                int first;
                int second;
            };
            constexpr std::array<BoneSegment, 16> kBodySegments = {{
                {esp::PELVIS, esp::SPINE1},
                {esp::SPINE1, esp::SPINE2},
                {esp::SPINE2, esp::AIM_NECK},
                {esp::AIM_NECK, esp::NECK},
                {esp::SPINE2, esp::SHOULDER_L},
                {esp::SHOULDER_L, esp::ELBOW_L},
                {esp::ELBOW_L, esp::HAND_L},
                {esp::SPINE2, esp::SHOULDER_R},
                {esp::SHOULDER_R, esp::ELBOW_R},
                {esp::ELBOW_R, esp::HAND_R},
                {esp::PELVIS, esp::HIP_L},
                {esp::HIP_L, esp::KNEE_L},
                {esp::KNEE_L, esp::FOOT_HEEL_L},
                {esp::PELVIS, esp::HIP_R},
                {esp::HIP_R, esp::KNEE_R},
                {esp::KNEE_R, esp::FOOT_HEEL_R},
            }};
            const std::array<BoneSegment, 2> toeSegments = {{
                {esp::FOOT_HEEL_L, esp::LeftToeBoneForTeam(player.team)},
                {esp::FOOT_HEEL_R, esp::RightToeBoneForTeam(player.team)},
            }};
            const float centerX = screenWidth * 0.5f;
            const float centerY = screenHeight * 0.5f;
            Vector3 best = {};
            float bestDistanceSquared = (std::numeric_limits<float>::max)();

            const auto considerSegment = [&](const BoneSegment& segment) {
                const Vector3 first = predicted(PlayerBone(player, segment.first));
                const Vector3 second = predicted(PlayerBone(player, segment.second));
                if (!IsUsablePoint(first) || !IsUsablePoint(second))
                    return;
                const ScreenPos firstScreen = WorldToScreen(
                    first, matrix, screenWidth, screenHeight);
                const ScreenPos secondScreen = WorldToScreen(
                    second, matrix, screenWidth, screenHeight);
                if (!firstScreen.onScreen || !secondScreen.onScreen)
                    return;

                const float segmentX = secondScreen.x - firstScreen.x;
                const float segmentY = secondScreen.y - firstScreen.y;
                const float lengthSquared =
                    segmentX * segmentX + segmentY * segmentY;
                float t = 0.0f;
                if (lengthSquared > 0.0001f) {
                    t = std::clamp(
                        ((centerX - firstScreen.x) * segmentX +
                         (centerY - firstScreen.y) * segmentY) /
                            lengthSquared,
                        0.0f,
                        1.0f);
                }
                const float closestX = firstScreen.x + segmentX * t;
                const float closestY = firstScreen.y + segmentY * t;
                const float dx = closestX - centerX;
                const float dy = closestY - centerY;
                const float distanceSquared = dx * dx + dy * dy;
                if (distanceSquared >= bestDistanceSquared)
                    return;
                bestDistanceSquared = distanceSquared;
                best = first + (second - first) * t;
            };

            for (const BoneSegment& segment : kBodySegments)
                considerSegment(segment);
            for (const BoneSegment& segment : toeSegments)
                considerSegment(segment);
            return best;
        }

        return predicted(SelectNamedAimPoint(player, aimBone));
    }

    Candidate SelectTarget(
        const esp::TargetSnapshot& snapshot,
        int aimBone,
        float screenWidth,
        float screenHeight,
        float radius,
        bool visibleOnly,
        bool predictive,
        uintptr_t preferredPawn = 0,
        uintptr_t excludedPawn = 0,
        int localPingMs = 0,
        float interpolationSeconds = 0.004f,
        bool damageAware = false,
        float minimumDamage = 1.0f,
        bool autowall = false,
        const target::convars::Values* convars = nullptr,
        target::SelectionDiagnostics* diagnostics = nullptr)
    {
        Candidate best;
        Candidate locked;
        if (snapshot.localTeam != 2 && snapshot.localTeam != 3)
            return best;

        const float centerX = screenWidth * 0.5f;
        const float centerY = screenHeight * 0.5f;
        for (size_t slot = 0; slot < snapshot.players.size(); ++slot) {
            const esp::PlayerData& player = snapshot.players[slot];
            if (!player.valid || player.pawn == 0 || player.health <= 0 ||
                player.pawn == excludedPawn ||
                player.pawn == snapshot.localPawn ||
                static_cast<int>(slot) == snapshot.localPlayerIndex ||
                (player.team != 2 && player.team != 3) ||
                player.team == snapshot.localTeam) {
                continue;
            }
            if (diagnostics) ++diagnostics->enemies;
            if (!IsPlayerCoreFresh(snapshot, player) || !ArePlayerBonesFresh(snapshot, player)) {
                if (diagnostics) ++diagnostics->stale;
                continue;
            }

            const uint64_t boneAgeUs =
                player.bonesUpdatedAtUs > 0 &&
                snapshot.sampledAtUs >= player.bonesUpdatedAtUs
                    ? snapshot.sampledAtUs - player.bonesUpdatedAtUs
                    : UINT64_MAX;
            const float leadSeconds = predictive
                ? std::min(
                    target::policy::ResolvePredictionSeconds(
                        boneAgeUs,
                        localPingMs,
                        interpolationSeconds),
                    target::policy::kMaximumAimPredictionSeconds)
                : 0.0f;
            const Vector3 point = SelectAimPoint(
                player,
                aimBone,
                snapshot.viewMatrix,
                screenWidth,
                screenHeight,
                leadSeconds);
            if (!IsUsablePoint(point))
                continue;
            ScreenPos screen = WorldToScreen(
                point,
                snapshot.viewMatrix,
                screenWidth,
                screenHeight);
            if (!screen.onScreen) {
                if (diagnostics) ++diagnostics->outsideFov;
                continue;
            }
            const float dx = screen.x - centerX;
            const float dy = screen.y - centerY;
            float distance = std::sqrt(dx * dx + dy * dy);
            // Damage/autowall and multipoint evaluation are intentionally
            // bounded to the lock circle. Scanning every on-screen player's
            // every capsule before this cheap test caused avoidable Target
            // thread spikes without changing which target could be selected.
            if (distance > radius * target::policy::kTargetLockRadiusScale) {
                if (diagnostics) ++diagnostics->outsideFov;
                continue;
            }
            Candidate considered;
            considered.player = &player;
            considered.point = point;
            considered.screenDistance = distance;
            considered.slot = static_cast<int>(slot);
            considered.requiredHitgroup =
                RequiredHitgroupForAimPoint(aimBone);
            considered.predictionOffset = PredictAimPoint(player, {}, leadSeconds);
            if (damageAware && (!player.hasHitboxes || !snapshot.localEyeValid ||
                    !snapshot.localWeaponTelemetryValid ||
                    !target::policy::IsFreshSample(player.hitboxesUpdatedAtUs,
                        snapshot.sampledAtUs, target::policy::kMaximumPlayerBoneAgeUs) ||
                    !target::policy::IsFreshSample(snapshot.localEyeUpdatedAtUs,
                        snapshot.sampledAtUs, target::policy::kMaximumTargetViewAgeUs) ||
                    !target::policy::IsFreshSample(snapshot.localWeaponTelemetryUpdatedAtUs,
                        snapshot.sampledAtUs, target::policy::kMaximumWeaponStateAgeUs))) {
                if (diagnostics) ++diagnostics->missingBallistics;
                continue;
            }
            if (damageAware &&
                (!convars || !ResolvePlannedAimShot(
                    snapshot,
                    considered,
                    leadSeconds,
                    visibleOnly,
                    minimumDamage,
                    autowall,
                    *convars, screenWidth, screenHeight,
                    player.pawn == preferredPawn ? radius * target::policy::kTargetLockRadiusScale : radius))) {
                if (diagnostics) ++diagnostics->damageRejected;
                continue;
            }
            if (visibleOnly &&
                !IsTargetVisible(snapshot, player, considered.point)) {
                if (diagnostics) ++diagnostics->visibilityRejected;
                continue;
            }
            screen = WorldToScreen(
                considered.point,
                snapshot.viewMatrix,
                screenWidth,
                screenHeight);
            if (!screen.onScreen)
                continue;
            const float plannedDx = screen.x - centerX;
            const float plannedDy = screen.y - centerY;
            distance = std::sqrt(
                plannedDx * plannedDx + plannedDy * plannedDy);
            considered.screenDistance = distance;
            if (preferredPawn != 0 && player.pawn == preferredPawn &&
                target::policy::ShouldKeepLockedTarget(distance, radius)) {
                locked = considered;
            }
            if (distance > radius || distance >= best.screenDistance)
                continue;

            best = considered;
        }
        return locked.player ? locked : best;
    }

    float RandomNormalClamped(
        float mean,
        float standardDeviation,
        float minimum,
        float maximum)
    {
        static thread_local std::mt19937 generator([] {
            std::random_device device;
            const uint32_t seed = device.entropy() > 0.0
                ? device()
                : static_cast<uint32_t>(NowUs());
            return seed;
        }());
        std::normal_distribution<float> distribution(mean, standardDeviation);
        return std::clamp(distribution(generator), minimum, maximum);
    }

    void ApplyHumanizedSmoothing(
        MotionRuntimeState& motion,
        float& pitchDelta,
        float& yawDelta,
        float smoothing,
        bool enabled)
    {
        const float targetPitchDelta = pitchDelta;
        const float targetYawDelta = yawDelta;
        const float safeSmoothing =
            target::policy::SanitizeSmoothing(smoothing);
        const float deltaLength = std::sqrt(
            pitchDelta * pitchDelta + yawDelta * yawDelta);
        const float precisionEnvelope = std::clamp(
            (deltaLength - 0.12f) / 0.70f,
            0.0f,
            1.0f);
        const float distanceFactor = std::clamp(deltaLength / 10.0f, 0.0f, 1.0f);
        const float ease = 1.0f - distanceFactor * distanceFactor;
        float smoothFactor = safeSmoothing <= 1.0f
            ? 1.0f
            : (0.3f + ease * 0.7f) / safeSmoothing;
        float pitchBias = 1.0f;
        float yawBias = 1.0f;
        if (enabled) {
            if (motion.humanInitialDistance <= 0.0f) {
                motion.humanInitialDistance = std::max(0.1f, deltaLength);
                motion.humanCurveStrength = std::clamp(
                    deltaLength * RandomNormalClamped(0.14f, 0.035f, 0.07f, 0.22f),
                    0.04f,
                    1.35f);
                motion.humanCurveSign =
                    RandomNormalClamped(0.0f, 1.0f, -2.0f, 2.0f) < 0.0f
                        ? -1.0f
                        : 1.0f;
            }

            const float remaining = std::clamp(
                deltaLength / std::max(0.1f, motion.humanInitialDistance),
                0.0f,
                1.0f);
            const float progress = 1.0f - remaining;
            const float curveEnvelope =
                std::sin(kPi * progress) * remaining;
            if (deltaLength > 0.0001f) {
                const float curve = motion.humanCurveStrength *
                    motion.humanCurveSign * curveEnvelope * precisionEnvelope;
                const float perpendicularPitch = -yawDelta / deltaLength;
                const float perpendicularYaw = pitchDelta / deltaLength;
                pitchDelta += perpendicularPitch * curve;
                yawDelta += perpendicularYaw * curve;
            }

            const uint64_t noiseNowUs = NowUs();
            const float tickSeconds = motion.humanNoiseUpdatedAtUs != 0u &&
                                      noiseNowUs >= motion.humanNoiseUpdatedAtUs
                ? std::clamp(
                    static_cast<float>(
                        noiseNowUs - motion.humanNoiseUpdatedAtUs) *
                        0.000001f,
                    0.0005f,
                    0.05f)
                : (1.0f / 240.0f);
            motion.humanNoiseUpdatedAtUs = noiseNowUs;
            const float correlation =
                target::policy::ResolveNoiseCorrelation(tickSeconds);
            motion.humanSmoothNoise =
                target::policy::UpdateCorrelatedNoise(
                    motion.humanSmoothNoise,
                    RandomNormalClamped(0.0f, 0.06f, -0.15f, 0.15f),
                    correlation);
            motion.humanPitchNoise =
                target::policy::UpdateCorrelatedNoise(
                    motion.humanPitchNoise,
                    RandomNormalClamped(0.0f, 0.02f, -0.05f, 0.05f),
                    correlation,
                    0.05f);
            motion.humanYawNoise =
                target::policy::UpdateCorrelatedNoise(
                    motion.humanYawNoise,
                    RandomNormalClamped(0.0f, 0.03f, -0.07f, 0.07f),
                    correlation,
                    0.07f);
            smoothFactor *= std::clamp(
                1.0f + motion.humanSmoothNoise * precisionEnvelope,
                0.85f,
                1.15f);
            pitchBias = 1.0f + motion.humanPitchNoise * precisionEnvelope;
            yawBias = 1.0f + motion.humanYawNoise * precisionEnvelope;
            const float microScale =
                std::min(1.0f, deltaLength / 1.5f) * precisionEnvelope;
            pitchDelta += motion.humanPitchNoise * 0.08f * microScale;
            yawDelta += motion.humanYawNoise * 0.08f * microScale;
        } else {
            motion.humanSmoothNoise = 0.0f;
            motion.humanPitchNoise = 0.0f;
            motion.humanYawNoise = 0.0f;
            motion.humanNoiseUpdatedAtUs = 0;
            motion.humanInitialDistance = 0.0f;
            motion.humanCurveStrength = 0.0f;
            motion.humanCurveSign = 1.0f;
        }
        const uint64_t stepNowUs = NowUs();
        const float stepSeconds = motion.lastStepAtUs != 0 && stepNowUs >= motion.lastStepAtUs
            ? static_cast<float>(stepNowUs - motion.lastStepAtUs) * 0.000001f
            : (1.0f / 128.0f);
        motion.lastStepAtUs = stepNowUs;
        pitchDelta *= target::policy::TimeAdjustedSmoothing(smoothFactor * pitchBias, stepSeconds);
        yawDelta *= target::policy::TimeAdjustedSmoothing(smoothFactor * yawBias, stepSeconds);
        pitchDelta = target::policy::ClampAimStepToTarget(
            pitchDelta,
            targetPitchDelta);
        yawDelta = target::policy::ClampAimStepToTarget(
            yawDelta,
            targetYawDelta);
    }

    void ResetAimState()
    {
        s_runtime.aim = {};
    }

    enum class MoveResult : uint8_t
    {
        Aligned,
        Accumulating,
        Queued,
        Failed,
    };

    WeaponControlProfile ResolveWeaponControlProfile(uint16_t weaponId)
    {
        WeaponControlProfile profile;
        using Category = esp::weapons::DroppedWeaponCategory;
        switch (esp::weapons::DroppedWeaponCategoryFromItemId(weaponId)) {
        case Category::Pistols:
            profile.maximumPrecisionSpeed = 24.0f;
            profile.shotCooldown = std::chrono::milliseconds(125);
            profile.accuracySettleTime = std::chrono::milliseconds(95);
            break;
        case Category::Snipers:
            profile.maximumPrecisionSpeed = 12.0f;
            profile.triggerStableTicks = 4;
            profile.shotCooldown = std::chrono::milliseconds(220);
            profile.accuracySettleTime = std::chrono::milliseconds(130);
            break;
        case Category::Smgs:
            profile.maximumPrecisionSpeed = 38.0f;
            profile.accuracySettleTime = std::chrono::milliseconds(65);
            [[fallthrough]];
        case Category::MachineGuns:
            profile.shotCooldown = std::chrono::milliseconds(80);
            break;
        case Category::Shotguns:
            profile.maximumPrecisionSpeed = 42.0f;
            profile.triggerStableTicks = 4;
            profile.shotCooldown = std::chrono::milliseconds(240);
            profile.accuracySettleTime = std::chrono::milliseconds(90);
            break;
        default:
            break;
        }

        // Slow, high-recoil weapons need a full recovery before another click.
        if (weaponId == 1u)
            profile.shotCooldown = std::chrono::milliseconds(225); // Desert Eagle
        else if (weaponId == 64u)
            profile.shotCooldown = std::chrono::milliseconds(325); // R8 Revolver
        else if (weaponId == 9u || weaponId == 40u)
            profile.shotCooldown = std::chrono::milliseconds(300); // AWP / SSG 08
        // The measured 256-seed hitchance now owns movement/jump recovery;
        // an additional guessed settle delay would reject already accurate
        // weapons and duplicate the live inaccuracy calculation.
        profile.accuracySettleTime = std::chrono::milliseconds(0);
        return profile;
    }

    int WeaponProfileIndex(uint16_t weaponId)
    {
        using Category = esp::weapons::DroppedWeaponCategory;
        switch (esp::weapons::DroppedWeaponCategoryFromItemId(weaponId)) {
        case Category::Pistols: return 0;
        case Category::Rifles: return 1;
        case Category::Snipers: return 2;
        case Category::Smgs: return 3;
        case Category::Shotguns: return 4;
        case Category::MachineGuns: return 5;
        default: return 1;
        }
    }

    Vector3 ResolveBallisticAngles(const esp::TargetSnapshot& snapshot)
    {
        Vector3 angles = snapshot.viewAngles;
        const bool recoilFresh = snapshot.localAimPunchValid &&
            target::policy::IsFreshSample(
                snapshot.localAimPunchUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumRecoilSampleAgeUs) &&
            target::policy::IsTimestampSkewAcceptable(
                snapshot.localAimPunchUpdatedAtUs,
                snapshot.viewUpdatedAtUs,
                target::policy::kMaximumRecoilViewSkewUs);
        if (recoilFresh) {
            angles.x += snapshot.localAimPunch.x * 2.0f;
            angles.y += snapshot.localAimPunch.y * 2.0f;
        }
        angles.y = std::remainder(angles.y, 360.0f);
        angles.z = 0.0f;
        return angles;
    }

    target::ballistics::WeaponSpread ResolveWeaponSpread(
        const esp::TargetSnapshot& snapshot,
        const target::convars::Values& convars)
    {
        float inaccuracy = snapshot.localInaccuracy;
        float spread = snapshot.localSpread;
        if (convars.accuracyValid) {
            if (convars.weaponAccuracyNoSpread) {
                inaccuracy = 0.0f;
                spread = 0.0f;
            } else if (convars.weaponAccuracyForceSpread > 0.0f) {
                inaccuracy = std::min(
                    convars.weaponAccuracyForceSpread,
                    1.0f);
            } else if (!snapshot.localOnGround && convars.jumpValid) {
                const float threshold = std::sqrt(std::fabs(
                    convars.jumpImpulse));
                const float vertical = std::sqrt(std::fabs(
                    snapshot.localVerticalVelocity));
                const float low = threshold * 0.25f;
                float air = snapshot.localJumpInaccuracyApex;
                if (threshold > low) {
                    const float fraction =
                        (vertical - low) / (threshold - low);
                    air = snapshot.localJumpInaccuracyApex + fraction *
                        (snapshot.localJumpInaccuracyInitial -
                         snapshot.localJumpInaccuracyApex);
                } else if (vertical >= threshold) {
                    air = snapshot.localJumpInaccuracyInitial;
                }
                air = std::clamp(
                    air,
                    0.0f,
                    std::max(
                        0.0f,
                        snapshot.localJumpInaccuracyInitial * 2.0f));
                inaccuracy = std::min(
                    1.0f,
                    snapshot.localInaccuracyWithoutAir + air);
            }
        }
        return {
            inaccuracy,
            spread,
            snapshot.localRecoilIndex,
            snapshot.localWeaponId,
            snapshot.localWeaponBullets,
            snapshot.localWeaponRange,
        };
    }

    struct ShotEvaluation
    {
        target::FireBlockReason reason = target::FireBlockReason::StaleLocal;
        bool geometryHit = false;
        bool damageReady = false;
        bool penetrated = false;
        bool deterministicSeedReady = false;
        float hitchance = -1.0f;
        float predictedSeedHitFraction = -1.0f;
        float predictedSeedSafety = 0.0f;
        float damage = -1.0f;
        int hitbox = -1;
        int hitgroup = 0;
        int predictedRenderTick = -1;
        uint32_t predictedSeed = 0;
    };

    ShotEvaluation EvaluateShot(
        const esp::TargetSnapshot& snapshot,
        const Candidate& candidate,
        float requiredHitchance,
        float minimumDamage,
        bool autowall,
        const target::convars::Values& convars,
        float fireLeadSeconds,
        const Vector3* aimAnglesOverride = nullptr,
        bool checkSeedWindow = true)
    {
        ShotEvaluation evaluation;
        if (!candidate.player || !snapshot.localWeaponTelemetryValid ||
            !snapshot.localEyeValid || !candidate.player->hasHitboxes) {
            return evaluation;
        }
        const Vector3 angles = aimAnglesOverride
            ? *aimAnglesOverride
            : ResolveBallisticAngles(snapshot);
        Vector3 forward = {};
        Vector3 right = {};
        Vector3 up = {};
        target::ballistics::AnglesToDirections(
            angles,
            forward,
            right,
            up);
        (void)right;
        (void)up;
        const target::ballistics::WeaponSpread spread =
            ResolveWeaponSpread(snapshot, convars);
        // Translating the ray origin is exactly equivalent to translating all
        // target capsules. Aim, spread and alignment must use the SAME horizon.
        const Vector3 capsuleEye = snapshot.localEyePos - candidate.predictionOffset;
        evaluation.hitchance = requiredHitchance > 0.0f
            ? target::ballistics::CalculateHitchance(
                capsuleEye,
                angles,
                *candidate.player,
                spread,
                candidate.requiredHitgroup)
            : -1.0f;
        const float safeInterval =
            std::isfinite(snapshot.localIntervalPerTick) &&
            snapshot.localIntervalPerTick >= 0.001f &&
            snapshot.localIntervalPerTick <= 0.1f
                ? snapshot.localIntervalPerTick
                : (1.0f / 64.0f);
        const int leadTicks = std::max(
            1,
            static_cast<int>(std::ceil(std::clamp(
                fireLeadSeconds,
                0.0f,
                0.25f) / safeInterval)));
        const target::ballistics::SeedTickSelection seedSelection =
            checkSeedWindow
            ? target::ballistics::SelectSpreadSeedTick(
                capsuleEye,
                angles,
                *candidate.player,
                spread,
                snapshot.localRenderTick + leadTicks,
                3,
                candidate.requiredHitgroup)
            : target::ballistics::SeedTickSelection{};
        evaluation.predictedSeedHitFraction = checkSeedWindow ? seedSelection.Confidence() : -1.0f;
        const float hitchancePercent = evaluation.hitchance * 100.0f;
        const bool fullHitchanceReady = requiredHitchance <= 0.0f ||
            std::isfinite(hitchancePercent) &&
            hitchancePercent + 0.001f >= requiredHitchance;
        constexpr float kMinimumEarliestSeedSafety = 0.03f;
        const bool deterministicSeedReady = !checkSeedWindow ||
            target::policy::IsDeterministicSeedWindowReady(
                seedSelection.tested,
                seedSelection.hits,
                seedSelection.candidates[0].hit,
                seedSelection.candidates[0].geometricSafety,
                kMinimumEarliestSeedSafety);
        if (!fullHitchanceReady || !deterministicSeedReady) {
            evaluation.reason = !fullHitchanceReady
                ? target::FireBlockReason::Hitchance : target::FireBlockReason::SeedWindow;
            return evaluation;
        }
        evaluation.deterministicSeedReady = checkSeedWindow;
        if (checkSeedWindow) {
            // The first candidate is the calibrated earliest delivery bound.
            // Requiring it to hit avoids selecting a prettier later seed that
            // the external device may never reach. The 2/3 rule covers the
            // unavoidable one-tick delivery uncertainty without claiming an
            // impossible atomic render-tick guarantee.
            evaluation.predictedRenderTick =
                seedSelection.candidates[0].renderTick;
            evaluation.predictedSeed = seedSelection.candidates[0].seed;
            evaluation.predictedSeedSafety =
                seedSelection.candidates[0].geometricSafety;
            if (!target::ballistics::ResolveSpreadDirection(
                    angles,
                    spread,
                    evaluation.predictedSeed,
                    forward)) {
                evaluation.reason = target::FireBlockReason::SeedWindow;
                return evaluation;
            }
        }

        target::ballistics::CapsuleHit capsuleHit;
        if (!target::ballistics::TracePlayerCapsules(
                capsuleEye,
                forward,
                *candidate.player,
                snapshot.localWeaponRange,
                candidate.requiredHitgroup,
                &capsuleHit)) {
            evaluation.reason = target::FireBlockReason::CapsuleMiss;
            return evaluation;
        }
        // Dynamic player occlusion is not part of the static map BVH. Reject
        // the shot when any other live player capsule is in front, including
        // a teammate; this prevents firing through a model at the selected
        // target behind it. The direction is the actual predicted seed ray.
        for (const esp::PlayerData& blocker : snapshot.players) {
            if (!blocker.valid || blocker.health <= 0 ||
                blocker.pawn == 0 || blocker.pawn == candidate.player->pawn ||
                blocker.pawn == snapshot.localPawn || !blocker.hasHitboxes ||
                !target::policy::IsFreshSample(
                    blocker.hitboxesUpdatedAtUs,
                    snapshot.sampledAtUs,
                    target::policy::kMaximumTriggerTargetAgeUs))
                continue;
            target::ballistics::CapsuleHit blockerHit;
            if (target::ballistics::TracePlayerCapsules(
                    snapshot.localEyePos,
                    forward,
                    blocker,
                    capsuleHit.distance,
                    0,
                    &blockerHit) &&
                blockerHit.distance + 0.25f < capsuleHit.distance) {
                evaluation.reason = target::FireBlockReason::PlayerOccluded;
                return evaluation;
            }
        }
        evaluation.geometryHit = true;
        evaluation.hitbox = capsuleHit.hitbox;
        evaluation.hitgroup = capsuleHit.hitgroup;
        if (capsuleHit.hitgroup == 1 && candidate.player->armor > 0 &&
            (!candidate.player->hasHelmetValid ||
             !target::policy::IsFreshSample(
                 candidate.player->helmetUpdatedAtUs,
                 snapshot.sampledAtUs,
                 target::policy::kMaximumDamageStateAgeUs))) {
            evaluation.reason = target::FireBlockReason::ArmorStale;
            return evaluation;
        }

        float damage = snapshot.localWeaponDamage;
        int penetrationCount = 4;
        std::vector<target::physics::PenetrationSegment> segments;
        const Vector3 rayEnd = snapshot.localEyePos +
            forward * snapshot.localWeaponRange;
        const bool geometryAvailable =
            target::physics::TracePenetrationSegments(
                snapshot.mapKey,
                snapshot.localEyePos,
                rayEnd,
                segments);
        bool penetrated = false;
        evaluation.reason = target::FireBlockReason::WallBlocked;
        if (geometryAvailable) {
            for (const target::physics::PenetrationSegment& segment : segments) {
                if (segment.enterDistance >= capsuleHit.distance - 1.0f)
                    break;
                if (!autowall || segment.exitDistance >= capsuleHit.distance ||
                    penetrationCount <= 0 ||
                    snapshot.localWeaponPenetration <= 0.0f) {
                    return evaluation;
                }
                float penetrationModifier =
                    segment.minimumPenetrationModifier;
                if (segment.enterSurface.surfaceType !=
                    segment.exitSurface.surfaceType) {
                    penetrationModifier = std::min(
                        penetrationModifier,
                        segment.exitSurface.penetration);
                }
                if (segment.exitDistance > 3000.0f ||
                    penetrationModifier < 0.1f) {
                    return evaluation;
                }
                float damageModifier = 0.16f;
                const uint16_t enterType = segment.enterSurface.surfaceType;
                const uint16_t exitType = segment.exitSurface.surfaceType;
                if (enterType == exitType) {
                    if (((enterType - 85u) & 0xFFFFFFFDu) == 0u)
                        penetrationModifier = 3.0f;
                    else if (enterType == 76u)
                        penetrationModifier = 2.0f;
                    if (segment.thickness < 6.0f &&
                        (enterType == 71u || enterType == 89u)) {
                        damageModifier = 0.05f;
                        penetrationModifier = 3.0f;
                    }
                }
                const float inversePenetration =
                    1.0f / penetrationModifier;
                const float baseLoss = damageModifier * damage;
                const float penetrationLoss = std::max(
                    0.0f,
                    (3.0f / snapshot.localWeaponPenetration) * 1.25f) *
                    (inversePenetration * 3.0f);
                const float distanceLoss =
                    (segment.thickness * segment.thickness *
                     inversePenetration) /
                    24.0f;
                damage -= baseLoss + penetrationLoss + distanceLoss;
                if (damage < 1.0f)
                    return evaluation;
                penetrated = true;
                --penetrationCount;
            }
        } else if (!candidate.player->visible || !target::policy::IsFreshSample(
                candidate.player->visibilityUpdatedAtUs, snapshot.sampledAtUs,
                target::policy::kMaximumTriggerTargetAgeUs)) {
            // Workshop maps may not expose a remote physics world. In that
            // case only a live visibility sample may authorize a direct shot;
            // penetration is never guessed.
            evaluation.reason = target::FireBlockReason::WorldUnavailable;
            return evaluation;
        }

        damage *= std::pow(
            snapshot.localWeaponRangeModifier,
            capsuleHit.distance / 500.0f);
        damage = target::ballistics::ScaleDamage(
            damage,
            capsuleHit.hitgroup,
            candidate.player->armor,
            candidate.player->hasHelmet,
            candidate.player->team,
            snapshot.localWeaponArmorRatio,
            snapshot.localWeaponHeadshotMultiplier,
            convars.damageScaleValid ? convars.damageScaleCtHead : 1.0f,
            convars.damageScaleValid ? convars.damageScaleTHead : 1.0f,
            convars.damageScaleValid ? convars.damageScaleCtBody : 1.0f,
            convars.damageScaleValid ? convars.damageScaleTBody : 1.0f);
        const float requiredDamage = std::min(
            minimumDamage,
            static_cast<float>(candidate.player->health));
        evaluation.damage = damage;
        if (!std::isfinite(damage) || damage + 0.001f < requiredDamage) {
            evaluation.reason = target::FireBlockReason::DamageTooLow;
            return evaluation;
        }
        evaluation.reason = target::FireBlockReason::Ready;
        evaluation.damageReady = true;
        evaluation.penetrated = penetrated;
        evaluation.damage = damage;
        return evaluation;
    }

    bool ResolvePlannedAimShot(
        const esp::TargetSnapshot& snapshot,
        Candidate& candidate,
        float leadSeconds,
        bool visibleOnly,
        float minimumDamage,
        bool autowall,
        const target::convars::Values& convars,
        float screenWidth, float screenHeight, float radius)
    {
        if (!candidate.player || !candidate.player->hasHitboxes ||
            !snapshot.localEyeValid ||
            !snapshot.localWeaponTelemetryValid) {
            return false;
        }
        const Vector3 requestedPoint = candidate.point;
        const int requestedHitgroup = candidate.requiredHitgroup;
        bool found = false;
        float bestDamage = -1.0f;
        float bestPointDistance = (std::numeric_limits<float>::max)();
        Vector3 bestPoint = {};
        int bestHitgroup = 0;
        const target::ballistics::WeaponSpread weaponSpread =
            ResolveWeaponSpread(snapshot, convars);
        const int count = std::min<int>(
            candidate.player->hitboxCount,
            esp::kMaximumPlayerHitboxes);
        for (int index = 0; index < count; ++index) {
            const esp::HitboxCapsule& capsule =
                candidate.player->hitboxes[index];
            if (!capsule.valid || capsule.radius <= 0.0f ||
                !IsUsablePoint(capsule.center) || capsule.hitgroup <= 0 ||
                (requestedHitgroup > 0 && capsule.hitgroup != requestedHitgroup)) {
                continue;
            }
            const target::ballistics::CapsuleMultipoints multipoints =
                target::ballistics::GenerateCapsuleMultipoints(
                    capsule,
                    snapshot.localEyePos,
                    weaponSpread);
            for (int pointIndex = 0;
                 pointIndex < multipoints.count;
                 ++pointIndex) {
                const Vector3 currentPoint =
                    multipoints.points[pointIndex].position;
                if (!IsUsablePoint(currentPoint))
                    continue;
                const Vector3 predictedPoint = PredictAimPoint(
                    *candidate.player,
                    currentPoint,
                    leadSeconds);
                const ScreenPos projected = WorldToScreen(predictedPoint,
                    snapshot.viewMatrix, screenWidth, screenHeight);
                if (!projected.onScreen || std::hypot(projected.x - screenWidth * 0.5f,
                        projected.y - screenHeight * 0.5f) > radius)
                    continue;
                if (visibleOnly && !IsTargetVisible(
                        snapshot,
                        *candidate.player,
                        predictedPoint)) {
                    continue;
                }
                const Vector3 delta = predictedPoint - snapshot.localEyePos;
                const float planar = std::hypot(delta.x, delta.y);
                if (!std::isfinite(planar) || planar < 0.001f)
                    continue;
                const Vector3 aimAngles = {
                    -std::atan2(delta.z, planar) * (180.0f / kPi),
                    std::atan2(delta.y, delta.x) * (180.0f / kPi),
                    0.0f,
                };
                Candidate planned = candidate;
                planned.point = predictedPoint;
                planned.predictionOffset = predictedPoint - currentPoint;
                planned.requiredHitgroup = capsule.hitgroup;
                const ShotEvaluation evaluation = EvaluateShot(
                    snapshot,
                    planned,
                    0.0f,
                    minimumDamage,
                    autowall,
                    convars,
                    0.0f,
                    &aimAngles,
                    false); // Aimbot damage planning does not wait for trigger spread checks.
                if (!evaluation.damageReady)
                    continue;

                const float pointDistance =
                    (predictedPoint - requestedPoint).Length();
                const bool better =
                    target::policy::IsBetterPlannedHitboxChoice(
                        requestedHitgroup,
                        capsule.hitgroup,
                        evaluation.damage,
                        pointDistance,
                        found,
                        bestHitgroup,
                        bestDamage,
                        bestPointDistance);
                if (!better)
                    continue;
                found = true;
                bestDamage = evaluation.damage;
                bestPointDistance = pointDistance;
                bestPoint = predictedPoint;
                bestHitgroup = capsule.hitgroup;
            }
        }
        if (!found)
            return false;
        candidate.point = bestPoint;
        candidate.requiredHitgroup = bestHitgroup;
        return true;
    }

    MoveResult MoveToward(
        const esp::TargetSnapshot& snapshot,
        const Candidate& candidate,
        const WeaponControlProfile& weaponProfile,
        MotionRuntimeState& motion,
        float smoothing,
        bool humanization,
        bool recoilControl,
        float alignmentToleranceDegrees,
        int* outMoveX,
        int* outMoveY,
        float* outAngularError = nullptr,
        float holdHitchanceThreshold = -1.0f,
        const target::convars::Values* convars = nullptr)
    {
        if (outMoveX)
            *outMoveX = 0;
        if (outMoveY)
            *outMoveY = 0;
        if (outAngularError)
            *outAngularError = (std::numeric_limits<float>::max)();
        if (!candidate.player || !snapshot.localEyeValid || !IsUsablePoint(snapshot.localEyePos) ||
            !target::policy::IsFreshSample(snapshot.localEyeUpdatedAtUs,
                snapshot.sampledAtUs, target::policy::kMaximumTargetViewAgeUs))
            return MoveResult::Failed;
        const Vector3 eye = snapshot.localEyePos;
        const Vector3 delta = candidate.point - eye;
        const float planar = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        if (!std::isfinite(planar) || planar < 0.001f)
            return MoveResult::Failed;

        const float desiredPitch =
            -std::atan2(delta.z, planar) * (180.0f / kPi);
        const float desiredYaw =
            std::atan2(delta.y, delta.x) * (180.0f / kPi);
        const bool recoilSamplesFresh =
            snapshot.localAimPunchValid &&
            snapshot.localShotsFiredValid &&
            // A fresh residual punch still changes the ballistic direction
            // after shotsFired resets to zero. Do not stop compensation early.
            target::policy::IsFreshSample(
                snapshot.localAimPunchUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumRecoilSampleAgeUs) &&
            target::policy::IsFreshSample(
                snapshot.localShotsUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumRecoilSampleAgeUs) &&
            target::policy::IsTimestampSkewAcceptable(
                snapshot.localAimPunchUpdatedAtUs,
                snapshot.viewUpdatedAtUs,
                target::policy::kMaximumRecoilViewSkewUs) &&
            target::policy::IsTimestampSkewAcceptable(
                snapshot.localAimPunchUpdatedAtUs,
                snapshot.localShotsUpdatedAtUs,
                target::policy::kMaximumRecoilViewSkewUs);
        const target::policy::AimAngleDelta compensated =
            target::policy::ResolveCompensatedAimDelta(
                desiredPitch,
                desiredYaw,
                snapshot.viewAngles.x,
                snapshot.viewAngles.y,
                snapshot.localAimPunch.x,
                snapshot.localAimPunch.y,
                weaponProfile.recoilPitchScale,
                weaponProfile.recoilYawScale,
                recoilControl && recoilSamplesFresh);
        if (!compensated.valid)
            return MoveResult::Failed;
        float pitchDelta = compensated.pitch;
        float yawDelta = compensated.yaw;
        const float angularError = std::sqrt(
            pitchDelta * pitchDelta + yawDelta * yawDelta);
        if (outAngularError)
            *outAngularError = angularError;
        if (!std::isfinite(angularError))
            return MoveResult::Failed;

        if (holdHitchanceThreshold > 0.0f && convars &&
            snapshot.localWeaponTelemetryValid &&
            candidate.player->hasHitboxes) {
            const Vector3 ballisticAngles = ResolveBallisticAngles(snapshot);
            Vector3 forward = {};
            Vector3 right = {};
            Vector3 up = {};
            target::ballistics::AnglesToDirections(
                ballisticAngles,
                forward,
                right,
                up);
            (void)right;
            (void)up;
            const bool insideSelectedCapsule =
                target::ballistics::TracePlayerCapsules(
                    snapshot.localEyePos - candidate.predictionOffset,
                    forward,
                    *candidate.player,
                    snapshot.localWeaponRange,
                    candidate.requiredHitgroup);
            if (insideSelectedCapsule) {
                const float currentHitchance =
                    target::ballistics::CalculateHitchance(
                        snapshot.localEyePos - candidate.predictionOffset,
                        ballisticAngles,
                        *candidate.player,
                        ResolveWeaponSpread(snapshot, *convars),
                        candidate.requiredHitgroup) * 100.0f;
                if (target::policy::ShouldHoldCapsuleAim(
                        insideSelectedCapsule,
                        currentHitchance,
                        holdHitchanceThreshold)) {
                    motion.mouseRemainderX = 0.0f;
                    motion.mouseRemainderY = 0.0f;
                    if (outAngularError)
                        *outAngularError = 0.0f;
                    return MoveResult::Aligned;
                }
            }
        }

        const float sensitivity =
            std::isfinite(snapshot.sensitivity) && snapshot.sensitivity > 0.01f
                ? snapshot.sensitivity
                : 1.0f;
        const float fovAdjust =
            std::isfinite(snapshot.fovSensitivityAdjust) &&
            snapshot.fovSensitivityAdjust >= 0.05f &&
            snapshot.fovSensitivityAdjust <= 2.0f
                ? snapshot.fovSensitivityAdjust
                : 1.0f;
        const float safeAlignmentTolerance =
            std::isfinite(alignmentToleranceDegrees) &&
            alignmentToleranceDegrees > 0.0f
                ? alignmentToleranceDegrees
                : target::policy::ResolveAlignmentToleranceDegrees(
                    sensitivity,
                    fovAdjust);
        if (angularError <= safeAlignmentTolerance) {
            motion.mouseRemainderX = 0.0f;
            motion.mouseRemainderY = 0.0f;
            return MoveResult::Aligned;
        }
        if (motion.lastViewIssuedAtUs != 0 &&
            snapshot.viewUpdatedAtUs <= motion.lastViewIssuedAtUs)
            return MoveResult::Accumulating;
        ApplyHumanizedSmoothing(
            motion,
            pitchDelta,
            yawDelta,
            smoothing,
            humanization);
        const target::policy::MouseMove move = target::policy::ResolveMouseMove(
            pitchDelta,
            yawDelta,
            sensitivity,
            fovAdjust,
            1.0f,
            motion.mouseRemainderX,
            motion.mouseRemainderY);
        if (!move.valid)
            return MoveResult::Failed;
        const int moveX = move.x;
        const int moveY = move.y;
        if (outMoveX)
            *outMoveX = moveX;
        if (outMoveY)
            *outMoveY = moveY;
        if (moveX == 0 && moveY == 0) {
            motion.mouseRemainderX = move.remainderX;
            motion.mouseRemainderY = move.remainderY;
            return MoveResult::Accumulating;
        }
        if (!app::input::RequestMove(moveX, moveY))
            return MoveResult::Failed;
        motion.mouseRemainderX = move.remainderX;
        motion.mouseRemainderY = move.remainderY;
        motion.lastViewIssuedAtUs = snapshot.viewUpdatedAtUs;
        return MoveResult::Queued;
    }

    bool IsKeyDown(int virtualKey)
    {
        return app::input::IsActivationKeyDown(virtualKey);
    }

    bool ResolveActivation(
        bool enabled,
        int virtualKey,
        int mode,
        target::policy::ActivationState& state,
        bool paused)
    {
        const auto key = app::input::ReadActivationKeyState(virtualKey);
        return target::policy::UpdateActivation(state, enabled, virtualKey, mode,
            key.down, key.available, paused);
    }

    bool ValidTargetFrame(const esp::TargetSnapshot& snapshot)
    {
        // Per-feature movement/ballistics paths validate their eye samples.
        // This gate describes the shared frame and does not own activation.
        return snapshot.viewValid && !snapshot.localIsDead &&
               snapshot.snapshotAgeUs <= kMaximumTargetSnapshotAgeUs &&
               target::policy::IsFreshSample(
                   snapshot.viewUpdatedAtUs,
                   snapshot.sampledAtUs,
                   target::policy::kMaximumTargetViewAgeUs);
    }

    bool IsSupportedFirearm(const esp::TargetSnapshot& snapshot)
    {
        using Category = esp::weapons::DroppedWeaponCategory;
        switch (esp::weapons::DroppedWeaponCategoryFromItemId(
            snapshot.localWeaponId)) {
        case Category::Pistols:
        case Category::Rifles:
        case Category::Snipers:
        case Category::Smgs:
        case Category::Shotguns:
        case Category::MachineGuns:
            return esp::weapons::WeaponMaxClipFromItemId(
                snapshot.localWeaponId) > 0;
        default:
            return false;
        }
    }

    bool IsEligibleWeapon(const esp::TargetSnapshot& snapshot)
    {
        if (!IsSupportedFirearm(snapshot) ||
            !snapshot.localAmmoValid ||
            snapshot.localAmmoClip <= 0 ||
            snapshot.localWeaponHandle == 0 ||
            snapshot.localWeaponHandle == 0xFFFFFFFFu ||
            snapshot.localWeaponEntity == 0 ||
            !snapshot.localWeaponTelemetryValid ||
            snapshot.localIsReloading ||
            !snapshot.localWeaponReady ||
            !target::policy::IsFreshSample(
                snapshot.localWeaponUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumWeaponStateAgeUs) ||
            !target::policy::IsFreshSample(
                snapshot.localAmmoUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumWeaponStateAgeUs) ||
            !target::policy::IsTimestampSkewAcceptable(
                snapshot.localWeaponUpdatedAtUs,
                snapshot.localAmmoUpdatedAtUs,
                target::policy::kMaximumWeaponStateSkewUs) ||
            !target::policy::IsFreshSample(
                snapshot.localWeaponTelemetryUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumWeaponStateAgeUs) ||
            !target::policy::IsTimestampSkewAcceptable(
                snapshot.localWeaponTelemetryUpdatedAtUs,
                snapshot.localAmmoUpdatedAtUs,
                target::policy::kMaximumWeaponStateSkewUs)) {
            return false;
        }
        return true;
    }

    const esp::PlayerData* FindLocalPlayer(
        const esp::TargetSnapshot& snapshot)
    {
        if (snapshot.localPlayerIndex >= 0 &&
            snapshot.localPlayerIndex <
                static_cast<int>(snapshot.players.size())) {
            const esp::PlayerData& indexed =
                snapshot.players[snapshot.localPlayerIndex];
            if (indexed.valid && indexed.pawn == snapshot.localPawn)
                return &indexed;
        }
        for (const esp::PlayerData& player : snapshot.players) {
            if (player.valid && player.pawn != 0 &&
                player.pawn == snapshot.localPawn) {
                return &player;
            }
        }
        return nullptr;
    }

    bool IsLocalPrecisionStateReady(
        const esp::TargetSnapshot& snapshot,
        const WeaponControlProfile& profile)
    {
        (void)profile;
        if (!snapshot.localWeaponTelemetryValid ||
            !snapshot.localWeaponReady || snapshot.localIsReloading ||
            !target::policy::IsFreshSample(
                snapshot.localWeaponTelemetryUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumWeaponStateAgeUs))
            return false;
        if (esp::weapons::DroppedWeaponCategoryFromItemId(
                snapshot.localWeaponId) ==
                esp::weapons::DroppedWeaponCategory::Snipers &&
            !snapshot.localIsScoped) {
            return false;
        }
        return true;
    }

    float SafeAimPointRadius(int aimBone)
    {
        switch (target::policy::SanitizeAimBone(aimBone)) {
        case 0: return 2.75f;
        case 1: return 2.50f;
        case 2: return 4.25f;
        case 3: return 4.00f;
        default: return 3.00f;
        }
    }

    float ResolveCandidateAlignmentTolerance(
        const esp::TargetSnapshot& snapshot,
        const Candidate& candidate,
        int aimBone)
    {
        if (!candidate.player || !IsUsablePoint(snapshot.localEyePos) ||
            !IsUsablePoint(candidate.point)) {
            return target::policy::ResolveAlignmentToleranceDegrees(
                snapshot.sensitivity,
                snapshot.fovSensitivityAdjust);
        }
        const float distance =
            (candidate.point - snapshot.localEyePos).Length();
        float safeRadius = SafeAimPointRadius(aimBone);
        if (candidate.player->hasHitboxes) {
            float nearestCenterDistance =
                (std::numeric_limits<float>::max)();
            const int count = std::min<int>(
                candidate.player->hitboxCount,
                esp::kMaximumPlayerHitboxes);
            for (int index = 0; index < count; ++index) {
                const esp::HitboxCapsule& capsule =
                    candidate.player->hitboxes[index];
                if (!capsule.valid || capsule.radius <= 0.0f ||
                    (candidate.requiredHitgroup > 0 &&
                     capsule.hitgroup != candidate.requiredHitgroup)) {
                    continue;
                }
                const float centerDistance =
                    (capsule.center - candidate.point).Length();
                if (centerDistance < nearestCenterDistance) {
                    nearestCenterDistance = centerDistance;
                    safeRadius = capsule.radius * 0.78f;
                }
            }
        }
        return target::policy::ResolvePrecisionAlignmentToleranceDegrees(
            snapshot.sensitivity,
            snapshot.fovSensitivityAdjust,
            distance,
            safeRadius);
    }

    bool AreTriggerSamplesFresh(const esp::TargetSnapshot& snapshot)
    {
        return snapshot.localEyeValid &&
            target::policy::IsFreshSample(
                snapshot.localEyeUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumTriggerCrosshairAgeUs) &&
            target::policy::IsFreshSample(
                snapshot.viewUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumTriggerCrosshairAgeUs) &&
            target::policy::IsTimestampSkewAcceptable(
                snapshot.localEyeUpdatedAtUs,
                snapshot.viewUpdatedAtUs,
                target::policy::kMaximumTriggerViewSkewUs);
    }

    bool AreTriggerTargetSamplesFresh(
        const esp::TargetSnapshot& snapshot,
        const esp::PlayerData& player)
    {
        return player.hasBones && player.hasHitboxes &&
            target::policy::IsFreshSample(
                player.coreUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumTriggerTargetAgeUs) &&
            target::policy::IsFreshSample(
                player.bonesUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumTriggerTargetAgeUs) &&
            target::policy::IsFreshSample(
                player.hitboxesUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumTriggerTargetAgeUs) &&
            target::policy::IsTimestampSkewAcceptable(
                player.coreUpdatedAtUs,
                player.bonesUpdatedAtUs,
                target::policy::kMaximumTriggerTargetSkewUs) &&
            target::policy::IsTimestampSkewAcceptable(
                player.bonesUpdatedAtUs,
                player.hitboxesUpdatedAtUs,
                target::policy::kMaximumTriggerTargetSkewUs) &&
            target::policy::IsTimestampSkewAcceptable(
                player.bonesUpdatedAtUs,
                snapshot.viewUpdatedAtUs,
                target::policy::kMaximumTriggerViewSkewUs) &&
            target::policy::IsTimestampSkewAcceptable(
                player.bonesUpdatedAtUs,
                snapshot.localEyeUpdatedAtUs,
                target::policy::kMaximumTriggerViewSkewUs);
    }

    bool DoesCurrentBallisticRayHitCandidate(
        const esp::TargetSnapshot& snapshot,
        const Candidate& candidate)
    {
        if (!candidate.player || !candidate.player->hasHitboxes ||
            !snapshot.localEyeValid ||
            !snapshot.localWeaponTelemetryValid) {
            return false;
        }
        Vector3 forward = {};
        Vector3 right = {};
        Vector3 up = {};
        target::ballistics::AnglesToDirections(
            ResolveBallisticAngles(snapshot),
            forward,
            right,
            up);
        return target::ballistics::TracePlayerCapsules(
            snapshot.localEyePos - candidate.predictionOffset,
            forward,
            *candidate.player,
            snapshot.localWeaponRange,
            candidate.requiredHitgroup);
    }

    bool IsEnemyTarget(
        const esp::TargetSnapshot& snapshot,
        const esp::PlayerData& player)
    {
        const bool spawnProtected = player.gunGameImmunityValid &&
            player.gunGameImmunity &&
            target::policy::IsFreshSample(
                player.gunGameImmunityUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumTriggerTargetAgeUs);
        return player.valid && player.pawn != 0 && player.health > 0 &&
            player.pawn != snapshot.localPawn &&
            player.team != snapshot.localTeam &&
            (player.team == 2 || player.team == 3) &&
            IsPlayerCoreFresh(snapshot, player) && !spawnProtected;
    }

    void RequestTriggerRelease()
    {
        if (!s_runtime.trigger.held &&
            !s_runtime.trigger.clickReserved)
            return;
        app::input::RequestLeftButton(false);
        s_runtime.trigger.held = false;
        s_runtime.trigger.clickReserved = false;
    }

    struct LatchedTargetObservation
    {
        const esp::PlayerData* player = nullptr;
        bool identityPresent = false;
        bool identityReplaced = false;
        bool coreFresh = false;
    };

    LatchedTargetObservation ObserveLatchedTarget(
        const esp::TargetSnapshot& snapshot)
    {
        LatchedTargetObservation observation;
        const TriggerRuntimeState& trigger = s_runtime.trigger;
        if (trigger.latchedPawn == 0 || trigger.latchedSlot < 0 ||
            trigger.latchedSlot >= static_cast<int>(snapshot.players.size())) {
            return observation;
        }

        const esp::PlayerData& player = snapshot.players[trigger.latchedSlot];
        const uint32_t nameHash = target::policy::HashTargetName(player.name);
        observation.coreFresh = target::policy::IsFreshSample(
            player.coreUpdatedAtUs,
            snapshot.sampledAtUs,
            target::policy::kMaximumTriggerTargetAgeUs);
        observation.identityPresent = player.valid &&
            target::policy::IsSameTargetIdentity(
                trigger.latchedPawn,
                trigger.latchedSlot,
                trigger.latchedNameHash,
                player.pawn,
                trigger.latchedSlot,
                nameHash, trigger.latchedPawnHandle, player.pawnHandle);
        observation.identityReplaced = !observation.identityPresent &&
            player.valid && player.pawn != 0 && observation.coreFresh;
        if (observation.identityPresent)
            observation.player = &player;
        return observation;
    }

    void ClearBlockedPawnState()
    {
        TriggerRuntimeState& trigger = s_runtime.trigger;
        trigger.blockedUnobservedPawn = 0;
        trigger.blockedAfterConfirmedDeath = false;
        trigger.blockedPawnCoreUpdatedAtUs = 0;
        trigger.blockedRevivalCoreUpdatedAtUs = 0;
        trigger.blockedRevivalSamples = 0;
    }

    void RefreshBlockedPawnRevival(const esp::TargetSnapshot& snapshot)
    {
        TriggerRuntimeState& trigger = s_runtime.trigger;
        if (trigger.blockedUnobservedPawn == 0 ||
            !trigger.blockedAfterConfirmedDeath) {
            return;
        }
        const esp::PlayerData* revived = nullptr;
        for (const esp::PlayerData& player : snapshot.players) {
            if (player.pawn == trigger.blockedUnobservedPawn) {
                revived = &player;
                break;
            }
        }
        if (!revived || !IsEnemyTarget(snapshot, *revived) ||
            revived->coreUpdatedAtUs <= trigger.blockedPawnCoreUpdatedAtUs) {
            trigger.blockedRevivalSamples = 0;
            return;
        }
        if (revived->coreUpdatedAtUs ==
            trigger.blockedRevivalCoreUpdatedAtUs) {
            return;
        }
        trigger.blockedRevivalCoreUpdatedAtUs = revived->coreUpdatedAtUs;
        trigger.blockedRevivalSamples = static_cast<uint8_t>(std::min<int>(
            trigger.blockedRevivalSamples + 1u,
            UINT8_MAX));
        // A same-address pawn may be reused on workshop respawn. Two distinct,
        // fresh alive core publications after the death tombstone prove this
        // is a new life and prevent a permanent block in auto-shot mode.
        if (trigger.blockedRevivalSamples >= 2u)
            ClearBlockedPawnState();
    }

    void ClearLatchedShotState()
    {
        RequestTriggerRelease();
        TriggerRuntimeState& trigger = s_runtime.trigger;
        trigger.shotLatched = false;
        trigger.shotObserved = false;
        trigger.requireCrosshairExit = false;
        trigger.latchedPawn = 0;
        trigger.latchedSlot = -1;
        trigger.latchedNameHash = 0;
        trigger.latchedPawnHandle = 0;
        trigger.latchedHealthBefore = 0;
        trigger.latchedCoreUpdatedAtBefore = 0;
        trigger.lastPostShotCoreUpdatedAtUs = 0;
        trigger.lastMissingSnapshotGeneration = 0;
        trigger.postShotCoreSamples = 0;
        trigger.missingTargetSamples = 0;
        trigger.predictedDamage = 0.0f;
        trigger.predictedLethal = false;
        trigger.crosshairExitSamples = 0;
        trigger.ammoBefore = -1;
        trigger.ammoUpdatedAtBefore = 0;
        trigger.shotsBefore = 0;
        trigger.shotsBeforeValid = false;
        trigger.shotsUpdatedAtBefore = 0;
        trigger.lastShotTimeBefore = 0.0f;
        trigger.lastShotTimeBeforeValid = false;
        trigger.weaponTelemetryUpdatedAtBefore = 0;
        trigger.previousPostShotAimPunch = {};
        trigger.previousPostShotAimPunchUpdatedAtUs = 0;
        trigger.stablePostShotRecoilSamples = 0;
        trigger.triggerShotIssuedAt = {};
        trigger.shotObservationDeadline = {};
        trigger.targetOutcomeNotBefore = {};
        trigger.waiting = false;
        trigger.alignedPawn = 0;
        trigger.alignedTicks = 0;
    }

    bool HasFreshPostClickWeaponEvidence(
        const esp::TargetSnapshot& snapshot,
        const TriggerRuntimeState& trigger)
    {
        const bool telemetryFresh = snapshot.localWeaponTelemetryValid &&
            snapshot.localWeaponTelemetryUpdatedAtUs >
                trigger.weaponTelemetryUpdatedAtBefore &&
            target::policy::IsFreshSample(
                snapshot.localWeaponTelemetryUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumWeaponStateAgeUs);
        const bool ammoFresh = trigger.ammoBefore < 0 ||
            (snapshot.localAmmoValid &&
             snapshot.localAmmoUpdatedAtUs > trigger.ammoUpdatedAtBefore &&
             target::policy::IsFreshSample(
                 snapshot.localAmmoUpdatedAtUs,
                 snapshot.sampledAtUs,
                 target::policy::kMaximumWeaponStateAgeUs));
        const bool shotsFresh = !trigger.shotsBeforeValid ||
            (snapshot.localShotsFiredValid &&
             snapshot.localShotsUpdatedAtUs > trigger.shotsUpdatedAtBefore &&
             target::policy::IsFreshSample(
                 snapshot.localShotsUpdatedAtUs,
                 snapshot.sampledAtUs,
                 target::policy::kMaximumWeaponStateAgeUs));
        return telemetryFresh && ammoFresh && shotsFresh;
    }

    bool ObserveWeaponShotTransition(
        const esp::TargetSnapshot& snapshot,
        const TriggerRuntimeState& trigger)
    {
        if (snapshot.localWeaponEntity == 0 || snapshot.localWeaponEntity != s_runtime.localWeaponEntity ||
            snapshot.localWeaponHandle != s_runtime.localWeaponHandle)
            return false;
        return (target::policy::IsFreshSample(snapshot.localAmmoUpdatedAtUs,
                    snapshot.sampledAtUs, target::policy::kMaximumWeaponStateAgeUs) &&
                target::policy::IsAmmoConsumptionObserved(
                   trigger.ammoBefore,
                   trigger.ammoUpdatedAtBefore,
                   snapshot.localAmmoClip,
                   snapshot.localAmmoValid,
                   snapshot.localAmmoUpdatedAtUs)) ||
            (target::policy::IsFreshSample(snapshot.localShotsUpdatedAtUs,
                    snapshot.sampledAtUs, target::policy::kMaximumWeaponStateAgeUs) &&
             target::policy::IsShotsFiredAdvanceObserved(
                   trigger.shotsBefore,
                   trigger.shotsBeforeValid,
                   trigger.shotsUpdatedAtBefore,
                   snapshot.localShotsFired,
                   snapshot.localShotsFiredValid,
                   snapshot.localShotsUpdatedAtUs)) ||
            (target::policy::IsFreshSample(snapshot.localWeaponTelemetryUpdatedAtUs,
                    snapshot.sampledAtUs, target::policy::kMaximumWeaponStateAgeUs) &&
             target::policy::IsLastShotTimeAdvanceObserved(
                   trigger.lastShotTimeBefore,
                   trigger.lastShotTimeBeforeValid,
                   trigger.weaponTelemetryUpdatedAtBefore,
                   snapshot.localLastShotTime,
                   snapshot.localWeaponTelemetryValid,
                   snapshot.localWeaponTelemetryUpdatedAtUs));
    }

    bool UpdatePostShotRecoilState(const esp::TargetSnapshot& snapshot)
    {
        TriggerRuntimeState& trigger = s_runtime.trigger;
        const bool shotsFresh = snapshot.localShotsFiredValid &&
            target::policy::IsFreshSample(
                snapshot.localShotsUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumRecoilSampleAgeUs);
        const bool punchFresh = snapshot.localAimPunchValid &&
            target::policy::IsFreshSample(
                snapshot.localAimPunchUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumRecoilSampleAgeUs);
        if (shotsFresh && snapshot.localShotsFired <= 0) {
            trigger.stablePostShotRecoilSamples =
                target::policy::kRequiredPostShotRecoilSamples;
        } else if (shotsFresh && punchFresh &&
                   snapshot.localAimPunchUpdatedAtUs != 0u &&
                   snapshot.localAimPunchUpdatedAtUs !=
                       trigger.previousPostShotAimPunchUpdatedAtUs) {
            const bool stable =
                trigger.previousPostShotAimPunchUpdatedAtUs != 0u &&
                target::policy::IsRecoilDeltaStable(
                    snapshot.localAimPunch.x,
                    snapshot.localAimPunch.y,
                    trigger.previousPostShotAimPunch.x,
                    trigger.previousPostShotAimPunch.y);
            trigger.stablePostShotRecoilSamples = stable
                ? static_cast<uint8_t>(std::min<int>(
                    trigger.stablePostShotRecoilSamples + 1u,
                    UINT8_MAX))
                : 0u;
            trigger.previousPostShotAimPunch = snapshot.localAimPunch;
            trigger.previousPostShotAimPunchUpdatedAtUs =
                snapshot.localAimPunchUpdatedAtUs;
        }
        return target::policy::IsPostShotRecoilReady(
            shotsFresh,
            snapshot.localShotsFired,
            punchFresh,
            trigger.stablePostShotRecoilSamples);
    }

    void ReleaseTriggerIfNeeded()
    {
        if (!s_runtime.trigger.held && !s_runtime.trigger.clickReserved &&
            !s_runtime.trigger.shotLatched) {
            return;
        }
        const app::input::LeftClickStatus click =
            app::input::GetLeftClickStatus();
        s_runtime.trigger.clickReserved = click.active;
        s_runtime.trigger.held = click.outputDown;
    }

    void ResetRuntimeState()
    {
        RequestTriggerRelease();
        s_runtime = {};
    }

    void ResetTriggerTracking()
    {
        RequestTriggerRelease();
        s_runtime.trigger = {};
    }

    void SuspendRuntimeMotion()
    {
        // Keep the shot observation/cooldown ledger across a read gap: losing
        // it could allow another click before the preceding shot is observed.
        RequestTriggerRelease();
        ResetAimState();
        s_runtime.trigger.assist = {};
        s_runtime.trigger.waiting = false;
        s_runtime.trigger.alignedPawn = 0;
        s_runtime.trigger.alignedTicks = 0;
        s_runtime.trigger.movementIssuedGeneration = 0;
        s_runtime.trigger.accuracyStableSince = {};
    }

    Candidate SelectCrosshairTarget(
        const esp::TargetSnapshot& snapshot,
        int aimBone,
        float screenWidth,
        float screenHeight,
        bool visibleOnly,
        bool predictive)
    {
        Candidate candidate;
        const int slot = snapshot.crosshairPlayerIndex;
        if (slot < 0 || slot >= static_cast<int>(snapshot.players.size()))
            return candidate;
        const esp::PlayerData& player = snapshot.players[slot];
        if (!IsEnemyTarget(snapshot, player) ||
            !ArePlayerBonesFresh(snapshot, player) ||
            snapshot.crosshairPawn == 0 ||
            snapshot.crosshairPawn != player.pawn) {
            return candidate;
        }

        const uint64_t boneAgeUs =
            player.bonesUpdatedAtUs > 0 &&
            snapshot.sampledAtUs >= player.bonesUpdatedAtUs
                ? snapshot.sampledAtUs - player.bonesUpdatedAtUs
                : UINT64_MAX;
        const float leadSeconds = predictive
            ? std::min(
                target::policy::ResolvePredictionSeconds(
                    boneAgeUs,
                    0),
                target::policy::kMaximumTriggerPredictionSeconds)
            : 0.0f;
        const Vector3 point = SelectAimPoint(
            player,
            aimBone,
            snapshot.viewMatrix,
            screenWidth,
            screenHeight,
            leadSeconds);
        if (!IsUsablePoint(point) ||
            (visibleOnly && !IsTargetVisible(snapshot, player, point))) {
            return candidate;
        }
        const ScreenPos screen = WorldToScreen(
            point,
            snapshot.viewMatrix,
            screenWidth,
            screenHeight);
        if (!screen.onScreen)
            return candidate;

        const float dx = screen.x - screenWidth * 0.5f;
        const float dy = screen.y - screenHeight * 0.5f;
        candidate.player = &player;
        candidate.point = point;
        candidate.screenDistance = std::sqrt(dx * dx + dy * dy);
        candidate.slot = slot;
        candidate.requiredHitgroup = RequiredHitgroupForAimPoint(aimBone);
        return candidate;
    }

    Candidate SelectGeometricCrosshairTarget(
        const esp::TargetSnapshot& snapshot,
        int aimBone,
        float screenWidth,
        float screenHeight,
        bool visibleOnly)
    {
        Candidate best;
        if (!snapshot.localEyeValid || !IsUsablePoint(snapshot.localEyePos))
            return best;
        Vector3 forward = {};
        Vector3 right = {};
        Vector3 up = {};
        target::ballistics::AnglesToDirections(
            ResolveBallisticAngles(snapshot),
            forward,
            right,
            up);
        (void)right;
        (void)up;
        float nearest = snapshot.localWeaponTelemetryValid
            ? snapshot.localWeaponRange
            : 8192.0f;
        for (size_t slot = 0; slot < snapshot.players.size(); ++slot) {
            const esp::PlayerData& player = snapshot.players[slot];
            if (!IsEnemyTarget(snapshot, player) ||
                !AreTriggerTargetSamplesFresh(snapshot, player)) {
                continue;
            }
            const int requiredHitgroup = RequiredHitgroupForAimPoint(aimBone);
            target::ballistics::CapsuleHit hit;
            if (!target::ballistics::TracePlayerCapsules(
                    snapshot.localEyePos,
                    forward,
                    player,
                    nearest,
                    requiredHitgroup,
                    &hit)) {
                continue;
            }
            if (visibleOnly &&
                !IsTargetVisible(snapshot, player, hit.position)) {
                continue;
            }
            const Vector3 point = SelectAimPoint(
                player,
                aimBone,
                snapshot.viewMatrix,
                screenWidth,
                screenHeight,
                0.0f);
            if (!IsUsablePoint(point))
                continue;
            const ScreenPos screen = WorldToScreen(
                point,
                snapshot.viewMatrix,
                screenWidth,
                screenHeight);
            if (!screen.onScreen)
                continue;
            nearest = hit.distance;
            best.player = &player;
            best.point = point;
            best.screenDistance = std::hypot(
                screen.x - screenWidth * 0.5f,
                screen.y - screenHeight * 0.5f);
            best.slot = static_cast<int>(slot);
            best.requiredHitgroup = requiredHitgroup;
        }
        return best;
    }

    void TickTarget(
        float screenWidth,
        float screenHeight,
        const g::TargetSettings& settings,
        bool menuOpen)
    {
        ReleaseTriggerIfNeeded();
        // Poll before any frame/device early return. A key press must not be
        // lost just because the data worker missed the same Target tick.
        bool aimKeyDown = ResolveActivation(settings.enabled && settings.aimbotEnabled,
            settings.aimKey, settings.aimActivationMode, s_aimActivation, menuOpen);
        bool triggerKeyDown = ResolveActivation(settings.enabled && settings.triggerbotEnabled,
            settings.triggerKey, settings.triggerActivationMode, s_triggerActivation, menuOpen);
        const bool triggerIntent = settings.triggerActivationMode == 1
            ? s_triggerActivation.toggled : s_triggerActivation.wasDown;
        if (!settings.enabled || !settings.triggerbotEnabled || !triggerIntent)
            s_triggerActivationShotIssued = false;
        if (!settings.enabled) {
            s_fireDiagnostics = {};
            ResetRuntimeState();
            PublishStatus(target::RuntimePhase::Disabled);
            return;
        }
        if (menuOpen) {
            SuspendRuntimeMotion();
            // Retain the last operational diagnostics so opening the menu
            // does not erase the reason the feature was blocked.
            s_status.pausedByMenu = true;
            s_status.aimKeyDown = s_aimActivation.toggled;
            s_status.triggerKeyDown = settings.triggerbotEnabled &&
                s_triggerActivation.toggled;
            s_status.updatedAtUs = NowUs();
            return;
        }
        s_aimSelection = {};
        s_triggerSelection = {};
        s_fireDiagnostics = {};
        if (!std::isfinite(screenWidth) || !std::isfinite(screenHeight) ||
            screenWidth <= 4.0f || screenHeight <= 4.0f) {
            SuspendRuntimeMotion();
            PublishStatus(target::RuntimePhase::DataUnavailable);
            return;
        }

        const app::input::DeviceStatus device = app::input::GetDeviceStatus();
        if (device.state != app::input::ConnectionState::Connected) {
            SuspendRuntimeMotion();
            s_aimActivation.toggled = false;
            s_triggerActivation.toggled = false;
            PublishStatus(target::RuntimePhase::InputUnavailable);
            return;
        }
        const app::input::LeftClickTiming clickTiming =
            app::input::GetLeftClickTiming();
        const float outputLatencySeconds =
            clickTiming.calibrated &&
            std::isfinite(clickTiming.dispatchLatencyMs) &&
            std::isfinite(clickTiming.dispatchJitterMs)
                ? std::clamp(
                    (clickTiming.dispatchLatencyMs +
                     clickTiming.dispatchJitterMs * 0.5f) * 0.001f,
                    0.002f,
                    0.035f)
                : 0.012f;

        esp::TargetSnapshot snapshot = {};
        const bool gotTargetSnapshot = esp::GetTargetSnapshot(&snapshot);
        if (gotTargetSnapshot) {
            const bool sceneChanged = s_activationScene != 0 && snapshot.sceneSerial != s_activationScene;
            const bool pawnChanged = s_activationPawn != 0 && snapshot.localPawn != 0 &&
                snapshot.localPawn != s_activationPawn;
            if (sceneChanged || pawnChanged || snapshot.localIsDead) {
                s_aimActivation.toggled = false;
                s_triggerActivation.toggled = false;
                aimKeyDown = false;
                triggerKeyDown = false;
                ResetRuntimeState();
                s_shotDiagnostics = {};
            }
            s_activationScene = snapshot.sceneSerial;
            if (snapshot.localPawn != 0) s_activationPawn = snapshot.localPawn;
        }
        if (!gotTargetSnapshot || !ValidTargetFrame(snapshot)) {
            SuspendRuntimeMotion();
            PublishStatus(target::RuntimePhase::DataUnavailable, aimKeyDown, triggerKeyDown);
            if (gotTargetSnapshot) {
                s_status.snapshotAgeUs = static_cast<int64_t>(snapshot.snapshotAgeUs);
                s_status.viewAgeUs = snapshot.viewUpdatedAtUs > 0 && snapshot.sampledAtUs >= snapshot.viewUpdatedAtUs
                    ? static_cast<int64_t>(snapshot.sampledAtUs - snapshot.viewUpdatedAtUs) : -1;
                s_status.eyeAgeUs = snapshot.localEyeUpdatedAtUs > 0 && snapshot.sampledAtUs >= snapshot.localEyeUpdatedAtUs
                    ? static_cast<int64_t>(snapshot.sampledAtUs - snapshot.localEyeUpdatedAtUs) : -1;
            }
            return;
        }
        const int activeWeaponProfileIndex =
            WeaponProfileIndex(snapshot.localWeaponId);
        const auto& activeWeaponSettings = settings.weaponProfiles[
            std::clamp(activeWeaponProfileIndex, 0, 5)];
        s_activeWeaponProfile.store(std::clamp(activeWeaponProfileIndex, 0, 5), std::memory_order_relaxed);
        const float configuredFov = target::policy::ResolveConfiguredFov(
            settings.fovRadius, settings.fovPerWeapon, activeWeaponSettings.fovRadius);
        const target::convars::Values dynamicConVars =
            target::convars::Read();
        int localPingMs = 0;
        if (snapshot.localPlayerIndex >= 0 &&
            snapshot.localPlayerIndex <
                static_cast<int>(snapshot.players.size())) {
            const esp::PlayerData& local =
                snapshot.players[snapshot.localPlayerIndex];
            if (local.valid && local.pawn == snapshot.localPawn)
                localPingMs = std::clamp(local.ping, 0, 200);
        }
        float interpolationSeconds =
            std::isfinite(snapshot.localIntervalPerTick) &&
            snapshot.localIntervalPerTick >= 0.001f &&
            snapshot.localIntervalPerTick <= 0.1f
                ? snapshot.localIntervalPerTick
                : (1.0f / 64.0f);
        if (dynamicConVars.interpolationValid) {
            interpolationSeconds = std::max(
                dynamicConVars.clientInterpolation,
                dynamicConVars.clientInterpolationRatio /
                    static_cast<float>(
                        dynamicConVars.clientUpdateRate));
            interpolationSeconds = std::clamp(
                interpolationSeconds,
                0.0f,
                0.1f);
        }
        // Direct damage checks also need geometry. Disabling Visible Only
        // and Autowall must not prevent the required map build from starting.
        const bool needsGeometry = settings.aimbotEnabled || settings.triggerbotEnabled;
        if (needsGeometry)
            target::physics::RequestForMap(snapshot.mapKey);

        const bool weaponIdentityFresh = snapshot.localWeaponId != 0 &&
            snapshot.localWeaponEntity != 0 && snapshot.localWeaponHandle != 0 &&
            snapshot.localWeaponHandle != 0xFFFFFFFFu && target::policy::IsFreshSample(
                snapshot.localWeaponUpdatedAtUs, snapshot.sampledAtUs,
                target::policy::kMaximumWeaponStateAgeUs);
        const bool contextChanged =
            s_runtime.contextInitialized &&
            (s_runtime.sceneSerial != snapshot.sceneSerial ||
             s_runtime.localPawn != snapshot.localPawn ||
             (weaponIdentityFresh && (s_runtime.localWeaponId != snapshot.localWeaponId ||
             s_runtime.localWeaponHandle != snapshot.localWeaponHandle ||
             s_runtime.localWeaponEntity != snapshot.localWeaponEntity)));
        if (!s_runtime.contextInitialized || contextChanged) {
            ResetRuntimeState();
            s_runtime.contextInitialized = true;
            s_runtime.sceneSerial = snapshot.sceneSerial;
            s_runtime.localPawn = snapshot.localPawn;
            if (weaponIdentityFresh) {
                s_runtime.localWeaponId = snapshot.localWeaponId;
                s_runtime.localWeaponHandle = snapshot.localWeaponHandle;
                s_runtime.localWeaponEntity = snapshot.localWeaponEntity;
            }
        }

        target::RuntimePhase runtimePhase = target::RuntimePhase::Ready;
        Candidate aimCandidate;
        Candidate triggerCandidate;
        int lastMoveX = 0;
        int lastMoveY = 0;
        const WeaponControlProfile weaponProfile =
            ResolveWeaponControlProfile(snapshot.localWeaponId);
        MoveResult triggerMovementResult = MoveResult::Failed;
        float triggerAngularError = (std::numeric_limits<float>::max)();
        bool triggerMovementOwned = false;
        // Use one monotonic clock domain for movement acknowledgement and
        // stability. Mixing the small crosshair counter with a view timestamp
        // could make `new > movementIssued` permanently false when workshop
        // maps changed crosshair-read availability mid-track.
        const uint64_t triggerEvidenceGeneration = snapshot.viewUpdatedAtUs;

        if (s_runtime.observedShotsFired >= 0 &&
            snapshot.localShotsFired != s_runtime.observedShotsFired) {
            s_runtime.trigger.waiting = false;
            s_runtime.trigger.alignedPawn = 0;
            s_runtime.trigger.alignedTicks = 0;
            s_runtime.aim.mouseRemainderX = 0.0f;
            s_runtime.aim.mouseRemainderY = 0.0f;
            s_runtime.trigger.assist.mouseRemainderX = 0.0f;
            s_runtime.trigger.assist.mouseRemainderY = 0.0f;
        }
        s_runtime.observedShotsFired = snapshot.localShotsFired;

        const target::policy::FovCircle fovCircle =
            target::policy::ResolveFovCircle(
                screenWidth,
                screenHeight,
                configuredFov);
        if (!fovCircle.valid) {
            SuspendRuntimeMotion();
            PublishStatus(target::RuntimePhase::DataUnavailable);
            return;
        }
        const float radius = fovCircle.radius;
        const bool triggerRequested =
            settings.triggerbotEnabled && triggerKeyDown;
        const bool triggerShotBudgetReady =
            settings.triggerAutoShot || !s_triggerActivationShotIssued;
        const auto now = std::chrono::steady_clock::now();
        const bool localPrecisionStateReady =
            triggerRequested &&
            IsLocalPrecisionStateReady(snapshot, weaponProfile);
        if (!localPrecisionStateReady) {
            s_runtime.trigger.accuracyStableSince = {};
        } else if (s_runtime.trigger.accuracyStableSince ==
                   std::chrono::steady_clock::time_point{}) {
            s_runtime.trigger.accuracyStableSince = now;
        }
        const bool triggerAccuracySettled =
            localPrecisionStateReady &&
            s_runtime.trigger.accuracyStableSince !=
                std::chrono::steady_clock::time_point{} &&
            now - s_runtime.trigger.accuracyStableSince >=
                weaponProfile.accuracySettleTime;
        if (s_runtime.trigger.shotLatched) {
            TriggerRuntimeState& trigger = s_runtime.trigger;
            if (!trigger.shotObserved &&
                ObserveWeaponShotTransition(snapshot, trigger)) {
                trigger.shotObserved = true;
                trigger.unobservedRetryCount = 0;
            }

            const bool outcomeWindowElapsed =
                trigger.targetOutcomeNotBefore !=
                    std::chrono::steady_clock::time_point{} &&
                now >= trigger.targetOutcomeNotBefore;
            const LatchedTargetObservation targetObservation =
                ObserveLatchedTarget(snapshot);
            const uint64_t currentCoreUpdatedAtUs =
                targetObservation.player
                    ? targetObservation.player->coreUpdatedAtUs
                    : 0u;
            trigger.postShotCoreSamples =
                target::policy::AdvancePostShotCoreSamples(
                    trigger.postShotCoreSamples,
                    currentCoreUpdatedAtUs,
                    trigger.lastPostShotCoreUpdatedAtUs,
                    trigger.latchedCoreUpdatedAtBefore,
                    targetObservation.identityPresent,
                    targetObservation.coreFresh,
                    outcomeWindowElapsed);
            if (targetObservation.identityPresent &&
                targetObservation.coreFresh && outcomeWindowElapsed &&
                currentCoreUpdatedAtUs >
                    trigger.latchedCoreUpdatedAtBefore &&
                currentCoreUpdatedAtUs !=
                    trigger.lastPostShotCoreUpdatedAtUs) {
                trigger.lastPostShotCoreUpdatedAtUs =
                    currentCoreUpdatedAtUs;
            }

            const bool targetMissing =
                !targetObservation.identityPresent &&
                !targetObservation.identityReplaced;
            trigger.missingTargetSamples =
                target::policy::AdvanceMissingTargetSamples(
                    trigger.missingTargetSamples,
                    snapshot.captureTimeUs,
                    trigger.lastMissingSnapshotGeneration,
                    targetMissing,
                    outcomeWindowElapsed);
            if (targetMissing && outcomeWindowElapsed &&
                snapshot.captureTimeUs != 0u &&
                snapshot.captureTimeUs !=
                    trigger.lastMissingSnapshotGeneration) {
                trigger.lastMissingSnapshotGeneration =
                    snapshot.captureTimeUs;
            } else if (!targetMissing) {
                trigger.missingTargetSamples = 0;
                trigger.lastMissingSnapshotGeneration =
                    snapshot.captureTimeUs;
            }

            const int observedHealth = targetObservation.player
                ? targetObservation.player->health
                : 0;
            const target::policy::TriggerTargetOutcome targetOutcome =
                target::policy::ResolveTriggerTargetOutcome(
                    outcomeWindowElapsed,
                    targetObservation.identityPresent,
                    targetObservation.identityReplaced,
                    targetObservation.coreFresh,
                    observedHealth,
                    trigger.postShotCoreSamples,
                    trigger.missingTargetSamples);
            s_shotDiagnostics.weaponConfirmed = trigger.shotObserved;
            s_shotDiagnostics.outcome = static_cast<uint8_t>(targetOutcome);
            if (targetObservation.identityPresent && targetObservation.coreFresh &&
                currentCoreUpdatedAtUs > trigger.latchedCoreUpdatedAtBefore)
                s_shotDiagnostics.healthAfter = observedHealth;
            const bool recoilReady = trigger.shotObserved
                ? UpdatePostShotRecoilState(snapshot)
                : false;
            const bool weaponCycleReady =
                snapshot.localWeaponTelemetryValid &&
                snapshot.localWeaponReady &&
                !snapshot.localIsReloading &&
                target::policy::IsFreshSample(
                    snapshot.localWeaponTelemetryUpdatedAtUs,
                    snapshot.sampledAtUs,
                    target::policy::kMaximumWeaponStateAgeUs);

            if (targetOutcome ==
                    target::policy::TriggerTargetOutcome::DeadOrGone) {
                // Keep this exact pawn excluded until selection observes a
                // different identity. This prevents a one-frame stale alive
                // resurrection from producing a second tap.
                trigger.blockedUnobservedPawn = trigger.latchedPawn;
                trigger.blockedAfterConfirmedDeath = true;
                trigger.blockedPawnCoreUpdatedAtUs = std::max(
                    trigger.latchedCoreUpdatedAtBefore,
                    currentCoreUpdatedAtUs);
                trigger.blockedRevivalCoreUpdatedAtUs = 0;
                trigger.blockedRevivalSamples = 0;
                ClearLatchedShotState();
            } else if (trigger.shotObserved &&
                       targetOutcome ==
                           target::policy::TriggerTargetOutcome::AliveConfirmed &&
                       weaponCycleReady && recoilReady && !trigger.held &&
                       now >= trigger.nextAutoShotAt) {
                // The same pawn is coherently alive after the full replicated
                // outcome horizon. Only now may aim/RCS reacquire for another
                // independently validated single shot.
                ClearLatchedShotState();
            } else if (!trigger.shotObserved) {
                const bool observationWindowElapsed =
                    trigger.shotObservationDeadline !=
                        std::chrono::steady_clock::time_point{} &&
                    now >= trigger.shotObservationDeadline;
                const bool hardObservationTimeoutElapsed =
                    trigger.shotObservationDeadline !=
                        std::chrono::steady_clock::time_point{} &&
                    now >= trigger.shotObservationDeadline +
                        std::chrono::milliseconds(150);
                const bool postClickWeaponEvidenceFresh =
                    HasFreshPostClickWeaponEvidence(snapshot, trigger);
                if (observationWindowElapsed && !trigger.held &&
                    !trigger.clickReserved &&
                    ((postClickWeaponEvidenceFresh && weaponCycleReady) ||
                     hardObservationTimeoutElapsed)) {
                    const uintptr_t blockedPawn = trigger.latchedPawn;
                    trigger.blockedUnobservedPawn = blockedPawn;
                    trigger.blockedAfterConfirmedDeath = false;
                    trigger.blockedPawnCoreUpdatedAtUs =
                        trigger.latchedCoreUpdatedAtBefore;
                    trigger.blockedRevivalCoreUpdatedAtUs = 0;
                    trigger.blockedRevivalSamples = 0;
                    ClearLatchedShotState();
                    DmaLogPrintf(
                        "[WARN] Target click remained unobserved after bounded retry; blocking pawn=0x%llX until identity changes.",
                        static_cast<unsigned long long>(blockedPawn));
                }
            }
        }

        RefreshBlockedPawnRevival(snapshot);

        if (triggerRequested && settings.triggerAimAssist) {
            const uintptr_t preferredTriggerPawn =
                s_runtime.trigger.assist.targetPawn != 0 &&
                s_runtime.trigger.assist.targetPawn !=
                    s_runtime.trigger.blockedUnobservedPawn
                    ? s_runtime.trigger.assist.targetPawn
                    : snapshot.crosshairPawn;
            triggerCandidate = SelectTarget(
                snapshot,
                settings.triggerAimBone,
                screenWidth,
                screenHeight,
                radius,
                settings.triggerVisibleOnly,
                settings.triggerAimPredictive,
                preferredTriggerPawn,
                s_runtime.trigger.blockedUnobservedPawn,
                localPingMs,
                interpolationSeconds, false, 1.0f, false, nullptr, &s_triggerSelection);
        } else if (triggerRequested) {
            triggerCandidate = SelectGeometricCrosshairTarget(
                snapshot,
                settings.triggerAimBone,
                screenWidth,
                screenHeight,
                settings.triggerVisibleOnly);
            if (triggerCandidate.player &&
                triggerCandidate.player->pawn ==
                    s_runtime.trigger.blockedUnobservedPawn) {
                triggerCandidate = {};
            }
        }

        if (triggerCandidate.player &&
            s_runtime.trigger.blockedUnobservedPawn != 0 &&
            triggerCandidate.player->pawn !=
                s_runtime.trigger.blockedUnobservedPawn) {
            ClearBlockedPawnState();
        }

        const bool observingSameShot = s_runtime.trigger.shotLatched &&
            s_runtime.trigger.shotObserved && triggerCandidate.player &&
            triggerCandidate.player->pawn == s_runtime.trigger.latchedPawn;
        const bool triggerAssistMayOwnMovement =
            triggerRequested && settings.triggerAimAssist &&
            triggerCandidate.player &&
            (!s_runtime.trigger.shotLatched || observingSameShot) &&
            !s_runtime.trigger.held &&
            !s_runtime.trigger.clickReserved &&
            triggerCandidate.player->pawn !=
                s_runtime.trigger.blockedUnobservedPawn &&
            IsSupportedFirearm(snapshot) && snapshot.localEyeValid &&
            target::policy::IsFreshSample(snapshot.localEyeUpdatedAtUs,
                snapshot.sampledAtUs, target::policy::kMaximumTargetViewAgeUs) &&
            ArePlayerBonesFresh(snapshot, *triggerCandidate.player);
        if (triggerAssistMayOwnMovement) {
            if (!triggerCandidate.player) {
                s_runtime.trigger.assist = {};
                s_runtime.trigger.waiting = false;
                s_runtime.trigger.alignedPawn = 0;
                s_runtime.trigger.alignedTicks = 0;
            } else {
                if (s_runtime.trigger.assist.targetPawn !=
                    triggerCandidate.player->pawn) {
                    s_runtime.trigger.assist = {};
                    s_runtime.trigger.assist.targetPawn =
                        triggerCandidate.player->pawn;
                    s_runtime.trigger.waiting = false;
                    s_runtime.trigger.alignedPawn = 0;
                    s_runtime.trigger.alignedTicks = 0;
                }
                triggerMovementOwned = true;
                triggerMovementResult = MoveToward(
                    snapshot,
                    triggerCandidate,
                    weaponProfile,
                    s_runtime.trigger.assist,
                    activeWeaponSettings.triggerSmoothing,
                    settings.triggerAimHumanization,
                    settings.triggerAimRecoilControl,
                    ResolveCandidateAlignmentTolerance(
                        snapshot,
                        triggerCandidate,
                        settings.triggerAimBone),
                    &lastMoveX,
                    &lastMoveY,
                    &triggerAngularError,
                    activeWeaponSettings.hitchanceEnabled ? activeWeaponSettings.hitchance : 0.0f,
                    &dynamicConVars);
                if (triggerMovementResult == MoveResult::Queued) {
                    s_runtime.trigger.movementIssuedGeneration =
                        triggerEvidenceGeneration;
                    s_runtime.trigger.waiting = false;
                    s_runtime.trigger.alignedPawn = 0;
                    s_runtime.trigger.alignedTicks = 0;
                }
                runtimePhase = triggerMovementResult == MoveResult::Failed
                    ? target::RuntimePhase::OutputFailed
                    : target::RuntimePhase::Tracking;
            }
        } else {
            s_runtime.trigger.assist = {};
            s_runtime.trigger.movementIssuedGeneration = 0;
        }

        // Once a trigger click has been reserved, no other producer may move
        // the mouse through its DOWN/UP/latch window. Different Aim/Trigger
        // points or RCS policies must never shift the crosshair mid-shot.
        if (triggerRequested &&
            (s_runtime.trigger.held ||
             s_runtime.trigger.clickReserved ||
             s_runtime.trigger.shotLatched)) {
            triggerMovementOwned = true;
        }

        const bool aimRequested = settings.aimbotEnabled && aimKeyDown;
        if (aimRequested) {
            aimCandidate = SelectTarget(
                snapshot,
                settings.aimBone,
                screenWidth,
                screenHeight,
                radius,
                settings.aimVisibleOnly,
                settings.aimPredictive,
                s_runtime.aim.targetPawn,
                0,
                localPingMs,
                interpolationSeconds,
                true,
                activeWeaponSettings.aimMinimumDamage,
                activeWeaponSettings.aimAutowall,
                &dynamicConVars, &s_aimSelection);
            if (!aimCandidate.player) {
                ResetAimState();
                runtimePhase = target::RuntimePhase::NoTarget;
            } else {
                if (s_runtime.aim.targetPawn != aimCandidate.player->pawn) {
                    s_runtime.aim = {};
                }
                s_runtime.aim.targetPawn = aimCandidate.player->pawn;
                const bool aimMayCorrectObservedShot = !triggerAssistMayOwnMovement &&
                    settings.aimRecoilControl && s_runtime.trigger.shotLatched &&
                    s_runtime.trigger.shotObserved && !s_runtime.trigger.held &&
                    !s_runtime.trigger.clickReserved &&
                    aimCandidate.player->pawn == s_runtime.trigger.latchedPawn;
                if (!triggerMovementOwned || aimMayCorrectObservedShot) {
                    const MoveResult movementResult = MoveToward(
                        snapshot,
                        aimCandidate,
                        weaponProfile,
                        s_runtime.aim,
                        activeWeaponSettings.aimSmoothing,
                        settings.aimHumanization,
                        settings.aimRecoilControl,
                        ResolveCandidateAlignmentTolerance(
                            snapshot,
                            aimCandidate,
                            settings.aimBone),
                        &lastMoveX,
                        &lastMoveY);
                    runtimePhase = movementResult == MoveResult::Failed
                        ? target::RuntimePhase::OutputFailed
                        : target::RuntimePhase::Tracking;
                }
            }
        } else {
            ResetAimState();
            if (settings.aimbotEnabled || settings.triggerbotEnabled)
                runtimePhase = target::RuntimePhase::WaitingForKey;
        }

        const bool triggerTargetValid =
            triggerRequested && triggerCandidate.player &&
            IsEnemyTarget(snapshot, *triggerCandidate.player) &&
            triggerCandidate.player->pawn !=
                s_runtime.trigger.blockedUnobservedPawn;
        const uintptr_t triggerPawn = triggerTargetValid
            ? triggerCandidate.player->pawn
            : 0;
        const bool freshCrosshairIdentity = snapshot.crosshairValid &&
            target::policy::IsFreshSample(
                snapshot.crosshairUpdatedAtUs,
                snapshot.sampledAtUs,
                target::policy::kMaximumTriggerCrosshairAgeUs);
        const bool crosshairConsistent =
            !freshCrosshairIdentity || snapshot.crosshairPawn == 0 ||
            (snapshot.crosshairPlayerIndex == triggerCandidate.slot &&
             snapshot.crosshairPawn == triggerPawn);
        const float triggerTolerance =
            ResolveCandidateAlignmentTolerance(
                snapshot,
                triggerCandidate,
                settings.triggerAimBone);
        const bool pointAligned =
            !settings.triggerAimAssist ||
            (triggerMovementResult == MoveResult::Aligned &&
             std::isfinite(triggerAngularError) &&
             triggerAngularError <= triggerTolerance);
        const bool confirmedAfterMovement =
            !settings.triggerAimAssist ||
            (triggerEvidenceGeneration != 0u &&
             triggerEvidenceGeneration >
                s_runtime.trigger.movementIssuedGeneration);
        const bool triggerSamplesFresh = AreTriggerSamplesFresh(snapshot);
        const bool triggerTargetSamplesFresh =
            triggerTargetValid && AreTriggerTargetSamplesFresh(
                snapshot,
                *triggerCandidate.player);
        const bool physicalLeftButtonFree =
            (!device.physicalButtonsAvailable ||
             (device.physicalButtonMask & 0x01u) == 0u) &&
            !IsKeyDown(0x01);
        const uint64_t weaponTelemetryAgeUs =
            snapshot.localWeaponTelemetryUpdatedAtUs > 0u &&
            snapshot.sampledAtUs >= snapshot.localWeaponTelemetryUpdatedAtUs
                ? snapshot.sampledAtUs -
                    snapshot.localWeaponTelemetryUpdatedAtUs
                : 0u;
        // localRenderTick belongs to the weapon telemetry publication, not to
        // this Target tick. Include both its measured age and the calibrated
        // device dispatch latency when selecting the earliest future seed.
        float fireLeadSeconds = outputLatencySeconds + std::clamp(
            static_cast<float>(weaponTelemetryAgeUs) * 0.000001f,
            0.0f,
            0.050f);
        if (s_runtime.trigger.waiting &&
            s_runtime.trigger.triggerAt > now) {
            fireLeadSeconds += std::chrono::duration<float>(
                s_runtime.trigger.triggerAt - now).count();
        } else if (!s_runtime.trigger.waiting) {
            fireLeadSeconds += static_cast<float>(
                target::policy::SanitizeDelayMs(
                    settings.triggerDelayMs)) * 0.001f;
        }
        const ShotEvaluation shotEvaluation =
            triggerTargetValid && pointAligned && confirmedAfterMovement &&
            triggerSamplesFresh && triggerTargetSamplesFresh
                ? EvaluateShot(
                    snapshot,
                    triggerCandidate,
                    activeWeaponSettings.hitchanceEnabled ? activeWeaponSettings.hitchance : 0.0f,
                    activeWeaponSettings.minimumDamage,
                    activeWeaponSettings.autowall,
                    dynamicConVars,
                    fireLeadSeconds,
                    nullptr,
                    activeWeaponSettings.seedWindowEnabled)
                : ShotEvaluation{};
        const bool shotTraceMatchesTarget =
            shotEvaluation.geometryHit && crosshairConsistent;
        const bool alignmentTraceMatchesTarget =
            triggerTargetValid && crosshairConsistent &&
            DoesCurrentBallisticRayHitCandidate(snapshot, triggerCandidate);
        const bool alignmentEvidence =
            triggerTargetValid && alignmentTraceMatchesTarget && pointAligned &&
            confirmedAfterMovement && triggerSamplesFresh &&
            triggerTargetSamplesFresh &&
            physicalLeftButtonFree && triggerAccuracySettled;

        if (!alignmentEvidence || s_runtime.trigger.shotLatched) {
            s_runtime.trigger.alignedPawn = 0;
            s_runtime.trigger.alignedTicks = 0;
            s_runtime.trigger.lastCrosshairGeneration =
                triggerEvidenceGeneration;
        } else if (s_runtime.trigger.alignedPawn != triggerPawn) {
            s_runtime.trigger.alignedPawn = triggerPawn;
            s_runtime.trigger.alignedTicks = 0;
            s_runtime.trigger.stableSince = now;
        }
        if (alignmentEvidence && !s_runtime.trigger.shotLatched) {
            const uint64_t previousGeneration =
                s_runtime.trigger.lastCrosshairGeneration;
            s_runtime.trigger.alignedTicks =
                target::policy::AdvanceStableSamples(
                    s_runtime.trigger.alignedTicks,
                    triggerEvidenceGeneration,
                    previousGeneration,
                    true);
            if (triggerEvidenceGeneration != 0u &&
                triggerEvidenceGeneration != previousGeneration) {
                s_runtime.trigger.lastCrosshairGeneration =
                    triggerEvidenceGeneration;
            }
        }
        const bool triggerAligned =
            alignmentEvidence &&
            s_runtime.trigger.alignedTicks >=
                weaponProfile.triggerStableTicks &&
            s_runtime.trigger.stableSince !=
                std::chrono::steady_clock::time_point{} &&
            now - s_runtime.trigger.stableSince >=
                kTriggerMinimumStableTime;
        const bool triggerFireGateOpen =
            target::policy::IsTriggerFireGateOpen(
                triggerRequested && triggerShotBudgetReady,
                triggerTargetValid,
                shotTraceMatchesTarget,
                pointAligned && confirmedAfterMovement,
                triggerSamplesFresh && triggerTargetSamplesFresh,
                IsEligibleWeapon(snapshot) && physicalLeftButtonFree &&
                    triggerAccuracySettled && shotEvaluation.damageReady,
                s_runtime.trigger.shotLatched,
                s_runtime.trigger.held || s_runtime.trigger.clickReserved,
                now >= s_runtime.trigger.nextAutoShotAt);
        const bool triggerArmGateOpen =
            triggerRequested && triggerShotBudgetReady && triggerTargetValid && pointAligned &&
            confirmedAfterMovement && triggerSamplesFresh &&
            triggerTargetSamplesFresh && IsEligibleWeapon(snapshot) &&
            physicalLeftButtonFree && triggerAccuracySettled &&
            !s_runtime.trigger.shotLatched &&
            !s_runtime.trigger.held &&
            !s_runtime.trigger.clickReserved &&
            now >= s_runtime.trigger.nextAutoShotAt;
        using FireReason = target::FireBlockReason;
        s_fireDiagnostics.hitchanceEnabled = activeWeaponSettings.hitchanceEnabled;
        s_fireDiagnostics.seedWindowEnabled = activeWeaponSettings.seedWindowEnabled;
        s_fireDiagnostics.hitchancePercent = shotEvaluation.hitchance < 0.0f
            ? -1.0f : shotEvaluation.hitchance * 100.0f;
        s_fireDiagnostics.requiredHitchancePercent = activeWeaponSettings.hitchance;
        s_fireDiagnostics.damage = shotEvaluation.damage;
        s_fireDiagnostics.requiredDamage = activeWeaponSettings.minimumDamage;
        s_fireDiagnostics.seedHitPercent = shotEvaluation.predictedSeedHitFraction < 0.0f
            ? -1.0f : shotEvaluation.predictedSeedHitFraction * 100.0f;
        const auto diagnosticSpread = ResolveWeaponSpread(snapshot, dynamicConVars);
        s_fireDiagnostics.inaccuracy = diagnosticSpread.inaccuracy;
        s_fireDiagnostics.spread = diagnosticSpread.spread;
        auto& fireReason = s_fireDiagnostics.reason;
        if (!triggerRequested) fireReason = FireReason::Inactive;
        else if (s_runtime.trigger.shotLatched || s_runtime.trigger.held ||
                 s_runtime.trigger.clickReserved) fireReason = FireReason::ShotPending;
        else if (!triggerShotBudgetReady) fireReason = FireReason::ActivationSpent;
        else if (!triggerTargetValid) fireReason = FireReason::NoTarget;
        else if (!pointAligned) fireReason = FireReason::Aligning;
        else if (!confirmedAfterMovement) fireReason = FireReason::AwaitingView;
        else if (!triggerSamplesFresh) fireReason = FireReason::StaleLocal;
        else if (!triggerTargetSamplesFresh) fireReason = FireReason::StaleTarget;
        else if (!IsEligibleWeapon(snapshot)) fireReason = FireReason::WeaponNotReady;
        else if (!physicalLeftButtonFree) fireReason = FireReason::ManualFire;
        else if (now < s_runtime.trigger.nextAutoShotAt) fireReason = FireReason::Cooldown;
        else if (!shotEvaluation.damageReady) fireReason = shotEvaluation.reason;
        else if (!crosshairConsistent) fireReason = FireReason::PlayerOccluded;
        else if (!alignmentTraceMatchesTarget) fireReason = FireReason::CapsuleMiss;
        else if (!triggerAligned || !triggerAccuracySettled) fireReason = FireReason::Stabilizing;
        else if (!s_runtime.trigger.waiting || now < s_runtime.trigger.triggerAt)
            fireReason = FireReason::Delay;
        else fireReason = FireReason::Ready;

        if (fireReason == FireReason::Hitchance) {
            const Vector3 centerDelta = triggerCandidate.point - snapshot.localEyePos;
            const Vector3 centerAngles = {
                -std::atan2(centerDelta.z, std::hypot(centerDelta.x, centerDelta.y)) * (180.0f / kPi),
                std::atan2(centerDelta.y, centerDelta.x) * (180.0f / kPi), 0.0f};
            s_fireDiagnostics.centeredHitchancePercent = target::ballistics::CalculateHitchance(
                snapshot.localEyePos - triggerCandidate.predictionOffset, centerAngles,
                *triggerCandidate.player, diagnosticSpread, triggerCandidate.requiredHitgroup) * 100.0f;
        }
        // Expected gate failures are status, not transport errors. Uncomputed
        // stages remain -1 so they cannot be mistaken for missing map geometry.
        if (triggerRequested && triggerTargetValid && pointAligned &&
            fireReason != FireReason::Ready && fireReason != FireReason::Delay &&
            fireReason != FireReason::Stabilizing && fireReason != FireReason::ShotPending) {
            static uint64_t lastFireDiagnosticUs = 0;
            const uint64_t diagnosticNowUs = NowUs();
            if (lastFireDiagnosticUs == 0 || diagnosticNowUs - lastFireDiagnosticUs >= 3000000u) {
                lastFireDiagnosticUs = diagnosticNowUs;
                DmaLogPrintf("Target fire gate: reason=%s slot=%d hc=%.1f/%.1f center=%.1f seed=%.0fpct damage=%.1f/%.1f inaccuracy=%.6f spread=%.6f weapon=%u hc_enabled=%u seed_enabled=%u (-1=not_evaluated).",
                    target::FireBlockReasonName(fireReason), triggerCandidate.slot,
                    s_fireDiagnostics.hitchancePercent, s_fireDiagnostics.requiredHitchancePercent,
                    s_fireDiagnostics.centeredHitchancePercent, s_fireDiagnostics.seedHitPercent,
                    s_fireDiagnostics.damage, s_fireDiagnostics.requiredDamage,
                    diagnosticSpread.inaccuracy, diagnosticSpread.spread,
                    static_cast<unsigned>(snapshot.localWeaponId),
                    static_cast<unsigned>(activeWeaponSettings.hitchanceEnabled),
                    static_cast<unsigned>(activeWeaponSettings.seedWindowEnabled));
            }
        }
        if (!triggerAligned || !triggerArmGateOpen) {
            s_runtime.trigger.waiting = false;
            s_runtime.trigger.targetPawn = triggerPawn;
        } else if (!s_runtime.trigger.waiting ||
                   s_runtime.trigger.targetPawn != triggerPawn) {
            s_runtime.trigger.waiting = true;
            s_runtime.trigger.targetPawn = triggerPawn;
            s_runtime.trigger.triggerAt = now + std::chrono::milliseconds(
                target::policy::SanitizeDelayMs(settings.triggerDelayMs));
        } else if (now >= s_runtime.trigger.triggerAt &&
                   triggerFireGateOpen) {
            // Recheck the physical button immediately before reserving the
            // asynchronous click; the earlier status sample may already be
            // stale by the end of the configured delay.
            if (IsKeyDown(0x01)) {
                s_runtime.trigger.waiting = false;
            } else if (app::input::RequestLeftClick(
                    static_cast<uint32_t>(kTriggerHoldTime.count()))) {
                fireReason = FireReason::Queued;
                s_triggerActivationShotIssued = true;
                s_runtime.trigger.waiting = false;
                s_runtime.trigger.held = true;
                s_runtime.trigger.clickReserved = true;
                s_runtime.trigger.shotLatched = true;
                s_runtime.trigger.shotObserved = false;
                s_runtime.trigger.requireCrosshairExit = true;
                s_runtime.trigger.latchedPawn = triggerPawn;
                s_runtime.trigger.latchedSlot = triggerCandidate.slot;
                s_runtime.trigger.latchedPawnHandle = triggerCandidate.player->pawnHandle;
                s_shotDiagnostics = {triggerCandidate.slot, triggerCandidate.player->health, -1, false, 0};
                s_runtime.trigger.latchedNameHash =
                    target::policy::HashTargetName(
                        triggerCandidate.player->name);
                s_runtime.trigger.latchedHealthBefore =
                    triggerCandidate.player->health;
                s_runtime.trigger.latchedCoreUpdatedAtBefore =
                    triggerCandidate.player->coreUpdatedAtUs;
                s_runtime.trigger.lastPostShotCoreUpdatedAtUs = 0u;
                s_runtime.trigger.lastMissingSnapshotGeneration = 0u;
                s_runtime.trigger.postShotCoreSamples = 0u;
                s_runtime.trigger.missingTargetSamples = 0u;
                s_runtime.trigger.predictedDamage = shotEvaluation.damage;
                s_runtime.trigger.predictedLethal =
                    std::isfinite(shotEvaluation.damage) &&
                    shotEvaluation.damage + 0.001f >=
                        static_cast<float>(
                            triggerCandidate.player->health);
                s_runtime.trigger.lastLatchCrosshairGeneration =
                    snapshot.crosshairGeneration;
                s_runtime.trigger.crosshairExitSamples = 0;
                s_runtime.trigger.ammoBefore = snapshot.localAmmoValid
                    ? snapshot.localAmmoClip
                    : -1;
                s_runtime.trigger.ammoUpdatedAtBefore =
                    snapshot.localAmmoValid
                        ? snapshot.localAmmoUpdatedAtUs
                        : 0;
                s_runtime.trigger.shotsBefore = snapshot.localShotsFired;
                s_runtime.trigger.shotsBeforeValid =
                    snapshot.localShotsFiredValid &&
                    target::policy::IsFreshSample(
                        snapshot.localShotsUpdatedAtUs,
                        snapshot.sampledAtUs,
                        target::policy::kMaximumWeaponStateAgeUs);
                s_runtime.trigger.shotsUpdatedAtBefore =
                    s_runtime.trigger.shotsBeforeValid
                        ? snapshot.localShotsUpdatedAtUs
                        : 0;
                s_runtime.trigger.lastShotTimeBefore =
                    snapshot.localLastShotTime;
                s_runtime.trigger.lastShotTimeBeforeValid =
                    snapshot.localWeaponTelemetryValid &&
                    std::isfinite(snapshot.localLastShotTime) &&
                    target::policy::IsFreshSample(
                        snapshot.localWeaponTelemetryUpdatedAtUs,
                        snapshot.sampledAtUs,
                        target::policy::kMaximumWeaponStateAgeUs);
                s_runtime.trigger.weaponTelemetryUpdatedAtBefore =
                    snapshot.localWeaponTelemetryUpdatedAtUs;
                s_runtime.trigger.previousPostShotAimPunch =
                    snapshot.localAimPunch;
                s_runtime.trigger.previousPostShotAimPunchUpdatedAtUs =
                    snapshot.localAimPunchValid
                        ? snapshot.localAimPunchUpdatedAtUs
                        : 0u;
                s_runtime.trigger.stablePostShotRecoilSamples = 0u;
                const uint32_t latchedNameHash =
                    s_runtime.trigger.latchedNameHash;
                if (s_runtime.trigger.clickSeriesPawn != triggerPawn ||
                    (s_runtime.trigger.clickSeriesNameHash != 0u &&
                     latchedNameHash != 0u &&
                     s_runtime.trigger.clickSeriesNameHash !=
                        latchedNameHash)) {
                    s_runtime.trigger.unobservedRetryCount = 0u;
                }
                s_runtime.trigger.clickSeriesPawn = triggerPawn;
                s_runtime.trigger.clickSeriesNameHash = latchedNameHash;
                s_runtime.trigger.triggerShotIssuedAt = now;
                const float observationSeconds =
                    target::policy::ResolveUnobservedClickTimeoutSeconds(
                        outputLatencySeconds,
                        std::chrono::duration<float>(
                            kTriggerHoldTime).count());
                s_runtime.trigger.shotObservationDeadline = now +
                    std::chrono::duration_cast<
                        std::chrono::steady_clock::duration>(
                            std::chrono::duration<float>(
                                observationSeconds));
                const float outcomeSeconds =
                    target::policy::ResolvePostShotOutcomeDelaySeconds(
                        localPingMs,
                        interpolationSeconds,
                        snapshot.localIntervalPerTick,
                        outputLatencySeconds);
                s_runtime.trigger.targetOutcomeNotBefore = now +
                    std::chrono::duration_cast<
                        std::chrono::steady_clock::duration>(
                            std::chrono::duration<float>(outcomeSeconds));
                s_runtime.trigger.alignedPawn = 0;
                s_runtime.trigger.alignedTicks = 0;
                s_runtime.trigger.lastCrosshairGeneration =
                    triggerEvidenceGeneration;
                s_runtime.trigger.releaseAt =
                    now + kTriggerHoldTime + std::chrono::milliseconds(25);
                s_runtime.trigger.nextAutoShotAt = now +
                    weaponProfile.shotCooldown +
                    std::chrono::duration_cast<
                        std::chrono::steady_clock::duration>(
                            std::chrono::duration<float>(
                                outputLatencySeconds));
            } else {
                s_runtime.trigger.waiting = false;
                s_runtime.trigger.nextAutoShotAt =
                    now + kTriggerRetryBackoff;
                fireReason = FireReason::OutputFailed;
                runtimePhase = target::RuntimePhase::OutputFailed;
            }
        }

        if (!triggerRequested) {
            if (s_runtime.trigger.shotLatched) {
                // Releasing an activation key is not evidence that the last
                // click did not fire; retain its ledger until resolved.
                RequestTriggerRelease();
                s_runtime.trigger.waiting = false;
                s_runtime.trigger.assist = {};
            } else {
                ResetTriggerTracking();
            }
        } else if (triggerCandidate.player &&
                   runtimePhase != target::RuntimePhase::OutputFailed) {
            runtimePhase = target::RuntimePhase::Tracking;
        } else if (!triggerCandidate.player && !aimRequested &&
                   runtimePhase != target::RuntimePhase::OutputFailed) {
            runtimePhase = target::RuntimePhase::NoTarget;
        }

        if ((aimRequested || triggerRequested) &&
            !aimCandidate.player && !triggerCandidate.player) {
            static uint64_t s_lastNoCandidateDiagnosticUs = 0;
            const uint64_t diagnosticNowUs = NowUs();
            if (s_lastNoCandidateDiagnosticUs == 0 ||
                diagnosticNowUs - s_lastNoCandidateDiagnosticUs >= 3000000u) {
                s_lastNoCandidateDiagnosticUs = diagnosticNowUs;
                int validPlayers = 0;
                int enemyPlayers = 0;
                int bonePlayers = 0;
                int freshBonePlayers = 0;
                int visiblePlayers = 0;
                for (const esp::PlayerData& player : snapshot.players) {
                    if (!player.valid || player.pawn == 0 || player.health <= 0)
                        continue;
                    ++validPlayers;
                    if (player.pawn == snapshot.localPawn ||
                        player.team == snapshot.localTeam ||
                        (player.team != 2 && player.team != 3)) {
                        continue;
                    }
                    ++enemyPlayers;
                    if (player.visible)
                        ++visiblePlayers;
                    if (player.hasBones) {
                        ++bonePlayers;
                        if (ArePlayerBonesFresh(snapshot, player))
                            ++freshBonePlayers;
                    }
                }
                const target::physics::Stats geometry =
                    target::physics::GetStats();
                DmaLogPrintf(
                    "[DEBUG] Target diagnostic: no candidate aim=%u trigger=%u keys=%u/%u team=%d players=%d enemies=%d bones=%d fresh_bones=%d visible=%d map=%s geometry=%u fov=%.1f visible_only=%u/%u.",
                    static_cast<unsigned>(aimRequested),
                    static_cast<unsigned>(triggerRequested),
                    static_cast<unsigned>(aimKeyDown),
                    static_cast<unsigned>(triggerKeyDown),
                    snapshot.localTeam,
                    validPlayers,
                    enemyPlayers,
                    bonePlayers,
                    freshBonePlayers,
                    visiblePlayers,
                    snapshot.mapKey,
                    static_cast<unsigned>(geometry.state),
                    radius,
                    static_cast<unsigned>(settings.aimVisibleOnly),
                    static_cast<unsigned>(settings.triggerVisibleOnly));
            }
        }

        const Candidate* statusCandidate =
            triggerMovementOwned && triggerCandidate.player
                ? &triggerCandidate
                : (aimCandidate.player
                    ? &aimCandidate
                    : (triggerCandidate.player ? &triggerCandidate : nullptr));
        PublishStatus(
            runtimePhase,
            aimKeyDown,
            triggerRequested,
            statusCandidate,
            lastMoveX,
            lastMoveY);
    }

    void RuntimeWorkerLoop()
    {
        auto nextTick = std::chrono::steady_clock::now();
        for (;;) {
            float screenWidth = 0.0f;
            float screenHeight = 0.0f;
            {
                std::unique_lock<std::mutex> lock(s_workerMutex);
                if (s_workerCondition.wait_until(lock, nextTick, [] {
                        return s_workerStopping;
                    })) {
                    break;
                }
                screenWidth = s_viewportWidth;
                screenHeight = s_viewportHeight;
            }

            g::TargetSettings settings;
            {
                std::lock_guard<std::recursive_mutex> lock(g::settingsMutex);
                settings = g::targetSettings;
            }
            const bool menuOpen =
                g::menuOpen.load(std::memory_order_relaxed);
            {
                std::lock_guard<std::mutex> lock(s_runtimeMutex);
                TickTarget(
                    screenWidth,
                    screenHeight,
                    settings,
                    menuOpen);
                PublishCompletedStatus();
            }

            nextTick += kRuntimeTickInterval;
            const auto now = std::chrono::steady_clock::now();
            if (nextTick < now - kRuntimeTickInterval)
                nextTick = now + kRuntimeTickInterval;
        }

        std::lock_guard<std::mutex> lock(s_runtimeMutex);
        ResetRuntimeState();
        PublishStatus(target::RuntimePhase::Disabled);
        PublishCompletedStatus();
    }
}

void target::Start()
{
    std::lock_guard<std::mutex> lock(s_workerMutex);
    if (s_workerStarted)
        return;
    s_workerStopping = false;
    s_worker = std::thread(RuntimeWorkerLoop);
    s_workerStarted = true;
}

void target::DrawOverlay()
{
    Start();
    const ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    if (displaySize.x <= 4.0f || displaySize.y <= 4.0f)
        return;

    {
        std::lock_guard<std::mutex> lock(s_workerMutex);
        s_viewportWidth = displaySize.x;
        s_viewportHeight = displaySize.y;
    }
    s_workerCondition.notify_one();

    if (!g::targetEnabled || !g::targetFovEnabled)
        return;
    const policy::FovCircle circle = policy::ResolveFovCircle(
        displaySize.x,
        displaySize.y,
        policy::ResolveConfiguredFov(g::targetFovRadius, g::targetFovPerWeapon,
            g::targetWeaponProfiles[std::clamp(s_activeWeaponProfile.load(std::memory_order_relaxed), 0, 5)].fovRadius));
    if (!circle.valid)
        return;

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    const ImVec2 center(circle.centerX, circle.centerY);
    drawList->AddCircle(
        center,
        circle.radius,
        IM_COL32(0, 0, 0, 150),
        128,
        3.2f);
    drawList->AddCircle(
        center,
        circle.radius,
        ImGui::ColorConvertFloat4ToU32(ImVec4(
            g::targetFovColor[0],
            g::targetFovColor[1],
            g::targetFovColor[2],
            g::targetFovColor[3])),
        128,
        1.5f);
}

target::RuntimeStatus target::GetRuntimeStatus()
{
    std::lock_guard<std::mutex> lock(s_statusMutex);
    return s_publishedStatus;
}

void target::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(s_workerMutex);
        s_workerStopping = true;
    }
    s_workerCondition.notify_all();
    if (s_worker.joinable())
        s_worker.join();
    {
        std::lock_guard<std::mutex> lock(s_workerMutex);
        s_workerStarted = false;
        s_workerStopping = false;
        s_viewportWidth = 0.0f;
        s_viewportHeight = 0.0f;
    }
    {
        std::lock_guard<std::mutex> lock(s_runtimeMutex);
        ResetRuntimeState();
        s_status = {};
        s_aimActivation = {};
        s_triggerActivation = {};
        s_triggerActivationShotIssued = false;
        s_activationScene = 0;
        s_activationPawn = 0;
        s_shotDiagnostics = {};
        s_aimSelection = {};
        s_triggerSelection = {};
        s_fireDiagnostics = {};
        PublishCompletedStatus();
    }
    target::convars::Reset();
    physics::Shutdown();
}
