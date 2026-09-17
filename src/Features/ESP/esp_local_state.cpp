#include <Windows.h>
#include "Features/ESP/esp_local_state.h"
#include "Features/ESP/esp_helpers.h"
#include "Features/ESP/DataReader/deferred_lane_policy.h"
#include "Features/ESP/DataReader/player_commit_policy.h"
#include "Features/Target/target_policy.h"
#include "Features/ESP/Recovery/dma_cache_profile.h"
#include "Features/ESP/Recovery/dma_refresh_policy.h"
#include "Features/ESP/State/snapshot_ring.h"
#include "Features/ESP/Worker/worker_policy.h"
#include "app/Core/globals.h"
#include "app/Config/config.h"
#include "app/Platform/overlay.h"
#include "Game/Offsets/runtime_offsets.h"
#include <DMALibrary/Memory/Memory.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>

namespace esp {
    namespace {
        std::mutex s_dataSettingsSnapshotMutex;
        DataSettingsSnapshot s_dataSettingsSnapshot = {};
        bool s_dataSettingsSnapshotInitialized = false;
        std::mutex s_readQualityMutex;
        worker::CounterIntervalSample<4> s_readQualityInterval;
    }

    void UpdateReadQualityTelemetry(uint64_t nowUs)
    {
        // Called only by the data worker. Sampling is once a second, including
        // idle mode, and requires no extra DMA requests or hot-path read lock.
        static worker::CounterInterval<4> sampler;
        if (!sampler.Due(nowUs)) return;
        const auto sample = sampler.Observe({
            Memory::DMA_SCATTER_REQUESTS.load(std::memory_order_relaxed),
            Memory::DMA_SCATTER_INCOMPLETE_REQUESTS.load(std::memory_order_relaxed),
            Memory::DMA_SCATTER_PARTIAL_BATCHES.load(std::memory_order_relaxed),
            Memory::DMA_SCATTER_SETUP_FAILURES.load(std::memory_order_relaxed)
        }, nowUs);
        std::lock_guard<std::mutex> lock(s_readQualityMutex);
        s_readQualityInterval = sample;
    }

    void PublishDataSettingsSnapshot()
    {
        DataSettingsSnapshot snapshot;
        {
            std::lock_guard<std::recursive_mutex> lock(g::settingsMutex);
            snapshot.espSkeleton = g::espEnabled && g::espSkeleton;
            snapshot.espShowTeammates = g::espShowTeammates;
            snapshot.espWeaponIconNoKnife = g::espWeaponIconNoKnife;
            snapshot.espBombInfo = g::espBombInfo;
            snapshot.radarShowBomb = g::radarShowBomb;
            snapshot.espWeapon = g::espEnabled && g::espWeapon;
            snapshot.espWeaponAmmo = snapshot.espWeapon && g::espWeaponAmmo;
            snapshot.espWeaponIcon = snapshot.espWeapon && g::espWeaponIcon;
            snapshot.espName = g::espEnabled && g::espFlags && g::espName;
            snapshot.radarSpectatorList = g::radarSpectatorList;
            snapshot.espFlags = g::espEnabled && g::espFlags;
            snapshot.espFlagMoney = g::espFlagMoney;
            snapshot.espFlagScoped = g::espFlagScoped;
            snapshot.espFlagDefusing = g::espFlagDefusing;
            snapshot.espFlagBlind = g::espFlagBlind;
            snapshot.espFlagKit = g::espFlagKit;
            snapshot.radarEnabled = g::radarEnabled;
            snapshot.radarShowAngles = g::radarShowAngles;
            snapshot.espVisibilityColoring = g::espEnabled && g::espVisibilityColoring;
            snapshot.targetNeedsBones = target::policy::NeedsBoneData(
                g::targetEnabled,
                g::targetAimbotEnabled,
                g::targetTriggerbotEnabled,
                g::targetTriggerAutoShot);
            snapshot.targetNeedsVisibility = target::policy::NeedsVisibilityData(
                g::targetEnabled,
                g::targetAimbotEnabled,
                g::targetAimVisibleOnly,
                g::targetTriggerbotEnabled,
                g::targetTriggerVisibleOnly,
                g::targetTriggerAutoShot);
            snapshot.targetNeedsRecoil =
                g::targetEnabled &&
                ((g::targetAimbotEnabled && g::targetAimRecoilControl) ||
                 g::targetTriggerbotEnabled);
            snapshot.targetNeedsVelocity =
                g::targetEnabled &&
                ((g::targetAimbotEnabled && g::targetAimPredictive) ||
                 g::targetTriggerbotEnabled);
            snapshot.targetNeedsWeaponState =
                g::targetEnabled &&
                (g::targetAimbotEnabled || g::targetTriggerbotEnabled);
            snapshot.webRadarEnabled = g::webRadarEnabled;
            snapshot.webRadarRemoteEnabled = g::webRadarRemoteEnabled;
            snapshot.espItemEnabledMask = g::espItemEnabledMask;
            snapshot.espItem = g::espItem;
            snapshot.espWorld = g::espWorld;
            snapshot.espWorldProjectiles = g::espWorldProjectiles;
        }

        std::lock_guard<std::mutex> lock(s_dataSettingsSnapshotMutex);
        s_dataSettingsSnapshot = std::move(snapshot);
        s_dataSettingsSnapshotInitialized = true;
    }

    bool TryReadDataSettingsSnapshot(DataSettingsSnapshot& out)
    {
        std::unique_lock<std::mutex> lock(
            s_dataSettingsSnapshotMutex,
            std::try_to_lock);
        if (!lock.owns_lock() || !s_dataSettingsSnapshotInitialized)
            return false;
        out = s_dataSettingsSnapshot;
        return true;
    }

    std::atomic<bool> s_dmaCacheProfileVerified{false};
    std::atomic<uint8_t> s_dmaCacheMode{static_cast<uint8_t>(DmaCacheMode::Maintenance)};
    std::atomic<bool> s_dmaBackgroundRefreshEnabled{false};
    std::atomic<uint64_t> s_dmaCacheProfileLastAttemptUs{0};
    std::atomic<uint32_t> s_attachedCs2ProcessId{0};
    std::mutex s_dmaCacheProfileMutex;

    void SetAttachedCs2ProcessId(uint32_t processId)
    {
        s_attachedCs2ProcessId.store(processId, std::memory_order_release);
    }

    uint32_t GetAttachedCs2ProcessId()
    {
        return s_attachedCs2ProcessId.load(std::memory_order_acquire);
    }

    bool ApplyDmaRuntimeCacheProfile(DmaCacheMode mode, bool force)
    {
        if (!mem.vHandle) {
            s_dmaCacheProfileVerified.store(false, std::memory_order_release);
            s_dmaBackgroundRefreshEnabled.store(false, std::memory_order_release);
            s_dmaCacheProfileLastAttemptUs.store(0, std::memory_order_release);
            return false;
        }

        const auto requestedMode = static_cast<uint8_t>(mode);
        uint64_t attemptNowUs = SubsystemNowUs();
        bool sameMode =
            s_dmaCacheMode.load(std::memory_order_relaxed) == requestedMode;
        bool profileVerified =
            s_dmaCacheProfileVerified.load(std::memory_order_acquire);
        if (!recovery::ShouldAttemptDmaCacheProfileApply(
                force,
                sameMode,
                profileVerified,
                s_dmaCacheProfileLastAttemptUs.load(std::memory_order_acquire),
                attemptNowUs)) {
            return profileVerified;
        }

        std::lock_guard<std::mutex> lock(s_dmaCacheProfileMutex);
        attemptNowUs = SubsystemNowUs();
        sameMode =
            s_dmaCacheMode.load(std::memory_order_relaxed) == requestedMode;
        profileVerified =
            s_dmaCacheProfileVerified.load(std::memory_order_acquire);
        if (!recovery::ShouldAttemptDmaCacheProfileApply(
                force,
                sameMode,
                profileVerified,
                s_dmaCacheProfileLastAttemptUs.load(std::memory_order_acquire),
                attemptNowUs)) {
            return profileVerified;
        }
        s_dmaCacheProfileLastAttemptUs.store(
            attemptNowUs,
            std::memory_order_release);

        ULONG64 refreshEnabledValue = 1;
        const bool refreshStateKnown =
            VMMDLL_ConfigGet(
                mem.vHandle,
                VMMDLL_OPT_CONFIG_IS_REFRESH_ENABLED,
                &refreshEnabledValue) != FALSE;
        const bool backgroundRefreshEnabled =
            !refreshStateKnown || refreshEnabledValue != 0;
        s_dmaBackgroundRefreshEnabled.store(
            backgroundRefreshEnabled,
            std::memory_order_release);

        // Interval profiles require the VMM refresh scheduler. Treat a disabled
        // scheduler as a configuration failure instead of reporting a verified
        // profile while translation caches can remain stale indefinitely.
        if (refreshStateKnown && !backgroundRefreshEnabled) {
            s_dmaCacheMode.store(requestedMode, std::memory_order_relaxed);
            s_dmaCacheProfileVerified.store(false, std::memory_order_release);
            return false;
        }

        struct CacheOption {
            ULONG64 option = 0;
            ULONG64 value = 0;
        };

        const auto& profile = recovery::CacheProfileForMode(mode);
        const CacheOption options[] = {
            { VMMDLL_OPT_CONFIG_TICK_PERIOD, profile.tickPeriodMs },
            { VMMDLL_OPT_CONFIG_READCACHE_TICKS, profile.readCacheTicks },
            { VMMDLL_OPT_CONFIG_TLBCACHE_TICKS, profile.tlbCacheTicks },
            { VMMDLL_OPT_CONFIG_PROCCACHE_TICKS_PARTIAL, profile.processPartialTicks },
            { VMMDLL_OPT_CONFIG_PROCCACHE_TICKS_TOTAL, profile.processFullTicks },
        };

        bool allApplied = true;
        for (const CacheOption& entry : options) {
            if (!VMMDLL_ConfigSet(mem.vHandle, entry.option, entry.value)) {
                allApplied = false;
                continue;
            }

            ULONG64 observed = 0;
            if (!VMMDLL_ConfigGet(mem.vHandle, entry.option, &observed) || observed != entry.value)
                allApplied = false;
        }
        // Remember failed attempts too. The bounded retry policy repairs transient
        // ConfigSet failures without hammering MemProcFS from the hot data loop.
        s_dmaCacheMode.store(requestedMode, std::memory_order_relaxed);
        s_dmaCacheProfileVerified.store(allApplied, std::memory_order_release);
        return allApplied;
    }

    uint32_t s_activeEntitySlotSize = 0x70u;

    const BonePair skeletonPairs[] = {
        { esp::PELVIS,     esp::SPINE1 },
        { esp::SPINE1,     esp::SPINE2 },
        { esp::SPINE2,     esp::CHEST },
        { esp::CHEST,      esp::NECK },
        { esp::NECK,       esp::HEAD },
        { esp::NECK,       esp::SHOULDER_L },
        { esp::SHOULDER_L, esp::ELBOW_L },
        { esp::ELBOW_L,    esp::HAND_L },
        { esp::NECK,       esp::SHOULDER_R },
        { esp::SHOULDER_R, esp::ELBOW_R },
        { esp::ELBOW_R,    esp::HAND_R },
        { esp::PELVIS,     esp::HIP_L },
        { esp::HIP_L,      esp::KNEE_L },
        { esp::KNEE_L,     esp::FOOT_HEEL_L },
        { esp::PELVIS,     esp::HIP_R },
        { esp::HIP_R,      esp::KNEE_R },
        { esp::KNEE_R,     esp::FOOT_HEEL_R },
    };
    const int skeletonPairCount = static_cast<int>(std::size(skeletonPairs));

    SnapshotSlot s_snapshotSlots[8] = {};
    std::atomic<int> s_publishedSnapshotIdx{0};
    int s_nextSnapshotWriteIdx = 1;
    PlayerVisibilityFrameSlot s_visibilityFrameSlots[4] = {};
    std::atomic<int> s_publishedVisibilityFrameIdx{0};
    std::mutex s_visibilityFramePublishMutex;
    int s_nextVisibilityFrameWriteIdx = 1;

    esp::PlayerData s_players[64] = {};
    esp::PlayerData s_prevPlayers[64] = {};
    esp::PlayerData s_webRadarPlayers[64] = {};
    char       s_localName[128] = {};
    int        s_localPlayerIndex = -1;
    int        s_localTeam = 0;
    uintptr_t  s_localPawn = 0;
    Vector3    s_prevLocalPos = {};
    view_matrix_t s_viewMatrix = {};
    uint64_t s_viewMatrixUpdatedAtUs = 0;
    Vector3    s_localPos = {};
    bool       s_localPosValid = false;
    uint64_t   s_localPosUpdatedAtUs = 0;
    Vector3    s_localViewOffset = { 0.0f, 0.0f, 64.0f };
    bool       s_localViewOffsetValid = false;
    uint64_t   s_localViewOffsetUpdatedAtUs = 0;
    Vector3    s_localAimPunch = {};
    int        s_localShotsFired = 0;
    bool       s_localShotsFiredValid = false;
    uint64_t   s_localShotsUpdatedAtUs = 0;
    bool       s_localAimPunchValid = false;
    uint64_t   s_localAimPunchUpdatedAtUs = 0;
    bool       s_localIsDead = false;
    int        s_localHealth = 0;
    int        s_localArmor = 0;
    int        s_localMoney = 0;
    uint16_t   s_localWeaponId = 0;
    uint32_t   s_localWeaponHandle = 0;
    uintptr_t  s_localWeaponEntity = 0;
    int        s_localAmmoClip = -1;
    bool       s_localAmmoValid = false;
    uint64_t   s_localAmmoUpdatedAtUs = 0;
    uint64_t   s_localWeaponUpdatedAtUs = 0;
    WeaponTelemetry s_localWeaponTelemetry = {};
    bool       s_localHasBomb = false;
    bool       s_localHasDefuser = false;
    uint16_t   s_localGrenadeIds[esp::PlayerData::kMaxGrenades] = {};
    int        s_localGrenadeCount = 0;
    Vector3    s_viewAngles = {};
    float      s_sensitivity = 1.0f;
    float      s_fovSensitivityAdjust = 1.0f;
    Vector3    s_minimapMins = {};
    Vector3    s_minimapMaxs = {};
    bool       s_hasMinimapBounds = false;
    bool       s_localMaskResolved = false;
    uint64_t   s_captureTimeUs = 0;
    uint64_t   s_prevCaptureTimeUs = 0;
    std::atomic<uint64_t> s_playerCoreGeneration{0};
    std::atomic<uint64_t> s_playerCoreCaptureTimeUs{0};
    std::atomic<uint64_t> s_prevPlayerCoreCaptureTimeUs{0};
    std::atomic<uint8_t>  s_playerCoreBatchQuality{0};
    std::atomic<uint64_t> s_playerCoreIncompleteMask{0};
    std::atomic<uint64_t> s_playerCoreInvalidMask{0};
    std::atomic<uint64_t> s_playerCoreBatchHoldCount{0};
    uint64_t   s_playerLastSeenMs[64] = {};
    uint8_t    s_playerInvalidReadStreak[64] = {};
    uint8_t    s_playerDeathConfirmCount[64] = {};

    std::mutex s_dataMutex;
    std::atomic<uint64_t> s_sceneResetSerial{1};
    std::atomic<uint64_t> s_lastSceneResetUs{0};

    std::atomic<uint64_t> s_lastBulkEvictionUs{0};
    std::atomic<uint8_t> s_sceneWarmupState{static_cast<uint8_t>(esp::SceneWarmupState::ColdAttach)};
    std::atomic<uint64_t> s_sceneWarmupEnteredUs{0};
    std::atomic<uint8_t> s_lastRuntimeResetKind{static_cast<uint8_t>(esp::RuntimeResetKind::None)};
    std::atomic<bool> s_lastRuntimeResetPublishedClearedSnapshot{false};
    std::atomic<uint64_t> s_lastRuntimeResetUs{0};
    std::mutex s_runtimeResetMutex;
    char s_lastRuntimeResetReason[96] = {};
    std::atomic<uint64_t> s_mapEpoch{1};
    std::atomic<uint64_t> s_bombEpoch{1};
    uint64_t s_mapFingerprint = 0;

    CameraFrameSlot s_cameraFrameSlots[4] = {};
    std::atomic<int> s_publishedCameraFrameIdx{0};
    std::mutex s_cameraFramePublishMutex;
    int s_nextCameraFrameWriteIdx = 1;

    WorldMarker s_worldMarkers[256] = {};
    int s_worldMarkerCount = 0;
    BombState s_bombState = {};
    SpectatorEntry s_spectators[64] = {};
    int s_spectatorCount = 0;
    uintptr_t s_localPawnFarewellPtr = 0;
    uint32_t s_localPawnFarewellHandle = 0;
    uint64_t s_localPawnFarewellExpiryUs = 0;
    uint32_t s_localPawnHandleLastSeen = 0;
    uint64_t s_lastWorldScanUs = 0;

