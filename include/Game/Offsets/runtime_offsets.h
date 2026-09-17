#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace runtime_offsets
{
    struct PatchInfo
    {
        std::string etag;
        std::string lastFileSha;
        int clientVersion = 0;
        int sourceRevision = 0;
        std::string patchVersion;
    };

    struct AutoUpdateReport
    {
        PatchInfo currentPatch = {};
        PatchInfo previousOffsetsPatch = {};
        PatchInfo previousLastSeenPatch = {};
        bool patchChanged = false;
        bool offsetsUpdated = false;
        bool offsetsCompatibleWithCurrentPatch = false;
        bool offsetSourcePredatesCurrentPatch = false;
        bool networkCheckSkipped = false;
        std::string offsetSource;
        std::string offsetSourceTimestamp;
        std::string networkSkipReason;
        int offsetSourceBuildNumber = 0;
        std::string currentPatchVersionDate;
        std::string currentPatchVersionTime;
    };

    struct StateView
    {
        PatchInfo offsetsPatch = {};
        PatchInfo lastSeenPatch = {};
        std::string selectedSource;
        std::string selectedSourceTimestamp;
        int selectedSourceBuildNumber = 0;
    };

    struct RuntimeResolveReport
    {
        bool applied = false;
        bool cached = false;
        bool persisted = false;
        std::size_t resolvedOffsets = 0;
        std::size_t resolvedSchemas = 0;
        std::size_t retainedFallbackFields = 0;
        std::size_t bytesRead = 0;
        std::size_t classesVisited = 0;
        std::size_t expectedOffsets = 0;
        std::size_t expectedSchemas = 0;
        std::size_t modulesRead = 0;
        std::size_t executableSectionsRead = 0;
        std::size_t unreadableCodePages = 0;
        std::size_t duplicatePatterns = 0;
        std::size_t unresolvedRequired = 0;
        std::size_t validationChecksPassed = 0;
        std::size_t validationChecksAttempted = 0;
        double elapsedMs = 0.0;
        std::uint64_t moduleFingerprint = 0;
        std::uint32_t pid = 0;
        bool validationPassed = false;
        std::string source;
        std::string detail;
    };

    struct Values
    {
        
        std::ptrdiff_t dwEntityList = 0;
        std::ptrdiff_t dwGameEntitySystem_highestEntityIndex = 0;
        std::ptrdiff_t dwGameRules = 0;
        std::ptrdiff_t dwGlobalVars = 0;
        std::ptrdiff_t dwLocalPlayerController = 0;
        std::ptrdiff_t dwLocalPlayerPawn = 0;
        std::ptrdiff_t dwPlantedC4 = 0;
        std::ptrdiff_t dwViewMatrix = 0;
        std::ptrdiff_t dwViewAngles = 0;
        std::ptrdiff_t dwWeaponC4 = 0;
        std::ptrdiff_t dwSensitivity = 0;
        std::ptrdiff_t dwSensitivity_sensitivity = 0;
        std::ptrdiff_t dwNetworkGameClient = 0;
        std::ptrdiff_t dwNetworkGameClient_signOnState = 0;
        std::ptrdiff_t dwNetworkGameClient_localPlayer = 0;
        std::ptrdiff_t dwNetworkGameClient_maxClients = 0;
        std::ptrdiff_t dwNetworkGameClient_isBackgroundMap = 0;
        std::ptrdiff_t dwGameTypes = 0;
        std::ptrdiff_t dwGameTypes_mapName = 0;

        
        std::ptrdiff_t CBasePlayerController_m_iPing = 0;
        std::ptrdiff_t CCSPlayerController_m_pInGameMoneyServices = 0;
        std::ptrdiff_t CCSPlayerController_InGameMoneyServices_m_iAccount = 0;
        std::ptrdiff_t C_BaseEntity_m_iTeamNum = 0;
        std::ptrdiff_t C_BasePlayerPawn_m_vOldOrigin = 0;
        std::ptrdiff_t C_BaseModelEntity_m_vecViewOffset = 0;
        std::ptrdiff_t C_BasePlayerPawn_m_flFOVSensitivityAdjust = 0;
        std::ptrdiff_t C_BasePlayerPawn_m_pWeaponServices = 0;
        std::ptrdiff_t C_BasePlayerPawn_m_pObserverServices = 0;
        std::ptrdiff_t C_BasePlayerPawn_m_hController = 0;
        std::ptrdiff_t CBasePlayerController_m_hPawn = 0;
        std::ptrdiff_t CCSPlayerController_m_hObserverPawn = 0;
        std::ptrdiff_t CCSPlayerController_m_hPlayerPawn = 0;
        std::ptrdiff_t CBasePlayerController_m_iszPlayerName = 0;
        std::ptrdiff_t CPlayer_WeaponServices_m_hActiveWeapon = 0;
        std::ptrdiff_t CPlayer_WeaponServices_m_hMyWeapons = 0;
        std::ptrdiff_t C_BaseEntity_m_iHealth = 0;
        std::ptrdiff_t C_CSPlayerPawn_m_ArmorValue = 0;
        std::ptrdiff_t C_BaseEntity_m_lifeState = 0;
        std::ptrdiff_t C_BaseEntity_m_pGameSceneNode = 0;
        std::ptrdiff_t C_BaseEntity_m_hOwnerEntity = 0;
        std::ptrdiff_t C_BaseEntity_m_nSubclassID = 0;
        std::ptrdiff_t C_BaseEntity_m_vecVelocity = 0;
        std::ptrdiff_t C_BaseEntity_m_fFlags = 0;
        std::ptrdiff_t CEntityInstance_m_pEntity = 0;
        std::ptrdiff_t CEntityIdentity_m_designerName = 0;
        std::ptrdiff_t C_BasePlayerWeapon_m_iClip1 = 0;
        std::ptrdiff_t C_CSWeaponBase_m_bCanBePickedUp = 0;
        std::ptrdiff_t C_CSWeaponBase_m_nDropTick = 0;
        std::ptrdiff_t C_CSWeaponBase_m_fAccuracyPenalty = 0;
        std::ptrdiff_t C_CSWeaponBase_m_flTurningInaccuracy = 0;
        std::ptrdiff_t C_CSWeaponBase_m_flRecoilIndex = 0;
        std::ptrdiff_t C_CSWeaponBase_m_bInReload = 0;
        std::ptrdiff_t C_CSWeaponBase_m_weaponMode = 0;
        std::ptrdiff_t C_CSWeaponBase_m_fLastShotTime = 0;
        std::ptrdiff_t C_CSPlayerPawn_m_bGunGameImmunity = 0;
        std::ptrdiff_t C_CSPlayerPawn_m_bIsScoped = 0;
        std::ptrdiff_t C_CSPlayerPawn_m_bIsWalking = 0;
        std::ptrdiff_t C_CSPlayerPawn_m_bIsDefusing = 0;
        std::ptrdiff_t C_CSPlayerPawn_m_angEyeAngles = 0;
        std::ptrdiff_t C_CSPlayerPawn_m_pAimPunchServices = 0;
        std::ptrdiff_t C_CSPlayerPawn_m_iShotsFired = 0;
        std::ptrdiff_t CCSPlayer_AimPunchServices_m_predictableBaseAngle = 0;
        std::ptrdiff_t C_CSPlayerPawnBase_m_iIDEntIndex = 0;
        std::ptrdiff_t C_CSPlayerPawnBase_m_flFlashBangTime = 0;
        std::ptrdiff_t C_CSPlayerPawnBase_m_flFlashDuration = 0;
        std::ptrdiff_t C_CSPlayerPawnBase_m_pItemServices = 0;
        std::ptrdiff_t CCSPlayer_ItemServices_m_bHasDefuser = 0;
        std::ptrdiff_t CCSPlayer_ItemServices_m_bHasHelmet = 0;
        std::ptrdiff_t C_CSPlayerPawn_m_entitySpottedState = 0;
        std::ptrdiff_t C_EconEntity_m_AttributeManager = 0;
        std::ptrdiff_t C_AttributeContainer_m_Item = 0;
        std::ptrdiff_t C_EconItemView_m_iItemDefinitionIndex = 0;
        std::ptrdiff_t C_CSGameRules_m_vMinimapMins = 0;
        std::ptrdiff_t C_CSGameRules_m_vMinimapMaxs = 0;
        std::ptrdiff_t C_CSGameRules_m_bBombPlanted = 0;
        std::ptrdiff_t C_CSGameRules_m_bBombDropped = 0;
        std::ptrdiff_t C_CSGameRules_m_nRoundStartCount = 0;
        std::ptrdiff_t C_CSGameRules_m_totalRoundsPlayed = 0;
        std::ptrdiff_t CPlayer_ObserverServices_m_iObserverMode = 0;
        std::ptrdiff_t CPlayer_ObserverServices_m_hObserverTarget = 0;
        std::ptrdiff_t CGameSceneNode_m_bDormant = 0;
        std::ptrdiff_t CGameSceneNode_m_vecAbsOrigin = 0;
        std::ptrdiff_t EntitySpottedState_t_m_bSpottedByMask = 0;
        std::ptrdiff_t C_PlantedC4_m_bBombTicking = 0;
        std::ptrdiff_t C_PlantedC4_m_flC4Blow = 0;
        std::ptrdiff_t C_PlantedC4_m_bHasExploded = 0;
        std::ptrdiff_t C_PlantedC4_m_flTimerLength = 0;
        std::ptrdiff_t C_PlantedC4_m_bBeingDefused = 0;
        std::ptrdiff_t C_PlantedC4_m_bC4Activated = 0;
        std::ptrdiff_t C_PlantedC4_m_flDefuseLength = 0;
        std::ptrdiff_t C_PlantedC4_m_flDefuseCountDown = 0;
        std::ptrdiff_t C_PlantedC4_m_bBombDefused = 0;
        std::ptrdiff_t C_PlantedC4_m_hBombDefuser = 0;
        std::ptrdiff_t C_Inferno_m_nFireEffectTickBegin = 0;
        std::ptrdiff_t C_Inferno_m_nFireLifetime = 0;
        std::ptrdiff_t C_Inferno_m_fireCount = 0;
        std::ptrdiff_t C_Inferno_m_firePositions = 0;
        std::ptrdiff_t C_Inferno_m_bInPostEffectTime = 0;
        std::ptrdiff_t C_SmokeGrenadeProjectile_m_nSmokeEffectTickBegin = 0;
        std::ptrdiff_t C_SmokeGrenadeProjectile_m_bDidSmokeEffect = 0;
        std::ptrdiff_t C_SmokeGrenadeProjectile_m_bSmokeVolumeDataReceived = 0;
        std::ptrdiff_t C_SmokeGrenadeProjectile_m_bSmokeEffectSpawned = 0;
        std::ptrdiff_t C_DecoyProjectile_m_nDecoyShotTick = 0;
        std::ptrdiff_t C_DecoyProjectile_m_nClientLastKnownDecoyShotTick = 0;
        std::ptrdiff_t C_BaseCSGrenadeProjectile_m_nExplodeEffectTickBegin = 0;
        std::ptrdiff_t CSkeletonInstance_m_modelState = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_WeaponType = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_nNumBullets = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_flSpread = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_flMaxSpeed = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_flInaccuracyMove = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_flInaccuracyJumpInitial = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_flInaccuracyJumpApex = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_flInaccuracyStand = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_flInaccuracyCrouch = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_flCycleTime = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_bIsFullAuto = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_nDamage = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_flPenetration = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_flRangeModifier = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_flRange = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_flArmorRatio = 0;
        std::ptrdiff_t CCSWeaponBaseVData_m_flHeadshotMultiplier = 0;
    };

    Values Get();
    StateView GetStateView();
    bool PrepareLocalFallback(std::string* message = nullptr);
    bool ResolveFromAttachedProcess(
        RuntimeResolveReport* report = nullptr,
        std::string* message = nullptr,
        bool force = false);
    bool AutoUpdateFromGitHub(
        std::string* message = nullptr,
        AutoUpdateReport* report = nullptr,
        bool forceRemote = false);
    bool Load(std::string* message = nullptr);
    bool SanityCheckOffsets(std::string* message = nullptr);
}
