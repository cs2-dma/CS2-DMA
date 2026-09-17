#include "Features/ESP/esp.h"
#include "Features/ESP/esp_local_state.h"
#include "Features/ESP/player_data_reader.h"
#include "Features/ESP/esp_renderer.h"
#include "Features/ESP/bone_reader.h"
#include "Features/ESP/Recovery/reset_policy.h"
#include "Features/ESP/Worker/worker_policy.h"
#include "Features/World/grenade_helper.h"
#include "app/Core/fallback_log.h"
#include <DMALibrary/Memory/Memory.h>
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace {
    std::mutex s_workerLifecycleMutex;
    std::atomic<bool> s_cacheRefreshRequested{false};

    void ApplyWorkerThreadTuning(const wchar_t* description)
    {
        if (description)
            SetThreadDescription(GetCurrentThread(), description);
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    }

    template <typename Body, typename OnFailure>
    void RunRestartableWorker(
        const std::stop_token& stopToken,
        Body&& body,
        OnFailure&& onFailure) noexcept
    {
        uint32_t consecutiveFailures = 0;
        while (!stopToken.stop_requested()) {
            const auto runStartedAt = std::chrono::steady_clock::now();
            bool terminatedByException = false;
            try {
                body();
                if (stopToken.stop_requested())
                    return;
            } catch (...) {
                terminatedByException = true;
            }

            if ((std::chrono::steady_clock::now() - runStartedAt) >= std::chrono::seconds(10))
                consecutiveFailures = 0;
            ++consecutiveFailures;

            try {
                onFailure(terminatedByException);
            } catch (...) {
                app::diagnostics::WriteFallbackError(
                    "ESP worker failure handler threw an exception");
            }

            if (!stopToken.stop_requested())
                std::this_thread::sleep_for(std::chrono::milliseconds(
                    esp::worker::WorkerRestartBackoffMs(consecutiveFailures)));
        }
    }
}

namespace esp {
    void RequestCacheRefresh()
    {
        s_cacheRefreshRequested.store(true, std::memory_order_release);
    }

    bool ProcessPendingCacheRefreshRequest()
    {
        if (!s_cacheRefreshRequested.exchange(false, std::memory_order_acq_rel))
            return false;

        const auto reset = esp::recovery::EvaluateResetPolicy(
            esp::recovery::ResetTrigger::UserRequestedRefresh);
        s_bombEpoch.fetch_add(1, std::memory_order_relaxed);
        ResetRuntimeStateSoft(reset.reason);
        // A manual cache refresh preserves the live snapshot and is not a
        // scene boundary. Re-enter hierarchy warming so a transient local
        // identity mismatch cannot leave the pipeline in SceneTransition.
        SetSceneWarmupState(esp::SceneWarmupState::HierarchyWarming);
        RefreshDmaCaches("user_requested_refresh", DmaRefreshTier::Full, true);
        return true;
    }

    bool UpdateData()
    {
        esp::PlayerDataReader reader;
        return reader.UpdateData();
    }

