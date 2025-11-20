
#include "contact_common.h"
#include "earray.h"
#include "EntityIterator.h"
#include "EString.h"
#include "Expression.h"
#include "ExpressionFunc.h"
#include "GameServerLib.h"
#include "gslContact.h"
#include "gslEventTracker.h"
#include "gslMapState.h"
#include "gslMapVariable.h"
#include "gslMechanics.h"
#include "gslMission.h"
#include "gslOldEncounter.h"
#include "gslOpenMission.h"
#include "gslPartition.h"
#include "gslQueue.h"
#include "gslShardVariable.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "Player.h"
#include "SavedPetCommon.h"
#include "ShardVariableCommon.h"
#include "StringCache.h"
#include "structNet.h"
#include "Team.h"
#include "textparser.h"
#include "WorldVariable.h"
#include "WorldGrid.h"

#include "../common/autogen/GameClientLib_autogen_clientcmdwrappers.h"
#include "mapstate_common_h_ast.h"
#include "gameevent_h_ast.h"
#include "AutoGen/mission_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// ----------------------------------------------------------------------------------
// Defines and forward declarations
// ----------------------------------------------------------------------------------

extern StaticDefineInt ScoreboardStateEnum[];
extern bool g_bMapHasBeenEdited;

#define MAX_SYNC_DIALOG_DURATION 30

static MapState** s_eaMapStatesBypartitionIdx = NULL;
static MapState** s_eaOldMapStatesBypartitionIdx = NULL;

S64 g_ulAbsTimes[MAX_ACTUAL_PARTITIONS+1];


// ----------------------------------------------------------------------------------
// Initialization
// ----------------------------------------------------------------------------------

static void mapState_Init(MapState *pState)
{
	assertmsgf(pState, "Requires a valid MapState");
}


static MapState* mapState_FromPartitionIdxOrInit(int iPartitionIdx)
{
	MapState *s = eaGet(&s_eaMapStatesBypartitionIdx, iPartitionIdx);
	if (!s) {
		s = StructCreate(parse_MapState);
		s->iPartitionIdx = iPartitionIdx;
		mapState_Init(s);
		eaSet(&s_eaMapStatesBypartitionIdx, s, iPartitionIdx);
	}
	s->pcMapName = zmapInfoGetPublicName(NULL); // Update on partition re-load in case map name changes while editing
	return s;
}


MapState* mapState_FromPartitionIdx(int iPartitionIdx)
{
	MapState *pState;

	devassertmsgf(!exprCurAutoTrans, "Cannot access mapstate from an autotransaction (%s)", exprCurAutoTrans);

	pState = eaGet(&s_eaMapStatesBypartitionIdx, iPartitionIdx);

	assertmsgf(pState, "MapState doesn't exist for partition %d", iPartitionIdx);
	return pState;
}


MapState* mapState_FromEnt(Entity *pEnt)
{
	MapState *pState;
	int iPartitionIdx = entGetPartitionIdx(pEnt);

	devassertmsgf(!exprCurAutoTrans, "Cannot access mapstate from an autotransaction (%s)", exprCurAutoTrans);

	pState = eaGet(&s_eaMapStatesBypartitionIdx, iPartitionIdx);

	assertmsgf(pState, "MapState doesn't exist for partition %d", iPartitionIdx);
	return pState;
}


void mapState_PartitionLoad(int iPartitionIdx)
{
	PERFINFO_AUTO_START_FUNC();

	// Destroy any previous map state
	mapState_PartitionUnload(iPartitionIdx);

	// Create new map state
	mapState_FromPartitionIdxOrInit(iPartitionIdx);

	// zero is a special value meaning "this isn't initialized"  Nobody reading g_ulAbsTimes expects to ever see that.
	g_ulAbsTimes[iPartitionIdx] = 1;

	PERFINFO_AUTO_STOP();
}


void mapState_PartitionUnload(int iPartitionIdx)
{
	MapState *pState = eaGet(&s_eaMapStatesBypartitionIdx, iPartitionIdx);
	MapState *pOldState = eaGet(&s_eaOldMapStatesBypartitionIdx, iPartitionIdx);

	if (!pState) {
		return; // It's not an error to unload a non-existing partition
	}

	// Clear open mission tracking
	// Note that this is an unowned pointer into gslOpenMission data
	pState->eaOpenMissions = NULL;

	// Stop tracking events
	mapState_StopTrackingEvents(iPartitionIdx);

	// Null out array entry
	eaSet(&s_eaMapStatesBypartitionIdx, NULL, iPartitionIdx);
	eaSet(&s_eaOldMapStatesBypartitionIdx, NULL, iPartitionIdx);
	StructDestroy(parse_MapState, pState);
	StructDestroy(parse_MapState, pOldState);
}


void mapState_MapLoad(void)
{
	// Reload any open partitions
	partition_ExecuteOnEachPartition(mapState_PartitionLoad);
}


void mapState_MapUnload(void)
{
	int i;

	// Unload all partitions
	for (i=0; i<eaSize(&s_eaMapStatesBypartitionIdx); ++i) {
		MapState *pState = s_eaMapStatesBypartitionIdx[i];
		if (pState) {
			mapState_PartitionUnload(i);
		}
	}
}


// ----------------------------------------------------------------------------------
// Event Tracking
// ----------------------------------------------------------------------------------

void mapState_StopTrackingPlayerEvents(MapState *pState)
{
	int i,j;

	if (!pState->pPlayerValueData) {
		return;
	}

	for(i=eaSize(&pState->pPlayerValueData->eaPlayerValues)-1; i>=0; --i) {
		PlayerMapValues *pPlayerValues = pState->pPlayerValueData->eaPlayerValues[i];
		for(j=eaSize(&pPlayerValues->eaValues)-1; j>=0; --j) {
			MapStateValue* pValue = pPlayerValues->eaValues[j];
			if (pValue->pGameEvent) {
				eventtracker_StopTracking(pState->iPartitionIdx, pValue->pGameEvent, pValue);
			}
		}
	}
}


static void mapState_StopTrackingMapValueEvents(MapState *pState)
{
	int i;

	if (!pState->pMapValues) {
		return;
	}

	for(i=eaSize(&pState->pMapValues->eaValues)-1; i>=0; --i) {
		MapStateValue* pValue = pState->pMapValues->eaValues[i];
		if(pValue->pGameEvent) {
			eventtracker_StopTracking(pState->iPartitionIdx, pValue->pGameEvent, pValue);
		}
	}
}


void mapState_StopTrackingEvents(int iPartitionIdx)
{
	MapState *pState;
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);

	mapState_StopTrackingPlayerEvents(pState);
	mapState_StopTrackingMapValueEvents(pState);
}


static void mapState_EventUpdateMapValue(MapStateValue *pValue, GameEvent *ev, GameEvent *specific, int iIncrement)
{
	if (MultiValIsNumber(&pValue->mvValue)) {
		F64 newValue = iIncrement + MultiValGetInt(&pValue->mvValue, NULL);
		MultiValSetInt(&pValue->mvValue, newValue);

		// Set dirty bit since map value data was modified
		ParserSetDirtyBit_sekret(parse_MapStateValueData, pValue->pState->pMapValues, true MEM_DBG_PARMS_INIT);
		pValue->pState->dirtyBitSet = 1;
	}
}


