//stuff relating to the actual getting of the server address, along with launching new servers etc
#include "aslUGCDataManager.h"
#include "aslMapManagerNewMapTransfer_InnerStructs.h"
#include "textParser.h"
#include "AslMapManagerNewMapTransfer_Private.h"
#include "textparserUtils.h"
#include "textParserEnums.h"
#include "mapDescription.h"
#include "AslMapManager.h"
#include "stashTable.h"
#include "aslMapManagerNewMapTransfer_Private_h_ast.h"
#include "resourceInfo.h"
#include "logging.h"
#include "staticworld/worldGridPrivate.h"
#include "mapdescription_h_ast.h"
#include "aslMapManagerConfig_h_ast.h"
#include "stringCache.h"
#include "alerts.h"
#include "autogen/controller_autogen_remotefuncs.h"
#include "../../core/controller/pub/controllerpub.h"
#include "aslMapManagerNewMapTransfer.h"
#include "autogen/gameserverlib_autogen_remotefuncs.h"
#include "autogen/appserverlib_autogen_remotefuncs.h"
#include "autogen/serverlib_autogen_remotefuncs.h"
#include "autogen/AppServerLib_autogen_slowfuncs.h"
#include "stringUtil.h"
#include "aslMapManager.h"
#include "ugcprojectCommon.h"
#include "aslUGCDataManagerProject.h"
#include "UGCCommon.h"
#include "aslMapManagerNewMapTransfer_GetAddress.h"
#include "UGCProjectUtils.h"
#include "aslMapManagerNewMapTransfer_GetAddress_c_ast.h"

static bool sbPrintRecentTeammateChecks = false;
AUTO_CMD_INT(sbPrintRecentTeammateChecks, sbPrintRecentTeammateChecks);

//when a gameserver chooses between multiple maps to go to with the same name, presumably going to a static map and choosing
//the one with the most teammates or something, it sets a flag saying "if one of my teammates went to one of these maps
//in the last n seconds, go to that one instead. This is the cutoff length here. Sufficiently far in the past, and the
//team structure should get updated so we now know where the guy is
#define RECENT_TEAM_LOGIN_CUTOFF_TO_OVERRIDE_CHOICE (10.0f)

//if a partition is requested by something other than a player, override the map inactivity timeout to this value
#define OVERRIDE_MAP_INACTIVITY_TIMEOUT_MINUTES 5


static bool sbTestAlwaysAllowMorePartitionsOnAServer = false;
AUTO_CMD_INT(sbTestAlwaysAllowMorePartitionsOnAServer, TestAlwaysAllowMorePartitionsOnAServer) ACMD_CATEGORY(debug) ACMD_COMMANDLINE;

void NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(SlowRemoteCommandID iCmdID, FORMAT_STR const char *pFmt, ...)
{
	ReturnedGameServerAddress retVal = {0};
	char *pTemp = NULL;
	estrGetVarArgs(&pTemp, pFmt);
	ANALYSIS_ASSUME(pTemp != NULL); // 99% sure this is correct
	strcpy_trunc(retVal.errorString, pTemp);

	log_printf(LOG_LOGIN, "RNOE %u: RequestNewOrExistGameServerAddress_Fail: %s", iCmdID, pTemp);

	estrDestroy(&pTemp);
	SlowRemoteCommandReturn_RequestNewOrExistingGameServerAddress(iCmdID, &retVal);
}

void NewMapTransfer_SendPlayerToServerNow(TrackedGameServerExe *pServer, PendingRequestCache *pCache)
{
	static U32 sNextCacheID = 1;
	MapPartitionSummary *pPartition = GetPartitionFromServerAndID(pServer, pCache->uPartitionID);
	PendingRequestCache *pOldCache = NULL;

	if (!pPartition)
	{
		NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(pCache->iCmdID, "Partition vanished before login could complete");
		StructDestroy(parse_PendingRequestCache, pCache);
		return;
	}

	if (pCache->iTeamID)
	{
		NewMapTransfer_TeamBeingSentToPartitionNow(pPartition, pCache->iTeamID);
	}

	pPartition->iLastTimeSendPlayerThere = timeSecondsSince2000();

	//some chance that a previous transfer got aborted, or something along those lines
	if (stashIntRemovePointer(sPlayersBeingSentByID, pCache->iEntContainerID, &pOldCache))
	{
		NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(pOldCache->iCmdID, "Transfer rendered obsolete by newer transfer with same character");
		StructDestroy(parse_PendingRequestCache, pOldCache);
	}
		
	pCache->iUniqueCacheID = sNextCacheID++;
	if (!sNextCacheID)
	{
		sNextCacheID++;
	}

	pCache->iGameServerID = pServer->iContainerID;
	
	log_printf(LOG_LOGIN, "RNOE %u: Sending player to server %u partition %u now", 
		pCache->iCmdID, pServer->iContainerID, pCache->uPartitionID);
	
	RemoteCommand_HereIsPartitionInfoForUpcomingMapTransfer(GLOBALTYPE_GAMESERVER, pServer->iContainerID, pCache->iUniqueCacheID, 
		pCache->iEntContainerID, pCache->uPartitionID, pPartition);
	pServer->iNumPlayersRecentlyLoggingIn++;
	pPartition->iNumPlayersRecentlyLoggingIn++;

	stashIntAddPointer(sPlayersBeingSentByID, pCache->iEntContainerID, pCache, false);	
}

void NewMapTransfer_SendReturnAddressNow(TrackedGameServerExe *pServer, PendingRequestCache *pCache)
{
	// If the request didn't come from a player, just return the GameServerAddress with the partition ID
	ReturnedGameServerAddress retVal = {0};	
	retVal.iContainerID = pServer->iContainerID;
	retVal.uPartitionID = pCache->uPartitionID;
	retVal.iInstanceIndex = pCache->iInstanceIndex;

	if (pServer->pMachine)
	{
		retVal.iIPs[0] = pServer->pMachine->iIP;
		retVal.iIPs[1] = pServer->pMachine->iPublicIP;
	}

	log_printf(LOG_LOGIN, "RNOE %u: No player, returning address of server %u partition %u", 
		pCache->iCmdID, pServer->iContainerID, pCache->uPartitionID);

	SlowRemoteCommandReturn_RequestNewOrExistingGameServerAddress(pCache->iCmdID, &retVal);
	
	// Override the map inactivity timeout
	RemoteCommand_SetServerInactivityTimeoutMinutes(GLOBALTYPE_GAMESERVER, 
													pServer->iContainerID, 
													OVERRIDE_MAP_INACTIVITY_TIMEOUT_MINUTES);
	StructDestroy(parse_PendingRequestCache, pCache);
}

