void TestModernMotionControl()
{
    using namespace target::control;
    for (int hz : {64, 128, 250, 500}) {
        Trajectory motion;
        Vec2 remaining{20, -8};
        const float dt = 1.0f / hz;
        for (int i = 0; i < hz * 4; ++i) {
            const auto before = remaining;
            const auto oldAcceleration = motion.acceleration;
            const auto step = motion.Advance({remaining.x * 0.2f, remaining.y * 0.2f},
                remaining, dt, 25, 800, 40000);
            CHECK(Length(motion.velocity) <= 25.001f);
            CHECK(Length(motion.acceleration) <= 800.01f);
            if (std::fabs(step.x) < std::fabs(before.x) && std::fabs(step.y) < std::fabs(before.y) &&
                step.x * before.x > 0 && step.y * before.y > 0)
                CHECK(Length({motion.acceleration.x - oldAcceleration.x,
                    motion.acceleration.y - oldAcceleration.y}) <= 40000 * dt + 0.1f);
            remaining = {remaining.x - step.x, remaining.y - step.y};
            CHECK(std::fabs(remaining.x) <= std::fabs(before.x) + 0.0001f);
            CHECK(std::fabs(remaining.y) <= std::fabs(before.y) + 0.0001f);
        }
        CHECK(Length(remaining) < 0.02f);
        CHECK(Length(motion.Advance({1, 1}, {2, 2}, 0, 25, 800, 40000)) == 0);
        CHECK(Length(motion.velocity) == 0);
    }
    Intent intent;
    for (int i = 0; i < 25; ++i) intent.Advance({0, -1}, {0, 2}, 0.004f, 0.02f, true);
    CHECK(intent.confident && intent.gain < 0.01f);
    for (int i = 0; i < 150; ++i) intent.Advance({}, {0, 2}, 0.004f, 0.02f, false);
    CHECK(!intent.confident && intent.gain > 0.99f);
    VelocityFilter filter;
    auto velocity = filter.Update({200, 0}, {100, 0}, 1000000, 1, 2);
    CHECK(velocity.x == 200 && filter.confidence == 1);
    velocity = filter.Update({200, 0}, {101, 0}, 1005000, 1, 2);
    CHECK(velocity.x == 200);
    velocity = filter.Update({-200, 0}, {102, 0}, 1010000, 1, 2);
    CHECK(velocity.x > -200 && velocity.x < 200 && filter.confidence < 1);
    velocity = filter.Update({75, 0}, {500, 0}, 1015000, 1, 3);
    CHECK(velocity.x == 75 && filter.confidence == 1);
    velocity = filter.Update({25, 0}, {501, 0}, 1000000, 1, 3);
    CHECK(velocity.x == 25);
    LatencyWindow latency;
    for (uint64_t i = 1; i <= 128; ++i) latency.Add(4000);
    CHECK(latency.count == 128 && latency.Mean() == 4000 && latency.Percentile(99) == 4000);
    for (uint64_t i = 1; i <= 128; ++i) latency.Add(24000);
    CHECK(latency.count == 128 && latency.Mean() == 24000 && latency.Percentile(95) == 24000);
}