static void mapState_EventUpdatePlayerValue(MapStateValue *pValue, GameEvent *ev, GameEvent *specific, int iIncrement)
{
	if (MultiValIsNumber(&pValue->mvValue)) {
		F64 newValue = iIncrement + MultiValGetInt(&pValue->mvValue, NULL);
		MultiValSetInt(&pValue->mvValue, newValue);

		// Set dirty bit since map value data was modified
		ParserSetDirtyBit_sekret(parse_PlayerMapValueData, pValue->pState->pPlayerValueData, true MEM_DBG_PARMS_INIT);
		pValue->pState->dirtyBitSet = 1;
	}
}


// ----------------------------------------------------------------------------------
// Map value management
// ----------------------------------------------------------------------------------

bool mapState_SetValue(int iPartitionIdx, const char *pcValueName, int iNewValue, bool bAddIfMissing)
{
	MapState *pState;
	MapStateValue* pValue = NULL;

	pState = mapState_FromPartitionIdx(iPartitionIdx);

	if (pState->pMapValues) {
		pValue = mapState_FindMapValueInArray(&pState->pMapValues->eaValues, pcValueName);
	}

	if (pValue) {
		MultiValSetInt(&pValue->mvValue, iNewValue);

		// Set dirty bit since map value data was modified
		ParserSetDirtyBit_sekret(parse_MapStateValueData, pState->pMapValues, true MEM_DBG_PARMS_INIT);
		pState->dirtyBitSet = 1;
	} else if (bAddIfMissing) {
		return mapState_AddValue(iPartitionIdx, pcValueName, iNewValue, NULL);
	} else {
		return false;
	}

	return true;
}


bool mapState_SetString(int iPartitionIdx, const char *pcValueName, const char *pcNewString, bool bAddIfMissing)
{
	MapState *pState;
	MapStateValue* pValue = NULL;

	pState = mapState_FromPartitionIdx(iPartitionIdx);

	if (pState->pMapValues) {
		pValue = mapState_FindMapValueInArray(&pState->pMapValues->eaValues, pcValueName);
	}

	if (pValue) {
		MultiValSetString(&pValue->mvValue, pcNewString);

		// Set dirty bit since map value data was modified
		ParserSetDirtyBit_sekret(parse_MapStateValueData, pState->pMapValues, true MEM_DBG_PARMS_INIT);
		pState->dirtyBitSet = 1;
	} else if (bAddIfMissing) {
		return mapState_AddString(iPartitionIdx, pcValueName, pcNewString);
	} else {
		return false;
	}

	return true;
}


bool mapState_AddValue(int iPartitionIdx, const char *pcValueName, int iStartingValue, GameEvent *pEvent)
{
	MapState *pState;
	MapStateValue* pValue = NULL;

	pState = mapState_FromPartitionIdx(iPartitionIdx);

	if (!pState->pMapValues) {
		pState->pMapValues = StructCreate(parse_MapStateValueData);
	}

	// Can't create a new value if it already exists
	if (mapState_FindMapValueInArray(&pState->pMapValues->eaValues, pcValueName)) {
		return false;
	}

	pValue = StructCreate(parse_MapStateValue);

	pValue->pState = pState;
	pValue->pcName = allocAddString(pcValueName);
	MultiValSetInt(&pValue->mvValue, iStartingValue);
	if (pEvent) {
		pValue->pGameEvent = StructClone(parse_GameEvent, pEvent);
	}

	if (pValue->pGameEvent) {
		// Start tracking the event
		pValue->pGameEvent->iPartitionIdx = iPartitionIdx;
		eventtracker_StartTracking(pValue->pGameEvent, NULL, pValue, mapState_EventUpdateMapValue, mapState_EventUpdateMapValue);
	}

	eaIndexedAdd(&pState->pMapValues->eaValues, pValue);

	// Set dirty bit since map value data was modified
	ParserSetDirtyBit_sekret(parse_MapStateValueData, pState->pMapValues, true MEM_DBG_PARMS_INIT);
	pState->dirtyBitSet = 1;

	return true;
}


bool mapState_AddString(int iPartitionIdx, const char *pcValueName, const char *pcStartingValue)
{
	MapState *pState;
	MapStateValue* pValue;

	pState = mapState_FromPartitionIdx(iPartitionIdx);

	if (!pState->pMapValues) {
		pState->pMapValues = StructCreate(parse_MapStateValueData);
	}

	// Can't create a new value if it already exists
	if (mapState_FindMapValueInArray(&pState->pMapValues->eaValues, pcValueName)) {
		return false;
	}

	pValue = StructCreate(parse_MapStateValue);
	pValue->pState = pState;
	pValue->pcName = allocAddString(pcValueName);
	MultiValSetString(&pValue->mvValue, pcStartingValue);
	
	eaIndexedAdd(&pState->pMapValues->eaValues, pValue);
	
	// Set dirty bit since map value data was modified
	ParserSetDirtyBit_sekret(parse_MapStateValueData, pState->pMapValues, true MEM_DBG_PARMS_INIT);
	pState->dirtyBitSet = 1;

	return true;
}


// ----------------------------------------------------------------------------------
// Public map variable management
// ----------------------------------------------------------------------------------


void mapState_AddPublicVar(int iPartitionIdx, WorldVariable* pVar) 
{
	MapState *pState;
	bool bDirty = false;

	pState = mapState_FromPartitionIdx(iPartitionIdx);

	if (!pState->pPublicVarData) {
		pState->pPublicVarData = StructCreate(parse_PublicVariableData);
		bDirty = true;
	}

	if (pVar && !mapState_GetPublicVarByName(pState, pVar->pcName)) {
		eaPush(&pState->pPublicVarData->eaPublicVars, StructClone(parse_WorldVariable, pVar));
		bDirty = true;
	}

	if (bDirty) {
		// Set dirty bit since public variable data was modified
		ParserSetDirtyBit_sekret(parse_PublicVariableData, pState->pPublicVarData, true MEM_DBG_PARMS_INIT);
		pState->dirtyBitSet = 1;
	}
}


void mapState_SetPublicVar(int iPartitionIdx, WorldVariable *pVar, bool bAddIfMissing) 
{
	MapState *pState;

	if (!pVar) {
		return;
	}
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);

	if (pState->pPublicVarData) {
		int i;
		for(i=0; i<eaSize(&pState->pPublicVarData->eaPublicVars); i++) {
			if (!stricmp(pState->pPublicVarData->eaPublicVars[i]->pcName, pVar->pcName)) {
				// Found existing one so copy over it
				StructCopy(parse_WorldVariable, pVar, pState->pPublicVarData->eaPublicVars[i], 0, 0, 0);

				// Set dirty bit since public variable data was modified
				ParserSetDirtyBit_sekret(parse_PublicVariableData, pState->pPublicVarData, true MEM_DBG_PARMS_INIT);
				pState->dirtyBitSet = 1;
				return;
			}
		}
	}

	if (bAddIfMissing) {
		mapState_AddPublicVar(iPartitionIdx, pVar);
	}
}


// ----------------------------------------------------------------------------------
// Player value management
// ----------------------------------------------------------------------------------