AUTO_COMMAND_REMOTE;
void PartitionInfoReceived(ContainerID iPlayerID, U32 iCacheID)
{
	PendingRequestCache *pCache;
	TrackedGameServerExe *pServer;
	ReturnedGameServerAddress retVal = {0};



	if (!stashIntRemovePointer(sPlayersBeingSentByID, iPlayerID, &pCache))
	{
		//something weird happened with a transfer taking forever and a guy cancelling out of his first transfer or something
		return;
	}

	if (pCache->iUniqueCacheID != iCacheID)
	{
		//weird corner case if a player requests a transfer then requests another transfer while the first one is going,
		//then the partition info for the first one comes back.
		//partition info we received is obsolete, we've already failed its transfer in NewMapTransfer_SendPlayerToServerNow,
		//put the cache back into the stash table
		stashIntAddPointer(sPlayersBeingSentByID, iPlayerID, pCache, false);
		return;
	}

	if (!stashIntFindPointer(sGameServerExesByID, pCache->iGameServerID, &pServer))
	{
		//gameserver has gone away
		NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(pCache->iCmdID, "GS died during cache handshaking");
		StructDestroy(parse_PendingRequestCache, pCache);
		return;
	}

	//	LogPlayerWasSentToMap(pMap, pCache->iEntContainerID);

	retVal.iContainerID = pCache->iGameServerID;
	retVal.iIPs[0] = pServer->pMachine->iIP;
	retVal.iIPs[1] = pServer->pMachine->iPublicIP;
	retVal.iPortNum = pServer->description.iListeningPortNum;
	retVal.uPartitionID = pCache->uPartitionID;
	retVal.iInstanceIndex = pCache->iInstanceIndex;

	log_printf(LOG_LOGIN, "RNOE %u: Got handshake back from GS, sending address to player now",
		pCache->iCmdID);

	SlowRemoteCommandReturn_RequestNewOrExistingGameServerAddress(pCache->iCmdID, &retVal);

	StructDestroy(parse_PendingRequestCache, pCache);
}

typedef enum enumSendPlayerFlags
{
	SENDPLAYERFLAGS_ALREADYDIDNOTIFICATION = 1 << 0,
} enumSendPlayerFlags;

static void NewMapTransfer_SendWaitingNotificationNow(TrackedGameServerExe *pServer, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo)
{
	char *pTempString = NULL;

	estrStackCreate(&pTempString);
	estrPrintf(&pTempString, "You are WAITING for gameserver %u on machine %s, pid %u",
		pServer->iContainerID, NewMapTransfer_GetDebugTransferNotificationLinkString(pServer), pServer->pid);

	RemoteCommand_SendDebugTransferMessage(pRequesterInfo->eRequestingServerType, pRequesterInfo->iRequestingServerID, pRequesterInfo->iPlayerIdentificationCookie, pTempString);
	estrDestroy(&pTempString);
}

static void NewMapTransfer_SendWaitingNotificationWhenReady(TrackedGameServerExe *pServer, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo)
{
	if (pServer->pMachine && pServer->pid)
	{
		NewMapTransfer_SendWaitingNotificationNow(pServer, pRequesterInfo);
	}
	else
	{
		eaPush(&pServer->pPendingDebugTransferNotifications, StructClone(parse_NewOrExistingGameServerAddressRequesterInfo, pRequesterInfo));
	}
}

void NewMapTransfer_MaybeFulfillPendingDebugTransferNotifications(TrackedGameServerExe *pServer)
{
	int i;

	if (pServer->pMachine && pServer->pid)
	{
		for (i=0; i < eaSize(&pServer->pPendingDebugTransferNotifications); i++)
		{
			NewMapTransfer_SendWaitingNotificationNow(pServer, pServer->pPendingDebugTransferNotifications[i]);
		}

		eaDestroyStruct(&pServer->pPendingDebugTransferNotifications, parse_NewOrExistingGameServerAddressRequesterInfo);
	}
}

static void ReturnAddressOrSendPlayerToServerWhenReady(TrackedGameServerExe *pServer, MapPartitionSummary *pPartition, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, SlowRemoteCommandID iCmdID, enumSendPlayerFlags eFlags)
{
	PendingRequestCache *pCache = StructCreate(parse_PendingRequestCache);
	pCache->iCmdID = iCmdID;
	pCache->iEntContainerID = pRequesterInfo->iEntContainerID;
	pCache->uPartitionID = pPartition->uPartitionID;
	pCache->iInstanceIndex = pPartition->iPublicIndex;
	pCache->iTeamID = pRequesterInfo->iRequestingTeamID;

	NewMapTransfer_LogPlayerWasSentToMap(pServer, pRequesterInfo->iEntContainerID);

	log_printf(LOG_LOGIN, "RNOE %u: Found server/partition %u/%u, going to send player when ready (current state: %s)", iCmdID, pServer->iContainerID, 
		pPartition->uPartitionID, StaticDefineIntRevLookup(GameServerExeStateEnum, pServer->eState));

	if (!pRequesterInfo->iEntContainerID && pRequesterInfo->eRequestingServerType != GLOBALTYPE_QUEUESERVER)
	{
		servLogWithStruct(LOG_BUG, "BadRequesterInfo", pRequesterInfo, parse_NewOrExistingGameServerAddressRequesterInfo);
		AssertOrAlert("BAD_REQUESTER_INFO", "A server other than a queue server seems to have issued a newOrExisting address request with no ent container ID (presumably causing STO-29274)");
	}

	switch (pServer->eState)
	{
	case GSSTATE_LAUNCH_REQUESTED_FROM_CONTROLLER:
	case GSSTATE_PREEXISTING_WAITING_FOR_DESCRIPTION:
	case GSSTATE_SPAWNED_WAITING_FOR_HANDSHAKE:
	case GSSTATE_LOADING:
		{
			if (gbDebugTransferNotifications && !(eFlags & SENDPLAYERFLAGS_ALREADYDIDNOTIFICATION))
			{
				if (pRequesterInfo->iEntContainerID)
				{
					NewMapTransfer_SendWaitingNotificationWhenReady(pServer, pRequesterInfo);
				}
			}

		
			pPartition->iNumPendingRequests++;
			eaPush(&pServer->ppPendingRequests, pCache);

			RemoteCommand_AddressReturnWillBeSlow(pRequesterInfo->eRequestingServerType, pRequesterInfo->iRequestingServerID, pRequesterInfo->iPlayerIdentificationCookie);
		}
		break;

	case GSSTATE_RUNNING:
		if (gbDebugTransferNotifications && pRequesterInfo->iEntContainerID)
		{	
			char *pTempString = NULL;

			estrStackCreate(&pTempString);
			estrPrintf(&pTempString, "You are being transferred to gameserver %u on machine %s, pid %u",
				pServer->iContainerID, NewMapTransfer_GetDebugTransferNotificationLinkString(pServer), pServer->pid);

			RemoteCommand_SendDebugTransferMessage(pRequesterInfo->eRequestingServerType, pRequesterInfo->iRequestingServerID, pRequesterInfo->iPlayerIdentificationCookie, pTempString);
			estrDestroy(&pTempString);
		}

		if (pRequesterInfo->iEntContainerID)
		{
			NewMapTransfer_SendPlayerToServerNow(pServer, pCache);
		}
		else
		{
			NewMapTransfer_SendReturnAddressNow(pServer, pCache);
		}
		break;

	default:
		StructDestroy(parse_PendingRequestCache, pCache);
		NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "Unknown GS state: %s", StaticDefineIntRevLookup(GameServerExeStateEnum, pServer->eState));
		break;
	}
}

