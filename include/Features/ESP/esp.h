#pragma once
#include "Game/Schema/structs.h"
#include "Features/ESP/Worker/worker_policy.h"
#include <array>
#include <cstdint>

namespace esp {

    // A sustainable fixed-rate data cadence is preferable to nominal 300 Hz
    // with frequent deadline misses. Camera sampling remains independently
    // phase-shifted at 300 Hz. The 250 Hz target gives a nominal 4 ms
    // interval, not a freshness guarantee: DMA and optional lanes can overrun.
    inline constexpr int kDataWorkerLiveHz = 250;
    inline constexpr int kCameraWorkerLiveHz = 300;

    enum BoneIndex : int {
        ORIGIN = 0,
        PELVIS = 1,
        SPINE0 = 2,
        SPINE1 = 3,
        SPINE2 = 4,
        AIM_NECK = 5,
        NECK = 6,
        HEAD = 7,
        CLAVICLE_L = 8,
        SHOULDER_L = 9,
        ELBOW_L = 10,
        HAND_L = 11,
        CLAVICLE_R = 12,
        SHOULDER_R = 13,
        ELBOW_R = 14,
        HAND_R = 15,
        HIP_L = 17,
        KNEE_L = 18,
        FOOT_HEEL_L = 19,
        HIP_R = 20,
        KNEE_R = 21,
        FOOT_HEEL_R = 22,
        CHEST = 23,
        GUN = 24,
        EYE_L = 25,
        EYE_R = 26,
        RANDOM = 27,
        CVJ_BONE = 28,
        FOOT_TOES_L_T = 74,
        FOOT_TOES_R_T = 77,
        FOOT_TOES_L_CT = 81,
        FOOT_TOES_R_CT = 86,
        BONE_MAX = 128
    };

    inline constexpr int kPlayerStoredBoneIds[] = {
        PELVIS,
        SPINE1,
        SPINE2,
        NECK,
        HEAD,
        SHOULDER_L,
        ELBOW_L,
        HAND_L,
        SHOULDER_R,
        ELBOW_R,
        HAND_R,
        HIP_L,
        KNEE_L,
        FOOT_HEEL_L,
        HIP_R,
        KNEE_R,
        FOOT_HEEL_R,
        CHEST,
        FOOT_TOES_L_T,
        FOOT_TOES_R_T,
        FOOT_TOES_L_CT,
        FOOT_TOES_R_CT,
        AIM_NECK
    };
    inline constexpr int kPlayerStoredBoneCount = static_cast<int>(std::size(kPlayerStoredBoneIds));
    inline constexpr int kSkeletonScreenBoneCapacity = BONE_MAX;

    constexpr int PlayerStoredBoneIndex(int boneId)
    {
        switch (boneId) {
        case PELVIS: return 0;
        case SPINE1: return 1;
        case SPINE2: return 2;
        case NECK: return 3;
        case HEAD: return 4;
        case SHOULDER_L: return 5;
        case ELBOW_L: return 6;
        case HAND_L: return 7;
        case SHOULDER_R: return 8;
        case ELBOW_R: return 9;
        case HAND_R: return 10;
        case HIP_L: return 11;
        case KNEE_L: return 12;
        case FOOT_HEEL_L: return 13;
        case HIP_R: return 14;
        case KNEE_R: return 15;
        case FOOT_HEEL_R: return 16;
        case CHEST: return 17;
        case FOOT_TOES_L_T: return 18;
        case FOOT_TOES_R_T: return 19;
        case FOOT_TOES_L_CT: return 20;
        case FOOT_TOES_R_CT: return 21;
        case AIM_NECK: return 22;
        default: return -1;
        }
    }

    constexpr int LeftToeBoneForTeam(int team)
    {
        return team == 3 ? FOOT_TOES_L_CT : FOOT_TOES_L_T;
    }

    constexpr int RightToeBoneForTeam(int team)
    {
        return team == 3 ? FOOT_TOES_R_CT : FOOT_TOES_R_T;
    }

    enum class GameStatus : uint8_t {
        Ok = 0,       
        WaitCs2 = 2,  
    };

    enum class SceneWarmupState : uint8_t {
        ColdAttach = 0,
        SceneTransition,
        HierarchyWarming,
        Stable,
        Recovery,
    };

