#include <Windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")
#include <DMALibrary/Memory/Memory.h>

#include "app/Bootstrap/crash_handler.h"
#include "app/Bootstrap/runtime_console.h"
#include "app/Config/config.h"
#include "app/Core/build_info.h"
#include "app/Core/fallback_log.h"
#include "app/Core/globals.h"
#include "app/Input/input_device.h"
#include "app/Input/primary_keyboard.h"
#include "app/Localization/localization.h"
#include "app/Platform/overlay.h"
#include "Features/ESP/esp.h"
#include "Features/ESP/Recovery/process_identity_policy.h"
#include "Features/Target/target.h"
#include "Features/WebRadar/webradar.h"
#include "Game/Offsets/runtime_offsets.h"
#include "Game/Offsets/runtime_offsets_parse_utils.h"

#include <atomic>
#include <chrono>
#include <conio.h>
#include <cstring>
#include <format>
#include <string>
#include <thread>
#include <utility>

namespace
{
    struct TimerResolutionGuard
    {
        bool active = false;

        TimerResolutionGuard()
        {
            active = (timeBeginPeriod(1) == TIMERR_NOERROR);
        }

        ~TimerResolutionGuard()
        {
            if (active)
                timeEndPeriod(1);
        }

        TimerResolutionGuard(const TimerResolutionGuard&) = delete;
        TimerResolutionGuard& operator=(const TimerResolutionGuard&) = delete;
    };

    bool HasFlag(int argc, char* argv[], const char* flag)
    {
        for (int i = 1; i < argc; ++i) {
            if (argv[i] && std::strcmp(argv[i], flag) == 0)
                return true;
        }
        return false;
    }

    bool SafeAutoUpdateOffsets(
        std::string* message,
        runtime_offsets::AutoUpdateReport* report,
        bool forceRemote = false)
    {
        try
        {
            return runtime_offsets::AutoUpdateFromGitHub(
                message,
                report,
                forceRemote);
        }
        catch (const std::exception& e)
        {
            if (message)
                *message = app::localization::Format(
                    "Offset sync failed: {}",
                    e.what());
            return false;
        }
        catch (...)
        {
            if (message)
                *message = app::localization::GetCopy(
                    "Offset sync failed. Using local offsets.");
            return false;
        }
    }

    bool SafeLoadOffsets(std::string* message)
    {
        try
        {
            return runtime_offsets::Load(message);
        }
        catch (const std::exception& e)
        {
            if (message)
                *message = app::localization::Format(
                    "Offsets load failed: {}",
                    e.what());
            return false;
        }
        catch (...)
        {
            if (message)
                *message = app::localization::GetCopy(
                    "Offsets load failed: runtime exception.");
            return false;
        }
    }

    bool SafeInitDma(std::string* message)
    {
        try
        {
            return mem.InitDma(true, false);
        }
        catch (const std::exception& e)
        {
            if (message)
                *message = app::localization::Format(
                    "DMA initialization failed: {}",
                    e.what());
            return false;
        }
        catch (...)
        {
            if (message)
                *message = app::localization::GetCopy(
                    "DMA initialization failed: runtime exception.");
            return false;
        }
    }

    std::string BuildPatchDisplay(const runtime_offsets::PatchInfo& patch)
    {
        if (patch.patchVersion.empty())
            return {};
        if (patch.clientVersion <= 0)
            return patch.patchVersion;

        const int lastThreeDigits = patch.clientVersion % 1000;
        return std::format("{} ({:03d})", patch.patchVersion, lastThreeDigits);
    }

    void WaitForInitialEspSnapshot()
    {
        constexpr auto kWarmupBudget = std::chrono::milliseconds(450);
        constexpr auto kWarmupRetryDelay = std::chrono::milliseconds(5);

        const uint64_t initialPublishCount = esp::GetPublishCount();
        const auto deadline = std::chrono::steady_clock::now() + kWarmupBudget;
        do {
            if (esp::GetPublishCount() > initialPublishCount)
                return;
            std::this_thread::sleep_for(kWarmupRetryDelay);
        } while (std::chrono::steady_clock::now() < deadline);
    }