    uintptr_t s_worldEntityRefs[8192] = {};
    uint32_t s_worldEntitySubclassIds[8192] = {};
    uint16_t s_worldEntityItemIds[8192] = {};
    uint8_t s_worldEntityClassKinds[8192] = {};
    int s_worldTrackedIndices[8192] = {};
    uint16_t s_worldTrackedIndexPos[8192] = {};
    int s_worldTrackedIndexCount = 0;
    uint32_t s_worldSmokeSubclassIds[8] = {};
    uint32_t s_worldMolotovSubclassIds[8] = {};
    uint32_t s_worldDecoySubclassIds[8] = {};
    uint32_t s_worldHeSubclassIds[8] = {};
    uint32_t s_worldInfernoSubclassIds[8] = {};
    bool s_worldSmokeLatched[8192] = {};
    bool s_worldInfernoLatched[8192] = {};
    bool s_worldDecoyLatched[8192] = {};
    bool s_worldExplosiveLatched[8192] = {};
    bool s_worldUtilityHasHistory[8192] = {};
    uint8_t s_worldSmokeEvidenceCount[8192] = {};
    uint8_t s_worldInfernoEvidenceCount[8192] = {};
    uint8_t s_worldDecoyEvidenceCount[8192] = {};
    uint8_t s_worldExplosiveEvidenceCount[8192] = {};
    uint64_t s_worldSmokeStartUs[8192] = {};
    uint64_t s_worldInfernoStartUs[8192] = {};
    uint64_t s_worldDecoyStartUs[8192] = {};
    uint64_t s_worldExplosiveStartUs[8192] = {};
    uint64_t s_worldUtilityDeadlinesUs[8192][4] = {};
    Vector3 s_worldPrevPos[8192] = {};
    int s_worldPrevSmokeTick[8192] = {};
    uint8_t s_worldPrevSmokeActive[8192] = {};
    uint8_t s_worldPrevSmokeVolumeDataReceived[8192] = {};
    uint8_t s_worldPrevSmokeEffectSpawned[8192] = {};
    int s_worldPrevInfernoTick[8192] = {};
    float s_worldPrevInfernoLife[8192] = {};
    int s_worldPrevInfernoFireCount[8192] = {};
    uint8_t s_worldPrevInfernoInPostEffect[8192] = {};
    int s_worldPrevDecoyTick[8192] = {};
    int s_worldPrevDecoyClientTick[8192] = {};
    int s_worldPrevExplodeTick[8192] = {};
    Vector3 s_worldPrevVelocity[8192] = {};
    esp::data::UtilityFieldTimes s_worldUtilityFieldTimes[8192] = {};
    uint64_t s_worldUtilityPositionSampleUs[8192] = {};
    uint64_t s_worldUtilityStationarySinceUs[8192] = {};
    uint8_t s_worldUtilityStationarySamples[8192] = {};
    float s_lastStableIntervalPerTick = 0.015625f;
    float s_lastStableGameTime = 0.0f;
    uint64_t s_lastStableGameTimeUs = 0;

    std::jthread s_dataWorker;
    std::jthread s_cameraWorker;
    std::atomic<bool> s_dataWorkerRunning{false};
    std::atomic<bool> s_cameraWorkerRunning{false};
    std::atomic<bool> s_dataWorkerStopRequested{false};

    std::atomic<bool> s_dmaRecovering{false};
    std::atomic<bool> s_dmaManualRefreshInProgress{false};
    std::atomic<uint64_t> s_dmaManualRefreshLastUs{0};
    std::atomic<uint64_t> s_dmaManualRefreshPeakUs{0};
    std::atomic<uint64_t> s_dmaManualRefreshCount{0};
    std::atomic<uint64_t> s_dmaManualRefreshRecentPeakUs{0};
    std::atomic<uint64_t> s_dmaManualRefreshRecentCount{0};
    std::atomic<uint64_t> s_dmaManualRefreshQueuedCount{0};
    std::atomic<uint64_t> s_dmaManualRefreshQueuedRecentCount{0};
    std::atomic<uint64_t> s_dmaManualRefreshSuppressedCount{0};
    std::atomic<uint64_t> s_dmaManualRefreshSuppressedRecentCount{0};
    std::atomic<uint64_t> s_dmaManualRefreshAvoidedCount{0};
    std::atomic<uint64_t> s_dmaManualRefreshAvoidedRecentCount{0};
    std::atomic<uint64_t> s_dmaManualRefreshCoalescedCount{0};
    std::atomic<uint32_t> s_cameraWorkerPauseRequests{0};
    std::atomic<bool> s_dmaAdminPauseActive{false};
    std::atomic<bool> s_cameraWorkerPaused{false};
    std::atomic<uint64_t> s_cameraWorkerPauseOrphanRecoveryCount{0};
    std::atomic<uint64_t> s_cameraWorkerPauseReleaseImbalanceCount{0};
    std::shared_timed_mutex s_dmaLifecycleMutex;
    std::atomic<uint64_t> s_dmaSessionGeneration{1};
    std::atomic<uint64_t> s_scatterHandleCreateTotalUs{0};
    std::atomic<uint64_t> s_scatterHandleCreatePeakUs{0};
    std::atomic<uint64_t> s_scatterHandleCreateCount{0};
    std::atomic<uint64_t> s_scatterHandleCloseTotalUs{0};
    std::atomic<uint64_t> s_scatterHandleClosePeakUs{0};
    std::atomic<uint64_t> s_scatterHandleCloseCount{0};
    std::atomic<bool> s_dmaRecoveryRequested{false};
    std::atomic<uint64_t> s_dmaRecoveryRequestedAtUs{0};
    std::atomic<uint32_t> s_dmaConsecutiveFailures{0};
    std::atomic<uint32_t> s_dmaConsecutiveDegraded{0};
    std::atomic<uint64_t> s_dmaTotalFailures{0};
    std::atomic<uint64_t> s_dmaTotalDegraded{0};
    std::atomic<uint64_t> s_dmaTotalSuccesses{0};
    std::atomic<uint64_t> s_dmaTotalRecoveries{0};
    std::atomic<uint64_t> s_dmaLastSuccessTick{0};

    std::mutex s_dmaEventMutex;
    DmaEventRecord s_dmaEvents[esp::DmaHealthStats::kMaxEvents] = {};
    uint32_t s_dmaEventWriteIndex = 0;
    uint32_t s_dmaEventCount = 0;
    std::atomic<uint64_t> s_publishCount{0};
    std::atomic<uint64_t> s_publishDropCount{0};
    std::atomic<uint64_t> s_visibilityPublishDropCount{0};
    std::atomic<uint64_t> s_cameraPublishDropCount{0};
    std::atomic<uint64_t> s_settingsSnapshotReuseCount{0};
    std::atomic<uint64_t> s_lastPublishUs{0};
    std::atomic<uint64_t> s_sessionStartUs{0};
    std::atomic<uint64_t> s_dataWorkerCycleUs{0};
    std::atomic<int> s_dataWorkerTargetHz{0};
    std::atomic<int> s_cameraWorkerTargetHz{0};
    std::atomic<bool> s_cameraReadsEnabled{false};
    std::atomic<uint64_t> s_dataWorkerMaxCycleUs{0};
    std::atomic<uint64_t> s_dataWorkerRecentMaxCycleUs{0};
    std::atomic<uint64_t> s_dataWorkerRecentWindowStartUs{0};
    std::atomic<uint64_t> s_dataWorkerCycleP50Us{0};
    std::atomic<uint64_t> s_dataWorkerCycleP95Us{0};
    std::atomic<uint64_t> s_dataWorkerCycleP99Us{0};
    std::atomic<uint64_t> s_dataWorkerDeadlineMissCount{0};
    std::atomic<uint64_t> s_dataWorkerCycleSampleCount{0};
    std::atomic<uint64_t> s_dataWorkerCycleOverBudgetCount{0};
    std::atomic<uint64_t> s_dataWorkerCycleOver5msCount{0};
    std::atomic<uint64_t> s_dataWorkerCycleOver16msCount{0};
    std::atomic<uint64_t> s_dataWorkerLastLoopStartUs{0};
    std::atomic<uint64_t> s_dataWorkerLastLoopEndUs{0};
    std::atomic<uint64_t> s_dataWorkerInFlightSinceUs{0};
    std::atomic<bool> s_dataWorkerUpdateInFlight{false};
    std::atomic<uint64_t> s_cameraWorkerCycleUs{0};
    std::atomic<uint64_t> s_cameraWorkerMaxCycleUs{0};
    std::atomic<uint64_t> s_cameraWorkerRecentMaxCycleUs{0};
    std::atomic<uint64_t> s_cameraWorkerRecentWindowStartUs{0};
    std::atomic<uint64_t> s_cameraWorkerCycleP50Us{0};
    std::atomic<uint64_t> s_cameraWorkerCycleP95Us{0};
    std::atomic<uint64_t> s_cameraWorkerCycleP99Us{0};
    std::atomic<uint64_t> s_cameraWorkerDeadlineMissCount{0};
    std::atomic<uint64_t> s_cameraWorkerCycleSampleCount{0};
    std::atomic<uint64_t> s_cameraWorkerCycleOverBudgetCount{0};
    std::atomic<uint64_t> s_cameraWorkerCycleOver5msCount{0};
    std::atomic<uint64_t> s_cameraWorkerCycleOver16msCount{0};
    std::atomic<uint64_t> s_playerCoreAnomalyCount{0};
    std::atomic<uint64_t> s_playerCoreRecoveredCount{0};
    std::atomic<uint64_t> s_playerCoreGlobalRefreshAvoidedCount{0};
    std::atomic<uint64_t> s_playerUnexpectedEvictionCount{0};
    std::atomic<uint64_t> s_playerExpectedEvictionCount{0};
    std::atomic<int32_t>  s_playerControllerSlotCountStat{0};
    std::atomic<int32_t>  s_playerResolvedSlotCountStat{0};
    std::atomic<int32_t>  s_playerPlausibleCoreSlotCountStat{0};
    std::atomic<int32_t>  s_playerDuplicateIdentityFilteredStat{0};
    std::atomic<int32_t>  s_playerBacklinkMismatchStat{0};
    std::atomic<int32_t>  s_playerHierarchyHeldSlotCount{0};
    std::atomic<int32_t>  s_playerZeroPawnHeldSlotCount{0};
    std::atomic<int32_t>  s_playerCoreHeldSlotCount{0};
    std::atomic<int32_t>  s_activePlayerCount{0};
    std::atomic<int32_t>  s_playerSlotScanLimitStat{64};
    std::atomic<int32_t>  s_playerHierarchyHighWaterSlot{0};
    std::atomic<int32_t>  s_highestEntityIdxStat{0};
    std::atomic<uint32_t> s_entitySlotStrideStat{0x70u};
    std::atomic<int32_t>  s_worldMarkerCountStat{0};
    std::atomic<int32_t>  s_worldTrackedEntityCountStat{0};
    std::atomic<int32_t>  s_worldCandidateCountStat{0};
    std::atomic<int32_t>  s_worldUtilityCandidateCountStat{0};
    std::atomic<int32_t>  s_worldClassifiedCandidateCountStat{0};
    std::atomic<int32_t>  s_worldIdentityPendingCountStat{0};
    std::atomic<uint64_t> s_worldMarkerCapacityDropsStat{0};
    std::atomic<uint64_t> s_worldMarkerReadGapHoldsStat{0};
    std::atomic<uint64_t> s_worldPositionReadMissesStat{0};
    std::atomic<int32_t>  s_visibilityFreshSlots{0};
    std::atomic<int32_t>  s_visibilityVisibleSlots{0};
    std::atomic<int32_t>  s_visibilityMaskSlots{0};
    std::atomic<int32_t>  s_visibilityCrosshairSlot{-1};
    std::atomic<uint64_t> s_visibilityLastCommitUs{0};
    std::atomic<bool>     s_visibilityCrosshairValid{false};
    std::atomic<uint64_t> s_visibilityFrameGeneration{0};
    std::atomic<bool>     s_visibilityEnabled{false};
    std::atomic<bool>     s_visibilityLocalMaskResolved{false};
    std::atomic<uint64_t> s_lastWorldScanCommittedUs{0};
    std::atomic<uint32_t> s_bombDebugFlags{0};
    std::atomic<uint32_t> s_bombDebugSourceFlags{0};
    std::atomic<uint64_t> s_bombDebugPositionSampleUs{0};
    std::atomic<uint64_t> s_bombDropPublicationDebug{0};
    std::atomic<uint32_t> s_bombDebugRawFlags{0};
    std::atomic<uint8_t>  s_bombDebugConfidence{0};
    std::atomic<int32_t>  s_bombDebugDefuserSlot{-1};
    std::atomic<int32_t>  s_bombDebugBlowLeftMs{-1};
    std::atomic<int32_t>  s_bombDebugDefuseLeftMs{-1};

    std::atomic<uint64_t> s_stageTimingSequence{0};
    std::atomic<uint64_t> s_stageEngineUs{0};
    std::atomic<uint64_t> s_stageBaseReadsUs{0};
    std::atomic<uint64_t> s_stagePlayerReadsUs{0};
    std::atomic<uint64_t> s_stagePlayerHierarchyUs{0};
    std::atomic<uint64_t> s_stagePlayerCoreUs{0};
    std::atomic<uint64_t> s_stagePlayerRepairUs{0};
    std::atomic<uint64_t> s_stageCommitStateUs{0};
    std::atomic<uint64_t> s_stagePlayerAuxUs{0};
    std::atomic<uint64_t> s_stageInventoryUs{0};
    std::atomic<uint64_t> s_stageBoneReadsUs{0};
    std::atomic<uint64_t> s_stageBombScanUs{0};
    std::atomic<uint64_t> s_stageWorldScanUs{0};
    std::atomic<uint64_t> s_stageWorldScanLastUs{0};
    std::atomic<uint64_t> s_stageCommitEnrichUs{0};
    std::atomic<uint64_t> s_stagePlayerAuxLastUs{0};
    std::atomic<uint64_t> s_stageInventoryLastUs{0};
    std::atomic<uint64_t> s_stageBoneReadsLastUs{0};
    std::atomic<uint64_t> s_stagePlayerAuxLastAtUs{0};
    std::atomic<uint64_t> s_stageInventoryLastAtUs{0};
    std::atomic<uint64_t> s_stageBoneReadsLastAtUs{0};
    std::atomic<uint32_t> s_stageBonePoseSlots{0};
    std::atomic<uint32_t> s_stageBonePointerValidationSlots{0};
    std::atomic<uint32_t> s_stageBonePoseRanges{0};
    std::atomic<uint32_t> s_stageBonePoseBytes{0};
    std::atomic<uint8_t> s_gameStatus{static_cast<uint8_t>(esp::GameStatus::WaitCs2)};
    std::atomic<bool> s_engineStatusResolved{false};
    std::atomic<int32_t> s_engineSignOnState{-1};
    std::atomic<int32_t> s_engineLocalPlayerSlot{-1};
    std::atomic<int32_t> s_engineMaxClients{0};
    std::atomic<bool> s_engineBackgroundMap{false};
    std::atomic<bool> s_engineMenu{false};
    std::atomic<bool> s_engineInGame{false};

    std::atomic<uint8_t> s_subsystemStates[5] = {};
    std::atomic<uint32_t> s_subsystemFailureStreaks[5] = {};
    std::atomic<uint64_t> s_subsystemLastGoodUs[5] = {};

    EspEventRecord s_espEventRing[4096] = {};
    std::mutex s_espEventMutex;
    std::atomic<uint32_t> s_espEventWriteIndex{0};
    std::atomic<uint32_t> s_espEventCount{0};

    std::string s_activeMapKey;
    std::atomic<uint64_t> s_lastLiveMapNameSeenUs{0};
    float s_lastSavedMapRotation = 0.0f;
    float s_lastSavedMapScale = 1.0f;
    float s_lastSavedMapOffsetX = 0.0f;
    float s_lastSavedMapOffsetY = 0.0f;
    float s_activeMapBaseOffsetX = 0.0f;
    float s_activeMapBaseOffsetY = 0.0f;
    bool s_activeMapOverviewAvailable = false;
    float s_activeMapOverviewPosX = 0.0f;
    float s_activeMapOverviewPosY = 0.0f;
    float s_activeMapOverviewScale = 0.0f;

    std::mutex s_activeMapMutex;

    std::atomic<uint32_t> s_pendingRefreshFlags{0};
    std::atomic<bool> s_dmaAdminThreadStarted{false};
    std::jthread s_dmaAdminThread;

    uint32_t s_requiredReadFailureCount = 0;

    namespace {
        template <typename Slot, typename Frame, size_t SlotCount>
        bool ReadPublishedFrame(
            Slot (&slots)[SlotCount],
            const std::atomic<int>& publishedIndex,
            Frame& out)
        {
            for (;;) {
                const int readIndex = publishedIndex.load(std::memory_order_acquire);
                if (readIndex < 0 || readIndex >= static_cast<int>(SlotCount)) {
                    out = {};
                    return false;
                }

                Slot& slot = slots[readIndex];
                std::shared_lock lock(slot.mutex);
                if (publishedIndex.load(std::memory_order_acquire) != readIndex)
                    continue;

                out = slot.data;
                return true;
            }
        }

