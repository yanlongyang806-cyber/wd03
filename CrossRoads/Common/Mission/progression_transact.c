/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "progression_transact.h"
#include "AutoTransDefs.h"
#include "qsortG.h"
#include "StringCache.h"
#include "mission_common.h"
#include "AutoGen/mission_common_h_ast.h"
#include "progression_common.h"
#include "AutoGen/progression_common_h_ast.h"
#include "Entity.h"
#include "AutoGen/Entity_h_ast.h"
#include "Player.h"
#include "AutoGen/Player_h_ast.h"
#include "Team.h"
#include "AutoGen/Team_h_ast.h"
#include "EntityLib.h"

AUTO_TRANS_HELPER;
bool progression_trh_IsValidStoryArcForPlayer(ATH_ARG NOCONST(Entity)* pEnt, SA_PARAM_OP_VALID GameProgressionNodeDef* pNodeDef)
{
	GameProgressionNodeDef* pRootNodeDef = progression_GetStoryRootNode(pNodeDef);

	if (!pEnt || !pRootNodeDef)
	{
		return false;
	}
	if (IS_HANDLE_ACTIVE(pRootNodeDef->hRequiredAllegiance))
	{
		if (GET_REF(pRootNodeDef->hRequiredAllegiance) != GET_REF(pEnt->hAllegiance) ||
			GET_REF(pRootNodeDef->hRequiredSubAllegiance) != GET_REF(pEnt->hSubAllegiance))
		{
			return false;
		}
	}
	if (pRootNodeDef->bDebug && (!pEnt->pPlayer || pEnt->pPlayer->accessLevel < ACCESS_DEBUG))
	{
		return false;
	}
	return true;
}

AUTO_TRANS_HELPER;
bool progression_trh_StoryWindBackCheck(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameProgressionNodeDef* pNodeJustCompleted)
{
	if (NONNULL(pEnt) && 
		NONNULL(pEnt->pPlayer) && 
		NONNULL(pEnt->pPlayer->pProgressionInfo) && 
		pNodeJustCompleted &&
		GET_REF(pNodeJustCompleted->hNodeToWindBack))
	{
		GameProgressionNodeDef* pNodeToWindBack = GET_REF(pNodeJustCompleted->hNodeToWindBack);
		GameProgressionNodeDef* pCurrNodeDef = pNodeJustCompleted;
		GameProgressionNodeDef* pLastValidNode = NULL;

		while (pCurrNodeDef && pCurrNodeDef->pMissionGroupInfo)
		{
			pLastValidNode = pCurrNodeDef;

			// Remove the node from the completed node list
			if (eaSize(&pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes) > 0)
			{
				// Uncomplete this node
				int iIndex = (int)eaBFind(pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, strCmp, pCurrNodeDef->pchName);

				if (pCurrNodeDef->pchName == eaGet(&pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, iIndex))
				{
					eaRemove(&pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, iIndex);									
				}
			}

			// Add windback missions
			FOR_EACH_IN_EARRAY_FORWARDS(pCurrNodeDef->pMissionGroupInfo->eaMissions, GameProgressionMission, pProgMission)
			{
				if (!progression_trh_IsMissionOptional(pEnt, pProgMission))
				{
					int iIndex = (int)eaBFind(pEnt->pPlayer->pProgressionInfo->ppchWindBackMissions, strCmp, pProgMission->pchMissionName);

					if (pProgMission->pchMissionName != eaGet(&pEnt->pPlayer->pProgressionInfo->ppchWindBackMissions, iIndex))
					{
						eaInsert(&pEnt->pPlayer->pProgressionInfo->ppchWindBackMissions, (char*)pProgMission->pchMissionName, iIndex);
					}
				}
			}
			FOR_EACH_END

			if (pCurrNodeDef == pNodeToWindBack)
			{
				break;
			}
			pCurrNodeDef = GET_REF(pCurrNodeDef->hPrevSibling);
		}

		return true;
	}

	return false;
}

