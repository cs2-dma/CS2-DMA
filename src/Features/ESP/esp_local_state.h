#pragma once
#include <Windows.h>
#include "Features/ESP/esp.h"
#include "Features/ESP/DataReader/world_marker_policy.h"
#include "Game/Schema/structs.h"
#include <atomic>
#include <chrono>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>
#include <array>
#include <bitset>
#include <algorithm>
#include <cmath>

namespace esp {

    // Constants
    constexpr uint32_t kEntitySlotMask = 0x1FFu;
    constexpr uint32_t kEntityHandleMask = 0x7FFFu;
    constexpr uint32_t kEntitySlotSize = 0x70u;
    constexpr uint32_t kEntitySlotSizeFallback = 0x78u;
    constexpr uint16_t kWeaponC4Id = 49u;
    constexpr int kMaxInventoryWeapons = 16;

    extern uint32_t s_activeEntitySlotSize;

    enum class WorldMarkerType : uint8_t {
        DroppedWeapon = 0,
        Smoke,
        Inferno,
        Decoy,
        Explosive,
        SmokeProjectile,
        MolotovProjectile,
        DecoyProjectile
    };

    struct WorldMarker {
        bool valid = false;
        WorldMarkerType type = WorldMarkerType::DroppedWeapon;
        Vector3 position = {};
        uint16_t weaponId = 0;
        float lifeHint = 0.0f;
        uint64_t expiresUs = 0;
        uintptr_t sourceEntity = 0;
        uint16_t sourceSlot = 0;
        uint64_t sourceSampleUs = 0;
    };

    struct BombState {
        bool planted = false;
        bool ticking = false;
        bool beingDefused = false;
        bool dropped = false;
        uint32_t sourceFlags = 0;
        uint8_t confidence = 0;
        Vector3 position = {};
        Vector3 velocity = {};
        uint64_t positionSampleTimeUs = 0;
        uint64_t positionGeneration = 0;
        uintptr_t positionEntity = 0;
        Vector3 boundsMins = {};
        Vector3 boundsMaxs = {};
        bool boundsValid = false;
        float blowTime = 0.0f;
        float timerLength = 0.0f;
        float defuseEndTime = 0.0f;
        float defuseLength = 0.0f;
        float currentGameTime = 0.0f;
    };

    struct SpectatorEntry {
        bool valid = false;
        char name[128] = {};
        char targetName[128] = {};
        bool targetIsLocal = false;
    };

    struct DataSettingsSnapshot {
        bool espSkeleton = false;
        bool espShowTeammates = false;
        bool espWeaponIconNoKnife = false;
        bool espBombInfo = false;
        bool radarShowBomb = false;
        bool espWeapon = false;
        bool espWeaponAmmo = false;
        bool espWeaponIcon = false;
        bool espName = false;
        bool radarSpectatorList = false;
        bool espFlags = false;
        bool espFlagMoney = false;
        bool espFlagScoped = false;
        bool espFlagDefusing = false;
        bool espFlagBlind = false;
        bool espFlagKit = false;
        bool radarEnabled = false;
        bool radarShowAngles = false;
        bool espVisibilityColoring = false;
        bool targetNeedsBones = false;
        bool targetNeedsVisibility = false;
        bool targetNeedsRecoil = false;
        bool targetNeedsVelocity = false;
        bool targetNeedsWeaponState = false;
        bool webRadarEnabled = false;
        bool webRadarRemoteEnabled = false;
        std::bitset<1200> espItemEnabledMask;
        bool espItem = false;
        bool espWorld = false;
        bool espWorldProjectiles = false;
    };

    enum class BombResolveKind : uint8_t {
        Hidden = 0,
        Carried,
        DroppedProbable,
        DroppedConfirmed,
        Planted,
    };

    enum BombResolveSourceFlags : uint32_t {
        BombResolveSourceRules        = 1u << 0,
        BombResolveSourceWeaponEntity = 1u << 1,
        BombResolveSourceWorldC4      = 1u << 2,
        BombResolveSourceCarrySignal  = 1u << 3,
        BombResolveSourceCarrySticky  = 1u << 4,
        BombResolveSourceAttachGrace  = 1u << 5,
        BombResolveSourceStickyDrop   = 1u << 6,
        BombResolveSourceStickyState  = 1u << 7,
        BombResolveSourceSpatialVeto  = 1u << 8,
        BombResolveSourcePositionFallback = 1u << 9,
        BombResolveSourcePickable     = 1u << 10,
        BombResolveSourceDropTickEdge = 1u << 11,
    };

    struct BombResolveResult {
        BombResolveKind kind = BombResolveKind::Hidden;
        uint32_t sourceFlags = 0;
        uint8_t confidence = 0;
        Vector3 position = { static_cast<float>(NAN), static_cast<float>(NAN), static_cast<float>(NAN) };
        Vector3 velocity = {};
        uint64_t positionSampleTimeUs = 0;
        Vector3 boundsMins = {};
        Vector3 boundsMaxs = {};
        bool boundsValid = false;
    };

