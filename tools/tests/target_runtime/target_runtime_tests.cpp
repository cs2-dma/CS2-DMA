// Execute the actual Target tick/motion implementation with deterministic
// snapshots and fake transport. Never attach DMA, spawn workers or send input.
#include "Features/ESP/esp.h"
#include "app/Input/input_device.h"
#include "app/Input/primary_keyboard.h"
#include <iostream>
#pragma warning(push)
#pragma warning(disable: 4200) // Upstream C vendor ABI uses flexible array members.
#include "vendor/DMALibrary/pch.h"
#pragma warning(pop)

namespace fixture {
    esp::TargetSnapshot snapshot;
    bool haveSnapshot = true;
    bool keyDown = false;
    bool keyboardAvailable = true;
    bool acceptMove = true;
    int moves = 0;
    int clicks = 0;
    int mapRequests = 0;
    bool geometryAvailable = true;
    int logs = 0;
    app::input::LeftClickStatus click;
}

#include "../../../src/Features/Target/target.cpp"

void DmaLogPrintf(const char*, ...) { ++fixture::logs; }
bool esp::GetTargetSnapshot(TargetSnapshot* out) { *out = fixture::snapshot; return fixture::haveSnapshot; }
app::input::DeviceStatus app::input::GetDeviceStatus() {
    DeviceStatus result;
    result.state = ConnectionState::Connected;
    return result;
}
app::input::PrimaryKeyboardStatus app::input::GetPrimaryKeyboardStatus() {
    return {fixture::keyboardAvailable, 1, 0, 1};
}
bool app::input::IsActivationKeyDown(int key) { return key == 0x06 && fixture::keyDown; }
app::input::KeyState app::input::ReadActivationKeyState(int key) {
    return {fixture::keyboardAvailable, key == 0x06 && fixture::keyDown};
}
app::input::LeftClickTiming app::input::GetLeftClickTiming() { return {}; }
app::input::LeftClickStatus app::input::GetLeftClickStatus() { return fixture::click; }
bool app::input::RequestMove(int, int) {
    if (!fixture::acceptMove) return false;
    ++fixture::moves; return true;
}
bool app::input::RequestLeftClick(uint32_t) {
    if (fixture::click.active) return false;
    ++fixture::clicks; fixture::click = {true, true, 1}; return true;
}
bool app::input::RequestLeftButton(bool down) { if (!down) fixture::click = {}; return true; }
target::convars::Values target::convars::Read() { return {}; }
void target::convars::Reset() {}
void target::physics::RequestForMap(const char*) { ++fixture::mapRequests; }
bool target::physics::IsLineVisible(const char*, const Vector3&, const Vector3&, float) { return true; }
bool target::physics::TracePenetrationSegments(const char*, const Vector3&, const Vector3&,
    std::vector<PenetrationSegment>& segments) { segments.clear(); return fixture::geometryAvailable; }
target::physics::Stats target::physics::GetStats() { Stats result; result.state = BuildState::Ready; return result; }
void target::physics::Shutdown() {}