        template <typename Slot, size_t SlotCount, typename WriteFn>
        bool TryPublishFrame(
            Slot (&slots)[SlotCount],
            std::atomic<int>& publishedIndex,
            std::mutex& publishMutex,
            int& nextWriteIndex,
            WriteFn&& writeFrame)
        {
            const std::lock_guard publishLock(publishMutex);
            const int currentPublishedIndex =
                publishedIndex.load(std::memory_order_acquire);
            for (int offset = 0; offset < static_cast<int>(SlotCount); ++offset) {
                const int writeIndex =
                    (nextWriteIndex + offset) % static_cast<int>(SlotCount);
                if (writeIndex == currentPublishedIndex)
                    continue;

                std::unique_lock lock(slots[writeIndex].mutex, std::try_to_lock);
                if (!lock.owns_lock())
                    continue;

                writeFrame(slots[writeIndex].data);
                publishedIndex.store(writeIndex, std::memory_order_release);
                nextWriteIndex =
                    (writeIndex + 1) % static_cast<int>(SlotCount);
                return true;
            }
            return false;
        }
    }

    // Helper Functions
    bool IsLocalSnapshotSlot(
        const EntitySnapshot& snapshot,
        SnapshotPlayerIdentity player) noexcept
    {
        if (player.slotIndex < 0 || player.slotIndex >= 64)
            return false;
        if (snapshot.localPlayerIndex >= 0 &&
            player.slotIndex == snapshot.localPlayerIndex) {
            return true;
        }
        if (snapshot.localPawn != 0 &&
            player.pawn != 0 &&
            player.pawn == snapshot.localPawn) {
            return true;
        }
        return false;
    }

    void AdvanceSnapshotWriteIndex(int usedIdx)
    {
        const int publishedIdx = s_publishedSnapshotIdx.load(std::memory_order_acquire);
        s_nextSnapshotWriteIdx = state::AdvanceSnapshotWriteCursor(usedIdx, publishedIdx);
    }

    bool ReadCurrentSnapshot(EntitySnapshot& out)
    {
        for (;;) {
            const int publishedIdx = s_publishedSnapshotIdx.load(std::memory_order_acquire);
            if (!state::IsSnapshotSlotIndexValid(publishedIdx)) {
                memset(&out, 0, sizeof(out));
                return false;
            }

            SnapshotSlot& slot = s_snapshotSlots[publishedIdx];
            std::shared_lock lock(slot.mutex);
            if (s_publishedSnapshotIdx.load(std::memory_order_acquire) != publishedIdx)
                continue;

            out = slot.data;
            return true;
        }
    }