    constexpr bool IsDroppedBombResolveKind(BombResolveKind kind)
    {
        return kind == BombResolveKind::DroppedConfirmed ||
               kind == BombResolveKind::DroppedProbable;
    }

    struct EntitySnapshot {
        esp::PlayerData players[64] = {};
        esp::PlayerData prevPlayers[64] = {};
        esp::PlayerData webRadarPlayers[64] = {};
        char       localName[128] = {};
        char       activeMapKey[64] = {};
        int        localPlayerIndex = -1;
        int        localTeam = 0;
        uintptr_t  localPawn = 0;
        Vector3    localPos = {};
        bool       localPosValid = false;
        uint64_t   localPosUpdatedAtUs = 0;
        Vector3    localViewOffset = {};
        bool       localViewOffsetValid = false;
        uint64_t   localViewOffsetUpdatedAtUs = 0;
        Vector3    localAimPunch = {};
        int        localShotsFired = 0;
        bool       localShotsFiredValid = false;
        bool       localAimPunchValid = false;
        uint64_t   localAimPunchUpdatedAtUs = 0;
        Vector3    prevLocalPos = {};
        bool       localIsDead = false;
        int        localHealth = 0;
        int        localArmor = 0;
        int        localMoney = 0;
        uint16_t   localWeaponId = 0;
        uint32_t   localWeaponHandle = 0;
        uintptr_t  localWeaponEntity = 0;
        int        localAmmoClip = -1;
        bool       localAmmoValid = false;
        uint64_t   localAmmoUpdatedAtUs = 0;
        uint64_t   localWeaponUpdatedAtUs = 0;
        WeaponTelemetry localWeaponTelemetry = {};
        uint64_t   localShotsUpdatedAtUs = 0;
        bool       localHasBomb = false;
        bool       localHasDefuser = false;
        uint16_t   localGrenadeIds[esp::PlayerData::kMaxGrenades] = {};
        int        localGrenadeCount = 0;
        Vector3    viewAngles = {};
        view_matrix_t viewMatrix = {};
        uint64_t viewMatrixUpdatedAtUs = 0;
        float      sensitivity = 1.0f;
        float      fovSensitivityAdjust = 1.0f;
        Vector3    minimapMins = {};
        Vector3    minimapMaxs = {};
        bool       hasMinimapBounds = false;
        bool       localMaskResolved = false;
        uint64_t   captureTimeUs = 0;
        uint64_t   prevCaptureTimeUs = 0;
        uint64_t   playerCoreGeneration = 0;
        uint64_t   playerCoreCaptureTimeUs = 0;
        uint64_t   prevPlayerCoreCaptureTimeUs = 0;
        uint8_t    playerCoreBatchQuality = 0;
        uint64_t   sceneSerial = 0;
        WorldMarker worldMarkers[256] = {};
        int        worldMarkerCount = 0;
        BombState  bombState = {};
        SpectatorEntry spectators[64] = {};
        int spectatorCount = 0;
    };

    struct SnapshotSlot {
        mutable std::shared_mutex mutex;
        EntitySnapshot data = {};
    };

    struct PlayerVisibilitySample {
        uintptr_t pawn = 0;
        bool visible = false;
    };

    struct PlayerVisibilityFrame {
        PlayerVisibilitySample players[64] = {};
        int crosshairPlayerIndex = -1;
        bool crosshairValid = false;
        uint64_t captureTimeUs = 0;
        uint64_t crosshairUpdatedAtUs = 0;
        uint64_t generation = 0;
        uint64_t sceneSerial = 0;
    };

    struct PlayerVisibilityFrameSlot {
        mutable std::shared_mutex mutex;
        PlayerVisibilityFrame data = {};
    };

    struct CameraFrame {
        view_matrix_t viewMatrix = {};
        Vector3 viewAngles = {};
        Vector3 localPos = {};
        uintptr_t localPosPawn = 0;
        bool viewValid = false;
        bool viewAnglesValid = false;
        bool localPosValid = false;
        uint64_t viewUpdatedUs = 0;
        uint64_t viewAnglesUpdatedUs = 0;
        uint64_t localPosUpdatedUs = 0;
        uint64_t sceneSerial = 0;
    };

    struct CameraFrameSlot {
        mutable std::shared_mutex mutex;
        CameraFrame data = {};
    };

    struct ActiveMapStateSnapshot {
        std::string key;
        float baseOffsetX = 0.0f;
        float baseOffsetY = 0.0f;
        bool overviewAvailable = false;
        float overviewPosX = 0.0f;
        float overviewPosY = 0.0f;
        float overviewScale = 0.0f;
    };

    struct BonePair { int from, to; };

    enum class RuntimeSubsystem : uint8_t {
        PlayersCore = 0,
        CameraView,
        GameRulesMap,
        Bones,
        World,
        Count,
    };

    enum class EspEventType : uint8_t {
        InvalidPositionFallback = 0,
        DeathHold,
        SnapshotFallback,
        WebRadarPlayerHold,
        SlotEvictedStale,
        SlotEvictedFallback,
        SlotEvictedHierarchy,
        SlotEvictedMissing,
        SlotEvictedLocal,
        SlotEvictedDead,
        BulkRecoveryEntered,  
        BoneRejected,
    };