static void mapState_RefreshPlayerValuesFromPrototype(int iPartitionIdx)
{
	// Call mapState_AddPlayerValues on each player
	EntityIterator *pIter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
	Entity *pCurrEnt = NULL;

	// Clean up map-specific information on the player
	while ((pCurrEnt = EntityIteratorGetNext(pIter))) {
		mapState_AddPlayerValues(pCurrEnt);
	}
	EntityIteratorRelease(pIter);
}


bool mapState_AddPrototypePlayerValue(int iPartitionIdx, const char *pcValueName, int iStartingValue, GameEvent *pEvent)
{
	MapStateValue* pValue;
	MapState *pState;
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);

	if (!pState->pPrototypeValues) {
		pState->pPrototypeValues = StructCreate(parse_MapStateValueData);
		eaIndexedEnable(&pState->pPrototypeValues->eaValues, parse_MapStateValue);
	}

	// Make sure this is a unique prototype value
	if (mapState_FindMapValueInArray(&pState->pPrototypeValues->eaValues, pcValueName)) {
		return false;
	}

	pValue = StructCreate(parse_MapStateValue);
	pValue->pState = pState;
	pValue->pcName = allocAddString(pcValueName);
	MultiValSetInt(&pValue->mvValue, iStartingValue);
	if (pEvent) {
		pValue->pGameEvent = StructClone(parse_GameEvent, pEvent);
	}

	eaIndexedAdd(&pState->pPrototypeValues->eaValues, pValue);

	mapState_RefreshPlayerValuesFromPrototype(iPartitionIdx);

	// Set dirty bit since prototype value data was modified
	ParserSetDirtyBit_sekret(parse_MapStateValueData, pState->pPrototypeValues, true MEM_DBG_PARMS_INIT);
	pState->dirtyBitSet = 1;

	return true;
}


void mapState_InitPlayerValues(MapState *pState, Entity *pPlayerEnt)
{
	PlayerMapValues *pPlayerValues;
	bool bDirty = false;

	if (!pState->pPlayerValueData) {
		pState->pPlayerValueData = StructCreate(parse_PlayerMapValueData);
		eaIndexedEnable(&pState->pPlayerValueData->eaPlayerValues, parse_PlayerMapValues);
		bDirty = true;
	}

	// If each player has a set of values, push those values now
	pPlayerValues = eaIndexedGetUsingInt(&pState->pPlayerValueData->eaPlayerValues, entGetContainerID(pPlayerEnt));

	// Create a new set of player values
	if (!pPlayerValues) {
		pPlayerValues = StructCreate(parse_PlayerMapValues);
		pPlayerValues->iEntID = entGetContainerID(pPlayerEnt);
		eaIndexedAdd(&pState->pPlayerValueData->eaPlayerValues, pPlayerValues);
		bDirty = true;
	}

	if (bDirty) {
		// Set dirty bit since prototype value data was modified
		ParserSetDirtyBit_sekret(parse_PlayerMapValueData, pState->pPlayerValueData, true MEM_DBG_PARMS_INIT);
		pState->dirtyBitSet = 1;
	}
}


void mapState_AddPlayerValues(Entity* pPlayerEnt)
{
	MapState *pState;
	PlayerMapValues* pPlayerValues = NULL;
	int iPartitionIdx;
	int i, n;
	
	if (!pPlayerEnt) {
		return;
	}

	iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	pState = mapState_FromPartitionIdx(iPartitionIdx);

	if (!pState->pPrototypeValues) {
		pState->pPrototypeValues = StructCreate(parse_MapStateValueData);
		eaIndexedEnable(&pState->pPrototypeValues->eaValues, parse_MapStateValue);

		// Set dirty bit since prototype value data was modified
		ParserSetDirtyBit_sekret(parse_MapStateValueData, pState->pPrototypeValues, true MEM_DBG_PARMS_INIT);
		pState->dirtyBitSet = 1;
	}

	if (!pState->pPlayerValueData) {
		pState->pPlayerValueData = StructCreate(parse_PlayerMapValueData);
		eaIndexedEnable(&pState->pPlayerValueData->eaPlayerValues, parse_PlayerMapValues);
	}

	// If each player has a set of values, push those values now
	pPlayerValues = eaIndexedGetUsingInt(&pState->pPlayerValueData->eaPlayerValues, entGetContainerID(pPlayerEnt));

	// Create a new set of player values
	if (!pPlayerValues) {
		pPlayerValues = StructCreate(parse_PlayerMapValues);
		pPlayerValues->iEntID = entGetContainerID(pPlayerEnt);
		eaIndexedAdd(&pState->pPlayerValueData->eaPlayerValues, pPlayerValues);
	}

	// Try to add each value in the prototype values to the player values.  Some values may already
	// be present.
	// We don't remove values that used to be in the prototype array but are no longer.
	n = eaSize(&pState->pPrototypeValues->eaValues);
	for(i=0; i<n; i++) {
		MapStateValue *pValue =  mapState_FindMapValueInArray(&pPlayerValues->eaValues, pState->pPrototypeValues->eaValues[i]->pcName);
		if ( pValue == NULL ) {
			// Add the new value
			MapStateValue* pNewValue = StructClone(parse_MapStateValue, pState->pPrototypeValues->eaValues[i]);
			GameEvent* ev;

			if (!pNewValue) {
				continue;
			}

			pNewValue->pState = pState;

			// Setup a player-scoped event
			ev = gameevent_SetupPlayerScopedEvent(pNewValue->pGameEvent, pPlayerEnt);
			if (ev) {
				StructDestroy(parse_GameEvent, pNewValue->pGameEvent);
				pNewValue->pGameEvent = ev;
			} else {
				// If player scope function didn't create clone, need to clone here anyway
				pNewValue->pGameEvent = StructClone(parse_GameEvent, pNewValue->pGameEvent);
			}

			eaIndexedAdd(&pPlayerValues->eaValues, pNewValue);

			// Start tracking events for this value
			if (pNewValue->pGameEvent) {
				pNewValue->pGameEvent->iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
				eventtracker_StartTracking(pNewValue->pGameEvent, NULL, pNewValue, mapState_EventUpdatePlayerValue, mapState_EventUpdatePlayerValue);
			}

		} else if (pValue->pGameEvent) {
			GameEvent* ev = gameevent_SetupPlayerScopedEvent(pValue->pGameEvent, pPlayerEnt);
			eventtracker_StopTracking(pState->iPartitionIdx, pValue->pGameEvent, pValue);
			if (ev) {
				StructDestroy(parse_GameEvent, pValue->pGameEvent);
				pValue->pGameEvent = ev;
				pValue->pGameEvent->iPartitionIdx = entGetPartitionIdx(pPlayerEnt);
				eventtracker_StartTracking(pValue->pGameEvent, NULL, pValue, mapState_EventUpdatePlayerValue, mapState_EventUpdatePlayerValue);
			}
		}
	}

	// Set dirty bit since player value data was modified
	ParserSetDirtyBit_sekret(parse_PlayerMapValueData, pState->pPlayerValueData, true MEM_DBG_PARMS_INIT);
	pState->dirtyBitSet = 1;
}


void mapState_ClearAllCommandsForPlayerEx(MapState *pState, PlayerMapValues *pPlayerValues)
{
	if (!pState || !pPlayerValues) {
		return;
	}
	
	if (pPlayerValues->eaPetTargetingInfo) {
		eaDestroyStruct(&pPlayerValues->eaPetTargetingInfo, parse_PetTargetingInfo);

		// Set dirty bit since player value data was modified
		ParserSetDirtyBit_sekret(parse_PlayerMapValueData, pState->pPlayerValueData, true MEM_DBG_PARMS_INIT);
		pState->dirtyBitSet = 1;
	}
}


