    {
        static uint64_t s_coreRepairResetSerial = 0;
        static uint8_t s_coreRepairStreaks[64] = {};
        static uint64_t s_lastCoreRepairAttemptUs[64] = {};
        static uint32_t s_partialCoreStreak = 0;
        static uint64_t s_partialCoreSinceUs = 0;
        static uint64_t s_lastPartialCoreBadMask = 0;
        static uint32_t s_partialCoreConfirmedStreak = 0;
        static bool s_partialCoreIncidentActive = false;
        const uint64_t coreRepairResetSerial = s_sceneResetSerial.load(std::memory_order_relaxed);
        if (s_coreRepairResetSerial != coreRepairResetSerial) {
            s_coreRepairResetSerial = coreRepairResetSerial;
            memset(s_coreRepairStreaks, 0, sizeof(s_coreRepairStreaks));
            memset(s_lastCoreRepairAttemptUs, 0, sizeof(s_lastCoreRepairAttemptUs));
            s_partialCoreStreak = 0;
            s_partialCoreSinceUs = 0;
            s_lastPartialCoreBadMask = 0;
            s_partialCoreConfirmedStreak = 0;
            s_partialCoreIncidentActive = false;
        }
        const int repairSlotLimit = std::max(
            playerSlotScanLimit,
            std::clamp(s_playerHierarchyHighWaterSlot.load(std::memory_order_relaxed), 0, 64));
        for (int i = 0; i < repairSlotLimit; ++i) {
            if (pawns[i])
                continue;
            s_coreRepairStreaks[i] = 0;
            s_lastCoreRepairAttemptUs[i] = 0;
        }

        int pawnCount = 0;
        int saneCoreCount = 0;
        uint64_t partialCoreBadMask = 0;
        const uint64_t coreRepairNowUs = TickNowUs();
        const uint64_t recentResetUs = s_lastSceneResetUs.load(std::memory_order_relaxed);
        const bool recentStructuralReset =
            esp::data::IsRecentStructuralReset(recentResetUs, coreRepairNowUs);

        const auto coreRepairPolicy =
            esp::data::SelectCoreRepairPolicy(sceneSettling, recentStructuralReset);
        
        bool anyCoreRepairNeeded = false;
        for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
            const int i = playerResolvedSlots[resolvedIdx];
            if (!pawns[i]) {
                s_coreRepairStreaks[i] = 0;
                s_lastCoreRepairAttemptUs[i] = 0;
                continue;
            }
            ++pawnCount;
            if (coreReadPlausible[i]) {
                s_coreRepairStreaks[i] = 0;
                continue;
            }
            partialCoreBadMask |= (1ull << static_cast<unsigned>(i));
            if (s_coreRepairStreaks[i] < 255u)
                ++s_coreRepairStreaks[i];
            if (s_coreRepairStreaks[i] < coreRepairPolicy.repairThreshold)
                continue;
            if (!esp::data::IsCoreRepairAttemptDue(
                    s_lastCoreRepairAttemptUs[i],
                    coreRepairNowUs,
                    esp::data::SelectCoreRepairRetryIntervalUs(
                        coreRepairPolicy.retryIntervalUs,
                        s_coreRepairStreaks[i])))
                continue;
            s_lastCoreRepairAttemptUs[i] = coreRepairNowUs;
            queueMandatoryCoreReads(i, coreRepairNowUs, false);
            anyCoreRepairNeeded = true;
        }
        if (anyCoreRepairNeeded) {
            executeOptionalScatterRead();
            // Repairs use the same coalesced vital blocks as the first batch.
            // Unpack them and refresh per-field caches before evaluating success.
            applyCoreBatchResults();
        }
        for (int resolvedIdx = 0; resolvedIdx < playerResolvedSlotCount; ++resolvedIdx) {
            const int i = playerResolvedSlots[resolvedIdx];
            if (pawns[i] &&
                coreReadCompleted(i) &&
                coreStateLooksSane(i)) {
                s_coreRepairStreaks[i] = 0;
                coreReadFresh[i] = true;
                coreReadPlausible[i] = true;
                coreReadAlive[i] = healths[i] > 0 && lifeStates[i] == 0;
                gunGameImmunityReadFresh[i] =
                    esp::data::IsBinaryPlayerFlagReadComplete(
                        wantsGunGameImmunity,
                        gunGameImmunityBytesRead[i],
                        gunGameImmunityFlags[i]);
                markCoreAvailability(i, true);
                ++saneCoreCount;
            }
        }

        const bool localCoreEvidence =
            localPawn != 0 ||
            (localControllerPawnHandle != 0u && localControllerPawnHandle != 0xFFFFFFFFu);
        const bool coreLooksPartial =
            esp::data::IsCorePartial(
                pawnCount,
                saneCoreCount,
                coreRepairPolicy.missingTolerance);
        if (coreLooksPartial && localCoreEvidence) {
            if (s_partialCoreStreak < 0xFFFFFFFFu)
                ++s_partialCoreStreak;
            if (s_partialCoreSinceUs == 0)
                s_partialCoreSinceUs = coreRepairNowUs;
            const bool repeatedBadSlots =
                esp::data::ArePartialCoreBadSlotsRepeated(
                    s_lastPartialCoreBadMask,
                    partialCoreBadMask);
            if (repeatedBadSlots) {
                if (s_partialCoreConfirmedStreak < 0xFFFFFFFFu)
                    ++s_partialCoreConfirmedStreak;
            } else {
                s_partialCoreConfirmedStreak = 0;
            }
            s_lastPartialCoreBadMask = partialCoreBadMask;
            const uint64_t partialAgeUs =
                esp::data::ElapsedSinceOrZero(coreRepairNowUs, s_partialCoreSinceUs);
            const bool confirmedPartial =
                esp::data::IsPartialCoreConfirmed(
                    sceneSettling,
                    repeatedBadSlots,
                    partialCoreBadMask,
                    s_partialCoreConfirmedStreak,
                    partialAgeUs,
                    recentStructuralReset);
            if (esp::data::ShouldReportPartialCoreIncident(
                    sceneSettling,
                    s_partialCoreStreak,
                    coreRepairPolicy.partialCoreThreshold,
                    confirmedPartial) &&
                !s_partialCoreIncidentActive) {
                s_partialCoreIncidentActive = true;
                s_playerCoreGlobalRefreshAvoidedCount.fetch_add(1, std::memory_order_relaxed);
            }
        } else {
            s_partialCoreStreak = 0;
            s_partialCoreSinceUs = 0;
            s_lastPartialCoreBadMask = 0;
            s_partialCoreConfirmedStreak = 0;
            s_partialCoreIncidentActive = false;
        }
    }
