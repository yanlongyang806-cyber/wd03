/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "chatCommon.h"
#include "Entity.h"
#include "EntityLib.h"
#include "Expression.h"
#include "GameEvent.h"
#include "gslChatConfig.h"
#include "gslEventTracker.h"
#include "gslMission.h"
#include "gslOpenMission.h"
#include "mission_common.h"
#include "ResourceInfo.h"
#include "reward.h"
#include "StringCache.h"
#include "gslEventSend.h"
#include "inventoryCommon.h"

#include "AutoGen/chatCommon_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"


// ----------------------------------------------------------------------------------
// Open Mission Expression Functions
// ----------------------------------------------------------------------------------


AUTO_EXPR_FUNC_STATIC_CHECK;
int openmission_FuncOpenMissionState_LoadVerify(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName)
{
	// Track any Mission State Events
	MissionDef *pDef = exprContextGetVarPointerUnsafePooled(pContext, g_MissionDefVarName);
	if (pDef) {
		GameEvent *pEvent = StructCreate(parse_GameEvent);
		char *estrBuffer = NULL;

		estrPrintf(&estrBuffer, "OpenMissionStateChange_%s", pcMissionName);
		pEvent->pchEventName = allocAddString(estrBuffer);
		pEvent->type = EventType_MissionState;
		pEvent->pchMissionRefString = allocAddString(pcMissionName);

		eventtracker_AddNamedEventToList(&pDef->eaTrackedEventsNoSave, pEvent, pDef->filename);

		estrDestroy(&estrBuffer);
	}
	return 0;
}


AUTO_EXPR_FUNC(ai, encounter_action, mission, gameutil) ACMD_NAME(OpenMissionStateInProgress) ACMD_EXPR_STATIC_CHECK(openmission_FuncOpenMissionState_LoadVerify);
int openmission_FuncOpenMissionStateInProgress(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	MissionDef *pDef = missiondef_DefFromRefString(pcMissionName);
	if (pDef && (missiondef_GetType(pDef) == MissionType_OpenMission)) {
		Mission *pMission = openmission_FindMissionFromRefString(iPartitionIdx, pcMissionName);
		return (pMission && (pMission->state == MissionState_InProgress));

	} else if(!pDef) {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "OpenMissionStateInProgress %s : No such mission", pcMissionName);
	}

	return false;
}



AUTO_EXPR_FUNC(ai, encounter_action, mission, gameutil) ACMD_NAME(OpenMissionStateSucceeded) ACMD_EXPR_STATIC_CHECK(openmission_FuncOpenMissionState_LoadVerify);
int openmission_FuncOpenMissionStateSucceeded(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	MissionDef *pDef = missiondef_DefFromRefString(pcMissionName);
	if (pDef && (missiondef_GetType(pDef) == MissionType_OpenMission)) {
		Mission *pMission = openmission_FindMissionFromRefString(iPartitionIdx, pcMissionName);
		return (pMission && (pMission->state == MissionState_Succeeded));

	} else if (!pDef) {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "OpenMissionStateSucceeded %s : No such mission", pcMissionName);
	}

	return false;
}


AUTO_EXPR_FUNC(ai, encounter_action, mission, gameutil) ACMD_NAME(OpenMissionStateFailed) ACMD_EXPR_STATIC_CHECK(openmission_FuncOpenMissionState_LoadVerify);
int openmission_FuncOpenMissionStateFailed(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	MissionDef *pDef = missiondef_DefFromRefString(pcMissionName);
	if (pDef && (missiondef_GetType(pDef) == MissionType_OpenMission)) {
		Mission *pMission = openmission_FindMissionFromRefString(iPartitionIdx, pcMissionName);
		return (pMission && (pMission->state == MissionState_Failed));

	} else if (!pDef) {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "OpenMissionStateFailed %s : No such mission", pcMissionName);
	}

	return false;
}