AUTO_TRANS_HELPER;
bool progression_trh_IsMissionCompleteForNode(ATH_ARG NOCONST(Entity)* pEnt, const char* pchMissionName, GameProgressionNodeDef* pNodeDef)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pProgressionInfo) && pchMissionName && pNodeDef)
	{
		GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pNodeDef);
		const char* pchBranchNodeDefName = SAFE_MEMBER(pBranchNodeDef, pchName);
		NOCONST(ReplayProgressionData)* pReplayData = eaIndexedGetUsingString(&pEnt->pPlayer->pProgressionInfo->eaReplayData, pchBranchNodeDefName);

		if (NONNULL(pReplayData) && pNodeDef == GET_REF(pReplayData->hNode))
		{
			return eaBSearch(pReplayData->ppchCompletedMissions, strCmp, pchMissionName) != NULL;
		}
		if (progression_trh_PlayerFindWindBackMission(pEnt, pchMissionName) < 0 &&
			eaIndexedFindUsingString(&pEnt->pPlayer->missionInfo->completedMissions, pchMissionName) >= 0)
		{
			return true;
		}
	}
	return false;
}

AUTO_TRANS_HELPER;
bool progression_trh_UpdateTeamProgress(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ProgressionUpdateParams* pUpdateParams)
{
	GameProgressionNodeDef* pNodeDef = GET_REF(pUpdateParams->hOverrideNode);
	GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pNodeDef);

	if (ISNULL(pEnt) || 
		ISNULL(pEnt->pPlayer) ||
		ISNULL(pEnt->pPlayer->pProgressionInfo))
	{
		return false;
	}
	if (pUpdateParams->bDestroyTeamData)
	{
		StructDestroyNoConstSafe(parse_TeamProgressionData, &pEnt->pPlayer->pProgressionInfo->pTeamData);
		return true;
	}
	if (ISNULL(pEnt->pPlayer->pProgressionInfo->pTeamData))
	{
		pEnt->pPlayer->pProgressionInfo->pTeamData = StructCreateNoConst(parse_TeamProgressionData);
	}
	pEnt->pPlayer->pProgressionInfo->pTeamData->iLastUpdated = pUpdateParams->iOverrideTime;
	SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pBranchNodeDef, pEnt->pPlayer->pProgressionInfo->pTeamData->hStoryArcNode);
	COPY_HANDLE(pEnt->pPlayer->pProgressionInfo->pTeamData->hNode, pUpdateParams->hOverrideNode);

	if (g_GameProgressionConfig.bStoreMostRecentlyPlayedNode)
	{
		// Set the last played node
		COPY_HANDLE(pEnt->pPlayer->pProgressionInfo->hMostRecentlyPlayedNode, pUpdateParams->hOverrideNode);
	}

	if (eaSize(&pUpdateParams->ppchMissionsToUncomplete) > 0 && 
		eaSize(&pEnt->pPlayer->pProgressionInfo->pTeamData->ppchCompletedMissions) > 0)
	{
		S32 i;
		for (i = 0; i < eaSize(&pUpdateParams->ppchMissionsToUncomplete); i++)
		{
			const char* pchUncompleteMission = pUpdateParams->ppchMissionsToUncomplete[i];
			S32 iCompletedMissionCount = eaSize(&pEnt->pPlayer->pProgressionInfo->pTeamData->ppchCompletedMissions);

			if (iCompletedMissionCount > 0)
			{
				int iIndex = (int)eaBFind(pEnt->pPlayer->pProgressionInfo->pTeamData->ppchCompletedMissions, strCmp, pchUncompleteMission);
				
				if (pchUncompleteMission == eaGet(&pEnt->pPlayer->pProgressionInfo->pTeamData->ppchCompletedMissions, iIndex))
				{
					eaRemove(&pEnt->pPlayer->pProgressionInfo->pTeamData->ppchCompletedMissions, iIndex);
				}
			}
			else
			{
				break;
			}
		}
	}
	return true;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.pProgressionInfo.pTeamData, .Pplayer.Pprogressioninfo.Hmostrecentlyplayednode");
enumTransactionOutcome progression_tr_UpdateTeamProgress(ATR_ARGS, NOCONST(Entity)* pEnt, ProgressionUpdateParams *pUpdateParams)
{
	if (!progression_trh_UpdateTeamProgress(ATR_PASS_ARGS, pEnt, pUpdateParams))
	{
		if (NONNULL(pEnt))
		{
			TRANSACTION_RETURN_LOG_FAILURE("Failed to update team progress for Entity[%u]", pEnt->myContainerID);
		}
		TRANSACTION_RETURN_LOG_FAILURE("Failed to update team progress for unknown entity");
	}
	TRANSACTION_RETURN_LOG_SUCCESS("progression_tr_UpdateTeamProgress updated team progress for Entity[%u]", pEnt->myContainerID);
}