    void WaitForExitAcknowledge(const bootstrap::RuntimeConsole& console)
    {
        HANDLE inputHandle = GetStdHandle(STD_INPUT_HANDLE);
        if (inputHandle == INVALID_HANDLE_VALUE)
            return;
        if (GetFileType(inputHandle) != FILE_TYPE_CHAR)
            return;

        console.PrintInfoLine("Press any key to exit.");
        [[maybe_unused]] const int key = _getch();
    }

    void PrintCommunityLinks(const bootstrap::RuntimeConsole& console)
    {
        console.PrintInfoLine("Menu: P | Screen F2 | Telegram: @ne_sravnim");
    }

    std::string BuildCompactOffsetTimestampDisplay(const std::string& canonicalTimestamp)
    {
        if (canonicalTimestamp.size() < 12)
            return {};

        const int year = runtime_offsets::parse_utils::ParseDecimalPart(canonicalTimestamp, 0, 4);
        const int month = runtime_offsets::parse_utils::ParseDecimalPart(canonicalTimestamp, 4, 2);
        const int day = runtime_offsets::parse_utils::ParseDecimalPart(canonicalTimestamp, 6, 2);
        const int hour = runtime_offsets::parse_utils::ParseDecimalPart(canonicalTimestamp, 8, 2);
        const int minute = runtime_offsets::parse_utils::ParseDecimalPart(canonicalTimestamp, 10, 2);
        if (year < 0 || month < 1 || day < 1 || hour < 0 || minute < 0)
            return {};

        return std::format(
            "{:02d}-{:02d}-{:04d} {:02d}:{:02d} UTC",
            day, month, year, hour, minute);
    }

    template <typename Fn>
    auto RunWithPendingAnimation(const bootstrap::RuntimeConsole& console,
                                 const std::string& label,
                                 const std::string& text,
                                 Fn&& fn)
        -> decltype(fn())
    {
        using ResultT = decltype(fn());

        const DmaLogLevel savedLogLevel = DmaGetLogLevel();
        DmaSetLogLevel(DmaLogLevel::Silent);

        std::jthread animator([&console, &label, &text](const std::stop_token& stopToken) noexcept {
            try {
                int phase = 0;
                while (!stopToken.stop_requested()) {
                    console.PrintPending(label, text, phase++);
                    std::this_thread::sleep_for(std::chrono::milliseconds(90));
                }
            } catch (...) {
                app::diagnostics::WriteFallbackError(
                    "Startup progress animation stopped unexpectedly");
            }
        });

        try
        {
            ResultT result = fn();
            animator.request_stop();
            if (animator.joinable())
                animator.join();
            DmaSetLogLevel(savedLogLevel);
            return result;
        }
        catch (...)
        {
            animator.request_stop();
            if (animator.joinable())
                animator.join();
            DmaSetLogLevel(savedLogLevel);
            throw;
        }
    }

    template <typename Fn>
    auto RunWithPendingAnimation(const bootstrap::RuntimeConsole& console, const std::string& text, Fn&& fn)
        -> decltype(fn())
    {
        return RunWithPendingAnimation(console, "Info", text, std::forward<Fn>(fn));
    }
}