    struct EspEventDescriptor
    {
        EspEventType type = EspEventType::InvalidPositionFallback;
        uint8_t slot = 0xFF;
        uint16_t param = 0;
    };

    struct DmaEventDescriptor
    {
        const char* action = "unknown";
        const char* reason = "unspecified";
    };

    struct EspEventRecord {
        uint64_t timeUs = 0;
        uint8_t type = 0;
        uint8_t slot = 0xFF;
        uint16_t param = 0;
    };

    struct DmaEventRecord {
        uint64_t timeUs = 0;
        char action[24] = {};
        char reason[96] = {};
    };

    enum class LocalPlayerIndexSource : uint8_t
    {
        None = 0,
        PawnMatch,
        ControllerMatch,
        EngineFallback,
    };

    struct LocalPlayerIndexHints
    {
        int controllerMaskBit = -1;
        int pawnMaskBit = -1;
    };

    struct SharedLocalIdentitySlotData
    {
        const char (*names)[128] = nullptr;
        const int* healths = nullptr;
        const uint8_t* lifeStates = nullptr;
        const int* armors = nullptr;
        const int* moneys = nullptr;
        const uint8_t* hasDefuserFlags = nullptr;
    };

    struct SharedLocalIdentity
    {
        char name[128] = {};
        bool isDead = false;
        int health = 0;
        int armor = 0;
        int money = 0;
        bool hasDefuser = false;
    };

    struct SnapshotPlayerIdentity
    {
        int slotIndex = -1;
        uintptr_t pawn = 0;
    };

    // Extern Constants & Variables
    extern const BonePair skeletonPairs[];
    extern const int skeletonPairCount;
    inline constexpr int DATA_WORKER_HZ = kDataWorkerLiveHz;
    // The overlay runs at up to 240 FPS. A phase-shifted 300 Hz camera lane
    // supplies a fresh view per frame while the 250 Hz data lane leaves a
    // bounded slot for scheduled inventory/world work.
    inline constexpr int CAMERA_WORKER_HZ = kCameraWorkerLiveHz;
    inline constexpr uint64_t kPlayerStaleEvictionMs = 1000;
    inline constexpr uint64_t kLocalPawnFarewellWindowUs = 5000000;
    inline constexpr int kMaxTrackedWorldEntities = 8191;
    inline constexpr int kMaxTrackedWorldBlocks = (kMaxTrackedWorldEntities >> 9) + 1;
    inline constexpr int kTrackedWorldSubclassSlots = 8;
    inline constexpr uint64_t kLiveCameraFreshnessUs = 125000;
    inline constexpr uint32_t kCameraInvalidateMissThreshold = 40;
    inline constexpr uint32_t kCameraRecoveryMissThreshold = 180;
    inline constexpr uint64_t kDataWorkerStallUs = 250000;
    inline constexpr uint32_t RECOVERY_FAILURE_THRESHOLD = 120;

    // Shared State Variables
    extern SnapshotSlot s_snapshotSlots[8];
    extern std::atomic<int> s_publishedSnapshotIdx;
    extern int s_nextSnapshotWriteIdx;
    extern PlayerVisibilityFrameSlot s_visibilityFrameSlots[4];
    extern std::atomic<int> s_publishedVisibilityFrameIdx;
    extern int s_nextVisibilityFrameWriteIdx;

    extern esp::PlayerData s_players[64];
    extern esp::PlayerData s_prevPlayers[64];
    extern esp::PlayerData s_webRadarPlayers[64];
    extern char       s_localName[128];
    extern int        s_localPlayerIndex;
    extern int        s_localTeam;
    extern uintptr_t  s_localPawn;
    extern Vector3    s_prevLocalPos;
    extern view_matrix_t s_viewMatrix;
    extern uint64_t s_viewMatrixUpdatedAtUs;
    extern Vector3    s_localPos;
    extern bool       s_localPosValid;
    extern uint64_t   s_localPosUpdatedAtUs;
    extern Vector3    s_localViewOffset;
    extern bool       s_localViewOffsetValid;
    extern uint64_t   s_localViewOffsetUpdatedAtUs;
    extern Vector3    s_localAimPunch;
    extern int        s_localShotsFired;
    extern bool       s_localShotsFiredValid;
    extern uint64_t   s_localShotsUpdatedAtUs;
    extern bool       s_localAimPunchValid;
    extern uint64_t   s_localAimPunchUpdatedAtUs;
    extern bool       s_localIsDead;
    extern int        s_localHealth;
    extern int        s_localArmor;
    extern int        s_localMoney;
    extern uint16_t   s_localWeaponId;
    extern uint32_t   s_localWeaponHandle;
    extern uintptr_t  s_localWeaponEntity;
    extern int        s_localAmmoClip;
    extern bool       s_localAmmoValid;
    extern uint64_t   s_localAmmoUpdatedAtUs;
    extern uint64_t   s_localWeaponUpdatedAtUs;
    extern WeaponTelemetry s_localWeaponTelemetry;
    extern bool       s_localHasBomb;
    extern bool       s_localHasDefuser;
    extern uint16_t   s_localGrenadeIds[esp::PlayerData::kMaxGrenades];
    extern int        s_localGrenadeCount;
    extern Vector3    s_viewAngles;
    extern float      s_sensitivity;
    extern float      s_fovSensitivityAdjust;
    extern Vector3    s_minimapMins;
    extern Vector3    s_minimapMaxs;
    extern bool       s_hasMinimapBounds;
    extern bool       s_localMaskResolved;
    extern uint64_t   s_captureTimeUs;
    extern uint64_t   s_prevCaptureTimeUs;
    extern std::atomic<uint64_t> s_playerCoreGeneration;
    extern std::atomic<uint64_t> s_playerCoreCaptureTimeUs;
    extern std::atomic<uint64_t> s_prevPlayerCoreCaptureTimeUs;
    extern std::atomic<uint8_t>  s_playerCoreBatchQuality;
    extern std::atomic<uint64_t> s_playerCoreIncompleteMask;
    extern std::atomic<uint64_t> s_playerCoreInvalidMask;
    extern std::atomic<uint64_t> s_playerCoreBatchHoldCount;
    extern uint64_t   s_playerLastSeenMs[64];
    extern uint8_t    s_playerInvalidReadStreak[64];
    extern uint8_t    s_playerDeathConfirmCount[64];