void mapState_ClearAllCommandsForPlayer(MapState *pState, Entity *pPlayerEnt)
{
	int iIndex;

	if (!pState || !pState->pPlayerValueData) {
		return;
	}
	
	iIndex = eaIndexedFindUsingInt( &pState->pPlayerValueData->eaPlayerValues, entGetContainerID(pPlayerEnt) );
	if ( iIndex >= 0 && pState->pPlayerValueData->eaPlayerValues[iIndex]->eaPetTargetingInfo )
	{
		mapState_ClearAllCommandsForPlayerEx(pState, pState->pPlayerValueData->eaPlayerValues[iIndex]);
	}
}


void mapState_ClearAllPlayerCommandsTargetingEnt(MapState *pState, Entity *pTargetEnt)
{
	bool bDirty = false;
	int i, j;
	
	if (!pState || !pState->pPlayerValueData) {
		return;
	}

	for (i = 0; i < eaSize(&pState->pPlayerValueData->eaPlayerValues); i++)	{
		PlayerMapValues *pPlayerMapValues = pState->pPlayerValueData->eaPlayerValues[i];
		if ( pPlayerMapValues ) {
			for (j = eaSize(&pPlayerMapValues->eaPetTargetingInfo)-1; j>=0; j--) {
				if (pPlayerMapValues->eaPetTargetingInfo[j]->erTarget == pTargetEnt->myRef) {
					eaRemove(&pPlayerMapValues->eaPetTargetingInfo, j);
					bDirty = true;
					break;
				}
			}
		}
	}

	if (bDirty) {
		// Set dirty bit since player value data was modified
		ParserSetDirtyBit_sekret(parse_PlayerMapValueData, pState->pPlayerValueData, true MEM_DBG_PARMS_INIT);
		pState->dirtyBitSet = 1;
	}
}


// ----------------------------------------------------------------------------------
// Team value management
// ----------------------------------------------------------------------------------

void mapState_InitTeamValues(MapState *pState, int iTeamID)
{
	TeamMapValues *pTeamValues;
	bool bDirty = false;

	if (!pState->pTeamValueData) {
		pState->pTeamValueData = StructCreate(parse_TeamMapValueData);
		eaIndexedEnable(&pState->pTeamValueData->eaTeamValues, parse_TeamMapValues);
		bDirty = true;
	}

	pTeamValues = eaIndexedGetUsingInt(&pState->pTeamValueData->eaTeamValues, iTeamID);

	// Create a new set of team values
	if (!pTeamValues) {
		pTeamValues = StructCreate(parse_TeamMapValues);
		pTeamValues->iTeamID = iTeamID;
		eaIndexedAdd(&pState->pTeamValueData->eaTeamValues, pTeamValues);
		bDirty = true;
	}

	if (bDirty) {
		// Set dirty bit since team value data was modified
		ParserSetDirtyBit_sekret(parse_TeamMapValueData, pState->pTeamValueData, true MEM_DBG_PARMS_INIT);
		pState->dirtyBitSet = 1;
	}
}


void mapState_ClearAllCommandsForTeam(MapState *pState, int iTeamID, Entity *pPetOwner)
{
	int iIndex;
	bool bDirty = false;

	if (!pState || !pState->pTeamValueData) {
		return;
	}
	
	iIndex = eaIndexedFindUsingInt( &pState->pTeamValueData->eaTeamValues, iTeamID);
	if ( iIndex >= 0 && pState->pTeamValueData->eaTeamValues[iIndex]->eaPetTargetingInfo )
	{
		TeamMapValues* pTeamValues = pState->pTeamValueData->eaTeamValues[iIndex];
		S32 j;
		for ( j = eaSize(&pTeamValues->eaPetTargetingInfo)-1; j >= 0; j-- )
		{
			PetTargetingInfo* pInfo = pTeamValues->eaPetTargetingInfo[j];
			Entity* pPetEnt = entFromEntityRef(pState->iPartitionIdx, pInfo->erPet);

			if ( !pPetEnt || SavedPet_GetPetFromContainerID( pPetOwner, entGetContainerID(pPetEnt), true ) )
			{
				StructDestroy(parse_PetTargetingInfo, eaRemoveFast(&pTeamValues->eaPetTargetingInfo, j));
				bDirty = true;
			}
		}
		if ( eaSize(&pTeamValues->eaPetTargetingInfo) == 0 )
		{
			eaDestroy(&pTeamValues->eaPetTargetingInfo);
			bDirty = true;
		}
	}

	if (bDirty) {
		// Set dirty bit since team value data was modified
		ParserSetDirtyBit_sekret(parse_TeamMapValueData, pState->pTeamValueData, true MEM_DBG_PARMS_INIT);
		pState->dirtyBitSet = 1;
	}
}


void mapState_ClearAllTeamCommandsTargetingEnt(MapState *pState, Entity *pTargetEnt)
{
	bool bDirty = false;
	int i, j;
	
	if (!pState || !pState->pTeamValueData) {
		return;
	}

	for (i = 0; i < eaSize(&pState->pTeamValueData->eaTeamValues); i++)	{
		TeamMapValues *pTeamMapValues = pState->pTeamValueData->eaTeamValues[i];
		if ( pTeamMapValues ) {
			for (j = eaSize(&pTeamMapValues->eaPetTargetingInfo)-1; j>=0; j--) {
				if (pTeamMapValues->eaPetTargetingInfo[j]->erTarget == pTargetEnt->myRef) {
					eaRemove(&pTeamMapValues->eaPetTargetingInfo, j);
					bDirty = true;
					break;
				}
			}
		}
	}

	if (bDirty) {
		// Set dirty bit since team value data was modified
		ParserSetDirtyBit_sekret(parse_TeamMapValueData, pState->pTeamValueData, true MEM_DBG_PARMS_INIT);
		pState->dirtyBitSet = 1;
	}
}


void mapState_DestroyTeamValues(MapState *pState, int iIndex)
{
	StructDestroy( parse_TeamMapValues, pState->pTeamValueData->eaTeamValues[iIndex]);
	eaRemove(&pState->pTeamValueData->eaTeamValues, iIndex);

	// Set dirty bit since team value data was modified
	ParserSetDirtyBit_sekret(parse_TeamMapValueData, pState->pTeamValueData, true MEM_DBG_PARMS_INIT);
	pState->dirtyBitSet = 1;
}

// ----------------------------------------------------------------------------------
// Team or player value management (pet commands)
// ----------------------------------------------------------------------------------

