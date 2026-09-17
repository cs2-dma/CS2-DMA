            
            
            
            // Rules-dropped: accept dormant C4 too. Ghost last-round entities
            // often stay non-dormant at the old origin while the live drop is
            // briefly dormant in PVS; skipping dormant hid the real bomb.
            const bool c4DormantOk =
                worldDormantFlags[idx] == 0u || bombDroppedByRules;
            if (rawItemId == kWeaponC4Id &&
                worldC4DynamicComplete[idx] != 0u &&
                worldSceneNodes[idx] != 0 &&
                c4DormantOk &&
                posNonOrigin) {
                rememberWorldBombCandidateSlot(idx);
                const bool c4OwnerAlive =
                    ownerPlayerIndex >= 0 && ownerPlayerIndex < 64 &&
                    healths[ownerPlayerIndex] > 0 &&
                    lifeStates[ownerPlayerIndex] == 0;
                bool c4NearTeamT = false;
                for (int resolvedIdx = 0;
                     resolvedIdx < playerResolvedSlotCount;
                     ++resolvedIdx) {
                    const int playerIdx = playerResolvedSlots[resolvedIdx];
                    if (playerIdx < 0 || playerIdx >= 64 ||
                        teams[playerIdx] != esp::data::kBombCarrierTeamT ||
                        !std::isfinite(positions[playerIdx].x) ||
                        !std::isfinite(positions[playerIdx].y) ||
                        !std::isfinite(positions[playerIdx].z)) {
                        continue;
                    }
                    const float playerDx = pos.x - positions[playerIdx].x;
                    const float playerDy = pos.y - positions[playerIdx].y;
                    if ((playerDx * playerDx + playerDy * playerDy) <=
                        (768.0f * 768.0f)) {
                        c4NearTeamT = true;
                        break;
                    }
                }
                bool continuousWithPublishedDrop = false;
                if (s_bombState.dropped &&
                    s_bombState.confidence >= esp::data::kDroppedC4ConfirmedScore &&
                    (s_bombState.sourceFlags & BombResolveSourceWorldC4) != 0u &&
                    IsFiniteVec(s_bombState.position)) {
                    const float c4Dx = pos.x - s_bombState.position.x;
                    const float c4Dy = pos.y - s_bombState.position.y;
                    continuousWithPublishedDrop =
                        (c4Dx * c4Dx + c4Dy * c4Dy) <= (160.0f * 160.0f);
                }
                int c4CandidateScore =
                    esp::data::ScoreWorldDroppedC4Candidate(
                        bombDroppedByRules,
                        noOwner,
                        ownerPlayerIndex,
                        c4OwnerAlive,
                        ownerHoldingNearby,
                        ownerActiveWeaponMatches,
                        c4NearTeamT,
                        continuousWithPublishedDrop);
                // When rules say dropped, IGNORE dwWeaponC4 match entirely;
                // it commonly still points at the previous-round ghost entity
                // sitting at the last drop origin.
                if (!bombDroppedByRules && weaponC4Entity != 0) {
                    c4CandidateScore +=
                        ent == weaponC4Entity ? 400 : -400;
                }
                const bool candidateDormant = worldDormantFlags[idx] != 0u;
                if (esp::data::ShouldReplaceWorldC4Candidate(
                        worldScanFoundC4,
                        worldDropTicks[idx],
                        worldScanC4DropTick,
                        c4CandidateScore,
                        worldScanC4Score,
                        candidateDormant,
                        worldScanC4Dormant)) {
                    worldScanFoundC4 = true;
                    worldScanC4Score = c4CandidateScore;
                    worldScanC4Entity = ent;
                    worldScanC4Pos = pos;
                    worldScanC4OwnerIdx = ownerPlayerIndex;
                    worldScanC4NoOwner = noOwner;
                    worldScanC4OwnerAlive = c4OwnerAlive;
                    worldScanC4OwnerNearby = ownerHoldingNearby;
                    worldScanC4Dormant = candidateDormant;
                    worldScanC4DropTick = worldDropTicks[idx];
                }
            }

            if (droppedItemCandidate &&
                droppedOwnerReleased &&
                worldSceneNodes[idx] != 0 &&
                posNonOrigin) {
                pushWorldMarker(WorldMarkerType::DroppedWeapon, pos, itemId, 0.0f, nowUs + 400000);
            }