    extern std::mutex s_dataMutex;
    extern std::atomic<uint64_t> s_sceneResetSerial;
    extern std::atomic<uint64_t> s_lastSceneResetUs;

    extern std::atomic<uint64_t> s_lastBulkEvictionUs;
    extern std::atomic<uint8_t> s_sceneWarmupState;
    extern std::atomic<uint64_t> s_sceneWarmupEnteredUs;
    extern std::atomic<uint8_t> s_lastRuntimeResetKind;
    extern std::atomic<bool> s_lastRuntimeResetPublishedClearedSnapshot;
    extern std::atomic<uint64_t> s_lastRuntimeResetUs;
    extern std::atomic<uint64_t> s_mapEpoch;
    extern std::atomic<uint64_t> s_bombEpoch;
    extern uint64_t s_mapFingerprint;

    extern CameraFrameSlot s_cameraFrameSlots[4];
    extern std::atomic<int> s_publishedCameraFrameIdx;
    extern int s_nextCameraFrameWriteIdx;

    extern WorldMarker s_worldMarkers[256];
    extern int s_worldMarkerCount;
    extern BombState s_bombState;
    extern SpectatorEntry s_spectators[64];
    extern int s_spectatorCount;
    extern uintptr_t s_localPawnFarewellPtr;
    extern uint32_t s_localPawnFarewellHandle;
    extern uint64_t s_localPawnFarewellExpiryUs;
    extern uint32_t s_localPawnHandleLastSeen;
    extern uint64_t s_lastWorldScanUs;

    extern uintptr_t s_worldEntityRefs[8192];
    extern uint32_t s_worldEntitySubclassIds[8192];
    extern uint16_t s_worldEntityItemIds[8192];
    extern uint8_t s_worldEntityClassKinds[8192];
    extern int s_worldTrackedIndices[8192];
    extern uint16_t s_worldTrackedIndexPos[8192];
    extern int s_worldTrackedIndexCount;
    extern uint32_t s_worldSmokeSubclassIds[8];
    extern uint32_t s_worldMolotovSubclassIds[8];
    extern uint32_t s_worldDecoySubclassIds[8];
    extern uint32_t s_worldHeSubclassIds[8];
    extern uint32_t s_worldInfernoSubclassIds[8];
    extern bool s_worldSmokeLatched[8192];
    extern bool s_worldInfernoLatched[8192];
    extern bool s_worldDecoyLatched[8192];
    extern bool s_worldExplosiveLatched[8192];
    extern bool s_worldUtilityHasHistory[8192];
    extern uint8_t s_worldSmokeEvidenceCount[8192];
    extern uint8_t s_worldInfernoEvidenceCount[8192];
    extern uint8_t s_worldDecoyEvidenceCount[8192];
    extern uint8_t s_worldExplosiveEvidenceCount[8192];
    extern uint64_t s_worldSmokeStartUs[8192];
    extern uint64_t s_worldInfernoStartUs[8192];
    extern uint64_t s_worldDecoyStartUs[8192];
    extern uint64_t s_worldExplosiveStartUs[8192];
    extern uint64_t s_worldUtilityDeadlinesUs[8192][4];
    extern Vector3 s_worldPrevPos[8192];
    extern int s_worldPrevSmokeTick[8192];
    extern uint8_t s_worldPrevSmokeActive[8192];
    extern uint8_t s_worldPrevSmokeVolumeDataReceived[8192];
    extern uint8_t s_worldPrevSmokeEffectSpawned[8192];
    extern int s_worldPrevInfernoTick[8192];
    extern float s_worldPrevInfernoLife[8192];
    extern int s_worldPrevInfernoFireCount[8192];
    extern uint8_t s_worldPrevInfernoInPostEffect[8192];
    extern int s_worldPrevDecoyTick[8192];
    extern int s_worldPrevDecoyClientTick[8192];
    extern int s_worldPrevExplodeTick[8192];
    extern Vector3 s_worldPrevVelocity[8192];
    extern esp::data::UtilityFieldTimes s_worldUtilityFieldTimes[8192];
    extern uint64_t s_worldUtilityPositionSampleUs[8192];
    extern uint64_t s_worldUtilityStationarySinceUs[8192];
    extern uint8_t s_worldUtilityStationarySamples[8192];

