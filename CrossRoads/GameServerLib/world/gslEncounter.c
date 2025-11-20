/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aiCombatRoles.h"
#include "aiJobs.h"
#include "aiLib.h"
#include "aiTeam.h"
#include "beacon.h"
#include "Character.h"
#include "ChoiceTable_common.h"
#include "contact_common.h"
#include "CostumeCommonEntity.h"
#include "cutscene.h"
#include "encounter_common.h"
#include "entCritter.h"
#include "Entity.h"
#include "EntityLib.h"
#include "EntityGrid.h"
#include "EntitySavedData.h"
#include "EntityMovementDefault.h"
#include "error.h"
#include "Expression.h"
#include "GameEvent.h"
#include "GameServerLib.h"
#include "gslContact.h"
#include "gslCritter.h"
#include "gslEncounter.h"
#include "gslEntity.h"
#include "gslEventSend.h"
#include "gslEventTracker.h"
#include "gslMapState.h"
#include "gslMapTransfer.h"
#include "gslMechanics.h"
#include "gslMission.h"
#include "gslOldEncounter.h"
#include "gslPartition.h"
#include "gslPatrolRoute.h"
#include "gslPlayerDifficulty.h"
#include "gslPowerTransactions.h"
#include "gslQueue.h"
#include "gslWorldVariable.h"
#include "HashFunctions.h"
#include "interaction_common.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "nemesis_common.h"
#include "nemesis.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "PlayerDifficultyCommon.h"
#include "rand.h"
#include "RegionRules.h"
#include "RoomConn.h"
#include "../RoomConnPrivate.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "structInternals.h"
#include "UtilitiesLib.h"
#include "wlEncounter.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "../StaticWorld/WorldGridPrivate.h"
#include "UGCProjectUtils.h"
#include "gslMapVariable.h"
#include "Team.h"
#include "CostumeCommonLoad.h"

#include "Entity_h_ast.h"
#include "Player_h_ast.h"
#include "AutoGen/gslEncounter_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#include "wlVolumes.h"
#include "EntityIterator.h"
#include "gslEntityPresence.h"


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

typedef struct LongSearchData {
	int iPartitionIdx;
	U32 uNextSearchTime;
	ContainerID *eaiLongSearchPlayers;
} LongSearchData;

static ExprContext *s_pEncounterContext = NULL;
static ExprContext *s_pEncounterPlayerContext = NULL;
static ExprContext *s_pEncounterInteractContext = NULL;
static ExprContext *s_pGeneralPlayerContext = NULL;

static LongSearchData **s_eaLongSearchData = NULL;

static U32 s_uMaxInteractDist = 0.0;
ContainerID s_uEditingClient = 0;

static bool s_bRecacheTemplates = false;

// Global debugging controls, NOT per-partition
bool g_EncounterProcessing = true;
bool g_EncounterResetOnNextTick = false;
int g_encounterDisableSleeping = false;
int g_encounterIgnoreProximity = false;
U32 g_ForceTeamSize = 0; // Force encounters to spawn at a certain team size, set to 0 to disable

// This is the number of ticks between processing of each encounter
#define ENCOUNTER_TICK_COUNT  10

// Tick logical groups every second
#define ENCOUNTER_LOGICAL_GROUP_TICK_RATE 1.0

// Time to wait for owner entity before processing encounters
#define SECONDS_TO_WAIT_FOR_OWNER 30

// Distance an actor has to fall to be considered to have fallen through the world
#define ACTOR_FALLTHROUGHWORLD_CHECKDIST 1000

// Each phase is ENCOUNTER_TICK_COUNT frames
#define MIN_PHASES_FOR_FALL_CHECK   6
#define MIN_PHASES_FOR_SPAWN_CHECK  3
#define MIN_PHASES_FOR_ACTIVE_CHECK 6

// The time (in seconds) between checking if a wave should spawn
#define WAVE_ATTACK_POLL_INTERVAL 1.0f

// Distance the actor will look for a ground to snap to before spawning in midair
#define FIRST_ACTOR_SPAWN_SNAP_TO_DIST 7
#define SECOND_ACTOR_SPAWN_SNAP_TO_DIST 20
// Distance an actor will look for ground to snap to that doesn't want to snap
#define ACTOR_SPAWN_NOSNAP_SNAP_TO_DIST_UP		3
#define ACTOR_SPAWN_NOSNAP_SNAP_TO_DIST_DOWN	1

#define DEFAULT_ENT_INTERACT_RANGE 10

// Time to wait (in seconds) before clearing the nearby teammate count used in calculating team size
#define NEARBY_TEAMMATE_COUNT_LIFESPAN 120

// Every time the player gets close to an ambush, it has a 10% chance of springing and a 10% chance of not
// springing but entering ambush cooldown anyway (so players can't reliably trigger ambushes by running past them).
// Logic assumes that AMBUSH_CHANCE + SKIP_AMBUSH_CHANCE <= 100
// These are effectively #defines that are exposed to commands for debugging, so they are NOT per-partition
U32 g_AmbushCooldown = 600;
U32 g_AmbushChance = 10;
U32 g_AmbushSkipChance = 10;
U32 g_AmbushDebugEnabled = 0;

// Partition-specific state data
PartitionEncounterData **s_eaPartitionEncounterData = NULL;

// Encounter Data
GameEncounter **s_eaEncounters = NULL;
static GameEncounter **s_eaOldEncounters = NULL;
static GameEncounterGroup **s_eaEncounterGroups = NULL;
static GameEncounterGroup **s_eaOldEncounterGroups = NULL;
static GameEncounterGroup **s_eaEncounterLogicalGroups = NULL;

// Octree to make searching for encounters by location better (shared by all partitions)
static Octree *s_pOctree;

static U32 s_uTick = 0;
static F32 s_afTimeSinceUpdate[ENCOUNTER_TICK_COUNT];


static void encounter_PostProcessPartition(GameEncounter *pEncounter, GameEncounterPartitionState *pState, PartitionEncounterData *pData);
static void encounter_CacheTemplate(GameEncounter *pEncounter);


static bool s_bEnableSpawnAggroDelay = false;

// ----------------------------------------------------------------------------------
// Partition Data Management
// ----------------------------------------------------------------------------------

static PartitionEncounterData* encounter_GetPartitionData(int iPartitionIdx)
{
	PartitionEncounterData *pData = eaGet(&s_eaPartitionEncounterData, iPartitionIdx);
	assertmsgf(pData, "Partition %d does not exist", iPartitionIdx);
	return pData;
}


static PartitionEncounterData* encounter_CreatePartitionData(int iPartitionIdx)
{
	PartitionEncounterData *pData = eaGet(&s_eaPartitionEncounterData, iPartitionIdx);

	if (!pData) {
		pData = calloc(1,sizeof(PartitionEncounterData));
		pData->uPartition_dbg = partition_IDFromIdx(iPartitionIdx);
		pData->iPartitionIdx = iPartitionIdx;
		eaIndexedEnable(&pData->eaLogicalGroupData, parse_PartitionLogicalGroupEncData);
		eaSet(&s_eaPartitionEncounterData, pData, iPartitionIdx);

		// We don't expect this to be non-NULL, but just for safety
		if (pData->eaPetContactNameExcludeList!=NULL)
		{
			eaDestroyEString(&(pData->eaPetContactNameExcludeList));
			pData->eaPetContactNameExcludeList=NULL;
		}
	}

	return pData;
}


static void encounter_DestroyPartitionData(int iPartitionIdx)
{
	PartitionEncounterData *pData = eaGet(&s_eaPartitionEncounterData, iPartitionIdx);
	if (pData) {
		if (pData->eaPetContactNameExcludeList!=NULL)
		{
			eaDestroyEString(&(pData->eaPetContactNameExcludeList));
			pData->eaPetContactNameExcludeList=NULL;
		}
		wlVolumeQueryCacheFree(pData->pPlayableVolumeQuery);
		eaDestroyStruct(&pData->eaLogicalGroupData, parse_PartitionLogicalGroupEncData);
		free(pData);
		eaSet(&s_eaPartitionEncounterData, NULL, iPartitionIdx);
	}
}


// ----------------------------------------------------------------------------------
// Sole interfaces to searching s_eaEncounters.  No other function
// than encounter_GetByName and encounter_GetByEntry should be searching s_eaEncounters.
// ----------------------------------------------------------------------------------
static GameEncounter *encounter_GetByEntry(WorldEncounter *pWorldEncounter)
{
	int i;

	for(i=eaSize(&s_eaEncounters)-1; i>=0; --i) {
		if (SAFE_MEMBER(s_eaEncounters[i], pWorldEncounter) == pWorldEncounter) {
			return s_eaEncounters[i];
		}
	}

	return NULL;
}


GameEncounter *encounter_GetByName(const char *pcEncounterName, const WorldScope *pScope)
{
	if (pScope && gUseScopedExpr) {
		WorldEncounterObject *pObject = worldScopeGetObject(pScope, pcEncounterName);

		if (pObject && pObject->type == WL_ENC_ENCOUNTER) {
			WorldEncounter *pNamedEncounter = (WorldEncounter *)pObject;
			GameEncounter *pGameEncounter = encounter_GetByEntry(pNamedEncounter);
			if (pGameEncounter) {
				return pGameEncounter;
			}
		}
	} else {
		int i;

		for(i=eaSize(&s_eaEncounters)-1; i>=0; --i) {
			GameEncounter *pGameEncounter = s_eaEncounters[i];
			if (pGameEncounter && stricmp(pcEncounterName, pGameEncounter->pcName) == 0) {
				return pGameEncounter;
			}
		}
	}

	return NULL;
}


__forceinline GameEncounterPartitionState *encounter_GetPartitionState(int iPartitionIdx, GameEncounter *pEncounter)
{
	GameEncounterPartitionState *pState = eaGet(&pEncounter->eaPartitionStates, iPartitionIdx);
	assertmsgf(pState, "Partition %d does not exist", iPartitionIdx);
	return pState;
}


GameEncounterPartitionState *encounter_GetPartitionStateIfExists(int iPartitionIdx, GameEncounter *pEncounter)
{
	GameEncounterPartitionState *pState = eaGet(&pEncounter->eaPartitionStates, iPartitionIdx);
	return pState;
}


#define FOR_EACH_ENCOUNTER(it) { int i##it##Index; for(i##it##Index=eaSize(&s_eaEncounters)-1; i##it##Index>=0; --i##it##Index) { GameEncounter *it = s_eaEncounters[i##it##Index];

// ----------------------------------------------------------------------------------
// End of sole interfaces to searching s_eaEncounters.
// ----------------------------------------------------------------------------------


static GameEncounterGroup *encounter_GetGroupByEntry(WorldLogicalGroup *pWorldGroup)
{
	int i;

	for(i=eaSize(&s_eaEncounterGroups)-1; i>=0; --i) {
		if (SAFE_MEMBER(s_eaEncounterGroups[i], pWorldGroup) == pWorldGroup) {
			return s_eaEncounterGroups[i];
		}
	}

	return NULL;
}


GameEncounterGroup *encounter_GetGroupByName(const char *pcGroupName, const WorldScope *pScope)
{
	if (pScope && gUseScopedExpr) {
		WorldEncounterObject *pObject = worldScopeGetObject(pScope, pcGroupName);

		if (pObject && pObject->type == WL_ENC_LOGICAL_GROUP) {
			WorldLogicalGroup *pWorldGroup = (WorldLogicalGroup *)pObject;
			GameEncounterGroup *pGroup = encounter_GetGroupByEntry(pWorldGroup);
			if (pGroup) {
				return pGroup;
			}
		}
	} else {
		int i;

		for(i=eaSize(&s_eaEncounterGroups)-1; i>=0; --i) {
			if (stricmp(pcGroupName, s_eaEncounterGroups[i]->pcName) == 0) {
				return s_eaEncounterGroups[i];
			}
		}
	}

	return NULL;
}

#define FOR_EACH_ENCOUNTER_GROUP(it) { int i##it##Index; for(i##it##Index=eaSize(&s_eaEncounterGroups)-1; i##it##Index>=0; --i##it##Index) { GameEncounterGroup *it = s_eaEncounterGroups[i##it##Index];

// ----------------------------------------------------------------------------------
// Expression Context Function Tables and Evaluation
// ----------------------------------------------------------------------------------

// Function table for things in the encounter layer that have a "current player"
ExprFuncTable* encPlayer_CreateExprFuncTable()
{
	static ExprFuncTable* s_encPlayerFuncTable = NULL;
	if (!s_encPlayerFuncTable) {
		s_encPlayerFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_encPlayerFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_encPlayerFuncTable, "event_count");
		exprContextAddFuncsToTableByTag(s_encPlayerFuncTable, "encounter_action");
		exprContextAddFuncsToTableByTag(s_encPlayerFuncTable, "player");
		exprContextAddFuncsToTableByTag(s_encPlayerFuncTable, "PTECharacter");
		exprContextAddFuncsToTableByTag(s_encPlayerFuncTable, "entity");
		exprContextAddFuncsToTableByTag(s_encPlayerFuncTable, "entityutil");
		exprContextAddFuncsToTableByTag(s_encPlayerFuncTable, "gameutil");
	}
	return s_encPlayerFuncTable;
}


// Function table for encounters
ExprFuncTable* encounter_CreateExprFuncTable()
{
	static ExprFuncTable* s_encounterFuncTable = NULL;
	if (!s_encounterFuncTable) {
		s_encounterFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_encounterFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_encounterFuncTable, "event_count");
		exprContextAddFuncsToTableByTag(s_encounterFuncTable, "encounter_action");
		exprContextAddFuncsToTableByTag(s_encounterFuncTable, "encounter");
		exprContextAddFuncsToTableByTag(s_encounterFuncTable, "gameutil");
		exprContextAddFuncsToTableByTag(s_encounterFuncTable, "entityutil");
	}
	return s_encounterFuncTable;
}


// Function table for encounter spawn conditions that run on players
ExprFuncTable* encounter_CreatePlayerExprFuncTable()
{
	static ExprFuncTable* s_encounterPlayerFuncTable = NULL;
	if (!s_encounterPlayerFuncTable) {
		s_encounterPlayerFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(s_encounterPlayerFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_encounterPlayerFuncTable, "event_count");
		exprContextAddFuncsToTableByTag(s_encounterPlayerFuncTable, "encounter_action");
		exprContextAddFuncsToTableByTag(s_encounterPlayerFuncTable, "encounter");
		exprContextAddFuncsToTableByTag(s_encounterPlayerFuncTable, "player");
		exprContextAddFuncsToTableByTag(s_encounterPlayerFuncTable, "PTECharacter");
		exprContextAddFuncsToTableByTag(s_encounterPlayerFuncTable, "gameutil");
		exprContextAddFuncsToTableByTag(s_encounterPlayerFuncTable, "entity");
		exprContextAddFuncsToTableByTag(s_encounterPlayerFuncTable, "entityutil");
	}
	return s_encounterPlayerFuncTable;
}

int encPlayer_Evaluate(Entity *pEnt, Expression *pExpr, WorldScope *pScope)
{
	MultiVal mvResultVal;

	exprContextSetSelfPtr(s_pGeneralPlayerContext, pEnt);
	exprContextSetScope(s_pGeneralPlayerContext, pScope);
	exprContextSetPartition(s_pGeneralPlayerContext, entGetPartitionIdx(pEnt));

	// If the entity is a player, add it to the context as "Player"
	if (entGetPlayer(pEnt)) {
		exprContextSetPointerVarPooled(s_pGeneralPlayerContext, g_PlayerVarName, pEnt, NULL, false, true);
	} else {
		exprContextRemoveVarPooled(s_pGeneralPlayerContext, g_PlayerVarName);
	}

	exprEvaluate(pExpr, s_pGeneralPlayerContext, &mvResultVal);

	return MultiValGetInt(&mvResultVal, NULL);
}


static int encounter_ExprEvaluate(int iPartitionIdx, GameEncounter *pEncounter, Expression *pExpr)
{
	MultiVal mvResultVal;

	// If no expression, consider value as 1
	if (!pExpr) {
		return 1;
	}

	exprContextSetPartition(s_pEncounterContext, iPartitionIdx);
	exprContextSetScope(s_pEncounterContext, pEncounter->pWorldEncounter->common_data.closest_scope);
	exprContextSetPointerVarPooled(s_pEncounterContext, g_Encounter2VarName, pEncounter, parse_GameEncounter, false, true);

	exprEvaluate(pExpr, s_pEncounterContext, &mvResultVal);
	return MultiValGetInt(&mvResultVal, NULL);
}


static int encounter_ExprPlayerEvaluate(int iPartitionIdx, GameEncounter *pEncounter, Entity *pEnt, Expression *pExpr)
{
	MultiVal mvResultVal;

	// If no expression, consider value as 1
	if (!pExpr) {
		return 1;
	}

	exprContextSetPartition(s_pEncounterPlayerContext, iPartitionIdx);
	exprContextSetScope(s_pEncounterPlayerContext, pEncounter->pWorldEncounter->common_data.closest_scope);
	exprContextSetSelfPtr(s_pEncounterPlayerContext, pEnt);
	exprContextSetPointerVarPooled(s_pEncounterPlayerContext, g_Encounter2VarName, pEncounter, parse_GameEncounter, false, true);

	// If the entity is a player, add it to the context as "Player"
	if (entGetPlayer(pEnt)) {
		exprContextSetPointerVarPooled(s_pEncounterPlayerContext, g_PlayerVarName, pEnt, NULL, false, true);
	} else {
		exprContextRemoveVarPooled(s_pEncounterPlayerContext, g_PlayerVarName);
	}

	exprEvaluate(pExpr, s_pEncounterPlayerContext, &mvResultVal);
	return MultiValGetInt(&mvResultVal, NULL);
}


int encounter_ExprInteractEvaluate(int iPartitionIdx, Entity *pPlayerEnt, Entity *pCritterEnt, Expression *pExpr, WorldScope *pScope)
{
	ExprContext *pContext = s_pEncounterInteractContext;
	MultiVal mval = {0};
	bool bResult;

	PERFINFO_AUTO_START_FUNC();

	exprContextSetPointerVarPooled(pContext, g_PlayerVarName, pPlayerEnt, NULL, false, true);
	exprContextSetSelfPtr(pContext, pCritterEnt);
	exprContextSetPartition(pContext, iPartitionIdx);

	if (pCritterEnt->pCritter->encounterData.pGameEncounter) {
		exprContextSetPointerVarPooled(pContext, g_Encounter2VarName, pCritterEnt->pCritter->encounterData.pGameEncounter, parse_GameEncounter, false, true);
	} else {
		exprContextRemoveVarPooled(pContext, g_Encounter2VarName);
	}

	exprEvaluate(pExpr, pContext, &mval);

	bResult = (bool)!!MultiValGetInt(&mval, NULL);

	PERFINFO_AUTO_STOP();

	return bResult;
}


// ----------------------------------------------------------------------------------
// Event Processing
// ----------------------------------------------------------------------------------

int encounter_EventCount(int iPartitionIdx, GameEncounter *pEncounter, const char *pcEventName)
{
	int iEventCount = 0;
	if (pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		if (!stashFindInt(pState->eventData.stEventLog, pcEventName, &iEventCount)) {
			Errorf("Encounter '%s' had EventCount called on an event it was not subscribed to (%s).  This is a programmer bug, not a design data problem.", pEncounter->pcName, pcEventName);
		}
	}
	return iEventCount;
}


int encounter_EventCountSinceSpawn(int iPartitionIdx, GameEncounter *pEncounter, const char *pcEventName)
{
	int iEventCount = 0;
	if (pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		if (!stashFindInt(pState->eventData.stEventLogSinceSpawn, pcEventName, &iEventCount)) {
			Errorf("Encounter '%s' had EventCountSinceSpawn called on an event it was not subscribed to (%s).  This is a programmer bug, not a design data problem.", pEncounter->pcName, pcEventName);
		}
	}
	return iEventCount;
}


int encounter_EventCountSinceComplete(int iPartitionIdx, GameEncounter *pEncounter, const char *pcEventName)
{
	int iEventCount = 0;
	if (pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		if (!stashFindInt(pState->eventData.stEventLogSinceComplete, pcEventName, &iEventCount)) {
			Errorf("Encounter '%s' had EventCountSinceComplete called on an event it was not subscribed to (%s).  This is a programmer bug, not a design data problem.", pEncounter->pcName, pcEventName);
		}
	}
	return iEventCount;
}


static void encounter_EventCountAdd(GameEncounter *pEncounter, GameEvent *pEvent, GameEvent *pSpecific, int iIncrement)
{
	if (pEvent && pEvent->pchEventName && pEncounter) {
		int iEventCount;
		GameEncounterPartitionState *pState = encounter_GetPartitionState(pEvent->iPartitionIdx, pEncounter);
		stashFindInt(pState->eventData.stEventLog, pEvent->pchEventName, &iEventCount);
		stashAddInt(pState->eventData.stEventLog, pEvent->pchEventName, iEventCount+iIncrement, true);
	}
}


static void encounter_EventCountAddSinceSpawn(GameEncounter *pEncounter, GameEvent *pEvent, GameEvent *pSpecific, int iIncrement)
{
	if (pEvent && pEvent->pchEventName && pEncounter) {
		int iEventCount;
		GameEncounterPartitionState *pState = encounter_GetPartitionState(pEvent->iPartitionIdx, pEncounter);
		stashFindInt(pState->eventData.stEventLogSinceSpawn, pEvent->pchEventName, &iEventCount);
		stashAddInt(pState->eventData.stEventLogSinceSpawn, pEvent->pchEventName, iEventCount+iIncrement, true);
	}
}


static void encounter_EventCountAddSinceComplete(GameEncounter *pEncounter, GameEvent *pEvent, GameEvent *pSpecific, int iIncrement)
{
	if (pEvent && pEvent->pchEventName && pEncounter) {
		int iEventCount;
		GameEncounterPartitionState *pState = encounter_GetPartitionState(pEvent->iPartitionIdx, pEncounter);
		stashFindInt(pState->eventData.stEventLogSinceComplete, pEvent->pchEventName, &iEventCount);
		stashAddInt(pState->eventData.stEventLogSinceComplete, pEvent->pchEventName, iEventCount+iIncrement, true);
	}
}


static void encounter_EventCountSet(GameEncounter *pEncounter, GameEvent *pEvent, GameEvent *pSpecific, int iValue)
{
	if (pEvent && pEvent->pchEventName && pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(pEvent->iPartitionIdx, pEncounter);
		stashAddInt(pState->eventData.stEventLog, pEvent->pchEventName, iValue, true);
	}
}


static void encounter_EventCountSetSinceSpawn(GameEncounter *pEncounter, GameEvent *pEvent, GameEvent *pSpecific, int iValue)
{
	if (pEvent && pEvent->pchEventName && pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(pEvent->iPartitionIdx, pEncounter);
		stashAddInt(pState->eventData.stEventLogSinceSpawn, pEvent->pchEventName, iValue, true);
	}
}


static void encounter_EventCountSetSinceComplete(GameEncounter *pEncounter, GameEvent *pEvent, GameEvent *specific, int iValue)
{
	if (pEvent && pEvent->pchEventName && pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(pEvent->iPartitionIdx, pEncounter);
		stashAddInt(pState->eventData.stEventLogSinceComplete, pEvent->pchEventName, iValue, true);
	}
}


static void encounter_BeginTrackingEvents(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	int i;

	for(i=eaSize(&pState->eventData.eaTrackedEvents)-1; i>=0; --i) {
		GameEvent *pEvent = pState->eventData.eaTrackedEvents[i];
		eventtracker_StartTracking(pEvent, SAFE_MEMBER(pEncounter->pWorldEncounter, common_data.closest_scope), pEncounter, encounter_EventCountAdd, encounter_EventCountSet);
		stashAddInt(pState->eventData.stEventLog, pEvent->pchEventName, 0, true);
	}

	for(i=eaSize(&pState->eventData.eaTrackedEventsSinceSpawn)-1; i>=0; --i) {
		GameEvent *pEvent = pState->eventData.eaTrackedEventsSinceSpawn[i];
		eventtracker_StartTracking(pEvent, SAFE_MEMBER(pEncounter->pWorldEncounter, common_data.closest_scope), pEncounter, encounter_EventCountAddSinceSpawn, encounter_EventCountSetSinceSpawn);
		stashAddInt(pState->eventData.stEventLogSinceSpawn, pEvent->pchEventName, 0, true);
	}

	for(i=eaSize(&pState->eventData.eaTrackedEventsSinceComplete)-1; i>=0; --i) {
		GameEvent *pEvent = pState->eventData.eaTrackedEventsSinceComplete[i];
		eventtracker_StartTracking(pEvent, SAFE_MEMBER(pEncounter->pWorldEncounter, common_data.closest_scope), pEncounter, encounter_EventCountAddSinceComplete, encounter_EventCountSetSinceComplete);
		stashAddInt(pState->eventData.stEventLogSinceComplete, pEvent->pchEventName, 0, true);
	}
}


static void encounter_StopTrackingEvents(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	int i;

	for(i=eaSize(&pState->eventData.eaTrackedEvents)-1; i>=0; --i) {
		GameEvent *pEvent = pState->eventData.eaTrackedEvents[i];
		eventtracker_StopTracking(pState->iPartitionIdx, pEvent, pEncounter);
	}
	eaDestroyStruct(&pState->eventData.eaTrackedEvents, parse_GameEvent);
	stashTableClear(pState->eventData.stEventLog);

	for(i=eaSize(&pState->eventData.eaTrackedEventsSinceSpawn)-1; i>=0; --i) {
		GameEvent *pEvent = pState->eventData.eaTrackedEventsSinceSpawn[i];
		eventtracker_StopTracking(pState->iPartitionIdx, pEvent, pEncounter);
	}
	eaDestroyStruct(&pState->eventData.eaTrackedEventsSinceSpawn, parse_GameEvent);
	stashTableClear(pState->eventData.stEventLogSinceSpawn);

	for(i=eaSize(&pState->eventData.eaTrackedEventsSinceComplete)-1; i>=0; --i) {
		GameEvent *pEvent = pState->eventData.eaTrackedEventsSinceComplete[i];
		eventtracker_StopTracking(pState->iPartitionIdx, pEvent, pEncounter);
	}
	eaDestroyStruct(&pState->eventData.eaTrackedEventsSinceComplete, parse_GameEvent);
	stashTableClear(pState->eventData.stEventLogSinceComplete);
}