void mapState_UpdatePlayerInfoAttackTarget(MapState * pState, Entity *pOwner, Entity *pPetEnt, EntityRef erTarget, PetTargetType eType, bool bAddAsFirstTarget, bool onePerType)
{	
	S32 i, j;
	PetTargetingInfo* pTargetInfo = NULL;
	PetTargetingInfo*** pppTargetInfoArray = NULL;
	S32 erPet = pPetEnt ? entGetRef(pPetEnt) : -1;
	int iLowestIndex = INT_MAX;
	int iHighestIndex = 0;
	bool bTeam = false;
	if ( team_IsMember(pOwner) ) //if the player is on a team, query the team pet targeting info
	{
		TeamMapValues* pTeamMapValues = NULL;

		// pState->pTeamValueData will be NULL if not initialized yet
		if(pState->pTeamValueData)
		{
			pTeamMapValues = eaIndexedGetUsingInt(&pState->pTeamValueData->eaTeamValues, pOwner->pTeam->iTeamID);
		}

		bTeam = true;
		if ( pTeamMapValues )
		{
			for ( i = 0; i < eaSize(&pTeamMapValues->eaPetTargetingInfo); i++ )
			{
				if (((erPet >= 0) && (pTeamMapValues->eaPetTargetingInfo[i]->erPet == (EntityRef)erPet)) ||
					(erPet == -1 && pTeamMapValues->eaPetTargetingInfo[i]->erTarget == erTarget))
				{
					pTargetInfo = pTeamMapValues->eaPetTargetingInfo[i];
				}
				//remember highest/lowest indices for later
				if (eType == pTeamMapValues->eaPetTargetingInfo[i]->eType)
				{
					iLowestIndex = min(pTeamMapValues->eaPetTargetingInfo[i]->iIndex, iLowestIndex);
					iHighestIndex = max(pTeamMapValues->eaPetTargetingInfo[i]->iIndex, iHighestIndex);
				}
				
			}
		}
		else if ( erTarget )
		{
			mapState_InitTeamValues(pState, pOwner->pTeam->iTeamID);
			pTeamMapValues = pState->pTeamValueData ? eaIndexedGetUsingInt(&pState->pTeamValueData->eaTeamValues, pOwner->pTeam->iTeamID) : NULL;
		}

		pppTargetInfoArray = pTeamMapValues ? &pTeamMapValues->eaPetTargetingInfo : NULL;
	}
	else //otherwise look in the per-player pet targeting array
	{
		PlayerMapValues* pPlayerValues = pState->pPlayerValueData ? eaIndexedGetUsingInt(&pState->pPlayerValueData->eaPlayerValues, entGetContainerID(pOwner)) : NULL;

		if ( pPlayerValues )
		{
			for ( i = 0; i < eaSize(&pPlayerValues->eaPetTargetingInfo); i++ )
			{
				if (((erPet >= 0) && (pPlayerValues->eaPetTargetingInfo[i]->erPet == (EntityRef)erPet)) ||
					(erPet == -1 && pPlayerValues->eaPetTargetingInfo[i]->erTarget == erTarget))
				{
					pTargetInfo = pPlayerValues->eaPetTargetingInfo[i];
				}
				//remember highest/lowest indices for later
				if (eType == pPlayerValues->eaPetTargetingInfo[i]->eType)
				{
					iLowestIndex = min(pPlayerValues->eaPetTargetingInfo[i]->iIndex, iLowestIndex);
					iHighestIndex = max(pPlayerValues->eaPetTargetingInfo[i]->iIndex, iHighestIndex);
				}
			}
		}
		else if ( erTarget )
		{
			mapState_InitPlayerValues(pState, pOwner);
			pPlayerValues = pState->pPlayerValueData ? eaIndexedGetUsingInt(&pState->pPlayerValueData->eaPlayerValues, entGetContainerID(pOwner)) : NULL;
		}

		pppTargetInfoArray = pPlayerValues ? &pPlayerValues->eaPetTargetingInfo : NULL;
	}

	if (iLowestIndex == INT_MAX)
	{
		iLowestIndex = iHighestIndex;
	}

	if ( pTargetInfo )
	{
		// If the new type of the target info is different, and we are one per type
		if (eType > 0 && pTargetInfo->eType != eType && onePerType)
		{
			// remove any existing instances of this type
			for (j = eaSize(pppTargetInfoArray) - 1; j >= 0; j--)
			{
				if ((*pppTargetInfoArray)[j]->eType == eType)
				{
					StructDestroy(parse_PetTargetingInfo, eaRemove(pppTargetInfoArray, j));
				}
			}
			iLowestIndex = 0;
			iHighestIndex = 0;
		}

		// update this target info
		pTargetInfo->erTarget = erTarget; //TODO: destroy pTargetInfo if erTarget is zero?
		if (pTargetInfo->eType != eType || bAddAsFirstTarget)
		{
			pTargetInfo->iIndex = bAddAsFirstTarget ? iLowestIndex - 1 : iHighestIndex + 1;
		}
		pTargetInfo->eType = eType;
	}
	else if ( erTarget && pppTargetInfoArray )
	{
		// If we are one per type
		if (eType > 0 && onePerType)
		{
			// remove any existing instances of this type
			for (j = eaSize(pppTargetInfoArray)-1; j >= 0; j--)
			{
				if ((*pppTargetInfoArray)[j]->eType == eType)
				{
					StructDestroy(parse_PetTargetingInfo, eaRemove(pppTargetInfoArray, j));
				}
			}
			iLowestIndex = 0;
			iHighestIndex = 0;
		}

		// create a new target info
		pTargetInfo = StructCreate( parse_PetTargetingInfo );
		pTargetInfo->erPet = erPet;
		pTargetInfo->eType = eType;
		pTargetInfo->erTarget = erTarget;
		pTargetInfo->iIndex = bAddAsFirstTarget ? iLowestIndex - 1 : iHighestIndex + 1;
		eaPush( pppTargetInfoArray, pTargetInfo );
	}

	if (bTeam)
	{
		// The following can be NULL if team_IsMember and erTarget == 0
		if(pState->pTeamValueData)
		{
			// Set dirty bit since team value data was modified
			ParserSetDirtyBit_sekret(parse_TeamMapValueData, pState->pTeamValueData, true MEM_DBG_PARMS_INIT);
		}
	}
	else
	{
		if (pState->pPlayerValueData)
		{
			// Set dirty bit since team value data was modified
			ParserSetDirtyBit_sekret(parse_PlayerMapValueData, pState->pPlayerValueData, true MEM_DBG_PARMS_INIT);
		}
	}
	pState->dirtyBitSet = 1;
}

void mapState_ClearAllCommandsForOwner(MapState * pState, Entity *pOwner)
{
	const PetTargetingInfo** eaTargetingInfo = mapState_GetPetTargetingInfo(pOwner);

	eaClear(&eaTargetingInfo);

	if (team_IsMember(pOwner))
	{
		// The following can be NULL if team_IsMember and erTarget == 0
		if(pState->pTeamValueData)
		{
			ParserSetDirtyBit_sekret(parse_TeamMapValueData, pState->pTeamValueData, true MEM_DBG_PARMS_INIT);
		}
	}
	else
	{
		if (pState->pPlayerValueData)
		{
			ParserSetDirtyBit_sekret(parse_PlayerMapValueData, pState->pPlayerValueData, true MEM_DBG_PARMS_INIT);
		}
	}

	pState->dirtyBitSet = 1;
}

// ----------------------------------------------------------------------------------
// Sync'd dialog management
// ----------------------------------------------------------------------------------

