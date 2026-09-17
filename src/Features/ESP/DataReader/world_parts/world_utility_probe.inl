        
        
        
        auto& isUnknownUtilityProbe = worldUnknownUtilityProbe;
        std::memset(isUnknownUtilityProbe, 0, sizeof(isUnknownUtilityProbe));
        if (worldScanOk && shouldReadWorldIdentityProbeDetails &&
            cachedIdentityRefreshCount > 0)
            s_lastWorldUtilityProbeScanUs = nowUs;