static void encounter_StopTrackingEventsAll(int iPartitionIdx)
{
	FOR_EACH_ENCOUNTER(pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		encounter_StopTrackingEvents(pEncounter, pState);
	} FOR_EACH_END;
}


// ----------------------------------------------------------------------------------
// Encounter Data Access and Manipulation
// ----------------------------------------------------------------------------------

#define MAX_FAR_PLAYER_ENTITIES_ALLOWED 10
#define LONG_SEACH_TIME_INTERVAL_SECONDS 3

// Note that this function uses a static variable inside, so the returned array is
// only good until the next time this is called.  
// The array may be eaDestroyed, but doesn't have to be.
Entity ***encounter_GetNearbyPlayers(int iPartitionIdx, GameEncounter *pEncounter, F32 fDist)
{
	static Entity **s_eaNearbyPlayers = NULL;
	PerfInfoGuard* piGuardFunc;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuardFunc);

	if (fDist < 0) {
		fDist = pEncounter->fSpawnRadius;
	}

	// unless this is a map wide search use visibility range which is set by region rules
	if (fDist < (WORLD_ENCOUNTER_RADIUS_TYPE_ALWAYS_DISTANCE - 1.0f)) {
		// Count ENTITYFLAG_IGNORE players; they should spawn encounters, even if the encounters don't aggro on them
		entGridProximityLookupExEArray(iPartitionIdx, pEncounter->pWorldEncounter->encounter_pos, &s_eaNearbyPlayers, fDist, ENTITYFLAG_IS_PLAYER, 0, NULL);
	} else {
		PerfInfoGuard* piGuard;
		LongSearchData *pData = eaGet(&s_eaLongSearchData, iPartitionIdx);
		S32 i;

		PERFINFO_AUTO_START_GUARD("LongSearch",1,&piGuard);
		
		if (!pData || (timeSecondsSince2000() >= pData->uNextSearchTime)) {
			if (!pData) {
				pData = calloc(1,sizeof(LongSearchData));
				eaSet(&s_eaLongSearchData, pData, iPartitionIdx);
			}
			eaiClear(&pData->eaiLongSearchPlayers);
			
			if (partition_GetPlayerCount(iPartitionIdx) > 0) {
				EntityIterator *pIter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
				Entity *pEnt;
				U32 iCount = 0;
				
				while(pEnt = EntityIteratorGetNext(pIter)) {
					eaiPush(&pData->eaiLongSearchPlayers, pEnt->myContainerID);
					++iCount;
					if (iCount >= MAX_FAR_PLAYER_ENTITIES_ALLOWED) {
						break;
					}
				}
				EntityIteratorRelease(pIter);
			}
			
			pData->uNextSearchTime = timeSecondsSince2000() + LONG_SEACH_TIME_INTERVAL_SECONDS;
		}

		eaClear(&s_eaNearbyPlayers);
		for(i = 0; i < eaiSize(&pData->eaiLongSearchPlayers); ++i) {
			Entity *pLastEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pData->eaiLongSearchPlayers[i]);
			if(pLastEnt) {
				eaPush(&s_eaNearbyPlayers, pLastEnt);
			}
		}

		PERFINFO_AUTO_STOP_GUARD(&piGuard);
		
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
	
	return &s_eaNearbyPlayers;
}


void encounter_GetRewardedPlayers(int iPartitionIdx, GameEncounter *pEncounter, Entity ***peaRewardedPlayers)
{
	GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
	Entity *pEnt;
	int i;

	for(i=eaiSize(&pState->playerData.eauEntsWithCredit)-1; i>=0; --i) {
		pEnt = entFromEntityRef(iPartitionIdx, pState->playerData.eauEntsWithCredit[i]);
		if (pEnt && pEnt->pPlayer) {
			eaPush(peaRewardedPlayers, pEnt);
		}
	}
}


void encounter_GetAllSpawnedEntities(int iPartitionIdx, Entity ***peaEntities)
{
	int i;
	
	FOR_EACH_ENCOUNTER(pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		for(i=eaSize(&pState->eaEntities)-1; i>=0; --i) {
			eaPush(peaEntities, pState->eaEntities[i]);
		}
	} FOR_EACH_END;
}


GameEncounter **encounter_GetEncountersWithinDistance(const Vec3 vPos, F32 fDistance)
{
	static GameEncounter **eaNearbyEncs = NULL;

	PERFINFO_AUTO_START_FUNC();

	eaClear(&eaNearbyEncs);
	octreeFindInSphereEA(s_pOctree, &eaNearbyEncs, vPos, fDistance, NULL, NULL);

	PERFINFO_AUTO_STOP_FUNC();

	return eaNearbyEncs;
}


int encounter_GetNumEncounters(int iPartitionIdx)
{
	return eaSize(&s_eaEncounters);
}


int encounter_GetNumRunningEncounters(int iPartitionIdx)
{
	int iCount = 0;

	FOR_EACH_ENCOUNTER(pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		if ((pState->eState == EncounterState_Spawned) || (pState->eState == EncounterState_Active) || (pState->eState == EncounterState_Aware)) {
			++iCount;
		}
	} FOR_EACH_END;

	return iCount;
}


// Gets the FSM for the specified actor.  Checks world actor first, then encounter template actor.
FSM *encounter_GetActorFSM(int iPartitionIdx, GameEncounter *pEncounter, int iActorIndex) 
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	EncounterTemplate *pTemplate = pProps ? GET_REF(pProps->hTemplate) : NULL;
	FSM *pFSM = NULL;

	if (pProps && pProps->eaActors && iActorIndex >= 0 && iActorIndex < eaSize(&pProps->eaActors)) {
		WorldActorProperties *pWActor = pProps->eaActors[iActorIndex];

		if(pWActor) {
			if (pWActor->bOverrideFSM || GET_REF(pWActor->hFSMOverride)) {
				pFSM = GET_REF(pWActor->hFSMOverride);
			} else if (pTemplate) {
				EncounterActorProperties *pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, pProps, pWActor);
				if (pActor) {
					pFSM = encounterTemplate_GetActorFSM(pTemplate, pActor, iPartitionIdx);
				}
			}
		}
	}

	return pFSM;
}


F32 encounter_GetActorMaxFSMCost(int iPartitionIdx, GameEncounter *pEncounter, int iActorIndex)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	F32 fCost = 0;

	if (pProps && (iActorIndex < eaSize(&pProps->eaActors))) {
		FSM *pFSM = encounter_GetActorFSM(iPartitionIdx, pEncounter, iActorIndex);
		if (pFSM) {
			fCost += pFSM->cost;
		}
	}
	return fCost;
}


F32 encounter_GetPotentialFSMCost(int iPartitionIdx)
{
	WorldEncounterProperties *pProps;
	F32 fCost = 0;
	int i;

	FOR_EACH_ENCOUNTER(pEncounter) {
		pProps = pEncounter->pWorldEncounter->properties;
		if (pProps && pProps->eaActors) {
			for(i=eaSize(&pProps->eaActors)-1; i>=0; --i) {
				FSM *pFSM = encounter_GetActorFSM(iPartitionIdx, pEncounter, i);
				if (pFSM) {
					fCost += pFSM->cost;
				}
			}
		}
	} FOR_EACH_END;

	return fCost;
}


const char *encounter_GetFilename(GameEncounter *pEncounter)
{
	return layerGetFilename(pEncounter->pWorldEncounter->common_data.layer);
}


const char *encounter_GetName(GameEncounter *pEncounter)
{
	return pEncounter->pcName;
}


void encounter_GetContactLocations(ContactLocation ***peaLocations)
{
	FOR_EACH_ENCOUNTER(pEncounter) {
		WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
		EncounterTemplate *pTemplate = GET_REF(pProps->hTemplate);
		int i,j;

		if (pTemplate) {
			// Scan all actors
			for(i=eaSize(&pProps->eaActors)-1; i>=0; --i) {
				WorldActorProperties *pWorldActor = pProps->eaActors[i];
				EncounterActorProperties *pActor;
				WorldInteractionProperties* pInteractionProps = NULL;
				WorldInteractionPropertyEntry** eaInteractionEntries = NULL;
				bool bFound = false;

				if (pWorldActor && pWorldActor->pInteractionProperties) {
					// Scan all override interaction properties for the actor
					for(j=eaSize(&pWorldActor->pInteractionProperties->eaEntries)-1; j>=0; --j) {
						WorldInteractionPropertyEntry *pEntry = pWorldActor->pInteractionProperties->eaEntries[j];
						WorldContactInteractionProperties *pContactProps = interaction_GetContactProperties(pEntry);
						if (pContactProps) {
							// If it's a contact, save off its location
							ContactDef *pContact = GET_REF(pContactProps->hContactDef);
							if (pContact != NULL) {
								ContactLocation *pLocation = StructCreate(parse_ContactLocation);
								copyVec3(pWorldActor->vPos, pLocation->loc);
								pLocation->pchContactDefName = pContact->name;
								pLocation->pchStaticEncName = pEncounter->pcName;
								eaPush(peaLocations, pLocation);
								bFound = true;
							}
						}
					}
				}
				if (bFound)//If we found an override, we're done with this actor.
					continue;
				pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, pProps, pWorldActor);
				pInteractionProps = encounterTemplate_GetActorInteractionProps(pTemplate, pActor);
				encounterTemplate_FillActorInteractionEarray(pTemplate, pActor, &eaInteractionEntries);
				if (pActor && pInteractionProps) {
					// Scan all interaction properties for the actor
					for(j=eaSize(&eaInteractionEntries)-1; j>=0; --j) {
						WorldInteractionPropertyEntry *pEntry = eaInteractionEntries[j];
						WorldContactInteractionProperties *pContactProps = interaction_GetContactProperties(pEntry);
						if (pContactProps) {
							// If it's a contact, save off its location
							ContactDef *pContact = GET_REF(pContactProps->hContactDef);
							if (pContact != NULL) {
								ContactLocation *pLocation = StructCreate(parse_ContactLocation);
								copyVec3(pWorldActor->vPos, pLocation->loc);
								pLocation->pchContactDefName = pContact->name;
								pLocation->pchStaticEncName = pEncounter->pcName;
								eaPush(peaLocations, pLocation);
							}
						}
					}
				}
				eaDestroy(&eaInteractionEntries);
			}
		}
	} FOR_EACH_END;
}


void encounter_GetEncounterNames(char ***peaNames)
{
	FOR_EACH_ENCOUNTER(pEncounter) {
		eaPush(peaNames, strdup(pEncounter->pcName));
	} FOR_EACH_END;
}


void encounter_GetUsedEncounterTemplateNames(char ***peaNames)
{
	EncounterTemplate **eaTemplates = NULL;

	FOR_EACH_ENCOUNTER(pEncounter) {
		EncounterTemplate *pTemplate = GET_REF(pEncounter->pWorldEncounter->properties->hTemplate);
		if (pTemplate && (eaFind(&eaTemplates, pTemplate) == -1)) {
			eaPush(&eaTemplates, pTemplate);
			eaPush(peaNames, strdup(pTemplate->pcName));
		}
	} FOR_EACH_END;

	eaDestroy(&eaTemplates);
}


void encounter_GetPosition(GameEncounter *pEncounter, Vec3 vPos)
{
	copyVec3(pEncounter->pWorldEncounter->encounter_pos, vPos);
}


const char *encounter_GetPatrolRoute(GameEncounter *pEncounter)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	return pProps->pcPatrolRoute;
}


int encounter_GetNumActors(GameEncounter *pEncounter)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	return eaSize(&pProps->eaActors);
}


bool encounter_HasActorName(GameEncounter *pEncounter, const char *pcActorName)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	int i;

	for(i=eaSize(&pProps->eaActors)-1; i>=0; --i) {
		if (stricmp(pcActorName, pProps->eaActors[i]->pcName) == 0) {
			return true;
		}
	}

	return false;
}


bool encounter_IsNoDespawn(GameEncounter *pEncounter)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;

	if (pProps && pProps->pSpawnProperties) {
		return pProps->pSpawnProperties->bNoDespawn || 
			(pProps->pSpawnProperties->eMastermindSpawnType != WorldEncounterMastermindSpawnType_None); 
	}
	return false;
}


const char *encounter_GetActorName(GameEncounter *pEncounter, int iActorIndex)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;

	if (pProps && (iActorIndex < eaSize(&pProps->eaActors))) {
		return pProps->eaActors[iActorIndex]->pcName;
	}
	return NULL;
}


bool encounter_GetActorPosition(GameEncounter *pEncounter, int iActorIndex, Vec3 vPos, Vec3 vRot)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;

	if (pProps && (iActorIndex < eaSize(&pProps->eaActors))) {
		if (vPos) {
			copyVec3(pProps->eaActors[iActorIndex]->vPos, vPos);
		}
		if (vRot) {
			copyVec3(pProps->eaActors[iActorIndex]->vRot, vRot);
		}
		return true;
	}
	return false;
}


static EncounterActorProperties *encounter_GetActorPropsByIndex(GameEncounter *pEncounter, int iActorIndex)
{
	WorldEncounterProperties *pProps;
	EncounterActorProperties *pActor = NULL;

	PERFINFO_AUTO_START_FUNC();
	
	pProps = pEncounter->pWorldEncounter->properties;

	if (pProps && (iActorIndex < eaSize(&pProps->eaActors))) {
		EncounterTemplate *pTemplate = GET_REF(pProps->hTemplate);
		if (pTemplate) {
			pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, pProps, pProps->eaActors[iActorIndex]);
		}
	}

	PERFINFO_AUTO_STOP();

	return pActor;
}


int encounter_GetActorNumInteractionEntries(GameEncounter *pEncounter, int iActorIndex, Critter *pCritter)
{
	EncounterActorProperties *pActor;
	EncounterTemplate* pTemplate;
	WorldInteractionProperties* pInteractionProps = NULL;
	WorldInteractionPropertyEntry** eaInteractionEntries = NULL;
	int iNumEntries = 0;

	PERFINFO_AUTO_START_FUNC();

	// Use cached value on the critter when possible
	if (pCritter && pCritter->encounterData.iNumInteractionEntries >= 0) {
		return pCritter->encounterData.iNumInteractionEntries;
	}

	pActor = encounter_GetActorPropsByIndex(pEncounter, iActorIndex);
	pTemplate = GET_REF(pEncounter->pWorldEncounter->properties->hTemplate);

	pInteractionProps = encounterTemplate_GetActorInteractionProps(pTemplate, pActor);
	encounterTemplate_FillActorInteractionEarray(pTemplate, pActor, &eaInteractionEntries);

	if (pActor && pInteractionProps && eaInteractionEntries) {
		iNumEntries += eaSize(&eaInteractionEntries);
	}
	
	// World Actor
	if(pEncounter && pEncounter->pWorldEncounter && pEncounter->pWorldEncounter->properties)
	{
		WorldEncounterProperties *pWorldEncProps = pEncounter->pWorldEncounter->properties;
		if(pWorldEncProps && pWorldEncProps->eaActors && iActorIndex >= 0 && iActorIndex < eaSize(&pWorldEncProps->eaActors))
		{
			WorldActorProperties *pWorldActor = pWorldEncProps->eaActors[iActorIndex];
			if(pWorldActor && pWorldActor->pInteractionProperties && pWorldActor->pInteractionProperties->eaEntries)
			{
				iNumEntries += eaSize(&pWorldActor->pInteractionProperties->eaEntries);
			}
		}
	}
	eaDestroy(&eaInteractionEntries);

	// Cache on the critter when possible (which is not possible if edits can be done by designers)
	if (pCritter && !isDevelopmentMode()) {
		pCritter->encounterData.iNumInteractionEntries = iNumEntries;
	}

	PERFINFO_AUTO_STOP();

	return iNumEntries;
}


WorldInteractionPropertyEntry *encounter_GetActorInteractionEntry(GameEncounter *pEncounter, int iActorIndex, int iInteractIndex)
{
	EncounterActorProperties *pActor;
	WorldEncounterProperties *pWorldEncProps;
	WorldActorProperties *pWorldActor;
	EncounterTemplate* pTemplate;
	WorldInteractionProperties* pInteractionProps = NULL;
	WorldInteractionPropertyEntry** eaInteractionEntries = NULL;
	WorldInteractionPropertyEntry *pReturnEntry = NULL;
	bool bFoundEntry = false;

	PERFINFO_AUTO_START_FUNC();

	pActor = encounter_GetActorPropsByIndex(pEncounter, iActorIndex);
	pWorldEncProps = pEncounter && pEncounter->pWorldEncounter ? pEncounter->pWorldEncounter->properties : NULL;
	pWorldActor = (pWorldEncProps && pWorldEncProps->eaActors && iActorIndex >= 0 && iActorIndex < eaSize(&pWorldEncProps->eaActors)) ? pWorldEncProps->eaActors[iActorIndex] : NULL;
	pTemplate = SAFE_GET_REF(pWorldEncProps, hTemplate);

	pInteractionProps = encounterTemplate_GetActorInteractionProps(pTemplate, pActor);
	encounterTemplate_FillActorInteractionEarray(pTemplate, pActor, &eaInteractionEntries);

	// Check ET Actor
	if (pActor && pInteractionProps && (iInteractIndex >= 0)) {
		if(iInteractIndex < eaSize(&eaInteractionEntries)) {
			pReturnEntry = eaInteractionEntries[iInteractIndex];
			bFoundEntry = true;
		} else {
			iInteractIndex -= eaSize(&eaInteractionEntries);
		}
	}	

	// Check World Actor if we don't have a return entry yet
	if(!bFoundEntry && pWorldActor && pWorldActor->pInteractionProperties && 
		iInteractIndex >= 0 && iInteractIndex < eaSize(&pWorldActor->pInteractionProperties->eaEntries)) {
			pReturnEntry = pWorldActor->pInteractionProperties->eaEntries[iInteractIndex];
			bFoundEntry = true;
	}

	eaDestroy(&eaInteractionEntries);

	PERFINFO_AUTO_STOP();
	return pReturnEntry;
}


Entity *encounter_GetActorEntity(int iPartitionIdx, GameEncounter *pEncounter, const char *pcActorName)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	int i, j;

	for(i=eaSize(&pProps->eaActors)-1; i>=0; --i) {
		if (stricmp(pcActorName, pProps->eaActors[i]->pcName) == 0) {
			GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
			for(j=eaSize(&pState->eaEntities)-1; j>=0; --j) {
				Entity *pEnt = pState->eaEntities[j];
				if (pEnt->pCritter && (pEnt->pCritter->encounterData.iActorIndex == i)) {
					return pEnt;
				}
			}
			return NULL;
		}
	}

	return NULL;
}


void encounter_GetEntities(int iPartitionIdx, GameEncounter *pEncounter, Entity ***peaEntities, bool bFilter, bool bIncludeDead)
{
	GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
	int i;

	for(i=eaSize(&pState->eaEntities)-1; i>=0; --i) {
		Entity *pEnt = pState->eaEntities[i];
		if(pEnt && 
			(bIncludeDead || entIsAlive(pEnt)) &&
			(!bFilter || !exprFuncHelperShouldExcludeFromEntArray(pEnt))) {
			eaPush(peaEntities, pEnt);
		}
	}
}


EncounterTemplate *encounter_GetTemplate(GameEncounter *pEncounter)
{
	WorldEncounterProperties *pProps = SAFE_MEMBER2(pEncounter, pWorldEncounter, properties);
	if(pProps) {
		return GET_REF(pProps->hTemplate);
	}
	return NULL;
}


void encounter_GatherBeaconPositions(void)
{
	WorldEncounterProperties *pProps;
	char idString[1024];
	int i;

	FOR_EACH_ENCOUNTER(pEncounter) {
		pProps = pEncounter->pWorldEncounter->properties;
		if (pProps) {
			for(i=eaSize(&pProps->eaActors)-1; i>=0; --i) {
				sprintf(idString, "%s:%d", pEncounter->pcName, i);
				beaconAddCritterSpawn(pProps->eaActors[i]->vPos, idString);
			}
		}
	} FOR_EACH_END;
}


static void encounter_GetEncounterActorPosition(GameEncounter *pEncounter, int iActorIndex, Vec3 vOutVec, Quat qOutQuat)
{
	WorldActorProperties *pActor = NULL;

	if ((iActorIndex >= 0) && (iActorIndex < eaSize(&pEncounter->pWorldEncounter->properties->eaActors))) {
		pActor = pEncounter->pWorldEncounter->properties->eaActors[iActorIndex];
	}

	if (vOutVec) {
		if (pActor) {
			copyVec3(pActor->vPos, vOutVec);
		} else {
			copyVec3(pEncounter->pWorldEncounter->encounter_pos, vOutVec);
		}
	}

	if (qOutQuat) {
		if (pActor) {
			PYRToQuat(pActor->vRot, qOutQuat);
		} else {
			PYRToQuat(pEncounter->pWorldEncounter->encounter_rot, qOutQuat);
		}
	}
}


static const char *encounter_GetEncounterActorName(GameEncounter *pEncounter, int iActorIndex)
{
	if ((iActorIndex >= 0) && (iActorIndex < eaSize(&pEncounter->pWorldEncounter->properties->eaActors))) {
		return pEncounter->pWorldEncounter->properties->eaActors[iActorIndex]->pcName;
	} else {
		return "UnnamedActor";
	}
}


static bool encounter_ActorIsCombatant(GameEncounter *pEncounter, int iActorIndex)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	EncounterTemplate *pTemplate;

	if (pProps && (iActorIndex < eaSize(&pProps->eaActors))) {
		pTemplate = GET_REF(pProps->hTemplate);
		if (pTemplate) {
			EncounterActorProperties *pActor = encounterTemplate_GetActorFromWorldActor(pTemplate, pProps, pProps->eaActors[iActorIndex]);
			if (!encounterTemplate_GetActorIsCombatant(pTemplate,pActor)) {
				return false;
			}
		}
	}
	return true;
}


void encounter_AddActor(int iPartitionIdx, GameEncounter *pEncounter, int iActorIndex, Entity *pEntity, int iTeamSize, int iTeamLevel)
{
	if (pEntity && pEntity->pCritter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		Critter *pCritter = pEntity->pCritter;
		pCritter->encounterData.activeTeamSize = iTeamSize;
		pCritter->encounterData.activeTeamLevel = iTeamLevel;
		pCritter->encounterData.iActorIndex = iActorIndex;
		pCritter->encounterData.bNonCombatant = !encounter_ActorIsCombatant(pEncounter, iActorIndex);
		encounter_GetEncounterActorPosition(pEncounter, iActorIndex, pCritter->encounterData.origPos, NULL);
		pCritter->encounterData.pGameEncounter = pEncounter;
		pCritter->encounterData.bEnableSpawnAggroDelay = s_bEnableSpawnAggroDelay;
		pCritter->encounterData.iNumInteractionEntries = -1;
		entity_SetDirtyBit(pEntity, parse_Critter, pCritter, false);
		pCritter->iEncounterKey = pEncounter && pEncounter->pcName ? hashString(pEncounter->pcName,true) : 0;
		eaPush(&pState->eaEntities, pEntity);
	}
}


void encounter_RemoveActor(Entity *pEnt)
{
	if (pEnt->pCritter) {
		if (pEnt->pCritter->encounterData.pGameEncounter) {
			GameEncounterPartitionState *pState = encounter_GetPartitionState(entGetPartitionIdx(pEnt), pEnt->pCritter->encounterData.pGameEncounter);
			eaFindAndRemove(&pState->eaEntities, pEnt);
		}
		pEnt->pCritter->encounterData.iActorIndex = 0;
		pEnt->pCritter->encounterData.pGameEncounter = NULL;
		// Destroy pGameEventInfo here because some of the fields in GameEventParticipant
		// are set from data in pGameEncounter (see eventsend_GetOrCreateEntityInfo)
		StructDestroySafe(parse_GameEventParticipant, &pEnt->pGameEventInfo);
	}
}


void encounter_MoveCritterToSpawn(Entity *pEnt)
{
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);

	if (pEnt && pEnt->pCritter) {
		if (pEnt->pCritter->encounterData.pGameEncounter) {
			WorldEncounterProperties *pProps = pEnt->pCritter->encounterData.pGameEncounter->pWorldEncounter->properties;
			int iIndex = pEnt->pCritter->encounterData.iActorIndex;
			if (pProps && (iIndex >= 0) && (iIndex < eaSize(&pProps->eaActors))) {
				Quat qRot;
				PYRToQuat(pProps->eaActors[iIndex]->vRot, qRot);

				entSetPos(pEnt, pProps->eaActors[iIndex]->vPos, true, __FUNCTION__);
				entSetRot(pEnt, qRot, true, __FUNCTION__);
			}
		} else if (gConf.bAllowOldEncounterData && pEnt->pCritter->encounterData.parentEncounter && pEnt->pCritter->encounterData.sourceActor) {
			Vec3 vPos;
			Quat qRot;
			OldEncounter *pEncounter = pEnt->pCritter->encounterData.parentEncounter;
			OldActor *pActor = pEnt->pCritter->encounterData.sourceActor;
			oldencounter_GetEncounterActorPosition(pEncounter, pActor, vPos, qRot);
			entSetPos(pEnt, vPos, true, __FUNCTION__);
			entSetRot(pEnt, qRot, true, __FUNCTION__);
		}
	}
	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}


static void encounter_TriggerActions(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;

	if (pProps && pProps->pEventProperties) {
		if (pState->eState == EncounterState_Success) {
			encounter_ExprEvaluate(pState->iPartitionIdx, pEncounter, pProps->pEventProperties->pSuccessExpr);
		} else if (pState->eState == EncounterState_Failure) {
			encounter_ExprEvaluate(pState->iPartitionIdx, pEncounter, pProps->pEventProperties->pFailureExpr);
		}
	}
}