// This function is called right after a mission completes and it updates the list of completed missions stored in TeamProgressionData
AUTO_TRANS_HELPER;
void progression_trh_UpdateCompletedMissionForTeam(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, MissionDef* pCompletedMissionDef)
{
	GameProgressionNodeDef* pNodeDef = progression_trh_GetNodeFromMissionDef(pEnt, pCompletedMissionDef, NULL);

	if (NONNULL(pEnt) &&
		NONNULL(pEnt->pPlayer) &&
		NONNULL(pEnt->pPlayer->pProgressionInfo) && 
		NONNULL(pEnt->pPlayer->pProgressionInfo->pTeamData) && 
		pNodeDef && 
		g_GameProgressionConfig.bEnableTeamProgressionTracking)
	{
		if (pCompletedMissionDef)
		{
			int iIndex = (int)eaBFind(pEnt->pPlayer->pProgressionInfo->pTeamData->ppchCompletedMissions, strCmp, pCompletedMissionDef->name);

			if (pCompletedMissionDef->name != eaGet(&pEnt->pPlayer->pProgressionInfo->pTeamData->ppchCompletedMissions, iIndex))
			{
				// Add to the list
				eaInsert(&pEnt->pPlayer->pProgressionInfo->pTeamData->ppchCompletedMissions, (char*)pCompletedMissionDef->name, iIndex);
			}
		}
	}
}

AUTO_TRANS_HELPER;
bool progression_trh_CheckNodeShouldBeCompleted(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameProgressionNodeDef* pNodeDef, const char* pchCompletedMission)
{
	int i, iMissionCount;

	if (!pNodeDef || !pNodeDef->pMissionGroupInfo)
		return false;

	iMissionCount = eaSize(&pNodeDef->pMissionGroupInfo->eaMissions);
	for (i = 0; i < iMissionCount; i++)
	{
		GameProgressionMission* pProgMission = pNodeDef->pMissionGroupInfo->eaMissions[i];

		if (pProgMission->pchMissionName == pchCompletedMission ||
			progression_trh_IsMissionOptional(pEnt, pProgMission))
		{
			continue;
		}
		if (!progression_trh_IsMissionCompleteForNode(pEnt, pProgMission->pchMissionName, pNodeDef))
		{
			return false;
		}
	}
	return true;
}

