#include "entCritter.h"
#include "Entity.h"
#include "Estring.h"
#include "Expression.h"
#include "gslMapState.h"
#include "gslOpenMission.h"
#include "gslQueue.h"
#include "gslScoreboard.h"
#include "mapstate_common.h"
#include "math.h"
#include "PowersMovement.h"

#include "mapstate_common_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// ----------------------------------------------------------------------------------
// Map State Expression Functions
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MapValueSet);
ExprFuncReturnVal mapState_FuncMapValueSet(ACMD_EXPR_PARTITION iPartitionIdx, const char *pcValueName, int iValue, ACMD_EXPR_ERRSTRING errString)
{
	if (!pcValueName) {
		estrPrintf(errString, "No name given for map value");
		return ExprFuncReturnError;
	}

	mapState_SetValue(iPartitionIdx, pcValueName, iValue, true);
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MapStringSet);
ExprFuncReturnVal mapState_FuncMapStringSet(ACMD_EXPR_PARTITION iPartitionIdx, const char *pcValueName, const char *pcNewString, ACMD_EXPR_ERRSTRING errString)
{
	if (!pcValueName) {
		estrPrintf(errString, "No name given for map value");
		return ExprFuncReturnError;
	}

	mapState_SetString(iPartitionIdx, pcValueName, pcNewString, true);
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MapValueAdd);
ExprFuncReturnVal mapState_FuncMapValueAdd(ACMD_EXPR_PARTITION iPartitionIdx, const char *pcValueName, int iAmountToAdd, ACMD_EXPR_ERRSTRING errString)
{
	MapState *pState;
	MultiVal *pmvValue;
	int iCurValue;

	if (!pcValueName) {
		estrPrintf(errString, "No name given for map value");
		return ExprFuncReturnError;
	}

	pState = mapState_FromPartitionIdx(iPartitionIdx);

	pmvValue = mapState_GetValue(pState, pcValueName);

	if (!pmvValue) {
		estrPrintf(errString, "Map value %s doesn't exist", pcValueName);
		return ExprFuncReturnError;
	} else if(!MultiValIsNumber(pmvValue)) {
		estrPrintf(errString, "Map value %s is a string, not a number", pcValueName);
		return ExprFuncReturnError;
	}

	iCurValue = MultiValGetInt(pmvValue, NULL);

	mapState_SetValue(iPartitionIdx, pcValueName, iCurValue + iAmountToAdd, true);
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MapValueTrackEvent);
ExprFuncReturnVal mapState_FuncMapValueTrackEvent(ACMD_EXPR_PARTITION iPartitionIdx, const char *pcValueName, ACMD_EXPR_SC_TYPE(Event) const char *pcEventString, ACMD_EXPR_ERRSTRING errString)
{
	GameEvent *pEvent;
	bool bResult;

	if (!pcValueName || !pcEventString) {
		estrPrintf(errString, "Missing value name or event string");
		return ExprFuncReturnError;
	}

	pEvent = gameevent_EventFromString(pcEventString);

	if (!pEvent) {
		estrPrintf(errString, "Couldn't create event from string %s", pcEventString);
		return ExprFuncReturnError;
	}

	bResult = mapState_AddValue(iPartitionIdx, pcValueName, 0, pEvent);

	// Free the allocated event
	if (pEvent) {
		StructDestroy(parse_GameEvent, pEvent);
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayerMapValueCreateForAll);
ExprFuncReturnVal mapState_FuncPlayerMapValueCreateForAll(ACMD_EXPR_PARTITION iPartitionIdx, const char *pcValueName, int iStartingValue, ACMD_EXPR_ERRSTRING errString)
{
	if (!pcValueName) {
		estrPrintf(errString, "No name given for map value");
		return ExprFuncReturnError;
	}

	mapState_AddPrototypePlayerValue(iPartitionIdx, pcValueName, iStartingValue, NULL);
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayerMapValueTrackEventForAll);
ExprFuncReturnVal mapState_FuncPlayerMapValueTrackEventForAll(ACMD_EXPR_PARTITION iPartitionIdx, const char *pcValueName, ACMD_EXPR_SC_TYPE(PlayerScopedEvent) const char *pcEventString, ACMD_EXPR_ERRSTRING errString)
{
	GameEvent* pEvent;
	bool bResult;

	if (!pcValueName || !pcEventString) {
		estrPrintf(errString, "Missing value name or event string");
		return ExprFuncReturnError;
	}

	pEvent = gameevent_EventFromString(pcEventString);

	if (!pEvent) {
		estrPrintf(errString, "Couldn't create event from string %s", pcEventString);
		return ExprFuncReturnError;
	}

	bResult = mapState_AddPrototypePlayerValue(iPartitionIdx, pcValueName, 0, pEvent);

	// Free the allocated event
	if (pEvent) {
		StructDestroy(parse_GameEvent, pEvent);
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayerMapValueSet);
ExprFuncReturnVal mapState_FuncPlayerMapValueSet(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN eaEntsIn, const char *pcValueName, int iNewValue, ACMD_EXPR_ERRSTRING errString)
{
	int i, n;
	MapState *pState;
	MapStateValue* pValue = NULL;
	ExprFuncReturnVal retVal = ExprFuncReturnFinished;

	if (!pcValueName) {
		estrPrintf(errString, "No name given for player map value");
		return ExprFuncReturnError;
	}

	pState = mapState_FromPartitionIdx(iPartitionIdx);

	n = eaSize(eaEntsIn);
	for(i=0; i<n; i++) {
		Entity* pEntity = (*eaEntsIn)[i];

		// Only operate on entities in this partition
		if (entGetPartitionIdx(pEntity) != iPartitionIdx) {
			continue;
		}

		pValue = mapState_FindPlayerValue(pState, pEntity, pcValueName);

		if (!pValue) {
			estrPrintf(errString, "Player map value %s doesn't exist for player %s", pcValueName, pEntity->debugName);
			retVal = ExprFuncReturnError;
		} else {
			MultiValSetInt(&pValue->mvValue, iNewValue);
		}
	}
	return ExprFuncReturnFinished;
}


// This is like PlayerMapValueSet, but guaranteed to set the map value for all players (even those who aren't on the map
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayerMapValueSetForAll);
ExprFuncReturnVal mapState_FuncPlayerMapValueSetForAll(ACMD_EXPR_PARTITION iPartitionIdx, const char *pcValueName, int iNewValue, ACMD_EXPR_ERRSTRING errString)
{
	int i;
	MapStateValue *pValue = NULL;
	ExprFuncReturnVal retVal = ExprFuncReturnFinished;
	MapState *pState;

	pState = mapState_FromPartitionIdx(iPartitionIdx);

	if (!pcValueName) {
		estrPrintf(errString, "No name given for player map value");
		return ExprFuncReturnError;
	}

	if (!pState || !pState->pPlayerValueData) {
		return ExprFuncReturnFinished;
	}

	for(i=eaSize(&pState->pPlayerValueData->eaPlayerValues)-1; i>=0; --i) {
		pValue = mapState_FindMapValueInArray(&pState->pPlayerValueData->eaPlayerValues[i]->eaValues, pcValueName);
		if (pValue) {
			MultiValSetInt(&pValue->mvValue, iNewValue);

			// Set dirty bit since player value data was modified
			ParserSetDirtyBit_sekret(parse_PlayerMapValueData, pState->pPlayerValueData, true MEM_DBG_PARMS_INIT);
			pState->dirtyBitSet = 1;
		}
	}
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayerMapValueAdd);
ExprFuncReturnVal mapState_FuncPlayerMapValueAdd(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_IN eaEntsIn, const char *pcValueName, int iAmountToAdd, ACMD_EXPR_ERRSTRING errString)
{
	MapState *pState;
	MapStateValue *pValue = NULL;
	ExprFuncReturnVal retVal = ExprFuncReturnFinished;
	int i;

	if (!pcValueName) {
		estrPrintf(errString, "No name given for player map value");
		return ExprFuncReturnError;
	}

	pState = mapState_FromPartitionIdx(iPartitionIdx);

	for(i=eaSize(eaEntsIn)-1; i>=0; --i) {
		Entity* pEntity = (*eaEntsIn)[i];

		// Only operate on entities in this partition
		if (entGetPartitionIdx(pEntity) != iPartitionIdx) {
			continue;
		}

		pValue = mapState_FindPlayerValue(pState, pEntity, pcValueName);

		if (!pValue) {
			estrPrintf(errString, "Player map value %s doesn't exist for player %s", pcValueName, pEntity->debugName);
			retVal = ExprFuncReturnError;
		} else if( !MultiValIsNumber(&pValue->mvValue)) {
			estrPrintf(errString, "Player map value %s is a string, not a number", pcValueName);
			retVal = ExprFuncReturnError;
		} else {
			F64 fTemp = MultiValGetInt(&pValue->mvValue, NULL);
			MultiValSetInt(&pValue->mvValue, fTemp + iAmountToAdd);

			// Set dirty bit since player value data was modified
			ParserSetDirtyBit_sekret(parse_PlayerMapValueData, pState->pPlayerValueData, true MEM_DBG_PARMS_INIT);
			pState->dirtyBitSet = 1;
		}
	}
	return retVal;
}


// Get the current map value for a given player.  The input array can only contain one player.
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(PlayerMapValueGetFromArray);
ExprFuncReturnVal mapState_FuncPlayerMapValueGetFromArray(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piValOut, ACMD_EXPR_ENTARRAY_IN eaEntsIn, SA_PARAM_NN_STR const char *pcValueName, ACMD_EXPR_ERRSTRING errString)
{
	MapState *pState;
	Entity *pEntity;
	MultiVal *pmvPlayerVal;

	if (eaSize(eaEntsIn)>1) {
		estrPrintf(errString, "Too many entities passed in: %d (1 allowed)", eaSize(eaEntsIn));
		return ExprFuncReturnError;
	}
	if(eaSize(eaEntsIn)<=0) {
		estrPrintf(errString, "No entities passed in (1 allowed)");
		return ExprFuncReturnError;
	}

	devassert((*eaEntsIn)); // Fool static check
	pEntity = (*eaEntsIn)[0];
	assertmsgf(entGetPartitionIdx(pEntity) == iPartitionIdx, "Entity (%d) must be in same partition as the expression being executed (%d)", entGetPartitionIdx(pEntity), iPartitionIdx);

	if (!pEntity || !pEntity->pPlayer) {
		estrPrintf(errString, "Passed in non-player");
		return ExprFuncReturnError;
	}	

	pState = mapState_FromPartitionIdx(iPartitionIdx);
	pmvPlayerVal = mapState_GetPlayerValue(pState, pEntity, pcValueName);

	if (NULL == pmvPlayerVal) {
		estrPrintf(errString, "Couldn't find map state variable named %s for player %s", pcValueName, pEntity->debugName);
		return ExprFuncReturnError;
	} else if(!MultiValIsNumber(pmvPlayerVal)) {
		estrPrintf(errString, "Player value %s is a string, not a number", pcValueName);
		return ExprFuncReturnError;
	}

	(*piValOut) = MultiValGetInt(pmvPlayerVal, NULL);
	return ExprFuncReturnFinished;
}


//Stops tracking all players and all game events on the map
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MapStateStopTrackingEvents);
ExprFuncReturnVal mapState_FuncMapStateStopTrackingEvents(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ERRSTRING errString)
{
	mapState_StopTrackingEvents(iPartitionIdx);
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MapStateStopTrackingPlayers);
ExprFuncReturnVal mapState_FuncMapStateStopTrackingPlayers(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ERRSTRING errString)
{
	MapState *pState;
	
	pState = mapState_FromPartitionIdx(iPartitionIdx);
	mapState_StopTrackingPlayerEvents(pState);

	return ExprFuncReturnFinished;
}

