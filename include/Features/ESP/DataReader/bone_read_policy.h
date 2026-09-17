#pragma once

#include "Features/ESP/esp.h"
#include "Features/ESP/DataReader/player_slot_policy.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <type_traits>

namespace esp::data
{
    inline constexpr size_t kBoneTransformStrideBytes = 32u;
    inline constexpr int kBoneReadFirst = esp::ORIGIN;
    inline constexpr int kBoneReadTransformCount = esp::BONE_MAX;
    inline constexpr int kBoneReadLast =
        kBoneReadFirst + kBoneReadTransformCount - 1;
    inline constexpr size_t kBoneBatchBytesPerPlayer =
        static_cast<size_t>(kBoneReadTransformCount) *
        kBoneTransformStrideBytes;
    // Refresh every eligible player on each scheduled bone tick. Splitting the
    // batch across ticks halves pose cadence and is visible as skeleton jitter.
    inline constexpr int kBoneReadSpreadTicks = 1;
    // Validate each scheduled pose's source in the SAME scatter as its payload.
    // Training respawns can recycle arrays without changing the pawn/scene node.
    inline constexpr uint64_t kBonePointerValidationUs = 0u;
    inline constexpr int kBonePointerValidationBudgetPerTick = 64;

    inline bool HasBoneCoreDiscontinuity(const esp::PlayerData& previous,
        uintptr_t pawn, uint32_t handle, int health, uint8_t lifeState,
        const Vector3& position) noexcept
    {
        if (!pawn || previous.pawn != pawn || previous.pawnHandle != handle ||
            health <= 0 || lifeState != 0)
            return false;
        return previous.health <= 0 ||
            (IsFiniteVec(previous.position) && IsFiniteVec(position) &&
             (position - previous.position).Length() >= 512.0f);
    }

    struct BoneReadBatch
    {
        std::array<std::byte, kBoneBatchBytesPerPlayer> transforms = {};
    };