    enum class RuntimeResetKind : uint8_t {
        None = 0,
        Soft,
        Hard,
    };

    enum class DmaRefreshTier : uint8_t {
        Probe = 0,
        Repair,
        Full,
    };

    enum class DmaRefreshTrigger : uint8_t {
        General = 0,
        BoneSlotStale,
    };

    enum class DmaCacheMode : uint8_t {
        Maintenance = 0,
        Live,
        ProcessDiscovery,
    };

    enum class SubsystemHealthState : uint8_t {
        Unknown = 0,
        Healthy,
        Degraded,
        Failed,
    };

    struct SubsystemHealthInfo {
        SubsystemHealthState state = SubsystemHealthState::Unknown;
        uint32_t failureStreak = 0;
        uint64_t lastGoodAgeUs = 0;
    };

    struct DmaHealthStats {
        struct Event {
            char action[24] = {};
            char reason[96] = {};
            uint64_t ageMs = 0;
        };
        static constexpr int kMaxEvents = 8;

        bool     workerRunning = false;
        bool     cameraWorkerRunning = false;
        bool     dataWorkerInFlight = false;
        bool     dataWorkerStalled = false;
        bool     recovering = false;
        bool     recoveryRequested = false;
        uint32_t consecutiveFailures = 0;
        uint32_t consecutiveDegraded = 0;
        uint64_t totalFailures = 0;
        uint64_t totalDegraded = 0;
        uint64_t totalSuccesses = 0;
        uint64_t totalRecoveries = 0;
        uint64_t lastSuccessAgeMs = 0;
        uint64_t recoveryRequestAgeMs = 0;
        uint64_t dataWorkerLoopAgeMs = 0;
        uint64_t dataWorkerInFlightAgeMs = 0;
        GameStatus gameStatus = GameStatus::WaitCs2;
        Event events[kMaxEvents] = {};
        int eventCount = 0;
    };

    struct EspDebugEvent {
        char type[40] = {};
        uint8_t slot = 0xFF;
        uint16_t param = 0;
        uint64_t ageMs = 0;
    };

    
    struct StageTiming {
        uint64_t engineUs = 0;
        uint64_t baseReadsUs = 0;
        uint64_t playerReadsUs = 0;
        uint64_t playerHierarchyUs = 0;
        uint64_t playerCoreUs = 0;
        uint64_t playerRepairUs = 0;
        uint64_t commitStateUs = 0;
        uint64_t playerAuxUs = 0;
        uint64_t inventoryUs = 0;
        uint64_t boneReadsUs = 0;
        uint64_t bombScanUs = 0;
        uint64_t worldScanUs = 0;
        uint64_t worldScanLastUs = 0;
        uint64_t commitEnrichUs = 0;
        uint64_t playerAuxLastUs = 0;
        uint64_t inventoryLastUs = 0;
        uint64_t boneReadsLastUs = 0;
        uint64_t playerAuxAgeUs = 0;
        uint64_t inventoryAgeUs = 0;
        uint64_t boneReadsAgeUs = 0;
        uint32_t bonePoseSlots = 0;
        uint32_t bonePointerValidationSlots = 0;
        uint32_t bonePoseRanges = 0;
        uint32_t bonePoseBytes = 0;
        uint64_t totalUs = 0;
        uint64_t totalHeldUs = 0;
    };

    struct CameraTiming {
        uint64_t cycleUs = 0;
        uint64_t maxCycleUs = 0;
        uint64_t recentMaxCycleUs = 0;
        uint64_t recentWindowAgeUs = 0;
        uint64_t p50Us = 0;
        uint64_t p95Us = 0;
        uint64_t p99Us = 0;
        uint64_t deadlineMissCount = 0;
        uint64_t sampleCount10s = 0;
        uint64_t overBudgetCount10s = 0;
        uint64_t over5msCount10s = 0;
        uint64_t over16msCount10s = 0;
    };

    struct OverlayTiming {
        uint64_t frameUs = 0;
        uint64_t maxFrameUs = 0;
        uint64_t syncUs = 0;
        uint64_t drawUs = 0;
        uint64_t presentUs = 0;
        uint64_t pacingWaitUs = 0;
    };

