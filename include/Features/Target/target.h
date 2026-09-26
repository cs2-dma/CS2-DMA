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
        Reacting,
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
        OutputFailed, OutputBusy, CapsuleEdge, SeedClockUnavailable,
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
        case FireBlockReason::SeedClockUnavailable: return "seed_clock_unavailable";
        case FireBlockReason::CapsuleMiss: return "capsule_miss";
        case FireBlockReason::CapsuleEdge: return "capsule_edge";
        case FireBlockReason::PlayerOccluded: return "player_occluded";
        case FireBlockReason::ArmorStale: return "armor_stale";
        case FireBlockReason::WorldUnavailable: return "world_unavailable";
        case FireBlockReason::WallBlocked: return "wall_blocked";
        case FireBlockReason::DamageTooLow: return "damage_too_low";
        case FireBlockReason::Ready: return "ready";
        case FireBlockReason::Queued: return "queued";
        case FireBlockReason::OutputFailed: return "output_failed";
        case FireBlockReason::OutputBusy: return "output_busy";
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

    enum class MoveBlockReason : uint8_t
    {
        None, EyeUnavailable, InvalidDirection, InvalidAngles, InvalidMouseDelta, ViewExpired, OutputRejected,
        AwaitingView, OutputBusy, RecoilUnavailable, QuantizationLimit
    };

    inline const char* MoveBlockReasonName(MoveBlockReason reason) noexcept
    {
        switch (reason) {
        case MoveBlockReason::None: return "none";
        case MoveBlockReason::EyeUnavailable: return "eye_unavailable";
        case MoveBlockReason::InvalidDirection: return "invalid_direction";
        case MoveBlockReason::InvalidAngles: return "invalid_angles";
        case MoveBlockReason::InvalidMouseDelta: return "invalid_mouse_delta";
        case MoveBlockReason::ViewExpired: return "view_expired";
        case MoveBlockReason::OutputRejected: return "output_rejected";
        case MoveBlockReason::AwaitingView: return "awaiting_view";
        case MoveBlockReason::OutputBusy: return "output_busy";
        case MoveBlockReason::RecoilUnavailable: return "recoil_unavailable";
        case MoveBlockReason::QuantizationLimit: return "quantization_limit";
        }
        return "unknown";
    }

    struct MoveDiagnostics
    {
        MoveBlockReason reason = MoveBlockReason::None;
        bool eyeValid = false;
        int64_t eyeAgeUs = -1;
        float angularErrorDegrees = -1.0f;
        int64_t viewAgeUs = -1;
        uint64_t validForUs = 0;
        uint16_t weaponId = 0;
        bool recoilAvailable = false;
        int64_t recoilAgeUs = -1;
        float recoilPitchStep = 0.0f;
        float recoilYawStep = 0.0f;
        bool assistanceResting = false;
        float pointX = 0.0f;
        float pointY = 0.0f;
        float pointZ = 0.0f;
        float predictionZ = 0.0f;
        float eyeZ = 0.0f;
        float rawPunchPitch = 0.0f;
        float rawPunchYaw = 0.0f;
        int shotsFired = 0;
        int pointHitbox = -1;
        float recoilScale = 2.0f;
    };

    struct GeometryCapabilities
    {
        bool evaluated = false;
        bool ready = false;
        bool aimVisibilityDeferred = false;
        bool triggerVisibilityDeferred = false;
        bool aimDamageDeferred = false;
        bool aimAutowallActive = false;
        bool triggerAutowallActive = false;
    };

    struct OutputFeedbackDiagnostics
    {
        bool pending = false;
        int64_t ageUs = -1;
        float expectedPitch = 0.0f;
        float expectedYaw = 0.0f;
        float observedPitch = 0.0f;
        float observedYaw = 0.0f;
        uint64_t timeouts = 0;
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
        MoveDiagnostics aimMove;
        MoveDiagnostics triggerMove;
        GeometryCapabilities geometry;
        OutputFeedbackDiagnostics outputFeedback;
        uint64_t workUs = 0;
        uint64_t recentPeakWorkUs = 0;
        uint64_t workSamples = 0;
        uint64_t workWindowUs = 0;
        uint64_t overBudgetSamples = 0;
        // Populated when data freshness blocks a tick; no repeating terminal log.
        int64_t snapshotAgeUs = -1;
        int64_t viewAgeUs = -1;
        int64_t eyeAgeUs = -1;
        uint64_t updatedAtUs = 0;
        uint16_t weaponId = 0;
        int weaponProfile = -1;
    };

    void Start();
    void DrawOverlay();
    RuntimeStatus GetRuntimeStatus();
    void Shutdown();
}
