/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "LobbyCommon.h"

#include "gslLobby.h"
#include "Entity.h"
#include "Player.h"
#include "EntityLib.h"
#include "utilitiesLib.h"
#include "progression_common.h"
#include "progression_transact.h"
#include "ugcprojectcommon.h"
#include "LoggedTransactions.h"
#include "UGCCommon.h"
#include "ResourceInfo.h"
#include "StringCache.h"
#include "GameServerLib.h"
#include "WorldGrid.h"
#include "gslMapTransfer.h"
#include "gslSpawnPoint.h"

#include "AutoGen/gslLobbyCommands_c_ast.h"
#include "AutoGen/LobbyCommon_h_ast.h"
#include "AutoGen/progression_common_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

// Params for the initiate play content call back
AUTO_STRUCT;
typedef struct InitiatePlayContentCBParams
{
	ContainerID entContainerID;
	GameContentNodeRef * pNodeRef;
} InitiatePlayContentCBParams;

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_NAME(LobbyCommand_GetMissionRewards);
void gslLobbyCommand_scmdGetMissionRewards(Entity *pEnt, GameContentNodeRef *pNodeRef)
{
	GameContentNodeRewardResult *pResult = gslLobby_GetMissionRewards(pEnt, pNodeRef);

	ClientCmd_LobbyCommand_ReceiveGameContentNodeRewards(pEnt, pResult);

	StructDestroy(parse_GameContentNodeRewardResult, pResult);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER) ACMD_NAME(LobbyCommand_GetMissionRewards);
GameContentNodeRewardResult * gslLobbyCommand_rcmd_GetMissionRewards(LobbyEntityContainer *pEntContainer, GameContentNodeRef *pNodeRef)
{	
	return gslLobby_GetMissionRewards(pEntContainer->pEnt, pNodeRef);
}

