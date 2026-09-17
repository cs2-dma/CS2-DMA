    nlohmann::json payload = {
        {"m_seq", nowMs},
        {"m_ts", nowMs},
        {"m_server_time", nowMs},
        {"m_capture_time", nowMs},
        {"m_language", app::localization::LanguageCode(app::localization::GetLanguage())},
        {"m_map", mapName.empty() ? "unknown" : mapName},
        {"m_local_team", 0},
        {"m_updated_at", nowMs},
        {"m_players", nlohmann::json::array()},
        {"m_bomb", BuildLegacyBombJson(LegacyBombJsonState {
            .sequence = nowMs,
            .timestampMs = nowMs
        })}
    };
    return payload.dump();
