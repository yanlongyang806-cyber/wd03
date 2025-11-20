#ifndef __REGIONRULES_H__
#define __REGIONRULES_H__
GCC_SYSTEM
#include "ReferenceSystem.h"
#include "Quat.h"
#include "Message.h"
#include "WorldLibEnums.h"
#include "conversions.h"

typedef struct AllegianceDef AllegianceDef;
typedef struct ControlSchemeRegionInfo ControlSchemeRegionInfo;
typedef struct DisplayMessage DisplayMessage;
typedef struct Entity Entity;
typedef struct Expression Expression;
typedef struct PetDef PetDef;
typedef struct TempPuppetChoice TempPuppetChoice;
typedef struct PuppetEntity PuppetEntity;
typedef struct WorldRegion WorldRegion;
typedef struct DoorTransitionSequenceRef DoorTransitionSequenceRef;
typedef struct DoorTransitionSequenceDef DoorTransitionSequenceDef;
typedef struct GenesisZoneMapData GenesisZoneMapData;
typedef struct ZoneMapInfo ZoneMapInfo;
typedef struct WorldRegion WorldRegion;
typedef enum WorldRegionType WorldRegionType;
typedef enum VehicleRules VehicleRules;

extern StaticDefineInt ControlSchemeRegionTypeEnum[];

AUTO_STRUCT;
typedef struct RegionLeashSettings {
	// Time to wait after losing targets but before leashing
	F32 fNoTargetWaitTime;		AST(NAME(NoTargetWaitTime))

	//Animation to play if you teleport
	const char* animListStart;  AST(NAME(AnimListStart) POOL_STRING)
	const char* animListFinish; AST(NAME(AnimListFinish) POOL_STRING)
	// Length of animation
	F32 fDurationStart;			AST(NAME(DurationStart))
	F32 fDurationFinish;		AST(NAME(DurationFinish))
	// Distance at which you should teleport (0 obviously means always)
	F32 fTeleportDist;			AST(NAME(TeleportDist))

	// Movement settings for leashing
	F32 fSpeed;					AST(NAME("Speed"))
	F32 fTurnRate;				AST(NAME("TurnRate") DEFAULT(-1))
	F32 fTraction;				AST(NAME("Traction"))
	F32 fFriction;				AST(NAME("Friction"))

	// If this is true, instead just walk while playing the anim
	//U32 noTeleport : 1;	AST(NAME(NoTeleport))
} RegionLeashSettings;

AUTO_STRUCT;
typedef struct CameraDistancePresets
{
	F32 fClosest;
	F32 fClose;
	F32 fMedium;
	F32 fFar;
	F32 fMaxZoomMin;
	F32 fMaxZoomMax;
	F32 fCloseAdjustDistance;
	F32 fDefaultAdjustDistance;
	F32 fFadeDistanceScale; AST(NAME(FadeScale, FadeDistanceScale) DEFAULT(1.0))
} CameraDistancePresets;

AUTO_STRUCT;
typedef struct SpawnBoxes{
	U32 uiSpawnBoxes[3];	AST(STRUCTPARAM)
	Vec3 vSpawnBoxSize;		AST(NAME(SpawnBoxSize))
	F32 fAreaCheck;			AST(NAME(AreaCheck))
}SpawnBoxes;

AUTO_STRUCT;
typedef struct RegionEncounterRadius {
	WorldEncounterRadiusType eType;		AST(STRUCTPARAM)
	S64 fValue;							AST(STRUCTPARAM)
} RegionEncounterRadius;

AUTO_STRUCT;
typedef struct RegionEncounterTimer {
	WorldEncounterTimerType eType;		AST(STRUCTPARAM)
	S64 fValue;							AST(STRUCTPARAM)
} RegionEncounterTimer;

AUTO_STRUCT;
typedef struct RegionEncounterWaveTimer {
	WorldEncounterWaveTimerType eType;	AST(STRUCTPARAM)
	S64 fValue;							AST(STRUCTPARAM)
} RegionEncounterWaveTimer;