bool mapState_AddSyncDialog(Entity *pEnt, ContactDef *pContactDef, ContactDialogOptionData *pData)
{
	MapState *pState;
	Team* pTeam;
	SyncDialog* pSyncDialog;

	if (!pEnt) {
		return false;
	}

	pState = mapState_FromEnt(pEnt);

	pTeam = team_GetTeam(pEnt);
	pSyncDialog = pTeam ? mapState_GetSyncDialogForTeam(pState, team_GetTeamID(pEnt)) : NULL;

	if (pContactDef && pData && pTeam && !pSyncDialog) {
		Entity** eaTeammates = NULL;
		int i;
		bool bDestroy = true;

		// Build the sync dialog
		pSyncDialog = StructCreate(parse_SyncDialog);
		pSyncDialog->uiTeamID = pTeam->iContainerID;
		pSyncDialog->uiExpireTime = timeSecondsSince2000() + MAX_SYNC_DIALOG_DURATION;
		pSyncDialog->iInitiator = entGetRef(pEnt);
		pSyncDialog->pData = StructCreate(parse_ContactDialogOptionData);
		pSyncDialog->pData->pchCompletedDialogName = allocAddString(pData->pchCompletedDialogName);
		pSyncDialog->pData->iDialogActionIndex = pData->iDialogActionIndex;
		pSyncDialog->pData->pchVolumeName = StructAllocString(pData->pchVolumeName);
		pSyncDialog->pData->iOptionalActionIndex = pData->iOptionalActionIndex;
		pSyncDialog->pData->action = pData->action;
		SET_HANDLE_FROM_REFERENT("ContactDef", pContactDef, pSyncDialog->hContactDef);

		// Check each teammate to determine if we need to wait on them
		team_GetOnMapEntsUnique(pState->iPartitionIdx, &eaTeammates, pTeam, false);
		if (eaTeammates) {
			for(i = eaSize(&eaTeammates)-1; i >= 0; i--) {
				InteractInfo* pInfo = SAFE_MEMBER2(eaTeammates[i],pPlayer,pInteractInfo);
				SyncDialogMember* pMember = StructCreate(parse_SyncDialogMember);

				pMember->entRef = entGetRef(eaTeammates[i]);
				if (eaTeammates[i] == pEnt) {
					pMember->bAwaitingResponse = false;
				} else {
					pMember->bAwaitingResponse = pInfo && pInfo->pContactDialog;
				}
				bDestroy &= !pMember->bAwaitingResponse;
				eaIndexedAdd(&pSyncDialog->eaMembers, pMember);
			}
			eaDestroy(&eaTeammates);
		}

		// If no players are awaiting a response, then the sync dialog is not needed
		if (bDestroy) {
			eaDestroyStruct(&pSyncDialog->eaMembers, parse_SyncDialogMember);
			StructDestroy(parse_SyncDialog, pSyncDialog);
			return false;
		} else {
			eaIndexedAdd(&pState->eaSyncDialogs, pSyncDialog);
			return true;
		}
	}
	return false;
}


void mapState_RemoveSyncDialog(int iPartitionIdx, SyncDialog *pSyncDialog)
{
	int index;
	MapState *pState;
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);

	if (pSyncDialog) {
		Entity *pEnt = pSyncDialog->iInitiator ? entFromEntityRef(iPartitionIdx, pSyncDialog->iInitiator) : NULL;
		ContactDef *pContactDef = GET_REF(pSyncDialog->hContactDef);

		// Find an Entity to perfom the actions on
		if (!pEnt && pSyncDialog->eaMembers) {
			int i;
			for(i = 0; i < eaSize(&pSyncDialog->eaMembers) && !pEnt; i++) {
				pEnt = entFromEntityRef(iPartitionIdx, pSyncDialog->eaMembers[i]->entRef);
			}
		}

		// Perfom queued actions
		if (pEnt && pSyncDialog->pData) {
			if(pSyncDialog->pData->action == ContactActionType_PerformAction) {
				contactdialog_PerformSpecialDialogAction(pEnt, pContactDef, pSyncDialog->pData, true);
			} else if(pSyncDialog->pData->action == ContactActionType_PerformOptionalAction) {
				contactdialog_PerformOptionalAction(pEnt, pContactDef, pSyncDialog->pData);
			}
		}

		// Destroy and remove the sync dialog.
		index = eaIndexedFind(&pState->eaSyncDialogs, pSyncDialog);
		if (index >= 0) {
			eaRemove(&pState->eaSyncDialogs, index);
		}
		StructDestroy(parse_SyncDialog, pSyncDialog);
	}
}


// ----------------------------------------------------------------------------------
// Scoreboard management
// ----------------------------------------------------------------------------------

void mapState_SetScoreboard(int iPartitionIdx, const char* newScoreboardName)
{
	MapState *pState;
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);
	pState->matchState.pcScoreboardName = allocAddString(newScoreboardName);
}


void mapState_SetScoreboardState(int iPartitionIdx, ScoreboardState eState)
{
	MapState *pState;

	pState = mapState_FromPartitionIdx(iPartitionIdx);
	pState->matchState.eState = eState;
}


void mapState_SetScoreboardTimer(int iPartitionIdx, U32 timestamp, bool bCountdown)
{
	MapState *pState;
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);
	pState->matchState.uCounterTime = timestamp;
	pState->matchState.bCountdown = bCountdown;
}


void mapState_SetScoreboardOvertime(int iPartitionIdx, bool bOvertime)
{
	MapState *pState;
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);
	pState->matchState.bOvertime = bOvertime;
}

void mapState_SetScoreboardTotalMatchTime(int iPartitionIdx, U32 matchTime)
{
	MapState *pState;

	pState = mapState_FromPartitionIdx(iPartitionIdx);
	pState->matchState.uTotalMatchTime = matchTime;
}



// ----------------------------------------------------------------------------------
// Hidden node management
// ----------------------------------------------------------------------------------

void mapState_ClearNodeEntries(int iPartitionIdx)
{
	MapState *pState;
	NodeMapStateData *pData;
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);

	pData = pState->pNodeData;
	if (pData) {
		// Clear old data
		eaDestroyStruct(&pData->eaNodeEntries, parse_NodeMapStateEntry);

		// Make sure earray is indexed
		if (!pData->eaNodeEntries) {
			eaCreate(&pData->eaNodeEntries);
		}
		eaIndexedEnable(&pData->eaNodeEntries, parse_NodeMapStateEntry);

		// Set dirty bit since node data was modified
		ParserSetDirtyBit_sekret(parse_NodeMapStateData, pData, true MEM_DBG_PARMS_INIT);
		pState->dirtyBitSet = 1;
	}
}


void mapState_ClearAllNodeEntries(void)
{
	int i;
	for(i=eaSize(&s_eaMapStatesBypartitionIdx)-1; i>=0; --i) {
		MapState *pState = s_eaMapStatesBypartitionIdx[i];
		if (pState) {
			mapState_ClearNodeEntries(i);
		}
	}
}


