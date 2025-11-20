#include "gslWarp.h"
//#include "gslWarp_h_ast.h"
#include "gslWarp_c_ast.h"

#include "Entity.h"
#include "Entity_h_ast.h"
#include "EntityLib.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "gslMapTransfer.h"
#include "gslPartition.h"
#include "GameStringFormat.h"
#include "GameServerLib.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "itemServer.h"
#include "NotifyCommon.h"
#include "gslSpawnPoint.h"
#include "objContainer.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "rand.h"
#include "ServerLib.h"
#include "StringCache.h"
#include "Team.h"
#include "WorldGrid.h"


#include "autogen/AppServerLib_autogen_remotefuncs.h"
#include "autogen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

//////////////////////////////////////////////////////
// Recruit Warping Code
//////////////////////////////////////////////////////

static void gslWarp_WarpCleanup(Entity *pEnt)
{
	StructDestroySafe(parse_PlayerWarpToData, &pEnt->pPlayer->pWarp);
}

AUTO_STRUCT;
typedef struct WarpMapMoveData
{
	ContainerID iSourceEntID;
	PlayerWarpToData *pWarpData;
	SpawnpointMapMoveData *pSpawnpointData;
} WarpMapMoveData;

AUTO_COMMAND ACMD_NAME("WarpToRecruitReset") ACMD_ACCESSLEVEL(9);
void gslWarp_RecruitReset(Entity *pEnt)
{
	AutoTrans_gslWarp_RecruitCompleteTime(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), 0);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gslWarp_GetWarpData(ContainerID iEntID, ContainerID iCallingGameServerID, EntityRef entref)
{
	PlayerWarpToData *pData = NULL;
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, iEntID);

	if (pEnt && pEnt->pPlayer)
	{
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		const char *pchAllegiance;

		ANALYSIS_ASSUME(pEnt != NULL);

		pData = StructCreate(parse_PlayerWarpToData);
		pData->iAccountID = pEnt->pPlayer->accountID;
		pData->iEntID = entGetContainerID(pEnt);
		pData->eContType = entGetType(pEnt);
		pData->iTeamID = team_GetTeamID(pEnt);
		pData->iInstance = partition_PublicInstanceIndexFromIdx(iPartitionIdx);
		pData->iMapID = gGSLState.gameServerDescription.baseMapDescription.containerID;
		pData->uPartitionID = partition_IDFromIdx(iPartitionIdx);
		pData->pcMapVariables = allocAddString(partition_MapVariablesFromIdx(iPartitionIdx));
		pData->pchMap = StructAllocString(zmapInfoGetPublicName(NULL));

		pchAllegiance = REF_STRING_FROM_HANDLE(pEnt->hAllegiance);
		if (pchAllegiance)
		{
			pData->pchAllegiance = StructAllocString(pchAllegiance);
		}

		entGetPos(pEnt, pData->vecTarget);
	}

	RemoteCommand_gslWarp_SendWarpData(GLOBALTYPE_GAMESERVER, iCallingGameServerID, pData, entref);
	StructDestroy(parse_PlayerWarpToData, pData);
}

