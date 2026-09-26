// Execute the actual Target tick/motion implementation with deterministic
// snapshots and fake transport. Never attach DMA, spawn workers or send input.
#if defined(KEVQ_TARGET_INPUT_INTEGRATION)
#include <WinSock2.h>
#include <WS2tcpip.h>
#endif
#include "Features/ESP/esp.h"
#include "Features/ESP/DataReader/view_offset_sample.h"
#include "Features/ESP/DataReader/recoil_sample.h"
#include "Game/Offsets/runtime_offsets.h"
#include "Features/Target/physics_bvh.h"
#include "app/Input/input_device.h"
#include "app/Input/primary_keyboard.h"
#include <iostream>
#include <chrono>
#pragma warning(push)
#pragma warning(disable: 4200) // Upstream C vendor ABI uses flexible array members.
#include "vendor/DMALibrary/pch.h"
#pragma warning(pop)

namespace fixture {
    esp::TargetSnapshot snapshot;
    bool haveSnapshot = true;
    std::atomic<bool> keyDown = false;
    std::atomic<int> primaryKey = 0x06;
    app::input::ActivationKeyRouter activationRouter;
    std::atomic<bool> keyboardAvailable = true;
    app::input::DeviceStatus device;
    bool acceptMove = true;
    int moves = 0;
    int clicks = 0;
    int mapRequests = 0;
    bool geometryAvailable = true;
    bool wallBlocks = false;
    std::string geometryMap = "fixture_map";
    target::physics::BuildState geometryState = target::physics::BuildState::Ready;
    int logs = 0;
    int moveCancellations = 0;
    bool alternateKeySamples = false;
    int activationReads = 0;
    app::input::LeftClickStatus click;
    uint64_t clockAdvanceUs = 0;
    float recoilScale = 2.0f;
    bool recoilScaleValid = false;
    uint64_t DataNowUs() {
        static const auto epoch = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - epoch).count();
        return (std::max)(snapshot.sampledAtUs, 10000000u + static_cast<uint64_t>(elapsed)) + clockAdvanceUs;
    }
}

#include "../../../src/Features/Target/target.cpp"

void DmaLogPrintf(const char*, ...) { ++fixture::logs; }
bool esp::GetTargetSnapshot(TargetSnapshot* out) { *out = fixture::snapshot; return fixture::haveSnapshot; }
uint64_t esp::GetSnapshotTimeUs() { return fixture::DataNowUs(); }
#if !defined(KEVQ_TARGET_INPUT_INTEGRATION)
app::input::DeviceStatus app::input::GetDeviceStatus() {
    return fixture::device;
}
#endif
app::input::PrimaryKeyboardStatus app::input::GetPrimaryKeyboardStatus() {
    return {fixture::keyboardAvailable, 1, 0, 1};
}
#if !defined(KEVQ_TARGET_INPUT_INTEGRATION)
bool app::input::IsActivationKeyDown(int key) {
    const auto state = ReadActivationKeyState(key);
    return state.available && state.down;
}
app::input::KeyState app::input::ReadActivationKeyState(int key) {
    if (fixture::alternateKeySamples && key == 0x06)
        return {true, (++fixture::activationReads % 2) == 1};
    const KeyState primary = {fixture::keyboardAvailable, key == fixture::primaryKey && fixture::keyDown};
    return fixture::activationRouter.Read(fixture::device, key, primary, GetTickCount64());
}
app::input::LeftClickTiming app::input::GetLeftClickTiming() { return {}; }
app::input::LeftClickStatus app::input::GetLeftClickStatus() { return fixture::click; }
bool app::input::RequestMove(int, int, uint64_t validForUs) {
    if (validForUs == 0) return false;
    if (!fixture::acceptMove) return false;
    ++fixture::moves; return true;
}
void app::input::CancelPendingMoves() { ++fixture::moveCancellations; }
bool app::input::RequestLeftClick(uint32_t) {
    if (fixture::click.active) return false;
    ++fixture::clicks; fixture::click = {true, true, 1}; return true;
}
app::input::ClickRequestResult app::input::TryRequestLeftClick(uint32_t holdMs, uint64_t validForUs) {
    if (validForUs == 0) return ClickRequestResult::Unavailable;
    if (fixture::device.moveInFlight || fixture::device.movePending || fixture::device.probeInFlight)
        return ClickRequestResult::Busy;
    return RequestLeftClick(holdMs) ? ClickRequestResult::Queued : ClickRequestResult::Busy;
}
bool app::input::RequestLeftButton(bool down) { if (!down) fixture::click = {}; return true; }
#else
app::input::KeyState app::input::ReadPrimaryKeyState(int key) {
    return {fixture::keyboardAvailable.load(), key == fixture::primaryKey && fixture::keyDown.load()};
}
bool app::input::IsPrimaryKeyDown(int key) {
    const auto state = ReadPrimaryKeyState(key);
    return state.available && state.down;
}
#endif
void target::convars::Start() {}
target::convars::Values target::convars::Read(uint64_t) {
    Values result;
    result.recoilScale = fixture::recoilScale;
    result.recoilScaleValid = fixture::recoilScaleValid;
    return result;
}
void target::convars::Reset() {}
void target::physics::RequestForMap(const char*) { ++fixture::mapRequests; }
bool target::physics::IsReadyForMap(const char* mapKey) {
    return fixture::geometryAvailable && fixture::geometryState == BuildState::Ready &&
        mapKey && fixture::geometryMap == mapKey;
}
bool target::physics::IsLineVisible(const char* mapKey, const Vector3&, const Vector3&, float) {
    return IsReadyForMap(mapKey) && !fixture::wallBlocks;
}
bool target::physics::TracePenetrationSegments(const char* mapKey, const Vector3&, const Vector3&,
    std::vector<PenetrationSegment>& segments) {
    segments.clear();
    if (!IsReadyForMap(mapKey)) return false;
    if (fixture::wallBlocks) {
        PenetrationSegment wall;
        wall.enterDistance = 100; wall.exitDistance = 110; wall.thickness = 10;
        wall.minimumPenetrationModifier = 1;
        segments.push_back(wall);
    }
    return true;
}
target::physics::Stats target::physics::GetStats() {
    Stats result;
    result.state = fixture::geometryState;
    strcpy_s(result.mapKey, fixture::geometryMap.c_str());
    return result;
}
void target::physics::Shutdown() {}

namespace {
    int failures = 0;
    void Check(bool condition, const char* expression, int line) {
        if (!condition) { ++failures; std::cerr << line << ": " << expression << '\n'; }
    }
#define CHECK(x) Check((x), #x, __LINE__)

    void FreshFrame() {
        auto& snap = fixture::snapshot;
        snap.sampledAtUs = (std::max)(snap.sampledAtUs + 1000, fixture::DataNowUs());
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
        s_outputFeedback = {};
        s_recoilScale = 2.0f;
        s_aimActivation = {}; s_triggerActivation = {};
        s_triggerActivationShotIssued = false;
        s_activationScene = 0; s_activationPawn = 0;
        s_shotDiagnostics = {}; s_status = {};
        fixture::snapshot = {}; fixture::haveSnapshot = true;
        fixture::clockAdvanceUs = 0;
        fixture::recoilScale = 2.0f;
        fixture::recoilScaleValid = false;
        fixture::keyDown = false; fixture::keyboardAvailable = true;
        fixture::primaryKey = 0x06; fixture::activationRouter = {};
        fixture::alternateKeySamples = false; fixture::activationReads = 0;
        fixture::moveCancellations = 0;
        fixture::device = {};
        fixture::device.selected = app::input::DeviceKind::Makcu;
        fixture::device.state = app::input::ConnectionState::Connected;
        fixture::acceptMove = true; fixture::moves = 0; fixture::clicks = 0;
        fixture::click = {}; fixture::mapRequests = 0;
        fixture::geometryAvailable = true;
        fixture::wallBlocks = false;
        fixture::geometryMap = "fixture_map";
        fixture::geometryState = target::physics::BuildState::Ready;
        g::menuOpen = false;
        g::targetKeyCaptureUntilMs = 0;
        s_fireDiagnostics = {};
        auto& snap = fixture::snapshot;
        strcpy_s(snap.mapKey, "fixture_map");
        snap.sampledAtUs = fixture::DataNowUs(); snap.sceneSerial = 1;
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
        settings.aimBone = 0;
        settings.fovRadius = 150;
        settings.aimHumanization = false;
        for (auto& profile : settings.weaponProfiles) {
            profile.aimReactionMs = 0;
            profile.aimAdaptiveSmoothing = false;
            profile.aimAssistStrength = 35;
            profile.aimAssistMaxSpeed = 35;
        }
        settings.weaponProfiles[1].aimSmoothing = 1;
        settings.weaponProfiles[1].aimDamageCheck = true;
        return settings;
    }

    void Tick(const g::TargetSettings& settings, bool capture = false) {
        FreshFrame(); TickTarget(1920, 1080, settings, capture || target::policy::IsInputCaptureActive(
            g::menuOpen.load(), g::targetKeyCaptureUntilMs.load(), GetTickCount64()));
    }

    void TestConvarResolutionRecoveryPolicy() {
        using namespace target::convars::policy;
        constexpr uint64_t first = 10000000;
        CHECK(ShouldResolve(first, 0, false, true, true, 0, false));
        for (uint64_t age : {uint64_t(0), uint64_t(1), kResolveRetryUs - 1})
            CHECK(!ShouldResolve(first + age, first, false, true, true, 0, true));
        CHECK(ShouldResolve(first + kResolveRetryUs, first, true, true, false, 3, false));
        CHECK(ShouldResolve(first + kResolveRetryUs, first, true, false, false, 3, true));
        for (uint8_t attempts = 0; attempts < kMaximumOptionalResolveAttempts; ++attempts)
            CHECK(ShouldResolve(first + kResolveRetryUs, first, true, false, true, attempts, false));
        CHECK(!ShouldResolve(first + kResolveRetryUs, first, true, false, true, 3, false));
        CHECK(!ShouldResolve(first + kResolveRetryUs, first, true, false, false, 0, false));
        CHECK(ShouldResolve(first - 1, first, true, true, false, 3, false));
        uint8_t failures = 0;
        for (int i = 0; i < 1000; ++i) {
            failures = NextReadFailureCount(failures, false);
            CHECK(failures == static_cast<uint8_t>(std::min(i + 1, 3)));
        }
        CHECK(NextReadFailureCount(failures, true) == 0);
        CHECK(NextReadFailureCount(1, true) == 0);
    }

    void TestAimWithoutBallisticTelemetry() {
        for (const int style : {0, 1, 2}) {
            auto settings = ResetFixture();
            auto& snap = fixture::snapshot;
            settings.weaponProfiles[1].aimDamageCheck = false;
            settings.weaponProfiles[1].aimMotionStyle = style;
            snap.localWeaponTelemetryValid = false;
            fixture::keyDown = true;
            Tick(settings);
            CHECK(fixture::moves > 0 && fixture::clicks == 0);
            CHECK(s_status.phase == target::RuntimePhase::Tracking);
            const int before = fixture::moves;
            settings.weaponProfiles[1].aimDamageCheck = true;
            Tick(settings);
            CHECK(fixture::moves == before && s_status.phase == target::RuntimePhase::NoTarget);
            settings.aimbotEnabled = false;
            settings.triggerbotEnabled = true;
            settings.triggerAimAssist = false;
            settings.triggerActivationMode = 0;
            snap.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
            Tick(settings);
            CHECK(fixture::clicks == 0);
        }
    }

    void TestRecoilPublication() {
        Vector3 punch{};
        bool valid = false;
        uint64_t updated = 0;
        esp::data::CommitRecoilSample(punch, valid, updated, {0.5f, -0.2f, 0}, true, false, 100000);
        CHECK(valid && updated == 100000 && punch.x == 0.5f);
        esp::data::CommitRecoilSample(punch, valid, updated, {0.2f, -0.1f, 0}, true, false, 108000);
        CHECK(valid && updated == 108000 && punch.x == 0.2f);
        esp::data::CommitRecoilSample(punch, valid, updated, {}, false, false, 116000);
        CHECK(valid && updated == 108000 && punch.x == 0.2f);
        esp::data::CommitRecoilSample(punch, valid, updated,
            {std::numeric_limits<float>::quiet_NaN(), 0, 0}, true, false, 120000);
        CHECK(valid && updated == 108000);
        esp::data::CommitRecoilSample(punch, valid, updated, {}, false, false, 160000);
        CHECK(!valid && updated == 0 && punch.x == 0);
        esp::data::CommitRecoilSample(punch, valid, updated, {}, true, false, 170000);
        CHECK(valid && updated == 170000 && punch.x == 0);
        esp::data::CommitRecoilSample(punch, valid, updated, {1, 1, 0}, true, true, 180000);
        CHECK(!valid && updated == 0 && punch.x == 0);
    }