// Transition an encounter to its new state
static void encounter_StateTransition(GameEncounter *pEncounter, GameEncounterPartitionState *pState, EncounterState eNewState)
{
	if (pState->eState != eNewState) {
		pState->eState = eNewState;
		encounter_TriggerActions(pEncounter, pState);
		eventsend_Encounter2StateChange(pEncounter, pState, eNewState);

		if (eNewState == EncounterState_Waiting || eNewState == EncounterState_Spawned || eNewState == EncounterState_GroupManaged) {
			pState->playerData.uWaitingForInitializationTimestamp = 0;
		}

		if (eNewState == EncounterState_Active) {
			gslEntityPresence_OnEncounterActivate(pState->iPartitionIdx, pEncounter);
		}

		if (pEncounter->bIsDynamicMastermind && ((eNewState == EncounterState_Waiting) || (eNewState == EncounterState_Asleep))) {
			pState->eState = EncounterState_Off;
		}
	}
}


static bool encounter_IsDynamicSpawn(GameEncounter *pEncounter)
{
	WorldEncounterDynamicSpawnType eDynamicSpawnType;
	WorldEncounterSpawnProperties* pSpawn = SAFE_MEMBER3(pEncounter, pWorldEncounter, properties, pSpawnProperties);
	if (pSpawn)
	{
		eDynamicSpawnType = pSpawn->eDyamicSpawnType;
	}
	else
	{
		if (gConf.bLegacyEncounterDynamicSpawnType)
			eDynamicSpawnType = WorldEncounterDynamicSpawnType_Static;
		else
			eDynamicSpawnType = WorldEncounterDynamicSpawnType_Default;
	}

	if (eDynamicSpawnType == WorldEncounterDynamicSpawnType_Dynamic
		|| (eDynamicSpawnType == WorldEncounterDynamicSpawnType_Default && (zmapInfoGetMapType(NULL) == ZMTYPE_STATIC || zmapInfoGetMapType(NULL) == ZMTYPE_SHARED))){
			return true;
	}
	return false;
}

static EncounterDifficulty encounter_GetDifficulty(int iPartitionIdx, GameEncounter* pEncounter)
{
	EncounterDifficulty eDifficulty = 0;
	WorldEncounterProperties* pEncProps = pEncounter && pEncounter->pWorldEncounter ? pEncounter->pWorldEncounter->properties : NULL;
	EncounterTemplate* pTemplate = pEncProps ? GET_REF(pEncProps->hTemplate) : NULL;
	WorldEncounterSpawnProperties* pSpawnProps = pEncProps ? pEncProps->pSpawnProperties : NULL;

	if (pTemplate) {
		eDifficulty = encounterTemplate_GetDifficulty(pTemplate, iPartitionIdx);
	}

	if (pSpawnProps) {
		eDifficulty += pSpawnProps->iDifficultyOffset;
	}

	return eDifficulty;
}

static WorldEncounterRewardProperties* encounter_GetRewardProperties(GameEncounter* pEncounter)
{
	WorldEncounterRewardProperties* pRewardProps = NULL;
	if (pEncounter) {
		WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;

		if (pProps) {
			pRewardProps = pProps->pRewardProperties;
		}
	}

	return pRewardProps;
}

static F32 encounter_GetOverrideSendDistance(GameEncounter* pEncounter)
{
	F32 fOverrideSendDistance = 0.0f;
	if (pEncounter) {
		WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;

		// Check world actor
		if (pProps && pProps->fOverrideSendDistance > FLT_EPSILON) {
			fOverrideSendDistance = pProps->fOverrideSendDistance;
		} else if (pProps) {
			// Check template
			EncounterTemplate* pTemplate = GET_REF(pProps->hTemplate);
			EncounterActorSharedProperties* pSharedProps = encounterTemplate_GetActorSharedProperties(pTemplate);
			if (pSharedProps && pSharedProps->fOverrideSendDistance > FLT_EPSILON) {
				fOverrideSendDistance = pSharedProps->fOverrideSendDistance;
			}
		}
	}
	return fOverrideSendDistance;
}

// ----------------------------------------------------------------------------------
// Encounter FSM Variable
// ----------------------------------------------------------------------------------

static WorldVariable *encounter_AddCachedActorVariableFromDef(SA_PARAM_NN_VALID GameEncounter *pEncounter, SA_PARAM_NN_VALID GameEncounterPartitionState *pState, SA_PARAM_NN_VALID WorldVariableDef* pDef, U32 iActorIndex)
{
	GameEncounterCachedActorVar* pCachedVar;
	WorldVariable *pVar;
	
	// Get the variable value
	pVar = worldVariableCalcVariableAndAlloc(pState->iPartitionIdx, pDef, NULL, randomInt(), 0);

	if (pVar) {
		pCachedVar = StructCreate(parse_GameEncounterCachedActorVar);
		pCachedVar->iActorIndex = iActorIndex;
		pCachedVar->pVariable = StructClone(parse_WorldVariable, pVar);
		assert(pCachedVar->pVariable);
		if (pCachedVar->pVariable->pcName != pDef->pcName) {
			pCachedVar->pVariable->pcName = pDef->pcName;
		}
		if (pCachedVar->pVariable->eType != pDef->eType) {
			pCachedVar->pVariable->eType = pDef->eType;
		}
		eaPush(&pState->eaCachedVars, pCachedVar);
		StructDestroy(parse_WorldVariable, pVar);
		return pCachedVar->pVariable;
	} else {
		Errorf("Unable to calculate a value from world variable def: %s in %s encounter", pDef ? pDef->pcName : "", pEncounter ? pEncounter->pcName : NULL);
	}

	StructDestroy(parse_WorldVariable, pVar);
	return NULL;
}


static void encounter_AddCachedActorVariable(SA_PARAM_NN_VALID GameEncounter *pEncounter, SA_PARAM_NN_VALID GameEncounterPartitionState *pState, SA_PARAM_NN_VALID WorldVariable* pVar, U32 iActorIndex)
{
	GameEncounterCachedActorVar* pCachedVar;
	
	pCachedVar = StructCreate(parse_GameEncounterCachedActorVar);
	pCachedVar->iActorIndex = iActorIndex;
	pCachedVar->pVariable = StructClone(parse_WorldVariable, pVar);
	eaPush(&pState->eaCachedVars, pCachedVar);
}


static WorldVariable *encounter_LookupActorVariable(int iPartitionIdx, GameEncounter *pEncounter, U32 iActorIndex, Entity *pCritterEnt, const char *pcVarName)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	GameEncounterPartitionState *pState;
	int i;

	if (!pcVarName) {
		return NULL;
	}

	pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);

	// First Check cached vars
	if (pState->eaCachedVars) {
		for(i = eaSize(&pState->eaCachedVars)-1; i >= 0; i--) {
			GameEncounterCachedActorVar *pVar = pState->eaCachedVars[i];
			if (pVar->pVariable && (pVar->iActorIndex == iActorIndex) && !stricmp(pVar->pVariable->pcName,pcVarName)) {
				return pVar->pVariable;
			}
		}
	}

	// Then check world layer actor
	if (pProps && pProps->eaActors && iActorIndex >= 0 && iActorIndex < (U32)eaSize(&pProps->eaActors)) {
		WorldActorProperties *pWActor = pProps->eaActors[iActorIndex];
		if (pWActor && pWActor->eaFSMVariableDefs) {
			for(i = eaSize(&pWActor->eaFSMVariableDefs)-1; i>= 0; i--) {
				WorldVariableDef *pVarDef = pWActor->eaFSMVariableDefs[i];
				if (pVarDef->pcName && (stricmp(pVarDef->pcName, pcVarName) == 0)) {
					return encounter_AddCachedActorVariableFromDef(pEncounter, pState, pVarDef, iActorIndex);
				}
			}
		}
	

		if (pEncounter->pCachedTemplate && pWActor) {
			EncounterActorProperties *pActor = encounterTemplate_GetActorFromWorldActor(pEncounter->pCachedTemplate, pProps, pWActor);
			EncounterAIProperties *pAIProperties = encounterTemplate_GetAIProperties(pEncounter->pCachedTemplate);
			WorldVariableDef** eaActorFSMVarDefs = NULL;
			WorldVariableDef** eaEncounterFSMVarDefs = NULL;
			encounterTemplate_GetActorFSMVarDefs(pEncounter->pCachedTemplate, pActor, &eaActorFSMVarDefs, NULL);
			encounterTemplate_GetEncounterFSMVarDefs(pEncounter->pCachedTemplate, &eaEncounterFSMVarDefs, NULL);

			// Then check the actor on the template
			if (pActor) {
				for(i=eaSize(&eaActorFSMVarDefs)-1; i>=0; --i) {
					WorldVariableDef *pVarDef = eaActorFSMVarDefs[i];
					if (pVarDef->pcName && (stricmp(pVarDef->pcName, pcVarName) == 0)) {
						return encounter_AddCachedActorVariableFromDef(pEncounter, pState, pVarDef, iActorIndex);
					}
				}
			}
		
	
			// Then check the encounter template
			if (pAIProperties && (pAIProperties->eFSMType == EncounterCritterOverrideType_Specified)) {
				for(i=eaSize(&eaEncounterFSMVarDefs)-1; i>=0; --i) {
					WorldVariableDef *pVarDef = eaEncounterFSMVarDefs[i];
					if (pVarDef->pcName && (stricmp(pVarDef->pcName, pcVarName) == 0)) {
						return encounter_AddCachedActorVariableFromDef(pEncounter, pState, pVarDef, iActorIndex);
					}
				}
			}
			eaDestroy(&eaActorFSMVarDefs);
			eaDestroy(&eaEncounterFSMVarDefs);
		}
	}

	if (pCritterEnt && pCritterEnt->pCritter) {
		// Then check the critter def
		CritterDef *pDef = GET_REF(pCritterEnt->pCritter->critterDef);
		CritterGroup *pGroup;
		if (pDef) {
			for(i=eaSize(&pDef->ppCritterVars)-1; i>=0; --i) {
				WorldVariable *pVar = &pDef->ppCritterVars[i]->var;
				if (pVar->pcName && (stricmp(pVar->pcName, pcVarName) == 0)) {
					encounter_AddCachedActorVariable(pEncounter, pState, pVar, iActorIndex);
					return pVar;
				}
			}

			// Then check the critter group
			pGroup = GET_REF(pDef->hGroup);
			if (pGroup) {
				for(i=eaSize(&pGroup->ppCritterVars)-1; i>=0; --i) {
					WorldVariable *pVar = &pGroup->ppCritterVars[i]->var;
					if (pVar->pcName && (stricmp(pVar->pcName, pcVarName) == 0)) {
						encounter_AddCachedActorVariable(pEncounter, pState, pVar, iActorIndex);
						return pVar;
					}
				}
			}
		}
	}

	return NULL;
}


static void encounter_GetAllActorVariables(int iPartitionIdx, GameEncounter *pEncounter, int iActorIndex, WorldVariable ***peaVars)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	int i;

	if (pProps && pProps->eaActors && iActorIndex >= 0 && iActorIndex < eaSize(&pProps->eaActors)) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);

		// First check the world layer actor
		WorldActorProperties *pWActor = pProps->eaActors[iActorIndex];
		if (pWActor && pWActor->eaFSMVariableDefs) {
			for(i = eaSize(&pWActor->eaFSMVariableDefs)-1; i>= 0; i--) {
				WorldVariableDef *pVarDef = pWActor->eaFSMVariableDefs[i];
				encounterTemplate_AddVarIfNotPresent(NULL, peaVars, encounter_AddCachedActorVariableFromDef(pEncounter, pState, pVarDef, iActorIndex));
			}
		}


		if (pEncounter->pCachedTemplate && pWActor) {
			EncounterActorProperties *pActor = encounterTemplate_GetActorFromWorldActor(pEncounter->pCachedTemplate, pProps, pWActor);
			EncounterAIProperties *pAIProperties = encounterTemplate_GetAIProperties(pEncounter->pCachedTemplate);
			WorldVariableDef** eaActorFSMVarDefs = NULL;
			WorldVariableDef** eaEncounterFSMVarDefs = NULL;
			encounterTemplate_GetActorFSMVarDefs(pEncounter->pCachedTemplate, pActor, &eaActorFSMVarDefs, NULL);
			encounterTemplate_GetEncounterFSMVarDefs(pEncounter->pCachedTemplate, &eaEncounterFSMVarDefs, NULL);
			
			// Then check the actor on the template
			if (pActor) {
				for(i=eaSize(&eaActorFSMVarDefs)-1; i>=0; --i) {
					encounterTemplate_AddVarIfNotPresent(NULL, peaVars, encounter_AddCachedActorVariableFromDef(pEncounter, pState, eaActorFSMVarDefs[i], iActorIndex));
				}
			}

			// Then check the encounter template
			if (pAIProperties && (pAIProperties->eFSMType == EncounterCritterOverrideType_Specified)) {
				for(i=eaSize(&eaEncounterFSMVarDefs)-1; i>=0; --i) {
					encounterTemplate_AddVarIfNotPresent(NULL, peaVars, encounter_AddCachedActorVariableFromDef(pEncounter, pState, eaEncounterFSMVarDefs[i], -1));
				}
			}
			eaDestroy(&eaActorFSMVarDefs);
			eaDestroy(&eaEncounterFSMVarDefs);
		}
	}
}


static MultiVal *encfsm_AIExternVarLookup(ExprContext *pContext, const char *pcVarName)
{
	static MultiVal mvFailedLookupVal = {0};

	Entity *pCritterEnt = exprContextGetVarPointerUnsafe(pContext, "Me");
	CritterDef *pCritterDef;
	CritterGroup *pCritterGroup;

	if (pCritterEnt && pCritterEnt->pCritter) {
		int iPartitionIdx = entGetPartitionIdx(pCritterEnt);

		if (pCritterEnt->pCritter->encounterData.pGameEncounter) {
			// This path checks the actor, encounter, critter, and critter group if the actor is found
			GameEncounter *pEncounter = pCritterEnt->pCritter->encounterData.pGameEncounter;
			WorldVariable *pActorVar = encounter_LookupActorVariable(iPartitionIdx, pEncounter, pCritterEnt->pCritter->encounterData.iActorIndex, pCritterEnt, pcVarName);
			if (pActorVar) {
				MultiVal *pMval = exprContextAllocScratchMemory(pContext, sizeof(MultiVal) );
				worldVariableToMultival(pContext, pActorVar, pMval);
				return pMval;
			}
		} else {
			if (gConf.bAllowOldEncounterData && pCritterEnt->pCritter->encounterData.parentEncounter) {
				OldActorAIInfo* actorAIInfo = oldencounter_GetActorAIInfo(pCritterEnt->pCritter->encounterData.sourceActor);
				OldEncounterVariable* actorVar = oldencounter_LookupActorVariable(actorAIInfo, pcVarName);
				if (actorVar) {
					int numSubVars = eaSize(&actorVar->parsedStrVals);
					if (numSubVars) {
						return actorVar->parsedStrVals[randInt(numSubVars)];
					}
					return &actorVar->varValue;
				}
			}
			// Get here if the critter is not part of an encounter
			// Or for old encounters it does it all the time
			pCritterDef = GET_REF(pCritterEnt->pCritter->critterDef);
			if (pCritterDef) {
				int i;

				for(i=eaSize(&pCritterDef->ppCritterVars)-1; i>=0; --i) {
					CritterVar *pVar = pCritterDef->ppCritterVars[i];
					if (stricmp(pcVarName, pVar->var.pcName) == 0) {
						MultiVal *pMval = exprContextAllocScratchMemory(pContext, sizeof(MultiVal) );
						worldVariableToMultival(pContext, &pVar->var, pMval);
						return pMval;
					}
				}

				//var not found on critter, see if it is on crittergroup
				pCritterGroup = GET_REF(pCritterDef->hGroup);
				if (pCritterGroup) {
					for(i=eaSize(&pCritterGroup->ppCritterVars)-1; i>=0; --i) {
						CritterVar *pVar = pCritterGroup->ppCritterVars[i];
						if ( stricmp(pcVarName, pVar->var.pcName) == 0 ) {
							MultiVal *pMval = exprContextAllocScratchMemory(pContext, sizeof(MultiVal) );
							worldVariableToMultival(pContext, &pVar->var, pMval);
							return pMval;
						}
					}
				}
			}
		}
	}

	if (!mvFailedLookupVal.str) {
		MultiValSetString(&mvFailedLookupVal, "");
	}
	return &mvFailedLookupVal;
}


void encfsm_GetAllExternVars(Entity *pEnt, ExprContext *pContext, WorldVariable ***peaVars, OldEncounterVariable ***peaOldVars, CritterVar ***peaCritterVars, CritterVar ***peaGroupVars)
{
	Entity *pCritterEnt = exprContextGetVarPointerUnsafe(pContext, "Me");
	CritterDef *pCritterDef;

	if (pCritterEnt && pCritterEnt->pCritter) {
		CritterGroup *pGroup;
		int iPartitionIdx = entGetPartitionIdx(pCritterEnt);
		if (pCritterEnt->pCritter->encounterData.pGameEncounter) {
			// Get the actor's FSM variables
			encounter_GetAllActorVariables(iPartitionIdx, pCritterEnt->pCritter->encounterData.pGameEncounter, pCritterEnt->pCritter->encounterData.iActorIndex, peaVars);
		}
		else if (gConf.bAllowOldEncounterData && pCritterEnt->pCritter->encounterData.parentEncounter) {
			// Get the actor's FSM variables
			OldActorAIInfo* actorAIInfo = oldencounter_GetActorAIInfo(pCritterEnt->pCritter->encounterData.sourceActor);
			oldencounter_GetAllActorVariables(actorAIInfo, peaOldVars);
		}

		// Get the critter variables
		pCritterDef = GET_REF(pCritterEnt->pCritter->critterDef);
		if (pCritterDef) {
			eaPushEArray(peaCritterVars, &pCritterDef->ppCritterVars);

			// Get Critter group vars
			pGroup = GET_REF(pCritterDef->hGroup);
			if(pGroup)
				eaPushEArray(peaGroupVars, &pGroup->ppCritterVars);
		}
	}
}	


static void encfsm_AddAIVarCallbacks(Entity *pEnt, ExprContext *pContext)
{
	exprContextAddExternVarCategory(pContext, "Encounter", fsmRegisterExternVar, encfsm_AIExternVarLookup, fsmRegisterExternVarSCType);
}


static void encounter_GetAIStateChangeNotification(Entity *pEnt, const char *pcOldState, const char *pcNewState)
{
	// This sometimes gets called within transactions due to critter_CreateByDef being called in them
	if (entGetPartitionIdx(pEnt) != PARTITION_IN_TRANSACTION) {
		eventsend_EncounterActorStateChange(pEnt, (char*)pcNewState);
	}
}


// ----------------------------------------------------------------------------------
// Encounter Runtime Processing Logic
// ----------------------------------------------------------------------------------

static void encounter_UpdateDynamicSpawnRate(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	PerfInfoGuard* piGuardFunc;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuardFunc);

	if (pEncounter->bIsDynamicSpawn && 
		pState->timerData.fSpawnTimer > 0 && 
		(pState->eState == EncounterState_Success || pState->eState == EncounterState_Failure || pState->eState == EncounterState_Off) && 
		g_EnableDynamicRespawn) {

		WorldEncounterProperties* pProperties = pEncounter->pWorldEncounter ? pEncounter->pWorldEncounter->properties : NULL;
		int iNumEnabledEncs = 0, iNumOnCooldown = 0;

		if (pProperties) {
			GameEncounter** eaNearbyEncs;

			PERFINFO_AUTO_START("CheckingDynamicRespawn", 1);

			eaNearbyEncs = encounter_GetEncountersWithinDistance(pEncounter->vPos, pEncounter->fSpawnRadius);

			if (eaSize(&eaNearbyEncs)) {
				int iPartitionIdx = pState->iPartitionIdx;
				CritterFaction *pFaction = encounterTemplate_GetMajorityFaction(pEncounter->pCachedTemplate, iPartitionIdx);
				U32 iGangID = encounterTemplate_GetMajorityGangID(pEncounter->pCachedTemplate, iPartitionIdx);
				int i;

				for (i = eaSize(&eaNearbyEncs)-1; i >= 0; --i) {
					GameEncounter *pOtherEnc = eaNearbyEncs[i];
					CritterFaction *pOtherFaction;
					U32 iOtherGangID;

					if ((pOtherEnc != pEncounter) && (!pOtherEnc->pCachedTemplate) && pOtherEnc->bIsDynamicSpawn) {
						continue;
					}

					pOtherFaction = encounterTemplate_GetMajorityFaction(pOtherEnc->pCachedTemplate, iPartitionIdx);
					iOtherGangID = encounterTemplate_GetMajorityGangID(pOtherEnc->pCachedTemplate, iPartitionIdx);

					if (((pFaction || pOtherFaction) && pFaction == pOtherFaction) || 
						((iGangID || iOtherGangID) && iGangID == iOtherGangID) )
					{
						GameEncounterPartitionState *pNearbyState = encounter_GetPartitionState(iPartitionIdx, pOtherEnc);
						if (pNearbyState->timerData.fSpawnTimer > 0 && (pNearbyState->eState == EncounterState_Success || pNearbyState->eState == EncounterState_Failure)) {
							iNumOnCooldown++;
						}
						if (pNearbyState->eState != EncounterState_Off && pNearbyState->eState != EncounterState_Disabled && pNearbyState->eState != EncounterState_Waiting) {
							iNumEnabledEncs++;
						}
					}
				}
			}

			PERFINFO_AUTO_STOP(); // CheckingDynamicRespawn
		}

		// Hack to smooth out behavior with small numbers of encounters
		// If there are less than 5 encounters nearby, always act as if there are at least 5
		if (iNumEnabledEncs < 5){
			iNumEnabledEncs = 5;
		}

		if (iNumEnabledEncs && g_fDynamicRespawnScale > 1.f){
			F32 fRatio = ((F32)iNumOnCooldown/(F32)iNumEnabledEncs);
			pState->timerData.fSpawnRateMultiplier = 1.f + (g_fDynamicRespawnScale-1.f)*fRatio;
		} else {
			pState->timerData.fSpawnRateMultiplier = 0.f;
		}
	} else {
		pState->timerData.fSpawnRateMultiplier = 0.f;
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
}


void encounter_SetNearbyTeamsize(Entity* pEnt) 
{
	if (pEnt && pEnt->pTeam && pEnt->pTeam->iNearbyTeamSize) {
		U32 uCurrentTime;
		U32 uPlayerZoneTime = 0;
		PartitionEncounterData *pData;

		PERFINFO_AUTO_START_FUNC();
		
		pData = encounter_GetPartitionData(entGetPartitionIdx(pEnt));
		uCurrentTime = timeSecondsSince2000();

		// Did the player arrive at the right map, and within the first NEARBY_TEAMMATE_COUNT_LIFESPAN seconds of the map starting
		
		// re-entering a map will not re-set s_uStartTime, use the time the player left the map or the start time (whatever is greater) 
		// to fix whole team re-entrance issue of incorrect team size
		if (pEnt->pTeam->iAverageTeamLevel > 0) {
			uPlayerZoneTime = pEnt->pTeam->iAverageTeamLevelTime - AVERAGE_PARTY_LEVEL_EXPIRE_TIME_SECONDS;
		}
		if (pEnt->pTeam->pchDestinationMap && !stricmp(pEnt->pTeam->pchDestinationMap, zmapInfoGetPublicName(NULL)) && (uCurrentTime - max(pData->uStartTime, uPlayerZoneTime) < NEARBY_TEAMMATE_COUNT_LIFESPAN) ) {
			pData->uNearbyTeamSize = pEnt->pTeam->iNearbyTeamSize;
		}
		pEnt->pTeam->iNearbyTeamSize = 0;
		pEnt->pTeam->pchDestinationMap = NULL;
		entity_SetDirtyBit(pEnt, parse_PlayerTeam, pEnt->pTeam, true);

		PERFINFO_AUTO_STOP();
	}
}


bool encounter_isMastermindType(GameEncounter *pEncounter)
{
	return	pEncounter && 
			pEncounter->pWorldEncounter && 
			pEncounter->pWorldEncounter->properties &&
			pEncounter->pWorldEncounter->properties->pSpawnProperties &&
			pEncounter->pWorldEncounter->properties->pSpawnProperties->eMastermindSpawnType != WorldEncounterMastermindSpawnType_None;
}


bool encounter_IsMapOwnerAvailable(int iPartitionIdx)
{
	static bool bErrorOnce = false;

	Entity *pOwnerEnt = NULL;
	PartitionEncounterData *pData = encounter_GetPartitionData(iPartitionIdx);

	// Some maps have an "owner"; try to get the owner for this map.  Skip encounter processing until we get
	// them, or until we've waited a long time
	// This is really part of loading the map, but the reference system isn't ready to receive references
	// until the map is running.
	if (pData->bOwnerFound ||
		(gGSLState.gameServerDescription.baseMapDescription.eMapType != ZMTYPE_OWNED) ||
		!partition_OwnerIDFromIdx(iPartitionIdx) ||
        zmapInfoGetIsGuildOwned(NULL) ) {
		return true;
	}

	// If this is the first time in the loop, set the owner entity ref from the containter ID
	if (pData->uFindOwnerStartTime == 0) 
	{
		pData->uFindOwnerStartTime = timeSecondsSince2000();
	}

	// Try to get the entity
	pOwnerEnt = partition_GetPlayerMapOwner(iPartitionIdx);

	// Hack - if the map owner has a primary nemesis, wait for that nemesis to load
	ANALYSIS_ASSUME(pOwnerEnt); // this isn't in the correct spot, but I don't want to rework the logic here. it achieves it's goal - silencing the static analyzer
	if (pOwnerEnt && (player_GetPrimaryNemesis(entFromContainerID(iPartitionIdx,entGetType(pOwnerEnt),entGetContainerID(pOwnerEnt))) || !player_GetPrimaryNemesisID(pOwnerEnt))) {
		pData->bOwnerFound = true;
	} else if (timeSecondsSince2000() < pData->uFindOwnerStartTime + SECONDS_TO_WAIT_FOR_OWNER) {
		// Skip encounter processing until we find the entity or it's been a long time
		return false;
	} else if (!bErrorOnce) {
		bErrorOnce = true;
		// Don't return; proceed with normal encounter processing
		ErrorDetailsf("This is most likely due to the player crashing during map transfer, or the map transfer process taking over %d seconds.  If this error is occurring frequently, then it may be a sign of a problem.", SECONDS_TO_WAIT_FOR_OWNER);
		Errorf("Timeout trying to find player who owns map.");
	}

	return true;
}


