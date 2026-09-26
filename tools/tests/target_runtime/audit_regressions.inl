void TestClickDeadlineAndBoundedRecovery()
{
    ResetFixture();
    CHECK(AreTriggerSamplesFresh(fixture::snapshot));
    CHECK(TriggerSampleLifetimeUs(fixture::snapshot, fixture::snapshot.players[1]) > 0);
    fixture::clockAdvanceUs = 100000;
    CHECK(!AreTriggerSamplesFresh(fixture::snapshot));
    CHECK(!AreTriggerTargetSamplesFresh(fixture::snapshot, fixture::snapshot.players[1]));
    CHECK(TriggerSampleLifetimeUs(fixture::snapshot, fixture::snapshot.players[1]) == 0);

    for (bool evidenceAvailable : {true, false}) {
        auto settings = ResetFixture();
        settings.aimbotEnabled = false;
        settings.triggerbotEnabled = true;
        settings.triggerActivationMode = 1;
        settings.triggerDelayMs = 0;
        settings.triggerAutoShot = true;
        settings.weaponProfiles[1].triggerSmoothing = 1;
        auto& snap = fixture::snapshot;
        auto& head = snap.players[1].hitboxes[0];
        head.start.y = head.end.y = head.center.y = 0;
        fixture::keyDown = true;
        const auto acquire = [&] {
            for (int frame = 0; frame < 20 && !s_runtime.trigger.shotLatched; ++frame) {
                s_runtime.trigger.nextAutoShotAt = {};
                Tick(settings);
                s_runtime.trigger.stableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
                s_runtime.trigger.accuracyStableSince = std::chrono::steady_clock::now() - std::chrono::seconds(1);
            }
        };
        acquire();
        CHECK(fixture::clicks == 1 && s_runtime.trigger.shotLatched);
        const auto expire = [&] {
            fixture::click = {};
            s_runtime.trigger.releaseAt = {};
            s_runtime.trigger.targetOutcomeNotBefore = std::chrono::steady_clock::now() - std::chrono::seconds(1);
            for (int frame = 0; frame < 6; ++frame) Tick(settings);
            s_runtime.trigger.shotObservationDeadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
            Tick(settings);
        };
        if (!evidenceAvailable) snap.localAmmoValid = false;
        expire();
        CHECK(!s_runtime.trigger.shotLatched);
        if (evidenceAvailable) {
            CHECK(s_runtime.trigger.unobservedRetryCount == 1);
            CHECK(s_runtime.trigger.blockedUnobservedPawn == 0);
            acquire();
            CHECK(fixture::clicks == 2 && s_runtime.trigger.shotLatched);
            expire();
        }
        CHECK(s_runtime.trigger.blockedUnobservedPawn == snap.players[1].pawn);
        const int clicksBefore = fixture::clicks;
        for (int frame = 0; frame < 50; ++frame) Tick(settings);
        CHECK(fixture::clicks == clicksBefore);
        fixture::keyDown = false; Tick(settings);
        fixture::keyDown = true; Tick(settings);
        CHECK(!s_status.triggerKeyDown);
        CHECK(s_runtime.trigger.blockedUnobservedPawn == 0);
    }
}