bool NewMapTransfer_ServerHasRoomForNewPartition(TrackedGameServerExe *pServer)
{
	MapCategoryConfig *pConfig;

	TrackedGameServerExe **ppPossibilities = NULL;

	if (sbTestAlwaysAllowMorePartitionsOnAServer)
	{
		return true;
	}

	pConfig = pServer->pList->pCategoryConfig;
	if (!pConfig)
	{
		pConfig = &gFallbackDefaultConfig;
	}


	if (eaSize(&pServer->description.ppPartitions) >= pConfig->iMaxPartitions)
	{
		return false;
	}

	return true;
}

static TrackedGameServerExe *FindServerWithRoomForNewPartition(GameServerList *pList)
{
	int i;


	for (i=0; i < eaSize(&pList->ppGameServers); i++)
	{
		TrackedGameServerExe *pServer = pList->ppGameServers[i];

		if (NewMapTransfer_ServerHasRoomForNewPartition(pServer))
		{
			return pServer;
		}
	}

	return NULL;
}

static char *GetPlayerString(NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo)
{
	static char *spRetVal = NULL;
	estrPrintf(&spRetVal, "%s@%s(%u)", pRequesterInfo->pPlayerName, pRequesterInfo->pPlayerAccountName, pRequesterInfo->iEntContainerID);
	return spRetVal;
}

static TrackedGameServerExe *FindAndPreparePreLoadServer(GameServerList *pList)
{	
	MapCategoryConfig *pCategory;
	int i;

	if (!pList->pSpecifiedCategoryName)
	{
		return NULL;
	}

	pCategory = FindCategoryByName(pList->pSpecifiedCategoryName);
	if (!pCategory)
	{
		return NULL;
	}

	if (!ea32Size(&pCategory->pPreloadMapContainerIDs))
	{
		return NULL;
	}

	for (i=ea32Size(&pCategory->pPreloadMapContainerIDs) - 1; i >= 0; i--)
	{
		TrackedGameServerExe *pServer = GetGameServerFromID(pCategory->pPreloadMapContainerIDs[i]);
		if (pServer && pServer->eState == GSSTATE_PRELOAD)
		{
			if (!pServer->iPendingGameServerDescriptionRequest)
			{
				AssertOrAlert("CORRUPT_PRELOAD_MAP", "A map ended up in the preload list without a pending description request");
				RemoteCommand_KillServer(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_GAMESERVER, pServer->iContainerID, "Preload corruption");
				RemoveAndDestroyGameServerExe(pServer, "Preload corruption");
				return NULL;
			}
			else
			{
				//this is no longer a preload map, so take it out of all preload lists
				ea32RemoveFast(&pCategory->pPreloadMapContainerIDs, i);
				NewMapTransfer_RemoveGameServerFromList(pServer);

				//start creating a new preload list to replace it
				NewMapTransfer_CreatePreloadMap(pCategory);

				pServer->description.eServerType = GSTYPE_NORMAL;
				pServer->description.eMapType = GetListZoneMapType(pList);
				pServer->description.pMapDescription = pList->pMapDescription;

				NewMapTransfer_SendDescriptionToGameServer(pServer, pServer->iPendingGameServerDescriptionRequest);
				pServer->iPendingGameServerDescriptionRequest = 0;
				pServer->eState = GSSTATE_LOADING;

				NewMapTransfer_AddGameServerToList(pServer);

				return pServer;
			}
		}
	}

	return NULL;
}
	


