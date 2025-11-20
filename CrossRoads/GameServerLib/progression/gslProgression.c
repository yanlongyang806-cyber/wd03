/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Expression.h"
#include "gslProgression.h"
#include "AutoGen/gslProgression_h_ast.h"
#include "progression_common.h"
#include "progression_transact.h"
#include "AutoGen/progression_common_h_ast.h"
#include "EntityIterator.h"
#include "EntityLib.h"
#include "mission_common.h"
#include "Player.h"
#include "StringCache.h"
#include "Team.h"
#include "gslMapTransfer.h"
#include "gslMission.h"
#include "GameServerLib.h"
#include "WorldGrid.h"
#include "gslSpawnPoint.h"
#include "LoggedTransactions.h"
#include "qsortG.h"

#include "objTransactions.h"
#include "Autogen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "Autogen/GameServerLib_autogen_RemoteFuncs.h"

#define PROGRESSION_SYSTEM_TICK	5
#define PROGRESSION_DEFAULT_ALLOWED_MAP_COUNT 100

static const char** s_ppchProgressionTrackedMaps = NULL;
// Evaluate a progression expression
bool gslProgression_EvalExpression(int iPartitionIdx, Expression* pExpr, Entity *pEnt)
{
	if (pExpr) 
	{
		MultiVal mvResultVal = {0};
		ExprContext* pContext = progression_GetContext(pEnt);

		exprEvaluate(pExpr, pContext, &mvResultVal);

		if (!MultiValGetInt(&mvResultVal, NULL))
		{
			return false;
		}
	} 
	return true;
}

// Adds a map to the list of tracked progression maps allowed for players
void gslProgression_AddTrackedProgressionMap(const char* pchAllowedMapName)
{
	const char* pchAllowedMapNamePooled = allocFindString(pchAllowedMapName);
	int i = (int)eaBFind(s_ppchProgressionTrackedMaps, strCmp, pchAllowedMapNamePooled);
	if (i == eaSize(&s_ppchProgressionTrackedMaps) || s_ppchProgressionTrackedMaps[i] != pchAllowedMapNamePooled)
	{
		eaInsert(&s_ppchProgressionTrackedMaps, pchAllowedMapNamePooled, i);
	}
}

static bool gslProgression_FindTrackedProgressionMap(const char* pchAllowedMapName)
{
	if (s_ppchProgressionTrackedMaps)
	{
		const char* pchAllowedMapNamePooled = allocFindString(pchAllowedMapName);
		int i = (int)eaBFind(s_ppchProgressionTrackedMaps, strCmp, pchAllowedMapNamePooled);
		if (i != eaSize(&s_ppchProgressionTrackedMaps) && s_ppchProgressionTrackedMaps[i] == pchAllowedMapNamePooled)
		{
			return true;
		}
	}
	return false;
}