void mapState_UpdateNodeEntry(int iPartitionIdx, const char *pcNodeName, bool bHidden, bool bDisabled, EntityRef uEntToWaitFor )
{
	NodeMapStateData *pData;
	NodeMapStateEntry *pEntry;
	MapState *pState;
	bool bDirty = false;

	PERFINFO_AUTO_START_FUNC();
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);

	if (!pState->pNodeData) {
		pState->pNodeData = StructCreate(parse_NodeMapStateData);

		// Make sure earray is indexed
		if (!pState->pNodeData->eaNodeEntries) {
			eaCreate(&pState->pNodeData->eaNodeEntries);
		}
		eaIndexedEnable(&pState->pNodeData->eaNodeEntries, parse_NodeMapStateEntry);

		bDirty = true;
	}

	pData = pState->pNodeData;
	pEntry = eaIndexedGetUsingString(&pData->eaNodeEntries, pcNodeName);

	// Decide if entry is required at all
	// If map has been edited, need to track all entries and can't optmize
	if (!g_bMapHasBeenEdited && !bHidden && !bDisabled && !uEntToWaitFor) {
		// Entry should not be present so remove if there
		if (pEntry) {
			int iIndex = eaIndexedFindUsingString(&pData->eaNodeEntries, pcNodeName);
			devassert(iIndex >=0);
			eaRemove(&pData->eaNodeEntries, iIndex);
			StructDestroy(parse_NodeMapStateEntry, pEntry);

			bDirty = true;
		}
	} else {
		// Entry should be present, so create if not there and then update if needed
		if (!pEntry) {
			// Add entry since didn't have one previously
			pEntry = StructCreate(parse_NodeMapStateEntry);
			pEntry->pcNodeName = StructAllocString(pcNodeName);
			eaIndexedAdd(&pData->eaNodeEntries, pEntry);
			bDirty = true;
		}

		if ((pEntry->bHidden != bHidden) ||
			(pEntry->bDisabled != bDisabled) ||
			(pEntry->uEntToWaitFor != uEntToWaitFor)) {
			// Update data on the entry
			pEntry->bHidden = bHidden;
			pEntry->bDisabled = bDisabled;
			pEntry->uEntToWaitFor = uEntToWaitFor;

			bDirty = true;
		}
	}

	if (bDirty) {
		// Set dirty bit since node data was modified
		ParserSetDirtyBit_sekret(parse_NodeMapStateData, pData, true MEM_DBG_PARMS_INIT);
		pState->dirtyBitSet = 1;
	}

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Open mission management
// ----------------------------------------------------------------------------------

void mapState_UpdateOpenMissions(int iPartitionIdx, OpenMission **eaOpenMisions)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	pState->eaOpenMissions = eaOpenMisions;
}


// ----------------------------------------------------------------------------------
// Difficulty management
// ----------------------------------------------------------------------------------

void mapState_SetDifficulty(int iPartitionIdx, int iDifficulty)
{
	MapState *pState;
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);
	pState->iDifficulty = iDifficulty;
	pState->bDifficultyInitialized = true;
}


void mapState_SetDifficultyIfNotInitialized(int iPartitionIdx, int iDifficulty)
{
	MapState *pState;

	pState = mapState_FromPartitionIdx(iPartitionIdx);
	if (!pState->bDifficultyInitialized) {
		pState->iDifficulty = iDifficulty;
		pState->bDifficultyInitialized = true;
	}
}


bool mapState_IsDifficultyInitialized(int iPartitionIdx)
{
	MapState *pState;

	pState = mapState_FromPartitionIdx(iPartitionIdx);
	if (pState) {
		return pState->bDifficultyInitialized;
	} else {
		return true;
	}
}


void mapState_SetDifficultyInitialized(int iPartitionIdx)
{
	MapState *pState;

	pState = mapState_FromPartitionIdx(iPartitionIdx);
	if (pState) {
		pState->bDifficultyInitialized = true;
	}
}


// ----------------------------------------------------------------------------------
// PvP queue state manement
// ----------------------------------------------------------------------------------

void mapState_SetPVPQueuesDisabled(int iPartitionIdx, bool bDisabled)
{
	MapState *pState; 
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);
	pState->bPVPQueuesDisabled = bDisabled;
}


// ----------------------------------------------------------------------------------
// Power speed manement
// ----------------------------------------------------------------------------------

void mapState_SetPowersSpeedRecharge(int iPartitionIdx, F32 fSpeed)
{
	MapState *pState;
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);
	
	if (fSpeed >= 0) {
		pState->fSpeedRecharge = fSpeed;
	}
}


// ----------------------------------------------------------------------------------
// Network sending logic
// ----------------------------------------------------------------------------------

void mapState_ServerAppendMapStateToPacket(Packet *pPak, bool bFullSend, int iPartitionIdx)
{
	MapState *pState;
	MapState *pOldState;
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);
	pOldState = eaGet(&s_eaOldMapStatesBypartitionIdx, iPartitionIdx);

	pktSendBits(pPak, 1, 1);

	// If the client has no map state, send them the full state
	if (bFullSend || !pOldState) {
		pktSendBits(pPak, 1, 1);
		pktSendStruct(pPak, pState, parse_MapState);
	} else {
		// Otherwise, just send a diff
		pktSendBits(pPak, 1, 0);
		ParserSend(	parse_MapState,
					pPak,
					pOldState,
					pState,
					SENDDIFF_FLAG_COMPAREBEFORESENDING,
					0,
					(TOK_SERVER_ONLY | TOK_CLIENT_ONLY | TOK_SELF_ONLY | TOK_SELF_AND_TEAM_ONLY),
					NULL);
	}
}


void mapState_ApplyDiffToOldState(int iPartitionIdx, Packet *pPak)
{
	MapState *pState, *pOldState;
	
	pState = eaGet(&s_eaMapStatesBypartitionIdx, iPartitionIdx);
	if (!pState) {
		return;
	}

	pOldState = eaGet(&s_eaOldMapStatesBypartitionIdx,iPartitionIdx);

	if (!pPak) {
		// No players on map so clear old state if one exists
		if (pOldState) {
			StructDestroy(parse_MapState, pOldState);
			eaSet(&s_eaOldMapStatesBypartitionIdx, NULL, iPartitionIdx);
		}
	} else if (!pOldState) {
		// Create new old state
		pOldState = StructClone(parse_MapState, pState);
		eaSet(&s_eaOldMapStatesBypartitionIdx, pOldState, iPartitionIdx);
	} else {
		// Remove leading bit on packet
		pktGetBits(pPak, 1);
		pktGetBits(pPak, 1);

		// Apply diff to old state
		ParserRecv(	parse_MapState,
					pPak,
					pOldState,
					RECVDIFF_FLAG_COMPAREBEFORESENDING);
	}
}


void mapState_ClearDirtyBits(void)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	for(i=eaSize(&s_eaMapStatesBypartitionIdx)-1; i>=0; --i) {
		MapState *pState = s_eaMapStatesBypartitionIdx[i];
		if (pState) {
			if (TRUE_THEN_RESET(pState->dirtyBitSet)) {
				FixupStructLeafFirst(parse_MapState, pState, FIXUPTYPE__BUILTIN__CLEAR_DIRTY_BITS, NULL);
			}
		}
	}

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Time management
// ----------------------------------------------------------------------------------

S64 mapState_GetTime(int iPartitionIdx)
{
	assertmsgf(iPartitionIdx>=0 && iPartitionIdx<=MAX_ACTUAL_PARTITIONS, "Invalid partition index: %d", iPartitionIdx);
	return g_ulAbsTimes[iPartitionIdx];
}

void mapState_UpdatePartitionTime(int iPartitionIdx, F32 fTimeStep)
{
	S64 inc;
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);

	if(pState->bPaused)
		return;

	inc = fTimeStep*3000;
	g_ulAbsTimes[iPartitionIdx] += inc;
}

