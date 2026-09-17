    {
        const uint64_t playerAuxDelta = _stagePlayerAuxEnd - _stageCommitEnd;
        const uint64_t inventoryDelta = _stageInvEnd - _stagePlayerAuxEnd;
        const uint64_t boneReadsDelta = _stageBoneEnd - _stageInvEnd;
        s_stageTimingSequence.fetch_add(1, std::memory_order_acq_rel);
        s_stageEngineUs.store(_stageEngineEnd - _stagePipelineStart, std::memory_order_relaxed);
        s_stageBaseReadsUs.store(_stageBaseEnd - _stageEngineEnd, std::memory_order_relaxed);
        s_stagePlayerReadsUs.store(_stagePlayerEnd - _stageBaseEnd, std::memory_order_relaxed);
        s_stagePlayerHierarchyUs.store(_playerHierarchyUs, std::memory_order_relaxed);
        s_stagePlayerCoreUs.store(_playerCoreUs, std::memory_order_relaxed);
        s_stagePlayerRepairUs.store(_playerRepairUs, std::memory_order_relaxed);
        s_stageCommitStateUs.store(_stageCommitEnd - _stagePlayerEnd, std::memory_order_relaxed);
        s_stagePlayerAuxUs.store(playerAuxDelta, std::memory_order_relaxed);
        s_stageInventoryUs.store(inventoryDelta, std::memory_order_relaxed);
        s_stageBoneReadsUs.store(boneReadsDelta, std::memory_order_relaxed);
        if (_playerAuxActiveTick) {
            s_stagePlayerAuxLastUs.store(playerAuxDelta, std::memory_order_relaxed);
            s_stagePlayerAuxLastAtUs.store(_stagePlayerAuxEnd, std::memory_order_relaxed);
        }
        if (_inventoryActiveTick || _inventoryFullTick) {
            s_stageInventoryLastUs.store(inventoryDelta, std::memory_order_relaxed);
            s_stageInventoryLastAtUs.store(_stageInvEnd, std::memory_order_relaxed);
        }
        if (_boneReadsActiveTick) {
            s_stageBoneReadsLastUs.store(boneReadsDelta, std::memory_order_relaxed);
            s_stageBoneReadsLastAtUs.store(_stageBoneEnd, std::memory_order_relaxed);
        }
        const uint64_t worldScanDelta = _stageWorldEnd - _stageBoneEnd;
        s_stageWorldScanUs.store(worldScanDelta, std::memory_order_relaxed);
        if (worldScanCommitted || worldScanDelta > 500)
            s_stageWorldScanLastUs.store(worldScanDelta, std::memory_order_relaxed);
        s_stageBombScanUs.store(_stageBombEnd - _stageWorldEnd, std::memory_order_relaxed);
        s_stageCommitEnrichUs.store(_stageEnrichEnd - _stageBombEnd, std::memory_order_relaxed);
        s_stageTimingSequence.fetch_add(1, std::memory_order_release);

        const uint64_t totalCycleUs = _stageEnrichEnd - _stagePipelineStart;
        static uint64_t lastStallLogUs = 0;
        const uint64_t stallNowUs = TickNowUs();
        if (esp::worker::ShouldLogWorkerStall(totalCycleUs, lastStallLogUs, stallNowUs)) {
            lastStallLogUs = stallNowUs;
            DmaLogPrintf(
                "[PERF] Stall detected! total=%.2f ms (Engine=%.2f Base=%.2f Players=%.2f Commit=%.2f Aux=%.2f Inv=%.2f Bones=%.2f World=%.2f Bomb=%.2f Enrich=%.2f)",
                static_cast<double>(totalCycleUs) / 1000.0,
                static_cast<double>(_stageEngineEnd - _stagePipelineStart) / 1000.0,
                static_cast<double>(_stageBaseEnd - _stageEngineEnd) / 1000.0,
                static_cast<double>(_stagePlayerEnd - _stageBaseEnd) / 1000.0,
                static_cast<double>(_stageCommitEnd - _stagePlayerEnd) / 1000.0,
                static_cast<double>(playerAuxDelta) / 1000.0,
                static_cast<double>(inventoryDelta) / 1000.0,
                static_cast<double>(boneReadsDelta) / 1000.0,
                static_cast<double>(worldScanDelta) / 1000.0,
                static_cast<double>(_stageBombEnd - _stageWorldEnd) / 1000.0,
                static_cast<double>(_stageEnrichEnd - _stageBombEnd) / 1000.0
            );
        }
    }