void gslProgression_OncePerFrame(F32 fTimeStep)
{
	PERFINFO_AUTO_START("ProgressionTick", 1);

	if (g_GameProgressionConfig.bEnableTeamProgressionTracking)
	{
		static U32 s_ProgressionTick = 0;
		Entity* pCurrEnt;
		U32 uWhichPlayer = 0;
		EntityIterator* pIter;	
		U32 uCurrTickModded = s_ProgressionTick % PROGRESSION_SYSTEM_TICK;

		// Process all players
		// Don't process ENTITYFLAG_IGNORE ents here, just to save time; they're probably still loading the map
		pIter = entGetIteratorSingleTypeAllPartitions(0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
		while ((pCurrEnt = EntityIteratorGetNext(pIter))) 
		{
			if ((uWhichPlayer % PROGRESSION_SYSTEM_TICK) == uCurrTickModded)
			{
				ProgressionInfo *pProgressionInfo = progression_GetInfoFromPlayer(pCurrEnt);

				if (pProgressionInfo)
				{
					PERFINFO_AUTO_START("TeamProgressionTick", 1);
					progression_ProcessTeamProgression(pCurrEnt, true);
					PERFINFO_AUTO_STOP();

					PERFINFO_AUTO_START("PlayerProgressionTick", 1);
					progression_ProcessPlayer(pCurrEnt, pProgressionInfo);
					PERFINFO_AUTO_STOP();
				}
			}
			uWhichPlayer++;
		}
		EntityIteratorRelease(pIter);

		s_ProgressionTick++;
	}
	PERFINFO_AUTO_STOP();
}

static bool gslProgression_NodeAllowsMap(SA_PARAM_NN_VALID GameProgressionNodeDef* pNodeDef, SA_PARAM_NN_STR const char *pchMapDescription)
{
	if (pNodeDef->pMissionGroupInfo)
	{
		return eaFindString(&pNodeDef->pMissionGroupInfo->ppchAllowedMissionMaps, pchMapDescription) >= 0;
	}
	return false;
}

// Indicates whether the player is allowed to be on the given map based on player's progression
bool gslProgression_PlayerIsAllowedOnMap(Entity *pEnt, const char *pchMapDescription)
{
	ProgressionInfo *pInfo = progression_GetInfoFromPlayer(pEnt);

	if (!gslProgression_FindTrackedProgressionMap(pchMapDescription))
	{
		return true;
	}
	if (pInfo)
	{
		// If there is an override we ignore personal progression
		if (pInfo->pTeamData)
		{
			if (GET_REF(pInfo->pTeamData->hStoryArcNode))
			{
				GameProgressionNodeDef* pNodeDef = GET_REF(pInfo->pTeamData->hNode);

				if (pNodeDef && gslProgression_NodeAllowsMap(pNodeDef, pchMapDescription))
				{
					return true;
				}
			}
			return false;
		}
		FOR_EACH_IN_EARRAY_FORWARDS(pInfo->eaTrackingData, ProgressionTrackingData, pData)
		{
			GameProgressionNodeDef* pNodeDef = GET_REF(pData->hNode);
			GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pNodeDef);
			ReplayProgressionData* pReplayData = NULL;
			
			if (pBranchNodeDef)
			{
				pReplayData = eaIndexedGetUsingString(&pInfo->eaReplayData, pBranchNodeDef->pchName);
				if (pReplayData)
				{
					pNodeDef = GET_REF(pReplayData->hNode);
				}
			}
			if (pNodeDef && gslProgression_NodeAllowsMap(pNodeDef, pchMapDescription))
			{
				return true;
			}
		}
		FOR_EACH_END
	}	

	return false;
}