    void TestAssistanceMath() {
        using namespace target::policy;
        CHECK(RemoveAimDeadzone({0.1f, 0.1f, true}, 0.15f).pitch == 0);
        const auto edge = RemoveAimDeadzone({0.0f, 1.0f, true}, 0.15f);
        CHECK(std::fabs(edge.yaw - 0.85f) < 0.0001f);
        CHECK(!RemoveAimDeadzone({0, 0, false}, 0.15f).valid);
        for (const float dt : {1.0f / 64, 1.0f / 128, 1.0f / 240, 0.5f}) {
            float pitch = 3, yaw = 4;
            LimitAimVelocity(pitch, yaw, 35, dt);
            CHECK(std::hypot(pitch, yaw) <= 35 * std::min(dt, 0.03125f) + 0.0001f);
        }
        float reference = 0;
        for (const int hz : {64, 128, 240}) {
            float remaining = 8;
            for (int i = 0; i < hz; ++i) {
                auto delta = RemoveAimDeadzone({remaining, 0, true}, 0.15f);
                delta.pitch *= TimeAdjustedSmoothing(ResolveSmoothingFraction(delta.pitch, 5, false) * 0.35f, 1.0f / hz);
                LimitAimVelocity(delta.pitch, delta.yaw, 35, 1.0f / hz);
                remaining -= delta.pitch;
                CHECK(remaining >= 0.1499f);
            }
            if (reference == 0) reference = remaining;
            CHECK(std::fabs(remaining - reference) < 0.01f);
        }
        RecoilDeltaState state;
        CHECK(AdvanceRecoilDelta(state, 0, 0, 100000, 100000, true, 100, 0, 0).pitch == 0);
        auto recoil = AdvanceRecoilDelta(state, 1, -0.5f, 108000, 108000, true, 100, 0, 1);
        CHECK(recoil.pitch == -1 && recoil.yaw == 0.5f);
        recoil = AdvanceRecoilDelta(state, 0, 0, 104000, 109000, true, 100, 0, 0);
        CHECK(recoil.pitch == 0 && state.updatedAtUs == 108000);
        recoil = AdvanceRecoilDelta(state, 1, -0.5f, 108000, 109000, true, 100, 5, 1);
        CHECK(recoil.pitch == 0 && recoil.yaw == 0);
        recoil = AdvanceRecoilDelta(state, 0.5f, -0.25f, 116000, 116000, true, 100, 0, 0);
        CHECK(recoil.pitch == 0.5f && recoil.yaw == -0.25f);
        recoil = AdvanceRecoilDelta(state, 0, 0, 124000, 200000, true, 100, 0, 0);
        CHECK(!recoil.valid && state.updatedAtUs == 0);
        CHECK(AdvanceRecoilDelta(state, 8, 0, 210000, 210000, true, 100, 0, 0).pitch == 0);
        CHECK(AdvanceRecoilDelta(state, 0, 0, 220000, 220000, true, 50, 0, 0).pitch == 0);
        recoil = AdvanceRecoilDelta(state, 1, 0, 228000, 228000, true, 50, 5, 1);
        CHECK(std::fabs(recoil.pitch + 0.525f) < 0.0001f);
        CHECK(AdvanceRecoilDelta(state, 1, 0, 236000, 236000, true, 50, 5, -1).pitch == 0);
        CHECK(!AdvanceRecoilDelta(state, 1, 0, 244000, 244000, false, 50, 0, 0).valid);
        CHECK(state.updatedAtUs == 0);
    }

    void TestGentleDefaultsAndCurveBounds() {
        g::TargetSettings defaults;
        CHECK(!defaults.enabled && !defaults.aimbotEnabled && !defaults.triggerbotEnabled);
        CHECK(defaults.aimBone == 5 && defaults.fovRadius == 80 && !defaults.fovPerWeapon);
        CHECK(defaults.aimVisibleOnly && !defaults.aimPredictive && defaults.aimRecoilControl);
        for (const auto& profile : defaults.weaponProfiles) {
            CHECK(profile.aimSoftAssist && profile.aimAdaptiveSmoothing);
            CHECK(profile.aimAssistStrength == 25 && profile.aimAssistMaxSpeed == 25);
            CHECK(profile.aimReactionMs == 80 && profile.aimSmoothing >= 7);
            CHECK(target::settings_policy::AimMotionStyle(defaults, profile) == 1);
            CHECK(!profile.aimAutowall && !profile.aimDamageCheck);
        }
        using target::policy::ConstrainCurvedAimStep;
        for (int angle = 0; angle < 360; angle += 15)
        for (float distance : {0.01f, 0.1f, 0.2f, 1.0f, 5.0f, 90.0f})
        for (float x : {-100.0f, -1.0f, 0.0f, 1.0f, 100.0f})
        for (float y : {-100.0f, -1.0f, 0.0f, 1.0f, 100.0f}) {
            const float px = std::cos(angle * kPi / 180), py = std::sin(angle * kPi / 180);
            const auto step = ConstrainCurvedAimStep({x, y, true}, {distance * px, distance * py, true});
            const float along = step.pitch * px + step.yaw * py;
            const float side = -step.pitch * py + step.yaw * px;
            CHECK(std::isfinite(step.pitch) && std::isfinite(step.yaw));
            CHECK(along >= -0.0001f && along <= distance + 0.0001f);
            CHECK(std::hypot(distance * px - step.pitch, distance * py - step.yaw) <= distance + 0.0001f);
            CHECK(std::hypot(step.pitch, step.yaw) <= std::hypot(x, y) + 0.0001f);
            CHECK(std::fabs(side) <= std::max(0.0f, along) * 0.2f + 0.0001f);
            if (distance <= 0.12f) CHECK(std::fabs(side) < 0.0001f);
        }
        const auto invalid = ConstrainCurvedAimStep({NAN, 1, true}, {1, 1, true});
        CHECK(invalid.pitch == 0 && invalid.yaw == 0);
        ResetFixture();
        defaults.enabled = true; defaults.aimbotEnabled = true;
        fixture::keyDown = true;
        Tick(defaults);
        CHECK(s_status.phase == target::RuntimePhase::Reacting && fixture::moves == 0);
        fixture::snapshot.sampledAtUs += 90000;
        Tick(defaults);
        CHECK(s_status.phase == target::RuntimePhase::Tracking && fixture::moves > 0);
        CHECK(std::hypot(static_cast<float>(s_status.moveX), static_cast<float>(s_status.moveY)) *
            0.022f <= 25.0f / 128.0f + 0.032f);
    }

    void TestRecoilReleaseNoSnap() {
        for (const bool soft : {false, true}) {
            auto settings = ResetFixture();
            auto& snap = fixture::snapshot;
            auto& profile = settings.weaponProfiles[1];
            profile.aimSoftAssist = soft;
            Candidate candidate;
            candidate.player = &snap.players[1]; candidate.slot = 1;
            candidate.point = candidate.player->hitboxes[0].center;
            candidate.requiredHitgroup = 1;
            const float yaw = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
            snap.localShotsFired = 8;
            snap.localAimPunch = {-2, 0, 0};
            snap.viewAngles = {4, yaw, 0};
            MotionRuntimeState motion;
            int x = 0, y = 0;
            const auto step = [&] {
                return MoveToward(snap, candidate, {}, motion, 1, false, true,
                    0.01f, &x, &y, nullptr, -1, nullptr, false, nullptr, &profile);
            };
            CHECK(step() == MoveResult::Aligned);
            snap.localShotsFired = 0;
            for (int frame = 0; frame < 60; ++frame) {
                const auto previousViewAtUs = snap.viewUpdatedAtUs;
                snap.sampledAtUs += 8000; FreshFrame();
                const auto result = step();
                CHECK(std::fabs(motion.diagnostics.recoilPitchStep) < 0.00001f);
                CHECK(std::abs(y) * 0.022f <=
                    45.0f * (snap.viewUpdatedAtUs - previousViewAtUs) * 0.000001f + 0.022f);
                if (result == MoveResult::Queued) {
                    snap.viewAngles.x += y * 0.022f;
                    CHECK(snap.viewAngles.x >= -0.023f);
                }
            }
            CHECK(std::fabs(snap.viewAngles.x) < 0.17f);
        }
    }

    void TestHorizontalHumanizedCurve() {
        auto settings = ResetFixture();
        auto& snap = fixture::snapshot;
        auto& profile = settings.weaponProfiles[1];
        profile.aimSoftAssist = false;
        Candidate candidate;
        candidate.player = &snap.players[1]; candidate.slot = 1;
        candidate.point = candidate.player->hitboxes[0].center;
        candidate.requiredHitgroup = 1;
        MotionRuntimeState motion;
        float maximumPitch = 0;
        for (int frame = 0; frame < 100; ++frame) {
            snap.sampledAtUs += 8000; FreshFrame();
            int x = 0, y = 0;
            if (MoveToward(snap, candidate, {}, motion, 7, true, false, 0.01f,
                    &x, &y, nullptr, -1, nullptr, true, nullptr, &profile) == MoveResult::Queued) {
                snap.viewAngles.x += y * 0.022f;
                snap.viewAngles.y -= x * 0.022f;
            }
            maximumPitch = std::max(maximumPitch, std::fabs(snap.viewAngles.x));
        }
        CHECK(maximumPitch > 0.01f && maximumPitch < 0.3f);
        CHECK(std::fabs(snap.viewAngles.x) < 0.025f);
    }

    void TestRecoilSampleGap() {
        for (const int gap : {0, 1, 2, 3}) for (const int style : {0, 1, 2}) {
            auto settings = ResetFixture();
            auto& snap = fixture::snapshot;
            auto& profile = settings.weaponProfiles[1];
            Candidate candidate;
            candidate.player = &snap.players[1]; candidate.slot = 1;
            candidate.point = candidate.player->hitboxes[0].center;
            candidate.requiredHitgroup = 1;
            const float targetYaw = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
            snap.viewAngles = {0, targetYaw, 0};
            MotionRuntimeState motion;
            target::wind::Settings windSettings;
            int x = 0, y = 0;
            const auto step = [&] {
                return MoveToward(snap, candidate, {}, motion, 1, style == 1, true,
                    0.01f, &x, &y, nullptr, -1, nullptr, true,
                    style == 2 ? &windSettings : nullptr, &profile);
            };
            snap.localShotsFiredValid = false;
            CHECK(step() == MoveResult::Accumulating);
            CHECK(motion.diagnostics.reason == target::MoveBlockReason::RecoilUnavailable);
            CHECK(fixture::moves == 0 && fixture::moveCancellations > 0);
            CHECK(MoveToward(snap, candidate, {}, motion, 1, false, false, 0.01f,
                &x, &y, nullptr, -1, nullptr, false, nullptr, &profile) == MoveResult::Aligned);
            snap.localShotsFiredValid = true;
            snap.sampledAtUs += 8000; FreshFrame();
            CHECK(step() == MoveResult::Aligned);
            snap.sampledAtUs += 8000; FreshFrame();
            snap.localShotsFired = 4;
            snap.localAimPunch = {-2, 0.25f, 0};
            snap.viewAngles = {4, targetYaw - 0.5f, 0};
            CHECK(step() == MoveResult::Aligned);
            const int before = fixture::moves;
            for (int tick = 0; tick < 5; ++tick) {
                snap.sampledAtUs += 8000; FreshFrame();
                if (gap == 0) snap.localAimPunchValid = false;
                if (gap == 1) snap.localAimPunchUpdatedAtUs -= 40000;
                if (gap == 2) snap.localShotsFiredValid = false;
                if (gap == 3) snap.localWeaponUpdatedAtUs = 0;
                step();
                CHECK(fixture::moves == before && x == 0 && y == 0);
            }
            snap.localAimPunchValid = true;
            snap.localShotsFiredValid = true;
            snap.sampledAtUs += 8000; FreshFrame();
            CHECK(step() == MoveResult::Aligned);
            CHECK(fixture::moves == before);
            snap.localShotsFired = 0;
            snap.localAimPunchValid = false;
            snap.viewAngles = {0, targetYaw, 0};
            snap.sampledAtUs += 8000; FreshFrame();
            CHECK(step() == MoveResult::Aligned);
        }
    }