    struct DmaTiming {
        uint64_t executeScatterTotalUs = 0;
        uint64_t executeScatterPeakUs = 0;
        uint64_t executeScatterRecentPeakUs = 0;
        uint64_t executeScatterRecentWindowAgeUs = 0;
        uint64_t executeScatterCount = 0;
        uint64_t executeScatterRecentCount = 0;
        uint64_t executeScatterOver1msRecentCount = 0;
        uint64_t executeScatterOverBudgetRecentCount = 0;
        uint64_t executeScatterOver5msRecentCount = 0;
        uint64_t executeScatterOver16msRecentCount = 0;
        uint64_t scatterBudgetUs = 0;
        uint64_t scatterRequestedBytes = 0;
        uint64_t scatterCompletedBytes = 0;
        uint64_t scatterRequests = 0;
        uint64_t scatterIncompleteRequests = 0;
        uint64_t scatterPartialBatches = 0;
        uint64_t scatterSetupFailures = 0;
        // Requests, incomplete requests, partial batches, setup failures.
        worker::CounterIntervalSample<4> readQualityInterval = {};
        uint64_t readQualityIntervalAgeUs = 0;
        uint64_t scatterHandleCreateTotalUs = 0;
        uint64_t scatterHandleCreatePeakUs = 0;
        uint64_t scatterHandleCreateCount = 0;
        uint64_t scatterHandleCloseTotalUs = 0;
        uint64_t scatterHandleClosePeakUs = 0;
        uint64_t scatterHandleCloseCount = 0;
        uint64_t sessionGeneration = 0;
        uint32_t attachedProcessId = 0;
        uint64_t readCacheIntervalMs = 0;
        uint64_t tlbCacheIntervalMs = 0;
        uint64_t processPartialIntervalMs = 0;
        uint64_t processFullIntervalMs = 0;
        uint64_t estimatedSlowIntervalMs = 0;
        uint64_t manualRefreshLastUs = 0;
        uint64_t manualRefreshPeakUs = 0;
        uint64_t manualRefreshCount = 0;
        uint64_t manualRefreshRecentPeakUs = 0;
        uint64_t manualRefreshRecentCount = 0;
        uint64_t manualRefreshQueuedCount = 0;
        uint64_t manualRefreshQueuedRecentCount = 0;
        uint64_t manualRefreshSuppressedCount = 0;
        uint64_t manualRefreshSuppressedRecentCount = 0;
        uint64_t manualRefreshAvoidedCount = 0;
        uint64_t manualRefreshAvoidedRecentCount = 0;
        uint64_t manualRefreshCoalescedCount = 0;
        uint64_t cameraPauseOrphanRecoveryCount = 0;
        uint64_t cameraPauseReleaseImbalanceCount = 0;
        uint32_t cameraPauseRequests = 0;
        uint8_t cacheMode = 0;
        bool cacheProfileVerified = false;
        bool backgroundRefreshEnabled = false;
        bool manualRefreshInProgress = false;
        bool adminPauseActive = false;
        bool cameraWorkerPaused = false;
    };

    struct DebugStats {
        static constexpr int kMaxEspEvents = 8;