static void CreateNewPartitionInListAndSendPlayerWhenReady(GameServerList *pList, PossibleMapChoice *pRequest, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, SlowRemoteCommandID iCmdID,
	FORMAT_STR const char *pReasonFmt, ...)
{
	MapPartitionSummary *pPartition;
	TrackedGameServerExe *pServer;

	char *pReason = NULL;

	estrGetVarArgs(&pReason, pReasonFmt);

	pServer = FindServerWithRoomForNewPartition(pList);
	if (pServer)
	{
		log_printf(LOG_LOGIN, "RNOE %u: Need a new partition because %s, found a server with space", iCmdID, pReason);

		pPartition = AddPartitionSummaryInternal(pServer, pRequest->baseMapDescription.mapVariables, pRequest->baseMapDescription.ownerType, pRequest->baseMapDescription.ownerID, pRequest->baseMapDescription.iVirtualShardID);
		ReturnAddressOrSendPlayerToServerWhenReady(pServer, pPartition, pRequesterInfo, iCmdID, 0);
	}
	else if (!pRequest->patchInfo && (pServer = FindAndPreparePreLoadServer(pList)))
	{
		log_printf(LOG_LOGIN, "RNOE %u: Need a new partition because %s, found a preload server", iCmdID, pReason);

		pPartition = AddPartitionSummaryInternal(pServer, pRequest->baseMapDescription.mapVariables, pRequest->baseMapDescription.ownerType, pRequest->baseMapDescription.ownerID, pRequest->baseMapDescription.iVirtualShardID);
		ReturnAddressOrSendPlayerToServerWhenReady(pServer, pPartition, pRequesterInfo, iCmdID, 0);
	}
	else
	{
		GameServerExe_Description newServerDescription = {0};

		if (pList->iUGCContainerIDForPlayingServers)
		{
			newServerDescription.eServerType = GSTYPE_UGC_PLAY;
			newServerDescription.iUGCProjectID = pList->iUGCContainerIDForPlayingServers;
		}
		else
		{
			newServerDescription.eServerType = GSTYPE_NORMAL;
		}

		newServerDescription.eMapType = GetListZoneMapType(pList);
		newServerDescription.pMapDescription = pList->pMapDescription;
		
		newServerDescription.bAllowInstanceSwitchingBetweenOwnedMaps = (pList->pCategoryConfig && pList->pCategoryConfig->bAllowMultipleCopiesOfOwnedMaps);

		log_printf(LOG_LOGIN, "RNOE %u: Need a new partition because %s, no servers found, launching a new one", iCmdID, pReason);

		pServer = NewMapTransfer_LaunchNewServer(&newServerDescription, pReason, pRequest->patchInfo, pRequesterInfo, NULL, false);
		pPartition = AddPartitionSummaryInternal(pServer, pRequest->baseMapDescription.mapVariables, pRequest->baseMapDescription.ownerType, pRequest->baseMapDescription.ownerID, pRequest->baseMapDescription.iVirtualShardID);
		ReturnAddressOrSendPlayerToServerWhenReady(pServer, pPartition, pRequesterInfo, iCmdID, SENDPLAYERFLAGS_ALREADYDIDNOTIFICATION);
	}

	estrDestroy(&pReason);
}

static void RequestNewOrExistingGameServerAddress_ForceNew(PossibleMapChoice *pRequest, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, SlowRemoteCommandID iCmdID)
{
	GameServerList *pList;

	if (!stashFindPointer(sGameServerListsByMapDescription, pRequest->baseMapDescription.mapDescription, &pList))
	{
		ErrorOrAlert("UNKNOWN_MAP", "Player %u requested transfer to unknown map %s, not allowed", pRequesterInfo->iEntContainerID, pRequest->baseMapDescription.mapDescription);
		NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "Unknown map %s", pRequest->baseMapDescription.mapDescription);

		return;
	}

	CreateNewPartitionInListAndSendPlayerWhenReady(pList, pRequest, pRequesterInfo, iCmdID, "Player %s requested a ForceNew partition", GetPlayerString(pRequesterInfo));

}

AUTO_STRUCT;
typedef struct CachedNewUgcMapRequest
{
	PossibleMapChoice *pRequest;
	NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo;
	SlowRemoteCommandID iCmdID;
} CachedNewUgcMapRequest;

static void GotZoneMapInfoForCachedNewUgcMapRequest(PossibleMapChoice *pRequest, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, SlowRemoteCommandID iCmdID, ZoneMapInfo *pZoneMapInfo)
{
	GameServerList *pList;

	if (!stashFindPointer(sGameServerListsByMapDescription, pRequest->baseMapDescription.mapDescription, &pList))
	{
		pList = NewMapTransfer_CreateAndRegisterNewGameServerList(pRequest->baseMapDescription.mapDescription, pZoneMapInfo, LISTTYPE_NORMAL);
	}

	//our map list now exists, so start the entire transfer process over again
	NewMapTransfer_RequestNewOrExistingGameServerAddress(pRequest, pRequesterInfo, iCmdID);
}

static void RequestZoneMapInfoForUGCMapThenRestartTransferCB(ZoneMapInfo *pZMInfo, CachedNewUgcMapRequest *pCachedRequest)
{
	if (!pZMInfo)
	{
		NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(pCachedRequest->iCmdID, "Couldn't get ZoneMapInfo for %s despite using LocallyRequestZoneMapInfoByPublicName", pCachedRequest->pRequest->baseMapDescription.mapDescription);
	}
	else
	{
		GotZoneMapInfoForCachedNewUgcMapRequest(pCachedRequest->pRequest, pCachedRequest->pRequesterInfo, pCachedRequest->iCmdID, pZMInfo);
	}

	StructDestroy(parse_CachedNewUgcMapRequest, pCachedRequest);
}

static void RequestZoneMapInfoForUGCMapThenRestartTransfer(PossibleMapChoice *pRequest, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, SlowRemoteCommandID iCmdID)
{
	ZoneMapInfo* pZoneMapInfo = worldGetZoneMapByPublicName(pRequest->baseMapDescription.mapDescription);

	if (pZoneMapInfo)
	{
		GotZoneMapInfoForCachedNewUgcMapRequest(pRequest, pRequesterInfo, iCmdID, pZoneMapInfo);
	}
	else
	{
		CachedNewUgcMapRequest *pCachedRequest = StructCreate(parse_CachedNewUgcMapRequest);
		pCachedRequest->iCmdID = iCmdID;
		pCachedRequest->pRequest = StructClone(parse_PossibleMapChoice, pRequest);
		pCachedRequest->pRequesterInfo = StructClone(parse_NewOrExistingGameServerAddressRequesterInfo, pRequesterInfo);

		log_printf(LOG_LOGIN, "RNOE %u: don't have zone map info for %s, requesting it from resource DB", 
			iCmdID, pRequest->baseMapDescription.mapDescription);

		LocallyRequestZoneMapInfoByPublicName(pRequest->baseMapDescription.mapDescription, RequestZoneMapInfoForUGCMapThenRestartTransferCB, pCachedRequest);
		
	}
}

//if true, then if someone wants to go to this owned map, and the owned map is full, then it's NOT a problem, just
//go to another one, or perhaps create a new one. This is true, for instance, on STO when it's a map owned by a guild
bool DontAlertOnFullOwnedMap(GameServerList *pList)
{
	if (pList->pCategoryConfig && pList->pCategoryConfig->bAllowMultipleCopiesOfOwnedMaps)
	{
		return true;
	}

	return false;
}


