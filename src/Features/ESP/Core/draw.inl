#include "Features/ESP/Render/style.h"
#include "Features/World/grenade_helper.h"


void esp::EspRenderer::Draw()
{
    static esp::render::BombPositionInterpolator bombInterpolator;
    // A background menu camera can still produce a valid projection matrix.
    // Do not project the last gameplay frame through that unrelated camera.
    if (!s_engineStatusResolved.load(std::memory_order_relaxed) ||
        !s_engineInGame.load(std::memory_order_relaxed) ||
        s_engineMenu.load(std::memory_order_relaxed)) {
        bombInterpolator.Reset();
        return;
    }

    if (!g::espEnabled && !g::espWorld && !g::radarEnabled && !g::radarSpectatorList &&
        !g::espBombTime && !g::grenadeHelperEnabled) return;

    const float screenW = static_cast<float>(g::screenWidth);
    const float screenH = static_cast<float>(g::screenHeight);
    if (screenW <= 0 || screenH <= 0) return;

    static thread_local EntitySnapshot snap;
    if (!ReadCurrentSnapshot(snap))
        return;

    PlayerVisibilityFrame visibilityFrame = {};
    const bool hasVisibilityFrame = ReadPlayerVisibilityFrame(visibilityFrame);
    CameraFrame cameraFrame = {};
    const bool hasCameraFrame = ReadCameraFrame(cameraFrame);
    // Sample time after acquiring every frame: a concurrent publication must
    // not make a freshly acquired frame appear to come from the future.
    const uint64_t nowUs = TickNowUs();
    if (hasVisibilityFrame &&
        state::ShouldApplyPlayerVisibilityFrame(
            visibilityFrame.sceneSerial,
            snap.sceneSerial,
            visibilityFrame.captureTimeUs,
            snap.captureTimeUs,
            nowUs)) {
        for (int i = 0; i < 64; ++i) {
            if (snap.players[i].pawn != 0 &&
                snap.players[i].pawn == visibilityFrame.players[i].pawn) {
                snap.players[i].visible = visibilityFrame.players[i].visible;
            }
        }
    }

    const esp::PlayerData* players = snap.players;
    const esp::PlayerData* prevPlayers = snap.prevPlayers;
    view_matrix_t viewMatrix = {};
    const int localTeam = snap.localTeam;
    Vector3 localPos = snap.localPos;
    Vector3 prevLocalPos = snap.prevLocalPos;
    Vector3 viewAngles = snap.viewAngles;
    const bool localHasBomb = snap.localHasBomb;
    const Vector3 minimapMins = snap.minimapMins;
    const Vector3 minimapMaxs = snap.minimapMaxs;
    const bool hasMinimapBounds = snap.hasMinimapBounds;
    const WorldMarker* worldMarkers = snap.worldMarkers;
    const int worldMarkerCount = std::clamp(
        snap.worldMarkerCount, 0, static_cast<int>(std::size(snap.worldMarkers)));
    BombState bombState = snap.bombState;
    bombState.position = bombInterpolator.Update(
        bombState.position, bombState.dropped,
        (bombState.sourceFlags & (BombResolveSourceStickyDrop |
            BombResolveSourceStickyState | BombResolveSourcePositionFallback)) == 0u,
        bombState.positionSampleTimeUs, nowUs, snap.sceneSerial,
        bombState.positionGeneration, bombState.positionEntity);
    const uint64_t captureTimeUs = snap.captureTimeUs;
    const uint64_t prevCaptureTimeUs = snap.prevCaptureTimeUs;
    const uint64_t playerCaptureTimeUs =
        snap.playerCoreCaptureTimeUs != 0
            ? snap.playerCoreCaptureTimeUs
            : captureTimeUs;
    const uint64_t prevPlayerCaptureTimeUs =
        snap.prevPlayerCoreCaptureTimeUs != 0
            ? snap.prevPlayerCoreCaptureTimeUs
            : prevCaptureTimeUs;

    bool usingLiveLocalPos = false;
    auto isLikelyViewMatrix = [](const view_matrix_t& matrix) -> bool {
        return esp::IsLikelyViewMatrix(matrix);
    };
    static view_matrix_t s_lastGoodDrawViewMatrix = {};
    static bool s_lastGoodDrawViewMatrixValid = false;
    static uint64_t s_lastGoodDrawViewMatrixUs = 0;
    static uint64_t s_drawViewCacheSceneSerial = 0;
    if (s_drawViewCacheSceneSerial != snap.sceneSerial) {
        s_drawViewCacheSceneSerial = snap.sceneSerial;
        memset(&s_lastGoodDrawViewMatrix, 0, sizeof(s_lastGoodDrawViewMatrix));
        s_lastGoodDrawViewMatrixValid = false;
        s_lastGoodDrawViewMatrixUs = 0;
    }
    if (esp::render::ShouldUpdateCachedViewMatrix(
            isLikelyViewMatrix(snap.viewMatrix), snap.viewMatrixUpdatedAtUs,
            s_lastGoodDrawViewMatrixValid, s_lastGoodDrawViewMatrixUs, nowUs)) {
        memcpy(&s_lastGoodDrawViewMatrix, &snap.viewMatrix, sizeof(view_matrix_t));
        s_lastGoodDrawViewMatrixValid = true;
        s_lastGoodDrawViewMatrixUs = snap.viewMatrixUpdatedAtUs;
    }
    if (esp::render::ShouldReuseCachedViewMatrix(
                   s_lastGoodDrawViewMatrixValid,
                   s_lastGoodDrawViewMatrixUs,
                   nowUs)) {
        memcpy(&viewMatrix, &s_lastGoodDrawViewMatrix, sizeof(view_matrix_t));
    } else {
        s_lastGoodDrawViewMatrixValid = false;
    }

    {
        const bool cameraSceneMatches =
            hasCameraFrame &&
            cameraFrame.sceneSerial == snap.sceneSerial;
        const bool liveViewFresh =
            cameraSceneMatches &&
            cameraFrame.viewValid &&
            cameraFrame.viewUpdatedUs > 0 &&
            nowUs >= cameraFrame.viewUpdatedUs &&
            (nowUs - cameraFrame.viewUpdatedUs) <= kLiveCameraFreshnessUs;
        if (liveViewFresh && esp::render::ShouldUpdateCachedViewMatrix(
                isLikelyViewMatrix(cameraFrame.viewMatrix), cameraFrame.viewUpdatedUs,
                s_lastGoodDrawViewMatrixValid, s_lastGoodDrawViewMatrixUs, nowUs)) {
            memcpy(&viewMatrix, &cameraFrame.viewMatrix, sizeof(view_matrix_t));
            if (cameraFrame.viewAnglesValid &&
                cameraFrame.viewAnglesUpdatedUs > 0 &&
                nowUs >= cameraFrame.viewAnglesUpdatedUs &&
                (nowUs - cameraFrame.viewAnglesUpdatedUs) <=
                    kLiveCameraFreshnessUs) {
                viewAngles = cameraFrame.viewAngles;
            }
            memcpy(&s_lastGoodDrawViewMatrix, &viewMatrix, sizeof(view_matrix_t));
            s_lastGoodDrawViewMatrixValid = true;
            s_lastGoodDrawViewMatrixUs = cameraFrame.viewUpdatedUs;
        } else if (!isLikelyViewMatrix(viewMatrix) &&
                   esp::render::ShouldReuseCachedViewMatrix(
                       s_lastGoodDrawViewMatrixValid,
                       s_lastGoodDrawViewMatrixUs,
                       nowUs)) {
            memcpy(&viewMatrix, &s_lastGoodDrawViewMatrix, sizeof(view_matrix_t));
        }

        const bool liveLocalPosFresh =
            hasCameraFrame && state::ShouldApplyCameraLocalPosition(
                cameraFrame.sceneSerial, snap.sceneSerial,
                cameraFrame.localPosPawn, snap.localPawn,
                cameraFrame.localPosValid, cameraFrame.localPosUpdatedUs,
                nowUs, kLiveCameraFreshnessUs);
        if (liveLocalPosFresh) {
            localPos = cameraFrame.localPos;
            usingLiveLocalPos = true;
        }
    }

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    if (!drawList)
        return;

    const bool usableViewMatrix = isLikelyViewMatrix(viewMatrix);

    const uint64_t nominalIntervalUs = 1000000u / DATA_WORKER_HZ;
    const esp::render::SnapshotTimingResult localSnapshotTiming =
        esp::render::EvaluateSnapshotTiming({
            captureTimeUs,
            prevCaptureTimeUs,
            nowUs,
            nominalIntervalUs,
        });
    const esp::render::SnapshotTimingResult playerSnapshotTiming =
        esp::render::EvaluateSnapshotTiming({
            playerCaptureTimeUs,
            prevPlayerCaptureTimeUs,
            nowUs,
            nominalIntervalUs,
        });
    const float quinticT = playerSnapshotTiming.smoothAlpha;
    const float extrapolationSec = playerSnapshotTiming.extrapolationSec;
    auto lerpVec3 = [&](const Vector3& a, const Vector3& b) -> Vector3 {
        return {
            a.x + (b.x - a.x) * quinticT,
            a.y + (b.y - a.y) * quinticT,
            a.z + (b.z - a.z) * quinticT
        };
    };
    auto lerpLocalVec3 = [&](const Vector3& a, const Vector3& b) -> Vector3 {
        return {
            a.x + (b.x - a.x) * localSnapshotTiming.smoothAlpha,
            a.y + (b.y - a.y) * localSnapshotTiming.smoothAlpha,
            a.z + (b.z - a.z) * localSnapshotTiming.smoothAlpha
        };
    };

    Vector3 renderLocalPos = localPos;
    if (!usingLiveLocalPos && prevCaptureTimeUs > 0 && captureTimeUs > prevCaptureTimeUs)
        renderLocalPos = lerpLocalVec3(prevLocalPos, localPos);

    const float yawRad = viewAngles.y * (std::numbers::pi_v<float> / 180.0f);
    const float yawSin = sinf(yawRad);
    const float yawCos = cosf(yawRad);

    ImU32 boxCol = ColorToImU32(g::espBoxColor);
    ImU32 nameCol = ColorToImU32(g::espNameColor);
    ImU32 distanceCol = ColorToImU32(g::espDistanceColor);
    ImU32 visibleCol = ColorToImU32(g::espVisibleColor);
    ImU32 hiddenCol = ColorToImU32(g::espHiddenColor);
    ImU32 skelCol = ColorToImU32(g::espSkeletonColor);
    ImU32 snapCol = ColorToImU32(g::espSnaplineColor);
    ImU32 worldCol = ColorToImU32(g::espWorldColor);
    ImU32 bombCol = ColorToImU32(g::espBombColor);
    auto getPlayerBone = [&](const esp::PlayerData& player, int boneId) -> Vector3 {
        const int idx = esp::PlayerStoredBoneIndex(boneId);
        if (idx < 0)
            return {};
        return player.bones[idx];
    };
    auto hasUsableBone = [&](const esp::PlayerData& player, int boneId) -> bool {
        const Vector3 bone = getPlayerBone(player, boneId);
        return IsFiniteVec(bone) &&
               ((player.hasBones && (boneId == esp::PELVIS || boneId == esp::HEAD)) ||
                !(bone.x == 0.0f && bone.y == 0.0f && bone.z == 0.0f));
    };
    auto isPlausibleToeSegment = [&](const Vector3& heel, const Vector3& toe) -> bool {
        if (!IsFiniteVec(heel) || !IsFiniteVec(toe))
            return false;
        const Vector3 delta = toe - heel;
        const float distance = delta.Length();
        return distance >= 1.0f &&
               distance <= 30.0f &&
               std::fabs(delta.z) <= 18.0f;
    };
    auto selectToeBoneId = [&](const esp::PlayerData& player, bool leftSide) -> int {
        const int primary = leftSide
            ? esp::LeftToeBoneForTeam(player.team)
            : esp::RightToeBoneForTeam(player.team);
        const int alternate = leftSide
            ? (primary == esp::FOOT_TOES_L_CT ? esp::FOOT_TOES_L_T : esp::FOOT_TOES_L_CT)
            : (primary == esp::FOOT_TOES_R_CT ? esp::FOOT_TOES_R_T : esp::FOOT_TOES_R_CT);
        if (hasUsableBone(player, primary))
            return primary;
        if (hasUsableBone(player, alternate))
            return alternate;
        return -1;
    };

    if (g::espEnabled && usableViewMatrix) {
        static uint64_t s_lastDrawInvalidPosEventUs[64] = {};
        static uint64_t s_lastDrawDeathHoldEventUs[64] = {};
        static uint64_t s_lastDrawValidHoldEventUs[64] = {};
        
        
        
        
        static uint64_t s_lastValidAliveUs[64] = {};
        static uintptr_t s_lastValidPawn[64] = {};
        static Vector3 s_lastValidBonePos[64][esp::kPlayerStoredBoneCount] = {};
        static uint64_t s_lastValidBoneUs[64][esp::kPlayerStoredBoneCount] = {};
        static uint64_t s_lastValidBonePawn[64] = {};
        static uint32_t s_lastValidBoneHandle[64] = {};
        static uint64_t s_playerRenderCacheSceneSerial = 0;
        if (s_playerRenderCacheSceneSerial != snap.sceneSerial) {
            s_playerRenderCacheSceneSerial = snap.sceneSerial;
            memset(s_lastDrawInvalidPosEventUs, 0, sizeof(s_lastDrawInvalidPosEventUs));
            memset(s_lastDrawDeathHoldEventUs, 0, sizeof(s_lastDrawDeathHoldEventUs));
            memset(s_lastDrawValidHoldEventUs, 0, sizeof(s_lastDrawValidHoldEventUs));
            memset(s_lastValidAliveUs, 0, sizeof(s_lastValidAliveUs));
            memset(s_lastValidPawn, 0, sizeof(s_lastValidPawn));
            memset(s_lastValidBonePos, 0, sizeof(s_lastValidBonePos));
            memset(s_lastValidBoneUs, 0, sizeof(s_lastValidBoneUs));
            memset(s_lastValidBonePawn, 0, sizeof(s_lastValidBonePawn));
            memset(s_lastValidBoneHandle, 0, sizeof(s_lastValidBoneHandle));
        }
        
        
        
        
        for (int i = 0; i < 64; i++) {
            const esp::PlayerData& current = players[i];
            const esp::PlayerData& prevP = prevPlayers[i];

            if (!current.valid || current.health <= 0 || !current.hasBones) {
                s_lastValidBonePawn[i] = 0;
                s_lastValidBoneHandle[i] = 0;
            }

            const bool currentValid =
                current.valid && current.pawn != 0 && current.health > 0 &&
                isValidWorldPos(current.position);
            
            
            
            if (esp::render::ShouldTrackLastValidAlive(
                    currentValid,
                    IsLocalSnapshotSlot(snap, {
                        .slotIndex = i,
                        .pawn = current.pawn
                    }),
                    current.team)) {
                s_lastValidAliveUs[i] = nowUs;
                s_lastValidPawn[i] = current.pawn;
            }
            const bool prevValidAlive =
                prevP.valid && prevP.pawn != 0 && prevP.health > 0 &&
                isValidWorldPos(prevP.position);
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            const bool slotIdentityMatchesCurrent =
                esp::render::IsSameNonZeroPlayerIdentity(
                    current.pawn,
                    prevP.pawn) && current.pawnHandle == prevP.pawnHandle;
            const bool currentConfirmedDead =
                current.valid &&
                current.pawn != 0 &&
                current.pawn == prevP.pawn &&
                current.health <= 0;
            const bool slotIdentityMatchesCache =
                s_lastValidPawn[i] != 0 &&
                prevP.pawn == s_lastValidPawn[i];
            const bool prevIsLocal = IsLocalSnapshotSlot(snap, {
                .slotIndex = i,
                .pawn = prevP.pawn
            });
            const uint64_t lastValidAge =
                (s_lastValidAliveUs[i] > 0 && nowUs >= s_lastValidAliveUs[i])
                ? nowUs - s_lastValidAliveUs[i]
                : esp::render::kPrevTickFallbackMaxAgeUs + 1;
            const bool prevFallbackAllowed =
                esp::render::ShouldUsePrevTickFallback(
                    prevValidAlive,
                    slotIdentityMatchesCurrent,
                    slotIdentityMatchesCache,
                    currentConfirmedDead,
                    prevIsLocal,
                    lastValidAge);

            esp::PlayerData fallbackBuf;
            const esp::PlayerData* effective = nullptr;
            if (currentValid) {
                effective = &current;
            } else if (prevFallbackAllowed) {
                fallbackBuf = prevP;
                fallbackBuf.staleFrames =
                    esp::render::IncrementFallbackStaleFrames(fallbackBuf.staleFrames);
                effective = &fallbackBuf;
                if (esp::render::ShouldRecordDrawEvent(nowUs, s_lastDrawValidHoldEventUs[i])) {
                    s_lastDrawValidHoldEventUs[i] = nowUs;
                    RecordEspEvent({
                        .type = EspEventType::SnapshotFallback,
                        .slot = static_cast<uint8_t>(i)
                    });
                }
            } else {
                
                
                
                if (esp::render::ShouldClearLastAliveCache(current.pawn, s_lastValidPawn[i])) {
                    s_lastValidAliveUs[i] = 0;
                    s_lastValidPawn[i] = 0;
                }
                continue;
            }
            const esp::PlayerData& p = *effective;
            if (!esp::render::IsFreshTimestamp(p.coreUpdatedAtUs, nowUs, esp::render::kPlayerRenderMaxAgeUs))
                continue;
            const bool prevSlotAlive =
                prevP.valid && prevP.pawn != 0 && prevP.pawn == p.pawn && prevP.health > 0;

            if (p.health <= 0 &&
                prevSlotAlive &&
                esp::render::ShouldRecordDrawEvent(nowUs, s_lastDrawDeathHoldEventUs[i])) {
                s_lastDrawDeathHoldEventUs[i] = nowUs;
                RecordEspEvent({
                    .type = EspEventType::DeathHold,
                    .slot = static_cast<uint8_t>(i)
                });
            }
            const int effectiveHealth = (p.health > 0) ? p.health : (prevSlotAlive ? prevP.health : 0);
            if (effectiveHealth <= 0)
                continue;
            if (IsLocalSnapshotSlot(snap, {
                    .slotIndex = i,
                    .pawn = p.pawn
                })) {
                continue;
            }
            if (!g::espShowTeammates && (localTeam == 2 || localTeam == 3) && p.team == localTeam)
                continue;

            const bool canBlendSnapshots =
                prevP.valid &&
                esp::render::CanInterpolatePlayer(p.pawn, prevP.pawn, p.pawnHandle,
                    prevP.pawnHandle, prevP.health, p.position, prevP.position) &&
                playerCaptureTimeUs > prevPlayerCaptureTimeUs;

            Vector3 effectivePos = p.position;
            if (!isValidWorldPos(effectivePos)) {
                if (prevSlotAlive && isValidWorldPos(prevP.position)) {
                    if (esp::render::ShouldRecordDrawEvent(nowUs, s_lastDrawInvalidPosEventUs[i])) {
                        s_lastDrawInvalidPosEventUs[i] = nowUs;
                        RecordEspEvent({
                            .type = EspEventType::InvalidPositionFallback,
                            .slot = static_cast<uint8_t>(i)
                        });
                    }
                    effectivePos = prevP.position;
                } else {
                    continue;
                }
            }

            const Vector3 smoothedPos =
                canBlendSnapshots ? lerpVec3(prevP.position, effectivePos) : effectivePos;
            const esp::render::PlayerMotionTransform motion =
                esp::render::ResolvePlayerMotion(
                    effectivePos,
                    smoothedPos,
                    p.velocity,
                    esp::validation::IsValidVelocity(p.velocity),
                    extrapolationSec);
            const Vector3 renderPlayerPos = motion.renderPosition;
            if (!isValidWorldPos(renderPlayerPos))
                continue;
            const Vector3 positionOffset = motion.interpolationOffset;
            const Vector3 velocityOffset = motion.extrapolationOffset;
            const Vector3 fallbackFeetPos = renderPlayerPos;
            Vector3 fallbackHeadPos = renderPlayerPos;
            fallbackHeadPos.z += esp::render::kFallbackStandingHeight;

            
            Vector3 feetPos;
            Vector3 headPos;
            ScreenPos boneScreen[esp::kSkeletonScreenBoneCapacity] = {};
            bool boneScreenValid[esp::kSkeletonScreenBoneCapacity] = {};
            int validBoneSegmentCount = 0;
            bool renderHasReliableBones = false;
            int leftToeBoneId = -1;
            int rightToeBoneId = -1;
            bool hasLeftToeBone = false;
            bool hasRightToeBone = false;

            if (p.hasBones && esp::render::IsFreshTimestamp(p.bonesUpdatedAtUs, nowUs, esp::render::kPoseRenderMaxAgeUs)) {
                const Vector3 poseAnchor = p.boneAnchorValid && IsFiniteVec(p.boneAnchorPosition)
                    ? p.boneAnchorPosition : renderPlayerPos;
                leftToeBoneId = selectToeBoneId(p, true);
                rightToeBoneId = selectToeBoneId(p, false);
                const Vector3 headBone = getPlayerBone(p, esp::HEAD);
                const Vector3 pelvisBone = getPlayerBone(p, esp::PELVIS);
                const bool canBlendBones = canBlendSnapshots && prevP.hasBones &&
                    prevP.pawnHandle == p.pawnHandle &&
                    (headBone - getPlayerBone(prevP, esp::HEAD)).Length() < 128.0f;
                const Vector3 chestBone = getPlayerBone(p, esp::CHEST);
                const Vector3 leftHeelBone = getPlayerBone(p, esp::FOOT_HEEL_L);
                const Vector3 rightHeelBone = getPlayerBone(p, esp::FOOT_HEEL_R);
                const Vector3 leftToeBone = leftToeBoneId >= 0 ? getPlayerBone(p, leftToeBoneId) : Vector3{};
                const Vector3 rightToeBone = rightToeBoneId >= 0 ? getPlayerBone(p, rightToeBoneId) : Vector3{};
                const bool hasHeadBone = hasUsableBone(p, esp::HEAD);
                const bool hasPelvisBone = hasUsableBone(p, esp::PELVIS);
                const bool hasChestBone = hasUsableBone(p, esp::CHEST);
                const bool hasLeftHeelBone = hasUsableBone(p, esp::FOOT_HEEL_L);
                const bool hasRightHeelBone = hasUsableBone(p, esp::FOOT_HEEL_R);
                hasLeftToeBone =
                    leftToeBoneId >= 0 &&
                    hasLeftHeelBone &&
                    isPlausibleToeSegment(leftHeelBone, leftToeBone);
                hasRightToeBone =
                    rightToeBoneId >= 0 &&
                    hasRightHeelBone &&
                    isPlausibleToeSegment(rightHeelBone, rightToeBone);
                const bool hasLeftFootBone = hasLeftHeelBone;
                const bool hasRightFootBone = hasRightHeelBone;
                const bool hasCoreBoneChain =
                    hasHeadBone &&
                    hasPelvisBone &&
                    (hasChestBone || hasUsableBone(p, esp::SPINE2));
                int validStoredBoneCount = 0;
                for (int bIdx = 0; bIdx < esp::kPlayerStoredBoneCount; ++bIdx) {
                    if (hasUsableBone(p, esp::kPlayerStoredBoneIds[bIdx]))
                        ++validStoredBoneCount;
                }
                float skeletonHeight = headBone.z - pelvisBone.z;
                if (hasHeadBone && (hasLeftFootBone || hasRightFootBone)) {
                    float lowestFootZ = FLT_MAX;
                    if (hasLeftHeelBone)
                        lowestFootZ = std::min(lowestFootZ, leftHeelBone.z);
                    if (hasRightHeelBone)
                        lowestFootZ = std::min(lowestFootZ, rightHeelBone.z);
                    skeletonHeight = headBone.z - lowestFootZ;
                }
                const float pelvisAnchor2D =
                    hasPelvisBone
                    ? static_cast<float>(std::hypot(
                        pelvisBone.x - poseAnchor.x,
                        pelvisBone.y - poseAnchor.y))
                    : FLT_MAX;
                float footAnchor2D = FLT_MAX;
                if (hasLeftFootBone) {
                    footAnchor2D = std::min(
                        footAnchor2D,
                        static_cast<float>(std::hypot(
                            leftHeelBone.x - poseAnchor.x,
                            leftHeelBone.y - poseAnchor.y)));
                }
                if (hasRightFootBone) {
                    footAnchor2D = std::min(
                        footAnchor2D,
                        static_cast<float>(std::hypot(
                            rightHeelBone.x - poseAnchor.x,
                            rightHeelBone.y - poseAnchor.y)));
                }
                const bool plausibleBoneAnchor =
                    pelvisAnchor2D <= 96.0f ||
                    footAnchor2D <= 96.0f;
                const bool plausibleBoneHeight =
                    skeletonHeight >= ((hasLeftFootBone || hasRightFootBone) ? 20.0f : 12.0f) &&
                    skeletonHeight <= 112.0f;
                renderHasReliableBones =
                    hasCoreBoneChain &&
                    validStoredBoneCount >= 6 &&
                    plausibleBoneAnchor &&
                    plausibleBoneHeight;

                if (renderHasReliableBones) {
                    headPos = headBone;
                    if (canBlendBones)
                        headPos = lerpVec3(getPlayerBone(prevP, esp::HEAD), headBone);
                    else
                        headPos = headPos + positionOffset;
                    if (extrapolationSec > 0.0f)
                        headPos = headPos + velocityOffset;
                    headPos.z += esp::render::kFallbackHeadPadding;

                    Vector3 leftFoot = leftHeelBone;
                    Vector3 rightFoot = rightHeelBone;
                    if (canBlendBones) {
                        const Vector3 prevLeftFoot = getPlayerBone(prevP, esp::FOOT_HEEL_L);
                        const Vector3 prevRightFoot = getPlayerBone(prevP, esp::FOOT_HEEL_R);
                        if (IsFiniteVec(prevLeftFoot) &&
                            !(prevLeftFoot.x == 0.0f && prevLeftFoot.y == 0.0f && prevLeftFoot.z == 0.0f))
                            leftFoot = lerpVec3(prevLeftFoot, leftFoot);
                        if (IsFiniteVec(prevRightFoot) &&
                            !(prevRightFoot.x == 0.0f && prevRightFoot.y == 0.0f && prevRightFoot.z == 0.0f))
                            rightFoot = lerpVec3(prevRightFoot, rightFoot);
                    } else {
                        leftFoot = leftFoot + positionOffset;
                        rightFoot = rightFoot + positionOffset;
                    }
                    if (extrapolationSec > 0.0f) {
                        leftFoot = leftFoot + velocityOffset;
                        rightFoot = rightFoot + velocityOffset;
                    }

                    const bool hasLeft = hasLeftFootBone;
                    const bool hasRight = hasRightFootBone;
                    if (hasLeft && hasRight)
                        feetPos = (leftFoot.z < rightFoot.z) ? leftFoot : rightFoot;
                    else if (hasLeft)
                        feetPos = leftFoot;
                    else if (hasRight)
                        feetPos = rightFoot;
                    else
                        feetPos = poseAnchor + positionOffset + velocityOffset;
                    feetPos.z -= esp::render::kFallbackFeetPadding;

                    if (s_lastValidBonePawn[i] != p.pawn || s_lastValidBoneHandle[i] != p.pawnHandle ||
                        (headBone - s_lastValidBonePos[i][esp::PlayerStoredBoneIndex(esp::HEAD)]).Length() >= 128.0f) {
                        for (int rb = 0; rb < esp::kPlayerStoredBoneCount; ++rb) {
                            s_lastValidBonePos[i][rb] = {};
                            s_lastValidBoneUs[i][rb] = 0;
                        }
                        s_lastValidBonePawn[i] = p.pawn;
                        s_lastValidBoneHandle[i] = p.pawnHandle;
                    }
                    for (int bIdx = 0; bIdx < esp::kPlayerStoredBoneCount; ++bIdx) {
                        const int b = esp::kPlayerStoredBoneIds[bIdx];
                        Vector3 bonePos = getPlayerBone(p, b);
                        const bool bonePosValid =
                            hasUsableBone(p, b);
                        if (bonePosValid) {
                            s_lastValidBonePos[i][bIdx] = bonePos;
                            s_lastValidBoneUs[i][bIdx] = p.bonesUpdatedAtUs;
                        } else if (esp::render::ShouldReusePersistedBone(
                                       s_lastValidBoneUs[i][bIdx],
                                       nowUs)) {
                            bonePos = s_lastValidBonePos[i][bIdx];
                        } else {
                            boneScreenValid[b] = false;
                            continue;
                        }
                        if (canBlendBones) {
                            const Vector3 prevBone = getPlayerBone(prevP, b);
                            if (!(prevBone.x == 0.0f && prevBone.y == 0.0f && prevBone.z == 0.0f))
                                bonePos = lerpVec3(prevBone, bonePos);
                        } else {
                            bonePos = bonePos + positionOffset;
                        }
                        if (extrapolationSec > 0.0f)
                            bonePos = bonePos + velocityOffset;
                        boneScreen[b] = WorldToScreen(bonePos, viewMatrix, screenW, screenH);
                        boneScreenValid[b] = boneScreen[b].onScreen;
                    }
                    for (int pairIdx = 0; pairIdx < skeletonPairCount; ++pairIdx) {
                        const BonePair& pair = skeletonPairs[pairIdx];
                        if (boneScreenValid[pair.from] && boneScreenValid[pair.to])
                            ++validBoneSegmentCount;
                    }
                    if (boneScreenValid[esp::FOOT_HEEL_L] && hasLeftToeBone && boneScreenValid[leftToeBoneId])
                        ++validBoneSegmentCount;
                    if (boneScreenValid[esp::FOOT_HEEL_R] && hasRightToeBone && boneScreenValid[rightToeBoneId])
                        ++validBoneSegmentCount;
                }
            }

            if (!renderHasReliableBones) {
                feetPos = renderPlayerPos;
                headPos = feetPos;
                headPos.z += esp::render::kFallbackStandingHeight;
            }

            if (!IsFiniteVec(feetPos) || !IsFiniteVec(headPos))
                continue;

            ScreenPos screenFeet = WorldToScreen(feetPos, viewMatrix, screenW, screenH);
            ScreenPos screenHead = WorldToScreen(headPos, viewMatrix, screenW, screenH);
            const ScreenPos fallbackScreenFeet = WorldToScreen(fallbackFeetPos, viewMatrix, screenW, screenH);
            const ScreenPos fallbackScreenHead = WorldToScreen(fallbackHeadPos, viewMatrix, screenW, screenH);
            bool usingFallbackBox = false;
            bool onScreen = screenFeet.onScreen && screenHead.onScreen;
            const bool fallbackOnScreen = fallbackScreenFeet.onScreen && fallbackScreenHead.onScreen;
            if (esp::render::ShouldUseFallbackProjectionBox(
                    onScreen,
                    screenFeet.y,
                    screenHead.y,
                    fallbackOnScreen)) {
                screenFeet = fallbackScreenFeet;
                screenHead = fallbackScreenHead;
                feetPos = fallbackFeetPos;
                headPos = fallbackHeadPos;
                onScreen = true;
                usingFallbackBox = true;
            }
            if (!onScreen) {
#include "../Render/player_offscreen_arrows.inl"
                continue;
            }

            float boxHeight = screenFeet.y - screenHead.y;
            if (!esp::render::IsRenderableBoxHeight(boxHeight) && fallbackOnScreen) {
                screenFeet = fallbackScreenFeet;
                screenHead = fallbackScreenHead;
                feetPos = fallbackFeetPos;
                headPos = fallbackHeadPos;
                boxHeight = screenFeet.y - screenHead.y;
                usingFallbackBox = true;
            }
            if (!esp::render::IsRenderableBoxHeight(boxHeight))
                continue;
            float boxWidth = boxHeight * 0.5f;
            float boxLeft = screenHead.x - boxWidth * 0.5f;
            float boxTop = screenHead.y;
            const float boxCenterX = boxLeft + boxWidth * 0.5f;
            const float sideBarWidth = 3.0f;
            const float sideBarGap = 4.0f;
            const float primaryLeftBarX = boxLeft - sideBarWidth - sideBarGap;
            const esp::render::SkeletonGateResult skeletonGate =
                esp::render::EvaluateSkeletonGate({
                    renderHasReliableBones,
                    validBoneSegmentCount,
                });
            const bool canRenderRealSkeleton = esp::render::ShouldDrawSkeleton(skeletonGate);

            bool isVisibleNow = p.visible;
            const ImU32 entityCol = g::espVisibilityColoring ? (isVisibleNow ? visibleCol : hiddenCol) : boxCol;



#include "../Render/player_box.inl"
#include "../Render/player_health_armor.inl"
#include "../Render/player_name.inl"

            float bottomTextY = boxTop + boxHeight + 2.0f;
            auto drawBottomLabel = [&](const char* text, ImU32 color, bool requireVisible, bool strictBounds, ImFont* font, float fontSize) {
                if (!text || text[0] == '\0')
                    return;
                if (requireVisible && !isVisibleNow)
                    return;
                if (!font)
                    font = ImGui::GetFont();
                if (!font)
                    return;
                if (fontSize <= 0.0f)
                    fontSize = ImGui::GetFontSize();
                ImVec2 textSize = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text, nullptr);
                float textX = boxCenterX - textSize.x * 0.5f;
                const float textY = bottomTextY;
                if (strictBounds) {
                    if (textX < 2.0f || textX + textSize.x > screenW - 2.0f || textY + textSize.y > screenH - 2.0f)
                        return;
                } else {
                    if (textX < 2.0f) textX = 2.0f;
                    if (textX + textSize.x > screenW - 2.0f) textX = screenW - textSize.x - 2.0f;
                    if (textY + textSize.y > screenH - 2.0f)
                        return;
                }
                drawList->AddText(font, fontSize, ImVec2(textX + 1.0f, textY + 1.0f), IM_COL32(0, 0, 0, 210), text);
                drawList->AddText(font, fontSize, ImVec2(textX, textY), color, text);
                bottomTextY += textSize.y + 1.0f;
            };

            auto drawBottomWeaponIcon = [&](uint16_t itemId, ImU32 color, float height) {
                ImVec2 iconSize = {};
                if (!esp::render::weapon_icons::CalculateDrawSize(itemId, height, &iconSize))
                    return false;

                const float iconX = boxCenterX - iconSize.x * 0.5f;
                const float iconY = bottomTextY;
                if (iconX < 2.0f ||
                    iconX + iconSize.x > screenW - 2.0f ||
                    iconY + iconSize.y > screenH - 2.0f) {
                    return false;
                }

                if (!esp::render::weapon_icons::Draw(
                        drawList,
                        itemId,
                        ImVec2(iconX, iconY),
                        height,
                        color)) {
                    return false;
                }
                bottomTextY += iconSize.y + 1.0f;
                return true;
            };

#include "../Render/player_weapon_and_bomb.inl"
#include "../Render/player_weapon_ammo.inl"
#include "../Render/player_flags.inl"
#include "../Render/player_skeleton.inl"
#include "../Render/player_snaplines.inl"
        }
    }

    if (usableViewMatrix) {
#include "../Render/world_esp.inl"

        world::grenade_helper::DrawOverlay(
            viewMatrix,
            renderLocalPos,
            viewAngles,
            snap.activeMapKey,
            screenW,
            screenH);

#include "../Render/bomb_esp.inl"
    }

#include "Features/Radar/Render/radar_overlay.inl"
#include "../Render/bomb_timer_overlay.inl"
#include "../Render/spectator_list_overlay.inl"

    g::configJustLoaded = false;
}