        uint64_t publishCount = 0;
        uint64_t publishDropCount = 0;
        uint64_t visibilityPublishDropCount = 0;
        uint64_t cameraPublishDropCount = 0;
        uint64_t settingsSnapshotReuseCount = 0;
        uint64_t lastPublishUs = 0;
        uint64_t cycleUs = 0;
        uint64_t maxCycleUs = 0;
        uint64_t recentMaxCycleUs = 0;
        uint64_t cycleWindowAgeUs = 0;
        uint64_t cycleP50Us = 0;
        uint64_t cycleP95Us = 0;
        uint64_t cycleP99Us = 0;
        uint64_t deadlineMissCount = 0;
        uint64_t cycleSampleCount10s = 0;
        uint64_t cycleOverBudgetCount10s = 0;
        uint64_t cycleOver5msCount10s = 0;
        uint64_t cycleOver16msCount10s = 0;
        uint64_t playerCoreAnomalyCount10s = 0;
        uint64_t playerCoreRecoveredCount10s = 0;
        uint64_t playerCoreGlobalRefreshAvoidedCount10s = 0;
        uint64_t playerUnexpectedEvictionCount10s = 0;
        uint64_t playerExpectedEvictionCount10s = 0;
        int32_t  playerControllerSlots = 0;
        int32_t  playerResolvedSlots = 0;
        int32_t  playerPlausibleCoreSlots = 0;
        uint64_t playerCoreGeneration = 0;
        uint64_t playerCoreGenerationAgeUs = 0;
        uint64_t playerCoreBatchHoldCount10s = 0;
        uint8_t  playerCoreBatchQuality = 0;
        uint64_t playerCoreIncompleteMask = 0;
        uint64_t playerCoreInvalidMask = 0;
        int32_t  playerDuplicateIdentityFiltered = 0;
        int32_t  playerBacklinkMismatchCount = 0;
        int32_t  playerHierarchyHeldSlots = 0;
        int32_t  playerZeroPawnHeldSlots = 0;
        int32_t  playerCoreHeldSlots = 0;
        int32_t  activePlayers = 0;
        int32_t  playerSlotBudget = 64;
        int32_t  engineMaxClients = 0;
        int32_t  highestEntityIdx = 0;
        uint32_t entitySlotStride = 0x70u;
        int32_t  worldMarkerCount = 0;
        int32_t  worldTrackedEntityCount = 0;
        int32_t  worldCandidateCount = 0;
        int32_t  worldUtilityCandidateCount = 0;
        int32_t  worldClassifiedCandidateCount = 0;
        int32_t  worldIdentityPendingCount = 0;
        uint64_t worldMarkerCapacityDrops = 0;
        uint64_t worldMarkerReadGapHolds = 0;
        uint64_t worldPositionReadMisses = 0;
        int32_t  visibilityFreshSlots = 0;
        int32_t  visibilityVisibleSlots = 0;
        int32_t  visibilityMaskSlots = 0;
        int32_t  visibilityCrosshairSlot = -1;
        uint64_t visibilityCommitAgeUs = 0;
        bool     visibilityEnabled = false;
        bool     visibilityLocalMaskResolved = false;
        uint64_t uptimeUs = 0;
        uint64_t sessionUptimeUs = 0;
        uint64_t worldScanAgeUs = 0;
        uint64_t worldScanTargetIntervalUs = 0;
        uint64_t cameraViewAgeUs = 0;
        uint64_t cameraLocalPosAgeUs = 0;
        bool     liveViewValid = false;
        bool     liveViewFresh = false;
        bool     liveLocalPosValid = false;
        bool     liveLocalPosFresh = false;
        bool     engineMenu = false;
        int32_t  dataWorkerTargetHz = 0;
        int32_t  cameraWorkerTargetHz = 0;
        bool     cameraReadsEnabled = false;
        bool     engineInGame = false;
        bool     engineResolved = false;
        bool     engineBackgroundMap = false;
        int32_t  engineSignOnState = -1;
        uint64_t sceneEpoch = 0;
        uint64_t mapEpoch = 0;
        uint64_t bombEpoch = 0;
        uint64_t warmupAgeUs = 0;
        SceneWarmupState warmupState = SceneWarmupState::ColdAttach;
        RuntimeResetKind lastResetKind = RuntimeResetKind::None;
        bool     lastResetPublishedClearedSnapshot = false;
        uint64_t lastResetAgeUs = 0;
        char     lastResetReason[96] = {};
        SubsystemHealthInfo playersCore = {};
        SubsystemHealthInfo cameraView = {};
        SubsystemHealthInfo gamerulesMap = {};
        SubsystemHealthInfo bones = {};
        SubsystemHealthInfo world = {};
        bool     bombPlanted = false;
        bool     bombTicking = false;
        bool     bombBeingDefused = false;
        bool     bombDropped = false;
        bool     bombCarried = false;
        bool     bombBoundsValid = false;
        bool     bombPositionValid = false;
        uint64_t bombPositionAgeUs = 0;
        uint64_t bombDropPublicationDebug = 0;
        uint32_t bombSourceFlags = 0;
        uint32_t bombRawFlags = 0;
        uint8_t  bombConfidence = 0;
        int32_t  bombDefuserSlot = -1;
        int32_t  bombBlowLeftMs = -1;
        int32_t  bombDefuseLeftMs = -1;
        CameraTiming camera = {};
        OverlayTiming overlay = {};
        StageTiming stages = {};
        DmaTiming dma = {};
        EspDebugEvent espEvents[kMaxEspEvents] = {};
        int espEventCount = 0;
    };

    inline constexpr int kMaximumPlayerHitboxes = 20;

