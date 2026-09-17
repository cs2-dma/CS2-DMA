#include <Windows.h>
#include "Features/ESP/player_data_reader.h"
#include "Features/ESP/DataReader/bone_read_policy.h"
#include "Features/ESP/DataReader/bone_plausibility.h"
#include "Features/ESP/esp_local_state.h"
#include "Features/ESP/esp_helpers.h"
#include "Features/ESP/bone_reader.h"
#include "app/Core/memory_address.h"
#include "Features/ESP/weapon_catalog.h"
#include "Features/ESP/DataReader/intervals.h"
#include "Features/ESP/DataReader/base_recovery_policy.h"
#include "Features/ESP/DataReader/bomb_policy.h"
#include "Features/ESP/DataReader/player_commit_policy.h"
#include "Features/ESP/DataReader/player_core_policy.h"
#include "Features/ESP/DataReader/player_flag_policy.h"
#include "Features/ESP/DataReader/player_hierarchy_policy.h"
#include "Features/ESP/DataReader/population_watchdog_policy.h"
#include "Features/ESP/DataReader/player_repair_policy.h"
#include "Features/ESP/DataReader/player_slot_policy.h"
#include "Features/ESP/DataReader/scene_transition_policy.h"
#include "Features/ESP/DataReader/visibility_policy.h"
#include "Features/ESP/DataReader/world_domain_policy.h"
#include "Features/ESP/DataReader/world_marker_policy.h"
#include "Features/ESP/DataReader/zero_population_policy.h"
#include "Features/ESP/Diagnostics/narrow_debug.h"
#include "Features/ESP/Recovery/dma_recovery_policy.h"
#include "Features/ESP/Recovery/process_identity_policy.h"
#include "Features/ESP/Recovery/reset_policy.h"
#include "Features/ESP/Render/draw_policy.h"
#include "Features/ESP/State/snapshot_ring.h"
#include "Features/ESP/Worker/worker_policy.h"
#include "app/Config/config.h"
#include "app/Core/fallback_log.h"
#include "app/Core/globals.h"
#include "app/Input/primary_keyboard.h"
#include "app/Config/project_paths.h"
#include "app/Config/user_state.h"
#include "Features/Radar/map_registry.h"
#include "Features/WebRadar/webradar.h"
#include "Game/Offsets/runtime_offsets.h"
#include "Game/Schema/structs.h"
#include <DMALibrary/Memory/Memory.h>
#include <imgui.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <exception>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <bitset>
#include <numbers>

using namespace esp;

static std::chrono::steady_clock::time_point s_lastMapPersistTime;

namespace {
    VMMDLL_SCATTER_HANDLE s_dataScatterHandle = nullptr;
    VMMDLL_SCATTER_HANDLE s_engineScatterHandle = nullptr;

    void UpdatePeak(std::atomic<uint64_t>& peak, uint64_t value)
    {
        uint64_t previous = peak.load(std::memory_order_relaxed);
        while (previous < value &&
               !peak.compare_exchange_weak(
                   previous,
                   value,
                   std::memory_order_relaxed,
                   std::memory_order_relaxed)) {
        }
    }