static void RequestNewOrExistingGameServerAddress_NewOrExistingOwned(PossibleMapChoice *pRequest, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, SlowRemoteCommandID iCmdID)
{
	GameServerList *pList;

	MapPartitionSummary *pPartition;
	int iServerNum, iPartitionNum;

	if (!stashFindPointer(sGameServerListsByMapDescription, pRequest->baseMapDescription.mapDescription, &pList))
	{
		U32 iUGCContainerID = 0;


		
		if ((iUGCContainerID = UGCProject_GetProjectContainerIDFromUGCResource(pRequest->baseMapDescription.mapDescription)))
		{
			RequestZoneMapInfoForUGCMapThenRestartTransfer(pRequest, pRequesterInfo, iCmdID);
			return;
		}
		else
		{
			ErrorOrAlert("UNKNOWN_MAP", "Player %u requested transfer to unknown map %s, not allowed", pRequesterInfo->iEntContainerID, pRequest->baseMapDescription.mapDescription);
			NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "Unknown map %s", pRequest->baseMapDescription.mapDescription);

			return;
		}
	}

	for (iServerNum = 0; iServerNum < eaSize(&pList->ppGameServers); iServerNum++)
	{
		TrackedGameServerExe *pServer = pList->ppGameServers[iServerNum];

		if (pServer->bToldToDie || pServer->bLocked)
		{

			continue;
		}

		for (iPartitionNum = 0; iPartitionNum < eaSize(&pServer->description.ppPartitions); iPartitionNum++)
		{
			pPartition = pServer->description.ppPartitions[iPartitionNum];


			if (pPartition->eOwnerType == pRequest->baseMapDescription.ownerType 
				&& pPartition->iOwnerID == pRequest->baseMapDescription.ownerID 
				&& pPartition->iVirtualShardID == pRequest->baseMapDescription.iVirtualShardID
				&& stricmp_safe(pPartition->pMapVariables, pRequest->baseMapDescription.mapVariables) == 0)
			{
				static char *pNoLoginsReason = NULL;


				if (!NewMapTransfer_GameServerIsAcceptingLogins(pServer, true, &pNoLoginsReason, NULL))
				{
					if (DontAlertOnFullOwnedMap(pList))
					{
						continue;
					}

					ErrorOrAlert("OWNED_MAP_FULL", "new map transfer code wants to send %s to GS %u partition %u map %s (has to be that one, because it's owned). However, the GS is not accepting logins because: %s",
						GetPlayerString(pRequesterInfo), pServer->iContainerID, pPartition->uPartitionID, 
						pList->pMapDescription, pNoLoginsReason);
					NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "Full GS");
					return;
				}

				if (!NewMapTransfer_PartitionIsAcceptingLogins(pServer, pPartition, true, &pNoLoginsReason, NULL))
				{
					if (DontAlertOnFullOwnedMap(pList))
					{
						continue;
					}

					ErrorOrAlert("OWNED_MAP_FULL", "new map transfer code wants to send %s to GS %u partition %u map %s (has to be that one, because it's owned). However, the partition is not accepting logins because: %s",
						GetPlayerString(pRequesterInfo), pServer->iContainerID, pPartition->uPartitionID, 
						pList->pMapDescription, 
						pNoLoginsReason);
					NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "Full GS");
					return;
				}

					ReturnAddressOrSendPlayerToServerWhenReady(pServer, pPartition, pRequesterInfo, iCmdID, 0);
				return;
			}
		}		
	}
	
	//couldn't find an existing server/partition
    // If this map is configured to have a maximum number of instances, then don't allow the play to transfer to it if that would require launching a new instance
    if ( pList->pCategoryConfig->iMaxInstances && ( eaSize(&pList->ppGameServers) >= pList->pCategoryConfig->iMaxInstances ) )
    {
        NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "Max Instances");
    }
    else
    {
    	CreateNewPartitionInListAndSendPlayerWhenReady(pList, pRequest, pRequesterInfo, iCmdID, "Player %s looking for partition with owner %s[%u], none existed",
	    	GetPlayerString(pRequesterInfo), GlobalTypeToName(pRequest->baseMapDescription.ownerType), pRequest->baseMapDescription.ownerID);
    }
}

//checks all maps that match a particular search info, returns the one to which a teammate most recently went, if any, but only if it was very recent
static bool FindWhereATeammateRecentlyWent(PossibleMapChoice *pRequest, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo,
	MapPartitionSummary **ppOutPartition, TrackedGameServerExe **ppOutServer)
{	
	GameServerList *pList;
	U32 iBestTime = 0;
	
	if (sbPrintRecentTeammateChecks)
	{
		printf("Doing a recent teammate check\n");
	}

	if (!pRequesterInfo->iRequestingTeamID)
	{
		return false;
	}

	if (!stashFindPointer(sGameServerListsByMapDescription, pRequest->baseMapDescription.mapDescription, &pList))
	{
		return false;
	}

	FOR_EACH_PARTITION_IN_LIST(pList, pPartition, pServer)
	{
		if (pPartition->iVirtualShardID == pRequest->baseMapDescription.iVirtualShardID && stricmp_safe(pPartition->pMapVariables, pRequest->baseMapDescription.mapVariables) == 0)
		{
			if (NewMapTransfer_GameServerIsAcceptingLogins(pServer, true, NULL, NULL) && NewMapTransfer_PartitionIsAcceptingLogins(pServer, pPartition, true, NULL, NULL))
			{
				U32 iCurTime = NewMapTransfer_GetLastTimeTeamWasSentToPartition(pPartition, pRequesterInfo->iRequestingTeamID, RECENT_TEAM_LOGIN_CUTOFF_TO_OVERRIDE_CHOICE);
				if (iCurTime > iBestTime)
				{
					iBestTime = iCurTime;
					*ppOutPartition = pPartition;
					*ppOutServer = pServer;
				}
			}
		}
	}
	FOR_EACH_PARTITION_END;

	if (iBestTime)
	{
		if (sbPrintRecentTeammateChecks)
		{
			printf("Found one\n");
		}
		return true;
	}

	return false;

}