AUTO_TRANS_HELPER;
void progression_trh_UpdateCompletedMissionForPlayer(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, MissionDef* pCompletedMissionDef)
{
	GameProgressionNodeDef* pNodeDef = progression_trh_GetNodeFromMissionDef(pEnt, pCompletedMissionDef, NULL);
	GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pNodeDef);
	
	if (NONNULL(pEnt) && 
		NONNULL(pEnt->pPlayer) && 
		NONNULL(pEnt->pPlayer->pProgressionInfo) && 
		NONNULL(pEnt->pPlayer->missionInfo) &&
		pNodeDef && 
		pNodeDef->pMissionGroupInfo &&
		pBranchNodeDef)
	{
		NOCONST(ReplayProgressionData)* pReplayData;
		int iWindbackIndex = progression_trh_PlayerFindWindBackMission(pEnt, pCompletedMissionDef->name);
		int iReplayIndex;

		// Remove the windback mission
		if (iWindbackIndex >= 0)
		{
			eaRemove(&pEnt->pPlayer->pProgressionInfo->ppchWindBackMissions, iWindbackIndex);
		}

		// Find the replay data
		iReplayIndex = eaIndexedFindUsingString(&pEnt->pPlayer->pProgressionInfo->eaReplayData, pBranchNodeDef->pchName);
		pReplayData = eaGet(&pEnt->pPlayer->pProgressionInfo->eaReplayData, iReplayIndex);

		// If the node isn't complete
		if (!progression_trh_CheckNodeShouldBeCompleted(ATR_PASS_ARGS, pEnt, pNodeDef, pCompletedMissionDef->name))
		{
			if (NONNULL(pReplayData) && pNodeDef == GET_REF(pReplayData->hNode))
			{
				int iIndex = (int)eaBFind(pReplayData->ppchCompletedMissions, strCmp, pCompletedMissionDef->name);

				if (pCompletedMissionDef->name != eaGet(&pReplayData->ppchCompletedMissions, iIndex))
				{
					// Add the completed replay mission
					eaInsert(&pReplayData->ppchCompletedMissions, (char *)pCompletedMissionDef->name, iIndex);
				}
			}
		}
		else
		{
			int iIndex = (int)eaBFind(pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, strCmp, pNodeDef->pchName);
			
			// If all missions are complete, mark the node as completed
			if (pNodeDef->pchName != eaGet(&pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, iIndex))
			{
				// Add this node to the list of completed nodes
				eaInsert(&pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, (char *)pNodeDef->pchName, iIndex);
			}

			// Try to wind back
			if (progression_trh_StoryWindBackCheck(ATR_PASS_ARGS, pEnt, pNodeDef))
			{
				if (NONNULL(pReplayData) && pNodeDef == GET_REF(pReplayData->hNode))
				{
					StructDestroyNoConst(parse_ReplayProgressionData, eaRemove(&pEnt->pPlayer->pProgressionInfo->eaReplayData, iReplayIndex));
				}
			}
			else
			{
				// Advance or destroy replay data
				if (NONNULL(pReplayData) && pNodeDef == GET_REF(pReplayData->hNode))
				{
					GameProgressionNodeDef* pNextNodeDef = GET_REF(pNodeDef->hNextSibling);
					int iNextIndex = pNextNodeDef ? (int)eaBFind(pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, strCmp, pNextNodeDef->pchName) : -1;

					if (!pNextNodeDef || pNextNodeDef->pchName != eaGet(&pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, iNextIndex))
					{
						StructDestroyNoConst(parse_ReplayProgressionData, eaRemove(&pEnt->pPlayer->pProgressionInfo->eaReplayData, iReplayIndex));
					}
					else
					{
						SET_HANDLE_FROM_REFERENT("GameProgressionNodeDef", pNextNodeDef, pReplayData->hNode);
						eaDestroy(&pReplayData->ppchCompletedMissions);
					}
				}
			}
		}
	}
}

AUTO_TRANS_HELPER;
int progression_trh_PlayerFindWindBackMission(ATH_ARG NOCONST(Entity)* pEnt, const char* pchMissionName)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pProgressionInfo) && pchMissionName)
	{
		int iIndex = (int)eaBFind(pEnt->pPlayer->pProgressionInfo->ppchWindBackMissions, strCmp, pchMissionName);
		if (pchMissionName == eaGet(&pEnt->pPlayer->pProgressionInfo->ppchWindBackMissions, iIndex))
		{
			return iIndex;
		}
	}
	return -1;
}

AUTO_TRANS_HELPER;
bool progression_trh_ProgressionNodeCompleted(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameProgressionNodeDef* pNodeDef)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pProgressionInfo) && 
		pNodeDef && eaSize(&pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes))
	{
		int iIndex = (int)eaBFind(pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, strCmp, pNodeDef->pchName);
		if (pNodeDef->pchName == eaGet(&pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, iIndex))
		{
			return true;
		}
	}
	return false;
}

AUTO_TRANS_HELPER;
bool progression_trh_ProgressionNodeUnlocked(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameProgressionNodeDef* pNodeDef)
{
	if (pNodeDef)
	{
		GameProgressionNodeDef* pRequiredNodeDef = GET_REF(pNodeDef->hRequiredNode);
		GameProgressionNodeDef* pPrevNodeDef = GET_REF(pNodeDef->hPrevSibling);
		GameProgressionNodeDef *pStoryRootNode = progression_GetStoryRootNode(pNodeDef);

		// Make sure the required node is completed
		if (pRequiredNodeDef && !progression_trh_ProgressionNodeCompleted(ATR_PASS_ARGS, pEnt, pRequiredNodeDef))
		{
			return false;
		}
		// Make sure the previous node is also completed
		if (progression_NodeUnlockRequiresPreviousNodeToBeCompleted(pNodeDef) &&
			pPrevNodeDef && !progression_trh_ProgressionNodeCompleted(ATR_PASS_ARGS, pEnt, pPrevNodeDef))
		{
			return false;
		}

		if (pNodeDef->pMissionGroupInfo &&
			pNodeDef->pMissionGroupInfo->iRequiredPlayerLevel &&
			entity_trh_GetSavedExpLevel(pEnt) < pNodeDef->pMissionGroupInfo->iRequiredPlayerLevel)
		{
			return false;
		}
		return true;
	}
	return false;
}

