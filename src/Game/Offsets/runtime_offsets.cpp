#include "Game/Offsets/runtime_offsets.h"
#include "Game/Offsets/runtime_offset_resolver.h"
#include "Game/Offsets/runtime_offsets_parse_utils.h"
#include "app/Config/project_paths.h"
#include "app/Core/build_info.h"
#include "app/Core/globals.h"
#include "app/Core/memory_address.h"
#include "app/Localization/localization.h"
#include "app/Platform/file_replace.h"
#include "app/Platform/win_handle.h"
#include <DMALibrary/Memory/Memory.h>

#include <Windows.h>
#include <winhttp.h>

#include <json/json.hpp>

#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <regex>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace
{
    using json = nlohmann::json;
    using ordered_json = nlohmann::ordered_json;
    using LegacyIniSection = std::unordered_map<std::string, std::string>;
    using LegacyIniDocument = std::unordered_map<std::string, LegacyIniSection>;

    struct OffsetField
    {
        const char* section;
        const char* key;
        std::ptrdiff_t runtime_offsets::Values::*member;
    };

    struct RemoteField
    {
        const char* remoteKey;
        const char* sourceFile;
        std::ptrdiff_t runtime_offsets::Values::*member;
    };

    struct OutputDirectoryCandidate
    {
        std::filesystem::path directory;
        std::string label;
        std::string timestamp;
        int buildNumber = 0;
        bool hasRequiredFiles = false;
    };

    struct OffsetState
    {
        runtime_offsets::PatchInfo offsetsPatch = {};
        runtime_offsets::PatchInfo lastSeenPatch = {};
        std::string selectedSource;
        std::string selectedSourceTimestamp;
        std::string remoteOutputTimestamp;
        int selectedSourceBuildNumber = 0;
    };

    struct SteamInfSnapshot
    {
        runtime_offsets::PatchInfo patch = {};
        std::string versionDate;
        std::string versionTime;
        int patchBuildNumber = 0;
    };

    constexpr int kGitHubHttpResolveTimeoutMs = 3000;
    constexpr int kGitHubHttpConnectTimeoutMs = 3000;
    constexpr int kGitHubHttpSendTimeoutMs = 4500;
    constexpr int kGitHubHttpReceiveTimeoutMs = 4500;

    #include "runtime_offsets_parts/runtime_offsets_remote_fields.inl"

    runtime_offsets::Values g_values = {};
    std::shared_mutex g_valuesMutex;
    std::mutex g_runtimeResolveMutex;

    struct RuntimeResolveCache
    {
        DWORD pid = 0;
        uintptr_t clientBase = 0;
        uintptr_t engineBase = 0;
        std::uint64_t moduleFingerprint = 0;
        bool success = false;
        std::chrono::steady_clock::time_point attemptedAt = {};
        runtime_offsets::RuntimeResolveReport report = {};
    };

    RuntimeResolveCache g_runtimeResolveCache = {};
    constexpr auto kRuntimeResolveFailureCooldown = std::chrono::seconds(30);

    struct LiveValidationStats
    {
        std::size_t passed = 0;
        std::size_t attempted = 0;
    };

    std::uint64_t MixFingerprint(std::uint64_t hash, std::uint64_t value) noexcept
    {
        constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
        for (std::size_t i = 0; i < sizeof(value); ++i) {
            hash ^= static_cast<std::uint8_t>(value >> (i * 8));
            hash *= kFnvPrime;
        }
        return hash;
    }

    bool ReadModuleImageInfo(
        uintptr_t base,
        std::size_t& imageSize,
        std::uint32_t& timestamp)
    {
        imageSize = 0;
        timestamp = 0;
        IMAGE_DOS_HEADER dos = {};
        if (!base ||
            !mem.Read(base, &dos, sizeof(dos)) ||
            dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew <= 0 || dos.e_lfanew > 0x4000) {
            return false;
        }

        IMAGE_NT_HEADERS64 nt = {};
        if (!mem.Read(
                base + static_cast<uintptr_t>(dos.e_lfanew),
                &nt,
                sizeof(nt)) ||
            nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
            nt.OptionalHeader.SizeOfImage == 0 ||
            nt.OptionalHeader.SizeOfImage > (512u * 1024u * 1024u)) {
            return false;
        }
        imageSize = nt.OptionalHeader.SizeOfImage;
        timestamp = nt.FileHeader.TimeDateStamp;
        return true;
    }

    std::uint64_t BuildModuleFingerprint()
    {
        constexpr std::array<const char*, 4> kRuntimeModules = {
            "client.dll",
            "engine2.dll",
            "schemasystem.dll",
            "matchmaking.dll",
        };
        std::uint64_t hash = 1469598103934665603ULL;
        for (const char* moduleName : kRuntimeModules) {
            const uintptr_t base = mem.GetModuleBase(moduleName);
            std::size_t size = 0;
            std::uint32_t timestamp = 0;
            ReadModuleImageInfo(base, size, timestamp);
            hash = MixFingerprint(hash, static_cast<std::uint64_t>(base));
            hash = MixFingerprint(hash, static_cast<std::uint64_t>(size));
            hash = MixFingerprint(hash, timestamp);
        }
        return hash;
    }

    void SetRuntimeValues(const runtime_offsets::Values& values)
    {
        std::unique_lock lock(g_valuesMutex);
        g_values = values;
    }

    runtime_offsets::Values GetRuntimeValuesSnapshot()
    {
        std::shared_lock lock(g_valuesMutex);
        return g_values;
    }

    #include "runtime_offsets_parts/runtime_offsets_paths_helpers.inl"

    #include "runtime_offsets_parts/runtime_offsets_parse_helpers.inl"

    runtime_offsets::PatchInfo ReadPatchInfo(const json& root, const char* key)
    {
        runtime_offsets::PatchInfo info = {};
        const auto it = root.find(key);
        if (it == root.end() || !it->is_object())
            return info;

        info.etag = JsonStringOrEmpty(*it, "etag");
        info.lastFileSha = JsonStringOrEmpty(*it, "last_file_sha");
        info.clientVersion = JsonIntOrZero(*it, "client_version");
        info.sourceRevision = JsonIntOrZero(*it, "source_revision");
        info.patchVersion = JsonStringOrEmpty(*it, "patch_version");
        return info;
    }

    template <typename JsonT>
    void WritePatchInfo(JsonT& root, const char* key, const runtime_offsets::PatchInfo& info)
    {
        root[key] = {
            { "etag", info.etag },
            { "last_file_sha", info.lastFileSha },
            { "client_version", info.clientVersion },
            { "source_revision", info.sourceRevision },
            { "patch_version", info.patchVersion }
        };
    }

    bool TryParseOffsetsJsonFile(const std::filesystem::path& jsonPath, json& outRoot);
    void LoadValuesFromJson(const json& root,
                            runtime_offsets::Values& outValues,
                            std::vector<std::string>* missingKeys,
                            std::vector<std::string>* invalidKeys);
    std::vector<std::string> ValidateLoadedValues(const runtime_offsets::Values& values, bool requiredOnly = false);

    const std::vector<OffsetField> kOffsetFields = {
        OffsetField{"offsets", "dwEntityList", &runtime_offsets::Values::dwEntityList},
        OffsetField{"offsets", "dwGameEntitySystem_highestEntityIndex", &runtime_offsets::Values::dwGameEntitySystem_highestEntityIndex},
        OffsetField{"offsets", "dwGameRules", &runtime_offsets::Values::dwGameRules},
        OffsetField{"offsets", "dwGlobalVars", &runtime_offsets::Values::dwGlobalVars},
        OffsetField{"offsets", "dwLocalPlayerController", &runtime_offsets::Values::dwLocalPlayerController},
        OffsetField{"offsets", "dwLocalPlayerPawn", &runtime_offsets::Values::dwLocalPlayerPawn},
        OffsetField{"offsets", "dwPlantedC4", &runtime_offsets::Values::dwPlantedC4},
        OffsetField{"offsets", "dwViewMatrix", &runtime_offsets::Values::dwViewMatrix},
        OffsetField{"offsets", "dwViewAngles", &runtime_offsets::Values::dwViewAngles},
        OffsetField{"offsets", "dwWeaponC4", &runtime_offsets::Values::dwWeaponC4},
        OffsetField{"offsets", "dwSensitivity", &runtime_offsets::Values::dwSensitivity},
        OffsetField{"offsets", "dwSensitivity_sensitivity", &runtime_offsets::Values::dwSensitivity_sensitivity},
        OffsetField{"offsets", "dwNetworkGameClient", &runtime_offsets::Values::dwNetworkGameClient},
        OffsetField{"offsets", "dwNetworkGameClient_signOnState", &runtime_offsets::Values::dwNetworkGameClient_signOnState},
        OffsetField{"offsets", "dwNetworkGameClient_localPlayer", &runtime_offsets::Values::dwNetworkGameClient_localPlayer},
        OffsetField{"offsets", "dwNetworkGameClient_maxClients", &runtime_offsets::Values::dwNetworkGameClient_maxClients},
        OffsetField{"offsets", "dwNetworkGameClient_isBackgroundMap", &runtime_offsets::Values::dwNetworkGameClient_isBackgroundMap},
        OffsetField{"offsets", "dwGameTypes", &runtime_offsets::Values::dwGameTypes},
        OffsetField{"offsets", "dwGameTypes_mapName", &runtime_offsets::Values::dwGameTypes_mapName},
        OffsetField{"schemas", "CBasePlayerController.m_iPing", &runtime_offsets::Values::CBasePlayerController_m_iPing},
        OffsetField{"schemas", "CCSPlayerController.m_pInGameMoneyServices", &runtime_offsets::Values::CCSPlayerController_m_pInGameMoneyServices},
        OffsetField{"schemas", "CCSPlayerController_InGameMoneyServices.m_iAccount", &runtime_offsets::Values::CCSPlayerController_InGameMoneyServices_m_iAccount},
        OffsetField{"schemas", "C_BaseEntity.m_iTeamNum", &runtime_offsets::Values::C_BaseEntity_m_iTeamNum},
        OffsetField{"schemas", "C_BasePlayerPawn.m_vOldOrigin", &runtime_offsets::Values::C_BasePlayerPawn_m_vOldOrigin},
        OffsetField{"schemas", "C_BaseModelEntity.m_vecViewOffset", &runtime_offsets::Values::C_BaseModelEntity_m_vecViewOffset},
        OffsetField{"schemas", "C_BasePlayerPawn.m_flFOVSensitivityAdjust", &runtime_offsets::Values::C_BasePlayerPawn_m_flFOVSensitivityAdjust},
        OffsetField{"schemas", "C_BasePlayerPawn.m_pWeaponServices", &runtime_offsets::Values::C_BasePlayerPawn_m_pWeaponServices},
        OffsetField{"schemas", "C_BasePlayerPawn.m_pObserverServices", &runtime_offsets::Values::C_BasePlayerPawn_m_pObserverServices},
        OffsetField{"schemas", "C_BasePlayerPawn.m_hController", &runtime_offsets::Values::C_BasePlayerPawn_m_hController},
        OffsetField{"schemas", "CBasePlayerController.m_hPawn", &runtime_offsets::Values::CBasePlayerController_m_hPawn},
        OffsetField{"schemas", "CCSPlayerController.m_hObserverPawn", &runtime_offsets::Values::CCSPlayerController_m_hObserverPawn},
        OffsetField{"schemas", "CCSPlayerController.m_hPlayerPawn", &runtime_offsets::Values::CCSPlayerController_m_hPlayerPawn},
        OffsetField{"schemas", "CBasePlayerController.m_iszPlayerName", &runtime_offsets::Values::CBasePlayerController_m_iszPlayerName},
        OffsetField{"schemas", "CPlayer_WeaponServices.m_hActiveWeapon", &runtime_offsets::Values::CPlayer_WeaponServices_m_hActiveWeapon},
        OffsetField{"schemas", "CPlayer_WeaponServices.m_hMyWeapons", &runtime_offsets::Values::CPlayer_WeaponServices_m_hMyWeapons},
        OffsetField{"schemas", "C_BaseEntity.m_iHealth", &runtime_offsets::Values::C_BaseEntity_m_iHealth},
        OffsetField{"schemas", "C_CSPlayerPawn.m_ArmorValue", &runtime_offsets::Values::C_CSPlayerPawn_m_ArmorValue},
        OffsetField{"schemas", "C_BaseEntity.m_lifeState", &runtime_offsets::Values::C_BaseEntity_m_lifeState},
        OffsetField{"schemas", "C_BaseEntity.m_pGameSceneNode", &runtime_offsets::Values::C_BaseEntity_m_pGameSceneNode},
        OffsetField{"schemas", "C_BaseEntity.m_hOwnerEntity", &runtime_offsets::Values::C_BaseEntity_m_hOwnerEntity},
        OffsetField{"schemas", "C_BaseEntity.m_nSubclassID", &runtime_offsets::Values::C_BaseEntity_m_nSubclassID},
        OffsetField{"schemas", "C_BaseEntity.m_vecVelocity", &runtime_offsets::Values::C_BaseEntity_m_vecVelocity},
        OffsetField{"schemas", "C_BaseEntity.m_fFlags", &runtime_offsets::Values::C_BaseEntity_m_fFlags},
        OffsetField{"schemas", "CEntityInstance.m_pEntity", &runtime_offsets::Values::CEntityInstance_m_pEntity},
        OffsetField{"schemas", "CEntityIdentity.m_designerName", &runtime_offsets::Values::CEntityIdentity_m_designerName},
        OffsetField{"schemas", "C_BasePlayerWeapon.m_iClip1", &runtime_offsets::Values::C_BasePlayerWeapon_m_iClip1},
        OffsetField{"schemas", "C_CSWeaponBase.m_bCanBePickedUp", &runtime_offsets::Values::C_CSWeaponBase_m_bCanBePickedUp},
        OffsetField{"schemas", "C_CSWeaponBase.m_nDropTick", &runtime_offsets::Values::C_CSWeaponBase_m_nDropTick},
        OffsetField{"schemas", "C_CSWeaponBase.m_fAccuracyPenalty", &runtime_offsets::Values::C_CSWeaponBase_m_fAccuracyPenalty},
        OffsetField{"schemas", "C_CSWeaponBase.m_flTurningInaccuracy", &runtime_offsets::Values::C_CSWeaponBase_m_flTurningInaccuracy},
        OffsetField{"schemas", "C_CSWeaponBase.m_flRecoilIndex", &runtime_offsets::Values::C_CSWeaponBase_m_flRecoilIndex},
        OffsetField{"schemas", "C_CSWeaponBase.m_bInReload", &runtime_offsets::Values::C_CSWeaponBase_m_bInReload},
        OffsetField{"schemas", "C_CSWeaponBase.m_weaponMode", &runtime_offsets::Values::C_CSWeaponBase_m_weaponMode},
        OffsetField{"schemas", "C_CSWeaponBase.m_fLastShotTime", &runtime_offsets::Values::C_CSWeaponBase_m_fLastShotTime},
        OffsetField{"schemas", "C_CSPlayerPawn.m_bGunGameImmunity", &runtime_offsets::Values::C_CSPlayerPawn_m_bGunGameImmunity},
        OffsetField{"schemas", "C_CSPlayerPawn.m_bIsScoped", &runtime_offsets::Values::C_CSPlayerPawn_m_bIsScoped},
        OffsetField{"schemas", "C_CSPlayerPawn.m_bIsWalking", &runtime_offsets::Values::C_CSPlayerPawn_m_bIsWalking},
        OffsetField{"schemas", "C_CSPlayerPawn.m_bIsDefusing", &runtime_offsets::Values::C_CSPlayerPawn_m_bIsDefusing},
        OffsetField{"schemas", "C_CSPlayerPawn.m_angEyeAngles", &runtime_offsets::Values::C_CSPlayerPawn_m_angEyeAngles},
        OffsetField{"schemas", "C_CSPlayerPawn.m_pAimPunchServices", &runtime_offsets::Values::C_CSPlayerPawn_m_pAimPunchServices},
        OffsetField{"schemas", "C_CSPlayerPawn.m_iShotsFired", &runtime_offsets::Values::C_CSPlayerPawn_m_iShotsFired},
        OffsetField{"schemas", "CCSPlayer_AimPunchServices.m_predictableBaseAngle", &runtime_offsets::Values::CCSPlayer_AimPunchServices_m_predictableBaseAngle},
        OffsetField{"schemas", "C_CSPlayerPawnBase.m_iIDEntIndex", &runtime_offsets::Values::C_CSPlayerPawnBase_m_iIDEntIndex},
        OffsetField{"schemas", "C_CSPlayerPawnBase.m_flFlashBangTime", &runtime_offsets::Values::C_CSPlayerPawnBase_m_flFlashBangTime},
        OffsetField{"schemas", "C_CSPlayerPawnBase.m_flFlashDuration", &runtime_offsets::Values::C_CSPlayerPawnBase_m_flFlashDuration},
        OffsetField{"schemas", "C_CSPlayerPawnBase.m_pItemServices", &runtime_offsets::Values::C_CSPlayerPawnBase_m_pItemServices},
        OffsetField{"schemas", "CCSPlayer_ItemServices.m_bHasDefuser", &runtime_offsets::Values::CCSPlayer_ItemServices_m_bHasDefuser},
        OffsetField{"schemas", "CCSPlayer_ItemServices.m_bHasHelmet", &runtime_offsets::Values::CCSPlayer_ItemServices_m_bHasHelmet},
        OffsetField{"schemas", "C_CSPlayerPawn.m_entitySpottedState", &runtime_offsets::Values::C_CSPlayerPawn_m_entitySpottedState},
        OffsetField{"schemas", "C_EconEntity.m_AttributeManager", &runtime_offsets::Values::C_EconEntity_m_AttributeManager},
        OffsetField{"schemas", "C_AttributeContainer.m_Item", &runtime_offsets::Values::C_AttributeContainer_m_Item},
        OffsetField{"schemas", "C_EconItemView.m_iItemDefinitionIndex", &runtime_offsets::Values::C_EconItemView_m_iItemDefinitionIndex},
        OffsetField{"schemas", "C_CSGameRules.m_vMinimapMins", &runtime_offsets::Values::C_CSGameRules_m_vMinimapMins},
        OffsetField{"schemas", "C_CSGameRules.m_vMinimapMaxs", &runtime_offsets::Values::C_CSGameRules_m_vMinimapMaxs},
        OffsetField{"schemas", "C_CSGameRules.m_bBombPlanted", &runtime_offsets::Values::C_CSGameRules_m_bBombPlanted},
        OffsetField{"schemas", "C_CSGameRules.m_bBombDropped", &runtime_offsets::Values::C_CSGameRules_m_bBombDropped},
        OffsetField{"schemas", "C_CSGameRules.m_nRoundStartCount", &runtime_offsets::Values::C_CSGameRules_m_nRoundStartCount},
        OffsetField{"schemas", "C_CSGameRules.m_totalRoundsPlayed", &runtime_offsets::Values::C_CSGameRules_m_totalRoundsPlayed},
        OffsetField{"schemas", "CPlayer_ObserverServices.m_iObserverMode", &runtime_offsets::Values::CPlayer_ObserverServices_m_iObserverMode},
        OffsetField{"schemas", "CPlayer_ObserverServices.m_hObserverTarget", &runtime_offsets::Values::CPlayer_ObserverServices_m_hObserverTarget},
        OffsetField{"schemas", "CGameSceneNode.m_bDormant", &runtime_offsets::Values::CGameSceneNode_m_bDormant},
        OffsetField{"schemas", "CGameSceneNode.m_vecAbsOrigin", &runtime_offsets::Values::CGameSceneNode_m_vecAbsOrigin},
        OffsetField{"schemas", "EntitySpottedState_t.m_bSpottedByMask", &runtime_offsets::Values::EntitySpottedState_t_m_bSpottedByMask},
        OffsetField{"schemas", "C_PlantedC4.m_bBombTicking", &runtime_offsets::Values::C_PlantedC4_m_bBombTicking},
        OffsetField{"schemas", "C_PlantedC4.m_flC4Blow", &runtime_offsets::Values::C_PlantedC4_m_flC4Blow},
        OffsetField{"schemas", "C_PlantedC4.m_bHasExploded", &runtime_offsets::Values::C_PlantedC4_m_bHasExploded},
        OffsetField{"schemas", "C_PlantedC4.m_flTimerLength", &runtime_offsets::Values::C_PlantedC4_m_flTimerLength},
        OffsetField{"schemas", "C_PlantedC4.m_bBeingDefused", &runtime_offsets::Values::C_PlantedC4_m_bBeingDefused},
        OffsetField{"schemas", "C_PlantedC4.m_bC4Activated", &runtime_offsets::Values::C_PlantedC4_m_bC4Activated},
        OffsetField{"schemas", "C_PlantedC4.m_flDefuseLength", &runtime_offsets::Values::C_PlantedC4_m_flDefuseLength},
        OffsetField{"schemas", "C_PlantedC4.m_flDefuseCountDown", &runtime_offsets::Values::C_PlantedC4_m_flDefuseCountDown},
        OffsetField{"schemas", "C_PlantedC4.m_bBombDefused", &runtime_offsets::Values::C_PlantedC4_m_bBombDefused},
        OffsetField{"schemas", "C_PlantedC4.m_hBombDefuser", &runtime_offsets::Values::C_PlantedC4_m_hBombDefuser},
        OffsetField{"schemas", "C_Inferno.m_nFireEffectTickBegin", &runtime_offsets::Values::C_Inferno_m_nFireEffectTickBegin},
        OffsetField{"schemas", "C_Inferno.m_nFireLifetime", &runtime_offsets::Values::C_Inferno_m_nFireLifetime},
        OffsetField{"schemas", "C_Inferno.m_fireCount", &runtime_offsets::Values::C_Inferno_m_fireCount},
        OffsetField{"schemas", "C_Inferno.m_firePositions", &runtime_offsets::Values::C_Inferno_m_firePositions},
        OffsetField{"schemas", "C_Inferno.m_bInPostEffectTime", &runtime_offsets::Values::C_Inferno_m_bInPostEffectTime},
        OffsetField{"schemas", "C_SmokeGrenadeProjectile.m_nSmokeEffectTickBegin", &runtime_offsets::Values::C_SmokeGrenadeProjectile_m_nSmokeEffectTickBegin},
        OffsetField{"schemas", "C_SmokeGrenadeProjectile.m_bDidSmokeEffect", &runtime_offsets::Values::C_SmokeGrenadeProjectile_m_bDidSmokeEffect},
        OffsetField{"schemas", "C_SmokeGrenadeProjectile.m_bSmokeVolumeDataReceived", &runtime_offsets::Values::C_SmokeGrenadeProjectile_m_bSmokeVolumeDataReceived},
        OffsetField{"schemas", "C_SmokeGrenadeProjectile.m_bSmokeEffectSpawned", &runtime_offsets::Values::C_SmokeGrenadeProjectile_m_bSmokeEffectSpawned},
        OffsetField{"schemas", "C_DecoyProjectile.m_nDecoyShotTick", &runtime_offsets::Values::C_DecoyProjectile_m_nDecoyShotTick},
        OffsetField{"schemas", "C_DecoyProjectile.m_nClientLastKnownDecoyShotTick", &runtime_offsets::Values::C_DecoyProjectile_m_nClientLastKnownDecoyShotTick},
        OffsetField{"schemas", "C_BaseCSGrenadeProjectile.m_nExplodeEffectTickBegin", &runtime_offsets::Values::C_BaseCSGrenadeProjectile_m_nExplodeEffectTickBegin},
        OffsetField{"schemas", "CSkeletonInstance.m_modelState", &runtime_offsets::Values::CSkeletonInstance_m_modelState},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_WeaponType", &runtime_offsets::Values::CCSWeaponBaseVData_m_WeaponType},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_nNumBullets", &runtime_offsets::Values::CCSWeaponBaseVData_m_nNumBullets},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_flSpread", &runtime_offsets::Values::CCSWeaponBaseVData_m_flSpread},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_flMaxSpeed", &runtime_offsets::Values::CCSWeaponBaseVData_m_flMaxSpeed},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_flInaccuracyMove", &runtime_offsets::Values::CCSWeaponBaseVData_m_flInaccuracyMove},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_flInaccuracyJumpInitial", &runtime_offsets::Values::CCSWeaponBaseVData_m_flInaccuracyJumpInitial},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_flInaccuracyJumpApex", &runtime_offsets::Values::CCSWeaponBaseVData_m_flInaccuracyJumpApex},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_flInaccuracyStand", &runtime_offsets::Values::CCSWeaponBaseVData_m_flInaccuracyStand},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_flInaccuracyCrouch", &runtime_offsets::Values::CCSWeaponBaseVData_m_flInaccuracyCrouch},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_flCycleTime", &runtime_offsets::Values::CCSWeaponBaseVData_m_flCycleTime},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_bIsFullAuto", &runtime_offsets::Values::CCSWeaponBaseVData_m_bIsFullAuto},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_nDamage", &runtime_offsets::Values::CCSWeaponBaseVData_m_nDamage},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_flPenetration", &runtime_offsets::Values::CCSWeaponBaseVData_m_flPenetration},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_flRangeModifier", &runtime_offsets::Values::CCSWeaponBaseVData_m_flRangeModifier},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_flRange", &runtime_offsets::Values::CCSWeaponBaseVData_m_flRange},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_flArmorRatio", &runtime_offsets::Values::CCSWeaponBaseVData_m_flArmorRatio},
        OffsetField{"schemas", "CCSWeaponBaseVData.m_flHeadshotMultiplier", &runtime_offsets::Values::CCSWeaponBaseVData_m_flHeadshotMultiplier},
    };

    bool HasMeaningfulOffsetState(const OffsetState& state)
    {
        return !state.offsetsPatch.patchVersion.empty() ||
               !state.offsetsPatch.etag.empty() ||
               !state.offsetsPatch.lastFileSha.empty() ||
               state.offsetsPatch.clientVersion != 0 ||
               state.offsetsPatch.sourceRevision != 0 ||
               !state.lastSeenPatch.patchVersion.empty() ||
               !state.lastSeenPatch.etag.empty() ||
               !state.lastSeenPatch.lastFileSha.empty() ||
               state.lastSeenPatch.clientVersion != 0 ||
               state.lastSeenPatch.sourceRevision != 0 ||
               !state.selectedSource.empty() ||
               !state.selectedSourceTimestamp.empty() ||
               !state.remoteOutputTimestamp.empty() ||
               state.selectedSourceBuildNumber != 0;
    }

    OffsetState ReadOffsetStateFromRoot(const json& root)
    {
        OffsetState state = {};

        const json* stateRoot = &root;
        const auto stateIt = root.find("state");
        if (stateIt != root.end() && stateIt->is_object())
            stateRoot = &(*stateIt);

        state.offsetsPatch = ReadPatchInfo(*stateRoot, "offsets_patch");
        state.lastSeenPatch = ReadPatchInfo(*stateRoot, "last_seen_patch");
        state.selectedSource = JsonStringOrEmpty(*stateRoot, "selected_source");
        state.selectedSourceTimestamp = JsonStringOrEmpty(*stateRoot, "selected_source_timestamp");
        state.remoteOutputTimestamp = JsonStringOrEmpty(*stateRoot, "remote_output_timestamp");
        state.selectedSourceBuildNumber = JsonIntOrZero(*stateRoot, "selected_source_build_number");
        return state;
    }

    OffsetState ReadOffsetState(const std::filesystem::path& jsonPath, const std::filesystem::path& legacyStatePath)
    {
        OffsetState state = {};
        if (!jsonPath.empty())
        {
            std::ifstream file(jsonPath, std::ios::binary);
            if (file.is_open())
            {
                json root = json::parse(file, nullptr, false);
                if (!root.is_discarded() && root.is_object()) {
                    state = ReadOffsetStateFromRoot(root);
                    if (HasMeaningfulOffsetState(state))
                        return state;
                }
            }
        }

        if (legacyStatePath.empty())
            return state;

        std::ifstream legacyFile(legacyStatePath, std::ios::binary);
        if (!legacyFile.is_open())
            return state;

        json legacyRoot = json::parse(legacyFile, nullptr, false);
        if (legacyRoot.is_discarded() || !legacyRoot.is_object())
            return state;

        state = ReadOffsetStateFromRoot(legacyRoot);
        return state;
    }

    template <typename JsonT>
    void WriteOffsetStateSection(JsonT& root, const OffsetState& state)
    {
        if (!HasMeaningfulOffsetState(state)) {
            root.erase("state");
            return;
        }

        JsonT stateRoot = JsonT::object();
        WritePatchInfo(stateRoot, "offsets_patch", state.offsetsPatch);
        WritePatchInfo(stateRoot, "last_seen_patch", state.lastSeenPatch);
        stateRoot["selected_source"] = state.selectedSource;
        stateRoot["selected_source_timestamp"] = state.selectedSourceTimestamp;
        stateRoot["remote_output_timestamp"] = state.remoteOutputTimestamp;
        stateRoot["selected_source_build_number"] = state.selectedSourceBuildNumber;
        root["state"] = std::move(stateRoot);
    }

    bool WriteOffsetsJson(
        const std::filesystem::path& jsonPath,
        const runtime_offsets::Values& values,
        const OffsetState* state = nullptr)
    {
        try
        {
            if (jsonPath.empty())
                return false;

            std::filesystem::create_directories(jsonPath.parent_path());

            std::filesystem::path tmpPath = jsonPath;
            tmpPath += ".tmp";

            ordered_json root = ordered_json::object();
            for (const auto& field : kOffsetFields)
                root[field.section][field.key] = ToHex(values.*(field.member));

            if (state != nullptr)
                WriteOffsetStateSection(root, *state);

            {
                std::ofstream out(tmpPath, std::ios::out | std::ios::trunc);
                if (!out.is_open())
                    return false;
                out << root.dump(4) << '\n';
                out.flush();
                if (!out.good())
                    return false;
            }

            std::error_code ec;
            if (!app::platform::ReplaceFileWithTemp(tmpPath, jsonPath, ec)) {
                std::filesystem::remove(tmpPath, ec);
                return false;
            }
            app::platform::RemoveFileIfExists(GetOffsetsStatePath());
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool WriteOffsetState(const std::filesystem::path& jsonPath, const OffsetState& state)
    {
        if (jsonPath.empty())
            return false;

        json root;
        if (!TryParseOffsetsJsonFile(jsonPath, root))
            return false;

        runtime_offsets::Values values = {};
        LoadValuesFromJson(root, values, nullptr, nullptr);
        if (!ValidateLoadedValues(values, true).empty())
            return false;
        return WriteOffsetsJson(jsonPath, values, &state);
    }

    void CleanupObsoleteOffsetsIniFiles()
    {
        std::unordered_set<std::string> seen;
        const std::filesystem::path removablePaths[] = {
            GetOffsetsDirectory() / "offsets.ini",
            GetLegacyProfilesDirectory() / "offsets.ini",
            GetExecutableDirectory() / "offsets.ini",
        };

        for (const auto& path : removablePaths)
        {
            const std::string key = path.lexically_normal().generic_string();
            if (!seen.insert(key).second)
                continue;
            app::platform::RemoveFileIfExists(path);
        }
    }

    enum class ReadResult : uint8_t
    {
        Parsed,
        Missing,
        Invalid
    };

    LegacyIniDocument ParseLegacyIniFile(const std::filesystem::path& path)
    {
        LegacyIniDocument ini;
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return ini;

        std::string currentSection;
        std::string line;
        while (std::getline(file, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
            {
                line.erase(0, 3);
            }

            const std::string trimmed = Trim(line);
            if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#')
                continue;

            if (trimmed.front() == '[' && trimmed.back() == ']')
            {
                currentSection = Trim(trimmed.substr(1, trimmed.size() - 2));
                continue;
            }

            const size_t equals = trimmed.find('=');
            if (equals == std::string::npos || currentSection.empty())
                continue;

            const std::string key = Trim(trimmed.substr(0, equals));
            if (key.empty())
                continue;

            ini[currentSection][key] = Trim(trimmed.substr(equals + 1));
        }

        return ini;
    }

    const std::string* FindLegacyIniValue(const LegacyIniDocument& ini,
                                          std::string_view section,
                                          std::string_view key)
    {
        const auto secIt = ini.find(std::string(section));
        if (secIt == ini.end())
            return nullptr;

        const auto keyIt = secIt->second.find(std::string(key));
        if (keyIt == secIt->second.end())
            return nullptr;

        return &keyIt->second;
    }

    std::wstring ToWide(std::string_view text)
    {
        if (text.empty())
            return {};

        const int length =
            MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
        if (length <= 0)
            return {};

        std::wstring result(static_cast<size_t>(length), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), length);
        return result;
    }

    const std::wstring& HttpUserAgentWide()
    {
        static const std::wstring kUserAgent = ToWide(app::build_info::HttpUserAgent());
        return kUserAgent;
    }

    const std::wstring& HttpUserAgentHeader()
    {
        static const std::wstring kHeader = L"User-Agent: " + HttpUserAgentWide();
        return kHeader;
    }

    bool HasHeaderName(const std::vector<std::wstring>& headers, std::wstring_view headerName)
    {
        for (const std::wstring& header : headers)
        {
            if (header.size() < headerName.size() + 1)
                continue;
            if (_wcsnicmp(header.c_str(), headerName.data(), headerName.size()) != 0)
                continue;
            if (header[headerName.size()] == L':')
                return true;
        }

        return false;
    }

    std::vector<std::wstring> BuildRequestHeaders(const std::vector<std::wstring>& extraHeaders = {})
    {
        std::vector<std::wstring> headers = extraHeaders;
        if (!HasHeaderName(headers, L"User-Agent"))
            headers.push_back(HttpUserAgentHeader());
        return headers;
    }

    ReadResult ReadLegacyIniOffset(const LegacyIniDocument& ini,
                                   const char* section,
                                   const char* key,
                                   std::ptrdiff_t& outValue)
    {
        const std::string* raw = FindLegacyIniValue(ini, section, key);
        if (raw == nullptr)
            return ReadResult::Missing;

        if (!TryParseOffset(*raw, outValue))
            return ReadResult::Invalid;

        return ReadResult::Parsed;
    }

    ReadResult ReadJsonOffset(const json& root,
                              const char* section,
                              const char* key,
                              std::ptrdiff_t& outValue)
    {
        auto parseValue = [&](const json& value) -> ReadResult {
            if (value.is_number_integer())
            {
                const auto parsed = value.get<std::int64_t>();
                if (parsed < static_cast<std::int64_t>((std::numeric_limits<std::ptrdiff_t>::min)()) ||
                    parsed > static_cast<std::int64_t>((std::numeric_limits<std::ptrdiff_t>::max)())) {
                    return ReadResult::Invalid;
                }
                outValue = static_cast<std::ptrdiff_t>(parsed);
                return ReadResult::Parsed;
            }

            if (value.is_number_unsigned())
            {
                const auto parsed = value.get<std::uint64_t>();
                if (parsed > static_cast<std::uint64_t>((std::numeric_limits<std::ptrdiff_t>::max)()))
                    return ReadResult::Invalid;
                outValue = static_cast<std::ptrdiff_t>(parsed);
                return ReadResult::Parsed;
            }

            if (value.is_string() && TryParseOffset(value.get_ref<const std::string&>(), outValue))
                return ReadResult::Parsed;

            return ReadResult::Invalid;
        };

        auto readFromSection = [&](const char* sectionName) -> ReadResult {
            const auto secIt = root.find(sectionName);
            if (secIt == root.end() || !secIt->is_object())
                return ReadResult::Missing;

            const auto keyIt = secIt->find(key);
            if (keyIt == secIt->end())
                return ReadResult::Missing;

            return parseValue(*keyIt);
        };

        const ReadResult directResult = readFromSection(section);
        if (directResult != ReadResult::Missing)
            return directResult;

        if (std::string_view(section) == "offsets")
        {
            static constexpr const char* kModuleSections[] = {
                "client.dll",
                "engine2.dll",
                "matchmaking.dll",
                "inputsystem.dll",
                "soundsystem.dll"
            };
            for (const char* moduleSection : kModuleSections)
            {
                const ReadResult moduleResult = readFromSection(moduleSection);
                if (moduleResult != ReadResult::Missing)
                    return moduleResult;
            }
        }

        return ReadResult::Missing;
    }

    bool IsRequiredOffsetMember(std::ptrdiff_t runtime_offsets::Values::*member)
    {
        for (const auto& field : kRequiredRemoteFields)
        {
            if (field.member == member)
                return true;
        }
        return false;
    }

    void LoadValuesFromSource(runtime_offsets::Values& outValues,
                              std::vector<std::string>* missingKeys,
                              std::vector<std::string>* invalidKeys,
                              auto&& reader)
    {
        outValues = {};
        for (const auto& field : kOffsetFields)
        {
            std::ptrdiff_t value = 0;
            const ReadResult result = reader(field.section, field.key, value);
            if (result == ReadResult::Parsed)
                outValues.*(field.member) = value;
            else
                outValues.*(field.member) = 0;

            if (result == ReadResult::Missing && missingKeys)
                missingKeys->emplace_back(std::string(field.section) + "." + field.key);
            else if (result == ReadResult::Invalid && invalidKeys)
                invalidKeys->emplace_back(std::string(field.section) + "." + field.key);
        }
    }

    bool TryParseOffsetsJsonFile(const std::filesystem::path& jsonPath, json& outRoot)
    {
        std::ifstream file(jsonPath, std::ios::binary);
        if (!file.is_open())
            return false;

        outRoot = json::parse(file, nullptr, false);
        return !outRoot.is_discarded() && outRoot.is_object();
    }

    void LoadValuesFromJson(const json& root,
                            runtime_offsets::Values& outValues,
                            std::vector<std::string>* missingKeys,
                            std::vector<std::string>* invalidKeys)
    {
        LoadValuesFromSource(
            outValues,
            missingKeys,
            invalidKeys,
            [&](const char* section, const char* key, std::ptrdiff_t& outValue) {
                return ReadJsonOffset(root, section, key, outValue);
            });
    }

    void LoadValuesFromLegacyIni(const LegacyIniDocument& ini,
                                 runtime_offsets::Values& outValues,
                                 std::vector<std::string>* missingKeys,
                                 std::vector<std::string>* invalidKeys)
    {
        LoadValuesFromSource(
            outValues,
            missingKeys,
            invalidKeys,
            [&](const char* section, const char* key, std::ptrdiff_t& outValue) {
                return ReadLegacyIniOffset(ini, section, key, outValue);
            });
    }

    std::vector<std::string> ValidateLoadedValues(const runtime_offsets::Values& values, bool requiredOnly)
    {
        std::vector<std::string> zeroFields;
        for (const auto& field : kOffsetFields)
        {
            if (requiredOnly && !IsRequiredOffsetMember(field.member))
                continue;
            if (values.*(field.member) <= 0)
                zeroFields.emplace_back(std::string(field.section) + "." + field.key);
        }
        return zeroFields;
    }

    size_t CountValueDifferences(const runtime_offsets::Values& a, const runtime_offsets::Values& b)
    {
        size_t count = 0;
        for (const auto& field : kOffsetFields)
        {
            if (a.*(field.member) != b.*(field.member))
                ++count;
        }
        return count;
    }

    std::string JoinKeys(const std::vector<std::string>& keys)
    {
        std::ostringstream oss;
        for (size_t i = 0; i < keys.size(); ++i)
        {
            if (i > 0)
                oss << ", ";
            oss << keys[i];
        }
        return oss.str();
    }

    std::string WinHttpErrorText(const char* operation, DWORD errorCode)
    {
        std::string message = app::localization::Format(
            "{} failed",
            operation ? operation : "WinHTTP");
        if (errorCode != ERROR_SUCCESS)
        {
            std::string detail;
            char* rawMessage = nullptr;
            const DWORD length = FormatMessageA(
                FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                nullptr,
                errorCode,
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                reinterpret_cast<LPSTR>(&rawMessage),
                0,
                nullptr);
            if (length > 0 && rawMessage)
            {
                detail = Trim(std::string(rawMessage, length));
                LocalFree(rawMessage);
            }

            message += detail.empty()
                ? app::localization::Format(" (error {})", errorCode)
                : app::localization::Format(
                    " (error {}: {})",
                    errorCode,
                    detail);
        }
        return message;
    }

    void AppendAttemptError(std::string& combined, int attempt, const std::string& attemptError)
    {
        if (!combined.empty())
            combined += "; ";
        combined += app::localization::Format(
            "try {}: {}",
            attempt,
            attemptError);
    }

    struct NetworkRetryBudget
    {
        static constexpr int kMaxConsecutiveFailures = 3;

        bool localFallbackAllowed = true;
        int consecutiveFailures = 0;
        std::string failureDetails;

        bool Exhausted() const noexcept
        {
            return consecutiveFailures >= kMaxConsecutiveFailures;
        }

        void RecordSuccess()
        {
            consecutiveFailures = 0;
            failureDetails.clear();
        }

        void RecordFailure(const std::string& error)
        {
            if (Exhausted())
                return;
            ++consecutiveFailures;
            AppendAttemptError(
                failureDetails,
                consecutiveFailures,
                error.empty()
                    ? app::localization::GetCopy("source unavailable")
                    : error);
        }

        std::string BuildSkipReason() const
        {
            if (localFallbackAllowed) {
                return failureDetails.empty()
                    ? app::localization::GetCopy(
                        "Network offset checks skipped after 3 failed attempts. Local offsets were used.")
                    : app::localization::Format(
                        "Network offset checks skipped after 3 failed attempts ({}). Local offsets were used.",
                        failureDetails);
            }
            return failureDetails.empty()
                ? app::localization::GetCopy(
                    "Network offset fallback stopped after 3 failed attempts.")
                : app::localization::Format(
                    "Network offset fallback stopped after 3 failed attempts ({}).",
                    failureDetails);
        }
    };

    std::vector<std::string> BuildGitHubDownloadCandidates(const std::string& url)
    {
        constexpr std::string_view kRawPrefix = "https://raw.githubusercontent.com/";
        if (!url.starts_with(kRawPrefix))
            return { url };

        const std::string_view tail(url.data() + kRawPrefix.size(), url.size() - kRawPrefix.size());
        const size_t ownerEnd = tail.find('/');
        const size_t repoEnd = ownerEnd == std::string_view::npos ? std::string_view::npos : tail.find('/', ownerEnd + 1);
        const size_t branchEnd = repoEnd == std::string_view::npos ? std::string_view::npos : tail.find('/', repoEnd + 1);
        if (ownerEnd == std::string_view::npos ||
            repoEnd == std::string_view::npos ||
            branchEnd == std::string_view::npos) {
            return { url };
        }

        const std::string owner(tail.substr(0, ownerEnd));
        const std::string repository(tail.substr(ownerEnd + 1, repoEnd - ownerEnd - 1));
        const std::string branch(tail.substr(repoEnd + 1, branchEnd - repoEnd - 1));
        const std::string path(tail.substr(branchEnd + 1));
        if (owner.empty() || repository.empty() || branch.empty() || path.empty())
            return { url };

        return {
            "https://cdn.jsdelivr.net/gh/" + owner + "/" + repository + "@" + branch + "/" + path,
            url,
            "https://cdn.statically.io/gh/" + owner + "/" + repository + "/" + branch + "/" + path,
        };
    }

    template <typename AttemptFn>
    bool TryGitHubCandidates(const std::string& url,
                             const char* failureLabel,
                             NetworkRetryBudget& retryBudget,
                             AttemptFn&& attemptFn,
                             std::string* error)
    {
        const std::vector<std::string> candidates = BuildGitHubDownloadCandidates(url);
        std::string combinedErrors;
        size_t performedAttempts = 0;
        for (size_t attempt = 0;
             attempt < candidates.size() && attempt < 3 && !retryBudget.Exhausted();
             ++attempt) {
            ++performedAttempts;
            std::string attemptError;
            if (attemptFn(candidates[attempt], &attemptError)) {
                retryBudget.RecordSuccess();
                if (error)
                    error->clear();
                return true;
            }

            AppendAttemptError(combinedErrors, static_cast<int>(attempt + 1), attemptError);
            retryBudget.RecordFailure(attemptError);
            if (attempt + 1 < candidates.size() &&
                attempt + 1 < 3 &&
                !retryBudget.Exhausted()) {
                Sleep(static_cast<DWORD>(120 * (attempt + 1)));
            }
        }

        if (error) {
            if (retryBudget.Exhausted()) {
                *error = retryBudget.BuildSkipReason();
            } else {
                *error = app::localization::Format(
                    "{} failed after {} attempt(s) ({})",
                    failureLabel ? failureLabel : "Request",
                    performedAttempts,
                    combinedErrors);
            }
        }
        return false;
    }

    struct WinHttpGetRequest
    {
        app::platform::UniqueWinHttpHandle session;
        app::platform::UniqueWinHttpHandle connection;
        app::platform::UniqueWinHttpHandle request;
    };

    bool OpenAndSendWinHttpGet(const std::string& url,
                               const std::vector<std::wstring>& extraHeaders,
                               WinHttpGetRequest& out,
                               std::string* error)
    {
        const bool useHttps = url.rfind("https://", 0) == 0;
        size_t hostStart = url.find("://");
        if (hostStart == std::string::npos) {
            if (error)
                *error = "Invalid URL: " + url;
            return false;
        }
        hostStart += 3;
        const size_t pathStart = url.find('/', hostStart);
        const std::string host =
            pathStart != std::string::npos
                ? url.substr(hostStart, pathStart - hostStart)
                : url.substr(hostStart);
        const std::string path =
            pathStart != std::string::npos ? url.substr(pathStart) : "/";
        const std::wstring wideHost(host.begin(), host.end());
        const std::wstring widePath(path.begin(), path.end());

        out.session.Reset(WinHttpOpen(
            HttpUserAgentWide().c_str(),
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0));
        if (!out.session) {
            if (error)
                *error = WinHttpErrorText("WinHttpOpen", GetLastError());
            return false;
        }

        const INTERNET_PORT port =
            useHttps ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
        out.connection.Reset(WinHttpConnect(out.session.Get(), wideHost.c_str(), port, 0));
        if (!out.connection) {
            if (error)
                *error = WinHttpErrorText("WinHttpConnect", GetLastError()) + " for " + host;
            return false;
        }

        const DWORD flags = useHttps ? WINHTTP_FLAG_SECURE : 0;
        out.request.Reset(WinHttpOpenRequest(
            out.connection.Get(),
            L"GET",
            widePath.c_str(),
            nullptr,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            flags));
        if (!out.request) {
            if (error)
                *error = WinHttpErrorText("WinHttpOpenRequest", GetLastError());
            return false;
        }

        WinHttpSetTimeouts(
            out.request.Get(),
            kGitHubHttpResolveTimeoutMs,
            kGitHubHttpConnectTimeoutMs,
            kGitHubHttpSendTimeoutMs,
            kGitHubHttpReceiveTimeoutMs);
        for (const std::wstring& header : BuildRequestHeaders(extraHeaders)) {
            if (header.empty())
                continue;
            WinHttpAddRequestHeaders(
                out.request.Get(),
                header.c_str(),
                static_cast<DWORD>(header.size()),
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
        }

        const BOOL sendOk = WinHttpSendRequest(
            out.request.Get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0);
        DWORD requestError = sendOk ? ERROR_SUCCESS : GetLastError();
        BOOL receiveOk = FALSE;
        if (sendOk) {
            receiveOk = WinHttpReceiveResponse(out.request.Get(), nullptr);
            if (!receiveOk)
                requestError = GetLastError();
        }
        if (!sendOk || !receiveOk) {
            if (error) {
                *error = WinHttpErrorText(
                    sendOk ? "WinHttpReceiveResponse" : "WinHttpSendRequest",
                    requestError) + " for " + url;
            }
            return false;
        }
        return true;
    }

    bool DownloadFileOnce(const char* url, const std::filesystem::path& destination, std::string* error)
    {
        try
        {
            std::filesystem::create_directories(destination.parent_path());
        }
        catch (...)
        {
            if (error)
                *error = "Cannot create temp directory for download.";
            return false;
        }

        const std::string urlStr = url ? url : "";
        WinHttpGetRequest httpRequest;
        if (!OpenAndSendWinHttpGet(urlStr, {}, httpRequest, error))
            return false;
        const auto& hRequest = httpRequest.request;

        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        if (!WinHttpQueryHeaders(
                hRequest.Get(),
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &statusCode,
                &statusCodeSize,
                WINHTTP_NO_HEADER_INDEX)) {
            statusCode = 0;
        }

        if (statusCode < 200 || statusCode >= 300) {
            if (error)
                *error = "Download returned status " + std::to_string(statusCode) + " for " + urlStr;
            return false;
        }

        std::filesystem::path tmpDestination = destination;
        tmpDestination += ".tmp";

        std::ofstream out(tmpDestination, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            if (error) *error = "Cannot open output file";
            return false;
        }

        DWORD bytesRead = 0;
        char buf[8192];
        BOOL readOk = TRUE;
        while ((readOk = WinHttpReadData(hRequest.Get(), buf, sizeof(buf), &bytesRead)) == TRUE && bytesRead > 0) {
            out.write(buf, bytesRead);
        }
        const DWORD readError = readOk ? ERROR_SUCCESS : GetLastError();

        out.flush();
        const bool writeOk = out.good() && readOk;
        out.close();

        if (!readOk) {
            std::error_code rmEc;
            std::filesystem::remove(tmpDestination, rmEc);
            if (error)
                *error = WinHttpErrorText("WinHttpReadData", readError) + " for " + urlStr;
            return false;
        }

        if (!writeOk) {
            std::error_code rmEc;
            std::filesystem::remove(tmpDestination, rmEc);
            if (error) *error = "Write failed";
            return false;
        }

        std::error_code ec;
        if (!app::platform::ReplaceFileWithTemp(tmpDestination, destination, ec)) {
            std::filesystem::remove(tmpDestination, ec);
            if (error) *error = "Replace failed: " + ec.message();
            return false;
        }
        return true;
    }

    bool DownloadFile(const char* url,
                      const std::filesystem::path& destination,
                      NetworkRetryBudget& retryBudget,
                      std::string* error)
    {
        return TryGitHubCandidates(
            url ? std::string(url) : std::string(),
            "Download",
            retryBudget,
            [&](const std::string& candidate, std::string* attemptError) {
                return DownloadFileOnce(candidate.c_str(), destination, attemptError);
            },
            error);
    }

    struct HttpTextResponse
    {
        DWORD statusCode = 0;
        std::string body;
        std::string etag;
    };

    bool HttpGetTextOnce(const std::string& url,
                         const std::vector<std::wstring>& extraHeaders,
                         HttpTextResponse& out,
                         std::string* error)
    {
        const std::string& urlStr = url;
        WinHttpGetRequest httpRequest;
        if (!OpenAndSendWinHttpGet(urlStr, extraHeaders, httpRequest, error))
            return false;
        const auto& hRequest = httpRequest.request;

        DWORD statusCode = 0;
        DWORD statusCodeSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusCodeSize, WINHTTP_NO_HEADER_INDEX);
        out.statusCode = statusCode;

        std::wstring etagHeaderName = L"ETag";
        DWORD etagBytes = 0;
        if (!WinHttpQueryHeaders(hRequest.Get(), WINHTTP_QUERY_CUSTOM, etagHeaderName.data(), WINHTTP_NO_OUTPUT_BUFFER, &etagBytes, WINHTTP_NO_HEADER_INDEX) &&
            GetLastError() == ERROR_INSUFFICIENT_BUFFER &&
            etagBytes > sizeof(wchar_t))
        {
            std::wstring etagWide(etagBytes / sizeof(wchar_t), L'\0');
            if (WinHttpQueryHeaders(hRequest.Get(), WINHTTP_QUERY_CUSTOM, etagHeaderName.data(), etagWide.data(), &etagBytes, WINHTTP_NO_HEADER_INDEX))
            {
                const size_t returnedCharacters =
                    (std::min)(etagWide.size(), static_cast<size_t>(etagBytes / sizeof(wchar_t)));
                etagWide.resize(returnedCharacters);
                while (!etagWide.empty() && etagWide.back() == L'\0')
                    etagWide.pop_back();
                out.etag.clear();
                out.etag.reserve(etagWide.size());
                for (const wchar_t ch : etagWide)
                    out.etag.push_back(static_cast<char>(ch));
            }
        }

        out.body.clear();
        DWORD bytesRead = 0;
        char buffer[8192];
        BOOL readOk = TRUE;
        while ((readOk = WinHttpReadData(hRequest.Get(), buffer, sizeof(buffer), &bytesRead)) == TRUE && bytesRead > 0)
            out.body.append(buffer, buffer + bytesRead);
        const DWORD readError = readOk ? ERROR_SUCCESS : GetLastError();

        if (!readOk)
        {
            if (error)
                *error = WinHttpErrorText("WinHttpReadData", readError) + " for " + urlStr;
            return false;
        }

        if (out.statusCode < 200 || out.statusCode >= 300)
        {
            if (error)
                *error = "HTTP GET returned status " + std::to_string(out.statusCode) + " for " + urlStr;
            return false;
        }

        return true;
    }

    bool HttpGetText(const std::string& url,
                     const std::vector<std::wstring>& extraHeaders,
                     HttpTextResponse& out,
                     NetworkRetryBudget& retryBudget,
                     std::string* error)
    {
        return TryGitHubCandidates(
            url,
            "HTTP GET",
            retryBudget,
            [&](const std::string& candidate, std::string* attemptError) {
                return HttpGetTextOnce(candidate, extraHeaders, out, attemptError);
            },
            error);
    }

    std::string StripQuotes(std::string text)
    {
        text = Trim(text);
        if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
            return text.substr(1, text.size() - 2);
        return text;
    }

    bool TryParseInt(const std::string& text, int& outValue)
    {
        try
        {
            outValue = std::stoi(Trim(text));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool FetchSteamInfSnapshot(SteamInfSnapshot& snapshot,
                               NetworkRetryBudget& retryBudget,
                               std::string* error)
    {
        HttpTextResponse response = {};
        if (!HttpGetText(
                "https://raw.githubusercontent.com/SteamTracking/GameTracking-CS2/master/game/csgo/steam.inf",
                {},
                response,
                retryBudget,
                error))
        {
            return false;
        }

        std::unordered_map<std::string, std::string> kv;
        std::istringstream stream(response.body);
        std::string line;
        while (std::getline(stream, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            const size_t equals = line.find('=');
            if (equals == std::string::npos)
                continue;

            const std::string key = Trim(line.substr(0, equals));
            if (key.empty())
                continue;
            kv[key] = Trim(line.substr(equals + 1));
        }

        snapshot = {};
        snapshot.patch.etag = StripQuotes(response.etag);
        snapshot.patch.lastFileSha = snapshot.patch.etag;
        snapshot.patch.patchVersion = kv["PatchVersion"];
        snapshot.versionDate = kv["VersionDate"];
        snapshot.versionTime = kv["VersionTime"];
        TryParseInt(kv["ClientVersion"], snapshot.patch.clientVersion);
        TryParseInt(kv["SourceRevision"], snapshot.patch.sourceRevision);
        snapshot.patchBuildNumber = ParsePatchBuildNumber(snapshot.patch.patchVersion);

        if (snapshot.patch.patchVersion.empty() || snapshot.patch.clientVersion <= 0 || snapshot.patch.sourceRevision <= 0)
        {
            if (error)
                *error = "steam.inf is missing PatchVersion, ClientVersion, or SourceRevision.";
            return false;
        }

        return true;
    }

    bool ParseHeaderConstants(const std::filesystem::path& filePath,
                              std::unordered_map<std::string, std::ptrdiff_t>& outMap,
                              std::string* error)
    {
        std::ifstream file(filePath, std::ios::in | std::ios::binary);
        if (!file.is_open())
        {
            if (error)
                *error = "Unable to open " + filePath.string();
            return false;
        }

        static const std::regex kConstRegex(
            R"(^\s*constexpr\s+std::ptrdiff_t\s+([A-Za-z_]\w*)\s*=\s*(0x[0-9A-Fa-f]+|\d+)\s*;)");

        std::vector<std::string> namespaceStack;
        std::vector<int> namespaceDepths;
        int braceDepth = 0;
        std::string line;
        std::smatch match;

        while (std::getline(file, line))
        {
            if (std::regex_search(line, match, kConstRegex))
            {
                const std::string name = match[1].str();
                const std::string valueText = match[2].str();

                std::ptrdiff_t value = 0;
                try
                {
                    const int base = (valueText.size() > 2 && valueText[0] == '0' &&
                                      (valueText[1] == 'x' || valueText[1] == 'X'))
                        ? 16
                        : 10;
                    value = static_cast<std::ptrdiff_t>(std::stoll(valueText, nullptr, base));
                }
                catch (...)
                {
                    if (error)
                        *error = "Invalid constexpr value in " + filePath.string();
                    return false;
                }

                std::ostringstream keyBuilder;
                for (size_t i = 0; i < namespaceStack.size(); ++i)
                {
                    if (i > 0)
                        keyBuilder << "::";
                    keyBuilder << namespaceStack[i];
                }
                if (!namespaceStack.empty())
                    keyBuilder << "::";
                keyBuilder << name;

                outMap[keyBuilder.str()] = value;
            }

            for (size_t i = 0; i < line.size(); ++i)
            {
                if (line.compare(i, 9, "namespace") == 0)
                {
                    const bool hasBoundaryBefore = (i == 0) ||
                        !(std::isalnum(static_cast<unsigned char>(line[i - 1])) || line[i - 1] == '_');
                    if (!hasBoundaryBefore)
                        continue;

                    size_t j = i + 9;
                    while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j])))
                        ++j;

                    const size_t nameStart = j;
                    while (j < line.size() &&
                        (std::isalnum(static_cast<unsigned char>(line[j])) || line[j] == '_'))
                    {
                        ++j;
                    }
                    const size_t nameEnd = j;

                    if (j > nameStart)
                    {
                        while (j < line.size() && std::isspace(static_cast<unsigned char>(line[j])))
                            ++j;

                        if (j < line.size() && line[j] == '{')
                        {
                            ++braceDepth;
                            namespaceStack.push_back(line.substr(nameStart, nameEnd - nameStart));
                            namespaceDepths.push_back(braceDepth);
                            i = j;
                            continue;
                        }
                    }
                }

                if (line[i] == '{')
                {
                    ++braceDepth;
                }
                else if (line[i] == '}')
                {
                    if (braceDepth > 0)
                        --braceDepth;
                    while (!namespaceDepths.empty() && braceDepth < namespaceDepths.back())
                    {
                        namespaceDepths.pop_back();
                        namespaceStack.pop_back();
                    }
                }
            }
        }

        return true;
    }

    bool AssignRequired(const std::unordered_map<std::string, std::ptrdiff_t>& map,
                        const char* key,
                        std::ptrdiff_t& outValue,
                        std::vector<std::string>& missingKeys)
    {
        const auto it = map.find(key);
        if (it == map.end())
        {
            missingKeys.emplace_back(key);
            return false;
        }

        outValue = it->second;
        return true;
    }

    bool ExtractRequiredValues(const std::unordered_map<std::string, std::ptrdiff_t>& parsedMap,
                               runtime_offsets::Values& outValues,
                               std::string* error)
    {
        std::vector<std::string> missingKeys;
        for (const auto& field : kRequiredRemoteFields)
            AssignRequired(parsedMap, field.remoteKey, outValues.*(field.member), missingKeys);

        if (!missingKeys.empty())
        {
            if (error)
                *error = "Missing keys in remote output: " + JoinKeys(missingKeys);
            return false;
        }

        return true;
    }

    void ExtractOptionalValues(const std::unordered_map<std::string, std::ptrdiff_t>& parsedMap,
                               runtime_offsets::Values& outValues)
    {
        for (const auto& field : kOptionalRemoteFields)
        {
            const auto it = parsedMap.find(field.remoteKey);
            if (it != parsedMap.end())
                outValues.*(field.member) = it->second;
        }
    }

    std::string BuildRawOutputUrl(const std::string& sourceFile)
    {
        return "https://raw.githubusercontent.com/a2x/cs2-dumper/main/output/" + sourceFile;
    }

    std::filesystem::path GetPackagedOutputDirectory()
    {
        const auto exeDir = GetExecutableDirectory();
        if (exeDir.empty())
            return {};
        return exeDir / "output";
    }

    std::filesystem::path GetProjectOutputDirectory()
    {
        const auto projectRoot = FindProjectRoot();
        if (projectRoot.empty())
            return {};
        return projectRoot / "output";
    }

    std::filesystem::path NormalizeDirectoryPath(const std::filesystem::path& path)
    {
        if (path.empty())
            return {};

        std::error_code ec;
        const auto absolutePath = std::filesystem::absolute(path, ec);
        if (ec)
            return path.lexically_normal();

        return absolutePath.lexically_normal();
    }

    std::filesystem::path ReadOverrideDirectoryPath(const std::filesystem::path& filePath)
    {
        try
        {
            if (filePath.empty() || !std::filesystem::exists(filePath))
                return {};

            std::ifstream file(filePath, std::ios::in | std::ios::binary);
            if (!file.is_open())
                return {};

            std::ostringstream buffer;
            buffer << file.rdbuf();
            const std::string raw = Trim(buffer.str());
            if (raw.empty())
                return {};

            return std::filesystem::path(raw);
        }
        catch (...)
        {
            return {};
        }
    }

    std::filesystem::path FindLegacyOffsetsSourceDirectory()
    {
        char envBuffer[MAX_PATH * 4] = {};
        const DWORD envLen = GetEnvironmentVariableA(
            "KEVQDMA_OFFSETS_LOCAL_DIR",
            envBuffer,
            static_cast<DWORD>(sizeof(envBuffer)));
        if (envLen > 0 && envLen < sizeof(envBuffer))
        {
            const std::filesystem::path envPath = Trim(std::string(envBuffer, envLen));
            if (!envPath.empty())
                return envPath;
        }

        const auto offsetsDir = GetOffsetsDirectory();
        const auto newOverrideFile = offsetsDir / "offsets_source.txt";
        auto newOverridePath = ReadOverrideDirectoryPath(newOverrideFile);
        if (!newOverridePath.empty())
            return newOverridePath;

        const auto legacyProfilesDir = GetLegacyProfilesDirectory();
        const auto projectRoot = FindProjectRoot();
        const std::filesystem::path legacyOverrideFiles[] = {
            legacyProfilesDir / "offsets_source.txt",
            projectRoot / "sdk" / "offsets_source.txt",
            projectRoot / "include" / "sdk" / "offsets_source.txt",
        };

        for (const auto& legacyFile : legacyOverrideFiles)
        {
            const auto legacyPath = ReadOverrideDirectoryPath(legacyFile);
            if (legacyPath.empty())
                continue;

            CopyFileToTargetIfPresent(legacyFile, newOverrideFile);
            return legacyPath;
        }

        return {};
    }

    std::unordered_set<std::string> CollectRemoteSourceFiles()
    {
        std::unordered_set<std::string> requiredFiles;
        for (const auto& field : kRequiredRemoteFields)
            requiredFiles.insert(field.sourceFile);
        for (const auto& field : kOptionalRemoteFields)
            requiredFiles.insert(field.sourceFile);
        return requiredFiles;
    }

    bool ParseOutputDirectory(const std::filesystem::path& directory,
                              std::unordered_map<std::string, std::ptrdiff_t>& parsedMap,
                              std::string* error)
    {
        const auto requiredFiles = CollectRemoteSourceFiles();
        for (const std::string& sourceFile : requiredFiles)
        {
            const std::filesystem::path sourcePath = directory / sourceFile;
            if (!std::filesystem::exists(sourcePath))
            {
                if (error)
                    *error = "Missing local dump file: " + sourcePath.string();
                return false;
            }

            if (!ParseHeaderConstants(sourcePath, parsedMap, error))
                return false;
        }

        return true;
    }

    bool HasRequiredOutputFiles(const std::filesystem::path& directory)
    {
        if (directory.empty() || !std::filesystem::exists(directory))
            return false;

        const auto requiredFiles = CollectRemoteSourceFiles();
        for (const std::string& sourceFile : requiredFiles)
        {
            if (!std::filesystem::exists(directory / sourceFile))
                return false;
        }

        return true;
    }

    std::string ReadFileText(const std::filesystem::path& filePath)
    {
        try
        {
            std::ifstream file(filePath, std::ios::in | std::ios::binary);
            if (!file.is_open())
                return {};

            std::ostringstream buffer;
            buffer << file.rdbuf();
            return buffer.str();
        }
        catch (...)
        {
            return {};
        }
    }

    std::string CanonicalizeTimestamp(const std::string& text)
    {
        static const std::regex kTimestampRegex(
            R"((\d{4})-(\d{2})-(\d{2})[ T](\d{2}):(\d{2}):(\d{2})(?:\.(\d+))?(?:\s*(?:UTC|Z))?)");

        std::smatch match;
        if (!std::regex_search(text, match, kTimestampRegex))
            return {};

        std::string fraction = match[7].matched ? match[7].str() : "";
        if (fraction.size() < 9)
            fraction.append(9 - fraction.size(), '0');
        else if (fraction.size() > 9)
            fraction.resize(9);

        std::ostringstream canonical;
        canonical << match[1].str()
                  << match[2].str()
                  << match[3].str()
                  << match[4].str()
                  << match[5].str()
                  << match[6].str()
                  << fraction;
        return canonical.str();
    }

    std::string ReadOutputDirectoryTimestamp(const std::filesystem::path& directory)
    {
        if (directory.empty() || !std::filesystem::exists(directory))
            return {};

        const std::string infoText = ReadFileText(directory / "info.json");
        if (!infoText.empty())
        {
            static const std::regex kInfoTimestampRegex(R"json("timestamp"\s*:\s*"([^"]+)")json");
            std::smatch match;
            if (std::regex_search(infoText, match, kInfoTimestampRegex))
            {
                const std::string canonical = CanonicalizeTimestamp(match[1].str());
                if (!canonical.empty())
                    return canonical;
            }

            const std::string canonical = CanonicalizeTimestamp(infoText);
            if (!canonical.empty())
                return canonical;
        }

        for (const char* headerFile : { "offsets.hpp", "client_dll.hpp" })
        {
            const std::string headerText = ReadFileText(directory / headerFile);
            const std::string canonical = CanonicalizeTimestamp(headerText);
            if (!canonical.empty())
                return canonical;
        }

        return {};
    }

    int ReadOutputDirectoryBuildNumber(const std::filesystem::path& directory)
    {
        if (directory.empty() || !std::filesystem::exists(directory))
            return 0;

        const std::string infoText = ReadFileText(directory / "info.json");
        if (infoText.empty())
            return 0;

        static const std::regex kBuildRegex(R"json("build_number"\s*:\s*(\d+))json");
        std::smatch match;
        if (!std::regex_search(infoText, match, kBuildRegex))
            return 0;

        try
        {
            return std::stoi(match[1].str());
        }
        catch (...)
        {
            return 0;
        }
    }

    void AddOutputDirectoryCandidate(std::vector<OutputDirectoryCandidate>& candidates,
                                     std::unordered_set<std::string>& seenDirectories,
                                     const std::filesystem::path& directory,
                                     const char* label)
    {
        const auto normalizedDirectory = NormalizeDirectoryPath(directory);
        if (normalizedDirectory.empty())
            return;

        const std::string key = normalizedDirectory.generic_string();
        if (!seenDirectories.insert(key).second)
            return;

        OutputDirectoryCandidate candidate;
        candidate.directory = normalizedDirectory;
        candidate.label = label;
        candidate.hasRequiredFiles = HasRequiredOutputFiles(normalizedDirectory);
        if (candidate.hasRequiredFiles)
        {
            candidate.timestamp = ReadOutputDirectoryTimestamp(normalizedDirectory);
            candidate.buildNumber = ReadOutputDirectoryBuildNumber(normalizedDirectory);
        }

        candidates.push_back(candidate);
    }

    std::vector<OutputDirectoryCandidate> CollectLocalOutputCandidates()
    {
        std::vector<OutputDirectoryCandidate> candidates;
        std::unordered_set<std::string> seenDirectories;

        AddOutputDirectoryCandidate(
            candidates,
            seenDirectories,
            FindLegacyOffsetsSourceDirectory(),
            "override output");
        AddOutputDirectoryCandidate(
            candidates,
            seenDirectories,
            GetProjectOutputDirectory(),
            "project output");
        AddOutputDirectoryCandidate(
            candidates,
            seenDirectories,
            GetPackagedOutputDirectory(),
            "packaged output");

        return candidates;
    }

    const OutputDirectoryCandidate* FindPreferredLocalOutputCandidate(
        const std::vector<OutputDirectoryCandidate>& candidates)
    {
        const OutputDirectoryCandidate* bestCandidate = nullptr;
        for (const auto& candidate : candidates)
        {
            if (!candidate.hasRequiredFiles)
                continue;

            if (bestCandidate == nullptr)
            {
                bestCandidate = &candidate;
                continue;
            }

            if (!candidate.timestamp.empty() &&
                (bestCandidate->timestamp.empty() || candidate.timestamp > bestCandidate->timestamp))
            {
                bestCandidate = &candidate;
            }
        }

        return bestCandidate;
    }

    std::string DescribeOutputDirectoryCandidate(const OutputDirectoryCandidate& candidate)
    {
        const std::string folderName =
            candidate.directory.filename().empty()
            ? candidate.directory.generic_string()
            : candidate.directory.filename().generic_string();
        return folderName.empty()
            ? candidate.label
            : (candidate.label + " (" + folderName + ")");
    }

    bool DownloadFileWithRetry(const std::string& url,
                               const std::filesystem::path& destination,
                               NetworkRetryBudget& retryBudget,
                               std::string* error)
    {
        return DownloadFile(url.c_str(), destination, retryBudget, error);
    }

    bool DownloadOutputDirectoryFiles(const std::filesystem::path& directory,
                                       bool includeInfoJson,
                                       NetworkRetryBudget& retryBudget,
                                       std::string* error)
    {
        auto sourceFiles = CollectRemoteSourceFiles();
        if (includeInfoJson)
            sourceFiles.insert("info.json");

        for (const std::string& sourceFile : sourceFiles)
        {
            if (!DownloadFileWithRetry(
                    BuildRawOutputUrl(sourceFile),
                    directory / sourceFile,
                    retryBudget,
                    error))
                return false;
        }

        return true;
    }

    bool DownloadOutputInfoFile(const std::filesystem::path& directory,
                                NetworkRetryBudget& retryBudget,
                                std::string* error)
    {
        try
        {
            std::filesystem::create_directories(directory);
        }
        catch (...)
        {
            if (error)
                *error = "Cannot create temporary output directory: " + directory.string();
            return false;
        }

        return DownloadFileWithRetry(
            BuildRawOutputUrl("info.json"),
            directory / "info.json",
            retryBudget,
            error);
    }

    const OffsetField* FindOffsetField(
        std::ptrdiff_t runtime_offsets::Values::*member)
    {
        for (const OffsetField& field : kOffsetFields) {
            if (field.member == member)
                return &field;
        }
        return nullptr;
    }

    const RemoteField* FindRemoteSchemaField(
        std::ptrdiff_t runtime_offsets::Values::*member)
    {
        for (const RemoteField& field : kRequiredRemoteFields) {
            if (field.member == member &&
                std::string_view(field.remoteKey).starts_with(
                    "cs2_dumper::schemas::client_dll::")) {
                return &field;
            }
        }
        for (const RemoteField& field : kOptionalRemoteFields) {
            if (field.member == member &&
                std::string_view(field.remoteKey).starts_with(
                    "cs2_dumper::schemas::client_dll::")) {
                return &field;
            }
        }
        return nullptr;
    }

    std::vector<runtime_offsets::resolver::SchemaRequest>
    BuildRuntimeSchemaRequests()
    {
        constexpr std::string_view kPrefix =
            "cs2_dumper::schemas::client_dll::";
        std::vector<runtime_offsets::resolver::SchemaRequest> requests;
        requests.reserve(kOffsetFields.size());
        for (const OffsetField& field : kOffsetFields) {
            if (std::string_view(field.section) != "schemas")
                continue;
            const RemoteField* remote = FindRemoteSchemaField(field.member);
            if (!remote)
                continue;
            const std::string_view remoteKey(remote->remoteKey);
            const std::string_view qualified = remoteKey.substr(kPrefix.size());
            const std::size_t separator = qualified.find("::");
            if (separator == std::string_view::npos ||
                separator == 0 || separator + 2 >= qualified.size()) {
                continue;
            }
            requests.push_back({
                std::string(qualified.substr(0, separator)),
                std::string(qualified.substr(separator + 2)),
                field.key
            });
        }
        return requests;
    }

    bool ResolverSuppliedMember(
        const runtime_offsets::resolver::Result& result,
        std::ptrdiff_t runtime_offsets::Values::*member)
    {
        const OffsetField* field = FindOffsetField(member);
        if (!field)
            return false;
        if (std::string_view(field->section) == "offsets")
            return result.offsets.contains(field->key);
        return result.schemas.contains(field->key);
    }

    bool ShouldClearMissingResolverFallback(
        std::ptrdiff_t runtime_offsets::Values::*member)
    {
        // These fields disappeared from current client schemas. Retaining an
        // older local value turns valid GameRules reads into false failures.
        return member == &runtime_offsets::Values::C_CSGameRules_m_vMinimapMins ||
               member == &runtime_offsets::Values::C_CSGameRules_m_vMinimapMaxs;
    }

    std::size_t ApplyResolverResult(
        const runtime_offsets::resolver::Result& result,
        runtime_offsets::Values& values)
    {
        std::size_t retainedFallbackFields = 0;
        for (const OffsetField& field : kOffsetFields) {
            const bool isOffset = std::string_view(field.section) == "offsets";
            const auto& source = isOffset ? result.offsets : result.schemas;
            const auto it = source.find(field.key);
            if (it != source.end() && it->second > 0) {
                values.*(field.member) = it->second;
            } else if (ShouldClearMissingResolverFallback(field.member)) {
                values.*(field.member) = 0;
            } else if (values.*(field.member) > 0) {
                ++retainedFallbackFields;
            }
        }
        return retainedFallbackFields;
    }

    bool SanityCheckValues(
        const runtime_offsets::Values& ofs,
        std::string* message,
        LiveValidationStats* validationStats = nullptr,
        bool strictModuleValidation = false)
    {
        LiveValidationStats stats = {};
        auto require = [&](bool condition, std::string_view failure) {
            ++stats.attempted;
            if (condition) {
                ++stats.passed;
                return true;
            }
            if (message)
                *message = std::string(failure);
            if (validationStats)
                *validationStats = stats;
            return false;
        };
        auto finish = [&](bool result) {
            if (validationStats)
                *validationStats = stats;
            return result;
        };

        const uintptr_t clientBase = g::clientBase.load(std::memory_order_relaxed);
        const uintptr_t engineBase = g::engine2Base.load(std::memory_order_relaxed);
        if (!require(clientBase != 0, "client.dll base is not loaded."))
            return false;

        auto requireRva = [&](std::ptrdiff_t rva, std::size_t imageSize, const char* name) {
            const bool valid = rva > 0 &&
                static_cast<std::uint64_t>(rva) < static_cast<std::uint64_t>(imageSize);
            if (valid) {
                ++stats.attempted;
                ++stats.passed;
                return true;
            }
            ++stats.attempted;
            if (message)
                *message = std::string(name) + " is outside its module image.";
            if (validationStats)
                *validationStats = stats;
            return false;
        };

        if (strictModuleValidation) {
            std::size_t clientSize = 0;
            std::size_t engineSize = 0;
            std::size_t matchmakingSize = 0;
            std::uint32_t ignoredTimestamp = 0;
            const uintptr_t matchmakingBase = mem.GetModuleBase("matchmaking.dll");
            const bool clientImageValid =
                ReadModuleImageInfo(clientBase, clientSize, ignoredTimestamp);
            const bool engineImageValid =
                ReadModuleImageInfo(engineBase, engineSize, ignoredTimestamp);
            const bool matchmakingImageValid =
                ReadModuleImageInfo(matchmakingBase, matchmakingSize, ignoredTimestamp);
            if (!require(engineBase != 0, "engine2.dll base is not loaded."))
                return false;
            if (!require(clientImageValid, "client.dll image size is unavailable."))
                return false;
            if (!require(engineImageValid, "engine2.dll image size is unavailable."))
                return false;

            const std::pair<std::ptrdiff_t, const char*> clientRvas[] = {
                {ofs.dwEntityList, "dwEntityList"},
                {ofs.dwGameRules, "dwGameRules"},
                {ofs.dwGlobalVars, "dwGlobalVars"},
                {ofs.dwLocalPlayerController, "dwLocalPlayerController"},
                {ofs.dwLocalPlayerPawn, "dwLocalPlayerPawn"},
                {ofs.dwPlantedC4, "dwPlantedC4"},
                {ofs.dwViewMatrix, "dwViewMatrix"},
                {ofs.dwViewAngles, "dwViewAngles"},
                {ofs.dwWeaponC4, "dwWeaponC4"},
                {ofs.dwSensitivity, "dwSensitivity"},
            };
            for (const auto& [rva, name] : clientRvas) {
                if (!requireRva(rva, clientSize, name))
                    return false;
            }
            if (!requireRva(ofs.dwNetworkGameClient, engineSize, "dwNetworkGameClient"))
                return false;
            if (!require(matchmakingBase != 0 && matchmakingImageValid,
                         "matchmaking.dll module is unavailable.")) {
                return false;
            }
            if (!requireRva(ofs.dwGameTypes, matchmakingSize, "dwGameTypes"))
                return false;
        }

        uintptr_t entityListPtr = 0;
        if (!mem.Read(
                clientBase + ofs.dwEntityList,
                &entityListPtr,
                sizeof(entityListPtr)) ||
            !entityListPtr) {
            require(false, "Failed to read dwEntityList or got null pointer.");
            return false;
        }
        ++stats.attempted;
        if (!app::memory_address::IsLikelyGamePointer(entityListPtr)) {
            if (message)
                *message = app::localization::Format(
                    "dwEntityList resolved pointer {} is invalid.",
                    ToAddressHex(entityListPtr));
            if (validationStats)
                *validationStats = stats;
            return false;
        }
        ++stats.passed;

        uintptr_t listEntryPtr = 0;
        if (!mem.Read(
                entityListPtr + 0x10,
                &listEntryPtr,
                sizeof(listEntryPtr))) {
            require(false, "Failed to read entityList + 0x10.");
            return false;
        }
        ++stats.attempted;
        if (listEntryPtr != 0 &&
            !app::memory_address::IsLikelyGamePointer(listEntryPtr)) {
            if (message)
                *message = app::localization::Format(
                    "listEntry resolved pointer {} is invalid.",
                    ToAddressHex(listEntryPtr));
            if (validationStats)
                *validationStats = stats;
            return false;
        }
        ++stats.passed;

        if (strictModuleValidation) {
            uintptr_t networkGameClient = 0;
            if (!mem.Read(
                    engineBase + ofs.dwNetworkGameClient,
                    &networkGameClient,
                    sizeof(networkGameClient))) {
                require(false, "Failed to read dwNetworkGameClient.");
                return false;
            }
            if (!require(
                    networkGameClient == 0 ||
                        app::memory_address::IsLikelyGamePointer(networkGameClient),
                    "dwNetworkGameClient resolved an invalid pointer.")) {
                return false;
            }
        }

        if (ofs.dwLocalPlayerPawn > 0) {
            uintptr_t localPawnPtr = 0;
            if (mem.Read(
                    clientBase + ofs.dwLocalPlayerPawn,
                    &localPawnPtr,
                    sizeof(localPawnPtr)) &&
                localPawnPtr != 0) {
                ++stats.attempted;
                if (!app::memory_address::IsLikelyGamePointer(localPawnPtr)) {
                    if (message)
                        *message = app::localization::Format(
                            "dwLocalPlayerPawn resolved pointer {} is invalid.",
                            ToAddressHex(localPawnPtr));
                    if (validationStats)
                        *validationStats = stats;
                    return false;
                }
                ++stats.passed;

                int32_t health = 0;
                if (ofs.C_BaseEntity_m_iHealth > 0 &&
                    mem.Read(
                        localPawnPtr + ofs.C_BaseEntity_m_iHealth,
                        &health,
                        sizeof(health)) &&
                    !require(health >= 0 && health <= 1000,
                             "Local player health is outside the plausible range.")) {
                    return false;
                }
                uint8_t team = 0; // m_iTeamNum is one byte; do not read adjacent spawn flags.
                if (ofs.C_BaseEntity_m_iTeamNum > 0 &&
                    mem.Read(
                        localPawnPtr + ofs.C_BaseEntity_m_iTeamNum,
                        &team,
                        sizeof(team)) &&
                    !require(team <= 3,
                             "Local player team is outside the plausible range.")) {
                    return false;
                }
                uintptr_t sceneNode = 0;
                if (ofs.C_BaseEntity_m_pGameSceneNode > 0 &&
                    mem.Read(
                        localPawnPtr + ofs.C_BaseEntity_m_pGameSceneNode,
                        &sceneNode,
                        sizeof(sceneNode)) &&
                    !require(
                        sceneNode == 0 ||
                            app::memory_address::IsLikelyGamePointer(sceneNode),
                        "Local player scene node pointer is invalid.")) {
                    return false;
                }
                if (ofs.C_BasePlayerPawn_m_hController > 0) {
                    uint32_t controllerHandle = 0;
                    if (mem.Read(
                            localPawnPtr + ofs.C_BasePlayerPawn_m_hController,
                            &controllerHandle,
                            sizeof(controllerHandle)) &&
                        controllerHandle != 0) {
                        const uint32_t index = controllerHandle & 0x3FFF;
                        ++stats.attempted;
                        if (index == 0 || index > 16384) {
                            if (message)
                                *message = app::localization::Format(
                                    "Local player pawn controller handle {} has invalid entity index: {}.",
                                    ToHex(controllerHandle),
                                    index);
                            if (validationStats)
                                *validationStats = stats;
                            return false;
                        }
                        ++stats.passed;
                    }
                }
            }
        }

        if (message)
            *message = app::localization::GetCopy("Sanity check passed.");
        return finish(true);
    }
}

runtime_offsets::Values runtime_offsets::Get()
{
    return GetRuntimeValuesSnapshot();
}

bool runtime_offsets::PrepareLocalFallback(std::string* message)
{
    try {
        const std::filesystem::path jsonPath = FindOffsetsJsonPath(true);
        if (jsonPath.empty()) {
            if (message)
                *message = "Unable to resolve offsets.json path.";
            return false;
        }

        json existingRoot;
        if (TryParseOffsetsJsonFile(jsonPath, existingRoot)) {
            Values existing = {};
            LoadValuesFromJson(existingRoot, existing, nullptr, nullptr);
            if (ValidateLoadedValues(existing, true).empty()) {
                if (message)
                    *message = "Existing local offsets are available.";
                return true;
            }
        }

        const std::vector<OutputDirectoryCandidate> candidates =
            CollectLocalOutputCandidates();
        const OutputDirectoryCandidate* preferred =
            FindPreferredLocalOutputCandidate(candidates);
        if (!preferred) {
            if (message)
                *message = "No valid local offset dump is available.";
            return false;
        }

        std::unordered_map<std::string, std::ptrdiff_t> parsed;
        std::string parseError;
        if (!ParseOutputDirectory(preferred->directory, parsed, &parseError)) {
            if (message)
                *message = parseError;
            return false;
        }

        Values values = {};
        if (!ExtractRequiredValues(parsed, values, &parseError)) {
            if (message)
                *message = parseError;
            return false;
        }
        ExtractOptionalValues(parsed, values);

        OffsetState state = ReadOffsetState(jsonPath, GetOffsetsStatePath());
        state.selectedSource = DescribeOutputDirectoryCandidate(*preferred);
        state.selectedSourceTimestamp = preferred->timestamp;
        state.selectedSourceBuildNumber = preferred->buildNumber;
        if (!WriteOffsetsJson(jsonPath, values, &state)) {
            if (message)
                *message = "Unable to persist the local offset fallback.";
            return false;
        }

        if (message)
            *message = "Local offset fallback prepared.";
        return true;
    } catch (const std::exception& e) {
        if (message)
            *message = std::string("Local offset fallback failed: ") + e.what();
        return false;
    } catch (...) {
        if (message)
            *message = "Local offset fallback failed.";
        return false;
    }
}

bool runtime_offsets::ResolveFromAttachedProcess(
    RuntimeResolveReport* report,
    std::string* message,
    bool force)
{
    std::scoped_lock resolveLock(g_runtimeResolveMutex);
    const DWORD pid = mem.GetAttachedPid();
    const uintptr_t clientBase = g::clientBase.load(std::memory_order_relaxed);
    const uintptr_t engineBase = g::engine2Base.load(std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now();
    const std::uint64_t moduleFingerprint =
        pid && clientBase && engineBase ? BuildModuleFingerprint() : 0;

    auto publishReport = [&](const RuntimeResolveReport& value) {
        if (report)
            *report = value;
    };
    auto cacheFailure = [&](RuntimeResolveReport value, const std::string& reason) {
        value.pid = pid;
        value.source = "runtime memory";
        value.validationPassed = false;
        value.detail = reason;
        value.moduleFingerprint = moduleFingerprint;
        g_runtimeResolveCache.pid = pid;
        g_runtimeResolveCache.clientBase = clientBase;
        g_runtimeResolveCache.engineBase = engineBase;
        g_runtimeResolveCache.moduleFingerprint = moduleFingerprint;
        g_runtimeResolveCache.success = false;
        g_runtimeResolveCache.attemptedAt = now;
        g_runtimeResolveCache.report = value;
        publishReport(value);
        if (message)
            *message = reason;
        return false;
    };

    RuntimeResolveReport currentReport = {};
    currentReport.pid = pid;
    currentReport.source = "runtime memory";
    currentReport.moduleFingerprint = moduleFingerprint;
    if (!pid || !clientBase || !engineBase)
        return cacheFailure(currentReport, "Runtime resolver requires an attached CS2 process and loaded modules.");

    const bool sameTarget =
        g_runtimeResolveCache.pid == pid &&
        g_runtimeResolveCache.clientBase == clientBase &&
        g_runtimeResolveCache.engineBase == engineBase &&
        g_runtimeResolveCache.moduleFingerprint == moduleFingerprint;
    if (!force && sameTarget && g_runtimeResolveCache.success) {
        RuntimeResolveReport cached = g_runtimeResolveCache.report;
        cached.cached = true;
        publishReport(cached);
        if (message)
            *message = "Runtime offsets are already validated for this process.";
        return true;
    }
    if (!force && sameTarget &&
        g_runtimeResolveCache.attemptedAt != std::chrono::steady_clock::time_point{} &&
        now - g_runtimeResolveCache.attemptedAt < kRuntimeResolveFailureCooldown) {
        RuntimeResolveReport cached = g_runtimeResolveCache.report;
        cached.cached = true;
        publishReport(cached);
        if (message)
            *message = "Runtime offset retry is cooling down; local offsets remain active.";
        return false;
    }

    resolver::Result resolved;
    std::string resolveError;
    const bool resolverOk = resolver::ResolveAttachedProcess(
        BuildRuntimeSchemaRequests(),
        resolved,
        &resolveError);
    currentReport.resolvedOffsets = resolved.offsets.size();
    currentReport.resolvedSchemas = resolved.schemas.size();
    currentReport.bytesRead = resolved.bytesRead;
    currentReport.classesVisited = resolved.classesVisited;
    currentReport.expectedOffsets = resolved.expectedOffsets;
    currentReport.expectedSchemas = resolved.expectedSchemas;
    currentReport.modulesRead = resolved.modulesRead;
    currentReport.executableSectionsRead = resolved.executableSectionsRead;
    currentReport.unreadableCodePages = resolved.unreadableCodePages;
    currentReport.duplicatePatterns = resolved.duplicatePatterns;
    currentReport.elapsedMs = resolved.elapsedMs;
    if (!resolverOk)
        return cacheFailure(currentReport, resolveError.empty()
            ? "Runtime offset resolution was incomplete."
            : resolveError);

    Values candidate = GetRuntimeValuesSnapshot();
    currentReport.retainedFallbackFields =
        ApplyResolverResult(resolved, candidate);

    std::vector<std::string> missingRuntimeFields;
    for (const RemoteField& field : kRequiredRemoteFields) {
        if (!ResolverSuppliedMember(resolved, field.member))
            missingRuntimeFields.emplace_back(field.remoteKey);
    }
    currentReport.unresolvedRequired = missingRuntimeFields.size();
    if (!missingRuntimeFields.empty()) {
        return cacheFailure(
            currentReport,
            "Runtime resolver missed required fields: " +
                JoinKeys(missingRuntimeFields));
    }

    const std::vector<std::string> invalidRequired =
        ValidateLoadedValues(candidate, true);
    if (!invalidRequired.empty()) {
        return cacheFailure(
            currentReport,
            "Runtime candidate contains invalid required fields: " +
                JoinKeys(invalidRequired));
    }

    std::string sanityMessage;
    LiveValidationStats validationStats = {};
    if (!SanityCheckValues(candidate, &sanityMessage, &validationStats, true)) {
        currentReport.validationChecksPassed = validationStats.passed;
        currentReport.validationChecksAttempted = validationStats.attempted;
        return cacheFailure(
            currentReport,
            "Runtime candidate failed live validation: " + sanityMessage);
    }

    SetRuntimeValues(candidate);
    currentReport.applied = true;
    currentReport.validationPassed = true;
    currentReport.validationChecksPassed = validationStats.passed;
    currentReport.validationChecksAttempted = validationStats.attempted;
    currentReport.detail = "Live validation passed.";
    const std::filesystem::path jsonPath = FindOffsetsJsonPath(true);
    if (!jsonPath.empty()) {
        OffsetState state = ReadOffsetState(jsonPath, GetOffsetsStatePath());
        state.selectedSource = "runtime memory";
        state.selectedSourceTimestamp.clear();
        state.selectedSourceBuildNumber = 0;
        currentReport.persisted = WriteOffsetsJson(jsonPath, candidate, &state);
    }

    g_runtimeResolveCache.pid = pid;
    g_runtimeResolveCache.clientBase = clientBase;
    g_runtimeResolveCache.engineBase = engineBase;
    g_runtimeResolveCache.moduleFingerprint = moduleFingerprint;
    g_runtimeResolveCache.success = true;
    g_runtimeResolveCache.attemptedAt = now;
    g_runtimeResolveCache.report = currentReport;
    publishReport(currentReport);
    if (message) {
        std::ostringstream text;
        text << "Resolved " << currentReport.resolvedOffsets
             << " global and " << currentReport.resolvedSchemas
             << " schema offsets from the attached process in "
             << static_cast<int>(currentReport.elapsedMs + 0.5) << " ms.";
        if (!currentReport.persisted)
            text << " The validated values are active but could not be persisted.";
        *message = text.str();
    }
    return true;
}

bool runtime_offsets::AutoUpdateFromGitHub(
    std::string* message,
    runtime_offsets::AutoUpdateReport* report,
    bool forceRemote)
{
    #include "runtime_offsets_parts/runtime_offsets_autoupdate_body.inl"
}

bool runtime_offsets::Load(std::string* message)
{
    #include "runtime_offsets_parts/runtime_offsets_load_body.inl"
}

runtime_offsets::StateView runtime_offsets::GetStateView()
{
    const auto jsonPath = FindOffsetsJsonPath(false);
    const OffsetState state = ReadOffsetState(jsonPath, GetOffsetsStatePath());

    StateView view = {};
    view.offsetsPatch = state.offsetsPatch;
    view.lastSeenPatch = state.lastSeenPatch;
    view.selectedSource = state.selectedSource;
    view.selectedSourceTimestamp = state.selectedSourceTimestamp;
    view.selectedSourceBuildNumber = state.selectedSourceBuildNumber;
    return view;
}

bool runtime_offsets::SanityCheckOffsets(std::string* message)
{
    return SanityCheckValues(GetRuntimeValuesSnapshot(), message);
}
