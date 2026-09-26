#include "Features/Radar/map_registry.h"

#include "Features/WebRadar/embedded_assets.h"
#include "app/Config/project_paths.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <utility>

#include <json/json.hpp>

namespace
{

    bool AppendMapsFromJson(const nlohmann::json& root, std::vector<radar::MapDefinition>& maps)
    {
        if (root.is_discarded() || !root.contains("maps") || !root["maps"].is_array())
            return false;

        for (const auto& item : root["maps"]) {
            if (!item.is_object() || !item.contains("name") || !item["name"].is_string())
                continue;

            radar::MapDefinition map = {};
            map.name = item["name"].get<std::string>();
            map.displayName = item.contains("display_name") && item["display_name"].is_string()
                ? item["display_name"].get<std::string>() : map.name;
            map.dynamic = item.contains("dynamic") && item["dynamic"].is_boolean() && item["dynamic"].get<bool>();
            if (item.contains("parent") && item["parent"].is_string()) map.parent = item["parent"].get<std::string>();
            if (item.contains("section")) {
                if (!item["section"].is_string() || !item.contains("altitude") || !item["altitude"].is_object())
                    continue;
                const auto& altitude = item["altitude"];
                if (!altitude.contains("min") || !altitude["min"].is_number() ||
                    !altitude.contains("max") || !altitude["max"].is_number())
                    continue;
                map.section = item["section"].get<std::string>();
                map.altitudeMin = altitude["min"].get<double>();
                map.altitudeMax = altitude["max"].get<double>();
                if (map.section.empty() || !std::isfinite(map.altitudeMin) || !std::isfinite(map.altitudeMax) ||
                    map.altitudeMin >= map.altitudeMax)
                    continue;
            }

            if (item.contains("origin") && item["origin"].is_object()) {
                const auto& origin = item["origin"];
                if (origin.contains("x") && origin["x"].is_number())
                    map.originX = origin["x"].get<double>();
                if (origin.contains("y") && origin["y"].is_number())
                    map.originY = origin["y"].get<double>();
            }

            if (item.contains("bounds") && item["bounds"].is_object()) {
                const auto& bounds = item["bounds"];
                if (bounds.contains("min_x") && bounds["min_x"].is_number())
                    map.minX = bounds["min_x"].get<double>();
                if (bounds.contains("max_x") && bounds["max_x"].is_number())
                    map.maxX = bounds["max_x"].get<double>();
                if (bounds.contains("min_y") && bounds["min_y"].is_number())
                    map.minY = bounds["min_y"].get<double>();
                if (bounds.contains("max_y") && bounds["max_y"].is_number())
                    map.maxY = bounds["max_y"].get<double>();
            }

            if (item.contains("scale") && item["scale"].is_number())
                map.scale = item["scale"].get<double>();

            if (item.contains("images") && item["images"].is_object()) {
                const auto& images = item["images"];
                if (images.contains("radar") && images["radar"].is_string())
                    map.radarImage = images["radar"].get<std::string>();
                if (images.contains("background") && images["background"].is_string())
                    map.backgroundImage = images["background"].get<std::string>();
            }

            if (map.name.empty() ||
                !std::isfinite(map.originX) ||
                !std::isfinite(map.originY) ||
                !std::isfinite(map.scale) ||
                map.scale <= 0.001) {
                continue;
            }

            if (!map.dynamic) {
                const bool validBounds =
                    std::isfinite(map.minX) &&
                    std::isfinite(map.maxX) &&
                    std::isfinite(map.minY) &&
                    std::isfinite(map.maxY) &&
                    std::fabs(map.maxX - map.minX) >= 128.0 &&
                    std::fabs(map.maxY - map.minY) >= 128.0;
                if (!validBounds)
                    continue;
            }

            if (map.radarImage.empty())
                map.radarImage = std::format("/data/{}/radar.webp", map.name);
            if (map.backgroundImage.empty())
                map.backgroundImage = map.radarImage;

            maps.push_back(std::move(map));
        }

        return !maps.empty();
    }

    std::vector<radar::MapDefinition> LoadMapsFromEmbeddedAsset()
    {
        webradar::EmbeddedAsset asset{};
        if (!webradar::FindEmbeddedAsset("/maps.json", &asset))
            return {};

        const auto* first = static_cast<const char*>(asset.data);
        const auto* last = first + asset.size;
        nlohmann::json root = nlohmann::json::parse(first, last, nullptr, false);
        std::vector<radar::MapDefinition> maps;
        AppendMapsFromJson(root, maps);
        return maps;
    }

    std::vector<radar::MapDefinition> LoadMapsFromAssetFile()
    {
        const auto rawPath = app::paths::ResolveWebRadarAssetPath("maps.json");
        if (rawPath.empty())
            return {};

        std::error_code ec;
        const auto path = std::filesystem::weakly_canonical(rawPath, ec);
        const auto& candidate = ec ? rawPath : path;
        if (!std::filesystem::exists(candidate, ec))
            return {};

        std::ifstream file(candidate, std::ios::in | std::ios::binary);
        if (!file.is_open())
            return {};

        nlohmann::json root = nlohmann::json::parse(file, nullptr, false);
        std::vector<radar::MapDefinition> maps;
        AppendMapsFromJson(root, maps);
        return maps;
    }

