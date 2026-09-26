    const std::string resolvedMapKey = NormalizeMapName(snapshot.mapKey);
    if (!resolvedMapKey.empty()) {
        if (const auto* overview = radar::FindMapForPosition(resolvedMapKey,
                snapshot.localPos.x, snapshot.localPos.y, snapshot.localPos.z, previousMap))
            return overview->name;
        return resolvedMapKey;
    }

    if (!snapshot.hasMinimapBounds)
        return "unknown";

    const double runtimeMinX = static_cast<double>(snapshot.minimapMins.x);
    const double runtimeMaxX = static_cast<double>(snapshot.minimapMaxs.x);
    const double runtimeMinY = static_cast<double>(snapshot.minimapMins.y);
    const double runtimeMaxY = static_cast<double>(snapshot.minimapMaxs.y);

    if (!std::isfinite(runtimeMinX) || !std::isfinite(runtimeMaxX) ||
        !std::isfinite(runtimeMinY) || !std::isfinite(runtimeMaxY)) {
        return "unknown";
    }

    if (const auto* map = radar::ResolveMapByBounds(
            runtimeMinX,
            runtimeMinY,
            runtimeMaxX,
            runtimeMaxY,
            false)) {
        const auto* layer = radar::FindMapForPosition(map->name, snapshot.localPos.x,
            snapshot.localPos.y, snapshot.localPos.z, previousMap);
        return layer ? layer->name : map->name;
    }

    return "unknown";
