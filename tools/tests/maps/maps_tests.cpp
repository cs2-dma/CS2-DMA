#include <filesystem>
#include <iostream>
#include "../../../src/Features/Radar/map_registry.cpp"

std::filesystem::path app::paths::ResolveWebRadarAssetPath(const std::filesystem::path& path)
{
    return std::filesystem::current_path() / "src/Features/WebRadar/Assets" / path;
}
bool webradar::FindEmbeddedAsset(const std::string&, EmbeddedAsset*) { return false; }

int main()
{
    int failures = 0;
    const auto check = [&](bool value) { if (!value) ++failures; };
    const auto* root = radar::FindMapByName("rush_001");
    check(root && root->originX == -11240 && root->originY == 9944);
    check(root && std::fabs(root->scale-18.910156) < 0.000001);
    int rooms = 0;
    for (const auto& map : radar::GetMapDefinitions()) {
        if (map.dynamic) continue;
        check(std::fabs((map.maxX-map.originX)/map.scale-1024) < 0.01);
        check(std::fabs((map.originY-map.minY)/map.scale-1024) < 0.01);
        check(std::filesystem::exists(std::filesystem::current_path() /
            ("src/Features/WebRadar/Assets"+map.radarImage)));
        if (map.parent != "rush_001") continue;
        ++rooms;
        const auto* selected = radar::FindMapForPosition("rush_001",(map.minX+map.maxX)/2,(map.minY+map.maxY)/2);
        check(selected && selected->name == map.name);
    }
    check(rooms == 20);
    check(radar::FindMapForPosition("rush_001",1e20,1e20) == root);
    const auto* aimBotz = radar::FindMapByName("workshop/3070244462/aim_botz.vpk");
    check(aimBotz && aimBotz->dynamic);
    check(radar::FindMapByName("aim_custom")->dynamic);
    for (const auto* name : {"cs_shelter", "de_boulder", "de_debris", "de_eldorado", "de_fachwerk", "de_poseidon"})
        check(radar::FindMapByName(name) != nullptr);
    for (const auto& map : radar::GetMapDefinitions()) {
        if (map.section.empty() || map.parent.empty()) continue;
        const auto* base = radar::FindMapByName(map.parent);
        check(base && !base->section.empty());
        if (!base) continue;
        const double z = (map.altitudeMin + map.altitudeMax) / 2;
        check(radar::FindMapForPosition(map.parent, 0, 0, z) == &map);
        check(radar::FindMapForPosition(map.parent, 0, 0) == base);
        check(radar::FindMapForPosition(map.name, 0, 0, base->altitudeMin + 32) == &map);
        check(radar::FindMapForPosition(map.parent, 0, 0, map.altitudeMax - 0.01) == &map);
        check(radar::FindMapForPosition(map.parent, 0, 0, map.altitudeMax) == base);
        check(radar::FindMapForPosition(map.parent, 0, 0, map.altitudeMax + 8, map.name) == &map);
        check(radar::FindMapForPosition(map.parent, 0, 0, map.altitudeMax + 16, map.name) == base);
        check(radar::FindMapForPosition(map.parent, 0, 0, map.altitudeMax - 8, base->name) == base);
        check(radar::FindMapForPosition(map.parent, 0, 0, map.altitudeMax - 17, base->name) == &map);
        check(radar::FindMapForPosition(map.parent, 0, 0, z, "de_mirage") == &map);
        check(base->originX == map.originX && base->originY == map.originY && base->scale == map.scale);
    }
    const auto* mirage = radar::FindMapByName("de_mirage");
    const auto* vertigo = radar::FindMapByName("de_vertigo");
    check(mirage && mirage->originX == -3230 && mirage->originY == 1713 && mirage->scale == 5);
    check(vertigo && vertigo->originX == -3168 && vertigo->originY == 1762 && vertigo->scale == 4);
    std::vector<radar::MapDefinition> parsed;
    check(!AppendMapsFromJson({{"maps",nlohmann::json::array({{{"name",nullptr}},{{"name","invalid"},{"display_name",3},{"dynamic","yes"}}})}},parsed));
    const auto manifest = nlohmann::json::parse(radar::BuildMapsJson());
    check(manifest["maps"].size() == radar::GetMapDefinitions().size());
    check(AppendMapsFromJson(manifest, parsed));
    check(parsed.size() == radar::GetMapDefinitions().size());
    for (size_t i = 0; i < parsed.size(); ++i) {
        const auto& source = radar::GetMapDefinitions()[i];
        check(parsed[i].section == source.section && parsed[i].altitudeMin == source.altitudeMin &&
            parsed[i].altitudeMax == source.altitudeMax && parsed[i].parent == source.parent);
    }
    auto invalidFloor = manifest["maps"][0];
    invalidFloor["section"] = "lower";
    for (const auto& altitude : {nlohmann::json{{"min",1},{"max",0}}, nlohmann::json{{"min","bad"},{"max",1}}, nlohmann::json(nullptr)}) {
        invalidFloor["altitude"] = altitude;
        std::vector<radar::MapDefinition> invalid;
        check(!AppendMapsFromJson({{"maps",nlohmann::json::array({invalidFloor})}},invalid));
    }
    std::cout << "Map registry: " << rooms << " Rush zones; " << failures << " failures\n";
    return failures ? 1 : 0;
}
