#include "Features/ESP/DataReader/intervals.h"
#include "Features/ESP/DataReader/deferred_lane_policy.h"

// Large per-tick arrays live in thread-local scratch storage. The remaining
// analyzer estimate is small relative to the configured 8 MiB stack reserve.
#pragma warning(suppress: 6262)
bool esp::PlayerDataReader::UpdateData()
{
    static thread_local esp::data::BonePoseAnchor bonePoseAnchors[64] = {};
    if (s_dmaAdminPauseActive.load(std::memory_order_acquire))
        return false;

    std::shared_lock<std::shared_timed_mutex> dmaSessionLock(s_dmaLifecycleMutex);
    if (s_dmaAdminPauseActive.load(std::memory_order_acquire))
        return false;

    (void)app::input::PollPrimaryKeyboard();

    struct ReaderHandlesExceptionGuard {
        int uncaughtAtEntry = std::uncaught_exceptions();

        ~ReaderHandlesExceptionGuard()
        {
            if (std::uncaught_exceptions() > uncaughtAtEntry)
                CloseReaderScatterHandles(mem);
        }
    } readerHandlesExceptionGuard;

    struct ScopedDirectReadWarningSuppression {
        Memory& memory;

        explicit ScopedDirectReadWarningSuppression(Memory& memRef) : memory(memRef)
        {
            memory.SetDirectReadWarningSuppressed(true);
        }

        ~ScopedDirectReadWarningSuppression()
        {
            memory.SetDirectReadWarningSuppressed(false);
        }
    } directReadWarningScope(mem);

    struct ScopedScatterHandleCloser {
        Memory& memory;
        VMMDLL_SCATTER_HANDLE& scatterHandle;
        bool closeOnExit = true;

        ScopedScatterHandleCloser(Memory& memRef, VMMDLL_SCATTER_HANDLE& handleRef)
            : memory(memRef), scatterHandle(handleRef)
        {
        }

        ~ScopedScatterHandleCloser()
        {
            if (closeOnExit)
                Close();
        }

        void Close()
        {
            if (scatterHandle) {
                CloseTrackedScatterHandle(memory, scatterHandle);
                scatterHandle = nullptr;
            }
        }

        void KeepOpen()
        {
            closeOnExit = false;
        }
    };

    static thread_local DataSettingsSnapshot settingsSnapshot;
    static thread_local bool settingsSnapshotInitialized = false;
    bool settingsSnapshotRead =
        TryReadDataSettingsSnapshot(settingsSnapshot);
    if (!settingsSnapshotRead && !settingsSnapshotInitialized) {
        PublishDataSettingsSnapshot();
        settingsSnapshotRead =
            TryReadDataSettingsSnapshot(settingsSnapshot);
    }
    if (settingsSnapshotRead)
        settingsSnapshotInitialized = true;
    else
        s_settingsSnapshotReuseCount.fetch_add(1, std::memory_order_relaxed);

    const bool wantsEspSkeleton =
        settingsSnapshot.espSkeleton || settingsSnapshot.targetNeedsBones;
    const bool wantsEspShowTeammates = settingsSnapshot.espShowTeammates;
    const bool wantsEspWeaponIconNoKnife = settingsSnapshot.espWeaponIconNoKnife;
    const bool wantsEspBombInfo = settingsSnapshot.espBombInfo;
    const bool wantsRadarShowBomb = settingsSnapshot.radarShowBomb;
    const bool wantsEspWeapon = settingsSnapshot.espWeapon;
    const bool wantsEspWeaponAmmo = settingsSnapshot.espWeaponAmmo;
    const bool wantsEspWeaponIcon = settingsSnapshot.espWeaponIcon;
    const bool wantsEspName = settingsSnapshot.espName;
    const bool wantsRadarSpectatorList = settingsSnapshot.radarSpectatorList;
    const bool wantsEspFlags = settingsSnapshot.espFlags;
    const bool wantsEspFlagMoney = settingsSnapshot.espFlagMoney;
    const bool wantsEspFlagScoped = settingsSnapshot.espFlagScoped;
    const bool wantsEspFlagDefusing = settingsSnapshot.espFlagDefusing;
    const bool wantsEspFlagBlind = settingsSnapshot.espFlagBlind;
    const bool wantsEspFlagKit = settingsSnapshot.espFlagKit;
    const bool wantsRadarEnabled = settingsSnapshot.radarEnabled;
    const bool wantsRadarShowAngles = settingsSnapshot.radarShowAngles;
    const bool wantsPlayerVisibility =
        settingsSnapshot.espVisibilityColoring ||
        settingsSnapshot.targetNeedsVisibility;
    const bool wantsTargetRecoil = settingsSnapshot.targetNeedsRecoil;
    const bool wantsTargetVelocity = settingsSnapshot.targetNeedsVelocity;
    const bool wantsTargetWeaponState =
        settingsSnapshot.targetNeedsWeaponState;
    s_visibilityEnabled.store(wantsPlayerVisibility, std::memory_order_relaxed);
    const bool wantsWebRadarEnabled = settingsSnapshot.webRadarEnabled;
    const bool wantsWebRadarRemoteEnabled = settingsSnapshot.webRadarRemoteEnabled;
    const auto& espItemEnabledMaskSnapshot = settingsSnapshot.espItemEnabledMask;
    const bool wantsEspItem = settingsSnapshot.espItem;
    const bool wantsEspWorld = settingsSnapshot.espWorld;
    const bool wantsEspWorldProjectiles = settingsSnapshot.espWorldProjectiles;

#include "update_parts/update_bootstrap.inl"
#include "update_parts/update_engine_state.inl"

    // Menu/loading entities may remain readable long after leaving the map.
    // The confirmed scene transition publishes a cleared snapshot once. Do not
    // refill it from those entities or run population recovery in an idle scene.
    if (!engineSignonResolved || !engineSignonInGame || engineSignonMenu) {
        s_requiredReadFailureCount = 0;
        s_lastLiveMapNameSeenUs.store(0, std::memory_order_relaxed);
        s_stageEngineUs.store(TickNowUs() - _stagePipelineStart, std::memory_order_relaxed);
        if (engineSignonResolved)
            MarkDmaReadSuccess();
        else
            MarkDmaReadDegraded();
        return false;
    }

    uintptr_t entityList = 0;
    uintptr_t localPawn = 0;
    uintptr_t localController = 0;
    uintptr_t gameRules = 0;
    uintptr_t globalVars = 0;
    uintptr_t plantedC4Entity = 0;
    uintptr_t weaponC4Entity = 0;
    uintptr_t sensPtr = 0;
    uintptr_t listEntry = 0;
    int highestEntityIndex = 0;
    std::string liveMapKey;
    view_matrix_t viewMatrix = {};
    memcpy(&viewMatrix, &s_viewMatrix, sizeof(viewMatrix));
    Vector3 viewAngles = s_viewAngles;
    auto& handle = s_dataScatterHandle;
    if (!handle)
        handle = CreateTrackedScatterHandle(mem);
    if (!handle) {
        MarkDmaReadFailure();
        return false;
    }

    ScopedScatterHandleCloser scatterHandleScope(mem, handle);

    struct ScopedScatterWarningSuppression {
        Memory& memory;

        explicit ScopedScatterWarningSuppression(Memory& memRef) : memory(memRef)
        {
            memory.SetScatterReadWarningSuppressed(true);
        }

        ~ScopedScatterWarningSuppression()
        {
            memory.SetScatterReadWarningSuppressed(false);
        }
    } scatterWarningScope(mem);

    auto executeOptionalScatterRead = [&]() -> bool {
        return mem.ExecuteReadScatter(handle);
    };

    auto logUpdateDataIssue = [&](const char* stage, const char* reason) {
        const auto curStatus = static_cast<esp::GameStatus>(s_gameStatus.load(std::memory_order_relaxed));
        static uint32_t s_detailCount = 0;
        if (!esp::diagnostics::ShouldEmitUpdateIssue(
                reason,
                curStatus == esp::GameStatus::Ok,
                s_detailCount)) {
            return;
        }

        const std::string_view stageSv = stage ? stage : "unknown";
        const char* stageLabel = "unknown";
        if (stageSv == "engine_signon") stageLabel = "engine network client sign-on state";
        else if (stageSv == "scatter_1") stageLabel = "base pointers (entityList/localPawn/view/sensitivity)";
        else if (stageSv == "scatter_2") stageLabel = "local state (team/pos/listEntry/gameRules/globalVars)";
        else if (stageSv == "scatter_3") stageLabel = "planted C4 state";
        else if (stageSv == "scatter_4") stageLabel = "planted C4 world position";
        else if (stageSv == "scatter_5") stageLabel = "controller array";
        else if (stageSv == "scatter_6") stageLabel = "controller data (pawn/name/ping/moneyService)";
        else if (stageSv == "scatter_7") stageLabel = "money service account";
        else if (stageSv == "scatter_8") stageLabel = "pawn entries";
        else if (stageSv == "scatter_9") stageLabel = "pawn pointers";
        else if (stageSv == "scatter_10" || stageSv == "scatter_10_core") stageLabel = "player state batch";
        else if (stageSv == "scatter_11") stageLabel = "defuser flags";
        else if (stageSv == "scatter_11_spotted") stageLabel = "spotted state flags";
        else if (stageSv == "scatter_12") stageLabel = "active weapon handles";
        else if (stageSv == "scatter_12_inv") stageLabel = "inventory weapon handles";
        else if (stageSv == "scatter_13") stageLabel = "active weapon entries";
        else if (stageSv == "scatter_14") stageLabel = "active weapon entities";
        else if (stageSv == "scatter_15") stageLabel = "weapon id/ammo";
        else if (stageSv == "scatter_16") stageLabel = "bone array pointers";
        else if (stageSv == "scatter_17") stageLabel = "bone positions";
        else if (stageSv == "scatter_18") stageLabel = "world blocks";
        else if (stageSv == "scatter_19") stageLabel = "world entity pointers";
        else if (stageSv == "scatter_20") stageLabel = "world entity details";
        else if (stageSv == "scatter_21") stageLabel = "world entity positions";
        else if (stageSv == "scatter_bomb_1") stageLabel = "bomb scan blocks";
        else if (stageSv == "scatter_bomb_2") stageLabel = "bomb scan entity pointers";
        else if (stageSv == "scatter_bomb_3") stageLabel = "bomb scan entity details";
        else if (stageSv == "scatter_bomb_4") stageLabel = "bomb scan entity positions";
        else if (stageSv == "base_ptrs") stageLabel = "required base pointers";
        else if (stageSv == "entity_list_entry") stageLabel = "entity list entry";

        DmaLogPrintf(
            "[WARN] UpdateData stage=%s (%s) reason=%s",
            stage ? stage : "unknown",
            stageLabel,
            reason ? reason : "unknown");

        if (stageSv == "scatter_1" || stageSv == "base_ptrs") {
            DmaLogPrintf(
                "[WARN] UpdateData details: ptrs client=0x%llX entityList=0x%llX localPawn=0x%llX",
                static_cast<unsigned long long>(g::clientBase),
                static_cast<unsigned long long>(entityList),
                static_cast<unsigned long long>(localPawn));
        } else if (stageSv == "scatter_10" || stageSv == "scatter_10_core") {
            DmaLogPrintf(
                "[WARN] UpdateData details: player-batch listEntry=0x%llX localPawn=0x%llX",
                static_cast<unsigned long long>(listEntry),
                static_cast<unsigned long long>(localPawn));
        } else if (stageSv == "scatter_13" || stageSv == "scatter_12" || stageSv == "scatter_14") {
            DmaLogPrintf(
                "[WARN] UpdateData details: weapon-chain entityList=0x%llX listEntry=0x%llX",
                static_cast<unsigned long long>(entityList),
                static_cast<unsigned long long>(listEntry));
        }
    };

    auto failRequiredRead = [&](const char* stage, const char* reason) -> bool {
        ++s_requiredReadFailureCount;
        scatterHandleScope.Close();

        if (s_requiredReadFailureCount >= RECOVERY_FAILURE_THRESHOLD) {
            const auto reset = esp::recovery::EvaluateResetPolicy(
                esp::recovery::ResetTrigger::RequiredReadsPersistent);
            ResetRuntimeStateHard(reset.reason, reset.publishClearedSnapshot);
            g::clientBase = 0;
            g::engine2Base = 0;
            logUpdateDataIssue(stage, reason);
            RequestDmaRecovery("required_reads_persistent");
            MarkDmaReadFailure();
            return true;
        }

        MarkDmaReadDegraded();
        return true;
    };
    auto failScatter = [&](const char* stage) -> bool {
        return failRequiredRead(stage, "scatter_read_failed");
    };

    auto failMissing = [&](const char* stage, const char* what) -> bool {
        return failRequiredRead(stage, what);
    };

    const esp::diagnostics::NarrowDebugOptions narrowDebug;

#include "update_parts/update_scheduler_state.inl"

    const uint64_t _stageEngineEnd = TickNowUs();
#include "base_reads.inl"
    {
        static bool s_entityShapeLiveConfirmed = false;
        static uint32_t s_entityShapeLiveStreak = 0;
        static uint64_t s_entityShapeLiveSinceUs = 0;
        static uint64_t s_lastEntityShapeTransitionUs = 0;
        static uint64_t s_entityShapeSessionGeneration = 0;
        const uint64_t shapeNowUs = TickNowUs();
        const uint64_t shapeSessionGeneration =
            s_dmaSessionGeneration.load(std::memory_order_relaxed);
        if (s_entityShapeSessionGeneration != shapeSessionGeneration) {
            s_entityShapeSessionGeneration = shapeSessionGeneration;
            s_entityShapeLiveConfirmed = false;
            s_entityShapeLiveStreak = 0;
            s_entityShapeLiveSinceUs = 0;
            s_lastEntityShapeTransitionUs = 0;
        }
        const bool definitelyMenuByEngine =
            s_engineStatusResolved.load(std::memory_order_relaxed) &&
            s_engineMenu.load(std::memory_order_relaxed) &&
            !s_engineInGame.load(std::memory_order_relaxed);
        const bool entityShapeLive =
            esp::data::IsEntityShapeLive(
                definitelyMenuByEngine,
                g::clientBase != 0,
                g::engine2Base != 0,
                entityList != 0,
                listEntry != 0,
                playerSlotScanLimit,
                highestEntityIndex);
        if (entityShapeLive) {
            if (s_entityShapeLiveSinceUs == 0)
                s_entityShapeLiveSinceUs = shapeNowUs;
            if (s_entityShapeLiveStreak < 0xFFFFFFFFu)
                ++s_entityShapeLiveStreak;
        } else {
            s_entityShapeLiveStreak = 0;
            s_entityShapeLiveSinceUs = 0;
            s_entityShapeLiveConfirmed = false;
        }

        if (esp::data::ShouldTransitionOnEntityShapeLive(
                s_entityShapeLiveStreak,
                s_entityShapeLiveSinceUs,
                s_entityShapeLiveConfirmed,
                s_lastEntityShapeTransitionUs,
                shapeNowUs)) {
            s_entityShapeLiveConfirmed = true;
            s_lastEntityShapeTransitionUs = shapeNowUs;
            const bool engineAlreadyLive = esp::data::IsConfirmedLiveEngineState(
                s_engineStatusResolved.load(std::memory_order_relaxed),
                s_engineInGame.load(std::memory_order_relaxed),
                s_engineMenu.load(std::memory_order_relaxed),
                s_engineSignOnState.load(std::memory_order_relaxed),
                s_engineMaxClients.load(std::memory_order_relaxed));
            if (!engineAlreadyLive) {
                BumpSceneReset(shapeNowUs);
                SetSceneWarmupState(esp::SceneWarmupState::HierarchyWarming, shapeNowUs);
                RecordDmaEvent({
                    .action = "rewarm",
                    .reason = "entity_shape_match_enter"
                });
            }
        }
    }
    const uint64_t _stageBaseEnd = TickNowUs();
#include "player_reads.inl"
    const uint64_t _stagePlayerEnd = TickNowUs();
    const SharedLocalIdentitySlotData sharedLocalIdentitySlots{
        .names = names,
        .healths = healths,
        .lifeStates = lifeStates,
        .armors = armors,
        .moneys = moneys,
        .hasDefuserFlags = hasDefuserFlags
    };
    auto applyLiveLocalPawnCoreIdentity = [&](SharedLocalIdentity& identity) {
        if (!localPawnCoreLiveResolved)
            return;
        identity.isDead = localPawnHealth <= 0 || localPawnLifeState != 0;
        identity.health = identity.isDead ? 0 : std::clamp(localPawnHealth, 0, 100);
    };
    auto resolveSharedLocalIdentity = [&](
        int localPlayerIndex,
        SharedLocalIdentity& identity) {
        ResolveSharedLocalIdentityFromSlot(
            localPlayerIndex,
            sharedLocalIdentitySlots,
            identity);
        applyLiveLocalPawnCoreIdentity(identity);
    };
{
#include "commit_state.inl"
}
    const uint64_t _stageCommitEnd = TickNowUs();
    if (wantsPlayerVisibility)
        PublishPlayerVisibilityFrame();
#include "player_aux_reads.inl"
    const uint64_t _stagePlayerAuxEnd = TickNowUs();
#include "Features/ESP/bone_reader.h"
#include "inventory_reads.inl"
    const uint64_t _stageInvEnd = TickNowUs();
    {
        esp::BoneReader boneReader;
        // Reader-owned cache, reset by scene/pawn changes inside ReadBones.
        boneReader.ReadBones(
            handle,
            wantsEspSkeleton,
            wantsEspShowTeammates,
            pawns,
            pawnHandles,
            healths,
            lifeStates,
            positions,
            teams,
            liveTeamReads,
            playerResolvedSlots,
            playerResolvedSlotCount,
            localTeamLiveResolved,
            localControllerTeam,
            localTeamLikelySwitched,
            esp::data::ShouldRunBoneLane(
                _playerHierarchyActiveTick || _playerAuxActiveTick,
                _inventoryActiveTick,
                _inventoryFullTick),
            sceneNodes,
            hasBoneData,
            boneSampleTimeUs,
            bonePoseAnchors,
            allBones,
            allHitboxes,
            hitboxCounts,
            hasHitboxData,
            hitboxSampleTimeUs,
            _boneReadsActiveTick
        );
    }
    const uint64_t _stageBoneEnd = TickNowUs();
    const uint64_t nowUs = TickNowUs();
#include "world_reads.inl"
    const uint64_t _stageWorldEnd = TickNowUs();
#include "bomb_reads.inl"
    const uint64_t _stageBombEnd = TickNowUs();
{
#include "commit_enrichment_state.inl"
}
    const uint64_t _stageEnrichEnd = TickNowUs();
#include "update_parts/update_stage_metrics.inl"
    scatterHandleScope.KeepOpen();
    return true;
}