static void gslProgression_SetProgression_CB(TransactionReturnVal* returnVal, SetProgressionByMissionCallbackParams* pParams)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		GameProgressionNodeDef *pNode = pParams ? GET_REF(pParams->hNode) : NULL;
		if (pNode->pMissionGroupInfo &&
			pNode->pMissionGroupInfo->pchMapName &&
			pNode->pMissionGroupInfo->pchMapName[0])
		{
			// Get the entity
			Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pParams->iEntContainerID);
			ZoneMapInfo *pMapInfo = NULL;
			
			// Update the entity's current progression
			if (pEnt)
			{
				// Set dirty bits
				entity_SetDirtyBit(pEnt, parse_ProgressionInfo, pEnt->pPlayer->pProgressionInfo, true);
				entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, true);

				progression_UpdateCurrentProgression(pEnt);

				if (pParams->bGrantMission && 
					g_GameProgressionConfig.bAutoGrantMissionOnSetProgression && 
					pNode->pMissionGroupInfo &&
					!pNode->pMissionGroupInfo->bDontAutoGrantMissionOnSetProgression)
				{
					MissionDef *pFirstRequiredMission;
					devassert(pEnt->pPlayer && pEnt->pPlayer->pProgressionInfo);
					pFirstRequiredMission = progression_GetFirstRequiredMissionByNode(pNode);

					if (pFirstRequiredMission)
					{
						pEnt->astrMissionToGrant = pFirstRequiredMission->name;
					}
				}
			}
			if (pParams->bWarpToSpawnPoint)
			{
				pMapInfo = zmapInfoGetByPublicName(pNode->pMissionGroupInfo->pchMapName);
			}
			if (pEnt && pMapInfo)
			{
				ZoneMapType eMapType = zmapInfoGetMapType(pMapInfo);

				// Don't warp if entity is already in the same map
				if (pNode->pMissionGroupInfo->pchMapName != gGSLState.gameServerDescription.baseMapDescription.mapDescription)
				{
					const char *pchSpawnPoint = pNode->pMissionGroupInfo->pchSpawnPoint && pNode->pMissionGroupInfo->pchSpawnPoint[0] ? 
						pNode->pMissionGroupInfo->pchSpawnPoint : START_SPAWN;

					if (eMapType == ZMTYPE_MISSION)
					{
						MapMoveStaticEx(pEnt, pNode->pMissionGroupInfo->pchMapName, 
							pchSpawnPoint, 0, 0, 0, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), NULL, 
							0, MAPSEARCHTYPE_OWNED_MAP, 
							STACK_SPRINTF("progression_tr_SetProgression_CB is transferring Entity[%d] to map because the progression is set manually. (Name=%s)", 
							entGetContainerID(pEnt), pNode->pMissionGroupInfo->pchMapName), NULL);
					}
					else if (eMapType == ZMTYPE_STATIC)
					{
						MapMoveStaticEx(pEnt, pNode->pMissionGroupInfo->pchMapName, 
							pchSpawnPoint, 0, 0, 0, 0, 0, NULL, 
							0, MAPSEARCHTYPE_ONE_MAPNAME_ALL_CHOICES, 
							STACK_SPRINTF("progression_tr_SetProgression_CB is transferring Entity[%d] to map because the progression is set manually. (Name=%s)", 
							entGetContainerID(pEnt), pNode->pMissionGroupInfo->pchMapName), NULL);
					}
					else
					{
						Errorf("Encountered an invalid map type for a game progression node. Game progression node: %s, Map name: %s.", pNode->pchName, pNode->pMissionGroupInfo->pchMapName);
					}
				}
			}			
		}
	}

	if (pParams)
	{
		StructDestroy(parse_SetProgressionByMissionCallbackParams, pParams);
	}	
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(SetProgressionByMission) ACMD_PRIVATE;
void progression_cmd_SetProgressionByMission(Entity *pEnt, SA_PARAM_OP_STR const char *pchMissionName, bool bWarpToSpawnPoint)
{
	GameProgressionNodeDef* pNodeDef = progression_GetNodeFromMissionName(pEnt, pchMissionName, NULL);

	if (g_GameProgressionConfig.bAllowReplay &&
		pEnt && 
		pNodeDef && 
		!team_IsWithTeam(pEnt) &&
		progression_ProgressionNodeUnlocked(pEnt, pNodeDef))
	{
		GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pNodeDef);

		if (pBranchNodeDef)
		{
			ProgressionUpdateParams *pUpdateParams = StructCreate(parse_ProgressionUpdateParams);

			SetProgressionByMissionCallbackParams *pParams = StructCreate(parse_SetProgressionByMissionCallbackParams);
			pParams->iEntContainerID = pEnt->myContainerID; 
			pParams->bWarpToSpawnPoint = bWarpToSpawnPoint;
			SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNodeDef, pParams->hNode);
			pParams->bGrantMission = true;
			
			SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNodeDef, pUpdateParams->hOverrideNode);
			AutoTrans_progression_tr_SetProgression(LoggedTransactions_CreateManagedReturnVal("Progression_SetProgression", gslProgression_SetProgression_CB, (void*)pParams), 
				GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, pUpdateParams);
			StructDestroy(parse_ProgressionUpdateParams, pUpdateParams);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(SetProgressionByGameProgressionNode) ACMD_PRIVATE;