// Get the longest interact range of any actor
U32 encounter_GetMaxInteractRange()
{
	return s_uMaxInteractDist;
}


static void encounter_CheckFallThroughWorld(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	PerfInfoGuard *piGuard;
	int i;

	// Don't check for falling through world for entities in space region
	if (pEncounter->eRegionType == WRT_Space) {
		return;
	}

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);

	// Only check when phase count is high enough
	// This makes fall through would check less frequent (currently 6 phases = 1 per 2 seconds)
	if (pState->timerData.uFallPhasesSkipped < MIN_PHASES_FOR_FALL_CHECK) {
		++pState->timerData.uFallPhasesSkipped;
		PERFINFO_AUTO_STOP_GUARD(&piGuard);
		return;
	}
	pState->timerData.uFallPhasesSkipped = 0;

	for(i=eaSize(&pState->eaEntities)-1; i>=0; --i) {
		Entity *pEnt = pState->eaEntities[i];

		if (pEnt->pCritter && entIsAlive(pEnt)) {
			CritterEncounterData *pEncounterData = &pEnt->pCritter->encounterData;
			Vec3 vEntPos;

			entGetPos(pEnt, vEntPos);

			if (vEntPos[1] - pEncounterData->origPos[1] < -ACTOR_FALLTHROUGHWORLD_CHECKDIST) {
				const char *pcActorName = encounter_GetEncounterActorName(pEncounter, pEncounterData->iActorIndex);

				if (!resNamespaceIsUGC(zmapGetName(NULL)))
				{
					ErrorDetailsf("Error: %s::%s has fallen through the world!\n(%.2f %.2f %.2f - %.2f %.2f %.2f)", 
						pEncounter->pcName, pcActorName, vecParamsXYZ(vEntPos), vecParamsXYZ(pEncounterData->origPos));

					ErrorFilenamef(layerGetFilename(pEncounter->pWorldEncounter->common_data.layer), 
						"Error: Critter from encounter '%s' has fallen through the world!", pEncounter->pcName);
				}

				encounter_RemoveActor(pEnt);
				gslQueueEntityDestroy(pEnt);
			}
		}
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}


// Determine whether the player should be ambushed.  If the answer is "yes", reset the player's ambush timer
bool encounter_TryPlayerAmbush(Entity *pPlayerEnt)
{
	bool bRetVal = false;

	if (pPlayerEnt && pPlayerEnt->pPlayer) {
		unsigned int iCurTime = timeSecondsSince2000();
		bool bCooldown = false;

		if (pPlayerEnt->pPlayer->nextAmbushTime <= iCurTime) {
			U32 uProbRoll = 1 + (randomInt() % 100);
			if (uProbRoll <= g_AmbushChance) {
				bRetVal = true;
				bCooldown = true;
				if (g_AmbushDebugEnabled) {
					printf("Ambush: Ambush triggered.  Next ambush chance in %d seconds\n", g_AmbushCooldown);
				}
			} else if (uProbRoll > 100 - g_AmbushSkipChance) {
				bCooldown = true;
				if (g_AmbushDebugEnabled) {
					printf("Ambush: Ambush skipped.  Next ambush chance in %d seconds\n", g_AmbushCooldown);
				}
			}

			if (bCooldown) {
				pPlayerEnt->pPlayer->nextAmbushTime = iCurTime + g_AmbushCooldown;
			} else if (g_AmbushDebugEnabled) {
				printf("Ambush: No ambush triggered\n");
			}
		} else if (g_AmbushDebugEnabled){
			printf("Ambush: Ambush on cooldown, next ambush chance in %d seconds\n", pPlayerEnt->pPlayer->nextAmbushTime - iCurTime);
		}
	}

	return bRetVal;
}


static S32 encounter_PlayerDifficultyTeamSize(int iPartitionIdx)
{
	if(zmapInfoGetMapType(NULL) != ZMTYPE_STATIC)
	{
		PlayerDifficulty *pdiff = pd_GetDifficulty(mapState_GetDifficulty(mapState_FromPartitionIdx(iPartitionIdx)));
		if(pdiff)
		{
			if(pdiff->DisableTeamSizeMapVarName)
			{
				// find map and block changing difficulty if map var is present
				MapVariable *pMapVar = mapvariable_GetByNameIncludingCodeOnly(iPartitionIdx, pdiff->DisableTeamSizeMapVarName);
				if(pMapVar)
				{
					return 0;
				}
			}

			// return override team size
			return pdiff->iTeamSizeOverride;
		}
	}

	// no change
	return 0;
}

// Do a different player count check on mission maps?
static int encounter_GetActivePlayerCount(int iPartitionIdx, GameEncounter *pEncounter, Entity* pSpawningPlayer)
{
	int iCount = partition_GetPlayerCount(iPartitionIdx);
	U32 uPlayerZoneTime = 0;
	PartitionEncounterData *pData = encounter_GetPartitionData(iPartitionIdx);

	// re-entering a map will not re-set s_uStatTime, use the time the player left the map or the start time (whatever is greater) 
	// to fix whole team re-entrance issue of incorrect team size
	if (pSpawningPlayer && pSpawningPlayer->pTeam && pSpawningPlayer->pTeam->iAverageTeamLevel > 0) {
		uPlayerZoneTime = pSpawningPlayer->pTeam->iAverageTeamLevelTime - AVERAGE_PARTY_LEVEL_EXPIRE_TIME_SECONDS;
	}

	if (pData->uNearbyTeamSize && (timeSecondsSince2000() - max(pData->uStartTime, uPlayerZoneTime) > NEARBY_TEAMMATE_COUNT_LIFESPAN)) {
		pData->uNearbyTeamSize = 0;
	}

	// Is there a team size set in the debugger?
	if (g_ForceTeamSize) {
		iCount = g_ForceTeamSize;
	} else if (zmapInfoGetMapForceTeamSize(NULL)) {
		iCount = zmapInfoGetMapForceTeamSize(NULL);
	} else if (encounter_PlayerDifficultyTeamSize(iPartitionIdx)) {
		iCount = encounter_PlayerDifficultyTeamSize(iPartitionIdx);
	} else if (pData->uNearbyTeamSize) {
		iCount = pData->uNearbyTeamSize;
	} else if(pSpawningPlayer) {
		Entity** eaTeammaates = NULL;
		encounter_getTeammatesInRange(pSpawningPlayer, &eaTeammaates);
		iCount = eaSize(&eaTeammaates);
		eaDestroy(&eaTeammaates);
	}

	iCount = CLAMP(iCount, 1, MAX_TEAM_SIZE);
	return iCount;
}

static bool encounter_AreAllCombatantsDead(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	int i;

	for(i=eaSize(&pState->eaEntities)-1; i>=0; --i) {
		Entity *pEnt = pState->eaEntities[i];
		if (entIsAlive(pEnt) && (!pEnt->pCritter || !pEnt->pCritter->encounterData.bNonCombatant)) {
			return false;
		}
	}
	return true;
}


int encounter_GetNumLivingEnts(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	int iNumLiving = 0;
	int i;

	for(i=eaSize(&pState->eaEntities)-1; i>=0; --i) {
		if (entIsAlive(pState->eaEntities[i])) {
			++iNumLiving;
		}
	}
	return iNumLiving;
}

static Entity* encounter_GetNemesisEntity(int iPartitionIdx, EncounterActorProperties *pActor, Entity *pSpawningPlayer)
{
	Entity *pEnt = NULL;
	Entity *pNemesisEnt;
	pEnt = partition_GetPlayerMapOwner(iPartitionIdx);
	if (!pEnt)
		pEnt = pSpawningPlayer;
	if(!pEnt)
		return NULL;
	pNemesisEnt = player_GetPrimaryNemesis(entFromContainerID(iPartitionIdx,entGetType(pEnt),entGetContainerID(pEnt)));
	if(!pNemesisEnt || !pNemesisEnt->pNemesis)
		return NULL;
	return pNemesisEnt;
}

static Entity* encounter_GetNemesisForEntity(int iPartitionIdx, EncounterActorProperties *pActor, Entity *pSpawningPlayer)
{
	if(pSpawningPlayer)
	{

		Entity *pNemesisEnt;
		pNemesisEnt = player_GetPrimaryNemesis(entFromContainerID(iPartitionIdx,entGetType(pSpawningPlayer),entGetContainerID(pSpawningPlayer)));

		if(pNemesisEnt && pNemesisEnt->pNemesis)
		{
			return pNemesisEnt;
		}
	}

	return NULL;
}

static Entity* encounter_GetNemesisForLeader(int iPartitionIdx, Entity *pSpawningPlayer, bool bUseTeamIfNoLeaderNemesis)
{
	if(pSpawningPlayer)
	{
		Entity *pTeamLeader = Nemesis_TeamGetTeamLeader(pSpawningPlayer, bUseTeamIfNoLeaderNemesis);
		if(pTeamLeader)
		{
			Entity *pNemesisEnt = player_GetPrimaryNemesis(pTeamLeader);
			return pNemesisEnt;
		}
	}
	else
	{
		// the spawning player might out of range which will require this to get him
		Entity *pEnt =  Nemesis_TeamGetPlayerEntAtIndex(iPartitionIdx, 0, true);
		if(pEnt)
		{
			Entity *pNemesisEnt = player_GetPrimaryNemesis(pEnt);
			return pNemesisEnt;
		}
	}

	return NULL;
}

static Entity* encounter_GetNemesisTeamIndex(int iPartitionIdx, Entity *pSpawningPlayer, S32 iIndex)
{
	if(pSpawningPlayer && iIndex >= 0)
	{
		Entity *pTeamEnt = Nemesis_TeamGetTeamIndex(pSpawningPlayer, iIndex);
		if(pTeamEnt)
		{
			Entity *pNemesisEnt = player_GetPrimaryNemesis(pTeamEnt);
			return pNemesisEnt;
		}
	}
	else if(iIndex >= 0)
	{
		Entity *pEnt = Nemesis_TeamGetPlayerEntAtIndex(iPartitionIdx, iIndex, false);
		if(pEnt)
		{
			// the spawning player might out of range which will require this to get him and then his nemesis
			Entity *pNemesisEnt = player_GetPrimaryNemesis(pEnt);
			return pNemesisEnt;
		}
	}

	return NULL;
}

static void encounter_Complete(GameEncounter *pEncounter, GameEncounterPartitionState *pState, EncounterState eNewState)
{
	int i;
	
	// Mark the encounter as complete
	pState->timerData.uTimeLastCompleted = timeSecondsSince2000();
	encounter_StateTransition(pEncounter, pState, eNewState);

	// Set the respawn timer as appropriate
	pState->timerData.fSpawnTimer = encounter_GetRespawnTimerValueFromProperties(pEncounter->pWorldEncounter->properties, pEncounter->pWorldEncounter->encounter_pos);

	// Reset the event counts back to zero. This is done the first time in encounter_BeginTrackingEvents.
	for(i=eaSize(&pState->eventData.eaTrackedEventsSinceComplete)-1; i>=0; --i) {
		GameEvent *pEvent = pState->eventData.eaTrackedEventsSinceComplete[i];
		stashAddInt(pState->eventData.stEventLogSinceComplete, pEvent->pchEventName, 0, true);
	}
}


// Reset the encounter from its completed state back to waiting
// And kill any critters still alive
void encounter_Reset(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	EncounterState eNewState = EncounterState_Asleep;
	PerfInfoGuard* piGuard;
	
	if (pState->eState == EncounterState_Asleep) {
		return;
	}

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);

	if (pState->bShouldBeGroupManaged) {
		LogicalGroupProperties* pLogicalProps = SAFE_MEMBER3(pEncounter, pEncounterGroup, pWorldGroup, properties);
		if (pLogicalProps && pLogicalProps->encounterSpawnProperties.eRandomType == LogicalGroupRandomType_Continuous) {
			eNewState = EncounterState_GroupManaged;
		}
	}
	if (eNewState == EncounterState_Asleep && pProps && pProps->pSpawnProperties && pProps->pSpawnProperties->pSpawnCond) {
		eNewState = EncounterState_Waiting;
	}

	// Transition to the desired state
	encounter_StateTransition(pEncounter, pState, eNewState);

	// Clear tracking data
	eaiDestroy(&pState->playerData.eauEntsWithCredit);
	pState->debugStatus[0] = '\0';
	pState->playerData.uSpawningPlayer = 0;
	pState->iNumTimesSpawned = 0;
	pState->timerData.bSpawnDelayCountdown = false;
	pState->timerData.fSpawnDelayTimer = 0;

	// Kill all the critters that are not dead yet
	eaDestroyEx(&pState->eaEntities, gslQueueEntityDestroy);

	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}

// Force reset all encounters in the specified partition
void encounter_ResetAll(int iPartitionIdx)
{
	FOR_EACH_ENCOUNTER(pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
		encounter_Reset(pEncounter, pState);
	} FOR_EACH_END;
}

static bool encounter_SpawnActor(GameEncounter *pEncounter, GameEncounterPartitionState *pState,
								 EncounterTemplate *pTemplate, EncounterActorProperties *pActor, WorldActorProperties *pWorldActor, int iActorIndex,
								 int iBaseLevel, Entity *pSpawningPlayer, int iTeamSize, AITeam *pAITeam, CritterDef ***peaUnderlingList, CritterDef ***peaExcludeList, const char ***peaPetContactNameExcludeList,
								 AICombatRolesDef *pCombatRolesDef)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	PlayerDifficultyMapData *pDifficultyMapData = NULL;
	Entity *pNewEnt = NULL;
	Entity *pNemesisEntity = NULL;
	CritterGroup *pCritterGroup = NULL;
	CritterDef *pCritterDef;
	const char *pcRank;
	int i;
	const char* pcNameOverride = NULL;
	CritterCreateParams createParams = {0};
	WorldRegionType eRegion = pSpawningPlayer ? entGetWorldRegionTypeOfEnt(pSpawningPlayer) : WRT_None;
	int iPartitionIdx = pState->iPartitionIdx;
	bool bCritterFromOverride = false;
	MapState *pMapState;
	FSM *pFSM;
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);

	// if no spawning player 
	if (eRegion == WRT_None) {
		Vec3 vActorPos;
		RegionRules* pRegion = NULL;
		encounter_GetActorPosition(pEncounter, iActorIndex, vActorPos, NULL);
		pRegion = vActorPos ? RegionRulesFromVec3(vActorPos) : NULL;
		if (pRegion) {
			eRegion = pRegion->eRegionType;
		}
	}
	pMapState = mapState_FromPartitionIdx(iPartitionIdx);
	pDifficultyMapData = pd_GetDifficultyMapData(mapState_GetDifficulty(pMapState), zmapInfoGetPublicName(NULL), eRegion);
	
	// Collect data and
	// set up most of the critter create params
	pCritterDef = pWorldActor && pWorldActor->pCritterProperties ? GET_REF(pWorldActor->pCritterProperties->hCritterDef) : NULL;
	if(!pCritterDef) {
		pCritterDef = encounterTemplate_GetActorCritterDef(pTemplate, pActor, iPartitionIdx);
	} else {
		bCritterFromOverride = true;
	}

	createParams.enttype = GLOBALTYPE_ENTITYCRITTER;
	createParams.iPartitionIdx = iPartitionIdx;
	createParams.pEncounter2 = pEncounter;
	createParams.iActorIndex = iActorIndex;

	if (pWorldActor && pWorldActor->pFactionProperties) {
		createParams.pFaction = GET_REF(pWorldActor->pFactionProperties->hCritterFaction);
	} else if (bCritterFromOverride && GET_REF(pCritterDef->hFaction)) {
		createParams.pFaction = GET_REF(pCritterDef->hFaction);
	} else {
		createParams.pFaction = encounterTemplate_GetActorFaction(pTemplate, pActor, iPartitionIdx);
	}
	
	createParams.pchSpawnAnim = encounterTemplate_GetActorSpawnAnim(pTemplate, pActor, iPartitionIdx, &createParams.fSpawnTime);
	createParams.pcSubRank = encounterTemplate_GetActorSubRank(pTemplate, pActor);
	createParams.pCritterGroupDisplayNameMsg = encounter_GetActorOverrideCritterGroupDisplayMessage(iPartitionIdx, pEncounter->pWorldEncounter->properties, pWorldActor);
	createParams.pDisplayNameMsg = encounter_GetActorOverrideDisplayMessage(iPartitionIdx, pEncounter->pWorldEncounter->properties, pWorldActor);
	createParams.pDisplaySubNameMsg = encounter_GetActorOverrideDisplaySubNameMessage(iPartitionIdx, pEncounter->pWorldEncounter->properties, pWorldActor);
	createParams.iLevel = encounterTemplate_GetActorLevel(pTemplate, pActor, iBaseLevel);
	createParams.iBaseLevel = iBaseLevel;
		
	pFSM = encounter_GetActorFSM(iPartitionIdx, pEncounter, iActorIndex);
	createParams.fsmOverride = pFSM?pFSM->name:NULL;

	createParams.pCombatRolesDef = pCombatRolesDef;
	createParams.pcCombatRoleName = encounterTemplate_GetActorCombatRole(pTemplate, pActor); 

	createParams.iTeamSize = iTeamSize;
	createParams.spawningPlayer = pSpawningPlayer;
	createParams.aiTeam = pAITeam;
		

	// Adjust critter level according to difficulty
	if (pDifficultyMapData) {
		createParams.iLevel += pDifficultyMapData->iLevelModifier;
	}

	if (pActor && pActor->critterProps.eCritterType == ActorCritterType_PetContactList)
	{
		// Special handling for pet contact lists
		PetContactList* pList = GET_REF(pActor->miscProps.hPetContactList);
		if (pList!=NULL)
		{
			Entity* pOwner = partition_GetPlayerMapOwner(iPartitionIdx);
			if (!pOwner && gGSLState.gameServerDescription.baseMapDescription.eMapType != ZMTYPE_OWNED) {
				pOwner = pSpawningPlayer;
			}
			if (pOwner)
			{
				Entity* pEntPlayer;
				Entity* pPet;
				CritterCostume *pPetCostume;
				CritterDef *pDefaultCritter;
				
				pEntPlayer = entFromContainerID(iPartitionIdx,entGetType(pOwner),entGetContainerID(pOwner));

				// This will handle pEntPlayer being NULL, which is evidently possible considering the old code.
				PetContactList_GetPetOrCostume(pEntPlayer, pList, peaPetContactNameExcludeList, &pPet, &pDefaultCritter, &pPetCostume);
				
				// Attempt to get a real pet
				if (pPet && pPet->pSaved)
				{
					createParams.pCostume = costumeEntity_GetActiveSavedCostume(pPet);
					pcNameOverride = pPet->pSaved->savedName;
				}
				else
				{
					// Use the non-pet data
					createParams.pCostume = pPetCostume ? GET_REF(pPetCostume->hCostumeRef) : NULL;
					createParams.pDisplayNameMsg = pDefaultCritter ? GET_REF(pDefaultCritter->displayNameMsg.hMessage) : NULL;
					createParams.pDisplaySubNameMsg = pDefaultCritter ? GET_REF(pDefaultCritter->displaySubNameMsg.hMessage) : NULL;
				}
			}
		}

	} else if(pActor && pActor->critterProps.eCritterType == ActorCritterType_Nemesis) {
		pNemesisEntity = encounter_GetNemesisEntity(iPartitionIdx, pActor, pSpawningPlayer);
		if(pNemesisEntity)
			pCritterDef = critter_GetNemesisCritterAndSetParams(iPartitionIdx, pNemesisEntity, &createParams, false, -1);
	} else if(pActor && pActor->critterProps.eCritterType == ActorCritterType_NemesisMinion) {
		pNemesisEntity = encounter_GetNemesisEntity(iPartitionIdx, pActor, pSpawningPlayer);
		if(pNemesisEntity)
			pCritterGroup = critter_GetNemesisMinionGroupAndSetParams(iPartitionIdx, pNemesisEntity, &createParams, false, -1);
	}
	else if(pActor && pActor->critterProps.eCritterType == ActorCritterType_NemesisNormal)
	{
		// This spawns the nemesis for the spawning player
		pNemesisEntity = encounter_GetNemesisForEntity(iPartitionIdx, pActor, pSpawningPlayer);
		if(pNemesisEntity)
		{
			pCritterDef = critter_GetNemesisCritterAndSetParams(iPartitionIdx, pNemesisEntity, &createParams, false, -1);
		}
	}
	else if(pActor && pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionNormal)
	{
		// This spawns the nemesis minions for the spawning player
		pNemesisEntity = encounter_GetNemesisForEntity(iPartitionIdx, pActor, pSpawningPlayer);
		if(pNemesisEntity)
		{
			pCritterGroup = critter_GetNemesisMinionGroupAndSetParams(iPartitionIdx, pNemesisEntity, &createParams, false, -1);
		}
	}
	else if(pActor && pActor->critterProps.eCritterType == ActorCritterType_NemesisForLeader)
	{
		// This spawns the nemesis for the spawning player
		pNemesisEntity = encounter_GetNemesisForLeader(iPartitionIdx, pSpawningPlayer, pActor->critterProps.bNemesisLeaderTeam);
		pCritterDef = critter_GetNemesisCritterAndSetParams(iPartitionIdx, pNemesisEntity, &createParams, true, 0);
	}
	else if(pActor && pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionForLeader)
	{
		// This spawns the nemesis minions for the spawning player
		pNemesisEntity = encounter_GetNemesisForLeader(iPartitionIdx, pSpawningPlayer, pActor->critterProps.bNemesisLeaderTeam);
		pCritterGroup = critter_GetNemesisMinionGroupAndSetParams(iPartitionIdx, pNemesisEntity, &createParams, true, 0);
	}
	else if(pActor && pActor->critterProps.eCritterType == ActorCritterType_NemesisTeam)
	{
		// This spawns the nemesis for the spawning player
		pNemesisEntity = encounter_GetNemesisTeamIndex(iPartitionIdx, pSpawningPlayer, pActor->critterProps.iNemesisTeamIndex);
		pCritterDef = critter_GetNemesisCritterAndSetParams(iPartitionIdx, pNemesisEntity, &createParams, false, pActor->critterProps.iNemesisTeamIndex);
	}
	else if(pActor && pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionTeam)
	{
		// This spawns the nemesis minions for the spawning player
		pNemesisEntity = encounter_GetNemesisTeamIndex(iPartitionIdx, pSpawningPlayer, pActor->critterProps.iNemesisTeamIndex);
		pCritterGroup = critter_GetNemesisMinionGroupAndSetParams(iPartitionIdx, pNemesisEntity, &createParams, false, pActor->critterProps.iNemesisTeamIndex);
	}

	if (pWorldActor && pWorldActor->pCostumeProperties) {
		createParams.pCostume = GET_REF(pWorldActor->pCostumeProperties->hCostume);
	}

	// Create the Entity
	if (pCritterDef) {
		pNewEnt = critter_CreateByDef(pCritterDef, &createParams, pTemplate->pcFilename, true);
	} else {
		if(!pCritterGroup)
			pCritterGroup = encounterTemplate_GetActorCritterGroup(pTemplate, pActor, iPartitionIdx, encounterTemplate_GetActorCritterType(pTemplate, pActor));
		pcRank = encounterTemplate_GetActorRank(pTemplate, pActor);

		// Prefer an underling if one is available
		for(i=eaSize(peaUnderlingList)-1; i>=0; --i) {
			if ((*peaUnderlingList)[i]->pcRank == pcRank) {
				pNewEnt = critter_CreateByDef((*peaUnderlingList)[i], &createParams, pTemplate->pcFilename, true);
				eaRemove(peaUnderlingList, i);
				break;
			}
		}

		// If no underling, then make a random critter in the critter group
		if (!pNewEnt) {
			int totalFail=0;

			pCritterDef = critter_DefFind( pCritterGroup, pcRank, createParams.pcSubRank, createParams.iLevel, &totalFail, peaExcludeList);

			if(createParams.pCostumeSet && pCritterDef)
			{
				// find the costume for the nemesis minion
				PlayerCostume *pNemCostume = nemesis_MinionCostumeByClass(createParams.pCostumeSet, pCritterDef->pchClass);
				if(pNemCostume)
				{
					createParams.pCostume = pNemCostume;
				}
				else
				{
					ErrorFilenamef(pCritterDef->pchFileName, "Nemesis minion %s of class %s can't find a costume.", pCritterDef->pchName, pCritterDef->pchClass);
				}
			}

			if (totalFail) {
				if (gConf.bManualSubRank) {
					if (pTemplate) {
						ErrorFilenamef(pTemplate->pcFilename, "Encounter %s: No critter matches the given rank/subrank/group/level (%s, %s, %s, %d) or spawn limits ruled out the options", 
									pTemplate->pcName, pcRank, createParams.pcSubRank, pCritterGroup ? pCritterGroup->pchName : "No Group", createParams.iLevel);
					} else {
						Errorf("No critter matches the given rank/subrank/group/level (%s, %s, %s, %d)", 
									pcRank, createParams.pcSubRank, pCritterGroup ? pCritterGroup->pchName : "No Group", createParams.iLevel);
					}
				} else {
					if (pTemplate) {
						ErrorFilenamef(pTemplate->pcFilename, "Encounter %s: No critter matches the given rank/group/level (%s, %s, %d) or spawn limits ruled out the options", 
						pTemplate->pcName, pcRank, pCritterGroup ? pCritterGroup->pchName : "No Group", createParams.iLevel);
					} else {
						Errorf("No critter matches the given rank/group/level (%s, %s, %d)", 
						pcRank, pCritterGroup ? pCritterGroup->pchName : "No Group", createParams.iLevel);
					}
				}
			} else if (pCritterDef) {
				EncounterSpawnProperties *pSpawnProperties = encounterTemplate_GetSpawnProperties(pTemplate);
				
				// Get the proper spawn animation if one wasn't set
				if (!EMPTY_TO_NULL(createParams.pchSpawnAnim) && pSpawnProperties) {
					if (pSpawnProperties->eSpawnAnimType == EncounterSpawnAnimType_FromCritter) {
						if (pCritterDef && EMPTY_TO_NULL(pCritterDef->pchSpawnAnim)) {
							createParams.pchSpawnAnim = pCritterDef->pchSpawnAnim;
							createParams.fSpawnTime = pCritterDef->fSpawnLockdownTime;
						} else if (pCritterGroup && EMPTY_TO_NULL(pCritterGroup->pchSpawnAnim)) {
							createParams.pchSpawnAnim = pCritterGroup->pchSpawnAnim;
							createParams.fSpawnTime = pCritterGroup->fSpawnLockdownTime;
						}
					} else if (pSpawnProperties->eSpawnAnimType == EncounterSpawnAnimType_FromCritterAlternate) {
						if (pCritterDef && EMPTY_TO_NULL(pCritterDef->pchSpawnAnimAlternate)) {
							createParams.pchSpawnAnim = pCritterDef->pchSpawnAnimAlternate;
							createParams.fSpawnTime = pCritterDef->fSpawnLockdownTimeAlternate;
						} else if (pCritterGroup && EMPTY_TO_NULL(pCritterGroup->pchSpawnAnimAlternate)) {
							createParams.pchSpawnAnim = pCritterGroup->pchSpawnAnimAlternate;
							createParams.fSpawnTime = pCritterGroup->fSpawnLockdownTimeAlternate;
						}
					}
				}

				createParams.iPartitionIdx = iPartitionIdx;
				pNewEnt = critter_CreateByDef(pCritterDef, &createParams, NULL, true);
			}
		}
	}

	if (pNewEnt && pNewEnt->pCritter) {
		Vec3 vNewEntPos;
		Vec3 vNewEntRot;
		Quat qNewEntRot;
		F32 fOverrideSendDistance = encounter_GetOverrideSendDistance(pEncounter);
		WorldInteractionProperties* pInteractionProps = encounterTemplate_GetActorInteractionProps(pTemplate, pActor);
		WorldEncounterRewardProperties* pRewardProps = encounter_GetRewardProperties(pEncounter);
		WorldInteractionPropertyEntry** eaInteractionEntries = NULL;
		ANALYSIS_ASSUME(pNewEnt != NULL);
		encounterTemplate_FillActorInteractionEarray(pTemplate, pActor, &eaInteractionEntries);

		if (fOverrideSendDistance > FLT_EPSILON) {
			pNewEnt->pCritter->fOverrideSendDistance = fOverrideSendDistance;
		}
		if (pRewardProps) {
			if (GET_REF(pRewardProps->hRewardTable)) {
				RewardTableRef* pRef = StructCreate(parse_RewardTableRef);
				COPY_HANDLE(pRef->hRewardTable, pRewardProps->hRewardTable);
				eaPush(&pNewEnt->pCritter->eaAdditionalRewards, pRef);
			}
			pNewEnt->pCritter->eRewardType = pRewardProps->eRewardType;
			pNewEnt->pCritter->eRewardLevelType = pRewardProps->eRewardLevelType;
			pNewEnt->pCritter->iRewardLevel = pRewardProps->iRewardLevel;
		} else if (pEncounter->pWorldEncounter->properties) {
			EncounterTemplate* pRewardTemplate = GET_REF(pEncounter->pWorldEncounter->properties->hTemplate);
			EncounterRewardProperties* pTemplateRewardProps = encounterTemplate_GetRewardProperties(pRewardTemplate);
			if (pTemplateRewardProps) {
				if (GET_REF(pTemplateRewardProps->hRewardTable)) {
					RewardTableRef* pRef = StructCreate(parse_RewardTableRef);
					COPY_HANDLE(pRef->hRewardTable, pTemplateRewardProps->hRewardTable);
					eaPush(&pNewEnt->pCritter->eaAdditionalRewards, pRef);
				}
				pNewEnt->pCritter->eRewardType = pTemplateRewardProps->eRewardType;
				pNewEnt->pCritter->eRewardLevelType = pTemplateRewardProps->eRewardLevelType;
				pNewEnt->pCritter->iRewardLevel = pTemplateRewardProps->iRewardLevel;
			}
		}

		// Get position and rotation of the actor
		copyVec3(pWorldActor->vPos, vNewEntPos);
		copyVec3(pWorldActor->vRot, vNewEntRot);
		PYRToQuat(vNewEntRot, qNewEntRot);

		// Snap the actor's position to the ground in case the terrain got moved or something
		if (!pProps->pSpawnProperties || pProps->pSpawnProperties->bSnapToGround) {
			S32 bFloorFound = false;
			worldSnapPosToGround(iPartitionIdx, vNewEntPos, FIRST_ACTOR_SPAWN_SNAP_TO_DIST, -FIRST_ACTOR_SPAWN_SNAP_TO_DIST, &bFloorFound);

			// If we couldn't find any ground for the actor, try again with a bigger delta
			if (!bFloorFound) {
				worldSnapPosToGround(iPartitionIdx, vNewEntPos, SECOND_ACTOR_SPAWN_SNAP_TO_DIST, -SECOND_ACTOR_SPAWN_SNAP_TO_DIST, &bFloorFound);
			}
			
			if (bFloorFound) {	// If floor was hit, add a y-bias
				vecY(vNewEntPos) += 0.1;
				if (gConf.bNewAnimationSystem) {
					mrSurfaceSetSpawnedOnGround(pNewEnt->mm.mrSurface, true);
				}
			}
		} else {
			S32 bFloorFound = false;
			worldSnapPosToGround(iPartitionIdx, vNewEntPos, ACTOR_SPAWN_NOSNAP_SNAP_TO_DIST_UP, -ACTOR_SPAWN_NOSNAP_SNAP_TO_DIST_DOWN, &bFloorFound);

			if (bFloorFound) {	// If floor was hit, add a y-bias
				vecY(vNewEntPos) += 0.1;
				if (gConf.bNewAnimationSystem) {
					mrSurfaceSetSpawnedOnGround(pNewEnt->mm.mrSurface, true);
				}
			}
		}

		// Set the actual position
		entSetPos(pNewEnt, vNewEntPos, 1, __FUNCTION__);
		entSetRot(pNewEnt, qNewEntRot, 1, __FUNCTION__);

		// Set up boss bar if requested
		if (encounterTemplate_GetActorBossBar(pTemplate, pActor, iTeamSize, encounter_GetDifficulty(iPartitionIdx, pEncounter))) {
			MultiVal mvIntVal = {0};
			MultiValSetInt(&mvIntVal, 1);
			entSetUIVar(pNewEnt, "Boss", &mvIntVal);
		}

		if (pNewEnt->pChar) {
			// Set the gang
			pNewEnt->pChar->gangID = encounterTemplate_GetActorGangID(pTemplate, pActor, iPartitionIdx);
			entity_SetDirtyBit(pNewEnt, parse_Character, pNewEnt->pChar, false);
		}

		// Mark as interactable and set interact range
		// Check template actor for interactions
		if (pInteractionProps && eaSize(&eaInteractionEntries)) {
			pNewEnt->pCritter->bIsInteractable = true;
			pNewEnt->pCritter->uInteractDist = pWorldActor->uInteractDistCached;
		}
		eaDestroy(&eaInteractionEntries);

		// Check world actor for interactions
		if (!pNewEnt->pCritter->bIsInteractable && pWorldActor && pWorldActor->pInteractionProperties && eaSize(&pWorldActor->pInteractionProperties->eaEntries)) {
			pNewEnt->pCritter->bIsInteractable = true;
			pNewEnt->pCritter->uInteractDist = pWorldActor->uInteractDistCached;
		}
		
		if (!pCritterDef) {
			pCritterDef = GET_REF(pNewEnt->pCritter->critterDef);
		}
		if (pCritterDef) {
			// Check critter def for interact information
			if (!pNewEnt->pCritter->bIsInteractable && GET_REF(pCritterDef->hInteractionDef)) {
				pNewEnt->pCritter->bIsInteractable = true;
			}
			if (!pNewEnt->pCritter->uInteractDist) {
				pNewEnt->pCritter->uInteractDist = pCritterDef->uInteractRange;
			}

			// If this critter has underlings, add them to the preferred list
			for(i=eaSize(&pCritterDef->ppUnderlings)-1; i>=0; --i) {
				CritterDef *pUnderling = critter_DefGetByName(pCritterDef->ppUnderlings[i]);
				if (pUnderling) {
					eaPush(peaUnderlingList, pUnderling);
				}
			}
		}

		// If override name is specified, set it.
		if (EMPTY_TO_NULL(pcNameOverride)) {
			pNewEnt->pCritter->displayNameOverride = StructAllocString(pcNameOverride);
		}

		// If we are using a pet contact list, add the entity to the exclude list
		if (pActor && pActor->critterProps.eCritterType == ActorCritterType_PetContactList) {
			if (EMPTY_TO_NULL(pcNameOverride)) {
				eaPush(peaPetContactNameExcludeList, estrCreateFromStr(pcNameOverride));
			}
		} else if (pActor && (pActor->critterProps.eCritterType == ActorCritterType_Nemesis || pActor->critterProps.eCritterType == ActorCritterType_NemesisForLeader ||
			pActor->critterProps.eCritterType == ActorCritterType_NemesisTeam || pActor->critterProps.eCritterType == ActorCritterType_NemesisNormal)) {
			critter_SetupNemesisEntity(pNewEnt, pNemesisEntity, !createParams.pDisplayNameMsg, iPartitionIdx, pActor->critterProps.iNemesisTeamIndex, (pActor->critterProps.eCritterType == ActorCritterType_NemesisForLeader));
		} else if (pActor && (pActor->critterProps.eCritterType == ActorCritterType_NemesisMinion || pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionForLeader ||
			pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionTeam || pActor->critterProps.eCritterType == ActorCritterType_NemesisMinionNormal)) {
			critter_SetupNemesisMinionEntity(pNewEnt, pNemesisEntity);
		}

		// If we exceed the spawn limit for this critter def, put it on the exclude list
		if (pCritterDef && pCritterDef->iSpawnLimit > 0) {
			int iCount = 0;
			for(i=eaSize(&pState->eaEntities)-1; i>=0; --i) {
				CritterDef *pEntDef = GET_REF(pState->eaEntities[i]->pCritter->critterDef);
				if (pEntDef == pCritterDef) {
					++iCount;
				}
			}
			if (iCount >= pCritterDef->iSpawnLimit) {
				eaPush(peaExcludeList, pCritterDef);
			}
		}

		// set difficulty modifiers
		if (pNewEnt->pCritter) {
			PowerDef *pDifficultyPower = SAFE_GET_REF(pDifficultyMapData, hPowerDef);

			if (pDifficultyMapData) {
				pNewEnt->pCritter->fNumericRewardScale = pDifficultyMapData->fNumericRewardScale;
				{
					RewardTableRef* pRef = StructCreate(parse_RewardTableRef);
					COPY_HANDLE(pRef->hRewardTable, pDifficultyMapData->hRewardTable);
					eaPush(&pNewEnt->pCritter->eaAdditionalRewards, pRef);
				}
			}

			// add powers from difficulty setting
			if (pDifficultyPower && pNewEnt->pChar) {
				character_AddPowerPersonal(iPartitionIdx, pNewEnt->pChar, pDifficultyPower, 0, true, NULL);
			}
		}
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuard);

	return !!pNewEnt;
}


