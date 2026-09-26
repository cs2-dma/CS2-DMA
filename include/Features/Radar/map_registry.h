#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <limits>

namespace radar {

struct MapDefinition
{
    std::string name;
    std::string displayName;
    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
    double originX = 0.0;
    double originY = 0.0;
    double scale = 1.0;
    bool dynamic = false;
    std::string radarImage;
    std::string backgroundImage;
    std::string parent;
    std::string section;
    double altitudeMin = 0.0;
    double altitudeMax = 0.0;
};

const std::vector<MapDefinition>& GetMapDefinitions();
std::string NormalizeMapName(std::string_view name);
const MapDefinition* FindMapByName(std::string_view name);
const MapDefinition* FindMapForPosition(std::string_view name, double worldX, double worldY,
    double worldZ = std::numeric_limits<double>::quiet_NaN(), std::string_view previousName = {});
const MapDefinition* ResolveMapByBounds(
    double minX,
    double minY,
    double maxX,
    double maxY,
    bool allowLooseMatch = true);
std::string BuildMapKeyFromBounds(double minX, double minY, double maxX, double maxY);
std::string BuildMapsJson();

}