    inline void ResetWorldUtilityTrackingSlot(int idx) noexcept
    {
        if (idx < 0 || idx > kMaxTrackedWorldEntities)
            return;
        s_worldSmokeLatched[idx] = false;
        s_worldInfernoLatched[idx] = false;
        s_worldDecoyLatched[idx] = false;
        s_worldExplosiveLatched[idx] = false;
        s_worldUtilityHasHistory[idx] = false;
        s_worldSmokeStartUs[idx] = 0;
        s_worldInfernoStartUs[idx] = 0;
        s_worldDecoyStartUs[idx] = 0;
        s_worldExplosiveStartUs[idx] = 0;
        std::fill(std::begin(s_worldUtilityDeadlinesUs[idx]), std::end(s_worldUtilityDeadlinesUs[idx]), 0u);
        s_worldSmokeEvidenceCount[idx] = 0;
        s_worldInfernoEvidenceCount[idx] = 0;
        s_worldDecoyEvidenceCount[idx] = 0;
        s_worldExplosiveEvidenceCount[idx] = 0;
        s_worldPrevPos[idx] = {};
        s_worldPrevSmokeTick[idx] = 0;
        s_worldPrevSmokeActive[idx] = 0;
        s_worldPrevSmokeVolumeDataReceived[idx] = 0;
        s_worldPrevSmokeEffectSpawned[idx] = 0;
        s_worldPrevInfernoTick[idx] = 0;
        s_worldPrevInfernoLife[idx] = 0.0f;
        s_worldPrevInfernoFireCount[idx] = 0;
        s_worldPrevInfernoInPostEffect[idx] = 0;
        s_worldPrevDecoyTick[idx] = 0;
        s_worldPrevDecoyClientTick[idx] = 0;
        s_worldPrevExplodeTick[idx] = 0;
        s_worldPrevVelocity[idx] = {};
        s_worldUtilityFieldTimes[idx] = {};
        s_worldUtilityPositionSampleUs[idx] = 0;
        s_worldUtilityStationarySinceUs[idx] = 0;
        s_worldUtilityStationarySamples[idx] = 0;
    }

    extern float s_lastStableIntervalPerTick;
    extern float s_lastStableGameTime;
    extern uint64_t s_lastStableGameTimeUs;

    extern std::jthread s_dataWorker;
    extern std::jthread s_cameraWorker;
    extern std::atomic<bool> s_dataWorkerRunning;
    extern std::atomic<bool> s_cameraWorkerRunning;
    extern std::atomic<bool> s_dataWorkerStopRequested;

    extern std::atomic<bool> s_dmaRecovering;
    extern std::atomic<bool> s_dmaBackgroundRefreshEnabled;
    extern std::atomic<bool> s_dmaManualRefreshInProgress;
    extern std::atomic<uint64_t> s_dmaManualRefreshLastUs;
    extern std::atomic<uint64_t> s_dmaManualRefreshPeakUs;
    extern std::atomic<uint64_t> s_dmaManualRefreshCount;
    extern std::atomic<uint64_t> s_dmaManualRefreshRecentPeakUs;
    extern std::atomic<uint64_t> s_dmaManualRefreshRecentCount;
    extern std::atomic<uint64_t> s_dmaManualRefreshQueuedCount;
    extern std::atomic<uint64_t> s_dmaManualRefreshQueuedRecentCount;
    extern std::atomic<uint64_t> s_dmaManualRefreshSuppressedCount;
    extern std::atomic<uint64_t> s_dmaManualRefreshSuppressedRecentCount;
    extern std::atomic<uint64_t> s_dmaManualRefreshAvoidedCount;
    extern std::atomic<uint64_t> s_dmaManualRefreshAvoidedRecentCount;
    extern std::atomic<uint64_t> s_dmaManualRefreshCoalescedCount;
    extern std::atomic<uint32_t> s_cameraWorkerPauseRequests;
    extern std::atomic<bool> s_dmaAdminPauseActive;
    extern std::atomic<bool> s_cameraWorkerPaused;
    extern std::atomic<uint64_t> s_cameraWorkerPauseOrphanRecoveryCount;
    extern std::atomic<uint64_t> s_cameraWorkerPauseReleaseImbalanceCount;
    extern std::shared_timed_mutex s_dmaLifecycleMutex;
    extern std::atomic<uint64_t> s_dmaSessionGeneration;
    extern std::atomic<uint64_t> s_scatterHandleCreateTotalUs;
    extern std::atomic<uint64_t> s_scatterHandleCreatePeakUs;
    extern std::atomic<uint64_t> s_scatterHandleCreateCount;
    extern std::atomic<uint64_t> s_scatterHandleCloseTotalUs;
    extern std::atomic<uint64_t> s_scatterHandleClosePeakUs;
    extern std::atomic<uint64_t> s_scatterHandleCloseCount;
    extern std::atomic<bool> s_dmaRecoveryRequested;
    extern std::atomic<uint64_t> s_dmaRecoveryRequestedAtUs;
    extern std::atomic<uint32_t> s_dmaConsecutiveFailures;
    extern std::atomic<uint64_t> s_dmaTotalFailures;
    extern std::atomic<uint64_t> s_dmaTotalSuccesses;
    extern std::atomic<uint64_t> s_dmaTotalRecoveries;
    extern std::atomic<uint64_t> s_dmaLastSuccessTick;