AUTO_STRUCT;
typedef struct RegionEncounterWaveDelayTimer {
	WorldEncounterWaveTimerType eType;	AST(STRUCTPARAM)
	S64 fMinValue;						AST(STRUCTPARAM)
	S64 fMaxValue;						AST(STRUCTPARAM)
} RegionEncounterWaveDelayTimer;

AUTO_STRUCT;
typedef struct RegionAllegianceFXData
{
	REF_TO(AllegianceDef) hAllegiance; AST(STRUCTPARAM)
	const char** ppchSuccessFX; AST(NAME(SuccessFX) POOL_STRING)
	const char** ppchFailFX; AST(NAME(FailFX) POOL_STRING)
	const char* pchAnimList; AST(NAME(AnimList) POOL_STRING)
} RegionAllegianceFXData;

AUTO_STRUCT;
typedef struct KillCreditLimits
{
	S32 iMaxKills;		AST(NAME(MaxKills))
		// The maximum number of kills the player is allowed to get credit for in the specified time period
	S32 iTimePeriod;	AST(NAME(TimePeriod))
		// The amount of time (in seconds) that it takes to get back to the max kill count
	F32 fUpdateRate;	AST(NO_TEXT_SAVE)
		// Calculated update rate
} KillCreditLimits;

AUTO_STRUCT AST_IGNORE(uiMissionSharingDist);
typedef struct RegionRules
{
	const char* pchFileName;			AST( CURRENTFILE )

	// Region type information
	S32 eRegionType;					AST(KEY SUBTABLE(WorldRegionTypeEnum) STRUCTPARAM)
	U32* peCharClassTypes;				AST(NAME(CharClassType) SUBTABLE(CharClassTypesEnum))
	S32 eSchemeRegionType;				AST(SUBTABLE(ControlSchemeRegionTypeEnum))

	// Region display name
	DisplayMessage displayNameMsg;		AST(STRUCT(parse_DisplayMessage))

	// Control Scheme Options
	char*			pchSchemeToLoad;	AST(NAME(SchemeToLoad) POOL_STRING)

	// Camera and Control Options
	CameraDistancePresets* pCamDistPresets; AST(NAME(CameraDistancePresets))
	U32	bSpaceFlight : 1;
	U32 bFlightRotationIgnorePitch : 1;
	U32 bCameraFlightZoom : 1;
	U32 bCameraNearOffset : 1;
	U32 bAllowCameraLock : 1;
	U32 bAllowCrouchAndAim : 1;				AST(NAME(AllowAimMode))
	U32 bAllowNavThrottleAdjust : 1;
	U32 bHandleEntityAvoidance : 1;
	U32 bUseOverheadEntityGens : 1;	//Per-region override for the gConf flag 'bOverheadEntityGens'
	U32 bAlwaysCollideWithPets : 1; // If set, players will always collide with their pets
	F32 fCamDistInterpSpeed;			AST(DEFAULT(-1.0f))
	F32 fLoginThrottle;					AST(DEFAULT(1.0f))
	F32 fGravityMulti;			AST(DEFAULT(1.0))
	F32 fLaunchMultiDistance;				AST(DEFAULT(1.0f)) // Loot with type scatter will be launched with these multipliers applied
	F32 fLaunchMultiHeight;					AST(DEFAULT(1.0f)) // Loot with type scatter will be launched with these multipliers applied
	

	// Power Options
	S32 *piCategoryDoNotAdd;			AST(SUBTABLE(PowerCategoriesEnum))
	S32 *piCategoryExclude;				AST(SUBTABLE(PowerCategoriesEnum))
	S32	*piCategoryRequire;				AST(SUBTABLE(PowerCategoriesEnum))
	U32 bNoPrediction : 1;
	
	// Scan for clickies settings
	U32 bAllowScanForInteractables : 1;	AST(ADDNAMES(ScanForClickiesAllowed))
	U32 iScanForInteractablesCooldown;	AST(ADDNAMES(ScanForClickiesCooldownSecs) DEFAULT(5))
	F32 fScanForInteractablesRange;
	RegionAllegianceFXData** eaScanForInteractablesAllegianceFX; AST(ADDNAMES(ScanForClickiesAllegianceFX) SERVER_ONLY)

	// Legal Pet Types and number
	U32 *ePetType;						AST(SUBTABLE(CharClassTypesEnum))
	S32 iAllowedPetsPerPlayer;			AST(NAME("AllowedPetsPerPlayer"), DEFAULT(-1))

	// Temporary Puppet Options
	TempPuppetChoice **ppTempPuppets;	NO_AST

	// PvP Options
	U32 bDisableDueling : 1;
	U32 bEnableDueling : 1;

	// Time after death before player can respawn
	U32	uiRespawnTime;					AST(NAME(RespawnTime))


	// Entity Send Distances
	F32 fSendDistanceMin;				AST(NAME(MinSendDistance))
	F32 fSendDistanceMax;				AST(NAME(MaxSendDistance))
	F32 fSendDistanceBaseHeight;		AST(NAME(SendDistanceBaseHeight))

	// World Cell Distance Scale
	F32 fWorldCellDistanceScale;		AST(DEFAULT(1.0f))

	// Interaction Distances
	F32 fDefaultInteractDist;			AST(DEFAULT(10))
	F32 fDefaultInteractTargetDist;		AST(DEFAULT(50))
	F32 fDefaultInteractDistForEnts;	AST(DEFAULT(40))
	// Range after which interactables get put in the global list so they don't increase search range
	F32 fInteractDistCutoff;			AST(DEFAULT(100))

	U32 bDisableTargetableFX : 1;
	U32 bClickablesTargetable : 1;

	// Minimap Settings, if set these override the 1024 size heuristic
	U32 bUseRoomMapReveal : 1;
	U32 bUseGridMapReveal : 1;

	// Distance for unlocking a spawn point
	F32 fSpawnUnlockRadius;				AST(DEFAULT(150))

	// Mission Credit Distance
	U32 uiTeamKillCreditDist;			AST(NAME("TeamKillCreditDistance"))

	// Distance for being considered part of team
	U32 uiEncounterSpawnTeamDist;		AST(NAME("EncounterSpawnTeamDistance") DEFAULT(500))

	// Max distance to share a mission
//	U32 uiMissionSharingDist;			AST(NAME("MissionSharingDistance"))

	//Preset values for encounter distances and times
	RegionEncounterRadius** eaEncounterDistancePresets;					AST(NAME(EncounterDistance))
	RegionEncounterTimer** eaEncounterTimePresets;						AST(NAME(EncounterTime))
	RegionEncounterWaveTimer** eaEncounterWaveTimePresets;				AST(NAME(EncounterWaveTime))
	RegionEncounterWaveDelayTimer** eaEncounterWaveDelayTimePresets;	AST(NAME(EncounterWaveDelayTime))

	// Unit conversion rules
	MeasurementType eDefaultMeasurement;AST(DEFAULT(kMeasurementType_Base))
	MeasurementSize eMeasurementSize;	AST(DEFAULT(kMeasurementSize_Base))
	F32 fDisplayDistanceScale;			AST(DEFAULT(1.0f))
	F32 fDefaultDistanceScale;			AST(DEFAULT(1.0f))
	DisplayMessage dmsgDistUnits;		AST(NAME(DisplayDistanceUnits) STRUCT(parse_DisplayMessage))
	DisplayMessage dmsgDistUnitsShort;	AST(NAME(DisplayDistanceUnitsShort) STRUCT(parse_DisplayMessage))

	// Distance for UI drawing of Entities
	// Far is where entity becomes less known
	// Separation is for combining far entity gens
	F32	fEncounterFarDistance;
	F32 fEncounterMaxSeparation;		AST(DEFAULT(50.0f))

	// Default distances for social aggro
	F32 fSocialAggroPrimaryDist;
	F32 fSocialAggroSecondaryDist;

	// Distance multiplier for FormationDef slots
	F32 fFormationSlotScale;			AST(DEFAULT(10.0f))

	// Graphics Option
	U32 bIgnoreNoDraw : 1;

	// Positioning rules for pets and team members
	SpawnBoxes *pSpawnBox;				AST(NAME(SpawnBox))
	F32 fSpawnRadius;					AST(NAME(SpawnRadius))
	Expression *pXRelocation;			AST(NAME(XRelocation) LATEBIND)
	Expression *pYRelocation;			AST(NAME(YRelocation) LATEBIND)
	Expression *pZRelocation;			AST(NAME(ZRelocation) LATEBIND)

	// Leash Rules
	RegionLeashSettings *leashSettings; AST(NAME(LeashSettings))

	//Global Enter/Exit Sequences for this region type
	DoorTransitionSequenceRef** eaArriveSequences;	AST(NAME(ArriveSequence) SERVER_ONLY) 
	DoorTransitionSequenceRef** eaDepartSequences;	AST(NAME(DepartSequence) SERVER_ONLY)

	// Depart not supported until someone complains
	REF_TO(DoorTransitionSequenceDef) hPetRequestDepart;	AST(NAME(PetRequestDepart) SERVER_ONLY)
	REF_TO(DoorTransitionSequenceDef) hPetRequestArrive;	AST(NAME(PetRequestArrive) SERVER_ONLY)

	// AI Settings
	const char *aiAmbientDefaults;		AST(NAME(AIAmbientDefaults) DEFAULT("Default"))
	const char *aiCombatJobDefaults;	AST(NAME(AICombatJobDefaults) DEFAULT("Default"))
	const char* aiGroupCombatSettings;	AST(NAME(AIGroupCombatSettings))

	// All critter drops will drop this reward table unless there is an override reward table
	const char* pchGlobalCritterDropRewardTable; AST(NAME(GlobalCritterDropRewardTable) POOL_STRING)

	// Kill credit limit
	KillCreditLimits* pKillCreditLimit; AST(NAME(KillCreditLimit))

	// Vehicle Rules
	VehicleRules eVehicleRules;			AST(NAME(VehicleRules) DEFAULT(kVehicleRules_VehicleAllowed))	

}RegionRules;
extern ParseTable parse_RegionRules[];
#define TYPE_parse_RegionRules RegionRules

