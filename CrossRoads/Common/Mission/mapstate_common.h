#pragma once
GCC_SYSTEM
/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CombatEnums.h"
#include "gametimer_common.h"
#include "GameEvent.h"
#include "MultiVal.h"
#include "referencesystem.h"
#include "aiStructCommon.h"
#include "PVPGameCommon.h"


typedef struct OpenMission OpenMission;
typedef struct WorldVariable WorldVariable;
typedef struct ContactDialogOptionData ContactDialogOptionData;
typedef struct ContactDef ContactDef;
typedef struct PVPGroupGameParams PVPGroupGameParams;
typedef struct DOMControlPoint DOMControlPoint;
typedef struct MapState MapState;
typedef struct CritterDef CritterDef;
typedef struct CritterGroup CritterGroup;
typedef struct PlayerCostume PlayerCostume;
typedef enum NemesisMotivation NemesisMotivation;				
typedef enum NemesisPersonality NemesisPersonality;

typedef int PlayerDifficultyIdx;

AUTO_ENUM;
typedef enum ScoreboardState
{
	kScoreboardState_Init,			ENAMES(Init)
	kScoreboardState_Active,		ENAMES(Active)
	kScoreboardState_Final,			ENAMES(Final)
	kScoreboardState_Intermission,  ENAMES(Intermission)
} ScoreboardState;

AUTO_STRUCT;
typedef struct MatchMapState
{
	const char *pcScoreboardName;		AST(POOL_STRING)
	ScoreboardState eState;
	U32 uCounterTime;
	U8 bCountdown;
	U8 bOvertime;
	U32 uTotalMatchTime;
	PVPGroupGameParams **ppGroupGameParams; 
	// Custom parameters defined by the current game type
	DOMControlPoint **ppGameSpecific;						
	// Game specific structures
	PVPPublicGameRules pvpRules;
} MatchMapState;

AUTO_STRUCT;
typedef struct MapStateValue
{
	MapState *pState;			NO_AST

	const char *pcName;			AST(KEY POOL_STRING)
	MultiVal mvValue;

	GameEvent* pGameEvent;		AST(SERVER_ONLY)	// Event that causes the value to increment.  May be NULL
} MapStateValue;

AUTO_STRUCT;
typedef struct MapStateValueData
{
	DirtyBit dirtyBit;

	MapStateValue **eaValues;
} MapStateValueData;

AUTO_ENUM;
typedef enum PetTargetType
{
	kPetTargetType_NONE = -1, 
	kPetTargetType_Generic,		ENAMES(Generic)
	kPetTargetType_Kill,		ENAMES(Kill)
	kPetTargetType_Tank,		ENAMES(Tank)
	kPetTargetType_Control,		ENAMES(Control)
	kPetTargetType_Protect,		ENAMES(Protect)
	kPetTargetType_COUNT,
} PetTargetType;
extern StaticDefineInt PetTargetTypeEnum[];

AUTO_STRUCT;
typedef struct PetTargetingInfo
{
	EntityRef erPet;
	PetTargetType eType;
	EntityRef erTarget;
	S32 iIndex;
} PetTargetingInfo;

AUTO_STRUCT;
typedef struct PlayerMapValues
{
	// TODO: make sure these player ents stay on the server for postgame, even if they log out
	ContainerID iEntID; AST(KEY)
	MapStateValue **eaValues;

	PetTargetingInfo **eaPetTargetingInfo;
	U32 uiRespawnCount; AST(SERVER_ONLY)
		// The amount of times the player has respawned on this map (only valid if there is an incremental respawn time)
	U32 uiLastRespawnTime; AST(SERVER_ONLY)
		// The last time the player respawned (only valid if there is a respawn attrition time)
} PlayerMapValues;

AUTO_STRUCT;
typedef struct PlayerMapValueData
{
	DirtyBit dirtyBit;

	PlayerMapValues **eaPlayerValues;
} PlayerMapValueData;

AUTO_STRUCT;
typedef struct NodeMapStateEntry
{
	const char *pcNodeName;		AST(KEY)
	EntityRef uEntToWaitFor;
	bool bHidden;
	bool bDisabled;

	int iWaitingForEnt;			AST(CLIENT_ONLY) // Used to mark if actually waiting for the entity
} NodeMapStateEntry;

AUTO_STRUCT;
typedef struct NodeMapStateData
{
	DirtyBit dirtyBit;							AST(NO_NETSEND)
	
	NodeMapStateEntry **eaNodeEntries;
} NodeMapStateData;

