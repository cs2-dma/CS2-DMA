        if (shouldScanWorld && worldScanCommitted) {
            s_worldMarkerCount = esp::data::PublishBoundedWorldMarkers(
                scannedMarkers, std::size(scannedMarkers), scannedMarkerCount,
                s_worldMarkers, nowUs, [](const WorldMarker& marker) {
                    if (marker.type == WorldMarkerType::DroppedWeapon)
                        return marker.weaponId == kWeaponC4Id ? 0 : 3;
                    if (marker.type == WorldMarkerType::SmokeProjectile ||
                        marker.type == WorldMarkerType::MolotovProjectile ||
                        marker.type == WorldMarkerType::DecoyProjectile)
                        return 2;
                    return 1; // Active effect timers precede ordinary dropped items.
                });
            const int capacityDrops = (std::max)(0,
                esp::data::ClampWorldMarkerCount(scannedMarkerCount, std::size(scannedMarkers)) -
                static_cast<int>(std::size(s_worldMarkers)));
            s_worldMarkerCapacityDropsStat.fetch_add(capacityDrops, std::memory_order_relaxed);
            s_worldMarkerReadGapHoldsStat.fetch_add(worldMarkerReadGapHolds, std::memory_order_relaxed);
            s_worldPositionReadMissesStat.fetch_add(worldPositionReadMisses, std::memory_order_relaxed);
        } else {
            s_worldMarkerCount = esp::data::ClampWorldMarkerCount(
                s_worldMarkerCount, std::size(s_worldMarkers));
            for (int i = 0; i < s_worldMarkerCount; ++i) {
                if (s_worldMarkers[i].valid && s_worldMarkers[i].expiresUs > 0 &&
                    s_worldMarkers[i].expiresUs <= nowUs)
                    s_worldMarkers[i].valid = false;
            }
        }