namespace {
    int failures = 0;
    void Check(bool condition, const char* expression, int line) {
        if (!condition) { ++failures; std::cerr << line << ": " << expression << '\n'; }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    void FreshFrame() {
        auto& snap = fixture::snapshot;
        snap.sampledAtUs += 1000;
        snap.captureTimeUs = snap.sampledAtUs;
        snap.viewUpdatedAtUs = snap.sampledAtUs;
        snap.localEyeUpdatedAtUs = snap.sampledAtUs;
        snap.localWeaponUpdatedAtUs = snap.sampledAtUs;
        snap.localWeaponTelemetryUpdatedAtUs = snap.sampledAtUs;
        snap.localAmmoUpdatedAtUs = snap.sampledAtUs;
        snap.localShotsUpdatedAtUs = snap.sampledAtUs;
        snap.localAimPunchUpdatedAtUs = snap.sampledAtUs;
        for (auto& player : snap.players) if (player.pawn) {
            player.coreUpdatedAtUs = snap.sampledAtUs;
            player.bonesUpdatedAtUs = snap.sampledAtUs;
            player.hitboxesUpdatedAtUs = snap.sampledAtUs;
            player.visibilityUpdatedAtUs = snap.sampledAtUs;
        }
    }

    g::TargetSettings ResetFixture() {
        ResetRuntimeState();
        s_aimActivation = {}; s_triggerActivation = {};
        s_triggerActivationShotIssued = false;
        s_activationScene = 0; s_activationPawn = 0;
        s_shotDiagnostics = {}; s_status = {};
        fixture::snapshot = {}; fixture::haveSnapshot = true;
        fixture::keyDown = false; fixture::keyboardAvailable = true;
        fixture::acceptMove = true; fixture::moves = 0; fixture::clicks = 0;
        fixture::click = {}; fixture::mapRequests = 0;
        fixture::geometryAvailable = true;
        s_fireDiagnostics = {};
        auto& snap = fixture::snapshot;
        snap.sampledAtUs = NowUs(); snap.sceneSerial = 1;
        snap.localPawn = 0x10000; snap.localTeam = 2; snap.localPlayerIndex = 0;
        snap.localWeaponId = 7; snap.localWeaponHandle = 0x10003; snap.localWeaponEntity = 0x30000;
        snap.localEyePos = {0, 0, 64}; snap.localEyeValid = true; snap.viewValid = true;
        snap.localAmmoClip = 30; snap.localAmmoValid = true;
        snap.localShotsFiredValid = true; snap.localAimPunchValid = true;
        snap.localWeaponTelemetryValid = true; snap.localWeaponReady = true;
        snap.localWeaponDamage = 40; snap.localWeaponRange = 8192;
        snap.localWeaponRangeModifier = 0.98f; snap.localWeaponHeadshotMultiplier = 4;
        snap.localWeaponArmorRatio = 1.5f; snap.localWeaponPenetration = 2;
        snap.localIntervalPerTick = 1.0f / 64; snap.localRenderTick = 6400;
        snap.localCurrentTime = 100; snap.localLastShotTime = 99;
        snap.viewMatrix[0][1] = 1; snap.viewMatrix[1][2] = 1;
        snap.viewMatrix[1][3] = -64; snap.viewMatrix[3][0] = 1;
        auto& enemy = snap.players[1];
        enemy.valid = true; enemy.pawn = 0x20000; enemy.pawnHandle = 0x10002;
        enemy.health = 100; enemy.team = 3; enemy.visible = true;
        enemy.hasBones = true; enemy.hasHitboxes = true; enemy.hitboxCount = 1;
        enemy.hitboxes[0] = {{1000, 50, 62}, {1000, 50, 66}, {1000, 50, 64}, 4, 7, 0, 1, true};
        for (auto& bone : enemy.bones) bone = enemy.hitboxes[0].center;
        FreshFrame();
        g::TargetSettings settings;
        settings.enabled = true; settings.aimbotEnabled = true;
        settings.aimActivationMode = 1; settings.aimPredictive = false;
        settings.aimHumanization = false;
        settings.weaponProfiles[1].aimSmoothing = 1;
        return settings;
    }

    void Tick(const g::TargetSettings& settings, bool menu = false) {
        FreshFrame(); TickTarget(1920, 1080, settings, menu);
    }

    void TestToggleLifecycle() {
        auto settings = ResetFixture();
        fixture::keyDown = true; Tick(settings);
        CHECK(s_aimActivation.toggled); CHECK(fixture::moves > 0);
        CHECK(s_status.phase == target::RuntimePhase::Tracking);
        fixture::keyDown = false; Tick(settings); CHECK(s_aimActivation.toggled);
        const int beforeGap = fixture::moves;
        fixture::haveSnapshot = false; Tick(settings);
        CHECK(s_aimActivation.toggled); CHECK(fixture::moves == beforeGap);
        fixture::haveSnapshot = true; Tick(settings); CHECK(fixture::moves > beforeGap);
        fixture::snapshot.localWeaponEntity += 16; Tick(settings); CHECK(s_aimActivation.toggled);
        const int beforeMenu = fixture::moves;
        Tick(settings, true); fixture::keyDown = true; Tick(settings, true);
        CHECK(!s_aimActivation.toggled); CHECK(fixture::moves == beforeMenu);
        Tick(settings); CHECK(!s_aimActivation.toggled); CHECK(fixture::moves == beforeMenu);
        fixture::keyDown = false; Tick(settings);
        fixture::keyDown = true; Tick(settings); CHECK(s_aimActivation.toggled);
        fixture::keyDown = false; Tick(settings);
        fixture::keyDown = true; Tick(settings); CHECK(!s_aimActivation.toggled);
        fixture::keyDown = false; Tick(settings); fixture::keyDown = true; Tick(settings);
        CHECK(s_aimActivation.toggled);
        fixture::keyboardAvailable = false; fixture::keyDown = false; Tick(settings);
        CHECK(s_aimActivation.toggled);
        fixture::keyboardAvailable = true; fixture::keyDown = true; Tick(settings);
        CHECK(s_aimActivation.toggled); // missing input was not a release edge
        fixture::snapshot.localIsDead = true; Tick(settings); CHECK(!s_aimActivation.toggled);
        fixture::snapshot.localIsDead = false; Tick(settings); CHECK(!s_aimActivation.toggled);
    }

    void TestMovementAndSelection() {
        auto settings = ResetFixture();
        settings.aimVisibleOnly = false; settings.weaponProfiles[1].aimAutowall = false;
        fixture::keyDown = true; Tick(settings); CHECK(fixture::mapRequests > 0);
        CHECK(fixture::moves > 0);
        const int sent = fixture::moves;
        TickTarget(1920, 1080, settings, false); CHECK(fixture::moves == sent); // same camera sample
        fixture::snapshot.players[1].hasHitboxes = false; Tick(settings);
        CHECK(s_status.aimSelection.missingBallistics == 1);
        CHECK(s_status.phase == target::RuntimePhase::NoTarget);
        Tick(settings, true); CHECK(s_status.aimSelection.missingBallistics == 1);

        settings = ResetFixture();
        Candidate candidate; candidate.player = &fixture::snapshot.players[1];
        candidate.point = {1000, 1.74533f, 64}; candidate.requiredHitgroup = 1;
        MotionRuntimeState motion; int x = 0, y = 0;
        const auto result = MoveToward(fixture::snapshot, candidate, {}, motion,
            50, false, false, 0.006f, &x, &y);
        CHECK(result == MoveResult::Accumulating); CHECK(x == 0 && y == 0);
        CHECK(motion.mouseRemainderX != 0);
        candidate.point.y = 50; motion = {}; fixture::acceptMove = false;
        CHECK(MoveToward(fixture::snapshot, candidate, {}, motion,
            1, false, false, 0.006f, &x, &y) == MoveResult::Failed);
        CHECK(motion.mouseRemainderX == 0);
    }

    void TestRepeatShotsObeyToggle() {
        auto settings = ResetFixture();
        settings.aimbotEnabled = false; settings.triggerbotEnabled = true;
        settings.triggerAutoShot = true; settings.triggerActivationMode = 1;
        Tick(settings);
        CHECK(!s_status.triggerKeyDown); CHECK(fixture::moves == 0); CHECK(fixture::clicks == 0);
        fixture::keyDown = true; Tick(settings);
        CHECK(s_status.triggerKeyDown); CHECK(fixture::moves > 0);
        fixture::keyDown = false; Tick(settings);
        fixture::keyDown = true; Tick(settings);
        CHECK(!s_status.triggerKeyDown);
        const int movesAtOff = fixture::moves;
        for (int i = 0; i < 20; ++i) Tick(settings);
        CHECK(fixture::moves == movesAtOff); CHECK(fixture::clicks == 0);
        CHECK(!s_runtime.trigger.waiting);
        Tick(settings, true); CHECK(!s_status.triggerKeyDown);
    }

    void TestSingleShotBudget() {
        auto settings = ResetFixture();
        settings.aimbotEnabled = false; settings.triggerbotEnabled = true;
        settings.triggerActivationMode = 1; settings.triggerDelayMs = 0;
        settings.triggerAutoShot = false;
        auto& head = fixture::snapshot.players[1].hitboxes[0];
        head.start.y = head.center.y = head.end.y = 0;
        fixture::keyDown = true;
        auto settle = [&] {
            for (int frame = 0; frame < 20; ++frame) {
                Tick(settings);
                s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
            }
        };
        settle(); CHECK(fixture::clicks == 1); CHECK(s_triggerActivationShotIssued);
        // A runtime reset (e.g. weapon context) must not bypass single-shot mode.
        ResetRuntimeState(); fixture::click = {};
        settle(); CHECK(fixture::clicks == 1);
        settings.triggerAutoShot = true;
        settle(); CHECK(fixture::clicks == 2);
        fixture::keyDown = false; Tick(settings);
        fixture::keyDown = true; Tick(settings);
        settle(); CHECK(fixture::clicks == 2); CHECK(!s_status.triggerKeyDown);
    }

    void TestTriggerShotLedger(bool triggerAssist) {
        auto settings = ResetFixture();
        settings.aimbotEnabled = !triggerAssist; settings.triggerbotEnabled = true;
        settings.triggerAimAssist = triggerAssist;
        settings.triggerActivationMode = 1; settings.triggerDelayMs = 0;
        settings.weaponProfiles[1].triggerSmoothing = 1;
        auto& snap = fixture::snapshot;
        auto& head = snap.players[1].hitboxes[0];
        head.start.y = 0; head.end.y = 0; head.center.y = 0;
        fixture::keyDown = true;
        for (int frame = 0; frame < 12 && fixture::clicks == 0; ++frame) {
            Tick(settings);
            s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        }
        CHECK(fixture::clicks == 1); CHECK(s_runtime.trigger.shotLatched);
        const uint32_t latchedHandle = s_runtime.trigger.latchedPawnHandle;
        CHECK(latchedHandle == snap.players[1].pawnHandle);
        fixture::click = {}; // device UP completed
        fixture::haveSnapshot = false; Tick(settings);
        CHECK(s_runtime.trigger.shotLatched); CHECK(fixture::clicks == 1);
        fixture::haveSnapshot = true;
        snap.localWeaponId = 0; snap.localWeaponEntity = 0; snap.localWeaponHandle = 0;
        Tick(settings); CHECK(s_runtime.trigger.shotLatched);
        snap.localWeaponId = 7; snap.localWeaponEntity = 0x30000; snap.localWeaponHandle = 0x10003;
        snap.players[1].health = 75; Tick(settings);
        CHECK(!s_runtime.trigger.shotObserved); // HP alone does not prove our shot
        --snap.localAmmoClip; snap.localShotsFired = 1; snap.localLastShotTime = 100;
        snap.localAimPunch.x = 1;
        s_runtime.trigger.targetOutcomeNotBefore = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        const int beforeRcs = fixture::moves;
        Tick(settings);
        CHECK(s_runtime.trigger.shotObserved); CHECK(fixture::moves > beforeRcs);
        CHECK(fixture::clicks == 1); CHECK(s_runtime.trigger.shotLatched);
        s_runtime.trigger.targetOutcomeNotBefore = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        snap.players[1].health = 0; Tick(settings);
        CHECK(!s_runtime.trigger.shotLatched); CHECK(fixture::clicks == 1);
        CHECK(s_status.shot.healthAfter == 0); CHECK(s_status.shot.weaponConfirmed);
        CHECK(s_runtime.trigger.blockedAfterConfirmedDeath);
    }

    void TestFeatureMatrix() {
        constexpr uint16_t weaponIds[] = {1, 7, 9, 17, 25, 28};
        for (const uint16_t weaponId : weaponIds) for (int flags = 0; flags < 8; ++flags) {
            auto settings = ResetFixture();
            fixture::snapshot.localWeaponId = weaponId;
            settings.aimPredictive = (flags & 1) != 0;
            settings.aimHumanization = (flags & 2) != 0;
            settings.aimRecoilControl = (flags & 4) != 0;
            settings.aimVisibleOnly = (flags & 1) != 0;
            settings.fovPerWeapon = (flags & 2) != 0;
            fixture::snapshot.localShotsFired = 1;
            fixture::snapshot.localAimPunch = {0.5f, 0.1f, 0};
            fixture::keyDown = true; Tick(settings);
            CHECK(s_status.phase == target::RuntimePhase::Tracking);
            CHECK(fixture::moves > 0);
        }
        auto settings = ResetFixture();
        settings.aimBone = 2; // chest cannot silently become a head target
        fixture::keyDown = true; Tick(settings);
        CHECK(s_status.phase == target::RuntimePhase::NoTarget);
        CHECK(s_status.aimSelection.damageRejected == 1);
        auto& enemy = fixture::snapshot.players[1];
        enemy.hitboxes[1] = enemy.hitboxes[0];
        enemy.hitboxes[1].hitgroup = 2; enemy.hitboxes[1].index = 4; enemy.hitboxCount = 2;
        Tick(settings); CHECK(s_status.phase == target::RuntimePhase::Tracking);
        settings.aimBone = 4; Tick(settings); CHECK(s_status.phase == target::RuntimePhase::Tracking);
        settings.fovPerWeapon = false; settings.fovRadius = 5; Tick(settings);
        CHECK(s_status.phase == target::RuntimePhase::NoTarget);
        settings.fovPerWeapon = true; Tick(settings);
        CHECK(s_status.phase == target::RuntimePhase::Tracking);

        settings = ResetFixture(); settings.aimbotEnabled = false;
        settings.triggerbotEnabled = true; settings.triggerActivationMode = 1;
        fixture::snapshot.localWeaponReady = false; fixture::snapshot.localIsReloading = true;
        fixture::keyDown = true; Tick(settings);
        CHECK(fixture::moves > 0); CHECK(fixture::clicks == 0); // aim != fire readiness
    }
}

void TestFireEvaluationAndPrediction() {
    ResetFixture();
    auto& snapshot = fixture::snapshot;
    auto& enemy = snapshot.players[1];
    Candidate candidate;
    candidate.player = &enemy; candidate.slot = 1; candidate.requiredHitgroup = 1;
    candidate.predictionOffset = {0, 30, 0};
    candidate.point = enemy.hitboxes[0].center + candidate.predictionOffset;
    const Vector3 delta = candidate.point - snapshot.localEyePos;
    snapshot.viewAngles = {-std::atan2(delta.z, std::hypot(delta.x, delta.y)) * 180.0f / kPi,
        std::atan2(delta.y, delta.x) * 180.0f / kPi, 0};
    auto evaluation = EvaluateShot(snapshot, candidate, 80, 30, false, {}, 0);
    CHECK(evaluation.reason == target::FireBlockReason::Ready);
    CHECK(evaluation.damageReady && evaluation.hitchance == 1.0f);
    CHECK(DoesCurrentBallisticRayHitCandidate(snapshot, candidate));
    MotionRuntimeState motion;
    float angularError = 999;
    target::convars::Values convars;
    CHECK(MoveToward(snapshot, candidate, {}, motion, 1, false, false, 0.01f,
        nullptr, nullptr, &angularError, 80, &convars) == MoveResult::Aligned);
    snapshot.localAimPunch = {0.5f, 0.25f, 0};
    snapshot.localShotsFired = 0; // counter may reset before the punch settles
    CHECK(MoveToward(snapshot, candidate, {}, motion, 1, false, true, 0.01f,
        nullptr, nullptr, &angularError, 80, &convars) == MoveResult::Queued);
    snapshot.localAimPunch = {};
    auto unpredicted = candidate;
    unpredicted.predictionOffset = {};
    CHECK(!DoesCurrentBallisticRayHitCandidate(snapshot, unpredicted));
    CHECK(EvaluateShot(snapshot, unpredicted, 80, 30, false, {}, 0).reason ==
        target::FireBlockReason::Hitchance);

    snapshot.localInaccuracy = 0.04f;
    evaluation = EvaluateShot(snapshot, candidate, 80, 30, false, {}, 0);
    CHECK(evaluation.reason == target::FireBlockReason::Hitchance);
    CHECK(evaluation.hitchance >= 0 && evaluation.hitchance < 0.8f);
    CHECK(evaluation.damage < 0 && !evaluation.geometryHit); // uncomputed, NOT zero damage
    snapshot.localInaccuracy = 0;
    snapshot.localWeaponDamage = 1;
    evaluation = EvaluateShot(snapshot, candidate, 80, 30, false, {}, 0);
    CHECK(evaluation.reason == target::FireBlockReason::DamageTooLow);
    CHECK(evaluation.damage > 0 && evaluation.damage < 30 && evaluation.geometryHit);
    snapshot.localWeaponDamage = 40;

    fixture::geometryAvailable = false;
    enemy.visibilityUpdatedAtUs = snapshot.sampledAtUs - 1000000u;
    evaluation = EvaluateShot(snapshot, candidate, 80, 30, false, {}, 0);
    CHECK(evaluation.reason == target::FireBlockReason::WorldUnavailable);
    enemy.visibilityUpdatedAtUs = snapshot.sampledAtUs;
    CHECK(EvaluateShot(snapshot, candidate, 80, 30, false, {}, 0).damageReady);
    fixture::geometryAvailable = true;

    // The real tick must expose threshold refusal and must not send a click.
    auto settings = ResetFixture();
    settings.aimbotEnabled = false; settings.triggerbotEnabled = true;
    settings.triggerActivationMode = 1; settings.triggerAimPredictive = false;
    settings.weaponProfiles[1].hitchance = 80;
    snapshot.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
    snapshot.localInaccuracy = 0.04f;
    fixture::keyDown = true; Tick(settings);
    CHECK(fixture::clicks == 0);
    CHECK(s_status.fire.reason == target::FireBlockReason::Hitchance);
    CHECK(s_status.fire.hitchancePercent >= 0 && s_status.fire.hitchancePercent < 80);
    CHECK(s_status.fire.centeredHitchancePercent >= 0 && s_status.fire.damage < 0);

    // End-to-end selection -> prediction -> alignment -> fire on a moving
    // target, with an offset larger than the capsule radius. Transport is fake;
    // the production selection, gates and click-request path run unchanged.
    settings = ResetFixture();
    settings.aimbotEnabled = false; settings.triggerbotEnabled = true;
    settings.triggerActivationMode = 1; settings.triggerAimPredictive = true;
    settings.triggerDelayMs = 0;
    auto& moving = snapshot.players[1];
    moving.velocityValid = true; moving.velocity = {0, 200, 0};
    auto& local = snapshot.players[0];
    local.valid = true; local.pawn = snapshot.localPawn; local.ping = 100;
    for (int frame = 0; frame < 20 && fixture::clicks == 0; ++frame) {
        const Candidate planned = SelectTarget(snapshot, settings.triggerAimBone,
            1920, 1080, 150, false, true, 0, 0, 100, snapshot.localIntervalPerTick);
        CHECK(planned.player == &moving);
        CHECK(planned.predictionOffset.y > moving.hitboxes[0].radius);
        snapshot.viewAngles.y = std::atan2(planned.point.y, planned.point.x) * 180.0f / kPi;
        fixture::keyDown = true;
        Tick(settings);
        s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    }
    CHECK(fixture::clicks == 1);
    CHECK(s_status.fire.reason == target::FireBlockReason::Queued);
}

void TestIndependentAccuracyToggles() {
    // Default construction/legacy aggregate initialization must retain the
    // previous behavior. Each category owns its independent pair of switches.
    auto settings = ResetFixture();
    for (const auto& profile : settings.weaponProfiles) {
        CHECK(profile.hitchanceEnabled && profile.seedWindowEnabled);
    }
    for (int mode = 0; mode < 4; ++mode) {
        settings = ResetFixture();
        auto& snapshot = fixture::snapshot;
        auto& enemy = snapshot.players[1];
        snapshot.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
        snapshot.localInaccuracy = 0.08f;
        Candidate candidate;
        candidate.player = &enemy; candidate.slot = 1; candidate.requiredHitgroup = 1;
        candidate.point = enemy.hitboxes[0].center;
        const bool hc = (mode & 1) != 0;
        const bool seed = (mode & 2) != 0;
        auto evaluation = EvaluateShot(snapshot, candidate, hc ? 80.0f : 0.0f,
            30, false, {}, 0, nullptr, seed);
        if (hc) {
            CHECK(evaluation.reason == target::FireBlockReason::Hitchance);
            CHECK(evaluation.hitchance >= 0 && evaluation.hitchance < 0.8f);
        } else if (!seed) {
            CHECK(evaluation.damageReady);
            CHECK(evaluation.hitchance < 0 && evaluation.predictedSeedHitFraction < 0);
        } else {
            bool foundSeedRefusal = false;
            for (int tick = 100; tick < 180 && !foundSeedRefusal; ++tick) {
                snapshot.localRenderTick = tick;
                evaluation = EvaluateShot(snapshot, candidate, 0, 30, false, {}, 0, nullptr, true);
                foundSeedRefusal = evaluation.reason == target::FireBlockReason::SeedWindow;
            }
            CHECK(foundSeedRefusal); // disabling HC must not implicitly disable seed checks
            CHECK(evaluation.hitchance < 0 && evaluation.predictedSeedHitFraction >= 0);
        }
        if (!seed) CHECK(evaluation.predictedSeedHitFraction < 0);

        // Real tick, all four combinations with perfect spread: exactly one
        // click, published switches correct, saved percentage not overwritten.
        settings.aimbotEnabled = false; settings.triggerbotEnabled = true;
        settings.triggerActivationMode = 1; settings.triggerDelayMs = 0;
        settings.triggerAimPredictive = false; settings.triggerAutoShot = false;
        settings.weaponProfiles[1].hitchanceEnabled = hc;
        settings.weaponProfiles[1].seedWindowEnabled = seed;
        settings.weaponProfiles[1].hitchance = 83;
        snapshot.localInaccuracy = 0;
        fixture::keyDown = true;
        for (int frame = 0; frame < 20; ++frame) {
            Tick(settings);
            s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        }
        CHECK(fixture::clicks == 1);
        CHECK(s_status.fire.hitchanceEnabled == hc && s_status.fire.seedWindowEnabled == seed);
        CHECK(settings.weaponProfiles[1].hitchance == 83);
        CHECK(settings.weaponProfiles[0].hitchanceEnabled && settings.weaponProfiles[0].seedWindowEnabled);
    }

    // Switch off during the same activation. No re-press required, and
    // disabling spread never bypasses reload/freshness or minimum damage.
    settings = ResetFixture();
    settings.aimbotEnabled = false; settings.triggerbotEnabled = true;
    settings.triggerActivationMode = 1; settings.triggerDelayMs = 0;
    settings.triggerAimPredictive = false;
    auto& snapshot = fixture::snapshot;
    snapshot.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
    snapshot.localInaccuracy = 0.08f;
    fixture::keyDown = true; Tick(settings);
    CHECK(s_status.fire.reason == target::FireBlockReason::Hitchance && fixture::clicks == 0);
    settings.weaponProfiles[1].hitchanceEnabled = false;
    settings.weaponProfiles[1].seedWindowEnabled = false;
    snapshot.localIsReloading = true; snapshot.localWeaponReady = false;
    Tick(settings);
    CHECK(fixture::clicks == 0 && s_status.fire.reason == target::FireBlockReason::WeaponNotReady);
    snapshot.localIsReloading = false; snapshot.localWeaponReady = true;
    snapshot.localWeaponDamage = 1;
    Tick(settings);
    CHECK(fixture::clicks == 0 && s_status.fire.reason == target::FireBlockReason::DamageTooLow);
    snapshot.localWeaponDamage = 40;
    for (int frame = 0; frame < 20; ++frame) {
        Tick(settings);
        s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    }
    CHECK(fixture::clicks == 1 && s_triggerActivationShotIssued);
    CHECK(s_status.fire.hitchancePercent < 0 && s_status.fire.seedHitPercent < 0);
}

int main() {
    {
        auto settings = ResetFixture();
        fixture::keyDown = true;
        fixture::snapshot.snapshotAgeUs = 102521;
        fixture::snapshot.viewUpdatedAtUs = fixture::snapshot.sampledAtUs - 6294;
        fixture::snapshot.localEyeUpdatedAtUs = fixture::snapshot.sampledAtUs - 102521;
        fixture::logs = 0;
        for (int tick = 0; tick < 10; ++tick) TickTarget(1920, 1080, settings, false);
        CHECK(fixture::logs == 0 && fixture::moves == 0 && fixture::clicks == 0);
        CHECK(s_status.phase == target::RuntimePhase::DataUnavailable);
        CHECK(s_status.snapshotAgeUs == 102521 && s_status.viewAgeUs == 6294 && s_status.eyeAgeUs == 102521);
        fixture::snapshot.snapshotAgeUs = 0;
        FreshFrame(); Tick(settings);
        CHECK(s_status.phase != target::RuntimePhase::DataUnavailable);
        CHECK(s_status.snapshotAgeUs == -1); // stale diagnostic must not linger after recovery
    }
    TestIndependentAccuracyToggles();
    TestFireEvaluationAndPrediction();
    TestToggleLifecycle(); TestMovementAndSelection();
    TestTriggerShotLedger(true); TestTriggerShotLedger(false); TestFeatureMatrix();
    TestRepeatShotsObeyToggle();
    TestSingleShotBudget();
    if (failures) { std::cerr << failures << " Target runtime failures\n"; return 1; }
    std::cout << "Target runtime tests passed: prediction/fire integration, gate diagnostics, residual RCS, Toggle, FOV, shot ledger, death, 48 weapon/option combinations.\n";
}