static void gslWarp_RequestZoneInfo_CB(TransactionReturnVal *pVal, WarpMapMoveData *pData)
{
	ZoneMapInfoRequest *pRequest = NULL;
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pData->iSourceEntID);
	bool bSuccess = false;

	//Need the same ent, player and recruit warp to continue the process
	if(pEnt && pEnt->pPlayer && pEnt->pPlayer->pWarp)
	{
		switch(RemoteCommandCheck_aslMapManagerRequestZoneMapInfoByPublicName(pVal, &pRequest))
		{
		case TRANSACTION_OUTCOME_FAILURE:
			break;
		case TRANSACTION_OUTCOME_SUCCESS:
			{
				if( !pRequest )
				{
					char *estrMsg = NULL;
					entFormatGameMessageKey(pEnt, &estrMsg, "RecruitWarp_Failed_MapRestriction",
						STRFMT_STRING("Direction", "to"),
						STRFMT_MESSAGEKEY("MapName", "(Unknown Map)"),
						STRFMT_END);
					notify_NotifySend(pEnt, kNotifyType_Failed, estrMsg, NULL, NULL);
					
					estrDestroy(&estrMsg);

					if(pEnt->pPlayer->pWarp->uiItemId)
					{
						Item *pItem = inv_GetItemByID(pEnt, pEnt->pPlayer->pWarp->uiItemId);
						if(pItem)
							gslItem_RollbackWarp(pEnt, pItem);
					}
				}
				else if(pRequest->eMapType == ZMTYPE_PVP 
					|| pRequest->eMapType == ZMTYPE_QUEUED_PVE 
					|| entity_IsWarpRestricted( pData->pSpawnpointData->pchMapName ) )
				{
					char *estrMsg = NULL;
					entFormatGameMessageKey(pEnt, &estrMsg, "RecruitWarp_Failed_MapRestriction",
						STRFMT_STRING("Direction", "to"),
						STRFMT_MESSAGEKEY("MapName", pData->pSpawnpointData->pchMapName),
						STRFMT_END);
					notify_NotifySend(pEnt, kNotifyType_Failed, estrMsg, NULL, NULL);
					
					estrDestroy(&estrMsg);

					if(pEnt->pPlayer->pWarp->uiItemId)
					{
						Item *pItem = inv_GetItemByID(pEnt, pEnt->pPlayer->pWarp->uiItemId);
						if(pItem)
							gslItem_RollbackWarp(pEnt, pItem);
					}
				}

				//Else do the normal mapmove, teleport to the buddy
				else
				{
					PlayerWarpToData *pWarp = pEnt->pPlayer->pWarp;

					pWarp->pchMap = StructAllocString(pData->pSpawnpointData->pchMapName);
					pWarp->iMapID = pData->pSpawnpointData->uMapID;
					pWarp->uPartitionID = pData->pSpawnpointData->uPartitionID;
					pWarp->iEntID = pData->pWarpData->iEntID;
					pWarp->iInstance = pData->pWarpData->iInstance;
					pWarp->iTeamID = pData->pWarpData->iTeamID;

					bSuccess = true;

					//Stamp the time
					pWarp->iTimestamp = timeSecondsSince2000();

					//Move!
					spawnpoint_MovePlayerToMapAndSpawn(	pEnt,
						pWarp->pchMap,
						NULL,
						NULL,
						0,
						0,
						pWarp->iMapID,
						pWarp->uPartitionID,
						pData->pSpawnpointData->eaVars,
						NULL,
						NULL,
						pData->pSpawnpointData->eFlags,
						0);
				}
			}
			break;
		}
	}

	if(!bSuccess)
		gslWarp_WarpCleanup(pEnt);

	StructDestroy(parse_ZoneMapInfoRequest, pRequest);
	StructDestroy(parse_WarpMapMoveData, pData);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gslWarp_SendWarpData(PlayerWarpToData *pData, EntityRef entref)
{
	Entity *pEnt = entFromEntityRefAnyPartition(entref);
	PlayerWarpToData *pWarp = SAFE_MEMBER2(pEnt, pPlayer, pWarp);
	bool bSuccess = false;

	if(pData && pEnt && pEnt->pPlayer && pWarp)
	{
		char *pchMsg = NULL;
		ZoneMapInfo *pZoneInfo = pData ? zmapInfoGetByPublicName(pData->pchMap) : NULL;
		const char *pchAllegiance = REF_STRING_FROM_HANDLE(pEnt->hAllegiance);
		U32 iCurrentTime = timeSecondsSince2000();

		//Don't warp if the target couldn't be found
		if(!pData)
		{
			entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_TargetNotFound", STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);
		}
		//Not on the same team
		else if(pWarp->bMustBeTeamed && !team_IsOnTeamWithID(pEnt, pData->iTeamID))
		{
			entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_NotOnSameTeam", 
				STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);

			if(pWarp->uiItemId)
			{
				Item *pItem = inv_GetItemByID(pEnt, pData->uiItemId);
				if(pItem)
					gslItem_RollbackWarp(pEnt, pItem);
			}
		}
		//Not the same allegiance
		else if(pWarp->bMustBeSameAllegiance && (!pchAllegiance || stricmp(pchAllegiance, pData->pchAllegiance) != 0))
		{
			entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_NotOnSameFaction",
				STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);

			if(pWarp->uiItemId)
			{
				Item *pItem = inv_GetItemByID(pEnt, pData->uiItemId);
				if(pItem)
					gslItem_RollbackWarp(pEnt, pItem);
			}
		}
		//Or the warp is still on cooldown
		else if(pWarp->bRecruitWarp 
			&& (pEnt->pPlayer->uiLastRecruitWarpTime > iCurrentTime || 
				iCurrentTime - pEnt->pPlayer->uiLastRecruitWarpTime < RECRUIT_WARP_TIME))
		{
			entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_Cooldown", 
				STRFMT_INT("Cooldown", pEnt->pPlayer->uiLastRecruitWarpTime > iCurrentTime ? RECRUIT_WARP_TIME : pEnt->pPlayer->uiLastRecruitWarpTime - iCurrentTime),
				STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);

			if(pWarp->uiItemId)
			{
				Item *pItem = inv_GetItemByID(pEnt, pData->uiItemId);
				if(pItem)
					gslItem_RollbackWarp(pEnt, pItem);
			}
		}
		//Move to the player's location?
		else if ((gGSLState.gameServerDescription.baseMapDescription.containerID == pData->iMapID) &&
				 (partition_IDFromIdx(entGetPartitionIdx(pEnt)) == pData->uPartitionID))
		{
			Vec3 vecTargetPos;
			Entity *pTargetEnt = entFromContainerIDAnyPartition(pData->eContType, pData->iEntID);
			if(pTargetEnt)
			{
				entGetPos(pTargetEnt, vecTargetPos);
				vecTargetPos[0] += 0.01f * randomF32();
				vecTargetPos[2] += 0.01f * randomF32();
				entSetPos(pEnt, vecTargetPos, true, "RecruitWarp");
				if(pWarp->bRecruitWarp)
					AutoTrans_gslWarp_RecruitCompleteTime(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), timeSecondsSince2000());
				if(pWarp->uiItemId)
				{
					Item *pItem = inv_GetItemByID(pEnt, pWarp->uiItemId);
					if(pItem)
					{
						gslItem_ChargeForWarp(pEnt, pItem);
					}
				}
			}
			else
			{
				entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_TargetNotFound", STRFMT_END);
				notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);
				if(pWarp->uiItemId)
				{
					Item *pItem = inv_GetItemByID(pEnt, pData->uiItemId);
					if(pItem)
						gslItem_RollbackWarp(pEnt, pItem);
				}
			}
		}
		//If this map is name spaced...
		else if(!pZoneInfo)
		{
			// If the ZoneMapInfo is not available, request a copy from the map manager before proceeding
			TransactionReturnVal* pReturn = NULL;
			WorldVariable **eaMapVars = NULL;
			WarpMapMoveData* pCBData = StructCreate(parse_WarpMapMoveData);

			pCBData->pWarpData = StructClone(parse_PlayerWarpToData, pData);
			pCBData->iSourceEntID = entGetContainerID(pEnt);

			worldVariableStringToArray(pEnt->pPlayer->pWarp->pcMapVariables, &eaMapVars);
			
			//Consider this success for now
			bSuccess = true;
			
			pCBData->pSpawnpointData = spawnpoint_GetMapMoveData(pEnt, pData->pchMap, NULL,
				NULL, 0, 0, pData->iMapID, pData->uPartitionID, eaMapVars, NULL, NULL,TRANSFERFLAG_RECRUITWARP, false);
			
			pReturn = objCreateManagedReturnVal(gslWarp_RequestZoneInfo_CB, pCBData);
			RemoteCommand_aslMapManagerRequestZoneMapInfoByPublicName(pReturn, GLOBALTYPE_MAPMANAGER, 0, pData->pchMap);

			eaDestroyStruct(&eaMapVars, parse_WorldVariable);
		}
		//Or they're on the wrong map type
		else if(zmapInfoGetMapType(pZoneInfo) == ZMTYPE_PVP ||
			zmapInfoGetMapType(pZoneInfo) == ZMTYPE_QUEUED_PVE || 
			entity_IsWarpRestricted(zmapInfoGetPublicName(pZoneInfo)) )
			//TODO(BH): Owned maps?  Should owned nemesis maps be teleportable?
		{
			entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_MapRestriction",
				STRFMT_STRING("Direction", "to"),
				STRFMT_MESSAGEKEY("MapName", zmapInfoGetDisplayNameMsgKey(pZoneInfo)),
				STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);
			if(pWarp->uiItemId)
			{
				Item *pItem = inv_GetItemByID(pEnt, pData->uiItemId);
				if(pItem)
					gslItem_RollbackWarp(pEnt, pItem);
			}
		}

		//Else do the normal mapmove, teleport to the buddy
		else
		{
			WorldVariable **eaMapVars = NULL;

			pWarp->pchMap = StructAllocString(pData->pchMap);
			pWarp->iMapID = pData->iMapID;
			pWarp->uPartitionID = pData->uPartitionID;
			pWarp->iEntID = pData->iEntID;
			pWarp->iInstance = pData->iInstance;
			pWarp->iTeamID = pData->iTeamID;
			copyVec3(pData->vecTarget, pWarp->vecTarget);
			//Stamp the time
			pWarp->iTimestamp = timeSecondsSince2000();
			//Make some vars to use for the map move code
			worldVariableStringToArray(pWarp->pcMapVariables, &eaMapVars);


			bSuccess = true;
			//Move!
			spawnpoint_MovePlayerToMapAndSpawn(	pEnt,
				pWarp->pchMap,
				pWarp->pchSpawn,
				NULL,
				0,
				0,
				pWarp->iMapID,
				pWarp->uPartitionID,
				eaMapVars,
				NULL,
				NULL,
				pWarp->bRecruitWarp ? TRANSFERFLAG_RECRUITWARP : 0,
				0);

			eaDestroyStruct(&eaMapVars, parse_WorldVariable);
		}

		if(!bSuccess)
			gslWarp_WarpCleanup(pEnt);
		
		estrDestroy(&pchMsg);
	}
}