void gslProgression_cmd_SetProgressionByGameProgressionNode(Entity *pEnt, SA_PARAM_OP_STR const char *pchNodeName, bool bWarpToSpawnPoint)
{
	GameProgressionNodeDef *pNodeDef = progression_NodeDefFromName(pchNodeName);

	if (g_GameProgressionConfig.bAllowReplay &&
		pEnt && 
		pNodeDef && 
		!team_IsWithTeam(pEnt) &&
		progression_ProgressionNodeUnlocked(pEnt, pNodeDef))
	{
		GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pNodeDef);

		if (pBranchNodeDef)
		{
			ProgressionUpdateParams *pUpdateParams = StructCreate(parse_ProgressionUpdateParams);

			SetProgressionByMissionCallbackParams *pParams = StructCreate(parse_SetProgressionByMissionCallbackParams);
			pParams->iEntContainerID = pEnt->myContainerID; 
			pParams->bWarpToSpawnPoint = bWarpToSpawnPoint;
			SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNodeDef, pParams->hNode);
			pParams->bGrantMission = true;

			SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNodeDef, pUpdateParams->hOverrideNode);
			AutoTrans_progression_tr_SetProgression(LoggedTransactions_CreateManagedReturnVal("Progression_SetProgression", gslProgression_SetProgression_CB, (void*)pParams), 
				GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pUpdateParams);
			StructDestroy(parse_ProgressionUpdateParams, pUpdateParams);
		}
	}
}