    struct BoneQuaternion
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 1.0f;
    };

    struct BoneTransform
    {
        Vector3 position = {};
        float scale = 1.0f;
        BoneQuaternion rotation = {};
    };

    static_assert(sizeof(BoneTransform) == kBoneTransformStrideBytes);

    inline constexpr bool IsBonePointerReadComplete(size_t bytesRead)
    {
        return bytesRead == sizeof(uintptr_t);
    }

    inline constexpr bool IsBonePoseReadComplete(size_t bytesRead)
    {
        return bytesRead == kBoneBatchBytesPerPlayer;
    }

    static_assert(std::is_trivially_copyable_v<Vector3>);

    inline constexpr bool IsBoneCoveredByReadBatch(int boneId)
    {
        return boneId >= kBoneReadFirst && boneId <= kBoneReadLast;
    }

    inline constexpr bool AreStoredBonesCoveredByReadBatch()
    {
        for (const int boneId : esp::kPlayerStoredBoneIds) {
            if (!IsBoneCoveredByReadBatch(boneId))
                return false;
        }
        return true;
    }

    static_assert(AreStoredBonesCoveredByReadBatch());

    inline const std::byte* FindBoneInReadBatch(
        const BoneReadBatch& batch,
        int boneId)
    {
        if (!IsBoneCoveredByReadBatch(boneId))
            return nullptr;
        return batch.transforms.data() +
               static_cast<size_t>(boneId - kBoneReadFirst) *
                   kBoneTransformStrideBytes;
    }

    inline void UnpackStoredBones(
        const BoneReadBatch& batch,
        Vector3* output)
    {
        if (!output)
            return;
        for (int i = 0; i < esp::kPlayerStoredBoneCount; ++i) {
            const std::byte* source =
                FindBoneInReadBatch(batch, esp::kPlayerStoredBoneIds[i]);
            if (source)
                std::memcpy(&output[i], source, sizeof(Vector3));
            else
                output[i] = {};
        }
    }

    inline bool UnpackBoneTransform(
        const BoneReadBatch& batch,
        int boneId,
        BoneTransform& output)
    {
        const std::byte* source = FindBoneInReadBatch(batch, boneId);
        if (!source) {
            output = {};
            return false;
        }
        std::memcpy(&output, source, sizeof(output));
        return IsFiniteVec(output.position) &&
            std::isfinite(output.rotation.x) &&
            std::isfinite(output.rotation.y) &&
            std::isfinite(output.rotation.z) &&
            std::isfinite(output.rotation.w);
    }

    // The hitbox reader has already resolved model-specific bone remapping.
    // Reuse those actual transform indices when fixed core indices are invalid;
    // do not fabricate a head/pelvis from bounding-box offsets.
    inline bool RemapStoredCoreBones(const BoneReadBatch& batch,
        const esp::HitboxCapsule* hitboxes, int count, Vector3* output)
    {
        if (!hitboxes || !output || count < 1 || count > esp::kMaximumPlayerHitboxes)
            return false;
        bool headFound = false;
        bool pelvisFound = false;
        for (int i = 0; i < count; ++i) {
            if (!hitboxes[i].valid)
                continue;
            int storedBone = -1;
            switch (hitboxes[i].index) {
            case 0: storedBone = esp::HEAD; break;
            case 1: storedBone = esp::NECK; break;
            case 2: storedBone = esp::PELVIS; break;
            case 3: storedBone = esp::SPINE1; break;
            case 4: storedBone = esp::SPINE2; break;
            case 5: storedBone = esp::CHEST; break;
            default: continue;
            }
            BoneTransform transform;
            if (!UnpackBoneTransform(batch, hitboxes[i].bone, transform))
                return false;
            output[esp::PlayerStoredBoneIndex(storedBone)] = transform.position;
            if (storedBone == esp::NECK)
                output[esp::PlayerStoredBoneIndex(esp::AIM_NECK)] = transform.position;
            headFound = headFound || storedBone == esp::HEAD;
            pelvisFound = pelvisFound || storedBone == esp::PELVIS;
        }
        return headFound && pelvisFound;
    }

    inline Vector3 RotateByQuaternion(
        const BoneQuaternion& input,
        const Vector3& point)
    {
        float x = input.x;
        float y = input.y;
        float z = input.z;
        float w = input.w;
        const float lengthSquared = x * x + y * y + z * z + w * w;
        if (!std::isfinite(lengthSquared) || lengthSquared < 0.000001f)
            return point;
        const float inverseLength = 1.0f / std::sqrt(lengthSquared);
        x *= inverseLength;
        y *= inverseLength;
        z *= inverseLength;
        w *= inverseLength;
        const Vector3 quaternionVector{x, y, z};
        const Vector3 firstCross{
            quaternionVector.y * point.z - quaternionVector.z * point.y,
            quaternionVector.z * point.x - quaternionVector.x * point.z,
            quaternionVector.x * point.y - quaternionVector.y * point.x,
        };
        const Vector3 secondCross{
            quaternionVector.y * firstCross.z - quaternionVector.z * firstCross.y,
            quaternionVector.z * firstCross.x - quaternionVector.x * firstCross.z,
            quaternionVector.x * firstCross.y - quaternionVector.y * firstCross.x,
        };
        return point + firstCross * (2.0f * w) + secondCross * 2.0f;
    }

    inline constexpr int SelectBoneReadBatchSlotLimit(int eligibleSlotCount)
    {
        if (eligibleSlotCount <= 0)
            return 0;
        return (std::min)(
            eligibleSlotCount,
            (eligibleSlotCount + kBoneReadSpreadTicks - 1) /
                kBoneReadSpreadTicks);
    }

    inline bool IsBoneSlotReadDue(
        uint64_t lastReadUs,
        uint64_t nowUs,
        uint64_t normalIntervalUs)
    {
        return lastReadUs == 0 ||
               nowUs < lastReadUs ||
               (nowUs - lastReadUs) >= normalIntervalUs;
    }

    inline bool IsBonePointerValidationDue(
        uint64_t lastValidationUs,
        uint64_t nowUs,
        bool cacheReady)
    {
        return !cacheReady ||
               lastValidationUs == 0 ||
               nowUs < lastValidationUs ||
               (nowUs - lastValidationUs) >= kBonePointerValidationUs;
    }

    inline bool SelectBonePointerValidation(
        bool validationDue,
        bool cacheReady,
        int& stableValidationBudget)
    {
        if (!validationDue)
            return false;

        // Cache misses are correctness-critical and must resolve immediately.
        // Stable cache revalidation is deliberately spread across ticks.
        if (!cacheReady)
            return true;
        if (stableValidationBudget <= 0)
            return false;

        --stableValidationBudget;
        return true;
    }

    inline constexpr uint8_t kBoneSceneNodeHoldStreak = 12u;
    inline constexpr uint8_t kBoneSceneNodeBulkHoldStreak = 32u;
    inline constexpr uint8_t kBoneArrayHoldStreak = 4u;
    inline constexpr uint8_t kBoneArrayBulkHoldStreak = 16u;
    inline constexpr uint32_t kPerSlotBoneLocalResetFrames = 30u;
    inline constexpr uint8_t kPerSlotBoneProbeEscalations = 8u;
    inline constexpr uint64_t kPerSlotBoneLocalResetCooldownUs = 90000u;
    inline constexpr uint64_t kPerSlotBoneProbeCooldownUs = 12000000u;

    inline uint8_t SelectSceneNodeHoldStreak(bool inBulkRecovery)
    {
        return inBulkRecovery ? kBoneSceneNodeBulkHoldStreak
                              : kBoneSceneNodeHoldStreak;
    }

    inline uint8_t SelectBoneArrayHoldStreak(bool inBulkRecovery)
    {
        return inBulkRecovery ? kBoneArrayBulkHoldStreak
                              : kBoneArrayHoldStreak;
    }

    inline bool ShouldReuseCachedPointerOnZero(uint8_t zeroStreak, uint8_t holdStreak)
    {
        return zeroStreak <= holdStreak;
    }

    inline bool IsBoneSlotLive(bool liveCoreForBones, bool cachedLiveForBones)
    {
        return liveCoreForBones || cachedLiveForBones;
    }

    inline bool IsBoneSlotStale(
        uintptr_t sceneNode,
        uintptr_t boneArray,
        bool boneReadReturnedZeros)
    {
        return sceneNode == 0 ||
               boneArray == 0 ||
               boneReadReturnedZeros;
    }

    inline bool IsCooldownElapsed(uint64_t lastUs, uint64_t nowUs, uint64_t cooldownUs)
    {
        return lastUs == 0 ||
               nowUs <= lastUs ||
               (nowUs - lastUs) >= cooldownUs;
    }

    inline bool IsPerSlotBoneLocalResetDue(
        uint32_t staleStreak,
        uint64_t lastLocalResetUs,
        uint64_t nowUs)
    {
        return staleStreak >= kPerSlotBoneLocalResetFrames &&
               IsCooldownElapsed(lastLocalResetUs, nowUs, kPerSlotBoneLocalResetCooldownUs);
    }

    inline bool ShouldEscalatePerSlotBoneProbe(uint8_t staleEscalation)
    {
        return staleEscalation >= kPerSlotBoneProbeEscalations;
    }

    inline bool IsPerSlotBoneProbeDue(uint64_t lastProbeUs, uint64_t nowUs)
    {
        return IsCooldownElapsed(lastProbeUs, nowUs, kPerSlotBoneProbeCooldownUs);
    }
}