void WarpToTarget(Entity *pEnt)
{
	PlayerWarpToData *pWarp = SAFE_MEMBER2(pEnt,pPlayer,pWarp);
	Entity *pTargetEnt = NULL;
	bool bKeepWarpData = false;
	
	if(!pWarp)
		return;
	
	pTargetEnt = entFromContainerIDAnyPartition(pWarp->eContType, pWarp->iEntID);
	if(!pTargetEnt)
	{
		//So it doesn't delete the warp data at the end of this function
		bKeepWarpData = true;
		//They're not on this map, find out where they are
		RemoteCommand_gslWarp_GetWarpData(pWarp->eContType, pWarp->iEntID, pWarp->iEntID, GetAppGlobalID(), entGetRef(pEnt));
	}
	//Cannot warp because not on the same team
	else if(pWarp->bMustBeTeamed && !team_OnSameTeam(pEnt, pTargetEnt))
	{
		char *pchMsg = NULL;
		entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_NotOnSameTeam", 
			STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);
		estrDestroy(&pchMsg);

		if(pWarp->uiItemId)
		{
			Item *pItem = inv_GetItemByID(pEnt, pWarp->uiItemId);
			if(pItem)
				gslItem_RollbackWarp(pEnt, pItem);
		}
	}
	//Cannot warp because not of the same allegiance
	else if(pWarp->bMustBeSameAllegiance)
	{
		const char *pchAllegiance = REF_STRING_FROM_HANDLE(pEnt->hAllegiance);
		const char *pchTargetAllegiance = REF_STRING_FROM_HANDLE(pTargetEnt->hAllegiance);

		if (!pchAllegiance || !pchTargetAllegiance || stricmp(pchAllegiance, pchTargetAllegiance) != 0)
		{
			char *pchMsg = NULL;
			entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_NotOnSameFaction",
				STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);
			estrDestroy(&pchMsg);

			if(pWarp->uiItemId)
			{
				Item *pItem = inv_GetItemByID(pEnt, pWarp->uiItemId);
				if(pItem)
					gslItem_RollbackWarp(pEnt, pItem);
			}
		}
	}
	//Hey, they're already on the map
	else 
	{
		Vec3 vecTargetPos;
		entGetPos(pTargetEnt, vecTargetPos);
		vecTargetPos[0] += 0.01f * randomF32();
		vecTargetPos[2] += 0.01f * randomF32();
		entSetPos(pEnt, vecTargetPos, true, "WarpToTarget");
		
		if(SAFE_MEMBER3(pEnt, pPlayer, pWarp, bRecruitWarp))
			AutoTrans_gslWarp_RecruitCompleteTime(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), timeSecondsSince2000());

		if(pWarp->uiItemId)
		{
			Item *pItem = inv_GetItemByID(pEnt, pWarp->uiItemId);
			if(pItem)
			{
				gslItem_ChargeForWarp(pEnt, pItem);
			}
		}
	}

	if(!bKeepWarpData)
		gslWarp_WarpCleanup(pEnt);
}