static void RequestNewOrExistingGameServerAddress_SpecifiedOnly(PossibleMapChoice *pRequest, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, SlowRemoteCommandID iCmdID)
{	
	MapPartitionSummary *pPartition;
	TrackedGameServerExe *pServer;
	static char *pNoLoginReason = NULL;

	// this function already checks for virtual shard ID, map variables, and so forth
	if (pRequest->bGoWhereATeammateRecentlyWentInsteadIfPossible && FindWhereATeammateRecentlyWent(pRequest, pRequesterInfo, &pPartition, &pServer))
	{
		log_printf(LOG_LOGIN, "RNOE %u: Found where a teammate went, using that instead", iCmdID);
		ReturnAddressOrSendPlayerToServerWhenReady(pServer, pPartition, pRequesterInfo, iCmdID, 0);
		return;
	}

	if (pRequest->baseMapDescription.containerID && pRequest->baseMapDescription.iPartitionID)
	{
		pPartition = GetPartitionFromIDs(pRequest->baseMapDescription.containerID, pRequest->baseMapDescription.iPartitionID, &pServer);

		if (!pPartition)
		{
			NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "Specific GS partition no longer exists");
			return;
		}

		if (!NewMapTransfer_GameServerIsAcceptingLogins(pServer, true, &pNoLoginReason, NULL))
		{
			NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "Server %u not accepting logins: %s", pServer->iContainerID, pNoLoginReason);
			return;
		}

		if (!NewMapTransfer_PartitionIsAcceptingLogins(pServer, pPartition, true, &pNoLoginReason, NULL))
		{
			NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "Server %u partition %u not accepting logins: %s", pServer->iContainerID, pPartition->uPartitionID, pNoLoginReason);
			return;
		}

		ReturnAddressOrSendPlayerToServerWhenReady(pServer, pPartition, pRequesterInfo, iCmdID, 0);
	}
	else
	{
		NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "Invalid GS ID details");
	}
}


static void RequestNewOrExistingGameServerAddress_NewPartitionOnSpecifiedServer(PossibleMapChoice *pRequest, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, SlowRemoteCommandID iCmdID)
{
	if (pRequest->baseMapDescription.containerID)
	{
		TrackedGameServerExe *pServer;
		MapPartitionSummary *pPartition;

		if (!stashIntFindPointer(sGameServerExesByID, pRequest->baseMapDescription.containerID, &pServer))
		{
	
			NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "Specific GS no longer exists");
			return;
		}

		if (!NewMapTransfer_ServerHasRoomForNewPartition(pServer))
		{
			NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "Specific GS now full");
			return;
		}

		pPartition = AddPartitionSummaryInternal(pServer, pRequest->baseMapDescription.mapVariables, pRequest->baseMapDescription.ownerType, pRequest->baseMapDescription.ownerID, pRequest->baseMapDescription.iVirtualShardID);
		ReturnAddressOrSendPlayerToServerWhenReady(pServer, pPartition, pRequesterInfo, iCmdID, 0);
	}
	else
	{
		NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "No GS ID for NewPartitionOnSpecifiedServer");
	}
}

typedef struct PartitionAndServer 
{
	TrackedGameServerExe *pServer;
	MapPartitionSummary *pPartition;
} PartitionAndServer;

//if a team logged into a map > 15 minutes ago, it doesn't count for BEST_FIT purposes
#define RECENT_TEAM_LOGIN_CUTOFF (15 * 60)


static int SortPossiblePartitionsByBestFit(const PartitionAndServer **ppPart1, const PartitionAndServer **ppPart2)
{
	const PartitionAndServer *pPart1 = *ppPart1;
	const PartitionAndServer *pPart2 = *ppPart2;
	int iVal1;
	int iVal2;

	if (!pPart1->pServer->pList || !pPart2->pServer->pList)
	{
		return 0;
	}

	//more or less off the top of my head, want to send people to a map that's as close to half full as possible.. otherwise it's too empty or too full
	iVal1 = pPart1->pServer->pList->pCategoryConfig->iMaxPlayers_Soft / 2 - (pPart1->pPartition->iNumPlayers + pPart1->pPartition->iNumPlayersRecentlyLoggingIn + pPart1->pPartition->iNumPendingRequests);
	iVal2 = pPart2->pServer->pList->pCategoryConfig->iMaxPlayers_Soft / 2 - (pPart2->pPartition->iNumPlayers + pPart2->pPartition->iNumPlayersRecentlyLoggingIn + pPart2->pPartition->iNumPendingRequests);
	iVal1 = ABS(iVal1);
	iVal2 = ABS(iVal2);

	return iVal1 - iVal2;
}

static bool FindBestFitPartitionFromList(GameServerList *pList, PossibleMapChoice *pRequest, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, TrackedGameServerExe **ppOutServer, MapPartitionSummary **ppOutPartition)
{
	PartitionAndServer **ppPossibleList = NULL;
	int i;

	//first make a list of servers accepting logins with the hard cap so that cases like teams showing up in the same map can
	//work

	FOR_EACH_PARTITION_IN_LIST(pList, pPartition, pServer)
		if (pPartition->iVirtualShardID == pRequest->baseMapDescription.iVirtualShardID && stricmp_safe(pPartition->pMapVariables, pRequest->baseMapDescription.mapVariables) == 0)
		{
			if (NewMapTransfer_GameServerIsAcceptingLogins(pServer, true, NULL, NULL) && NewMapTransfer_PartitionIsAcceptingLogins(pServer, pPartition, true, NULL, NULL))
			{
				PartitionAndServer *pPartitionAndServer = malloc(sizeof(PartitionAndServer));
				pPartitionAndServer->pServer = pServer;
				pPartitionAndServer->pPartition = pPartition;

				eaPush(&ppPossibleList, pPartitionAndServer);
			}
		}
	FOR_EACH_PARTITION_END

	//when we have a team ID, find the most recent map that someone on our team was sent to
	if (pRequesterInfo->iRequestingTeamID && eaSize(&ppPossibleList) > 1)
	{
		int iBestIndex = 0;
		int iBestTime = NewMapTransfer_GetLastTimeTeamWasSentToPartition(ppPossibleList[0]->pPartition, pRequesterInfo->iRequestingTeamID, RECENT_TEAM_LOGIN_CUTOFF);

		for (i=0; i < eaSize(&ppPossibleList); i++)
		{
			int iCurTime = NewMapTransfer_GetLastTimeTeamWasSentToPartition(ppPossibleList[i]->pPartition, pRequesterInfo->iRequestingTeamID, RECENT_TEAM_LOGIN_CUTOFF);
			if (iCurTime > iBestTime)
			{
				iBestTime = iCurTime;
				iBestIndex = i;
			}
		}

		if (iBestTime)
		{
			*ppOutServer = ppPossibleList[iBestIndex]->pServer;
			*ppOutPartition = ppPossibleList[iBestIndex]->pPartition;

			eaDestroyEx(&ppPossibleList, NULL);

			return true;
		}
	}

	//now cull out everything that isn't acceptable via the soft cap
	for (i = eaSize(&ppPossibleList) - 1; i >= 0; i--)
	{
		PartitionAndServer *pPandS = ppPossibleList[i];
		if (!(NewMapTransfer_GameServerIsAcceptingLogins(pPandS->pServer, false, NULL, NULL) && NewMapTransfer_PartitionIsAcceptingLogins(pPandS->pServer, pPandS->pPartition, false, NULL, NULL)))
		{
			free(pPandS);
			eaRemoveFast(&ppPossibleList, i);
		}
	}

	if (eaSize(&ppPossibleList))
	{
		eaQSort(ppPossibleList, SortPossiblePartitionsByBestFit);
		*ppOutServer = ppPossibleList[0]->pServer;
		*ppOutPartition = ppPossibleList[0]->pPartition;

		eaDestroyEx(&ppPossibleList, NULL);

		return true;
	}

	
	eaDestroyEx(&ppPossibleList, NULL);

	return false;
}