    VMMDLL_SCATTER_HANDLE CreateTrackedScatterHandle(Memory& memory)
    {
        const auto startedAt = std::chrono::steady_clock::now();
        VMMDLL_SCATTER_HANDLE handle = memory.CreateScatterHandle();
        const uint64_t elapsedUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - startedAt)
                .count());
        esp::s_scatterHandleCreateTotalUs.fetch_add(elapsedUs, std::memory_order_relaxed);
        esp::s_scatterHandleCreateCount.fetch_add(1, std::memory_order_relaxed);
        UpdatePeak(esp::s_scatterHandleCreatePeakUs, elapsedUs);
        return handle;
    }

    void CloseTrackedScatterHandle(Memory& memory, VMMDLL_SCATTER_HANDLE handle)
    {
        if (!handle)
            return;

        const auto startedAt = std::chrono::steady_clock::now();
        memory.CloseScatterHandle(handle);
        const uint64_t elapsedUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - startedAt)
                .count());
        esp::s_scatterHandleCloseTotalUs.fetch_add(elapsedUs, std::memory_order_relaxed);
        esp::s_scatterHandleCloseCount.fetch_add(1, std::memory_order_relaxed);
        UpdatePeak(esp::s_scatterHandleClosePeakUs, elapsedUs);
    }

    void CloseReaderScatterHandles(Memory& memory)
    {
        if (s_dataScatterHandle) {
            CloseTrackedScatterHandle(memory, s_dataScatterHandle);
            s_dataScatterHandle = nullptr;
        }
        if (s_engineScatterHandle) {
            CloseTrackedScatterHandle(memory, s_engineScatterHandle);
            s_engineScatterHandle = nullptr;
        }
    }

    struct EntityBlockCache {
        static constexpr uint32_t kBlockCount = 64;

        uintptr_t owner = 0;
        uint64_t resetSerial = 0;
        uintptr_t blocks[kBlockCount] = {};

        void Synchronize(uintptr_t entityList, uint64_t sceneResetSerial) noexcept
        {
            if (owner == entityList && resetSerial == sceneResetSerial)
                return;

            owner = entityList;
            resetSerial = sceneResetSerial;
            std::memset(blocks, 0, sizeof(blocks));
        }

        uintptr_t Get(uint32_t block) const noexcept
        {
            return block < kBlockCount ? blocks[block] : 0;
        }

        void Store(uint32_t block, uintptr_t entry) noexcept
        {
            if (block < kBlockCount)
                blocks[block] = entry;
        }

        void Invalidate(uint32_t block) noexcept
        {
            Store(block, 0);
        }
    };

    struct PlayerReadScratch {
        uintptr_t controllers[64] = {};
        uint32_t pawnHandles[64] = {};
        char names[64][128] = {};
        uint32_t pings[64] = {};
        uintptr_t moneyServices[64] = {};
        int moneys[64] = {};
        uintptr_t pawnEntries[64] = {};
        uintptr_t pawns[64] = {};
        uintptr_t prevCachedControllers[64] = {};
        uintptr_t prevCachedPawnEntries[64] = {};
        uint32_t prevCachedPawnHandles[64] = {};
        uintptr_t prevCachedPawns[64] = {};
        int playerRefreshSlots[64] = {};
        bool playerRefreshSlotMask[64] = {};
        bool hierarchyWouldEvict[64] = {};
        bool hierarchyRefreshable[64] = {};
        bool hierarchyPrevHadAnything[64] = {};
        bool hierarchyControllerCompatible[64] = {};
        bool hierarchyHoldAgeAllowed[64] = {};
        uint32_t pawnControllerHandles[64] = {};
        bool hierarchyIdentityRejected[64] = {};
        int playerResolvedSlots[64] = {};
        int healths[64] = {};
        int armors[64] = {};
        int teams[64] = {};
        bool liveTeamReads[64] = {};
        bool coreArmorReadsQueued[64] = {};
        bool coreTeamReadsQueued[64] = {};
        DWORD coreHealthBytesRead[64] = {};
        DWORD coreArmorBytesRead[64] = {};
        DWORD coreTeamBytesRead[64] = {};
        DWORD coreLifeStateBytesRead[64] = {};
        DWORD corePositionBytesRead[64] = {};
        DWORD coreVelocityBytesRead[64] = {};
        uint8_t lifeStates[64] = {};
        Vector3 positions[64] = {};
        bool coreReadFresh[64] = {};
        bool coreReadPlausible[64] = {};
        bool coreReadAlive[64] = {};
        uintptr_t sceneNodes[64] = {};
        uintptr_t weaponServices[64] = {};
        uint32_t activeWeaponHandles[64] = {};
        uintptr_t activeWeaponEntries[64] = {};
        uintptr_t activeWeapons[64] = {};
        int inventoryWeaponCounts[64] = {};
        uintptr_t inventoryWeaponHandleArrays[64] = {};
        uint32_t inventoryWeaponHandles[64][esp::kMaxInventoryWeapons] = {};
        uintptr_t inventoryWeaponEntries[64][esp::kMaxInventoryWeapons] = {};
        uintptr_t inventoryWeapons[64][esp::kMaxInventoryWeapons] = {};
        uint16_t inventoryWeaponIds[64][esp::kMaxInventoryWeapons] = {};
        bool inventoryHasBombBySlot[64] = {};
        uint16_t weaponIds[64] = {};
        int ammoClips[64] = {};
        bool bombCarrierBySlot[64] = {};
        uintptr_t itemServices[64] = {};
        uint8_t hasDefuserFlags[64] = {};
        uint8_t gunGameImmunityFlags[64] = {};
        DWORD gunGameImmunityBytesRead[64] = {};
        bool gunGameImmunityReadFresh[64] = {};
        uint8_t scopedFlags[64] = {};
        uint8_t defusingFlags[64] = {};
        float flashBangTimes[64] = {};
        float flashDurations[64] = {};
        DWORD scopedFlagBytesRead[64] = {};
        DWORD defusingFlagBytesRead[64] = {};
        DWORD flashBangTimeBytesRead[64] = {};
        DWORD flashDurationBytesRead[64] = {};
        bool scopedReadFresh[64] = {};
        bool defusingReadFresh[64] = {};
        bool flashReadFresh[64] = {};
        Vector3 eyeAnglesPerPlayer[64] = {};
        Vector3 velocities[64] = {};
        bool velocityReadFresh[64] = {};
        uint32_t spottedMasks[64][2] = {};
        DWORD spottedMaskBytesRead[64] = {};
        bool spottedReadFresh[64] = {};
        uintptr_t observerServices[64] = {};
        uint32_t observerTargets[64] = {};
        uint32_t observerModes[64] = {};
        uint32_t observerPawnHandles[64] = {};
        uint32_t observerPlayerPawnHandles[64] = {};
        uint32_t observerBasePawnHandles[64] = {};
        bool moneyServiceRefreshMask[64] = {};
        bool itemServiceRefreshMask[64] = {};
        bool observerServiceRefreshMask[64] = {};
        SpectatorEntry resolvedSpectators[64] = {};
        int inventoryPlayerSlots[64] = {};
        bool inventoryPlayerSlotAdded[64] = {};
        bool activeMetaRefreshSlots[64] = {};
        uint16_t activeMetaWeaponIds[64] = {};
        bool inventoryChainDirty[64] = {};
        bool inventoryHandlesDirty[64] = {};
        bool inventoryEntriesDirty[64] = {};
        bool inventoryEntitiesDirty[64] = {};
        bool commitRefreshed[64] = {};
        bool activeCountedSlots[64] = {};
        bool hasBoneData[64] = {};
        uint64_t boneSampleTimeUs[64] = {};
        esp::HitboxCapsule hitboxes[64][esp::kMaximumPlayerHitboxes] = {};
        uint8_t hitboxCounts[64] = {};
        bool hasHitboxData[64] = {};
        uint64_t hitboxSampleTimeUs[64] = {};
    };

    thread_local PlayerReadScratch s_playerReadScratch;
    thread_local EntityBlockCache s_entityBlockCache;
    thread_local Vector3 s_boneReadScratch[64][esp::kPlayerStoredBoneCount] = {};
    thread_local WorldMarker s_worldMarkerScratch[esp::data::kWorldMarkerScratchCapacity] = {};
}

using esp::weapons::IsKnifeItemId;
using esp::weapons::IsPrimaryWeaponItemId;
using esp::weapons::WeaponNameFromItemId;

// Include DataReader helper inlines
#include "Helpers/basic_helpers.inl"
#include "Features/Radar/Calibration/ini_helpers.inl"
#include "Features/Radar/Calibration/get_radar_profiles_path.inl"
#include "Features/Radar/Calibration/build_map_key_from_bounds.inl"
#include "Features/Radar/Calibration/load_radar_calibration_for_map.inl"
#include "Features/Radar/Calibration/save_radar_calibration_for_map.inl"
#include "Features/Radar/Calibration/handle_map_calibration.inl"
#include "Worker/try_recover_dma.inl"

// Include loops
namespace esp {
#include "Worker/data_worker_loop.inl"
#include "Worker/camera_worker_loop.inl"
}

// Include update data logic
#include "DataReader/update_data.inl"
#include "Core/get_web_radar_snapshot.inl"
