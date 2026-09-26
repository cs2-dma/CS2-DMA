#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <json/json.hpp>

namespace app::updates::policy
{
    inline constexpr std::string_view kRepositoryUrl = "https://github.com/cs2-dma/CS2-DMA";
    inline constexpr std::string_view kReleasesUrl = "https://github.com/cs2-dma/CS2-DMA/releases";
    inline constexpr std::size_t kMaximumResponseBytes = 256 * 1024;
    inline constexpr uint64_t kAutomaticIntervalMs = 30 * 60 * 1000;
    inline constexpr uint64_t kManualCooldownMs = 60 * 1000;

    using Version = std::array<uint32_t, 4>;

    inline std::optional<Version> ParseVersion(std::string_view tag) noexcept
    {
        if (tag.empty() || tag.size() > 48) return {};
        if (tag.front() == 'v' || tag.front() == 'V') tag.remove_prefix(1);
        Version result = {};
        std::size_t count = 0;
        while (!tag.empty() && count < result.size()) {
            const auto dot = tag.find('.');
            const auto part = tag.substr(0, dot);
            if (part.empty() || (part.size() > 1 && part.front() == '0')) return {};
            for (const char c : part) if (c < '0' || c > '9') return {};
            const auto parsed = std::from_chars(part.data(), part.data() + part.size(), result[count++]);
            if (parsed.ec != std::errc{} || parsed.ptr != part.data() + part.size()) return {};
            if (dot == std::string_view::npos) return count >= 3 ? std::optional(result) : std::nullopt;
            tag.remove_prefix(dot + 1);
        }
        return {};
    }

    struct Release {
        std::string tag;
        std::string url;
        bool newer = false;
        std::string archiveName;
        std::string archiveUrl;
    };

    inline bool IsReleasePageUrl(std::string_view url, std::string_view tag)
    {
        return ParseVersion(tag) && url == std::string(kReleasesUrl) + "/tag/" + std::string(tag);
    }

    inline bool IsArchiveName(std::string_view name) noexcept
    {
        if (name.size() < 5 || name.size() > 160 || name.front() == '.') return false;
        for (const char c : name)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') || c == '.' || c == '-' || c == '_')) return false;
        const auto suffix = name.substr(name.size() - 4);
        return suffix[0] == '.' && (suffix[1] == 'z' || suffix[1] == 'Z') &&
            (suffix[2] == 'i' || suffix[2] == 'I') && (suffix[3] == 'p' || suffix[3] == 'P');
    }

    inline bool IsReleaseArchiveUrl(std::string_view url, std::string_view tag)
    {
        if (!ParseVersion(tag)) return false;
        const std::string prefix = std::string(kReleasesUrl) + "/download/" + std::string(tag) + "/";
        return url.starts_with(prefix) && IsArchiveName(url.substr(prefix.size()));
    }

