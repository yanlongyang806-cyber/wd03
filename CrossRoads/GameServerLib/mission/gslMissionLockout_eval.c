/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aimovement.h"
#include "aistruct.h"
#include "CommandQueue.h"
#include "error.h"
#include "Expression.h"
#include "GameEvent.h"
#include "gslMission.h"
#include "gslMissionLockout.h"
#include "mission_common.h"
#include "stringcache.h"
#include "gslEventTracker.h"

#include "../AILib/AutoGen/aiMovement_h_ast.h"
#include "Autogen/GameServerLib_autogen_QueuedFuncs.h"


// ----------------------------------------------------------------------------------
// Mission Lockout Expression Functions
// ----------------------------------------------------------------------------------

// Queued command to clean up a MissionLockoutList if the NPC managing it dies
AUTO_COMMAND_QUEUED();
void missionlockout_OnDeathCleanup(int iPartitionIdx, const char *pcMissionRefString)
{
	MissionDef* pDef = missiondef_DefFromRefString(pcMissionRefString);
	if (pDef) {
		missionlockout_DestroyLockoutList(iPartitionIdx, pDef);
	}
}


AUTO_EXPR_FUNC_STATIC_CHECK;
bool missionlockout_FuncPlayerOnMissionLockoutList_SC(ExprContext *pContext, const char *pcMissionRefString)
{
	MissionDef *pTargetDef = missiondef_DefFromRefString(pcMissionRefString);
	MissionDef *pDef;

	if (pTargetDef && !pTargetDef->lockoutType) {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "Mission %s does not have a Lockout Type set!", pcMissionRefString);
		return false;
	}
	
	// If this is being used from a MissionDef, track any MissionLockoutState Events
	pDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pDef && pTargetDef) {
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		char *estrBuffer = NULL;

		estrPrintf(&estrBuffer, "MissionLockoutStateChange_%s", pcMissionRefString);
		pEvent->pchEventName = allocAddString(estrBuffer);
		pEvent->type = EventType_MissionLockoutState;
		pEvent->pchMissionRefString = allocAddString(pcMissionRefString);
		pEvent->pchMissionCategoryName = REF_STRING_FROM_HANDLE(pTargetDef->hCategory);
		eventtracker_AddNamedEventToList(&pDef->eaTrackedEventsNoSave, pEvent, pDef->filename);

		estrDestroy(&estrBuffer);
	}
	return true;
}


// Returns TRUE if the player is on the Lockout List for the given MissionDef
AUTO_EXPR_FUNC(player, mission) ACMD_NAME(PlayerOnMissionLockoutList) ACMD_EXPR_STATIC_CHECK(missionlockout_FuncPlayerOnMissionLockoutList_SC);
bool missionlockout_FuncPlayerOnMissionLockoutList(ExprContext *pContext, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionRefString)
{
	MissionDef *pDef = missiondef_DefFromRefString(pcMissionRefString);
	Entity *pPlayerEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
	if (pDef && pPlayerEnt) {
		return missionlockout_PlayerInLockoutList(pDef, pPlayerEnt, entGetPartitionIdx(pPlayerEnt));
	}
	return false;
}


// Starts the Lockout List for the given Mission (stops new players from joining)
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MissionLockoutBegin);
void missionlockout_FuncMissionLockoutBegin(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionRefString)
{
	MissionDef* pDef = missiondef_DefFromRefString(pcMissionRefString);
	if (pDef) {
		missionlockout_BeginLockout(iPartitionIdx, pDef);		
	}
}


// Adds the entity to the player ercorting list. This allows client to show entity on the mini map
AUTO_EXPR_FUNC(ai) ACMD_NAME(MissionLockoutAddEscortee);
void missionlockout_FuncMissionLockoutAddEscortee(ACMD_EXPR_SELF Entity *pEnt, ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionRefString)
{
	MissionDef* pDef;

	if (!pEnt) {
		return;
	}

	pDef = missiondef_DefFromRefString(pcMissionRefString);
	if (pDef) {
		MissionLockoutList *pList = missionlockout_GetLockoutList(iPartitionIdx, pDef);
		if (pList) {
			eaiPush(&pList->eaiEscorting, pEnt->myRef);
			missionlockout_UpdatePlayerEscorting(pList);
		}
	}
}


// Makes the specified MissionLockout end as soon as this Entity dies
AUTO_EXPR_FUNC(ai) ACMD_NAME(MissionLockoutEndOnMyDeath);
void missionlockout_FuncMissionLockoutEndOnMyDeath(ACMD_EXPR_SELF Entity *pEnt, ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionRefString)
{
	AIVarsBase *pVarBase = pEnt->aibase;
	FSMLDGenericSetData *pMydata = getMyData(pContext, parse_FSMLDGenericSetData, (U64)"MissionLockoutEndOnMyDeath");
	if (!pMydata->setData) {
		if (!pVarBase->onDeathCleanup) {
			pVarBase->onDeathCleanup = CommandQueue_Create(4 * sizeof(void*), false);
		}
		QueuedCommand_missionlockout_OnDeathCleanup(pVarBase->onDeathCleanup, iPartitionIdx, allocAddString(pcMissionRefString));
		pMydata->setData = 1;
	}
}


// Stops the LockoutList and destroys it
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MissionLockoutEnd);
void missionlockout_FuncMissionLockoutEnd(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionRefString)
{
	MissionDef *pDef = missiondef_DefFromRefString(pcMissionRefString);
	if (pDef) {
		missionlockout_DestroyLockoutList(iPartitionIdx, pDef);
	}
}


// Gets an EntArray containing all players on the lockout list
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MissionLockoutGetEnts);
void missionlockout_FuncMissionLockoutGetEnts(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_ENTARRAY_OUT peaEntsOut, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionRefString)
{
	MissionDef *pDef = missiondef_DefFromRefString(pcMissionRefString);
	if (pDef) {
		missionlockout_GetLockoutListEnts(iPartitionIdx, pDef, peaEntsOut);
	}
}


// Sets the list of players on this LockoutList
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(MissionLockoutSetEnts);
void missionlockout_FuncMissionLockoutSetEnts(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionRefString, ACMD_EXPR_ENTARRAY_IN peaEntsIn)
{
	MissionDef* pDef = missiondef_DefFromRefString(pcMissionRefString);
	if (pDef) {
		missionlockout_SetLockoutListEnts(iPartitionIdx, pDef, peaEntsIn);
	}
}