static void RequestNewOrExistingGameServerAddress_BestFit(PossibleMapChoice *pRequest, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, SlowRemoteCommandID iCmdID)
{
	TrackedGameServerExe *pServer;
	MapPartitionSummary *pPartition;
	GameServerList *pList;

	if (!stashFindPointer(sGameServerListsByMapDescription, pRequest->baseMapDescription.mapDescription, &pList))
	{
		ErrorOrAlert("UNKNOWN_MAP", "Player %u requested transfer to unknown map %s, not allowed", pRequesterInfo->iEntContainerID, pRequest->baseMapDescription.mapDescription);
		NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "Unknown map %s", pRequest->baseMapDescription.mapDescription);

		return;
	}

	if (FindBestFitPartitionFromList(pList, pRequest, pRequesterInfo, &pServer, &pPartition))
	{
		ReturnAddressOrSendPlayerToServerWhenReady(pServer, pPartition, pRequesterInfo, iCmdID, 0);
	}
	else
	{
		CreateNewPartitionInListAndSendPlayerWhenReady(pList, pRequest, pRequesterInfo, iCmdID, "Player %s requested a BestFit partition, couldn't find one, so creating new", GetPlayerString(pRequesterInfo));
	}

}

static void RequestNewOrExistingGameServerAddress_SpecifiedOrBestFit(PossibleMapChoice *pRequest, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, SlowRemoteCommandID iCmdID)
{
	MapPartitionSummary *pPartition = NULL;
	TrackedGameServerExe *pServer = NULL;
	
	// this function already checks for virtual shard ID, map variables, and so forth
	if (pRequest->bGoWhereATeammateRecentlyWentInsteadIfPossible && FindWhereATeammateRecentlyWent(pRequest, pRequesterInfo, &pPartition, &pServer))
	{
		log_printf(LOG_LOGIN, "RNOE %u: Found where a teammate went", iCmdID);
		ReturnAddressOrSendPlayerToServerWhenReady(pServer, pPartition, pRequesterInfo, iCmdID, 0);
		return;
	}

	if (pRequest->baseMapDescription.containerID && pRequest->baseMapDescription.iPartitionID)
	{

		pPartition = GetPartitionFromIDs(pRequest->baseMapDescription.containerID, pRequest->baseMapDescription.iPartitionID, &pServer);

		if (pPartition && NewMapTransfer_GameServerIsAcceptingLogins(pServer, true, NULL, NULL) && NewMapTransfer_PartitionIsAcceptingLogins(pServer, pPartition, true, NULL, NULL))
		{
			ReturnAddressOrSendPlayerToServerWhenReady(pServer, pPartition, pRequesterInfo, iCmdID, 0);
			return;
		}
	}

	RequestNewOrExistingGameServerAddress_BestFit(pRequest, pRequesterInfo, iCmdID);
}


void NewMapTransfer_RequestNewOrExistingGameServerAddress(PossibleMapChoice *pRequest, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, SlowRemoteCommandID iCmdID)
{
	char *pRequestString = NULL;
	char *pRequesterInfoString = NULL;

	estrStackCreate(&pRequestString);
	estrStackCreate(&pRequesterInfoString);

	ParserWriteText(&pRequestString, parse_PossibleMapChoice, pRequest, 0, 0, 0);
	ParserWriteText(&pRequesterInfoString, parse_NewOrExistingGameServerAddressRequesterInfo, pRequesterInfo, 0, 0, 0);

	log_printf(LOG_LOGIN, "NewMapTransfer_RequestNewOrExistingGameServerAddress(RNOE %u). PossibleMapChoice: %s. Requester: %s",
		iCmdID, pRequestString, pRequesterInfoString);

	estrDestroy(&pRequestString);
	estrDestroy(&pRequesterInfoString);

	switch (pRequest->eChoiceType)
	{
	xcase MAPCHOICETYPE_UNSPECIFIED:
		AssertOrAlert("BAD_MAPCHOICE_TYPE", "Unspecified choices should never make it to new map transfer code");
		NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(iCmdID, "Unspecified request choice type");

	xcase MAPCHOICETYPE_FORCE_NEW:
		RequestNewOrExistingGameServerAddress_ForceNew(pRequest, pRequesterInfo, iCmdID);

	xcase MAPCHOICETYPE_NEW_OR_EXISTING_OWNED:
		RequestNewOrExistingGameServerAddress_NewOrExistingOwned(pRequest, pRequesterInfo, iCmdID);

	xcase MAPCHOICETYPE_SPECIFIED_ONLY:
		RequestNewOrExistingGameServerAddress_SpecifiedOnly(pRequest, pRequesterInfo, iCmdID);

	xcase MAPCHOICETYPE_BEST_FIT:
		RequestNewOrExistingGameServerAddress_BestFit(pRequest, pRequesterInfo, iCmdID);

	xcase MAPCHOICETYPE_SPECIFIED_OR_BEST_FIT:
		RequestNewOrExistingGameServerAddress_SpecifiedOrBestFit(pRequest, pRequesterInfo, iCmdID);

	xcase MAPCHOICETYPE_NEW_PARTITION_ON_SPECIFIED_SERVER:
		RequestNewOrExistingGameServerAddress_NewPartitionOnSpecifiedServer(pRequest, pRequesterInfo, iCmdID);
	}
}