AUTO_STRUCT;
typedef struct TeamMapValues
{
	ContainerID iTeamID; AST(KEY)
	
	PetTargetingInfo **eaPetTargetingInfo;

	U32 uiCreatedTime;

	bool bHasTeamMembers; NO_AST
} TeamMapValues;

AUTO_STRUCT;
typedef struct TeamMapValueData
{
	DirtyBit dirtyBit;

	TeamMapValues **eaTeamValues;
} TeamMapValueData;

AUTO_STRUCT;
typedef struct PublicVariableData
{
	DirtyBit dirtyBit;

	WorldVariable **eaPublicVars;
	WorldVariable **eaShardPublicVars;
} PublicVariableData;

AUTO_STRUCT;
typedef struct SyncDialogMember
{
	EntityRef entRef;					AST(KEY)
	bool bAwaitingResponse;
} SyncDialogMember;
extern ParseTable parse_SyncDialogMember[];
#define TYPE_parse_SyncDialogMember SyncDialogMember

AUTO_STRUCT;
typedef struct SyncDialog
{
	ContainerID uiTeamID;				AST(KEY)			// The container ID of the team to which this SyncDialog belongs
	SyncDialogMember **eaMembers;							// Members being held by the sync dialog
	U32 uiExpireTime;										// The time when this dialog will expire
	REF_TO(ContactDef) hContactDef;		AST(SERVER_ONLY)	// The contact def to use when running game actions upon expiration
	ContactDialogOptionData *pData;		AST(SERVER_ONLY)	// Data containing information on the actions which should be performed upon expiration
	EntityRef iInitiator;				AST(SERVER_ONLY)	// The entity who initiated the SyncDialog
} SyncDialog;
extern ParseTable parse_SyncDialog[];
#define TYPE_parse_SyncDialog SyncDialog

AUTO_STRUCT;
typedef struct NemesisTeamStruct
{
	// Id of player ent
	ContainerID iId;

	// critter def to use if character missing (nemesis)
	REF_TO(CritterDef) hCritter;				AST(NAME(CritterDef), RESOURCEDICT(CritterDef))

	// crittergroup (minions) for missing character
	REF_TO(CritterGroup) hCritterGroup;			AST(NAME(CritterGroup) RESOURCEDICT(CritterGroup))

	// checked and there is no nemesis (this is for team index, for leader see bLeaderNoNemesis in NemesisInfoStruct)
	bool bNoNemesis;

	// the name of the nemesis minion costume set
	const char* pchNemesisCostumeSet;			AST(POOL_STRING)

	// name of the nemesis
	const char *pchNemesisName;

	// costume of the nemesis
	PlayerCostume *pNemesisCostume;

	// type of the nemesis (mastermind etc)
	const char *pchNemesisType;					AST(POOL_STRING)

	NemesisMotivation motivation;				
	NemesisPersonality personality;			

}NemesisTeamStruct;

AUTO_STRUCT;
typedef struct NemesisInfoStruct
{
	// Index to the leader
	U32 iLeaderIdx;

	// information on each nemesis team member
	EARRAY_OF(NemesisTeamStruct) eaNemesisTeam;

	// If the leader has been set this is true (iLeaderIdx is now valid)
	bool bLeaderSet;

	// set if there is not a leader nemesis
	bool bLeaderNoNemesis;

	// The time that this is first checked, if too much time goes on then we can use no nemesis for team leader
	U32 uTimeFirstChecked;

}NemesisInfoStruct;