    void StartDataWorker()
    {
        std::scoped_lock lifecycleLock(s_workerLifecycleMutex);
        if (s_dataWorker.joinable() || s_cameraWorker.joinable())
            return;

        s_dataWorkerRunning.store(true, std::memory_order_relaxed);
        s_dataWorkerStopRequested.store(false, std::memory_order_relaxed);
        s_cameraWorkerPaused.store(false, std::memory_order_relaxed);
        s_dmaManualRefreshInProgress.store(false, std::memory_order_relaxed);
        const uint64_t nowUs = TickNowUs();
        s_sessionStartUs.store(nowUs, std::memory_order_relaxed);
        s_dataWorkerCycleUs.store(0, std::memory_order_relaxed);
        s_dataWorkerMaxCycleUs.store(0, std::memory_order_relaxed);
        s_dataWorkerRecentMaxCycleUs.store(0, std::memory_order_relaxed);
        s_dataWorkerCycleP50Us.store(0, std::memory_order_relaxed);
        s_dataWorkerCycleP95Us.store(0, std::memory_order_relaxed);
        s_dataWorkerCycleP99Us.store(0, std::memory_order_relaxed);
        s_dataWorkerDeadlineMissCount.store(0, std::memory_order_relaxed);
        s_dataWorkerCycleSampleCount.store(0, std::memory_order_relaxed);
        s_dataWorkerCycleOverBudgetCount.store(0, std::memory_order_relaxed);
        s_dataWorkerCycleOver5msCount.store(0, std::memory_order_relaxed);
        s_dataWorkerCycleOver16msCount.store(0, std::memory_order_relaxed);
        s_cameraWorkerCycleUs.store(0, std::memory_order_relaxed);
        s_cameraWorkerMaxCycleUs.store(0, std::memory_order_relaxed);
        s_cameraWorkerRecentMaxCycleUs.store(0, std::memory_order_relaxed);
        s_cameraWorkerCycleP50Us.store(0, std::memory_order_relaxed);
        s_cameraWorkerCycleP95Us.store(0, std::memory_order_relaxed);
        s_cameraWorkerCycleP99Us.store(0, std::memory_order_relaxed);
        s_cameraWorkerDeadlineMissCount.store(0, std::memory_order_relaxed);
        s_cameraWorkerCycleSampleCount.store(0, std::memory_order_relaxed);
        s_cameraWorkerCycleOverBudgetCount.store(0, std::memory_order_relaxed);
        s_cameraWorkerCycleOver5msCount.store(0, std::memory_order_relaxed);
        s_cameraWorkerCycleOver16msCount.store(0, std::memory_order_relaxed);
        s_playerCoreAnomalyCount.store(0, std::memory_order_relaxed);
        s_playerCoreRecoveredCount.store(0, std::memory_order_relaxed);
        s_playerCoreGlobalRefreshAvoidedCount.store(0, std::memory_order_relaxed);
        s_playerCoreGeneration.store(0, std::memory_order_relaxed);
        s_playerCoreCaptureTimeUs.store(0, std::memory_order_relaxed);
        s_prevPlayerCoreCaptureTimeUs.store(0, std::memory_order_relaxed);
        s_playerCoreBatchQuality.store(0, std::memory_order_relaxed);
        s_playerCoreIncompleteMask.store(0, std::memory_order_relaxed);
        s_playerCoreInvalidMask.store(0, std::memory_order_relaxed);
        s_playerCoreBatchHoldCount.store(0, std::memory_order_relaxed);
        s_playerUnexpectedEvictionCount.store(0, std::memory_order_relaxed);
        s_playerExpectedEvictionCount.store(0, std::memory_order_relaxed);
        s_playerControllerSlotCountStat.store(0, std::memory_order_relaxed);
        s_playerResolvedSlotCountStat.store(0, std::memory_order_relaxed);
        s_playerPlausibleCoreSlotCountStat.store(0, std::memory_order_relaxed);
        s_playerDuplicateIdentityFilteredStat.store(0, std::memory_order_relaxed);
        s_playerBacklinkMismatchStat.store(0, std::memory_order_relaxed);
        s_playerHierarchyHeldSlotCount.store(0, std::memory_order_relaxed);
        s_playerZeroPawnHeldSlotCount.store(0, std::memory_order_relaxed);
        s_playerCoreHeldSlotCount.store(0, std::memory_order_relaxed);
        s_dmaManualRefreshLastUs.store(0, std::memory_order_relaxed);
        s_dmaManualRefreshPeakUs.store(0, std::memory_order_relaxed);
        s_dmaManualRefreshCount.store(0, std::memory_order_relaxed);
        s_dmaManualRefreshRecentPeakUs.store(0, std::memory_order_relaxed);
        s_dmaManualRefreshRecentCount.store(0, std::memory_order_relaxed);
        s_dmaManualRefreshQueuedCount.store(0, std::memory_order_relaxed);
        s_dmaManualRefreshQueuedRecentCount.store(0, std::memory_order_relaxed);
        s_dmaManualRefreshSuppressedCount.store(0, std::memory_order_relaxed);
        s_dmaManualRefreshSuppressedRecentCount.store(0, std::memory_order_relaxed);
        s_dmaManualRefreshAvoidedCount.store(0, std::memory_order_relaxed);
        s_dmaManualRefreshAvoidedRecentCount.store(0, std::memory_order_relaxed);
        s_dmaManualRefreshCoalescedCount.store(0, std::memory_order_relaxed);
        s_cameraWorkerPauseOrphanRecoveryCount.store(0, std::memory_order_relaxed);
        s_cameraWorkerPauseReleaseImbalanceCount.store(0, std::memory_order_relaxed);
        Memory::DMA_EXECUTE_SCATTER_RECENT_PEAK_US.store(0, std::memory_order_relaxed);
        Memory::DMA_SCATTER_BUDGET_US.store(1000000u / kDataWorkerLiveHz, std::memory_order_relaxed);
        Memory::DMA_EXECUTE_SCATTER_RECENT_WINDOW_START_US.store(nowUs, std::memory_order_relaxed);
        Memory::DMA_EXECUTE_SCATTER_RECENT_COUNT.store(0, std::memory_order_relaxed);
        Memory::DMA_EXECUTE_SCATTER_OVER_1MS_RECENT_COUNT.store(0, std::memory_order_relaxed);
        Memory::DMA_EXECUTE_SCATTER_OVER_BUDGET_RECENT_COUNT.store(0, std::memory_order_relaxed);
        Memory::DMA_EXECUTE_SCATTER_OVER_5MS_RECENT_COUNT.store(0, std::memory_order_relaxed);
        Memory::DMA_EXECUTE_SCATTER_OVER_16MS_RECENT_COUNT.store(0, std::memory_order_relaxed);
        s_dataWorkerLastLoopStartUs.store(0, std::memory_order_relaxed);
        s_dataWorkerLastLoopEndUs.store(0, std::memory_order_relaxed);
        s_dataWorkerInFlightSinceUs.store(0, std::memory_order_relaxed);
        s_dataWorkerUpdateInFlight.store(false, std::memory_order_relaxed);
        RecoverOrphanedCameraWorkerPauseRequest();
        ResetCameraSnapshot();

        try {
            s_dataWorker = std::jthread([](const std::stop_token& stopToken) noexcept {
                RunRestartableWorker(stopToken, [&] {
                    ApplyWorkerThreadTuning(L"KevqDMA Data");
                    esp::PlayerDataReader{}.DataWorkerLoop(stopToken);
                }, [](bool terminatedByException) {
                    s_dataWorkerUpdateInFlight.store(false, std::memory_order_release);
                    s_dataWorkerInFlightSinceUs.store(0, std::memory_order_relaxed);
                    s_dataWorkerRunning.store(false, std::memory_order_relaxed);
                    RecordDmaEvent({
                        .action = terminatedByException
                            ? "worker_exception"
                            : "worker_exit",
                        .reason = "data_worker_thread"
                    });
                    RequestDmaRecovery(
                        terminatedByException
                            ? "data_worker_thread_exception"
                            : "data_worker_thread_unexpected_exit");
                });
            });

            s_cameraWorker = std::jthread([](const std::stop_token& stopToken) noexcept {
                RunRestartableWorker(stopToken, [&] {
                    ApplyWorkerThreadTuning(L"KevqDMA Camera");
                    esp::PlayerDataReader{}.CameraWorkerLoop(stopToken);
                }, [](bool terminatedByException) {
                    s_cameraWorkerRunning.store(false, std::memory_order_relaxed);
                    ResetCameraSnapshot();
                    RecordDmaEvent({
                        .action = terminatedByException
                            ? "worker_exception"
                            : "worker_exit",
                        .reason = "camera_worker_thread"
                    });
                    RequestDmaRecovery(
                        terminatedByException
                            ? "camera_worker_thread_exception"
                            : "camera_worker_thread_unexpected_exit");
                });
            });
        } catch (...) {
            s_cameraWorker.request_stop();
            s_dataWorker.request_stop();
            if (s_cameraWorker.joinable())
                s_cameraWorker.join();
            if (s_dataWorker.joinable())
                s_dataWorker.join();
            s_cameraWorkerRunning.store(false, std::memory_order_relaxed);
            s_dataWorkerRunning.store(false, std::memory_order_relaxed);
            s_dataWorkerStopRequested.store(true, std::memory_order_relaxed);
            throw;
        }
    }