    struct HitboxCapsule {
        Vector3 start = {};
        Vector3 end = {};
        Vector3 center = {};
        float radius = 0.0f;
        int16_t bone = -1;
        uint8_t index = 0;
        uint8_t hitgroup = 0;
        bool valid = false;
    };

    struct WeaponTelemetry {
        uintptr_t vdata = 0;
        bool valid = false;
        bool isReloading = false;
        bool ready = false;
        bool isScoped = false;
        bool onGround = false;
        bool isWalking = false;
        bool fullAuto = false;
        int mode = 0;
        int weaponType = 0;
        int bullets = 1;
        float currentTime = 0.0f;
        float intervalPerTick = 0.0f;
        int renderTick = 0;
        float cycleTime = 0.0f;
        float lastShotTime = 0.0f;
        float inaccuracy = 0.0f;
        float inaccuracyWithoutAir = 0.0f;
        float jumpInaccuracyInitial = 0.0f;
        float jumpInaccuracyApex = 0.0f;
        float localVerticalVelocity = 0.0f;
        float spread = 0.0f;
        float baseInaccuracy = 0.0f;
        float recoilIndex = 0.0f;
        float damage = 0.0f;
        float penetration = 0.0f;
        float range = 0.0f;
        float rangeModifier = 0.0f;
        float armorRatio = 0.0f;
        float headshotMultiplier = 0.0f;
        uint64_t updatedAtUs = 0;
    };

    struct PlayerData {
        bool     valid = false;
        uintptr_t pawn = 0;
        uint32_t pawnHandle = 0;
        int      health = 0;
        int      armor = 0;
        bool     hasHelmet = false;
        bool     hasHelmetValid = false;
        int      team = 0;
        int      money = 0;
        int      ping = 0;
        Vector3  position;
        Vector3  velocity;
        bool     velocityValid = false;
        char     name[128] = {};
        Vector3  bones[kPlayerStoredBoneCount] = {};
        bool     hasBones = false;
        Vector3  boneAnchorPosition = {};
        bool     boneAnchorValid = false;
        HitboxCapsule hitboxes[kMaximumPlayerHitboxes] = {};
        uint8_t  hitboxCount = 0;
        bool     hasHitboxes = false;
        bool     visible = false;
        bool     gunGameImmunity = false;
        bool     gunGameImmunityValid = false;
        bool     scoped = false;
        bool     defusing = false;
        bool     hasDefuser = false;
        bool     flashed = false;
        uint16_t weaponId = 0;
        uint16_t weaponIconId = 0;
        int      ammoClip = -1;
        bool     hasBomb = false;
        float    flashDuration = 0.0f;
        float    eyeYaw = 0.0f;
        int      staleFrames = 0;
        uint64_t coreUpdatedAtUs = 0;
        uint64_t bonesUpdatedAtUs = 0;
        uint64_t hitboxesUpdatedAtUs = 0;
        uint64_t helmetUpdatedAtUs = 0;
        uint64_t gunGameImmunityUpdatedAtUs = 0;
        uint64_t visibilityUpdatedAtUs = 0;
        
        static constexpr int kMaxGrenades = 4;
        uint16_t grenadeIds[kMaxGrenades] = {};
        int      grenadeCount = 0;
    };

    struct BombSnapshot {
        bool     planted = false;
        bool     ticking = false;
        bool     beingDefused = false;
        bool     dropped = false;
        Vector3  position = {};
        float    blowTime = 0.0f;
        float    timerLength = 0.0f;
        float    defuseEndTime = 0.0f;
        float    defuseLength = 0.0f;
        float    currentGameTime = 0.0f;
    };

    struct WebRadarWorldMarker {
        uint8_t  type = 0;        
        Vector3  position = {};
        uint16_t weaponId = 0;
        float    lifeRemainingSec = 0.0f;
    };

    struct WebRadarSnapshot {
        std::array<PlayerData, 64> players = {};
        char localName[128] = {};
        char mapKey[64] = {};
        Vector3 localPos = {};
        bool localIsDead = false;
        int localHealth = 0;
        int localArmor = 0;
        int localMoney = 0;
        float localYaw = 0.0f;
        uint16_t localWeaponId = 0;
        int localAmmoClip = -1;
        bool localHasBomb = false;
        bool localHasDefuser = false;
        uint16_t localGrenadeIds[PlayerData::kMaxGrenades] = {};
        int localGrenadeCount = 0;
        Vector3 minimapMins = {};
        Vector3 minimapMaxs = {};
        BombSnapshot bomb = {};
        int localTeam = 0;
        bool hasMinimapBounds = false;
        uint64_t captureTickMs = 0;
        static constexpr int kMaxWorldMarkers = 64;
        WebRadarWorldMarker worldMarkers[kMaxWorldMarkers] = {};
        int worldMarkerCount = 0;
    };