int RunApplication(int argc, char* argv[])
{
    bootstrap::InstallCrashHandler();

    TimerResolutionGuard timerResolutionGuard;

    const bool verboseLogs = HasFlag(argc, argv, "--verbose");
    const bool offsetsSelfTest = HasFlag(argc, argv, "--offsets-self-test");

    bootstrap::RuntimeConsole console;
    console.Initialize(verboseLogs);

    console.PrintStartupBanner();
    console.PrintInfoLine("KevqDMA");

    console.AnimateForAtLeast("Connection", 360);
    console.PrintInfoOk("Connection");
    PrintCommunityLinks(console);
    console.PrintBlankLine();

    std::string localFallbackMessage;
    const bool localFallbackReady = runtime_offsets::PrepareLocalFallback(
        &localFallbackMessage);

    std::string runtimeOffsetMessage;
    const std::string offsetSource = "offsets.json";
    const std::string offsetLoadLabel =
        app::localization::Format("Offset load ({})", offsetSource);
    const bool loadOk = RunWithPendingAnimation(console, offsetLoadLabel, [&]() {
        return SafeLoadOffsets(&runtimeOffsetMessage);
    });
    if (loadOk) {
        console.PrintInfoOk(offsetLoadLabel);
    } else {
        console.PrintInfoFail(offsetLoadLabel);
        if (!runtimeOffsetMessage.empty())
            console.PrintInfoLine(runtimeOffsetMessage);
        else if (!localFallbackReady && !localFallbackMessage.empty())
            console.PrintInfoLine(localFallbackMessage);
    }
    const runtime_offsets::StateView loadedOffsetState =
        runtime_offsets::GetStateView();
    const std::string loadedOffsetsPatch =
        BuildPatchDisplay(loadedOffsetState.offsetsPatch);
    const std::string loadedOffsetsTimestamp =
        BuildCompactOffsetTimestampDisplay(
            loadedOffsetState.selectedSourceTimestamp);
    if (loadOk && (!loadedOffsetsPatch.empty() || !loadedOffsetsTimestamp.empty())) {
        std::string offsetStateLine;
        if (!loadedOffsetsPatch.empty())
            offsetStateLine +=
                app::localization::Format("Last: {}", loadedOffsetsPatch);
        if (!loadedOffsetsTimestamp.empty()) {
            if (!offsetStateLine.empty())
                offsetStateLine += " | ";
            offsetStateLine += loadedOffsetsTimestamp;
        }
        console.PrintInfoMarkedLine(offsetStateLine);
    }

    if (offsetsSelfTest) {
        if (!loadOk) {
            std::string fallbackMessage;
            runtime_offsets::AutoUpdateReport fallbackReport = {};
            const bool fallbackUpdated = RunWithPendingAnimation(
                console,
                "GitHub offset fallback",
                [&]() {
                    return SafeAutoUpdateOffsets(
                        &fallbackMessage,
                        &fallbackReport,
                        true);
                });
            if (!fallbackUpdated || !SafeLoadOffsets(&fallbackMessage)) {
                console.PrintInfoFail("GitHub offset fallback");
                if (!fallbackMessage.empty())
                    console.PrintInfoLine(fallbackMessage);
                return 1;
            }
            console.PrintInfoOk("GitHub offset fallback");
        }
        return 0;
    }

    std::string dmaInitMessage;
    const bool dmaOk = RunWithPendingAnimation(console, "DMA subsystem initialized", [&]() {
        return SafeInitDma(&dmaInitMessage);
    });
    if (!dmaOk) {
        console.PrintInfoFail("DMA subsystem initialized");
        console.PrintErrorLine(dmaInitMessage.empty() ? "DMA initialization failed" : dmaInitMessage);
        WaitForExitAcknowledge(console);
        return 1;
    }
    console.PrintInfoOk("DMA subsystem initialized");

    // Resolve the target keyboard before latency-sensitive workers start.
    // Failure is non-fatal and is retried after a DMA session rebuild.
    (void)app::input::InitializePrimaryKeyboard();

    if (!esp::ApplyDmaRuntimeCacheProfile(esp::DmaCacheMode::ProcessDiscovery, true))
        console.PrintLine("DMA", "Cache profile could not be verified; default refresh timing remains active.");

    config::Load();

    {
        const DmaLogLevel attachWaitLogLevel = DmaGetLogLevel();
        DmaSetLogLevel(DmaLogLevel::Silent);

        std::atomic<bool> csReady{false};
        std::atomic<bool> csCanceled{false};

        console.PrintInfoPending("Waiting for cs2.exe", 0);
        std::jthread animator([&](const std::stop_token& stopToken) noexcept {
            try {
                int phase = 1;
                while (!stopToken.stop_requested() &&
                       !csReady.load(std::memory_order_acquire) &&
                       !csCanceled.load(std::memory_order_acquire)) {
                    console.PrintInfoPending("Waiting for cs2.exe", phase++);
                    std::this_thread::sleep_for(std::chrono::milliseconds(90));
                }
            } catch (...) {
                app::diagnostics::WriteFallbackError(
                    "Process-wait animation stopped unexpectedly");
            }
        });

        
        
        using Clock = std::chrono::steady_clock;
        constexpr auto kProcessPollInterval = std::chrono::milliseconds(100);
        constexpr auto kProcessCatalogRefreshInterval =
            std::chrono::milliseconds(250);
        constexpr uint32_t kProcessCatalogFullRefreshMisses = 4u;
        constexpr uint32_t kProcessCatalogReinitializeMisses = 12u;
        constexpr auto kAttachRetryInterval = std::chrono::milliseconds(750);
        constexpr auto kModuleResolveInterval = std::chrono::milliseconds(100);
        constexpr auto kModuleFullRefreshDelay = std::chrono::milliseconds(750);
        constexpr auto kModuleReattachDelay = std::chrono::seconds(8);
        constexpr auto kDmaReinitializeCooldown = std::chrono::seconds(10);

        esp::recovery::ProcessIdentityTracker processTracker = {};
        DWORD selectedPid = 0;
        bool processAttached = false;
        bool selectedPidObservedStable = false;
        bool moduleFullRefreshIssued = false;
        uint32_t catalogRefreshFailures = 0;
        uint32_t catalogDiscoveryMisses = 0;
        auto nextProcessPoll = Clock::time_point{};
        auto nextProcessCatalogRefresh = Clock::time_point{};
        auto nextAttachAttempt = Clock::time_point{};
        auto nextModuleResolve = Clock::time_point{};
        auto attachedAt = Clock::time_point{};
        auto lastDmaReinitialize = Clock::time_point{};

        while (true) {
            if (GetAsyncKeyState(VK_END) & 1) {
                csCanceled.store(true, std::memory_order_release);
                animator.request_stop();
                if (animator.joinable()) animator.join();
                DmaSetLogLevel(attachWaitLogLevel);
                console.PrintErrorLine("Canceled while waiting for cs2.exe");
                WaitForExitAcknowledge(console);
                return 1;
            }

            
            
            
            const auto now = Clock::now();
            try {
                bool moduleResolveTimedOut = false;
                if (now >= nextProcessPoll) {
                    bool catalogRefreshAttempted = false;
                    bool catalogRefreshed = false;
                    // Startup has no latency-sensitive render/data workers yet.
                    // Keep the remote process catalog current until both target
                    // modules are validated; otherwise a successful-but-stale
                    // lookup can pin the wait loop to an obsolete PID forever.
                    if (now >= nextProcessCatalogRefresh) {
                        catalogRefreshAttempted = true;
                        catalogRefreshed = mem.vHandle &&
                            VMMDLL_ConfigSet(
                                mem.vHandle,
                                VMMDLL_OPT_REFRESH_FREQ_MEDIUM,
                                1);
                        catalogRefreshFailures =
                            catalogRefreshed
                                ? 0u
                                : catalogRefreshFailures + 1u;
                        if (!catalogRefreshed)
                            selectedPidObservedStable = false;
                        nextProcessCatalogRefresh =
                            now + kProcessCatalogRefreshInterval;
                    }

                    DWORD observedPid =
                        mem.vHandle ? mem.GetPidFromName("cs2.exe") : 0;
                    if (observedPid != 0) {
                        catalogDiscoveryMisses = 0;
                        catalogRefreshFailures = 0;
                    } else if (catalogRefreshAttempted) {
                        ++catalogDiscoveryMisses;
                        if (catalogRefreshed &&
                            catalogDiscoveryMisses %
                                kProcessCatalogFullRefreshMisses == 0u) {
                            VMMDLL_ConfigSet(
                                mem.vHandle,
                                VMMDLL_OPT_REFRESH_ALL,
                                1);
                            observedPid = mem.GetPidFromName("cs2.exe");
                            if (observedPid != 0) {
                                catalogDiscoveryMisses = 0;
                                catalogRefreshFailures = 0;
                            }
                        }
                    }
                    const uint64_t nowUs = static_cast<uint64_t>(
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            now.time_since_epoch()).count());
                    const auto identityDecision =
                        esp::recovery::ObserveProcessIdentity(
                            processTracker,
                            selectedPid,
                            observedPid,
                            nowUs);
                    if (identityDecision ==
                        esp::recovery::ProcessIdentityDecision::Stable) {
                        selectedPidObservedStable = true;
                    } else if (identityDecision ==
                               esp::recovery::ProcessIdentityDecision::PendingConfirmation) {
                        selectedPidObservedStable = false;
                    }

                    if (identityDecision ==
                            esp::recovery::ProcessIdentityDecision::ConfirmedProcessLost ||
                        identityDecision ==
                            esp::recovery::ProcessIdentityDecision::ConfirmedReplacement) {
                        if (processAttached || selectedPid != 0)
                            mem.ResetProcessState();
                        processAttached = false;
                        selectedPid = 0;
                        selectedPidObservedStable = false;
                        esp::SetAttachedCs2ProcessId(0);
                        g::clientBase = 0;
                        g::engine2Base = 0;
                    }

                    if (identityDecision ==
                            esp::recovery::ProcessIdentityDecision::ConfirmedNewProcess ||
                        identityDecision ==
                            esp::recovery::ProcessIdentityDecision::ConfirmedReplacement) {
                        selectedPid = observedPid;
                        processAttached = false;
                        selectedPidObservedStable = true;
                        moduleFullRefreshIssued = false;
                        nextAttachAttempt = now;
                    }

                    nextProcessPoll = now + kProcessPollInterval;
                }

                if (selectedPid != 0 &&
                    !processAttached &&
                    now >= nextAttachAttempt) {
                    mem.ResetProcessState();
                    if (mem.vHandle &&
                        mem.AttachToProcessId("cs2.exe", selectedPid, true)) {
                        processAttached = true;
                        attachedAt = now;
                        nextModuleResolve = now;
                        moduleFullRefreshIssued = false;
                        esp::SetAttachedCs2ProcessId(selectedPid);
                    } else {
                        nextAttachAttempt = now + kAttachRetryInterval;
                    }
                }

                if (processAttached && now >= nextModuleResolve) {
                    g::clientBase = mem.GetModuleBase("client.dll");
                    g::engine2Base = mem.GetModuleBase("engine2.dll");
                    uint16_t clientMz = 0;
                    uint16_t engine2Mz = 0;
                    const bool modulesReady =
                        selectedPidObservedStable &&
                        g::clientBase &&
                        g::engine2Base &&
                        mem.Read(g::clientBase, &clientMz, sizeof(clientMz)) &&
                        clientMz == 0x5A4D &&
                        mem.Read(
                            g::engine2Base,
                            &engine2Mz,
                            sizeof(engine2Mz)) &&
                        engine2Mz == 0x5A4D;
                    if (modulesReady)
                        break;

                    g::clientBase = 0;
                    g::engine2Base = 0;
                    const auto moduleWaitAge = now - attachedAt;
                    if (!moduleFullRefreshIssued &&
                        moduleWaitAge >= kModuleFullRefreshDelay) {
                        VMMDLL_ConfigSet(mem.vHandle, VMMDLL_OPT_REFRESH_ALL, 1);
                        moduleFullRefreshIssued = true;
                    }
                    if (moduleWaitAge >= kModuleReattachDelay) {
                        mem.ResetProcessState();
                        processAttached = false;
                        esp::SetAttachedCs2ProcessId(0);
                        nextAttachAttempt = now + kAttachRetryInterval;
                        moduleResolveTimedOut = true;
                    }
                    nextModuleResolve = now + kModuleResolveInterval;
                }

                const bool processDiscoveryExhausted =
                    !processAttached &&
                    selectedPid == 0 &&
                    (catalogRefreshFailures >= 8u ||
                     catalogDiscoveryMisses >=
                         kProcessCatalogReinitializeMisses);
                if ((processDiscoveryExhausted || moduleResolveTimedOut) &&
                    (lastDmaReinitialize == Clock::time_point{} ||
                     now - lastDmaReinitialize >= kDmaReinitializeCooldown)) {
                    mem.CloseDma();
                    if (!mem.InitDma(true, false)) {
                        mem.CloseDma();
                        mem.InitDma(false, false);
                    }
                    esp::ApplyDmaRuntimeCacheProfile(
                        esp::DmaCacheMode::ProcessDiscovery,
                        true);
                    selectedPid = 0;
                    processAttached = false;
                    selectedPidObservedStable = false;
                    moduleFullRefreshIssued = false;
                    esp::SetAttachedCs2ProcessId(0);
                    g::clientBase = 0;
                    g::engine2Base = 0;
                    esp::recovery::ResetProcessIdentityCandidate(processTracker);
                    catalogRefreshFailures = 0;
                    catalogDiscoveryMisses = 0;
                    nextProcessCatalogRefresh = Clock::time_point{};
                    lastDmaReinitialize = now;
                }
            } catch (...) {
                g::clientBase = 0;
                g::engine2Base = 0;
                mem.ResetProcessState();
                processAttached = false;
                selectedPidObservedStable = false;
                esp::SetAttachedCs2ProcessId(0);
                nextAttachAttempt = now + kAttachRetryInterval;
            }

            std::this_thread::sleep_for(kProcessPollInterval);
        }

        esp::ApplyDmaRuntimeCacheProfile(esp::DmaCacheMode::Live, true);
        csReady.store(true, std::memory_order_release);
        animator.request_stop();
        if (animator.joinable()) animator.join();
        DmaSetLogLevel(attachWaitLogLevel);
    }

    if (!app::input::GetPrimaryKeyboardStatus().ready)
        (void)app::input::InitializePrimaryKeyboard();

    console.PrintInfoOk("Waiting for cs2.exe");
    console.PrintBlankLine();

    runtime_offsets::RuntimeResolveReport runtimeResolveReport = {};
    std::string runtimeResolveMessage;
    const bool runtimeResolved = RunWithPendingAnimation(
        console,
        "Runtime offset resolve",
        [&]() {
            return runtime_offsets::ResolveFromAttachedProcess(
                &runtimeResolveReport,
                &runtimeResolveMessage);
        });
    if (runtimeResolved) {
        console.PrintInfoOk("Runtime offset resolve");
    } else {
        console.PrintInfoFail("Runtime offset resolve");
        console.PrintInfoLine(
            "Runtime resolver unavailable; validating local offsets.");
    }
    const std::string runtimeOffsetsSummary = app::localization::Format(
        "Runtime offsets: globals {}/{} | schemas {}/{}",
        runtimeResolveReport.resolvedOffsets,
        runtimeResolveReport.expectedOffsets,
        runtimeResolveReport.resolvedSchemas,
        runtimeResolveReport.expectedSchemas);
    const bool runtimeOffsetsComplete =
        runtimeResolveReport.expectedOffsets > 0 &&
        runtimeResolveReport.resolvedOffsets == runtimeResolveReport.expectedOffsets &&
        runtimeResolveReport.expectedSchemas > 0 &&
        runtimeResolveReport.resolvedSchemas == runtimeResolveReport.expectedSchemas;
    if (runtimeOffsetsComplete)
        console.PrintInfoOk(runtimeOffsetsSummary);
    else
        console.PrintInfoFail(runtimeOffsetsSummary);

    const std::string runtimeVerificationSummary = app::localization::Format(
        "Runtime verification: checks {}/{}",
        runtimeResolveReport.validationChecksPassed,
        runtimeResolveReport.validationChecksAttempted);
    const bool runtimeVerificationComplete =
        runtimeResolveReport.validationPassed &&
        runtimeResolveReport.validationChecksAttempted > 0 &&
        runtimeResolveReport.validationChecksPassed ==
            runtimeResolveReport.validationChecksAttempted;
    if (runtimeVerificationComplete)
        console.PrintInfoOk(runtimeVerificationSummary);
    else
        console.PrintInfoFail(runtimeVerificationSummary);

    if (!runtimeResolved) {
        console.PrintInfoLine(app::localization::Format(
            "Runtime scan: modules {} | sections {} | unreadable pages {} | duplicate matches {} | {:.0f} ms",
            runtimeResolveReport.modulesRead,
            runtimeResolveReport.executableSectionsRead,
            runtimeResolveReport.unreadableCodePages,
            runtimeResolveReport.duplicatePatterns,
            runtimeResolveReport.elapsedMs));
        if (!runtimeResolveReport.detail.empty()) {
            console.PrintInfoLine(app::localization::Format(
                "Runtime resolve detail: {}",
                runtimeResolveReport.detail));
        }
    }

    std::string offsetSanityMessage;
    bool offsetsSane = runtime_offsets::SanityCheckOffsets(
        &offsetSanityMessage);
    if (!offsetsSane) {
        std::string fallbackMessage;
        runtime_offsets::AutoUpdateReport fallbackReport = {};
        const bool fallbackUpdated = RunWithPendingAnimation(
            console,
            "GitHub offset fallback",
            [&]() {
                return SafeAutoUpdateOffsets(
                    &fallbackMessage,
                    &fallbackReport,
                    true);
            });
        bool fallbackLoaded = false;
        if (fallbackUpdated)
            fallbackLoaded = SafeLoadOffsets(&fallbackMessage);
        if (fallbackLoaded) {
            offsetsSane = runtime_offsets::SanityCheckOffsets(
                &offsetSanityMessage);
        }
        if (offsetsSane) {
            console.PrintInfoOk("GitHub offset fallback");
        } else {
            console.PrintInfoFail("GitHub offset fallback");
            if (!fallbackMessage.empty())
                console.PrintInfoLine(fallbackMessage);
        }
    }

    if (offsetsSane && !runtimeResolved) {
        console.PrintInfoOk("Offset sanity check passed");
    } else if (!offsetsSane) {
        console.PrintErrorLine(app::localization::Format(
            "Offset sanity check failed: {}",
            offsetSanityMessage));
        console.PrintErrorLine(
            "Validated offsets unavailable after runtime, local, and GitHub fallback checks.");
        WaitForExitAcknowledge(console);
        return 1;
    }

    console.PrintBlankLine();

    std::string overlayError;
    if (!overlay::Create(g::screenWidth, g::screenHeight, &overlayError)) {
        console.PrintErrorLine("D3D11 overlay creation failed:");
        if (!overlayError.empty()) {
            console.PrintErrorLine(overlayError);
        }
        WaitForExitAcknowledge(console);
        return 1;
    }

    struct ShutdownGuard {
        ~ShutdownGuard()
        {
            target::Shutdown();
            esp::StopDataWorker();
            webradar::Shutdown();
            app::input::Shutdown();
            config::FlushAsyncSaves();
            overlay::Destroy();
        }
    } shutdownGuard;

    esp::StartDataWorker();
    target::Start();
    WaitForInitialEspSnapshot();
    webradar::Initialize();
    console.PrintInfoOk("The system is initialized and ready to work");
    console.PrintBlankLine();

    int exitCode = 0;
    try {
        overlay::Run();
    }
    catch (const std::exception& ex) {
        console.PrintErrorLine(app::localization::Format(
            "Fatal runtime error: {}",
            ex.what()));
        bootstrap::WriteCrashLog("FATAL_MAIN_EXCEPTION", ex.what(), nullptr);
        exitCode = 1;
        WaitForExitAcknowledge(console);
    }
    catch (...) {
        console.PrintErrorLine("Fatal runtime error");
        bootstrap::WriteCrashLog("FATAL_MAIN_EXCEPTION", "Unknown non-standard exception", nullptr);
        exitCode = 1;
        WaitForExitAcknowledge(console);
    }

    return exitCode;
}

int main(int argc, char* argv[]) noexcept
{
    try {
        return RunApplication(argc, argv);
    } catch (const std::exception& ex) {
        try {
            bootstrap::WriteCrashLog("FATAL_STARTUP_EXCEPTION", ex.what(), nullptr);
        } catch (...) {
            app::diagnostics::WriteFallbackError(
                "Unable to write startup crash log");
        }
        app::diagnostics::WriteFallbackError("Fatal startup error", ex.what());
        return 1;
    } catch (...) {
        try {
            bootstrap::WriteCrashLog(
                "FATAL_STARTUP_EXCEPTION",
                "Unknown non-standard exception",
                nullptr);
        } catch (...) {
            app::diagnostics::WriteFallbackError(
                "Unable to write startup crash log");
        }
        app::diagnostics::WriteFallbackError("Fatal startup error");
        return 1;
    }
}