    inline std::optional<bool> ParseUpdateReply(std::string_view reply) noexcept
    {
        const auto first = reply.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos) return true;
        reply = reply.substr(first, reply.find_last_not_of(" \t\r\n") - first + 1);
        if (reply == "y" || reply == "Y") return true;
        if (reply == "n" || reply == "N") return false;
        return {};
    }

    inline std::optional<std::string> ParseLatestRedirect(std::string_view location)
    {
        const std::string prefix = std::string(kReleasesUrl) + "/tag/";
        constexpr std::string_view relative = "/cs2-dma/CS2-DMA/releases/tag/";
        if (location.starts_with(prefix)) location.remove_prefix(prefix.size());
        else if (location.starts_with(relative)) location.remove_prefix(relative.size());
        else return {};
        if (!ParseVersion(location)) return {};
        return std::string(location);
    }

    inline nlohmann::json ParseReleaseAssetLinks(std::string_view html, std::string_view tag)
    {
        auto result = nlohmann::json::array();
        if (html.size() > kMaximumResponseBytes || !ParseVersion(tag)) return result;
        std::vector<std::string> seen;
        size_t offset = 0;
        while ((offset = html.find("<a ", offset)) != std::string_view::npos) {
            const size_t end = html.find('>', offset);
            if (end == std::string_view::npos) break;
            const auto anchor = html.substr(offset, end - offset);
            offset = end + 1;
            const size_t href = anchor.find(" href=");
            if (href == std::string_view::npos || href + 7 >= anchor.size()) continue;
            const char quote = anchor[href + 6];
            if (quote != '\'' && quote != '"') continue;
            const size_t finish = anchor.find(quote, href + 7);
            if (finish == std::string_view::npos) continue;
            const auto value = anchor.substr(href + 7, finish - href - 7);
            const std::string url = value.starts_with("/cs2-dma/CS2-DMA/releases/download/")
                ? "https://github.com" + std::string(value) : std::string(value);
            if (!IsReleaseArchiveUrl(url, tag) || std::find(seen.begin(), seen.end(), url) != seen.end()) continue;
            seen.push_back(url);
            result.push_back({{"state", "uploaded"}, {"name", url.substr(url.rfind('/') + 1)},
                {"browser_download_url", url}});
        }
        return result;
    }

    inline std::optional<Release> ParseRelease(std::string_view body, std::string_view currentTag)
    {
        const auto current = ParseVersion(currentTag);
        if (!current || body.empty() || body.size() > kMaximumResponseBytes) return {};
        const auto root = nlohmann::json::parse(body.begin(), body.end(), nullptr, false);
        if (!root.is_object()) return {};
        for (const char* key : {"draft", "prerelease"}) {
            const auto it = root.find(key);
            if (it == root.end() || !it->is_boolean() || it->get<bool>()) return {};
        }
        const auto tag = root.find("tag_name");
        const auto url = root.find("html_url");
        if (tag == root.end() || !tag->is_string() || url == root.end() || !url->is_string()) return {};
        Release release{tag->get<std::string>(), url->get<std::string>(), false, {}, {}};
        const auto version = ParseVersion(release.tag);
        if (!version || release.url != std::string(kReleasesUrl) + "/tag/" + release.tag) return {};
        release.newer = *version > *current;
        const auto assets = root.find("assets");
        if (assets != root.end() && assets->is_array()) {
            size_t candidates = 0;
            size_t preferred = 0;
            std::string candidateName;
            std::string candidateUrl;
            const std::string preferredName = "KevqDMA_" + release.tag + ".zip";
            for (const auto& asset : *assets) {
                if (!asset.is_object()) continue;
                const auto name = asset.find("name");
                const auto download = asset.find("browser_download_url");
                const auto state = asset.find("state");
                if (name == asset.end() || !name->is_string() || download == asset.end() ||
                    !download->is_string() || state == asset.end() || *state != "uploaded") continue;
                const auto& archiveName = name->get_ref<const std::string&>();
                const auto& archiveUrl = download->get_ref<const std::string&>();
                if (!IsArchiveName(archiveName) || !IsReleaseArchiveUrl(archiveUrl, release.tag) ||
                    !archiveUrl.ends_with("/" + archiveName)) continue;
                ++candidates;
                candidateName = archiveName;
                candidateUrl = archiveUrl;
                if (archiveName == preferredName) {
                    ++preferred;
                    release.archiveName = archiveName;
                    release.archiveUrl = archiveUrl;
                }
            }
            if (preferred == 0 && candidates == 1) {
                release.archiveName = std::move(candidateName);
                release.archiveUrl = std::move(candidateUrl);
            } else if (preferred != 1) {
                release.archiveName.clear();
                release.archiveUrl.clear();
            }
        }
        return release;
    }

    inline uint64_t ParseUnsigned(std::string_view value) noexcept
    {
        if (value.empty()) return 0;
        uint64_t parsed = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
        return result.ec == std::errc{} && result.ptr == value.data() + value.size() ? parsed : 0;
    }

    inline uint64_t RateLimitDelayMs(std::string_view retryAfter, std::string_view resetAt,
        uint64_t unixSeconds) noexcept
    {
        const uint64_t retry = ParseUnsigned(retryAfter);
        const uint64_t reset = ParseUnsigned(resetAt);
        uint64_t seconds = retry > 0 ? retry : (reset > unixSeconds ? reset - unixSeconds : 3600);
        if (reset > unixSeconds && reset - unixSeconds > seconds) seconds = reset - unixSeconds;
        return std::clamp<uint64_t>(seconds, 60, 7 * 24 * 3600) * 1000;
    }
}