    extern std::mutex s_dmaEventMutex;
    extern DmaEventRecord s_dmaEvents[esp::DmaHealthStats::kMaxEvents];
    extern uint32_t s_dmaEventWriteIndex;
    extern uint32_t s_dmaEventCount;
    extern std::atomic<uint64_t> s_publishCount;
    extern std::atomic<uint64_t> s_publishDropCount;
    extern std::atomic<uint64_t> s_visibilityPublishDropCount;
    extern std::atomic<uint64_t> s_cameraPublishDropCount;
    extern std::atomic<uint64_t> s_settingsSnapshotReuseCount;
    extern std::atomic<uint64_t> s_lastPublishUs;
    extern std::atomic<uint64_t> s_sessionStartUs;
    extern std::atomic<uint64_t> s_dataWorkerCycleUs;
    extern std::atomic<int> s_dataWorkerTargetHz;
    extern std::atomic<int> s_cameraWorkerTargetHz;
    extern std::atomic<bool> s_cameraReadsEnabled;
    void UpdateReadQualityTelemetry(uint64_t nowUs);
    extern std::atomic<uint64_t> s_dataWorkerMaxCycleUs;
    extern std::atomic<uint64_t> s_dataWorkerRecentMaxCycleUs;
    extern std::atomic<uint64_t> s_dataWorkerRecentWindowStartUs;
    extern std::atomic<uint64_t> s_dataWorkerCycleP50Us;
    extern std::atomic<uint64_t> s_dataWorkerCycleP95Us;
    extern std::atomic<uint64_t> s_dataWorkerCycleP99Us;
    extern std::atomic<uint64_t> s_dataWorkerDeadlineMissCount;
    extern std::atomic<uint64_t> s_dataWorkerCycleSampleCount;
    extern std::atomic<uint64_t> s_dataWorkerCycleOverBudgetCount;
    extern std::atomic<uint64_t> s_dataWorkerCycleOver5msCount;
    extern std::atomic<uint64_t> s_dataWorkerCycleOver16msCount;
    extern std::atomic<uint64_t> s_dataWorkerLastLoopStartUs;
    extern std::atomic<uint64_t> s_dataWorkerLastLoopEndUs;
    extern std::atomic<uint64_t> s_dataWorkerInFlightSinceUs;
    extern std::atomic<bool> s_dataWorkerUpdateInFlight;
    extern std::atomic<uint64_t> s_cameraWorkerCycleUs;
    extern std::atomic<uint64_t> s_cameraWorkerMaxCycleUs;
    extern std::atomic<uint64_t> s_cameraWorkerRecentMaxCycleUs;
    extern std::atomic<uint64_t> s_cameraWorkerRecentWindowStartUs;
    extern std::atomic<uint64_t> s_cameraWorkerCycleP50Us;
    extern std::atomic<uint64_t> s_cameraWorkerCycleP95Us;
    extern std::atomic<uint64_t> s_cameraWorkerCycleP99Us;
    extern std::atomic<uint64_t> s_cameraWorkerDeadlineMissCount;
    extern std::atomic<uint64_t> s_cameraWorkerCycleSampleCount;
    extern std::atomic<uint64_t> s_cameraWorkerCycleOverBudgetCount;
    extern std::atomic<uint64_t> s_cameraWorkerCycleOver5msCount;
    extern std::atomic<uint64_t> s_cameraWorkerCycleOver16msCount;
    extern std::atomic<uint64_t> s_playerCoreAnomalyCount;
    extern std::atomic<uint64_t> s_playerCoreRecoveredCount;
    extern std::atomic<uint64_t> s_playerCoreGlobalRefreshAvoidedCount;
    extern std::atomic<uint64_t> s_playerUnexpectedEvictionCount;
    extern std::atomic<uint64_t> s_playerExpectedEvictionCount;
    extern std::atomic<int32_t>  s_playerControllerSlotCountStat;
    extern std::atomic<int32_t>  s_playerResolvedSlotCountStat;
    extern std::atomic<int32_t>  s_playerPlausibleCoreSlotCountStat;
    extern std::atomic<int32_t>  s_playerDuplicateIdentityFilteredStat;
    extern std::atomic<int32_t>  s_playerBacklinkMismatchStat;
    extern std::atomic<int32_t>  s_playerHierarchyHeldSlotCount;
    extern std::atomic<int32_t>  s_playerZeroPawnHeldSlotCount;
    extern std::atomic<int32_t>  s_playerCoreHeldSlotCount;
    extern std::atomic<int32_t>  s_activePlayerCount;
    extern std::atomic<int32_t>  s_playerSlotScanLimitStat;
    extern std::atomic<int32_t>  s_playerHierarchyHighWaterSlot;
    extern std::atomic<int32_t>  s_highestEntityIdxStat;
    extern std::atomic<uint32_t> s_entitySlotStrideStat;
    extern std::atomic<int32_t>  s_worldMarkerCountStat;
    extern std::atomic<int32_t>  s_worldTrackedEntityCountStat;
    extern std::atomic<int32_t>  s_worldCandidateCountStat;
    extern std::atomic<int32_t>  s_worldUtilityCandidateCountStat;
    extern std::atomic<int32_t>  s_worldClassifiedCandidateCountStat;
    extern std::atomic<int32_t>  s_worldIdentityPendingCountStat;
    extern std::atomic<uint64_t> s_worldMarkerCapacityDropsStat;
    extern std::atomic<uint64_t> s_worldMarkerReadGapHoldsStat;
    extern std::atomic<uint64_t> s_worldPositionReadMissesStat;
    extern std::atomic<int32_t>  s_visibilityFreshSlots;
    extern std::atomic<int32_t>  s_visibilityVisibleSlots;
    extern std::atomic<int32_t>  s_visibilityMaskSlots;
    extern std::atomic<int32_t>  s_visibilityCrosshairSlot;
    extern std::atomic<uint64_t> s_visibilityLastCommitUs;
    extern std::atomic<bool>     s_visibilityCrosshairValid;
    extern std::atomic<bool>     s_visibilityEnabled;
    extern std::atomic<bool>     s_visibilityLocalMaskResolved;
    extern std::atomic<uint64_t> s_lastWorldScanCommittedUs;
    extern std::atomic<uint32_t> s_bombDebugFlags;
    extern std::atomic<uint32_t> s_bombDebugSourceFlags;
    extern std::atomic<uint64_t> s_bombDebugPositionSampleUs;
    extern std::atomic<uint64_t> s_bombDropPublicationDebug;
    extern std::atomic<uint32_t> s_bombDebugRawFlags;
    extern std::atomic<uint8_t>  s_bombDebugConfidence;
    extern std::atomic<int32_t>  s_bombDebugDefuserSlot;
    extern std::atomic<int32_t>  s_bombDebugBlowLeftMs;
    extern std::atomic<int32_t>  s_bombDebugDefuseLeftMs;

