#pragma once

#include "Features/ESP/esp.h"

namespace esp::recovery
{
    enum class ResetTrigger {
        UserRequestedRefresh = 0,
        ZeroPlayersRepair,
        ZeroPlayersFull,
        PopulationWatchdogStaleCommitted,
        RequiredReadsPersistent,
        DmaRecoverySuccess,
        WaitForProcess,
        SceneTransition,
        LegacyHardReset,
    };

    struct ResetPolicyDecision {
        RuntimeResetKind kind = RuntimeResetKind::Hard;
        bool publishClearedSnapshot = true;
        const char* reason = "runtime_reset";
    };

    inline ResetPolicyDecision EvaluateResetPolicy(ResetTrigger trigger)
    {
        switch (trigger) {
        case ResetTrigger::UserRequestedRefresh:
            return { RuntimeResetKind::Soft, false, "user_requested_refresh" };
        case ResetTrigger::ZeroPlayersRepair:
            return { RuntimeResetKind::Soft, false, "zero_players_repair" };
        case ResetTrigger::ZeroPlayersFull:
            return { RuntimeResetKind::Soft, false, "zero_players_full" };
        case ResetTrigger::PopulationWatchdogStaleCommitted:
            return { RuntimeResetKind::Soft, false, "population_watchdog_stale_committed_players" };
        case ResetTrigger::RequiredReadsPersistent:
            return { RuntimeResetKind::Hard, true, "required_reads_persistent" };
        case ResetTrigger::DmaRecoverySuccess:
            return { RuntimeResetKind::Hard, true, "dma_recovery_success" };
        case ResetTrigger::WaitForProcess:
            return { RuntimeResetKind::Hard, true, "wait_for_process" };
        case ResetTrigger::SceneTransition:
            return { RuntimeResetKind::Hard, true, "scene_transition" };
        case ResetTrigger::LegacyHardReset:
        default:
            return { RuntimeResetKind::Hard, true, "runtime_reset" };
        }
    }

}