AUTO_EXPR_FUNC(mission,OpenMission,player) ACMD_NAME(OpenMissionMapCredit) ACMD_EXPR_STATIC_CHECK(openmission_FuncOpenMissionState_LoadVerify);
int openmission_FuncOpenMissionMapCredit(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_RES_DICT(AllMissionsIndex) const char *pcMissionName)
{
	Mission *pMission = exprContextGetVarPointerUnsafePooled(pContext, g_MissionVarName);
	MissionDef *pDef = missiondef_DefFromRefString(pcMissionName);
	char ns[RESOURCE_NAME_MAX_SIZE];

	ns[0] = 0;

	// Don't test for the open mission state if it's in an unloaded namespace...
	// Just assume it's not complete
	if (resExtractNameSpace_s( pcMissionName, SAFESTR(ns), NULL, 0 )) {
		if (!resNameSpaceGetByName( ns )) {
			return false;
		}
	}
	
	// Find root of mission
	while(pMission && pMission->parent) {
		pMission = pMission->parent;
	}

	if (pDef && (missiondef_GetType(pDef) == MissionType_OpenMission)) {
		Entity* pEnt = exprContextGetVarPointerUnsafePooled(pContext, g_PlayerVarName);
		if (pEnt) {
			bool bTimeConstraint = true;
			OpenMission *pOpenMission = openmission_GetFromName(iPartitionIdx, pcMissionName);
			NOCONST(ChatConfig) *pConfig = ServerChatConfig_GetChatConfig(pEnt);

			if (pMission && pMission->startTime && pOpenMission && pOpenMission->uiSucceededTime) {
				bTimeConstraint = pMission->startTime < pOpenMission->uiSucceededTime;
				if (!bTimeConstraint) {
					return false;
				}
			}

			if (pConfig && (pConfig->status & USERSTATUS_AUTOAFK)) {
				return false;
			}

			return openmission_DidEntParticipateInOpenMission(iPartitionIdx, pcMissionName, pEnt);
		}
	} else if (!pDef) {
		ErrorFilenamef(exprContextGetBlameFile(pContext), "OpenMissionMapCredit %s : No such mission", pcMissionName);
	}
	return false;
}


// Begins an Open Mission using the given MissionDef
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(OpenMissionStart);
void openmission_FuncOpenMissionStart(ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName)
{
	MissionDef *pDef = RefSystem_ReferentFromString(g_MissionDictionary, pcMissionName);
	if (pDef) {
		openmission_BeginOpenMission(iPartitionIdx, pDef, false);
	}
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(OpenMissionStartWithNotification);
void openmission_FuncOpenMissionStartWithNotification(ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName)
{
	MissionDef *pDef = RefSystem_ReferentFromString(g_MissionDictionary, pcMissionName);
	if (pDef) {
		openmission_BeginOpenMission(iPartitionIdx, pDef, true);
	}
}

// Ends an Open Mission
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(OpenMissionEnd);
void openmission_FuncOpenMissionEnd(ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName)
{
	MissionDef *pDef = RefSystem_ReferentFromString(g_MissionDictionary, pcMissionName);
	if (pDef) {
		openmission_EndOpenMission(iPartitionIdx, pDef);
	}
}


// Starts a visible timer for when the Open Mission will restart
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(OpenMissionInitResetTimer);
void openmission_FuncOpenMissionInitResetTimer(ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName, F32 fTimeSeconds)
{
	OpenMission *pOpenMission = openmission_GetFromName(iPartitionIdx, pcMissionName);
	if (pOpenMission) {
		pOpenMission->fResetTimeRemaining = fTimeSeconds;
	}
}


// Returns TRUE if the Reset Timer has expired
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(OpenMissionResetTimerExpired);
bool openmission_FuncOpenMissionResetTimerExpired(ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName)
{
	OpenMission *pOpenMission = openmission_GetFromName(iPartitionIdx, pcMissionName);
	if (pOpenMission) {
		return (pOpenMission->fResetTimeRemaining <= 0.0f);
	}
	return true;
}

AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(OpenMissionSendShowScoreboardNotification);
void openmission_FuncOpenMissionSendShowScoreboardNotification(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName)
{
	OpenMission *pOpenMission = openmission_GetFromName(iPartitionIdx, pcMissionName);

	if(pOpenMission && pOpenMission->eaScores)
	{
		Entity *pEnt = NULL;
		int i;

		for(i = 0; i < eaSize(&pOpenMission->eaScores); ++i)
		{
			pEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pOpenMission->eaScores[i]->playerID);
			if(pEnt)
				ClientCmd_OpenMissionShowLeaderboard(pEnt);
		}
	}
}