// Resets the event count to 0 for each element in the stash table
static int encounter_ResetEventCounts(StashElement element)
{
	stashElementSetInt(element, 0);
	return true;
}


void encounter_SpawnEncounter(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	WorldEncounterSpawnProperties *pSpawnProps = pProps ? pProps->pSpawnProperties : NULL;
	int iPartitionIdx = pState->iPartitionIdx;
	PartitionEncounterData *pPartitionEncounterData = encounter_GetPartitionData(iPartitionIdx);
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);

	// This test is here to avoid waves that generate endless entities
	if (pEncounter->pCachedTemplate && (encounter_GetNumLivingEnts(pEncounter, pState) < 100)) {
		CritterDef **eaUnderlingList = NULL;
		CritterDef **eaExcludeList = NULL;	// CritterDefs that shouldn't be spawned
		AITeam *pAITeam;
		Entity *pSpawningPlayer;
		AICombatRolesDef *pCombatRolesDef;
		AIJobDesc** eaJobs = NULL;
		EncounterDifficulty eDifficulty = encounter_GetDifficulty(iPartitionIdx, pEncounter);
		int iLevel, iMinLevel=0, iMaxLevel=0, iTeamSize;
		int i, iActorIndex;
		int iNumSpawned = 0;

		// Set up the AI
		pAITeam = aiTeamCreate(pState->iPartitionIdx, NULL, false);

		encounterTemplate_FillAIJobEArray(pEncounter->pCachedTemplate, &eaJobs);
		aiTeamAddJobs(pAITeam, eaJobs, pEncounter->pCachedTemplate->pcFilename);
		eaDestroy(&eaJobs);

		pCombatRolesDef = encounterTemplate_GetCombatRolesDef(pEncounter->pCachedTemplate);
		if (pCombatRolesDef) {
			aiCombatRole_TeamSetCombatRolesDef(pAITeam, pCombatRolesDef->pchName);
		}
		
		// Collect useful information
		pSpawningPlayer = entFromEntityRef(iPartitionIdx, pState->playerData.uSpawningPlayer);
		encounterTemplate_GetLevelRange(pEncounter->pCachedTemplate, pSpawningPlayer, iPartitionIdx, &iMinLevel, &iMaxLevel, pEncounter->vPos);
		iTeamSize = pSpawnProps && pSpawnProps->eForceTeamSize > 0 ? pSpawnProps->eForceTeamSize : encounter_GetActivePlayerCount(iPartitionIdx, pEncounter, pSpawningPlayer);
		iTeamSize = CLAMP(iTeamSize,0,MAX_TEAM_SIZE);

		// Pick a level to spawn at
		iLevel = randomIntRange(iMinLevel, iMaxLevel);

		// TODO: Sort actors in power order so underlings logic works right

		// Spawn the actors
		iActorIndex = 0;
		for(i=0; i<eaSize(&pProps->eaActors); ++i) {
			EncounterActorProperties *pActor = NULL;
			if (pProps->bFillActorsInOrder) {
				// find the next valid actor on the template
				pActor = encounterTemplate_GetActorByIndex(pEncounter->pCachedTemplate, iActorIndex);
				while(pActor && !encounterTemplate_GetActorEnabled(pEncounter->pCachedTemplate, pActor, iTeamSize, eDifficulty)) {
					iActorIndex++;
					pActor = encounterTemplate_GetActorByIndex(pEncounter->pCachedTemplate, iActorIndex);
				}
				// If no actors left, then we're done spawning
				if (!pActor) {
					pProps->eaActors[i]->iActorIndex = -1;
					pProps->eaActors[i]->bActorIndexSet = false;
				} else {
					// increment the index for the next cycle of the outer loop
					pProps->eaActors[i]->iActorIndex = iActorIndex;
					pProps->eaActors[i]->bActorIndexSet = true;
					iActorIndex++;
					if (s_uEditingClient) {
						Entity* pClientEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, s_uEditingClient);
						if (pClientEnt) {
							ClientCmd_encounter_UpdateActorIndex(pClientEnt, pEncounter->pcName, pProps->eaActors[i]->pcName, pProps->eaActors[i]->iActorIndex, pProps->eaActors[i]->bActorIndexSet);
						}
					}
				}
			} else {
				pActor = encounterTemplate_GetActorByName(pEncounter->pCachedTemplate, pProps->eaActors[i]->pcName);
				pProps->eaActors[i]->iActorIndex = -1;
				pProps->eaActors[i]->bActorIndexSet = false;
			}

			if (pActor && encounterTemplate_GetActorEnabled(pEncounter->pCachedTemplate, pActor, iTeamSize, eDifficulty)) {
				bool bSpawned = encounter_SpawnActor(pEncounter, pState, pEncounter->pCachedTemplate, pActor, pProps->eaActors[i], i, iLevel, pSpawningPlayer, 
													iTeamSize, pAITeam, &eaUnderlingList, &eaExcludeList,
													 &(pPartitionEncounterData->eaPetContactNameExcludeList), pCombatRolesDef);

				if (bSpawned) {
					++iNumSpawned;
				}
			}
		}

		// Set up tracking data
		pState->iNumEntsSpawned = iNumSpawned;
		pState->iNumTimesSpawned++;
		pState->timerData.uTimeLastSpawned = timeSecondsSince2000();

		// Reset event counts
		stashForEachElement(pState->eventData.stEventLogSinceSpawn, encounter_ResetEventCounts);
		encounter_StateTransition(pEncounter, pState, EncounterState_Spawned);

		// Clean up
		eaDestroy(&eaUnderlingList);
		eaDestroy(&eaExcludeList);
		if (!aiTeamGetMemberCount(pAITeam)) {
			aiTeamDestroy(pAITeam);
		}
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}


// Will return true (and throw an error) if SECONDS_TO_WAIT_FOR_OWNER has passed since this encounter 
// first called this function with an uninitialized entity and the entity is still not initialized.
static bool encounter_PreSpawnCheckPlayerInitTimeout(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	if (!pState->playerData.uWaitingForInitializationTimestamp) {
		pState->playerData.uWaitingForInitializationTimestamp = timeSecondsSince2000();

	} else if(timeSecondsSince2000() > pState->playerData.uWaitingForInitializationTimestamp + SECONDS_TO_WAIT_FOR_OWNER) {
		ErrorDetailsf("Map:%s, Encounter:%s, Time spent waiting on initialization:%d seconds", 
			zmapInfoGetPublicName(NULL), pEncounter->pcName, SECONDS_TO_WAIT_FOR_OWNER);
		Errorf("Encounter Spawning: Timeout waiting for a spawning player to be initialized. Spawning with uninitialized player.");
		return true;
	}
	return false;
}


// Check that this encounter is allowed to try to spawn.
// If this check fails, the encounter will keep trying to spawn every time a player enters its radius
// Compare with encounter_ValidateSpawnConditions below (which shuts the encounter off if it fails)
static bool encounter_ValidatePreSpawnConditions(GameEncounter *pEncounter, GameEncounterPartitionState *pState, bool bNearCutscene)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	Expression *pSpawnCond = NULL;
	WorldEncounterSpawnCondType eSpawnType = WorldEncounterSpawnCondType_Normal;
	Entity ***peaNearbyPlayers;
	bool bIsAmbush = false;
	EncounterLevelProperties* pLevelProp = NULL;
	int i;
	PerfInfoGuard* piGuardFunc;
	PerfInfoGuard* piGuard;

	if (!pProps) {
		return false;
	}

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuardFunc);
	
	// Update List of nearby players and cutscenes
	peaNearbyPlayers = encounter_GetNearbyPlayers(pState->iPartitionIdx, pEncounter, pEncounter->fSpawnRadius);
	
	if (pState->eState != EncounterState_Waiting &&
		pState->eState != EncounterState_GroupManaged &&
		!eaSize(peaNearbyPlayers) && 
		!bNearCutscene &&
		!g_encounterIgnoreProximity) {
		PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
		return false;
	}

	// Can't spawn if asleep, has no nearby players, and no nearby cutscene
	if ((pState->eState == EncounterState_Asleep) && !eaSize(peaNearbyPlayers) && !bNearCutscene) {
		// Clear the waiting timer
		pState->playerData.uWaitingForInitializationTimestamp = 0;
		PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
		return false;
	}

	// If spawn properties, use them.  Else use defaults;
	if (pProps->pSpawnProperties) {
		pSpawnCond = pProps->pSpawnProperties->pSpawnCond;
		eSpawnType = pProps->pSpawnProperties->eSpawnCondType;
	}

	// Clear the spawning player ref to make sure that the entity is current
	pState->playerData.uSpawningPlayer = 0;

	pLevelProp =  encounterTemplate_GetLevelProperties(pEncounter->pCachedTemplate);

	if (bIsAmbush || eSpawnType == WorldEncounterSpawnCondType_RequiresPlayer || 
		(pLevelProp && pLevelProp->eLevelType == EncounterLevelType_PlayerLevel) )
	{
		// Get in here if encounter spawn condition requires player or encounter spawns
		// at the player's level.  We need to get a player for processing.
		bool bAnyPlayerPassesSpawnCond = false;

		PERFINFO_AUTO_START_GUARD("CheckRequiresPlayer", 1, &piGuard);

		// If requires player or is an ambush, then evaluate until find at least one valid player
		for(i=eaSize(peaNearbyPlayers)-1; i>=0; --i) {
			Entity *pEnt = (*peaNearbyPlayers)[i];
			if (!pSpawnCond || (encounter_ExprPlayerEvaluate(pState->iPartitionIdx, pEncounter, pEnt, pSpawnCond) &&
				(!bIsAmbush || encounter_TryPlayerAmbush(pEnt)))) {
				bAnyPlayerPassesSpawnCond = true;
				if (SAFE_MEMBER2(pEnt,pChar,bLoaded)) {
					// Set the spawning player
					pState->playerData.uSpawningPlayer = pEnt->myRef;
					break;
				}
			}
		}

		PERFINFO_AUTO_STOP_GUARD(&piGuard); // CheckRequiresPlayer

		if (i<0) {
			// No valid player found
			if (bAnyPlayerPassesSpawnCond) {
				if (!encounter_PreSpawnCheckPlayerInitTimeout(pEncounter, pState)) {
					PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
					return false;
				}
			}
			pState->playerData.uWaitingForInitializationTimestamp = 0;
			PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
			return false;
		}

	} else if (pSpawnCond) {
		PERFINFO_AUTO_START_GUARD("CheckSpawnCondition", 1, &piGuard);

		// For simple spawn condition, just evaluate once
		if (!encounter_ExprEvaluate(pState->iPartitionIdx, pEncounter, pSpawnCond)) {
			PERFINFO_AUTO_STOP_GUARD(&piGuard);
			PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
			return false;
		}

		if (eaSize(peaNearbyPlayers)) {
			// If any nearby players, make the first initialized one be the spawning player
			for (i=0; i < eaSize(peaNearbyPlayers); i++) {
				Entity* pEnt = (*peaNearbyPlayers)[i];
				if (SAFE_MEMBER2(pEnt,pChar,bLoaded)) {
					pState->playerData.uSpawningPlayer = pEnt->myRef;
					break;
				}
			}
		} else {
			pState->playerData.uWaitingForInitializationTimestamp = 0;
		}

		PERFINFO_AUTO_STOP_GUARD(&piGuard);
	} else if (!bNearCutscene) {
		int iNearbyPlayersSize;

		PERFINFO_AUTO_START_GUARD("CheckInitialPlayer", 1, &piGuard);

		// If no spawn condition is set, spawn only if an initialized player is within specified radius
		iNearbyPlayersSize = eaSize(peaNearbyPlayers);
		if (iNearbyPlayersSize) {
			for (i=0; i < iNearbyPlayersSize; i++) {
				Entity* pEnt = (*peaNearbyPlayers)[i];
				if (SAFE_MEMBER2(pEnt,pChar,bLoaded)) {
					pState->playerData.uSpawningPlayer = pEnt->myRef;
					break;
				}
			}

			if (i == iNearbyPlayersSize) {
				if (!encounter_PreSpawnCheckPlayerInitTimeout(pEncounter, pState)) {
					PERFINFO_AUTO_STOP_GUARD(&piGuard);
					PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
					return false;
				}
			}
		} else {
			pState->playerData.uWaitingForInitializationTimestamp = 0;
			PERFINFO_AUTO_STOP_GUARD(&piGuard);
			PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
			return false;
		}

		PERFINFO_AUTO_STOP_GUARD(&piGuard);
	}

	PERFINFO_AUTO_START_GUARD("CheckCostumes", 1, &piGuard);
	// Delay encounter from spawning if it has costume overrides that are not present yet
	// This can happen with UGC content where the costumes load up later
	for(i=eaSize(&pProps->eaActors)-1; i>=0; --i) {
		WorldActorProperties *pWorldActor = pProps->eaActors[i];

		if (pWorldActor && pWorldActor->pCostumeProperties) {
			if (!GET_REF(pWorldActor->pCostumeProperties->hCostume)) {
				const char *pcName = REF_STRING_FROM_HANDLE(pWorldActor->pCostumeProperties->hCostume);
				if (pcName) {
					if(!pWorldActor->pCostumeProperties->bHasErrored) {
						pWorldActor->pCostumeProperties->bHasErrored = true;
						Errorf("Critter cosutme \"%s\" does not exist, critter will not spawn.", pcName);
					}
					PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
					return false;
				}
			}
		}
	}
	PERFINFO_AUTO_STOP_GUARD(&piGuard);

	PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
	return true;
}


static bool encounter_ValidatePreSpawnConditionsAndSpawnDelay(GameEncounter *pEncounter, GameEncounterPartitionState *pState, bool bNearCutscene)
{
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);

	if (encounter_ValidatePreSpawnConditions(pEncounter, pState, bNearCutscene)) {
		// If it has not been valid to spawn and now it is, start the spawn delay
		if (!pState->timerData.bSpawnDelayCountdown) {
			WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
			pState->timerData.bSpawnDelayCountdown = true;
			pState->timerData.fSpawnDelayTimer = ((pProps && pProps->pSpawnProperties) ? pProps->pSpawnProperties->fSpawnDelay : 0);
		}

		PERFINFO_AUTO_STOP_GUARD(&piGuard);
		return (pState->timerData.fSpawnDelayTimer <= 0);
	}

	//If spawning was valid and is no longer valid, reset the delay timer.
	if (pState->timerData.bSpawnDelayCountdown) {
		WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
		pState->timerData.bSpawnDelayCountdown = false;
		pState->timerData.fSpawnDelayTimer = ((pProps && pProps->pSpawnProperties) ? pProps->pSpawnProperties->fSpawnDelay : 0);
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuard);
	return false;
}


