bool esp::GetWebRadarSnapshot(WebRadarSnapshot* outSnapshot)
{
    if (!outSnapshot)
        return false;

    static thread_local EntitySnapshot snap;
    if (!ReadCurrentSnapshot(snap))
        return false;

    Vector3 effectiveLocalPos = snap.localPos;
    if (!std::isfinite(effectiveLocalPos.x) ||
        !std::isfinite(effectiveLocalPos.y) ||
        !std::isfinite(effectiveLocalPos.z) ||
        (std::fabs(effectiveLocalPos.x) + std::fabs(effectiveLocalPos.y) + std::fabs(effectiveLocalPos.z) <= 1.0f)) {
        CameraFrame cameraFrame = {};
        const bool hasCameraFrame = ReadCameraFrame(cameraFrame);
        const uint64_t nowUs = TickNowUs();
        const bool liveLocalPosFresh =
            hasCameraFrame && state::ShouldApplyCameraLocalPosition(
                cameraFrame.sceneSerial, snap.sceneSerial,
                cameraFrame.localPosPawn, snap.localPawn,
                cameraFrame.localPosValid, cameraFrame.localPosUpdatedUs,
                nowUs, kLiveCameraFreshnessUs);
        if (liveLocalPosFresh)
            effectiveLocalPos = cameraFrame.localPos;
    }
    const bool hasEffectiveLocalPos =
        std::isfinite(effectiveLocalPos.x) &&
        std::isfinite(effectiveLocalPos.y) &&
        std::isfinite(effectiveLocalPos.z) &&
        (std::fabs(effectiveLocalPos.x) + std::fabs(effectiveLocalPos.y) + std::fabs(effectiveLocalPos.z) > 1.0f);
    if (!hasEffectiveLocalPos)
        effectiveLocalPos = { NAN, NAN, NAN };

    WebRadarSnapshot& snapshot = *outSnapshot;
    snapshot = {};
    for (size_t i = 0; i < snapshot.players.size(); ++i)
        snapshot.players[i] = snap.webRadarPlayers[i];

    {
        // The data worker owns continuity. A second cache here used to restart
        // its grace period on every poll, resurrect rejected slots and extend
        // stale frames indefinitely when the producer stopped.
        static thread_local uint64_t lastHoldEventUs[64] = {};
        const uint64_t nowUs = TickNowUs();
        const bool sceneMatches =
            snap.sceneSerial == s_sceneResetSerial.load(std::memory_order_relaxed);
        const bool bulkRecovery = esp::render::IsWithinWebRadarBulkRecovery(
            s_lastBulkEvictionUs.load(std::memory_order_relaxed), nowUs);
        for (size_t i = 0; i < snapshot.players.size(); ++i) {
            auto& player = snapshot.players[i];
            if (!sceneMatches || !player.valid || player.pawn == 0 ||
                !isValidWorldPos(player.position) ||
                (player.team != 2 && player.team != 3) ||
                !esp::render::IsWebRadarCoreSampleFresh(
                    player.coreUpdatedAtUs, nowUs, player.health, bulkRecovery)) {
                player = {};
                continue;
            }
            player.health = std::clamp(player.health, 0, 100);
            player.armor = std::clamp(player.armor, 0, 100);
            if (player.staleFrames > 0 &&
                esp::render::ShouldRecordDrawEvent(nowUs, lastHoldEventUs[i])) {
                lastHoldEventUs[i] = nowUs;
                RecordEspEvent({
                    .type = EspEventType::WebRadarPlayerHold,
                    .slot = static_cast<uint8_t>(i),
                    .param = static_cast<uint16_t>(player.staleFrames)
                });
            }
        }
    }

    std::copy(std::begin(snap.localName), std::end(snap.localName), std::begin(snapshot.localName));
    std::copy(std::begin(snap.activeMapKey), std::end(snap.activeMapKey), std::begin(snapshot.mapKey));
    snapshot.localPos = effectiveLocalPos;
    snapshot.localIsDead = snap.localIsDead;
    snapshot.localHealth = snap.localHealth;
    snapshot.localArmor = snap.localArmor;
    snapshot.localMoney = snap.localMoney;
    snapshot.localYaw = snap.viewAngles.y;
    snapshot.localWeaponId = snap.localWeaponId;
    snapshot.localAmmoClip = snap.localAmmoClip;
    snapshot.localHasBomb = snap.localHasBomb;
    snapshot.localHasDefuser = snap.localHasDefuser;
    snapshot.localGrenadeCount = snap.localGrenadeCount;
    std::copy(std::begin(snap.localGrenadeIds), std::end(snap.localGrenadeIds), std::begin(snapshot.localGrenadeIds));
    snapshot.minimapMins = snap.minimapMins;
    snapshot.minimapMaxs = snap.minimapMaxs;
    snapshot.localTeam = snap.localTeam;
    snapshot.hasMinimapBounds = snap.hasMinimapBounds;
    snapshot.captureTickMs = TickNowMs();

    snapshot.bomb.planted = snap.bombState.planted;
    snapshot.bomb.ticking = snap.bombState.ticking;
    snapshot.bomb.beingDefused = snap.bombState.beingDefused;
    snapshot.bomb.dropped = snap.bombState.dropped;
    snapshot.bomb.position = snap.bombState.position;
    snapshot.bomb.blowTime = snap.bombState.blowTime;
    snapshot.bomb.timerLength = snap.bombState.timerLength;
    snapshot.bomb.defuseEndTime = snap.bombState.defuseEndTime;
    snapshot.bomb.defuseLength = snap.bombState.defuseLength;
    snapshot.bomb.currentGameTime = snap.bombState.currentGameTime;

    
    bool wantsWorldMarkers = false;
    {
        std::lock_guard<std::recursive_mutex> settingsLock(g::settingsMutex);
        wantsWorldMarkers = g::espWorld;
    }

    {
        const uint64_t nowUs = TickNowUs();
        snapshot.worldMarkerCount = 0;
        if (!wantsWorldMarkers) {
            return true;
        }
        const int sourceMarkerCount = std::clamp(
            snap.worldMarkerCount, 0, static_cast<int>(std::size(snap.worldMarkers)));
        for (int i = 0; i < sourceMarkerCount && snapshot.worldMarkerCount < WebRadarSnapshot::kMaxWorldMarkers; ++i) {
            const auto& wm = snap.worldMarkers[i];
            if (!wm.valid)
                continue;
            if (wm.expiresUs > 0 && wm.expiresUs <= nowUs)
                continue;
            
            if (wm.type == WorldMarkerType::DroppedWeapon ||
                wm.type == WorldMarkerType::SmokeProjectile ||
                wm.type == WorldMarkerType::MolotovProjectile ||
                wm.type == WorldMarkerType::DecoyProjectile)
                continue;
            auto& out = snapshot.worldMarkers[snapshot.worldMarkerCount++];
            out.type = static_cast<uint8_t>(wm.type);
            out.position = wm.position;
            out.weaponId = wm.weaponId;
            out.lifeRemainingSec = (wm.expiresUs > nowUs)
                ? static_cast<float>(wm.expiresUs - nowUs) / 1000000.0f
                : 0.0f;
        }
    }

    return true;
}