static int openmission_ScoreEntrySortByScore(const OpenMissionScoreEntry **ppEntry1, const OpenMissionScoreEntry **ppEntry2)
{
	F32 fResult = (*ppEntry2)->fPoints - (*ppEntry1)->fPoints;

	if(fResult > 0)
	{
		return 1;
	}
	else if (fResult < 0)
	{
		return -1;
	}
	else
	{
		return 0;
	}
}

void openmission_FuncOpenMissionGrantRewardsEx(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName, ACMD_EXPR_LOC_MAT4_IN pRewardDropPoint, bool bFailure)
{
	OpenMission *pOpenMission = openmission_GetFromName(iPartitionIdx, pcMissionName);
	Mission *pMission = pOpenMission ? pOpenMission->pMission : NULL;
	MissionDef *pDef = mission_GetDef(pMission);
	OpenMissionScoreEntry **eaSortedScores = NULL;
	int i;

	if (pDef && pOpenMission) {
		F32 fAvgScore = 0.f;
		eaPushEArray(&eaSortedScores, &pOpenMission->eaScores);
		eaQSort(eaSortedScores, openmission_ScoreEntrySortByScore);

		// First compute average score, for numeric scaling
		for(i = eaSize(&eaSortedScores) - 1; i >= 0; i--) {
			fAvgScore += eaSortedScores[i]->fPoints;
		}

		if (eaSize(&eaSortedScores)) {
			fAvgScore = fAvgScore/eaSize(&eaSortedScores);
		}

		// Grant rewards to each player
		for(i = eaSize(&eaSortedScores) - 1; i >= 0; i--) {
			Entity *pTargetEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, eaSortedScores[i]->playerID);

			if (pDef->bOnlyRewardIfScored && !eaSortedScores[i]->fPoints)
				continue;

			if (pTargetEnt){
				int iPlayerLevel = pTargetEnt ? entity_GetSavedExpLevel(pTargetEnt) : 0;
				int iLevel = 0;
				int iTable;
				F32 fScale = (pDef->params?pDef->params->NumericRewardScale:0.0f);

				// Determine which Reward Table and what scaling to use based on player's rank
				if (i == 0) {
					iTable = 0;
					fScale *= 1.5;
				} else if (i == 1 || i < eaSize(&eaSortedScores)*.2) {
					iTable = 1;
					fScale *= 1.25;
				} else if (i == 2 || i < eaSize(&eaSortedScores)*.5) {
					iTable = 2;
					fScale *= 1.1;
				} else {
					iTable = 3;
				}

				// Scale down Numerics for players with worse-than-average scores
				if(fAvgScore && (eaSortedScores[i]->fPoints < fAvgScore)) {
					fScale *= (eaSortedScores[i]->fPoints/fAvgScore);
				}

				// If iScaleRewardOverTime is specified, scale the reward based on the expected mission time
				if (pDef->params && pDef->params->iScaleRewardOverTime) {
					U32 uCurrentTime = timeSecondsSince2000();
					S32 iScaleRewardOverTime = pDef->params->iScaleRewardOverTime;
					S32 iElapsedMissionTime = (S32)(uCurrentTime - pMission->startTime);
					fScale *= reward_GetRewardScaleOverTime(iElapsedMissionTime, iScaleRewardOverTime);
				}

				// Determine the level to grant rewards at
				iLevel = missiondef_CalculateLevel(iPartitionIdx, iPlayerLevel, pDef);
				if (iPlayerLevel < missiondef_GetOfferLevel(iPartitionIdx, pDef, iPlayerLevel)) {
					// Use the player's level if they are below the Offer Level.
					// That way they'll get the same rewards as everyone else if they are only
					// 1 or 2 levels below the mission's level, but it will scale down if they
					// are below that.
					iLevel = iPlayerLevel;
				}

				// Grant rewards to this player
				if (pRewardDropPoint) {
					Mission **eaMissions = NULL;
					int j;

					mission_GetSubmissions(pOpenMission->pMission, &eaMissions, -1, NULL, NULL, NULL, true);
					eaInsert(&eaMissions, pOpenMission->pMission, 0);

					// Grant rewards for all complete submissions and the root mission
					for(j = 0; j < eaSize(&eaMissions); j++) {
						RewardTable *pTable = NULL;
						MissionDef *pRewardMissionDef = mission_GetDef(eaMissions[j]);
						if (pRewardMissionDef) {
							switch(iTable) {
								case 0:
									pTable = pDef->params?RefSystem_ReferentFromString(g_hRewardTableDict, bFailure ? pRewardMissionDef->params->pchFailureGoldRewardTable : pRewardMissionDef->params->pchGoldRewardTable):NULL;
								xcase 1:
									pTable = pDef->params?RefSystem_ReferentFromString(g_hRewardTableDict, bFailure ? pRewardMissionDef->params->pchFailureSilverRewardTable : pRewardMissionDef->params->pchSilverRewardTable):NULL;
								xcase 2:
									pTable = pDef->params?RefSystem_ReferentFromString(g_hRewardTableDict, bFailure ? pRewardMissionDef->params->pchFailureBronzeRewardTable : pRewardMissionDef->params->pchBronzeRewardTable):NULL;
									break;
								default:
									pTable = pDef->params?RefSystem_ReferentFromString(g_hRewardTableDict, bFailure ? pRewardMissionDef->params->pchFailureDefaultRewardTable : pRewardMissionDef->params->pchDefaultRewardTable):NULL;
									break;
							}
							if (pTable) {
								ItemChangeReason reason = {0};
								inv_FillItemChangeReason(&reason, pTargetEnt, "Mission:OpenMissionReward", pRewardMissionDef->name);
								reward_OpenMissionExec(iPartitionIdx, pTargetEnt, pDef, pTable, iLevel, fScale, pRewardDropPoint[3], &reason);
							}
						}
					}
				}
			}
		}
	}
	eaDestroy(&eaSortedScores);

}

