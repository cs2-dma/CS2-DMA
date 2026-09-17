#pragma once

#include "Features/ESP/esp.h"

#include <cmath>
#include <cstdint>

namespace esp::data
{
    // The scene-node origin and pose must come from the same read batch and
    // pawn. Never borrow an anchor from another pose/respawn.
    struct BonePoseAnchor {
        uintptr_t pawn = 0;
        uint64_t sampleTimeUs = 0;
        Vector3 position = {};
    };

    inline bool HasMatchingBonePoseAnchor(const BonePoseAnchor& anchor,
        uintptr_t pawn, uint64_t sampleTimeUs) noexcept
    {
        return pawn != 0 && anchor.pawn == pawn && sampleTimeUs != 0 &&
            anchor.sampleTimeUs == sampleTimeUs &&
            std::isfinite(anchor.position.x) && std::isfinite(anchor.position.y) &&
            std::isfinite(anchor.position.z) &&
            std::fabs(anchor.position.x) < 1000000.0f &&
            std::fabs(anchor.position.y) < 1000000.0f &&
            std::fabs(anchor.position.z) < 1000000.0f;
    }

    inline constexpr uint64_t kBoneAnchorMismatchHoldUs = 250000u;

    inline constexpr bool IsReusableBoneSample(uint64_t sampleUs, uint64_t nowUs, uint64_t maximumAgeUs)
    {
        return sampleUs != 0 && nowUs >= sampleUs && nowUs - sampleUs <= maximumAgeUs;
    }

    enum class BonePlausibilityRejectReason {
        None = 0,
        MissingInput,
        MissingCoreBones,
        InvalidCoreBones,
        HeadNotAbovePelvis,
        InvalidChestOrder,
        InvalidSpineOrder,
        HeelTooHigh,
        TooFarFromAnchor,
        InvalidPoseAnchor,
    };

    inline const char* BonePlausibilityRejectReasonName(BonePlausibilityRejectReason reason)
    {
        switch (reason) {
        case BonePlausibilityRejectReason::None:
            return "none";
        case BonePlausibilityRejectReason::MissingInput:
            return "missing_input";
        case BonePlausibilityRejectReason::MissingCoreBones:
            return "missing_core_bones";
        case BonePlausibilityRejectReason::InvalidCoreBones:
            return "invalid_core_bones";
        case BonePlausibilityRejectReason::HeadNotAbovePelvis:
            return "head_not_above_pelvis";
        case BonePlausibilityRejectReason::InvalidChestOrder:
            return "invalid_chest_order";
        case BonePlausibilityRejectReason::InvalidSpineOrder:
            return "invalid_spine_order";
        case BonePlausibilityRejectReason::HeelTooHigh:
            return "heel_too_high";
        case BonePlausibilityRejectReason::TooFarFromAnchor:
            return "too_far_from_anchor";
        case BonePlausibilityRejectReason::InvalidPoseAnchor:
            return "invalid_pose_anchor";
        default:
            return "unknown";
        }
    }

    struct BonePlausibilityInput {
        const Vector3* storedBones = nullptr;
        Vector3 anchorPosition = {};
        bool hasAnchorPosition = false;
    };

    struct BonePlausibilityResult {
        bool plausible = false;
        BonePlausibilityRejectReason rejectReason = BonePlausibilityRejectReason::MissingInput;
    };

    inline bool IsValidBoneWorldPos(const Vector3& pos)
    {
        return std::isfinite(pos.x) &&
               std::isfinite(pos.y) &&
               std::isfinite(pos.z) &&
               (std::fabs(pos.x) + std::fabs(pos.y) + std::fabs(pos.z) > 1.0f);
    }

    inline BonePlausibilityResult EvaluateStoredBonePlausibility(const BonePlausibilityInput& input)
    {
        if (!input.storedBones)
            return {};

        constexpr int pelvisIdx = PlayerStoredBoneIndex(PELVIS);
        constexpr int chestIdx = PlayerStoredBoneIndex(CHEST);
        constexpr int spine2Idx = PlayerStoredBoneIndex(SPINE2);
        constexpr int headIdx = PlayerStoredBoneIndex(HEAD);
        constexpr int leftHeelIdx = PlayerStoredBoneIndex(FOOT_HEEL_L);
        constexpr int rightHeelIdx = PlayerStoredBoneIndex(FOOT_HEEL_R);

        const Vector3& pelvis = input.storedBones[pelvisIdx];
        const Vector3& head = input.storedBones[headIdx];
        const float coreSpan = (head - pelvis).Length();
        // A real joint can lie at world (0,0,0), especially on training maps.
        // Reject a zero/corrupt pose by its geometry, not by either joint alone.
        if (!std::isfinite(pelvis.x) || !std::isfinite(pelvis.y) || !std::isfinite(pelvis.z) ||
            !std::isfinite(head.x) || !std::isfinite(head.y) || !std::isfinite(head.z) ||
            !std::isfinite(coreSpan) || coreSpan < 1.0f || coreSpan > 256.0f) {
            return {
                false,
                BonePlausibilityRejectReason::InvalidCoreBones,
            };
        }

        if (head.z <= pelvis.z) {
            return {
                false,
                BonePlausibilityRejectReason::HeadNotAbovePelvis,
            };
        }

        const bool hasChest = chestIdx >= 0 && IsValidBoneWorldPos(input.storedBones[chestIdx]);
        const bool hasSpine2 = spine2Idx >= 0 && IsValidBoneWorldPos(input.storedBones[spine2Idx]);
        if (hasChest) {
            const Vector3& chest = input.storedBones[chestIdx];
            if (head.z <= chest.z || chest.z <= pelvis.z) {
                return {
                    false,
                    BonePlausibilityRejectReason::InvalidChestOrder,
                };
            }
        } else if (hasSpine2) {
            const Vector3& spine2 = input.storedBones[spine2Idx];
            if (head.z <= spine2.z || spine2.z <= pelvis.z) {
                return {
                    false,
                    BonePlausibilityRejectReason::InvalidSpineOrder,
                };
            }
        }

        if (leftHeelIdx >= 0) {
            const Vector3& leftHeel = input.storedBones[leftHeelIdx];
            if (IsValidBoneWorldPos(leftHeel) && leftHeel.z > pelvis.z + 24.0f) {
                return {
                    false,
                    BonePlausibilityRejectReason::HeelTooHigh,
                };
            }
        }
        if (rightHeelIdx >= 0) {
            const Vector3& rightHeel = input.storedBones[rightHeelIdx];
            if (IsValidBoneWorldPos(rightHeel) && rightHeel.z > pelvis.z + 24.0f) {
                return {
                    false,
                    BonePlausibilityRejectReason::HeelTooHigh,
                };
            }
        }

        if (input.hasAnchorPosition && std::isfinite(input.anchorPosition.x) &&
            std::isfinite(input.anchorPosition.y) && std::isfinite(input.anchorPosition.z)) {
            const float pelvisDelta = (pelvis - input.anchorPosition).Length();
            const float headDelta = (head - input.anchorPosition).Length();
            if (pelvisDelta > 240.0f || headDelta > 320.0f) {
                return {
                    false,
                    BonePlausibilityRejectReason::TooFarFromAnchor,
                };
            }
        }

        return {
            true,
            BonePlausibilityRejectReason::None,
        };
    }