AUTO_RUN;
void mapState_Autorun(void)
{
	exprSetPartitionTimeCallback(mapState_GetTime);
}

// ----------------------------------------------------------------------------------
// Tick Function
// ----------------------------------------------------------------------------------

static void mapState_Update(int iPartitionIdx, F32 fTimeStep)
{
	U32 uTimeLast;
	MapState *pState;
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);
	
	uTimeLast = pState->uServerTimeSecondsSince2000;
	pState->uServerTimeSecondsSince2000 = timeSecondsSince2000();

	mapState_UpdatePartitionTime(iPartitionIdx, fTimeStep);

	// This feature was Champions only and is essentially deprecated.
	// Now we allow each shardVar to go into one of two containers, one is broadcast, one is not. Broadcast
	// behaves as bFCSpecialPublicShardVariablesFeature used to control and broadcasts to ALL maps.
	//   bFCSpecialPublicShardVariablesFeature also (via this code) copied all vars into eaShardPublicVars.
	// Neither STO or NW used this. Leaving code in place for now just in case there are stray expressions
	if (gConf.bFCSpecialPublicShardVariablesFeature) {
		if (pState->pPublicVarData && (uTimeLast != pState->uServerTimeSecondsSince2000)) {
			int i,s;
			const char ***peaShardVariableNames;

			// Rebuild public ShardVariables EArray from scratch
			eaDestroyStruct(&pState->pPublicVarData->eaShardPublicVars, parse_WorldVariable);
			peaShardVariableNames = shardvariable_GetShardVariableNames();
			s = eaSize(peaShardVariableNames);
			for(i=0; i<s; i++) {
				ShardVariable *pVar = shardvariable_GetByName((*peaShardVariableNames)[i]);
				if (pVar && pVar->pVariable) {
					if (!eaSize(&pState->pPublicVarData->eaShardPublicVars)) {
						eaIndexedEnable(&pState->pPublicVarData->eaShardPublicVars, parse_WorldVariable);
					}
					eaPush(&pState->pPublicVarData->eaShardPublicVars,StructClone(parse_WorldVariable, pVar->pVariable));
				}
			}
		}
	}
}


void mapState_UpdateAllPartitions(F32 fTimeStep)
{
	int i;
	for(i=eaSize(&s_eaMapStatesBypartitionIdx)-1; i>=0; --i) {
		MapState *pState = s_eaMapStatesBypartitionIdx[i];
		if (pState) {
			mapState_Update(i, fTimeStep);
		}
	}
}

static void mapState_GetMinMaxCombatLevel(MapState *pState, int* piMinLevel, int* piMaxLevel)
{
	int iPartitionIdx = pState->iPartitionIdx;
	BolsterType eBolsterType = pState->eBolsterType;
	S32 iBolsterLevel = pState->iBolsterLevel;
	int iMinLevel = 0;
	int iMaxLevel = 0;

	if (iBolsterLevel > 0 && eBolsterType != kBolsterType_None)
	{
		switch(eBolsterType)
		{
		xcase kBolsterType_SetTo:
			iMinLevel = iMaxLevel = iBolsterLevel;
		xcase kBolsterType_RaiseTo:
			iMinLevel = iBolsterLevel;
		xcase kBolsterType_LowerTo:
			iMaxLevel = iBolsterLevel;
		}
	}
	else
	{
		MapVariable *pMinLevelMapVar = mapvariable_GetByNameIncludingCodeOnly(iPartitionIdx, FORCESIDEKICK_MAPVAR_MIN);
		MapVariable *pMaxLevelMapVar = mapvariable_GetByNameIncludingCodeOnly(iPartitionIdx, FORCESIDEKICK_MAPVAR_MAX);
		if (pMinLevelMapVar && pMinLevelMapVar->pVariable){
			iMinLevel = pMinLevelMapVar->pVariable->iIntVal;
		}
		if (pMaxLevelMapVar && pMaxLevelMapVar->pVariable){
			iMaxLevel = pMaxLevelMapVar->pVariable->iIntVal;
		}
	}

	(*piMinLevel) = iMinLevel;
	(*piMaxLevel) = iMaxLevel;
}

void mapState_UpdateCombatLevelsForAllPartitions(void)
{
	int i;
	for(i=eaSize(&s_eaMapStatesBypartitionIdx)-1; i>=0; --i) {
		MapState *pState = s_eaMapStatesBypartitionIdx[i];
		if (pState && !pState->bPaused && !pState->bBeingDestroyed) {
			int iMinLevel, iMaxLevel;
			mapState_GetMinMaxCombatLevel(pState, &iMinLevel, &iMaxLevel);
			mechanics_UpdateCombatLevels(i, iMinLevel, iMaxLevel);
		}
	}
}

bool mapState_CanSidekick(int iPartitionIdx)
{
	int iMinLevel, iMaxLevel;
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);

	mapState_GetMinMaxCombatLevel(pState, &iMinLevel, &iMaxLevel);
	
	return (!iMinLevel && !iMaxLevel);
}

void mapState_SetBolsterLevel(int iPartitionIdx, BolsterType eBolsterType, S32 iBolsterLevel)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);

	pState->eBolsterType = eBolsterType;
	pState->iBolsterLevel = iBolsterLevel;
}

void mapState_CutscenePlayed(int iPartitionIdx, SA_PARAM_NN_STR const char *pcCutsceneName)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);

	eaPush(&pState->ppchCutscenesPlayed, pcCutsceneName);
}

bool mapState_HasCutscenePlayed(int iPartitionIdx, SA_PARAM_NN_STR const char *pcCutsceneName)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);

	return (-1 < eaFindString(&pState->ppchCutscenesPlayed, pcCutsceneName));
}

U32 mapState_GetPlayerSpawnCount(Entity *pEnt)
{
	if (pEnt && entIsPlayer(pEnt)) {
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		PlayerMapValues *pPlayerMapValues = mapState_FindPlayerValues(iPartitionIdx, entGetContainerID(pEnt));
		if (pPlayerMapValues) {
			return pPlayerMapValues->uiRespawnCount;
		}
	}
	return 0;
}

U32 mapState_GetPlayerLastRespawnTime(Entity *pEnt)
{
	if (pEnt && entIsPlayer(pEnt)) {
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		PlayerMapValues *pPlayerMapValues = mapState_FindPlayerValues(iPartitionIdx, entGetContainerID(pEnt));
		if (pPlayerMapValues) {
			return pPlayerMapValues->uiLastRespawnTime;
		}
	}
	return 0;
}

void mapState_SetPlayerSpawnCount(Entity *pEnt, U32 uiSpawnCount)
{
	if (pEnt && entIsPlayer(pEnt)) {
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		PlayerMapValues *pPlayerMapValues = mapState_FindPlayerValues(iPartitionIdx, entGetContainerID(pEnt));

		if (!pPlayerMapValues) {
			MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
			mapState_InitPlayerValues(pState, pEnt);
			pPlayerMapValues = eaIndexedGetUsingInt(&pState->pPlayerValueData->eaPlayerValues, entGetContainerID(pEnt));
		}
		pPlayerMapValues->uiRespawnCount = uiSpawnCount;
		pPlayerMapValues->uiLastRespawnTime = timeSecondsSince2000();
	}
}
