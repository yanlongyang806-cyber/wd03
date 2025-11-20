/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "Expression.h"
#include "GameEvent.h"
#include "gslEventTracker.h"
#include "gslMissionRequest.h"
#include "mission_common.h"
#include "StringCache.h"


// ----------------------------------------------------------------------------------
// Mission Request  (Mission)
// ----------------------------------------------------------------------------------


// Returns TRUE if this mission is currently being requested by another mission
AUTO_EXPR_FUNC(mission) ACMD_NAME(MissionIsRequested);
int missionrequest_FuncMissionIsRequested(ExprContext *pContext)
{
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	Entity *pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);

	if (pMissionDef && pEnt) {
		return missionrequest_MissionRequestIsOpen(pEnt, pMissionDef);
	} else {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "MissionIsRequested: Need a Mission and a Player in the expression context");
	}

	return false;
}


AUTO_EXPR_FUNC_STATIC_CHECK;
int missionrequest_FuncHasCompletedAllRequests_StaticCheck(ExprContext *pContext)
{
	// Track any Mission State Events
	MissionDef *pMissionDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pMissionDef) {
		GameEvent *pEvent = StructCreate(parse_GameEvent);

		pEvent->pchEventName = allocAddString("AnyMissionCompleted");
		pEvent->type = EventType_MissionState;
		pEvent->missionState = MissionState_TurnedIn;
		pEvent->tMatchSource = TriState_Yes;

		eventtracker_AddNamedEventToList(&pMissionDef->eaTrackedEventsNoSave, pEvent, pMissionDef->filename);
	}
	return 0;
}


AUTO_EXPR_FUNC(mission) ACMD_NAME(HasCompletedAllRequests) ACMD_EXPR_STATIC_CHECK(missionrequest_FuncHasCompletedAllRequests_StaticCheck);
int missionrequest_FuncHasCompletedAllRequests(ExprContext *pContext)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	MissionDef *pDef = pMission?mission_GetDef(pMission):NULL;
	MissionInfo *pInfo = pMission?pMission->infoOwner:NULL;
	int i;

	if (!pDef || !pInfo) {
		return false;
	}

	for (i = eaSize(&pInfo->eaMissionRequests)-1; i>=0; --i){
		if (pInfo->eaMissionRequests[i]->pchRequesterRef == pDef->pchRefString 
			&& pInfo->eaMissionRequests[i]->eState != MissionRequestState_Succeeded) {
			return false;
		}
	}

	return true;
}