static void progression_UpdateTeamProgress_CB(TransactionReturnVal* returnVal, SetProgressionByMissionCallbackParams* pParams)
{
	if (returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (pParams->bGrantMission || pParams->bWarpToSpawnPoint)
		{
			// Get the entity
			Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pParams->iEntContainerID);

			// Get the team
			Team *pTeam = team_GetTeam(pEnt);

			if (pTeam)
			{
				FOR_EACH_IN_CONST_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
				{
					RemoteCommand_gslProgression_rcmd_PostSetTeamProgression(GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID, pParams);
				}
				FOR_EACH_END
			}
		}
	}

	if (pParams)
	{
		StructDestroy(parse_SetProgressionByMissionCallbackParams, pParams);
	}	
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gslProgression_rcmd_PostSetTeamProgression(CmdContext* context, SetProgressionByMissionCallbackParams *pParams)
{
	// Get the entity
	Entity *pEnt = entFromContainerIDAnyPartition(context->clientType, context->clientID);

	if (pEnt)
	{
		GameProgressionNodeDef *pNode = pParams ? GET_REF(pParams->hNode) : NULL;

		if (pParams->bWarpToSpawnPoint && 
			team_IsTeamLeader(pEnt) &&
			pNode->pMissionGroupInfo &&
			pNode->pMissionGroupInfo->pchMapName &&
			pNode->pMissionGroupInfo->pchMapName[0] &&
			pNode->pMissionGroupInfo->pchMapName != gGSLState.gameServerDescription.baseMapDescription.mapDescription) // Don't warp if entity is already in the same map
		{
			const char *pchSpawnPoint = pNode->pMissionGroupInfo->pchSpawnPoint && pNode->pMissionGroupInfo->pchSpawnPoint[0] ? 
				pNode->pMissionGroupInfo->pchSpawnPoint : START_SPAWN;

			spawnpoint_MovePlayerToMapAndSpawn(pEnt, pNode->pMissionGroupInfo->pchMapName, pchSpawnPoint, NULL, 0, 0, 0, 0, NULL, NULL, NULL, 0, true);
		}

		if (pParams->bGrantMission && 
			g_GameProgressionConfig.bAutoGrantMissionOnSetProgression &&
			pNode->pMissionGroupInfo &&
			!pNode->pMissionGroupInfo->bDontAutoGrantMissionOnSetProgression)
		{
			MissionDef *pFirstRequiredMission;
			devassert(pEnt->pPlayer && pEnt->pPlayer->pProgressionInfo);
			pFirstRequiredMission = progression_GetFirstRequiredMissionByNode(pNode);

			if (pFirstRequiredMission)
			{
				pEnt->astrMissionToGrant = pFirstRequiredMission->name;
			}
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(SetTeamProgressionByGameProgressionNode) ACMD_PRIVATE;
void gslProgression_cmd_SetTeamProgressionByGameProgressionNode(Entity *pEnt, SA_PARAM_OP_STR const char *pchNodeName, bool bWarpToSpawnPoint)
{
	GameProgressionNodeDef* pNodeDef = progression_NodeDefFromName(pchNodeName);
	Team* pTeam = team_GetTeam(pEnt);

	if (g_GameProgressionConfig.bAllowReplay &&
		g_GameProgressionConfig.bEnableTeamProgressionTracking &&
		pTeam && 
		pNodeDef && 
		team_IsTeamLeader(pEnt) &&
		progression_NodeUnlockedByAnyTeamMember(pTeam, pNodeDef))
	{
		GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pNodeDef);

		if (pBranchNodeDef)
		{
			ProgressionUpdateParams *pUpdateParams = StructCreate(parse_ProgressionUpdateParams);

			SetProgressionByMissionCallbackParams *pParams = StructCreate(parse_SetProgressionByMissionCallbackParams);
			pParams->iEntContainerID = pEnt->myContainerID; 
			pParams->bWarpToSpawnPoint = bWarpToSpawnPoint;
			SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNodeDef, pParams->hNode);
			pParams->bGrantMission = true;

			SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNodeDef, pUpdateParams->hOverrideNode);
			pUpdateParams->iOverrideTime = timeSecondsSince2000();

			AutoTrans_progression_tr_UpdateTeamProgress(LoggedTransactions_CreateManagedReturnVal("Progression_UpdateTeamProgress", progression_UpdateTeamProgress_CB, pParams), GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pUpdateParams);
			StructDestroy(parse_ProgressionUpdateParams, pUpdateParams);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_NAME(ProgressionUncompleteThroughNode) ACMD_PRIVATE;
void gslProgression_cmd_DebugUncompleteThroughNode(Entity *pEnt, ACMD_NAMELIST("GameProgressionNodeDef", REFDICTIONARY) const char *pchNodeName)
{
	GameProgressionNodeDef* pNodeDef = progression_NodeDefFromName(pchNodeName);
	if (pEnt && pNodeDef)
	{
		SetProgressionByMissionCallbackParams *pParams = StructCreate(parse_SetProgressionByMissionCallbackParams);
		pParams->iEntContainerID = pEnt->myContainerID; 
		pParams->bWarpToSpawnPoint = false;
		SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNodeDef, pParams->hNode);
		AutoTrans_progression_tr_UncompleteThroughNode(LoggedTransactions_CreateManagedReturnVal("Progression_UncompleteThroughNode", gslProgression_SetProgression_CB, pParams), GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pchNodeName);
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9) ACMD_NAME(ProgressionCompleteThroughNode) ACMD_PRIVATE;
void gslProgression_cmd_DebugCompleteThroughNode(Entity *pEnt, ACMD_NAMELIST("GameProgressionNodeDef", REFDICTIONARY) const char *pchNodeName)
{
	GameProgressionNodeDef* pNodeDef = progression_NodeDefFromName(pchNodeName);

	if (pEnt && pNodeDef)
	{
		SetProgressionByMissionCallbackParams *pParams = StructCreate(parse_SetProgressionByMissionCallbackParams);
		pParams->iEntContainerID = pEnt->myContainerID; 
		pParams->bWarpToSpawnPoint = false;
		SET_HANDLE_FROM_REFERENT(g_hGameProgressionNodeDictionary, pNodeDef, pParams->hNode);
		AutoTrans_progression_tr_CompleteThroughNode(LoggedTransactions_CreateManagedReturnVal("Progression_CompleteThroughNode", gslProgression_SetProgression_CB, pParams), GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pchNodeName);
	}
}

static void gslProgression_SkipMission_CB(TransactionReturnVal* pReturn, void* pData)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		Entity* pEnt = entFromEntityRefAnyPartition((EntityRef)(intptr_t)pData);
		if (pEnt)
		{
			progression_UpdateCurrentProgression(pEnt);
		}
	}
}

static bool gslProgression_PlayerMeetsMissionRequirements(Entity* pEnt, GameProgressionMission* pProgMission)
{
	if (pProgMission)
	{
		MissionInfo* pInfo = mission_GetInfoFromPlayer(pEnt);
		Mission* pMission = mission_FindMissionFromRefString(pInfo, pProgMission->pchMissionName);
		if (pMission)
		{
			if (pMission->state == MissionState_InProgress)
			{
				return true;
			}
		}
		else
		{
			MissionDef* pMissionDef = missiondef_FindMissionByName(NULL, pProgMission->pchMissionName);
			if (pMissionDef && missiondef_CanBeOfferedAsPrimary(pEnt, pMissionDef, NULL, NULL))
			{
				return true;
			}
		}
	}
	return false;
}

// Skip a progression mission
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(ProgressionSkipMission) ACMD_HIDE;
void gslProgression_cmd_SkipMission(Entity *pEnt, const char *pchMissionName)
{
	ProgressionInfo *pProgressionInfo = progression_GetInfoFromPlayer(pEnt);
	GameProgressionMission* pProgMission = NULL;
	GameProgressionNodeDef* pNodeDef = progression_GetNodeFromMissionName(pEnt, pchMissionName, NULL);
	int iProgMissionIdx = progression_FindMissionForNode(pNodeDef, pchMissionName);
	if (iProgMissionIdx >= 0)
	{
		pProgMission = eaGet(&pNodeDef->pMissionGroupInfo->eaMissions, iProgMissionIdx);
	}
	if (pProgressionInfo && 
		pProgMission && 
		!progression_IsMissionOptional(pEnt, pProgMission) && 
		progression_IsMissionSkippable(pEnt, pNodeDef, iProgMissionIdx) &&
		(!g_GameProgressionConfig.bMustMeetRequirementsToSkipMissions || gslProgression_PlayerMeetsMissionRequirements(pEnt, pProgMission)))
	{
		TransactionReturnVal* pReturn = objCreateManagedReturnVal(gslProgression_SkipMission_CB, (void *)(intptr_t)entGetRef(pEnt));
		AutoTrans_progression_tr_SkipMission(pReturn, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pchMissionName);
	}
}

// Remove a specific skipped progression mission
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(ProgressionRemoveSkippedMission) ACMD_HIDE;
void gslProgression_cmd_RemoveSkippedMission(Entity *pEnt, const char* pchMissionName)
{
	ProgressionInfo *pProgressionInfo = progression_GetInfoFromPlayer(pEnt);
	GameProgressionMission* pProgMission = NULL;
	progression_GetNodeFromMissionName(pEnt, pchMissionName, &pProgMission);

	if (pProgressionInfo && pProgMission && pProgMission->bSkippable && progression_IsMissionOptional(pEnt, pProgMission))
	{
		TransactionReturnVal* pReturn = objCreateManagedReturnVal(gslProgression_SkipMission_CB, (void *)(intptr_t)entGetRef(pEnt));
		AutoTrans_progression_tr_RemoveSkippedMission(pReturn, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt), pchMissionName);
	}
}

// Remove all skipped progression missions
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(ProgressionClearSkippedMissions) ACMD_HIDE;
void gslProgression_cmd_ClearSkippedMissions(Entity *pEnt)
{
	ProgressionInfo *pProgressionInfo = progression_GetInfoFromPlayer(pEnt);
	if (pProgressionInfo && eaSize(&pProgressionInfo->ppchSkippedMissions))
	{
		TransactionReturnVal* pReturn = objCreateManagedReturnVal(gslProgression_SkipMission_CB, (void *)(intptr_t)entGetRef(pEnt));
		AutoTrans_progression_tr_ClearSkippedMissions(pReturn, GetAppGlobalType(), entGetType(pEnt), entGetContainerID(pEnt));
	}
}

#include "AutoGen/gslProgression_h_ast.c"