void TestTrackedDeliveryFeedback()
{
    ResetFixture();
    auto& snap = fixture::snapshot;
    Candidate candidate;
    candidate.player = &snap.players[1];
    candidate.point = candidate.player->hitboxes[0].center;
    MotionRuntimeState motion;
    int x = 0, y = 0;
    CHECK(MoveToward(snap, candidate, {}, motion, 1, false, false, 0.01f, &x, &y) == MoveResult::Queued);
    const auto token = fixture::moveReceipt.token;
    fixture::moveReceipt.state = app::input::MoveDeliveryState::Queued;
    snap.viewAngles.y -= x * 0.022f;
    FreshFrame();
    CHECK(AwaitMotionFeedback(snap));
    fixture::moveReceipt.state = app::input::MoveDeliveryState::Dispatching;
    CHECK(AwaitMotionFeedback(snap));
    fixture::moveReceipt.state = app::input::MoveDeliveryState::Sent;
    fixture::moveReceipt.sentAtUs = snap.sampledAtUs;
    CHECK(AwaitMotionFeedback(snap));
    snap.sampledAtUs += 8000;
    FreshFrame();
    CHECK(!AwaitMotionFeedback(snap));
    CHECK(s_outputFeedback.observedToken == token && s_outputFeedback.diagnostics.latencySamples == 1);
    CHECK(s_outputFeedback.diagnostics.latencyP99Us >= 8000);
    for (auto state : {app::input::MoveDeliveryState::Cancelled, app::input::MoveDeliveryState::Replaced,
            app::input::MoveDeliveryState::Expired, app::input::MoveDeliveryState::Failed}) {
        s_outputFeedback.issuedAtUs = snap.sampledAtUs;
        fixture::moveReceipt.state = state;
        const auto timeouts = s_outputFeedback.diagnostics.timeouts;
        CHECK(!AwaitMotionFeedback(snap));
        CHECK(timeouts == s_outputFeedback.diagnostics.timeouts);
    }
    for (int i = 0; i < 128; ++i) s_outputFeedback.latency.Add(4000);
    fixture::moveReceipt.state = app::input::MoveDeliveryState::Sent;
    fixture::moveReceipt.sentAtUs = snap.sampledAtUs;
    s_outputFeedback.issuedAtUs = snap.sampledAtUs;
    s_outputFeedback.viewAngles = snap.viewAngles;
    s_outputFeedback.diagnostics.expectedYaw = 1;
    snap.sampledAtUs += 60000; FreshFrame();
    CHECK(AwaitMotionFeedback(snap));
    snap.viewAngles.y += 1; FreshFrame();
    CHECK(!AwaitMotionFeedback(snap));
    fixture::moveReceipt = {};
    s_outputFeedback.issuedAtUs = snap.sampledAtUs;
    CHECK(AwaitMotionFeedback(snap));
    snap.sampledAtUs += 110000; FreshFrame();
    CHECK(!AwaitMotionFeedback(snap));
}