static void gslLobby_InitiatePlayContentCB(TransactionReturnVal* returnVal, InitiatePlayContentCBParams *pParams)
{
	Entity * pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pParams->entContainerID);
	UGCProjectList* ugcResult = NULL;
	bool bSuccess = false;

	if (pEnt == NULL)
	{
		StructDestroy(parse_InitiatePlayContentCBParams, pParams);
		return;
	}

	// Came from quest selector (solo play)
	if (REF_HANDLE_IS_ACTIVE(pParams->pNodeRef->hNode)) 
	{
		bSuccess = returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS;
	} 
	else if (pParams->pNodeRef->iUGCProjectID) 
	{
		// This code is likely to be removed by Jeff Weinstein very soon.
		devassertmsg(false, "UGC is no longer supporting lobby with multi-shard. See Andrew Ames or Jeff Weinstein if you get this.");

		bSuccess = false;//TRANSACTION_OUTCOME_SUCCESS == RemoteCommandCheck_UGCSearchByID(returnVal, &ugcResult);
	}

	if (bSuccess)
	{
		const UGCProject* ugcProject = ugcResult ? eaGet( &ugcResult->eaProjects, 0 ) : NULL;
		const UGCProjectVersion* ugcVersion = UGCProject_GetMostRecentPublishedVersion( ugcProject );

		if (pParams->pNodeRef->iUGCProjectID && ugcVersion == NULL)
		{
			// Failed to retrieve UGC project information
			return;
		}

		if (ugcVersion) 
		{
			char ugcMissionName[ RESOURCE_NAME_MAX_SIZE ];			

			sprintf(ugcMissionName, "%s:Mission", ugcVersion->pNameSpace);
			pEnt->astrMissionToGrant = (const char *)allocAddString( ugcMissionName );

			// MJF July/30/2012 - We no longer want players to
			// automove when using the Lobby.  The following code
			// warps the player to the target map, in case we ever
			// want to resurrect that behavior.
			//
			// if (ugcVersion->strInitialMapName == gGSLState.gameServerDescription.baseMapDescription.mapDescription)
			// {
			// 	// Don't do anything, you're already on the map
			// }
			// else
			// {
			// 	WorldVariable *mission_num = StructCreate(parse_WorldVariable);
			// 	WorldVariable **worldVariables = NULL;

			// 	mission_num->pcName = "mission_num";
			// 	mission_num->eType = WVAR_INT;
			// 	mission_num->iIntVal = 1;
			// 	eaPush(&worldVariables, mission_num);
			
			// 	MapMoveStaticEx(pEnt, ugcVersion->strInitialMapName, 
			// 					ugcVersion->strInitialSpawnPoint, 0, 0, 0, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), worldVariables, 
			// 					0, MAPSEARCHTYPE_OWNED_MAP, 
			// 					STACK_SPRINTF("progression_tr_SetProgression_CB is transferring Entity[%d] to map because the progression is set manually. (Name=%s)", 
			// 								  entGetContainerID(pEnt), ugcVersion->strInitialMapName), NULL);
			// 	StructDestroy(parse_WorldVariable, mission_num);
			// 	eaDestroy(&worldVariables);
			// }
		}
		else if (IS_HANDLE_ACTIVE(pParams->pNodeRef->hNode))
		{
			GameProgressionNodeDef *pDestinationNode = GET_REF(pParams->pNodeRef->hNode);
			
			devassert(pEnt->pPlayer && pEnt->pPlayer->pProgressionInfo);

			if (pDestinationNode)
			{
				ZoneMapInfo * pMapInfo = zmapInfoGetByPublicName(pDestinationNode->pMissionGroupInfo->pchMapName);

				if (g_GameProgressionConfig.bAutoGrantMissionOnSetProgression &&
					pDestinationNode->pMissionGroupInfo && 
					!pDestinationNode->pMissionGroupInfo->bDontAutoGrantMissionOnSetProgression)
				{
					pEnt->astrMissionToGrant = progression_GetFirstRequiredMissionNameByNode(pDestinationNode);
				}
				
				if (pMapInfo)
				{
					const char *pchSpawnPoint = pDestinationNode->pMissionGroupInfo->pchSpawnPoint && pDestinationNode->pMissionGroupInfo->pchSpawnPoint[0] ? 
						pDestinationNode->pMissionGroupInfo->pchSpawnPoint : START_SPAWN;

					if (pDestinationNode->pMissionGroupInfo->pchMapName == gGSLState.gameServerDescription.baseMapDescription.mapDescription)
					{
						spawnpoint_MovePlayerToNamedSpawn(pEnt, pchSpawnPoint, NULL, true);
					}					
					else 
					{
						ZoneMapType eMapType = zmapInfoGetMapType(pMapInfo);

						if (eMapType == ZMTYPE_MISSION)
						{
							MapMoveStaticEx(pEnt, pDestinationNode->pMissionGroupInfo->pchMapName, 
								pchSpawnPoint, 0, 0, 0, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), NULL, 
								0, MAPSEARCHTYPE_OWNED_MAP, 
								STACK_SPRINTF("progression_tr_SetProgression_CB is transferring Entity[%d] to map because the progression is set manually. (Name=%s)", 
								entGetContainerID(pEnt), pDestinationNode->pMissionGroupInfo->pchMapName), NULL);
						}
						else if (eMapType == ZMTYPE_STATIC)
						{
							MapMoveStaticEx(pEnt, pDestinationNode->pMissionGroupInfo->pchMapName, 
								pchSpawnPoint, 0, 0, 0, 0, 0, NULL, 
								0, MAPSEARCHTYPE_ONE_MAPNAME_ALL_CHOICES, 
								STACK_SPRINTF("progression_tr_SetProgression_CB is transferring Entity[%d] to map because the progression is set manually. (Name=%s)", 
								entGetContainerID(pEnt), pDestinationNode->pMissionGroupInfo->pchMapName), NULL);
						}
						else
						{
							Errorf("Encountered an invalid map type for a game progression node. Game progression node: %s, Map name: %s.", 
								pDestinationNode->pchName, 
								pDestinationNode->pMissionGroupInfo->pchMapName);
						}
					}
				}				
			}
		}

		progression_UpdateCurrentProgression(pEnt);
	}

	StructDestroy(parse_InitiatePlayContentCBParams, pParams);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_SearchByProjectID_ForPlaying_Return(UGCProject *pUGCProject, ContainerID entContainerID)
{
	if(pUGCProject && entContainerID)
	{
		Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
		if(pEnt)
		{
			const UGCProjectVersion *pUGCProjectVersion = UGCProject_GetMostRecentPublishedVersion(pUGCProject);
			if(pUGCProjectVersion)
			{
				char ugcMissionName[RESOURCE_NAME_MAX_SIZE];

				sprintf(ugcMissionName, "%s:Mission", pUGCProjectVersion->pNameSpace);
				pEnt->astrMissionToGrant = (const char *)allocAddString(ugcMissionName);
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE ACMD_NAME(LobbyCommand_InitiatePlayContent);
void gslLobbyCommand_scmdInitiatePlayContent(Entity *pEnt, GameContentNodeRef *pNodeRef)
{
	if (pNodeRef == NULL)
	{
		return;
	}

	// Make sure the destination is set
	if (!IS_HANDLE_ACTIVE(pNodeRef->hNode) && pNodeRef->iUGCProjectID == 0)
	{
		return;
	}

	if (IS_HANDLE_ACTIVE(pNodeRef->hNode))
	{
		// Get the game progression node
		GameProgressionNodeDef *pNode = GET_REF(pNodeRef->hNode);

		if (pNode == NULL || !progression_ProgressionNodeUnlocked(pEnt, pNode))
		{
			return;
		}
	}

	if (pNodeRef->iUGCProjectID) 
	{
		RemoteCommand_Intershard_SearchByProjectID_ForPlaying(ugc_ShardName(), GLOBALTYPE_UGCSEARCHMANAGER, SPECIAL_CONTAINERID_RANDOM,
			pNodeRef->iUGCProjectID, GetShardNameFromShardInfoString(), entGetContainerID(pEnt));
	} 
	else 
	{
		GameProgressionNodeDef* pNodeDef = GET_REF(pNodeRef->hNode);
		GameProgressionNodeDef* pBranchNodeDef = progression_GetStoryBranchNode(pNodeDef);

		if (pBranchNodeDef)
		{
			ProgressionUpdateParams *pUpdateParams = StructCreate(parse_ProgressionUpdateParams);
			InitiatePlayContentCBParams *pParams = StructCreate(parse_InitiatePlayContentCBParams);

			pParams->entContainerID = entGetContainerID(pEnt);
			pParams->pNodeRef = StructCreate(parse_GameContentNodeRef);
			COPY_HANDLE(pParams->pNodeRef->hNode, pNodeRef->hNode);			

			COPY_HANDLE(pUpdateParams->hOverrideNode, pNodeRef->hNode);
			AutoTrans_progression_tr_SetProgression(LoggedTransactions_CreateManagedReturnVal("Lobby", gslLobby_InitiatePlayContentCB, pParams), 
				GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), pUpdateParams);
			StructDestroy(parse_ProgressionUpdateParams, pUpdateParams);
		}
	}
}

#include "AutoGen/gslLobbyCommands_c_ast.c"