    void TestRecoilDuringOffAxisAcquisition() {
        for (const int style : {0, 1, 2})
        for (const float pitchSign : {-1.0f, 1.0f})
        for (const float yawSign : {-1.0f, 1.0f}) {
            auto settings = ResetFixture();
            auto& snap = fixture::snapshot;
            auto& profile = settings.weaponProfiles[1];
            profile.aimAssistStrength = 25;
            profile.aimAssistMaxSpeed = 25;
            Candidate candidate;
            candidate.player = &snap.players[1]; candidate.slot = 1;
            candidate.point = candidate.player->hitboxes[0].center;
            candidate.requiredHitgroup = 1;
            const float targetYaw = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
            snap.viewAngles = {0, targetYaw, 0};
            MotionRuntimeState motion;
            target::wind::Settings windSettings;
            int x = 0, y = 0;
            const auto step = [&] {
                return MoveToward(snap, candidate, {}, motion, 7, style == 1, true,
                    0.01f, &x, &y, nullptr, -1, nullptr, true,
                    style == 2 ? &windSettings : nullptr, &profile);
            };
            CHECK(step() == MoveResult::Aligned);
            const auto previousViewAtUs = snap.viewUpdatedAtUs;
            snap.sampledAtUs += 8000; FreshFrame();
            snap.localShotsFired = 1;
            snap.localAimPunch = {-pitchSign, 0, 0};
            snap.viewAngles.y = targetYaw - 3.0f * yawSign;
            CHECK(step() == MoveResult::Queued);
            const float pitchMovement = y * target::policy::kMouseYawDegrees;
            const float yawMovement = -x * target::policy::kMouseYawDegrees;
            const float acquisitionLimit = profile.aimAssistMaxSpeed *
                (snap.viewUpdatedAtUs - previousViewAtUs) * 0.000001f;
            CHECK(std::fabs(pitchMovement - 2.0f * pitchSign) <= 0.025f);
            CHECK(yawMovement * yawSign >= 0);
            CHECK(std::fabs(yawMovement) <= acquisitionLimit + 0.022f);
        }
    }

    void TestRecoilOutputFeedback() {
        auto settings = ResetFixture();
        auto& snap = fixture::snapshot;
        auto& profile = settings.weaponProfiles[1];
        profile.aimSmoothing = 50;
        Candidate candidate;
        candidate.player = &snap.players[1]; candidate.slot = 1;
        candidate.point = candidate.player->hitboxes[0].center;
        candidate.requiredHitgroup = 1;
        snap.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
        MotionRuntimeState motion;
        target::wind::Settings windSettings{4, 40, 1, 40};
        int x = 0, y = 0;
        const auto step = [&] {
            return MoveToward(snap, candidate, {}, motion, 50, true, true,
                0.01f, &x, &y, nullptr, -1, nullptr, true, &windSettings, &profile);
        };
        CHECK(step() == MoveResult::Aligned);
        snap.sampledAtUs += 8000; FreshFrame();
        snap.localShotsFired = 1; snap.localAimPunch = {1, 0, 0};
        CHECK(step() == MoveResult::Queued);
        CHECK(std::abs(y + 91) <= 1 && x == 0);
        const int firstY = y;
        const int afterFirst = fixture::moves;
        for (int i = 0; i < 5; ++i) {
            snap.sampledAtUs += 1000; FreshFrame();
            step();
            CHECK(fixture::moves == afterFirst);
        }
        snap.viewAngles.x += firstY * target::policy::kMouseYawDegrees;
        snap.sampledAtUs += 1000; FreshFrame();
        CHECK(step() == MoveResult::Aligned);
        CHECK(fixture::moves == afterFirst);
        snap.localAimPunch.x = 1.5f;
        snap.sampledAtUs += 8000; FreshFrame();
        fixture::device.movePending = true;
        step(); CHECK(fixture::moves == afterFirst);
        fixture::device.movePending = false;
        snap.sampledAtUs += 1000; FreshFrame();
        CHECK(step() == MoveResult::Queued);
        CHECK(std::abs(y + 45) <= 1);
        const int beforeTimeout = fixture::moves;
        snap.sampledAtUs += 110000; FreshFrame();
        step();
        CHECK(fixture::moves > beforeTimeout);
    }

    void TestDelayedRecoilFeedback() {
        for (const int latencyMs : {0, 8, 16, 32, 48, 64, 80}) for (const int style : {0, 1, 2, 3})
        for (const float sensitivity : {0.5f, 2.0f}) for (const float scale : {0.0f, 1.0f, 2.0f}) {
            auto settings = ResetFixture();
            auto& snap = fixture::snapshot;
            snap.sensitivity = sensitivity;
            snap.fovSensitivityAdjust = 0.75f;
            auto& profile = settings.weaponProfiles[1];
            s_recoilScale = scale;
            const auto control = ResolveWeaponControlProfile(7);
            CHECK(control.recoilPitchScale == scale && control.recoilYawScale == scale);
            Candidate candidate;
            candidate.player = &snap.players[1]; candidate.slot = 1;
            candidate.point = snap.players[1].hitboxes[0].center; candidate.requiredHitgroup = 1;
            const float targetYaw = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
            snap.viewAngles = {0, targetYaw, 0};
            MotionRuntimeState motion;
            target::wind::Settings windSettings{4, 40, 1, 40};
            struct Pending { int due, x, y; };
            std::vector<Pending> pending;
            float maximumError = 0.0f;
            const float count = sensitivity * snap.fovSensitivityAdjust * target::policy::kMouseYawDegrees;
            for (int tick = 0; tick < 260; ++tick) {
                for (auto it = pending.begin(); it != pending.end();) {
                    if (it->due <= tick) {
                        snap.viewAngles.x += it->y * count;
                        snap.viewAngles.y -= it->x * count;
                        it = pending.erase(it);
                    } else ++it;
                }
                snap.sampledAtUs += 3000; FreshFrame();
                const int progress = std::min(tick, 160);
                snap.localShotsFired = tick == 0 ? 0 : 1 + progress / 25;
                snap.localAimPunch = {-0.015f * progress, 0.2f * std::sin(progress * 0.06f), 0};
                int x = 0, y = 0;
                const auto result = MoveToward(snap, candidate, control, motion, 50, style == 1, true,
                    0.01f, &x, &y, nullptr, -1, nullptr, true, style == 2 ? &windSettings : nullptr,
                    style == 3 ? nullptr : &profile);
                CHECK(result != MoveResult::Failed);
                if (result == MoveResult::Queued) {
                    if (latencyMs == 0) { snap.viewAngles.x += y * count; snap.viewAngles.y -= x * count; }
                    else pending.push_back({tick + latencyMs / 4, x, y});
                }
                CHECK(pending.size() <= 1);
                const float pitchError = snap.viewAngles.x + snap.localAimPunch.x * scale;
                const float yawError = snap.viewAngles.y + snap.localAimPunch.y * scale - targetYaw;
                maximumError = std::max(maximumError, std::hypot(pitchError, yawError));
                if (tick > 220) CHECK(std::hypot(pitchError, yawError) < 0.08f);
            }
            const float delayedBound = std::max(0.08f,
                2.0f * (latencyMs / 4 + 1) * std::hypot(0.015f, 0.012f) * scale + count * 1.5f);
            if (maximumError >= delayedBound) std::cerr << "feedback latency=" << latencyMs << " style=" << style
                << " sensitivity=" << sensitivity << " scale=" << scale << " maximum_error=" << maximumError << '\n';
            CHECK(maximumError < delayedBound);
        }
        ResetFixture();
    }

    void TestUnrelatedMotionDoesNotAcknowledgeRecoil() {
        for (const bool resetMotion : {false, true}) {
            ResetFixture();
            auto& snap = fixture::snapshot;
            Candidate candidate;
            candidate.player = &snap.players[1]; candidate.slot = 1;
            candidate.point = candidate.player->hitboxes[0].center;
            candidate.requiredHitgroup = 1;
            snap.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
            MotionRuntimeState motion;
            int x = 0, y = 0;
            const auto step = [&] {
                return MoveToward(snap, candidate, {}, motion, 1, false, true,
                    0.01f, &x, &y);
            };
            CHECK(step() == MoveResult::Aligned);
            snap.localShotsFired = 1;
            snap.localAimPunch.x = -1.0f;
            FreshFrame();
            CHECK(step() == MoveResult::Queued && y > 0 && x == 0);
            const int issuedY = y;
            const int sent = fixture::moves;
            if (resetMotion) motion = {};
            for (int i = 0; i < 16; ++i) {
                snap.sampledAtUs += 3000; FreshFrame();
                snap.viewAngles.y += 0.04f;
                if (i == 4) snap.viewAngles.x = issuedY * target::policy::kMouseYawDegrees * 0.2f;
                step();
                CHECK(fixture::moves == sent);
                CHECK(motion.diagnostics.reason == target::MoveBlockReason::AwaitingView);
            }
            snap.viewAngles.x = issuedY * target::policy::kMouseYawDegrees;
            FreshFrame();
            step();
            CHECK(motion.diagnostics.reason != target::MoveBlockReason::AwaitingView);
        }
    }

    void TestNearestMouseCountDoesNotOscillate() {
        for (const float sensitivity : {1.0f, 2.0f, 4.0f, 8.0f}) {
            ResetFixture();
            auto& snap = fixture::snapshot;
            snap.sensitivity = sensitivity;
            Candidate candidate;
            candidate.player = &snap.players[1]; candidate.slot = 1;
            candidate.point = candidate.player->hitboxes[0].center;
            candidate.requiredHitgroup = 1;
            const float count = sensitivity * target::policy::kMouseYawDegrees;
            snap.viewAngles = {-count * 0.49f,
                std::atan2(50.0f, 1000.0f) * 180.0f / kPi - count * 0.49f, 0};
            MotionRuntimeState motion;
            for (int frame = 0; frame < 80; ++frame) {
                FreshFrame();
                int x = 0, y = 0;
                const auto result = MoveToward(snap, candidate, {}, motion, 1, false, false,
                    ResolveCandidateAlignmentTolerance(snap, candidate, 0), &x, &y);
                if (result == MoveResult::Queued) {
                    snap.viewAngles.x += y * count;
                    snap.viewAngles.y -= x * count;
                }
            }
            CHECK(fixture::moves == 0);
        }
    }

    void TestFixedPointConvergence() {
        using target::policy::PointConvergenceGain;
        CHECK(PointConvergenceGain(NAN) == 1.0f);
        CHECK(PointConvergenceGain(0.0f) == 1.0f);
        CHECK(PointConvergenceGain(0.35f) == 1.0f);
        CHECK(PointConvergenceGain(5.0f) == 1.0f);
        CHECK(PointConvergenceGain(0.03f) > PointConvergenceGain(0.20f));
        for (const float dt : {1.0f / 64, 1.0f / 128, 1.0f / 240}) {
            MotionRuntimeState normal, finish;
            float pitch = 0.07f, yaw = -0.07f;
            float finishPitch = pitch, finishYaw = yaw;
            ApplyHumanizedSmoothing(normal, pitch, yaw, 7, false, true, 0.25f, dt);
            ApplyHumanizedSmoothing(finish, finishPitch, finishYaw, 7, false, true, 0.25f, dt, true);
            CHECK(finishPitch > pitch && finishYaw < yaw);
            CHECK(finishPitch < 0.07f && finishYaw > -0.07f);
        }
        for (const int style : {0, 1, 2})
        for (const bool soft : {false, true})
        for (const float sensitivity : {1.0f, 4.0f})
        for (const float sign : {-1.0f, 1.0f})
        for (const uint64_t period : {uint64_t(4167), uint64_t(7812), uint64_t(15625)}) {
            auto settings = ResetFixture();
            auto& profile = settings.weaponProfiles[1];
            profile.aimSoftAssist = soft;
            profile.aimAssistStrength = 25;
            profile.aimAssistDeadzone = 0.5f;
            profile.aimAssistMaxSpeed = 25;
            auto& snap = fixture::snapshot;
            snap.sensitivity = sensitivity;
            const Candidate candidate = SelectTarget(snap, 0, 1920, 1080, 150, false, false);
            CHECK(candidate.player && candidate.fixedPoint);
            if (!candidate.player) continue;
            const float targetYaw = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
            const float count = sensitivity * target::policy::kMouseYawDegrees;
            snap.viewAngles = {sign * 0.083f, targetYaw - sign * 0.113f, 0};
            MotionRuntimeState motion;
            target::wind::Settings wind;
            int finalMoves = -1;
            for (int frame = 0; frame < 420; ++frame) {
                snap.sampledAtUs += period - 1000;
                FreshFrame();
                int x = 0, y = 0;
                const auto result = MoveToward(snap, candidate, {}, motion, 7, style == 1, false,
                    ResolveCandidateAlignmentTolerance(snap, candidate, 0), &x, &y,
                    nullptr, -1, nullptr, true, style == 2 ? &wind : nullptr, &profile);
                if (result == MoveResult::Queued) {
                    snap.viewAngles.x += y * count;
                    snap.viewAngles.y -= x * count;
                }
                if (frame == 399) finalMoves = fixture::moves;
            }
            CHECK(std::fabs(snap.viewAngles.x) <= count * 0.5f + 0.0001f);
            CHECK(std::fabs(snap.viewAngles.y - targetYaw) <= count * 0.5f + 0.0001f);
            CHECK(finalMoves == fixture::moves);
            CHECK(fixture::clicks == 0);
        }
        ResetFixture();
        auto& snap = fixture::snapshot;
        Candidate candidate = SelectTarget(snap, 0, 1920, 1080, 150, false, false);
        const Vector3 center = candidate.point;
        CHECK(ResolvePlannedAimShot(snap, candidate, 0.025f, false, 1, false, {}, 1920, 1080, 150));
        CHECK((candidate.point - center).Length() == 0.0f);
        fixture::wallBlocks = true;
        CHECK(!ResolvePlannedAimShot(snap, candidate, 0, true, 1, false, {}, 1920, 1080, 150));
        CHECK((candidate.point - center).Length() == 0.0f);
    }