    inline BonePlausibilityResult EvaluateAnchoredBonePose(const Vector3* bones,
        const BonePoseAnchor& anchor, uintptr_t pawn, uint64_t sampleUs)
    {
        // Successful byte counts are not proof that the scene/pose is usable.
        // In particular, never validate a fresh NaN anchor against old origin.
        if (!HasMatchingBonePoseAnchor(anchor, pawn, sampleUs))
            return {false, BonePlausibilityRejectReason::InvalidPoseAnchor};
        return EvaluateStoredBonePlausibility({bones, anchor.position, true});
    }

    inline BonePlausibilityResult EvaluateRetainedBonePose(const Vector3* bones,
        const BonePoseAnchor& retained, const BonePoseAnchor& current,
        uintptr_t pawn, uint64_t sampleUs)
    {
        const auto original = EvaluateAnchoredBonePose(bones, retained, pawn, sampleUs);
        if (!original.plausible)
            return original;
        if (current.sampleTimeUs >= sampleUs &&
            HasMatchingBonePoseAnchor(current, pawn, current.sampleTimeUs)) {
            // Do not draw a previous life/teleport at its old location, even
            // when m_vOldOrigin has not caught up with the scene node yet.
            if ((current.position - retained.position).Length() > 128.0f)
                return {false, BonePlausibilityRejectReason::TooFarFromAnchor};
            return EvaluateStoredBonePlausibility({bones, current.position, true});
        }
        return original;
    }

    inline BonePlausibilityRejectReason SelectReportedBoneRejectReason(
        BonePlausibilityRejectReason currentReason,
        BonePlausibilityRejectReason previousReason,
        BonePlausibilityRejectReason lastGoodReason)
    {
        if (currentReason != BonePlausibilityRejectReason::None &&
            currentReason != BonePlausibilityRejectReason::MissingInput) {
            return currentReason;
        }
        if (previousReason != BonePlausibilityRejectReason::None &&
            previousReason != BonePlausibilityRejectReason::MissingInput) {
            return previousReason;
        }
        if (lastGoodReason != BonePlausibilityRejectReason::None &&
            lastGoodReason != BonePlausibilityRejectReason::MissingInput) {
            return lastGoodReason;
        }
        return BonePlausibilityRejectReason::MissingInput;
    }

    inline bool ShouldClearCurrentBoneScratch(BonePlausibilityRejectReason reason)
    {
        return reason == BonePlausibilityRejectReason::InvalidCoreBones ||
               reason == BonePlausibilityRejectReason::InvalidPoseAnchor ||
               reason == BonePlausibilityRejectReason::TooFarFromAnchor;
    }

    inline bool ShouldHoldBoneAnchorMismatch(
        bool samePawn,
        bool structurallyPlausible,
        uint64_t mismatchSinceUs,
        uint64_t nowUs) noexcept
    {
        return samePawn &&
               structurallyPlausible &&
               mismatchSinceUs != 0u &&
               nowUs >= mismatchSinceUs &&
               (nowUs - mismatchSinceUs) <= kBoneAnchorMismatchHoldUs;
    }

    inline uint8_t BoneRejectPersistenceThreshold(BonePlausibilityRejectReason reason)
    {
        return reason == BonePlausibilityRejectReason::InvalidCoreBones ||
               reason == BonePlausibilityRejectReason::TooFarFromAnchor
            ? 2u
            : 3u;
    }

    inline bool ShouldReportBoneReject(
        BonePlausibilityRejectReason reason,
        uint8_t streak,
        bool reasonChanged,
        bool cooldownElapsed)
    {
        return reason != BonePlausibilityRejectReason::None &&
               reason != BonePlausibilityRejectReason::MissingInput &&
               streak >= BoneRejectPersistenceThreshold(reason) &&
               (reasonChanged || cooldownElapsed);
    }
}