//Get the region rules from a vec3
RegionRules *RegionRulesFromVec3(const Vec3 vPos);
//Warning: Does not get override values (i.e., it isn't incredibly slow)
RegionRules *RegionRulesFromVec3NoOverride(const Vec3 vPos);
//Get the region rules from the type
//WARNING: will not get override values
RegionRules *getRegionRulesFromRegionType(S32 eRegionType);
RegionRules *getRegionRulesFromSchemeRegionType(S32 eSchemeRegionType);
RegionRules *getRegionRulesFromEnt(Entity* ent);

//Get the region rules from the Region
RegionRules *getRegionRulesFromRegion(WorldRegion *pRegion);
RegionRules* getRegionRulesFromZoneMap( ZoneMapInfo* pNextZoneMap );

//Chose a temp puppet from a list of temp puppets
TempPuppetChoice *entity_ChooseTempPuppet(Entity *pEnt, TempPuppetChoice **ppChoices);
#define entity_ChoosePuppet(pEnt, pRegionRules, ppChoiceOut) entity_ChoosePuppetEx(pEnt, pRegionRules, NULL, ppChoiceOut)
bool entity_ChoosePuppetEx(Entity *pEnt, RegionRules *pRegionRules, ZoneMapInfo *pZoneMapInfo, PuppetEntity **ppChoiceOut);

//Spawn Position Functions
void Entity_GetPositionOffset(int iPartitionIdx, RegionRules *pRules, Quat qRot, int iSeedValue, Vec3 vSpawnPos, int iSpawnBox[3]);

void Entity_SavedPetGetOriginalSpawnPos(Entity *pPetEntity, RegionRules *pRules, Quat qRot, Vec3 vSpawnPos);

void Entity_FindSpawnBox(Entity *pEntity, Vec3 vSpawnPos);

bool Entity_IsValidControlSchemeForCurrentRegionEx(Entity* pEntity, const char* pchScheme, ControlSchemeRegionInfo** ppInfo);
bool Entity_IsValidControlSchemeForCurrentRegion(Entity* pEntity, const char* pchScheme);

// Misc functions
RegionRules* getRegionRulesFromGenesisData(GenesisZoneMapData* genesis_data);

RegionRules* getValidRegionRulesForCharacterClassType(U32 eClassType);

extern DictionaryHandle g_hRegionRulesDict;

#endif