void TestFeedbackClockDomains()
{
    for (const uint64_t offset : {uint64_t(0), uint64_t(1000000000), uint64_t(1000000000000)}) {
        auto settings = ResetFixture();
        fixture::inputClockOffsetUs = offset;
        fixture::geometryAvailable = false;
        fixture::keyDown = true;
        Tick(settings);
        CHECK(fixture::moves == 1 && s_outputFeedback.diagnostics.pending);
        const uint64_t firstToken = s_outputFeedback.token;
        for (int frame = 0; frame < 24; ++frame) {
            auto& snap = fixture::snapshot;
            if (s_outputFeedback.diagnostics.pending) {
                snap.viewAngles.x += s_outputFeedback.diagnostics.expectedPitch;
                snap.viewAngles.y += s_outputFeedback.diagnostics.expectedYaw;
            }
            snap.sampledAtUs += 8000;
            FreshFrame();
            Tick(settings);
        }
        CHECK(fixture::moves > 4);
        CHECK(s_outputFeedback.observedToken >= firstToken);
        CHECK(s_outputFeedback.diagnostics.latencySamples > 4);
        CHECK(s_outputFeedback.diagnostics.latencyP99Us <= 24000);
        CHECK(s_outputFeedback.diagnostics.timeouts == 0);
    }

    auto settings = ResetFixture();
    fixture::inputClockOffsetUs = 1000000000000;
    fixture::keyDown = true;
    Tick(settings);
    CHECK(s_outputFeedback.diagnostics.pending);
    fixture::snapshot.players[1].valid = false;
    fixture::snapshot.viewAngles.y += std::copysign(90.0f, s_outputFeedback.diagnostics.expectedYaw);
    fixture::snapshot.sampledAtUs += 8000;
    FreshFrame();
    Tick(settings);
    CHECK(!s_outputFeedback.diagnostics.pending);
    CHECK(s_status.targetSlot == -1);
    fixture::snapshot.viewAngles = {};
    fixture::snapshot.players[1].valid = true;
    fixture::snapshot.sampledAtUs += 8000;
    FreshFrame();
    Tick(settings);
    CHECK(fixture::moves > 1);

    ResetFixture();
    fixture::inputClockOffsetUs = 1000000000000;
    auto& snap = fixture::snapshot;
    Candidate candidate;
    candidate.player = &snap.players[1];
    candidate.point = candidate.player->hitboxes[0].center;
    MotionRuntimeState motion;
    CHECK(MoveToward(snap, candidate, {}, motion, 1, false, false, 0.01f, nullptr, nullptr) == MoveResult::Queued);
    const auto sampledAtUs = snap.sampledAtUs;
    fixture::clockAdvanceUs = 110000;
    CHECK(!AwaitMotionFeedback(snap));
    CHECK(snap.sampledAtUs == sampledAtUs);
    CHECK(s_outputFeedback.diagnostics.timeouts == 1);

    fixture::clockAdvanceUs = 0;
    ResetFixture();
    fixture::inputClockOffsetUs = 1000000000000;
    candidate.player = &fixture::snapshot.players[1];
    candidate.point = candidate.player->hitboxes[0].center;
    motion = {};
    CHECK(MoveToward(fixture::snapshot, candidate, {}, motion, 1, false, false, 0.01f, nullptr, nullptr) == MoveResult::Queued);
    fixture::snapshot.viewAngles.y += s_outputFeedback.diagnostics.expectedYaw;
    fixture::snapshot.sampledAtUs += 8000;
    FreshFrame();
    fixture::snapshot.sceneSerial++;
    CHECK(!AwaitMotionFeedback(fixture::snapshot));
    fixture::clockAdvanceUs = 110000;
    CHECK(!AwaitMotionFeedback(fixture::snapshot));
    CHECK(s_outputFeedback.diagnostics.latencySamples == 0);
    CHECK(s_outputFeedback.diagnostics.timeouts == 0);
    fixture::clockAdvanceUs = 0;

    ResetFixture();
    candidate.player = &fixture::snapshot.players[1];
    candidate.point = candidate.player->hitboxes[0].center;
    motion = {};
    CHECK(MoveToward(fixture::snapshot, candidate, {}, motion, 1, false, false, 0.01f, nullptr, nullptr) == MoveResult::Queued);
    s_outputFeedback.issuedAtUs += 1000000;
    CHECK(!AwaitMotionFeedback(fixture::snapshot));
    CHECK(!s_outputFeedback.diagnostics.pending && s_outputFeedback.diagnostics.timeouts == 1);
}

void TestHitchanceEarlyDecision()
{
    ResetFixture();
    const auto& snap = fixture::snapshot;
    const auto& player = snap.players[1];
    for (uint16_t weaponId : {uint16_t(1), uint16_t(7), uint16_t(28)}) {
        for (float spread : {0.0f, 0.002f, 0.02f}) for (float yaw : {0.0f, 2.86f, 3.1f}) {
            target::ballistics::WeaponSpread weapon;
            weapon.itemDefinitionIndex = weaponId;
            weapon.inaccuracy = spread;
            weapon.spread = spread * 0.5f;
            const Vector3 angles{0, yaw, 0};
            const float full = target::ballistics::CalculateHitchance(snap.localEyePos, angles, player, weapon, 1);
            for (float threshold : {0.01f, 0.25f, 0.5f, 0.8f, 1.0f}) {
                int tested = -1;
                CHECK(target::ballistics::MeetsHitchance(snap.localEyePos, angles, player,
                    weapon, threshold, 1, &tested) == (full + 0.0000001f >= threshold));
                CHECK(tested >= 1 && tested <= 256);
            }
        }
    }
    CHECK(!target::ballistics::MeetsHitchance(snap.localEyePos, {}, player, {}, NAN));
    CHECK(!target::ballistics::MeetsHitchance(snap.localEyePos, {}, player, {}, 0));
}