    void TestQuantizedAlignmentStillValidatesCapsule() {
        for (const bool tinyCapsule : {false, true}) {
            auto settings = ResetFixture();
            settings.aimbotEnabled = false;
            settings.triggerbotEnabled = true;
            settings.triggerDelayMs = 0;
            settings.weaponProfiles[1].hitchanceEnabled = false;
            settings.weaponProfiles[1].seedWindowEnabled = false;
            settings.weaponProfiles[1].minimumDamage = 1;
            settings.weaponProfiles[1].triggerForceCenter = true;
            auto& snap = fixture::snapshot;
            snap.sensitivity = 4;
            auto& head = snap.players[1].hitboxes[0];
            head.start = head.end = head.center;
            if (tinyCapsule) head.radius = 0.01f;
            const float count = snap.sensitivity * target::policy::kMouseYawDegrees;
            snap.viewAngles = {-count * 0.49f,
                std::atan2(50.0f, 1000.0f) * 180.0f / kPi - count * 0.49f, 0};
            fixture::keyDown = true;
            for (int i = 0; i < 24; ++i) {
                Tick(settings);
                s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
            }
            CHECK(fixture::moves == 0);
            CHECK(fixture::clicks == (tinyCapsule ? 0 : 1));
        }
    }

    void TestTriggerClickPreservesRecoilHistory() {
        auto settings = ResetFixture();
        settings.aimbotEnabled = false;
        settings.triggerbotEnabled = true;
        auto& snap = fixture::snapshot;
        snap.localWeaponReady = false;
        snap.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
        fixture::keyDown = true;
        Tick(settings);
        s_runtime.trigger.assist.recoil = {2, 0, 1, snap.sampledAtUs};
        s_runtime.trigger.held = true;
        s_runtime.trigger.clickReserved = true;
        fixture::click = {true, true, 1};
        snap.localShotsFired = 4;
        snap.localAimPunch.x = 1;
        snap.viewAngles.x = -2;
        Tick(settings);
        CHECK(s_runtime.trigger.assist.recoil.pitch == 2);
        CHECK(s_runtime.trigger.assist.targetPawn == snap.players[1].pawn);
        CHECK(fixture::moves == 0);
        fixture::click = {};
        snap.localShotsFired = 0;
        snap.localAimPunch = {};
        Tick(settings);
        CHECK(s_runtime.trigger.assist.recoilRecovering);
        CHECK(std::abs(s_status.moveY) * target::policy::kMouseYawDegrees < 0.4f);
        CHECK(s_status.triggerMove.weaponId == 7 && s_status.aimMove.weaponId == 0);
        CHECK(s_status.workSamples > 0 && s_status.recentPeakWorkUs >= s_status.workUs);
    }

    void TestTriggerWithoutAssistWaitsForPriorAimOutput() {
        auto settings = ResetFixture();
        auto& snap = fixture::snapshot;
        snap.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
        Candidate candidate;
        candidate.player = &snap.players[1]; candidate.slot = 1;
        candidate.point = candidate.player->hitboxes[0].center;
        candidate.point.z += 40;
        MotionRuntimeState motion;
        int x = 0, y = 0;
        CHECK(MoveToward(snap, candidate, {}, motion, 1, false, false, 0.01f,
            &x, &y) == MoveResult::Queued);
        settings.aimbotEnabled = false;
        settings.triggerbotEnabled = true;
        settings.triggerAimAssist = false;
        settings.triggerDelayMs = 0;
        fixture::keyDown = true;
        for (int frame = 0; frame < 20; ++frame) {
            Tick(settings);
            s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        }
        CHECK(fixture::clicks == 0 && fixture::moves == 1);
        CHECK(s_status.fire.reason == target::FireBlockReason::AwaitingView);
    }

    void TestTriggerRecoilFeedbackAndRelease() {
        ResetFixture();
        auto& snap = fixture::snapshot;
        Candidate candidate;
        candidate.player = &snap.players[1]; candidate.slot = 1;
        candidate.point = candidate.player->hitboxes[0].center;
        candidate.requiredHitgroup = 1;
        snap.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
        MotionRuntimeState motion;
        int x = 0, y = 0;
        const auto step = [&] {
            return MoveToward(snap, candidate, {}, motion, 1, false, true,
                0.01f, &x, &y);
        };
        CHECK(step() == MoveResult::Aligned);
        snap.localShotsFired = 1;
        snap.localAimPunch.x = -2.0f;
        FreshFrame();
        CHECK(step() == MoveResult::Queued && y > 0);
        const int deliveredY = y;
        const int sent = fixture::moves;
        for (int i = 0; i < 5; ++i) {
            FreshFrame();
            step();
            CHECK(fixture::moves == sent);
        }
        snap.viewAngles.x += deliveredY * target::policy::kMouseYawDegrees;
        FreshFrame();
        step();
        snap.localShotsFired = 0;
        snap.localAimPunch = {};
        FreshFrame();
        step();
        CHECK(std::abs(y) * target::policy::kMouseYawDegrees <= 0.4f);
        snap.localShotsFired = 1;
        snap.localAimPunchValid = false;
        FreshFrame();
        const int beforeGap = fixture::moves;
        CHECK(step() == MoveResult::Accumulating && fixture::moves == beforeGap);
        CHECK(motion.diagnostics.reason == target::MoveBlockReason::RecoilUnavailable);
    }

    void TestSaturatedRecoilOutput() {
        auto settings = ResetFixture();
        auto& snap = fixture::snapshot;
        snap.sensitivity = 0.5f;
        auto& profile = settings.weaponProfiles[1];
        Candidate candidate;
        candidate.player = &snap.players[1]; candidate.slot = 1;
        candidate.point = snap.players[1].hitboxes[0].center; candidate.requiredHitgroup = 1;
        const float targetYaw = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
        snap.viewAngles = {0, targetYaw, 0};
        MotionRuntimeState motion;
        target::wind::Settings windSettings{4, 40, 1, 40};
        int x = 0, y = 0;
        const auto step = [&] {
            return MoveToward(snap, candidate, {}, motion, 50, true, true,
                0.01f, &x, &y, nullptr, -1, nullptr, true, &windSettings, &profile);
        };
        CHECK(step() == MoveResult::Aligned);
        snap.localShotsFired = 1;
        snap.localAimPunch = {-2.5f, 0.75f, 0};
        for (int tick = 0; tick < 5; ++tick) {
            snap.sampledAtUs += 7000; FreshFrame();
            if (step() == MoveResult::Queued) {
                snap.viewAngles.x += y * 0.011f;
                snap.viewAngles.y -= x * 0.011f;
            }
        }
        CHECK(std::fabs(snap.viewAngles.x - 5.0f) < 0.025f);
        CHECK(std::fabs(snap.viewAngles.y + 1.5f - targetYaw) < 0.025f);
    }

    void TestSeparatedRecoilController() {
        for (const bool wind : {false, true}) {
            auto settings = ResetFixture();
            auto& profile = settings.weaponProfiles[1];
            profile.aimSmoothing = 50;
            auto& snap = fixture::snapshot;
            Candidate candidate;
            candidate.player = &snap.players[1]; candidate.slot = 1;
            candidate.point = snap.players[1].hitboxes[0].center; candidate.requiredHitgroup = 1;
            const float targetYaw = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
            snap.viewAngles = {0, targetYaw, 0};
            snap.localShotsFired = 1;
            MotionRuntimeState motion;
            target::wind::Settings windSettings{4, 40, 1, 40};
            int x = 0, y = 0;
            const auto step = [&] {
                return MoveToward(snap, candidate, {}, motion, profile.aimSmoothing, true, true,
                    0.01f, &x, &y, nullptr, -1, nullptr, true, wind ? &windSettings : nullptr, &profile);
            };
            CHECK(step() == MoveResult::Aligned);
            CHECK(motion.recoil.updatedAtUs == snap.localAimPunchUpdatedAtUs);
            snap.sampledAtUs += 7812; FreshFrame();
            snap.localShotsFired = 1; snap.localAimPunch = {0.5f, -0.25f, 0};
            CHECK(step() == MoveResult::Queued);
            CHECK(std::abs(y + 45) <= 1 && std::abs(x + 23) <= 1);
            CHECK(std::fabs(motion.diagnostics.recoilPitchStep + 1.0f) < 0.0001f);
            snap.viewAngles.x += y * target::policy::kMouseYawDegrees;
            snap.viewAngles.y -= x * target::policy::kMouseYawDegrees;
            const int before = fixture::moves;
            CHECK(step() == MoveResult::Accumulating && fixture::moves == before);
            snap.sampledAtUs += 7812; FreshFrame();
            CHECK(step() == MoveResult::Aligned && fixture::moves == before);
            snap.sampledAtUs += 7812; FreshFrame();
            snap.localShotsFired = 0;
            snap.localShotsFiredValid = false;
            snap.localAimPunch = {0.25f, -0.125f, 0};
            snap.viewAngles = {0, targetYaw, 0};
            CHECK(step() == MoveResult::Accumulating);
            CHECK(motion.diagnostics.reason == target::MoveBlockReason::RecoilUnavailable);
            CHECK(y == 0 && x == 0 && !motion.diagnostics.recoilAvailable);
            snap.sampledAtUs += 7812; FreshFrame();
            snap.localAimPunchUpdatedAtUs -= 40000;
            step();
            CHECK(!motion.diagnostics.recoilAvailable && motion.recoil.updatedAtUs == 0);
            snap.sampledAtUs += 7812; FreshFrame();
            fixture::acceptMove = false;
            snap.localShotsFiredValid = true;
            profile.aimSmoothing = 1;
            profile.aimSoftAssist = false;
            snap.viewAngles = {};
            CHECK(step() == MoveResult::Failed);
            CHECK(motion.recoil.updatedAtUs == 0);
        }

        for (const uint16_t weapon : {uint16_t(1), uint16_t(7), uint16_t(9), uint16_t(17), uint16_t(25), uint16_t(28)}) {
            auto settings = ResetFixture();
            auto& snap = fixture::snapshot;
            snap.localWeaponId = weapon;
            const auto& profile = settings.weaponProfiles[WeaponProfileIndex(weapon)];
            const auto control = ResolveWeaponControlProfile(weapon);
            Candidate candidate;
            candidate.player = &snap.players[1]; candidate.slot = 1;
            candidate.point = snap.players[1].hitboxes[0].center; candidate.requiredHitgroup = 1;
            const float targetYaw = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
            snap.viewAngles = {0, targetYaw, 0};
            MotionRuntimeState motion;
            target::wind::Settings windSettings;
            for (int tick = 0; tick < 96; ++tick) {
                snap.sampledAtUs += 7812; FreshFrame();
                snap.localAimPunch = {0.7f * std::sin(tick * 0.07f), 0.4f * std::sin(tick * 0.11f), 0};
                snap.localShotsFired = 1 + tick / 8;
                int x = 0, y = 0;
                const auto result = MoveToward(snap, candidate, control, motion, 50, true, true,
                    0.01f, &x, &y, nullptr, -1, nullptr, true, &windSettings, &profile);
                CHECK(result != MoveResult::Failed);
                if (result == MoveResult::Queued) {
                    snap.viewAngles.x += y * target::policy::kMouseYawDegrees;
                    snap.viewAngles.y -= x * target::policy::kMouseYawDegrees;
                }
                const float pitchError = snap.viewAngles.x + snap.localAimPunch.x * 2;
                const float yawError = snap.viewAngles.y + snap.localAimPunch.y * 2 - targetYaw;
                CHECK(std::hypot(pitchError, yawError) < 0.065f);
            }
        }
    }