AUTO_TRANS_HELPER;
enumTransactionOutcome progression_trh_ExecutePostMissionAcceptTasks(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, 
	MissionDef *pMissionDef)
{
	GameProgressionNodeDef *pNodeDef = progression_trh_GetNodeFromMissionDef(pEnt, pMissionDef, NULL);

	if (pNodeDef)
	{
		// This mission is linked with a progression node.
		// Check if we should set the current progression to this node.
		GameProgressionNodeDef *pStoryRootNodeDef = progression_GetStoryRootNode(pNodeDef);

		// Set the last played node
		if (g_GameProgressionConfig.bStoreMostRecentlyPlayedNode && 
			NONNULL(pEnt->pPlayer) && 
			NONNULL(pEnt->pPlayer->pProgressionInfo))
		{
			SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNodeDef, pEnt->pPlayer->pProgressionInfo->hMostRecentlyPlayedNode);
		}		

		if (pStoryRootNodeDef && pStoryRootNodeDef->bSetProgressionOnMissionAccept)
		{
			ProgressionUpdateParams *pUpdateParams = StructCreate(parse_ProgressionUpdateParams);
			SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNodeDef, pUpdateParams->hOverrideNode);
			pUpdateParams->iOverrideTime = timeSecondsSince2000();

			if (progression_trh_SetProgression(ATR_PASS_ARGS, pEnt, pUpdateParams) != TRANSACTION_OUTCOME_SUCCESS)
			{
				StructDestroy(parse_ProgressionUpdateParams, pUpdateParams);
				return TRANSACTION_OUTCOME_FAILURE;
			}

			if (NONNULL(pEnt->pTeam) && 
				pEnt->pTeam->iTeamID &&
				pEnt->pTeam->eState == TeamState_Member)
			{
				progression_trh_UpdateTeamProgress(ATR_PASS_ARGS, pEnt, pUpdateParams);
			}

			StructDestroy(parse_ProgressionUpdateParams, pUpdateParams);
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pteam.Iteamid, .Pteam.Estate, .Hallegiance, .Hsuballegiance, .Pplayer.Pprogressioninfo.Eareplaydata, .Pplayer.Pprogressioninfo.Pteamdata, .Pplayer.Pprogressioninfo.Ppchcompletednodes, \
.Pplayer.Pprogressioninfo.Hmostrecentlyplayednode, .Pchar.Ilevelexp");
enumTransactionOutcome progression_tr_ExecutePostMissionAcceptTasks(ATR_ARGS, NOCONST(Entity)* pEnt, const char *pchMissionName)
{
	MissionDef *pMissionDef = missiondef_FindMissionByName(NULL, pchMissionName);

	if (pMissionDef == NULL)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return progression_trh_ExecutePostMissionAcceptTasks(ATR_PASS_ARGS, pEnt, pMissionDef);
}

AUTO_TRANS_HELPER;
enumTransactionOutcome progression_trh_SetProgression(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ProgressionUpdateParams *pUpdateParams)
{
	GameProgressionNodeDef* pNodeDef = GET_REF(pUpdateParams->hOverrideNode);
	GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pNodeDef);

	if (!g_GameProgressionConfig.bAllowReplay)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	// Validate the update params
	if (!pUpdateParams || !GET_REF(pUpdateParams->hOverrideNode))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pProgressionInfo) && pNodeDef && pBranchNodeDef)
	{
		NOCONST(ReplayProgressionData)* pReplayData;

		if (!progression_trh_ProgressionNodeUnlocked(ATR_PASS_ARGS, pEnt, pNodeDef))
		{
			return TRANSACTION_OUTCOME_FAILURE;
		}		

		pReplayData = eaIndexedGetUsingString(&pEnt->pPlayer->pProgressionInfo->eaReplayData, pBranchNodeDef->pchName);
		if (!pReplayData)
		{
			pReplayData = StructCreateNoConst(parse_ReplayProgressionData);
			SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pBranchNodeDef, pReplayData->hStoryArcNode);
			eaIndexedAdd(&pEnt->pPlayer->pProgressionInfo->eaReplayData, pReplayData);
		}

		COPY_HANDLE(pReplayData->hNode, pUpdateParams->hOverrideNode);
		eaClear(&pReplayData->ppchCompletedMissions);

		if (pUpdateParams->bDestroyTeamData)
		{
			StructDestroyNoConstSafe(parse_TeamProgressionData, &pEnt->pPlayer->pProgressionInfo->pTeamData);
		}

		if (g_GameProgressionConfig.bStoreMostRecentlyPlayedNode)
		{
			// Set the last played node
			COPY_HANDLE(pEnt->pPlayer->pProgressionInfo->hMostRecentlyPlayedNode, pReplayData->hNode);
		}

		return TRANSACTION_OUTCOME_SUCCESS;
	}
	else
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pprogressioninfo.Ppchcompletednodes, .Pplayer.Pprogressioninfo.Eareplaydata, .Pplayer.Pprogressioninfo.Pteamdata, \
.Pplayer.Pprogressioninfo.Hmostrecentlyplayednode, .Pchar.Ilevelexp");
enumTransactionOutcome progression_tr_SetProgression(ATR_ARGS, NOCONST(Entity)* pEnt, ProgressionUpdateParams *pUpdateParams)
{
	if (progression_trh_SetProgression(ATR_PASS_ARGS, pEnt, pUpdateParams) == TRANSACTION_OUTCOME_SUCCESS)
	{
		TRANSACTION_RETURN_LOG_SUCCESS("progression_tr_SetProgression for entity[%u] executed successfully.", pEnt->myContainerID);
	}
	else
	{
		TRANSACTION_RETURN_LOG_FAILURE("progression_tr_SetProgression for entity[%u] failed.", pEnt->myContainerID);
	}
}