void TestAngularFovAndCompactSnapshot()
{
    using namespace target::policy;
    for (float smoothing : {1.0f, 2.0f, 8.0f, 50.0f})
        CHECK(std::fabs(SmoothingFromResponseMs(SmoothingResponseMs(smoothing)) - smoothing) < 0.0001f);
    const auto wide = ResolveAngularFov(1920, 1080, 0.5625f, 1, 5);
    CHECK(wide.valid && std::fabs(wide.radiusX - wide.radiusY) < 0.0001f);
    const auto square = ResolveAngularFov(1280, 960, 0.75f, 1, 5);
    CHECK(square.valid && std::fabs(square.radiusX - square.radiusY) < 0.0001f);
    const auto stretched = ResolveAngularFov(1920, 1080, 0.75f, 1, 5);
    CHECK(stretched.valid && stretched.radiusX > stretched.radiusY);
    CHECK(!ResolveAngularFov(1920, 1080, NAN, 1, 5).valid);
    esp::PlayerData full;
    full.pawn = 0x12345; full.pawnHandle = 0x10042; full.health = 90;
    full.bones[0] = {1, 2, 3}; full.hasBones = true; full.coreUpdatedAtUs = 123;
    strcpy_s(full.name, "Identity");
    const esp::TargetPlayerData compact(full);
    CHECK(compact.pawn == full.pawn && compact.pawnHandle == full.pawnHandle && compact.health == 90);
    CHECK(compact.bones[0].z == 3 && compact.coreUpdatedAtUs == 123 && strcmp(compact.name, "Identity") == 0);
    std::cout << "Target player snapshot: " << sizeof(full) << " -> " << sizeof(compact) << " bytes\n";
}

void TestIntentAndPointContinuityRuntime()
{
    auto settings = ResetFixture();
    auto& snap = fixture::snapshot;
    auto& profile = settings.weaponProfiles[1];
    Candidate candidate;
    candidate.player = &snap.players[1]; candidate.point = candidate.player->hitboxes[0].center;
    candidate.fixedPoint = true;
    MotionRuntimeState motion;
    int x = 0, y = 0;
    motion.lastIntentView = snap.viewAngles;
    motion.lastIntentViewUs = snap.viewUpdatedAtUs - 4000;
    snap.viewAngles.y -= 0.4f;
    CHECK(MoveToward(snap, candidate, {}, motion, 5, false, false, 0.01f,
        &x, &y, nullptr, -1, nullptr, true, nullptr, &profile) != MoveResult::Failed);
    CHECK(motion.diagnostics.inputIntentEstimated && motion.diagnostics.assistanceGain < 1);
    const auto same = candidate.player;
    s_runtime.aim.targetPawn = same->pawn; s_runtime.aim.targetPawnHandle = same->pawnHandle;
    s_runtime.aim.targetSlot = 1; s_runtime.aim.targetHitboxIndex = -2;
    s_runtime.aim.acquiredAtUs = snap.sampledAtUs - 10000;
    s_runtime.aim.trajectory.velocity = {0.0f, 0.1f};
    settings.aimBone = 5;
    s_runtime.contextInitialized = true; s_runtime.sceneSerial = snap.sceneSerial;
    s_runtime.localPawn = snap.localPawn; s_runtime.localWeaponId = snap.localWeaponId;
    s_runtime.localWeaponHandle = snap.localWeaponHandle; s_runtime.localWeaponEntity = snap.localWeaponEntity;
    fixture::keyDown = true;
    s_outputFeedback = {};
    const auto acquired = s_runtime.aim.acquiredAtUs;
    Tick(settings);
    CHECK(s_runtime.aim.targetPawn == same->pawn && s_runtime.aim.acquiredAtUs == acquired);
    CHECK(s_runtime.aim.targetHitboxIndex == 0);
    settings.fovUnit = 1; settings.fovDegrees = 5;
    Tick(settings);
    CHECK(s_status.fovRadiusX > s_status.fovRadiusY && s_status.targetSlot == 1);
}