    void PublishCurrentSnapshot()
    {
        const int publishedIdx = s_publishedSnapshotIdx.load(std::memory_order_acquire);
        int writeIdx = -1;
        std::unique_lock<std::shared_mutex> writeLock;
        for (int offset = 0; offset < state::kSnapshotRingSlotCount; ++offset) {
            const int candidate = state::NormalizeSnapshotSlotIndex(
                s_nextSnapshotWriteIdx + offset);
            if (candidate == publishedIdx)
                continue;

            std::unique_lock candidateLock(
                s_snapshotSlots[candidate].mutex,
                std::try_to_lock);
            if (!candidateLock.owns_lock())
                continue;

            writeIdx = candidate;
            writeLock = std::move(candidateLock);
            break;
        }
        if (writeIdx < 0) {
            s_publishDropCount.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        SnapshotSlot& writeSlot = s_snapshotSlots[writeIdx];
        EntitySnapshot& snap = writeSlot.data;
        const auto warmupState = static_cast<esp::SceneWarmupState>(
            s_sceneWarmupState.load(std::memory_order_relaxed));
        const bool exposePlayerRoster = esp::data::ShouldExposePlayerRoster(
            warmupState == esp::SceneWarmupState::Stable,
            warmupState == esp::SceneWarmupState::Recovery);
        if (exposePlayerRoster) {
            memcpy(snap.players, s_players, sizeof(snap.players));
            memcpy(snap.prevPlayers, s_prevPlayers, sizeof(snap.prevPlayers));
            memcpy(snap.webRadarPlayers, s_webRadarPlayers, sizeof(snap.webRadarPlayers));
        } else {
            memset(snap.players, 0, sizeof(snap.players));
            memset(snap.prevPlayers, 0, sizeof(snap.prevPlayers));
            memset(snap.webRadarPlayers, 0, sizeof(snap.webRadarPlayers));
        }
        memcpy(snap.localName, s_localName, sizeof(snap.localName));
        memset(snap.activeMapKey, 0, sizeof(snap.activeMapKey));
        const ActiveMapStateSnapshot activeMapState = CopyActiveMapState();
        if (!activeMapState.key.empty())
            strncpy_s(snap.activeMapKey, sizeof(snap.activeMapKey), activeMapState.key.c_str(), _TRUNCATE);
        snap.localPlayerIndex = s_localPlayerIndex;
        snap.localTeam = s_localTeam;
        snap.localPawn = s_localPawn;
        snap.localPos = s_localPos;
        snap.localPosValid = s_localPosValid;
        snap.localPosUpdatedAtUs = s_localPosUpdatedAtUs;
        snap.localViewOffset = s_localViewOffset;
        snap.localViewOffsetValid = s_localViewOffsetValid;
        snap.localViewOffsetUpdatedAtUs = s_localViewOffsetUpdatedAtUs;
        snap.localAimPunch = s_localAimPunch;
        snap.localShotsFired = s_localShotsFired;
        snap.localShotsFiredValid = s_localShotsFiredValid;
        snap.localAimPunchValid = s_localAimPunchValid;
        snap.localAimPunchUpdatedAtUs = s_localAimPunchUpdatedAtUs;
        snap.prevLocalPos = s_prevLocalPos;
        snap.localIsDead = s_localIsDead;
        snap.localHealth = s_localHealth;
        snap.localArmor = s_localArmor;
        snap.localMoney = s_localMoney;
        memcpy(&snap.viewMatrix, &s_viewMatrix, sizeof(view_matrix_t));
        snap.viewMatrixUpdatedAtUs = s_viewMatrixUpdatedAtUs;
        snap.viewAngles = s_viewAngles;
        snap.sensitivity = s_sensitivity;
        snap.fovSensitivityAdjust = s_fovSensitivityAdjust;
        snap.minimapMins = s_minimapMins;
        snap.minimapMaxs = s_minimapMaxs;
        snap.hasMinimapBounds = s_hasMinimapBounds;
        snap.localMaskResolved = s_localMaskResolved;
        snap.localWeaponId = s_localWeaponId;
        snap.localWeaponHandle = s_localWeaponHandle;
        snap.localWeaponEntity = s_localWeaponEntity;
        snap.localAmmoClip = s_localAmmoClip;
        snap.localAmmoValid = s_localAmmoValid;
        snap.localAmmoUpdatedAtUs = s_localAmmoUpdatedAtUs;
        snap.localWeaponUpdatedAtUs = s_localWeaponUpdatedAtUs;
        snap.localWeaponTelemetry = s_localWeaponTelemetry;
        snap.localShotsUpdatedAtUs = s_localShotsUpdatedAtUs;
        snap.localHasBomb = s_localHasBomb;
        snap.localHasDefuser = s_localHasDefuser;
        snap.localGrenadeCount = s_localGrenadeCount;
        memcpy(snap.localGrenadeIds, s_localGrenadeIds, sizeof(snap.localGrenadeIds));
        snap.captureTimeUs = s_captureTimeUs;
        snap.prevCaptureTimeUs = s_prevCaptureTimeUs;
        snap.playerCoreGeneration =
            s_playerCoreGeneration.load(std::memory_order_relaxed);
        snap.playerCoreCaptureTimeUs =
            s_playerCoreCaptureTimeUs.load(std::memory_order_relaxed);
        snap.prevPlayerCoreCaptureTimeUs =
            s_prevPlayerCoreCaptureTimeUs.load(std::memory_order_relaxed);
        snap.playerCoreBatchQuality =
            s_playerCoreBatchQuality.load(std::memory_order_relaxed);
        snap.sceneSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
        snap.worldMarkerCount = std::clamp(s_worldMarkerCount, 0, 256);
        memcpy(snap.worldMarkers, s_worldMarkers, sizeof(snap.worldMarkers));
        snap.bombState = s_bombState;
        snap.spectatorCount = std::clamp(s_spectatorCount, 0, 64);
        memcpy(snap.spectators, s_spectators, sizeof(snap.spectators));

        s_publishedSnapshotIdx.store(writeIdx, std::memory_order_release);
        AdvanceSnapshotWriteIndex(writeIdx);
        s_publishCount.fetch_add(1, std::memory_order_relaxed);
        s_lastPublishUs.store(TickNowUs(), std::memory_order_relaxed);
    }

    bool ReadPlayerVisibilityFrame(PlayerVisibilityFrame& out)
    {
        return ReadPublishedFrame(
            s_visibilityFrameSlots,
            s_publishedVisibilityFrameIdx,
            out);
    }

    void PublishPlayerVisibilityFrame()
    {
        const bool published = TryPublishFrame(
            s_visibilityFrameSlots,
            s_publishedVisibilityFrameIdx,
            s_visibilityFramePublishMutex,
            s_nextVisibilityFrameWriteIdx,
            [&](PlayerVisibilityFrame& frame) {
                for (int i = 0; i < 64; ++i) {
                    frame.players[i].pawn = s_players[i].pawn;
                    frame.players[i].visible = s_players[i].visible;
                }
                frame.crosshairValid =
                    s_visibilityCrosshairValid.load(std::memory_order_relaxed);
                frame.crosshairPlayerIndex = frame.crosshairValid
                    ? s_visibilityCrosshairSlot.load(std::memory_order_relaxed)
                    : -1;
                frame.captureTimeUs = s_captureTimeUs;
                frame.crosshairUpdatedAtUs = frame.crosshairValid
                    ? s_captureTimeUs
                    : 0u;
                frame.generation = frame.crosshairValid
                    ? s_visibilityFrameGeneration.fetch_add(
                        1u,
                        std::memory_order_relaxed) + 1u
                    : s_visibilityFrameGeneration.load(
                        std::memory_order_relaxed);
                frame.sceneSerial =
                    s_sceneResetSerial.load(std::memory_order_relaxed);
            });
        if (!published)
            s_visibilityPublishDropCount.fetch_add(1, std::memory_order_relaxed);
    }

    void ResetPlayerVisibilityFrame()
    {
        const bool published = TryPublishFrame(
            s_visibilityFrameSlots,
            s_publishedVisibilityFrameIdx,
            s_visibilityFramePublishMutex,
            s_nextVisibilityFrameWriteIdx,
            [&](PlayerVisibilityFrame& frame) {
                frame = {};
                frame.generation = s_visibilityFrameGeneration.load(
                    std::memory_order_relaxed);
                frame.sceneSerial =
                    s_sceneResetSerial.load(std::memory_order_relaxed);
            });
        if (!published)
            s_visibilityPublishDropCount.fetch_add(1, std::memory_order_relaxed);
    }

    bool ReadCameraFrame(CameraFrame& out)
    {
        return ReadPublishedFrame(
            s_cameraFrameSlots,
            s_publishedCameraFrameIdx,
            out);
    }

    void PublishCameraFrame(const CameraFrame& frame)
    {
        const bool published = TryPublishFrame(
            s_cameraFrameSlots,
            s_publishedCameraFrameIdx,
            s_cameraFramePublishMutex,
            s_nextCameraFrameWriteIdx,
            [&](CameraFrame& target) {
                target = frame;
                const uint64_t scene = s_sceneResetSerial.load(std::memory_order_relaxed);
                if (frame.sceneSerial != scene || !worker::ShouldReadLiveCamera(
                        g::clientBase.load(std::memory_order_relaxed) != 0,
                        s_engineStatusResolved.load(std::memory_order_relaxed),
                        s_engineInGame.load(std::memory_order_relaxed),
                        s_engineMenu.load(std::memory_order_relaxed),
                        s_dmaRecovering.load(std::memory_order_relaxed))) {
                    target = {};
                    target.sceneSerial = scene;
                }
            });
        if (!published)
            s_cameraPublishDropCount.fetch_add(1, std::memory_order_relaxed);
    }

    void ResetCameraSnapshot()
    {
        CameraFrame frame = {};
        frame.sceneSerial =
            s_sceneResetSerial.load(std::memory_order_relaxed);
        PublishCameraFrame(frame);
    }

    void RecordRuntimeReset(esp::RuntimeResetKind kind, const char* reason, bool publishClearedSnapshot)
    {
        const uint64_t nowUs = TickNowUs();
        s_lastRuntimeResetKind.store(static_cast<uint8_t>(kind), std::memory_order_relaxed);
        s_lastRuntimeResetPublishedClearedSnapshot.store(publishClearedSnapshot, std::memory_order_relaxed);
        s_lastRuntimeResetUs.store(nowUs, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(s_runtimeResetMutex);
            strncpy_s(s_lastRuntimeResetReason, sizeof(s_lastRuntimeResetReason), reason ? reason : "unspecified", _TRUNCATE);
        }
        RecordDmaEvent({
            .action = kind == esp::RuntimeResetKind::Soft ? "soft_reset" : "hard_reset",
            .reason = reason
        });
    }

    void ResetRuntimeStateSoft(const char* reason)
    {
        RecordRuntimeReset(esp::RuntimeResetKind::Soft, reason, false);
        BumpSceneReset();
        // Health belongs to a scene just like its positions. Keep cumulative
        // transport errors/events, but do not carry per-scene failure streaks.
        for (uint8_t i = 0; i < static_cast<uint8_t>(RuntimeSubsystem::Count); ++i)
            SetSubsystemUnknown(static_cast<RuntimeSubsystem>(i));
    }

    void ResetWorldUtilityTrackingState()
    {
        memset(s_worldSmokeSubclassIds, 0, sizeof(s_worldSmokeSubclassIds));
        memset(s_worldMolotovSubclassIds, 0, sizeof(s_worldMolotovSubclassIds));
        memset(s_worldDecoySubclassIds, 0, sizeof(s_worldDecoySubclassIds));
        memset(s_worldHeSubclassIds, 0, sizeof(s_worldHeSubclassIds));
        memset(s_worldInfernoSubclassIds, 0, sizeof(s_worldInfernoSubclassIds));
        memset(s_worldTrackedIndices, 0, sizeof(s_worldTrackedIndices));
        memset(s_worldTrackedIndexPos, 0, sizeof(s_worldTrackedIndexPos));
        s_worldTrackedIndexCount = 0;
        memset(s_worldEntityRefs, 0, sizeof(s_worldEntityRefs));
        memset(s_worldEntitySubclassIds, 0, sizeof(s_worldEntitySubclassIds));
        memset(s_worldEntityItemIds, 0, sizeof(s_worldEntityItemIds));
        memset(s_worldEntityClassKinds, 0, sizeof(s_worldEntityClassKinds));
        memset(s_worldSmokeLatched, 0, sizeof(s_worldSmokeLatched));
        memset(s_worldInfernoLatched, 0, sizeof(s_worldInfernoLatched));
        memset(s_worldDecoyLatched, 0, sizeof(s_worldDecoyLatched));
        memset(s_worldExplosiveLatched, 0, sizeof(s_worldExplosiveLatched));
        memset(s_worldUtilityHasHistory, 0, sizeof(s_worldUtilityHasHistory));
        memset(s_worldSmokeEvidenceCount, 0, sizeof(s_worldSmokeEvidenceCount));
        memset(s_worldInfernoEvidenceCount, 0, sizeof(s_worldInfernoEvidenceCount));
        memset(s_worldDecoyEvidenceCount, 0, sizeof(s_worldDecoyEvidenceCount));
        memset(s_worldExplosiveEvidenceCount, 0, sizeof(s_worldExplosiveEvidenceCount));
        memset(s_worldSmokeStartUs, 0, sizeof(s_worldSmokeStartUs));
        memset(s_worldInfernoStartUs, 0, sizeof(s_worldInfernoStartUs));
        memset(s_worldDecoyStartUs, 0, sizeof(s_worldDecoyStartUs));
        memset(s_worldExplosiveStartUs, 0, sizeof(s_worldExplosiveStartUs));
        memset(s_worldUtilityDeadlinesUs, 0, sizeof(s_worldUtilityDeadlinesUs));
        memset(s_worldPrevPos, 0, sizeof(s_worldPrevPos));
        memset(s_worldPrevSmokeTick, 0, sizeof(s_worldPrevSmokeTick));
        memset(s_worldPrevSmokeActive, 0, sizeof(s_worldPrevSmokeActive));
        memset(s_worldPrevSmokeVolumeDataReceived, 0, sizeof(s_worldPrevSmokeVolumeDataReceived));
        memset(s_worldPrevSmokeEffectSpawned, 0, sizeof(s_worldPrevSmokeEffectSpawned));
        memset(s_worldPrevInfernoTick, 0, sizeof(s_worldPrevInfernoTick));
        memset(s_worldPrevInfernoLife, 0, sizeof(s_worldPrevInfernoLife));
        memset(s_worldPrevInfernoFireCount, 0, sizeof(s_worldPrevInfernoFireCount));
        memset(s_worldPrevInfernoInPostEffect, 0, sizeof(s_worldPrevInfernoInPostEffect));
        memset(s_worldPrevDecoyTick, 0, sizeof(s_worldPrevDecoyTick));
        memset(s_worldPrevDecoyClientTick, 0, sizeof(s_worldPrevDecoyClientTick));
        memset(s_worldPrevExplodeTick, 0, sizeof(s_worldPrevExplodeTick));
        memset(s_worldPrevVelocity, 0, sizeof(s_worldPrevVelocity));
        memset(s_worldUtilityFieldTimes, 0, sizeof(s_worldUtilityFieldTimes));
        memset(s_worldUtilityPositionSampleUs, 0, sizeof(s_worldUtilityPositionSampleUs));
        memset(s_worldUtilityStationarySinceUs, 0, sizeof(s_worldUtilityStationarySinceUs));
        memset(s_worldUtilityStationarySamples, 0, sizeof(s_worldUtilityStationarySamples));
    }

    void ResetRuntimeStateHard(const char* reason, bool publishClearedSnapshot)
    {
        RecordRuntimeReset(esp::RuntimeResetKind::Hard, reason, publishClearedSnapshot);
        s_bombEpoch.fetch_add(1, std::memory_order_relaxed);
        s_lastBulkEvictionUs.store(0, std::memory_order_relaxed);
        BumpSceneReset();
        for (uint8_t i = 0; i < static_cast<uint8_t>(RuntimeSubsystem::Count); ++i)
            SetSubsystemUnknown(static_cast<RuntimeSubsystem>(i));
        {
            std::lock_guard<std::mutex> lock(s_dataMutex);
            for (int i = 0; i < 64; ++i) {
                s_players[i] = {};
                s_prevPlayers[i] = {};
                s_webRadarPlayers[i] = {};
                s_playerLastSeenMs[i] = 0;
                s_playerInvalidReadStreak[i] = 0;
                s_playerDeathConfirmCount[i] = 0;
            }
            s_bombState = {};
            s_spectatorCount = 0;
            memset(s_spectators, 0, sizeof(s_spectators));
            s_localPawnFarewellPtr = 0;
            s_localPawnFarewellHandle = 0;
            s_localPawnFarewellExpiryUs = 0;
            s_localPawnHandleLastSeen = 0;
            s_worldMarkerCount = 0;
            s_localPawn = 0;
            memset(s_localName, 0, sizeof(s_localName));
            s_localIsDead = false;
            s_localHealth = 0;
            s_localArmor = 0;
            s_localMoney = 0;
            s_localHasBomb = false;
            s_localHasDefuser = false;
            s_localGrenadeCount = 0;
            memset(s_localGrenadeIds, 0, sizeof(s_localGrenadeIds));
            s_localWeaponId = 0;
            s_localWeaponHandle = 0;
            s_localWeaponEntity = 0;
            s_localAmmoClip = -1;
            s_localAmmoValid = false;
            s_localAmmoUpdatedAtUs = 0;
            s_localWeaponUpdatedAtUs = 0;
            s_localWeaponTelemetry = {};
            s_localPlayerIndex = -1;
            s_localTeam = 0;
            s_localPos = {};
            s_localPosValid = false;
            s_localPosUpdatedAtUs = 0;
            s_localViewOffset = { 0.0f, 0.0f, 64.0f };
            s_localViewOffsetValid = false;
            s_localViewOffsetUpdatedAtUs = 0;
            s_localAimPunch = {};
            s_localShotsFired = 0;
            s_localShotsFiredValid = false;
            s_localShotsUpdatedAtUs = 0;
            s_localAimPunchValid = false;
            s_localAimPunchUpdatedAtUs = 0;
            s_prevLocalPos = {};
            s_viewAngles = {};
            s_fovSensitivityAdjust = 1.0f;
            memset(&s_viewMatrix, 0, sizeof(s_viewMatrix));
            s_viewMatrixUpdatedAtUs = 0;
            s_captureTimeUs = 0;
            s_prevCaptureTimeUs = 0;
            s_playerCoreGeneration.store(0, std::memory_order_relaxed);
            s_playerCoreCaptureTimeUs.store(0, std::memory_order_relaxed);
            s_prevPlayerCoreCaptureTimeUs.store(0, std::memory_order_relaxed);
            s_playerCoreBatchQuality.store(0, std::memory_order_relaxed);
            s_playerCoreIncompleteMask.store(0, std::memory_order_relaxed);
            s_playerCoreInvalidMask.store(0, std::memory_order_relaxed);
            s_playerCoreBatchHoldCount.store(0, std::memory_order_relaxed);
            s_minimapMins = {};
            s_minimapMaxs = {};
            s_hasMinimapBounds = false;
            s_mapFingerprint = 0;
            ResetActiveMapState();
            s_lastLiveMapNameSeenUs.store(0, std::memory_order_relaxed);
            s_localMaskResolved = false;
            s_lastWorldScanUs = 0;
            s_activePlayerCount.store(0, std::memory_order_relaxed);
            s_playerControllerSlotCountStat.store(0, std::memory_order_relaxed);
            s_playerResolvedSlotCountStat.store(0, std::memory_order_relaxed);
            s_playerPlausibleCoreSlotCountStat.store(0, std::memory_order_relaxed);
            s_playerDuplicateIdentityFilteredStat.store(0, std::memory_order_relaxed);
            s_playerBacklinkMismatchStat.store(0, std::memory_order_relaxed);
            s_playerHierarchyHeldSlotCount.store(0, std::memory_order_relaxed);
            s_playerZeroPawnHeldSlotCount.store(0, std::memory_order_relaxed);
            s_playerCoreHeldSlotCount.store(0, std::memory_order_relaxed);
            s_playerSlotScanLimitStat.store(0, std::memory_order_relaxed);
            s_playerHierarchyHighWaterSlot.store(0, std::memory_order_relaxed);
            s_highestEntityIdxStat.store(0, std::memory_order_relaxed);
            s_entitySlotStrideStat.store(0x70u, std::memory_order_relaxed);
            s_worldMarkerCountStat.store(0, std::memory_order_relaxed);
            s_worldTrackedEntityCountStat.store(0, std::memory_order_relaxed);
            s_worldCandidateCountStat.store(0, std::memory_order_relaxed);
            s_worldUtilityCandidateCountStat.store(0, std::memory_order_relaxed);
            s_worldClassifiedCandidateCountStat.store(0, std::memory_order_relaxed);
            s_worldIdentityPendingCountStat.store(0, std::memory_order_relaxed);
            s_worldMarkerCapacityDropsStat.store(0, std::memory_order_relaxed);
            s_worldMarkerReadGapHoldsStat.store(0, std::memory_order_relaxed);
            s_worldPositionReadMissesStat.store(0, std::memory_order_relaxed);
            s_visibilityFreshSlots.store(0, std::memory_order_relaxed);
            s_visibilityVisibleSlots.store(0, std::memory_order_relaxed);
            s_visibilityMaskSlots.store(0, std::memory_order_relaxed);
            s_visibilityCrosshairSlot.store(-1, std::memory_order_relaxed);
            s_visibilityLastCommitUs.store(0, std::memory_order_relaxed);
            s_visibilityCrosshairValid.store(false, std::memory_order_relaxed);
            s_visibilityEnabled.store(false, std::memory_order_relaxed);
            s_visibilityLocalMaskResolved.store(false, std::memory_order_relaxed);
            s_lastWorldScanCommittedUs.store(0, std::memory_order_relaxed);
            s_lastPublishUs.store(0, std::memory_order_relaxed);
            s_bombDebugFlags.store(0, std::memory_order_relaxed);
            s_bombDebugSourceFlags.store(0, std::memory_order_relaxed);
            s_bombDebugPositionSampleUs.store(0, std::memory_order_relaxed);
            s_bombDropPublicationDebug.store(0, std::memory_order_relaxed);
            s_bombDebugRawFlags.store(0, std::memory_order_relaxed);
            s_bombDebugConfidence.store(0, std::memory_order_relaxed);
            s_bombDebugDefuserSlot.store(-1, std::memory_order_relaxed);
            s_bombDebugBlowLeftMs.store(-1, std::memory_order_relaxed);
            s_bombDebugDefuseLeftMs.store(-1, std::memory_order_relaxed);
            s_stageEngineUs.store(0, std::memory_order_relaxed);
            s_stageBaseReadsUs.store(0, std::memory_order_relaxed);
            s_stagePlayerReadsUs.store(0, std::memory_order_relaxed);
            s_stagePlayerHierarchyUs.store(0, std::memory_order_relaxed);
            s_stagePlayerCoreUs.store(0, std::memory_order_relaxed);
            s_stagePlayerRepairUs.store(0, std::memory_order_relaxed);
            s_stageCommitStateUs.store(0, std::memory_order_relaxed);
            s_stagePlayerAuxUs.store(0, std::memory_order_relaxed);
            s_stageInventoryUs.store(0, std::memory_order_relaxed);
            s_stageBoneReadsUs.store(0, std::memory_order_relaxed);
            s_stageBombScanUs.store(0, std::memory_order_relaxed);
            s_stageWorldScanUs.store(0, std::memory_order_relaxed);
            s_stageWorldScanLastUs.store(0, std::memory_order_relaxed);
            s_stageCommitEnrichUs.store(0, std::memory_order_relaxed);
            s_stagePlayerAuxLastUs.store(0, std::memory_order_relaxed);
            s_stageInventoryLastUs.store(0, std::memory_order_relaxed);
            s_stageBoneReadsLastUs.store(0, std::memory_order_relaxed);
            s_stagePlayerAuxLastAtUs.store(0, std::memory_order_relaxed);
            s_stageInventoryLastAtUs.store(0, std::memory_order_relaxed);
            s_stageBoneReadsLastAtUs.store(0, std::memory_order_relaxed);
            s_stageBonePoseSlots.store(0, std::memory_order_relaxed);
            s_stageBonePointerValidationSlots.store(0, std::memory_order_relaxed);
            s_stageBonePoseRanges.store(0, std::memory_order_relaxed);
            s_stageBonePoseBytes.store(0, std::memory_order_relaxed);
            const uint64_t stageSequence =
                s_stageTimingSequence.load(std::memory_order_relaxed);
            s_stageTimingSequence.store(
                (stageSequence + 2u) & ~uint64_t{1},
                std::memory_order_release);
            s_cameraWorkerCycleUs.store(0, std::memory_order_relaxed);
            ResetWorldUtilityTrackingState();
            s_lastStableIntervalPerTick = 0.015625f;
            s_lastStableGameTime = 0.0f;
            s_lastStableGameTimeUs = 0;

            if (publishClearedSnapshot)
                PublishCurrentSnapshot();
        }
        ResetPlayerVisibilityFrame();
        ResetCameraSnapshot();
    }

    int ResolveLocalPlayerIndex(LocalPlayerIndexHints hints)
    {
        if (hints.pawnMaskBit >= 0 && hints.pawnMaskBit < 64)
            return hints.pawnMaskBit;
        if (hints.controllerMaskBit > 0 && hints.controllerMaskBit <= 64)
            return hints.controllerMaskBit - 1;
        const int engineLocalPlayerSlot = s_engineLocalPlayerSlot.load(std::memory_order_relaxed);
        return (engineLocalPlayerSlot >= 0 && engineLocalPlayerSlot < 64)
            ? engineLocalPlayerSlot
            : -1;
    }

    LocalPlayerIndexSource ResolveLocalPlayerIndexSource(LocalPlayerIndexHints hints)
    {
        if (hints.pawnMaskBit >= 0 && hints.pawnMaskBit < 64)
            return LocalPlayerIndexSource::PawnMatch;
        if (hints.controllerMaskBit > 0 && hints.controllerMaskBit <= 64)
            return LocalPlayerIndexSource::ControllerMatch;
        const int engineLocalPlayerSlot = s_engineLocalPlayerSlot.load(std::memory_order_relaxed);
        return (engineLocalPlayerSlot >= 0 && engineLocalPlayerSlot < 64)
            ? LocalPlayerIndexSource::EngineFallback
            : LocalPlayerIndexSource::None;
    }

    bool IsLiveLocalPlayerIndexSource(LocalPlayerIndexSource source)
    {
        return source == LocalPlayerIndexSource::PawnMatch ||
               source == LocalPlayerIndexSource::ControllerMatch;
    }

    bool IsValidLocalPlayerIndex(int localPlayerIndex)
    {
        return localPlayerIndex >= 0 && localPlayerIndex < 64;
    }

    void ResolveSharedLocalIdentityFromSlot(
        int localPlayerIndex,
        const SharedLocalIdentitySlotData& slots,
        SharedLocalIdentity& identity)
    {
        if (!IsValidLocalPlayerIndex(localPlayerIndex) ||
            !slots.names ||
            !slots.healths ||
            !slots.lifeStates ||
            !slots.armors ||
            !slots.moneys ||
            !slots.hasDefuserFlags) {
            return;
        }

        memcpy(identity.name, slots.names[localPlayerIndex], sizeof(identity.name));
        identity.name[127] = '\0';
        identity.isDead =
            (slots.healths[localPlayerIndex] <= 0) ||
            (slots.lifeStates[localPlayerIndex] != 0);
        identity.health = identity.isDead
            ? 0
            : std::clamp(slots.healths[localPlayerIndex], 0, 100);
        identity.armor = std::clamp(slots.armors[localPlayerIndex], 0, 100);
        identity.money = std::max(0, slots.moneys[localPlayerIndex]);
        identity.hasDefuser = slots.hasDefuserFlags[localPlayerIndex] == 1u;
    }

    void ApplySharedLocalIdentityState(const SharedLocalIdentity& identity)
    {
        std::copy(std::begin(identity.name), std::end(identity.name), std::begin(s_localName));
        s_localIsDead = identity.isDead;
        s_localHealth = identity.health;
        s_localArmor = identity.armor;
        s_localMoney = identity.money;
        s_localHasDefuser = identity.hasDefuser;
    }

    void RequestDmaRecovery(const char* reason)
    {
        const uint64_t nowUs = SubsystemNowUs();
        bool expected = false;
        if (!s_dmaRecoveryRequested.compare_exchange_strong(
                expected,
                true,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return;
        }

        RecordDmaEvent({
            .action = "recovery_request",
            .reason = reason
        });
        SetSceneWarmupState(esp::SceneWarmupState::Recovery, nowUs);
        s_dmaRecoveryRequestedAtUs.store(nowUs, std::memory_order_relaxed);

        static std::atomic<uint64_t> s_lastRecoveryRequestLogUs{0};
        uint64_t previousLogUs =
            s_lastRecoveryRequestLogUs.load(std::memory_order_relaxed);
        if (esp::worker::IsWorkerCooldownElapsed(
                previousLogUs,
                nowUs,
                750000u) &&
            s_lastRecoveryRequestLogUs.compare_exchange_strong(
                previousLogUs,
                nowUs,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
            DmaLogPrintf("[INFO] DMA recovery requested (%s)", reason ? reason : "unspecified");
        }
    }

    void AcquireCameraWorkerPauseRequest()
    {
        uint32_t current =
            s_cameraWorkerPauseRequests.load(std::memory_order_acquire);
        while (current != UINT32_MAX) {
            if (s_cameraWorkerPauseRequests.compare_exchange_weak(
                    current,
                    current + 1u,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return;
            }
        }
        s_cameraWorkerPauseReleaseImbalanceCount.fetch_add(
            1,
            std::memory_order_relaxed);
    }

    void ReleaseCameraWorkerPauseRequest()
    {
        uint32_t current =
            s_cameraWorkerPauseRequests.load(std::memory_order_acquire);
        while (current != 0u) {
            if (s_cameraWorkerPauseRequests.compare_exchange_weak(
                    current,
                    current - 1u,
                    std::memory_order_acq_rel,
                    std::memory_order_acquire)) {
                return;
            }
        }
        // A concurrent lifecycle reset must never turn a stale release into
        // UINT32_MAX and permanently park the camera worker.
        s_cameraWorkerPauseReleaseImbalanceCount.fetch_add(
            1,
            std::memory_order_relaxed);
    }

    bool RecoverOrphanedCameraWorkerPauseRequest()
    {
        uint32_t current =
            s_cameraWorkerPauseRequests.load(std::memory_order_acquire);
        if (!esp::worker::IsCameraPauseOrphaned(
                current,
                s_dmaAdminPauseActive.load(std::memory_order_acquire),
                s_dmaRecovering.load(std::memory_order_acquire))) {
            return false;
        }

        if (!s_cameraWorkerPauseRequests.compare_exchange_strong(
                current,
                0u,
                std::memory_order_acq_rel,
                std::memory_order_acquire)) {
            return false;
        }

        s_cameraWorkerPauseOrphanRecoveryCount.fetch_add(
            1,
            std::memory_order_relaxed);
        RecordDmaEvent({
            .action = "pause_recovered",
            .reason = "orphaned_camera_pause",
        });
        return true;
    }

    void DmaAdminThreadFn(const std::stop_token& stopToken) noexcept
    {
        while (!stopToken.stop_requested()) {
            uint32_t flags = 0;
            try {
                flags = s_pendingRefreshFlags.exchange(0, std::memory_order_acq_rel);
                if (flags && !s_dmaRecovering.load(std::memory_order_relaxed)) {
                    struct ScopedCameraPause {
                        bool refreshInProgress = false;

                        ScopedCameraPause()
                        {
                            s_dmaAdminPauseActive.store(
                                true,
                                std::memory_order_release);
                            AcquireCameraWorkerPauseRequest();
                        }

                        ~ScopedCameraPause()
                        {
                            if (refreshInProgress) {
                                s_dmaManualRefreshInProgress.store(
                                    false,
                                    std::memory_order_release);
                            }
                            ReleaseCameraWorkerPauseRequest();
                            s_dmaAdminPauseActive.store(
                                false,
                                std::memory_order_release);
                        }
                    } cameraPause;

                    const auto pauseDeadline =
                        std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(750);
                    while (s_cameraWorkerRunning.load(std::memory_order_acquire) &&
                           !s_cameraWorkerPaused.load(std::memory_order_acquire) &&
                           !stopToken.stop_requested() &&
                           std::chrono::steady_clock::now() < pauseDeadline) {
                        std::this_thread::sleep_for(std::chrono::microseconds(250));
                    }
                    if (stopToken.stop_requested())
                        break;
                    if (s_cameraWorkerRunning.load(std::memory_order_acquire) &&
                        !s_cameraWorkerPaused.load(std::memory_order_acquire)) {
                        s_pendingRefreshFlags.fetch_or(flags, std::memory_order_acq_rel);
                        continue;
                    }
                    if (s_dmaRecovering.load(std::memory_order_acquire)) {
                        s_pendingRefreshFlags.fetch_or(flags, std::memory_order_acq_rel);
                        continue;
                    }

                    std::unique_lock<std::shared_timed_mutex> lifecycleLock(
                        s_dmaLifecycleMutex,
                        std::defer_lock);
                    if (!lifecycleLock.try_lock_for(std::chrono::milliseconds(750)) ||
                        !mem.vHandle) {
                        s_pendingRefreshFlags.fetch_or(flags, std::memory_order_acq_rel);
                        continue;
                    }
                    if (stopToken.stop_requested())
                        break;
                    if (s_dmaRecovering.load(std::memory_order_acquire)) {
                        s_pendingRefreshFlags.fetch_or(flags, std::memory_order_acq_rel);
                        continue;
                    }

                    const bool wantFull = (flags & 0x4u) != 0u;
                    const bool wantRepair = (flags & 0x2u) != 0u;
                    const bool wantProbe = (flags & 0x1u) != 0u;
                    const DmaRefreshTier selectedTier =
                        wantFull
                            ? DmaRefreshTier::Full
                            : wantRepair
                                ? DmaRefreshTier::Repair
                                : DmaRefreshTier::Probe;
                    const auto refreshStartedAt = std::chrono::steady_clock::now();
                    s_dmaManualRefreshInProgress.store(true, std::memory_order_release);
                    cameraPause.refreshInProgress = true;

                    const uint32_t operations =
                        esp::recovery::DmaRefreshOperations(selectedTier);
                    bool refreshSucceeded = true;
                    auto applyRefresh = [&](ULONG64 option) {
                        if (!VMMDLL_ConfigSet(mem.vHandle, option, 1))
                            refreshSucceeded = false;
                    };
                    if (esp::recovery::HasDmaRefreshOperation(
                            operations,
                            esp::recovery::DmaRefreshOperationMemoryFull)) {
                        applyRefresh(VMMDLL_OPT_REFRESH_FREQ_MEM);
                    }
                    if (esp::recovery::HasDmaRefreshOperation(
                            operations,
                            esp::recovery::DmaRefreshOperationMemoryPartial)) {
                        applyRefresh(VMMDLL_OPT_REFRESH_FREQ_MEM_PARTIAL);
                    }
                    if (esp::recovery::HasDmaRefreshOperation(
                            operations,
                            esp::recovery::DmaRefreshOperationTlbFull)) {
                        applyRefresh(VMMDLL_OPT_REFRESH_FREQ_TLB);
                    }
                    if (esp::recovery::HasDmaRefreshOperation(
                            operations,
                            esp::recovery::DmaRefreshOperationTlbPartial)) {
                        applyRefresh(VMMDLL_OPT_REFRESH_FREQ_TLB_PARTIAL);
                    }
                    if (esp::recovery::HasDmaRefreshOperation(
                            operations,
                            esp::recovery::DmaRefreshOperationProcessSpecific)) {
                        uint32_t targetPid = GetAttachedCs2ProcessId();
                        if (targetPid == 0) {
                            const DWORD attachedPid = mem.GetAttachedPid();
                            if (attachedPid != 0)
                                targetPid = attachedPid;
                        }

                        if (targetPid != 0) {
                            applyRefresh(
                                VMMDLL_OPT_REFRESH_SPECIFIC_PROCESS |
                                static_cast<ULONG64>(targetPid));
                        } else {
                            applyRefresh(
                                selectedTier == DmaRefreshTier::Full
                                    ? VMMDLL_OPT_REFRESH_FREQ_MEDIUM
                                    : VMMDLL_OPT_REFRESH_FREQ_FAST);
                        }
                    }
                    if (!refreshSucceeded) {
                        RecordDmaEvent({
                            .action = "refresh_fail",
                            .reason = esp::recovery::DmaRefreshTierLabel(selectedTier),
                        });
                        RequestDmaRecovery("dma_refresh_failed");
                    }

                    const uint32_t coveredFlags =
                        (wantFull || wantRepair || wantProbe)
                            ? esp::recovery::DmaRefreshCoveredFlags(selectedTier)
                            : 0u;
                    if (coveredFlags != 0u) {
                        const uint32_t coveredPending =
                            s_pendingRefreshFlags.fetch_and(
                                ~coveredFlags,
                                std::memory_order_acq_rel) &
                            coveredFlags;
                        if (coveredPending != 0u) {
                            s_dmaManualRefreshCoalescedCount.fetch_add(
                                1,
                                std::memory_order_relaxed);
                        }
                    }

                    const uint64_t refreshUs = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now() - refreshStartedAt)
                            .count());
                    s_dmaManualRefreshLastUs.store(refreshUs, std::memory_order_relaxed);
                    s_dmaManualRefreshCount.fetch_add(1, std::memory_order_relaxed);
                    s_dmaManualRefreshRecentCount.fetch_add(1, std::memory_order_relaxed);
                    uint64_t previousPeak =
                        s_dmaManualRefreshPeakUs.load(std::memory_order_relaxed);
                    while (refreshUs > previousPeak &&
                           !s_dmaManualRefreshPeakUs.compare_exchange_weak(
                               previousPeak,
                               refreshUs,
                               std::memory_order_relaxed,
                                std::memory_order_relaxed)) {
                    }
                    previousPeak =
                        s_dmaManualRefreshRecentPeakUs.load(std::memory_order_relaxed);
                    while (refreshUs > previousPeak &&
                           !s_dmaManualRefreshRecentPeakUs.compare_exchange_weak(
                               previousPeak,
                               refreshUs,
                               std::memory_order_relaxed,
                               std::memory_order_relaxed)) {
                    }
                }
            } catch (...) {
                if (flags)
                    s_pendingRefreshFlags.fetch_or(flags, std::memory_order_acq_rel);
                static uint64_t s_lastAdminExceptionLogUs = 0;
                const uint64_t nowUs = TickNowUs();
                if (nowUs - s_lastAdminExceptionLogUs >= 1000000u) {
                    s_lastAdminExceptionLogUs = nowUs;
                    DmaLogPrintf("[ERROR] DMA admin refresh exception caught, retrying");
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }

    void EnsureDmaAdminThread()
    {
        bool expected = false;
        if (s_dmaAdminThreadStarted.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            s_dmaAdminThread = std::jthread(DmaAdminThreadFn);
        }
    }

    void StopDmaAdminThread()
    {
        s_dmaAdminThread.request_stop();
        if (s_dmaAdminThread.joinable())
            s_dmaAdminThread.join();
        s_pendingRefreshFlags.store(0, std::memory_order_release);
        s_dmaAdminThreadStarted.store(false, std::memory_order_release);
    }

    void RefreshDmaCaches(
        const char* reason,
        DmaRefreshTier tier,
        bool force,
        DmaRefreshTrigger trigger)
    {
        static std::mutex s_refreshPolicyMutex;
        static uint64_t s_lastProbeRefreshUs = 0;
        static uint64_t s_lastRepairRefreshUs = 0;
        static uint64_t s_lastFullRefreshUs = 0;
        static uint64_t s_refreshPolicySessionGeneration = 0;

        if (s_dmaRecovering.load(std::memory_order_relaxed)) {
            s_dmaManualRefreshSuppressedCount.fetch_add(1, std::memory_order_relaxed);
            s_dmaManualRefreshSuppressedRecentCount.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        std::lock_guard<std::mutex> policyLock(s_refreshPolicyMutex);
        const uint64_t sessionGeneration =
            s_dmaSessionGeneration.load(std::memory_order_acquire);
        if (s_refreshPolicySessionGeneration != sessionGeneration) {
            s_refreshPolicySessionGeneration = sessionGeneration;
            s_lastProbeRefreshUs = 0;
            s_lastRepairRefreshUs = 0;
            s_lastFullRefreshUs = 0;
        }
        const uint64_t nowUs = TickNowUs();
        uint64_t* lastRefreshUs = &s_lastProbeRefreshUs;
        if (tier == DmaRefreshTier::Full)
            lastRefreshUs = &s_lastFullRefreshUs;
        else if (tier == DmaRefreshTier::Repair)
            lastRefreshUs = &s_lastRepairRefreshUs;
        const auto decision = esp::recovery::EvaluateDmaRefreshPolicy({
            .tier = tier,
            .force = force,
            .nowUs = nowUs,
            .lastRefreshUs = *lastRefreshUs,
            .trigger = trigger,
            .backgroundRefreshEnabled =
                s_dmaBackgroundRefreshEnabled.load(std::memory_order_acquire),
            .stableLiveScene =
                static_cast<esp::GameStatus>(
                    s_gameStatus.load(std::memory_order_relaxed)) ==
                    esp::GameStatus::Ok &&
                static_cast<esp::SceneWarmupState>(
                    s_sceneWarmupState.load(std::memory_order_relaxed)) ==
                    esp::SceneWarmupState::Stable,
            .activePlayerCount =
                s_activePlayerCount.load(std::memory_order_relaxed),
            .consecutiveFailures =
                s_dmaConsecutiveFailures.load(std::memory_order_relaxed),
            .consecutiveDegraded =
                s_dmaConsecutiveDegraded.load(std::memory_order_relaxed),
        });
        if (!decision.queueRefresh) {
            if (decision.containedLocally) {
                s_dmaManualRefreshAvoidedCount.fetch_add(1, std::memory_order_relaxed);
                s_dmaManualRefreshAvoidedRecentCount.fetch_add(1, std::memory_order_relaxed);
            } else {
                s_dmaManualRefreshSuppressedCount.fetch_add(1, std::memory_order_relaxed);
                s_dmaManualRefreshSuppressedRecentCount.fetch_add(1, std::memory_order_relaxed);
            }
            return;
        }
        *lastRefreshUs = decision.nextLastRefreshUs;
        if (tier == DmaRefreshTier::Full) {
            s_lastRepairRefreshUs = decision.nextLastRefreshUs;
            s_lastProbeRefreshUs = decision.nextLastRefreshUs;
        } else if (tier == DmaRefreshTier::Repair) {
            s_lastProbeRefreshUs = decision.nextLastRefreshUs;
        }

        EnsureDmaAdminThread();
        const uint32_t previousFlags =
            s_pendingRefreshFlags.fetch_or(decision.pendingFlag, std::memory_order_acq_rel);
        s_dmaManualRefreshQueuedCount.fetch_add(1, std::memory_order_relaxed);
        s_dmaManualRefreshQueuedRecentCount.fetch_add(1, std::memory_order_relaxed);
        if ((previousFlags & decision.pendingFlag) != 0u)
            s_dmaManualRefreshCoalescedCount.fetch_add(1, std::memory_order_relaxed);

        RecordDmaEvent({
            .action = decision.tierLabel,
            .reason = reason
        });
        DmaLogPrintf("[INFO] DMA cache refresh queued (%s) [%s]", reason ? reason : "transition", decision.tierLabel);
    }

    bool IsDmaRecoveryRequested()
    {
        return s_dmaRecoveryRequested.load(std::memory_order_acquire);
    }

    void ClearDmaRecoveryRequest()
    {
        s_dmaRecoveryRequested.store(false, std::memory_order_release);
        s_dmaRecoveryRequestedAtUs.store(0, std::memory_order_relaxed);
    }

    void RecordEspEvent(EspEventDescriptor descriptor)
    {
        switch (descriptor.type) {
        case EspEventType::SlotEvictedStale:
        case EspEventType::SlotEvictedFallback:
        case EspEventType::SlotEvictedMissing:
            s_playerUnexpectedEvictionCount.fetch_add(1, std::memory_order_relaxed);
            break;
        case EspEventType::SlotEvictedLocal:
        case EspEventType::SlotEvictedDead:
            s_playerExpectedEvictionCount.fetch_add(1, std::memory_order_relaxed);
            break;
        default:
            break;
        }

        std::lock_guard<std::mutex> lock(s_espEventMutex);
        const uint32_t writeIndex = s_espEventWriteIndex.fetch_add(1, std::memory_order_relaxed);
        EspEventRecord& event = s_espEventRing[writeIndex % 4096];
        event.timeUs = TickNowUs();
        event.type = static_cast<uint8_t>(descriptor.type);
        event.slot = descriptor.slot;
        event.param = descriptor.param;
        uint32_t count = s_espEventCount.load(std::memory_order_relaxed);
        while (count < 4096 &&
               !s_espEventCount.compare_exchange_weak(count, count + 1u, std::memory_order_relaxed))
            ;
    }

    uint64_t SubsystemNowUs()
    {
        return TickNowUs();
    }

    void RecordDmaEvent(DmaEventDescriptor descriptor)
    {
        std::lock_guard<std::mutex> lock(s_dmaEventMutex);
        DmaEventRecord& event = s_dmaEvents[s_dmaEventWriteIndex % esp::DmaHealthStats::kMaxEvents];
        event.timeUs = SubsystemNowUs();
        strncpy_s(
            event.action,
            sizeof(event.action),
            descriptor.action ? descriptor.action : "unknown",
            _TRUNCATE);
        strncpy_s(
            event.reason,
            sizeof(event.reason),
            descriptor.reason ? descriptor.reason : "unspecified",
            _TRUNCATE);
        s_dmaEventWriteIndex = (s_dmaEventWriteIndex + 1u) % esp::DmaHealthStats::kMaxEvents;
        if (s_dmaEventCount < esp::DmaHealthStats::kMaxEvents)
            ++s_dmaEventCount;
    }

    void SetSubsystemUnknown(RuntimeSubsystem subsystem)
    {
        const size_t index = static_cast<size_t>(subsystem);
        s_subsystemStates[index].store(static_cast<uint8_t>(esp::SubsystemHealthState::Unknown), std::memory_order_relaxed);
        s_subsystemFailureStreaks[index].store(0, std::memory_order_relaxed);
        s_subsystemLastGoodUs[index].store(0, std::memory_order_relaxed);
    }

    void MarkSubsystemHealthy(RuntimeSubsystem subsystem, uint64_t nowUs)
    {
        const uint64_t timestampUs = nowUs > 0 ? nowUs : SubsystemNowUs();
        const size_t index = static_cast<size_t>(subsystem);
        s_subsystemStates[index].store(static_cast<uint8_t>(esp::SubsystemHealthState::Healthy), std::memory_order_relaxed);
        s_subsystemFailureStreaks[index].store(0, std::memory_order_relaxed);
        s_subsystemLastGoodUs[index].store(timestampUs, std::memory_order_relaxed);
    }

    void MarkSubsystemDegraded(RuntimeSubsystem subsystem, uint64_t)
    {
        const size_t index = static_cast<size_t>(subsystem);
        s_subsystemFailureStreaks[index].fetch_add(1u, std::memory_order_relaxed);
        s_subsystemStates[index].store(
            static_cast<uint8_t>(esp::SubsystemHealthState::Degraded),
            std::memory_order_relaxed);
    }

    void MarkSubsystemFailed(RuntimeSubsystem subsystem, uint64_t)
    {
        const size_t index = static_cast<size_t>(subsystem);
        s_subsystemFailureStreaks[index].fetch_add(1u, std::memory_order_relaxed);
        s_subsystemStates[index].store(static_cast<uint8_t>(esp::SubsystemHealthState::Failed), std::memory_order_relaxed);
    }

    esp::SubsystemHealthInfo GetSubsystemHealthInfo(RuntimeSubsystem subsystem, uint64_t nowUs)
    {
        esp::SubsystemHealthInfo info = {};
        const size_t index = static_cast<size_t>(subsystem);
        info.state = static_cast<esp::SubsystemHealthState>(s_subsystemStates[index].load(std::memory_order_relaxed));
        info.failureStreak = s_subsystemFailureStreaks[index].load(std::memory_order_relaxed);
        if (info.state == esp::SubsystemHealthState::Unknown)
            return info;
        const uint64_t lastGoodUs = s_subsystemLastGoodUs[index].load(std::memory_order_relaxed);
        if (lastGoodUs > 0 && nowUs >= lastGoodUs)
            info.lastGoodAgeUs = nowUs - lastGoodUs;
        return info;
    }

    void SetSceneWarmupState(esp::SceneWarmupState state, uint64_t nowUs)
    {
        const uint64_t timestampUs = nowUs > 0 ? nowUs : TickNowUs();
        s_sceneWarmupState.store(static_cast<uint8_t>(state), std::memory_order_relaxed);
        s_sceneWarmupEnteredUs.store(timestampUs, std::memory_order_relaxed);
    }

    void BumpSceneReset(uint64_t nowUs)
    {
        const uint64_t timestampUs = nowUs > 0 ? nowUs : TickNowUs();
        s_sceneResetSerial.fetch_add(1, std::memory_order_relaxed);
        s_lastSceneResetUs.store(timestampUs, std::memory_order_relaxed);
    }

    ActiveMapStateSnapshot CopyActiveMapState()
    {
        std::lock_guard<std::mutex> lock(s_activeMapMutex);
        ActiveMapStateSnapshot snapshot;
        snapshot.key = s_activeMapKey;
        snapshot.baseOffsetX = s_activeMapBaseOffsetX;
        snapshot.baseOffsetY = s_activeMapBaseOffsetY;
        snapshot.overviewAvailable = s_activeMapOverviewAvailable;
        snapshot.overviewPosX = s_activeMapOverviewPosX;
        snapshot.overviewPosY = s_activeMapOverviewPosY;
        snapshot.overviewScale = s_activeMapOverviewScale;
        return snapshot;
    }

    void ResetActiveMapState()
    {
        std::lock_guard<std::mutex> lock(s_activeMapMutex);
        s_activeMapKey.clear();
        s_activeMapBaseOffsetX = 0.0f;
        s_activeMapBaseOffsetY = 0.0f;
        s_activeMapOverviewAvailable = false;
        s_activeMapOverviewPosX = 0.0f;
        s_activeMapOverviewPosY = 0.0f;
        s_activeMapOverviewScale = 0.0f;
    }

    uint64_t TickNowUs()
    {
        static const auto s_epoch = std::chrono::steady_clock::now();
        const auto now = std::chrono::steady_clock::now();
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now - s_epoch).count());
    }

    uint64_t TickNowMs()
    {
        return TickNowUs() / 1000u;
    }

    const char* EspEventTypeName(EspEventType type)
    {
        switch (type) {
        case EspEventType::InvalidPositionFallback: return "invalid_position_fallback";
        case EspEventType::DeathHold: return "death_hold";
        case EspEventType::SnapshotFallback: return "snapshot_fallback";
        case EspEventType::WebRadarPlayerHold: return "web_radar_player_hold";
        case EspEventType::SlotEvictedStale: return "slot_evicted_stale";
        case EspEventType::SlotEvictedFallback: return "slot_evicted_fallback";
        case EspEventType::SlotEvictedHierarchy: return "slot_evicted_hierarchy";
        case EspEventType::SlotEvictedMissing: return "slot_evicted_missing";
        case EspEventType::SlotEvictedLocal: return "slot_evicted_local";
        case EspEventType::SlotEvictedDead: return "slot_evicted_dead";
        case EspEventType::BulkRecoveryEntered: return "bulk_recovery_entered";
        case EspEventType::BoneRejected: return "bone_rejected";
        default: return "unknown";
        }
    }

    void MarkDmaReadFailure()
    {
        s_dmaTotalFailures.fetch_add(1, std::memory_order_relaxed);
        s_dmaConsecutiveFailures.fetch_add(1, std::memory_order_relaxed);
        s_dmaConsecutiveDegraded.store(0, std::memory_order_relaxed);
    }

    void MarkDmaReadDegraded()
    {
        s_dmaTotalDegraded.fetch_add(1, std::memory_order_relaxed);
        s_dmaConsecutiveDegraded.fetch_add(1, std::memory_order_relaxed);
    }

    void MarkDmaReadSuccess()
    {
        s_dmaTotalSuccesses.fetch_add(1, std::memory_order_relaxed);
        s_dmaConsecutiveFailures.store(0, std::memory_order_relaxed);
        s_dmaConsecutiveDegraded.store(0, std::memory_order_relaxed);
        s_dmaLastSuccessTick.store(TickNowUs() / 1000u, std::memory_order_relaxed);
    }

    esp::DmaHealthStats GetDmaHealthStats()
    {
        const uint64_t nowMs = TickNowUs() / 1000u;
        esp::DmaHealthStats stats = {};
        stats.workerRunning = s_dataWorkerRunning.load(std::memory_order_relaxed);
        stats.cameraWorkerRunning = s_cameraWorkerRunning.load(std::memory_order_relaxed);
        stats.dataWorkerInFlight = s_dataWorkerUpdateInFlight.load(std::memory_order_acquire);
        stats.recovering = s_dmaRecovering.load(std::memory_order_relaxed);
        stats.recoveryRequested = s_dmaRecoveryRequested.load(std::memory_order_relaxed);
        stats.consecutiveFailures = s_dmaConsecutiveFailures.load(std::memory_order_relaxed);
        stats.consecutiveDegraded = s_dmaConsecutiveDegraded.load(std::memory_order_relaxed);
        stats.totalFailures = s_dmaTotalFailures.load(std::memory_order_relaxed);
        stats.totalDegraded = s_dmaTotalDegraded.load(std::memory_order_relaxed);
        stats.totalSuccesses = s_dmaTotalSuccesses.load(std::memory_order_relaxed);
        stats.totalRecoveries = s_dmaTotalRecoveries.load(std::memory_order_relaxed);
        const uint64_t lastLoopStartUs = s_dataWorkerLastLoopStartUs.load(std::memory_order_relaxed);
        if (lastLoopStartUs > 0) {
            const uint64_t lastLoopStartMs = lastLoopStartUs / 1000u;
            if (nowMs >= lastLoopStartMs)
                stats.dataWorkerLoopAgeMs = nowMs - lastLoopStartMs;
        }
        const uint64_t inFlightSinceUs = s_dataWorkerInFlightSinceUs.load(std::memory_order_relaxed);
        if (stats.dataWorkerInFlight && inFlightSinceUs > 0) {
            const uint64_t inFlightSinceMs = inFlightSinceUs / 1000u;
            if (nowMs >= inFlightSinceMs)
                stats.dataWorkerInFlightAgeMs = nowMs - inFlightSinceMs;
        }
        stats.dataWorkerStalled =
            stats.workerRunning &&
            stats.dataWorkerInFlight &&
            stats.dataWorkerInFlightAgeMs >= (kDataWorkerStallUs / 1000u);
        const uint64_t lastSuccessMs = s_dmaLastSuccessTick.load(std::memory_order_relaxed);
        if (lastSuccessMs > 0 && nowMs >= lastSuccessMs)
            stats.lastSuccessAgeMs = nowMs - lastSuccessMs;
        const uint64_t recoveryRequestedUs = s_dmaRecoveryRequestedAtUs.load(std::memory_order_relaxed);
        if (stats.recoveryRequested && recoveryRequestedUs > 0) {
            const uint64_t recoveryRequestedMs = recoveryRequestedUs / 1000u;
            if (nowMs >= recoveryRequestedMs)
                stats.recoveryRequestAgeMs = nowMs - recoveryRequestedMs;
        }
        {
            std::lock_guard<std::mutex> lock(s_dmaEventMutex);
            const uint32_t availableEventCount = std::min<uint32_t>(
                s_dmaEventCount,
                static_cast<uint32_t>(esp::DmaHealthStats::kMaxEvents));
            const bool liveEngineContext =
                s_engineInGame.load(std::memory_order_relaxed) &&
                !s_engineMenu.load(std::memory_order_relaxed);
            int collectedEvents = 0;
            for (uint32_t i = 0; i < availableEventCount && collectedEvents < esp::DmaHealthStats::kMaxEvents; ++i) {
                const uint32_t newestIndex =
                    (s_dmaEventWriteIndex + esp::DmaHealthStats::kMaxEvents - 1u - i) %
                    esp::DmaHealthStats::kMaxEvents;
                const DmaEventRecord& source = s_dmaEvents[newestIndex];
                uint64_t eventAgeMs = 0;
                if (source.timeUs > 0) {
                    const uint64_t eventMs = source.timeUs / 1000u;
                    eventAgeMs = nowMs >= eventMs ? nowMs - eventMs : 0;
                }
                const bool transientEvent =
                    std::strcmp(source.action, "probe") == 0 ||
                    std::strcmp(source.action, "repair") == 0 ||
                    std::strcmp(source.action, "full") == 0;
                const bool expectedLifecycleEvent =
                    (std::strcmp(source.reason, "scene_transition") == 0 ||
                     std::strcmp(source.reason, "engine_match_enter") == 0 ||
                     std::strcmp(source.reason, "engine_match_exit") == 0 ||
                     std::strcmp(source.reason, "entity_shape_match_enter") == 0) &&
                    (transientEvent ||
                     std::strcmp(source.action, "hard_reset") == 0 ||
                     std::strcmp(source.action, "soft_reset") == 0);
                if (expectedLifecycleEvent && eventAgeMs > 15000u)
                    continue;
                if (!liveEngineContext && transientEvent && eventAgeMs > 30000u)
                    continue;

                auto& target = stats.events[collectedEvents++];
                strncpy_s(target.action, sizeof(target.action), source.action, _TRUNCATE);
                strncpy_s(target.reason, sizeof(target.reason), source.reason, _TRUNCATE);
                target.ageMs = eventAgeMs;
            }
            stats.eventCount = collectedEvents;
        }
        stats.gameStatus = static_cast<esp::GameStatus>(s_gameStatus.load(std::memory_order_relaxed));
        return stats;
    }

    esp::DebugStats GetDebugStats()
    {
        const uint64_t nowUs = TickNowUs();
        esp::DebugStats stats = {};
        stats.publishCount = s_publishCount.load(std::memory_order_relaxed);
        stats.publishDropCount = s_publishDropCount.load(std::memory_order_relaxed);
        stats.visibilityPublishDropCount =
            s_visibilityPublishDropCount.load(std::memory_order_relaxed);
        stats.cameraPublishDropCount =
            s_cameraPublishDropCount.load(std::memory_order_relaxed);
        stats.settingsSnapshotReuseCount =
            s_settingsSnapshotReuseCount.load(std::memory_order_relaxed);
        stats.lastPublishUs = s_lastPublishUs.load(std::memory_order_relaxed);
        stats.cycleUs = s_dataWorkerCycleUs.load(std::memory_order_relaxed);
        stats.maxCycleUs = s_dataWorkerMaxCycleUs.load(std::memory_order_relaxed);
        stats.recentMaxCycleUs =
            s_dataWorkerRecentMaxCycleUs.load(std::memory_order_relaxed);
        const uint64_t dataWindowStartUs =
            s_dataWorkerRecentWindowStartUs.load(std::memory_order_relaxed);
        if (dataWindowStartUs > 0 && nowUs >= dataWindowStartUs)
            stats.cycleWindowAgeUs = nowUs - dataWindowStartUs;
        stats.cycleP50Us = s_dataWorkerCycleP50Us.load(std::memory_order_relaxed);
        stats.cycleP95Us = s_dataWorkerCycleP95Us.load(std::memory_order_relaxed);
        stats.cycleP99Us = s_dataWorkerCycleP99Us.load(std::memory_order_relaxed);
        stats.deadlineMissCount =
            s_dataWorkerDeadlineMissCount.load(std::memory_order_relaxed);
        stats.cycleSampleCount10s =
            s_dataWorkerCycleSampleCount.load(std::memory_order_relaxed);
        stats.cycleOverBudgetCount10s =
            s_dataWorkerCycleOverBudgetCount.load(std::memory_order_relaxed);
        stats.cycleOver5msCount10s =
            s_dataWorkerCycleOver5msCount.load(std::memory_order_relaxed);
        stats.cycleOver16msCount10s =
            s_dataWorkerCycleOver16msCount.load(std::memory_order_relaxed);
        stats.playerCoreAnomalyCount10s =
            s_playerCoreAnomalyCount.load(std::memory_order_relaxed);
        stats.playerCoreRecoveredCount10s =
            s_playerCoreRecoveredCount.load(std::memory_order_relaxed);
        stats.playerCoreGlobalRefreshAvoidedCount10s =
            s_playerCoreGlobalRefreshAvoidedCount.load(std::memory_order_relaxed);
        stats.playerUnexpectedEvictionCount10s =
            s_playerUnexpectedEvictionCount.load(std::memory_order_relaxed);
        stats.playerExpectedEvictionCount10s =
            s_playerExpectedEvictionCount.load(std::memory_order_relaxed);
        stats.playerControllerSlots =
            s_playerControllerSlotCountStat.load(std::memory_order_relaxed);
        stats.playerResolvedSlots =
            s_playerResolvedSlotCountStat.load(std::memory_order_relaxed);
        stats.playerPlausibleCoreSlots =
            s_playerPlausibleCoreSlotCountStat.load(std::memory_order_relaxed);
        stats.playerCoreGeneration =
            s_playerCoreGeneration.load(std::memory_order_relaxed);
        const uint64_t playerCoreCaptureUs =
            s_playerCoreCaptureTimeUs.load(std::memory_order_relaxed);
        if (playerCoreCaptureUs > 0 && nowUs >= playerCoreCaptureUs)
            stats.playerCoreGenerationAgeUs = nowUs - playerCoreCaptureUs;
        stats.playerCoreBatchHoldCount10s =
            s_playerCoreBatchHoldCount.load(std::memory_order_relaxed);
        stats.playerCoreBatchQuality =
            s_playerCoreBatchQuality.load(std::memory_order_relaxed);
        stats.playerCoreIncompleteMask = s_playerCoreIncompleteMask.load(std::memory_order_relaxed);
        stats.playerCoreInvalidMask = s_playerCoreInvalidMask.load(std::memory_order_relaxed);
        stats.playerDuplicateIdentityFiltered =
            s_playerDuplicateIdentityFilteredStat.load(std::memory_order_relaxed);
        stats.playerBacklinkMismatchCount =
            s_playerBacklinkMismatchStat.load(std::memory_order_relaxed);
        stats.playerHierarchyHeldSlots =
            s_playerHierarchyHeldSlotCount.load(std::memory_order_relaxed);
        stats.playerZeroPawnHeldSlots =
            s_playerZeroPawnHeldSlotCount.load(std::memory_order_relaxed);
        stats.playerCoreHeldSlots =
            s_playerCoreHeldSlotCount.load(std::memory_order_relaxed);
        stats.activePlayers = s_activePlayerCount.load(std::memory_order_relaxed);
        stats.playerSlotBudget = s_playerSlotScanLimitStat.load(std::memory_order_relaxed);
        stats.engineMaxClients = s_engineMaxClients.load(std::memory_order_relaxed);
        stats.highestEntityIdx = s_highestEntityIdxStat.load(std::memory_order_relaxed);
        stats.entitySlotStride =
            s_entitySlotStrideStat.load(std::memory_order_relaxed);
        stats.worldMarkerCount = s_worldMarkerCountStat.load(std::memory_order_relaxed);
        stats.worldTrackedEntityCount = s_worldTrackedEntityCountStat.load(std::memory_order_relaxed);
        stats.worldCandidateCount = s_worldCandidateCountStat.load(std::memory_order_relaxed);
        stats.worldUtilityCandidateCount = s_worldUtilityCandidateCountStat.load(std::memory_order_relaxed);
        stats.worldClassifiedCandidateCount = s_worldClassifiedCandidateCountStat.load(std::memory_order_relaxed);
        stats.worldIdentityPendingCount = s_worldIdentityPendingCountStat.load(std::memory_order_relaxed);
        stats.worldMarkerCapacityDrops = s_worldMarkerCapacityDropsStat.load(std::memory_order_relaxed);
        stats.worldMarkerReadGapHolds = s_worldMarkerReadGapHoldsStat.load(std::memory_order_relaxed);
        stats.worldPositionReadMisses = s_worldPositionReadMissesStat.load(std::memory_order_relaxed);
        stats.visibilityFreshSlots = s_visibilityFreshSlots.load(std::memory_order_relaxed);
        stats.visibilityVisibleSlots = s_visibilityVisibleSlots.load(std::memory_order_relaxed);
        stats.visibilityMaskSlots = s_visibilityMaskSlots.load(std::memory_order_relaxed);
        stats.visibilityCrosshairSlot = s_visibilityCrosshairSlot.load(std::memory_order_relaxed);
        const uint64_t visibilityLastCommitUs = s_visibilityLastCommitUs.load(std::memory_order_relaxed);
        if (visibilityLastCommitUs > 0 && nowUs >= visibilityLastCommitUs)
            stats.visibilityCommitAgeUs = nowUs - visibilityLastCommitUs;
        stats.visibilityEnabled = s_visibilityEnabled.load(std::memory_order_relaxed);
        stats.visibilityLocalMaskResolved = s_visibilityLocalMaskResolved.load(std::memory_order_relaxed);
        stats.uptimeUs = nowUs;
        const uint64_t sessionStartUs = s_sessionStartUs.load(std::memory_order_relaxed);
        if (sessionStartUs > 0 && nowUs >= sessionStartUs)
            stats.sessionUptimeUs = nowUs - sessionStartUs;
        if (stats.highestEntityIdx <= 800) stats.worldScanTargetIntervalUs = 50000;
        else if (stats.highestEntityIdx <= 1200) stats.worldScanTargetIntervalUs = 70000;
        else if (stats.highestEntityIdx <= 2000) stats.worldScanTargetIntervalUs = 90000;
        else stats.worldScanTargetIntervalUs = 120000;
        const uint64_t lastWorldScanUs = s_lastWorldScanCommittedUs.load(std::memory_order_relaxed);
        if (lastWorldScanUs > 0 && nowUs >= lastWorldScanUs)
            stats.worldScanAgeUs = nowUs - lastWorldScanUs;
        uint64_t playerAuxLastAtUs = 0;
        uint64_t inventoryLastAtUs = 0;
        uint64_t boneReadsLastAtUs = 0;
        for (int attempt = 0; attempt < 8; ++attempt) {
            const uint64_t sequenceBefore =
                s_stageTimingSequence.load(std::memory_order_acquire);
            if ((sequenceBefore & 1u) != 0u)
                continue;

            stats.stages.engineUs = s_stageEngineUs.load(std::memory_order_relaxed);
            stats.stages.baseReadsUs = s_stageBaseReadsUs.load(std::memory_order_relaxed);
            stats.stages.playerReadsUs = s_stagePlayerReadsUs.load(std::memory_order_relaxed);
            stats.stages.playerHierarchyUs = s_stagePlayerHierarchyUs.load(std::memory_order_relaxed);
            stats.stages.playerCoreUs = s_stagePlayerCoreUs.load(std::memory_order_relaxed);
            stats.stages.playerRepairUs = s_stagePlayerRepairUs.load(std::memory_order_relaxed);
            stats.stages.commitStateUs = s_stageCommitStateUs.load(std::memory_order_relaxed);
            stats.stages.playerAuxUs = s_stagePlayerAuxUs.load(std::memory_order_relaxed);
            stats.stages.inventoryUs = s_stageInventoryUs.load(std::memory_order_relaxed);
            stats.stages.boneReadsUs = s_stageBoneReadsUs.load(std::memory_order_relaxed);
            stats.stages.bombScanUs = s_stageBombScanUs.load(std::memory_order_relaxed);
            stats.stages.worldScanUs = s_stageWorldScanUs.load(std::memory_order_relaxed);
            stats.stages.worldScanLastUs = s_stageWorldScanLastUs.load(std::memory_order_relaxed);
            stats.stages.commitEnrichUs = s_stageCommitEnrichUs.load(std::memory_order_relaxed);
            stats.stages.playerAuxLastUs = s_stagePlayerAuxLastUs.load(std::memory_order_relaxed);
            stats.stages.inventoryLastUs = s_stageInventoryLastUs.load(std::memory_order_relaxed);
            stats.stages.boneReadsLastUs = s_stageBoneReadsLastUs.load(std::memory_order_relaxed);
            stats.stages.bonePoseSlots = s_stageBonePoseSlots.load(std::memory_order_relaxed);
            stats.stages.bonePointerValidationSlots =
                s_stageBonePointerValidationSlots.load(std::memory_order_relaxed);
            stats.stages.bonePoseRanges = s_stageBonePoseRanges.load(std::memory_order_relaxed);
            stats.stages.bonePoseBytes = s_stageBonePoseBytes.load(std::memory_order_relaxed);
            playerAuxLastAtUs = s_stagePlayerAuxLastAtUs.load(std::memory_order_relaxed);
            inventoryLastAtUs = s_stageInventoryLastAtUs.load(std::memory_order_relaxed);
            boneReadsLastAtUs = s_stageBoneReadsLastAtUs.load(std::memory_order_relaxed);

            const uint64_t sequenceAfter =
                s_stageTimingSequence.load(std::memory_order_acquire);
            if (sequenceBefore == sequenceAfter && (sequenceAfter & 1u) == 0u)
                break;
        }
        if (playerAuxLastAtUs > 0 && nowUs >= playerAuxLastAtUs)
            stats.stages.playerAuxAgeUs = nowUs - playerAuxLastAtUs;
        if (inventoryLastAtUs > 0 && nowUs >= inventoryLastAtUs)
            stats.stages.inventoryAgeUs = nowUs - inventoryLastAtUs;
        if (boneReadsLastAtUs > 0 && nowUs >= boneReadsLastAtUs)
            stats.stages.boneReadsAgeUs = nowUs - boneReadsLastAtUs;
        stats.stages.totalUs = stats.stages.engineUs + stats.stages.baseReadsUs +
            stats.stages.playerReadsUs + stats.stages.commitStateUs +
            stats.stages.playerAuxUs + stats.stages.inventoryUs +
            stats.stages.boneReadsUs + stats.stages.bombScanUs +
            stats.stages.worldScanUs + stats.stages.commitEnrichUs;
        uint64_t heldWorldScanUs = stats.stages.worldScanLastUs;
        if (stats.worldScanAgeUs > 0) {
            const uint64_t staleWorldThresholdUs =
                std::max<uint64_t>(stats.worldScanTargetIntervalUs * 2u, 120000u);
            if (stats.worldScanAgeUs > staleWorldThresholdUs)
                heldWorldScanUs = 0;
        }
        const uint64_t fixedCurrentUs =
            stats.stages.engineUs +
            stats.stages.baseReadsUs +
            stats.stages.playerReadsUs +
            stats.stages.commitStateUs +
            stats.stages.bombScanUs +
            stats.stages.commitEnrichUs;
        const uint64_t deferredLanePeakUs =
            esp::data::SelectDeferredLanePeakUs(
                (std::max)(stats.stages.playerAuxUs, stats.stages.playerAuxLastUs),
                (std::max)(stats.stages.inventoryUs, stats.stages.inventoryLastUs),
                (std::max)(stats.stages.boneReadsUs, stats.stages.boneReadsLastUs),
                (std::max)(stats.stages.worldScanUs, heldWorldScanUs));
        stats.stages.totalHeldUs =
            (std::max)(stats.stages.totalUs, fixedCurrentUs + deferredLanePeakUs);
        stats.camera.cycleUs = s_cameraWorkerCycleUs.load(std::memory_order_relaxed);
        stats.camera.maxCycleUs = s_cameraWorkerMaxCycleUs.load(std::memory_order_relaxed);
        stats.camera.recentMaxCycleUs =
            s_cameraWorkerRecentMaxCycleUs.load(std::memory_order_relaxed);
        const uint64_t cameraWindowStartUs =
            s_cameraWorkerRecentWindowStartUs.load(std::memory_order_relaxed);
        if (cameraWindowStartUs > 0 && nowUs >= cameraWindowStartUs)
            stats.camera.recentWindowAgeUs = nowUs - cameraWindowStartUs;
        stats.camera.p50Us = s_cameraWorkerCycleP50Us.load(std::memory_order_relaxed);
        stats.camera.p95Us = s_cameraWorkerCycleP95Us.load(std::memory_order_relaxed);
        stats.camera.p99Us = s_cameraWorkerCycleP99Us.load(std::memory_order_relaxed);
        stats.camera.deadlineMissCount =
            s_cameraWorkerDeadlineMissCount.load(std::memory_order_relaxed);
        stats.camera.sampleCount10s =
            s_cameraWorkerCycleSampleCount.load(std::memory_order_relaxed);
        stats.camera.overBudgetCount10s =
            s_cameraWorkerCycleOverBudgetCount.load(std::memory_order_relaxed);
        stats.camera.over5msCount10s =
            s_cameraWorkerCycleOver5msCount.load(std::memory_order_relaxed);
        stats.camera.over16msCount10s =
            s_cameraWorkerCycleOver16msCount.load(std::memory_order_relaxed);
        stats.dma.executeScatterTotalUs = Memory::DMA_EXECUTE_SCATTER_TOTAL_US.load(std::memory_order_relaxed);
        stats.dma.executeScatterPeakUs = Memory::DMA_EXECUTE_SCATTER_PEAK_US.load(std::memory_order_relaxed);
        stats.dma.executeScatterRecentPeakUs =
            Memory::DMA_EXECUTE_SCATTER_RECENT_PEAK_US.load(std::memory_order_relaxed);
        const uint64_t scatterWindowStartUs =
            Memory::DMA_EXECUTE_SCATTER_RECENT_WINDOW_START_US.load(
                std::memory_order_relaxed);
        const uint64_t scatterNowUs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count());
        if (scatterWindowStartUs > 0 &&
            scatterNowUs >= scatterWindowStartUs) {
            stats.dma.executeScatterRecentWindowAgeUs =
                scatterNowUs - scatterWindowStartUs;
        }
        stats.dma.executeScatterCount = Memory::DMA_EXECUTE_SCATTER_COUNT.load(std::memory_order_relaxed);
        stats.dma.scatterBudgetUs = Memory::DMA_SCATTER_BUDGET_US.load(std::memory_order_relaxed);
        stats.dma.scatterRequestedBytes = Memory::DMA_SCATTER_REQUESTED_BYTES.load(std::memory_order_relaxed);
        stats.dma.scatterCompletedBytes = Memory::DMA_SCATTER_COMPLETED_BYTES.load(std::memory_order_relaxed);
        stats.dma.scatterRequests = Memory::DMA_SCATTER_REQUESTS.load(std::memory_order_relaxed);
        stats.dma.scatterIncompleteRequests = Memory::DMA_SCATTER_INCOMPLETE_REQUESTS.load(std::memory_order_relaxed);
        stats.dma.scatterPartialBatches = Memory::DMA_SCATTER_PARTIAL_BATCHES.load(std::memory_order_relaxed);
        stats.dma.scatterSetupFailures = Memory::DMA_SCATTER_SETUP_FAILURES.load(std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lock(s_readQualityMutex);
            stats.dma.readQualityInterval = s_readQualityInterval;
        }
        if (stats.dma.readQualityInterval.valid &&
            nowUs >= stats.dma.readQualityInterval.completedAtUs)
            stats.dma.readQualityIntervalAgeUs = nowUs - stats.dma.readQualityInterval.completedAtUs;
        stats.dma.executeScatterRecentCount =
            Memory::DMA_EXECUTE_SCATTER_RECENT_COUNT.load(std::memory_order_relaxed);
        stats.dma.executeScatterOver1msRecentCount =
            Memory::DMA_EXECUTE_SCATTER_OVER_1MS_RECENT_COUNT.load(std::memory_order_relaxed);
        stats.dma.executeScatterOverBudgetRecentCount =
            Memory::DMA_EXECUTE_SCATTER_OVER_BUDGET_RECENT_COUNT.load(std::memory_order_relaxed);
        stats.dma.executeScatterOver5msRecentCount =
            Memory::DMA_EXECUTE_SCATTER_OVER_5MS_RECENT_COUNT.load(std::memory_order_relaxed);
        stats.dma.executeScatterOver16msRecentCount =
            Memory::DMA_EXECUTE_SCATTER_OVER_16MS_RECENT_COUNT.load(std::memory_order_relaxed);
        stats.dma.scatterHandleCreateTotalUs =
            s_scatterHandleCreateTotalUs.load(std::memory_order_relaxed);
        stats.dma.scatterHandleCreatePeakUs =
            s_scatterHandleCreatePeakUs.load(std::memory_order_relaxed);
        stats.dma.scatterHandleCreateCount =
            s_scatterHandleCreateCount.load(std::memory_order_relaxed);
        stats.dma.scatterHandleCloseTotalUs =
            s_scatterHandleCloseTotalUs.load(std::memory_order_relaxed);
        stats.dma.scatterHandleClosePeakUs =
            s_scatterHandleClosePeakUs.load(std::memory_order_relaxed);
        stats.dma.scatterHandleCloseCount =
            s_scatterHandleCloseCount.load(std::memory_order_relaxed);
        stats.dma.sessionGeneration =
            s_dmaSessionGeneration.load(std::memory_order_relaxed);
        stats.dma.attachedProcessId =
            s_attachedCs2ProcessId.load(std::memory_order_acquire);
        const auto cacheMode = static_cast<DmaCacheMode>(
            s_dmaCacheMode.load(std::memory_order_relaxed));
        const auto& cacheProfile = recovery::CacheProfileForMode(cacheMode);
        stats.dma.backgroundRefreshEnabled =
            s_dmaBackgroundRefreshEnabled.load(std::memory_order_acquire);
        if (stats.dma.backgroundRefreshEnabled) {
            stats.dma.readCacheIntervalMs = recovery::CacheIntervalMs(
                cacheProfile.tickPeriodMs,
                cacheProfile.readCacheTicks);
            stats.dma.tlbCacheIntervalMs = recovery::CacheIntervalMs(
                cacheProfile.tickPeriodMs,
                cacheProfile.tlbCacheTicks);
            stats.dma.processPartialIntervalMs = recovery::CacheIntervalMs(
                cacheProfile.tickPeriodMs,
                cacheProfile.processPartialTicks);
            stats.dma.processFullIntervalMs = recovery::CacheIntervalMs(
                cacheProfile.tickPeriodMs,
                cacheProfile.processFullTicks);
            stats.dma.estimatedSlowIntervalMs = recovery::CacheIntervalMs(
                cacheProfile.tickPeriodMs,
                recovery::kMemProcFsSlowRefreshTicks);
        }
        stats.dma.manualRefreshLastUs =
            s_dmaManualRefreshLastUs.load(std::memory_order_relaxed);
        stats.dma.manualRefreshPeakUs =
            s_dmaManualRefreshPeakUs.load(std::memory_order_relaxed);
        stats.dma.manualRefreshCount =
            s_dmaManualRefreshCount.load(std::memory_order_relaxed);
        stats.dma.manualRefreshRecentPeakUs =
            s_dmaManualRefreshRecentPeakUs.load(std::memory_order_relaxed);
        stats.dma.manualRefreshRecentCount =
            s_dmaManualRefreshRecentCount.load(std::memory_order_relaxed);
        stats.dma.manualRefreshQueuedCount =
            s_dmaManualRefreshQueuedCount.load(std::memory_order_relaxed);
        stats.dma.manualRefreshQueuedRecentCount =
            s_dmaManualRefreshQueuedRecentCount.load(std::memory_order_relaxed);
        stats.dma.manualRefreshSuppressedCount =
            s_dmaManualRefreshSuppressedCount.load(std::memory_order_relaxed);
        stats.dma.manualRefreshSuppressedRecentCount =
            s_dmaManualRefreshSuppressedRecentCount.load(std::memory_order_relaxed);
        stats.dma.manualRefreshAvoidedCount =
            s_dmaManualRefreshAvoidedCount.load(std::memory_order_relaxed);
        stats.dma.manualRefreshAvoidedRecentCount =
            s_dmaManualRefreshAvoidedRecentCount.load(std::memory_order_relaxed);
        stats.dma.manualRefreshCoalescedCount =
            s_dmaManualRefreshCoalescedCount.load(std::memory_order_relaxed);
        stats.dma.cameraPauseOrphanRecoveryCount =
            s_cameraWorkerPauseOrphanRecoveryCount.load(std::memory_order_relaxed);
        stats.dma.cameraPauseReleaseImbalanceCount =
            s_cameraWorkerPauseReleaseImbalanceCount.load(std::memory_order_relaxed);
        stats.dma.cameraPauseRequests =
            s_cameraWorkerPauseRequests.load(std::memory_order_acquire);
        stats.dma.manualRefreshInProgress =
            s_dmaManualRefreshInProgress.load(std::memory_order_acquire);
        stats.dma.adminPauseActive =
            s_dmaAdminPauseActive.load(std::memory_order_acquire);
        stats.dma.cameraWorkerPaused =
            s_cameraWorkerPaused.load(std::memory_order_acquire);
        stats.dma.cacheMode = static_cast<uint8_t>(cacheMode);
        stats.dma.cacheProfileVerified = s_dmaCacheProfileVerified.load(std::memory_order_acquire);
        const auto overlayStats = overlay::GetPerfStats();
        stats.overlay.frameUs = overlayStats.frameUs;
        stats.overlay.maxFrameUs = overlayStats.maxFrameUs;
        stats.overlay.syncUs = overlayStats.syncUs;
        stats.overlay.drawUs = overlayStats.drawUs;
        stats.overlay.presentUs = overlayStats.presentUs;
        stats.overlay.pacingWaitUs = overlayStats.pacingWaitUs;
        {
            CameraFrame cameraFrame = {};
            if (ReadCameraFrame(cameraFrame)) {
                const uint64_t cameraSampledAtUs = TickNowUs();
                const bool cameraSceneMatches =
                    cameraFrame.sceneSerial ==
                    s_sceneResetSerial.load(std::memory_order_relaxed);
                stats.liveViewValid =
                    cameraSceneMatches && cameraFrame.viewValid;
                stats.liveLocalPosValid =
                    cameraSceneMatches && cameraFrame.localPosValid;
                if (cameraFrame.viewUpdatedUs > 0 && cameraSampledAtUs >= cameraFrame.viewUpdatedUs)
                    stats.cameraViewAgeUs = cameraSampledAtUs - cameraFrame.viewUpdatedUs;
                if (cameraFrame.localPosUpdatedUs > 0 && cameraSampledAtUs >= cameraFrame.localPosUpdatedUs)
                    stats.cameraLocalPosAgeUs = cameraSampledAtUs - cameraFrame.localPosUpdatedUs;
            }
        }
        stats.liveViewFresh =
            stats.liveViewValid &&
            stats.cameraViewAgeUs <= kLiveCameraFreshnessUs;
        stats.liveLocalPosFresh =
            stats.liveLocalPosValid &&
            stats.cameraLocalPosAgeUs <= kLiveCameraFreshnessUs;
        stats.engineMenu = s_engineMenu.load(std::memory_order_relaxed);
        stats.dataWorkerTargetHz = s_dataWorkerTargetHz.load(std::memory_order_relaxed);
        stats.cameraWorkerTargetHz = s_cameraWorkerTargetHz.load(std::memory_order_relaxed);
        stats.cameraReadsEnabled = s_cameraReadsEnabled.load(std::memory_order_relaxed);
        stats.engineInGame = s_engineInGame.load(std::memory_order_relaxed);
        stats.engineResolved = s_engineStatusResolved.load(std::memory_order_relaxed);
        if (!worker::ShouldReadLiveCamera(true, stats.engineResolved,
                stats.engineInGame, stats.engineMenu, false)) {
            stats.liveViewValid = stats.liveViewFresh = false;
            stats.liveLocalPosValid = stats.liveLocalPosFresh = false;
        }
        stats.engineBackgroundMap = s_engineBackgroundMap.load(std::memory_order_relaxed);
        stats.engineSignOnState = s_engineSignOnState.load(std::memory_order_relaxed);
        stats.sceneEpoch = s_sceneResetSerial.load(std::memory_order_relaxed);
        stats.mapEpoch = s_mapEpoch.load(std::memory_order_relaxed);
        stats.bombEpoch = s_bombEpoch.load(std::memory_order_relaxed);
        stats.warmupState =
            static_cast<esp::SceneWarmupState>(s_sceneWarmupState.load(std::memory_order_relaxed));
        const uint64_t warmupEnteredUs = s_sceneWarmupEnteredUs.load(std::memory_order_relaxed);
        if (warmupEnteredUs > 0 && nowUs >= warmupEnteredUs)
            stats.warmupAgeUs = nowUs - warmupEnteredUs;
        stats.lastResetKind =
            static_cast<esp::RuntimeResetKind>(s_lastRuntimeResetKind.load(std::memory_order_relaxed));
        stats.lastResetPublishedClearedSnapshot =
            s_lastRuntimeResetPublishedClearedSnapshot.load(std::memory_order_relaxed);
        const uint64_t lastResetUs = s_lastRuntimeResetUs.load(std::memory_order_relaxed);
        if (lastResetUs > 0 && nowUs >= lastResetUs)
            stats.lastResetAgeUs = nowUs - lastResetUs;
        {
            std::lock_guard<std::mutex> lock(s_runtimeResetMutex);
            strncpy_s(stats.lastResetReason, sizeof(stats.lastResetReason), s_lastRuntimeResetReason, _TRUNCATE);
        }
        stats.playersCore = GetSubsystemHealthInfo(RuntimeSubsystem::PlayersCore, nowUs);
        stats.cameraView = GetSubsystemHealthInfo(RuntimeSubsystem::CameraView, nowUs);
        stats.gamerulesMap = GetSubsystemHealthInfo(RuntimeSubsystem::GameRulesMap, nowUs);
        stats.bones = GetSubsystemHealthInfo(RuntimeSubsystem::Bones, nowUs);
        stats.world = GetSubsystemHealthInfo(RuntimeSubsystem::World, nowUs);
        const uint32_t bombFlags = s_bombDebugFlags.load(std::memory_order_relaxed);
        stats.bombPlanted = (bombFlags & (1u << 0)) != 0u;
        stats.bombTicking = (bombFlags & (1u << 1)) != 0u;
        stats.bombBeingDefused = (bombFlags & (1u << 2)) != 0u;
        stats.bombDropped = (bombFlags & (1u << 3)) != 0u;
        stats.bombBoundsValid = (bombFlags & (1u << 4)) != 0u;
        stats.bombPositionValid = (bombFlags & (1u << 5)) != 0u;
        stats.bombDropPublicationDebug = s_bombDropPublicationDebug.load(std::memory_order_relaxed);
        const uint64_t bombPositionSampleUs = s_bombDebugPositionSampleUs.load(std::memory_order_relaxed);
        if (stats.bombPositionValid && bombPositionSampleUs > 0 && nowUs >= bombPositionSampleUs)
            stats.bombPositionAgeUs = nowUs - bombPositionSampleUs;
        stats.bombSourceFlags = s_bombDebugSourceFlags.load(std::memory_order_relaxed);
        stats.bombRawFlags = s_bombDebugRawFlags.load(std::memory_order_relaxed);
        stats.bombConfidence = s_bombDebugConfidence.load(std::memory_order_relaxed);
        stats.bombDefuserSlot = s_bombDebugDefuserSlot.load(std::memory_order_relaxed);
        stats.bombBlowLeftMs = s_bombDebugBlowLeftMs.load(std::memory_order_relaxed);
        stats.bombDefuseLeftMs = s_bombDebugDefuseLeftMs.load(std::memory_order_relaxed);
        stats.bombCarried =
            !stats.bombPlanted &&
            !stats.bombDropped &&
            stats.bombConfidence > 0 &&
            (stats.bombSourceFlags &
             (BombResolveSourceCarrySignal |
              BombResolveSourceCarrySticky |
              BombResolveSourceAttachGrace |
              BombResolveSourceSpatialVeto)) != 0u;
        constexpr uint64_t kRecentEspEventWindowUs = 30000000u;
        constexpr uint64_t kBoneRejectEventWindowUs = 5000000u;
        std::lock_guard<std::mutex> espEventLock(s_espEventMutex);
        const uint32_t espEventCount = std::min<uint32_t>(
            s_espEventCount.load(std::memory_order_relaxed),
            static_cast<uint32_t>(esp::DebugStats::kMaxEspEvents));
        const uint32_t espEventWrite = s_espEventWriteIndex.load(std::memory_order_relaxed);
        uint32_t collectedEspEvents = 0;
        for (uint32_t i = 0; i < espEventCount && collectedEspEvents < esp::DebugStats::kMaxEspEvents; ++i) {
            const uint32_t index = (espEventWrite + 4096 - 1u - i) % 4096;
            const EspEventRecord& source = s_espEventRing[index];
            if (source.timeUs == 0)
                continue;
            const uint64_t ageUs = nowUs >= source.timeUs ? (nowUs - source.timeUs) : 0;
            if (ageUs > kRecentEspEventWindowUs)
                continue;
            if (static_cast<EspEventType>(source.type) == EspEventType::BoneRejected &&
                ageUs > kBoneRejectEventWindowUs) {
                continue;
            }
            auto& target = stats.espEvents[collectedEspEvents++];
            strncpy_s(
                target.type,
                sizeof(target.type),
                EspEventTypeName(static_cast<EspEventType>(source.type)),
                _TRUNCATE);
            target.slot = source.slot;
            target.param = source.param;
            target.ageMs = ageUs / 1000u;
        }
        stats.espEventCount = static_cast<int>(collectedEspEvents);
        return stats;
    }

    bool GetTargetSnapshot(TargetSnapshot* outSnapshot)
    {
        if (!outSnapshot)
            return false;

        TargetSnapshot result = {};
        Vector3 snapshotViewOffset = {};
        bool snapshotViewOffsetValid = false;
        uint64_t snapshotViewOffsetUpdatedAtUs = 0;
        for (;;) {
            const int publishedIndex =
                s_publishedSnapshotIdx.load(std::memory_order_acquire);
            if (!state::IsSnapshotSlotIndexValid(publishedIndex))
                return false;
            SnapshotSlot& slot = s_snapshotSlots[publishedIndex];
            std::shared_lock lock(slot.mutex);
            if (s_publishedSnapshotIdx.load(std::memory_order_acquire) !=
                publishedIndex) {
                continue;
            }
            const EntitySnapshot& snap = slot.data;
            std::copy(
                std::begin(snap.players),
                std::end(snap.players),
            result.players.begin());
            result.localPos = snap.localPos;
            result.localEyePos = snap.localPos + snap.localViewOffset;
            snapshotViewOffset = snap.localViewOffset;
            snapshotViewOffsetValid = snap.localViewOffsetValid;
            snapshotViewOffsetUpdatedAtUs =
                snap.localViewOffsetUpdatedAtUs;
            result.localEyeValid = snap.localPawn != 0 &&
                snap.localPosValid &&
                snap.localPosUpdatedAtUs > 0 &&
                snap.localViewOffsetValid &&
                snap.localViewOffsetUpdatedAtUs > 0 &&
                IsFiniteVec(snap.localPos) &&
                IsFiniteVec(result.localEyePos);
            result.localEyeUpdatedAtUs = result.localEyeValid
                ? (std::min)(
                    snap.localViewOffsetUpdatedAtUs,
                    snap.localPosUpdatedAtUs)
                : 0u;
            result.viewAngles = snap.viewAngles;
            result.localAimPunch = snap.localAimPunch;
            result.localShotsFired = snap.localShotsFired;
            result.localShotsFiredValid = snap.localShotsFiredValid;
            result.localAimPunchValid = snap.localAimPunchValid;
            result.localAimPunchUpdatedAtUs =
                snap.localAimPunchUpdatedAtUs;
            std::memcpy(
                &result.viewMatrix,
                &snap.viewMatrix,
                sizeof(view_matrix_t));
            result.localPawn = snap.localPawn;
            result.localTeam = snap.localTeam;
            result.localPlayerIndex = snap.localPlayerIndex;
            result.localWeaponId = snap.localWeaponId;
            result.localWeaponHandle = snap.localWeaponHandle;
            result.localWeaponEntity = snap.localWeaponEntity;
            result.localAmmoClip = snap.localAmmoClip;
            result.localAmmoValid = snap.localAmmoValid;
            result.localAmmoUpdatedAtUs = snap.localAmmoUpdatedAtUs;
            result.localWeaponUpdatedAtUs = snap.localWeaponUpdatedAtUs;
            result.localWeaponVData = snap.localWeaponTelemetry.vdata;
            result.localWeaponTelemetryValid = snap.localWeaponTelemetry.valid;
            result.localIsReloading = snap.localWeaponTelemetry.isReloading;
            result.localWeaponReady = snap.localWeaponTelemetry.ready;
            result.localIsScoped = snap.localWeaponTelemetry.isScoped;
            result.localOnGround = snap.localWeaponTelemetry.onGround;
            result.localIsWalking = snap.localWeaponTelemetry.isWalking;
            result.localWeaponFullAuto = snap.localWeaponTelemetry.fullAuto;
            result.localWeaponMode = snap.localWeaponTelemetry.mode;
            result.localWeaponType = snap.localWeaponTelemetry.weaponType;
            result.localWeaponBullets = snap.localWeaponTelemetry.bullets;
            result.localCurrentTime = snap.localWeaponTelemetry.currentTime;
            result.localIntervalPerTick =
                snap.localWeaponTelemetry.intervalPerTick;
            result.localRenderTick = snap.localWeaponTelemetry.renderTick;
            result.localCycleTime = snap.localWeaponTelemetry.cycleTime;
            result.localLastShotTime = snap.localWeaponTelemetry.lastShotTime;
            result.localInaccuracy = snap.localWeaponTelemetry.inaccuracy;
            result.localInaccuracyWithoutAir =
                snap.localWeaponTelemetry.inaccuracyWithoutAir;
            result.localJumpInaccuracyInitial =
                snap.localWeaponTelemetry.jumpInaccuracyInitial;
            result.localJumpInaccuracyApex =
                snap.localWeaponTelemetry.jumpInaccuracyApex;
            result.localVerticalVelocity =
                snap.localWeaponTelemetry.localVerticalVelocity;
            result.localSpread = snap.localWeaponTelemetry.spread;
            result.localBaseInaccuracy = snap.localWeaponTelemetry.baseInaccuracy;
            result.localRecoilIndex = snap.localWeaponTelemetry.recoilIndex;
            result.localWeaponDamage = snap.localWeaponTelemetry.damage;
            result.localWeaponPenetration = snap.localWeaponTelemetry.penetration;
            result.localWeaponRange = snap.localWeaponTelemetry.range;
            result.localWeaponRangeModifier = snap.localWeaponTelemetry.rangeModifier;
            result.localWeaponArmorRatio = snap.localWeaponTelemetry.armorRatio;
            result.localWeaponHeadshotMultiplier =
                snap.localWeaponTelemetry.headshotMultiplier;
            result.localWeaponTelemetryUpdatedAtUs =
                snap.localWeaponTelemetry.updatedAtUs;
            result.localShotsUpdatedAtUs = snap.localShotsUpdatedAtUs;
            result.sensitivity = snap.sensitivity;
            result.fovSensitivityAdjust = snap.fovSensitivityAdjust;
            result.captureTimeUs = snap.playerCoreCaptureTimeUs != 0
                ? snap.playerCoreCaptureTimeUs
                : snap.captureTimeUs;
            result.viewUpdatedAtUs = 0;
            result.sceneSerial = snap.sceneSerial;
            result.localIsDead = snap.localIsDead;
            break;
        }
        const ActiveMapStateSnapshot activeMapState = CopyActiveMapState();
        if (!activeMapState.key.empty()) {
            strncpy_s(
                result.mapKey,
                sizeof(result.mapKey),
                activeMapState.key.c_str(),
                _TRUNCATE);
        }

        PlayerVisibilityFrame visibility = {};
        const bool hasVisibility = ReadPlayerVisibilityFrame(visibility);
        CameraFrame camera = {};
        const bool hasCamera = ReadCameraFrame(camera);
        const uint64_t nowUs = TickNowUs();
        result.sampledAtUs = nowUs;
        result.snapshotAgeUs =
            result.captureTimeUs > 0 && nowUs >= result.captureTimeUs
                ? nowUs - result.captureTimeUs
                : UINT64_MAX;
        if (hasVisibility &&
            visibility.sceneSerial == result.sceneSerial &&
            visibility.captureTimeUs > 0 &&
            nowUs >= visibility.captureTimeUs &&
             nowUs - visibility.captureTimeUs <= 100000u) {
            result.crosshairValid = visibility.crosshairValid &&
                visibility.crosshairUpdatedAtUs > 0 &&
                nowUs >= visibility.crosshairUpdatedAtUs &&
                nowUs - visibility.crosshairUpdatedAtUs <= 100000u;
            result.crosshairPlayerIndex = result.crosshairValid
                ? visibility.crosshairPlayerIndex
                : -1;
            result.crosshairUpdatedAtUs = result.crosshairValid
                ? visibility.crosshairUpdatedAtUs
                : 0u;
            result.crosshairGeneration = result.crosshairValid
                ? visibility.generation
                : 0u;
            if (result.crosshairValid &&
                visibility.crosshairPlayerIndex >= 0 &&
                visibility.crosshairPlayerIndex <
                    static_cast<int>(result.players.size()) &&
                visibility.players[visibility.crosshairPlayerIndex].pawn != 0 &&
                visibility.players[visibility.crosshairPlayerIndex].pawn ==
                    result.players[visibility.crosshairPlayerIndex].pawn) {
                result.crosshairPawn =
                    visibility.players[visibility.crosshairPlayerIndex].pawn;
            }
            for (size_t i = 0; i < result.players.size(); ++i) {
                if (result.players[i].pawn != 0 &&
                    result.players[i].pawn == visibility.players[i].pawn) {
                    result.players[i].visible = visibility.players[i].visible;
                    result.players[i].visibilityUpdatedAtUs =
                        visibility.captureTimeUs;
                }
            }
        }

        if (hasCamera &&
            camera.sceneSerial == result.sceneSerial &&
            camera.viewValid &&
            camera.viewUpdatedUs > 0 &&
            nowUs >= camera.viewUpdatedUs &&
            nowUs - camera.viewUpdatedUs <= kLiveCameraFreshnessUs &&
            IsLikelyViewMatrix(camera.viewMatrix)) {
            std::memcpy(&result.viewMatrix, &camera.viewMatrix, sizeof(view_matrix_t));
            if (camera.viewAnglesValid &&
                camera.viewAnglesUpdatedUs > 0 &&
                nowUs >= camera.viewAnglesUpdatedUs &&
                nowUs - camera.viewAnglesUpdatedUs <=
                    kLiveCameraFreshnessUs &&
                target::policy::IsTimestampSkewAcceptable(
                    camera.viewUpdatedUs,
                    camera.viewAnglesUpdatedUs,
                    target::policy::kMaximumTriggerViewSkewUs)) {
                result.viewAngles = camera.viewAngles;
                result.viewUpdatedAtUs = camera.viewAnglesUpdatedUs;
            }
        }
        if (hasCamera && state::ShouldApplyCameraLocalPosition(
                camera.sceneSerial, result.sceneSerial,
                camera.localPosPawn, result.localPawn,
                camera.localPosValid, camera.localPosUpdatedUs,
                nowUs, kLiveCameraFreshnessUs)) {
            result.localPos = camera.localPos;
            if (!snapshotViewOffsetValid ||
                snapshotViewOffsetUpdatedAtUs == 0 ||
                !IsFiniteVec(snapshotViewOffset) ||
                std::fabs(snapshotViewOffset.x) > 32.0f ||
                std::fabs(snapshotViewOffset.y) > 32.0f ||
                snapshotViewOffset.z < 8.0f ||
                snapshotViewOffset.z > 96.0f) {
                result.localEyePos = result.localPos + Vector3(0.0f, 0.0f, 64.0f);
                result.localEyeValid = false;
                result.localEyeUpdatedAtUs = 0;
            } else {
                result.localEyePos = camera.localPos + snapshotViewOffset;
                result.localEyeValid = IsFiniteVec(result.localEyePos);
                result.localEyeUpdatedAtUs = (std::min)(
                    snapshotViewOffsetUpdatedAtUs,
                    camera.localPosUpdatedUs);
                if (!result.localEyeValid)
                    result.localEyeUpdatedAtUs = 0;
            }
        }

        result.viewValid = IsLikelyViewMatrix(result.viewMatrix);
        *outSnapshot = std::move(result);
        return result.viewValid;
    }
}