AUTO_TRANS_HELPER;
void progression_trh_CompleteThroughNode(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameProgressionNodeDef* pNodeDef)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pProgressionInfo))
	{
		GameProgressionNodeDef* pStoryArcNodeDef = progression_GetStoryBranchNode(pNodeDef);
		GameProgressionNodeDef* pCurrNodeDef = progression_FindLeftMostLeaf(pStoryArcNodeDef);
		
		while (pCurrNodeDef)
		{
			int iIndex = (int)eaBFind(pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, strCmp, pCurrNodeDef->pchName);

			if (pCurrNodeDef->pchName != eaGet(&pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, iIndex))
			{
				// Add this node to the list of completed nodes
				eaInsert(&pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, (char *)pCurrNodeDef->pchName, iIndex);
			}
			if (pCurrNodeDef == pNodeDef)
			{
				// We're done
				break;
			}
			pCurrNodeDef = GET_REF(pCurrNodeDef->hNextSibling);
		}
	}
}

// Completes all the nodes in the story up to and including the given node.
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pprogressioninfo.Ppchcompletednodes");
enumTransactionOutcome progression_tr_CompleteThroughNode(ATR_ARGS, NOCONST(Entity)* pEnt, const char* pchNodeName)
{
	GameProgressionNodeDef* pNodeDef = progression_NodeDefFromName(pchNodeName);

	if (NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pProgressionInfo))
	{
		if (pNodeDef)
		{
			progression_trh_CompleteThroughNode(ATR_PASS_ARGS, pEnt, pNodeDef);

			TRANSACTION_RETURN_LOG_SUCCESS("progression_tr_CompleteThroughNode for entity[%u] executed successfully.", pEnt->myContainerID);
		}
		else
		{
			TRANSACTION_RETURN_LOG_FAILURE("Node passed to progression_tr_CompleteThroughNode is not linked with any story. Entity[%u], Node: %s", pEnt->myContainerID, pNodeDef->pchName);
		}
	}
	else
	{
		if (!pNodeDef)
		{
			TRANSACTION_RETURN_LOG_FAILURE("Node passed to progression_tr_CompleteThroughNode is NULL. progression_tr_CompleteThroughNode failed. Entity[%u]", pEnt->myContainerID);
		}
		else
		{
			TRANSACTION_RETURN_LOG_FAILURE("Entity[%u] does not have any progression info. progression_tr_CompleteThroughNode failed.", pEnt->myContainerID);
		}		
	}
}

