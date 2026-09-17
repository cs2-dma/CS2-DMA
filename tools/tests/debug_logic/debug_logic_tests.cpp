#include <WinSock2.h>
#include <WS2tcpip.h>

#include "app/Platform/file_replace.h"
#include "app/Config/config_parse_utils.h"
#include "app/Localization/localization_catalog.h"
#include "app/Input/input_device_policy.h"
#include "app/Input/input_device.h"
#include "app/Config/profile_name_utils.h"
#include "Game/Offsets/runtime_offsets_parse_utils.h"
#include "Game/Offsets/runtime_resolver_policy.h"
#include "Features/ESP/DataReader/bone_read_policy.h"
#include "Features/ESP/DataReader/deferred_lane_policy.h"
#include "Features/ESP/DataReader/bone_plausibility.h"
#include "Features/ESP/DataReader/base_recovery_policy.h"
#include "Features/ESP/DataReader/bomb_policy.h"
#include "Features/ESP/DataReader/player_commit_policy.h"
#include "Features/ESP/DataReader/player_core_policy.h"
#include "Features/ESP/DataReader/player_flag_policy.h"
#include "Features/ESP/DataReader/player_hierarchy_policy.h"
#include "Features/ESP/DataReader/player_repair_policy.h"
#include "Features/ESP/DataReader/player_slot_policy.h"
#include "Features/ESP/DataReader/population_watchdog_policy.h"
#include "Features/ESP/DataReader/scene_transition_policy.h"
#include "Features/ESP/DataReader/visibility_policy.h"
#include "Features/ESP/DataReader/world_domain_policy.h"
#include "Features/ESP/DataReader/world_marker_policy.h"
#include "Features/ESP/DataReader/zero_population_policy.h"
#include "Features/ESP/Recovery/dma_cache_profile.h"
#include "Features/ESP/Recovery/dma_recovery_policy.h"
#include "Features/ESP/Recovery/dma_refresh_policy.h"
#include "Features/ESP/Recovery/process_identity_policy.h"
#include "Features/ESP/Recovery/reset_policy.h"
#include "Features/ESP/Render/draw_policy.h"
#include "Features/ESP/Render/skeleton_gate.h"
#include "Features/ESP/Render/visual_style_policy.h"
#include "Features/ESP/Render/weapon_icon_atlas_data.generated.h"
#include "Features/ESP/State/snapshot_ring.h"
#include "Features/ESP/Worker/worker_policy.h"
#include "app/Core/memory_address.h"
#include "DMALibrary/Memory/ScatterReadTracker.h"
#include "Features/ESP/weapon_catalog.h"
#include "Features/Target/target_policy.h"
#include "Features/Target/target_ballistics.h"
#include "Features/WebRadar/runtime_utils.h"

#include <json/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    int g_failedChecks = 0;

    void Check(bool condition, const char* expression, const char* file, int line)
    {
        if (condition)
            return;

        ++g_failedChecks;
        std::cerr << file << ":" << line << ": CHECK failed: " << expression << '\n';
    }

#define CHECK(expr) Check((expr), #expr, __FILE__, __LINE__)

    void TestTrim()
    {
        using runtime_offsets::parse_utils::Trim;
        CHECK(Trim("  abc  ") == "abc");
        CHECK(Trim("\t\nabc\r\n") == "abc");
        CHECK(Trim("   ").empty());
    }

    void TestConfigParseUtils()
    {
        using app::config_parse::ParseBoolString;
        using app::config_parse::ParseFloatString;
        using app::config_parse::ParseIntString;
        using app::config_parse::ToLower;
        using app::config_parse::Trim;

        CHECK(ToLower("AbC123") == "abc123");
        CHECK(Trim(" \tvalue\r\n") == "value");
        CHECK(ParseBoolString(" ON ", false));
        CHECK(ParseBoolString("yes", false));
        CHECK(!ParseBoolString(" off ", true));
        CHECK(!ParseBoolString("0", true));
        CHECK(ParseBoolString("unknown", true));
        CHECK(!ParseBoolString("unknown", false));
        CHECK(ParseIntString(" 42 ", -1) == 42);
        CHECK(ParseIntString("42px", -1) == 42);
        CHECK(ParseIntString("+42", -1) == 42);
        CHECK(ParseIntString("999999999999999999999", -1) == -1);
        CHECK(ParseIntString("bad", -1) == -1);
        CHECK(ParseFloatString(" 1.5 ", -1.0f) == 1.5f);
        CHECK(ParseFloatString("1.5f", -1.0f) == 1.5f);
        CHECK(ParseFloatString("+1.5", -1.0f) == 1.5f);
        CHECK(ParseFloatString("nan", -1.0f) == -1.0f);
        CHECK(ParseFloatString("bad", -1.0f) == -1.0f);
    }

    void TestWeaponCatalog()
    {
        using namespace esp::weapons;

        CHECK(std::string(WeaponNameFromItemId(7)) == "AK-47");
        CHECK(std::string(WeaponVisualKeyFromItemId(7)) == "ak47");
        CHECK(IsPrimaryWeaponItemId(7));
        CHECK(WeaponMaxClipFromItemId(7) == 30);
        CHECK(IsKnifeItemId(42));
        CHECK(IsKnifeItemId(59));
        CHECK(IsKnifeItemId(41));
        CHECK(IsKnifeItemId(526));
        CHECK(std::string(WeaponVisualKeyFromItemId(516)) == "knife_push");
        CHECK(std::string(WeaponVisualKeyFromItemId(525)) == "knife_skeleton");
        CHECK(std::string(WeaponVisualKeyFromItemId(526)) == "knife_kukri");
        CHECK(WeaponItemIdFromDesignerName("weapon_ak47") == 7);
        CHECK(WeaponItemIdFromDesignerName("WEAPON_M4A1_SILENCER") == 60);
        CHECK(WeaponItemIdFromDesignerName("client::weapon_awp") == 9);
        CHECK(WeaponItemIdFromDesignerName("smokegrenade_projectile") == 0);
        CHECK(DroppedWeaponCategoryFromItemId(7) == DroppedWeaponCategory::Rifles);
        CHECK(DroppedWeaponCategoryFromItemId(9) == DroppedWeaponCategory::Snipers);
        CHECK(DroppedWeaponCategoryFromItemId(4) == DroppedWeaponCategory::Pistols);
        CHECK(IsDroppedWeaponItemId(57));
        CHECK(!IsDroppedWeaponItemId(45));
        CHECK(!IsDroppedWeaponItemId(49));
        for (const auto& atlasRegion : resources::weapon_icons::kRegions) {
            CHECK(WeaponNameFromItemId(atlasRegion.itemId) != nullptr);
            CHECK(WeaponVisualKeyFromItemId(atlasRegion.itemId) != nullptr);
            WeaponIconRegion resolvedRegion = {};
            CHECK(WeaponIconRegionFromItemId(atlasRegion.itemId, &resolvedRegion));
            CHECK(resolvedRegion.x == atlasRegion.x);
            CHECK(resolvedRegion.y == atlasRegion.y);
            CHECK(resolvedRegion.width == atlasRegion.width);
            CHECK(resolvedRegion.height == atlasRegion.height);
        }
        CHECK(WeaponNameFromItemId(0) == nullptr);
        CHECK(WeaponVisualKeyFromItemId(0) == nullptr);
        WeaponIconRegion missingRegion = {};
        CHECK(!WeaponIconRegionFromItemId(0, &missingRegion));
        CHECK(!WeaponIconRegionFromItemId(7, nullptr));
    }

    void TestMemoryValidation()
    {
        using namespace app::memory_address;

        CHECK(!IsCanonicalUserPointer(0));
        CHECK(!IsCanonicalUserPointer(kMinimumUserAddress - 1u));
        CHECK(IsCanonicalUserPointer(kMinimumUserAddress));
        CHECK(IsLikelyGamePointer(kMinimumUserAddress));
        CHECK(!IsLikelyGamePointer(kMinimumUserAddress + 1u));
        CHECK(IsLikelyGamePointer(kMaximumUserAddress - alignof(uintptr_t)));
        CHECK(!IsCanonicalUserPointer(kMaximumUserAddress));
        CHECK(SanitizeGamePointer(kMinimumUserAddress) == kMinimumUserAddress);
        CHECK(SanitizeGamePointer(kMinimumUserAddress + 1u) == 0);
        CHECK(IsCompleteGamePointerSample(0, sizeof(uintptr_t)));
        CHECK(IsCompleteGamePointerSample(kMinimumUserAddress, sizeof(uintptr_t)));
        CHECK(!IsCompleteGamePointerSample(kMinimumUserAddress + 1u, sizeof(uintptr_t)));
        CHECK(!IsCompleteGamePointerSample(kMaximumUserAddress, sizeof(uintptr_t)));
        for (std::size_t bytes = 0; bytes < sizeof(uintptr_t); ++bytes) {
            CHECK(!IsCompleteGamePointerSample(0, bytes));
            CHECK(!IsCompleteGamePointerSample(kMinimumUserAddress, bytes));
        }
        CHECK(!IsCompleteGamePointerSample(kMinimumUserAddress, sizeof(uintptr_t) + 1));
    }

    void TestRuntimeResolverPolicy()
    {
        using namespace runtime_offsets::resolver_policy;

        CHECK(AddRvaOffset(std::ptrdiff_t{0x1000}, 8).value_or(0) == 0x1008);
        CHECK(!AddRvaOffset(std::nullopt, 8).has_value());
        CHECK(!AddRvaOffset(std::ptrdiff_t{0x1000}, -1).has_value());
        CHECK(!AddRvaOffset(
            (std::numeric_limits<std::ptrdiff_t>::max)(),
            8).has_value());

        const auto pattern = CompilePattern("48 8B ?? 0F");
        CHECK(pattern.size() == 4);
        CHECK(!pattern[0].wildcard && pattern[0].value == 0x48);
        CHECK(pattern[2].wildcard);
        CHECK(CompilePattern("48 xyz").empty());
        CHECK(CompilePattern("4").empty());

        std::array<std::uint8_t, 0x18> allocatedNodeBytes = {};
        const std::uintptr_t allocatedNext = 0x10000u;
        const std::uintptr_t allocatedData = 0x20000u;
        std::memcpy(allocatedNodeBytes.data() + 0x08, &allocatedNext, sizeof(allocatedNext));
        std::memcpy(allocatedNodeBytes.data() + 0x10, &allocatedData, sizeof(allocatedData));
        const auto allocatedNode = DecodeSchemaHashNode(allocatedNodeBytes, 0x08);
        CHECK(allocatedNode.has_value());
        CHECK(allocatedNode->next == allocatedNext);
        CHECK(allocatedNode->data == allocatedData);

        std::array<std::uint8_t, 0x18> freeNodeBytes = {};
        const std::uintptr_t freeNext = 0x30000u;
        std::memcpy(freeNodeBytes.data(), &freeNext, sizeof(freeNext));
        std::memcpy(freeNodeBytes.data() + 0x10, &allocatedData, sizeof(allocatedData));
        const auto freeNode = DecodeSchemaHashNode(freeNodeBytes, 0x00);
        CHECK(freeNode.has_value());
        CHECK(freeNode->next == freeNext);
        CHECK(!DecodeSchemaHashNode(freeNodeBytes, 0x11).has_value());

        const std::array<std::uint8_t, 8> uniqueBytes = {
            0x90, 0x48, 0x8B, 0x35, 0x0F, 0x90, 0x90, 0x90
        };
        const auto unique = FindUniquePattern(uniqueBytes, pattern);
        CHECK(unique.has_value());
        CHECK(unique.value_or(0) == 1);

        const std::array<std::uint8_t, 9> duplicateBytes = {
            0x48, 0x8B, 0x01, 0x0F, 0x90,
            0x48, 0x8B, 0x02, 0x0F
        };
        const auto duplicateMatches =
            FindPatternMatches(duplicateBytes, pattern);
        CHECK(duplicateMatches.matchCount == 2);
        CHECK(duplicateMatches.firstOffset.value_or(99) == 0);
        CHECK(!FindUniquePattern(duplicateBytes, pattern).has_value());
        CHECK(!FindUniquePattern(
            std::span<const std::uint8_t>{},
            pattern).has_value());

        std::array<std::uint8_t, 16> relativeBytes = {};
        const std::int32_t forward = 0x20;
        std::memcpy(relativeBytes.data() + 5, &forward, sizeof(forward));
        const auto forwardRva = ResolveRelativeRva(
            0x1000,
            3,
            relativeBytes,
            2,
            0x2000);
        CHECK(forwardRva.has_value());
        CHECK(forwardRva.value_or(0) == 0x1027);

        const std::int32_t backward = -0x20;
        std::memcpy(relativeBytes.data() + 5, &backward, sizeof(backward));
        const auto backwardRva = ResolveRelativeRva(
            0x1000,
            3,
            relativeBytes,
            2,
            0x2000);
        CHECK(backwardRva.has_value());
        CHECK(backwardRva.value_or(0) == 0x0FE7);
        CHECK(!ResolveRelativeRva(
            0x1000,
            14,
            relativeBytes,
            2,
            0x2000).has_value());
        std::memcpy(relativeBytes.data() + 5, &forward, sizeof(forward));
        CHECK(!ResolveRelativeRva(
            0x1FF0,
            3,
            relativeBytes,
            2,
            0x2000).has_value());
    }

    void TestBase64()
    {
        using webradar::runtime::Base64Encode;

        const auto encode = [&](std::string_view value) {
            return Base64Encode(
                reinterpret_cast<const unsigned char*>(value.data()),
                value.size());
        };
        CHECK(encode("").empty());
        CHECK(encode("f") == "Zg==");
        CHECK(encode("fo") == "Zm8=");
        CHECK(encode("foo") == "Zm9v");
        CHECK(encode("foobar") == "Zm9vYmFy");
        CHECK(Base64Encode(nullptr, 1).empty());
    }

    void TestLocalizationCatalog()
    {
        using app::localization::Catalog;
        using app::localization::FindTranslation;
        using app::localization::HasCompatibleFormatArguments;
        using app::localization::ParseCatalogJson;

        Catalog english;
        Catalog chinese;
        CHECK(ParseCatalogJson(
            R"({"translations":{"Status":"Status","Value %d":"Value %d"}})",
            &english));
        CHECK(ParseCatalogJson(
            R"({"translations":{"Status":"\u72b6\u6001","Value %d":"\u6570\u503c %d"}})",
            &chinese));
        CHECK(std::string(FindTranslation(&chinese, &english, "Status")) ==
              "\xE7\x8A\xB6\xE6\x80\x81");
        CHECK(std::string(FindTranslation(&chinese, &english, "Missing")) ==
              "Missing");
        CHECK(HasCompatibleFormatArguments("Value %llu %.1f", "X %llu %.1f"));
        CHECK(!HasCompatibleFormatArguments("Value %llu", "X %u"));
        CHECK(HasCompatibleFormatArguments("Value {}", "X {}"));
        CHECK(!HasCompatibleFormatArguments("Value {}", "X"));

        Catalog invalid;
        CHECK(!ParseCatalogJson(
            R"({"translations":{"Value %llu":"\u6570\u503c %u"}})",
            &invalid));
        CHECK(!ParseCatalogJson("{}", &invalid));
        CHECK(!ParseCatalogJson("{", &invalid));
    }

    void TestProfileNameUtils()
    {
        using app::profile_name::IsReservedProfileName;
        using app::profile_name::IsUsableProfileName;
        using app::profile_name::SanitizeProfileName;

        CHECK(SanitizeProfileName("Default_01-test") == "Default_01-test");
        CHECK(SanitizeProfileName("bad/name with spaces") == "bad_name_with_spaces");
        CHECK(SanitizeProfileName("") == "KevqDefault");
        CHECK(SanitizeProfileName("...") == "___");

        const std::string longName(80, 'a');
        CHECK(SanitizeProfileName(longName).size() == app::profile_name::kMaxProfileNameLength);

        CHECK(IsReservedProfileName("offsets"));
        CHECK(IsReservedProfileName("WebRadarConfig"));
        CHECK(IsReservedProfileName("profile.web"));
        CHECK(!IsReservedProfileName("legit_profile"));
        CHECK(IsUsableProfileName("legit_profile"));
        CHECK(!IsUsableProfileName("offsets"));
        CHECK(!IsUsableProfileName("WebRadarConfig"));
    }

    void TestOffsetParsing()
    {
        CHECK(runtime_offsets::parse_utils::ToAddressHex(
            static_cast<std::uintptr_t>(0xFEDCBA9876543210ull)) ==
            "0xFEDCBA9876543210");

        using runtime_offsets::parse_utils::ToHex;
        using runtime_offsets::parse_utils::TryParseOffset;

        std::ptrdiff_t value = 0;
        CHECK(TryParseOffset("42", value) && value == 42);
        CHECK(TryParseOffset("  1337  ", value) && value == 1337);
        CHECK(TryParseOffset("0x2A", value) && value == 42);
        CHECK(TryParseOffset("0X2a", value) && value == 42);
        CHECK(!TryParseOffset("", value));
        CHECK(!TryParseOffset("0x", value));
        CHECK(!TryParseOffset("123abc", value));
        CHECK(!TryParseOffset("0x10zz", value));
        CHECK(ToHex(0x2A) == "0x2A");
    }

    std::filesystem::path FindProjectRoot()
    {
        auto findFrom = [](std::filesystem::path candidate) {
            for (int depth = 0; depth < 8 && !candidate.empty(); ++depth) {
                if (std::filesystem::is_regular_file(candidate / "KevqDMA.vcxproj"))
                    return candidate;
                const auto parent = candidate.parent_path();
                if (parent == candidate)
                    break;
                candidate = parent;
            }
            return std::filesystem::path{};
        };

        if (const auto root = findFrom(std::filesystem::current_path()); !root.empty())
            return root;

        const std::filesystem::path sourcePath(__FILE__);
        return sourcePath.is_absolute()
            ? findFrom(sourcePath.parent_path())
            : std::filesystem::path{};
    }

    void TestWebRadarMapMetadata()
    {
        const std::filesystem::path projectRoot = FindProjectRoot();
        CHECK(!projectRoot.empty());
        if (projectRoot.empty())
            return;

        const std::filesystem::path dataRoot =
            projectRoot / "src/Features/WebRadar/Assets/data";
        CHECK(std::filesystem::is_directory(dataRoot));
        if (!std::filesystem::is_directory(dataRoot))
            return;

        using Transform = std::array<double, 3>;
        std::map<std::string, Transform> transforms;
        std::map<std::array<long long, 3>, std::vector<std::string>> duplicateGroups;

        for (const auto& entry : std::filesystem::directory_iterator(dataRoot)) {
            if (!entry.is_directory())
                continue;

            const std::filesystem::path metadataPath = entry.path() / "data.json";
            if (!std::filesystem::is_regular_file(metadataPath))
                continue;

            try {
                std::ifstream input(metadataPath, std::ios::binary);
                CHECK(input.is_open());
                if (!input.is_open())
                    continue;

                nlohmann::json metadata;
                input >> metadata;
                CHECK(metadata.contains("x") && metadata["x"].is_number());
                CHECK(metadata.contains("y") && metadata["y"].is_number());
                CHECK(metadata.contains("scale") && metadata["scale"].is_number());
                if (!metadata.contains("x") || !metadata["x"].is_number() ||
                    !metadata.contains("y") || !metadata["y"].is_number() ||
                    !metadata.contains("scale") || !metadata["scale"].is_number()) {
                    continue;
                }

                const Transform transform{
                    metadata["x"].get<double>(),
                    metadata["y"].get<double>(),
                    metadata["scale"].get<double>()
                };
                CHECK(std::isfinite(transform[0]));
                CHECK(std::isfinite(transform[1]));
                CHECK(std::isfinite(transform[2]));
                CHECK(transform[2] > 0.1 && transform[2] < 20.0);

                const std::string mapName = entry.path().filename().string();
                transforms[mapName] = transform;
                duplicateGroups[{
                    std::llround(transform[0] * 1000000.0),
                    std::llround(transform[1] * 1000000.0),
                    std::llround(transform[2] * 1000000.0)
                }].push_back(mapName);

                const std::filesystem::path radarPath = entry.path() / "radar.webp";
                std::error_code sizeError;
                const auto radarSize = std::filesystem::file_size(radarPath, sizeError);
                CHECK(!sizeError && radarSize > 0);
            } catch (...) {
                CHECK(false);
            }
        }

        const auto checkTransform = [&](const char* mapName, Transform expected) {
            const auto it = transforms.find(mapName);
            CHECK(it != transforms.end());
            if (it == transforms.end())
                return;
            for (size_t component = 0; component < expected.size(); ++component)
                CHECK(std::abs(it->second[component] - expected[component]) < 0.000001);
        };

        checkTransform("ar_baggage", {-1316.0, 1288.0, 2.539062});
        checkTransform("ar_shoots", {-1368.0, 1952.0, 2.6875});
        checkTransform("ar_shoots_night", {-1368.0, 1952.0, 2.6875});

        const std::set<std::set<std::string>> allowedAliases{
            {"ar_shoots", "ar_shoots_night"},
            {"de_ancient", "de_ancient_night"}
        };
        for (const auto& [key, maps] : duplicateGroups) {
            (void)key;
            if (maps.size() < 2)
                continue;
            CHECK(allowedAliases.contains(std::set<std::string>(maps.begin(), maps.end())));
        }
    }

    void WriteTextFile(const std::filesystem::path& path, const char* value)
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << value;
    }

    std::string ReadTextFile(const std::filesystem::path& path)
    {
        std::ifstream input(path, std::ios::binary);
        return std::string(
            std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>());
    }

    void TestReplaceFileWithTemp()
    {
        const auto uniqueName =
            "KevqDMA_debug_logic_tests_" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / uniqueName;
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        CHECK(!ec);

        const std::filesystem::path finalPath = dir / "user_state.json";
        std::filesystem::path tempPath = finalPath;
        tempPath += ".tmp";

        WriteTextFile(finalPath, "old");
        WriteTextFile(tempPath, "new");
        CHECK(app::platform::ReplaceFileWithTemp(tempPath, finalPath, ec));
        CHECK(!ec);
        CHECK(!std::filesystem::exists(tempPath));
        CHECK(ReadTextFile(finalPath) == "new");

        WriteTextFile(tempPath, "fresh");
        std::filesystem::remove(finalPath, ec);
        ec.clear();
        CHECK(app::platform::ReplaceFileWithTemp(tempPath, finalPath, ec));
        CHECK(!ec);
        CHECK(ReadTextFile(finalPath) == "fresh");
        CHECK(app::platform::RemoveFileIfExists(finalPath));
        CHECK(!std::filesystem::exists(finalPath));
        CHECK(!app::platform::RemoveFileIfExists(finalPath));
        CHECK(!app::platform::RemoveFileIfExists({}));

        std::filesystem::remove_all(dir, ec);
    }

    void TestPatchBuildParsing()
    {
        using runtime_offsets::parse_utils::ParsePatchBuildNumber;

        CHECK(ParsePatchBuildNumber("1.40.8.9") == 14089);
        CHECK(ParsePatchBuildNumber("client 2000420") == 2000420);
        CHECK(ParsePatchBuildNumber("no digits") == 0);
    }

    void TestCanonicalTimestampParsing()
    {
        using runtime_offsets::parse_utils::ParseDecimalPart;
        using runtime_offsets::parse_utils::ParseCanonicalUtcTimestamp;

        CHECK(ParseDecimalPart("20260624", 0, 4) == 2026);
        CHECK(ParseDecimalPart("20260624", 4, 2) == 6);
        CHECK(ParseDecimalPart("2026x624", 4, 2) == -1);
        CHECK(ParseDecimalPart("2026", 4, 1) == -1);
        CHECK(ParseDecimalPart("2026", 0, 0) == -1);
        CHECK(ParseCanonicalUtcTimestamp("20260624123456").has_value());
        CHECK(!ParseCanonicalUtcTimestamp("20260624").has_value());
        CHECK(!ParseCanonicalUtcTimestamp("20260230000000").has_value());
        CHECK(!ParseCanonicalUtcTimestamp("20260624243456").has_value());
        CHECK(!ParseCanonicalUtcTimestamp("20260624126000").has_value());
        CHECK(!ParseCanonicalUtcTimestamp("20260624123460").has_value());
    }

    void TestSteamTimestampParsing()
    {
        using runtime_offsets::parse_utils::ParseSteamVersionTimestamp;

        CHECK(ParseSteamVersionTimestamp("Jun 24 2026", "12:34:56").has_value());
        CHECK(!ParseSteamVersionTimestamp("Bad 24 2026", "12:34:56").has_value());
        CHECK(!ParseSteamVersionTimestamp("Jun 31 2026", "12:34:56").has_value());
        CHECK(!ParseSteamVersionTimestamp("Jun 24 2026", "24:34:56").has_value());
        CHECK(!ParseSteamVersionTimestamp("Jun 24 2026", "12:60:56").has_value());
        CHECK(!ParseSteamVersionTimestamp("Jun 24 2026", "12:34:60").has_value());
    }

    void TestSkeletonGate()
    {
        using esp::render::BoneConfidence;
        using esp::render::EvaluateSkeletonGate;
        using esp::render::ShouldDrawSkeleton;
        using esp::render::SkeletonDrawDecision;
        using esp::render::SkeletonSkipReason;

        {
            const auto result = EvaluateSkeletonGate({
                false,
                10,
            });
            CHECK(result.decision == SkeletonDrawDecision::Skip);
            CHECK(result.skipReason == SkeletonSkipReason::UnreliableBones);
            CHECK(result.confidence == BoneConfidence::None);
            CHECK(!ShouldDrawSkeleton(result));
        }

        {
            const auto result = EvaluateSkeletonGate({
                true,
                2,
            });
            CHECK(result.decision == SkeletonDrawDecision::Skip);
            CHECK(result.skipReason == SkeletonSkipReason::NotEnoughProjectedSegments);
            CHECK(result.confidence == BoneConfidence::Low);
            CHECK(!ShouldDrawSkeleton(result));
        }

        {
            const auto result = EvaluateSkeletonGate({
                true,
                3,
            });
            CHECK(result.decision == SkeletonDrawDecision::Partial);
            CHECK(result.skipReason == SkeletonSkipReason::None);
            CHECK(result.confidence == BoneConfidence::Low);
            CHECK(ShouldDrawSkeleton(result));
        }

        {
            const auto result = EvaluateSkeletonGate({
                true,
                5,
            });
            CHECK(result.decision == SkeletonDrawDecision::Full);
            CHECK(result.skipReason == SkeletonSkipReason::None);
            CHECK(result.confidence == BoneConfidence::High);
            CHECK(ShouldDrawSkeleton(result));
        }
    }

    void TestDrawPolicy()
    {
        using esp::render::IsWebRadarCoreSampleFresh;
        using esp::render::ShouldUpdateCachedViewMatrix;
        CHECK(ShouldUpdateCachedViewMatrix(true, 200, false, 0, 250));
        CHECK(!ShouldUpdateCachedViewMatrix(true, 199, true, 200, 250));
        CHECK(ShouldUpdateCachedViewMatrix(true, 201, true, 200, 250));
        CHECK(!ShouldUpdateCachedViewMatrix(false, 201, true, 200, 250));
        CHECK(!ShouldUpdateCachedViewMatrix(true, 251, true, 200, 250));
        CHECK(!ShouldUpdateCachedViewMatrix(true, 0, false, 0, 250));
        CHECK(!ShouldUpdateCachedViewMatrix(true, 200, true, 200,
            201 + esp::render::kCachedViewMatrixHoldUs));
        CHECK(IsWebRadarCoreSampleFresh(100, 100, 100, false));
        CHECK(!IsWebRadarCoreSampleFresh(0, 100, 100, false));
        CHECK(!IsWebRadarCoreSampleFresh(101, 100, 100, false));
        for (const uint64_t stepUs : {1000u, 4000u, 16000u, 100000u}) {
            // Polling the same source at different rates must never renew it.
            for (uint64_t ageUs = 0; ageUs < 4000000u; ageUs += stepUs) {
                CHECK(IsWebRadarCoreSampleFresh(100, 100 + ageUs, 100, false) ==
                    (ageUs <= esp::render::kWebRadarAliveHoldUs));
                CHECK(IsWebRadarCoreSampleFresh(100, 100 + ageUs, 0, false) ==
                    (ageUs <= esp::render::kWebRadarDeadHoldUs));
                CHECK(IsWebRadarCoreSampleFresh(100, 100 + ageUs, 100, true) ==
                    (ageUs <= esp::render::kWebRadarAliveHoldBulkRecoveryUs));
            }
        }
        using esp::render::EvaluateSnapshotTiming;
        using esp::render::IncrementFallbackStaleFrames;
        using esp::render::IsFreshTimestamp;
        using esp::render::IsSameNonZeroPlayerIdentity;
        using esp::render::IsWithinWebRadarBulkRecovery;
        using esp::render::IsRenderableBoxHeight;
        using esp::render::SelectWebRadarPlayerHoldUs;
        using esp::render::ShouldApplyVelocityExtrapolation;
        using esp::render::ShouldClearLastAliveCache;
        using esp::render::ShouldHoldWebRadarPlayer;
        using esp::render::ShouldRecordDrawEvent;
        using esp::render::ShouldRenderOverlayForMenuState;
        using esp::render::ShouldReuseCachedViewMatrix;
        using esp::render::ShouldReusePersistedBone;
        using esp::render::ShouldTrackLastValidAlive;
        using esp::render::ShouldUseFallbackProjectionBox;
        using esp::render::ShouldUsePrevTickFallback;

        CHECK(ShouldRenderOverlayForMenuState(false, false));
        CHECK(ShouldRenderOverlayForMenuState(false, true));
        CHECK(!ShouldRenderOverlayForMenuState(true, false));
        CHECK(ShouldRenderOverlayForMenuState(true, true));
        using esp::render::SmoothSnapshotAlpha;

        CHECK(IsFreshTimestamp(100u, 100u, 10u));
        CHECK(IsFreshTimestamp(100u, 110u, 10u));
        CHECK(!IsFreshTimestamp(100u, 111u, 10u));
        CHECK(!IsFreshTimestamp(0u, 100u, 10u));
        CHECK(!IsFreshTimestamp(100u, 99u, 10u));
        CHECK(ShouldReuseCachedViewMatrix(true, 100u, 100u + esp::render::kCachedViewMatrixHoldUs));
        CHECK(!ShouldReuseCachedViewMatrix(false, 100u, 100u));
        CHECK(!ShouldReuseCachedViewMatrix(true, 100u, 101u + esp::render::kCachedViewMatrixHoldUs));

        CHECK(SmoothSnapshotAlpha(0.0f) == 0.0f);
        CHECK(SmoothSnapshotAlpha(1.0f) == 1.0f);
        CHECK(SmoothSnapshotAlpha(-1.0f) == 0.0f);
        CHECK(SmoothSnapshotAlpha(2.0f) == 1.0f);

        {
            const auto timing = EvaluateSnapshotTiming({
                2000u,
                1000u,
                2500u,
                1000u,
            });
            CHECK(timing.snapshotIntervalUs == 1000u);
            CHECK(timing.renderDelayUs == 1000u);
            CHECK(timing.targetUs == 1500u);
            CHECK(timing.lerpAlpha == 0.5f);
            CHECK(timing.extrapolationSec == 0.0f);
        }

        {
            const auto timing = EvaluateSnapshotTiming({
                2000u,
                1000u,
                50000u,
                1000u,
            });
            CHECK(timing.lerpAlpha == 1.0f);
            CHECK(timing.extrapolationSec == esp::render::kMaxRenderExtrapolationSec);
        }

        CHECK(ShouldTrackLastValidAlive(true, false, 2));
        CHECK(ShouldTrackLastValidAlive(true, false, 3));
        CHECK(!ShouldTrackLastValidAlive(false, false, 2));
        CHECK(!ShouldTrackLastValidAlive(true, true, 2));
        CHECK(!ShouldTrackLastValidAlive(true, false, 1));

        CHECK(IsSameNonZeroPlayerIdentity(0x111u, 0x111u));
        CHECK(!IsSameNonZeroPlayerIdentity(0u, 0x111u));
        CHECK(!IsSameNonZeroPlayerIdentity(0x111u, 0u));
        CHECK(!IsSameNonZeroPlayerIdentity(0x111u, 0x222u));

        CHECK(ShouldUsePrevTickFallback(true, true, true, false, false, esp::render::kPrevTickFallbackMaxAgeUs));
        CHECK(!ShouldUsePrevTickFallback(false, true, true, false, false, 0u));
        CHECK(!ShouldUsePrevTickFallback(true, false, true, false, false, 0u));
        CHECK(!ShouldUsePrevTickFallback(true, true, false, false, false, 0u));
        CHECK(!ShouldUsePrevTickFallback(true, true, true, true, false, 0u));
        CHECK(!ShouldUsePrevTickFallback(true, true, true, false, true, 0u));
        CHECK(!ShouldUsePrevTickFallback(true, true, true, false, false, esp::render::kPrevTickFallbackMaxAgeUs + 1u));

        CHECK(ShouldClearLastAliveCache(0u, 0x111u));
        CHECK(ShouldClearLastAliveCache(0x222u, 0x111u));
        CHECK(!ShouldClearLastAliveCache(0x111u, 0x111u));
        CHECK(!ShouldClearLastAliveCache(0x111u, 0u));
        CHECK(IncrementFallbackStaleFrames(249) == 250);
        CHECK(IncrementFallbackStaleFrames(250) == 250);

        CHECK(!ShouldRecordDrawEvent(esp::render::kDrawEventThrottleUs, 0u));
        CHECK(ShouldRecordDrawEvent(esp::render::kDrawEventThrottleUs + 1u, 0u));
        CHECK(IsWithinWebRadarBulkRecovery(100u, 100u + esp::render::kWebRadarBulkRecoveryWindowUs));
        CHECK(!IsWithinWebRadarBulkRecovery(100u, 101u + esp::render::kWebRadarBulkRecoveryWindowUs));
        CHECK(SelectWebRadarPlayerHoldUs(true, 0) == esp::render::kWebRadarDeadHoldBulkRecoveryUs);
        CHECK(SelectWebRadarPlayerHoldUs(true, 100) == esp::render::kWebRadarAliveHoldBulkRecoveryUs);
        CHECK(SelectWebRadarPlayerHoldUs(false, 0) == esp::render::kWebRadarDeadHoldUs);
        CHECK(SelectWebRadarPlayerHoldUs(false, 100) == esp::render::kWebRadarAliveHoldUs);
        CHECK(ShouldHoldWebRadarPlayer(true, false, true, 0x123u, 100u, 110u, 10u));
        CHECK(!ShouldHoldWebRadarPlayer(true, false, true, 0x123u, 100u, 110u, 10u, true));
        CHECK(!ShouldHoldWebRadarPlayer(false, false, true, 0x123u, 100u, 110u, 10u));
        CHECK(!ShouldHoldWebRadarPlayer(true, true, true, 0x123u, 100u, 110u, 10u));
        CHECK(!ShouldHoldWebRadarPlayer(true, false, false, 0x123u, 100u, 110u, 10u));
        CHECK(!ShouldHoldWebRadarPlayer(true, false, true, 0u, 100u, 110u, 10u));
        CHECK(!ShouldHoldWebRadarPlayer(true, false, true, 0x123u, 100u, 111u, 10u));
        CHECK(ShouldReusePersistedBone(100u, 100u + esp::render::kBonePersistUs));
        CHECK(!ShouldReusePersistedBone(100u, 101u + esp::render::kBonePersistUs));
        CHECK(esp::render::CanInterpolatePlayer(1, 1, 10, 10, 100, {8, 0, 0}, {}));
        CHECK(!esp::render::CanInterpolatePlayer(1, 1, 11, 10, 100, {8, 0, 0}, {}));
        CHECK(!esp::render::CanInterpolatePlayer(1, 1, 10, 10, 0, {8, 0, 0}, {}));
        CHECK(!esp::render::CanInterpolatePlayer(1, 1, 10, 10, 100, {256, 0, 0}, {}));
        CHECK(!esp::render::CanInterpolatePlayer(1, 1, 10, 10, 100,
            {std::numeric_limits<float>::quiet_NaN(), 0, 0}, {}));
        CHECK(esp::render::IsFreshTimestamp(100, 500100, esp::render::kPlayerRenderMaxAgeUs));
        CHECK(!esp::render::IsFreshTimestamp(100, 500101, esp::render::kPlayerRenderMaxAgeUs));

        CHECK(!ShouldApplyVelocityExtrapolation(0.0f, true, 2.0f));
        CHECK(!ShouldApplyVelocityExtrapolation(0.01f, false, 2.0f));
        CHECK(!ShouldApplyVelocityExtrapolation(0.01f, true, esp::render::kMinVelocityExtrapolation2D));
        CHECK(ShouldApplyVelocityExtrapolation(0.01f, true, esp::render::kMinVelocityExtrapolation2D + 0.01f));
        {
            const auto motion = esp::render::ResolvePlayerMotion(
                Vector3{10.0f, 20.0f, 30.0f},
                Vector3{12.0f, 20.0f, 30.0f},
                Vector3{100.0f, 0.0f, 0.0f},
                true,
                0.01f);
            CHECK(std::fabs(motion.interpolationOffset.x - 2.0f) < 0.0001f);
            CHECK(std::fabs(motion.extrapolationOffset.x - 1.0f) < 0.0001f);
            CHECK(std::fabs(motion.renderPosition.x - 13.0f) < 0.0001f);

            const Vector3 transformedBone =
                Vector3{10.0f, 20.0f, 70.0f} +
                motion.interpolationOffset +
                motion.extrapolationOffset;
            CHECK(std::fabs(transformedBone.x - 13.0f) < 0.0001f);
        }
        {
            const auto motion = esp::render::ResolvePlayerMotion(
                Vector3{10.0f, 20.0f, 30.0f},
                Vector3{10.0f, 20.0f, 30.0f},
                Vector3{100.0f, 0.0f, 0.0f},
                true,
                0.01f);
            CHECK(std::fabs(motion.interpolationOffset.x) < 0.0001f);
            CHECK(std::fabs(motion.renderPosition.x - 11.0f) < 0.0001f);
        }
        {
            const Vector3 predicted = esp::render::ResolveBombRenderPosition(
                Vector3{10.0f, 20.0f, 30.0f},
                Vector3{100.0f, 0.0f, 0.0f},
                true,
                100000u,
                110000u);
            CHECK(std::fabs(predicted.x - 11.0f) < 0.0001f);
            CHECK(std::fabs(predicted.y - 20.0f) < 0.0001f);
        }
        {
            const Vector3 clamped = esp::render::ResolveBombRenderPosition(
                Vector3{10.0f, 20.0f, 30.0f},
                Vector3{100.0f, 0.0f, 0.0f},
                true,
                100000u,
                140000u);
            CHECK(std::fabs(clamped.x - 12.5f) < 0.0001f);
        }
        {
            const Vector3 unchanged = esp::render::ResolveBombRenderPosition(
                Vector3{10.0f, 20.0f, 30.0f},
                Vector3{100.0f, 0.0f, 0.0f},
                false,
                100000u,
                110000u);
            CHECK(std::fabs(unchanged.x - 10.0f) < 0.0001f);
        }
        {
            const Vector3 stale = esp::render::ResolveBombRenderPosition(
                Vector3{10.0f, 20.0f, 30.0f},
                Vector3{100.0f, 0.0f, 0.0f},
                true,
                100000u,
                100000u + esp::render::kBombPositionSampleMaxAgeUs + 1u);
            CHECK(std::fabs(stale.x - 10.0f) < 0.0001f);
        }
        CHECK(std::fabs(esp::render::ResolveBombDefuseTotal(5.0f) - 5.0f) < 0.0001f);
        {
            esp::render::BombPositionInterpolator smoothing;
            auto point = smoothing.Update({100, 200, 300}, true, true, 1000000, 1000000, 1, 1, 0x1000);
            CHECK(point.x == 100); // First sighting is immediate.
            point = smoothing.Update({112, 200, 300}, true, true, 1012000, 1012000, 1, 1, 0x1000);
            CHECK(point.x == 100); // No step at a new measurement.
            point = smoothing.Update({112, 200, 300}, true, true, 1012000, 1018000, 1, 1, 0x1000);
            CHECK(std::fabs(point.x - 106) < 0.0001f);
            point = smoothing.Update({112, 200, 300}, true, true, 1012000, 1024000, 1, 1, 0x1000);
            CHECK(point.x == 112);
            point = smoothing.Update({112, 200, 300}, true, true, 1012000, 1050000, 1, 1, 0x1000);
            CHECK(point.x == 112); // No extrapolation past measured position.
            point = smoothing.Update({1000, 200, 300}, true, true, 1060000, 1060000, 1, 1, 0x1000);
            CHECK(point.x == 1000); // Teleport/correction snaps, never drags across map.
            point = smoothing.Update({1012, 200, 300}, true, true, 1072000, 1072000, 1, 1, 0x1000);
            CHECK(point.x == 1000);
            point = smoothing.Update({1014, 200, 300}, true, true, 1076000, 1076000, 2, 1, 0x1000);
            CHECK(point.x == 1014); // Scene reset.
            point = smoothing.Update({1018, 200, 300}, true, true, 1080000, 1080000, 2, 2, 0x1000);
            CHECK(point.x == 1018); // New drop generation.
            point = smoothing.Update({1022, 200, 300}, true, true, 1084000, 1084000, 2, 2, 0x2000);
            CHECK(point.x == 1022); // Different C4 identity.
            point = smoothing.Update({1026, 200, 300}, false, true, 1088000, 1088000, 2, 2, 0x2000);
            CHECK(point.x == 1026 && !smoothing.initialized); // Planted/picked up.
            smoothing.Update({1026, 200, 300}, true, true, 1088000, 1088000, 2, 2, 0x2000);
            point = smoothing.Update({1030, 200, 300}, true, false, 1092000, 1092000, 2, 2, 0x2000);
            CHECK(point.x == 1030 && !smoothing.initialized); // Cached source is not new motion.
            smoothing.Update({1030, 200, 300}, true, true, 1092000, 1092000, 2, 2, 0x2000);
            point = smoothing.Update({1030, 200, 300}, true, true, 1092000, 1200000, 2, 2, 0x2000);
            CHECK(point.x == 1030 && !smoothing.initialized); // Long gap ends interpolation.
            point = smoothing.Update({NAN, NAN, NAN}, true, true, 1200000, 1200000, 2, 2, 0x2000);
            CHECK(!std::isfinite(point.x) && !smoothing.initialized);
            // At irregular presentation rates every result stays within its
            // measured segment; neither a bounce nor a delayed frame overshoots.
            smoothing.Reset();
            smoothing.Update({10, 20, 30}, true, true, 2000000, 2000000, 3, 3, 0x3000);
            smoothing.Update({30, 20, 10}, true, true, 2020000, 2020000, 3, 3, 0x3000);
            for (uint64_t now = 2021000; now <= 2040000; now += 1000) {
                point = smoothing.Update({30, 20, 10}, true, true, 2020000, now, 3, 3, 0x3000);
                CHECK(point.x >= 10 && point.x <= 30);
                CHECK(point.z >= 10 && point.z <= 30);
            }
            CHECK(point.x == 30 && point.z == 10);
        }
        CHECK(std::fabs(esp::render::ResolveBombDefuseTotal(10.0f) - 10.0f) < 0.0001f);
        CHECK(std::fabs(
            esp::render::ResolveBombDefuseTotal(
                std::numeric_limits<float>::quiet_NaN()) -
            esp::render::kFallbackBombDefuseSeconds) < 0.0001f);
        CHECK(std::fabs(
            esp::render::CalculateBombDefuseProgress(2.5f, 5.0f) -
            0.5f) < 0.0001f);
        CHECK(
            esp::render::EvaluateBombDefuseOutcome(105.0f, 106.0f) ==
            esp::render::BombDefuseOutcome::Completes);
        CHECK(
            esp::render::EvaluateBombDefuseOutcome(107.0f, 106.0f) ==
            esp::render::BombDefuseOutcome::Explodes);
        CHECK(
            esp::render::EvaluateBombDefuseOutcome(0.0f, 106.0f) ==
            esp::render::BombDefuseOutcome::Unknown);

        CHECK(ShouldUseFallbackProjectionBox(false, 10.0f, 0.0f, true));
        CHECK(ShouldUseFallbackProjectionBox(true, 3.0f, 0.0f, true));
        CHECK(!ShouldUseFallbackProjectionBox(true, 5.0f, 0.0f, true));
        CHECK(!ShouldUseFallbackProjectionBox(false, 10.0f, 0.0f, false));
        CHECK(IsRenderableBoxHeight(esp::render::kMinProjectedBoxHeight));
        CHECK(!IsRenderableBoxHeight(esp::render::kMinProjectedBoxHeight - 0.01f));
    }

    void TestVisualStylePolicy()
    {
        using esp::render::BarColorMode;
        using esp::render::BoxStyle;
        using esp::render::NormalizeBarColorMode;
        using esp::render::NormalizeBoxStyle;
        using esp::render::ResolveArmorBarColor;
        using esp::render::ResolveBarColor;
        using esp::render::ResolveCornerLength;

        CHECK(NormalizeBoxStyle(-1) == BoxStyle::Corners);
        CHECK(NormalizeBoxStyle(0) == BoxStyle::Corners);
        CHECK(NormalizeBoxStyle(1) == BoxStyle::Full);
        CHECK(NormalizeBoxStyle(2) == BoxStyle::Dashed);
        CHECK(NormalizeBoxStyle(3) == BoxStyle::Corners);
        CHECK(NormalizeBarColorMode(-1) == BarColorMode::Dynamic);
        CHECK(NormalizeBarColorMode(1) == BarColorMode::Solid);
        CHECK(NormalizeBarColorMode(2) == BarColorMode::Gradient);
        CHECK(std::fabs(ResolveCornerLength(100.0f, 200.0f, 25) - 25.0f) < 0.001f);
        CHECK(ResolveCornerLength(0.0f, 100.0f, 25) == 0.0f);
        CHECK(std::fabs(ResolveCornerLength(4.0f, 100.0f, 25) - 2.0f) < 0.001f);
        CHECK(std::fabs(ResolveCornerLength(20.0f, 100.0f, 1) - 3.0f) < 0.001f);
        CHECK(std::fabs(ResolveCornerLength(20.0f, 100.0f, 90) - 9.0f) < 0.001f);

        const float high[4] = { 0.2f, 1.0f, 0.4f, 1.0f };
        const float low[4] = { 1.0f, 0.0f, 0.0f, 0.5f };
        const auto dynamicLow = ResolveBarColor(0, 0.0f, high, low);
        const auto midpoint = ResolveBarColor(0, 0.5f, high, low);
        const auto dynamicHigh = ResolveBarColor(0, 1.0f, high, low);
        CHECK(std::fabs(dynamicLow.r - 1.0f) < 0.001f);
        CHECK(std::fabs(dynamicLow.g - 0.12f) < 0.001f);
        CHECK(std::fabs(midpoint.r - 1.0f) < 0.001f);
        CHECK(std::fabs(midpoint.g - 0.72f) < 0.001f);
        CHECK(std::fabs(midpoint.b - 0.08f) < 0.001f);
        CHECK(std::fabs(dynamicHigh.r - 0.25f) < 0.001f);
        CHECK(std::fabs(dynamicHigh.g - 0.95f) < 0.001f);
        CHECK(std::fabs(dynamicHigh.b - 0.35f) < 0.001f);
        CHECK(std::fabs(dynamicHigh.a - 1.0f) < 0.001f);
        const auto dynamicArmor = ResolveArmorBarColor(0, 0.2f, low, high);
        CHECK(std::fabs(dynamicArmor.r - 0.35f) < 0.001f);
        CHECK(std::fabs(dynamicArmor.g - 0.65f) < 0.001f);
        CHECK(std::fabs(dynamicArmor.b - 1.0f) < 0.001f);
        CHECK(std::fabs(dynamicArmor.a - 1.0f) < 0.001f);
        const auto solid = ResolveBarColor(1, 0.0f, high, low);
        CHECK(std::fabs(solid.r - high[0]) < 0.001f);
        CHECK(std::fabs(solid.g - high[1]) < 0.001f);
    }

    void TestSnapshotRing()
    {
        using esp::state::AdvanceSnapshotWriteCursor;
        using esp::state::IsSnapshotSlotIndexValid;
        using esp::state::NormalizeSnapshotSlotIndex;
        using esp::state::ShouldApplyPlayerVisibilityFrame;
        using esp::state::ShouldApplyCameraLocalPosition;

        CHECK(IsSnapshotSlotIndexValid(0));
        CHECK(IsSnapshotSlotIndexValid(7));
        CHECK(!IsSnapshotSlotIndexValid(-1));
        CHECK(!IsSnapshotSlotIndexValid(8));

        CHECK(NormalizeSnapshotSlotIndex(8) == 0);
        CHECK(NormalizeSnapshotSlotIndex(9) == 1);
        CHECK(NormalizeSnapshotSlotIndex(-1) == 7);

        CHECK(AdvanceSnapshotWriteCursor(1, 0) == 2);
        CHECK(AdvanceSnapshotWriteCursor(7, 0) == 1);
        CHECK(AdvanceSnapshotWriteCursor(6, 7) == 0);
        CHECK(AdvanceSnapshotWriteCursor(0, 0, 1) == 0);

        CHECK(ShouldApplyPlayerVisibilityFrame(3u, 3u, 200u, 100u, 250u));
        CHECK(!ShouldApplyPlayerVisibilityFrame(4u, 3u, 200u, 100u, 250u));
        CHECK(!ShouldApplyPlayerVisibilityFrame(3u, 3u, 99u, 100u, 250u));
        CHECK(!ShouldApplyPlayerVisibilityFrame(3u, 3u, 200u, 100u, 199u));
        CHECK(!ShouldApplyPlayerVisibilityFrame(3u, 3u, 200u, 100u, 301u, 100u));
        CHECK(ShouldApplyCameraLocalPosition(3, 3, 0x1000, 0x1000, true, 200, 250, 100));
        CHECK(!ShouldApplyCameraLocalPosition(2, 3, 0x1000, 0x1000, true, 200, 250, 100));
        CHECK(!ShouldApplyCameraLocalPosition(3, 3, 0x1000, 0x2000, true, 200, 250, 100));
        CHECK(!ShouldApplyCameraLocalPosition(3, 3, 0, 0, true, 200, 250, 100));
        CHECK(!ShouldApplyCameraLocalPosition(3, 3, 0x1000, 0x1000, false, 200, 250, 100));
        CHECK(!ShouldApplyCameraLocalPosition(3, 3, 0x1000, 0x1000, true, 0, 250, 100));
        CHECK(!ShouldApplyCameraLocalPosition(3, 3, 0x1000, 0x1000, true, 251, 250, 100));
        CHECK(ShouldApplyCameraLocalPosition(3, 3, 0x1000, 0x1000, true, 200, 300, 100));
        CHECK(!ShouldApplyCameraLocalPosition(3, 3, 0x1000, 0x1000, true, 200, 301, 100));
    }

    void TestPlayerSlotPolicy()
    {
        using esp::data::EvaluatePlayerSlotStalePolicy;
        using esp::data::IsWithinBulkRecoveryStaleWindow;
        using esp::data::PlayerSlotStaleAction;
        using esp::data::SelectPlayerStaleEvictionWindowMs;

        CHECK(!IsWithinBulkRecoveryStaleWindow(0, 10));
        CHECK(!IsWithinBulkRecoveryStaleWindow(100, 100));
        CHECK(IsWithinBulkRecoveryStaleWindow(100, 100 + esp::data::kBulkRecoveryStaleHoldWindowUs));
        CHECK(!IsWithinBulkRecoveryStaleWindow(100, 101 + esp::data::kBulkRecoveryStaleHoldWindowUs));
        CHECK(SelectPlayerStaleEvictionWindowMs(1000, false) == 1000);
        CHECK(SelectPlayerStaleEvictionWindowMs(1000, true) == esp::data::kBulkRecoveryStaleEvictionMs);

        {
            const auto result = EvaluatePlayerSlotStalePolicy({
                true,
                true,
                5000,
                1000,
                1000,
                false,
                7,
            });
            CHECK(result.action == PlayerSlotStaleAction::Refresh);
            CHECK(result.nextStaleFrames == 0);
            CHECK(result.elapsedMs == 0);
        }

        {
            const auto result = EvaluatePlayerSlotStalePolicy({
                false,
                false,
                5000,
                1000,
                1000,
                false,
                7,
            });
            CHECK(result.action == PlayerSlotStaleAction::Ignore);
            CHECK(result.nextStaleFrames == 7);
        }

        {
            const auto result = EvaluatePlayerSlotStalePolicy({
                false,
                true,
                1500,
                1000,
                1000,
                false,
                2,
            });
            CHECK(result.action == PlayerSlotStaleAction::Hold);
            CHECK(result.elapsedMs == 500);
            CHECK(result.nextStaleFrames == 3);
            CHECK(result.evictionWindowMs == 1000);
        }

        {
            const auto result = EvaluatePlayerSlotStalePolicy({
                false,
                true,
                2001,
                1000,
                1000,
                false,
                2,
            });
            CHECK(result.action == PlayerSlotStaleAction::Evict);
            CHECK(result.elapsedMs == 1001);
            CHECK(result.nextStaleFrames == 3);
        }

        {
            const auto result = EvaluatePlayerSlotStalePolicy({
                false,
                true,
                4500,
                1000,
                1000,
                true,
                2,
            });
            CHECK(result.action == PlayerSlotStaleAction::Hold);
            CHECK(result.evictionWindowMs == esp::data::kBulkRecoveryStaleEvictionMs);
        }
    }

    void TestPlayerCommitPolicy()
    {
        using esp::data::CanTemporarilyHoldAliveCore;
        using esp::data::IsAuthoritativeDeadCoreSample;
        using esp::data::IsDeathConfirmed;
        using esp::data::IsRecentMapFingerprintSceneReset;
        using esp::data::IsRecentStructuralReset;
        using esp::data::IsWithinGraceWindow;
        using esp::data::SelectCoreInvalidGraceUs;
        using esp::data::SelectCoreStaleHoldUs;
        using esp::data::SelectZeroPawnGraceUs;
        using esp::data::ShouldExposePlayerRoster;

        CHECK(!ShouldExposePlayerRoster(false, false));
        CHECK(ShouldExposePlayerRoster(true, false));
        CHECK(ShouldExposePlayerRoster(false, true));
        CHECK(ShouldExposePlayerRoster(true, true));

        CHECK(!IsRecentStructuralReset(0, 100));
        CHECK(!IsRecentStructuralReset(100, 100));
        CHECK(IsRecentStructuralReset(100, 100 + esp::data::kPlayerCommitRecentStructuralResetWindowUs));
        CHECK(!IsRecentStructuralReset(100, 101 + esp::data::kPlayerCommitRecentStructuralResetWindowUs));
        CHECK(!IsRecentMapFingerprintSceneReset(0u, 100u));
        CHECK(IsRecentMapFingerprintSceneReset(100u, 100u + esp::data::kMapFingerprintRecentSceneResetUs - 1u));
        CHECK(!IsRecentMapFingerprintSceneReset(100u, 100u + esp::data::kMapFingerprintRecentSceneResetUs));

        CHECK(SelectZeroPawnGraceUs(false) == esp::data::kPlayerCommitZeroPawnGraceUs);
        CHECK(SelectZeroPawnGraceUs(true) == esp::data::kPlayerCommitZeroPawnGraceAfterBulkUs);

        CHECK(SelectCoreStaleHoldUs(false, false, false) == esp::data::kPlayerCommitCoreStaleHoldUs);
        CHECK(SelectCoreStaleHoldUs(true, false, false) == esp::data::kPlayerCommitCoreStaleHoldAfterResetUs);
        CHECK(SelectCoreStaleHoldUs(false, true, false) == esp::data::kPlayerCommitCoreStaleHoldAfterResetUs);
        CHECK(SelectCoreStaleHoldUs(true, true, true) == esp::data::kPlayerCommitCoreStaleHoldAfterBulkUs);

        CHECK(CanTemporarilyHoldAliveCore({
            true,
            false,
            true,
            0x111u,
            0x111u,
            1000u,
            1000u + esp::data::kPlayerCommitCoreStaleHoldUs,
            esp::data::kPlayerCommitCoreStaleHoldUs,
        }));
        CHECK(!CanTemporarilyHoldAliveCore({
            true,
            true,
            true,
            0x111u,
            0x111u,
            1000u,
            1100u,
            esp::data::kPlayerCommitCoreStaleHoldUs,
        }));
        CHECK(!CanTemporarilyHoldAliveCore({
            true,
            false,
            true,
            0x111u,
            0x222u,
            1000u,
            1100u,
            esp::data::kPlayerCommitCoreStaleHoldUs,
        }));
        CHECK(!CanTemporarilyHoldAliveCore({
            true,
            false,
            true,
            0x111u,
            0x111u,
            1000u,
            1001u + esp::data::kPlayerCommitCoreStaleHoldUs,
            esp::data::kPlayerCommitCoreStaleHoldUs,
        }));

        CHECK(IsAuthoritativeDeadCoreSample(true, 0x111u, 0, 0u));
        CHECK(IsAuthoritativeDeadCoreSample(true, 0x111u, 100, 1u));
        CHECK(IsAuthoritativeDeadCoreSample(true, 0x111u, 0, 1u));
        CHECK(!IsAuthoritativeDeadCoreSample(false, 0x111u, 0, 1u));
        CHECK(!IsAuthoritativeDeadCoreSample(true, 0u, 0, 1u));
        CHECK(!IsAuthoritativeDeadCoreSample(true, 0x111u, 100, 0u));
        CHECK(!IsAuthoritativeDeadCoreSample(true, 0x111u, -1, 0u));
        CHECK(!IsAuthoritativeDeadCoreSample(true, 0x111u, 0, 0xFFu));

        CHECK(!IsDeathConfirmed(false, 2, 1000u, 1000u + esp::data::kPlayerCommitDeathConfirmUs));
        CHECK(!IsDeathConfirmed(true, 1, 1000u, 1000u + esp::data::kPlayerCommitDeathConfirmUs));
        CHECK(!IsDeathConfirmed(true, 2, 1000u, 999u + esp::data::kPlayerCommitDeathConfirmUs));
        CHECK(IsDeathConfirmed(true, 2, 1000u, 1000u + esp::data::kPlayerCommitDeathConfirmUs));

        CHECK(SelectCoreInvalidGraceUs(false, false, 123u) == 123u);
        CHECK(SelectCoreInvalidGraceUs(true, false, 123u) == esp::data::kPlayerCommitDeathConfirmUs);
        CHECK(SelectCoreInvalidGraceUs(false, true, 123000u) == esp::data::kPlayerCommitDeathConfirmUs);
        CHECK(SelectCoreInvalidGraceUs(false, true, 123u) == 123u);

        CHECK(IsWithinGraceWindow(1000u, 1000u, 10u));
        CHECK(IsWithinGraceWindow(1000u, 1009u, 10u));
        CHECK(!IsWithinGraceWindow(1000u, 1010u, 10u));
        CHECK(!IsWithinGraceWindow(1000u, 999u, 10u));
    }

    void TestPlayerCorePolicy()
    {
        using esp::data::ElapsedSinceOrZero;
        using esp::data::EvaluatePlayerCoreHealth;
        using esp::data::IsNewLocalTeamSwitchEdge;
        using esp::data::IsPlayerCoreReadComplete;
        using esp::data::IsSamePendingLocalTeamSwitch;
        using esp::data::PlayerCoreBatchDecision;
        using esp::data::IsPlayerCoreStatePlausible;
        using esp::data::ResolveCoreVitalReadLayout;
        using esp::data::SelectPlayerCoreBatchDecision;

        CHECK(ElapsedSinceOrZero(200u, 100u) == 100u);
        CHECK(ElapsedSinceOrZero(100u, 100u) == 0u);
        CHECK(ElapsedSinceOrZero(99u, 100u) == 0u);
        CHECK(IsSamePendingLocalTeamSwitch(2, 2, 3, 3, 100u, 100u + esp::data::kPendingLocalTeamSwitchWindowUs));
        CHECK(!IsSamePendingLocalTeamSwitch(2, 3, 3, 3, 100u, 100u));
        CHECK(!IsSamePendingLocalTeamSwitch(2, 2, 3, 2, 100u, 100u));
        CHECK(!IsSamePendingLocalTeamSwitch(2, 2, 3, 3, 100u, 101u + esp::data::kPendingLocalTeamSwitchWindowUs));
        CHECK(IsNewLocalTeamSwitchEdge(0u, 2, 2, 3, 3, 100u));
        CHECK(IsNewLocalTeamSwitchEdge(100u, 2, 2, 3, 3, 99u));
        CHECK(IsNewLocalTeamSwitchEdge(100u, 2, 2, 3, 3, 101u + esp::data::kLocalTeamSwitchEdgeCooldownUs));
        CHECK(IsNewLocalTeamSwitchEdge(100u, 2, 3, 3, 3, 101u));
        CHECK(IsNewLocalTeamSwitchEdge(100u, 2, 2, 3, 2, 101u));
        CHECK(!IsNewLocalTeamSwitchEdge(100u, 2, 2, 3, 3, 100u + esp::data::kLocalTeamSwitchEdgeCooldownUs));

        const esp::data::PlayerCoreReadCompletion completeCoreRead{
            sizeof(int),
            sizeof(int),
            sizeof(uint8_t),
            sizeof(Vector3),
            true,
            sizeof(esp::data::PlayerTeamSample),
        };
        CHECK(IsPlayerCoreReadComplete(completeCoreRead));
        auto wrongWidthTeamRead = completeCoreRead;
        wrongWidthTeamRead.teamBytes = sizeof(int);
        CHECK(!IsPlayerCoreReadComplete(wrongWidthTeamRead));
        const uint8_t teamAndSpawnFlags[] = { 2, 0xFF, 0xA5, 0x7F };
        esp::data::PlayerTeamSample teamSample = 0;
        std::memcpy(&teamSample, teamAndSpawnFlags, sizeof(teamSample));
        CHECK(teamSample == 2 && sizeof(teamSample) == 1);
        auto partialCoreRead = completeCoreRead;
        partialCoreRead.positionBytes = sizeof(Vector3) - 1u;
        CHECK(!IsPlayerCoreReadComplete(partialCoreRead));
        auto cachedTeamCoreRead = completeCoreRead;
        cachedTeamCoreRead.teamReadRequired = false;
        cachedTeamCoreRead.teamBytes = 0;
        CHECK(IsPlayerCoreReadComplete(cachedTeamCoreRead));

        CHECK(EvaluatePlayerCoreHealth(false, 10, 10, false, false) ==
              esp::SubsystemHealthState::Unknown);
        CHECK(EvaluatePlayerCoreHealth(true, 0, 0, false, false) ==
              esp::SubsystemHealthState::Unknown);
        CHECK(EvaluatePlayerCoreHealth(true, 10, 0, false, false) ==
              esp::SubsystemHealthState::Failed);
        CHECK(EvaluatePlayerCoreHealth(true, 10, 9, false, false) ==
              esp::SubsystemHealthState::Degraded);
        CHECK(EvaluatePlayerCoreHealth(true, 10, 7, false, false) ==
              esp::SubsystemHealthState::Degraded);
        CHECK(EvaluatePlayerCoreHealth(true, 10, 10, true, false) ==
              esp::SubsystemHealthState::Degraded);
        CHECK(EvaluatePlayerCoreHealth(true, 10, 10, false, true) ==
              esp::SubsystemHealthState::Failed);

        CHECK(SelectPlayerCoreBatchDecision(0, 0, 0, 0, 1000) ==
              PlayerCoreBatchDecision::Hold);
        CHECK(SelectPlayerCoreBatchDecision(10, 10, 0, 0, 1000) ==
              PlayerCoreBatchDecision::Coherent);
        const auto adjacentVitals = ResolveCoreVitalReadLayout(0x344, 0x348);
        CHECK(adjacentVitals.coalesced);
        CHECK(adjacentVitals.baseOffset == 0x344);
        CHECK(adjacentVitals.spanBytes == 5u);
        CHECK(adjacentVitals.healthDelta == 0u);
        CHECK(adjacentVitals.lifeStateDelta == 4u);
        CHECK(!ResolveCoreVitalReadLayout(0x344, 0x500).coalesced);
        CHECK(!ResolveCoreVitalReadLayout(0, 0x348).coalesced);
        CHECK(SelectPlayerCoreBatchDecision(10, 7, 1000, 500, 200000) ==
              PlayerCoreBatchDecision::Degraded);
        CHECK(SelectPlayerCoreBatchDecision(9, 6, 1000000, 999000, 4384900) ==
              PlayerCoreBatchDecision::Degraded);
        CHECK(SelectPlayerCoreBatchDecision(9, 0, 1000000, 999000, 4384900) ==
              PlayerCoreBatchDecision::Hold);
        for (int good = 1; good < 64; ++good) {
            for (uint64_t sampleUs = 1001000; sampleUs < 5000000; sampleUs += 4000)
                CHECK(SelectPlayerCoreBatchDecision(64, good, 1000000, 999000, sampleUs) ==
                      PlayerCoreBatchDecision::Degraded);
        }
        uint8_t vitalBytes[5]{};
        const int expectedHealth = 100;
        std::memcpy(vitalBytes, &expectedHealth, sizeof(expectedHealth));
        int decodedHealth = -1;
        uint8_t decodedLife = 255;
        CHECK(!esp::data::DecodeCoreVitalRead(adjacentVitals, vitalBytes, 5, 4,
            decodedHealth, decodedLife));
        CHECK(decodedHealth == -1 && decodedLife == 255);
        CHECK(esp::data::DecodeCoreVitalRead(adjacentVitals, vitalBytes, 5, 5,
            decodedHealth, decodedLife));
        CHECK(decodedHealth == 100 && decodedLife == 0);
        CHECK(!esp::data::DecodeCoreVitalRead(adjacentVitals, vitalBytes, 4, 5,
            decodedHealth, decodedLife));
        CHECK(SelectPlayerCoreBatchDecision(
                  10,
                  8,
                  1000,
                  0,
                  1000 + esp::data::kPlayerCoreInitialBatchHoldUs - 1) ==
              PlayerCoreBatchDecision::Hold);
        CHECK(SelectPlayerCoreBatchDecision(
                  10,
                  8,
                  1000,
                  0,
                  1000 + esp::data::kPlayerCoreInitialBatchHoldUs) ==
              PlayerCoreBatchDecision::Degraded);

        CHECK(IsPlayerCoreStatePlausible(true, 100, 50, 0, true));
        CHECK(!IsPlayerCoreStatePlausible(true, 100, 50, 0, false));
        CHECK(IsPlayerCoreStatePlausible(true, 0, 50, 1, false));
        CHECK(IsPlayerCoreStatePlausible(true, 0, 0, 0, false));
        CHECK(!IsPlayerCoreStatePlausible(false, 0, 0, 1, false));
        CHECK(!IsPlayerCoreStatePlausible(true, -1, 0, 1, false));
        CHECK(SelectPlayerCoreBatchDecision(
                  10,
                  8,
                  1000,
                  900,
                  1001) ==
              PlayerCoreBatchDecision::Degraded);
    }

    void TestPlayerFlagPolicy()
    {
        using esp::data::EvaluateBlindFlashSample;
        using esp::data::IsBinaryPlayerFlagReadComplete;
        using esp::data::IsBlindFlashReadComplete;
        using esp::data::IsValidFlashBangTimeSample;
        using esp::data::IsValidFlashDurationSample;
        using esp::data::IsValidPlayerFlagSample;
        using esp::data::PlayerFlagFilterState;
        using esp::data::ResetPlayerFlagFilter;
        using esp::data::UpdatePlayerFlagFilter;

        CHECK(IsValidPlayerFlagSample(0u));
        CHECK(IsValidPlayerFlagSample(1u));
        CHECK(!IsValidPlayerFlagSample(2u));
        CHECK(IsBinaryPlayerFlagReadComplete(true, sizeof(uint8_t), 1u));
        CHECK(!IsBinaryPlayerFlagReadComplete(true, 0u, 1u));
        CHECK(!IsBinaryPlayerFlagReadComplete(true, sizeof(uint8_t), 2u));
        CHECK(!IsBinaryPlayerFlagReadComplete(false, sizeof(uint8_t), 1u));
        CHECK(!IsValidPlayerFlagSample(0xFFu));

        CHECK(IsValidFlashDurationSample(0.0f));
        CHECK(IsValidFlashDurationSample(5.0f));
        CHECK(!IsValidFlashDurationSample(-0.01f));
        CHECK(!IsValidFlashDurationSample(10.01f));
        CHECK(!IsValidFlashDurationSample(
            std::numeric_limits<float>::quiet_NaN()));
        CHECK(IsValidFlashBangTimeSample(100.0f, 100.0f));
        CHECK(IsValidFlashBangTimeSample(100.4f, 100.0f));
        CHECK(!IsValidFlashBangTimeSample(100.6f, 100.0f));
        CHECK(!IsValidFlashBangTimeSample(-0.1f, 100.0f));
        CHECK(IsBlindFlashReadComplete(
            true,
            true,
            sizeof(float),
            sizeof(float),
            100.0f,
            2.0f,
            101.0f));
        CHECK(!IsBlindFlashReadComplete(
            true,
            true,
            sizeof(float) - 1u,
            sizeof(float),
            100.0f,
            2.0f,
            101.0f));
        CHECK(!IsBlindFlashReadComplete(
            true,
            false,
            sizeof(float),
            sizeof(float),
            100.0f,
            2.0f,
            101.0f));

        const auto justBelowActivation =
            EvaluateBlindFlashSample(100.0f, 2.0f, 101.81f, false);
        CHECK(justBelowActivation.fresh);
        CHECK(!justBelowActivation.active);
        const auto atActivation =
            EvaluateBlindFlashSample(100.0f, 2.0f, 101.79f, false);
        CHECK(atActivation.fresh);
        CHECK(atActivation.active);
        const auto activeHysteresis =
            EvaluateBlindFlashSample(100.0f, 2.0f, 101.91f, true);
        CHECK(activeHysteresis.fresh);
        CHECK(activeHysteresis.active);
        const auto atRelease =
            EvaluateBlindFlashSample(100.0f, 2.0f, 101.93f, true);
        CHECK(atRelease.fresh);
        CHECK(!atRelease.active);
        const auto expiredHistoricalDuration =
            EvaluateBlindFlashSample(50.0f, 5.0f, 100.0f, false);
        CHECK(expiredHistoricalDuration.fresh);
        CHECK(!expiredHistoricalDuration.active);
        const auto staleGameClock =
            EvaluateBlindFlashSample(100.0f, 2.0f, 0.0f, false);
        CHECK(!staleGameClock.fresh);

        PlayerFlagFilterState state = {};
        CHECK(!UpdatePlayerFlagFilter(state, true, true, true, 100u));
        CHECK(UpdatePlayerFlagFilter(state, true, true, true, 101u));
        CHECK(UpdatePlayerFlagFilter(
            state,
            true,
            false,
            false,
            101u + esp::data::kPlayerFlagReadGapHoldUs));
        CHECK(!UpdatePlayerFlagFilter(
            state,
            true,
            false,
            false,
            102u + esp::data::kPlayerFlagReadGapHoldUs));

        ResetPlayerFlagFilter(state);
        CHECK(!UpdatePlayerFlagFilter(state, true, true, true, 200u));
        CHECK(!UpdatePlayerFlagFilter(state, true, false, false, 201u));
        CHECK(state.positiveSamples == 1u);
        CHECK(UpdatePlayerFlagFilter(state, true, true, true, 201u));
        CHECK(!UpdatePlayerFlagFilter(state, true, true, false, 202u));
        CHECK(!state.active);

        CHECK(!UpdatePlayerFlagFilter(state, true, true, true, 300u));
        CHECK(!UpdatePlayerFlagFilter(state, false, true, true, 301u));
        CHECK(!state.active && state.positiveSamples == 0u);
    }

    void TestPlayerHierarchyPolicy()
    {
        using esp::data::ControllerBacklinkState;
        using esp::data::CommittedPlayerIdentityCandidate;
        using esp::data::ControllerWindowProbe;
        using esp::data::DoesEntityHandleReferenceIndex;
        using esp::data::EntityHierarchyRecoveryAction;
        using esp::data::EvaluateControllerBacklink;
        using esp::data::IsBulkPawnOnlyHierarchyLoss;
        using esp::data::IsEntityHierarchyRecoveryCooldownElapsed;
        using esp::data::IsFlatLiveEntityHierarchy;
        using esp::data::IsHierarchyBulkEvictionDetected;
        using esp::data::IsHierarchyBulkRecoveryExtensionCandidate;
        using esp::data::IsHierarchyHoldAgeAllowed;
        using esp::data::IsHierarchyWarmupSatisfied;
        using esp::data::IsEntityHierarchyObservationAgeElapsed;
        using esp::data::IsSameCommittedPlayerIdentity;
        using esp::data::IsSameResolvedPawnIdentity;
        using esp::data::PreferSecondCommittedPlayerIdentity;
        using esp::data::PreferSecondResolvedPlayerIdentity;
        using esp::data::ProjectHierarchyMissingStreak;
        using esp::data::ResolvedPlayerIdentityCandidate;
        using esp::data::SelectFlatEntityHierarchyRecovery;
        using esp::data::SelectControllerWindowProbe;
        using esp::data::SelectHierarchyMissingPolicy;
        using esp::data::SelectHierarchyDiscoveryBudget;
        using esp::data::SelectMissingEntityHierarchyRecovery;
        using esp::data::SelectZeroControllerRecovery;
        using esp::data::ShouldEvictMissingHierarchy;
        using esp::data::ShouldExtendHierarchyBulkRecoveryWindow;
        using esp::data::ShouldHoldMissingHierarchy;
        using esp::data::ShouldProbeAlternateControllerWindow;
        using esp::data::ShouldProbeFallbackEntityStride;
        using esp::data::ShouldRetryMissingControllerSlot;

        const ControllerWindowProbe unread = {};
        const ControllerWindowProbe onePointer = { true, false, 1 };
        const ControllerWindowProbe preferredWindow = { true, false, 3 };
        const ControllerWindowProbe alternateWindow = { true, false, 10 };
        const ControllerWindowProbe localWindow = { true, true, 1 };

        CHECK(ShouldProbeAlternateControllerWindow(false, preferredWindow, false));
        CHECK(ShouldProbeAlternateControllerWindow(true, unread, false));
        CHECK(!ShouldProbeAlternateControllerWindow(true, onePointer, false));
        CHECK(ShouldProbeAlternateControllerWindow(
            true,
            ControllerWindowProbe{ true, false, 0 },
            false));
        CHECK(ShouldProbeAlternateControllerWindow(true, preferredWindow, true));
        CHECK(!ShouldProbeAlternateControllerWindow(true, localWindow, true));
        CHECK(SelectControllerWindowProbe(unread, unread) == -1);
        CHECK(SelectControllerWindowProbe(preferredWindow, alternateWindow) == 1);
        CHECK(SelectControllerWindowProbe(localWindow, alternateWindow) == 0);
        CHECK(SelectControllerWindowProbe(preferredWindow, preferredWindow) == 0);
        CHECK(ShouldProbeFallbackEntityStride(false));
        CHECK(!ShouldProbeFallbackEntityStride(true));
        CHECK(ShouldRetryMissingControllerSlot(false, false, false, false));
        CHECK(ShouldRetryMissingControllerSlot(false, true, true, false));
        CHECK(ShouldRetryMissingControllerSlot(true, true, false, true));
        CHECK(!ShouldRetryMissingControllerSlot(true, false, true, false));
        CHECK(!ShouldRetryMissingControllerSlot(false, true, false, false));

        constexpr uint32_t entityHandleMask = 0x7FFFu;
        CHECK(DoesEntityHandleReferenceIndex(0x18064u, 0x64u, entityHandleMask));
        CHECK(DoesEntityHandleReferenceIndex(0x18064u, 0x8064u, entityHandleMask));
        CHECK(!DoesEntityHandleReferenceIndex(0x18064u, 0x65u, entityHandleMask));
        CHECK(!DoesEntityHandleReferenceIndex(0u, 0x64u, entityHandleMask));
        CHECK(!DoesEntityHandleReferenceIndex(0x18064u, 0u, entityHandleMask));
        CHECK(
            SelectHierarchyDiscoveryBudget(false, false) ==
            esp::data::kHierarchySteadyDiscoveryBudget);
        CHECK(SelectHierarchyDiscoveryBudget(true, false) == 64);
        CHECK(SelectHierarchyDiscoveryBudget(false, true) == 64);
        const ResolvedPlayerIdentityCandidate canonicalIdentity{
            .slot = 0,
            .controller = 0x1000u,
            .pawn = 0x2000u,
            .pawnHandle = 0x8064u,
            .pawnControllerHandle = 0x8001u,
            .committedIdentity = true,
        };
        const ResolvedPlayerIdentityCandidate duplicateIdentity{
            .slot = 1,
            .controller = 0x1100u,
            .pawn = 0x2000u,
            .pawnHandle = 0x10064u,
            .pawnControllerHandle = 0x8001u,
        };
        const ResolvedPlayerIdentityCandidate distinctIdentity{
            .slot = 2,
            .controller = 0x1200u,
            .pawn = 0x3000u,
            .pawnHandle = 0x8065u,
            .pawnControllerHandle = 0x8003u,
        };
        CHECK(IsSameResolvedPawnIdentity(
            canonicalIdentity,
            duplicateIdentity,
            entityHandleMask));
        CHECK(!IsSameResolvedPawnIdentity(
            canonicalIdentity,
            distinctIdentity,
            entityHandleMask));
        auto reusedGenerationIdentity = distinctIdentity;
        reusedGenerationIdentity.pawnHandle = 0x10064u;
        CHECK(!IsSameResolvedPawnIdentity(
            canonicalIdentity,
            reusedGenerationIdentity,
            entityHandleMask));
        auto migratedControllerIdentity = distinctIdentity;
        migratedControllerIdentity.controller = canonicalIdentity.controller;
        CHECK(IsSameResolvedPawnIdentity(
            canonicalIdentity,
            migratedControllerIdentity,
            entityHandleMask));
        auto sharedBacklinkIdentity = distinctIdentity;
        sharedBacklinkIdentity.pawnControllerHandle =
            canonicalIdentity.pawnControllerHandle;
        CHECK(IsSameResolvedPawnIdentity(
            canonicalIdentity,
            sharedBacklinkIdentity,
            entityHandleMask,
            true));
        CHECK(!IsSameResolvedPawnIdentity(
            canonicalIdentity,
            sharedBacklinkIdentity,
            entityHandleMask,
            false));
        auto unresolvedCanonicalIdentity = canonicalIdentity;
        unresolvedCanonicalIdentity.pawn = 0;
        auto unresolvedDuplicateIdentity = duplicateIdentity;
        unresolvedDuplicateIdentity.pawn = 0;
        unresolvedDuplicateIdentity.pawnHandle =
            unresolvedCanonicalIdentity.pawnHandle;
        CHECK(IsSameResolvedPawnIdentity(
            unresolvedCanonicalIdentity,
            unresolvedDuplicateIdentity,
            entityHandleMask));
        CHECK(EvaluateControllerBacklink(0, 0x8001u, entityHandleMask) ==
              ControllerBacklinkState::Match);
        CHECK(EvaluateControllerBacklink(1, 0x8001u, entityHandleMask) ==
              ControllerBacklinkState::Mismatch);
        CHECK(EvaluateControllerBacklink(1, 0u, entityHandleMask) ==
              ControllerBacklinkState::Unknown);
        CHECK(!PreferSecondResolvedPlayerIdentity(
            canonicalIdentity,
            duplicateIdentity,
            entityHandleMask,
            true));

        auto staleFirstIdentity = canonicalIdentity;
        staleFirstIdentity.pawnControllerHandle = 0x8002u;
        auto matchingSecondIdentity = duplicateIdentity;
        matchingSecondIdentity.pawnControllerHandle = 0x8002u;
        CHECK(PreferSecondResolvedPlayerIdentity(
            staleFirstIdentity,
            matchingSecondIdentity,
            entityHandleMask,
            true));

        auto localDuplicateIdentity = duplicateIdentity;
        localDuplicateIdentity.localIdentity = true;
        CHECK(PreferSecondResolvedPlayerIdentity(
            canonicalIdentity,
            localDuplicateIdentity,
            entityHandleMask,
            false));
        CHECK(!IsHierarchyWarmupSatisfied(
            esp::data::kHierarchyWarmupMinimumAgeUs - 1u,
            10,
            10,
            10,
            true,
            true));
        CHECK(!IsHierarchyWarmupSatisfied(
            esp::data::kHierarchyWarmupMinimumAgeUs,
            1,
            1,
            1,
            true,
            true));
        CHECK(!IsHierarchyWarmupSatisfied(
            esp::data::kHierarchyWarmupExactPopulationUs - 1u,
            esp::data::kHierarchyWarmupRosterFloor - 1,
            esp::data::kHierarchyWarmupRosterFloor - 1,
            esp::data::kHierarchyWarmupRosterFloor - 1,
            true,
            true));
        CHECK(IsHierarchyWarmupSatisfied(
            esp::data::kHierarchyWarmupMinimumAgeUs,
            esp::data::kHierarchyWarmupRosterFloor,
            esp::data::kHierarchyWarmupRosterFloor,
            esp::data::kHierarchyWarmupRosterFloor,
            true,
            true));
        CHECK(IsHierarchyWarmupSatisfied(
            esp::data::kHierarchyWarmupExactPopulationUs,
            1,
            1,
            1,
            true,
            true));
        CHECK(!IsHierarchyWarmupSatisfied(
            esp::data::kHierarchyWarmupMinimumAgeUs,
            10,
            2,
            2,
            true,
            true));
        CHECK(IsHierarchyWarmupSatisfied(
            esp::data::kHierarchyWarmupMinimumAgeUs,
            10,
            10,
            10,
            true,
            true));
        CHECK(!IsHierarchyWarmupSatisfied(
            esp::data::kHierarchyWarmupExactPopulationUs - 1u,
            10,
            9,
            9,
            true,
            true));
        CHECK(IsHierarchyWarmupSatisfied(
            esp::data::kHierarchyWarmupExactPopulationUs,
            10,
            9,
            9,
            true,
            true));
        CHECK(IsHierarchyWarmupSatisfied(
            esp::data::kHierarchyWarmupRelaxedPopulationUs,
            10,
            8,
            8,
            true,
            true));
        CHECK(!IsHierarchyWarmupSatisfied(
            esp::data::kHierarchyWarmupMinimumAgeUs,
            1,
            1,
            1,
            false,
            true));
        CHECK(IsHierarchyWarmupSatisfied(
            esp::data::kHierarchyWarmupLocalIdentityGraceUs,
            10,
            10,
            10,
            false,
            true));
        CHECK(IsHierarchyWarmupSatisfied(
            esp::data::kHierarchyWarmupLocalIdentityGraceUs,
            10,
            10,
            10,
            true,
            false));

        const CommittedPlayerIdentityCandidate staleCommitted{
            .slot = 8,
            .controller = 0x120000u,
            .pawn = 0x220000u,
            .freshCore = false,
            .currentHierarchy = false,
        };
        const CommittedPlayerIdentityCandidate migratedCommitted{
            .slot = 2,
            .controller = 0x120000u,
            .pawn = 0x230000u,
            .freshCore = true,
            .currentHierarchy = true,
        };
        CHECK(IsSameCommittedPlayerIdentity(
            staleCommitted,
            migratedCommitted));
        CHECK(PreferSecondCommittedPlayerIdentity(
            staleCommitted,
            migratedCommitted));
        auto samePawnCommitted = migratedCommitted;
        samePawnCommitted.controller = 0x130000u;
        samePawnCommitted.pawn = staleCommitted.pawn;
        CHECK(IsSameCommittedPlayerIdentity(
            staleCommitted,
            samePawnCommitted));
        auto unrelatedCommitted = migratedCommitted;
        unrelatedCommitted.controller = 0x140000u;
        unrelatedCommitted.pawn = 0x240000u;
        CHECK(!IsSameCommittedPlayerIdentity(
            staleCommitted,
            unrelatedCommitted));

        {
            const auto policy = SelectHierarchyMissingPolicy(false, true, false);
            CHECK(policy.holdUs == esp::data::kHierarchyStableMissingHoldUs);
            CHECK(policy.threshold == esp::data::kHierarchyStableMissingThreshold);
        }

        {
            const auto policy = SelectHierarchyMissingPolicy(true, true, false);
            CHECK(policy.holdUs == esp::data::kHierarchyResetMissingHoldUs);
            CHECK(policy.threshold == esp::data::kHierarchyResetMissingThreshold);
        }

        {
            const auto policy = SelectHierarchyMissingPolicy(false, false, false);
            CHECK(policy.holdUs == esp::data::kHierarchyResetMissingHoldUs);
            CHECK(policy.threshold == esp::data::kHierarchyResetMissingThreshold);
        }

        {
            const auto policy = SelectHierarchyMissingPolicy(true, false, true);
            CHECK(policy.holdUs == esp::data::kHierarchyBulkMissingHoldUs);
            CHECK(policy.threshold == esp::data::kHierarchyBulkMissingThreshold);
        }

        CHECK(IsHierarchyHoldAgeAllowed(0u, 100u, 10u));
        CHECK(IsHierarchyHoldAgeAllowed(100u, 99u, 10u));
        CHECK(IsHierarchyHoldAgeAllowed(100u, 110u, 10u));
        CHECK(!IsHierarchyHoldAgeAllowed(100u, 111u, 10u));

        CHECK(ProjectHierarchyMissingStreak(0u) == 1u);
        CHECK(ProjectHierarchyMissingStreak(0xFFFEu) == 0xFFFFu);
        CHECK(ProjectHierarchyMissingStreak(0xFFFFu) == 0xFFFFu);

        CHECK(!ShouldEvictMissingHierarchy(true, true, 9u, 10u));
        CHECK(ShouldEvictMissingHierarchy(false, true, 0u, 10u));
        CHECK(ShouldEvictMissingHierarchy(true, false, 0u, 10u));
        CHECK(ShouldEvictMissingHierarchy(true, true, 10u, 10u));

        CHECK(IsBulkPawnOnlyHierarchyLoss(2, 0));
        CHECK(!IsBulkPawnOnlyHierarchyLoss(1, 0));
        CHECK(!IsBulkPawnOnlyHierarchyLoss(2, 2));
        CHECK(IsHierarchyBulkEvictionDetected(2, 2, 0));
        CHECK(!IsHierarchyBulkEvictionDetected(1, 2, 0));
        CHECK(!IsHierarchyBulkEvictionDetected(2, 1, 0));

        CHECK(IsHierarchyBulkRecoveryExtensionCandidate(true, 2, 1, 0));
        CHECK(!IsHierarchyBulkRecoveryExtensionCandidate(false, 2, 1, 0));
        CHECK(!IsHierarchyBulkRecoveryExtensionCandidate(true, 1, 1, 0));
        CHECK(!IsHierarchyBulkRecoveryExtensionCandidate(true, 2, 1, 1));

        CHECK(ShouldExtendHierarchyBulkRecoveryWindow(esp::data::kHierarchyBulkRecoveryWindowMaxUs - 1u));
        CHECK(!ShouldExtendHierarchyBulkRecoveryWindow(esp::data::kHierarchyBulkRecoveryWindowMaxUs));

        CHECK(ShouldHoldMissingHierarchy(false, false));
        CHECK(ShouldHoldMissingHierarchy(true, true));
        CHECK(!ShouldHoldMissingHierarchy(true, false));


        CHECK(IsEntityHierarchyRecoveryCooldownElapsed(0u, 100u, 10u));
        CHECK(IsEntityHierarchyRecoveryCooldownElapsed(100u, 99u, 10u));
        CHECK(!IsEntityHierarchyRecoveryCooldownElapsed(100u, 109u, 10u));
        CHECK(IsEntityHierarchyRecoveryCooldownElapsed(100u, 110u, 10u));
        CHECK(!IsEntityHierarchyObservationAgeElapsed(0u, 100u, 10u));
        CHECK(!IsEntityHierarchyObservationAgeElapsed(100u, 109u, 10u));
        CHECK(IsEntityHierarchyObservationAgeElapsed(100u, 110u, 10u));

        constexpr uint64_t missingSinceUs = 100u;
        CHECK(SelectMissingEntityHierarchyRecovery(
            esp::data::kEntityHierarchyMissingForcedFullStreak,
            missingSinceUs,
            EntityHierarchyRecoveryAction::None,
            0u,
            missingSinceUs + esp::data::kEntityHierarchyMissingProbeAgeUs - 1u) ==
            EntityHierarchyRecoveryAction::None);
        CHECK(SelectMissingEntityHierarchyRecovery(
            esp::data::kEntityHierarchyMissingProbeStreak,
            missingSinceUs,
            EntityHierarchyRecoveryAction::None,
            0u,
            missingSinceUs + esp::data::kEntityHierarchyMissingProbeAgeUs) ==
            EntityHierarchyRecoveryAction::Probe);
        CHECK(SelectMissingEntityHierarchyRecovery(
            esp::data::kEntityHierarchyMissingRepairStreak,
            missingSinceUs,
            EntityHierarchyRecoveryAction::Probe,
            missingSinceUs + esp::data::kEntityHierarchyMissingProbeAgeUs,
            missingSinceUs + esp::data::kEntityHierarchyMissingRepairAgeUs) ==
            EntityHierarchyRecoveryAction::Repair);
        CHECK(SelectMissingEntityHierarchyRecovery(
            esp::data::kEntityHierarchyMissingFullStreak,
            missingSinceUs,
            EntityHierarchyRecoveryAction::Repair,
            missingSinceUs + esp::data::kEntityHierarchyMissingRepairAgeUs,
            missingSinceUs + esp::data::kEntityHierarchyMissingFullAgeUs) ==
            EntityHierarchyRecoveryAction::Full);
        CHECK(SelectMissingEntityHierarchyRecovery(
            esp::data::kEntityHierarchyMissingForcedFullStreak,
            missingSinceUs,
            EntityHierarchyRecoveryAction::Full,
            missingSinceUs + esp::data::kEntityHierarchyMissingFullAgeUs,
            missingSinceUs + esp::data::kEntityHierarchyMissingForcedFullAgeUs) ==
            EntityHierarchyRecoveryAction::ForcedFull);
        CHECK(SelectMissingEntityHierarchyRecovery(
            esp::data::kEntityHierarchyMissingForcedFullStreak,
            missingSinceUs,
            EntityHierarchyRecoveryAction::ForcedFull,
            0u,
            missingSinceUs + esp::data::kEntityHierarchyMissingForcedFullAgeUs) ==
            EntityHierarchyRecoveryAction::None);

        CHECK(IsFlatLiveEntityHierarchy(true, esp::data::kEntityHierarchyFlatSceneAgeUs, 32, 0, 31));
        CHECK(!IsFlatLiveEntityHierarchy(true, esp::data::kEntityHierarchyFlatSceneAgeUs - 1u, 32, 0, 31));
        CHECK(!IsFlatLiveEntityHierarchy(true, esp::data::kEntityHierarchyFlatSceneAgeUs, 32, 1, 31));
        CHECK(!IsFlatLiveEntityHierarchy(true, esp::data::kEntityHierarchyFlatSceneAgeUs, 32, 0, 32));

        CHECK(SelectFlatEntityHierarchyRecovery(
            esp::data::kEntityHierarchyFlatFullStreak,
            missingSinceUs,
            EntityHierarchyRecoveryAction::None,
            0u,
            missingSinceUs + esp::data::kEntityHierarchyFlatProbeAgeUs - 1u) ==
            EntityHierarchyRecoveryAction::None);
        CHECK(SelectFlatEntityHierarchyRecovery(
            esp::data::kEntityHierarchyFlatProbeStreak,
            missingSinceUs,
            EntityHierarchyRecoveryAction::None,
            0u,
            missingSinceUs + esp::data::kEntityHierarchyFlatProbeAgeUs) ==
            EntityHierarchyRecoveryAction::Probe);
        CHECK(SelectFlatEntityHierarchyRecovery(
            esp::data::kEntityHierarchyFlatRepairStreak,
            missingSinceUs,
            EntityHierarchyRecoveryAction::Probe,
            missingSinceUs + esp::data::kEntityHierarchyFlatProbeAgeUs,
            missingSinceUs + esp::data::kEntityHierarchyFlatRepairAgeUs) ==
            EntityHierarchyRecoveryAction::Repair);
        CHECK(SelectFlatEntityHierarchyRecovery(
            esp::data::kEntityHierarchyFlatFullStreak,
            missingSinceUs,
            EntityHierarchyRecoveryAction::Repair,
            missingSinceUs + esp::data::kEntityHierarchyFlatRepairAgeUs,
            missingSinceUs + esp::data::kEntityHierarchyFlatFullAgeUs) ==
            EntityHierarchyRecoveryAction::Full);
        CHECK(SelectFlatEntityHierarchyRecovery(
            esp::data::kEntityHierarchyFlatFullStreak,
            missingSinceUs,
            EntityHierarchyRecoveryAction::Full,
            0u,
            missingSinceUs + esp::data::kEntityHierarchyFlatFullAgeUs) ==
            EntityHierarchyRecoveryAction::None);

        CHECK(SelectZeroControllerRecovery(
            missingSinceUs,
            EntityHierarchyRecoveryAction::None,
            missingSinceUs + esp::data::kZeroControllerProbeAgeUs - 1u) ==
            EntityHierarchyRecoveryAction::None);
        CHECK(SelectZeroControllerRecovery(
            missingSinceUs,
            EntityHierarchyRecoveryAction::None,
            missingSinceUs + esp::data::kZeroControllerProbeAgeUs) ==
            EntityHierarchyRecoveryAction::Probe);
        CHECK(SelectZeroControllerRecovery(
            missingSinceUs,
            EntityHierarchyRecoveryAction::Probe,
            missingSinceUs + esp::data::kZeroControllerRepairAgeUs) ==
            EntityHierarchyRecoveryAction::Repair);
        CHECK(SelectZeroControllerRecovery(
            missingSinceUs,
            EntityHierarchyRecoveryAction::Repair,
            missingSinceUs + esp::data::kZeroControllerFullAgeUs) ==
            EntityHierarchyRecoveryAction::Full);
        CHECK(SelectZeroControllerRecovery(
            missingSinceUs,
            EntityHierarchyRecoveryAction::Full,
            missingSinceUs + esp::data::kZeroControllerFullAgeUs) ==
            EntityHierarchyRecoveryAction::None);
    }

    void TestPlayerRepairPolicy()
    {
        using esp::data::ArePartialCoreBadSlotsRepeated;
        using esp::data::IsCorePartial;
        using esp::data::IsCoreRepairAttemptDue;
        using esp::data::IsPartialCoreConfirmed;
        using esp::data::SelectCoreRepairPolicy;
        using esp::data::SelectCoreRepairRetryIntervalUs;
        using esp::data::SelectPartialCoreConfirmAgeUs;
        using esp::data::ShouldReportPartialCoreIncident;

        {
            const auto policy = SelectCoreRepairPolicy(false, false);
            CHECK(policy.retryIntervalUs == esp::data::kCoreRepairRetryNormalUs);
            CHECK(policy.repairThreshold == esp::data::kCoreRepairThresholdNormal);
            CHECK(policy.missingTolerance == esp::data::kCoreRepairMissingToleranceNormal);
            CHECK(policy.partialCoreThreshold == esp::data::kPartialCoreThresholdNormal);
        }

        {
            const auto policy = SelectCoreRepairPolicy(true, false);
            CHECK(policy.retryIntervalUs == esp::data::kCoreRepairRetryNormalUs);
            CHECK(policy.repairThreshold == esp::data::kCoreRepairThresholdAfterReset);
            CHECK(policy.missingTolerance == esp::data::kCoreRepairMissingToleranceNormal);
            CHECK(policy.partialCoreThreshold == esp::data::kPartialCoreThresholdNormal);
        }

        {
            const auto policy = SelectCoreRepairPolicy(false, true);
            CHECK(policy.retryIntervalUs == esp::data::kCoreRepairRetryAfterResetUs);
            CHECK(policy.repairThreshold == esp::data::kCoreRepairThresholdAfterReset);
            CHECK(policy.missingTolerance == esp::data::kCoreRepairMissingToleranceAfterReset);
            CHECK(policy.partialCoreThreshold == esp::data::kPartialCoreThresholdAfterReset);
        }

        CHECK(IsCoreRepairAttemptDue(0u, 100u, 10u));
        CHECK(IsCoreRepairAttemptDue(100u, 99u, 10u));
        CHECK(!IsCoreRepairAttemptDue(100u, 109u, 10u));
        CHECK(IsCoreRepairAttemptDue(100u, 110u, 10u));
        CHECK(SelectCoreRepairRetryIntervalUs(
            esp::data::kCoreRepairRetryNormalUs,
            esp::data::kCoreRepairSustainedStreak - 1u) ==
            esp::data::kCoreRepairRetryNormalUs);
        CHECK(SelectCoreRepairRetryIntervalUs(
            esp::data::kCoreRepairRetryNormalUs,
            esp::data::kCoreRepairSustainedStreak) ==
            esp::data::kCoreRepairRetrySustainedUs);
        CHECK(SelectCoreRepairRetryIntervalUs(
            esp::data::kCoreRepairRetryNormalUs,
            esp::data::kCoreRepairPersistentStreak) ==
            esp::data::kCoreRepairRetryPersistentUs);

        CHECK(IsCorePartial(2, 1, 0));
        CHECK(!IsCorePartial(1, 0, 0));
        CHECK(!IsCorePartial(2, 1, 1));

        CHECK(ArePartialCoreBadSlotsRepeated(0b0010u, 0b0110u));
        CHECK(!ArePartialCoreBadSlotsRepeated(0u, 0b0010u));
        CHECK(!ArePartialCoreBadSlotsRepeated(0b1000u, 0b0010u));

        CHECK(SelectPartialCoreConfirmAgeUs(true) == esp::data::kPartialCoreConfirmAgeAfterResetUs);
        CHECK(SelectPartialCoreConfirmAgeUs(false) == esp::data::kPartialCoreConfirmAgeNormalUs);

        CHECK(IsPartialCoreConfirmed(
            false,
            true,
            0b0010u,
            esp::data::kPartialCoreConfirmStreak,
            esp::data::kPartialCoreConfirmAgeNormalUs,
            false));
        CHECK(!IsPartialCoreConfirmed(
            true,
            true,
            0b0010u,
            esp::data::kPartialCoreConfirmStreak,
            esp::data::kPartialCoreConfirmAgeNormalUs,
            false));
        CHECK(!IsPartialCoreConfirmed(
            false,
            false,
            0b0010u,
            esp::data::kPartialCoreConfirmStreak,
            esp::data::kPartialCoreConfirmAgeNormalUs,
            false));
        CHECK(!IsPartialCoreConfirmed(
            false,
            true,
            0u,
            esp::data::kPartialCoreConfirmStreak,
            esp::data::kPartialCoreConfirmAgeNormalUs,
            false));

        CHECK(ShouldReportPartialCoreIncident(false, 60u, 60u, true));
        CHECK(!ShouldReportPartialCoreIncident(true, 60u, 60u, true));
        CHECK(!ShouldReportPartialCoreIncident(false, 59u, 60u, true));
        CHECK(!ShouldReportPartialCoreIncident(false, 60u, 60u, false));
    }

    void TestVisibilityPolicy()
    {
        using esp::data::BuildSpottedMask;
        using esp::data::IsSpottedStateReadComplete;
        using esp::data::IsVisibleByLocalSpottedMask;
        using esp::data::ResolveVisibilityFromSpotted;
        using esp::data::VisibilityResolveInput;
        using esp::data::ResolveVisibilityDiagnosticState;
        using esp::data::VisibilityDiagnosticState;

        CHECK(ResolveVisibilityDiagnosticState(
            false, 10, false, 0, 0u) == VisibilityDiagnosticState::Disabled);
        CHECK(ResolveVisibilityDiagnosticState(
            true, 1, false, 0, 1000000u) == VisibilityDiagnosticState::Waiting);
        CHECK(ResolveVisibilityDiagnosticState(
            true, 10, true, 9, 1000u) == VisibilityDiagnosticState::Healthy);
        CHECK(ResolveVisibilityDiagnosticState(
            true, 10, false, 0, 100001u) == VisibilityDiagnosticState::Unavailable);
        CHECK(ResolveVisibilityDiagnosticState(
            true, 10, true, 0, 250001u) == VisibilityDiagnosticState::Stale);

        const uint64_t bit10Mask = BuildSpottedMask(1u << 10, 0u);
        CHECK(IsSpottedStateReadComplete(true, 8u, 8u));
        CHECK(!IsSpottedStateReadComplete(true, 4u, 8u));
        CHECK(!IsSpottedStateReadComplete(false, 8u, 8u));
        CHECK(IsVisibleByLocalSpottedMask(bit10Mask, 10, -1, -1, -1));
        CHECK(!IsVisibleByLocalSpottedMask(bit10Mask, 9, -1, -1, -1));
        CHECK(!IsVisibleByLocalSpottedMask(bit10Mask, 11, -1, -1, -1));
        CHECK(IsVisibleByLocalSpottedMask(bit10Mask, 9, 10, -1, -1));
        CHECK(!IsVisibleByLocalSpottedMask(bit10Mask, 6, -1, -1, -1));

        const uint64_t highBitMask = BuildSpottedMask(0u, 1u << 2);
        CHECK(IsVisibleByLocalSpottedMask(highBitMask, 34, -1, -1, -1));

        {
            const auto result = ResolveVisibilityFromSpotted(VisibilityResolveInput{
                false,
                bit10Mask,
                false,
                true,
                10,
                -1,
                -1,
                -1,
            });
            CHECK(!result.hasFreshState);
            CHECK(!result.visible);
        }

        {
            const auto result = ResolveVisibilityFromSpotted(VisibilityResolveInput{
                true,
                bit10Mask,
                false,
                true,
                10,
                -1,
                -1,
                -1,
            });
            CHECK(result.hasFreshState);
            CHECK(result.visible);
        }

        {
            const auto result = ResolveVisibilityFromSpotted(VisibilityResolveInput{
                true,
                0,
                false,
                false,
                -1,
                -1,
                -1,
                -1,
            });
            CHECK(!result.hasFreshState);
            CHECK(!result.visible);
        }

        {
            const auto result = ResolveVisibilityFromSpotted(VisibilityResolveInput{
                true,
                0,
                false,
                true,
                -1,
                -1,
                -1,
                -1,
            });
            CHECK(result.hasFreshState);
            CHECK(!result.visible);
        }

        {
            const auto result = ResolveVisibilityFromSpotted(VisibilityResolveInput{
                false,
                0,
                true,
                true,
                -1,
                -1,
                -1,
                -1,
            });
            CHECK(result.hasFreshState);
            CHECK(result.visible);
        }
    }

    void TestBoneReadPolicy()
    {
        {
            esp::data::BoneReadBatch batch;
            esp::HitboxCapsule hitboxes[6] = {};
            const float heights[6] = {72, 64, 36, 45, 52, 58};
            for (int i = 0; i < 6; ++i) {
                const esp::data::BoneTransform transform{{100, 100, heights[i]}, 1, {0, 0, 0, 1}};
                memcpy(batch.transforms.data() + static_cast<size_t>(40 + i) * 32u, &transform, sizeof(transform));
                hitboxes[i].valid = true;
                hitboxes[i].index = static_cast<uint8_t>(i);
                hitboxes[i].bone = static_cast<int16_t>(40 + i);
            }
            Vector3 bones[esp::kPlayerStoredBoneCount] = {};
            esp::data::UnpackStoredBones(batch, bones);
            CHECK(!esp::data::EvaluateStoredBonePlausibility({bones, {100,100,0}, true}).plausible);
            CHECK(esp::data::RemapStoredCoreBones(batch, hitboxes, 6, bones));
            CHECK(esp::data::EvaluateStoredBonePlausibility({bones, {100,100,0}, true}).plausible);
            CHECK(bones[esp::PlayerStoredBoneIndex(esp::HEAD)].z == 72);
            hitboxes[0].bone = 200;
            CHECK(!esp::data::RemapStoredCoreBones(batch, hitboxes, 6, bones));
            CHECK(!esp::data::RemapStoredCoreBones(batch, nullptr, 6, bones));
        }
        using esp::data::BoneReadBatch;
        using esp::data::IsBoneCoveredByReadBatch;
        using esp::data::IsBonePointerReadComplete;
        using esp::data::IsBonePoseReadComplete;
        using esp::data::IsBoneSlotReadDue;
        using esp::data::IsBonePointerValidationDue;
        using esp::data::SelectBonePointerValidation;
        using esp::data::IsBoneSlotLive;
        using esp::data::IsBoneSlotStale;
        using esp::data::IsCooldownElapsed;
        using esp::data::IsPerSlotBoneLocalResetDue;
        using esp::data::IsPerSlotBoneProbeDue;
        using esp::data::SelectBoneArrayHoldStreak;
        using esp::data::SelectBoneReadBatchSlotLimit;
        using esp::data::SelectSceneNodeHoldStreak;
        using esp::data::ShouldEscalatePerSlotBoneProbe;
        using esp::data::ShouldReuseCachedPointerOnZero;

        CHECK(IsBoneSlotReadDue(0u, 100u, 8000u));
        CHECK(IsBoneSlotReadDue(100u, 99u, 8000u));
        CHECK(!IsBoneSlotReadDue(100u, 8099u, 8000u));
        CHECK(IsBoneSlotReadDue(100u, 8100u, 8000u));
        CHECK(IsBonePointerValidationDue(100u, 101u, false));
        CHECK(IsBonePointerValidationDue(0u, 101u, true));
        CHECK(IsBonePointerValidationDue(100u, 101u, true));
        CHECK(IsBonePointerValidationDue(
            100u,
            100u + esp::data::kBonePointerValidationUs,
            true));
        int validationBudget = esp::data::kBonePointerValidationBudgetPerTick;
        CHECK(SelectBonePointerValidation(true, false, validationBudget));
        CHECK(validationBudget == esp::data::kBonePointerValidationBudgetPerTick);
        for (int slot = 0; slot < 64; ++slot)
            CHECK(SelectBonePointerValidation(true, true, validationBudget));
        CHECK(validationBudget == 0);
        CHECK(!SelectBonePointerValidation(true, true, validationBudget));
        CHECK(!SelectBonePointerValidation(false, false, validationBudget));
        esp::PlayerData previous;
        previous.valid = true; previous.pawn = 0x10000; previous.pawnHandle = 0x10001;
        previous.health = 100; previous.position = {100, 200, 0};
        CHECK(!esp::data::HasBoneCoreDiscontinuity(previous, previous.pawn,
            previous.pawnHandle, 100, 0, {102, 200, 0}));
        CHECK(esp::data::HasBoneCoreDiscontinuity(previous, previous.pawn,
            previous.pawnHandle, 100, 0, {100, 200, 600})); // vertical teleport too
        previous.health = 0;
        CHECK(esp::data::HasBoneCoreDiscontinuity(previous, previous.pawn,
            previous.pawnHandle, 100, 0, previous.position));
        CHECK(!esp::data::HasBoneCoreDiscontinuity(previous, previous.pawn,
            previous.pawnHandle + 1, 100, 0, previous.position));
        CHECK(SelectBoneReadBatchSlotLimit(0) == 0);
        CHECK(SelectBoneReadBatchSlotLimit(1) == 1);
        CHECK(SelectBoneReadBatchSlotLimit(5) == 5);
        CHECK(SelectBoneReadBatchSlotLimit(7) == 7);
        CHECK(SelectBoneReadBatchSlotLimit(10) == 10);
        CHECK(SelectBoneReadBatchSlotLimit(64) == 64);
        for (int slotCount = 1; slotCount <= 64; ++slotCount) {
            const int limit = SelectBoneReadBatchSlotLimit(slotCount);
            CHECK(limit > 0);
            CHECK(limit <= slotCount);
            CHECK(limit * esp::data::kBoneReadSpreadTicks >= slotCount);
        }
        CHECK(IsBoneCoveredByReadBatch(esp::PELVIS));
        CHECK(IsBoneCoveredByReadBatch(esp::CHEST));
        CHECK(IsBoneCoveredByReadBatch(esp::FOOT_TOES_L_T));
        CHECK(IsBoneCoveredByReadBatch(esp::FOOT_TOES_R_CT));
        CHECK(IsBoneCoveredByReadBatch(esp::GUN));
        CHECK(IsBoneCoveredByReadBatch(esp::BONE_MAX - 1));
        CHECK(!IsBoneCoveredByReadBatch(esp::BONE_MAX));
        // Head/neck transforms used by Target must remain explicit; the wider
        // ESP display map is model-dependent and is validated structurally.
        CHECK(esp::AIM_NECK == 5);
        CHECK(esp::NECK == 6);
        CHECK(esp::HEAD == 7);
        CHECK(esp::PlayerStoredBoneIndex(esp::HEAD) !=
              esp::PlayerStoredBoneIndex(esp::NECK));
        CHECK(IsBonePointerReadComplete(sizeof(uintptr_t)));
        CHECK(!IsBonePointerReadComplete(sizeof(uintptr_t) - 1u));
        CHECK(esp::data::kBoneReadTransformCount == esp::BONE_MAX);
        CHECK(IsBonePoseReadComplete(esp::data::kBoneBatchBytesPerPlayer));
        CHECK(!IsBonePoseReadComplete(esp::data::kBoneBatchBytesPerPlayer - 1u));

        BoneReadBatch batch = {};
        auto storeBone = [&](int boneId, const Vector3& value) {
            std::byte* destination = nullptr;
            if (esp::data::IsBoneCoveredByReadBatch(boneId))
                destination = batch.transforms.data() +
                    static_cast<size_t>(boneId - esp::data::kBoneReadFirst) *
                        esp::data::kBoneTransformStrideBytes;
            CHECK(destination != nullptr);
            if (destination)
                std::memcpy(destination, &value, sizeof(value));
        };
        for (int i = 0; i < esp::kPlayerStoredBoneCount; ++i) {
            const float seed =
                static_cast<float>(esp::kPlayerStoredBoneIds[i]);
            storeBone(
                esp::kPlayerStoredBoneIds[i],
                {seed, seed + 0.25f, seed + 0.5f});
        }
        Vector3 unpackedBones[esp::kPlayerStoredBoneCount] = {};
        esp::data::UnpackStoredBones(batch, unpackedBones);
        for (int i = 0; i < esp::kPlayerStoredBoneCount; ++i) {
            const float seed =
                static_cast<float>(esp::kPlayerStoredBoneIds[i]);
            CHECK(unpackedBones[i].x == seed);
            CHECK(unpackedBones[i].y == seed + 0.25f);
            CHECK(unpackedBones[i].z == seed + 0.5f);
        }

        CHECK(SelectSceneNodeHoldStreak(false) == esp::data::kBoneSceneNodeHoldStreak);
        CHECK(SelectSceneNodeHoldStreak(true) == esp::data::kBoneSceneNodeBulkHoldStreak);
        CHECK(SelectBoneArrayHoldStreak(false) == esp::data::kBoneArrayHoldStreak);
        CHECK(SelectBoneArrayHoldStreak(true) == esp::data::kBoneArrayBulkHoldStreak);

        CHECK(ShouldReuseCachedPointerOnZero(4u, 4u));
        CHECK(!ShouldReuseCachedPointerOnZero(5u, 4u));
        CHECK(IsBoneSlotLive(true, false));
        CHECK(IsBoneSlotLive(false, true));
        CHECK(!IsBoneSlotLive(false, false));
        CHECK(IsBoneSlotStale(0u, 1u, false));
        CHECK(IsBoneSlotStale(1u, 0u, false));
        CHECK(IsBoneSlotStale(1u, 1u, true));
        CHECK(!IsBoneSlotStale(1u, 1u, false));

        CHECK(IsCooldownElapsed(0u, 100u, 10u));
        CHECK(IsCooldownElapsed(100u, 99u, 10u));
        CHECK(!IsCooldownElapsed(100u, 109u, 10u));
        CHECK(IsCooldownElapsed(100u, 110u, 10u));

        CHECK(!IsPerSlotBoneLocalResetDue(esp::data::kPerSlotBoneLocalResetFrames - 1u, 0u, 100u));
        CHECK(IsPerSlotBoneLocalResetDue(esp::data::kPerSlotBoneLocalResetFrames, 0u, 100u));
        CHECK(!IsPerSlotBoneLocalResetDue(
            esp::data::kPerSlotBoneLocalResetFrames,
            100u,
            100u + esp::data::kPerSlotBoneLocalResetCooldownUs - 1u));
        CHECK(IsPerSlotBoneLocalResetDue(
            esp::data::kPerSlotBoneLocalResetFrames,
            100u,
            100u + esp::data::kPerSlotBoneLocalResetCooldownUs));

        CHECK(!ShouldEscalatePerSlotBoneProbe(esp::data::kPerSlotBoneProbeEscalations - 1u));
        CHECK(ShouldEscalatePerSlotBoneProbe(esp::data::kPerSlotBoneProbeEscalations));
        CHECK(!IsPerSlotBoneProbeDue(100u, 100u + esp::data::kPerSlotBoneProbeCooldownUs - 1u));
        CHECK(IsPerSlotBoneProbeDue(100u, 100u + esp::data::kPerSlotBoneProbeCooldownUs));
    }

    void TestPopulationWatchdogPolicy()
    {
        using esp::data::IsLaunchUnderresolvedPopulation;
        using esp::data::IsLaunchUnderresolvedRefreshCooldownElapsed;
        using esp::data::IsControllerPopulationCollapsed;
        using esp::data::IsStableLowControllerGuardActive;
        using esp::data::IsPopulationGraceElapsed;
        using esp::data::IsPopulationWatchdogRefreshDue;
        using esp::data::IsStaleCommittedPopulation;
        using esp::data::IsWatchdogCooldownElapsed;
        using esp::data::LaunchUnderresolvedAction;
        using esp::data::PopulationWatchdogRefreshKind;
        using esp::data::SelectPopulationWatchdogRefreshKind;
        using esp::data::SelectLaunchUnderresolvedAction;
        using esp::data::ShouldAcceptStableLowControllerPopulation;
        using esp::data::ShouldRequestPopulationWatchdogRecovery;
        using esp::data::ShouldSoftResetStaleCommittedPopulation;

        CHECK(IsWatchdogCooldownElapsed(0u, 100u, 10u));
        CHECK(IsWatchdogCooldownElapsed(100u, 99u, 10u));
        CHECK(!IsWatchdogCooldownElapsed(100u, 109u, 10u));
        CHECK(IsWatchdogCooldownElapsed(100u, 110u, 10u));
        CHECK(!IsStableLowControllerGuardActive(0u, 100u));
        CHECK(IsStableLowControllerGuardActive(
            100u,
            99u + esp::data::kStableLowControllerGuardUs));
        CHECK(!IsStableLowControllerGuardActive(
            100u,
            100u + esp::data::kStableLowControllerGuardUs));
        CHECK(!ShouldAcceptStableLowControllerPopulation(
            100u,
            99u + esp::data::kStableLowControllerAcceptUs));
        CHECK(ShouldAcceptStableLowControllerPopulation(
            100u,
            100u + esp::data::kStableLowControllerAcceptUs));
        CHECK(IsControllerPopulationCollapsed(true, true, 10, 2, false));
        CHECK(!IsControllerPopulationCollapsed(true, true, 10, 2, true));
        CHECK(!IsControllerPopulationCollapsed(true, true, 3, 1, false));
        CHECK(!IsControllerPopulationCollapsed(true, true, 10, 0, false));
        CHECK(!IsControllerPopulationCollapsed(false, true, 10, 2, false));

        CHECK(!IsPopulationGraceElapsed(false, esp::data::kPopulationFullGraceResetAgeUs, 0u));
        CHECK(IsPopulationGraceElapsed(
            false,
            esp::data::kPopulationFullGraceResetAgeUs,
            esp::data::kPopulationFullGraceWarmupAgeUs));
        CHECK(IsPopulationGraceElapsed(true, esp::data::kPopulationKnownMapGraceUs, 0u));
        CHECK(IsPopulationGraceElapsed(true, 0u, esp::data::kPopulationKnownMapGraceUs));
        CHECK(!IsPopulationGraceElapsed(true, esp::data::kPopulationKnownMapGraceUs - 1u, 0u));

        CHECK(!IsPopulationWatchdogRefreshDue(
            100u,
            100u + esp::data::kPopulationWatchdogRefreshCooldownUs - 1u,
            esp::data::kPopulationWatchdogRefreshStreak,
            esp::data::kPopulationWatchdogRefreshAgeUs));
        CHECK(!IsPopulationWatchdogRefreshDue(
            0u,
            100u,
            esp::data::kPopulationWatchdogRefreshStreak - 1u,
            esp::data::kPopulationWatchdogRefreshAgeUs));
        CHECK(!IsPopulationWatchdogRefreshDue(
            0u,
            100u,
            esp::data::kPopulationWatchdogRefreshStreak,
            esp::data::kPopulationWatchdogRefreshAgeUs - 1u));
        CHECK(!IsStaleCommittedPopulation(true, 2, 10, 10));
        CHECK(!IsStaleCommittedPopulation(true, 8, 10, 7));
        CHECK(IsStaleCommittedPopulation(true, 2, 10, 2));
        CHECK(!IsStaleCommittedPopulation(false, 2, 10, 2));
        CHECK(IsPopulationWatchdogRefreshDue(
            0u,
            100u,
            esp::data::kPopulationWatchdogRefreshStreak,
            esp::data::kPopulationWatchdogRefreshAgeUs));

        CHECK(SelectPopulationWatchdogRefreshKind(
            esp::data::kPopulationWatchdogRefreshAgeUs) == PopulationWatchdogRefreshKind::Probe);
        CHECK(SelectPopulationWatchdogRefreshKind(
            esp::data::kPopulationWatchdogRepairAgeUs) == PopulationWatchdogRefreshKind::Repair);
        CHECK(SelectPopulationWatchdogRefreshKind(
            esp::data::kPopulationWatchdogRepairAgeUs - 1u) == PopulationWatchdogRefreshKind::Probe);
        CHECK(SelectPopulationWatchdogRefreshKind(
            esp::data::kPopulationWatchdogHardAgeUs) == PopulationWatchdogRefreshKind::Full);

        CHECK(ShouldSoftResetStaleCommittedPopulation(
            true,
            esp::data::kPopulationWatchdogStaleCommittedResetAgeUs));
        CHECK(!ShouldSoftResetStaleCommittedPopulation(
            false,
            esp::data::kPopulationWatchdogStaleCommittedResetAgeUs));
        CHECK(!ShouldSoftResetStaleCommittedPopulation(
            true,
            esp::data::kPopulationWatchdogStaleCommittedResetAgeUs - 1u));

        CHECK(ShouldRequestPopulationWatchdogRecovery(PopulationWatchdogRefreshKind::Full, 0u, 100u));
        CHECK(!ShouldRequestPopulationWatchdogRecovery(PopulationWatchdogRefreshKind::Repair, 0u, 100u));
        CHECK(!ShouldRequestPopulationWatchdogRecovery(
            PopulationWatchdogRefreshKind::Full,
            100u,
            100u + esp::data::kPopulationWatchdogHardCooldownUs - 1u));
        CHECK(ShouldRequestPopulationWatchdogRecovery(
            PopulationWatchdogRefreshKind::Full,
            100u,
            100u + esp::data::kPopulationWatchdogHardCooldownUs));

        CHECK(IsLaunchUnderresolvedPopulation(
            true,
            true,
            false,
            esp::data::kLaunchUnderresolvedMinResetAgeUs,
            64,
            1,
            1));
        CHECK(!IsLaunchUnderresolvedPopulation(
            true,
            true,
            false,
            esp::data::kLaunchUnderresolvedMinResetAgeUs - 1u,
            64,
            1,
            1));
        CHECK(!IsLaunchUnderresolvedPopulation(
            true,
            true,
            false,
            esp::data::kLaunchUnderresolvedMaxResetAgeUs + 1u,
            64,
            1,
            1));
        CHECK(IsLaunchUnderresolvedPopulation(
            true,
            false,
            true,
            esp::data::kLaunchUnderresolvedMaxResetAgeUs + 1u,
            64,
            1,
            1));
        CHECK(!IsLaunchUnderresolvedPopulation(
            true,
            false,
            true,
            esp::data::kLaunchUnderresolvedMaxResetAgeUs + 1u,
            64,
            1,
            2));
        CHECK(!IsLaunchUnderresolvedPopulation(
            true,
            false,
            false,
            esp::data::kLaunchUnderresolvedMaxResetAgeUs + 1u,
            64,
            1,
            1));
        CHECK(!IsLaunchUnderresolvedPopulation(
            true,
            true,
            false,
            esp::data::kLaunchUnderresolvedMinResetAgeUs,
            63,
            1,
            1));
        CHECK(!IsLaunchUnderresolvedPopulation(
            true,
            true,
            false,
            esp::data::kLaunchUnderresolvedMinResetAgeUs,
            64,
            2,
            1));

        CHECK(IsLaunchUnderresolvedRefreshCooldownElapsed(0u, 100u));
        CHECK(!IsLaunchUnderresolvedRefreshCooldownElapsed(
            100u,
            100u + esp::data::kLaunchUnderresolvedRefreshCooldownUs - 1u));
        CHECK(IsLaunchUnderresolvedRefreshCooldownElapsed(
            100u,
            100u + esp::data::kLaunchUnderresolvedRefreshCooldownUs));

        CHECK(SelectLaunchUnderresolvedAction(
            esp::data::kLaunchUnderresolvedProbeAgeUs - 1u,
            true,
            false,
            false,
            false,
            true) == LaunchUnderresolvedAction::None);
        CHECK(SelectLaunchUnderresolvedAction(
            esp::data::kLaunchUnderresolvedProbeAgeUs,
            true,
            false,
            false,
            false,
            true) == LaunchUnderresolvedAction::Probe);
        CHECK(SelectLaunchUnderresolvedAction(
            esp::data::kLaunchUnderresolvedRepairAgeUs,
            true,
            false,
            false,
            false,
            true) == LaunchUnderresolvedAction::Repair);
        CHECK(SelectLaunchUnderresolvedAction(
            esp::data::kLaunchUnderresolvedRepairAgeUs,
            false,
            false,
            false,
            false,
            true) == LaunchUnderresolvedAction::None);
        CHECK(SelectLaunchUnderresolvedAction(
            esp::data::kLaunchUnderresolvedRepairAgeUs,
            true,
            false,
            true,
            false,
            true) == LaunchUnderresolvedAction::None);
        CHECK(SelectLaunchUnderresolvedAction(
            esp::data::kLaunchUnderresolvedFullAgeUs,
            true,
            true,
            true,
            false,
            true) == LaunchUnderresolvedAction::Full);
        CHECK(SelectLaunchUnderresolvedAction(
            esp::data::kLaunchUnderresolvedFullAgeUs,
            true,
            true,
            true,
            false,
            false) == LaunchUnderresolvedAction::None);
        CHECK(SelectLaunchUnderresolvedAction(
            esp::data::kLaunchUnderresolvedFullAgeUs,
            true,
            true,
            true,
            true,
            true) == LaunchUnderresolvedAction::None);
    }

    void TestZeroPopulationPolicy()
    {
        using esp::data::IsZeroPopulationGraceElapsed;
        using esp::data::IsZeroPopulationRecoveryRetryCooldownElapsed;
        using esp::data::ShouldRetryZeroPopulationRecovery;
        using esp::data::ShouldRunZeroPopulationFull;
        using esp::data::ShouldRunZeroPopulationProbe;
        using esp::data::ShouldRunZeroPopulationRepair;

        CHECK(IsZeroPopulationRecoveryRetryCooldownElapsed(0u, 100u));
        CHECK(IsZeroPopulationRecoveryRetryCooldownElapsed(100u, 99u));
        CHECK(!IsZeroPopulationRecoveryRetryCooldownElapsed(
            100u,
            100u + esp::data::kZeroPopulationRecoveryRetryCooldownUs - 1u));
        CHECK(IsZeroPopulationRecoveryRetryCooldownElapsed(
            100u,
            100u + esp::data::kZeroPopulationRecoveryRetryCooldownUs));

        CHECK(!ShouldRunZeroPopulationProbe(false, 0u, std::chrono::milliseconds(300), false));
        CHECK(!ShouldRunZeroPopulationProbe(true, 1u, std::chrono::milliseconds(300), false));
        CHECK(!ShouldRunZeroPopulationProbe(true, 0u, std::chrono::milliseconds(299), false));
        CHECK(ShouldRunZeroPopulationProbe(true, 0u, std::chrono::milliseconds(299), true));
        CHECK(ShouldRunZeroPopulationProbe(true, 0u, std::chrono::milliseconds(300), false));

        CHECK(!ShouldRunZeroPopulationRepair(true, 2u, std::chrono::milliseconds(800)));
        CHECK(!ShouldRunZeroPopulationRepair(true, 1u, std::chrono::milliseconds(799)));
        CHECK(ShouldRunZeroPopulationRepair(true, 1u, std::chrono::milliseconds(800)));

        CHECK(!ShouldRunZeroPopulationFull(true, 3u, std::chrono::milliseconds(2000)));
        CHECK(!ShouldRunZeroPopulationFull(true, 2u, std::chrono::milliseconds(1999)));
        CHECK(ShouldRunZeroPopulationFull(true, 2u, std::chrono::milliseconds(2000)));

        CHECK(!ShouldRetryZeroPopulationRecovery(
            true,
            true,
            3u,
            std::chrono::milliseconds(4000),
            0u,
            100u));
        CHECK(!ShouldRetryZeroPopulationRecovery(
            true,
            false,
            2u,
            std::chrono::milliseconds(4000),
            0u,
            100u));
        CHECK(!ShouldRetryZeroPopulationRecovery(
            true,
            false,
            3u,
            std::chrono::milliseconds(3999),
            0u,
            100u));
        CHECK(ShouldRetryZeroPopulationRecovery(
            true,
            false,
            3u,
            std::chrono::milliseconds(4000),
            0u,
            100u));

        CHECK(IsZeroPopulationGraceElapsed(esp::data::kZeroPopulationGeneralGraceUs, 0u, false, false, false, false));
        CHECK(IsZeroPopulationGraceElapsed(0u, esp::data::kZeroPopulationGeneralGraceUs, false, false, false, false));
        CHECK(IsZeroPopulationGraceElapsed(esp::data::kZeroPopulationKnownMapGraceUs, 0u, true, false, false, false));
        CHECK(!IsZeroPopulationGraceElapsed(esp::data::kZeroPopulationKnownMapGraceUs - 1u, 0u, true, false, false, false));
        CHECK(IsZeroPopulationGraceElapsed(0u, 0u, false, true, false, false));
        CHECK(IsZeroPopulationGraceElapsed(0u, 0u, false, false, true, false));
        CHECK(IsZeroPopulationGraceElapsed(0u, 0u, false, false, false, true));
        CHECK(!IsZeroPopulationGraceElapsed(0u, 0u, false, false, false, false));
    }

    void TestWorldMarkerPolicy()
    {
        CHECK(esp::data::ResolveInfernoDuration(5.5f, true) == 5.5f);
        CHECK(esp::data::ResolveInfernoDuration(7.0f, true) == 7.0f);
        CHECK(esp::data::ResolveInfernoDuration(0.0f, true) == 7.0f);
        CHECK(esp::data::ResolveInfernoDuration(std::nanf(""), true) == 7.0f);
        CHECK(esp::data::ResolveInfernoDuration(5.5f, false) == 7.0f);
        CHECK(!esp::data::HasSmokeActivation(true, 0, true, 0));
        CHECK(esp::data::HasSmokeActivation(true, 1, false, 0));
        CHECK(esp::data::HasSmokeActivation(true, 255, true, 1));
        CHECK(!esp::data::HasSmokeActivation(false, 1, false, 1));
        CHECK(esp::data::CalculateUtilityRemainingFromTick(6400, 5.5f, 1.0f / 64.0f, 102.0f) == 3.5f);
        using esp::data::CalculateUtilityRemainingFromTick;
        using esp::data::ClampUtilityRemainingSeconds;
        using esp::data::IsUtilityRemainingUnknown;
        using esp::data::IsUtilityTickExpired;
        using esp::data::ShouldPreserveWorldMarker;
        using esp::data::ResolveUtilityTimer;
        using esp::data::UpdateUtilityStationaryEvidence;
        using esp::data::UtilityTimerSource;
        using esp::data::WorldMarkerDomain;

        CHECK(IsUtilityRemainingUnknown(-1.0f));
        CHECK(IsUtilityRemainingUnknown(61.0f));
        CHECK(IsUtilityRemainingUnknown(std::nanf("")));
        CHECK(!IsUtilityRemainingUnknown(0.0f));
        CHECK(!IsUtilityRemainingUnknown(60.0f));

        CHECK(ClampUtilityRemainingSeconds(std::nanf("")) == -1.0f);
        CHECK(ClampUtilityRemainingSeconds(-1.0f) == 0.0f);
        CHECK(ClampUtilityRemainingSeconds(61.0f) == -1.0f);
        CHECK(ClampUtilityRemainingSeconds(10.0f) == 10.0f);

        CHECK(CalculateUtilityRemainingFromTick(10, 18.0f, 0.1f, 1.0f) == 18.0f);
        CHECK(CalculateUtilityRemainingFromTick(0, 18.0f, 0.1f, 1.0f) == -1.0f);
        CHECK(CalculateUtilityRemainingFromTick(10, 18.0f, 0.0f, 1.0f) == -1.0f);
        CHECK(CalculateUtilityRemainingFromTick(10, 18.0f, 0.1f, 22.1f) == -1.0f);
        CHECK(!IsUtilityTickExpired(10, 18.0f, 0.1f, 18.9f));
        CHECK(IsUtilityTickExpired(10, 18.0f, 0.1f, 19.0f));
        CHECK(IsUtilityTickExpired(10, 18.0f, 0.1f, 40.0f));
        CHECK(!IsUtilityTickExpired(0, 18.0f, 0.1f, 40.0f));


        CHECK(ShouldPreserveWorldMarker(WorldMarkerDomain::ActiveUtility, true, false, false));
        CHECK(!ShouldPreserveWorldMarker(WorldMarkerDomain::ActiveUtility, false, false, true));
        CHECK(ShouldPreserveWorldMarker(WorldMarkerDomain::DroppedItem, true, false, false));
        CHECK(!ShouldPreserveWorldMarker(WorldMarkerDomain::DroppedBomb, true, false, false));
        CHECK(!ShouldPreserveWorldMarker(WorldMarkerDomain::DroppedBomb, false, true, false));

        uint64_t lastSampleUs = 0;
        uint64_t stationarySinceUs = 0;
        uint8_t stationarySamples = 0;
        CHECK(!UpdateUtilityStationaryEvidence(
            true, false, 100000u, lastSampleUs, stationarySinceUs, stationarySamples));
        CHECK(UpdateUtilityStationaryEvidence(
            true, true, 150000u, lastSampleUs, stationarySinceUs, stationarySamples));
        CHECK(stationarySinceUs == 100000u);
        CHECK(stationarySamples == 2u);
        CHECK(!UpdateUtilityStationaryEvidence(
            true, false, 200000u, lastSampleUs, stationarySinceUs, stationarySamples));
        CHECK(stationarySinceUs == 200000u);
        CHECK(!UpdateUtilityStationaryEvidence(
            false, false, 600001u, lastSampleUs, stationarySinceUs, stationarySamples));

        const auto tickTimer = ResolveUtilityTimer(
            true, false, true, 6.5f, false, true, true, 100000u, 200000u, 18.0f);
        CHECK(tickTimer.active);
        CHECK(!tickTimer.terminal);
        CHECK(tickTimer.remainingSec == 6.5f);
        CHECK(tickTimer.source == UtilityTimerSource::GameTick);

        const auto effectTimer = ResolveUtilityTimer(
            true, false, false, -1.0f, true, true, false, 0u, 200000u, 18.0f);
        CHECK(effectTimer.active);
        CHECK(effectTimer.source == UtilityTimerSource::EffectState);

        const auto stationaryTimer = ResolveUtilityTimer(
            true, false, false, -1.0f, false, true, true, 100000u, 2100000u, 18.0f);
        CHECK(stationaryTimer.active);
        CHECK(stationaryTimer.source == UtilityTimerSource::StationaryFallback);
        CHECK(stationaryTimer.remainingSec == 16.0f);

        const auto heldGrenadeTimer = ResolveUtilityTimer(
            true, false, false, -1.0f, false, false, true, 100000u, 2100000u, 18.0f);
        CHECK(!heldGrenadeTimer.active);
        CHECK(!heldGrenadeTimer.terminal);
        CHECK(heldGrenadeTimer.source == UtilityTimerSource::None);

        const auto unclassifiedEffectTimer = ResolveUtilityTimer(
            false, false, true, 6.5f, true, true, true, 100000u, 200000u, 18.0f);
        CHECK(!unclassifiedEffectTimer.active);
        CHECK(!unclassifiedEffectTimer.terminal);

        const auto terminalTimer = ResolveUtilityTimer(
            true, true, true, 5.0f, true, true, true, 100000u, 200000u, 18.0f);
        CHECK(!terminalTimer.active);
        CHECK(terminalTimer.terminal);
    }

    void TestWorldPublicationAndUtilityReadGaps()
    {
        using namespace esp::data;
        struct Marker {
            bool valid = false;
            uint64_t expiresUs = 0;
            int tier = 3;
            int id = 0;
        };
        struct GuardedDestination {
            uint64_t before = 0x12345678u;
            Marker markers[256]{};
            uint64_t after = 0xABCDEF12u;
        } destination;
        Marker source[512]{};
        for (int i = 0; i < 512; ++i)
            source[i] = { true, 9000000u, i < 384 ? 3 : 1, i };
        const auto priority = [](const Marker& marker) { return marker.tier; };
        CHECK(PublishBoundedWorldMarkers(source, std::size(source), 512,
            destination.markers, 1000000u, priority) == 256);
        CHECK(destination.before == 0x12345678u && destination.after == 0xABCDEF12u);
        for (int i = 0; i < 128; ++i) {
            CHECK(destination.markers[i].id == i + 384);
            CHECK(destination.markers[i + 128].id == i);
        }
        // Hostile counts cannot escape either source or destination capacity.
        CHECK(PublishBoundedWorldMarkers(source, 1, INT_MAX,
            destination.markers, 1000000u, priority) == 1);
        CHECK(!destination.markers[1].valid && !destination.markers[255].valid);
        CHECK(PublishBoundedWorldMarkers(source, std::size(source), -1,
            destination.markers, 1000000u, priority) == 0);
        CHECK(ClampWorldMarkerCount(-3, 256) == 0);
        CHECK(ClampWorldMarkerCount(INT_MAX, 256) == 256);
        CHECK(IsWorldMarkerSourceFresh(1000000u, 1350000u));
        CHECK(!IsWorldMarkerSourceFresh(1000000u, 1350001u));
        CHECK(!IsWorldMarkerSourceFresh(0u, 1u));
        CHECK(!IsWorldMarkerSourceFresh(1000000u, 999999u));
        CHECK(ShouldProcessDroppedItem(true, false, true));
        CHECK(!ShouldProcessDroppedItem(true, false, false));
        CHECK(ShouldProcessDroppedItem(true, true, false));
        CHECK(ShouldProcessDroppedItem(false, false, false));
        source[0].expiresUs = 1000000u;
        source[1].expiresUs = 0;
        source[2].valid = false;
        source[3].tier = 0;
        CHECK(PublishBoundedWorldMarkers(source, 4, 4,
            destination.markers, 1000000u, priority) == 2);
        CHECK(destination.markers[0].id == 3 && destination.markers[1].id == 1);
        CHECK(PublishBoundedWorldMarkers(static_cast<const Marker*>(nullptr), 512, 512,
            destination.markers, 1000000u, priority) == 0);
        Marker saturated[kWorldMarkerScratchCapacity]{};
        int total = 0;
        for (int tier : { 3, 2, 1, 0 }) {
            const int quota = tier == 3 ? kMaxDroppedWeaponMarkers :
                tier == 2 ? kMaxProjectileMarkers :
                tier == 1 ? kMaxUtilityEffectMarkers : kMaxDroppedBombMarkers;
            for (int i = 0; i < quota; ++i) {
                saturated[total] = { true, 9000000u, tier, total };
                ++total;
            }
        }
        CHECK(total == kWorldMarkerScratchCapacity);
        CHECK(PublishBoundedWorldMarkers(saturated, std::size(saturated), total,
            destination.markers, 1000000u, priority) == 256);
        int tierCounts[4]{};
        for (const Marker& marker : destination.markers)
            ++tierCounts[marker.tier];
        CHECK(tierCounts[0] == kMaxDroppedBombMarkers);
        CHECK(tierCounts[1] == kMaxUtilityEffectMarkers);
        CHECK(tierCounts[2] == kMaxProjectileMarkers);
        CHECK(destination.before == 0x12345678u && destination.after == 0xABCDEF12u);

        UtilityFieldTimes samples{};
        CHECK(!UpdateUtilityFieldFreshness(samples, UtilityField::SmokeTick, false, 1000000u));
        CHECK(UpdateUtilityFieldFreshness(samples, UtilityField::SmokeTick, true, 1000000u));
        CHECK(UpdateUtilityFieldFreshness(samples, UtilityField::SmokeTick, false, 1350000u));
        CHECK(UpdateUtilityFieldFreshness(samples, UtilityField::Velocity, true, 1350001u));
        CHECK(!UpdateUtilityFieldFreshness(samples, UtilityField::SmokeTick, false, 1350001u));
        CHECK(samples[static_cast<size_t>(UtilityField::SmokeTick)] == 1000000u);
        CHECK(!UpdateUtilityFieldFreshness(samples, UtilityField::Velocity, false, 1300000u));
        samples = {};
        CHECK(!UpdateUtilityFieldFreshness(samples, UtilityField::Velocity, false, 1400000u));

        uint64_t deadlineUs = 0;
        CHECK(UpdateUtilityMarkerDeadline(true, false, 6.0f, 1000000u,
            kSmokeFallbackUs, deadlineUs) == 7000000u);
        CHECK(UpdateUtilityMarkerDeadline(false, false, -1.0f, 2000000u,
            kSmokeFallbackUs, deadlineUs) == 7000000u);
        CHECK(UpdateUtilityMarkerDeadline(true, false, -1.0f, 3000000u,
            kSmokeFallbackUs, deadlineUs) == 7000000u);
        CHECK(UpdateUtilityMarkerDeadline(true, false, 16.0f, 3000000u,
            kSmokeFallbackUs, deadlineUs, UtilityTimerSource::StationaryFallback) == 7000000u);
        CHECK(UpdateUtilityMarkerDeadline(true, false, -1.0f, 7000000u,
            kSmokeFallbackUs, deadlineUs) == 0u);
        CHECK(UpdateUtilityMarkerDeadline(true, false, -1.0f, 8000000u,
            kSmokeFallbackUs, deadlineUs) == 0u);
        CHECK(deadlineUs == 7000000u);
        CHECK(UpdateUtilityMarkerDeadline(true, false, 10.0f, 8000000u,
            kSmokeFallbackUs, deadlineUs, UtilityTimerSource::StationaryFallback) == 0u);
        deadlineUs = 0; // A new entity/scene may start a new lifetime.
        CHECK(UpdateUtilityMarkerDeadline(true, false, -1.0f, 9000000u,
            kSmokeFallbackUs, deadlineUs) == 27000000u);
        CHECK(UpdateUtilityMarkerDeadline(true, false, 2.0f, 10000000u,
            kSmokeFallbackUs, deadlineUs) == 12000000u);
        CHECK(UpdateUtilityMarkerDeadline(true, true, 2.0f, 11000000u,
            kSmokeFallbackUs, deadlineUs) == 0u);
        CHECK(UpdateUtilityMarkerDeadline(true, false, std::nanf(""), 11500000u,
            kSmokeFallbackUs, deadlineUs) == 0u);
        // Only a valid game-tick measurement may move an established bound later.
        CHECK(UpdateUtilityMarkerDeadline(true, false, 2.0f, 12000000u,
            kSmokeFallbackUs, deadlineUs, UtilityTimerSource::GameTick) == 14000000u);

        CHECK(ShouldHoldWorldMarkerOnReadGap(0x1000u, true, false,
            0u, false, 1000000u, 1350000u));
        CHECK(!ShouldHoldWorldMarkerOnReadGap(0x1000u, true, false,
            0u, false, 1000000u, 1350001u));
        CHECK(!ShouldHoldWorldMarkerOnReadGap(0x1000u, true, true,
            0u, false, 1000000u, 1100000u));
        CHECK(!ShouldHoldWorldMarkerOnReadGap(0x1000u, true, true,
            0x2000u, false, 1000000u, 1100000u));
        CHECK(ShouldHoldWorldMarkerOnReadGap(0x1000u, true, true,
            0x1000u, false, 1000000u, 1100000u));
        CHECK(!ShouldHoldWorldMarkerOnReadGap(0x1000u, true, true,
            0x1000u, true, 1000000u, 1100000u));
        CHECK(!ShouldHoldWorldMarkerOnReadGap(0x1000u, false, false,
            0x1000u, false, 1000000u, 999999u));
        CHECK(!ShouldHoldWorldMarkerOnReadGap(0u, false, false,
            0u, false, 1000000u, 1100000u));

        const float infinity = std::numeric_limits<float>::infinity();
        CHECK(!IsUtilityTickExpired(64, 18.0f, 1.0f / 64.0f, infinity));
        CHECK(!IsUtilityTickExpired(64, 18.0f, infinity, 20.0f));
        const auto invalidTick = ResolveUtilityTimer(true, false, true,
            std::nanf(""), true, false, false, 0u, 1000000u, 18.0f);
        CHECK(invalidTick.active && !invalidTick.terminal);
        CHECK(invalidTick.source == UtilityTimerSource::EffectState);
    }

    void TestWorldDomainPolicy()
    {
        // Alternating discovery/refresh passes used to repeatedly discover
        // shard zero: both passes advanced the cursor. Every shard must now
        // be reached even with arbitrary extra refresh-only work in between.
        for (uint32_t shards = 2; shards <= 10; ++shards) {
            std::array<bool, 10> visited = {};
            uint32_t shard = 0;
            for (uint32_t pass = 0; pass < shards; ++pass) {
                visited[shard] = true;
                shard = esp::data::NextWorldDiscoveryShard(shard, shards, true);
                const auto afterDiscovery = shard;
                for (int refresh = 0; refresh < 3; ++refresh)
                    shard = esp::data::NextWorldDiscoveryShard(shard, shards, false);
                CHECK(shard == afterDiscovery);
            }
            for (uint32_t i = 0; i < shards; ++i) CHECK(visited[i]);
        }
        CHECK(esp::data::NextWorldDiscoveryShard(7, 0, true) == 0);
        CHECK(esp::data::CanonicalWorldItemId(49, esp::data::WorldEntityClass::Inferno) == 46);
        CHECK(esp::data::CanonicalWorldItemId(65535, esp::data::WorldEntityClass::SmokeProjectile) == 45);
        CHECK(esp::data::CanonicalWorldItemId(48, esp::data::WorldEntityClass::MolotovProjectile) == 48);
        CHECK(esp::data::CanonicalWorldItemId(49, esp::data::WorldEntityClass::DroppedWeapon) == 49);
        const char partialName[48] = "inferno";
        CHECK(esp::data::ReadWorldDesignerName(partialName, 48, 8) == "inferno");
        CHECK(esp::data::ReadWorldDesignerName(partialName, 48, 7).empty());
        CHECK(esp::data::ReadWorldDesignerName(partialName, 48, 0).empty());
        CHECK(esp::data::ReadWorldDesignerName(nullptr, 48, 8).empty());
        using esp::data::CalculateWorldDomainCadence;
        using esp::data::CalculateWorldScanIntervalUs;
        using esp::data::ClassifyWorldDesignerName;
        using esp::data::ShouldRetainWorldEntity;
        using esp::data::AreWorldBasicDetailsComplete;
        using esp::data::HasUsableWorldStructuralReads;
        using esp::data::IsWorldFieldReadComplete;
        using esp::data::IsWorldBombRescueScanAllowed;
        using esp::data::IsWorldScanAllowed;
        using esp::data::ShouldRunScheduledWorldScan;
        using esp::data::ShouldReadWorldItemDefinition;
        using esp::data::ShouldRetryWorldIdentity;
        using esp::data::UtilityItemIdFromWorldClass;
        using esp::data::ShouldForceBombWorldDiscovery;
        using esp::data::ShouldUseWorldIdleDiscovery;
        using esp::data::WorldEntityClass;
        using esp::data::WorldOwnerEvidence;
        using esp::data::ResolveWorldOwnerEvidence;
        using esp::data::IsWorldOwnerEvidenceFresh;

        CHECK(ClassifyWorldDesignerName("smokegrenade_projectile") == WorldEntityClass::SmokeProjectile);
        CHECK(ClassifyWorldDesignerName("SMOKEGRENADE_PROJECTILE") == WorldEntityClass::SmokeProjectile);
        CHECK(ClassifyWorldDesignerName("inferno") == WorldEntityClass::Inferno);
        CHECK(ClassifyWorldDesignerName("decoy_projectile") == WorldEntityClass::DecoyProjectile);
        CHECK(ClassifyWorldDesignerName("hegrenade_projectile") == WorldEntityClass::HeProjectile);
        CHECK(ClassifyWorldDesignerName("molotov_projectile") == WorldEntityClass::MolotovProjectile);
        CHECK(ClassifyWorldDesignerName("incendiarygrenade_projectile") == WorldEntityClass::MolotovProjectile);
        CHECK(ClassifyWorldDesignerName("incendiarygrenade_proj") == WorldEntityClass::MolotovProjectile);
        CHECK(ClassifyWorldDesignerName("flashbang_projectile") == WorldEntityClass::FlashProjectile);
        CHECK(ClassifyWorldDesignerName("C_SmokeGrenadeProjectile") == WorldEntityClass::SmokeProjectile);
        CHECK(ClassifyWorldDesignerName("client::C_Inferno") == WorldEntityClass::Inferno);
        CHECK(ClassifyWorldDesignerName("prefix_decoy_projectile_suffix") == WorldEntityClass::DecoyProjectile);
        CHECK(ClassifyWorldDesignerName("C_MolotovProjectile") == WorldEntityClass::MolotovProjectile);
        CHECK(ClassifyWorldDesignerName("weapon_ak47") == WorldEntityClass::DroppedWeapon);

        CHECK(!ShouldRetainWorldEntity(
            true, true, true,
            false, false, false,
            WorldEntityClass::Unknown,
            false,
            false));
        CHECK(ShouldRetainWorldEntity(
            true, false, false,
            true, false, false,
            WorldEntityClass::Unknown,
            false,
            false));
        CHECK(!ShouldRetainWorldEntity(
            false, true, false,
            false, true, false,
            WorldEntityClass::Unknown,
            false,
            false));
        CHECK(ShouldRetainWorldEntity(
            false, true, false,
            false, true, false,
            WorldEntityClass::DroppedWeapon,
            false,
            false));
        CHECK(ShouldRetainWorldEntity(
            false, false, true,
            false, false, false,
            WorldEntityClass::SmokeProjectile,
            false,
            false));
        CHECK(ShouldRetainWorldEntity(
            false, false, true,
            false, false, false,
            WorldEntityClass::Unknown,
            true,
            false));
        CHECK(!ShouldRetainWorldEntity(
            false, false, false,
            true, true, true,
            WorldEntityClass::Inferno,
            true,
            true));
        CHECK(esp::data::kFirstWorldEntitySlot == 64);
        CHECK(ShouldReadWorldItemDefinition(true, false, false));
        CHECK(ShouldReadWorldItemDefinition(false, true, false));
        CHECK(ShouldReadWorldItemDefinition(false, false, true));
        CHECK(!ShouldReadWorldItemDefinition(false, false, false));
        CHECK(ShouldRetryWorldIdentity(
            true, 0, WorldEntityClass::Unknown, false, 0u, 100u, 5000000u));
        CHECK(!ShouldRetryWorldIdentity(
            true, 7, WorldEntityClass::Unknown, false, 0u, 100u, 5000000u));
        CHECK(ShouldRetryWorldIdentity(
            true, 45, WorldEntityClass::Unknown, true, 0u, 100u, 5000000u));
        CHECK(!ShouldRetryWorldIdentity(
            true, 0, WorldEntityClass::SmokeProjectile, true, 0u, 100u, 5000000u));
        CHECK(!ShouldRetryWorldIdentity(
            true, 0, WorldEntityClass::Unknown, false, 100u, 4999999u, 5000000u));
        CHECK(ShouldRetryWorldIdentity(
            true, 0, WorldEntityClass::Unknown, false, 100u, 5000100u, 5000000u));
        CHECK(ShouldRetryWorldIdentity(
            true, 0, WorldEntityClass::Unknown, false, 500u, 100u, 5000000u));
        CHECK(UtilityItemIdFromWorldClass(WorldEntityClass::SmokeProjectile) == 45);
        CHECK(UtilityItemIdFromWorldClass(WorldEntityClass::Inferno) == 46);
        CHECK(UtilityItemIdFromWorldClass(WorldEntityClass::DecoyProjectile) == 47);
        CHECK(UtilityItemIdFromWorldClass(WorldEntityClass::Unknown) == 0);
        CHECK(ClassifyWorldDesignerName("weapon_ak47") == WorldEntityClass::DroppedWeapon);
        CHECK(!IsWorldUtilityClass(WorldEntityClass::DroppedWeapon));

        CHECK(IsWorldFieldReadComplete(sizeof(uintptr_t), sizeof(uintptr_t)));
        CHECK(!IsWorldFieldReadComplete(sizeof(uintptr_t) - 1u, sizeof(uintptr_t)));
        CHECK(!IsWorldFieldReadComplete(sizeof(uintptr_t), 0u));
        CHECK(HasUsableWorldStructuralReads(true, 0u));
        CHECK(HasUsableWorldStructuralReads(false, 1u));
        CHECK(!HasUsableWorldStructuralReads(false, 0u));
        CHECK(AreWorldBasicDetailsComplete(true, true));
        CHECK(!AreWorldBasicDetailsComplete(false, true));
        CHECK(!AreWorldBasicDetailsComplete(true, false));

        CHECK(ResolveWorldOwnerEvidence(true, true, 0u) == WorldOwnerEvidence::Dropped);
        CHECK(ResolveWorldOwnerEvidence(true, true, UINT32_MAX) == WorldOwnerEvidence::Dropped);
        CHECK(ResolveWorldOwnerEvidence(true, true, 0x1234u) == WorldOwnerEvidence::Held);
        CHECK(ResolveWorldOwnerEvidence(true, false, 0u) == WorldOwnerEvidence::Unknown);
        CHECK(ResolveWorldOwnerEvidence(false, true, 0u) == WorldOwnerEvidence::Unknown);
        CHECK(IsWorldOwnerEvidenceFresh(WorldOwnerEvidence::Held, 1000u, 2000u));
        CHECK(!IsWorldOwnerEvidenceFresh(
            WorldOwnerEvidence::Held,
            1000u,
            1000u + esp::data::kWorldOwnerEvidenceHoldUs + 1u));

        CHECK(!IsWorldScanAllowed(false, 3000000u));
        CHECK(!IsWorldScanAllowed(true, 2000000u));
        CHECK(IsWorldScanAllowed(true, 2000001u));
        CHECK(!IsWorldBombRescueScanAllowed(true, 300000u));
        CHECK(IsWorldBombRescueScanAllowed(true, 300001u));

        CHECK(CalculateWorldScanIntervalUs(800) == 50000u);
        CHECK(CalculateWorldScanIntervalUs(1200) == 70000u);
        CHECK(CalculateWorldScanIntervalUs(2000) == 90000u);
        CHECK(CalculateWorldScanIntervalUs(2001) == 120000u);

        CHECK(ShouldUseWorldIdleDiscovery(false, true, 3u, 0u, 0u));
        CHECK(!ShouldUseWorldIdleDiscovery(true, true, 3u, 0u, 0u));
        CHECK(!ShouldUseWorldIdleDiscovery(false, true, 2u, 0u, 0u));
        CHECK(!ShouldUseWorldIdleDiscovery(false, true, 3u, 1u, 0u));
        CHECK(!ShouldRunScheduledWorldScan(false, false, false, false));
        CHECK(ShouldRunScheduledWorldScan(true, false, false, false));
        CHECK(!ShouldRunScheduledWorldScan(true, false, true, false));
        CHECK(ShouldRunScheduledWorldScan(true, true, true, false));
        CHECK(ShouldRunScheduledWorldScan(true, false, true, true));
        CHECK(!ShouldForceBombWorldDiscovery(
            false, false, false, 0u, 1000u));
        CHECK(ShouldForceBombWorldDiscovery(
            true, false, true, 900u, 1000u));
        CHECK(ShouldForceBombWorldDiscovery(
            true, true, false, 900u, 1000u));
        CHECK(!ShouldForceBombWorldDiscovery(
            true, true, true, 900u, 1000u));
        CHECK(ShouldForceBombWorldDiscovery(
            true,
            true,
            true,
            1000u,
            1000u + esp::data::kWorldBombFullDiscoverySafetyUs));

        {
            const auto cadence = CalculateWorldDomainCadence(1000, true, false, false, 2u);
            CHECK(cadence.scanIntervalUs == 70000u);
            CHECK(cadence.utilityDetailIntervalUs == 70000u);
            CHECK(cadence.utilityProbeIntervalUs == 150000u);
            CHECK(cadence.discoveryShardCount == 3u);
            CHECK(cadence.droppedItemsIntervalUs == 70000u);
            CHECK(cadence.activeUtilityIntervalUs == 70000u);
            CHECK(cadence.slowDiscoveryIntervalUs == 70000u);
            CHECK(cadence.bombRescueIntervalUs == 70000u);
        }

        {
            const auto cadence = CalculateWorldDomainCadence(1800, false, false, true, 2u);
            CHECK(cadence.scanIntervalUs == 90000u);
            CHECK(cadence.utilityDetailIntervalUs == 180000u);
            CHECK(cadence.utilityProbeIntervalUs == 260000u);
            CHECK(cadence.discoveryShardCount == 8u);
            CHECK(cadence.droppedItemsIntervalUs == 90000u);
            CHECK(cadence.activeUtilityIntervalUs == 180000u);
            CHECK(cadence.slowDiscoveryIntervalUs == 180000u);
            CHECK(cadence.bombRescueIntervalUs == 90000u);
        }

        {
            const auto cadence = CalculateWorldDomainCadence(2500, true, true, false, 0u);
            CHECK(cadence.discoveryShardCount == 1u);
            CHECK(cadence.bombRescueIntervalUs == 30000u);
        }
    }

    void TestBombPolicy()
    {
        using esp::data::ShouldHoldPlantedC4Entity;
        CHECK(ShouldHoldPlantedC4Entity(true, 0, 100, 200, false, 1000, 1001));
        CHECK(ShouldHoldPlantedC4Entity(true, 100, 100, 200, false, 1000, 251000));
        CHECK(!ShouldHoldPlantedC4Entity(true, 0, 100, 200, false, 1000, 251001));
        CHECK(!ShouldHoldPlantedC4Entity(true, 101, 100, 200, false, 1000, 1001));
        CHECK(!ShouldHoldPlantedC4Entity(false, 0, 100, 200, false, 1000, 1001));
        CHECK(!ShouldHoldPlantedC4Entity(true, 0, 100, 200, true, 1000, 1001));
        CHECK(!ShouldHoldPlantedC4Entity(true, 0, 100, 0, false, 1000, 1001));
        CHECK(!ShouldHoldPlantedC4Entity(true, 0, 0, 200, false, 1000, 1001));
        CHECK(!ShouldHoldPlantedC4Entity(true, 0, 100, 200, false, 0, 1001));
        CHECK(!ShouldHoldPlantedC4Entity(true, 0, 100, 200, false, 1000, 999));
        using esp::data::IsAuthoritativeBombTimer;
        using esp::data::IsAuthoritativeDetachedWeaponC4;
        using esp::data::IsAuthoritativeDefuseTimer;
        using esp::data::IsBombFieldReadComplete;
        using esp::data::IsBombInventorySamplePastDropEdge;
        using esp::data::IsBombCooldownElapsed;
        using esp::data::IsBombMetadataFresh;
        using esp::data::IsBombRulesExitConfirmed;
        using esp::data::IsCachedBombCarryEvidenceFresh;
        using esp::data::IsDefusingPawnCandidate;
        using esp::data::IsDistinctContinuousC4Sample;
        using esp::data::IsDroppedC4EntityCacheFresh;
        using esp::data::IsFreshLiveBombCycleMetadata;
        using esp::data::IsFreshInventoryC4CarrierEvidence;
        using esp::data::HasRulesCompatibleCarryEvidence;
        using esp::data::HasStrictCurrentCarryEvidence;
        using esp::data::HasStrictInventoryCarryEvidence;
        using esp::data::IsLivePlantedC4;
        using esp::data::IsPlausiblePlantedC4Metadata;
        using esp::data::IsPlantedC4Terminal;
        using esp::data::IsResolvedBombCarrierFresh;
        using esp::data::IsStickyBombEvidenceFresh;
        using esp::data::IsWeaponC4DynamicReadDue;
        using esp::data::IsWeaponC4OwnerAttachTransition;
        using esp::data::IsWeaponC4PositionSampleCurrent;
        using esp::data::IsWeaponC4ProbeDue;
        using esp::data::IsUnconfirmedDroppedC4PublicationAllowed;
        using esp::data::IsValidWeaponPickableSample;
        using esp::data::NewestFirstC4ListIndex;
        using esp::data::ScoreWorldDroppedC4Candidate;
        using esp::data::SelectNewestC4DropCandidateIndex;
        using esp::data::ShouldReplaceWorldC4Candidate;
        using esp::data::ShouldFreshCarryOverrideDetachedWeaponC4;
        using esp::data::ShouldInvalidateChangedDropCandidateCache;
        using esp::data::ShouldAdvanceBombTerminalEpoch;
        using esp::data::SelectConfirmedDroppedStickyUs;
        using esp::data::SelectDroppedBombStickyUs;
        using esp::data::SelectStableGameTimeCandidate;
        using esp::data::SelectStableBombRuleSignal;
        using esp::data::SelectValidatedWeaponC4Entity;
        using esp::data::SelectWeaponC4DynamicRefreshUs;
        using esp::data::SelectWorldC4StickyUs;
        using esp::data::ShouldCarryOverrideDroppedBomb;
        using esp::data::ShouldDeferBombReadForBusyLane;
        using esp::data::ShouldDeferWeaponC4Probe;
        using esp::data::ShouldExpireCachedBombCarryOwner;
        using esp::data::ShouldExpireResolvedBombCarrier;
        using esp::data::ShouldHoldTransientBombTimer;
        using esp::data::ShouldHoldTransientDefuseTimer;
        using esp::data::ShouldUsePlantedC4PositionFallback;
        using esp::data::ShouldWorldC4ReplaceWeaponPosition;
        using esp::data::IsWeaponDropTickAdvance;

        CHECK(IsBombCooldownElapsed(0u, 100u, 10u));
        CHECK(IsBombCooldownElapsed(100u, 99u, 10u));
        CHECK(!IsBombCooldownElapsed(100u, 109u, 10u));
        CHECK(IsBombCooldownElapsed(100u, 110u, 10u));
        CHECK(!IsBombRulesExitConfirmed(false, 100u, 100u + esp::data::kBombRulesExitConfirmUs));
        CHECK(!IsBombRulesExitConfirmed(true, 0u, 100u + esp::data::kBombRulesExitConfirmUs));
        CHECK(!IsBombRulesExitConfirmed(
            true,
            100u,
            99u + esp::data::kBombRulesExitConfirmUs));
        CHECK(IsBombRulesExitConfirmed(
            true,
            100u,
            100u + esp::data::kBombRulesExitConfirmUs));
        CHECK(IsFreshLiveBombCycleMetadata(true, false, 110.0f, 100.0f));
        CHECK(!IsFreshLiveBombCycleMetadata(false, false, 110.0f, 100.0f));
        CHECK(!IsFreshLiveBombCycleMetadata(true, true, 110.0f, 100.0f));
        CHECK(!IsFreshLiveBombCycleMetadata(true, false, 100.0f, 100.0f));
        CHECK(ShouldAdvanceBombTerminalEpoch(true, false, true));
        CHECK(!ShouldAdvanceBombTerminalEpoch(false, false, true));
        CHECK(!ShouldAdvanceBombTerminalEpoch(true, true, true));
        CHECK(!ShouldAdvanceBombTerminalEpoch(true, false, false));
        {
            auto signal = SelectStableBombRuleSignal(true, false, 0u);
            CHECK(signal.value && signal.falseStreak == 0u);
            signal = SelectStableBombRuleSignal(false, signal.value, signal.falseStreak);
            CHECK(signal.value && signal.falseStreak == 1u);
            signal = SelectStableBombRuleSignal(false, signal.value, signal.falseStreak);
            CHECK(signal.value && signal.falseStreak == 2u);
            signal = SelectStableBombRuleSignal(false, signal.value, signal.falseStreak);
            CHECK(!signal.value && signal.falseStreak == 0u);
            signal = SelectStableBombRuleSignal(false, signal.value, signal.falseStreak);
            CHECK(!signal.value && signal.falseStreak == 0u);
        }

        CHECK(IsResolvedBombCarrierFresh(1, 100u, 100u + esp::data::kResolvedBombCarrierFreshUs));
        CHECK(!IsResolvedBombCarrierFresh(-1, 100u, 100u));
        CHECK(!IsResolvedBombCarrierFresh(1, 0u, 100u));
        CHECK(!IsResolvedBombCarrierFresh(1, 100u, 100u + esp::data::kResolvedBombCarrierFreshUs + 1u));
        CHECK(!ShouldExpireResolvedBombCarrier(100u, 100u + esp::data::kResolvedBombCarrierExpireUs));
        CHECK(ShouldExpireResolvedBombCarrier(100u, 100u + esp::data::kResolvedBombCarrierExpireUs + 1u));

        CHECK(IsCachedBombCarryEvidenceFresh(1, 100u, 100u + esp::data::kCachedBombCarryEvidenceUs));
        CHECK(!IsCachedBombCarryEvidenceFresh(1, 100u, 100u + esp::data::kCachedBombCarryEvidenceUs + 1u));
        CHECK(!IsCachedBombCarryEvidenceFresh(-1, 100u, 100u));
        CHECK(!ShouldExpireCachedBombCarryOwner(0u, 100u));
        CHECK(!ShouldExpireCachedBombCarryOwner(100u, 100u + esp::data::kCachedBombCarryOwnerExpireUs));
        CHECK(ShouldExpireCachedBombCarryOwner(100u, 101u + esp::data::kCachedBombCarryOwnerExpireUs));

        CHECK(ShouldDeferWeaponC4Probe(false, false, false, true, false, false, false));
        CHECK(ShouldDeferWeaponC4Probe(false, false, false, false, true, false, false));
        CHECK(ShouldDeferWeaponC4Probe(false, true, true, false, false, false, true));
        CHECK(!ShouldDeferWeaponC4Probe(true, false, false, true, false, false, false));
        CHECK(!ShouldDeferWeaponC4Probe(false, true, false, true, false, false, false));
        CHECK(!ShouldDeferWeaponC4Probe(false, false, false, false, false, false, false));
        CHECK(!ShouldDeferWeaponC4Probe(false, false, false, true, false, true, false));

        CHECK(!IsWeaponC4ProbeDue(false, 100u, 100u + esp::data::kWeaponC4ProbeUrgentCooldownUs - 1u));
        CHECK(IsWeaponC4ProbeDue(false, 100u, 100u + esp::data::kWeaponC4ProbeUrgentCooldownUs));
        CHECK(IsWeaponC4ProbeDue(true, 0u, 100u));
        CHECK(!IsWeaponC4ProbeDue(true, 100u, 100u + esp::data::kWeaponC4ProbeCooldownUs - 1u));
        CHECK(IsWeaponC4ProbeDue(true, 100u, 100u + esp::data::kWeaponC4ProbeCooldownUs));
        CHECK(IsDroppedC4EntityCacheFresh(
            true, false, false, 0x1000u, 100u,
            99u + esp::data::kWeaponC4ProbeUrgentCooldownUs));
        CHECK(!IsDroppedC4EntityCacheFresh(
            true, false, false, 0x1000u, 100u,
            100u + esp::data::kWeaponC4ProbeUrgentCooldownUs));
        CHECK(!IsDroppedC4EntityCacheFresh(
            true, true, false, 0x1000u, 100u, 101u));
        CHECK(!IsDroppedC4EntityCacheFresh(
            true, false, true, 0x1000u, 100u, 101u));
        CHECK(!IsDroppedC4EntityCacheFresh(
            false, false, false, 0x1000u, 100u, 101u));

        CHECK(SelectWeaponC4DynamicRefreshUs(true, false, false) == esp::data::kWeaponC4DynamicDroppedRefreshUs);
        CHECK(SelectWeaponC4DynamicRefreshUs(false, true, true) == esp::data::kWeaponC4DynamicDroppedRefreshUs);
        CHECK(SelectWeaponC4DynamicRefreshUs(false, false, true) == esp::data::kWeaponC4DynamicCarriedRefreshUs);
        CHECK(SelectWeaponC4DynamicRefreshUs(false, false, false) == esp::data::kWeaponC4DynamicUnknownRefreshUs);
        CHECK(ShouldDeferBombReadForBusyLane(true, false, false, true, 40000u, 40000u));
        CHECK(ShouldDeferBombReadForBusyLane(true, true, false, true, 40000u, 40000u));
        CHECK(ShouldDeferBombReadForBusyLane(
            true,
            true,
            false,
            true,
            40000u + esp::data::kUrgentBombHeavyReadMaxDeferralUs - 1u,
            40000u));
        CHECK(!ShouldDeferBombReadForBusyLane(
            true,
            true,
            false,
            true,
            40000u + esp::data::kUrgentBombHeavyReadMaxDeferralUs,
            40000u));
        CHECK(!ShouldDeferBombReadForBusyLane(true, false, true, true, 40000u, 40000u));
        CHECK(!ShouldDeferBombReadForBusyLane(true, false, false, true, 48000u, 40000u));
        CHECK(!ShouldDeferBombReadForBusyLane(false, false, false, true, 40000u, 40000u));
        CHECK(!IsWeaponC4DynamicReadDue(false, false, false, false, false, 0u, 100u, 10000u));
        CHECK(IsWeaponC4DynamicReadDue(true, false, false, false, true, 0u, 100u, 10000u));
        CHECK(!IsWeaponC4DynamicReadDue(true, true, false, false, true, 100u, 10100u, 10000u));
        CHECK(!IsWeaponC4DynamicReadDue(true, true, false, true, true, 100u, 10100u, 10000u));
        CHECK(IsWeaponC4DynamicReadDue(
            true,
            true,
            false,
            true,
            true,
            100u,
            10100u + esp::data::kUrgentBombHeavyReadMaxDeferralUs,
            10000u));
        CHECK(IsWeaponC4DynamicReadDue(true, true, false, false, true, 100u, 18100u, 10000u));

        CHECK(IsValidWeaponPickableSample(true, 1u, 0u));
        CHECK(IsValidWeaponPickableSample(true, 1u, 1u));
        CHECK(!IsValidWeaponPickableSample(false, 1u, 1u));
        CHECK(!IsValidWeaponPickableSample(true, 0u, 1u));
        CHECK(!IsValidWeaponPickableSample(true, 1u, 2u));
        CHECK(IsWeaponDropTickAdvance(true, true, 0u, 100u));
        CHECK(IsWeaponDropTickAdvance(true, true, 100u, 101u));
        CHECK(IsWeaponDropTickAdvance(true, true, 0xFFFFFFFEu, 1u));
        CHECK(!IsWeaponDropTickAdvance(true, true, 100u, 100u));
        CHECK(!IsWeaponDropTickAdvance(false, true, 100u, 101u));
        CHECK(!IsWeaponDropTickAdvance(true, false, 100u, 101u));
        CHECK(!IsWeaponDropTickAdvance(true, true, 100u, 0u));
        CHECK(IsAuthoritativeDetachedWeaponC4(true, true, true, false));
        CHECK(IsAuthoritativeDetachedWeaponC4(true, true, false, true));
        CHECK(IsAuthoritativeDetachedWeaponC4(true, false, false, true));
        CHECK(!IsAuthoritativeDetachedWeaponC4(false, true, true, true));
        CHECK(ShouldFreshCarryOverrideDetachedWeaponC4(
            true, false, true, true, true, false, false, false));
        CHECK(ShouldFreshCarryOverrideDetachedWeaponC4(
            true, false, true, false, false, true, false, false));
        CHECK(ShouldFreshCarryOverrideDetachedWeaponC4(
            true, false, false, false, false, true, false, false));
        CHECK(ShouldFreshCarryOverrideDetachedWeaponC4(
            true, false, true, true, false, false, true, false));
        CHECK(ShouldFreshCarryOverrideDetachedWeaponC4(
            true, false, false, true, false, false, false, true));
        CHECK(!ShouldFreshCarryOverrideDetachedWeaponC4(
            true, true, false, true, true, true, true, true));
        CHECK(!ShouldFreshCarryOverrideDetachedWeaponC4(
            true, false, true, true, false, false, false, true));
        CHECK(!ShouldFreshCarryOverrideDetachedWeaponC4(
            false, false, false, true, true, true, true, true));
        CHECK(IsBombInventorySamplePastDropEdge(false, 0u, 1u));
        CHECK(!IsBombInventorySamplePastDropEdge(true, 0u, 1u));
        CHECK(!IsBombInventorySamplePastDropEdge(
            true,
            100u,
            100u + esp::data::kBombInventoryDropEdgeSettleUs - 1u));
        CHECK(IsBombInventorySamplePastDropEdge(
            true,
            100u,
            100u + esp::data::kBombInventoryDropEdgeSettleUs));
        CHECK(IsFreshInventoryC4CarrierEvidence(
            2,
            100u,
            100u + esp::data::kFreshInventoryC4CarrierUs));
        CHECK(!IsFreshInventoryC4CarrierEvidence(
            -1,
            100u,
            100u));
        CHECK(!IsFreshInventoryC4CarrierEvidence(
            2,
            0u,
            100u));
        CHECK(!IsFreshInventoryC4CarrierEvidence(
            2,
            100u,
            101u + esp::data::kFreshInventoryC4CarrierUs));
        CHECK(IsWeaponC4PositionSampleCurrent(
            true,
            true,
            100u,
            100u,
            100u + esp::data::kWeaponC4PositionFreshUs));
        CHECK(!IsWeaponC4PositionSampleCurrent(
            true,
            true,
            101u,
            100u,
            101u));
        CHECK(!IsWeaponC4PositionSampleCurrent(
            false,
            true,
            100u,
            100u,
            100u));
        CHECK(!IsWeaponC4PositionSampleCurrent(
            true,
            false,
            100u,
            100u,
            100u));
        CHECK(!IsWeaponC4PositionSampleCurrent(
            true,
            true,
            100u,
            100u,
            101u + esp::data::kWeaponC4PositionFreshUs));
        CHECK(IsWeaponC4OwnerAttachTransition(
            true, true, false, true, true, false, false));
        CHECK(IsWeaponC4OwnerAttachTransition(
            true, true, true, true, true, false, false));
        CHECK(IsWeaponC4OwnerAttachTransition(
            true, true, true, true, false, true, false));
        CHECK(!IsWeaponC4OwnerAttachTransition(
            true, true, false, true, true, false, true));
        CHECK(!IsWeaponC4OwnerAttachTransition(
            false, true, false, true, true, true, false));
        CHECK(!IsWeaponC4OwnerAttachTransition(
            true, true, true, false, true, true, false));

        CHECK(ShouldCarryOverrideDroppedBomb(true, 255, true, true, false));
        CHECK(ShouldCarryOverrideDroppedBomb(true, 80, false, false, true));
        CHECK(!ShouldCarryOverrideDroppedBomb(true, esp::data::kDroppedC4ConfirmedScore, false, false, true));
        CHECK(!ShouldCarryOverrideDroppedBomb(true, 80, true, false, true));
        CHECK(ShouldCarryOverrideDroppedBomb(false, 0, true, true, true));
        CHECK(ShouldCarryOverrideDroppedBomb(false, 0, false, false, true));
        CHECK(!ShouldCarryOverrideDroppedBomb(false, 0, false, false, false));
        CHECK(!HasStrictInventoryCarryEvidence(true, false, false));
        CHECK(!HasStrictInventoryCarryEvidence(false, true, false));
        CHECK(HasStrictInventoryCarryEvidence(true, true, false));
        CHECK(HasStrictInventoryCarryEvidence(false, false, true));
        CHECK(!HasStrictCurrentCarryEvidence(false, true, false, false));
        CHECK(HasStrictCurrentCarryEvidence(true, true, false, false));
        CHECK(HasStrictCurrentCarryEvidence(true, false, true, false));
        CHECK(HasStrictCurrentCarryEvidence(false, false, false, true));
        CHECK(!HasRulesCompatibleCarryEvidence(true, false, true));
        CHECK(HasRulesCompatibleCarryEvidence(true, true, false));
        CHECK(HasRulesCompatibleCarryEvidence(false, false, true));
        CHECK(!HasRulesCompatibleCarryEvidence(false, false, false));
        CHECK(!ShouldWorldC4ReplaceWeaponPosition(true, false, false));
        CHECK(ShouldWorldC4ReplaceWeaponPosition(true, true, false));
        CHECK(ShouldWorldC4ReplaceWeaponPosition(false, false, false));
        CHECK(!ShouldWorldC4ReplaceWeaponPosition(false, true, true));
        CHECK(ShouldWorldC4ReplaceWeaponPosition(true, false, false, true));
        CHECK(!ShouldWorldC4ReplaceWeaponPosition(true, false, true, true));
        using esp::data::IsDetachedWorldC4Candidate;
        using esp::data::ShouldPreferDetachedWorldC4Position;
        CHECK(IsDetachedWorldC4Candidate(true, false, 1, true));
        CHECK(IsDetachedWorldC4Candidate(false, true, 1, true));
        CHECK(IsDetachedWorldC4Candidate(false, false, -1, false));
        CHECK(!IsDetachedWorldC4Candidate(false, false, 1, true));
        CHECK(ShouldPreferDetachedWorldC4Position(true, true, true, false, 0.0f));
        CHECK(ShouldPreferDetachedWorldC4Position(true, true, true, true, 64.0f));
        CHECK(!ShouldPreferDetachedWorldC4Position(true, true, true, true, 16.0f));
        CHECK(!ShouldPreferDetachedWorldC4Position(false, true, true, true, 64.0f));
        CHECK(SelectValidatedWeaponC4Entity(0x1000u, true, 0, false, 0) == 0x1000u);
        CHECK(SelectValidatedWeaponC4Entity(0x1000u, false, 0x2000u, true, 0) == 0x2000u);
        CHECK(SelectValidatedWeaponC4Entity(0x1000u, false, 0x2000u, false, 0x2000u) == 0x2000u);
        CHECK(SelectValidatedWeaponC4Entity(0x1000u, false, 0x2000u, false, 0x3000u) == 0u);
        CHECK(NewestFirstC4ListIndex(3, 0) == 2);
        CHECK(NewestFirstC4ListIndex(3, 1) == 1);
        CHECK(NewestFirstC4ListIndex(3, 2) == 0);
        CHECK(NewestFirstC4ListIndex(3, 3) == -1);
        CHECK(NewestFirstC4ListIndex(0, 0) == -1);
        CHECK(NewestFirstC4ListIndex(33, 0) == -1);
        {
            const uint32_t dropTicks[] = { 900u, 1200u, 1000u };
            const bool allEligible[] = { true, true, true };
            const bool middleIneligible[] = { true, false, true };
            CHECK(SelectNewestC4DropCandidateIndex(
                      dropTicks, allEligible, 3) == 1);
            CHECK(SelectNewestC4DropCandidateIndex(
                      dropTicks, middleIneligible, 3) == 2);

            const uint32_t unknownTicks[] = { 0u, 0u, 0u };
            const bool sparseEligible[] = { true, false, true };
            CHECK(SelectNewestC4DropCandidateIndex(
                      unknownTicks, sparseEligible, 3) == 2);
            CHECK(SelectNewestC4DropCandidateIndex(
                      nullptr, sparseEligible, 3) == -1);

            CHECK(ShouldReplaceWorldC4Candidate(
                false, 0u, 0u, 0, 0, true, false));
            CHECK(ShouldReplaceWorldC4Candidate(
                true, 1201u, 1200u, 10, 1000, true, false));
            CHECK(!ShouldReplaceWorldC4Candidate(
                true, 1199u, 1200u, 1000, 10, false, true));
            CHECK(ShouldReplaceWorldC4Candidate(
                true, 1200u, 1200u, 20, 10, true, false));
            CHECK(ShouldReplaceWorldC4Candidate(
                true, 1200u, 1200u, 10, 10, false, true));
            CHECK(!ShouldReplaceWorldC4Candidate(
                true, 0u, 1200u, 1000, 10, false, true));
        }
        {
            esp::data::BombRoundCounterTracker tracker;
            CHECK(!tracker.Observe(3u));
            CHECK(!tracker.Observe(3u));
            CHECK(tracker.initialized && tracker.committed == 3u);
            CHECK(!tracker.Observe(4u));
            CHECK(tracker.Observe(4u));
            CHECK(tracker.committed == 4u);
            CHECK(!tracker.Observe(
                esp::data::BombRoundCounterTracker::kMaximumPlausibleValue + 1u));
            CHECK(!tracker.Observe(5u));
            tracker.DiscardPending();
            CHECK(!tracker.Observe(5u));
            CHECK(tracker.Observe(5u));
            tracker.Reset();
            CHECK(!tracker.initialized);
            CHECK(sizeof(esp::data::BombRoundStartCounter) == 1);
            const uint8_t roundBytes[] = { 255, 0xA5, 0xFF, 0x7F };
            esp::data::BombRoundStartCounter roundCounter = 0;
            std::memcpy(&roundCounter, roundBytes, sizeof(roundCounter));
            CHECK(roundCounter == 255);
            CHECK(!tracker.Observe(roundCounter));
            CHECK(!tracker.Observe(roundCounter));
            CHECK(!tracker.Observe(0));
            CHECK(tracker.Observe(0));
        }
        CHECK(IsDistinctContinuousC4Sample(100u, 101u, 0.0f));
        CHECK(IsDistinctContinuousC4Sample(
            100u,
            100u + esp::data::kWeaponC4CandidateSampleMaxGapUs,
            esp::data::kWeaponC4CandidateSampleMaxDelta *
                esp::data::kWeaponC4CandidateSampleMaxDelta));
        CHECK(!IsDistinctContinuousC4Sample(100u, 100u, 0.0f));
        CHECK(!IsDistinctContinuousC4Sample(
            100u,
            101u + esp::data::kWeaponC4CandidateSampleMaxGapUs,
            0.0f));
        CHECK(!IsDistinctContinuousC4Sample(
            100u,
            101u,
            1.0f +
                esp::data::kWeaponC4CandidateSampleMaxDelta *
                    esp::data::kWeaponC4CandidateSampleMaxDelta));

        const esp::data::C4EntityKey c4KeyA{
            0x1000u, 3u, 0x2000u, 7u, 1200u
        };
        const esp::data::C4EntityKey c4KeyB{
            0x1000u, 3u, 0x2000u, 7u, 1200u
        };
        const esp::data::C4EntityKey c4OldOrdinal{
            0x1000u, 2u, 0x2000u, 7u, 1200u
        };
        const esp::data::C4EntityKey c4OldGeneration{
            0x1000u, 3u, 0x2000u, 6u, 1200u
        };
        const esp::data::C4EntityKey c4OldDropTick{
            0x1000u, 3u, 0x2000u, 7u, 1100u
        };
        const esp::data::C4EntityKey c4UnknownOrdinal{
            0x1000u, 0u, 0x2000u, 7u, 1200u
        };
        CHECK(c4KeyA.IsValid());
        CHECK(c4UnknownOrdinal.IsValid()); // Direct/world lookup has no vector ordinal.
        CHECK(c4KeyA.Matches(c4KeyB));
        CHECK(c4KeyA.Matches(c4OldOrdinal)); // Reordering is not entity replacement.
        CHECK(c4KeyA.Matches(c4UnknownOrdinal));
        CHECK(!c4KeyA.Matches(c4OldGeneration));
        CHECK(!c4KeyA.Matches(c4OldDropTick));
        CHECK(IsUnconfirmedDroppedC4PublicationAllowed(0x5u, 80, false, true));
        CHECK(IsUnconfirmedDroppedC4PublicationAllowed(0x3u, 90, true, false));
        CHECK(!IsUnconfirmedDroppedC4PublicationAllowed(0x3u, 90, false, false));
        CHECK(!IsUnconfirmedDroppedC4PublicationAllowed(0x204u, 255, true, true));
        CHECK(!IsUnconfirmedDroppedC4PublicationAllowed(0x40u, 96, false, false));
        CHECK(esp::data::IsBombPositionSampleFresh(1000000, 1125000, 125000));
        CHECK(!esp::data::IsBombPositionSampleFresh(1000000, 1125001, 125000));
        CHECK(!esp::data::IsBombPositionSampleFresh(1000000, 999999, 125000));
        CHECK(!esp::data::IsBombPositionSampleFresh(0, 1000000, 125000));
        using esp::data::DroppedC4PositionSource;
        using esp::data::SelectDroppedC4PositionSource;
        // A valid coordinate rejected by confirmation is NOT a cached position.
        CHECK(SelectDroppedC4PositionSource(true, true, false, true, false, 0, 1000000, 220000) ==
              DroppedC4PositionSource::None);
        CHECK(SelectDroppedC4PositionSource(true, true, true, false, false, 0, 1000000, 220000) ==
              DroppedC4PositionSource::Current);
        CHECK(SelectDroppedC4PositionSource(true, true, false, true, true, 900000, 1000000, 220000) ==
              DroppedC4PositionSource::Cache);
        CHECK(SelectDroppedC4PositionSource(true, true, false, false, true, 900000, 1000000, 220000) ==
              DroppedC4PositionSource::None); // New round/entity cannot reuse old cache.
        CHECK(SelectDroppedC4PositionSource(true, false, false, true, true, 900000, 1120001, 220000) ==
              DroppedC4PositionSource::None);
        CHECK(SelectDroppedC4PositionSource(true, false, false, true, true, 1100000, 1000000, 220000) ==
              DroppedC4PositionSource::None);
        CHECK(SelectDroppedC4PositionSource(false, true, true, true, true, 900000, 1000000, 220000) ==
              DroppedC4PositionSource::None); // Carried/terminal state.
        // Repeated cached publications must retain the original sample time:
        // 220 ms of allowed continuity cannot become an indefinite live marker.
        const uint64_t originalDropSampleUs = 1000000;
        for (uint64_t now = 1000000; now < 3000000; now += 4000) {
            const auto selected = SelectDroppedC4PositionSource(
                true, true, false, true, true, originalDropSampleUs, now, 220000);
            CHECK(selected == (now <= 1220000 ? DroppedC4PositionSource::Cache :
                                                       DroppedC4PositionSource::None));
        }
        CHECK(!esp::data::IsBombPositionModeCompatible(true, false, false, true));
        CHECK(!esp::data::IsBombPositionModeCompatible(false, true, true, false));
        CHECK(esp::data::IsBombPositionModeCompatible(false, true, false, true));
        CHECK(!esp::data::IsBombPositionModeCompatible(false, false, false, true));
        CHECK(esp::data::IsBombPositionModeCompatible(false, false, false, true, true));
        CHECK(!IsUnconfirmedDroppedC4PublicationAllowed(0x2u, 90, true, false));
        CHECK(esp::data::SelectPublishedDroppedC4Confidence(90u, false, false) == 90u);
        CHECK(esp::data::SelectPublishedDroppedC4Confidence(90u, true, false) ==
              esp::data::kDroppedC4ConfirmedScore);
        CHECK(esp::data::SelectPublishedDroppedC4Confidence(90u, false, true) ==
              esp::data::kDroppedC4ConfirmedScore);
        CHECK(esp::data::SelectPublishedDroppedC4Confidence(220u, true, false) == 220u);
        CHECK(IsUnconfirmedDroppedC4PublicationAllowed(0x2u, 120, true, false));

        // Replay the reported source=0x403 case: fully read pickable C4 found
        // without a CUtlVector ordinal. Three DISTINCT reads must publish it.
        esp::data::C4PositionConfirmation confirmation;
        auto replayKey = c4UnknownOrdinal;
        for (int frame = 0; frame < 30; ++frame) {
            const uint64_t now = 1000000u + frame * 4000u;
            const uint64_t sample = 1000000u + (frame / 3) * 12000u;
            // Lookup routes and vector order may alternate on every tick.
            replayKey.listOrdinal = static_cast<uint32_t>(frame % 4);
            const bool confirmed = confirmation.Observe(
                replayKey, true, sample, now, 100.0f, 200.0f, 10.0f);
            CHECK(!confirmation.keyChanged);
            CHECK(confirmed == (frame >= 6));
            CHECK(confirmation.samples == (std::min)(frame / 3 + 1, 3));
            const bool allowed = IsUnconfirmedDroppedC4PublicationAllowed(
                0x403u, 255, confirmed, false);
            CHECK(SelectDroppedC4PositionSource(
                true, true, allowed, false, false, 0, now, 220000) ==
                (frame >= 6 ? DroppedC4PositionSource::Current : DroppedC4PositionSource::None));
        }

        confirmation.Reset();
        CHECK(!confirmation.Observe(replayKey, true, 2000000, 2000000, 0, 0, 1));
        CHECK(!confirmation.Observe(replayKey, false, 0, 2004000, NAN, NAN, NAN));
        CHECK(confirmation.samples == 1); // Short partial read does not erase progress.
        CHECK(!confirmation.Observe(replayKey, true, 2012000, 2012000, 1, 0, 1));
        CHECK(!confirmation.Observe(replayKey, false, 0, 2016000, NAN, NAN, NAN));
        CHECK(confirmation.Observe(replayKey, true, 2024000, 2024000, 2, 0, 1));
        CHECK(confirmation.samples == 3);
        // A cached sample cannot keep confirmation alive forever.
        for (uint64_t now = 2028000; now <= 2200000; now += 4000)
            confirmation.Observe(replayKey, false, 2024000, now, 2, 0, 1);
        CHECK(confirmation.samples == 0);
        CHECK(confirmation.sampleUs == 0);
        CHECK(!confirmation.Observe(replayKey, true, 2204000, 2204000, 2, 0, 1));
        CHECK(confirmation.samples == 1);
        CHECK(!confirmation.Observe(replayKey, true, 2216000, 2216000, 1000, 0, 1));
        CHECK(confirmation.samples == 1); // Teleported position starts over.
        CHECK(!confirmation.Observe(replayKey, true, 2212000, 2220000, 1000, 0, 1));
        CHECK(confirmation.sampleUs == 2216000); // Out-of-order evidence ignored.
        CHECK(!confirmation.Observe(replayKey, true, 2228000, 2228000, 1000, 0, 1));
        CHECK(confirmation.Observe(replayKey, true, 2240000, 2240000, 1000, 0, 1));
        // Same address reused in a new drop/round must lose old confirmation.
        ++replayKey.dropGeneration;
        CHECK(!confirmation.Observe(replayKey, true, 2252000, 2252000, 1000, 0, 1));
        CHECK(confirmation.keyChanged);
        CHECK(confirmation.samples == 1);
        ++replayKey.dropTick;
        CHECK(!confirmation.Observe(replayKey, true, 2264000, 2264000, 1000, 0, 1));
        CHECK(confirmation.keyChanged);
        ++replayKey.sceneNode;
        CHECK(!confirmation.Observe(replayKey, true, 2276000, 2276000, 1000, 0, 1));
        CHECK(confirmation.keyChanged);
        // Replacement detected even when its scene-node read is incomplete.
        replayKey.entityPtr += 0x1000;
        replayKey.sceneNode = 0;
        CHECK(!confirmation.Observe(replayKey, false, 0, 2280000, NAN, NAN, NAN));
        CHECK(confirmation.keyChanged);
        CHECK(confirmation.samples == 0);
        CHECK(!confirmation.key.IsValid());
        confirmation.Reset(); // Pickup/plant/terminal and epoch reset use this path.
        CHECK(!confirmation.Observe(c4UnknownOrdinal, true, 2300000, 2290000, 1, 2, 3));
        CHECK(confirmation.samples == 0); // Future timestamp rejected.
        CHECK(!confirmation.Observe(c4UnknownOrdinal, true, 2200000, 2300001, 1, 2, 3));
        CHECK(!confirmation.Observe(c4UnknownOrdinal, true, 2300001, 2300001, NAN, 2, 3));
        CHECK(confirmation.samples == 0);

        CHECK(ShouldInvalidateChangedDropCandidateCache(true, true));
        CHECK(!ShouldInvalidateChangedDropCandidateCache(false, true));
        CHECK(!ShouldInvalidateChangedDropCandidateCache(true, false));

        const int currentDropNearCarrier = ScoreWorldDroppedC4Candidate(
            true, false, 4, true, true, true, true, false);
        const int oldNoOwnerGhost = ScoreWorldDroppedC4Candidate(
            true, true, -1, false, false, false, false, false);
        const int oldNoOwnerGhostNearTeam = ScoreWorldDroppedC4Candidate(
            true, true, -1, false, false, false, true, false);
        const int continuousCurrentDrop = ScoreWorldDroppedC4Candidate(
            true, true, -1, false, false, false, false, true);
        CHECK(currentDropNearCarrier > oldNoOwnerGhost);
        CHECK(currentDropNearCarrier > oldNoOwnerGhostNearTeam);
        CHECK(continuousCurrentDrop > oldNoOwnerGhost);
        CHECK(ScoreWorldDroppedC4Candidate(
                  false, true, -1, false, false, false, false, false) == 520);

        {
            const float candidates[] = { 4000.0f, 102.02f, 80.0f };
            const auto selected =
                SelectStableGameTimeCandidate(candidates, 3, 102.0f, 1000000u, 1020000u);
            CHECK(selected.acceptedRaw);
            CHECK(selected.candidateIndex == 1u);
            CHECK(std::fabs(selected.value - 102.02f) < 0.001f);
        }
        {
            const float candidates[] = { 40.0f, 5000.0f };
            const auto selected =
                SelectStableGameTimeCandidate(candidates, 2, 102.0f, 1000000u, 1100000u);
            CHECK(!selected.acceptedRaw);
            CHECK(selected.value >= 102.0f);
            CHECK(selected.value <= 102.11f);
        }
        {
            const float candidates[] = { 102.0f, 112.0f };
            const auto paused =
                SelectStableGameTimeCandidate(candidates, 2, 102.0f, 1000000u, 11000000u, 0u);
            const auto running =
                SelectStableGameTimeCandidate(candidates, 2, 102.0f, 1000000u, 11000000u, 1u);
            CHECK(paused.acceptedRaw && paused.candidateIndex == 1u);
            CHECK(running.acceptedRaw && running.candidateIndex == 1u);
        }
        {
            const float candidates[] = { 102.0f, 0.0f };
            const auto selected =
                SelectStableGameTimeCandidate(candidates, 2, 102.0f, 1000000u, 11000000u, 0u);
            CHECK(selected.acceptedRaw && selected.candidateIndex == 0u);
        }
        CHECK(IsPlantedC4Terminal(true, false));
        CHECK(IsPlantedC4Terminal(false, true));
        CHECK(!IsPlantedC4Terminal(false, false));
        CHECK(IsBombFieldReadComplete(false, 0u, sizeof(uint32_t)));
        CHECK(IsBombFieldReadComplete(true, sizeof(uint32_t), sizeof(uint32_t)));
        CHECK(!IsBombFieldReadComplete(true, sizeof(uint32_t) - 1u, sizeof(uint32_t)));
        CHECK(IsPlausiblePlantedC4Metadata({
            1u,
            0u,
            0u,
            0u,
            1u,
            120.0f,
            40.0f,
            0.0f,
            0.0f,
        }));
        CHECK(!IsPlausiblePlantedC4Metadata({
            2u,
            0u,
            0u,
            0u,
            1u,
            120.0f,
            40.0f,
            0.0f,
            0.0f,
        }));
        CHECK(!IsPlausiblePlantedC4Metadata({
            1u,
            0u,
            0u,
            0u,
            1u,
            120.0f,
            40.0f,
            0.0f,
            20.0f,
        }));
        CHECK(IsLivePlantedC4(true, true, true, false, false, true, false, false, false));
        CHECK(IsLivePlantedC4(true, true, true, false, false, false, true, false, false));
        CHECK(!IsLivePlantedC4(true, true, false, false, false, true, true, true, true));
        CHECK(!IsLivePlantedC4(true, true, true, false, true, true, true, true, true));
        CHECK(!IsLivePlantedC4(true, true, true, true, false, true, true, true, true));
        CHECK(!IsLivePlantedC4(false, true, true, false, false, true, true, true, true));
        CHECK(IsDefusingPawnCandidate(true, true, 100, 0, 3));
        CHECK(IsDefusingPawnCandidate(true, true, 100, 0, 0));
        CHECK(!IsDefusingPawnCandidate(true, true, 100, 0, 2));
        CHECK(!IsDefusingPawnCandidate(false, true, 100, 0, 3));
        CHECK(IsBombMetadataFresh(true, true, 100u, 100u + esp::data::kPlantedC4MetaFreshUs));
        CHECK(!IsBombMetadataFresh(true, true, 100u, 101u + esp::data::kPlantedC4MetaFreshUs));
        CHECK(!IsBombMetadataFresh(true, false, 100u, 100u));
        CHECK(!IsBombMetadataFresh(false, true, 100u, 100u));
        CHECK(IsAuthoritativeBombTimer(true, false, true, true, false, true));
        CHECK(IsAuthoritativeBombTimer(true, false, true, false, true, true));
        CHECK(!IsAuthoritativeBombTimer(true, false, true, false, false, true));
        CHECK(!IsAuthoritativeBombTimer(true, false, false, true, true, true));
        CHECK(!IsAuthoritativeBombTimer(true, true, true, true, true, true));
        CHECK(ShouldHoldTransientBombTimer(
            true,
            true,
            false,
            100u,
            100u + esp::data::kBombTimerSignalGapHoldUs,
            140.0f,
            40.0f,
            100.0f));
        CHECK(!ShouldHoldTransientBombTimer(
            true,
            true,
            false,
            100u,
            101u + esp::data::kBombTimerSignalGapHoldUs,
            140.0f,
            40.0f,
            100.0f));
        CHECK(!ShouldHoldTransientBombTimer(
            true,
            false,
            false,
            100u,
            101u,
            140.0f,
            40.0f,
            100.0f));
        CHECK(!ShouldHoldTransientBombTimer(
            true,
            true,
            true,
            100u,
            101u,
            140.0f,
            40.0f,
            100.0f));
        CHECK(!ShouldHoldTransientBombTimer(
            true,
            true,
            false,
            100u,
            101u,
            99.0f,
            40.0f,
            100.0f));
        CHECK(IsAuthoritativeDefuseTimer(true, false, true, true, false, true, true));
        CHECK(IsAuthoritativeDefuseTimer(true, false, true, false, true, true, true));
        CHECK(!IsAuthoritativeDefuseTimer(true, false, true, false, false, true, true));
        CHECK(!IsAuthoritativeDefuseTimer(true, false, true, true, false, false, true));
        CHECK(!IsAuthoritativeDefuseTimer(true, false, true, true, false, true, false));
        CHECK(!IsAuthoritativeDefuseTimer(true, true, true, true, true, true, true));
        CHECK(ShouldHoldTransientDefuseTimer(
            true,
            true,
            false,
            100u,
            100u + esp::data::kDefuseSignalGapHoldUs,
            105.0f,
            100.0f));
        CHECK(!ShouldHoldTransientDefuseTimer(
            true,
            true,
            false,
            100u,
            101u + esp::data::kDefuseSignalGapHoldUs,
            105.0f,
            100.0f));
        CHECK(!ShouldHoldTransientDefuseTimer(
            true,
            false,
            false,
            100u,
            101u,
            105.0f,
            100.0f));
        CHECK(!ShouldHoldTransientDefuseTimer(
            true,
            true,
            true,
            100u,
            101u,
            105.0f,
            100.0f));
        CHECK(!ShouldHoldTransientDefuseTimer(
            true,
            true,
            false,
            100u,
            101u,
            99.0f,
            100.0f));
        CHECK(ShouldUsePlantedC4PositionFallback(true, false, 100u, 100u + esp::data::kPlantedC4PositionFallbackUs));
        CHECK(!ShouldUsePlantedC4PositionFallback(true, true, 100u, 100u));
        CHECK(!ShouldUsePlantedC4PositionFallback(false, false, 100u, 100u));
        CHECK(!ShouldUsePlantedC4PositionFallback(true, false, 0u, 100u));
        CHECK(!ShouldUsePlantedC4PositionFallback(true, false, 100u, 101u + esp::data::kPlantedC4PositionFallbackUs));

        CHECK(SelectWorldC4StickyUs(false) == esp::data::kWeakDroppedC4StickyUs);
        CHECK(SelectWorldC4StickyUs(true) == esp::data::kRulesWorldC4StickyUs);
        CHECK(SelectDroppedBombStickyUs(false, 1800000u) == esp::data::kWeakDroppedC4StickyUs);
        CHECK(SelectDroppedBombStickyUs(true, 1800000u) == 1800000u);
        CHECK(SelectConfirmedDroppedStickyUs(false) == esp::data::kWeakConfirmedDroppedStickyUs);
        CHECK(SelectConfirmedDroppedStickyUs(true) == esp::data::kRulesConfirmedDroppedStickyUs);

        CHECK(IsStickyBombEvidenceFresh(100u, 100u + 10u, 10u));
        CHECK(!IsStickyBombEvidenceFresh(100u, 100u + 11u, 10u));
        CHECK(!IsStickyBombEvidenceFresh(0u, 100u, 10u));
    }

    void TestBaseRecoveryPolicy()
    {
        using esp::data::IsBasePolicyCooldownElapsed;
        using esp::data::IsBaseSceneSettling;
        using esp::data::IsConfirmedLiveEngineState;
        using esp::data::IsEntityShapeLive;
        using esp::data::IsGameRulesCoreSane;
        using esp::data::ShouldAcceptEntityListCandidate;
        using esp::data::ShouldLogGameRulesSanityFailure;
        using esp::data::ShouldRefreshAfterGameRulesSanityFailure;
        using esp::data::ShouldResolveMatchmakingBase;
        using esp::data::ShouldRequestPersistentGameRulesRecovery;
        using esp::data::ShouldPreserveRecentColdAttachReset;
        using esp::data::ShouldTransitionOnEntityShapeLive;

        CHECK(IsBasePolicyCooldownElapsed(0u, 100u, 10u));
        CHECK(IsBasePolicyCooldownElapsed(100u, 99u, 10u));
        CHECK(!IsBasePolicyCooldownElapsed(100u, 109u, 10u));
        CHECK(IsBasePolicyCooldownElapsed(100u, 110u, 10u));

        CHECK(IsGameRulesCoreSane(true, 0u, 0u));
        CHECK(IsGameRulesCoreSane(true, 1u, 0u));
        CHECK(IsGameRulesCoreSane(true, 0u, 1u));
        CHECK(IsGameRulesCoreSane(true, 1u, 1u));
        CHECK(!IsGameRulesCoreSane(false, 0u, 0u));
        CHECK(IsGameRulesCoreSane(true, 2u, 0u));
        CHECK(IsGameRulesCoreSane(true, 0u, 2u));
        CHECK(IsGameRulesCoreSane(true, 0x80u, 0x40u));

        CHECK(ShouldLogGameRulesSanityFailure(1u, 100u, 101u));
        CHECK(!ShouldLogGameRulesSanityFailure(10u, 100u, 101u));
        CHECK(!ShouldLogGameRulesSanityFailure(
            2u,
            100u,
            100u + esp::data::kGameRulesSanityLogCooldownUs - 1u));
        CHECK(ShouldLogGameRulesSanityFailure(
            2u,
            100u,
            100u + esp::data::kGameRulesSanityLogCooldownUs));

        CHECK(!ShouldRefreshAfterGameRulesSanityFailure(
            esp::data::kGameRulesSanityRepairStreak - 1u,
            100u,
            0u,
            100u + esp::data::kGameRulesSanityRepairAgeUs));
        CHECK(!ShouldRefreshAfterGameRulesSanityFailure(
            esp::data::kGameRulesSanityRepairStreak,
            100u,
            0u,
            100u + esp::data::kGameRulesSanityRepairAgeUs - 1u));
        CHECK(ShouldRefreshAfterGameRulesSanityFailure(
            esp::data::kGameRulesSanityRepairStreak,
            100u,
            0u,
            100u + esp::data::kGameRulesSanityRepairAgeUs));
        CHECK(!ShouldRefreshAfterGameRulesSanityFailure(
            esp::data::kGameRulesSanityRepairStreak,
            100u,
            100u,
            100u + esp::data::kGameRulesSanityRepairAgeUs));
        CHECK(!ShouldRequestPersistentGameRulesRecovery(
            100u,
            100u + esp::data::kGameRulesSanityPersistentAgeUs - 1u));
        CHECK(ShouldRequestPersistentGameRulesRecovery(
            100u,
            100u + esp::data::kGameRulesSanityPersistentAgeUs));

        CHECK(IsBaseSceneSettling(esp::data::kBaseSceneImmediateSettlingUs - 1u, true));
        CHECK(!IsBaseSceneSettling(esp::data::kBaseSceneImmediateSettlingUs, true));
        CHECK(IsBaseSceneSettling(esp::data::kBaseSceneWarmupSettlingUs - 1u, false));
        CHECK(!IsBaseSceneSettling(esp::data::kBaseSceneWarmupSettlingUs, false));
        CHECK(ShouldPreserveRecentColdAttachReset(
            true,
            true,
            100u,
            100u + esp::data::kColdAttachResetPreserveWindowUs - 1u));
        CHECK(!ShouldPreserveRecentColdAttachReset(false, true, 100u, 101u));
        CHECK(!ShouldPreserveRecentColdAttachReset(true, false, 100u, 101u));
        CHECK(!ShouldPreserveRecentColdAttachReset(true, true, 0u, 101u));
        CHECK(!ShouldPreserveRecentColdAttachReset(
            true,
            true,
            100u,
            100u + esp::data::kColdAttachResetPreserveWindowUs));

        CHECK(ShouldResolveMatchmakingBase(0u));
        CHECK(!ShouldResolveMatchmakingBase(0x123u));
        CHECK(!ShouldAcceptEntityListCandidate(
            true,
            esp::data::kEntityListRuntimeConfirmSamples,
            100u,
            99u + esp::data::kEntityListRuntimeConfirmAgeUs));
        CHECK(ShouldAcceptEntityListCandidate(
            true,
            esp::data::kEntityListRuntimeConfirmSamples,
            100u,
            100u + esp::data::kEntityListRuntimeConfirmAgeUs));
        CHECK(!ShouldAcceptEntityListCandidate(
            true,
            esp::data::kEntityListRuntimeConfirmSamples - 1u,
            100u,
            100u + esp::data::kEntityListRuntimeConfirmAgeUs));
        CHECK(ShouldAcceptEntityListCandidate(
            false,
            esp::data::kEntityListWarmupConfirmSamples,
            100u,
            100u + esp::data::kEntityListWarmupConfirmAgeUs));
        CHECK(!ShouldAcceptEntityListCandidate(
            false,
            esp::data::kEntityListWarmupConfirmSamples,
            0u,
            100u + esp::data::kEntityListWarmupConfirmAgeUs));

        CHECK(IsEntityShapeLive(false, true, true, true, true, 32, 64));
        CHECK(!IsEntityShapeLive(true, true, true, true, true, 32, 64));
        CHECK(!IsEntityShapeLive(false, true, true, true, true, 31, 64));
        CHECK(!IsEntityShapeLive(false, true, true, true, true, 32, 63));

        CHECK(!ShouldTransitionOnEntityShapeLive(
            esp::data::kEntityShapeLiveConfirmStreak - 1u,
            100u,
            false,
            0u,
            100u + esp::data::kEntityShapeLiveConfirmAgeUs));
        CHECK(!ShouldTransitionOnEntityShapeLive(
            esp::data::kEntityShapeLiveConfirmStreak,
            100u,
            false,
            0u,
            100u + esp::data::kEntityShapeLiveConfirmAgeUs - 1u));
        CHECK(!ShouldTransitionOnEntityShapeLive(
            esp::data::kEntityShapeLiveConfirmStreak,
            100u,
            true,
            0u,
            100u + esp::data::kEntityShapeLiveConfirmAgeUs));
        CHECK(ShouldTransitionOnEntityShapeLive(
            esp::data::kEntityShapeLiveConfirmStreak,
            100u,
            false,
            0u,
            100u + esp::data::kEntityShapeLiveConfirmAgeUs));
        CHECK(!ShouldTransitionOnEntityShapeLive(
            esp::data::kEntityShapeLiveConfirmStreak,
            100u,
            false,
            100u,
            100u + esp::data::kEntityShapeLiveConfirmAgeUs));
        CHECK(ShouldTransitionOnEntityShapeLive(
            esp::data::kEntityShapeLiveConfirmStreak,
            100u,
            false,
            100u,
            100u + esp::data::kEntityShapeTransitionCooldownUs));

        CHECK(IsConfirmedLiveEngineState(true, true, false, 6, 64));
        CHECK(!IsConfirmedLiveEngineState(false, true, false, 6, 64));
        CHECK(!IsConfirmedLiveEngineState(true, false, false, 6, 64));
        CHECK(!IsConfirmedLiveEngineState(true, true, true, 6, 64));
        CHECK(!IsConfirmedLiveEngineState(true, true, false, 5, 64));
        CHECK(!IsConfirmedLiveEngineState(true, true, false, 6, 1));
    }

    void TestSceneTransitionPolicy()
    {
        using esp::data::ObserveSceneTransition;
        using esp::data::SceneTransitionReason;
        using esp::data::SceneTransitionState;

        SceneTransitionState state = {};
        CHECK(!ObserveSceneTransition(state, false, 0x1000u, 1u, 1000u).transition);

        const auto enter = ObserveSceneTransition(state, true, 0x1000u, 1u, 2000u);
        CHECK(enter.transition);
        CHECK(enter.reason == SceneTransitionReason::MatchEnter);
        CHECK(enter.bumpMapEpoch);
        CHECK(!enter.preserveLiveSnapshot);
        CHECK(enter.refreshCaches);

        CHECK(!ObserveSceneTransition(state, false, 0x1000u, 1u, 3000u).transition);
        CHECK(!ObserveSceneTransition(state, true, 0x1000u, 1u, 4000u).transition);
        CHECK(!state.pending);

        constexpr uint64_t exitStartedUs = 10000u;
        for (uint32_t sample = 0;
             sample < esp::data::kMatchExitConfirmationSamples;
             ++sample) {
            CHECK(!ObserveSceneTransition(
                state,
                false,
                0x1000u,
                1u,
                exitStartedUs + sample * 10000u).transition);
        }
        const auto exit = ObserveSceneTransition(
            state,
            false,
            0x1000u,
            1u,
            exitStartedUs + esp::data::kMatchExitConfirmationUs);
        CHECK(exit.transition);
        CHECK(exit.reason == SceneTransitionReason::MatchExit);
        CHECK(!exit.bumpMapEpoch);
        CHECK(!exit.preserveLiveSnapshot);
        CHECK(!exit.refreshCaches);

        constexpr uint64_t clientChangeStartedUs = 300000u;
        for (uint32_t sample = 0;
             sample < esp::data::kNetworkClientConfirmationSamples;
             ++sample) {
            CHECK(!ObserveSceneTransition(
                state,
                false,
                0x2000u,
                1u,
                clientChangeStartedUs + sample * 10000u).transition);
        }
        const auto mapChange = ObserveSceneTransition(
            state,
            false,
            0x2000u,
            1u,
            clientChangeStartedUs + esp::data::kNetworkClientConfirmationUs);
        CHECK(mapChange.transition);
        CHECK(mapChange.reason == SceneTransitionReason::NetworkClientChanged);
        CHECK(mapChange.bumpMapEpoch);
        CHECK(!mapChange.preserveLiveSnapshot);
        CHECK(!mapChange.refreshCaches);

        CHECK(!ObserveSceneTransition(state, true, 0x3000u, 2u, 600000u).transition);
        CHECK(state.committedMatchLike);
        CHECK(state.committedNetworkClient == 0x3000u);
        CHECK(!state.pending);

        SceneTransitionState alreadyInMatch = {};
        const auto initialEnter =
            ObserveSceneTransition(alreadyInMatch, true, 0x4000u, 1u, 1000u);
        CHECK(initialEnter.transition);
        CHECK(initialEnter.reason == SceneTransitionReason::MatchEnter);

        constexpr uint64_t liveClientChangeStartedUs = 300000u;
        for (uint32_t sample = 0;
             sample < esp::data::kNetworkClientConfirmationSamples;
             ++sample) {
            CHECK(!ObserveSceneTransition(
                alreadyInMatch,
                true,
                0x5000u,
                1u,
                liveClientChangeStartedUs + sample * 10000u).transition);
        }
        const auto liveClientChange = ObserveSceneTransition(
            alreadyInMatch,
            true,
            0x5000u,
            1u,
            liveClientChangeStartedUs + esp::data::kNetworkClientConfirmationUs);
        CHECK(liveClientChange.transition);
        CHECK(liveClientChange.reason == SceneTransitionReason::NetworkClientChanged);
        CHECK(liveClientChange.preserveLiveSnapshot);
        CHECK(liveClientChange.refreshCaches);

        using esp::data::EngineResolveMissAction;
        using esp::data::EngineResolveMissActionBit;
        using esp::data::IsEngineStateConfirmationElapsed;
        using esp::data::IsValidSignOnState;
        using esp::data::SelectEngineResolveMissAction;
        using esp::data::SelectSignOnStateCandidate;

        CHECK(!IsEngineStateConfirmationElapsed(0u, 1000000u, 500000u));
        CHECK(!IsEngineStateConfirmationElapsed(100u, 500099u, 500000u));
        CHECK(IsEngineStateConfirmationElapsed(100u, 500100u, 500000u));
        CHECK(!IsEngineStateConfirmationElapsed(1000u, 999u, 1u));
        CHECK(IsValidSignOnState(0));
        CHECK(IsValidSignOnState(12));
        CHECK(!IsValidSignOnState(-1));
        CHECK(!IsValidSignOnState(13));
        CHECK(SelectSignOnStateCandidate(0, 6, 5, 64, 0, false) == 0);
        CHECK(SelectSignOnStateCandidate(0, 5, 6, 0, 0, true) == 0);
        CHECK(SelectSignOnStateCandidate(0, 6, 5, 1, 1, false) == 0);
        CHECK(SelectSignOnStateCandidate(-1, 5, 4, 0, 0, false) == 5);
        CHECK(SelectSignOnStateCandidate(-1, -1, 13, 0, 0, false) == -1);
        CHECK(SelectSignOnStateCandidate(5, 6, 6, 64, 0, true) == 5);
        CHECK(SelectSignOnStateCandidate(6, 0, 0, 64, 0, true) == 6);
        CHECK(esp::data::IsCompleteEngineActivitySample(6, 64, 0, 4, 1));
        CHECK(!esp::data::IsCompleteEngineActivitySample(6, 64, 0, 0, 1));
        CHECK(!esp::data::IsCompleteEngineActivitySample(6, 64, 0, 4, 0));
        CHECK(!esp::data::IsCompleteEngineActivitySample(6, 64, 2, 4, 1));
        CHECK(!esp::data::IsCompleteEngineActivitySample(-1, 64, 0, 4, 1));
        CHECK(esp::data::IsLiveEngineActivitySample(6, 64, 0));
        CHECK(!esp::data::IsLiveEngineActivitySample(6, 64, 1)); // Background menu, even after a match.
        CHECK(esp::data::IsLiveEngineActivitySample(6, 1, 0)); // One-client offline/workshop.
        CHECK(!esp::data::IsLiveEngineActivitySample(6, 1, 1)); // Background menu.
        CHECK(!esp::data::IsLiveEngineActivitySample(6, 0, 0)); // Do not invent 64 clients.
        CHECK(!esp::data::IsLiveEngineActivitySample(5, 64, 0));
        uint64_t nonLiveSince = 0;
        CHECK(esp::data::HoldTransientEngineExit(false, true, true, nonLiveSince, 1000000));
        CHECK(esp::data::HoldTransientEngineExit(false, true, true, nonLiveSince, 1249999));
        CHECK(!esp::data::HoldTransientEngineExit(false, true, true, nonLiveSince, 1250000));
        CHECK(!esp::data::HoldTransientEngineExit(false, false, true, nonLiveSince, 1260000));
        CHECK(nonLiveSince == 0); // Sustained menu cannot re-arm the old live latch.
        CHECK(esp::data::HoldTransientEngineExit(false, true, true, nonLiveSince, 2000000));
        CHECK(!esp::data::HoldTransientEngineExit(true, true, true, nonLiveSince, 2012000));
        CHECK(nonLiveSince == 0); // One bad in-match sample does not clear the scene.
        CHECK(!esp::data::HoldTransientEngineExit(false, true, false, nonLiveSince, 2100000));
        CHECK(esp::data::IsEngineStateCacheFresh(1000000, 1500000));
        CHECK(!esp::data::IsEngineStateCacheFresh(1000000, 1500001));
        CHECK(!esp::data::IsEngineStateCacheFresh(1000000, 999999));
        CHECK(!esp::data::IsEngineStateCacheFresh(0, 1000000));

        esp::data::SceneTransitionState menuReplay;
        auto menuDecision = esp::data::ObserveSceneTransition(menuReplay, true, 0x1000, 1, 1000000);
        CHECK(menuDecision.reason == esp::data::SceneTransitionReason::MatchEnter);
        // The reader already confirmed the exit for 250 ms: don't debounce it twice.
        menuDecision = esp::data::ObserveSceneTransition(menuReplay, false, 0x1000, 1, 2250000, true);
        CHECK(menuDecision.reason == esp::data::SceneTransitionReason::MatchExit);
        CHECK(menuDecision.transition && !menuDecision.preserveLiveSnapshot && !menuDecision.refreshCaches);
        for (uint64_t now = 2260000; now < 12000000; now += 30000) {
            menuDecision = esp::data::ObserveSceneTransition(menuReplay, false, 0x1000, 1, now, true);
            CHECK(!menuDecision.transition && !menuDecision.refreshCaches);
        }
        menuDecision = esp::data::ObserveSceneTransition(menuReplay, true, 0x2000, 1, 13000000);
        CHECK(!menuDecision.transition); // New network identity still needs confirmation.
        int menuReplayEnterCount = 0;
        for (int i = 1; i <= 10; ++i) {
            menuDecision = esp::data::ObserveSceneTransition(menuReplay, true, 0x2000, 1, 13000000 + i * 30000);
            if (menuDecision.transition) {
                ++menuReplayEnterCount;
                CHECK(menuDecision.reason == esp::data::SceneTransitionReason::MatchEnter);
                CHECK(!menuDecision.preserveLiveSnapshot);
                CHECK(menuDecision.refreshCaches && menuDecision.bumpMapEpoch);
            }
        }
        CHECK(menuReplayEnterCount == 1);
        CHECK(menuReplay.committedMatchLike && menuReplay.committedNetworkClient == 0x2000);

        uint8_t completedActions = 0;
        CHECK(SelectEngineResolveMissAction(
                  esp::data::kEngineResolveProbeAgeUs - 1u,
                  completedActions) == EngineResolveMissAction::None);
        CHECK(SelectEngineResolveMissAction(
                  esp::data::kEngineResolveProbeAgeUs,
                  completedActions) == EngineResolveMissAction::Probe);
        completedActions |= EngineResolveMissActionBit(EngineResolveMissAction::Probe);
        CHECK(SelectEngineResolveMissAction(
                  esp::data::kEngineResolveRepairAgeUs - 1u,
                  completedActions) == EngineResolveMissAction::None);
        CHECK(SelectEngineResolveMissAction(
                  esp::data::kEngineResolveRepairAgeUs,
                  completedActions) == EngineResolveMissAction::Repair);
        completedActions |= EngineResolveMissActionBit(EngineResolveMissAction::Repair);
        CHECK(SelectEngineResolveMissAction(
                  esp::data::kEngineResolveRecoveryAgeUs - 1u,
                  completedActions) == EngineResolveMissAction::None);
        CHECK(SelectEngineResolveMissAction(
                  esp::data::kEngineResolveRecoveryAgeUs,
                  completedActions) == EngineResolveMissAction::Recovery);
        completedActions |= EngineResolveMissActionBit(EngineResolveMissAction::Recovery);
        CHECK(SelectEngineResolveMissAction(
                  esp::data::kEngineResolveRecoveryAgeUs + 1000000u,
                  completedActions) == EngineResolveMissAction::None);
    }

    void TestWorkerPolicy()
    {
        using esp::worker::ShouldReadLiveCamera;
        using esp::worker::DataWorkerTargetHz;
        CHECK(ShouldReadLiveCamera(true, true, true, false, false));
        CHECK(!ShouldReadLiveCamera(true, true, true, true, false)); // background signon=6
        CHECK(!ShouldReadLiveCamera(true, false, true, false, false)); // old inGame flag
        CHECK(!ShouldReadLiveCamera(true, true, false, false, false)); // loading
        CHECK(!ShouldReadLiveCamera(true, true, true, false, true)); // recovery
        CHECK(!ShouldReadLiveCamera(false, true, true, false, false));
        CHECK(DataWorkerTargetHz(false, true, true, true, false, 250) == 250);
        CHECK(DataWorkerTargetHz(false, true, true, true, true, 250) == 90);
        CHECK(DataWorkerTargetHz(false, true, false, true, false, 250) == 180);
        CHECK(DataWorkerTargetHz(false, false, false, false, false, 250) == 45);
        CHECK(DataWorkerTargetHz(true, false, false, false, true, 250) == 220);

        esp::worker::CounterInterval<4> quality;
        CHECK(quality.Due(0));
        CHECK(!quality.Observe({ 100, 85, 72, 0 }, 0).valid);
        CHECK(!quality.Due(999999));
        auto interval = quality.Observe({ 200, 85, 72, 0 }, 1000000);
        CHECK(interval.valid && interval.durationUs == 1000000);
        CHECK(interval.counts[0] == 100 && interval.counts[1] == 0);
        CHECK(interval.counts[2] == 0 && interval.counts[3] == 0);
        CHECK(quality.Observe({ 210, 86, 73, 0 }, 1000001).counts[0] == 100);
        interval = quality.Observe({ 210, 86, 73, 2 }, 2400000);
        CHECK(interval.durationUs == 1400000 && interval.completedAtUs == 2400000);
        CHECK(interval.counts[0] == 10 && interval.counts[1] == 1);
        CHECK(interval.counts[2] == 1 && interval.counts[3] == 2);
        CHECK(!quality.Observe({ 1, 0, 0, 0 }, 3400000).valid); // counter reset, no underflow
        CHECK(!quality.Observe({ 2, 0, 0, 0 }, 10).valid); // clock reset
        interval = quality.Observe({ 2, 0, 0, 0 }, 1000010);
        CHECK(interval.valid && interval.counts[0] == 0); // zero activity is a valid window

        using esp::worker::IsCameraPauseOrphaned;
        using esp::worker::IsCameraSampleFresh;
        using esp::worker::IsDataSupervisorDue;
        using esp::worker::IsRecentWorkerTimestamp;
        using esp::worker::IsWorkerCooldownElapsed;
        using esp::worker::IsWorkerScheduleSkipped;
        using esp::worker::LatencyBucketIndex;
        using esp::worker::LatencyPercentileUpperBound;
        using esp::worker::NextWorkerDeadline;
        using esp::worker::ShouldLogWorkerStall;
        using esp::worker::ShouldRequestCameraExceptionRecovery;
        using esp::worker::ShouldRequestPersistentCameraRecovery;
        using esp::worker::ShouldRunCameraSelfHeal;

        CHECK(IsWorkerCooldownElapsed(0u, 100u, 10u));
        CHECK(IsWorkerCooldownElapsed(100u, 99u, 10u));
        CHECK(!IsWorkerCooldownElapsed(100u, 109u, 10u));
        CHECK(IsWorkerCooldownElapsed(100u, 110u, 10u));

        CHECK(LatencyBucketIndex(500u) == 0u);
        CHECK(LatencyBucketIndex(501u) == 1u);
        CHECK(LatencyBucketIndex(2000000u) ==
              esp::worker::kCycleLatencyUpperBoundsUs.size() - 1u);
        std::array<uint64_t, esp::worker::kCycleLatencyUpperBoundsUs.size()> latencyBuckets = {};
        latencyBuckets[0] = 50u;
        latencyBuckets[5] = 45u;
        latencyBuckets[LatencyBucketIndex(8334u)] = 5u;
        CHECK(esp::worker::kCycleLatencyUpperBoundsUs[LatencyBucketIndex(3999u)] == 4000u);
        CHECK(esp::worker::kCycleLatencyUpperBoundsUs[LatencyBucketIndex(4001u)] == 5000u);
        CHECK(LatencyPercentileUpperBound(latencyBuckets, 100u, 50u) == 500u);
        CHECK(LatencyPercentileUpperBound(latencyBuckets, 100u, 95u) == 2000u);
        CHECK(LatencyPercentileUpperBound(latencyBuckets, 100u, 99u) == 8334u);
        CHECK(LatencyPercentileUpperBound(latencyBuckets, 0u, 99u) == 0u);

        CHECK(IsRecentWorkerTimestamp(100u, 110u, 10u));
        CHECK(!IsRecentWorkerTimestamp(100u, 111u, 10u));
        CHECK(!IsRecentWorkerTimestamp(0u, 100u, 10u));
        CHECK(IsCameraSampleFresh(100u, 100u));
        CHECK(IsCameraSampleFresh(100u, 100u + esp::worker::kCameraSnapshotFreshUs));
        CHECK(!IsCameraSampleFresh(100u, 100u + esp::worker::kCameraSnapshotFreshUs + 1u));

        CHECK(!IsCameraPauseOrphaned(0u, false, false));
        CHECK(IsCameraPauseOrphaned(1u, false, false));
        CHECK(!IsCameraPauseOrphaned(1u, true, false));
        CHECK(!IsCameraPauseOrphaned(1u, false, true));

        CHECK(IsDataSupervisorDue(0u, 100u));
        CHECK(IsDataSupervisorDue(100u, 99u));
        CHECK(!IsDataSupervisorDue(
            100u,
            100u + esp::worker::kDataSupervisorIntervalUs - 1u));
        CHECK(IsDataSupervisorDue(
            100u,
            100u + esp::worker::kDataSupervisorIntervalUs));

        CHECK(!ShouldRunCameraSelfHeal(179u, 180u, true, 0u, 100u));
        CHECK(!ShouldRunCameraSelfHeal(180u, 180u, false, 0u, 100u));
        CHECK(ShouldRunCameraSelfHeal(180u, 180u, true, 0u, 100u));
        CHECK(!ShouldRunCameraSelfHeal(
            180u,
            180u,
            true,
            100u,
            100u + esp::worker::kCameraSelfHealCooldownUs - 1u));
        CHECK(ShouldRunCameraSelfHeal(
            180u,
            180u,
            true,
            100u,
            100u + esp::worker::kCameraSelfHealCooldownUs));

        CHECK(!ShouldRequestPersistentCameraRecovery(true, 720u, 180u));
        CHECK(!ShouldRequestPersistentCameraRecovery(false, 719u, 180u));
        CHECK(ShouldRequestPersistentCameraRecovery(false, 720u, 180u));

        CHECK(ShouldRequestCameraExceptionRecovery(true, true, false));
        CHECK(!ShouldRequestCameraExceptionRecovery(true, false, false));
        CHECK(!ShouldRequestCameraExceptionRecovery(true, true, true));

        CHECK(!ShouldLogWorkerStall(esp::worker::kCameraStallCycleUs, 0u, 100u));
        CHECK(ShouldLogWorkerStall(esp::worker::kCameraStallCycleUs + 1u, 0u, 100u));
        CHECK(!ShouldLogWorkerStall(
            esp::worker::kCameraStallCycleUs + 1u,
            100u,
            100u + esp::worker::kWorkerStallLogCooldownUs - 1u));
        CHECK(ShouldLogWorkerStall(
            esp::worker::kCameraStallCycleUs + 1u,
            100u,
            100u + esp::worker::kWorkerStallLogCooldownUs));

        CHECK(esp::worker::WorkerRestartBackoffMs(0u) == 100u);
        CHECK(esp::worker::WorkerRestartBackoffMs(1u) == 100u);
        CHECK(esp::worker::WorkerRestartBackoffMs(2u) == 200u);
        CHECK(esp::worker::WorkerRestartBackoffMs(5u) == 1600u);
        CHECK(esp::worker::WorkerRestartBackoffMs(6u) == 2000u);
        CHECK(esp::worker::WorkerRestartBackoffMs(100u) == 2000u);

        CHECK(!IsWorkerScheduleSkipped(100, 102, 3));
        CHECK(NextWorkerDeadline(100, 102, 3) == 103);
        CHECK(!IsWorkerScheduleSkipped(100, 105, 3));
        CHECK(NextWorkerDeadline(100, 103, 3) == 103);
        CHECK(IsWorkerScheduleSkipped(100, 106, 3));
        CHECK(IsWorkerScheduleSkipped(100, 110, 3));
        CHECK(NextWorkerDeadline(100, 110, 3) == 110);
        CHECK(!IsWorkerScheduleSkipped(100, 299, 100));
        CHECK(IsWorkerScheduleSkipped(100, 300, 100));
    }

    void TestDeferredLanePolicy()
    {
        using esp::data::IsInventoryMetadataRetryDue;
        using esp::data::IsInventoryPlayerCoverageComplete;
        using esp::data::IsInventoryWeaponCoverageComplete;
        using esp::data::NeedsFullInventoryData;
        using esp::data::SelectDeferredLanePeakUs;
        using esp::data::ShouldIncludeFullInventoryPlayer;
        using esp::data::ShouldRunBoneLane;
        using esp::data::ShouldRunActiveInventoryLane;
        using esp::data::ShouldRunFullInventoryLane;
        using esp::data::ShouldInvalidateInventoryMetadata;

        CHECK(!ShouldRunActiveInventoryLane(false, false));
        CHECK(ShouldRunActiveInventoryLane(true, false));
        CHECK(!ShouldRunActiveInventoryLane(true, true));
        CHECK(!ShouldRunFullInventoryLane(false, false, false));
        CHECK(ShouldRunFullInventoryLane(true, false, false));
        CHECK(!ShouldRunFullInventoryLane(true, true, false));
        CHECK(!ShouldRunFullInventoryLane(true, false, true));
        CHECK(ShouldRunBoneLane(false, false, false));
        CHECK(!ShouldRunBoneLane(true, false, false));
        CHECK(!ShouldRunBoneLane(false, true, false));
        CHECK(!ShouldRunBoneLane(false, false, true));
        CHECK(!NeedsFullInventoryData(false, false, false));
        CHECK(NeedsFullInventoryData(true, false, false));
        CHECK(NeedsFullInventoryData(false, true, false));
        CHECK(NeedsFullInventoryData(false, false, true));
        CHECK(IsInventoryPlayerCoverageComplete(
            true, sizeof(int), sizeof(uintptr_t), 4, true));
        CHECK(!IsInventoryPlayerCoverageComplete(
            false, sizeof(int), sizeof(uintptr_t), 4, true));
        CHECK(!IsInventoryPlayerCoverageComplete(
            true, 0u, sizeof(uintptr_t), 4, true));
        CHECK(!IsInventoryPlayerCoverageComplete(
            true, sizeof(int), sizeof(uintptr_t), 0, true));
        CHECK(!IsInventoryPlayerCoverageComplete(
            true, sizeof(int), sizeof(uintptr_t), 4, false));
        CHECK(IsInventoryWeaponCoverageComplete(
            sizeof(uint32_t), false, false, 0u));
        CHECK(IsInventoryWeaponCoverageComplete(
            sizeof(uint32_t), true, true, 49u));
        CHECK(!IsInventoryWeaponCoverageComplete(0u, true, true, 49u));
        CHECK(!IsInventoryWeaponCoverageComplete(
            sizeof(uint32_t), true, false, 49u));
        CHECK(!IsInventoryWeaponCoverageComplete(
            sizeof(uint32_t), true, true, 0u));
        CHECK(!IsInventoryWeaponCoverageComplete(
            sizeof(uint32_t), true, true, 20000u));
        CHECK(ShouldIncludeFullInventoryPlayer(true, 3));
        CHECK(ShouldIncludeFullInventoryPlayer(false, 2));
        CHECK(ShouldIncludeFullInventoryPlayer(false, 0));
        CHECK(!ShouldIncludeFullInventoryPlayer(false, 3));
        CHECK(SelectDeferredLanePeakUs(20u, 40u, 30u, 10u) == 40u);
        CHECK(SelectDeferredLanePeakUs(0u, 0u, 0u, 0u) == 0u);

        CHECK(!ShouldInvalidateInventoryMetadata(
            0x1234u, 0x1234u, 0x1000u, 0x1000u));
        CHECK(ShouldInvalidateInventoryMetadata(
            0x1234u, 0x2234u, 0x1000u, 0x1000u));
        CHECK(ShouldInvalidateInventoryMetadata(
            0x1234u, 0x1234u, 0x1000u, 0x2000u));
        CHECK(IsInventoryMetadataRetryDue(
            true, true, 0u, 1000u, 100u));
        CHECK(!IsInventoryMetadataRetryDue(
            true, false, 0u, 1000u, 100u));
        CHECK(!IsInventoryMetadataRetryDue(
            true, true, 950u, 1000u, 100u));
        CHECK(IsInventoryMetadataRetryDue(
            true, true, 900u, 1000u, 100u));
    }

    void TestDmaRefreshPolicy()
    {
        using esp::DmaRefreshTier;
        using esp::recovery::DmaRefreshCooldownUs;
        using esp::recovery::DmaRefreshCoveredFlags;
        using esp::recovery::DmaRefreshOperations;
        using esp::recovery::DmaRefreshPendingFlag;
        using esp::recovery::DmaRefreshTierLabel;
        using esp::recovery::EvaluateDmaRefreshPolicy;
        using esp::recovery::HasDmaRefreshOperation;

        CHECK(DmaRefreshCooldownUs(DmaRefreshTier::Probe) == esp::recovery::kDmaProbeRefreshCooldownUs);
        CHECK(DmaRefreshCooldownUs(DmaRefreshTier::Repair) == esp::recovery::kDmaRepairRefreshCooldownUs);
        CHECK(DmaRefreshCooldownUs(DmaRefreshTier::Full) == esp::recovery::kDmaFullRefreshCooldownUs);
        CHECK(DmaRefreshPendingFlag(DmaRefreshTier::Probe) == 0x1u);
        CHECK(DmaRefreshPendingFlag(DmaRefreshTier::Repair) == 0x2u);
        CHECK(DmaRefreshPendingFlag(DmaRefreshTier::Full) == 0x4u);
        CHECK(DmaRefreshCoveredFlags(DmaRefreshTier::Probe) == 0x1u);
        CHECK(DmaRefreshCoveredFlags(DmaRefreshTier::Repair) == 0x3u);
        CHECK(DmaRefreshCoveredFlags(DmaRefreshTier::Full) == 0x7u);
        CHECK(std::string(DmaRefreshTierLabel(DmaRefreshTier::Probe)) == "probe");
        CHECK(std::string(DmaRefreshTierLabel(DmaRefreshTier::Repair)) == "repair");
        CHECK(std::string(DmaRefreshTierLabel(DmaRefreshTier::Full)) == "full");

        const uint32_t probeOperations = DmaRefreshOperations(DmaRefreshTier::Probe);
        CHECK(HasDmaRefreshOperation(
            probeOperations,
            esp::recovery::DmaRefreshOperationTlbPartial));
        CHECK(!HasDmaRefreshOperation(
            probeOperations,
            esp::recovery::DmaRefreshOperationProcessSpecific));

        const uint32_t repairOperations = DmaRefreshOperations(DmaRefreshTier::Repair);
        CHECK(HasDmaRefreshOperation(
            repairOperations,
            esp::recovery::DmaRefreshOperationMemoryPartial));
        CHECK(HasDmaRefreshOperation(
            repairOperations,
            esp::recovery::DmaRefreshOperationTlbFull));
        CHECK(HasDmaRefreshOperation(
            repairOperations,
            esp::recovery::DmaRefreshOperationProcessSpecific));

        const uint32_t fullOperations = DmaRefreshOperations(DmaRefreshTier::Full);
        CHECK(HasDmaRefreshOperation(
            fullOperations,
            esp::recovery::DmaRefreshOperationMemoryFull));
        CHECK(HasDmaRefreshOperation(
            fullOperations,
            esp::recovery::DmaRefreshOperationTlbFull));
        CHECK(HasDmaRefreshOperation(
            fullOperations,
            esp::recovery::DmaRefreshOperationProcessSpecific));

        {
            const auto decision = EvaluateDmaRefreshPolicy({
                .tier = DmaRefreshTier::Probe,
                .force = false,
                .nowUs = 20000000u,
                .lastRefreshUs = 0u,
                .trigger = esp::DmaRefreshTrigger::BoneSlotStale,
                .backgroundRefreshEnabled = true,
                .stableLiveScene = true,
                .activePlayerCount = 10,
                .consecutiveFailures = 0u,
                .consecutiveDegraded = 0u,
            });
            CHECK(!decision.queueRefresh);
            CHECK(decision.containedLocally);
        }

        {
            const auto decision = EvaluateDmaRefreshPolicy({
                .tier = DmaRefreshTier::Probe,
                .force = false,
                .nowUs = 20000000u,
                .lastRefreshUs = 0u,
                .trigger = esp::DmaRefreshTrigger::BoneSlotStale,
                .backgroundRefreshEnabled = true,
                .stableLiveScene = true,
                .activePlayerCount = 10,
                .consecutiveFailures =
                    esp::recovery::kBoneProbeGlobalFailureThreshold,
                .consecutiveDegraded = 0u,
            });
            CHECK(decision.queueRefresh);
            CHECK(!decision.containedLocally);
        }

        {
            const auto decision = EvaluateDmaRefreshPolicy({
                .tier = DmaRefreshTier::Probe,
                .force = true,
                .nowUs = 20000000u,
                .lastRefreshUs = 19999999u,
                .trigger = esp::DmaRefreshTrigger::BoneSlotStale,
                .backgroundRefreshEnabled = true,
                .stableLiveScene = true,
                .activePlayerCount = 10,
                .consecutiveFailures = 0u,
                .consecutiveDegraded = 0u,
            });
            CHECK(decision.queueRefresh);
            CHECK(!decision.containedLocally);
        }

        {
            const auto decision = EvaluateDmaRefreshPolicy({
                DmaRefreshTier::Repair,
                false,
                1000000u + esp::recovery::kDmaRepairRefreshCooldownUs - 1u,
                1000000u,
            });
            CHECK(!decision.queueRefresh);
            CHECK(decision.nextLastRefreshUs == 1000000u);
            CHECK(decision.cooldownUs == esp::recovery::kDmaRepairRefreshCooldownUs);
        }

        {
            const auto decision = EvaluateDmaRefreshPolicy({
                DmaRefreshTier::Repair,
                false,
                1000000u + esp::recovery::kDmaRepairRefreshCooldownUs,
                1000000u,
            });
            CHECK(decision.queueRefresh);
            CHECK(
                decision.nextLastRefreshUs ==
                1000000u + esp::recovery::kDmaRepairRefreshCooldownUs);
            CHECK(decision.pendingFlag == 0x2u);
        }

        {
            const auto decision = EvaluateDmaRefreshPolicy({
                DmaRefreshTier::Full,
                true,
                1001000u,
                1000000u,
            });
            CHECK(decision.queueRefresh);
            CHECK(decision.nextLastRefreshUs == 1001000u);
            CHECK(decision.pendingFlag == 0x4u);
        }

        {
            const auto decision = EvaluateDmaRefreshPolicy({
                DmaRefreshTier::Probe,
                false,
                999000u,
                1000000u,
            });
            CHECK(decision.queueRefresh);
            CHECK(decision.nextLastRefreshUs == 999000u);
        }
    }

    void TestDmaCacheProfile()
    {
        using esp::recovery::CacheIntervalMs;
        using esp::recovery::CacheProfileForMode;
        using esp::recovery::ShouldAttemptDmaCacheProfileApply;
        const auto& maintenance = CacheProfileForMode(esp::DmaCacheMode::Maintenance);
        const auto& live = CacheProfileForMode(esp::DmaCacheMode::Live);
        const auto& discovery = CacheProfileForMode(esp::DmaCacheMode::ProcessDiscovery);

        CHECK(maintenance.tickPeriodMs == 300u);
        CHECK(CacheIntervalMs(
            maintenance.tickPeriodMs,
            maintenance.tlbCacheTicks) == 2100u);
        CHECK(CacheIntervalMs(
            maintenance.tickPeriodMs,
            maintenance.processPartialTicks) == 30000u);
        CHECK(CacheIntervalMs(
            live.tickPeriodMs,
            live.tlbCacheTicks) == 2000u);
        CHECK(CacheIntervalMs(
            live.tickPeriodMs,
            live.processPartialTicks) == 3600000u);
        CHECK(CacheIntervalMs(
            live.tickPeriodMs,
            live.processFullTicks) == 21600000u);
        CHECK(CacheIntervalMs(
            live.tickPeriodMs,
            esp::recovery::kMemProcFsSlowRefreshTicks) == 6000000u);
        CHECK(ShouldAttemptDmaCacheProfileApply(
            true, true, true, 100u, 101u));
        CHECK(ShouldAttemptDmaCacheProfileApply(
            false, false, true, 100u, 101u));
        CHECK(!ShouldAttemptDmaCacheProfileApply(
            false, true, true, 0u, 100u));
        CHECK(ShouldAttemptDmaCacheProfileApply(
            false, true, false, 0u, 100u));
        CHECK(!ShouldAttemptDmaCacheProfileApply(
            false, true, false, 100u, 100u + esp::recovery::kDmaCacheProfileRetryUs - 1u));
        CHECK(ShouldAttemptDmaCacheProfileApply(
            false, true, false, 100u, 100u + esp::recovery::kDmaCacheProfileRetryUs));
        CHECK(discovery.tickPeriodMs == 250u);
        CHECK(CacheIntervalMs(
            discovery.tickPeriodMs,
            discovery.processPartialTicks) == 250u);
        CHECK(CacheIntervalMs(
            discovery.tickPeriodMs,
            discovery.processFullTicks) == 250u);
        CHECK(std::string(esp::recovery::DmaCacheModeName(esp::DmaCacheMode::Live)) == "live");
        CHECK(std::string(esp::recovery::DmaCacheModeName(
            esp::DmaCacheMode::ProcessDiscovery)) == "process_discovery");
    }

    void TestProcessIdentityPolicy()
    {
        using esp::recovery::ObserveProcessIdentity;
        using esp::recovery::ProcessIdentityDecision;
        using esp::recovery::ProcessIdentityTracker;

        ProcessIdentityTracker tracker = {};
        CHECK(ObserveProcessIdentity(tracker, 0u, 0u, 100u) ==
              ProcessIdentityDecision::NoProcess);
        CHECK(ObserveProcessIdentity(tracker, 0u, 42u, 1000u) ==
              ProcessIdentityDecision::PendingConfirmation);
        CHECK(ObserveProcessIdentity(
                  tracker,
                  0u,
                  42u,
                  1000u + esp::recovery::kProcessIdentityConfirmationAgeUs) ==
              ProcessIdentityDecision::ConfirmedNewProcess);

        CHECK(ObserveProcessIdentity(tracker, 42u, 0u, 2000u) ==
              ProcessIdentityDecision::PendingConfirmation);
        CHECK(ObserveProcessIdentity(tracker, 42u, 42u, 2050u) ==
              ProcessIdentityDecision::Stable);
        CHECK(tracker.observationCount == 0u);

        CHECK(ObserveProcessIdentity(tracker, 42u, 0u, 3000u) ==
              ProcessIdentityDecision::PendingConfirmation);
        CHECK(ObserveProcessIdentity(
                  tracker,
                  42u,
                  0u,
                  3000u + esp::recovery::kProcessIdentityConfirmationAgeUs - 1u) ==
              ProcessIdentityDecision::PendingConfirmation);
        CHECK(ObserveProcessIdentity(
                  tracker,
                  42u,
                  0u,
                  3000u + esp::recovery::kProcessIdentityConfirmationAgeUs) ==
              ProcessIdentityDecision::ConfirmedProcessLost);

        CHECK(ObserveProcessIdentity(tracker, 42u, 77u, 4000u) ==
              ProcessIdentityDecision::PendingConfirmation);
        CHECK(ObserveProcessIdentity(tracker, 42u, 88u, 4050u) ==
              ProcessIdentityDecision::PendingConfirmation);
        CHECK(ObserveProcessIdentity(
                  tracker,
                  42u,
                  88u,
                  4050u + esp::recovery::kProcessIdentityConfirmationAgeUs) ==
              ProcessIdentityDecision::ConfirmedReplacement);

        CHECK(ObserveProcessIdentity(tracker, 0u, 99u, 5000u, 1u, 0u) ==
              ProcessIdentityDecision::ConfirmedNewProcess);
    }

    void TestDmaRecoveryPolicy()
    {
        using esp::recovery::ShouldHardReinitializeDma;

        CHECK(!ShouldHardReinitializeDma(0));
        CHECK(!ShouldHardReinitializeDma(4));
        CHECK(ShouldHardReinitializeDma(5));
        CHECK(ShouldHardReinitializeDma(6));
        CHECK(!ShouldHardReinitializeDma(2, 3));
        CHECK(ShouldHardReinitializeDma(3, 3));
        CHECK(!ShouldHardReinitializeDma(100, 0));
    }

    void FillStandingBones(Vector3 (&bones)[esp::kPlayerStoredBoneCount])
    {
        bones[esp::PlayerStoredBoneIndex(esp::PELVIS)] = { 100.0f, 100.0f, 40.0f };
        bones[esp::PlayerStoredBoneIndex(esp::SPINE2)] = { 100.0f, 100.0f, 58.0f };
        bones[esp::PlayerStoredBoneIndex(esp::CHEST)] = { 100.0f, 100.0f, 64.0f };
        bones[esp::PlayerStoredBoneIndex(esp::HEAD)] = { 100.0f, 100.0f, 112.0f };
        bones[esp::PlayerStoredBoneIndex(esp::FOOT_HEEL_L)] = { 92.0f, 100.0f, 0.0f };
        bones[esp::PlayerStoredBoneIndex(esp::FOOT_HEEL_R)] = { 108.0f, 100.0f, 0.0f };
    }

    void TestBonePlausibility()
    {
        {
            Vector3 bones[esp::kPlayerStoredBoneCount] = {};
            bones[esp::PlayerStoredBoneIndex(esp::HEAD)] = {0, 0, 50};
            CHECK(esp::data::EvaluateStoredBonePlausibility({bones, {}, true}).plausible);
            bones[esp::PlayerStoredBoneIndex(esp::HEAD)] = {};
            CHECK(!esp::data::EvaluateStoredBonePlausibility({bones, {}, true}).plausible);
            FillStandingBones(bones);
            const esp::data::BonePoseAnchor anchor{0x10000, 100, {100, 100, 0}};
            CHECK(esp::data::HasMatchingBonePoseAnchor(anchor, 0x10000, 100));
            CHECK(!esp::data::HasMatchingBonePoseAnchor(anchor, 0x20000, 100));
            CHECK(!esp::data::HasMatchingBonePoseAnchor(anchor, 0x10000, 101));
            CHECK(!esp::data::EvaluateStoredBonePlausibility({bones, {2000, 2000, 0}, true}).plausible);
            CHECK(esp::data::EvaluateStoredBonePlausibility({bones, anchor.position, true}).plausible);
            CHECK(esp::data::EvaluateAnchoredBonePose(bones, anchor, anchor.pawn, 100).plausible);
            CHECK(!esp::data::EvaluateAnchoredBonePose(bones, anchor, anchor.pawn, 101).plausible);
            auto corruptAnchor = anchor;
            corruptAnchor.position.z = std::numeric_limits<float>::quiet_NaN();
            CHECK(esp::data::EvaluateAnchoredBonePose(bones, corruptAnchor, anchor.pawn, 100).rejectReason ==
                esp::data::BonePlausibilityRejectReason::InvalidPoseAnchor);
            CHECK(!esp::data::EvaluateAnchoredBonePose(bones, {}, anchor.pawn, 100).plausible);
            auto currentAnchor = anchor;
            currentAnchor.sampleTimeUs = 101;
            currentAnchor.position.x += 256;
            CHECK(esp::data::EvaluateRetainedBonePose(bones, anchor, currentAnchor, anchor.pawn, 100).rejectReason ==
                esp::data::BonePlausibilityRejectReason::TooFarFromAnchor);
            currentAnchor.position.x = anchor.position.x + 8;
            CHECK(esp::data::EvaluateRetainedBonePose(bones, anchor, currentAnchor, anchor.pawn, 100).plausible);
            CHECK(!esp::data::EvaluateRetainedBonePose(bones, anchor, currentAnchor, 0x20000, 100).plausible);
            bones[esp::PlayerStoredBoneIndex(esp::PELVIS)] = {};
            bones[esp::PlayerStoredBoneIndex(esp::HEAD)] = {0, 0, 72};
            bones[esp::PlayerStoredBoneIndex(esp::CHEST)] = {0, 0, 48};
            CHECK(esp::data::EvaluateAnchoredBonePose(bones, {anchor.pawn, 100, {}}, anchor.pawn, 100).plausible);
            CHECK(esp::data::IsReusableBoneSample(100, 950, 850));
            CHECK(!esp::data::IsReusableBoneSample(100, 951, 850));
            CHECK(!esp::data::IsReusableBoneSample(100, 99, 850));
            CHECK(!esp::data::IsReusableBoneSample(0, 1, 850));
        }
        using esp::data::BonePlausibilityRejectReasonName;
        using esp::data::BonePlausibilityRejectReason;
        using esp::data::EvaluateStoredBonePlausibility;
        using esp::data::SelectReportedBoneRejectReason;
        using esp::data::ShouldClearCurrentBoneScratch;
        using esp::data::ShouldHoldBoneAnchorMismatch;
        using esp::data::ShouldReportBoneReject;

        CHECK(std::string(BonePlausibilityRejectReasonName(BonePlausibilityRejectReason::None)) == "none");
        CHECK(std::string(BonePlausibilityRejectReasonName(BonePlausibilityRejectReason::HeadNotAbovePelvis)) == "head_not_above_pelvis");
        CHECK(std::string(BonePlausibilityRejectReasonName(static_cast<BonePlausibilityRejectReason>(999))) == "unknown");

        {
            Vector3 bones[esp::kPlayerStoredBoneCount] = {};
            FillStandingBones(bones);
            const auto result = EvaluateStoredBonePlausibility({
                bones,
                { 100.0f, 100.0f, 40.0f },
                true,
            });
            CHECK(result.plausible);
            CHECK(result.rejectReason == BonePlausibilityRejectReason::None);
        }

        {
            Vector3 bones[esp::kPlayerStoredBoneCount] = {};
            FillStandingBones(bones);
            bones[esp::PlayerStoredBoneIndex(esp::HEAD)] = { 100.0f, 100.0f, 20.0f };
            const auto result = EvaluateStoredBonePlausibility({
                bones,
                { 100.0f, 100.0f, 40.0f },
                true,
            });
            CHECK(!result.plausible);
            CHECK(result.rejectReason == BonePlausibilityRejectReason::HeadNotAbovePelvis);
        }

        {
            Vector3 bones[esp::kPlayerStoredBoneCount] = {};
            FillStandingBones(bones);
            bones[esp::PlayerStoredBoneIndex(esp::FOOT_HEEL_L)] = { 92.0f, 100.0f, 70.0f };
            const auto result = EvaluateStoredBonePlausibility({
                bones,
                { 100.0f, 100.0f, 40.0f },
                true,
            });
            CHECK(!result.plausible);
            CHECK(result.rejectReason == BonePlausibilityRejectReason::HeelTooHigh);
        }

        {
            Vector3 bones[esp::kPlayerStoredBoneCount] = {};
            FillStandingBones(bones);
            const auto result = EvaluateStoredBonePlausibility({
                bones,
                { 1000.0f, 1000.0f, 40.0f },
                true,
            });
            CHECK(!result.plausible);
            CHECK(result.rejectReason == BonePlausibilityRejectReason::TooFarFromAnchor);
        }

        CHECK(SelectReportedBoneRejectReason(
            BonePlausibilityRejectReason::TooFarFromAnchor,
            BonePlausibilityRejectReason::None,
            BonePlausibilityRejectReason::MissingInput) == BonePlausibilityRejectReason::TooFarFromAnchor);
        CHECK(SelectReportedBoneRejectReason(
            BonePlausibilityRejectReason::MissingInput,
            BonePlausibilityRejectReason::HeelTooHigh,
            BonePlausibilityRejectReason::TooFarFromAnchor) == BonePlausibilityRejectReason::HeelTooHigh);
        CHECK(SelectReportedBoneRejectReason(
            BonePlausibilityRejectReason::MissingInput,
            BonePlausibilityRejectReason::MissingInput,
            BonePlausibilityRejectReason::TooFarFromAnchor) == BonePlausibilityRejectReason::TooFarFromAnchor);
        CHECK(ShouldClearCurrentBoneScratch(BonePlausibilityRejectReason::TooFarFromAnchor));
        CHECK(ShouldClearCurrentBoneScratch(BonePlausibilityRejectReason::InvalidCoreBones));
        CHECK(!ShouldClearCurrentBoneScratch(BonePlausibilityRejectReason::HeelTooHigh));
        CHECK(ShouldHoldBoneAnchorMismatch(true, true, 100u, 100u));
        CHECK(ShouldHoldBoneAnchorMismatch(
            true,
            true,
            100u,
            100u + esp::data::kBoneAnchorMismatchHoldUs));
        CHECK(!ShouldHoldBoneAnchorMismatch(
            true,
            true,
            100u,
            101u + esp::data::kBoneAnchorMismatchHoldUs));
        CHECK(!ShouldHoldBoneAnchorMismatch(false, true, 100u, 100u));
        CHECK(!ShouldHoldBoneAnchorMismatch(true, false, 100u, 100u));
        CHECK(!ShouldReportBoneReject(
            BonePlausibilityRejectReason::TooFarFromAnchor,
            1u,
            true,
            true));
        CHECK(ShouldReportBoneReject(
            BonePlausibilityRejectReason::TooFarFromAnchor,
            2u,
            false,
            true));
        CHECK(!ShouldReportBoneReject(
            BonePlausibilityRejectReason::InvalidChestOrder,
            2u,
            true,
            true));
        CHECK(ShouldReportBoneReject(
            BonePlausibilityRejectReason::InvalidChestOrder,
            3u,
            true,
            false));
        CHECK(!ShouldReportBoneReject(
            BonePlausibilityRejectReason::MissingInput,
            255u,
            true,
            true));
    }

    void TestResetPolicy()
    {
        using esp::RuntimeResetKind;
        using esp::recovery::EvaluateResetPolicy;
        using esp::recovery::ResetTrigger;

        {
            const auto decision = EvaluateResetPolicy(ResetTrigger::UserRequestedRefresh);
            CHECK(decision.kind == RuntimeResetKind::Soft);
            CHECK(!decision.publishClearedSnapshot);
        }

        {
            const auto decision = EvaluateResetPolicy(ResetTrigger::ZeroPlayersRepair);
            CHECK(decision.kind == RuntimeResetKind::Soft);
            CHECK(!decision.publishClearedSnapshot);
        }

        {
            const auto decision = EvaluateResetPolicy(ResetTrigger::PopulationWatchdogStaleCommitted);
            CHECK(decision.kind == RuntimeResetKind::Soft);
            CHECK(!decision.publishClearedSnapshot);
        }

        {
            const auto decision = EvaluateResetPolicy(ResetTrigger::DmaRecoverySuccess);
            CHECK(decision.kind == RuntimeResetKind::Hard);
            CHECK(decision.publishClearedSnapshot);
        }

        {
            const auto decision = EvaluateResetPolicy(ResetTrigger::RequiredReadsPersistent);
            CHECK(decision.kind == RuntimeResetKind::Hard);
            CHECK(decision.publishClearedSnapshot);
        }

        {
            const auto decision = EvaluateResetPolicy(ResetTrigger::WaitForProcess);
            CHECK(decision.kind == RuntimeResetKind::Hard);
            CHECK(decision.publishClearedSnapshot);
        }

        {
            const auto decision = EvaluateResetPolicy(ResetTrigger::SceneTransition);
            CHECK(decision.kind == RuntimeResetKind::Hard);
            CHECK(decision.publishClearedSnapshot);
        }
    }

    void TestInputDevicePolicy()
    {
        // A delayed down sample must never mask physical UP. Test two full
        // Toggle cycles with primary input stuck down throughout.
        target::policy::ActivationState activation;
        for (int cycle = 0; cycle < 4; ++cycle) {
            auto key = app::input::SelectActivationKeyState({true, true}, {true, true});
            const bool active = target::policy::UpdateActivation(activation, true, 2, 1,
                key.down, key.available, false);
            CHECK(active == (cycle % 2 == 0));
            key = app::input::SelectActivationKeyState({true, false}, {true, true});
            CHECK(key.available && !key.down);
            CHECK(target::policy::UpdateActivation(activation, true, 2, 1,
                key.down, key.available, false) == active);
        }
        CHECK(app::input::SelectActivationKeyState({}, {true, true}).down);
        CHECK(!app::input::SelectActivationKeyState({}, {}).available);
        CHECK(!app::input::IsLocalControlKeyDown(0x71, 1)); // unreliable low bit ignored
        CHECK(!app::input::IsLocalControlKeyDown(0x71, 0)); // Shift does not supply F2 state
        CHECK(app::input::IsLocalControlKeyDown(0x71, static_cast<short>(0x8000)));
        CHECK(!app::input::IsLocalControlKeyDown(-1, static_cast<short>(0x8000)));
        using app::input::BuildCircularTestPath;
        using app::input::DeviceKind;
        using app::input::IsMakcuHardwareId;
        using app::input::IsMakcuVersionResponse;
        using app::input::IsKmBoxSerialHardwareId;
        using app::input::IsSelectableDeviceKind;
        using app::input::IsSuccessfulKmCommandResponse;
        using app::input::IsValidIpv4Address;
        using app::input::IsValidKmBoxNetworkConfig;
        using app::input::IsValidDeviceKind;
        using app::input::MakcuButtonStreamParser;
        using app::input::ParseKmBoxHardwareKey;
        using app::input::SanitizeDeviceKind;
        using app::input::SanitizeSelectableDeviceKind;

        CHECK(IsValidDeviceKind(0));
        CHECK(IsValidDeviceKind(3));
        CHECK(!IsValidDeviceKind(-1));
        CHECK(IsValidDeviceKind(4));
        CHECK(!IsValidDeviceKind(5));
        CHECK(IsSelectableDeviceKind(4));
        CHECK(!IsSelectableDeviceKind(5));
        CHECK(SanitizeSelectableDeviceKind(4) == DeviceKind::FerrumOne);
        CHECK(app::input::IsNetworkDeviceKind(DeviceKind::FerrumOne));
        CHECK(app::input::IsNetworkDeviceKind(DeviceKind::KmBoxNet));
        CHECK(!app::input::IsNetworkDeviceKind(DeviceKind::KmBox));
        CHECK(!app::input::IsNetworkDeviceKind(DeviceKind::Makcu));
        CHECK(!app::input::IsNetworkDeviceKind(DeviceKind::None));
        CHECK(SanitizeDeviceKind(1) == DeviceKind::Makcu);
        CHECK(SanitizeDeviceKind(99) == DeviceKind::None);
        CHECK(!IsSelectableDeviceKind(0));
        CHECK(IsSelectableDeviceKind(1));
        CHECK(IsSelectableDeviceKind(3));
        CHECK(SanitizeSelectableDeviceKind(0) == DeviceKind::Makcu);
        CHECK(std::strcmp(
            app::input::DeviceKindLabel(DeviceKind::KmBox),
            "KMBox Serial") == 0);
        CHECK(std::strcmp(
            app::input::DeviceKindLabel(DeviceKind::KmBoxNet),
            "KMBox Network") == 0);
        CHECK(IsMakcuHardwareId(L"USB\\VID_1A86&PID_55D3\\MAKCU"));
        CHECK(IsMakcuHardwareId(L"usb\\vid_1a86&pid_55d3\\makcu"));
        CHECK(!IsMakcuHardwareId(L"USB\\VID_0000&PID_0000"));
        CHECK(IsKmBoxSerialHardwareId(L"USB\\VID_1A86&PID_7523\\KMBOX"));
        CHECK(!IsKmBoxSerialHardwareId(L"USB\\VID_1A86&PID_55D3\\MAKCU"));
        CHECK(IsMakcuVersionResponse("km.MAKCU\r\n>>> "));
        CHECK(!IsMakcuVersionResponse("km.version()\r\n>>> "));
        CHECK(IsSuccessfulKmCommandResponse("km.move(0,0)\r\n>>> "));
        CHECK(!IsSuccessfulKmCommandResponse("Traceback: AttributeError\r\n>>> "));
        uint32_t hardwareKey = 0;
        CHECK(ParseKmBoxHardwareKey("A1B2C3D4", &hardwareKey));
        CHECK(hardwareKey == 0xA1B2C3D4u);
        CHECK(ParseKmBoxHardwareKey("  0x0000000  ", &hardwareKey));
        CHECK(hardwareKey == 0u);
        CHECK(!ParseKmBoxHardwareKey("123456789", &hardwareKey));
        CHECK(!ParseKmBoxHardwareKey("not-hex", &hardwareKey));
        CHECK(IsValidIpv4Address("192.168.2.188"));
        CHECK(IsValidIpv4Address("0.0.0.0"));
        CHECK(!IsValidIpv4Address("192.168.2"));
        CHECK(!IsValidIpv4Address("192.168.2.256"));
        CHECK(!IsValidIpv4Address("host.local"));
        CHECK(IsValidKmBoxNetworkConfig(
            "192.168.2.188",
            6234,
            "A1B2C3D4"));
        CHECK(!IsValidKmBoxNetworkConfig(
            "192.168.2.256",
            6234,
            "A1B2C3D4"));
        CHECK(!IsValidKmBoxNetworkConfig(
            "192.168.2.188",
            0,
            "A1B2C3D4"));
        CHECK(!IsValidKmBoxNetworkConfig(
            "192.168.2.188",
            70000,
            "A1B2C3D4"));
        CHECK(!IsValidKmBoxNetworkConfig(
            "192.168.2.188",
            6234,
            "invalid"));

        CHECK(app::input::VirtualKeyToMouseButtonMask(0x01) == 0x01);
        CHECK(app::input::VirtualKeyToMouseButtonMask(0x02) == 0x02);
        CHECK(app::input::VirtualKeyToMouseButtonMask(0x04) == 0x04);
        CHECK(app::input::VirtualKeyToMouseButtonMask(0x05) == 0x08);
        CHECK(app::input::VirtualKeyToMouseButtonMask(0x06) == 0x10);
        CHECK(app::input::VirtualKeyToMouseButtonMask(0x20) == 0);

        MakcuButtonStreamParser parser;
        uint8_t buttonMask = 0xFF;
        constexpr std::array<uint8_t, 9> stream = {
            '>', 'k', 'm', '.', 0x10, 'k', 'm', '.', 0x00
        };
        int parsedEvents = 0;
        for (const uint8_t value : stream) {
            if (parser.Consume(value, &buttonMask))
                ++parsedEvents;
        }
        CHECK(parsedEvents == 2);
        CHECK(buttonMask == 0x00);
        constexpr std::array<uint8_t, 8> binaryMasks = {
            'k', 'm', '.', '\r', 'k', 'm', '.', '\n'
        };
        for (const uint8_t value : binaryMasks)
            parser.Consume(value, &buttonMask);
        CHECK(buttonMask == 0x0A);
        parser.Reset();
        CHECK(parser.Mask() == 0);

        const auto path = BuildCircularTestPath(28, 48);
        CHECK(path.size() == 50u);
        int totalX = 0;
        int totalY = 0;
        bool hasHorizontalMovement = false;
        bool hasVerticalMovement = false;
        for (const auto& delta : path) {
            totalX += delta.x;
            totalY += delta.y;
            hasHorizontalMovement = hasHorizontalMovement || delta.x != 0;
            hasVerticalMovement = hasVerticalMovement || delta.y != 0;
        }
        CHECK(totalX == 0);
        CHECK(totalY == 0);
        CHECK(hasHorizontalMovement);
        CHECK(hasVerticalMovement);

        const auto defaultPath = BuildCircularTestPath();
        CHECK(defaultPath.size() == 74u);
        int defaultX = 0;
        int defaultY = 0;
        int maximumDistance = 0;
        for (const auto& delta : defaultPath) {
            defaultX += delta.x;
            defaultY += delta.y;
            maximumDistance = std::max(
                maximumDistance,
                static_cast<int>(std::lround(std::hypot(defaultX, defaultY))));
        }
        CHECK(maximumDistance == app::input::kMovementTestRadius);
        CHECK(defaultX == 0);
        CHECK(defaultY == 0);
    }

    void TestMakcuHardwareIfRequested()
    {
        char enabled[8] = {};
        size_t enabledLength = 0;
        if (getenv_s(
                &enabledLength,
                enabled,
                sizeof(enabled),
                "KEVQ_MAKCU_SMOKE") != 0 ||
            std::strcmp(enabled, "1") != 0) {
            return;
        }

        using app::input::ConnectionState;
        using app::input::DeviceKind;
        app::input::SetSelectedDevice(DeviceKind::Makcu);
        CHECK(app::input::RequestConnectAndTest());

        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(4);
        app::input::DeviceStatus status;
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            status = app::input::GetDeviceStatus();
        } while (status.state != ConnectionState::Connected &&
                 status.state != ConnectionState::Error &&
                 std::chrono::steady_clock::now() < deadline);

        if (status.state != ConnectionState::Connected) {
            std::cerr << "MAKCU smoke failed: state="
                      << static_cast<int>(status.state)
                      << " error=" << static_cast<int>(status.error)
                      << " win32=" << status.systemError << '\n';
        }
        CHECK(status.state == ConnectionState::Connected);
        CHECK(!status.port.empty());
        CHECK(status.port.starts_with("COM"));

        CHECK(app::input::RequestMovementTest());
        const auto movementDeadline = std::chrono::steady_clock::now() +
                                      std::chrono::seconds(2);
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            status = app::input::GetDeviceStatus();
        } while (status.state != ConnectionState::Connected &&
                 status.state != ConnectionState::Error &&
                 std::chrono::steady_clock::now() < movementDeadline);
        if (status.state != ConnectionState::Connected) {
            std::cerr << "MAKCU repeat movement failed: state="
                      << static_cast<int>(status.state)
                      << " error=" << static_cast<int>(status.error)
                      << " win32=" << status.systemError << '\n';
        }
        CHECK(status.state == ConnectionState::Connected);
        CHECK(status.port.starts_with("COM"));

        app::input::RequestDisconnect();
        app::input::Shutdown();
    }

    void TestTargetPolicy()
    {
        CHECK(target::policy::ResolveConfiguredFov(77.0f, false, 180.0f) == 77.0f);
        CHECK(target::policy::ResolveConfiguredFov(77.0f, true, 180.0f) == 180.0f);
        CHECK(target::policy::ResolveConfiguredFov(77.0f, true, std::nanf("")) == 77.0f);
        using target::policy::ResolveFovCircle;
        using target::policy::SanitizeFovRadius;

        CHECK(SanitizeFovRadius(150.0f) == 150.0f);
        CHECK(SanitizeFovRadius(5.0f) == 5.0f);
        CHECK(SanitizeFovRadius(4.0f) == target::policy::kDefaultFovRadius);
        CHECK(SanitizeFovRadius(
            std::numeric_limits<float>::quiet_NaN()) ==
            target::policy::kDefaultFovRadius);

        const auto fullHd = ResolveFovCircle(1920.0f, 1080.0f, 150.0f);
        CHECK(fullHd.valid);
        CHECK(fullHd.centerX == 960.0f);
        CHECK(fullHd.centerY == 540.0f);
        CHECK(fullHd.radius == 150.0f);

        const auto constrained = ResolveFovCircle(200.0f, 100.0f, 150.0f);
        CHECK(constrained.valid);
        CHECK(constrained.radius == 48.0f);
        CHECK(!ResolveFovCircle(0.0f, 1080.0f, 150.0f).valid);
        CHECK(target::policy::SanitizeSmoothing(1.0f) == 1.0f);
        CHECK(target::policy::SanitizeSmoothing(51.0f) == 5.0f);
        CHECK(target::policy::SanitizeAimBone(4) == 4);
        CHECK(target::policy::SanitizeAimBone(5) == 0);
        CHECK(target::policy::SanitizeDelayMs(500) == 500);
        CHECK(target::policy::SanitizeDelayMs(501) == 10);
        CHECK(target::policy::SanitizeActivationMode(0) == 0);
        CHECK(target::policy::SanitizeActivationMode(1) == 1);
        CHECK(target::policy::SanitizeActivationMode(2) == 0);
        const auto held = target::policy::ResolveActivationState(
            true, 0, false, false);
        CHECK(held.active && held.wasDown && !held.toggled);
        const auto toggledOn = target::policy::ResolveActivationState(
            true, 1, false, false);
        CHECK(toggledOn.active && toggledOn.toggled);
        const auto toggledHeld = target::policy::ResolveActivationState(
            true, 1, toggledOn.wasDown, toggledOn.toggled);
        CHECK(toggledHeld.active && toggledHeld.toggled);
        const auto toggledReleased = target::policy::ResolveActivationState(
            false, 1, toggledHeld.wasDown, toggledHeld.toggled);
        CHECK(toggledReleased.active && toggledReleased.toggled);
        const auto toggledOff = target::policy::ResolveActivationState(
            true, 1, toggledReleased.wasDown, toggledReleased.toggled);
        CHECK(!toggledOff.active && !toggledOff.toggled);
        target::policy::ActivationState activation;
        CHECK(target::policy::UpdateActivation(activation, true, 6, 1, true, true, false));
        CHECK(!target::policy::UpdateActivation(activation, true, 6, 1, false, false, false));
        CHECK(target::policy::UpdateActivation(activation, true, 6, 1, true, true, false));
        CHECK(!target::policy::UpdateActivation(activation, true, 6, 1, false, true, true));
        CHECK(activation.toggled);
        CHECK(target::policy::UpdateActivation(activation, true, 6, 1, false, true, false));
        CHECK(!target::policy::UpdateActivation(activation, true, 5, 1, true, true, false));
        CHECK(!activation.toggled); // rebinding a held key must not arm Toggle
        CHECK(!target::policy::UpdateActivation(activation, true, 5, 1, false, true, false));
        CHECK(target::policy::UpdateActivation(activation, true, 5, 1, true, true, false));
        CHECK(!target::policy::UpdateActivation(activation, false, 5, 1, true, true, false));
        CHECK(!target::policy::UpdateActivation(activation, true, 5, 1, true, true, false));
        CHECK(target::policy::UpdateActivation(activation, true, 5, 0, true, true, false));
        CHECK(!activation.toggled);
        CHECK(!target::policy::UpdateActivation(activation, true, 5, 1, true, true, false));
        const float oneStep = target::policy::TimeAdjustedSmoothing(0.2f, 1.0f / 64.0f);
        const float halfStep = target::policy::TimeAdjustedSmoothing(0.2f, 1.0f / 128.0f);
        CHECK(std::fabs(oneStep - (1.0f - (1.0f - halfStep) * (1.0f - halfStep))) < 0.000001f);
        CHECK(target::policy::TimeAdjustedSmoothing(1.0f, 1.0f / 128.0f) == 1.0f);
        CHECK(target::policy::TimeAdjustedSmoothing(0.2f, 0.0f) == 0.0f);
        CHECK(target::policy::TimeAdjustedSmoothing(std::nanf(""), 0.01f) == 0.0f);
        CHECK(target::policy::IsSameTargetIdentity(0x100, 1, 111, 0x100, 1, 222, 0x10001, 0x10001));
        CHECK(!target::policy::IsSameTargetIdentity(0x100, 1, 111, 0x100, 1, 111, 0x10001, 0x20001));
        const auto uncompensated = target::policy::ResolveCompensatedAimDelta(
            10.0f, 20.0f, 8.0f, 17.0f,
            1.0f, -0.5f, 2.0f, 2.0f, false);
        CHECK(uncompensated.valid);
        CHECK(uncompensated.pitch == 2.0f);
        CHECK(uncompensated.yaw == 3.0f);
        CHECK(target::policy::ResolveCompensatedAimDelta(10, 20, 8, 17,
            std::nanf(""), std::nanf(""), 2, 2, false).valid);
        CHECK(!target::policy::ResolveCompensatedAimDelta(10, 20, 8, 17,
            std::nanf(""), std::nanf(""), 2, 2, true).valid);
        const auto compensated = target::policy::ResolveCompensatedAimDelta(
            10.0f, 20.0f, 8.0f, 17.0f,
            1.0f, -0.5f, 2.0f, 2.0f, true);
        CHECK(compensated.valid);
        CHECK(compensated.pitch == 0.0f);
        CHECK(compensated.yaw == 4.0f);
        CHECK(std::fabs(target::policy::ResolvePredictionSeconds(
            2000, 40) - 0.026f) < 0.0001f);
        CHECK(target::policy::ResolvePredictionSeconds(
            UINT64_MAX, 999, 1.0f) ==
            target::policy::kMaximumPredictionSeconds);
        CHECK(target::policy::ShouldKeepLockedTarget(111.0f, 100.0f));
        CHECK(!target::policy::ShouldKeepLockedTarget(113.0f, 100.0f));
        CHECK(std::fabs(target::policy::UpdateCorrelatedNoise(
            0.05f, 0.0f) - 0.041f) < 0.0001f);
        CHECK(target::policy::UpdateCorrelatedNoise(
            0.0f, 10.0f, 0.0f, 0.2f) == 0.2f);
        CHECK(std::fabs(target::policy::ResolveNoiseCorrelation(
            0.075f) - std::exp(-1.0f)) < 0.0001f);
        CHECK(target::policy::ResolveNoiseCorrelation(1.0f / 240.0f) >
              target::policy::ResolveNoiseCorrelation(1.0f / 60.0f));
        CHECK(target::policy::ResolveNoiseCorrelation(0.0f) == 0.0f);
        CHECK(target::policy::NeedsBoneData(true, true));
        CHECK(!target::policy::NeedsBoneData(true, false));
        CHECK(target::policy::NeedsBoneData(true, false, true, true));
        CHECK(target::policy::NeedsBoneData(true, false, true, false));
        CHECK(target::policy::NeedsVisibilityData(
            true, true, true, false, false));
        CHECK(target::policy::NeedsVisibilityData(
            true, false, false, true, true));
        CHECK(!target::policy::NeedsVisibilityData(
            false, true, true, true, true));
        CHECK(target::policy::NeedsVisibilityData(
            true, true, false, true, false));
        CHECK(target::policy::NeedsVisibilityData(
            true, false, false, true, false, true));
        CHECK(target::policy::IsFreshSample(900u, 1000u, 100u));
        CHECK(!target::policy::IsFreshSample(899u, 1000u, 100u));
        CHECK(!target::policy::IsFreshSample(1001u, 1000u, 100u));
        CHECK(std::fabs(target::policy::ResolveHeadAimCoordinate(
            10.0f, 0.0f) - 8.5f) < 0.0001f);
        CHECK(target::policy::ResolveHeadAimCoordinate(
            10.0f, 9.0f) == 9.85f);
        CHECK(target::policy::ClampAimStepToTarget(0.5f, 0.2f) == 0.2f);
        CHECK(target::policy::ClampAimStepToTarget(-0.5f, 0.2f) == 0.0f);
        CHECK(target::policy::ResolveAlignmentToleranceDegrees(
            1.0f, 1.0f) >= 0.02f);
        const float nearPrecisionTolerance =
            target::policy::ResolvePrecisionAlignmentToleranceDegrees(
                2.0f, 1.0f, 500.0f, 2.75f);
        const float farPrecisionTolerance =
            target::policy::ResolvePrecisionAlignmentToleranceDegrees(
                2.0f, 1.0f, 8000.0f, 2.75f);
        CHECK(nearPrecisionTolerance <= 0.032f);
        CHECK(farPrecisionTolerance < nearPrecisionTolerance);
        CHECK(farPrecisionTolerance >= 0.006f);
        CHECK(target::policy::IsTimestampSkewAcceptable(
            1000u, 1010u, 10u));
        CHECK(!target::policy::IsTimestampSkewAcceptable(
            1000u, 1011u, 10u));
        CHECK(target::policy::AdvanceStableSamples(
            1u, 8u, 8u, true) == 1u);
        // A generation that was already observed must not become stable merely
        // because alignment changed without a newly published sample.
        CHECK(target::policy::AdvanceStableSamples(
            0u, 8u, 8u, true) == 0u);
        CHECK(target::policy::AdvanceStableSamples(
            1u, 9u, 8u, true) == 2u);
        CHECK(target::policy::AdvanceStableSamples(
            2u, 10u, 9u, false) == 0u);
        CHECK(target::policy::IsDeterministicSeedWindowReady(
            3, 2, true, 0.03f));
        CHECK(!target::policy::IsDeterministicSeedWindowReady(
            3, 2, false, 0.50f));
        CHECK(!target::policy::IsDeterministicSeedWindowReady(
            3, 1, true, 0.50f));
        CHECK(!target::policy::IsDeterministicSeedWindowReady(
            3, 3, true, 0.029f));
        CHECK(!target::policy::ShouldHoldCapsuleAim(
            true, 75.0f, 80.0f));
        CHECK(target::policy::ShouldHoldCapsuleAim(
            true, 80.0f, 80.0f));
        CHECK(!target::policy::ShouldHoldCapsuleAim(
            true, 79.9f, 80.0f));
        CHECK(!target::policy::ShouldHoldCapsuleAim(
            false, 80.0f, 80.0f));
        CHECK(!target::policy::ShouldHoldCapsuleAim(
            true, 74.9f, 80.0f));
        CHECK(target::policy::IsMarginalSeedWindowReady(
            75.0f, 80.0f, 3, 2));
        CHECK(!target::policy::IsMarginalSeedWindowReady(
            75.0f, 80.0f, 3, 1));
        CHECK(!target::policy::IsMarginalSeedWindowReady(
            74.9f, 80.0f, 3, 3));
        const uint32_t targetNameHash =
            target::policy::HashTargetName("same-player");
        CHECK(targetNameHash != 0u);
        CHECK(target::policy::HashTargetName("same-player") ==
              targetNameHash);
        CHECK(target::policy::IsSameTargetIdentity(
            0x111u, 7, targetNameHash,
            0x111u, 7, targetNameHash));
        CHECK(target::policy::IsSameTargetIdentity(
            0x111u, 7, targetNameHash,
            0x111u, 7, 0u));
        CHECK(!target::policy::IsSameTargetIdentity(
            0x111u, 7, targetNameHash,
            0x222u, 7, targetNameHash));
        CHECK(!target::policy::IsSameTargetIdentity(
            0x111u, 7, targetNameHash,
            0x111u, 8, targetNameHash));
        CHECK(target::policy::IsAmmoConsumptionObserved(
            12, 100u, 11, true, 101u));
        CHECK(!target::policy::IsAmmoConsumptionObserved(
            12, 100u, 12, true, 101u));
        CHECK(target::policy::IsShotsFiredAdvanceObserved(
            0, true, 100u, 1, true, 101u));
        CHECK(target::policy::IsLastShotTimeAdvanceObserved(
            5.0f, true, 100u, 5.1f, true, 101u));
        CHECK(target::policy::AdvancePostShotCoreSamples(
            0u, 201u, 0u, 200u, true, true, true) == 1u);
        CHECK(target::policy::AdvancePostShotCoreSamples(
            1u, 201u, 201u, 200u, true, true, true) == 1u);
        CHECK(target::policy::AdvancePostShotCoreSamples(
            1u, 202u, 201u, 200u, true, true, true) == 2u);
        CHECK(target::policy::AdvanceMissingTargetSamples(
            0u, 301u, 300u, true, true) == 1u);
        CHECK(target::policy::ResolveTriggerTargetOutcome(
            true, true, false, true, 0, 1u, 0u) ==
            target::policy::TriggerTargetOutcome::DeadOrGone);
        CHECK(target::policy::ResolveTriggerTargetOutcome(
            true, true, false, true, 30, 2u, 0u) ==
            target::policy::TriggerTargetOutcome::AliveConfirmed);
        CHECK(target::policy::ResolveTriggerTargetOutcome(
            true, false, false, false, 0, 0u, 2u) ==
            target::policy::TriggerTargetOutcome::DeadOrGone);
        CHECK(target::policy::IsRecoilDeltaStable(
            1.02f, -0.98f, 1.0f, -1.0f));
        CHECK(!target::policy::IsRecoilDeltaStable(
            1.2f, -1.0f, 1.0f, -1.0f));
        CHECK(target::policy::IsPostShotRecoilReady(
            true, 0, false, 0u));
        CHECK(target::policy::IsPostShotRecoilReady(
            true, 1, true,
            target::policy::kRequiredPostShotRecoilSamples));
        CHECK(!target::policy::ShouldRetryUnobservedClick(
            true, false, true, true, true, 0u));
        CHECK(target::policy::ResolvePostShotOutcomeDelaySeconds(
            40, 0.015f, 1.0f / 64.0f, 0.012f) >= 0.075f);
        CHECK(target::policy::ResolveUnobservedClickTimeoutSeconds(
            0.012f, 0.018f) >= 0.090f);
        CHECK(target::policy::IsBetterPlannedHitboxChoice(
            1, 1, 80.0f, 4.0f, true, 2, 120.0f, 1.0f));
        CHECK(!target::policy::IsBetterPlannedHitboxChoice(
            1, 2, 120.0f, 1.0f, true, 1, 80.0f, 4.0f));
        CHECK(target::policy::IsBetterPlannedHitboxChoice(
            1, 1, 90.0f, 6.0f, true, 1, 80.0f, 2.0f));
        CHECK(target::policy::IsBetterPlannedHitboxChoice(
            0, 2, 40.0f, 1.0f, true, 1, 100.0f, 2.0f));
        CHECK(!target::policy::IsBetterPlannedHitboxChoice(
            0, 2, std::numeric_limits<float>::quiet_NaN(), 1.0f,
            false, 0, 0.0f, 0.0f));
        CHECK(target::policy::IsTriggerFireGateOpen(
            true, true, true, true, true, true, false, false, true));
        CHECK(!target::policy::IsTriggerFireGateOpen(
            true, true, false, true, true, true, false, false, true));
        CHECK(!target::policy::IsTriggerFireGateOpen(
            true, true, true, true, true, true, true, false, true));

        const auto rightTarget = target::policy::ResolveMouseMove(
            0.0f,
            10.0f,
            2.0f,
            1.0f,
            5.0f,
            0.0f,
            0.0f);
        CHECK(rightTarget.valid);
        CHECK(rightTarget.x < 0);
        CHECK(rightTarget.y == 0);

        const auto lowerTarget = target::policy::ResolveMouseMove(
            5.0f,
            0.0f,
            2.0f,
            1.0f,
            5.0f,
            0.0f,
            0.0f);
        CHECK(lowerTarget.valid);
        CHECK(lowerTarget.x == 0);
        CHECK(lowerTarget.y > 0);

        const auto clampedTarget = target::policy::ResolveMouseMove(
            100.0f,
            100.0f,
            1.0f,
            1.0f,
            1.0f,
            0.0f,
            0.0f);
        CHECK(clampedTarget.valid);
        CHECK(clampedTarget.x == -target::policy::kMaximumMouseStep);
        CHECK(clampedTarget.y == target::policy::kMaximumMouseStep);
        CHECK(std::fabs(clampedTarget.remainderX) <= 0.5f);
        CHECK(std::fabs(clampedTarget.remainderY) <= 0.5f);

        CHECK(!target::policy::ResolveMouseMove(
            1.0f,
            1.0f,
            0.0f,
            1.0f,
            5.0f,
            0.0f,
            0.0f).valid);
    }

    void TestKmBoxNetLoopback(app::input::DeviceKind device)
    {
        WSADATA winsock = {};
        CHECK(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);

        const SOCKET server = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        CHECK(server != INVALID_SOCKET);
        if (server == INVALID_SOCKET) {
            WSACleanup();
            return;
        }

        const DWORD timeoutMs = 100;
        CHECK(setsockopt(
            server,
            SOL_SOCKET,
            SO_RCVTIMEO,
            reinterpret_cast<const char*>(&timeoutMs),
            sizeof(timeoutMs)) == 0);

        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        CHECK(bind(
            server,
            reinterpret_cast<const sockaddr*>(&address),
            sizeof(address)) == 0);

        int addressLength = sizeof(address);
        CHECK(getsockname(
            server,
            reinterpret_cast<sockaddr*>(&address),
            &addressLength) == 0);
        const uint16_t port = ntohs(address.sin_port);
        CHECK(port != 0);

        std::atomic<bool> running { true };
        std::atomic<int> receivedPackets { 0 };
        std::atomic<int> leftDownPackets { 0 };
        std::atomic<int> leftUpPackets { 0 };
        std::atomic<int64_t> lastLeftDownAtUs { 0 };
        std::atomic<int64_t> lastLeftUpAtUs { 0 };
        std::atomic<uint32_t> lastCommand { 0 };
        std::atomic<int> movePackets { 0 };
        std::atomic<bool> blockNextMoveResponse { false };
        std::atomic<bool> moveResponseBlocked { false };
        std::atomic<bool> releaseMoveResponse { false };
        std::atomic<bool> blockNextLeftDownResponse { false };
        std::atomic<bool> leftDownResponseBlocked { false };
        std::atomic<bool> releaseLeftDownResponse { false };
        constexpr uint32_t kMouseMoveCommand = 0xAEDE7345u;
        constexpr uint32_t kMouseLeftCommand = 0x9823AE8Du;
        std::thread serverThread([&] {
            std::array<char, 1024> packet = {};
            while (running.load(std::memory_order_acquire)) {
                sockaddr_in sender = {};
                int senderLength = sizeof(sender);
                const int received = recvfrom(
                    server,
                    packet.data(),
                    static_cast<int>(packet.size()),
                    0,
                    reinterpret_cast<sockaddr*>(&sender),
                    &senderLength);
                if (received == SOCKET_ERROR)
                    continue;
                if (received < 16)
                    continue;
                uint32_t command = 0;
                std::memcpy(&command, packet.data() + 12, sizeof(command));
                lastCommand.store(command, std::memory_order_release);
                if (command == kMouseMoveCommand)
                    ++movePackets;
                bool isLeftDown = false;
                if (command == kMouseLeftCommand && received >= 20) {
                    int32_t buttons = 0;
                    std::memcpy(&buttons, packet.data() + 16, sizeof(buttons));
                    isLeftDown = (buttons & 1) != 0;
                    const int64_t receivedAtUs =
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            std::chrono::steady_clock::now().time_since_epoch())
                            .count();
                    if (isLeftDown) {
                        lastLeftDownAtUs.store(
                            receivedAtUs,
                            std::memory_order_release);
                        ++leftDownPackets;
                    } else {
                        lastLeftUpAtUs.store(
                            receivedAtUs,
                            std::memory_order_release);
                        ++leftUpPackets;
                    }
                }
                if (command == kMouseMoveCommand &&
                    blockNextMoveResponse.exchange(
                        false,
                        std::memory_order_acq_rel)) {
                    moveResponseBlocked.store(true, std::memory_order_release);
                    while (running.load(std::memory_order_acquire) &&
                           !releaseMoveResponse.load(std::memory_order_acquire)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    moveResponseBlocked.store(false, std::memory_order_release);
                } else if (isLeftDown &&
                           blockNextLeftDownResponse.exchange(
                               false,
                               std::memory_order_acq_rel)) {
                    leftDownResponseBlocked.store(
                        true,
                        std::memory_order_release);
                    while (running.load(std::memory_order_acquire) &&
                           !releaseLeftDownResponse.load(
                               std::memory_order_acquire)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    leftDownResponseBlocked.store(
                        false,
                        std::memory_order_release);
                }
                // Ignore a short, stale, and wrong-command ACK before accepting
                // the real reply (also covers late ACKs from unacknowledged clicks).
                std::array<char, 16> badReply = {};
                std::memcpy(badReply.data(), packet.data(), badReply.size());
                sendto(server, badReply.data(), 8, 0,
                    reinterpret_cast<const sockaddr*>(&sender), senderLength);
                badReply[8] ^= 1;
                sendto(server, badReply.data(), 16, 0,
                    reinterpret_cast<const sockaddr*>(&sender), senderLength);
                badReply[8] ^= 1;
                badReply[12] ^= 1;
                sendto(server, badReply.data(), 16, 0,
                    reinterpret_cast<const sockaddr*>(&sender), senderLength);
                const int replySent = sendto(
                    server,
                    packet.data(),
                    16,
                    0,
                    reinterpret_cast<const sockaddr*>(&sender),
                    senderLength);
                if (replySent == 16)
                    ++receivedPackets;
            }
        });
        SetThreadPriority(
            serverThread.native_handle(),
            THREAD_PRIORITY_ABOVE_NORMAL);

        using app::input::ConnectionState;
        using app::input::DeviceKind;
        app::input::SetSelectedDevice(device);
        app::input::SetKmBoxNetConfig({
            "127.0.0.1",
            port,
            "A1B2C3D4"
        });
        CHECK(app::input::RequestConnectAndTest());

        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(2);
        app::input::DeviceStatus status;
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            status = app::input::GetDeviceStatus();
        } while (status.state != ConnectionState::Connected &&
                 status.state != ConnectionState::Error &&
                 std::chrono::steady_clock::now() < deadline);

        CHECK(status.state == ConnectionState::Connected);
        CHECK(status.selected == device);
        CHECK(status.port == "127.0.0.1:" + std::to_string(port));
        const int expectedPackets = static_cast<int>(
            app::input::BuildCircularTestPath().size()) + 2;
        CHECK(receivedPackets.load(std::memory_order_acquire) == expectedPackets);
        const auto waitForPackets = [&](int expected) {
            const auto packetDeadline = std::chrono::steady_clock::now() +
                std::chrono::seconds(3);
            while (receivedPackets.load(std::memory_order_acquire) < expected &&
                   std::chrono::steady_clock::now() < packetDeadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            // Counted packets have already had their ACK emitted. Give the
            // client worker a bounded scheduling window to consume it and
            // publish the resulting state before the next API assertion.
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
            CHECK(receivedPackets.load(std::memory_order_acquire) == expected);
        };
        const auto waitForSignal = [&](const std::atomic<bool>& signal) {
            const auto signalDeadline = std::chrono::steady_clock::now() +
                std::chrono::seconds(3);
            while (!signal.load(std::memory_order_acquire) &&
                   std::chrono::steady_clock::now() < signalDeadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            CHECK(signal.load(std::memory_order_acquire));
        };
        CHECK(app::input::RequestMove(7, -4));
        waitForPackets(expectedPackets + 1);

        const int clickDownBefore =
            leftDownPackets.load(std::memory_order_acquire);
        const int clickUpBefore =
            leftUpPackets.load(std::memory_order_acquire);
        CHECK(app::input::RequestLeftClick(30));
        CHECK(!app::input::RequestLeftClick(30));
        waitForPackets(expectedPackets + 3);
        CHECK(leftDownPackets.load(std::memory_order_acquire) ==
              clickDownBefore + 1);
        CHECK(leftUpPackets.load(std::memory_order_acquire) ==
              clickUpBefore + 1);
        const app::input::LeftClickStatus firstClickStatus =
            app::input::GetLeftClickStatus();
        CHECK(!firstClickStatus.active);
        CHECK(!firstClickStatus.outputDown);
        CHECK(firstClickStatus.token == 0u);
        const app::input::LeftClickTiming firstClickTiming =
            app::input::GetLeftClickTiming();
        CHECK(firstClickTiming.calibrated);
        CHECK(firstClickTiming.samples >= 1u);
        CHECK(std::isfinite(firstClickTiming.dispatchLatencyMs));
        CHECK(firstClickTiming.dispatchLatencyMs >= 0.0f);

        CHECK(app::input::RequestLeftButton(true));
        waitForPackets(expectedPackets + 4);
        CHECK(!app::input::RequestLeftClick(30));
        CHECK(app::input::RequestLeftButton(false));
        waitForPackets(expectedPackets + 5);
        CHECK(app::input::RequestLeftButton(true));
        waitForPackets(expectedPackets + 6);
        waitForPackets(expectedPackets + 7); // automatic watchdog release
        const app::input::DeviceStatus watchdogStatus =
            app::input::GetDeviceStatus();
        if (watchdogStatus.state != ConnectionState::Connected) {
            std::cerr << "kmBox watchdog state="
                      << static_cast<int>(watchdogStatus.state)
                      << " error=" << static_cast<int>(watchdogStatus.error)
                      << " win32=" << watchdogStatus.systemError
                      << " packets="
                      << receivedPackets.load(std::memory_order_acquire)
                      << " moves="
                      << movePackets.load(std::memory_order_acquire)
                      << " downs="
                      << leftDownPackets.load(std::memory_order_acquire)
                      << " ups="
                      << leftUpPackets.load(std::memory_order_acquire)
                      << " last=0x" << std::hex
                      << lastCommand.load(std::memory_order_acquire)
                      << std::dec << '\n';
        }
        CHECK(watchdogStatus.state == ConnectionState::Connected);

        // Hold an unrelated backend request in flight. A click reserved while
        // the worker is blocked must start its hold interval at the actual
        // successful DOWN, not at RequestLeftClick().
        releaseMoveResponse.store(false, std::memory_order_release);
        moveResponseBlocked.store(false, std::memory_order_release);
        blockNextMoveResponse.store(true, std::memory_order_release);
        CHECK(app::input::RequestMove(3, -2));
        waitForSignal(moveResponseBlocked);
        CHECK(receivedPackets.load(std::memory_order_acquire) ==
              expectedPackets + 7);
        const int delayedDownBefore =
            leftDownPackets.load(std::memory_order_acquire);
        const int delayedUpBefore =
            leftUpPackets.load(std::memory_order_acquire);
        CHECK(app::input::RequestLeftClick(30));
        const app::input::LeftClickStatus delayedQueuedStatus =
            app::input::GetLeftClickStatus();
        CHECK(delayedQueuedStatus.active);
        CHECK(!delayedQueuedStatus.outputDown);
        CHECK(delayedQueuedStatus.token != 0u);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        CHECK(leftDownPackets.load(std::memory_order_acquire) ==
              delayedDownBefore);
        CHECK(leftUpPackets.load(std::memory_order_acquire) == delayedUpBefore);
        releaseMoveResponse.store(true, std::memory_order_release);
        waitForPackets(expectedPackets + 10);
        CHECK(leftDownPackets.load(std::memory_order_acquire) ==
              delayedDownBefore + 1);
        CHECK(leftUpPackets.load(std::memory_order_acquire) ==
              delayedUpBefore + 1);
        const int64_t delayedDownAtUs =
            lastLeftDownAtUs.load(std::memory_order_acquire);
        const int64_t delayedUpAtUs =
            lastLeftUpAtUs.load(std::memory_order_acquire);
        CHECK(delayedUpAtUs > delayedDownAtUs);
        CHECK(delayedUpAtUs - delayedDownAtUs >= 25000);
        CHECK(!app::input::GetLeftClickStatus().active);
        CHECK(app::input::GetLeftClickTiming().samples >=
              firstClickTiming.samples + 1u);

        // Cancelling while the click is still queued must suppress both DOWN
        // and a stray UP because this service never acquired button ownership.
        releaseMoveResponse.store(false, std::memory_order_release);
        moveResponseBlocked.store(false, std::memory_order_release);
        blockNextMoveResponse.store(true, std::memory_order_release);
        CHECK(app::input::RequestMove(-2, 3));
        waitForSignal(moveResponseBlocked);
        CHECK(receivedPackets.load(std::memory_order_acquire) ==
              expectedPackets + 10);
        const int queuedCancelDownBefore =
            leftDownPackets.load(std::memory_order_acquire);
        const int queuedCancelUpBefore =
            leftUpPackets.load(std::memory_order_acquire);
        CHECK(app::input::RequestLeftClick(80));
        CHECK(app::input::RequestLeftButton(false));
        CHECK(!app::input::GetLeftClickStatus().active);
        releaseMoveResponse.store(true, std::memory_order_release);
        waitForPackets(expectedPackets + 11);
        CHECK(leftDownPackets.load(std::memory_order_acquire) ==
              queuedCancelDownBefore);
        CHECK(leftUpPackets.load(std::memory_order_acquire) ==
              queuedCancelUpBefore);

        // Cancelling after DOWN was sent but before its response completes
        // must force a matching UP and keep duplicate clicks rejected.
        releaseLeftDownResponse.store(false, std::memory_order_release);
        leftDownResponseBlocked.store(false, std::memory_order_release);
        blockNextLeftDownResponse.store(true, std::memory_order_release);
        const int dispatchCancelDownBefore =
            leftDownPackets.load(std::memory_order_acquire);
        const int dispatchCancelUpBefore =
            leftUpPackets.load(std::memory_order_acquire);
        CHECK(app::input::RequestLeftClick(80));
        waitForSignal(leftDownResponseBlocked);
        CHECK(!app::input::RequestLeftClick(80));
        CHECK(app::input::RequestLeftButton(false));
        releaseLeftDownResponse.store(true, std::memory_order_release);
        waitForPackets(expectedPackets + 13);
        CHECK(leftDownPackets.load(std::memory_order_acquire) ==
              dispatchCancelDownBefore + 1);
        CHECK(leftUpPackets.load(std::memory_order_acquire) ==
              dispatchCancelUpBefore + 1);
        CHECK(!app::input::GetLeftClickStatus().active);
        CHECK(!app::input::GetLeftClickStatus().outputDown);

        const int disconnectClickDownBefore =
            leftDownPackets.load(std::memory_order_acquire);
        const int disconnectClickUpBefore =
            leftUpPackets.load(std::memory_order_acquire);
        CHECK(app::input::RequestLeftClick(80));
        const auto downDeadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(1);
        while (leftDownPackets.load(std::memory_order_acquire) <
                   disconnectClickDownBefore + 1 &&
               std::chrono::steady_clock::now() < downDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(leftDownPackets.load(std::memory_order_acquire) ==
              disconnectClickDownBefore + 1);
        CHECK(app::input::RequestDisconnect());
        waitForPackets(expectedPackets + 15); // release before disconnect
        CHECK(leftUpPackets.load(std::memory_order_acquire) ==
              disconnectClickUpBefore + 1);
        CHECK(!app::input::GetLeftClickStatus().active);
        CHECK(!app::input::GetLeftClickTiming().calibrated);
        CHECK(app::input::GetLeftClickTiming().samples == 0u);

        // A changed endpoint must invalidate the selected network adapter,
        // including Ferrum, and malformed settings must never become Connected.
        CHECK(app::input::RequestConnectAndTest());
        const auto reconnectDeadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(2);
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            status = app::input::GetDeviceStatus();
        } while (status.state != ConnectionState::Connected &&
                 status.state != ConnectionState::Error &&
                 std::chrono::steady_clock::now() < reconnectDeadline);
        CHECK(status.state == ConnectionState::Connected);
        app::input::SetKmBoxNetConfig({ "not-an-ip", 0, "invalid" });
        CHECK(app::input::GetDeviceStatus().state == ConnectionState::Disconnected);
        CHECK(app::input::RequestConnectAndTest());
        const auto invalidDeadline = std::chrono::steady_clock::now() +
            std::chrono::seconds(2);
        do {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            status = app::input::GetDeviceStatus();
        } while (status.state != ConnectionState::Error &&
                 std::chrono::steady_clock::now() < invalidDeadline);
        CHECK(status.state == ConnectionState::Error);
        CHECK(status.error == app::input::DeviceError::InvalidConfiguration);

        running.store(false, std::memory_order_release);
        serverThread.join();
        closesocket(server);
        WSACleanup();
    }
}

void TestTargetBallistics()
{
    esp::PlayerData player = {};
    player.valid = true;
    player.health = 100;
    player.hasHitboxes = true;
    player.hitboxCount = 1;
    player.hitboxes[0].valid = true;
    player.hitboxes[0].start = {100.0f, 0.0f, 0.0f};
    player.hitboxes[0].end = {100.0f, 0.0f, 0.0f};
    player.hitboxes[0].center = {100.0f, 0.0f, 0.0f};
    player.hitboxes[0].radius = 5.0f;
    player.hitboxes[0].index = 0;
    player.hitboxes[0].hitgroup = 1;

    float distance = 0.0f;
    CHECK(target::ballistics::RayCapsuleIntersection(
        {}, {1.0f, 0.0f, 0.0f},
        player.hitboxes[0].start,
        player.hitboxes[0].end,
        player.hitboxes[0].radius,
        200.0f,
        &distance));
    CHECK(std::fabs(distance - 95.0f) < 0.01f);
    CHECK(!target::ballistics::RayCapsuleIntersection(
        {}, {0.0f, 1.0f, 0.0f},
        player.hitboxes[0].start,
        player.hitboxes[0].end,
        player.hitboxes[0].radius,
        200.0f));
    CHECK(target::ballistics::IsPointInsideCapsule(
        {100.0f, 5.0f, 0.0f}, player.hitboxes[0], 0.0f));
    CHECK(!target::ballistics::IsPointInsideCapsule(
        {100.0f, 5.01f, 0.0f}, player.hitboxes[0], 0.0f));

    const target::ballistics::WeaponSpread perfect = {
        0.0f, 0.0f, 0.0f, 7u, 1, 8192.0f,
    };

    esp::HitboxCapsule rotatedHead = {};
    rotatedHead.valid = true;
    rotatedHead.start = {98.0f, -7.0f, 3.0f};
    rotatedHead.end = {106.0f, 5.0f, 11.0f};
    rotatedHead.center = {102.0f, -1.0f, 7.0f};
    rotatedHead.radius = 4.0f;
    rotatedHead.hitgroup = 1;
    const auto headPoints =
        target::ballistics::GenerateCapsuleMultipoints(
            rotatedHead, {}, perfect);
    CHECK(headPoints.count == target::ballistics::kMaximumCapsuleMultipoints);
    CHECK(headPoints.points[0].role ==
          target::ballistics::MultipointRole::Center);
    CHECK(headPoints.appliedScale > 0.70f);
    for (int index = 0; index < headPoints.count; ++index) {
        CHECK(target::ballistics::IsPointInsideCapsule(
            headPoints.points[index].position,
            rotatedHead,
            0.0f));
        CHECK(headPoints.points[index].normalizedRadialOffset >= 0.0f);
        CHECK(headPoints.points[index].normalizedRadialOffset < 1.0f);
    }

    const target::ballistics::WeaponSpread distantInaccurate = {
        0.04f, 0.02f, 1.5f, 7u, 1, 8192.0f,
    };
    const auto conservativeHeadPoints =
        target::ballistics::GenerateCapsuleMultipoints(
            rotatedHead,
            {-1900.0f, 0.0f, 0.0f},
            distantInaccurate);
    CHECK(conservativeHeadPoints.count ==
          target::ballistics::kMaximumCapsuleMultipoints);
    CHECK(conservativeHeadPoints.projectedSpreadRadius >
          headPoints.projectedSpreadRadius);
    CHECK(conservativeHeadPoints.appliedScale < headPoints.appliedScale);
    for (int index = 0; index < conservativeHeadPoints.count; ++index) {
        CHECK(target::ballistics::IsPointInsideCapsule(
            conservativeHeadPoints.points[index].position,
            rotatedHead,
            0.0f));
    }

    esp::HitboxCapsule rotatedBody = rotatedHead;
    rotatedBody.hitgroup = 2;
    const auto bodyPoints =
        target::ballistics::GenerateCapsuleMultipoints(
            rotatedBody, {}, perfect);
    CHECK(bodyPoints.count == target::ballistics::kMaximumCapsuleMultipoints);
    CHECK(bodyPoints.appliedScale < headPoints.appliedScale);
    for (int index = 0; index < bodyPoints.count; ++index) {
        CHECK(target::ballistics::IsPointInsideCapsule(
            bodyPoints.points[index].position,
            rotatedBody,
            0.0f));
    }

    const auto axisOnlyPoints =
        target::ballistics::GenerateCapsuleMultipoints(
            rotatedHead, {}, perfect, 0.0f);
    CHECK(axisOnlyPoints.count == 3);
    for (int index = 0; index < axisOnlyPoints.count; ++index)
        CHECK(axisOnlyPoints.points[index].normalizedRadialOffset == 0.0f);
    esp::HitboxCapsule invalidCapsule = rotatedHead;
    invalidCapsule.valid = false;
    CHECK(target::ballistics::GenerateCapsuleMultipoints(
        invalidCapsule, {}, perfect).count == 0);

    CHECK(target::ballistics::CalculateHitchance(
        {}, {}, player, perfect, 1) == 1.0f);
    CHECK(target::ballistics::CalculateHitchance(
        {}, {0.0f, 15.0f, 0.0f}, player, perfect, 1) == 0.0f);
    const target::ballistics::WeaponSpread inaccurate = {
        0.08f, 0.04f, 1.5f, 7u, 1, 8192.0f,
    };
    const float firstHitchance = target::ballistics::CalculateHitchance(
        {}, {}, player, inaccurate, 1);
    const float repeatedHitchance = target::ballistics::CalculateHitchance(
        {}, {}, player, inaccurate, 1);
    CHECK(firstHitchance > 0.0f && firstHitchance < 1.0f);
    CHECK(firstHitchance == repeatedHitchance);
    CHECK(target::ballistics::CalculateHitchance(
        {}, {}, player, inaccurate, 2) == 0.0f);
    CHECK(target::ballistics::ScaleDamage(
        40.0f, 1, 0, false, 2, 1.0f, 4.0f) == 160.0f);
    CHECK(target::ballistics::ScaleDamage(
        40.0f, 1, 100, true, 2, 1.0f, 4.0f) == 80.0f);
    const Vector3 seedAngles = {10.25f, 20.75f, 0.0f};
    CHECK(target::ballistics::CalculateSpreadSeed(
        seedAngles, 12345) == 2235951147u);
    CHECK(target::ballistics::CalculateSpreadSeed(
        {10.49f, 20.99f, 0.0f}, 12345) == 2235951147u);
    CHECK(target::ballistics::CalculateSpreadSeed(
        seedAngles, 12346) != 2235951147u);
    CHECK(target::ballistics::CalculateSpreadSeed(
        {370.25f, -339.25f, 81.0f}, 12345) == 2235951147u);
    CHECK(target::ballistics::CalculateSpreadSeed(
        seedAngles, -1) == 0u);
    Vector3 perfectSpreadDirection = {};
    CHECK(target::ballistics::ResolveSpreadDirection(
        {}, perfect, 2235951147u, perfectSpreadDirection));
    CHECK(std::fabs(perfectSpreadDirection.x - 1.0f) < 0.000001f);
    CHECK(std::fabs(perfectSpreadDirection.y) < 0.000001f);
    CHECK(std::fabs(perfectSpreadDirection.z) < 0.000001f);
    Vector3 seededSpreadDirection = {};
    Vector3 lowSeedDirection = {}, highSeedDirection = {};
    CHECK(target::ballistics::ResolveSpreadDirection({}, inaccurate, 42u, lowSeedDirection));
    CHECK(target::ballistics::ResolveSpreadDirection({}, inaccurate, 298u, highSeedDirection));
    CHECK((lowSeedDirection - highSeedDirection).Length() > 0.00001f);
    for (const uint32_t edgeSeed : {0u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu}) {
        Vector3 edgeDirection;
        CHECK(target::ballistics::ResolveSpreadDirection({}, inaccurate, edgeSeed, edgeDirection));
        CHECK(IsFiniteVec(edgeDirection));
        CHECK(std::fabs(edgeDirection.Length() - 1.0f) < 0.00001f);
    }
    CHECK(target::ballistics::ResolveSpreadDirection(
        {}, inaccurate, 2235951147u, seededSpreadDirection));
    CHECK(std::fabs(target::ballistics::Dot(
        seededSpreadDirection,
        seededSpreadDirection) - 1.0f) < 0.00001f);
    CHECK(target::ballistics::TraceSpreadSeed(
        {}, {}, player, inaccurate, 2235951147u, 1) ==
        target::ballistics::TracePlayerCapsules(
            {}, seededSpreadDirection, player, inaccurate.range, 1));
    auto invalidSpread = inaccurate;
    invalidSpread.inaccuracy = -1.0f;
    seededSpreadDirection = {1.0f, 1.0f, 1.0f};
    CHECK(!target::ballistics::ResolveSpreadDirection(
        {}, invalidSpread, 2235951147u, seededSpreadDirection));
    CHECK(target::ballistics::Dot(
        seededSpreadDirection,
        seededSpreadDirection) == 0.0f);
    const auto perfectSeedWindow =
        target::ballistics::TraceSpreadSeedWindow(
            {}, {}, player, perfect, 100, 3, 1);
    CHECK(perfectSeedWindow.tested == 3);
    CHECK(perfectSeedWindow.hits == 3);
    CHECK(perfectSeedWindow.Fraction() == 1.0f);
    const auto perfectSeedSelection =
        target::ballistics::SelectSpreadSeedTick(
            {}, {}, player, perfect, 100, 3, 1);
    CHECK(perfectSeedSelection.HasSelection());
    CHECK(perfectSeedSelection.tested == 3);
    CHECK(perfectSeedSelection.hits == 3);
    CHECK(perfectSeedSelection.earliestDeliveryTick == 100);
    CHECK(perfectSeedSelection.earliestHitTick == 100);
    CHECK(perfectSeedSelection.selectedRenderTick == 100);
    CHECK(perfectSeedSelection.selectedSeed ==
          target::ballistics::CalculateSpreadSeed({}, 100));
    CHECK(perfectSeedSelection.selectedGeometricSafety > 0.999f);
    CHECK(perfectSeedSelection.Confidence() == 1.0f);
    CHECK(!target::ballistics::SelectSpreadSeedTick(
        {}, {}, player, perfect, -1, 3, 1).HasSelection());
    CHECK(!target::ballistics::SelectSpreadSeedTick(
        {}, {}, player, perfect, 100, 0, 1).HasSelection());
    const auto clampedSeedSelection =
        target::ballistics::SelectSpreadSeedTick(
            {}, {}, player, perfect, 100, 99, 1);
    CHECK(clampedSeedSelection.tested ==
          target::ballistics::kMaximumSeedWindowTicks);
    const auto repeatedSeedSelection =
        target::ballistics::SelectSpreadSeedTick(
            {}, {}, player, perfect, 100, 3, 1);
    CHECK(repeatedSeedSelection.selectedRenderTick ==
          perfectSeedSelection.selectedRenderTick);
    CHECK(repeatedSeedSelection.selectedSeed ==
          perfectSeedSelection.selectedSeed);
    CHECK(repeatedSeedSelection.Confidence() ==
          perfectSeedSelection.Confidence());
    const auto missedSeedWindow =
        target::ballistics::TraceSpreadSeedWindow(
            {}, {0.0f, 15.0f, 0.0f}, player, perfect, 100, 3, 1);
    CHECK(missedSeedWindow.tested == 3);
    CHECK(missedSeedWindow.hits == 0);
    const auto missedSeedSelection =
        target::ballistics::SelectSpreadSeedTick(
            {}, {0.0f, 15.0f, 0.0f}, player, perfect, 100, 3, 1);
    CHECK(!missedSeedSelection.HasSelection());
    CHECK(missedSeedSelection.earliestHitTick == -1);
    CHECK(missedSeedSelection.selectedRenderTick == -1);
    CHECK(missedSeedSelection.Confidence() == 0.0f);

    bool foundMixedSeedWindow = false;
    for (int firstTick = 0; firstTick < 2048; firstTick += 8) {
        const auto mixed = target::ballistics::SelectSpreadSeedTick(
            {}, {}, player, inaccurate, firstTick, 8, 1);
        if (mixed.hits <= 0 || mixed.hits >= mixed.tested)
            continue;
        foundMixedSeedWindow = true;
        CHECK(mixed.HasSelection());
        CHECK(mixed.earliestHitTick >= firstTick);
        CHECK(mixed.selectedRenderTick >= firstTick);
        CHECK(mixed.selectedRenderTick < firstTick + 8);
        CHECK(mixed.selectedSeed == target::ballistics::CalculateSpreadSeed(
            {}, mixed.selectedRenderTick));
        CHECK(std::fabs(mixed.Confidence() -
            static_cast<float>(mixed.hits) /
                static_cast<float>(mixed.tested)) < 0.000001f);
        float maximumSafety = 0.0f;
        for (int index = 0; index < mixed.tested; ++index) {
            const auto& candidate = mixed.candidates[index];
            if (candidate.hit)
                maximumSafety = std::max(
                    maximumSafety,
                    candidate.geometricSafety);
        }
        CHECK(std::fabs(mixed.selectedGeometricSafety - maximumSafety) <
              0.000001f);
        const auto repeatedMixed = target::ballistics::SelectSpreadSeedTick(
            {}, {}, player, inaccurate, firstTick, 8, 1);
        CHECK(repeatedMixed.selectedRenderTick == mixed.selectedRenderTick);
        CHECK(repeatedMixed.selectedSeed == mixed.selectedSeed);
        CHECK(repeatedMixed.hits == mixed.hits);
        break;
    }
    CHECK(foundMixedSeedWindow);

    const auto spherePoints =
        target::ballistics::GenerateCapsuleMultipoints(
            player.hitboxes[0], {}, perfect);
    const auto multipointSeedSelection =
        target::ballistics::SelectMultipointSpreadTick(
            {}, player, perfect, spherePoints, 100, 3, 1);
    CHECK(multipointSeedSelection.valid);
    CHECK(multipointSeedSelection.pointIndex == 0);
    CHECK(multipointSeedSelection.seed.HasSelection());
    CHECK(multipointSeedSelection.seed.selectedRenderTick == 100);
    CHECK(multipointSeedSelection.seed.Confidence() == 1.0f);
    CHECK(std::fabs(multipointSeedSelection.point.x - 100.0f) < 0.001f);
    CHECK(std::fabs(multipointSeedSelection.point.y) < 0.001f);
    CHECK(std::fabs(multipointSeedSelection.point.z) < 0.001f);

    esp::data::BoneReadBatch batch = {};
    esp::data::BoneTransform transform = {};
    transform.position = {10.0f, 20.0f, 30.0f};
    transform.rotation = {0.0f, 0.0f, 0.70710678f, 0.70710678f};
    std::memcpy(
        batch.transforms.data() +
            static_cast<size_t>(7) * esp::data::kBoneTransformStrideBytes,
        &transform,
        sizeof(transform));
    esp::data::BoneTransform unpacked = {};
    CHECK(esp::data::UnpackBoneTransform(batch, 7, unpacked));
    const Vector3 rotated = esp::data::RotateByQuaternion(
        unpacked.rotation,
        {1.0f, 0.0f, 0.0f});
    CHECK(std::fabs(rotated.x) < 0.001f);
    CHECK(std::fabs(rotated.y - 1.0f) < 0.001f);
}

void TestDmaReadCompletionContracts()
{
    using esp::data::BasePointerSample;
    using esp::data::IsLocalVitalSampleComplete;
    constexpr uintptr_t pawn = 0x12340000;
    CHECK(IsLocalVitalSampleComplete(pawn, pawn, 100, 0, 4, 1));
    CHECK(IsLocalVitalSampleComplete(pawn, pawn, 0, 0, 4, 1)); // real death
    CHECK(!IsLocalVitalSampleComplete(pawn, pawn, 0, 0, 0, 0)); // zero-filled failure
    CHECK(!IsLocalVitalSampleComplete(pawn, pawn, 100, 0, 3, 1));
    CHECK(!IsLocalVitalSampleComplete(pawn, pawn, 100, 0, 4, 0));
    CHECK(!IsLocalVitalSampleComplete(pawn, pawn + 8, 100, 0, 4, 1));
    CHECK(!IsLocalVitalSampleComplete(0, 0, 0, 0, 4, 1));
    CHECK(!IsLocalVitalSampleComplete(pawn, pawn, -1, 0, 4, 1));
    CHECK(!IsLocalVitalSampleComplete(pawn, pawn, 100, 255, 4, 1));

    BasePointerSample root;
    CHECK(root.Resolve(pawn, 7, 1000) == 0);
    CHECK(root.Resolve(pawn, 8, 1000) == pawn);
    CHECK(root.Resolve(0, 0, 1001, false) == 0); // C4 cannot use generic pointer continuity
    CHECK(root.sampledUs == 1000);
    for (uint64_t now = 2000; now < 100000; now += 1000) {
        CHECK(root.Resolve(0, 0, now) == pawn);
        CHECK(root.sampledUs == 1000); // gaps cannot refresh their own TTL
    }
    CHECK(root.Resolve(0, 0, 101001) == 0);
    CHECK(root.Resolve(pawn + 8, 8, 102000) == pawn + 8);
    CHECK(root.Resolve(0, 8, 103000) == 0); // authoritative null clears immediately
    CHECK(root.Resolve(0, 0, 104000) == 0);
    CHECK(root.Resolve(pawn, 8, 105000) == pawn);
    CHECK(root.Resolve(pawn + 1, 8, 106000) == pawn); // invalid alignment
    CHECK(root.sampledUs == 105000);
    CHECK(root.Resolve(0, 0, 1) == 0); // clock regression
    root = {};
    CHECK(root.Resolve(0, 0, 106000) == 0); // scene reset

    dma::ScatterReadTracker<uint32_t, 2, 4> reads;
    uint32_t output = 99;
    auto* first = reads.Add(8, &output);
    CHECK(first != nullptr && output == 0);
    if (!first) return;
    first->prepared = true;
    first->completed = 8;
    for (int i = 0; i < 6; ++i) {
        auto* request = reads.Add(4, nullptr); // counters also exist without caller outputs
        CHECK(request != nullptr);
        if (!request) return;
        request->prepared = true;
        request->completed = i == 0 ? 2 : 4;
    }
    auto* failedPrepare = reads.Add(64, nullptr);
    CHECK(failedPrepare != nullptr);
    CHECK(reads.Add(4, nullptr) == nullptr); // bounded storage; never invalidate prior pointers
    CHECK(first->completed == 8);
    auto quality = reads.Complete(true);
    CHECK(output == 8);
    CHECK(quality.requests == 7);
    CHECK(quality.requestedBytes == 32);
    CHECK(quality.completedBytes == 30);
    CHECK(quality.incompleteRequests == 1);
    quality = reads.Complete(false);
    CHECK(output == 0 && quality.completedBytes == 0 && quality.incompleteRequests == 7);
    reads.Reset();
    CHECK(reads.Complete(true).requests == 0);
    auto* reused = reads.Add(4, &output);
    CHECK(reused == first && reused->completed == 0 && !reused->prepared);
    reused->prepared = true;
    reused->completed = 5; // malformed count cannot inflate metrics or look complete
    quality = reads.Complete(true);
    CHECK(output == 0 && quality.completedBytes == 0 && quality.incompleteRequests == 1);
    reads.Reset();
    CHECK(reads.Add(0, &output) == nullptr && output == 0);
}

int main()
{
    TestDmaReadCompletionContracts();
    TestTrim();
    TestConfigParseUtils();
    TestWeaponCatalog();
    TestMemoryValidation();
    TestRuntimeResolverPolicy();
    TestBase64();
    TestLocalizationCatalog();
    TestInputDevicePolicy();
    TestProfileNameUtils();
    TestOffsetParsing();
    TestWebRadarMapMetadata();
    TestReplaceFileWithTemp();
    TestPatchBuildParsing();
    TestCanonicalTimestampParsing();
    TestSteamTimestampParsing();
    TestSnapshotRing();
    TestPlayerSlotPolicy();
    TestPlayerCommitPolicy();
    TestPlayerCorePolicy();
    TestPlayerFlagPolicy();
    TestPlayerHierarchyPolicy();
    TestPlayerRepairPolicy();
    TestVisibilityPolicy();
    TestBoneReadPolicy();
    TestPopulationWatchdogPolicy();
    TestWorldMarkerPolicy();
    TestWorldPublicationAndUtilityReadGaps();
    TestWorldDomainPolicy();
    TestBombPolicy();
    TestBaseRecoveryPolicy();
    TestSceneTransitionPolicy();
    TestWorkerPolicy();
    TestDeferredLanePolicy();
    TestZeroPopulationPolicy();
    TestDmaCacheProfile();
    TestDmaRefreshPolicy();
    TestDmaRecoveryPolicy();
    TestProcessIdentityPolicy();
    TestDrawPolicy();
    TestVisualStylePolicy();
    TestSkeletonGate();
    TestBonePlausibility();
    TestResetPolicy();
    TestTargetPolicy();
    TestTargetBallistics();
    TestKmBoxNetLoopback(app::input::DeviceKind::KmBoxNet);
    TestKmBoxNetLoopback(app::input::DeviceKind::FerrumOne);
    TestMakcuHardwareIfRequested();

    if (g_failedChecks != 0) {
        std::cerr << "debug_logic_tests failed: " << g_failedChecks << " check(s)\n";
        return 1;
    }

    std::cout << "debug_logic_tests passed\n";
    return 0;
}