void gslWarp_WarpToLocation(Entity *pEnt, PlayerWarpToData *pData)
{
	assert(pEnt->pPlayer);

	if( zmapInfoGetMapType(NULL) == ZMTYPE_PVP || entity_IsWarpRestricted(zmapInfoGetPublicName(NULL)))
	{
		char *estrMsg = NULL;
		entFormatGameMessageKey(pEnt, &estrMsg, "RecruitWarp_Failed_MapRestriction",
			STRFMT_STRING("Direction", "from"),
			STRFMT_MESSAGEKEY("MapName", zmapInfoGetDisplayNameMsgKey(NULL)),
			STRFMT_END);
		notify_NotifySend(pEnt, kNotifyType_Failed, estrMsg, NULL, NULL);
		estrDestroy(&estrMsg);

		if(pData->uiItemId)
		{
			Item *pItem = inv_GetItemByID(pEnt, pData->uiItemId);
			if(pItem)
				gslItem_RollbackWarp(pEnt, pItem);
		}
	}
	else if ((pData->iMapID != gGSLState.gameServerDescription.baseMapDescription.containerID) ||
			 (pData->uPartitionID != partition_IDFromIdx(entGetPartitionIdx(pEnt))))
	{
		//No confirm, vavoom!
		WorldVariable **eaMapVars = NULL;

		//Copy the new data
		pEnt->pPlayer->pWarp = StructClone(parse_PlayerWarpToData, pData);
		ANALYSIS_ASSUME(pEnt->pPlayer->pWarp);
		//Stamp the time
		pEnt->pPlayer->pWarp->iTimestamp = timeSecondsSince2000();
		//Make some vars to use for the map move code
		worldVariableStringToArray(pEnt->pPlayer->pWarp->pcMapVariables, &eaMapVars);

		//Move!
		spawnpoint_MovePlayerToMapAndSpawn(	pEnt,
			pEnt->pPlayer->pWarp->pchMap,
			pEnt->pPlayer->pWarp->pchSpawn,
			NULL,
			0,
			0,
			pEnt->pPlayer->pWarp->iMapID,
			pEnt->pPlayer->pWarp->uPartitionID,
			eaMapVars,
			NULL,
			NULL,
			pEnt->pPlayer->pWarp->bRecruitWarp ? TRANSFERFLAG_RECRUITWARP : 0,
			0);

		eaDestroyStruct(&eaMapVars, parse_WorldVariable);
	}
	else
	{
		if(!pData->pchSpawn)
		{
			Quat spawnRot;
			entGetRot(pEnt, spawnRot);
			spawnpoint_MovePlayerToLocation(pEnt, pData->vecTarget, spawnRot, NULL, true );
		}
		else
		{
			spawnpoint_MovePlayerToNamedSpawn(pEnt, pData->pchSpawn, NULL, 0);
		}

		if(pData->uiItemId)
		{
			Item *pItem = inv_GetItemByID(pEnt, pData->uiItemId);
			if(pItem)
			{
				gslItem_ChargeForWarp(pEnt, pItem);
			}
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_NAME(gslWarp_WarpToLocation);
void gslWarp_cmd_WarpToLocation(ContainerID iEntID, PlayerWarpToData *pData, char *pcRequestingCharName, char *pchMapMsgKey, U32 uiTimeToConfirm)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER,iEntID);
	if(pEnt && pEnt->pPlayer)
	{
		ANALYSIS_ASSUME(pEnt != NULL);
		if(!pEnt->pPlayer->pWarp && !pEnt->pPlayer->pMapMoveConfirm)
		{
			if(uiTimeToConfirm)
			{
				PlayerMapMoveConfirm *pConfirm = StructCreate(parse_PlayerMapMoveConfirm);
				pConfirm->eType = kPlayerMapMove_Warp;
				pConfirm->pcMapName = StructAllocString(pData->pchMap);
				pConfirm->pcNamedSpawnPoint = StructAllocString(pData->pchSpawn);
				worldVariableStringToArray(pData->pcMapVariables, &pConfirm->eaVariables);
				pConfirm->eFlags = pData->bRecruitWarp ? TRANSFERFLAG_RECRUITWARP : 0;

				pConfirm->pWarp = StructClone(parse_PlayerWarpToData, pData);
				ANALYSIS_ASSUME(pConfirm->pWarp);
				pConfirm->pWarp->iTimestamp = timeSecondsSince2000();

				pConfirm->uiTimeStart = timeSecondsSince2000();
				pConfirm->uiTimeToConfirm = uiTimeToConfirm;

				pEnt->pPlayer->pMapMoveConfirm = pConfirm;

				//Client confirm and boosh!
				{
					//Send the confirmation information to the client
					PlayerMapMoveClient *pClientConfirm = StructCreate(parse_PlayerMapMoveClient);
					pClientConfirm->eType = kPlayerMapMove_Warp;

					if(pcRequestingCharName)
						pClientConfirm->pchRequestingEnt = StructAllocString(pcRequestingCharName);

					if(pchMapMsgKey)
						SET_HANDLE_FROM_STRING(gMessageDict,pchMapMsgKey, pClientConfirm->hDisplayName);

					pClientConfirm->uiTimeStart = timeSecondsSince2000();
					pClientConfirm->uiTimeToConfirm = uiTimeToConfirm;

					// Waiting on confirmation of some sort, send the request down
					ClientCmd_RequestMapMoveConfirm(pEnt, pClientConfirm);

					StructDestroy(parse_PlayerMapMoveClient, pClientConfirm);
				}
			}
			else
			{
				gslWarp_WarpToLocation(pEnt, pData);
			}
		}
		else
		{
			//This person tried to warp you but you were already involved in warping or confirming something
		}
	}
}

static void WarpToTarget_CB(U32 iTargetEntID, void *pvUserData)
{
	EntityRef *pRef = (EntityRef*)pvUserData;
	Entity *pEnt = pRef ? entFromEntityRefAnyPartition(*pRef) : NULL;

	if(pEnt && pEnt->pPlayer)
	{
		if(iTargetEntID)
		{
			pEnt->pPlayer->pWarp->iEntID = iTargetEntID;
			pEnt->pPlayer->pWarp->eContType = GLOBALTYPE_ENTITYPLAYER;
			WarpToTarget(pEnt);
		}
		else
		{
			char *pchMsg = NULL;
			entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_TargetNotFound", STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);
			
			gslWarp_WarpCleanup(pEnt);

			estrDestroy(&pchMsg);
		}
	}

	SAFE_FREE(pRef);
}

void gslWarp_WarpToTarget_ChargeItem(Entity *pEnt, U32 iTargetEntID, U64 uiItemId)
{
	if(pEnt && pEnt->pPlayer)
	{
		U32 iCurrentTime = timeSecondsSince2000();
		char *pchMsg = NULL;
		//Cannot warp with a previous pending warp
		if(pEnt->pPlayer->pWarp)
		{
			entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_PendingWarp", STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);
		}
		//Cannot warp from PVP
		else if( zmapInfoGetMapType(NULL) == ZMTYPE_PVP || entity_IsWarpRestricted(zmapInfoGetPublicName(NULL)))
		{
			entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_MapRestriction",
				STRFMT_STRING("Direction", "from"),
				STRFMT_MESSAGEKEY("MapName", zmapInfoGetDisplayNameMsgKey(NULL)),
				STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);
		}
		else
		{
			pEnt->pPlayer->pWarp = StructCreate(parse_PlayerWarpToData);
			pEnt->pPlayer->pWarp->iEntID = iTargetEntID;
			pEnt->pPlayer->pWarp->eContType = GLOBALTYPE_ENTITYPLAYER;
			pEnt->pPlayer->pWarp->uiItemId = uiItemId;
			pEnt->pPlayer->pWarp->iTimestamp = timeSecondsSince2000();
			pEnt->pPlayer->pWarp->bMustBeTeamed = true;
			pEnt->pPlayer->pWarp->bMustBeSameAllegiance = true;
			WarpToTarget(pEnt);
		}

		estrDestroy(&pchMsg);
	}
}