AUTO_TRANS_HELPER;
void progression_trh_UncompleteThroughNode(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, GameProgressionNodeDef* pNodeDef)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pProgressionInfo))
	{
		GameProgressionNodeDef* pStoryArcNodeDef = progression_GetStoryBranchNode(pNodeDef);
		GameProgressionNodeDef* pCurrNodeDef = progression_FindRightMostLeaf(pStoryArcNodeDef);
		
		while (pCurrNodeDef)
		{
			int iIndex = (int)eaBFind(pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, strCmp, pCurrNodeDef->pchName);
			
			if (pCurrNodeDef->pchName == eaGet(&pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, iIndex))
			{
				eaRemove(&pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, iIndex);
			}
			if (pCurrNodeDef == pNodeDef)
			{
				// We're done
				break;
			}
			pCurrNodeDef = GET_REF(pCurrNodeDef->hPrevSibling);
		}
	}
}

// Un-completes all the nodes in the story until the given node.
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pprogressioninfo.Ppchcompletednodes");
enumTransactionOutcome progression_tr_UncompleteThroughNode(ATR_ARGS, NOCONST(Entity)* pEnt, const char* pchNodeName)
{
	GameProgressionNodeDef* pNodeDef = progression_NodeDefFromName(pchNodeName);

	if (NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pProgressionInfo))
	{
		if (pNodeDef)
		{
			progression_trh_UncompleteThroughNode(ATR_PASS_ARGS, pEnt, pNodeDef);

			TRANSACTION_RETURN_LOG_SUCCESS("progression_tr_UncompleteUntilNode for entity[%u] executed successfully.", pEnt->myContainerID);
		}
		else
		{
			TRANSACTION_RETURN_LOG_FAILURE("Node passed to progression_tr_UncompleteUntilNode is not linked with any story. progression_tr_UncompleteUntilNode failed. Entity[%u], Node: %s", pEnt->myContainerID, pNodeDef->pchName);
		}
	}
	else
	{
		if (!pNodeDef)
		{
			TRANSACTION_RETURN_LOG_FAILURE("Node passed to progression_tr_UncompleteUntilNode is NULL. progression_tr_UncompleteUntilNode failed. Entity[%u]", pEnt->myContainerID);
		}
		else
		{
			TRANSACTION_RETURN_LOG_FAILURE("Entity[%u] does not have any progression info. progression_tr_UncompleteUntilNode failed.", pEnt->myContainerID);
		}		
	}
}