// Gives Rewards to all participants of an Open Mission according to their scores.
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(OpenMissionGrantRewards);
void openmission_FuncOpenMissionGrantRewards(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName, ACMD_EXPR_LOC_MAT4_IN pRewardDropPoint)
{
	openmission_FuncOpenMissionGrantRewardsEx(pContext, iPartitionIdx, pcMissionName, pRewardDropPoint, false);
}

// Gives Failure Rewards to all participants of an Open Mission according to their scores.
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(OpenMissionGrantFailureRewards);
void openmission_FuncOpenMissionGrantFailureRewards(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName, ACMD_EXPR_LOC_MAT4_IN pRewardDropPoint)
{
	openmission_FuncOpenMissionGrantRewardsEx(pContext, iPartitionIdx, pcMissionName, pRewardDropPoint, true);
}

static void openmission_SetPlayerRewardTier(Entity *pPlayerEnt, S32 iTier)
{
	if (pPlayerEnt)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pPlayerEnt);

		if(pInfo && pInfo->pLeaderboardInfo)
		{
			pInfo->pLeaderboardInfo->iRewardTier = iTier;
		}
	}
}

void openmission_FuncOpenMissionGrantRewardsNoPointEx(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName, bool bFailure)
{
	OpenMission *pOpenMission = openmission_GetFromName(iPartitionIdx, pcMissionName);
	Mission *pMission = pOpenMission ? pOpenMission->pMission : NULL;
	MissionDef *pDef = mission_GetDef(pMission);
	OpenMissionScoreEntry **eaSortedScores = NULL;
	int i;

	if (pDef && pOpenMission) {
		F32 fAvgScore = 0.f;
		eaPushEArray(&eaSortedScores, &pOpenMission->eaScores);
		eaQSort(eaSortedScores, openmission_ScoreEntrySortByScore);

		// First compute average score, for numeric scaling
		for(i = eaSize(&eaSortedScores) - 1; i >= 0; i--) {
			fAvgScore += eaSortedScores[i]->fPoints;
		}

		if (eaSize(&eaSortedScores)) {
			fAvgScore = fAvgScore/eaSize(&eaSortedScores);
		}

		// Grant rewards to each player
		for(i = eaSize(&eaSortedScores) - 1; i >= 0; i--) {
			Entity *pTargetEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, eaSortedScores[i]->playerID);

			if (pDef->bOnlyRewardIfScored && !eaSortedScores[i]->fPoints)
				continue;

			if (pTargetEnt){
				int iPlayerLevel = pTargetEnt ? entity_GetSavedExpLevel(pTargetEnt) : 0;
				int iLevel = 0;
				int iTable;
				F32 fScale = (pDef->params?pDef->params->NumericRewardScale:0.0f);

				// Determine which Reward Table and what scaling to use based on player's rank
				iTable = openmission_GetRewardTierForScoreEntry(&eaSortedScores, i, eaSize(&eaSortedScores));
				if (iTable == -1)
				{
					break;
				}

				// Scale down Numerics for players with worse-than-average scores
				if(fAvgScore && (eaSortedScores[i]->fPoints < fAvgScore)) {
					fScale *= (eaSortedScores[i]->fPoints/fAvgScore);
				}

				// If iScaleRewardOverTime is specified, scale the reward based on the expected mission time
				if (pDef->params && pDef->params->iScaleRewardOverTime) {
					U32 uCurrentTime = timeSecondsSince2000();
					S32 iScaleRewardOverTime = pDef->params->iScaleRewardOverTime;
					S32 iElapsedMissionTime = (S32)(uCurrentTime - pMission->startTime);
					fScale *= reward_GetRewardScaleOverTime(iElapsedMissionTime, iScaleRewardOverTime);
				}

				// Determine the level to grant rewards at
				iLevel = missiondef_CalculateLevel(iPartitionIdx, iPlayerLevel, pDef);
				if (iPlayerLevel < missiondef_GetOfferLevel(iPartitionIdx, pDef, iPlayerLevel)) {
					// Use the player's level if they are below the Offer Level.
					// That way they'll get the same rewards as everyone else if they are only
					// 1 or 2 levels below the mission's level, but it will scale down if they
					// are below that.
					iLevel = iPlayerLevel;
				}

				if (iTable == 0)
				{
					eventsend_ContestWin(pTargetEnt, iTable, pDef->pchRefString);
				}

				// Grant rewards to this player
				{
					Mission **eaMissions = NULL;
					int j;

					mission_GetSubmissions(pOpenMission->pMission, &eaMissions, -1, NULL, NULL, NULL, true);
					eaInsert(&eaMissions, pOpenMission->pMission, 0);

					// Grant rewards for all complete submissions and the root mission
					for(j = 0; j < eaSize(&eaMissions); j++) {
						RewardTable *pTable = NULL;
						MissionDef *pRewardMissionDef = mission_GetDef(eaMissions[j]);
						Vec3 v3Origin;
						copyVec3(zerovec3, v3Origin);
						if (pRewardMissionDef) {
							switch(iTable) {
							case 0:
								pTable = pDef->params?RefSystem_ReferentFromString(g_hRewardTableDict, bFailure ? pRewardMissionDef->params->pchFailureGoldRewardTable : pRewardMissionDef->params->pchGoldRewardTable):NULL;
							xcase 1:
								pTable = pDef->params?RefSystem_ReferentFromString(g_hRewardTableDict, bFailure ? pRewardMissionDef->params->pchFailureSilverRewardTable : pRewardMissionDef->params->pchSilverRewardTable):NULL;
							xcase 2:
								pTable = pDef->params?RefSystem_ReferentFromString(g_hRewardTableDict, bFailure ? pRewardMissionDef->params->pchFailureBronzeRewardTable : pRewardMissionDef->params->pchBronzeRewardTable):NULL;
								break;
							default:
								pTable = pDef->params?RefSystem_ReferentFromString(g_hRewardTableDict, bFailure ? pRewardMissionDef->params->pchFailureDefaultRewardTable : pRewardMissionDef->params->pchDefaultRewardTable):NULL;
								break;
							}
							if (pTable) {
								ItemChangeReason reason = {0};
								inv_FillItemChangeReason(&reason, pTargetEnt, "Mission:OpenMissionReward", pRewardMissionDef->name);
								reward_OpenMissionExec(iPartitionIdx, pTargetEnt, pDef, pTable, iLevel, fScale, v3Origin, &reason);
								ClientCmd_OpenMissionShowLeaderboard(pTargetEnt);
								openmission_SetPlayerRewardTier(pTargetEnt, iTable);
							}
						}
					}
				}
			}
		}
	}
	eaDestroy(&eaSortedScores);
}

