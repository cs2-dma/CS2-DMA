#pragma once

#include <cstdint>

namespace esp::state
{
    inline constexpr int kSnapshotRingSlotCount = 8;

    inline int NormalizeSnapshotSlotIndex(int idx, int slotCount = kSnapshotRingSlotCount)
    {
        if (slotCount <= 0)
            return 0;
        idx %= slotCount;
        if (idx < 0)
            idx += slotCount;
        return idx;
    }

    inline bool IsSnapshotSlotIndexValid(int idx, int slotCount = kSnapshotRingSlotCount)
    {
        return idx >= 0 && idx < slotCount;
    }

    inline int AdvanceSnapshotWriteCursor(
        int usedIdx,
        int publishedIdx,
        int slotCount = kSnapshotRingSlotCount)
    {
        if (slotCount < 2)
            return 0;

        int nextIdx = NormalizeSnapshotSlotIndex(usedIdx + 1, slotCount);
        publishedIdx = NormalizeSnapshotSlotIndex(publishedIdx, slotCount);
        if (nextIdx == publishedIdx)
            nextIdx = NormalizeSnapshotSlotIndex(nextIdx + 1, slotCount);
        return nextIdx;
    }

    inline bool ShouldApplyPlayerVisibilityFrame(
        uint64_t frameSceneSerial,
        uint64_t snapshotSceneSerial,
        uint64_t frameCaptureUs,
        uint64_t snapshotCaptureUs,
        uint64_t nowUs,
        uint64_t maxAgeUs = 100000u)
    {
        return frameSceneSerial == snapshotSceneSerial &&
               frameCaptureUs >= snapshotCaptureUs &&
               frameCaptureUs > 0 &&
               nowUs >= frameCaptureUs &&
               (nowUs - frameCaptureUs) <= maxAgeUs;
    }

    inline bool ShouldApplyCameraLocalPosition(
        uint64_t frameSceneSerial,
        uint64_t snapshotSceneSerial,
        uintptr_t positionPawn,
        uintptr_t snapshotPawn,
        bool positionValid,
        uint64_t positionUpdatedUs,
        uint64_t nowUs,
        uint64_t maxAgeUs)
    {
        return frameSceneSerial == snapshotSceneSerial &&
               positionPawn != 0 && positionPawn == snapshotPawn &&
               positionValid && positionUpdatedUs > 0 &&
               nowUs >= positionUpdatedUs &&
               nowUs - positionUpdatedUs <= maxAgeUs;
    }
}