    void TestSoftAssistanceRuntime() {
        auto settings = ResetFixture();
        auto& profile = settings.weaponProfiles[1];
        auto& snap = fixture::snapshot;
        Candidate candidate;
        candidate.player = &snap.players[1]; candidate.slot = 1;
        candidate.point = snap.players[1].hitboxes[0].center; candidate.requiredHitgroup = 1;
        int x = 0, y = 0;
        const auto step = [&](MotionRuntimeState& motion) {
            return MoveToward(snap, candidate, {}, motion, 2.9f, false, false,
                0.01f, &x, &y, nullptr, -1, nullptr, true, nullptr, &profile);
        };
        MotionRuntimeState soft, precise;
        CHECK(step(soft) == MoveResult::Queued);
        const int softX = std::abs(x);
        CHECK(softX * target::policy::kMouseYawDegrees <= 35.0f / 128.0f + 0.0111f);
        profile.aimSoftAssist = false;
        s_outputFeedback = {};
        CHECK(step(precise) == MoveResult::Queued);
        CHECK(std::abs(x) > softX * 2);
        profile.aimSoftAssist = true;
        s_outputFeedback = {};
        snap.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi - 0.08f;
        soft = {};
        CHECK(step(soft) == MoveResult::Aligned && soft.diagnostics.assistanceResting);
        candidate.point.y += 3.8f;
        snap.viewAngles.y = std::atan2(candidate.point.y, candidate.point.x) * 180.0f / kPi - 0.08f;
        soft = {};
        step(soft);
        CHECK(!soft.diagnostics.assistanceResting);
        candidate.point = snap.players[1].hitboxes[0].center;
        profile.aimAssistStrength = 0;
        snap.viewAngles = {};
        soft = {};
        CHECK(step(soft) == MoveResult::Accumulating && x == 0 && y == 0);
        profile.aimAssistStrength = 35;
        candidate.point = {10000, 500, 64};
        snap.players[1].hitboxes[0].center = candidate.point;
        snap.viewAngles.y = std::atan2(500.0f, 10000.0f) * 180.0f / kPi - 0.08f;
        soft = {};
        step(soft);
        CHECK(!soft.diagnostics.assistanceResting);

        settings = ResetFixture();
        fixture::keyDown = true;
        settings.weaponProfiles[1].aimAssistStrength = 0;
        Tick(settings);
        CHECK(s_status.aimKeyDown && fixture::moves == 0);
        snap.localWeaponId = 17;
        snap.localWeaponHandle += 1; snap.localWeaponEntity += 16;
        Tick(settings);
        CHECK(fixture::moves > 0 && s_activeWeaponProfile == 3);
        CHECK(s_runtime.aim.recoil.pitch == 0 && s_runtime.aim.recoil.yaw == 0);
        CHECK(s_runtime.aim.recoil.updatedAtUs == snap.localShotsUpdatedAtUs);
    }

    void TestIdleAimPointRegression() {
        auto settings = ResetFixture();
        settings.weaponProfiles[1].aimDamageCheck = false;
        settings.aimVisibleOnly = false;
        fixture::keyDown = true;
        auto& snap = fixture::snapshot;
        const float targetYaw = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
        snap.viewAngles = {0, targetYaw, 0};
        snap.localShotsFired = 0;
        snap.localAimPunch = {3, -2, 0};
        for (const bool soft : {false, true}) for (const bool predictive : {false, true}) {
            settings.weaponProfiles[1].aimSoftAssist = soft;
            settings.aimPredictive = predictive;
            const int before = fixture::moves;
            Tick(settings);
            CHECK(fixture::moves == before);
            CHECK(!s_status.aimMove.recoilAvailable);
        }
        const auto predicted = PredictAimPoint(snap.players[1], {1000, 50, 64}, 0.1f);
        CHECK(predicted.z == 64);
        snap.players[1].velocityValid = true;
        snap.players[1].velocity = {250, 0, 300};
        const auto jumping = PredictAimPoint(snap.players[1], {1000, 50, 64}, 0.1f);
        CHECK(jumping.z == 64);
    }

    void TestAutomaticPointAndPoseCoherence() {
        auto settings = ResetFixture();
        auto& snap = fixture::snapshot;
        auto& player = snap.players[1];
        const auto capsule = [](Vector3 center, int index, int group) {
            return esp::HitboxCapsule{center - Vector3{0, 0, 2}, center + Vector3{0, 0, 2},
                center, 4, 7, static_cast<uint8_t>(index), static_cast<uint8_t>(group), true};
        };
        player.hitboxCount = 4;
        player.hitboxes[0] = capsule({1000, 10, 80}, 0, 1);
        player.hitboxes[1] = capsule({1000, 10, 64}, 4, 2);
        player.hitboxes[2] = capsule({1000, 10, 40}, 2, 3);
        player.hitboxes[3] = capsule({1000, 0, 64}, 8, 4);
        auto choice = SelectAutomaticPoint(snap, player, 1920, 1080, 50, 0, false, -1);
        CHECK(choice.player && choice.hitboxIndex == 4 && choice.requiredHitgroup == 2);
        CHECK(choice.point.z == 64);
        CHECK(SelectNamedAimPoint(player, 3).z == 40);
        player.hitboxes[0] = capsule({1000, 11, 64}, 0, 1);
        choice = SelectAutomaticPoint(snap, player, 1920, 1080, 50, 0, false, 0);
        CHECK(choice.hitboxIndex == 0);
        player.hitboxes[0] = capsule({1000, 40, 64}, 0, 1);
        choice = SelectAutomaticPoint(snap, player, 1920, 1080, 50, 0, false, 0);
        CHECK(choice.hitboxIndex == 4);
        CHECK(!SelectAutomaticPoint(snap, player, 1920, 1080, 5, 0, false, 4).player);
        fixture::wallBlocks = true;
        CHECK(!SelectAutomaticPoint(snap, player, 1920, 1080, 50, 0, true, 4).player);
        fixture::wallBlocks = false;
        snap.viewAngles.y = std::atan2(10.0f, 1000.0f) * 180.0f / kPi;
        choice = SelectGeometricCrosshairTarget(snap, 5, 1920, 1080, false);
        CHECK(choice.player == &player && choice.requiredHitgroup == 2 && choice.hitboxIndex == 4);
        CHECK(std::fabs(choice.point.z - 64) < 0.001f);
        player.hitboxesUpdatedAtUs -= 1;
        CHECK(!HasCoherentHitboxes(player));
        CHECK(SelectNamedAimPoint(player, 0).y == 50);
        CHECK(!AreTriggerTargetSamplesFresh(snap, player));
        CHECK(SelectAutomaticPoint(snap, player, 1920, 1080, 50, 0, false, 4).hitboxIndex < -1);
        player.hitboxesUpdatedAtUs = player.bonesUpdatedAtUs;
        player.hitboxes[0].center.z = 10000;
        CHECK(!IsUsableCapsule(player.hitboxes[0]));
        CHECK(SelectNamedAimPoint(player, 0).z == player.bones[esp::HEAD].z);
        player.hitboxes[0] = capsule({1000, 55, 64}, 0, 1);
        settings.aimTargetLock = true;
        CHECK(!SelectTarget(snap, 0, 1920, 1080, 50, false, false, player.pawn).player);
        player.hitboxes[0] = capsule({1000, 50, 64}, 0, 1);
        CHECK(SelectTarget(snap, 0, 1920, 1080, 50, false, false, player.pawn).player == &player);
        player.hasHitboxes = false;
        for (auto& bone : player.bones) bone = {2000, 200, 84};
        player.bones[esp::PELVIS] = {1000, -100, 54};
        const auto closest = SelectAimPoint(player, 4, snap.viewMatrix, 1920, 1080, 0);
        const auto projected = WorldToScreen(closest, snap.viewMatrix, 1920, 1080);
        CHECK(std::fabs(projected.x - 960) < 0.001f);
        CHECK(std::fabs(projected.y - 540) < 0.001f);
        CHECK(std::fabs(closest.z - 64) < 0.001f);
    }

    void TestSpawnImmunitySelection() {
        ResetFixture();
        auto& snap = fixture::snapshot;
        auto& protectedPlayer = snap.players[1];
        auto& otherPlayer = snap.players[2];
        otherPlayer = protectedPlayer;
        otherPlayer.pawn = 0x40000;
        otherPlayer.pawnHandle = 0x10004;
        otherPlayer.hitboxes[0].start.y += 30;
        otherPlayer.hitboxes[0].end.y += 30;
        otherPlayer.hitboxes[0].center.y += 30;
        for (auto& bone : otherPlayer.bones) bone.y += 30;
        protectedPlayer.gunGameImmunityValid = true;
        protectedPlayer.gunGameImmunity = true;
        for (const int point : {0, 4, 5}) {
            protectedPlayer.gunGameImmunityUpdatedAtUs = snap.sampledAtUs;
            CHECK(SelectTarget(snap, point, 1920, 1080, 150, false, false).player == &otherPlayer);
            CHECK(SelectTarget(snap, point, 1920, 1080, 150, false, false,
                protectedPlayer.pawn).player == &otherPlayer);
            otherPlayer.valid = false;
            CHECK(!SelectTarget(snap, point, 1920, 1080, 150, false, false).player);
            otherPlayer.valid = true;
            protectedPlayer.gunGameImmunityUpdatedAtUs = snap.sampledAtUs -
                target::policy::kMaximumTriggerTargetAgeUs - 1;
            CHECK(SelectTarget(snap, point, 1920, 1080, 150, false, false).player == &protectedPlayer);
            protectedPlayer.gunGameImmunityUpdatedAtUs = snap.sampledAtUs;
            protectedPlayer.gunGameImmunityValid = false;
            CHECK(SelectTarget(snap, point, 1920, 1080, 150, false, false).player == &protectedPlayer);
            protectedPlayer.gunGameImmunityValid = true;
        }
    }

    void TestSettingTransitionsAndMotionStyle() {
        auto settings = ResetFixture();
        settings.aimVisibleOnly = false;
        settings.weaponProfiles[1].aimDamageCheck = false;
        fixture::keyDown = true;
        auto& snap = fixture::snapshot;
        snap.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
        Tick(settings);
        fixture::keyDown = false;
        Tick(settings);
        CHECK(s_status.aimKeyDown);
        for (int i = 0; i < 16; ++i) {
            s_runtime.aim.mouseRemainderX = 50;
            s_runtime.aim.mouseRemainderY = -50;
            s_runtime.aim.humanInitialDistance = 60;
            s_runtime.aim.recoil = {10, 20, 1, snap.sampledAtUs};
            if (i & 1) settings.aimPredictive = !settings.aimPredictive;
            else settings.weaponProfiles[1].aimSoftAssist = !settings.weaponProfiles[1].aimSoftAssist;
            const int previousMoves = fixture::moves;
            const int previousCancels = fixture::moveCancellations;
            Tick(settings);
            CHECK(s_status.aimKeyDown && fixture::moves == previousMoves);
            CHECK(fixture::moveCancellations > previousCancels);
            CHECK(s_runtime.aim.mouseRemainderX == 0 && s_runtime.aim.mouseRemainderY == 0);
            CHECK(s_runtime.aim.recoil.pitch == 0 && s_runtime.aim.recoil.yaw == 0);
            CHECK(s_runtime.aim.recoil.updatedAtUs == snap.localShotsUpdatedAtUs);
        }
        fixture::keyDown = true;
        Tick(settings);
        CHECK(!s_status.aimKeyDown);
        settings = ResetFixture();
        const auto triggerKey = target::settings_policy::MotionKey(settings, settings.weaponProfiles[1], true);
        const auto aimKey = target::settings_policy::MotionKey(settings, settings.weaponProfiles[1], false);
        settings.fovEnabled = !settings.fovEnabled;
        CHECK(aimKey == target::settings_policy::MotionKey(settings, settings.weaponProfiles[1], false));
        settings.weaponProfiles[1].aimSoftAssist = !settings.weaponProfiles[1].aimSoftAssist;
        CHECK(aimKey != target::settings_policy::MotionKey(settings, settings.weaponProfiles[1], false));
        CHECK(triggerKey == target::settings_policy::MotionKey(settings, settings.weaponProfiles[1], true));
        for (auto& profile : settings.weaponProfiles) {
            profile.aimMotionStyle = -1;
            settings.aimHumanization = false; profile.aimWindMouse = false;
            CHECK(target::settings_policy::AimMotionStyle(settings, profile) == 0);
            settings.aimHumanization = true;
            CHECK(target::settings_policy::AimMotionStyle(settings, profile) == 1);
            profile.aimWindMouse = true;
            CHECK(target::settings_policy::AimMotionStyle(settings, profile) == 2);
            for (int style = 0; style < 3; ++style) {
                profile.aimMotionStyle = style;
                CHECK(target::settings_policy::AimMotionStyle(settings, profile) == style);
            }
        }
        for (int style = 0; style < 3; ++style) {
            settings = ResetFixture();
            settings.weaponProfiles[1].aimMotionStyle = style;
            settings.weaponProfiles[1].aimWindMouse = style != 2;
            settings.aimHumanization = style != 1;
            fixture::keyDown = true;
            Tick(settings);
            CHECK(s_status.phase == target::RuntimePhase::Tracking && fixture::moves > 0);
            CHECK(fixture::clicks == 0);
            const auto key = target::settings_policy::MotionKey(settings, settings.weaponProfiles[1], false);
            settings.aimHumanization = !settings.aimHumanization;
            settings.weaponProfiles[1].aimWindMouse = !settings.weaponProfiles[1].aimWindMouse;
            CHECK(key == target::settings_policy::MotionKey(settings, settings.weaponProfiles[1], false));
        }
    }

