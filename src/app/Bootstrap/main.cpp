#include <Windows.h>
#include <timeapi.h>
#include <shellapi.h>
#include <objbase.h>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "ole32.lib")
#include <DMALibrary/Memory/Memory.h>

#include "app/Bootstrap/crash_handler.h"
#include "app/Bootstrap/runtime_console.h"
#include "app/Bootstrap/startup_offsets.h"
#include "app/Bootstrap/startup_process.h"
#include "app/Bootstrap/startup_update.h"
#include "app/Bootstrap/version_update.h"
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
#include <condition_variable>
#include <conio.h>
#include <cstring>
#include <format>
#include <iostream>
#include <mutex>
#include <optional>
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
        console.PrintInfoLine(app::build_info::VersionTag() + " | " +
            app::localization::Get("Menu: P | Screen F2"));
        console.PrintInfoLine("Telegram: @ne_sravnim | Discord: Corakevq");
    }

    std::optional<std::string> ReadStartupReply()
    {
        const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
        DWORD mode = 0;
        if (!GetConsoleMode(input, &mode)) return {};
        struct InputGuard {
            HANDLE input;
            DWORD mode;
            HANDLE output = GetStdHandle(STD_OUTPUT_HANDLE);
            CONSOLE_CURSOR_INFO cursor{};
            bool haveCursor = false;
            ~InputGuard() {
                SetConsoleMode(input, mode);
                if (haveCursor) SetConsoleCursorInfo(output, &cursor);
            }
        } guard{input, mode};
        SetConsoleMode(input, mode | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
        guard.haveCursor = GetConsoleCursorInfo(guard.output, &guard.cursor) != FALSE;
        if (guard.haveCursor) {
            auto visibleCursor = guard.cursor;
            visibleCursor.bVisible = TRUE;
            SetConsoleCursorInfo(guard.output, &visibleCursor);
        }
        std::string reply;
        if (!std::getline(std::cin, reply)) return {};
        return reply;
    }

    bool OpenReleaseUrl(const std::string& url)
    {
        const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const std::wstring wideUrl(url.begin(), url.end());
        const auto result = ShellExecuteW(nullptr, L"open", wideUrl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        if (SUCCEEDED(initialized)) CoUninitialize();
        return reinterpret_cast<INT_PTR>(result) > 32;
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

        std::mutex animationMutex;
        std::condition_variable_any animationWake;
        std::jthread animator([&](const std::stop_token& stopToken) noexcept {
            try {
                int phase = 0;
                while (!stopToken.stop_requested()) {
                    console.PrintPending(label, text, phase++);
                    std::unique_lock waitLock(animationMutex);
                    animationWake.wait_for(waitLock, stopToken,
                        std::chrono::milliseconds(90), [] { return false; });
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

    struct ReleaseCheckGuard {
        ~ReleaseCheckGuard() { app::updates::Shutdown(); }
    } releaseCheckGuard;

    console.PrintStartupBanner();
    console.PrintInfoLine("KevqDMA");

    if (offsetsSelfTest) {
        console.PrintInfoOk("Connection");
    } else {
        const auto release = RunWithPendingAnimation(console, "Connection", [] {
            app::updates::Start();
            return app::updates::WaitForInitialCheck();
        });
        bootstrap::StartupUpdateHooks hooks;
        hooks.connectionOk = [&] { console.PrintInfoOk("Connection"); };
        hooks.connectionQuestion = [&] { console.PrintInfoQuestion("Connection"); };
        hooks.info = [&](const std::string& text) { console.PrintInfoLine(text); };
        hooks.readReply = ReadStartupReply;
        hooks.openUrl = OpenReleaseUrl;
        hooks.exitCountdown = [&] {
            app::updates::Shutdown();
            const auto closesAt = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            for (int seconds = 5; seconds > 0; --seconds) {
                console.PrintInfoPending(app::localization::Format("Closing KevqDMA in {} seconds", seconds), 0);
                std::this_thread::sleep_until(closesAt - std::chrono::seconds(seconds - 1));
            }
            console.PrintInfoOk("Update handoff");
        };
        if (bootstrap::HandleStartupUpdate(release, hooks) == bootstrap::StartupUpdateResult::Exit)
            return 0;
    }
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

    if (!esp::ApplyDmaRuntimeCacheProfile(esp::DmaCacheMode::ProcessDiscovery, true))
        console.PrintLine("DMA", "Cache profile could not be verified; default refresh timing remains active.");

    config::Load();

    {
        const DmaLogLevel attachWaitLogLevel = DmaGetLogLevel();
        DmaSetLogLevel(DmaLogLevel::Silent);

        std::atomic<bool> csReady{false};
        std::atomic<bool> csCanceled{false};

        console.PrintInfoPending("Waiting for cs2.exe", 0);
        std::mutex animationMutex;
        std::condition_variable_any animationWake;
        std::jthread animator([&](const std::stop_token& stopToken) noexcept {
            try {
                int phase = 1;
                while (!stopToken.stop_requested() &&
                       !csReady.load(std::memory_order_acquire) &&
                       !csCanceled.load(std::memory_order_acquire)) {
                    console.PrintInfoPending("Waiting for cs2.exe", phase++);
                    std::unique_lock waitLock(animationMutex);
                    animationWake.wait_for(waitLock, stopToken,
                        std::chrono::milliseconds(90), [] { return false; });
                }
            } catch (...) {
                app::diagnostics::WriteFallbackError(
                    "Process-wait animation stopped unexpectedly");
            }
        });

        
        
        using Clock = std::chrono::steady_clock;

        esp::recovery::ProcessIdentityTracker processTracker = {};
        DWORD selectedPid = 0;
        bool processAttached = false;
        bool selectedPidObservedStable = false;
        bool moduleFullRefreshIssued = false;
        uint32_t catalogRefreshFailures = 0;
        uint32_t catalogDiscoveryMisses = 0;
        bootstrap::ProcessWaitSchedule schedule;

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

            
            
            
            auto now = Clock::now();
            try {
                bool moduleResolveTimedOut = false;
                bool catalogUnavailable = false;
                if (schedule.PollDue(now)) {
                    bool catalogRefreshAttempted = false;
                    bool catalogRefreshed = false;
                    // Startup has no latency-sensitive render/data workers yet.
                    // Keep the remote process catalog current until both target
                    // modules are validated; otherwise a successful-but-stale
                    // lookup can pin the wait loop to an obsolete PID forever.
                    if (now >= schedule.nextProcessCatalogRefresh) {
                        catalogRefreshAttempted = true;
                        catalogRefreshed = mem.vHandle &&
                            VMMDLL_ConfigSet(
                                mem.vHandle,
                                VMMDLL_OPT_REFRESH_FREQ_MEDIUM,
                                1);
                        catalogRefreshFailures =
                            catalogRefreshed
                                ? 0u
                                : (std::min)(catalogRefreshFailures + 1u,
                                    bootstrap::kProcessCatalogFailureLimit);
                        if (!catalogRefreshed)
                            selectedPidObservedStable = false;
                        schedule.nextProcessCatalogRefresh =
                            Clock::now() + bootstrap::kProcessCatalogRefreshInterval;
                    }

                    DWORD observedPid =
                        mem.vHandle ? mem.GetPidFromName("cs2.exe") : 0;
                    if (observedPid != 0) {
                        catalogDiscoveryMisses = 0;
                    } else if (catalogRefreshAttempted) {
                        ++catalogDiscoveryMisses;
                        if (catalogRefreshed &&
                            catalogDiscoveryMisses %
                                bootstrap::kProcessCatalogFullRefreshMisses == 0u) {
                            VMMDLL_ConfigSet(
                                mem.vHandle,
                                VMMDLL_OPT_REFRESH_ALL,
                                1);
                            observedPid = mem.GetPidFromName("cs2.exe");
                            if (observedPid != 0) {
                                catalogDiscoveryMisses = 0;
                            }
                        }
                    }
                    if (catalogRefreshAttempted &&
                        catalogDiscoveryMisses >= bootstrap::kProcessCatalogHealthProbeMisses) {
                        size_t processCount = 0;
                        catalogUnavailable = !mem.vHandle ||
                            !VMMDLL_PidList(mem.vHandle, nullptr, &processCount) || processCount == 0;
                        catalogDiscoveryMisses = 0;
                    }
                    now = Clock::now();
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
                        schedule.nextAttachAttempt = now;
                    }

                    schedule.nextProcessPoll = now + bootstrap::kProcessPollInterval;
                }

                if (selectedPid != 0 &&
                    !processAttached &&
                    now >= schedule.nextAttachAttempt) {
                    mem.ResetProcessState();
                    if (mem.vHandle &&
                        mem.AttachToProcessId("cs2.exe", selectedPid, true)) {
                        processAttached = true;
                        now = Clock::now();
                        schedule.attachedAt = now;
                        schedule.nextModuleResolve = now;
                        moduleFullRefreshIssued = false;
                        esp::SetAttachedCs2ProcessId(selectedPid);
                    } else {
                        now = Clock::now();
                        schedule.nextAttachAttempt = now + bootstrap::kAttachRetryInterval;
                    }
                }

                if (processAttached && now >= schedule.nextModuleResolve) {
                    g::clientBase = mem.GetModuleBase("client.dll");
                    g::engine2Base = mem.GetModuleBase("engine2.dll");
                    uint16_t clientMz = 0;
                    uint16_t engine2Mz = 0;
                    bool modulesReady =
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
                    now = Clock::now();
                    if (modulesReady && now >= schedule.nextProcessPoll) {
                        const bool catalogRefreshed = mem.vHandle &&
                            VMMDLL_ConfigSet(mem.vHandle, VMMDLL_OPT_REFRESH_FREQ_MEDIUM, 1);
                        catalogRefreshFailures = catalogRefreshed ? 0u :
                            (std::min)(catalogRefreshFailures + 1u, bootstrap::kProcessCatalogFailureLimit);
                        modulesReady = catalogRefreshed && mem.GetPidFromName("cs2.exe") == selectedPid;
                        selectedPidObservedStable = modulesReady;
                        now = Clock::now();
                        schedule.nextProcessPoll = now + bootstrap::kProcessPollInterval;
                        schedule.nextProcessCatalogRefresh = now + bootstrap::kProcessCatalogRefreshInterval;
                    }
                    if (modulesReady)
                        break;

                    g::clientBase = 0;
                    g::engine2Base = 0;
                    const auto moduleWaitAge = now - schedule.attachedAt;
                    if (!moduleFullRefreshIssued &&
                        moduleWaitAge >= bootstrap::kModuleFullRefreshDelay) {
                        VMMDLL_ConfigSet(mem.vHandle, VMMDLL_OPT_REFRESH_ALL, 1);
                        moduleFullRefreshIssued = true;
                    }
                    if (moduleWaitAge >= bootstrap::kModuleReattachDelay) {
                        mem.ResetProcessState();
                        processAttached = false;
                        esp::SetAttachedCs2ProcessId(0);
                        schedule.nextAttachAttempt = now + bootstrap::kAttachRetryInterval;
                        moduleResolveTimedOut = true;
                    }
                    schedule.nextModuleResolve = Clock::now() + bootstrap::kModuleResolveInterval;
                }

                now = Clock::now();
                if (bootstrap::ShouldReinitializeProcessDiscovery(mem.vHandle != nullptr,
                        processAttached, selectedPid, catalogRefreshFailures,
                        catalogUnavailable, moduleResolveTimedOut) && schedule.CanReinitialize(now)) {
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
                    schedule = {};
                    schedule.lastDmaReinitialize = Clock::now();
                }
            } catch (...) {
                g::clientBase = 0;
                g::engine2Base = 0;
                mem.ResetProcessState();
                processAttached = false;
                selectedPidObservedStable = false;
                esp::SetAttachedCs2ProcessId(0);
                schedule.nextAttachAttempt = Clock::now() + bootstrap::kAttachRetryInterval;
                schedule.nextProcessPoll = Clock::now() + bootstrap::kProcessPollInterval;
                schedule.nextProcessCatalogRefresh = schedule.nextProcessPoll;
            }

            std::this_thread::sleep_until(schedule.NextWake(
                Clock::now(), processAttached, selectedPid != 0));
        }

        esp::ApplyDmaRuntimeCacheProfile(esp::DmaCacheMode::Live, true);
        csReady.store(true, std::memory_order_release);
        animator.request_stop();
        if (animator.joinable()) animator.join();
        DmaSetLogLevel(attachWaitLogLevel);
    }

    console.PrintInfoOk("Waiting for cs2.exe");
    console.PrintBlankLine();

    runtime_offsets::RuntimeResolveReport runtimeResolveReport = {};
    std::string runtimeResolveMessage;
    std::string offsetSanityMessage;
    bool offsetsSane = false;
    const DWORD offsetTargetPid = mem.GetAttachedPid();
    const uintptr_t offsetClientBase = g::clientBase.load(std::memory_order_relaxed);
    const uintptr_t offsetEngineBase = g::engine2Base.load(std::memory_order_relaxed);
    const auto offsetTargetCurrent = [&](const runtime_offsets::ResolveControl* control = nullptr) {
        try {
            const auto checked = [&](auto&& operation) {
                return control ? control->Run(operation) : operation();
            };
            return mem.vHandle && offsetTargetPid != 0 &&
                mem.GetAttachedPid() == offsetTargetPid &&
                checked([&] { return VMMDLL_ConfigSet(mem.vHandle, VMMDLL_OPT_REFRESH_FREQ_MEDIUM, 1); }) &&
                checked([&] { return mem.GetPidFromName("cs2.exe"); }) == offsetTargetPid &&
                checked([&] { return mem.GetModuleBase("client.dll"); }) == offsetClientBase &&
                checked([&] { return mem.GetModuleBase("engine2.dll"); }) == offsetEngineBase;
        } catch (const runtime_offsets::ResolveInterrupted&) {
            throw;
        } catch (...) {
            return false;
        }
    };
    bootstrap::OffsetRecoveryPlan offsetRecovery;
    bool resolveCancelled = false;
    auto nextResolveCancelPoll = std::chrono::steady_clock::time_point{};
    const auto resolutionCancelled = [&] {
        const auto now = std::chrono::steady_clock::now();
        if (!resolveCancelled && now >= nextResolveCancelPoll) {
            resolveCancelled = (GetAsyncKeyState(VK_END) & 1) != 0;
            nextResolveCancelPoll = now + std::chrono::milliseconds(10);
        }
        return resolveCancelled;
    };
    const auto resolveWait = RunWithPendingAnimation(
        console,
        "Runtime offset resolve",
        [&]() {
            return bootstrap::WaitForRuntimeOffsets([&](bool force, auto deadline) {
                offsetsSane = false;
                runtimeResolveReport = {};
                const runtime_offsets::ResolveControl control{
                    (std::min)(deadline, std::chrono::steady_clock::now() + std::chrono::seconds(15)),
                    resolutionCancelled};
                try {
                    control.Check();
                    if (force && mem.vHandle && mem.GetAttachedPid() == offsetTargetPid) {
                        const auto action = offsetRecovery.BeforeRetry(std::chrono::steady_clock::now());
                        const bool recovered = bootstrap::RecoverOffsetReads(action, [&](bootstrap::OffsetCache cache) {
                            const ULONG64 option = cache == bootstrap::OffsetCache::Memory
                                ? VMMDLL_OPT_REFRESH_FREQ_MEM
                                : cache == bootstrap::OffsetCache::Translation
                                    ? VMMDLL_OPT_REFRESH_FREQ_TLB : VMMDLL_OPT_REFRESH_ALL;
                            return control.Run([&] { return VMMDLL_ConfigSet(mem.vHandle, option, 1) != FALSE; });
                        });
                        if (!recovered) {
                            runtimeResolveReport.startupPending = true;
                            runtimeResolveReport.detail = "Startup DMA cache recovery failed.";
                            return bootstrap::OffsetAttempt::Retry;
                        }
                    }
                    if (!offsetTargetCurrent(&control)) {
                        runtimeResolveReport.startupPending = true;
                        runtimeResolveReport.detail = "Attached process or modules are no longer ready.";
                        return bootstrap::OffsetAttempt::Retry;
                    }
                    const bool resolved = runtime_offsets::ResolveFromAttachedProcess(
                        &runtimeResolveReport, &runtimeResolveMessage, force, &control);
                    if (runtimeResolveReport.cancelled) resolveCancelled = true;
                    control.Check();
                    if (!offsetTargetCurrent(&control)) {
                        runtimeResolveReport.startupPending = true;
                        runtimeResolveReport.validationPassed = false;
                        runtimeResolveReport.detail = "Attached process changed during offset resolution.";
                        return bootstrap::OffsetAttempt::Retry;
                    }
                    if (resolved) {
                        offsetsSane = runtime_offsets::SanityCheckOffsets(&offsetSanityMessage, &control);
                        if (offsetsSane) return bootstrap::OffsetAttempt::Ready;
                        runtimeResolveReport.startupPending = true;
                        runtimeResolveReport.validationPassed = false;
                        runtimeResolveReport.detail = offsetSanityMessage;
                    }
                    return runtimeResolveReport.startupPending
                        ? bootstrap::OffsetAttempt::Retry : bootstrap::OffsetAttempt::Failed;
                } catch (const runtime_offsets::ResolveInterrupted& interrupted) {
                    if ((!runtimeResolveReport.timedOut && !runtimeResolveReport.cancelled) ||
                        runtimeResolveReport.detail.empty()) runtimeResolveReport.detail = interrupted.what();
                    resolveCancelled = resolveCancelled || interrupted.Cancelled();
                    runtimeResolveReport.startupPending = !resolveCancelled;
                    runtimeResolveReport.validationPassed = false;
                    runtimeResolveReport.timedOut = !resolveCancelled;
                    runtimeResolveReport.cancelled = resolveCancelled;
                    return bootstrap::OffsetAttempt::Retry;
                } catch (const std::exception& error) {
                    runtimeResolveReport.startupPending = true;
                    runtimeResolveReport.validationPassed = false;
                    runtimeResolveReport.detail = error.what();
                    return bootstrap::OffsetAttempt::Retry;
                }
            }, [] { return std::chrono::steady_clock::now(); },
                [](std::chrono::milliseconds delay) { std::this_thread::sleep_for(delay); },
                resolutionCancelled);
        });
    if (resolveWait == bootstrap::OffsetWaitResult::Cancelled) {
        console.PrintErrorLine("Canceled while waiting for cs2.exe");
        return 1;
    }
    if (resolveWait == bootstrap::OffsetWaitResult::TimedOut) {
        runtimeResolveReport.timedOut = true;
        runtimeResolveReport.validationPassed = false;
        runtimeResolveReport.detail += " Startup retry budget exhausted.";
    }
    const bool runtimeResolved = resolveWait == bootstrap::OffsetWaitResult::Ready;
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

    if (!offsetTargetCurrent()) {
        console.PrintErrorLine("Runtime resolver requires an attached CS2 process and loaded modules.");
        WaitForExitAcknowledge(console);
        return 1;
    }
    offsetsSane = runtime_offsets::SanityCheckOffsets(&offsetSanityMessage);
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
            offsetsSane = offsetTargetCurrent() && runtime_offsets::SanityCheckOffsets(
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
    if (!app::input::GetPrimaryKeyboardStatus().ready)
        (void)app::input::InitializePrimaryKeyboard();

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