// returns true if no active encounter was within the given lockout radius
static bool encounter_checkLockoutRadius(int iPartitionIdx, GameEncounter **eaTestEncounters, GameEncounter *pEncounter,
											F32 lockoutRadius, bool bIgnoreMastermind)
{
	S32 i;
	Vec3 vEncPos, vTestPos;
	
	lockoutRadius = SQR(lockoutRadius);

	encounter_GetPosition(pEncounter, vEncPos);

	for(i=eaSize(&eaTestEncounters)-1; i>=0; --i) {
		GameEncounter *pTestEncounter = eaTestEncounters[i];
		GameEncounterPartitionState *pTestState;
		if (pTestEncounter == pEncounter) {
			continue;
		}

		pTestState = encounter_GetPartitionState(iPartitionIdx, pTestEncounter);
		if ((pTestState->eState == EncounterState_Spawned) || 
			(pTestState->eState == EncounterState_Active) || 
			(pTestState->eState == EncounterState_Aware)) {

			if (bIgnoreMastermind && 
				pTestEncounter->pWorldEncounter->properties && 
				pTestEncounter->pWorldEncounter->properties->pSpawnProperties && 
				pTestEncounter->pWorldEncounter->properties->pSpawnProperties->eMastermindSpawnType != WorldEncounterMastermindSpawnType_None) {
				continue; // ignore mastermind spawn types if told to
			}

			encounter_GetPosition(pTestEncounter, vTestPos);
			if (lockoutRadius > distance3Squared(vEncPos, vTestPos)) {
				return false;
			}
		}
	}
	
	return true;
}

static bool encounter_ValidateLogicalGroupConditionsForEncounter(LogicalGroupProperties *pLogicalGroupProps, GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	// Check for possible lockout
	// Can't spawn if another encounter in this group is already spawned within the radius
	if (pLogicalGroupProps->encounterSpawnProperties.fLockoutRadius > 0) {

		if (!encounter_checkLockoutRadius(pState->iPartitionIdx, pEncounter->pEncounterGroup->eaGameEncounters, pEncounter, pLogicalGroupProps->encounterSpawnProperties.fLockoutRadius, false)) {
			strcpy(pState->debugStatus, "Lockout in group");
			return false;
		}
	}
	return true;
}

static int encounter_GetSpawnCountInLogicalGroup(int iPartitionIdx, LogicalGroupProperties *pLogicalGroupProps, GameEncounterGroup *pEncounterGroup, bool bIncludeEncountersWithSpawnTimer)
{
	int iNumSpawned = 0;
	switch (pLogicalGroupProps->encounterSpawnProperties.eRandomType) {
		xcase LogicalGroupRandomType_OnceOnLoad:
		acase LogicalGroupRandomType_Continuous:
		{
			int i;
			// Figure out how many are already spawned
			for(i=eaSize(&pEncounterGroup->eaGameEncounters)-1; i>=0; --i) {
				GameEncounter *pTestEncounter = pEncounterGroup->eaGameEncounters[i];
				GameEncounterPartitionState *pTestState = encounter_GetPartitionState(iPartitionIdx, pTestEncounter);
				if ((pTestState->eState == EncounterState_Spawned) || (pTestState->eState == EncounterState_Active) || (pTestState->eState == EncounterState_Aware)) {
					++iNumSpawned;
				} else if (bIncludeEncountersWithSpawnTimer && pTestState->timerData.fSpawnTimer > 0.0f) {
					++iNumSpawned;
				}
			}
			
		}
	}
	return iNumSpawned;
}

static bool encounter_ValidateLogicalGroupConditions(int iPartitionIdx, LogicalGroupProperties *pLogicalGroupProps, GameEncounterGroup *pEncounterGroup)
{
	// Check if too many in group are already spawned
	switch (pLogicalGroupProps->encounterSpawnProperties.eRandomType) {
		xcase LogicalGroupRandomType_OnceOnLoad:
		acase LogicalGroupRandomType_Continuous:
		{
			int iNumToSpawn, iNumSpawned;
			// Figure out how many encounters should spawn
			if (pLogicalGroupProps->encounterSpawnProperties.eSpawnAmountType == LogicalGroupSpawnAmountType_Percentage) {
				iNumToSpawn = ((F32)pLogicalGroupProps->encounterSpawnProperties.uSpawnAmount/100.f) * eaSize(&pEncounterGroup->eaGameEncounters);
			} else {
				iNumToSpawn = (int)pLogicalGroupProps->encounterSpawnProperties.uSpawnAmount;
			}

			iNumSpawned = encounter_GetSpawnCountInLogicalGroup(iPartitionIdx, pLogicalGroupProps, pEncounterGroup, false);
			if (iNumSpawned >= iNumToSpawn) {
				return false;
			}
		}
	}
	return true;
}


// Check that this encounter is allowed to spawn.
// If this check fails, the encounter will shut down and can't be spawned again until its respawn timer
// has elapsed (if this map supports respawn).
// Compare with encounter_ValidatePreSpawnConditions above (which keeps being tested until it's true)
static bool encounter_ValidateSpawnConditions(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);

	// Check logical group restrictions
	if (pEncounter->pEncounterGroup && 
		(eaSize(&pEncounter->pEncounterGroup->eaGameEncounters) > 1) && 
		pEncounter->pEncounterGroup->pWorldGroup && 
		pEncounter->pEncounterGroup->pWorldGroup->properties) {

		LogicalGroupProperties *pLogicalGroup = pEncounter->pEncounterGroup->pWorldGroup->properties;
		if (!encounter_ValidateLogicalGroupConditionsForEncounter(pLogicalGroup, pEncounter, pState)) {
			PERFINFO_AUTO_STOP_GUARD(&piGuard);
			return false;
		}
	}

	// Check to see if the encounter will go off
	if (pProps && pProps->pSpawnProperties) {
		
		if (pProps->pSpawnProperties->fLockoutRadius > 0) {
			GameEncounter **eaNearbyEncounters;

			eaNearbyEncounters = encounter_GetEncountersWithinDistance(pEncounter->vPos, pProps->pSpawnProperties->fLockoutRadius);

			if (!encounter_checkLockoutRadius(pState->iPartitionIdx, eaNearbyEncounters, pEncounter, pProps->pSpawnProperties->fLockoutRadius, true)) {
				strcpy(pState->debugStatus, "Lockout in group");
				PERFINFO_AUTO_STOP_GUARD(&piGuard);
				return false;
			}
		}

		if (pProps->pSpawnProperties->fSpawnChance < 100) {
			U32 uProbRoll = 1 + (randomInt() % 100);
			U32 uProbability = CLAMP(pProps->pSpawnProperties->fSpawnChance, 0, 100);
			if (uProbRoll > uProbability) {
				sprintf(pState->debugStatus, "Failed Roll: %i > %i", uProbRoll, uProbability);
				PERFINFO_AUTO_STOP_GUARD(&piGuard);
				return false;
			}
		}
	}
	
	PERFINFO_AUTO_STOP_GUARD(&piGuard);
	return true;
}


static void encounter_SetWaveSpawnTimer(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	EncounterWaveProperties* pWaveProps = NULL;

	if (pEncounter->bIsWave) {
		if (pProps && pProps->pSpawnProperties && pProps->pSpawnProperties->pWaveProps) {
			pWaveProps = pProps->pSpawnProperties->pWaveProps;
		} else  {
			pWaveProps = encounterTemplate_GetWaveProperties(pEncounter->pCachedTemplate);
		}

		if (pWaveProps) {
			F32 fWaveInterval = encounter_GetWaveTimerValue(pWaveProps->eWaveIntervalType, pWaveProps->fWaveInterval, pEncounter->pWorldEncounter->encounter_pos);

			// Make sure that the initial spawn counts as the "first" wave
			if (pWaveProps->pWaveCond && (fWaveInterval > 0)) {
				pState->timerData.fSpawnTimer = fWaveInterval;
				pState->bWaveReady = false;
			}
		}
	}
}

static void encounter_UpdateStateWaiting(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	PerfInfoGuard *piGuard;
	bool bNearCutscene;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);

	bNearCutscene = cutscene_GetNearbyCutscenes(pState->iPartitionIdx, pEncounter->pWorldEncounter->encounter_pos, pEncounter->fSpawnRadius);

	// Only check when phase count is high enough
	// This makes spawn check less frequent (currently 3 phases = 1 per second)
	if (pState->timerData.uSpawnPhasesSkipped < MIN_PHASES_FOR_SPAWN_CHECK && !bNearCutscene) {
		++pState->timerData.uSpawnPhasesSkipped;
		PERFINFO_AUTO_STOP_GUARD(&piGuard);
		return;
	}
	pState->timerData.uSpawnPhasesSkipped = 0;

	if (encounter_ValidatePreSpawnConditionsAndSpawnDelay(pEncounter, pState, bNearCutscene)) {

		if (encounter_ValidateSpawnConditions(pEncounter, pState)) {

			// Spawn the Encounter;
			s_bEnableSpawnAggroDelay = true;
			encounter_SpawnEncounter(pEncounter, pState);
			s_bEnableSpawnAggroDelay = false;

			// Adjust wave timing
			encounter_SetWaveSpawnTimer(pEncounter, pState);
		} else {
			// Turn off the encounter
			encounter_Complete(pEncounter, pState, EncounterState_Off);
		}
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}


static void encounter_TrySleep(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	static Entity **s_eaNearbyPlayers = NULL;

	Vec3 vAvgPos = {0,0,0};
	Vec3 vEntPos;
	int i, n;
	F32 fMaxDist;
	F32 fTempDist;
	PerfInfoGuard *piGuard;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuard);

	// Figure out average center of the encounter
	n = eaSize(&pState->eaEntities);
	for(i=n-1; i>=0; --i) {
		entGetPos(pState->eaEntities[i], vEntPos);
		addToVec3(vEntPos, vAvgPos);
	}

	// We add the encounter center to weight back toward where encounter should be
	addToVec3(pEncounter->pWorldEncounter->encounter_pos, vAvgPos);
	n++;
	vAvgPos[0] /= n;
	vAvgPos[1] /= n;
	vAvgPos[2] /= n;

	// Start the max at the spawn radius of the encounter
	fMaxDist = 20 + pEncounter->fSpawnRadius;
	// Get the largest distance any critter is from this center
	for(i=eaSize(&pState->eaEntities)-1; i>=0; --i) {
		entGetPos(pState->eaEntities[i], vEntPos);

		fTempDist = distance3(vAvgPos, vEntPos);
		fTempDist += pState->eaEntities[i]->fEntitySendDistance;

		fMaxDist = MAX(fTempDist, fMaxDist);
	}

	// Also check the distance from the encounter to the center
	fTempDist = distance3(vAvgPos, pEncounter->vPos);
	fMaxDist = MAX(fTempDist, fMaxDist);

	// If max distance is over 500, then never sleep
	if (fMaxDist <= 500) {
		entGridProximityLookupExEArray(pState->iPartitionIdx, vAvgPos, &s_eaNearbyPlayers, fMaxDist, ENTITYFLAG_IS_PLAYER, 0, NULL);

		if (!eaSize(&s_eaNearbyPlayers)) {
			if (g_EventLogDebug) {
				printf("No players near encounter %s, going to sleep.\n", pEncounter->pcName);
			}

			// Actually put the encounter to sleep
			encounter_Reset(pEncounter, pState);
			encounter_StateTransition(pEncounter, pState, EncounterState_Asleep);
		}
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}


static void encounter_UpdateStateRunning(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	bool bCanSleep = true;
	PerfInfoGuard* piGuardFunc;
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuardFunc);

	// If spawned but not yet active, become active if player or cut scene within range
	// Only check when phase count is high enough
	// This makes active check less frequent (currently 6 phases = 1 per 2 seconds)
	if ((pState->eState == EncounterState_Spawned) && (pState->timerData.uActivePhasesSkipped >= MIN_PHASES_FOR_ACTIVE_CHECK)) {
		bool bBecomeActive = g_encounterIgnoreProximity;

		PERFINFO_AUTO_START_GUARD("encounter_UpdateStateRunning - Check Active", 1, &piGuard);

		if (!bBecomeActive) {
			Entity ***peaNearbyPlayers = encounter_GetNearbyPlayers(pState->iPartitionIdx, pEncounter, encounter_GetActiveRadius(pEncounter->pWorldEncounter->encounter_pos));
			bBecomeActive = (eaSize(peaNearbyPlayers) > 0);
		}
		if (!bBecomeActive && cutscene_GetNearbyCutscenes(pState->iPartitionIdx, pEncounter->vPos, pEncounter->fSpawnRadius)) {
			bBecomeActive = true;
		}
		if (bBecomeActive) {
			encounter_StateTransition(pEncounter, pState, EncounterState_Active);
		}

		pState->timerData.uActivePhasesSkipped = 0;

		PERFINFO_AUTO_STOP_GUARD(&piGuard);
	} else {
		++pState->timerData.uActivePhasesSkipped;
	}


	PERFINFO_AUTO_START_GUARD("encounter_UpdateStateRunning - Check sleep", 1, &piGuard);

	if (g_encounterDisableSleeping ||
		g_encounterIgnoreProximity ||
		pEncounter->bIsNoDespawn ||
		pState->bShouldBeGroupManaged ||
		cutscene_GetNearbyCutscenes(pState->iPartitionIdx, pEncounter->vPos, pEncounter->fSpawnRadius)) {
		bCanSleep = false;
	} 
	if (bCanSleep) {
		// Try to go to sleep
		encounter_TrySleep(pEncounter, pState);

		if (pState->eState == EncounterState_Asleep) {
			PERFINFO_AUTO_STOP_GUARD(&piGuard);
			PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
			// If we've slept stop evaluating, otherwise it might erroneously complete a newly spawned-but-asleep encounter
			return;
		}
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuard);

	PERFINFO_AUTO_START_GUARD("encounter_UpdateStateRunning - Check Completion", 1, &piGuard);

	// Check success condition first
	if (pProps->pEventProperties && pProps->pEventProperties->pSuccessCond) {
		if (encounter_ExprEvaluate(pState->iPartitionIdx, pEncounter, pProps->pEventProperties->pSuccessCond)) {
			encounter_Complete(pEncounter, pState, EncounterState_Success);
		}
	} else if (encounter_AreAllCombatantsDead(pEncounter, pState)) {
		encounter_Complete(pEncounter, pState, EncounterState_Success);
	}

	// If not success, then check for failure
	if ((pState->eState != EncounterState_Success) && pProps->pEventProperties && pProps->pEventProperties->pFailureCond) {
		if (encounter_ExprEvaluate(pState->iPartitionIdx, pEncounter, pProps->pEventProperties->pFailureCond)) {
			encounter_Complete(pEncounter, pState, EncounterState_Failure);
		}
	}

	// If no "normal" reset, check despawn condition
	if(pProps->pSpawnProperties && pProps->pSpawnProperties->pDespawnCond) {
		if (encounter_ExprEvaluate(pState->iPartitionIdx, pEncounter, pProps->pSpawnProperties->pDespawnCond)) {
			encounter_Complete(pEncounter, pState, EncounterState_Off);
			eaDestroyEx(&pState->eaEntities, gslQueueEntityDestroy);
			PERFINFO_AUTO_STOP_GUARD(&piGuard);
			PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
			return;
		}
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuard);

	// If not success or failure, then check for wave reinforce
	// This happens if the spawn timer has run down and if the wave data has a valid condition and an interval
	if (pEncounter->bIsWave) {
		PERFINFO_AUTO_START_GUARD("encounter_UpdateStateRunning - Check Wave", 1, &piGuard);

		if ((pState->eState != EncounterState_Success) && 
			(pState->eState != EncounterState_Failure) && 
			(pState->timerData.fSpawnTimer <= 0)) {

			EncounterTemplate *pTemplate = GET_REF(pProps->hTemplate);
			EncounterWaveProperties *pWaveProps = NULL;
			F32 fWaveInterval = 0;

			if(pProps && pProps->pSpawnProperties && pProps->pSpawnProperties->pWaveProps)
				pWaveProps = pProps->pSpawnProperties->pWaveProps;
			else 
				pWaveProps = encounterTemplate_GetWaveProperties(pTemplate);
		
			if(pWaveProps)
				fWaveInterval = encounter_GetWaveTimerValue(pWaveProps->eWaveIntervalType, pWaveProps->fWaveInterval, pEncounter->pWorldEncounter->encounter_pos);

			if (pWaveProps && pWaveProps->pWaveCond && (fWaveInterval > 0)) {
			
				if (pState->bWaveReady) {
					// If the encounter is already scheduled to spawn, just do it.
					encounter_SpawnEncounter(pEncounter, pState);
					pState->timerData.fSpawnTimer = fWaveInterval;
					pState->bWaveReady = false;

				} else if (encounter_ExprEvaluate(pState->iPartitionIdx, pEncounter, pWaveProps->pWaveCond)) {
					int iDelay;
					F32 fWaveDelayMin, fWaveDelayMax;
					encounter_GetWaveDelayTimerValue(pWaveProps->eWaveDelayType, pWaveProps->fWaveDelayMin, pWaveProps->fWaveDelayMax, 
													pEncounter->pWorldEncounter->encounter_pos, &fWaveDelayMin, &fWaveDelayMax);
					iDelay = randomIntRange(fWaveDelayMin, fWaveDelayMax);
					if (iDelay) { 
						// Schedule the encounter to spawn after a delay
						pState->bWaveReady = true;
						pState->timerData.fSpawnTimer = iDelay;
					} else { 
						// spawn the encounter right away
						encounter_SpawnEncounter(pEncounter, pState);
						pState->timerData.fSpawnTimer = fWaveInterval;
					}

				} else {
					// It's time to spawn, but condition hasn't been met yet.  Poll until condition is met.
					pState->timerData.fSpawnTimer = WAVE_ATTACK_POLL_INTERVAL;
				}
			}
		}

		PERFINFO_AUTO_STOP_GUARD(&piGuard);
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
}


static void encounter_UpdateStateComplete(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	PerfInfoGuard* piGuard;

	PERFINFO_AUTO_START_GUARD("encounter_UpdateStateComplete", 1, &piGuard);

	if (pEncounter->fRespawnTime < 0) {
		// Should never respawn, so become disabled
		encounter_StateTransition(pEncounter, pState, EncounterState_Disabled);

	} else if (pState->timerData.fSpawnTimer <= 0) {
		// If the encounter is complete and it is set to respawn, then reset it
		encounter_Reset(pEncounter, pState);
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuard);
}

static void encounter_UpdateLogicalGroup(PartitionEncounterData* pData, GameEncounterGroup* pEncounterGroup)
{
	LogicalGroupProperties* pLogicalGroupProps = SAFE_MEMBER2(pEncounterGroup, pWorldGroup, properties);
	GameEncounter** eaSubEncList = NULL;
	PerfInfoGuard* piGuard;
	int i, iNumSpawned, iNumToSpawn;

	if (!pLogicalGroupProps)
		return;

	// Figure out how many encounters should spawn
	if (pLogicalGroupProps->encounterSpawnProperties.eSpawnAmountType == LogicalGroupSpawnAmountType_Percentage) {
		iNumToSpawn = ((F32)pLogicalGroupProps->encounterSpawnProperties.uSpawnAmount/100.f) * eaSize(&pEncounterGroup->eaGameEncounters);
	} else {
		iNumToSpawn = (int)pLogicalGroupProps->encounterSpawnProperties.uSpawnAmount;
	}

	// If the max number of encounters have already spawned, then there's no need to update
	iNumSpawned = encounter_GetSpawnCountInLogicalGroup(pData->iPartitionIdx, pLogicalGroupProps, pEncounterGroup, true);
	if (iNumSpawned >= iNumToSpawn) {
		return;
	}

	PERFINFO_AUTO_START_GUARD("encounter_UpdateLogicalGroup", 1, &piGuard);

	for (i = eaSize(&pEncounterGroup->eaGameEncounters)-1; i >= 0; i--) {
		GameEncounter* pEncounter = pEncounterGroup->eaGameEncounters[i];
		GameEncounterPartitionState* pState = encounter_GetPartitionState(pData->iPartitionIdx, pEncounter);
		bool bNearCutscene = cutscene_GetNearbyCutscenes(pState->iPartitionIdx, pEncounter->pWorldEncounter->encounter_pos, pEncounter->fSpawnRadius);

		switch (pState->eState) {
			xcase EncounterState_GroupManaged:
			{
				if (encounter_ValidatePreSpawnConditionsAndSpawnDelay(pEncounter, pState, bNearCutscene)) {
					if (encounter_ValidateSpawnConditions(pEncounter, pState)) {
						eaPush(&eaSubEncList, pEncounter);
					}
				}
			}
		}
	}

	while (eaSize(&eaSubEncList) && iNumSpawned < iNumToSpawn) {
		int iRandom = randomIntRange(0, eaSize(&eaSubEncList) - 1);
		GameEncounter* pSpawnEncounter = eaRemoveFast(&eaSubEncList, iRandom);
		GameEncounterPartitionState* pSpawnState = encounter_GetPartitionState(pData->iPartitionIdx, pSpawnEncounter);
		
		encounter_SpawnEncounter(pSpawnEncounter, pSpawnState);
		encounter_SetWaveSpawnTimer(pSpawnEncounter, pSpawnState);
		iNumSpawned++;

		// If this logical group has a lockout radius
		if (pLogicalGroupProps->encounterSpawnProperties.fLockoutRadius > 0.0f) {
			F32 fRadius = pLogicalGroupProps->encounterSpawnProperties.fLockoutRadius;
			F32 fRadiusSquared = fRadius * fRadius;
			Vec3 vSpawnPos, vTestPos;
			
			encounter_GetPosition(pSpawnEncounter, vSpawnPos);

			// Remove all others from the list that are within lockout radius of this one
			for (i = eaSize(&eaSubEncList)-1; i>=0; --i) {
				encounter_GetPosition(eaSubEncList[i], vTestPos);
				if (fRadiusSquared > distance3Squared(vSpawnPos, vTestPos)) {
					eaRemoveFast(&eaSubEncList, i);
				}
			}
		}
		
		// Special processing for logical groups that are randomized only once
		if (pLogicalGroupProps->encounterSpawnProperties.eRandomType == LogicalGroupRandomType_OnceOnLoad) {

			PartitionLogicalGroupEncData* pLogicalGroupData;
			pLogicalGroupData = eaIndexedGetUsingString(&pData->eaLogicalGroupData, pEncounterGroup->pcName);
			if (!pLogicalGroupData) {
				pLogicalGroupData = StructCreate(parse_PartitionLogicalGroupEncData);
				pLogicalGroupData->pchLogicalGroup = allocAddString(pEncounterGroup->pcName);
				eaPush(&pData->eaLogicalGroupData, pLogicalGroupData);
			}

			// Add the encounter to the logical group tracking data, if it isn't already in the list
			eaPushUnique(&pLogicalGroupData->ppchSpawnedEncountersOnce, pSpawnEncounter->pcName);

			// If the number of unique encounters exceeds the desired number to spawn, then 
			// this logical group no longer needs to be processed.
			if (eaSize(&pLogicalGroupData->ppchSpawnedEncountersOnce) >= iNumToSpawn) {
				for (i = eaSize(&pEncounterGroup->eaGameEncounters)-1; i >= 0; i--) {
					GameEncounter* pEncounter = pEncounterGroup->eaGameEncounters[i];
					GameEncounterPartitionState* pState = encounter_GetPartitionState(pData->iPartitionIdx, pEncounter);
					pState->bShouldBeGroupManaged = false;

					// If the encounter is not in the list of encounters ever spawned for this logic group, disable it
					if (eaFind(&pLogicalGroupData->ppchSpawnedEncountersOnce, pEncounter->pcName) < 0) {
						encounter_StateTransition(pEncounter, pState, EncounterState_Disabled);
					}
				}
				pLogicalGroupData->bDisableUpdates = true;
				break;
			}
		}
	}

	PERFINFO_AUTO_STOP_GUARD(&piGuard);
	eaDestroy(&eaSubEncList);
}


