#pragma once

#include "referencesystem.h"

typedef struct AIConfigMod AIConfigMod;
typedef struct AIMastermindDef AIMastermindDef;
typedef struct Entity Entity;
typedef struct GameEncounter GameEncounter;
typedef struct WorldVolumeEntry WorldVolumeEntry;
typedef struct Expression Expression;
typedef struct ExprContext ExprContext;
typedef struct FSMContext FSMContext;
typedef struct FSM FSM;
// -----------------------------------------------
// DEF STRUCTS
// -----------------------------------------------
AST_PREFIX(WIKI(AUTO))


AUTO_STRUCT;
typedef struct AIMMExposureLevelDef
{
	// strict time between trying to spawn
	F32			fSpawnCooldown;					AST(NAME(SpawnCooldown) DEFAULT(10))

	// max number of encounters that will be spawned and active 
	S32			iNumEncounters;					AST(NAME(NumEncounters) DEFAULT(0))

	// NOTE: Min/Max & Tier spawn order are all 0 inclusive. 
	// the allowed tiers to spawn to spawn
	S32			iAllowedTiersMin;				AST(NAME(AllowedTiersMin))

	// the max tier to allowed to be spawned.
	S32			iAllowedTiersMax;				AST(NAME(AllowedTiersMax))

	// the amount of exposure in this level
	F32			fExposureRange;					AST(NAME(ExposureRange) DEFAULT(20))

} AIMMExposureLevelDef;

AUTO_STRUCT;
typedef struct AIMastermindExposeDef
{
	// a list of encounter groups ordered in tiers
	const char				**eapcEncounterTiers;	AST(NAME(EncounterTiers) POOL_STRING)

	// information about each heat level. No limit on the number of heat levels.
	AIMMExposureLevelDef	**eaExposeLevels;		AST(NAME(ExposeLevel))

	// how far it will search for an encounter 
	F32		fSpawnSearchRadius;						AST(NAME(SpawnSearchRadius) DEFAULT(150))

	// distance from the player that encounters will not be spawned in
	F32		fSpawnMinLockoutRadius;					AST(NAME(SpawnMinLockoutRadius) DEFAULT(20))

	// time in seconds before sent entities update the position they are searching for the player character
	F32		fPlayerPosUpdateFrequency;				AST(NAME(PlayerPosUpdateFrequency) DEFAULT(5))

	// time in seconds before a sent encounter stops persuing the player 
	F32		fSentEncounterDespawnTimeout;			AST(NAME(SentEncounterDespawnTimeout) DEFAULT(30))

	// the string name of the attribute that the mastermind system keys off of
	const char *pchExposeAttribName;				AST(NAME(ExposeAttribName) POOL_STRING)

	U32		bUseLOSChecksByDefault;					AST(NAME(UseLOSChecksByDefault))

	// 
	const char *pchHiddenPowerMode;					AST(NAME("HiddenPowerMode") POOL_STRING)

	AST_STOP

	AIMastermindDef			*pParentMMDef;
	S32						iHiddenMode;


} AIMastermindExposeDef;



AUTO_STRUCT;
typedef struct AIMMHeatExprCallbacks
{
	// called anytime a player dies
	Expression			*pExprOnPlayerDeath;	AST(NAME("OnPlayerDeath"), REDUNDANT_STRUCT("ExprOnPlayerDeath", parse_Expression_StructParam), LATEBIND)

	// called when all the players have been killed
	Expression			*pExprOnWipe;			AST(NAME("OnWipe"), REDUNDANT_STRUCT("ExprOnWipe", parse_Expression_StructParam), LATEBIND)

	// called every heat update
	Expression			*pExprPerTick;			AST(NAME("PerTick"), REDUNDANT_STRUCT("ExprPerTick", parse_Expression_StructParam), LATEBIND)

	// called when the spawn period timer is up, returning false will not allow a wave to be sent
	Expression			*pExprCanSendWave;		AST(NAME("CanSendWave"), REDUNDANT_STRUCT("ExprCanSendWave", parse_Expression_StructParam), LATEBIND)
	
	// if returns true, a wave will be force spawned. A forced spawn wave will not spawn another force spawn wave
	// until the wave that was spawned is not in or seeking combat
	Expression			*pExprShouldForceWave;	AST(NAME("ShouldForceWave"), REDUNDANT_STRUCT("ExprShouldForceWave", parse_Expression_StructParam), LATEBIND)

	// Called whenever a player enters a room for the first time	
	Expression			*pExprEnteredNewRoom;	AST(NAME("EnteredNewRoom"), REDUNDANT_STRUCT("ExprEnteredNewRoom", parse_Expression_StructParam), LATEBIND)


} AIMMHeatExprCallbacks;	