AUTO_COMMAND ACMD_NAME("WarpToRecruit") ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRODUCTS(StarTrek, FightClub);
void gslWarp_WarpToRecruit(Entity *pEnt, U32 iAccountID)
{
	const GameAccountData *pData = entity_GetGameAccount(pEnt);
	if(pEnt && pEnt->pPlayer && pData)
	{
		U32 iCurrentTime = timeSecondsSince2000();
		char *pchMsg = NULL;
		//Cannot warp with a previous pending warp
		if(pEnt->pPlayer->pWarp)
		{
			entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_PendingWarp", STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);
		}
		// must be a team member
		else if(!team_IsMember(pEnt))
		{
			entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_NotOnSameTeam", 
				STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);
		}
		//Cannot warp from PVP
		else if( zmapInfoGetMapType(NULL) == ZMTYPE_PVP || entity_IsWarpRestricted(zmapInfoGetPublicName(NULL)))
		{
			entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_MapRestriction",
				STRFMT_STRING("Direction", "from"),
				STRFMT_MESSAGEKEY("MapName", zmapInfoGetDisplayNameMsgKey(NULL)),
				STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);
		}
		//Cannot warp while it's on cooldown
		else if(pEnt->pPlayer->uiLastRecruitWarpTime > iCurrentTime || 
			iCurrentTime - pEnt->pPlayer->uiLastRecruitWarpTime < RECRUIT_WARP_TIME)
		{
			entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_Cooldown", 
				STRFMT_INT("Cooldown", pEnt->pPlayer->uiLastRecruitWarpTime > iCurrentTime ? RECRUIT_WARP_TIME : pEnt->pPlayer->uiLastRecruitWarpTime - iCurrentTime),
				STRFMT_END);
			notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);
		}
		//Else this is looking good, make sure they're a recruit
		else
		{
			S32 iRecruitIdx, iRecruiterIdx;
			for(iRecruitIdx = eaSize(&pData->eaRecruits)-1; iRecruitIdx >= 0; iRecruitIdx--)
			{
				if(pData->eaRecruits[iRecruitIdx]->uAccountID == iAccountID)
					break;
			}
			for(iRecruiterIdx = eaSize(&pData->eaRecruiters)-1; iRecruiterIdx >= 0; iRecruiterIdx--)
			{
				if(pData->eaRecruiters[iRecruiterIdx]->uAccountID == iAccountID)
					break;
			}
			if(iRecruitIdx >= 0 || iRecruiterIdx >= 0)
			{
				//They are a recruit, find out which character they're on
				EntityRef *pRef = calloc(1, sizeof(EntityRef));
				*pRef = entGetRef(pEnt);
				pEnt->pPlayer->pWarp = StructCreate(parse_PlayerWarpToData);
				pEnt->pPlayer->pWarp->iAccountID = iAccountID;
				pEnt->pPlayer->pWarp->bRecruitWarp = true;
				pEnt->pPlayer->pWarp->bMustBeTeamed = true;
				pEnt->pPlayer->pWarp->bMustBeSameAllegiance = true;
				RequestOnlineCharacterIDFromAccountID(iAccountID, WarpToTarget_CB, pRef);
			}
			//Oops, they're not a recruit or recruiter, error!
			else
			{
				entFormatGameMessageKey(pEnt, &pchMsg, "RecruitWarp_Failed_NotRecruit", STRFMT_END);
				notify_NotifySend(pEnt, kNotifyType_Failed, pchMsg, NULL, NULL);
			}
		}

		estrDestroy(&pchMsg);
	}
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Uilastrecruitwarptime");
enumTransactionOutcome gslWarp_RecruitCompleteTime(ATR_ARGS, NOCONST(Entity) *pEnt, U32 iTimestamp)
{
	if(NONNULL(pEnt) &&
		NONNULL(pEnt->pPlayer))
	{
		pEnt->pPlayer->uiLastRecruitWarpTime = iTimestamp;
	}
	return(TRANSACTION_OUTCOME_SUCCESS);
}

#include "gslWarp_c_ast.c"