    std::vector<radar::MapDefinition> LoadMapDefinitions()
    {
        auto maps = LoadMapsFromAssetFile();
        if (!maps.empty())
            return maps;

        maps = LoadMapsFromEmbeddedAsset();
        if (!maps.empty())
            return maps;

        return {};
    }

    bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs)
    {
        if (lhs.size() != rhs.size())
            return false;

        for (size_t i = 0; i < lhs.size(); ++i) {
            const auto a = static_cast<unsigned char>(lhs[i]);
            const auto b = static_cast<unsigned char>(rhs[i]);
            if (std::tolower(a) != std::tolower(b))
                return false;
        }

        return true;
    }

    std::string NormalizeMapCandidate(std::string_view rawName)
    {
        size_t begin = 0;
        size_t end = rawName.size();
        while (begin < end && std::isspace(static_cast<unsigned char>(rawName[begin])) != 0)
            ++begin;
        while (end > begin && std::isspace(static_cast<unsigned char>(rawName[end - 1])) != 0)
            --end;
        if (begin >= end)
            return {};

        std::string text(rawName.substr(begin, end - begin));
        std::replace(text.begin(), text.end(), '\\', '/');

        const size_t slash = text.find_last_of('/');
        if (slash != std::string::npos)
            text.erase(0, slash + 1);

        const size_t dot = text.find_last_of('.');
        if (dot != std::string::npos)
            text.erase(dot);

        std::string normalized;
        normalized.reserve(text.size());
        for (const unsigned char ch : text) {
            if (std::isalnum(ch) != 0 || ch == '_')
                normalized.push_back(static_cast<char>(std::tolower(ch)));
        }

        if (normalized.empty() || normalized.size() >= 64)
            return {};

        return normalized;
    }

    int QuantizeMapCoord(double value)
    {
        constexpr double kStep = 16.0;
        return static_cast<int>(std::lround(value / kStep) * kStep);
    }
}