// Deprecated expression
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MapStateResetAll);
ExprFuncReturnVal mapState_FuncMapStateMapStateResetAll(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ERRSTRING errString)
{
	Alertf("MapStateResetAll is deprecated. Calling it will have no effect.");
	return ExprFuncReturnFinished;
}

// Reset scoreboard-related information for the map
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MapStateResetScoreboard);
ExprFuncReturnVal mapState_FuncMapStateMapStateResetScoreboard(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ERRSTRING errString)
{
	openmission_ResetOpenMissions(iPartitionIdx);

	mapState_SetScoreboardState(iPartitionIdx, kScoreboardState_Init);

	gslScoreboard_Reset(iPartitionIdx);

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MapStateCreateScoreboardGroup);
ExprFuncReturnVal mapState_FuncMapStateCreateScoreboardGroup(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_RES_DICT(CritterFaction) const char* pchFaction, ACMD_EXPR_DICT(Message) const char* pchDisplayMessage, ACMD_EXPR_ERRSTRING errString)
{
	CritterFaction* pFaction = RefSystem_ReferentFromString(g_hCritterFactionDict, pchFaction);
	Message* pDisplayMessage = RefSystem_ReferentFromString(gMessageDict, pchDisplayMessage);
	
	if (!pFaction)
	{
		estrPrintf(errString, "MapStateCreateScoreboardGroup was called with a non-existent CritterFaction %s", pchFaction);
		return ExprFuncReturnError;
	}
	if (pchDisplayMessage && pchDisplayMessage[0] && !pDisplayMessage)
	{
		estrPrintf(errString, "MapStateCreateScoreboardGroup was called with a non-existent Message %s", pchDisplayMessage);
		return ExprFuncReturnError;
	}

	gslScoreboard_CreateGroup(iPartitionIdx, pFaction, pDisplayMessage, NULL);

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(IsScoreboardInState);
ExprFuncReturnVal mapState_FuncIsScoreboardInState(ACMD_EXPR_PARTITION iPartitionidx, ACMD_EXPR_INT_OUT piValOut, ACMD_EXPR_ENUM(ScoreboardState) const char *pcState)
{
	int eState = StaticDefineIntGetInt(ScoreboardStateEnum, pcState);
	MapState *pState = mapState_FromPartitionIdx(iPartitionidx);

	if(mapState_GetScoreboardState(pState) == eState)
	{
		(*piValOut) = 1;
	}else{
		(*piValOut) = 0;
	}

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(IsScoreboardOfGameType);
ExprFuncReturnVal mapState_FuncIsScoreboardOfGametype(ACMD_EXPR_PARTITION iPartitionidx, ACMD_EXPR_INT_OUT piValOut, ACMD_EXPR_ENUM(PVPGameType) const char *pcGameType)
{
	int eGameType = StaticDefineIntGetInt(PVPGameTypeEnum,pcGameType);
	MapState *pMapState = mapState_FromPartitionIdx(iPartitionidx);

	if(pMapState && pMapState->matchState.pvpRules.eGameType == eGameType)
	{
		(*piValOut) = 1;
	}else{
		(*piValOut) = 0;
	}

	return ExprFuncReturnFinished;
}

// Show the scoreboard with the given name to all players in the entarray
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(ShowScoreboardInState);
ExprFuncReturnVal mapState_FuncShowScoreboardInState(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_NN_STR const char *pcScoreboardName, ACMD_EXPR_ENUM(ScoreboardState) const char *pcState, ACMD_EXPR_ERRSTRING errString)
{
	int eState = StaticDefineIntGetInt(ScoreboardStateEnum, pcState);
	
	mapState_SetScoreboard(iPartitionIdx, pcScoreboardName);
	
	mapState_SetScoreboardState(iPartitionIdx, eState);

	//TODO(BH): Untie these scoreboards from pvp leaderboards
	gslQueue_MapSetStateFromScoreboardState(iPartitionIdx, eState);

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(ShowScoreboardActive);
ExprFuncReturnVal mapState_FuncShowScoreboardActiveTimer(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_NN_STR const char *pcScoreboardName, ACMD_EXPR_ERRSTRING errString)
{
	mapState_SetScoreboard(iPartitionIdx, pcScoreboardName);

	mapState_SetScoreboardState(iPartitionIdx, kScoreboardState_Active);

	mapState_SetScoreboardTimer(iPartitionIdx, pmTimestamp(0), false);

	//TODO(BH): Untie these scoreboards from pvp leaderboards
	gslQueue_MapSetStateFromScoreboardState(iPartitionIdx, kScoreboardState_Active);

	return ExprFuncReturnFinished;
}


// Show the scoreboard with the given name to all players in the entarray
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(ShowScoreboard);
ExprFuncReturnVal mapState_FuncShowScoreboard(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_NN_STR const char *pcScoreboardName, ACMD_EXPR_ERRSTRING errString)
{	
	mapState_SetScoreboard(iPartitionIdx, pcScoreboardName);
	mapState_SetScoreboardState(iPartitionIdx, kScoreboardState_Init);

	return ExprFuncReturnFinished;
}


// Show the scoreboard with the given name to all players in the entarray
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(SetScoreboardState);
ExprFuncReturnVal mapState_FuncSetScoreboardState(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENUM(ScoreboardState) const char *pcState, ACMD_EXPR_ERRSTRING errString)
{
	char const *pcScoreboard = mapState_GetScoreboard(mapState_FromPartitionIdx(iPartitionIdx));
	if (pcScoreboard && *pcScoreboard)
	{
		int eState = StaticDefineIntGetInt(ScoreboardStateEnum, pcState);
		mapState_SetScoreboardState(iPartitionIdx, eState);
		
		//TODO(BH): Untie these scoreboards from pvp leaderboards
		gslQueue_MapSetStateFromScoreboardState(iPartitionIdx, eState);
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(ScoreboardFinishAndReportWinner);
ExprFuncReturnVal mapState_FuncScoreboardFinishAndReportWinner(ACMD_EXPR_PARTITION iPartitionIdx, const char *pcWinnerFaction, 
														  ACMD_EXPR_ERRSTRING errString)
{
	const char *pcScoreboardName = mapState_GetScoreboard(mapState_FromPartitionIdx(iPartitionIdx));
	if (pcScoreboardName && pcScoreboardName[0]) {
		mapState_SetScoreboardState(iPartitionIdx, kScoreboardState_Final);

		gslQueue_MapSetStateFromScoreboardStateEx(iPartitionIdx, kScoreboardState_Final, pcWinnerFaction);
	}
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(ShowFinalScoreboardAndReportWinner);
ExprFuncReturnVal mapState_FuncShowFinalScoreboardAndReportWinner(ACMD_EXPR_PARTITION iPartitionIdx, SA_PARAM_NN_STR const char *pcScoreboardName, 
															 const char *pcWinnerFaction, 
															 ACMD_EXPR_ERRSTRING errString)
{
	mapState_SetScoreboard(iPartitionIdx, pcScoreboardName);

	return mapState_FuncScoreboardFinishAndReportWinner(iPartitionIdx, pcWinnerFaction, errString);
}


// Show no scoreboard to the players in the entArray
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(HideScoreboard);
ExprFuncReturnVal mapState_FuncHideScoreboard(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ERRSTRING errString)
{
	mapState_SetScoreboard(iPartitionIdx, NULL);
	mapState_SetScoreboardState(iPartitionIdx, kScoreboardState_Init);

	return ExprFuncReturnFinished;
}

// just sets the scoreboard state along with the mapstate to Final
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME(ScoreboardMapStateSetFinal);
ExprFuncReturnVal mapState_FuncScoreboardMapStateSetFinal(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ERRSTRING errString)
{
	MapState *pMapState = mapState_FromPartitionIdx(iPartitionIdx);
	if(pMapState)
	{
		const char *pchScoreboard = mapState_GetScoreboard(pMapState);

		if (pchScoreboard)
		{
			ANALYSIS_ASSUME(pchScoreboard != NULL);
			if ((strncmp(pchScoreboard, "Pve", 3) == 0 || strncmp(pchScoreboard, "PVE", 3)))
			{
				mapState_SetScoreboardTotalMatchTime(iPartitionIdx, pmTimestamp(0) - pMapState->matchState.uCounterTime);
			}
		}
	
		if (mapState_GetScoreboardState(pMapState) != kScoreboardState_Final)
		{
			mapState_SetScoreboardState(iPartitionIdx, kScoreboardState_Final);
			gslQueue_MapSetStateFromScoreboardState(iPartitionIdx, kScoreboardState_Final);
		}
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(gameutil) ACMD_NAME(MapValueGet);
ExprFuncReturnVal mapState_FuncMapValueGet(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_INT_OUT piValOut, SA_PARAM_NN_STR const char *pcValueName, ACMD_EXPR_ERRSTRING errString)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	MultiVal *pmvMapValue = mapState_GetValue(pState, pcValueName);

	if (pmvMapValue)
	{
		ANALYSIS_ASSUME(pmvMapValue != NULL);
		if (MultiValIsNumber(pmvMapValue)) {
			(*piValOut) = MultiValGetInt(pmvMapValue, NULL);
		} else {
			(*piValOut) = 0;
		}
	} else {
		(*piValOut) = 0;
	}
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(gameutil) ACMD_NAME(MapStringGet);
ExprFuncReturnVal mapState_FuncMapStringGet(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_STRING_OUT ppcValOut, SA_PARAM_NN_STR const char *pcValueName, ACMD_EXPR_ERRSTRING errString)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	MultiVal *pmvMapValue = mapState_GetValue(pState, pcValueName);

	if (pmvMapValue)
	{
		ANALYSIS_ASSUME(pmvMapValue != NULL);
		if (MultiValIsString(pmvMapValue)) {
			(*ppcValOut) = MultiValGetString(pmvMapValue, NULL);
		} else {
			(*ppcValOut) = "";
		}
	} else {
		(*ppcValOut) = "";
	}
	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(gameutil) ACMD_NAME(IsMapPaused);
U32 mapState_FuncIsMapPaused(ACMD_EXPR_PARTITION iPartitionIdx)
{
	MapState *pState = NULL;
	pState = mapState_FromPartitionIdx(iPartitionIdx);
	return pState ? pState->bPaused : false;
}


AUTO_EXPR_FUNC(gameutil) ACMD_NAME(PauseTimeRemaining);
U32 mapState_FuncPauseTimeRemaining(ACMD_EXPR_PARTITION iPartitionIdx)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	return pState ? (U32)ceilf(pState->fTimeControlTimer) : 0;
}


AUTO_EXPR_FUNC(gameutil) ACMD_NAME(PauseList);
char *mapState_FuncPauseList(ACMD_EXPR_PARTITION iPartitionIdx)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	return pState ? pState->pchTimeControlList : NULL;
}


AUTO_EXPR_FUNC(gameutil) ACMD_NAME(GetMapDifficulty);
int mapState_FuncGetMapDifficulty(ACMD_EXPR_PARTITION iPartitionIdx)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	return mapState_GetDifficulty(pState);
}


AUTO_EXPR_FUNC(gameutil) ACMD_NAME(ArePVPQueuesDisabled);
bool mapState_FuncArePVPQueuesDisabled(ACMD_EXPR_PARTITION iPartitionIdx)
{
	MapState *pState = mapState_FromPartitionIdx(iPartitionIdx);
	return pState->bPVPQueuesDisabled;
}