// Skip a progression mission
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Hallegiance, .Hsuballegiance, .Pplayer.Pprogressioninfo.Ppchskippedmissions, .Pplayer.Pprogressioninfo.Ppchwindbackmissions, .Pplayer.Missioninfo.Completedmissions, .Pplayer.Pprogressioninfo.Eareplaydata, .Pplayer.Pprogressioninfo.Ppchcompletednodes");
enumTransactionOutcome progression_tr_SkipMission(ATR_ARGS, NOCONST(Entity)* pEnt, const char* pchMissionName)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pProgressionInfo))
	{
		GameProgressionMission* pProgMission = NULL;
		GameProgressionNodeDef* pNodeDef = progression_trh_GetNodeFromMissionName(pEnt, pchMissionName, NULL);
		int iProgMissionIdx = progression_FindMissionForNode(pNodeDef, pchMissionName);

		if (iProgMissionIdx >= 0)
		{
			pProgMission = eaGet(&pNodeDef->pMissionGroupInfo->eaMissions, iProgMissionIdx);
		}
		if (pProgMission && !pProgMission->bOptional && progression_trh_IsMissionSkippable(pEnt, pNodeDef, iProgMissionIdx))
		{
			int iIndex = (int)eaBFind(pEnt->pPlayer->pProgressionInfo->ppchSkippedMissions, strCmp, pProgMission->pchMissionName);

			if (pProgMission->pchMissionName != eaGet(&pEnt->pPlayer->pProgressionInfo->ppchSkippedMissions, iIndex))
			{
				eaInsert(&pEnt->pPlayer->pProgressionInfo->ppchSkippedMissions, (char*)pProgMission->pchMissionName, iIndex);

				// Try to complete the node
				if (progression_trh_CheckNodeShouldBeCompleted(ATR_PASS_ARGS, pEnt, pNodeDef, pProgMission->pchMissionName))
				{
					iIndex = (int)eaBFind(pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, strCmp, pNodeDef->pchName);
			
					// If all missions are complete, mark the node as completed
					if (pNodeDef->pchName != eaGet(&pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, iIndex))
					{
						// Add this node to the list of completed nodes
						eaInsert(&pEnt->pPlayer->pProgressionInfo->ppchCompletedNodes, (char *)pNodeDef->pchName, iIndex);
					}
				}
				TRANSACTION_RETURN_LOG_SUCCESS("progression_tr_SkipMission for entity[%u] mission[%s] executed successfully.", pEnt->myContainerID, pchMissionName);
			}
			else
			{
				TRANSACTION_RETURN_LOG_FAILURE("progression_tr_SkipMission for entity[%u] mission[%s] failed. The mission has already been skipped.", pEnt->myContainerID, pchMissionName);
			}
		}
		TRANSACTION_RETURN_LOG_FAILURE("progression_tr_SkipMission for entity[%u] mission[%s] failed. The mission wasn't found.", pEnt->myContainerID, pchMissionName);
	}
	TRANSACTION_RETURN_LOG_FAILURE("progression_tr_SkipMission failed. Invalid entity.");
}

// Remove a specific skipped progression mission
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Hallegiance, .Hsuballegiance, .Pplayer.Pprogressioninfo.Ppchskippedmissions");
enumTransactionOutcome progression_tr_RemoveSkippedMission(ATR_ARGS, NOCONST(Entity)* pEnt, const char* pchMissionName)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pProgressionInfo))
	{
		bool bFound = false;
		GameProgressionMission* pProgMission = NULL;
		progression_trh_GetNodeFromMissionName(pEnt, pchMissionName, &pProgMission);

		if (pProgMission)
		{
			int iIndex = (int)eaBFind(pEnt->pPlayer->pProgressionInfo->ppchSkippedMissions, strCmp, pProgMission->pchMissionName);

			if (pProgMission->pchMissionName == eaGet(&pEnt->pPlayer->pProgressionInfo->ppchSkippedMissions, iIndex))
			{
				eaRemove(&pEnt->pPlayer->pProgressionInfo->ppchSkippedMissions, iIndex);

				TRANSACTION_RETURN_LOG_SUCCESS("progression_tr_RemoveSkippedMission for entity[%u] mission[%s] executed successfully.", pEnt->myContainerID, pchMissionName);

				bFound = true;
			}
		}
		if (!bFound)
		{
			TRANSACTION_RETURN_LOG_FAILURE("progression_tr_RemoveSkippedMission failed. Entity[%u] does not have skipped mission[%s].", pEnt->myContainerID, pchMissionName);
		}
	}
	TRANSACTION_RETURN_LOG_FAILURE("progression_tr_RemoveSkippedMission failed. Invalid entity.");
}

// Remove all skipped progression missions
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pprogressioninfo.Ppchskippedmissions");
enumTransactionOutcome progression_tr_ClearSkippedMissions(ATR_ARGS, NOCONST(Entity)* pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pProgressionInfo))
	{
		if (eaSize(&pEnt->pPlayer->pProgressionInfo->ppchSkippedMissions))
		{
			eaDestroy(&pEnt->pPlayer->pProgressionInfo->ppchSkippedMissions);

			TRANSACTION_RETURN_LOG_SUCCESS("progression_tr_ClearSkippedMissions for entity[%u] executed successfully.", pEnt->myContainerID);
		}
		else
		{
			TRANSACTION_RETURN_LOG_FAILURE("progression_tr_ClearSkippedMissions failed. Entity[%u] has no skipped missions.", pEnt->myContainerID);
		}
	}
	TRANSACTION_RETURN_LOG_FAILURE("progression_tr_ClearSkippedMissions failed. Invalid entity.");
}
