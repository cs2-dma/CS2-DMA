#include <Windows.h>
#include "Features/ESP/bone_reader.h"
#include "Features/ESP/DataReader/bone_read_policy.h"
#include "Features/ESP/Diagnostics/narrow_debug.h"
#include "Features/ESP/esp_local_state.h"
#include "Features/ESP/esp_helpers.h"
#include "app/Core/memory_address.h"
#include "Features/ESP/DataReader/intervals.h"
#include "app/Core/globals.h"
#include "Game/Offsets/runtime_offsets.h"
#include <DMALibrary/Memory/Memory.h>
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace esp;

namespace esp {

    void BoneReader::ReadBones(
        void* voidHandle,
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
        bool& _boneReadsActiveTick
    ) {
        VMMDLL_SCATTER_HANDLE handle = static_cast<VMMDLL_SCATTER_HANDLE>(voidHandle);
        const auto& ofs = runtime_offsets::Get();

        // Helper check for pointers
        auto isLikelyGamePointer = [](uintptr_t val) -> bool {
            return app::memory_address::IsLikelyGamePointer(val);
        };
        auto isValidWorldPos = [](const Vector3& pos) -> bool {
            return std::isfinite(pos.x) && std::isfinite(pos.y) && std::isfinite(pos.z) &&
                   (std::fabs(pos.x) + std::fabs(pos.y) + std::fabs(pos.z) > 1.0f);
        };
        auto refreshDmaCaches = [](
            const char* reason,
            DmaRefreshTier tier,
            bool force,
            DmaRefreshTrigger trigger) {
            RefreshDmaCaches(reason, tier, force, trigger);
        };
        const esp::diagnostics::NarrowDebugOptions narrowDebug;

        auto logUpdateDataIssue = [](const char* stage, const char* reason) {
            const auto curStatus = static_cast<esp::GameStatus>(s_gameStatus.load(std::memory_order_relaxed));
            static uint32_t s_detailCount = 0;
            if (!esp::diagnostics::ShouldEmitUpdateIssue(
                    reason,
                    curStatus == esp::GameStatus::Ok,
                    s_detailCount)) {
                return;
            }

            DmaLogPrintf(
                "[WARN] UpdateData stage=%s reason=%s",
                stage ? stage : "unknown",
                reason ? reason : "unknown");
        };

        #include "DataReader/bone_reads.inl"
    }
}