namespace radar {

const std::vector<MapDefinition>& GetMapDefinitions()
{
    static const std::vector<MapDefinition> maps = LoadMapDefinitions();
    return maps;
}

std::string NormalizeMapName(std::string_view name)
{
    const std::string normalized = NormalizeMapCandidate(name);
    if (normalized.empty())
        return {};

    for (const auto& map : GetMapDefinitions()) {
        if (EqualsIgnoreCase(map.name, normalized))
            return map.name;
    }

    return {};
}

const MapDefinition* FindMapByName(std::string_view name)
{
    const std::string normalized = NormalizeMapCandidate(name);
    if (normalized.empty())
        return nullptr;

    for (const auto& map : GetMapDefinitions()) {
        if (EqualsIgnoreCase(map.name, normalized))
            return &map;
    }

    return nullptr;
}

const MapDefinition* FindMapForPosition(std::string_view name, double worldX, double worldY,
    double worldZ, std::string_view previousName)
{
    const auto* root = FindMapByName(name);
    if (!root || !std::isfinite(worldX) || !std::isfinite(worldY)) return root;
    if (!root->section.empty()) {
        if (!root->parent.empty() || !std::isfinite(worldZ)) return root;
        if (const auto* previous = FindMapByName(previousName);
            previous && !previous->section.empty() && (previous == root || previous->parent == root->name) &&
            worldZ >= previous->altitudeMin - 16.0 && worldZ < previous->altitudeMax + 16.0)
            return previous;
        if (worldZ >= root->altitudeMin && worldZ < root->altitudeMax) return root;
        for (const auto& map : GetMapDefinitions()) {
            if (map.parent == root->name && !map.section.empty() &&
                worldZ >= map.altitudeMin && worldZ < map.altitudeMax)
                return &map;
        }
        return root;
    }
    const MapDefinition* selected = root;
    double bestScore = 10.0;
    for (const auto& map : GetMapDefinitions()) {
        if (map.parent != root->name || map.dynamic || !map.section.empty()) continue;
        const double x = (worldX - map.originX) / (map.scale * 1024.0);
        const double y = (map.originY - worldY) / (map.scale * 1024.0);
        if (x < 0 || x > 1 || y < 0 || y > 1) continue;
        const double score = (x - 0.5) * (x - 0.5) + (y - 0.5) * (y - 0.5);
        if (score < bestScore) { bestScore = score; selected = &map; }
    }
    return selected;
}

const MapDefinition* ResolveMapByBounds(
    double minX,
    double minY,
    double maxX,
    double maxY,
    bool allowLooseMatch)
{
    if (!std::isfinite(minX) || !std::isfinite(maxX) ||
        !std::isfinite(minY) || !std::isfinite(maxY)) {
        return nullptr;
    }

    const double runtimeSpanX = std::fabs(maxX - minX);
    const double runtimeSpanY = std::fabs(maxY - minY);
    const double runtimeCenterX = (minX + maxX) * 0.5;
    const double runtimeCenterY = (minY + maxY) * 0.5;

    const MapDefinition* best = nullptr;
    const MapDefinition* second = nullptr;
    double bestLegacyScore = 1e18;
    double bestSpanScore = 1e18;
    double bestCenterScore = 1e18;
    double secondLegacyScore = 1e18;
    double secondSpanScore = 1e18;
    double secondCenterScore = 1e18;

    for (const auto& map : GetMapDefinitions()) {
        if (map.dynamic || !map.parent.empty())
            continue;

        const double mapSpanX = std::fabs(map.maxX - map.minX);
        const double mapSpanY = std::fabs(map.maxY - map.minY);
        const double mapCenterX = (map.minX + map.maxX) * 0.5;
        const double mapCenterY = (map.minY + map.maxY) * 0.5;

        const double legacyScore =
            std::fabs(minX - map.minX) +
            std::fabs(maxX - map.maxX) +
            std::fabs(minY - map.minY) +
            std::fabs(maxY - map.maxY);
        const double spanScore =
            std::fabs(runtimeSpanX - mapSpanX) +
            std::fabs(runtimeSpanY - mapSpanY);
        const double centerScore =
            std::fabs(runtimeCenterX - mapCenterX) +
            std::fabs(runtimeCenterY - mapCenterY);

        const bool better =
            legacyScore < bestLegacyScore - 0.1 ||
            (std::fabs(legacyScore - bestLegacyScore) <= 0.1 &&
             (spanScore < bestSpanScore - 0.1 ||
              (std::fabs(spanScore - bestSpanScore) <= 0.1 &&
               centerScore < bestCenterScore)));
        if (!better) {
            const bool secondBetter =
                legacyScore < secondLegacyScore - 0.1 ||
                (std::fabs(legacyScore - secondLegacyScore) <= 0.1 &&
                 (spanScore < secondSpanScore - 0.1 ||
                  (std::fabs(spanScore - secondSpanScore) <= 0.1 &&
                   centerScore < secondCenterScore)));
            if (secondBetter) {
                second = &map;
                secondLegacyScore = legacyScore;
                secondSpanScore = spanScore;
                secondCenterScore = centerScore;
            }
            continue;
        }

        second = best;
        secondLegacyScore = bestLegacyScore;
        secondSpanScore = bestSpanScore;
        secondCenterScore = bestCenterScore;
        best = &map;
        bestLegacyScore = legacyScore;
        bestSpanScore = spanScore;
        bestCenterScore = centerScore;
    }

    if (!best)
        return nullptr;

    const bool exactBoundsMatch = bestLegacyScore <= 320.0;
    const bool closeSpanMatch =
        allowLooseMatch &&
        bestLegacyScore <= 900.0 &&
        bestSpanScore <= 128.0 &&
        bestCenterScore <= 384.0;
    const bool ambiguousBounds =
        second != nullptr &&
        secondLegacyScore <= 900.0 &&
        std::fabs(secondLegacyScore - bestLegacyScore) <= 64.0 &&
        secondSpanScore <= 192.0 &&
        secondCenterScore <= 512.0;
    if (ambiguousBounds)
        return nullptr;

    return (exactBoundsMatch || closeSpanMatch) ? best : nullptr;
}

std::string BuildMapKeyFromBounds(double minX, double minY, double maxX, double maxY)
{
    if (const MapDefinition* map = ResolveMapByBounds(minX, minY, maxX, maxY, true))
        return map->name;

    return std::format(
        "dynamic_{}_{}__{}_{}",
        QuantizeMapCoord(minX),
        QuantizeMapCoord(minY),
        QuantizeMapCoord(maxX),
        QuantizeMapCoord(maxY));
}

std::string BuildMapsJson()
{
    nlohmann::json root;
    root["maps"] = nlohmann::json::array();
    for (const auto& map : GetMapDefinitions()) {
        root["maps"].push_back({
            {"name", map.name},
            {"display_name", map.displayName.empty() ? map.name : map.displayName},
            {"origin", { {"x", map.originX}, {"y", map.originY} }},
            {"scale", map.scale},
            {"dynamic", map.dynamic},
            {"parent", map.parent},
            {"bounds", {
                {"min_x", map.minX},
                {"max_x", map.maxX},
                {"min_y", map.minY},
                {"max_y", map.maxY}
            }},
            {"images", {
                {"radar", map.radarImage},
                {"background", map.backgroundImage}
            }}
        });
        if (!map.section.empty()) {
            root["maps"].back()["section"] = map.section;
            root["maps"].back()["altitude"] = {{"min", map.altitudeMin}, {"max", map.altitudeMax}};
        }
    }

    return root.dump();
}

}