    extern std::atomic<uint64_t> s_stageTimingSequence;
    extern std::atomic<uint64_t> s_stageEngineUs;
    extern std::atomic<uint64_t> s_stageBaseReadsUs;
    extern std::atomic<uint64_t> s_stagePlayerReadsUs;
    extern std::atomic<uint64_t> s_stagePlayerHierarchyUs;
    extern std::atomic<uint64_t> s_stagePlayerCoreUs;
    extern std::atomic<uint64_t> s_stagePlayerRepairUs;
    extern std::atomic<uint64_t> s_stageCommitStateUs;
    extern std::atomic<uint64_t> s_stagePlayerAuxUs;
    extern std::atomic<uint64_t> s_stageInventoryUs;
    extern std::atomic<uint64_t> s_stageBoneReadsUs;
    extern std::atomic<uint64_t> s_stageBombScanUs;
    extern std::atomic<uint64_t> s_stageWorldScanUs;
    extern std::atomic<uint64_t> s_stageWorldScanLastUs;
    extern std::atomic<uint64_t> s_stageCommitEnrichUs;
    extern std::atomic<uint64_t> s_stagePlayerAuxLastUs;
    extern std::atomic<uint64_t> s_stageInventoryLastUs;
    extern std::atomic<uint64_t> s_stageBoneReadsLastUs;
    extern std::atomic<uint64_t> s_stagePlayerAuxLastAtUs;
    extern std::atomic<uint64_t> s_stageInventoryLastAtUs;
    extern std::atomic<uint64_t> s_stageBoneReadsLastAtUs;
    extern std::atomic<uint32_t> s_stageBonePoseSlots;
    extern std::atomic<uint32_t> s_stageBonePointerValidationSlots;
    extern std::atomic<uint32_t> s_stageBonePoseRanges;
    extern std::atomic<uint32_t> s_stageBonePoseBytes;
    extern std::atomic<uint8_t> s_gameStatus;
    extern std::atomic<bool> s_engineStatusResolved;
    extern std::atomic<int32_t> s_engineSignOnState;
    extern std::atomic<int32_t> s_engineLocalPlayerSlot;
    extern std::atomic<int32_t> s_engineMaxClients;
    extern std::atomic<bool> s_engineBackgroundMap;
    extern std::atomic<bool> s_engineMenu;
    extern std::atomic<bool> s_engineInGame;

