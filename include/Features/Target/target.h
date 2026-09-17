#pragma once

#include <cstdint>

namespace target
{
    enum class RuntimePhase : uint8_t
    {
        Disabled,
        InputUnavailable,
        DataUnavailable,
        Ready,
        WaitingForKey,
        NoTarget,
        Tracking,
        OutputFailed,
    };

    struct SelectionDiagnostics
    {
        int enemies = 0;
        int stale = 0;
        int outsideFov = 0;
        int visibilityRejected = 0;
        int missingBallistics = 0;
        int damageRejected = 0;
    };

    struct ShotDiagnostics
    {
        int targetSlot = -1;
        int healthBefore = -1;
        int healthAfter = -1;
        bool weaponConfirmed = false;
        uint8_t outcome = 0; // Pending, DeadOrGone, AliveConfirmed; not kill attribution.
    };

    enum class FireBlockReason : uint8_t
    {
        Inactive, NoTarget, Aligning, AwaitingView, StaleLocal, StaleTarget,
        WeaponNotReady, ManualFire, ShotPending, Cooldown, ActivationSpent,
        Stabilizing, Delay, Hitchance, SeedWindow, CapsuleMiss, PlayerOccluded,
        ArmorStale, WorldUnavailable, WallBlocked, DamageTooLow, Ready, Queued,
        OutputFailed,
    };

    // Stable diagnostic codes shared by the menu, log and runtime tests.
    inline const char* FireBlockReasonName(FireBlockReason reason) noexcept
    {
        switch (reason) {
        case FireBlockReason::Inactive: return "inactive";
        case FireBlockReason::NoTarget: return "no_target";
        case FireBlockReason::Aligning: return "aligning";
        case FireBlockReason::AwaitingView: return "awaiting_view";
        case FireBlockReason::StaleLocal: return "stale_local";
        case FireBlockReason::StaleTarget: return "stale_target";
        case FireBlockReason::WeaponNotReady: return "weapon_not_ready";
        case FireBlockReason::ManualFire: return "manual_fire";
        case FireBlockReason::ShotPending: return "shot_pending";
        case FireBlockReason::Cooldown: return "cooldown";
        case FireBlockReason::ActivationSpent: return "activation_spent";
        case FireBlockReason::Stabilizing: return "stabilizing";
        case FireBlockReason::Delay: return "delay";
        case FireBlockReason::Hitchance: return "hitchance";
        case FireBlockReason::SeedWindow: return "seed_window";
        case FireBlockReason::CapsuleMiss: return "capsule_miss";
        case FireBlockReason::PlayerOccluded: return "player_occluded";
        case FireBlockReason::ArmorStale: return "armor_stale";
        case FireBlockReason::WorldUnavailable: return "world_unavailable";
        case FireBlockReason::WallBlocked: return "wall_blocked";
        case FireBlockReason::DamageTooLow: return "damage_too_low";
        case FireBlockReason::Ready: return "ready";
        case FireBlockReason::Queued: return "queued";
        case FireBlockReason::OutputFailed: return "output_failed";
        }
        return "unknown";
    }

    struct FireDiagnostics
    {
        FireBlockReason reason = FireBlockReason::Inactive;
        bool hitchanceEnabled = true;
        bool seedWindowEnabled = true;
        // Negative values mean the stage was NOT evaluated, never a miss/zero damage.
        float hitchancePercent = -1.0f;
        float centeredHitchancePercent = -1.0f;
        float requiredHitchancePercent = 0.0f;
        float damage = -1.0f;
        float requiredDamage = 0.0f;
        float seedHitPercent = -1.0f;
        float inaccuracy = 0.0f;
        float spread = 0.0f;
    };

    struct RuntimeStatus
    {
        RuntimePhase phase = RuntimePhase::Disabled;
        int targetSlot = -1;
        float targetDistancePx = 0.0f;
        int moveX = 0;
        int moveY = 0;
        bool aimKeyDown = false;
        bool triggerKeyDown = false;
        bool pausedByMenu = false;
        SelectionDiagnostics aimSelection;
        SelectionDiagnostics triggerSelection;
        ShotDiagnostics shot;
        FireDiagnostics fire;
        // Populated when data freshness blocks a tick; no repeating terminal log.
        int64_t snapshotAgeUs = -1;
        int64_t viewAgeUs = -1;
        int64_t eyeAgeUs = -1;
        uint64_t updatedAtUs = 0;
    };

    void Start();
    void DrawOverlay();
    RuntimeStatus GetRuntimeStatus();
    void Shutdown();
}