// Gives Rewards to all participants of an Open Mission according to their scores. This uses a different reward granting algorithm than the other expressions
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(OpenMissionGrantRewardsNoDropPoint);
void openmission_FuncOpenMissionGrantRewardsNoPoint(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName)
{
	openmission_FuncOpenMissionGrantRewardsNoPointEx(pContext, iPartitionIdx, pcMissionName, false);
}

// Gives Rewards to all participants of an Open Mission according to their scores. This uses a different reward granting algorithm than the other expressions
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(OpenMissionGrantFailureRewardsNoDropPoint);
void openmission_FuncOpenMissionGrantFailureRewardsNoPoint(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName)
{
	openmission_FuncOpenMissionGrantRewardsNoPointEx(pContext, iPartitionIdx, pcMissionName, true);
}

void openmission_FuncOpenMissionGrantRewardsNonRecursiveEx(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName, ACMD_EXPR_LOC_MAT4_IN pRewardDropPoint, bool bFailure)
{
	OpenMission *pOpenMission = openmission_GetFromName(iPartitionIdx, pcMissionName);
	Mission *pMission = openmission_FindMissionFromRefString(iPartitionIdx, pcMissionName);
	MissionDef *pDef = mission_GetDef(pMission);
	OpenMissionScoreEntry **eaSortedScores = NULL;
	int i;

	if (pDef && pOpenMission) {
		RewardTable *pGoldTable = pDef->params?RefSystem_ReferentFromString(g_hRewardTableDict, bFailure ? pDef->params->pchFailureGoldRewardTable : pDef->params->pchGoldRewardTable):NULL;
		RewardTable *pSilverTable = pDef->params?RefSystem_ReferentFromString(g_hRewardTableDict, bFailure ? pDef->params->pchFailureSilverRewardTable : pDef->params->pchSilverRewardTable):NULL;
		RewardTable *pBronzeTable = pDef->params?RefSystem_ReferentFromString(g_hRewardTableDict, bFailure ? pDef->params->pchFailureBronzeRewardTable : pDef->params->pchBronzeRewardTable):NULL;
		RewardTable *pDefaultTable = pDef->params?RefSystem_ReferentFromString(g_hRewardTableDict, bFailure ? pDef->params->pchFailureDefaultRewardTable : pDef->params->pchDefaultRewardTable):NULL;

		F32 fAvgScore = 0.f;
		eaPushEArray(&eaSortedScores, &pOpenMission->eaScores);
		eaQSort(eaSortedScores, openmission_ScoreEntrySortByScore);

		// First compute average score, for numeric scaling
		for(i = eaSize(&eaSortedScores) - 1; i >= 0; i--) {
			fAvgScore += eaSortedScores[i]->fPoints;
		}

		if (eaSize(&eaSortedScores)) {
			fAvgScore = fAvgScore/eaSize(&eaSortedScores);
		}

		// Grant rewards to each player
		for(i = eaSize(&eaSortedScores) - 1; i >= 0; i--) {
			Entity* pTargetEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, eaSortedScores[i]->playerID);

			if (pDef->bOnlyRewardIfScored && !eaSortedScores[i]->fPoints)
				continue;

			if (pTargetEnt) {
				int iPlayerLevel = pTargetEnt ? entity_GetSavedExpLevel(pTargetEnt) : 0;
				int iLevel = 0;
				RewardTable *pTable;
				F32 fScale = (pDef->params?pDef->params->NumericRewardScale:0.0f);

				// Determine which Reward Table and what scaling to use based on player's rank
				if (i == 0) {
					pTable = pGoldTable;
					fScale *= 1.5;
				} else if (i == 1 || i < eaSize(&eaSortedScores)*.2) {
					pTable = pSilverTable;
					fScale *= 1.25;
				} else if (i == 2 || i < eaSize(&eaSortedScores)*.5) {
					pTable = pBronzeTable;
					fScale *= 1.1;
				} else {
					pTable = pDefaultTable;
				}

				// Scale down Numerics for players with worse-than-average scores
				if (fAvgScore && (eaSortedScores[i]->fPoints < fAvgScore)) {
					fScale *= (eaSortedScores[i]->fPoints/fAvgScore);
				}

				// If iScaleRewardOverTime is specified, scale the reward based on the expected mission time
				if (pDef->params && pDef->params->iScaleRewardOverTime) {
					U32 uCurrentTime = timeSecondsSince2000();
					S32 iScaleRewardOverTime = pDef->params->iScaleRewardOverTime;
					S32 iElapsedMissionTime = (S32)(uCurrentTime - pMission->startTime);
					fScale *= reward_GetRewardScaleOverTime(iElapsedMissionTime, iScaleRewardOverTime);
				}

				// Determine the level to grant rewards at
				iLevel = missiondef_CalculateLevel(iPartitionIdx, iPlayerLevel, pDef);
				if (iPlayerLevel < missiondef_GetOfferLevel(iPartitionIdx, pDef, iPlayerLevel)) {
					// Use the player's level if they are below the Offer Level.
					// That way they'll get the same rewards as everyone else if they are only
					// 1 or 2 levels below the mission's level, but it will scale down if they
					// are below that.
					iLevel = iPlayerLevel;
				}
					
				// Grant rewards to this player
				if (pRewardDropPoint) {
					ItemChangeReason reason = {0};
					inv_FillItemChangeReason(&reason, pTargetEnt, "Mission:OpenMissionRewardExpression", pDef->name);
					reward_OpenMissionExec(iPartitionIdx, pTargetEnt, pDef, pTable, iLevel, fScale, pRewardDropPoint[3], &reason);
				}
			}
		}
	}
	eaDestroy(&eaSortedScores);
}

// Gives Rewards to all participants of an Open Mission according to their scores.
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(OpenMissionGrantRewardsNonRecursive);
void openmission_FuncOpenMissionGrantRewardsNonRecursive(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName, ACMD_EXPR_LOC_MAT4_IN pRewardDropPoint)
{
	openmission_FuncOpenMissionGrantRewardsNonRecursiveEx(pContext, iPartitionIdx, pcMissionName, pRewardDropPoint, false);
}

// Gives Rewards to all participants of an Open Mission according to their scores.
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(OpenMissionGrantFailureRewardsNonRecursive);
void openmission_FuncOpenMissionGrantFailureRewardsNonRecursive(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char *pcMissionName, ACMD_EXPR_LOC_MAT4_IN pRewardDropPoint)
{
	openmission_FuncOpenMissionGrantRewardsNonRecursiveEx(pContext, iPartitionIdx, pcMissionName, pRewardDropPoint, true);
}