    void TestReactionAndForceCenter() {
        auto settings = ResetFixture();
        settings.weaponProfiles[1].aimReactionMs = 100;
        fixture::keyDown = true;
        Tick(settings);
        CHECK(fixture::moves == 0 && s_status.phase == target::RuntimePhase::Reacting);
        fixture::snapshot.sampledAtUs += 110000;
        Tick(settings);
        CHECK(fixture::moves > 0 && s_status.phase == target::RuntimePhase::Tracking);
        const int moves = fixture::moves;
        fixture::snapshot.players[1].pawnHandle += 0x10000;
        Tick(settings);
        CHECK(fixture::moves == moves && s_status.phase == target::RuntimePhase::Reacting);
        fixture::snapshot.players[1].valid = false;
        Tick(settings);
        CHECK(s_runtime.aim.acquiredAtUs == 0);
        settings = ResetFixture();
        auto& snap = fixture::snapshot;
        Candidate candidate;
        candidate.player = &snap.players[1]; candidate.point = snap.players[1].hitboxes[0].center;
        candidate.requiredHitgroup = 1; candidate.slot = 1;
        snap.viewAngles.y = std::atan2(53.5f, 1000.0f) * 180.0f / kPi;
        CHECK(EvaluateShot(snap, candidate, 0, 1, false, {}, 0, nullptr, false, false).damageReady);
        CHECK(EvaluateShot(snap, candidate, 0, 1, false, {}, 0, nullptr, false, true).reason == target::FireBlockReason::CapsuleEdge);
        snap.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
        CHECK(EvaluateShot(snap, candidate, 0, 1, false, {}, 0, nullptr, false, true).damageReady);
        settings.aimbotEnabled = false; settings.triggerbotEnabled = true;
        settings.triggerActivationMode = 1;
        settings.weaponProfiles[1].triggerForceCenter = true;
        settings.weaponProfiles[1].seedWindowEnabled = false;
        snap.viewAngles.y = std::atan2(53.5f, 1000.0f) * 180.0f / kPi;
        fixture::keyDown = true;
        Tick(settings);
        CHECK(fixture::moves > 0 && fixture::clicks == 0);
    }

    void TestAimPointOptionMatrix() {
        for (const uint16_t weapon : {uint16_t(1), uint16_t(7), uint16_t(9), uint16_t(17), uint16_t(25), uint16_t(28)})
        for (int point = 0; point < 6; ++point) for (int flags = 0; flags < 8; ++flags) {
            auto settings = ResetFixture();
            auto& snap = fixture::snapshot;
            snap.localWeaponId = weapon;
            snap.localAimPunch = {3, -2, 0};
            snap.localShotsFired = 0;
            settings.aimBone = point;
            settings.aimPredictive = (flags & 1) != 0;
            settings.aimVisibleOnly = false;
            auto& profile = settings.weaponProfiles[WeaponProfileIndex(weapon)];
            profile.aimSoftAssist = (flags & 2) != 0;
            profile.aimWindMouse = (flags & 4) != 0;
            profile.aimDamageCheck = false;
            fixture::keyDown = true;
            Tick(settings);
            CHECK(s_status.phase == target::RuntimePhase::Tracking);
            CHECK(s_status.moveX <= 0);
            CHECK(profile.aimWindMouse ? std::abs(s_status.moveY) <=
                std::abs(s_status.moveX) * 0.2f + 1.0f : s_status.moveY == 0);
            CHECK(!s_status.aimMove.recoilAvailable);
            CHECK(fixture::clicks == 0);
        }
    }