void encounter_OncePerFrame(F32 fTimeStep)
{
	static F32 s_fLogicalGroupTick = 0.0f;
	F32 fTimeStepThisTick;
	int i;
	int iNumEncounters;
	int iPartitionIdx;
	PerfInfoGuard* piGuardFunc;

	// Skip if no processing on any partition
	if (!g_EncounterProcessing || gbMakeBinsAndExit) {
		return;
	}

	PERFINFO_AUTO_START_FUNC_GUARD(&piGuardFunc);

	// Reset map if some system has requested it.  Flag is turned off after all partitions have run.
	if (g_EncounterResetOnNextTick) {
		game_MapReInit();
		g_EncounterResetOnNextTick = false;

		PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
		return;
	}

	// When doing editing on templates, this gets set to force a re-caching
	if (s_bRecacheTemplates) {
		s_bRecacheTemplates = false;
		FOR_EACH_ENCOUNTER(pEncounter) {
			encounter_CacheTemplate(pEncounter);
		} FOR_EACH_END;
	}

	// Increment the tick
	++s_uTick;

	// Increment the logical group tick
	s_fLogicalGroupTick += fTimeStep;

	// We want to process only a fraction of the encounters each frame
	// To calculate time between processing for each set, we do the following
	for(i=0; i<ENCOUNTER_TICK_COUNT; i++) {
		s_afTimeSinceUpdate[i] += fTimeStep;
	}
	fTimeStepThisTick = s_afTimeSinceUpdate[s_uTick % ENCOUNTER_TICK_COUNT];

	iNumEncounters = eaSize(&s_eaEncounters);

	for(iPartitionIdx=eaSize(&s_eaPartitionEncounterData)-1; iPartitionIdx>=0; --iPartitionIdx) 
	{
		PartitionEncounterData *pData = s_eaPartitionEncounterData[iPartitionIdx];

		// Skip if partition is not ready
		if (!pData) {
			continue;
		}
		if (!pData->bIsRunning) {
			// Wait on this partition until it's able to run
			if (!encounter_IsMapOwnerAvailable(iPartitionIdx) ||
				gslQueue_WaitingForQueueData(iPartitionIdx) ||
				!gslpd_IsMapDifficultyInitialized(iPartitionIdx)) {
				continue;
			}

			// Once we get past the above test, this partition is able to run and can skip testing in future ticks
			pData->bIsRunning = true;
 			pData->uStartTime = timeSecondsSince2000();
		}
		if (mapState_IsMapPausedForPartition(iPartitionIdx)) {
			continue;
		}

		// Tick logical groups every second
		if (s_fLogicalGroupTick >= ENCOUNTER_LOGICAL_GROUP_TICK_RATE) {
			for (i = eaSize(&s_eaEncounterLogicalGroups)-1; i >= 0; i--) {
				GameEncounterGroup* pEncounterGroup = s_eaEncounterLogicalGroups[i];
				PartitionLogicalGroupEncData* pLogicalGroupData;
				pLogicalGroupData = eaIndexedGetUsingString(&pData->eaLogicalGroupData, pEncounterGroup->pcName);

				if (!pLogicalGroupData || !pLogicalGroupData->bDisableUpdates) {
					encounter_UpdateLogicalGroup(pData, pEncounterGroup);
				}
			}
		}

		// Iterate over encounters in this set
		for(i=s_uTick % ENCOUNTER_TICK_COUNT; i<iNumEncounters; i+=ENCOUNTER_TICK_COUNT)
		{
			GameEncounter *pEncounter = s_eaEncounters[i];
			GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
			PerfInfoGuard* piGuard;

			PERFINFO_AUTO_START_GUARD("encounter_UpdateState", 1, &piGuard);
			switch(pState->eState)
			{
				case EncounterState_Asleep:
				case EncounterState_Waiting:
					encounter_UpdateStateWaiting(pEncounter, pState);
					break;
				case EncounterState_Spawned:
				case EncounterState_Active:
				case EncounterState_Aware:
					encounter_CheckFallThroughWorld(pEncounter, pState);
					encounter_UpdateStateRunning(pEncounter, pState);
					break;
				case EncounterState_Success:
				case EncounterState_Failure:
				case EncounterState_Off:
					encounter_UpdateStateComplete(pEncounter, pState);

				// Ignore "EncounterState_Disabled" and "EncounterState_GroupManaged" during processing
			}
			PERFINFO_AUTO_STOP_GUARD(&piGuard);

			PERFINFO_AUTO_START_GUARD("encounter_UpdateSpawnTimer", 1, &piGuard);
			if (pState->timerData.fSpawnTimer > 0) {
				encounter_UpdateDynamicSpawnRate(pEncounter, pState);

				if (pState->timerData.fSpawnRateMultiplier) {
					pState->timerData.fSpawnTimer -= fTimeStepThisTick * pState->timerData.fSpawnRateMultiplier;
				} else {
					pState->timerData.fSpawnTimer -= fTimeStepThisTick;
				}
			}
			if (pState->timerData.bSpawnDelayCountdown && pState->timerData.fSpawnDelayTimer > 0) {
				pState->timerData.fSpawnDelayTimer -= fTimeStepThisTick;
			}
			PERFINFO_AUTO_STOP_GUARD(&piGuard);
		}
	}

	// Clear time on this tick so it can accumulate again
	s_afTimeSinceUpdate[s_uTick % ENCOUNTER_TICK_COUNT] = 0.0f;

	// Reset the logical group tick timer if it is above the tick rate
	if (s_fLogicalGroupTick >= ENCOUNTER_LOGICAL_GROUP_TICK_RATE)
		s_fLogicalGroupTick = 0.0f;

	PERFINFO_AUTO_STOP_GUARD(&piGuardFunc);
}


// ----------------------------------------------------------------------------------
// Encounter Template Loading Logic
// ----------------------------------------------------------------------------------


static void encounter_CacheTemplate(GameEncounter *pEncounter)
{
	WorldEncounterProperties *pProperties = pEncounter->pWorldEncounter ? pEncounter->pWorldEncounter->properties : NULL;

	pEncounter->pCachedTemplate = encounter_GetTemplate(pEncounter);
	pEncounter->bIsDynamicSpawn = encounter_IsDynamicSpawn(pEncounter);
	pEncounter->bIsNoDespawn = encounter_IsNoDespawn(pEncounter) || (zmapInfoGetMapType(NULL) == ZMTYPE_PVP);
	pEncounter->bIsDynamicMastermind = (pProperties->pSpawnProperties && (pProperties->pSpawnProperties->eMastermindSpawnType == WorldEncounterMastermindSpawnType_DynamicOnly));

	if ((pProperties && pProperties->pSpawnProperties && pProperties->pSpawnProperties->pWaveProps) ||
		(encounterTemplate_GetWaveProperties(pEncounter->pCachedTemplate))) {
		pEncounter->bIsWave = true;
	} else {
		pEncounter->bIsWave = false;
	}

	encounter_GetPosition(pEncounter, pEncounter->vPos);
	pEncounter->fSpawnRadius = pProperties ? encounter_GetSpawnRadiusValueFromProperties(pProperties, pEncounter->vPos) : 0;
	pEncounter->eRegionType = worldRegionGetType(worldGetWorldRegionByPos(pEncounter->vPos));
	pEncounter->fRespawnTime = 	encounter_GetRespawnTimerValueFromProperties(pProperties, pEncounter->vPos);
}


static void encounterTemplate_PostProcess(EncounterTemplate *pTemplate)
{
	static ExprContext *s_TemplateContext = NULL;
	ZoneMap *pZoneMap;
	int i,j;

	if (!s_TemplateContext) {
		s_TemplateContext = exprContextCreate();
		exprContextSetFuncTable(s_TemplateContext, encounter_CreateExprFuncTable());
		exprContextSetAllowRuntimePartition(s_TemplateContext);
		// Not allowing Self pointer
	}

	exprContextSetPointerVarPooled(s_TemplateContext, g_EncounterTemplateVarName, pTemplate, parse_EncounterTemplate, false, true);
	exprContextSetPointerVarPooled(s_pEncounterInteractContext, g_EncounterTemplateVarName, pTemplate, parse_EncounterTemplate, false, true);
	exprContextRemoveVarPooled(s_pEncounterInteractContext, g_Encounter2VarName);

	if (pTemplate->pWaveProperties) {
		if (pTemplate->pWaveProperties->pWaveCond) {
			exprGenerate(pTemplate->pWaveProperties->pWaveCond, s_TemplateContext);
		}
	}

	for(i=eaSize(&pTemplate->eaJobs)-1; i>=0; --i) {
		AIJobDesc *pJob = pTemplate->eaJobs[i];
		aiJobGenerateExpressions(pJob);
	}

	for(i=eaSize(&pTemplate->eaActors)-1; i>=0; --i) {
		EncounterActorProperties *pActor = pTemplate->eaActors[i];
		if (pActor->pInteractionProperties) {
			for(j=eaSize(&pActor->pInteractionProperties->eaEntries)-1; j>=0; --j) {
				interaction_InitPropertyEntry(pActor->pInteractionProperties->eaEntries[j], s_pEncounterInteractContext, pTemplate->pcFilename, "EncounterTemplate", pTemplate->pcName, false);
			}
		}

		for(j=eaSize(&pActor->eaVariableDefs)-1; j>=0; --j) {
			WorldVariableDef *pVar = pActor->eaVariableDefs[j];
			worldVariableDefGenerateExpressionsNoPlayer(pVar, "EncounterTemplate", pTemplate->pcFilename);
		}
	}

	// Fixup encounters using this template if it was changed after the map was loaded.
	// This cannot be done in the encounter tick function due to a race condition with AITick
	s_bRecacheTemplates = true;
	FOR_EACH_ENCOUNTER(pEncounter) {
		EncounterTemplate *pSpawnedTemplate = encounter_GetTemplate(pEncounter);
		if (pSpawnedTemplate && encounterTemplate_TemplateExistsInInheritanceChain(pSpawnedTemplate, pTemplate)) {
			// Reset this encounter because its template changed
			// Post process is required in case EventCount expressions on template changed
			int iPartitionIdx;
			for(iPartitionIdx=eaSize(&pEncounter->eaPartitionStates)-1; iPartitionIdx>=0; iPartitionIdx--) {
				GameEncounterPartitionState *pState = pEncounter->eaPartitionStates[iPartitionIdx];
				if (pState) {
					PartitionEncounterData *pData = s_eaPartitionEncounterData[iPartitionIdx];
					encounter_PostProcessPartition(pEncounter, pState, pData);
					encounter_Reset(pEncounter, pState);
				}
			}

			// Clear cached template temporarily
			pEncounter->pCachedTemplate = NULL;
		}
	} FOR_EACH_END;

	// Validate encounters for the the current zone map
	if (pZoneMap = worldGetActiveMap()) {
		encounter_MapValidate(pZoneMap);
	}
}


AUTO_STARTUP(Encounters) ASTRT_DEPS(Critters, AS_Messages, AI, Contacts, EncounterDifficulties);
void encounter_LoadTemplates(void)
{
	encounterTemplate_SetPostProcessCallback(encounterTemplate_PostProcess);

	encounterTemplate_Load();

	if (gConf.bAllowOldEncounterData) {
		oldencounter_LoadEncounterDefs();
	}
}


// ----------------------------------------------------------------------------------
// Encounter Post Processing Logic
// ----------------------------------------------------------------------------------

// Set up a "MatchSource" or "MatchTarget" event for this encounter, if necessary
static GameEvent* encounter_SetUpEncounterScopedEvent(int iPartitionIdx, GameEvent*** peaScopedEvents, GameEvent *ev, const char *pchPooledEncName)
{
	GameEvent* evClone = StructClone(parse_GameEvent, ev);
	
	// Mark event with proper partition and add to subscribe list
	evClone->iPartitionIdx = iPartitionIdx;
	eaPush(peaScopedEvents, evClone);

	if (peaScopedEvents && evClone && pchPooledEncName &&
		evClone->tMatchSource != TriState_DontCare || evClone->tMatchTarget != TriState_DontCare)
	{
		if (evClone->tMatchSource == TriState_Yes){
			evClone->pchSourceStaticEncName = pchPooledEncName;
		} else if (evClone->tMatchSource == TriState_No){
			evClone->pchSourceStaticEncExclude = pchPooledEncName;
		}

		if (evClone->tMatchTarget == TriState_Yes){
			evClone->pchTargetStaticEncName = pchPooledEncName;
		} else if (evClone->tMatchTarget == TriState_No){
			evClone->pchTargetStaticEncExclude = pchPooledEncName;
		}
	}

	return evClone;
}


static void encounter_InitPartition(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	WorldEncounterProperties *pProperties = SAFE_MEMBER(pEncounter->pWorldEncounter, properties);
	EncounterTemplate *pTemplate = pProperties ? GET_REF(pProperties->hTemplate) : NULL;
	GameEvent** eaTracked = NULL;
	GameEvent** eaTrackedSinceSpawn = NULL;
	GameEvent** eaTrackedSinceComplete = NULL;
	int i;

	// Clear out previously tracked event data
	encounter_StopTrackingEvents(pEncounter, pState);

	if (!pProperties || !pTemplate) {
		// Disable encounter if does not have properties or template
		pState->eState = EncounterState_Disabled;
		return;
	}

	if (pProperties->pSpawnProperties) {
		if (pProperties->pSpawnProperties->eMastermindSpawnType != WorldEncounterMastermindSpawnType_None) {
			// if this encounter is to be used for the mastermind system,
			if (pProperties->pSpawnProperties->eMastermindSpawnType == WorldEncounterMastermindSpawnType_DynamicOnly) {
				// it's a dynamic only spawn, force it to the off state, 
				// it will only spawn when told to by the mastermind system
				pState->eState = EncounterState_Off;
			}
		}
	}

	// Copy events from template and base encounter
	encounterTemplate_FillTrackedEventsEarrays(pTemplate, &eaTracked, &eaTrackedSinceSpawn, &eaTrackedSinceComplete);
	for(i=eaSize(&pEncounter->eaUnsharedTrackedEvents)-1; i>=0; --i) {
		eaPush(&eaTracked, pEncounter->eaUnsharedTrackedEvents[i]);
	}
	for(i=eaSize(&pEncounter->eaUnsharedTrackedEventsSinceSpawn)-1; i>=0; --i) {
		eaPush(&eaTrackedSinceSpawn, pEncounter->eaUnsharedTrackedEventsSinceSpawn[i]);
	}
	for(i=eaSize(&pEncounter->eaUnsharedTrackedEventsSinceComplete)-1; i>=0; --i) {
		eaPush(&eaTrackedSinceComplete, pEncounter->eaUnsharedTrackedEventsSinceComplete[i]);
	}

	// Set up events on the partition state
	for(i=eaSize(&eaTracked)-1; i>=0; --i) {
		encounter_SetUpEncounterScopedEvent(pState->iPartitionIdx, &pState->eventData.eaTrackedEvents, eaTracked[i], allocAddString(pEncounter->pcName));
	}
	for(i=eaSize(&eaTrackedSinceSpawn)-1; i>=0; --i) {
		encounter_SetUpEncounterScopedEvent(pState->iPartitionIdx, &pState->eventData.eaTrackedEventsSinceSpawn, eaTrackedSinceSpawn[i], allocAddString(pEncounter->pcName));
	}
	for(i=eaSize(&eaTrackedSinceComplete)-1; i>=0; --i) {
		encounter_SetUpEncounterScopedEvent(pState->iPartitionIdx, &pState->eventData.eaTrackedEventsSinceComplete, eaTrackedSinceComplete[i], allocAddString(pEncounter->pcName));
	}

	// Clean up
	eaDestroy(&eaTracked);
	eaDestroy(&eaTrackedSinceSpawn);
	eaDestroy(&eaTrackedSinceComplete);
}


static void encounter_validateEncounterPosition(PartitionEncounterData *pData, GameEncounter *pGameEncounter)
{
	const WorldVolume **volumes;
	static U32 playableVolumeType = 0;
	
	WorldVolumeEntry **pWorldVolumeEntries = gslPlayableGet();
	if (!gbMakeBinsAndExit && pWorldVolumeEntries && eaSize(&pWorldVolumeEntries) > 0) {
		if (!playableVolumeType) {
			playableVolumeType = wlVolumeTypeNameToBitMask("Playable");
		}

		if (!pData->pPlayableVolumeQuery) {
			pData->pPlayableVolumeQuery = wlVolumeQueryCacheCreate(pData->iPartitionIdx, playableVolumeType, NULL);
		}

		volumes = wlVolumeCacheQuerySphereByType(pData->pPlayableVolumeQuery, pGameEncounter->pWorldEncounter->encounter_pos, 0, playableVolumeType);

		if (eaSize(&volumes) == 0) {
			const char *pcFilename;
			pcFilename = layerGetFilename(pGameEncounter->pWorldEncounter->common_data.layer);
			ErrorFilenamef(pcFilename, "Encounter '%s; is not inside a playable volume.  It will not be beaconized.", pGameEncounter->pcName);
		}
	}
}


static void encounter_PostProcessPartition(GameEncounter *pEncounter, GameEncounterPartitionState *pState, PartitionEncounterData *pData)
{
	WorldEncounterProperties *pProperties = SAFE_MEMBER(pEncounter->pWorldEncounter, properties);
	EncounterTemplate *pTemplate = pProperties ? GET_REF(pProperties->hTemplate) : NULL;

	if (!pProperties || !pTemplate) {
		// Skip if does not have properties or template
		return;
	}

	encounter_validateEncounterPosition(pData, pEncounter);

	if (pProperties->pPresenceCond) {
		gslRequestEntityPresenceUpdate();
	}

	// Start tracking event after everything is ready
	encounter_BeginTrackingEvents(pEncounter, pState);
}


static void encounter_PostProcessBase(GameEncounter *pEncounter)
{
	WorldEncounterProperties *pProperties = SAFE_MEMBER(pEncounter->pWorldEncounter, properties);
	WorldScope *pScope = SAFE_MEMBER(pEncounter->pWorldEncounter, common_data.closest_scope);
	EncounterTemplate *pTemplate = pProperties ? GET_REF(pProperties->hTemplate) : NULL;
	const char *pcFilename = pEncounter && pEncounter->pWorldEncounter ? layerGetFilename(pEncounter->pWorldEncounter->common_data.layer) : NULL;
	int i,j;

	if (!pProperties || !pTemplate) {
		return;
	}

	exprContextSetScope(s_pEncounterContext, pScope);
	exprContextSetPointerVar(s_pEncounterContext, g_Encounter2VarName, pEncounter, parse_GameEncounter, false, true);
	exprContextSetScope(s_pEncounterPlayerContext, pScope);
	exprContextSetPointerVar(s_pEncounterPlayerContext, g_Encounter2VarName, pEncounter, parse_GameEncounter, false, true);
	exprContextSetScope(s_pEncounterInteractContext, pScope);
	exprContextSetPointerVar(s_pEncounterInteractContext, g_Encounter2VarName, pEncounter, parse_GameEncounter, false, true);

	if (pProperties->pSpawnProperties) {
		if (pProperties->pSpawnProperties->pSpawnCond) {
			if (pProperties->pSpawnProperties->eSpawnCondType == WorldEncounterSpawnCondType_RequiresPlayer) {
				exprGenerate(pProperties->pSpawnProperties->pSpawnCond, s_pEncounterPlayerContext);
			} else {
				exprGenerate(pProperties->pSpawnProperties->pSpawnCond, s_pEncounterContext);
			}
		}

		if(pProperties->pSpawnProperties->pDespawnCond) {
			exprGenerate(pProperties->pSpawnProperties->pDespawnCond, s_pEncounterContext);
		}

		if (pProperties->pSpawnProperties->pWaveProps && pProperties->pSpawnProperties->pWaveProps->pWaveCond) {
			exprGenerate(pProperties->pSpawnProperties->pWaveProps->pWaveCond, s_pEncounterContext);
		}
	}

	if (pProperties->pEventProperties) {
		if (pProperties->pEventProperties->pSuccessCond) {
			exprGenerate(pProperties->pEventProperties->pSuccessCond, s_pEncounterContext);
		}
		if (pProperties->pEventProperties->pFailureCond) {
			exprGenerate(pProperties->pEventProperties->pFailureCond, s_pEncounterContext);
		}
		if (pProperties->pEventProperties->pSuccessExpr) {
			exprGenerate(pProperties->pEventProperties->pSuccessExpr, s_pEncounterContext);
		}
		if (pProperties->pEventProperties->pFailureExpr) {
			exprGenerate(pProperties->pEventProperties->pFailureExpr, s_pEncounterContext);
		}
	}

	if (pProperties->pPresenceCond) {
		exprGenerate(pProperties->pPresenceCond, s_pGeneralPlayerContext);
	}

	// Init actor interactions
	for(i=eaSize(&pProperties->eaActors)-1; i>=0; --i) {
		WorldActorProperties *pActor = pProperties->eaActors[i];
		if (pActor->pInteractionProperties) {
			for(j=eaSize(&pActor->pInteractionProperties->eaEntries)-1; j>=0; --j) {
				interaction_InitPropertyEntry(pActor->pInteractionProperties->eaEntries[j], s_pEncounterInteractContext, pcFilename, "GameEncounter", pEncounter->pcName, false);
			}
		}
		for(j=eaSize(&pActor->eaFSMVariableDefs)-1; j>=0; --j) {
			worldVariableDefGenerateExpressionsNoPlayer(pActor->eaFSMVariableDefs[j], "GameEncounter Actor", pcFilename);
		}
	}

	exprContextRemoveVarPooled(s_pEncounterInteractContext, g_EncounterTemplateVarName);
}

static void encounter_PostProcessGroupBase(GameEncounterGroup* pGroup)
{
	WorldLogicalGroup *pWorldGroup = pGroup->pWorldGroup;

	if (pWorldGroup &&
		pWorldGroup->properties &&
		eaSize(&pGroup->eaGameEncounters) > 0) {

		switch (pWorldGroup->properties->encounterSpawnProperties.eRandomType) {
			xcase LogicalGroupRandomType_OnceOnLoad:
			acase LogicalGroupRandomType_Continuous:
			{
				eaPush(&s_eaEncounterLogicalGroups, pGroup);
			}
		}
	}
}

static void encounter_PostProcessAllBase(void)
{
	// Post-process all encounters
	FOR_EACH_ENCOUNTER(pEncounter) {
		encounter_PostProcessBase(pEncounter);
	} FOR_EACH_END;
	// Post-process all encounter groups
	FOR_EACH_ENCOUNTER_GROUP(pGroup) {
		encounter_PostProcessGroupBase(pGroup);
	} FOR_EACH_END;
}


// ----------------------------------------------------------------------------------
// Encounter Load Logic
// ----------------------------------------------------------------------------------


static void encounter_FreePartitionState(GameEncounter *pEncounter, GameEncounterPartitionState *pState)
{
	int i;

	// Stop tracking events and clean up event data
	encounter_StopTrackingEvents(pEncounter, pState);
	eventtracker_Destroy(pState->eventData.pEventTracker, false);
	StructDestroySafe(parse_GameEventParticipant, &pState->eventData.pGameEventInfo);
	pState->eventData.pEventTracker = NULL;

	// Free data
	eaiDestroy(&pState->playerData.eauEntsWithCredit);
	stashTableDestroySafe(&pState->eventData.stEventLog);
	stashTableDestroySafe(&pState->eventData.stEventLogSinceSpawn);
	stashTableDestroySafe(&pState->eventData.stEventLogSinceComplete);

	// Destroy Cached Variables
	eaDestroyStruct(&pState->eaCachedVars, parse_GameEncounterCachedActorVar);

	// Kill all the critters that are not dead yet
	for(i=eaSize(&pState->eaEntities)-1; i>=0; --i) {
		if (pState->eaEntities[i]->pCritter->encounterData.pGameEncounter == pEncounter) {
			gslQueueEntityDestroy(pState->eaEntities[i]);
			gslDestroyEntity(pState->eaEntities[i]);
		} else {
			Errorf("Wrong encounter being used in code and memory could be corrupted!  If you see this, tell Stephen D'Angelo.");
		}
	}
	eaDestroy(&pState->eaEntities);

	// Free the structure itself
	free(pState);
}


static void encounter_FreeEncounter(GameEncounter *pEncounter)
{
	int i;

	// Free data
	StructDestroySafe(parse_WorldEncounter, &pEncounter->pWorldEncounterCopy);

	// Remove the octree entry
	octreeRemove(&pEncounter->octreeEntry);

	// Destroy all the partition states
	for(i=eaSize(&pEncounter->eaPartitionStates)-1; i>=0; --i) {
		GameEncounterPartitionState *pState = pEncounter->eaPartitionStates[i];
		if (pState) {
			encounter_FreePartitionState(pEncounter, pState);
		}
	}
	eaDestroy(&pEncounter->eaPartitionStates);

	free(pEncounter);
}


static void encounter_AddWorldEncounter(const char *pcName, WorldEncounter *pWorldEncounter)
{
	GameEncounter *pEncounter = calloc(1,sizeof(GameEncounter));
	if (pcName) {
		pEncounter->pcName = allocAddString(pcName);
	}
	pEncounter->pWorldEncounter = pWorldEncounter;
	pEncounter->pWorldEncounterCopy = StructClone(parse_WorldEncounter, pWorldEncounter);

	// Set up octree entry for searching
	pEncounter->octreeEntry.node = pEncounter;
	copyVec3(pEncounter->pWorldEncounter->encounter_pos, pEncounter->octreeEntry.bounds.mid);
	pEncounter->octreeEntry.bounds.radius = 0;
	subVec3same(pEncounter->pWorldEncounter->encounter_pos, pEncounter->octreeEntry.bounds.radius, pEncounter->octreeEntry.bounds.min);
	addVec3same(pEncounter->pWorldEncounter->encounter_pos, pEncounter->octreeEntry.bounds.radius, pEncounter->octreeEntry.bounds.max);
	octreeAddEntry(s_pOctree, &pEncounter->octreeEntry, OCT_ROUGH_GRANULARITY);

	encounter_CacheTemplate(pEncounter);

	eaPush(&s_eaEncounters, pEncounter);
}


static void encounter_ClearEncounterList(GameEncounter ***peaEncounters)
{
	eaDestroyEx(peaEncounters, encounter_FreeEncounter);
}