extern ParseTable parse_AIMMHeatExprCallbacks[];
#define TYPE_parse_AIMMHeatExprCallbacks AIMMHeatExprCallbacks
 

AUTO_STRUCT;
typedef struct AIMMHeatLevelDef
{
	// strict the time between spawns 
	F32			fSpawnPeriod;					AST(NAME(SpawnPeriod) DEFAULT(10.f))

	// when the spawn period fails to produce a spawn for any reason, the time until it will try again
	F32			fSpawnFailedResetTime;			AST(NAME(SpawnFailedResetTime) DEFAULT(10.f))

	// time idle before forcing a spawn
	F32			fIdleTimeForceSpawn;			AST(NAME(IdleTimeForceSpawn))
		
	// the team's average health percent must be over this in order to spawn
	F32			fHealthPercentThreshold;		AST(NAME(HealthPercentThreshold))
	
	// the difficulty value over the players
	F32			fDifficultyValueThreshold;		AST(NAME(DifficultyValueThreshold) DEFAULT(1))

	// NOTE: Min/Max & Tier spawn order are all 0 inclusive. 
	// the allowed tiers to spawn to spawn, these are ignored if the TierSpawnOrder is specified
	S32			iAllowedTiersMin;				AST(NAME(AllowedTiersMin))

	// the max tier to allowed to be spawned.
	S32			iAllowedTiersMax;				AST(NAME(AllowedTiersMax))

	// the order in which tiers will spawn
	INT_EARRAY	eaiTierSpawnOrder;				AST(NAME(TierSpawnOrder))
	
	// heat expression overrides, if not set, uses the expression on the base mastermind def
	AIMMHeatExprCallbacks	exprCallbacks;		AST(NAME(ExpressionCallbacks))

} AIMMHeatLevelDef;

AUTO_STRUCT;
typedef struct AIMMHeatDef
{
	// a list of encounter groups ordered in tiers
	const char			**eapcEncounterTiers;	AST(NAME(EncounterTeirs) POOL_STRING)

	// information about each heat level. No limit on the number of heat levels.
	AIMMHeatLevelDef	**eaHeatLevels;			AST(NAME(HeatLevel))

	// 
	AIMMHeatExprCallbacks	exprCallbacks;		AST(NAME(ExpressionCallbacks))

} AIMMHeatDef;


AUTO_STRUCT WIKI("AIMastermindDef");
typedef struct AIMastermindDef
{
	// config mods applied 
	// example line for a conig mod:
	// ConfigMod leashRangeCurrentTargetDistAdd 15
	AIConfigMod** configMods; 					AST(NAME("ConfigMod"))

	// the FSM override that is placed on encounters that spawn by the mastermind system
	const char *pchSentEncounterFSMOverride;	AST(NAME(SentEncounterFSMOverride) POOL_STRING)


	AIMMHeatDef				*pHeatDef;			AST(NAME(HeatDef))

	AIMastermindExposeDef	*pExposeDef;		AST(NAME(ExposeDef))
	
	const char* pchName;						AST(KEY, STRUCTPARAM)
	char* pchFilename;							AST(CURRENTFILE)
} AIMastermindDef;


typedef struct AIMastermindAIVars
{
	U32				*configMods;
	REF_TO(FSM)		hPrevFSM;
} AIMastermindAIVars;

void aiMastermind_DestroyAIVars(Entity *e);


void aiMastermindHeat_TrackVolumeEntered(WorldVolumeEntry *pEntry, Entity *pEnt);
void aiMastermindHeat_TrackVolumeExited(WorldVolumeEntry *pEntry, Entity *pEnt);
void aiMastermind_Startup();
void aiMastermind_OncePerFrame();
void aiMastermind_OnMapLoad();
void aiMastermind_OnMapUnload();

//void aiMastermind_AIEntCreatedCallback(Entity *e);
void aiMastermind_AIEntDestroyedCallback(Entity *e);

int aiMastermind_IsMastermindMap();

ExprContext* aiMastermind_GetExprContext();

void aiMastermind_AIEntCreatedCallback(Entity *e);
void aiMastermind_AIEntDestroyedCallback(Entity *e);

void aiMastermind_PrimeEntityForSending(Entity *e, Entity *sendAtEnt, AIMastermindDef *pDef, bool bSaveOldFSM);
void aiMastermind_UndoMastermindPriming(Entity *e, EntityRef erSendAtEnt, AIMastermindDef *pDef);