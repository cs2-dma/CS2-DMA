    using runtime_offsets::parse_utils::ParseCanonicalUtcTimestamp;
    using runtime_offsets::parse_utils::ParsePatchBuildNumber;
    using runtime_offsets::parse_utils::ParseSteamVersionTimestamp;
    using runtime_offsets::parse_utils::ToAddressHex;
    using runtime_offsets::parse_utils::ToHex;
    using runtime_offsets::parse_utils::Trim;
    using runtime_offsets::parse_utils::TryParseOffset;

    bool SourceTimestampDefinitelyPredatesCurrentPatch(
        std::string_view selectedSourceTimestamp,
        const SteamInfSnapshot& currentPatch)
    {
        const auto sourceTime = ParseCanonicalUtcTimestamp(selectedSourceTimestamp);
        const auto patchTime = ParseSteamVersionTimestamp(currentPatch.versionDate, currentPatch.versionTime);
        if (!sourceTime || !patchTime)
            return false;

        return (*sourceTime + std::chrono::hours(18)) < *patchTime;
    }

    bool PatchInfoEquals(const runtime_offsets::PatchInfo& a, const runtime_offsets::PatchInfo& b)
    {
        if (a.patchVersion.empty() || b.patchVersion.empty())
            return false;

        return a.patchVersion == b.patchVersion &&
               a.clientVersion == b.clientVersion &&
               a.sourceRevision == b.sourceRevision &&
               a.lastFileSha == b.lastFileSha;
    }

    std::string JsonStringOrEmpty(const json& root, const char* key)
    {
        const auto it = root.find(key);
        if (it == root.end() || !it->is_string())
            return {};
        return it->get<std::string>();
    }

    int JsonIntOrZero(const json& root, const char* key)
    {
        const auto it = root.find(key);
        if (it == root.end())
            return 0;

        if (it->is_number_integer())
        {
            const auto value = it->get<std::int64_t>();
            if (value >= 0 && value <= (std::numeric_limits<int>::max)())
                return static_cast<int>(value);
        }
        else if (it->is_number_unsigned())
        {
            const auto value = it->get<std::uint64_t>();
            if (value <= static_cast<std::uint64_t>((std::numeric_limits<int>::max)()))
                return static_cast<int>(value);
        }

        return 0;
    }
