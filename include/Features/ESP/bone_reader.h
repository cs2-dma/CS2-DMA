#pragma once
#include <cstdint>
#include "Features/ESP/esp.h"
#include "Features/ESP/DataReader/bone_plausibility.h"
#include "Game/Schema/structs.h"

namespace esp {
    class BoneReader {
    public:
        BoneReader() = default;
        ~BoneReader() = default;

        void ReadBones(
            void* handle, // VMMDLL_SCATTER_HANDLE passed as void*
            bool wantsEspSkeleton,
            bool wantsEspShowTeammates,
            const uintptr_t* pawns,
            const uint32_t* pawnHandles,
            const int* healths,
            const uint8_t* lifeStates,
            const Vector3* positions,
            const int* teams,
            const bool* liveTeamReads,
            const int* playerResolvedSlots,
            int playerResolvedSlotCount,
            bool localTeamLiveResolved,
            int localControllerTeam,
            bool localTeamLikelySwitched,
            bool allowScheduledBoneReads,
            uintptr_t* sceneNodes,
            bool* hasBoneData,
            uint64_t* boneSampleTimeUs,
            data::BonePoseAnchor* poseAnchors,
            Vector3 (*allBones)[kPlayerStoredBoneCount],
            HitboxCapsule (*allHitboxes)[kMaximumPlayerHitboxes],
            uint8_t* hitboxCounts,
            bool* hasHitboxData,
            uint64_t* hitboxSampleTimeUs,
            bool& boneReadsActiveTick
        );
    };
}