    void StopDataWorker()
    {
        std::scoped_lock lifecycleLock(s_workerLifecycleMutex);
        s_dataWorkerStopRequested.store(true, std::memory_order_relaxed);
        s_cameraWorker.request_stop();
        s_dataWorker.request_stop();
        if (s_cameraWorker.joinable())
            s_cameraWorker.join();
        if (s_dataWorker.joinable())
            s_dataWorker.join();
        StopDmaAdminThread();
        s_cameraWorkerPauseRequests.store(0, std::memory_order_release);
        s_cameraWorkerPaused.store(false, std::memory_order_release);
        s_dmaAdminPauseActive.store(false, std::memory_order_release);
        s_dmaManualRefreshInProgress.store(false, std::memory_order_release);
        s_cameraWorkerRunning.store(false, std::memory_order_relaxed);
        s_dataWorkerRunning.store(false, std::memory_order_relaxed);
        s_dataWorkerUpdateInFlight.store(false, std::memory_order_relaxed);
        s_dataWorkerInFlightSinceUs.store(0, std::memory_order_relaxed);
    }

    uint64_t GetPublishCount()
    {
        return s_publishCount.load(std::memory_order_acquire);
    }

    void Draw()
    {
        esp::EspRenderer renderer;
        renderer.Draw();
    }
}