AUTO_STRUCT;
typedef struct MapState
{
	const char *pcMapName;				AST(POOL_STRING)
	int iPartitionIdx;

	// -- Simple Data Values --

	// Used to sync time with client
	U32 uServerTimeSecondsSince2000;

	// Flags
	U32 bPaused : 1;				// If true, the game is paused with time control
	U32 bBeingDestroyed : 1;		// If true, the partitionisBeingDestroyed
	U32 bPVPQueuesDisabled : 1;		// If true, all PVP queues are disabled

	// Combat system tweaking (not really part of the mission system, but there's no better place for it)
	F32 fSpeedRecharge;					AST(DEFAULT(1))

	// Time Control
	F32 fTimeControlTimer;       // How many seconds the map can be slowed/paused using the time control system
	char *pchTimeControlList;    // String with the list of players who have the game paused

	// Bolster Data
	BolsterType eBolsterType;			AST(DEFAULT(kBolsterType_None))
	S32 iBolsterLevel;

	// Difficulty Scale
	PlayerDifficultyIdx iDifficulty;	AST(INT)

	// -- Structured Data Values --
	// Note that many of these are optimized using DirtyBits

	// World node status
	NodeMapStateData *pNodeData;

	// Map Values
	MapStateValueData *pMapValues;
	PlayerMapValueData *pPlayerValueData;
	TeamMapValueData *pTeamValueData;
	MapStateValueData *pPrototypeValues;

	// Open Missions
	OpenMission **eaOpenMissions;

	// PVP specific state
	MatchMapState matchState;

	// Public map & shard variables
	PublicVariableData *pPublicVarData;

	// Synchronized Dialogs
	SyncDialog **eaSyncDialogs;

	// information about the team and team leader nemesis
	NemesisInfoStruct nemesisInfo;		AST(EMBEDDED_FLAT, SERVER_ONLY)

	// for keeping track of cutscenes played that are flagged with PlayOnceOnly
	const char **ppchCutscenesPlayed;	AST(NAME(CutscenesPlayed), POOL_STRING, SERVER_ONLY)

	// NO_AST Flags
	U32 bDifficultyInitialized : 1;		NO_AST

	// this is for Parser dirty bits optimization.  Leave this last on the structure.
	U32 dirtyBitSet : 1;				NO_AST
} MapState;
extern ParseTable parse_MapState[];
#define TYPE_parse_MapState MapState

// Functions for internal use (public here so that mapstate.c can use them
MapStateValue* mapState_FindMapValueInArray(MapStateValue ***peaValues, const char *pcValueName);

// Public functions
MultiVal* mapState_GetValue(SA_PARAM_OP_VALID MapState *pState, SA_PARAM_NN_STR const char *pcValueName);
SA_RET_OP_VALID MultiVal* mapState_GetPlayerValue(SA_PARAM_OP_VALID MapState *pState, SA_PARAM_NN_VALID Entity *pPlayerEnt, SA_PARAM_NN_STR const char *pcValueName);

void mapState_GetAllPublicVars(MapState *pState, WorldVariable ***peaWorldVars);
const PetTargetingInfo** mapState_GetPetTargetingInfo(Entity *pOwner);

bool mapState_IsMapPaused(MapState *pState);
bool mapState_IsMapPausedForPartition(int iPartitionIdx);
void mapState_SetHasAnyPausedPartition(bool bHasPaused);

bool mapState_ArePVPQueuesDisabled(MapState *pState);


// Indicates if the map hars any missions with the given state and tag

MapStateValue* mapState_FindPlayerValue(MapState *pState, Entity *pEnt, const char *pcValueName);
PlayerMapValues* mapState_FindPlayerValues(int iPartitionIdx, ContainerID iContID);
TeamMapValues* mapState_FindTeamValues(MapState *pState, U32 uiTeamID);
WorldVariable* mapState_GetPublicVarByName(MapState *pState, const char *pcVarName);
SA_RET_OP_VALID WorldVariable* mapState_GetShardPublicVar(MapState *pState, SA_PARAM_OP_STR const char *pcVarName);
SyncDialog* mapState_GetSyncDialogForTeam(MapState *pState, ContainerID uiTeamID);
const char* mapState_GetScoreboard(MapState *pState);
ScoreboardState mapState_GetScoreboardState(MapState *pState);
PVPGroupGameParams ***mapState_GetScoreboardGroupDefs(MapState *pState);
DOMControlPoint ***mapState_GetGameSpecificStructs(MapState *pState);
U32 mapState_GetScoreboardTimer(MapState *pState);
U32 mapState_GetScoreboardTotalMatchTimeInSeconds(MapState *pState);
bool mapState_IsScoreboardInOvertime(MapState *pState);
bool mapState_IsScoreboardInCountdown(MapState *pState);
Mission* mapState_FindMissionFromRefString(MapState *pState, const char *pcRefString);

// Finds an Open Mission with the given name
OpenMission* mapState_OpenMissionFromName(MapState* pState, const char *pcMissionName);

F32 mapState_SpeedRecharge(MapState *pState);
PlayerDifficultyIdx mapState_GetDifficulty(MapState *pState);

MapState* mapState_FromEnt(Entity *pEnt);
MapState* mapState_FromPartitionIdx(int iPartitionIdx);

