#pragma once

namespace esp::render
{
    enum class BoneConfidence {
        None = 0,
        Low,
        High,
    };

    enum class SkeletonDrawDecision {
        Skip = 0,
        Partial,
        Full,
    };

    enum class SkeletonSkipReason {
        None = 0,
        UnreliableBones,
        NotEnoughProjectedSegments,
    };

    struct SkeletonGateInput {
        bool reliableBones = false;
        int projectedSegmentCount = 0;
    };

    struct SkeletonGateResult {
        SkeletonDrawDecision decision = SkeletonDrawDecision::Skip;
        SkeletonSkipReason skipReason = SkeletonSkipReason::UnreliableBones;
        BoneConfidence confidence = BoneConfidence::None;
    };

    inline SkeletonGateResult EvaluateSkeletonGate(const SkeletonGateInput& input)
    {
        SkeletonGateResult result = {};

        if (!input.reliableBones) {
            result.skipReason = SkeletonSkipReason::UnreliableBones;
            return result;
        }

        if (input.projectedSegmentCount >= 5) {
            result.decision = SkeletonDrawDecision::Full;
            result.skipReason = SkeletonSkipReason::None;
            result.confidence = BoneConfidence::High;
            return result;
        }

        if (input.projectedSegmentCount >= 3) {
            result.decision = SkeletonDrawDecision::Partial;
            result.skipReason = SkeletonSkipReason::None;
            result.confidence = BoneConfidence::Low;
            return result;
        }

        result.skipReason = SkeletonSkipReason::NotEnoughProjectedSegments;
        result.confidence = BoneConfidence::Low;
        return result;
    }

    inline bool ShouldDrawSkeleton(const SkeletonGateResult& result)
    {
        return result.decision == SkeletonDrawDecision::Full ||
               result.decision == SkeletonDrawDecision::Partial;
    }
}