static bool NewMapTransfer_AttemptTransferToExistingEditServer_DevModeOnly(PossibleUGCProject *pUGCProjectRequest, SlowRemoteCommandID iCmdID, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo)
{
	GameServerList *pList = NewMapTransfer_FindEditingGameServerList();
	int i;

	const char *pMapName = GetEditMapNameFromPossibleUGCProject(pUGCProjectRequest, /*bCopy=*/false);

	if (!pList)
	{
		return false;
	}

	for (i=0; i < eaSize(&pList->ppGameServers); i++)
	{
		TrackedGameServerExe *pServer = pList->ppGameServers[i];
		
		if (pServer->description.pMapDescription == pMapName)
		{
			if (eaSize(&pServer->description.ppPartitions))
			{
				MapPartitionSummary *pPartition = pServer->description.ppPartitions[0];

				if (pPartition->eOwnerType == GLOBALTYPE_ENTITYPLAYER && pPartition->iOwnerID == pRequesterInfo->iEntContainerID)
				{
					ReturnAddressOrSendPlayerToServerWhenReady(pServer, pPartition, pRequesterInfo, iCmdID, 0);
					return true;
				}
			}
		}
	}
	
	return false;
}



void NewMapTransfer_RequestUGCEditingServer(PossibleUGCProject *pUGCProjectRequest, SlowRemoteCommandID iCmdID, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo)
{
	GameServerExe_Description *pDescription;
	char *pCommandLine = NULL;
	TrackedGameServerExe *pServer = NULL;
	MapPartitionSummary *pPartition = NULL;
	char *pRequestString = NULL;
	char *pRequesterInfoString = NULL;

	if(isDevelopmentMode() && ugc_DevMode)
	{
		if (NewMapTransfer_AttemptTransferToExistingEditServer_DevModeOnly(pUGCProjectRequest, iCmdID, pRequesterInfo))
		{
			return;
		}
	}

	estrStackCreate(&pRequestString);
	estrStackCreate(&pRequesterInfoString);

	ParserWriteText(&pRequestString, parse_PossibleUGCProject, pUGCProjectRequest, 0, 0, 0);
	ParserWriteText(&pRequesterInfoString, parse_NewOrExistingGameServerAddressRequesterInfo, pRequesterInfo, 0, 0, 0);

	log_printf(LOG_LOGIN, "NewMapTransfer_RequestUGCEditingServer(RNOE %u). pUGCProjectRequest: %s. Requester: %s",
		iCmdID, pRequestString, pRequesterInfoString);

	estrDestroy(&pRequestString);
	estrDestroy(&pRequesterInfoString);


	pDescription = StructCreate(parse_GameServerExe_Description);
	pDescription->eMapType = ZMTYPE_MISSION;
	pDescription->eServerType = GSTYPE_UGC_EDIT;
	pDescription->iUGCProjectID = pUGCProjectRequest->iID;
	pDescription->pMapDescription = GetEditMapNameFromPossibleUGCProject(pUGCProjectRequest, /*bCopy=*/false);

	if (pUGCProjectRequest->iCopyID != 0)
	{
		const char *pcCopyNamespace = "";
		const char *pcNoDownPatch = "";

		pcCopyNamespace = pUGCProjectRequest->strCopyEditVersionNamespace;
		if(pUGCProjectRequest->bCopyEditVersionIsNew)
			pcNoDownPatch = "-NoDownPatch";

		estrPrintf(&pCommandLine, " %s -UGCImportProjectOnStart %s -LoadUserNamespaces %s %s ", 
					pUGCProjectRequest->iPossibleUGCProjectFlags & POSSIBLEUGCPROJECT_FLAG_NOPUBLISHING ? "-NoPublishing" : "",
					pcCopyNamespace, pcCopyNamespace, pcNoDownPatch);
	}
	else
	{
		estrPrintf(&pCommandLine, " %s %s ", 
					pUGCProjectRequest->iPossibleUGCProjectFlags & POSSIBLEUGCPROJECT_FLAG_NOPUBLISHING ? "-NoPublishing" : "",
					(pUGCProjectRequest->bEditVersionIsNew) ? "-NoDownPatch" : "");
	}
	
	pServer = NewMapTransfer_LaunchNewServer(pDescription, STACK_SPRINTF("new edit server requested by %s", GetPlayerString(pRequesterInfo)), pUGCProjectRequest->pPatchInfo, pRequesterInfo, pCommandLine, false);
	pPartition = AddPartitionSummaryInternal(pServer, NULL, GLOBALTYPE_ENTITYPLAYER, pRequesterInfo->iEntContainerID, pUGCProjectRequest->iVirtualShardID);
	ReturnAddressOrSendPlayerToServerWhenReady(pServer, pPartition, pRequesterInfo, iCmdID, SENDPLAYERFLAGS_ALREADYDIDNOTIFICATION);

	StructDestroy(parse_GameServerExe_Description, pDescription);
	estrDestroy(&pCommandLine);
}


U32 NewMapTransfer_GetLastTimeTeamWasSentToPartition(MapPartitionSummary *pPartition, ContainerID iTeamID, int iCutoffPeriod)
{
	U32 iRetVal;

	if (!pPartition->sTeamsSentThereByID)
	{
		return 0;
	}

	if (!stashIntFindInt(pPartition->sTeamsSentThereByID, iTeamID, &iRetVal))
	{
		return 0;
	}

	if (iRetVal < timeSecondsSince2000() - iCutoffPeriod)
	{
		return 0;
	}

	return iRetVal;
}


void NewMapTransfer_TeamBeingSentToPartitionNow(MapPartitionSummary *pPartition, ContainerID iTeamID)
{
	if (!pPartition->sTeamsSentThereByID)
	{
		pPartition->sTeamsSentThereByID = stashTableCreateInt(32);
	}

	stashIntAddInt(pPartition->sTeamsSentThereByID, iTeamID, timeSecondsSince2000(), true);
}


#include "aslMapManagerNewMapTransfer_GetAddress_c_ast.c"