static void encounter_ValidateEncounter(ZoneMap *pZoneMap, GameEncounter *pEncounter, WorldScope *pScope)
{
	WorldEncounterProperties *pProps = pEncounter->pWorldEncounter->properties;
	EncounterTemplate *pTemplate = GET_REF(pProps->hTemplate);
	const char *pcFilename;
	WorldVariableDef *pVarDef;
	int i,j;
	EncounterActorProperties** eaTemplateActors = NULL;
	EncounterLevelProperties* pLevelProps = encounterTemplate_GetLevelProperties(pTemplate);
	EncounterActorSharedProperties* pActorSharedProps = encounterTemplate_GetActorSharedProperties(pTemplate);
	encounterTemplate_FillActorEarray(pTemplate, &eaTemplateActors);

	pcFilename = layerGetFilename(pEncounter->pWorldEncounter->common_data.layer);
	
	// Make sure there is a template
	if (!pTemplate) {
		if (REF_STRING_FROM_HANDLE(pProps->hTemplate)) {
			ErrorFilenamef(pcFilename, "Encounter '%s' refers to non-existent encounter template '%s'", pEncounter->pcName, REF_STRING_FROM_HANDLE(pProps->hTemplate));
		} else {
			ErrorFilenamef(pcFilename, "Encounter '%s' does not have an encounter template defined for it.  All encounters must refer to an encounter template", pEncounter->pcName);
		}
		return;
	}

	// Check the spawn data
	if (pProps->pSpawnProperties) {
		if ((pProps->pSpawnProperties->fSpawnChance < 0) || (pProps->pSpawnProperties->fSpawnChance > 100)) {
			ErrorFilenamef(pcFilename, "Encounter '%s' has its spawn chance set to '%g', which outside the legal range 0 to 100.", pEncounter->pcName, pProps->pSpawnProperties->fSpawnChance);
		}
	}

	// Check the patrol route
	if (pProps->pcPatrolRoute) {
		if (!patrolroute_PatrolRouteExists(pProps->pcPatrolRoute, pScope)) {
			ErrorFilenamef(pcFilename, "Encounter '%s' refers to non-existent patrol route '%s'.", pEncounter->pcName, pProps->pcPatrolRoute);
		}
	}

	// Check that all actors in the template exist in the encounter
	// It is okay to have actors in the encounter that are not in the template
	if (!pProps->bFillActorsInOrder) {
		for(i=eaSize(&eaTemplateActors)-1; i>=0; --i) {
			EncounterActorProperties *pActor = eaTemplateActors[i];

			for(j=eaSize(&pProps->eaActors)-1; j>=0; --j) {
				if (stricmp(pActor->pcName, pProps->eaActors[j]->pcName) == 0) {
					break;
				}
			}
			if (j<0) {
				ErrorFilenamef(pcFilename, "Encounter '%s' does not have an actor named '%s', and this actor is required by the template.", pEncounter->pcName, pActor->pcName);
			}
		}

	} else {
		// If this is a fillActorsInOrder encounter, just make sure there aren't more actors in the encounter than there are in the template
		int iNumTemplateActors;
		int iNumActors = eaSize(&pProps->eaActors);
		iNumTemplateActors = eaTemplateActors ? eaSize(&eaTemplateActors) : 0;
		if (iNumActors > iNumTemplateActors) {
			ErrorFilenamef(pcFilename, "Encounter '%s' is a FillActorsInOrder encounter with more actors than are defined on its template", pEncounter->pcName);
		}
		if (iNumActors > encounterTemplate_GetMaxNumActors(pTemplate)) {
			ErrorFilenamef(pcFilename, "Encounter '%s' is a FillActorsInOrder encounter with more actors than the maximum number that will spawn at any team size", pEncounter->pcName);
		}
	}

	// Check world actor FSM and FSM vars
	for(i=eaSize(&eaTemplateActors)-1; i>=0; --i) {
		WorldActorProperties *pWorldActor = NULL;

		// Find the matching world actor
		for(j=eaSize(&pProps->eaActors)-1; j>=0; --j) {
			if (pProps->eaActors[j]->pcName == eaTemplateActors[i]->pcName) {
				pWorldActor = pProps->eaActors[j];
				break;
			}
		}
		if (!pWorldActor) {
			continue;
		}

		// Is FSM valid?
		if (IS_HANDLE_ACTIVE(pWorldActor->hFSMOverride) && !GET_REF(pWorldActor->hFSMOverride)) {
			ErrorFilenamef(pcFilename, "World Actor '%s' in Encounter '%s' has an invalid overridden FSM '%s'.", pWorldActor->pcName, pEncounter->pcName, REF_STRING_FROM_HANDLE(pWorldActor->hFSMOverride));
		}

		// Perform variable validation
		if (pWorldActor->eaFSMVariableDefs) {
			for(j=eaSize(&pWorldActor->eaFSMVariableDefs)-1; j>=0; --j) {
				WorldVariableDef *pDef = pWorldActor->eaFSMVariableDefs[j];
				char buf[1024];

				sprintf(buf, "FSM variable named '%s' for World Actor '%s' in Encounter '%s'", pDef->pcName, pWorldActor->pcName, pEncounter->pcName);

				if (!resIsValidName(pDef->pcName)) {
					ErrorFilenamef(pcFilename, "FSM variable named '%s' for World Actor '%s' in Encounter '%s' is not properly formed.  It must start with an alphabetic character and contain only alphanumerics, underscore, dot, and dash.", pDef->pcName, pWorldActor->pcName, pEncounter->pcName);
				}

				worldVariableValidateDef(pDef, pDef, buf, pcFilename);
			}
		}
	}

	// Check the interact range
	for(i=eaSize(&eaTemplateActors)-1; i>=0; --i) {
		EncounterActorProperties *pActor = eaTemplateActors[i];
		WorldInteractionProperties* pInteractionProps = encounterTemplate_GetActorInteractionProps(pTemplate, pActor);
		WorldActorProperties *pWorldActor = NULL;

		// Find the matching world actor
		for(j=eaSize(&pProps->eaActors)-1; j>=0; --j) {
			if (pProps->eaActors[j]->pcName == pActor->pcName) {
				pWorldActor = pProps->eaActors[j];
				break;
			}
		}
		if (!pWorldActor) {
			continue;
		}

		if (pWorldActor->pInteractionProperties) {
			pInteractionProps = pWorldActor->pInteractionProperties;
		}

		if (!pInteractionProps) {
			continue;
		}

		// Start with manual distance
		pWorldActor->uInteractDistCached = pInteractionProps->uInteractDist;

		// If not set, find actor position and pull from region rules
		if (!pWorldActor->uInteractDistCached) {
			for(j=eaSize(&pProps->eaActors)-1; j>=0; --j) {
				if (stricmp(pActor->pcName, pProps->eaActors[j]->pcName) == 0) {
					WorldRegion *pRegion = worldGetWorldRegionByPos(pProps->eaActors[j]->vPos);
					if (pRegion) {
						RegionRules *pRules = getRegionRulesFromRegionType(worldRegionGetType(pRegion));
						if ( pRules && pRules->fDefaultInteractDist > 0.0f ) {
							pWorldActor->uInteractDistCached = pRules->fDefaultInteractDist;
						}
					}
					break;
				}
			}
		}

		// If still not set, use the default
		if (!pWorldActor->uInteractDistCached) {
			pWorldActor->uInteractDistCached = DEFAULT_ENT_INTERACT_RANGE;
		}

		// Set up the max distance
		if (pWorldActor->uInteractDistCached > s_uMaxInteractDist) {
			s_uMaxInteractDist = pWorldActor->uInteractDistCached;
		}
	}

	// Check that if the template uses map variables, that they are legal on this map
	if (pLevelProps) {
		if (pLevelProps->eLevelType == EncounterLevelType_MapVariable) {
			pVarDef = zmapInfoGetVariableDefByName(zmapGetInfo(pZoneMap), pLevelProps->pcMapVariable);
			if (!pVarDef) {
				ErrorFilenamef(pcFilename, "Encounter '%s' uses encounter template '%s' that refers to INT map variable '%s' that is not defined on this map.", pEncounter->pcName, pTemplate->pcName, pLevelProps->pcMapVariable);
			} else if (pVarDef->eType != WVAR_INT) {
				ErrorFilenamef(pcFilename, "Encounter '%s' uses encounter template '%s' that refers to  map variable '%s' expecting it to be an INT type, but it is a different type.", pEncounter->pcName, pTemplate->pcName, pLevelProps->pcMapVariable);
			}
		}
	}
	if (pActorSharedProps) {
		if (encounterTemplate_GetCritterGroupSource(pTemplate) == EncounterSharedCritterGroupSource_MapVariable) {
			pVarDef = zmapInfoGetVariableDefByName(zmapGetInfo(pZoneMap), encounterTemplate_GetCritterGroupSourceMapVarName(pTemplate));
			if (!pVarDef) {
				ErrorFilenamef(pcFilename, "Encounter '%s' uses encounter template '%s' that refers to CRITTER_GROUP map variable '%s' that is not defined on this map.", pEncounter->pcName, pTemplate->pcName, pActorSharedProps->pcCritterGroupMapVar);
			} else if (pVarDef->eType != WVAR_CRITTER_GROUP) {
				ErrorFilenamef(pcFilename, "Encounter '%s' uses encounter template '%s' that refers to  map variable '%s' expecting it to be an CRITTER_GROUP type, but it is a different type.", pEncounter->pcName, pTemplate->pcName, pActorSharedProps->pcCritterGroupMapVar);
			}
		}
	}
	for(j=eaSize(&eaTemplateActors)-1; j>=0; --j) {
		EncounterActorProperties *pActor = eaTemplateActors[j];

		if (encounterTemplate_GetActorCritterType(pTemplate, pActor) == ActorCritterType_MapVariableDef) {
			pVarDef = zmapInfoGetVariableDefByName(zmapGetInfo(pZoneMap), pActor->critterProps.pcCritterMapVariable);
			if (!pVarDef) {
				ErrorFilenamef(pcFilename, "Encounter '%s' uses encounter template '%s' that refers to CRITTER_DEF map variable '%s' that is not defined on this map.", pEncounter->pcName, pTemplate->pcName, pActor->critterProps.pcCritterMapVariable);
			} else if (pVarDef->eType != WVAR_CRITTER_DEF) {
				ErrorFilenamef(pcFilename, "Encounter '%s' uses encounter template '%s' that refers to  map variable '%s' expecting it to be an CRITTER_DEF type, but it is a different type.", pEncounter->pcName, pTemplate->pcName, pActor->critterProps.pcCritterMapVariable);
			}
		} else if (encounterTemplate_GetActorCritterType(pTemplate, pActor) == ActorCritterType_MapVariableGroup) {
			pVarDef = zmapInfoGetVariableDefByName(zmapGetInfo(pZoneMap), pActor->critterProps.pcCritterMapVariable);
			if (!pVarDef) {
				ErrorFilenamef(pcFilename, "Encounter '%s' uses encounter template '%s' that refers to CRITTER_GROUP map variable '%s' that is not defined on this map.", pEncounter->pcName, pTemplate->pcName, pActor->critterProps.pcCritterMapVariable);
			} else if (pVarDef->eType != WVAR_CRITTER_GROUP) {
				ErrorFilenamef(pcFilename, "Encounter '%s' uses encounter template '%s' that refers to  map variable '%s' expecting it to be an CRITTER_GROUP type, but it is a different type.", pEncounter->pcName, pTemplate->pcName, pActor->critterProps.pcCritterMapVariable);
			}
		}
	}

	if (pProps && pProps->eaActors) 
	{
		//Validate encounter costumes.
		for (j=eaSize(&pProps->eaActors)-1; j>=0; --j)
		{
			if (pProps->eaActors[j] && pProps->eaActors[j]->pCostumeProperties && !GET_REF(pProps->eaActors[j]->pCostumeProperties->hCostume)) {
				ErrorFilenamef(pcFilename, "World Actor '%s' in Encounter '%s' has an invalid costume specified. This critter WILL NOT SPAWN.", pProps->eaActors[j]->pcName, pEncounter->pcName);
			}
		}


		// If a multi-actor encounter, ensure that no actors have their critter def overridden
		if (eaSize(&pProps->eaActors) > 1)
		{
			for(j=eaSize(&pProps->eaActors)-1; j>=0; --j) {
				if (pProps->eaActors[j]->pCritterProperties) {
					ErrorFilenamef(pcFilename, "World Actor '%s' in Encounter '%s' has an overridden Critter Def.  This is only allowed in encounters with a single actor.", pProps->eaActors[j]->pcName, pEncounter->pcName);
				}
			}
		}
	}
	eaDestroy(&eaTemplateActors);
}


static void encounter_InitWorldGroupForPartition(int iPartitionIdx, GameEncounterGroup *pGroup, bool bDoOnceOnLoad)
{
	WorldLogicalGroup *pWorldGroup = pGroup->pWorldGroup;
	int i;

	if (bDoOnceOnLoad &&
		pWorldGroup->properties &&
		eaSize(&pGroup->eaGameEncounters) > 0) {

		switch (pWorldGroup->properties->encounterSpawnProperties.eRandomType) {
			xcase LogicalGroupRandomType_OnceOnLoad:
			acase LogicalGroupRandomType_Continuous:
			{
				for (i = eaSize(&pGroup->eaGameEncounters)-1; i >= 0; i--)
				{
					GameEncounter* pEncounter = pGroup->eaGameEncounters[i];
					GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);
					// Set each encounter to be group managed
					pState->eState = EncounterState_GroupManaged;
					pState->bShouldBeGroupManaged = true;
				}
			}
		}
	}
}


static void encounter_FreeGroup(GameEncounterGroup *pGroup)
{
	eaDestroy(&pGroup->eaGameEncounters);
	free(pGroup);
}


static void encounter_AddWorldGroup(const char *pcName, WorldLogicalGroup *pWorldGroup, bool bDoOnceOnLoad, bool bChanged)
{
	GameEncounterGroup *pGroup = calloc(1,sizeof(GameEncounterGroup));
	int i;

	if (pcName) {
		pGroup->pcName = allocAddString(pcName);
	}
	pGroup->pWorldGroup = pWorldGroup;
	pGroup->pWorldGroupCopy = StructClone(parse_WorldLogicalGroup, pGroup->pWorldGroup); 

	// Associate the group
	for(i=eaSize(&pWorldGroup->objects)-1; i>=0; --i) {
		WorldEncounterObject *pObj = pWorldGroup->objects[i];
		if (pObj && pObj->type == WL_ENC_ENCOUNTER) {
			GameEncounter *pEncounter = encounter_GetByEntry((WorldEncounter*)pObj);
			if (pEncounter) {
				pEncounter->pEncounterGroup = pGroup;
				eaPush(&pGroup->eaGameEncounters, pEncounter);

				// If the group changed, then reset the encounters in all partitions in it no matter what
				if (bChanged) {
					int iPartitionIdx;
					for(iPartitionIdx=eaSize(&pEncounter->eaPartitionStates)-1; iPartitionIdx>=0; --iPartitionIdx) {
						GameEncounterPartitionState *pState = pEncounter->eaPartitionStates[iPartitionIdx];
						if (pState) {
							encounter_Reset(pEncounter, pState);
							pState->eState = EncounterState_Waiting;
							if (pEncounter->bIsDynamicMastermind) {
								pState->eState = EncounterState_Off;
							}
						}
					}
				}
			}
		}
	}

	// Discard if no game encounters
	if (eaSize(&pGroup->eaGameEncounters)) {
		eaPush(&s_eaEncounterGroups, pGroup);
	} else {
		encounter_FreeGroup(pGroup);
		pGroup = NULL;
	}
}


static void encounter_ClearEncounterGroupList(GameEncounterGroup ***peaGroupList)
{
	eaDestroyEx(peaGroupList, encounter_FreeGroup);
}


static void encounter_ValidateSpawnProperties(LogicalGroupSpawnProperties *pProps, U32 iMaxCount, const char *pcName, const char *pcFilename, const char *pcType)
{
	if (pProps->eRandomType == LogicalGroupSpawnAmountType_Number) {
		if (pProps->uSpawnAmount < 0) {
			ErrorFilenamef(pcFilename, "Logical Group '%s' specifies a request to spawn a random number '%d' of %s that is less than zero.", pcName, pProps->uSpawnAmount, pcType);
		} else if (pProps->uSpawnAmount > iMaxCount) {
			ErrorFilenamef(pcFilename, "Logical Group '%s' specifies a request to spawn a random number '%d' of %s that is greater than the number of children '%u'.", pcName, pProps->uSpawnAmount, pcType, iMaxCount);
		}
	} else if (pProps->eRandomType == LogicalGroupSpawnAmountType_Percentage) {
		if ((pProps->uSpawnAmount < 0) || (pProps->uSpawnAmount > 100)) {
			ErrorFilenamef(pcFilename, "Logical Group '%s' specifies a request to spawn a random percentage '%d' of %s that is outside the range of 0 to 100.", pcName, pProps->uSpawnAmount, pcType);
		}
	}
}


static void encounter_ValidateGroup(ZoneMap *pZoneMap, GameEncounterGroup *pGroup, WorldScope *pScope)
{
	LogicalGroupProperties *pProps = pGroup->pWorldGroup->properties;
	const char *pcFilename;

	pcFilename = layerGetFilename(pGroup->pWorldGroup->common_data.layer);

	encounter_ValidateSpawnProperties(&pProps->encounterSpawnProperties, eaSize(&pGroup->eaGameEncounters), pGroup->pcName, pcFilename, "encounter");
}


static void encounter_SetAsideEncounters(void)
{
	int i;

	// Move to old encounters
	encounter_ClearEncounterList(&s_eaOldEncounters);
	s_eaOldEncounters = s_eaEncounters;
	s_eaEncounters = NULL;

	// Clear the encounter group pointer
	for(i=eaSize(&s_eaOldEncounters)-1; i>=0; --i) {
		s_eaOldEncounters[i]->pEncounterGroup = NULL;
	}
}


static void encounter_SetAsideEncounterGroups(void)
{
	// Move to old encounter groups
	encounter_ClearEncounterGroupList(&s_eaOldEncounterGroups);
	s_eaOldEncounterGroups = s_eaEncounterGroups;
	s_eaEncounterGroups = NULL;
}


void encounter_MapValidate(ZoneMap *pZoneMap)
{
	s_uMaxInteractDist = 1; // Starting max range

	// Validate all encounters - not partition-specific
	FOR_EACH_ENCOUNTER(pEncounter) {
		if (pEncounter->pWorldEncounter && pEncounter->pWorldEncounter->properties) {
			WorldScope *pScope = pEncounter->pWorldEncounter->common_data.closest_scope;
			encounter_ValidateEncounter(pZoneMap, pEncounter, pScope);
		}
	} FOR_EACH_END; 

	// Validate all encounter groups - not partition-specific
	FOR_EACH_ENCOUNTER_GROUP(pGroup) {
		if (pGroup->pWorldGroup && pGroup->pWorldGroup->properties) {
			WorldScope *pScope = pGroup->pWorldGroup->common_data.closest_scope;
			encounter_ValidateGroup(pZoneMap, pGroup, pScope);
		}
	} FOR_EACH_END;
}


void encounter_PartitionLoad(int iPartitionIdx, bool bFullInit)
{
	PartitionEncounterData *pData;

	PERFINFO_AUTO_START_FUNC();

	// Create partition data if not already present
	pData = eaGet(&s_eaPartitionEncounterData, iPartitionIdx);
	if (!pData) {
		pData = encounter_CreatePartitionData(iPartitionIdx);
	} else {
		eaClearStruct(&pData->eaLogicalGroupData, parse_PartitionLogicalGroupEncData);
	}

	// Create partition state on each encounter if not already present
	FOR_EACH_ENCOUNTER(pEncounter) {
		GameEncounterPartitionState *pState = eaGet(&pEncounter->eaPartitionStates, iPartitionIdx);
		if (!pState) {
			pState = calloc(1,sizeof(GameEncounterPartitionState));
			pState->iPartitionIdx = iPartitionIdx;
			eaSet(&pEncounter->eaPartitionStates, pState, iPartitionIdx);

			// Set up tracking data
			pState->eventData.stEventLog = stashTableCreate(8, StashDefault, StashKeyTypeStrings, sizeof(void*));
			pState->eventData.stEventLogSinceSpawn = stashTableCreate(8, StashDefault, StashKeyTypeStrings, sizeof(void*));
			pState->eventData.stEventLogSinceComplete = stashTableCreate(8, StashDefault, StashKeyTypeStrings, sizeof(void*));
			pState->eventData.pEventTracker = eventtracker_Create(iPartitionIdx);
			pState->eState = EncounterState_Waiting;
			if (pEncounter->bIsDynamicMastermind) {
				pState->eState = EncounterState_Off;
			}
		}

		// Initialize the encounter
		encounter_InitPartition(pEncounter, pState);
	} FOR_EACH_END;

	// Post process for each encounter (second pass to handle cross dependency)
	FOR_EACH_ENCOUNTER(pEncounter) {
		GameEncounterPartitionState *pState = eaGet(&pEncounter->eaPartitionStates, iPartitionIdx);
		encounter_PostProcessPartition(pEncounter, pState, pData);
	} FOR_EACH_END;

	// Process encounter group initialization for the partition
	FOR_EACH_ENCOUNTER_GROUP(pGroup) {
		encounter_InitWorldGroupForPartition(iPartitionIdx, pGroup, bFullInit);
	} FOR_EACH_END;

	PERFINFO_AUTO_STOP();
}


void encounter_PartitionUnload(int iPartitionIdx)
{
	// Free partition state on each encounter
	FOR_EACH_ENCOUNTER(pEncounter) {
		GameEncounterPartitionState *pState = eaGet(&pEncounter->eaPartitionStates,iPartitionIdx);
		encounter_FreePartitionState(pEncounter, pState);
		eaSet(&pEncounter->eaPartitionStates, NULL, iPartitionIdx);
	} FOR_EACH_END

	// Free shared partition state
	encounter_DestroyPartitionData(iPartitionIdx);
}


void encounter_CleanupAll(bool bFullInit)
{
	int i;

	// Stop tracking events in all partitions
	// It is necessary to do this before destroying any encounters or else order of destruction
	// can cause some encounters that depend on others to encounter errors
	for(i=eaSize(&s_eaPartitionEncounterData)-1; i>=0; --i) {
		PartitionEncounterData *pData = s_eaPartitionEncounterData[i];
		if (pData) {
			encounter_StopTrackingEventsAll(i);
		}
	}

	if (bFullInit) {
		// On full init, kill everything and start over
		encounter_ClearEncounterList(&s_eaEncounters);
		encounter_ClearEncounterList(&s_eaOldEncounters);
		encounter_ClearEncounterGroupList(&s_eaEncounterGroups);
		encounter_ClearEncounterGroupList(&s_eaOldEncounterGroups);
	} else {
		// On regular load, move existing encounters to an old list, but let them run if not changed
		encounter_SetAsideEncounters();
		encounter_SetAsideEncounterGroups();
	}

	// Clear the logical group list. It will be rebuild in encounter_PostProcessAllBase.
	eaClear(&s_eaEncounterLogicalGroups);

	// Reset the octree
	if (s_pOctree) 
	{
		octreeDestroy(s_pOctree);
	}
	s_pOctree = octreeCreate();
}


void encounter_MapLoad(ZoneMap *pZoneMap, bool bFullInit)
{
	WorldZoneMapScope *pScope;
	int i,j;

	// Do the cleanup
	encounter_CleanupAll(bFullInit);

	// Get zone map scope
	pScope = zmapGetScope(pZoneMap);

	if(pScope) {
		// Find all encounters in all scopes
		for(i=eaSize(&pScope->encounters)-1; i>=0; --i) {
			const char *pcName = worldScopeGetObjectName(&pScope->scope, &pScope->encounters[i]->common_data);
			bool bAddNew = true;

			// Check if an old encounter version of this exists
			for(j=eaSize(&s_eaOldEncounters)-1; j>=0; --j) {
				GameEncounter *pOldEncounter = s_eaOldEncounters[j];
				if (stricmp(pOldEncounter->pcName, pcName) == 0) {
					if (StructCompare(parse_WorldEncounter, pOldEncounter->pWorldEncounterCopy, pScope->encounters[i], 0, 0, 0) != 0) {
						// Encounter changed, so clean up old one
						encounter_FreeEncounter(pOldEncounter);
					} else {
						// Encounter did not change, so simply update it's pointer and put it in back in the list
						pOldEncounter->pWorldEncounter = pScope->encounters[i];
						eaPush(&s_eaEncounters, pOldEncounter);
						bAddNew = false;
					}
					eaRemove(&s_eaOldEncounters, j);
					break;
				}
			}

			// If no old encounter version, or if changed, then add new one
			if (bAddNew) {
				encounter_AddWorldEncounter(pcName, pScope->encounters[i]);
			}
		}

		// Find all logical groups in all scopes
		for(i=eaSize(&pScope->groups)-1; i>=0; --i) {
			const char *pcName = worldScopeGetObjectName(&pScope->scope, &pScope->groups[i]->common_data);
			bool bChanged = true;

			// Check if an old encounter group version of this exists

			// Check if an old encounter version of this exists
			for(j=eaSize(&s_eaOldEncounterGroups)-1; j>=0; --j) {
				GameEncounterGroup *pOldEncounterGroup = s_eaOldEncounterGroups[j];
				if (stricmp(pOldEncounterGroup->pcName, pcName) == 0) {
					if (StructCompare(parse_WorldLogicalGroup, pOldEncounterGroup->pWorldGroupCopy, pScope->groups[i], 0, 0, 0) == 0) {
						bChanged = false;
					}
					encounter_FreeGroup(pOldEncounterGroup);
					eaRemove(&s_eaOldEncounterGroups, j);
					break;
				}
			}

			encounter_AddWorldGroup(pcName, pScope->groups[i], (bChanged || bFullInit), bChanged);
		}

	}

	// Remove any old encounters and groups that no longer exist
	encounter_ClearEncounterList(&s_eaOldEncounters);
	encounter_ClearEncounterGroupList(&s_eaOldEncounterGroups);

	// Post-process after load
	encounter_PostProcessAllBase();

	// Notify all partitions of re-load
	// This will restart event tracking
	for(i=eaSize(&s_eaPartitionEncounterData)-1; i>=0; --i) {
		PartitionEncounterData *pData = s_eaPartitionEncounterData[i];
		if (pData) {
			encounter_PartitionLoad(i, bFullInit);
		}
	}
}


void encounter_MapUnload()
{
	// On unload, clean up all existing encounters
	encounter_CleanupAll(true);
}


AUTO_RUN;
void encounter_InitSystem(void)
{
	// Create the contexts
	s_pEncounterContext = exprContextCreate();
	exprContextSetFuncTable(s_pEncounterContext, encounter_CreateExprFuncTable());
	exprContextSetAllowRuntimePartition(s_pEncounterContext);
	// Not allowing Self pointer

	s_pEncounterPlayerContext = exprContextCreate();
	exprContextSetFuncTable(s_pEncounterPlayerContext, encounter_CreatePlayerExprFuncTable());
	exprContextSetAllowRuntimePartition(s_pEncounterPlayerContext);
	exprContextSetAllowRuntimeSelfPtr(s_pEncounterPlayerContext);

	s_pEncounterInteractContext = exprContextCreate();
	exprContextSetFuncTable(s_pEncounterInteractContext, encounter_CreateInteractExprFuncTable());
	exprContextSetAllowRuntimePartition(s_pEncounterInteractContext);
	exprContextSetAllowRuntimeSelfPtr(s_pEncounterInteractContext);

	s_pGeneralPlayerContext = exprContextCreate();
	exprContextSetFuncTable(s_pGeneralPlayerContext, encounter_CreatePlayerExprFuncTable());
	exprContextSetAllowRuntimePartition(s_pGeneralPlayerContext);
	exprContextSetAllowRuntimeSelfPtr(s_pGeneralPlayerContext);

	// Set up the FSM Var callback
	aiSetAddExternVarCallback(encfsm_AddAIVarCallbacks);

	// Set the FSM state change callback
	aiSetStateChangeCallback(encounter_GetAIStateChangeNotification);
}

#include "AutoGen/gslEncounter_h_ast.c"