    void TestViewOffsetReadPath() {
        runtime_offsets::Values offsets;
        offsets.C_BaseModelEntity_m_vecViewOffset = 0xE78;
        std::array<uint8_t, 96> raw;
        raw.fill(0xFF);
        const auto writeFloat = [&](size_t offset, float value) {
            std::memcpy(raw.data() + offset, &value, sizeof(value));
        };
        writeFloat(0x10, 1.25f); writeFloat(0x18, -2.5f); writeFloat(0x20, 64.0f);
        for (const auto kind : {app::input::DeviceKind::Makcu, app::input::DeviceKind::KmBox,
                app::input::DeviceKind::KmBoxNet, app::input::DeviceKind::FerrumOne}) {
            auto settings = ResetFixture();
            settings.weaponProfiles[1].aimDamageCheck = false;
            settings.aimVisibleOnly = false;
            fixture::geometryAvailable = false;
            fixture::device.selected = kind;
            fixture::keyDown = true;
            Vector3 legacy;
            std::memcpy(&legacy, raw.data(), sizeof(legacy));
            fixture::snapshot.localEyeValid = esp::data::IsValidViewOffset(legacy);
            Tick(settings);
            CHECK(fixture::moves == 0 && s_status.phase == target::RuntimePhase::OutputFailed);
            CHECK(s_status.aimMove.reason == target::MoveBlockReason::EyeUnavailable);
            esp::data::ViewOffsetSample sample;
            const uintptr_t pawn = fixture::snapshot.localPawn;
            CHECK(sample.Prepare(pawn, offsets.C_BaseModelEntity_m_vecViewOffset,
                offsets.CNetworkViewOffsetVector_m_vecX, offsets.CNetworkViewOffsetVector_m_vecY,
                offsets.CNetworkViewOffsetVector_m_vecZ));
            CHECK(sample.address == pawn + 0xE78 && sample.size == 36);
            std::memcpy(sample.bytes.data(), raw.data(), sample.size);
            Vector3 decoded;
            CHECK(sample.Decode(pawn, sample.size, decoded));
            CHECK(decoded.x == 1.25f && decoded.y == -2.5f && decoded.z == 64.0f);
            const Vector3 packed{0.5f, -1.0f, 46.0f};
            std::memcpy(sample.bytes.data(), &packed, sizeof(packed));
            CHECK(sample.Decode(pawn, sample.size, decoded));
            CHECK(decoded.x == packed.x && decoded.y == packed.y && decoded.z == packed.z);
            std::memcpy(sample.bytes.data(), raw.data(), sample.size);
            CHECK(sample.Decode(pawn, sample.size, decoded));
            fixture::snapshot.localEyePos = decoded;
            fixture::snapshot.localEyeValid = true;
            Tick(settings);
            CHECK(fixture::moves > 0 && s_status.phase == target::RuntimePhase::Tracking);
            CHECK(fixture::mapRequests == 0);
            CHECK(!sample.Decode(pawn + 0x100, sample.size, decoded));
            for (size_t complete = 0; complete < sample.size; ++complete)
                CHECK(!sample.Decode(pawn, complete, decoded));
            CHECK(!sample.Decode(pawn, sample.size + 1, decoded));
            CHECK(sample.Prepare(pawn, offsets.C_BaseModelEntity_m_vecViewOffset, 0x20, 0x28, 0x30));
            CHECK(!sample.Decode(pawn, 0, decoded));
            sample.bytes.fill(0xFF);
            const std::array<float, 3> moved{0.0f, 0.0f, 46.0f};
            for (size_t i = 0; i < moved.size(); ++i)
                std::memcpy(sample.bytes.data() + sample.components[i], &moved[i], sizeof(float));
            CHECK(sample.Decode(pawn, sample.size, decoded) && decoded.z == 46.0f);
            CHECK(!sample.Prepare(0, 0x100, 0x10, 0x18, 0x20));
            CHECK(!sample.Prepare(pawn, 0, 0x10, 0x18, 0x20));
            CHECK(sample.Prepare(pawn, 0x100, 0x10, 0x10, 0x20) && !sample.hasComponents);
            CHECK(sample.Prepare(pawn, 0x100, -1, 0x18, 0x20) && !sample.hasComponents);
            CHECK(sample.Prepare(pawn, 0x100, 0x10, 0x18, 0x100) && !sample.hasComponents);
            CHECK(sample.size == sizeof(Vector3));
            std::memcpy(sample.bytes.data(), &packed, sizeof(packed));
            CHECK(sample.Decode(pawn, sample.size, decoded) && decoded.z == packed.z);
        }
        for (const float height : {46.0f, 64.0f, 8.0f, 96.0f, 0.0f, 97.0f,
                std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
            esp::data::ViewOffsetSample sample;
            CHECK(sample.Prepare(0x10000, 0x100, 0x10, 0x18, 0x20));
            writeFloat(0x20, height);
            std::memcpy(sample.bytes.data(), raw.data(), sample.size);
            Vector3 decoded;
            const bool expected = std::isfinite(height) && height >= 8 && height <= 96;
            CHECK(sample.Decode(0x10000, sample.size, decoded) == expected);
            if (expected) CHECK(decoded.z == height);
        }
    }

    void TestSnapshotClockDomain() {
        auto settings = ResetFixture();
        fixture::keyDown = true;
        Tick(settings);
        CHECK(NowUs() > fixture::snapshot.viewUpdatedAtUs + target::policy::kMaximumTargetViewAgeUs);
        CHECK(fixture::moves > 0 && s_status.phase == target::RuntimePhase::Tracking);
        CHECK(s_status.aimMove.reason == target::MoveBlockReason::None);
        CHECK(s_status.aimMove.validForUs > 0 &&
            s_status.aimMove.validForUs <= target::policy::kMaximumTargetViewAgeUs);
        Candidate candidate;
        candidate.player = &fixture::snapshot.players[1];
        candidate.point = candidate.player->hitboxes[0].center;
        MotionRuntimeState motion;
        int x = 0, y = 0;
        const int before = fixture::moves;
        fixture::clockAdvanceUs = target::policy::kMaximumTargetViewAgeUs + 1;
        CHECK(MoveToward(fixture::snapshot, candidate, {}, motion, 1, false, false,
            0.006f, &x, &y) == MoveResult::Failed);
        CHECK(motion.diagnostics.reason == target::MoveBlockReason::ViewExpired);
        CHECK(motion.diagnostics.validForUs == 0);
        CHECK(fixture::moves == before);
        fixture::clockAdvanceUs = 0;
        s_outputFeedback = {};
        FreshFrame(); motion = {};
        CHECK(MoveToward(fixture::snapshot, candidate, {}, motion, 1, false, false,
            0.006f, &x, &y) == MoveResult::Queued);
        FreshFrame(); motion = {};
        s_outputFeedback = {};
        fixture::acceptMove = false;
        CHECK(MoveToward(fixture::snapshot, candidate, {}, motion, 1, false, false,
            0.006f, &x, &y) == MoveResult::Failed);
        CHECK(motion.diagnostics.reason == target::MoveBlockReason::OutputRejected);
        CHECK(motion.diagnostics.validForUs > 0);
        fixture::acceptMove = true;
        FreshFrame(); motion = {};
        fixture::snapshot.viewUpdatedAtUs += 1000000;
        CHECK(MoveToward(fixture::snapshot, candidate, {}, motion, 1, false, false,
            0.006f, &x, &y) == MoveResult::Failed);
        CHECK(motion.diagnostics.reason == target::MoveBlockReason::ViewExpired);
        CHECK(motion.diagnostics.viewAgeUs == -1);
    }

    void TestIndependentAimAndMenu() {
        CHECK(!g::TargetSettings{}.weaponProfiles[1].aimDamageCheck);
        for (const int mode : {0, 1}) {
            auto settings = ResetFixture();
            settings.aimActivationMode = mode;
            settings.weaponProfiles[1].aimDamageCheck = false;
            settings.aimVisibleOnly = false;
            fixture::geometryAvailable = false;
            fixture::snapshot.players[1].hasHitboxes = false;
            fixture::snapshot.players[1].visible = false;
            fixture::snapshot.localWeaponTelemetryValid = false;
            g::menuOpen = true;
            fixture::keyDown = true;
            Tick(settings);
            CHECK(s_status.aimKeyDown && fixture::moves > 0 && !s_status.pausedByMenu);
            CHECK(fixture::mapRequests == 0 && fixture::clicks == 0);
            int before = fixture::moves;
            settings.aimVisibleOnly = true;
            Tick(settings);
            CHECK(fixture::moves == before && s_status.geometry.aimVisibilityDeferred);
            CHECK(s_status.aimMove.reason == target::MoveBlockReason::AwaitingView);
            fixture::snapshot.players[1].visible = true;
            Tick(settings);
            CHECK(fixture::moves == before && s_status.aimMove.reason == target::MoveBlockReason::AwaitingView);
            settings.weaponProfiles[1].aimDamageCheck = true;
            before = fixture::moves;
            Tick(settings);
            CHECK(fixture::moves == before && s_status.geometry.aimDamageDeferred);
            CHECK(s_status.aimMove.reason == target::MoveBlockReason::AwaitingView);
            fixture::geometryAvailable = true;
            before = fixture::moves;
            Tick(settings);
            CHECK(fixture::moves == before && s_status.aimSelection.missingBallistics == 1);
            settings.weaponProfiles[1].aimDamageCheck = false;
            g::targetKeyCaptureUntilMs = GetTickCount64() + 500;
            Tick(settings);
            CHECK(fixture::moves == before && s_status.pausedByMenu);
            g::targetKeyCaptureUntilMs = GetTickCount64();
            fixture::snapshot.sampledAtUs += 110000;
            Tick(settings);
            CHECK(fixture::moves > before && !s_status.pausedByMenu);
            fixture::keyDown = false; Tick(settings);
            if (mode == 1) { fixture::keyDown = true; Tick(settings); }
            CHECK(!s_status.aimKeyDown);
        }
        CHECK(!target::policy::IsInputCaptureActive(false, 200, 100));
        CHECK(!target::policy::IsInputCaptureActive(true, 100, 100));
        CHECK(target::policy::IsInputCaptureActive(true, 101, 100));
        CHECK(target::policy::NeedsVisibilityData(true, true, false, false, false));
    }

    void TestGeometryAvailabilityTransitions() {
        auto settings = ResetFixture();
        settings.weaponProfiles[1].aimDamageCheck = true;
        settings.weaponProfiles[1].aimAutowall = true;
        fixture::snapshot.localWeaponDamage = 1;
        fixture::snapshot.players[1].visible = false;
        fixture::keyDown = true;
        for (const auto state : {target::physics::BuildState::Idle, target::physics::BuildState::Queued,
                target::physics::BuildState::Building, target::physics::BuildState::Failed}) {
            fixture::geometryState = state;
            const int before = fixture::moves;
            fixture::snapshot.sampledAtUs += 110000;
            Tick(settings);
            CHECK(fixture::moves > before && fixture::clicks == 0);
            CHECK(s_status.geometry.evaluated && !s_status.geometry.ready);
            CHECK(s_status.geometry.aimVisibilityDeferred && s_status.geometry.aimDamageDeferred);
            CHECK(!s_status.geometry.aimAutowallActive);
            CHECK(settings.aimVisibleOnly && settings.weaponProfiles[1].aimDamageCheck &&
                settings.weaponProfiles[1].aimAutowall);
        }
        fixture::geometryState = target::physics::BuildState::Ready;
        fixture::wallBlocks = true;
        fixture::snapshot.players[1].visible = true;
        int before = fixture::moves;
        Tick(settings);
        CHECK(fixture::moves == before && s_status.aimSelection.damageRejected == 1);
        CHECK(!IsTargetVisible(fixture::snapshot, fixture::snapshot.players[1],
            fixture::snapshot.players[1].hitboxes[0].center));
        CHECK(s_status.geometry.ready && !s_status.geometry.aimVisibilityDeferred &&
            !s_status.geometry.aimDamageDeferred && s_status.geometry.aimAutowallActive);
        fixture::wallBlocks = false;
        Tick(settings);
        CHECK(fixture::moves == before && s_status.aimSelection.damageRejected == 1);
        strcpy_s(fixture::snapshot.mapKey, "next_map");
        fixture::snapshot.sampledAtUs += 110000;
        Tick(settings);
        CHECK(fixture::moves > before && !s_status.geometry.ready && s_status.geometry.aimDamageDeferred);
        fixture::geometryMap = "next_map";
        before = fixture::moves;
        Tick(settings);
        CHECK(fixture::moves == before && s_status.geometry.ready && s_status.aimSelection.damageRejected == 1);
        settings.weaponProfiles[1].aimDamageCheck = false;
        fixture::snapshot.sampledAtUs += 110000;
        Tick(settings);
        CHECK(fixture::moves > before && !s_status.geometry.aimAutowallActive);
    }

    void TestTriggerAssistWithoutGeometry() {
        auto settings = ResetFixture();
        settings.aimbotEnabled = false;
        settings.triggerbotEnabled = true;
        settings.triggerDelayMs = 0;
        settings.weaponProfiles[1].hitchanceEnabled = false;
        settings.weaponProfiles[1].seedWindowEnabled = false;
        fixture::geometryAvailable = false;
        fixture::snapshot.players[1].visible = false;
        fixture::keyDown = true;
        Tick(settings);
        CHECK(fixture::moves > 0 && fixture::clicks == 0);
        CHECK(s_status.geometry.triggerVisibilityDeferred && !s_status.geometry.triggerAutowallActive);
        fixture::snapshot.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
        for (int frame = 0; frame < 20; ++frame) {
            Tick(settings);
            s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        }
        CHECK(fixture::clicks == 0 && s_status.fire.reason == target::FireBlockReason::WorldUnavailable);
        fixture::snapshot.players[1].visible = true;
        for (int frame = 0; frame < 20; ++frame) {
            Tick(settings);
            s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        }
        CHECK(fixture::clicks == 1);
        CHECK(settings.triggerVisibleOnly && settings.weaponProfiles[1].autowall);
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
        fixture::haveSnapshot = true; Tick(settings);
        CHECK(fixture::moves == beforeGap && s_status.aimMove.reason == target::MoveBlockReason::AwaitingView);
        fixture::snapshot.sampledAtUs += 110000; Tick(settings); CHECK(fixture::moves > beforeGap);
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

    void TestMotionIdentityAndOptions() {
        for (bool trigger : {false, true}) {
            auto settings = ResetFixture();
            settings.aimbotEnabled = !trigger;
            settings.triggerbotEnabled = trigger;
            settings.triggerActivationMode = 1;
            fixture::keyDown = true;
            Tick(settings);
            auto& motion = trigger ? s_runtime.trigger.assist : static_cast<MotionRuntimeState&>(s_runtime.aim);
            CHECK(motion.targetPawn == fixture::snapshot.players[1].pawn);
            CHECK(motion.targetSlot == 1);
            fixture::snapshot.players[2] = fixture::snapshot.players[1];
            auto& second = fixture::snapshot.players[2];
            second.pawn += 0x1000;
            second.pawnHandle += 1;
            second.hitboxes[0].start.y = second.hitboxes[0].end.y = second.hitboxes[0].center.y = 20;
            for (auto& bone : second.bones) bone.y = 20;
            Tick(settings);
            CHECK(motion.targetSlot == 1);
            settings.aimTargetLock = settings.triggerTargetLock = false;
            Tick(settings);
            CHECK(motion.targetSlot == 2);
            motion.lastViewIssuedAtUs = UINT64_MAX;
            const int before = fixture::moves;
            second.pawnHandle += 0x10000;
            Tick(settings);
            CHECK(motion.targetPawnHandle == second.pawnHandle);
            CHECK(motion.lastViewIssuedAtUs != UINT64_MAX);
            CHECK(fixture::moves == before && motion.diagnostics.reason == target::MoveBlockReason::AwaitingView);
            second.health = 0;
            Tick(settings);
            CHECK(motion.targetSlot == 1);
        }
        ResetFixture();
        Candidate candidate;
        candidate.player = &fixture::snapshot.players[1];
        candidate.point = {1000, 17.455f, 64};
        MotionRuntimeState legacy, adaptive;
        int legacyX = 0, legacyY = 0, adaptiveX = 0, adaptiveY = 0;
        CHECK(MoveToward(fixture::snapshot, candidate, {}, legacy,
            4, false, false, 0.006f, &legacyX, &legacyY) == MoveResult::Queued);
        s_outputFeedback = {};
        CHECK(MoveToward(fixture::snapshot, candidate, {}, adaptive,
            4, false, false, 0.006f, &adaptiveX, &adaptiveY, nullptr, -1, nullptr, true) == MoveResult::Queued);
        CHECK(std::abs(adaptiveX) > 0 && std::abs(adaptiveX) < std::abs(legacyX));
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
        fixture::recoilScale = 1.5f;
        fixture::recoilScaleValid = true;
        Tick(settings);
        CHECK(s_recoilScale == 1.5f && s_runtime.trigger.shotLatched && fixture::clicks == 1);
        fixture::recoilScale = std::numeric_limits<float>::quiet_NaN();
        Tick(settings);
        CHECK(s_recoilScale == 2.0f && s_runtime.trigger.shotLatched && fixture::clicks == 1);
        fixture::recoilScaleValid = false;
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
    snapshot.localShotsFired = 0;
    CHECK(MoveToward(snapshot, candidate, {}, motion, 1, false, true, 0.01f,
        nullptr, nullptr, &angularError, 80, &convars) == MoveResult::Aligned);
    snapshot.localShotsFired = 1;
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
        CHECK(planned.predictionOffset.y > 0 && planned.predictionOffset.y <= 1.5f);
        snapshot.viewAngles.y = std::atan2(planned.point.y, planned.point.x) * 180.0f / kPi;
        fixture::keyDown = true;
        Tick(settings);
        s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    }
    CHECK(fixture::clicks == 1);
    CHECK(s_status.fire.reason == target::FireBlockReason::Queued);
}

void TestSeedClockValidation() {
    for (int scenario = 0; scenario < 8; ++scenario) {
        ResetFixture();
        auto& snap = fixture::snapshot;
        snap.viewAngles.y = std::atan2(50.0f, 1000.0f) * 180.0f / kPi;
        Candidate candidate;
        candidate.player = &snap.players[1]; candidate.slot = 1;
        candidate.point = candidate.player->hitboxes[0].center;
        candidate.requiredHitgroup = 1;
        float lead = 0;
        if (scenario == 0) snap.localRenderTick = -1;
        if (scenario == 1) snap.localRenderTick = INT_MAX;
        if (scenario == 2) snap.localIntervalPerTick = 0;
        if (scenario == 3) snap.localIntervalPerTick = std::numeric_limits<float>::quiet_NaN();
        if (scenario == 4) lead = std::numeric_limits<float>::quiet_NaN();
        if (scenario == 5) snap.localWeaponTelemetryUpdatedAtUs = 0;
        if (scenario == 6) snap.localWeaponTelemetryUpdatedAtUs = snap.sampledAtUs + 1;
        if (scenario == 7) snap.localWeaponTelemetryUpdatedAtUs = snap.sampledAtUs - 1000000;
        const auto checked = EvaluateShot(snap, candidate, 0, 0, false, {}, lead);
        CHECK(!checked.damageReady);
        CHECK(checked.predictedSeedHitFraction < 0);
        const auto unchecked = EvaluateShot(snap, candidate, 0, 0, false, {}, lead, nullptr, false);
        CHECK(unchecked.damageReady);
    }
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

#if defined(KEVQ_TARGET_INPUT_INTEGRATION)
#include "network_target_integration.inl"
int main() { return RunNetworkTargetIntegration(); }
#else
#include "audit_regressions.inl"

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--view-offset") {
        TestViewOffsetReadPath();
        return failures ? 1 : 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--snapshot-clock") {
        TestSnapshotClockDomain();
        return failures ? 1 : 0;
    }
    for (const auto kind : {app::input::DeviceKind::Makcu, app::input::DeviceKind::KmBox,
            app::input::DeviceKind::KmBoxNet, app::input::DeviceKind::FerrumOne}) {
        for (const int key : {2, 4, 5, 6}) for (const int mode : {0, 1}) {
            auto settings = ResetFixture();
            settings.aimKey = key; settings.aimActivationMode = mode;
            settings.weaponProfiles[1].aimWindMouse = true;
            fixture::device.selected = kind;
            fixture::device.physicalButtonsAvailable = true;
            fixture::device.physicalButtonMask = 0;
            fixture::primaryKey = key;
            fixture::keyDown = true;
            Tick(settings, true);
            CHECK(fixture::moves == 0 && s_status.pausedByMenu && !s_status.aimKeyDown);
            fixture::keyDown = false; Tick(settings);
            fixture::keyDown = true; Tick(settings);
            CHECK(s_status.aimKeyDown && fixture::moves > 0);
            const int beforeRelease = fixture::moves;
            fixture::keyDown = false; Tick(settings);
            if (mode == 1) { fixture::keyDown = true; Tick(settings); }
            CHECK(!s_status.aimKeyDown && s_runtime.aim.windUpdatedAtUs == 0);
            if (mode == 0) CHECK(fixture::moves == beforeRelease);
            const int beforeMenu = fixture::moves;
            Tick(settings, true);
            CHECK(fixture::moves == beforeMenu && !s_status.aimKeyDown);
        }
    }
    {
        using namespace target::wind;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const auto safe = Sanitize({nan, -1.0f, 999.0f, nan});
        CHECK(safe.gravity == 18.0f && safe.wind == 0.0f && safe.maxStep == 40.0f && safe.distance == 12.0f);
        State state;
        auto step = Advance(state, {10, 0}, {100, 0}, 1.0f / 128, 1, {}, 0, 1);
        CHECK(step.x > 0 && step.y > 0 && std::hypot(step.x, step.y) <= 10.0001f);
        step = Advance(state, {-10, 0}, {-100, 0}, 1.0f / 128, 1, {}, 0, -1);
        CHECK(step.x < 0 && step.y < 0);
        step = Advance(state, {0.25f, 0.125f}, {0.5f, 0.25f}, 1.0f / 128, 1, {}, 1, 1);
        CHECK(step.x == 0.25f && step.y == 0.125f && state.windY == 0 && state.velocityX == 0);
        step = Advance(state, {nan, 1}, {100, 0}, 1.0f / 128, 1, {}, 1, 1);
        CHECK(step.x == 0 && step.y == 0);
        step = Advance(state, {0, 0}, {100, 0}, 1.0f / 128, 1, {}, 1, 1);
        CHECK(step.x == 0 && step.y == 0 && state.windX == 0);
        for (const int hz : {64, 128, 250}) {
            for (const Settings config : {Settings{}, Settings{4, 40, 1, 40}, Settings{40, 0, 40, 1}}) {
                state = {};
                Step remaining{300, -120};
                for (int tick = 0; tick < hz * 8; ++tick) {
                    const float before = std::hypot(remaining.x, remaining.y);
                    const float fraction = target::policy::TimeAdjustedSmoothing(0.1f, 1.0f / hz);
                    step = Advance(state, {remaining.x * fraction, remaining.y * fraction}, remaining,
                        1.0f / hz, 1, config, std::sin(tick * 0.15f), std::cos(tick * 0.19f));
                    CHECK(std::isfinite(step.x) && std::isfinite(step.y));
                    CHECK(std::hypot(step.x, step.y) <= before * fraction + 0.0002f);
                    CHECK(std::hypot(step.x, step.y) <= config.maxStep * 128.0f / hz + 0.0002f);
                    remaining.x -= step.x; remaining.y -= step.y;
                    CHECK(std::hypot(remaining.x, remaining.y) <= before + 0.0001f);
                }
                CHECK(std::hypot(remaining.x, remaining.y) < 0.002f);
            }
        }
        auto settings = ResetFixture();
        settings.weaponProfiles[1].aimWindMouse = true;
        fixture::keyDown = true; Tick(settings);
        CHECK(fixture::moves == 1 && s_runtime.aim.windUpdatedAtUs != 0);
        CHECK(std::hypot(s_runtime.aim.windState.velocityX, s_runtime.aim.windState.velocityY) > 0);
        const auto before = fixture::moves;
        fixture::haveSnapshot = false; Tick(settings);
        CHECK(fixture::moves == before && s_runtime.aim.windUpdatedAtUs == 0);
        fixture::haveSnapshot = true; Tick(settings);
        CHECK(s_runtime.aim.diagnostics.reason == target::MoveBlockReason::AwaitingView);
        fixture::snapshot.sampledAtUs += 110000; Tick(settings);
        CHECK(s_runtime.aim.windUpdatedAtUs != 0);
        fixture::snapshot.players[1].pawnHandle++;
        Tick(settings);
        CHECK(s_runtime.aim.targetPawnHandle == fixture::snapshot.players[1].pawnHandle);
        CHECK(std::hypot(s_runtime.aim.windState.velocityX, s_runtime.aim.windState.velocityY) <= 10.001f);
        settings.weaponProfiles[1].aimWindMouse = false; Tick(settings);
        CHECK(s_runtime.aim.windUpdatedAtUs == 0 && s_runtime.aim.windState.windX == 0);
        settings.weaponProfiles[1].aimWindMouse = true;
        fixture::keyDown = false; Tick(settings);
        fixture::keyDown = true; Tick(settings);
        CHECK(!s_aimActivation.toggled && s_runtime.aim.windUpdatedAtUs == 0);
        settings = ResetFixture();
        settings.aimbotEnabled = false; settings.triggerbotEnabled = true;
        settings.weaponProfiles[1].aimWindMouse = true;
        fixture::keyDown = true; Tick(settings);
        CHECK(fixture::moves > 0 && s_runtime.trigger.assist.windUpdatedAtUs == 0);
        CHECK(s_runtime.trigger.assist.windState.velocityX == 0 && s_runtime.trigger.assist.windState.windY == 0);
    }
    TestMotionIdentityAndOptions();
    {
        auto settings = ResetFixture();
        settings.aimbotEnabled = false; settings.triggerbotEnabled = true;
        settings.triggerDelayMs = 0; settings.triggerActivationMode = 1;
        auto& head = fixture::snapshot.players[1].hitboxes[0];
        head.start.y = head.center.y = head.end.y = 0;
        fixture::keyDown = true;
        fixture::device.moveInFlight = true;
        for (int i = 0; i < 20; ++i) {
            Tick(settings);
            s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        }
        CHECK(fixture::clicks == 0 && !s_runtime.trigger.shotLatched);
        CHECK(s_status.fire.reason == target::FireBlockReason::OutputBusy);
        CHECK(s_status.phase != target::RuntimePhase::OutputFailed);
        fixture::device.moveInFlight = false;
        for (int i = 0; i < 10; ++i) {
            Tick(settings);
            s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        }
        CHECK(fixture::clicks == 1 && s_runtime.trigger.shotLatched);
    }
    {
        auto settings = ResetFixture();
        settings.aimKey = settings.triggerKey = 0x06;
        settings.aimActivationMode = settings.triggerActivationMode = 0;
        settings.triggerbotEnabled = true;
        fixture::alternateKeySamples = true;
        Tick(settings);
        CHECK(fixture::activationReads == 1);
        CHECK(s_status.aimKeyDown && s_status.triggerKeyDown);
        Tick(settings);
        CHECK(fixture::activationReads == 2);
        CHECK(!s_status.aimKeyDown && !s_status.triggerKeyDown);
        CHECK(fixture::moveCancellations > 0);
    }
    {
        auto settings = ResetFixture();
        fixture::keyDown = true;
        FreshFrame();
        fixture::snapshot.snapshotAgeUs = 20000;
        fixture::snapshot.viewUpdatedAtUs = fixture::snapshot.sampledAtUs - 20000;
        CHECK(ValidTargetFrame(fixture::snapshot));
        fixture::snapshot.snapshotAgeUs = kMaximumTargetSnapshotAgeUs + 1;
        CHECK(!ValidTargetFrame(fixture::snapshot));
        fixture::snapshot.snapshotAgeUs = 20000;
        fixture::snapshot.viewUpdatedAtUs = fixture::snapshot.sampledAtUs -
            target::policy::kMaximumTargetViewAgeUs;
        CHECK(!ValidTargetFrame(fixture::snapshot));
        TickTarget(1920, 1080, settings, false);
        CHECK(s_status.phase == target::RuntimePhase::DataUnavailable);
        CHECK(fixture::moves == 0 && fixture::moveCancellations > 0);
    }
    for (const auto kind : {app::input::DeviceKind::KmBoxNet, app::input::DeviceKind::FerrumOne}) {
        for (const int key : {0x06, 0x75, 0x10}) {
            for (const int mode : {0, 1}) {
                auto settings = ResetFixture();
                settings.aimKey = key; settings.aimActivationMode = mode;
                fixture::keyboardAvailable = false;
                fixture::device.selected = kind;
                const auto report = [&](bool down, bool available) {
                    std::array<uint8_t, 20> bytes = {};
                    bytes[1] = down && key == 0x06 ? 0x10 : 0;
                    bytes[9] = down && key == 0x10 ? 0x20 : 0;
                    bytes[10] = down && key == 0x75 ? 0x3F : 0;
                    CHECK(app::input::ParseNetworkInputReport(bytes,
                        fixture::device.physicalButtonMask, fixture::device.physicalKeyboard));
                    fixture::device.physicalButtonsAvailable = available;
                    fixture::device.physicalKeyboard.available = available;
                };
                report(true, true); Tick(settings);
                CHECK(fixture::moves > 0);
                CHECK(s_status.phase == target::RuntimePhase::Tracking);
                const int beforeUnknown = fixture::moves;
                report(true, false); Tick(settings);
                CHECK(fixture::moves == beforeUnknown);
                report(false, true); Tick(settings);
                if (mode == 0) {
                    CHECK(fixture::moves == beforeUnknown);
                    CHECK(!s_aimActivation.toggled);
                } else {
                    CHECK(s_aimActivation.toggled);
                    report(true, true); Tick(settings);
                    CHECK(!s_aimActivation.toggled);
                    CHECK(s_status.phase == target::RuntimePhase::WaitingForKey);
                }
            }
        }
    }
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
    {
        auto settings = ResetFixture();
        settings.aimKey = 0x02;
        fixture::keyboardAvailable = false;
        fixture::geometryAvailable = false;
        fixture::geometryState = target::physics::BuildState::Failed;
        fixture::device.physicalButtonsAvailable = true;
        fixture::device.physicalButtonMask = 0;
        Tick(settings);
        CHECK(s_status.phase == target::RuntimePhase::WaitingForKey && fixture::moves == 0);
        fixture::device.physicalButtonMask = 0x02;
        Tick(settings);
        CHECK(s_status.phase == target::RuntimePhase::Tracking && fixture::moves > 0);
    }
    for (const std::string_view prefix : {std::string_view("km."), std::string_view("km.buttons")})
    for (const int mode : {0, 1}) {
        auto settings = ResetFixture();
        settings.aimKey = 0x02;
        settings.aimActivationMode = mode;
        fixture::keyboardAvailable = false;
        fixture::geometryAvailable = false;
        fixture::geometryState = target::physics::BuildState::Failed;
        fixture::device.physicalButtonsAvailable = false;
        app::input::MakcuButtonStreamParser parser;
        const auto consume = [&](const std::string& bytes) {
            for (const unsigned char byte : bytes) parser.Consume(byte);
            fixture::device.physicalButtonsAvailable = parser.HasSample();
            fixture::device.physicalButtonMask = parser.Mask();
        };
        consume("km.buttons(1)\r\n>>> ");
        Tick(settings);
        CHECK(!s_status.aimKeyDown && fixture::moves == 0);
        consume(std::string(prefix) + '\x02');
        Tick(settings);
        CHECK(s_status.phase == target::RuntimePhase::Tracking && fixture::moves > 0);
        const int beforeRelease = fixture::moves;
        consume(std::string(prefix) + '\0');
        Tick(settings);
        if (mode == 0) {
            CHECK(s_status.phase == target::RuntimePhase::WaitingForKey && fixture::moves == beforeRelease);
        } else {
            CHECK(s_status.phase == target::RuntimePhase::Tracking);
            CHECK(s_aimActivation.toggled && s_status.aimKeyDown);
            const int beforeToggleOff = fixture::moves;
            consume(std::string(prefix) + '\x02');
            Tick(settings);
            CHECK(s_status.phase == target::RuntimePhase::WaitingForKey && fixture::moves == beforeToggleOff);
        }
    }
    TestViewOffsetReadPath();
    TestIdleAimPointRegression();
    TestAutomaticPointAndPoseCoherence();
    TestSpawnImmunitySelection();
    TestSettingTransitionsAndMotionStyle();
    TestReactionAndForceCenter();
    TestAimPointOptionMatrix();
    TestRecoilPublication();
    TestConvarResolutionRecoveryPolicy();
    TestAimWithoutBallisticTelemetry();
    TestAssistanceMath();
    TestSeparatedRecoilController();
    TestRecoilOutputFeedback();
    TestRecoilDuringOffAxisAcquisition();
    TestRecoilSampleGap();
    TestGentleDefaultsAndCurveBounds();
    TestRecoilReleaseNoSnap();
    TestHorizontalHumanizedCurve();
    TestDelayedRecoilFeedback();
    TestUnrelatedMotionDoesNotAcknowledgeRecoil();
    TestNearestMouseCountDoesNotOscillate();
    TestFixedPointConvergence();
    TestQuantizedAlignmentStillValidatesCapsule();
    TestTriggerClickPreservesRecoilHistory();
    TestTriggerWithoutAssistWaitsForPriorAimOutput();
    TestTriggerRecoilFeedbackAndRelease();
    TestSaturatedRecoilOutput();
    TestSoftAssistanceRuntime();
    TestSnapshotClockDomain();
    TestIndependentAimAndMenu();
    TestGeometryAvailabilityTransitions();
    TestTriggerAssistWithoutGeometry();
    TestIndependentAccuracyToggles();
    TestFireEvaluationAndPrediction();
    TestSeedClockValidation();
    TestToggleLifecycle(); TestMovementAndSelection();
    TestTriggerShotLedger(true); TestTriggerShotLedger(false); TestFeatureMatrix();
    TestClickDeadlineAndBoundedRecovery();
    TestRepeatShotsObeyToggle();
    TestSingleShotBudget();
    if (failures) { std::cerr << failures << " Target runtime failures\n"; return 1; }
    std::cout << "Target runtime tests passed: Auto/pose coherence, hot settings, 288 point/weapon/option combinations, idle recoil regression, prediction/fire, Toggle, strict FOV, shot ledger.\n";
}
#endif