    struct TargetSnapshot {
        std::array<PlayerData, 64> players = {};
        char mapKey[64] = {};
        Vector3 localPos = {};
        Vector3 localEyePos = {};
        bool localEyeValid = false;
        Vector3 viewAngles = {};
        Vector3 localAimPunch = {};
        view_matrix_t viewMatrix = {};
        uintptr_t localPawn = 0;
        int localTeam = 0;
        int localPlayerIndex = -1;
        int crosshairPlayerIndex = -1;
        uintptr_t crosshairPawn = 0;
        bool crosshairValid = false;
        uint16_t localWeaponId = 0;
        uint32_t localWeaponHandle = 0;
        uintptr_t localWeaponEntity = 0;
        uintptr_t localWeaponVData = 0;
        int localAmmoClip = -1;
        bool localAmmoValid = false;
        int localShotsFired = 0;
        bool localShotsFiredValid = false;
        bool localAimPunchValid = false;
        bool localWeaponTelemetryValid = false;
        bool localIsReloading = false;
        bool localWeaponReady = false;
        bool localIsScoped = false;
        bool localOnGround = false;
        bool localIsWalking = false;
        bool localWeaponFullAuto = false;
        int localWeaponMode = 0;
        int localWeaponType = 0;
        int localWeaponBullets = 1;
        float localCurrentTime = 0.0f;
        float localIntervalPerTick = 0.0f;
        int localRenderTick = 0;
        float localCycleTime = 0.0f;
        float localLastShotTime = 0.0f;
        float localInaccuracy = 0.0f;
        float localInaccuracyWithoutAir = 0.0f;
        float localJumpInaccuracyInitial = 0.0f;
        float localJumpInaccuracyApex = 0.0f;
        float localVerticalVelocity = 0.0f;
        float localSpread = 0.0f;
        float localBaseInaccuracy = 0.0f;
        float localRecoilIndex = 0.0f;
        float localWeaponDamage = 0.0f;
        float localWeaponPenetration = 0.0f;
        float localWeaponRange = 0.0f;
        float localWeaponRangeModifier = 0.0f;
        float localWeaponArmorRatio = 0.0f;
        float localWeaponHeadshotMultiplier = 0.0f;
        float sensitivity = 1.0f;
        float fovSensitivityAdjust = 1.0f;
        uint64_t captureTimeUs = 0;
        uint64_t snapshotAgeUs = 0;
        uint64_t sampledAtUs = 0;
        uint64_t localEyeUpdatedAtUs = 0;
        uint64_t localAimPunchUpdatedAtUs = 0;
        uint64_t crosshairUpdatedAtUs = 0;
        uint64_t crosshairGeneration = 0;
        uint64_t viewUpdatedAtUs = 0;
        uint64_t localWeaponUpdatedAtUs = 0;
        uint64_t localWeaponTelemetryUpdatedAtUs = 0;
        uint64_t localAmmoUpdatedAtUs = 0;
        uint64_t localShotsUpdatedAtUs = 0;
        uint64_t sceneSerial = 0;
        bool localIsDead = false;
        bool viewValid = false;
    };
    void RequestCacheRefresh();

    bool UpdateData();

    void StartDataWorker();
    void StopDataWorker();
    bool ApplyDmaRuntimeCacheProfile(
        DmaCacheMode mode = DmaCacheMode::Maintenance,
        bool force = false);
    void SetAttachedCs2ProcessId(uint32_t processId);
    uint32_t GetAttachedCs2ProcessId();
    DmaHealthStats GetDmaHealthStats();
    DebugStats     GetDebugStats();

    void PublishDataSettingsSnapshot();
    void Draw();

    uint64_t    GetPublishCount();
    bool        GetWebRadarSnapshot(WebRadarSnapshot* outSnapshot);
    bool        GetTargetSnapshot(TargetSnapshot* outSnapshot);
}