    extern std::atomic<uint8_t> s_subsystemStates[5];
    extern std::atomic<uint32_t> s_subsystemFailureStreaks[5];
    extern std::atomic<uint64_t> s_subsystemLastGoodUs[5];

    extern EspEventRecord s_espEventRing[4096];
    extern std::atomic<uint32_t> s_espEventWriteIndex;
    extern std::atomic<uint32_t> s_espEventCount;

    extern std::string s_activeMapKey;
    extern std::atomic<uint64_t> s_lastLiveMapNameSeenUs;
    extern float s_lastSavedMapRotation;
    extern float s_lastSavedMapScale;
    extern float s_lastSavedMapOffsetX;
    extern float s_lastSavedMapOffsetY;
    extern float s_activeMapBaseOffsetX;
    extern float s_activeMapBaseOffsetY;
    extern bool s_activeMapOverviewAvailable;
    extern float s_activeMapOverviewPosX;
    extern float s_activeMapOverviewPosY;
    extern float s_activeMapOverviewScale;

    extern std::mutex s_activeMapMutex;

    extern std::atomic<uint32_t> s_pendingRefreshFlags;
    extern std::atomic<bool> s_dmaAdminThreadStarted;
    extern std::jthread s_dmaAdminThread;

    extern uint32_t s_requiredReadFailureCount;

    // Helper Functions from state.inl
    bool IsLocalSnapshotSlot(
        const EntitySnapshot& snapshot,
        SnapshotPlayerIdentity player) noexcept;
    void AdvanceSnapshotWriteIndex(int usedIdx);
    bool ReadCurrentSnapshot(EntitySnapshot& out);
    void PublishCurrentSnapshot();
    bool ReadPlayerVisibilityFrame(PlayerVisibilityFrame& out);
    void PublishPlayerVisibilityFrame();
    void ResetPlayerVisibilityFrame();
    bool ReadCameraFrame(CameraFrame& out);
    void PublishCameraFrame(const CameraFrame& frame);
    void ResetCameraSnapshot();
    void PublishDataSettingsSnapshot();
    bool TryReadDataSettingsSnapshot(DataSettingsSnapshot& out);
    void ResetWorldUtilityTrackingState();
    void ResetRuntimeStateHard(const char* reason, bool publishClearedSnapshot = false);
    void ResetRuntimeStateSoft(const char* reason);
    int ResolveLocalPlayerIndex(LocalPlayerIndexHints hints);
    LocalPlayerIndexSource ResolveLocalPlayerIndexSource(LocalPlayerIndexHints hints);
    bool IsLiveLocalPlayerIndexSource(LocalPlayerIndexSource source);
    bool IsValidLocalPlayerIndex(int localPlayerIndex);
    void ResolveSharedLocalIdentityFromSlot(
        int localPlayerIndex,
        const SharedLocalIdentitySlotData& slots,
        SharedLocalIdentity& identity);
    void ApplySharedLocalIdentityState(const SharedLocalIdentity& identity);
    void RequestDmaRecovery(const char* reason);
    bool ProcessPendingCacheRefreshRequest();
    void DmaAdminThreadFn(const std::stop_token& stopToken) noexcept;
    void EnsureDmaAdminThread();
    void StopDmaAdminThread();
    void AcquireCameraWorkerPauseRequest();
    void ReleaseCameraWorkerPauseRequest();
    bool RecoverOrphanedCameraWorkerPauseRequest();
    void RefreshDmaCaches(
        const char* reason,
        DmaRefreshTier tier = DmaRefreshTier::Probe,
        bool force = false,
        DmaRefreshTrigger trigger = DmaRefreshTrigger::General);
    bool IsDmaRecoveryRequested();
    void ClearDmaRecoveryRequest();
    void MarkDmaReadFailure();
    void MarkDmaReadDegraded();
    void MarkDmaReadSuccess();
    void DataWorkerLoop(std::stop_token stopToken);
    void CameraWorkerLoop(std::stop_token stopToken);

    void RecordEspEvent(EspEventDescriptor event);
    void RecordDmaEvent(DmaEventDescriptor event);
    void SetSubsystemUnknown(RuntimeSubsystem subsystem);
    void MarkSubsystemHealthy(RuntimeSubsystem subsystem, uint64_t nowUs = 0);
    void MarkSubsystemDegraded(RuntimeSubsystem subsystem, uint64_t nowUs = 0);
    void MarkSubsystemFailed(RuntimeSubsystem subsystem, uint64_t nowUs = 0);
    esp::SubsystemHealthInfo GetSubsystemHealthInfo(RuntimeSubsystem subsystem, uint64_t nowUs);
    void SetSceneWarmupState(esp::SceneWarmupState state, uint64_t nowUs = 0);
    void BumpSceneReset(uint64_t nowUs = 0);
    ActiveMapStateSnapshot CopyActiveMapState();
    void ResetActiveMapState();

    uint64_t TickNowUs();
    uint64_t TickNowMs();
    uint64_t SubsystemNowUs();
    const char* EspEventTypeName(EspEventType type);

    // Utility from basic_helpers.inl & others
    bool IsLikelyViewMatrix(const view_matrix_t& matrix);
}
