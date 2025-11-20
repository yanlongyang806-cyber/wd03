#include "estring.h"
#include "Textparser.h"
#include "earray.h"
#include "mapdescription.h"
#include "aslMapManagerConfig.h"
#include "Stashtable.h"
#include "stringCache.h"
#include "resourceInfo.h"
#include "staticworld/worldGridPrivate.h"
#include "aslMapManager.h"
#include "aslMapManagerConfig_h_ast.h"
#include "aslMapManagerNewMapTransfer.h"
#include "MapDescription_h_ast.h"
#include "alerts.h"
#include "autogen/controller_autogen_remotefuncs.h"
#include "autogen/gameserverlib_autogen_remotefuncs.h"
#include "logging.h"
#include "../../core/controller/pub/controllerpub.h"
#include "autogen/AppServerLib_autogen_slowfuncs.h"
#include "IntFIFO.h"
#include "autogen/gameserverlib_autogen_remotefuncs.h"
#include "aslMapManagerNewMapTransfer_Private.h"
#include "aslMapManagerNewMapTransfer_Private_h_ast.h"
#include "aslMapManagerNewMapTransfer_InnerStructs.h"
#include "aslMapManagerNewMapTransfer_GetAddress.h"
#include "serverlib.h"
#include "AslMapManager_h_ast.h"
#include "ugcProjectUtils.h"

#include "aslMapManagerActivity.h"
#include "aslMapManagerrandomQueueRelay.h"

ContainerID *spRecentlyDoneLoadingMaps = NULL;




MapCategoryConfig gFallbackDefaultConfig = 
{
	"__Fallback",
	0,
	100,
	80,
	0,
	0,
	1,
};




void NewMapTransfer_SendDescriptionToGameServer(TrackedGameServerExe *pServer, SlowRemoteCommandID iCmdID);
bool NewMapTransfer_ServerCanDieIfItHasTimedOut(TrackedGameServerExe *pServer);



//appserver.exe has finished loading all data and so forth, starting to go into our startup statemachine
void NewMapTransfer_Init(void)
{
	NewMapTransfer_InitMapLists();
}

void NewMapTransfer_BeginNormalOperation(void)
{
	U32 *pIDsToKill = NULL;


	if (isProductionMode())
	{
		//in production mode, at this point every gameserver is either one that is in gslRunning that we heard
		//about from the controller and are happily tracking, or it isn't. So we kill every one we don't know about
		Controller_KillAllButSomeServersOfTypeInfo *pInfo = StructCreate(parse_Controller_KillAllButSomeServersOfTypeInfo);
		pInfo->eServerType = GLOBALTYPE_GAMESERVER;
		pInfo->pReason = strdup("Map Manager beginning normal operations, killing everything it doesn't know about");

		FOR_EACH_IN_STASHTABLE(sGameServerExesByID, TrackedGameServerExe, pServer)
		{
			if (pServer->eState == GSSTATE_RUNNING)
			{
				ea32Push(&pInfo->pIDsNotToKill, pServer->iContainerID);
			}
			else
			{
				ea32Push(&pIDsToKill, pServer->iContainerID);
			}
		}
		FOR_EACH_END

		RemoteCommand_KillAllButSomeServersOfType(GLOBALTYPE_CONTROLLER, 0, pInfo);

		StructDestroy(parse_Controller_KillAllButSomeServersOfTypeInfo, pInfo);

		while (ea32Size(&pIDsToKill))
		{
			TrackedGameServerExe *pServer = GetGameServerFromID(ea32Pop(&pIDsToKill));
			if (pServer)
			{
				RemoveAndDestroyGameServerExe(pServer, "Improper state when beginning normal operation");
			}
		}

		ea32Destroy(&pIDsToKill);
	}


	if (gMapManagerState.bDoStartingMaps)
	{
		NewMapTransfer_DoStartingMaps();
	}
}




void NewMapTransfer_InformMapManagerOfGameServerDeath(ContainerID iContainerID, bool bUnnaturalDeath)
{

	TrackedGameServerExe *pServer;

	log_printf(LOG_MM_GAMESERVERS, "Map manager being informed of game server %u death\n", iContainerID);

	if (stashIntFindPointer(sGameServerExesByID, iContainerID, &pServer))
	{

		if (bUnnaturalDeath)
		{
			int i;
			char *pLogString = NULL;
			estrPrintf(&pLogString, "GameServer %u died an unnatural death. Recent players who logged in: ", iContainerID);
			for (i=0; i < eaSize(&pServer->ppPlayersSentHere); i++)
			{
				RegisterPlayerWasSentToMapBeforeItCrashed(pServer->ppPlayersSentHere[i], pServer->iContainerID);
				estrConcatf(&pLogString, "%s%u", i == 0 ? "" : ", ", pServer->ppPlayersSentHere[i]->iPlayerContainerID);
			}

			log_printf(LOG_ERRORS, "%s", pLogString);
			estrDestroy(&pLogString);
		}

		RemoveAndDestroyGameServerExe(pServer, "Informed by controller that map closed or crashed");
	}
}

//"return" for GetGameServerDescriptionFromMapManager_NewMapTransferCode
void NewMapTransfer_SendDescriptionToGameServer(TrackedGameServerExe *pServer, SlowRemoteCommandID iCmdID)
{
	SlowRemoteCommandReturn_GetGameServerDescriptionFromMapManager_NewMapTransferCode(iCmdID, &pServer->description);
}



static void NewMapTransfer_StartServerComplete(TransactionReturnVal *returnVal, void *userData)
{
	ContainerID iID = (ContainerID)(intptr_t)userData;
	TrackedGameServerExe *pServer;
	Controller_SingleServerInfo *pSingleServerInfo;
	enumTransactionOutcome eOutcome;

	if (!stashIntFindPointer(sGameServerExesByID, iID, &pServer))
	{
		log_printf(LOG_MM_GAMESERVERS, "Command %s returned for invalid map id %d", __FUNCTION__, iID);
		return;
	}

	if (pServer->eState != GSSTATE_LAUNCH_REQUESTED_FROM_CONTROLLER)
	{
		log_printf(LOG_MM_GAMESERVERS, "Command %s returned for map %d while in invalid state", __FUNCTION__, iID);
		return;
	}

	eOutcome = RemoteCommandCheck_StartServer(returnVal, &pSingleServerInfo);

	if (eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		RemoveAndDestroyGameServerExe(pServer, "Map creation failed");
		return;
	}

	if (pSingleServerInfo->eFailureType == FAILURE_NO_MACHINE_FOR_GAME_SERVER)
	{
		RemoveAndDestroyGameServerExe(pServer, "LoginError_NoMachineForGameServer");
	}
	else if (pSingleServerInfo->iIP == 0)
	{
		RemoveAndDestroyGameServerExe(pServer, "Map creation failed");
	}
	else
	{
		if (pServer->description.eServerType == GSTYPE_PRELOAD)
		{
			pServer->eState = GSSTATE_PRELOAD_WAITING_FOR_HANDSHAKE;
		}
		else
		{
			pServer->eState = GSSTATE_SPAWNED_WAITING_FOR_HANDSHAKE;
		}
		

		log_printf(LOG_MM_GAMESERVERS, "map creation succeeded for map %d\n", pServer->iContainerID);						
		AddGameServerToMachine(pServer, pSingleServerInfo->machineName, pSingleServerInfo->iIP, pSingleServerInfo->iPublicIP);

		pServer->description.iListeningPortNum = 0;

		if (pServer->iPendingGameServerDescriptionRequest)
		{
			NewMapTransfer_SendDescriptionToGameServer(pServer, pServer->iPendingGameServerDescriptionRequest);
			pServer->iPendingGameServerDescriptionRequest = 0;
			pServer->eState = GSSTATE_LOADING;
		}
	}

	StructDestroy(parse_Controller_SingleServerInfo, pSingleServerInfo);

}

TrackedGameServerExe *NewMapTransfer_LaunchNewServer(GameServerExe_Description *pDescription, char *pReason,  
	DynamicPatchInfo *pPatchInfo, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, char *pExtraCommandLine_In, bool bPreLoad)
{
	char *pExtraCommandLine = NULL;
	TrackedGameServerExe *pServer;
	char userNameSpace[RESOURCE_NAME_MAX_SIZE], baseName[RESOURCE_NAME_MAX_SIZE];

	AdditionalServerLaunchInfo additionalInfo = {0};
	MapCategoryConfig *pCategory = NULL;

	ServerLaunchDebugNotificationInfo *pLaunchDebugNotification = NULL;

	pServer = CreateAndLinkTrackedGameServerExe(GSSTATE_LAUNCH_REQUESTED_FROM_CONTROLLER, pDescription, 0);

	log_printf(LOG_MM_GAMESERVERS, "Creating new GS %u because: %s", pServer->iContainerID, pReason);

	if (pServer->pList)
	{
		pCategory = pServer->pList->pCategoryConfig;
	}

	if (resExtractNameSpace(pDescription->pMapDescription, userNameSpace, baseName))
	{
		estrPrintf(&pExtraCommandLine," -LoadUserNamespaces \"%s\"", userNameSpace);
	}

	// Test code to get UGC custom map GameServers to patch down namespace into dynamic.hogg to test playing a UGC custom map.
	/*if (pDescription->eServerType == GSTYPE_UGC_PLAY && isDevelopmentMode())
	{
		estrConcatf(&pExtraCommandLine, " -force_dynamic_patch 1 -OverridePatchDir %s", fileLocalDataDir());
	}*/

	if (pDescription->eServerType == GSTYPE_UGC_EDIT && isProductionEditAvailable())
		estrConcatf(&pExtraCommandLine, " -ProductionEdit 1 -SemiSharedMemory");

	if (pPatchInfo)
	{
		char *text=NULL, *superesc=NULL;
		ParserWriteText(&text, parse_DynamicPatchInfo, pPatchInfo, 0, 0, 0);
		estrSuperEscapeString(&superesc, text);
		estrConcatf(&pExtraCommandLine, " -PatchInfo %s", superesc);
		estrDestroy(&text);
		estrDestroy(&superesc);
	}

	if (pCategory)
	{
		additionalInfo.iGameServerLaunchWeight = pCategory->iLaunchWeight;
	}

	additionalInfo.zMapType = pDescription->eMapType;
	if (!additionalInfo.zMapType)
	{
		char *pAlertKey = NULL;
		char *pErrorString = NULL;

		ZoneMapType eActualType = MapCheckRequestedType(pDescription->pMapDescription, additionalInfo.zMapType, &pErrorString, &pAlertKey);

		additionalInfo.zMapType = eActualType;
		estrDestroy(&pErrorString);
	}

	if (pCategory && pCategory->iMegsBeforeMemLeakAlerts)
	{
		estrConcatf(&pExtraCommandLine, " -MemLeakTrackingIncreaseAmount %d", pCategory->iMegsBeforeMemLeakAlerts * 1024 * 1024);
	}

	if (pExtraCommandLine_In)
	{
		estrConcatf(&pExtraCommandLine, " %s", pExtraCommandLine_In);
	}

	estrConcatf(&pExtraCommandLine , " -UseNewPartitionMapTransferCode");

	if (gbDebugTransferNotifications && pRequesterInfo)
	{
		pLaunchDebugNotification = StructCreate(parse_ServerLaunchDebugNotificationInfo);
		pLaunchDebugNotification->eServerID = pRequesterInfo->iRequestingServerID;
		pLaunchDebugNotification->eServerType = pRequesterInfo->eRequestingServerType;
		pLaunchDebugNotification->iCookie = pRequesterInfo->iPlayerIdentificationCookie;
	}

	RemoteCommand_StartServer(objCreateManagedReturnVal(NewMapTransfer_StartServerComplete, (void *)(intptr_t)pServer->iContainerID),
		GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_GAMESERVER, pServer->iContainerID, 
		pServer->pList->pSpecifiedCategoryName, pExtraCommandLine, pReason, &additionalInfo, pLaunchDebugNotification, NULL, NULL);

	estrDestroy(&pExtraCommandLine);

	StructDestroySafe(parse_ServerLaunchDebugNotificationInfo, &pLaunchDebugNotification);

	return pServer;
}

AUTO_COMMAND_REMOTE_SLOW(TrackedGameServerExe*) ACMD_IFDEF(APPSERVER);
void aslMapManager_NewMapTransfer_LaunchNewServer(SlowRemoteCommandID iCmdID, GameServerExe_Description *pDescription, char *pReason,  
	DynamicPatchInfo *pPatchInfo, NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, char *pExtraCommandLine_In, bool bPreLoad)
{
	TrackedGameServerExe *pTrackedGameServerExe = NewMapTransfer_LaunchNewServer(pDescription, pReason, pPatchInfo, pRequesterInfo, pExtraCommandLine_In, bPreLoad);

	SlowRemoteCommandReturn_aslMapManager_NewMapTransfer_LaunchNewServer(iCmdID, pTrackedGameServerExe);
}

AUTO_COMMAND_REMOTE_SLOW(GameServerExe_Description*) ACMD_IFDEF(GAMESERVER);
void GetGameServerDescriptionFromMapManager_NewMapTransferCode(int iGameServerID, SlowRemoteCommandID iCmdID)
{
	TrackedGameServerExe *pServer;
	GameServerExe_Description failDescription = {GSTYPE_INVALID};

	if (eMMLState != MML_STATE_NORMAL || !stashIntFindPointer(sGameServerExesByID, iGameServerID, &pServer))
	{

		if (eMMLState != MML_STATE_NORMAL)
		{
			log_printf(LOG_MM_GAMESERVERS, "Description request for server %u failing... mapserver not ready\n", iGameServerID);
		}
		else
		{
			log_printf(LOG_MM_GAMESERVERS, "Description request for server %u failing... map doesn't exist\n", iGameServerID);
		}


		SlowRemoteCommandReturn_GetGameServerDescriptionFromMapManager_NewMapTransferCode(iCmdID, &failDescription);
		return;
	}
	

	switch (pServer->eState)
	{
	xcase GSSTATE_PRELOAD_WAITING_FOR_HANDSHAKE:
		pServer->iPendingGameServerDescriptionRequest = iCmdID;
		pServer->eState = GSSTATE_PRELOAD;
		return;
	
	xcase GSSTATE_LAUNCH_REQUESTED_FROM_CONTROLLER:
		pServer->iPendingGameServerDescriptionRequest = iCmdID;
		return;

	//should be this state most of the time
	xcase GSSTATE_SPAWNED_WAITING_FOR_HANDSHAKE:
		pServer->eState = GSSTATE_LOADING;
		break;

	xdefault:
		log_printf(LOG_MM_GAMESERVERS, "Description request for server %u failing... server somehow in state %s", iGameServerID,
			StaticDefineIntRevLookup(GameServerExeStateEnum, pServer->eState));
		SlowRemoteCommandReturn_GetGameServerDescriptionFromMapManager_NewMapTransferCode(iCmdID, &failDescription);
		return;
	}


	NewMapTransfer_SendDescriptionToGameServer(pServer, iCmdID);

/*	pServer->serverDescription.bDescriptionIsActive = true;
	pMap->serverDescription.iExpectedMaxPlayers = pMap->pCategory ? pMap->pCategory->iMaxPlayers_Hard : 0;
	SlowRemoteCommandReturn_GetGameServerDescriptionFromMapManager(iCmdID, &pMap->serverDescription);*/
}


bool NewMapTransfer_IsHandlingServer(ContainerID iContainerID)
{
	return stashIntFindPointer(sGameServerExesByID, iContainerID, NULL);
}


void NewMapTransfer_MapIsDoneLoading(ContainerID iID)
{
	ea32Push(&spRecentlyDoneLoadingMaps, iID);
}

void NewMapTransfer_ClearPendingRequestCount(TrackedGameServerExe *pServer, PendingRequestCache *pCache)
{
	MapPartitionSummary *pSummary = GetPartitionFromServerAndID(pServer, pCache->uPartitionID);
	if (pSummary && pSummary->iNumPendingRequests)
	{
		pSummary->iNumPendingRequests--;
	}
}

void NewMapTransfer_NormalOperation(void)
{
	int iServerNum;

	for (iServerNum=0; iServerNum < ea32Size(&spRecentlyDoneLoadingMaps); iServerNum++)
	{
		TrackedGameServerExe *pServer;

		if (stashIntFindPointer(sGameServerExesByID, spRecentlyDoneLoadingMaps[iServerNum], &pServer))
		{
			int i;

			for (i=0; i < eaSize(&pServer->ppPendingRequests); i++)
			{
				if (pServer->ppPendingRequests[i]->iEntContainerID)
				{
					NewMapTransfer_SendPlayerToServerNow(pServer, pServer->ppPendingRequests[i]);
				}
				else
				{
					NewMapTransfer_SendReturnAddressNow(pServer, pServer->ppPendingRequests[i]);
				}

				NewMapTransfer_ClearPendingRequestCount(pServer, pServer->ppPendingRequests[i]);
			}

			eaDestroy(&pServer->ppPendingRequests);
		}

		pServer->eState = GSSTATE_RUNNING;

		aslMapManager_MapInitActivities(pServer->iContainerID);
		aslMapManager_MapInitRandomActiveQueues(pServer->iContainerID);
	}

	ea32Destroy(&spRecentlyDoneLoadingMaps);
}


void NewMapTransfer_GameServerPortWorked(ContainerID iGameServerID, int iPortNum)
{
	TrackedGameServerExe *pServer;

	if (stashIntFindPointer(sGameServerExesByID, iGameServerID, &pServer))
	{
		pServer->description.iListeningPortNum = iPortNum;
	}
}

		








AUTO_COMMAND;
void NewMapTransfer_LaunchNewServerCmd(char *pMapName, int iNumPartitions, char *pExtraCmdLine)
{
	GameServerExe_Description description = {0};
	TrackedGameServerExe *pServer;
	int i;

	description.pMapDescription = allocAddString(pMapName);
	description.eServerType = GSTYPE_NORMAL;


	pServer = NewMapTransfer_LaunchNewServer(&description, "NewMapTransfer_LaunchNewServerCmd", NULL, NULL, pExtraCmdLine, false);

	for (i=0; i < iNumPartitions; i++)
	{
		AddPartitionSummaryInternal(pServer, NULL, GLOBALTYPE_NONE, 0, 0);
	}
}

bool NewMapTransfer_GameServerIsAcceptingLogins(TrackedGameServerExe *pServer, bool bUseHardLimit, char **ppWhyNot, NewMapTransfer_WhyNotAcceptingLogins *peWhyNot)
{
	MapCategoryConfig *pConfig;
	int iNumPlayers;

	if (pServer->bToldToDie)
	{
		if (ppWhyNot)
		{
			estrPrintf(ppWhyNot, "Server was already told to die");
		}
		if (peWhyNot)
		{
			*peWhyNot = WHYNOTACCEPTINGLOGINS_TOLD_TO_DIE;
		}
		return false;
	}

	if (pServer->bLocked)
	{
		if (ppWhyNot)
		{
			estrPrintf(ppWhyNot, "locked");
		}
		if (peWhyNot)
		{
			*peWhyNot = WHYNOTACCEPTINGLOGINS_LOCKED;
		}
		return false;
	}

	if (eaFindString(&gMapManagerConfig.ppBannedMaps, pServer->description.pMapDescription) != -1)
	{
		if (ppWhyNot)
		{
			estrPrintf(ppWhyNot, "banned");
		}
		if (peWhyNot)
		{
			*peWhyNot = WHYNOTACCEPTINGLOGINS_MAP_BANNED;
		}
		return false;
	}

	pConfig = SAFE_MEMBER2(pServer, pList, pCategoryConfig);
	iNumPlayers = NewMapTransfer_UsefulNumPlayers(pServer);

	if (!pConfig)
	{
		pConfig = &gFallbackDefaultConfig;
	}

	if (!pConfig->iMaxPlayers_AcrossPartitions_Hard)
	{
		if (peWhyNot)
		{
			*peWhyNot = WHYNOTACCEPTINGLOGINS_ALLOWED;
		}
		return true;
	}

	if (bUseHardLimit && iNumPlayers >= pConfig->iMaxPlayers_AcrossPartitions_Hard)
	{
		if (ppWhyNot)
		{
			estrPrintf(ppWhyNot, "Num players (%d) matches or exceeds across-partitions hard limit (%d)", iNumPlayers, pConfig->iMaxPlayers_AcrossPartitions_Hard);
		}
		if (peWhyNot)
		{
			*peWhyNot = WHYNOTACCEPTINGLOGINS_SERVER_HARD_FULL;
		}
		return false;
	}
	else if (!bUseHardLimit && iNumPlayers >= pConfig->iMaxPlayers_AcrossPartitions_Soft)
	{
		if (ppWhyNot)
		{
			estrPrintf(ppWhyNot, "Num players (%d) matches or exceeds across-partitions soft limit (%d)", iNumPlayers, pConfig->iMaxPlayers_AcrossPartitions_Soft);
		}
		if (peWhyNot)
		{
			*peWhyNot = WHYNOTACCEPTINGLOGINS_SERVER_SOFT_FULL;
		}
		return false;
	}

	if (peWhyNot)
	{
		*peWhyNot = WHYNOTACCEPTINGLOGINS_ALLOWED;
	}
	return true;
}

bool NewMapTransfer_PartitionIsAcceptingLogins(TrackedGameServerExe *pServer, MapPartitionSummary *pSummary, bool bUseHardLimit, char **ppWhyNot, NewMapTransfer_WhyNotAcceptingLogins *peWhyNot)
{
	MapCategoryConfig *pConfig = SAFE_MEMBER2(pServer, pList, pCategoryConfig);
	int iNumPlayers = pSummary->iNumPlayers + pSummary->iNumPlayersRecentlyLoggingIn + pSummary->iNumPendingRequests;

    if (!pConfig)
    {
        if (ppWhyNot)
        {
            estrPrintf(ppWhyNot, "Map category config not found");
        }
		if (peWhyNot)
		{
			*peWhyNot = WHYNOTACCEPTINGLOGINS_CONFIG_NOT_FOUND;
		}
        return false;
    }

	if (bUseHardLimit && iNumPlayers >= pConfig->iMaxPlayers_Hard)
	{
		if (ppWhyNot)
		{
			estrPrintf(ppWhyNot, "Num players in partition (%d (%d+%d+%d)) matches or exceeds hard limit (%d)", iNumPlayers, pSummary->iNumPlayers, pSummary->iNumPlayersRecentlyLoggingIn, pSummary->iNumPendingRequests, pConfig->iMaxPlayers_Hard);
		}
		if (peWhyNot)
		{
			*peWhyNot = WHYNOTACCEPTINGLOGINS_PARTITION_HARD_FULL;
		}
		return false;
	}
	else if (!bUseHardLimit && iNumPlayers >= pConfig->iMaxPlayers_Soft)
	{
		if (ppWhyNot)
		{
			estrPrintf(ppWhyNot, "Num players in partition (%d (%d+%d+%d)) matches or exceeds soft limit (%d)", iNumPlayers, pSummary->iNumPlayers, pSummary->iNumPlayersRecentlyLoggingIn, pSummary->iNumPendingRequests, pConfig->iMaxPlayers_Soft);
		}
		if (peWhyNot)
		{
			*peWhyNot = WHYNOTACCEPTINGLOGINS_PARTITION_SOFT_FULL;
		}
		return false;
	}

	if (peWhyNot)
	{
		*peWhyNot = WHYNOTACCEPTINGLOGINS_ALLOWED;
	}
	return true;
}


void NewMapTransfer_GameServerReportsPlayerBeganLogin(ContainerID iGameServerID, U32 uPartitionID)
{
	TrackedGameServerExe *pServer;

	if (stashIntFindPointer(sGameServerExesByID, iGameServerID, &pServer))
	{
		MapPartitionSummary *pPartition = GetPartitionFromServerAndID(pServer, uPartitionID);

		if (pPartition)
		{
			if (pPartition->iNumPlayersRecentlyLoggingIn)
			{
				pPartition->iNumPlayersRecentlyLoggingIn--;
				pPartition->iNumPlayers++;
			}
		}

		if (pServer->iNumPlayersRecentlyLoggingIn)
		{
			pServer->iNumPlayersRecentlyLoggingIn--;
			pServer->globalInfo.iNumPlayers++;
		}


	}
}


void NewMapTransfer_HereIsGSLGlobalInfo_ForMapManager(ContainerID iServerID, GameServerGlobalInfo *pInfo)
{
	TrackedGameServerExe *pServer;

	if (stashIntFindPointer(sGameServerExesByID, iServerID, &pServer))
	{
		int i;

		StructCopy(parse_GameServerGlobalInfo, pInfo, &pServer->globalInfo, 0, 0, TOK_EARRAY);

		for (i=0 ; i < eaSize(&pInfo->ppPartitions); i++)
		{
			MapPartitionSummary *pRefPartition = pInfo->ppPartitions[i];

			if (pRefPartition->uPartitionID)
			{
				MapPartitionSummary *pMyPartition = GetPartitionFromServerAndID(pServer, pRefPartition->uPartitionID);
				if (pMyPartition)
				{
					pMyPartition->iNumPlayers = pRefPartition->iNumPlayers;
				}
			}
		}
	}
}

char *NewMapTransfer_GetDebugTransferNotificationLinkString(TrackedGameServerExe *pServer)
{
	static char *pTemp = NULL;

	if (!pServer->pMachine)
	{
		estrPrintf(&pTemp, "(Unknown machine... something is wrong talk to Alex)");
	}
	

	estrPrintf(&pTemp, "<a href=\"cmd:OpenUrlCmd cryptic://vnc/%s\">%s</a>", 
		pServer->pMachine->pMachineName, pServer->pMachine->pMachineName);
	

	return pTemp;
}

void NewMapTransfer_HereIsControllerServerInfo(Controller_SingleServerInfo *pServerInfo)
{
	TrackedGameServerExe *pServer;

	if (stashIntFindPointer(sGameServerExesByID, pServerInfo->iGlobalID, &pServer))
	{
		if (!pServer->pid)
		{
			pServer->pid = pServerInfo->pid;
			NewMapTransfer_MaybeFulfillPendingDebugTransferNotifications(pServer);
		}
	}
}


static int siPartitionNoTimeoutAfterTransferPeriod = 30;
//a partition can't time out if it's gotten a map transfer within this many seconds
AUTO_CMD_INT(siPartitionNoTimeoutAfterTransferPeriod, PartitionNoTimeoutAfterTransferPeriod) ACMD_COMMANDLINE;

AUTO_COMMAND_REMOTE;
void IHaveTimedOutPartition(ContainerID iGameServerID, U32 uPartitionID)
{
	TrackedGameServerExe *pServer;
	MapPartitionSummary *pPartition = GetPartitionFromIDs(iGameServerID, uPartitionID, &pServer);
	
	if (!pPartition)
	{
		return;
	}

	if (pPartition->iLastTimeSendPlayerThere > timeSecondsSince2000() - siPartitionNoTimeoutAfterTransferPeriod)
	{
		return;
	}

	//for anything with only one partition, use server-level logic
	if (eaSize(&pServer->description.ppPartitions) == 1)
	{
		if (!NewMapTransfer_ServerCanDieIfItHasTimedOut(pServer))
		{
			return;
		}
	}

	NewMapTransfer_DestroyPartition(pServer, pPartition);

	RemoteCommand_PartitionShouldClose(GLOBALTYPE_GAMESERVER, iGameServerID, uPartitionID);

    if ( pServer->pList->pCategoryConfig->bAlwaysShutDownWhenLastPartitionCloses && ( eaSize(&pServer->description.ppPartitions) == 0 ) )
    {
        NewMapTransfer_KillGameServerDueToTimeoutIfAppropriate(iGameServerID);
    }
}

//used during transition from old-to-new code to convert old searches to new searches
void NewMapTransfer_FixupOldMapSearchInfo(MapSearchInfo *pSearchInfo, char *pReason)
{
	char *pErrorString = NULL;
	char *pAlertKey;
	ZoneMapType eActualType;
	char *pMapSearchString = NULL;

	if (!pSearchInfo || pSearchInfo->eSearchType != MAPSEARCHTYPE_UNSPECIFIED)
	{
		return;
	}

	if (pSearchInfo->developerAllStatic)
	{
		pSearchInfo->eSearchType = MAPSEARCHTYPE_ALL_FOR_DEBUGGING;
		return;
	}
	else if (pSearchInfo->newCharacter && !pSearchInfo->debugPosLogin)
	{
		if (pSearchInfo->bSkipTutorial)
		{
			pSearchInfo->eSearchType = MAPSEARCHTYPE_NEWPLAYER_SKIPTUTORIAL;
		}
		else
		{
			pSearchInfo->eSearchType = MAPSEARCHTYPE_NEWPLAYER;
		}
		return;
	}


	eActualType = MapCheckRequestedType(pSearchInfo->baseMapDescription.mapDescription, pSearchInfo->baseMapDescription.eMapType, &pErrorString, &pAlertKey);
			
	if (pErrorString)
	{
		

		if (pSearchInfo->iAccessLevel == 0)
		{
			ParserWriteText(&pMapSearchString, parse_MapSearchInfo, pSearchInfo, 0, 0, 0);
					
			ErrorOrAlert(allocAddString(pAlertKey), "NewMapTransfer_FixupOldMapSearchInfo got an error from MapCheckRequestedType. Error: %s. Reason: %s. MapSearchInfo: %s",
				pErrorString, pReason, pMapSearchString);

			estrDestroy(&pMapSearchString);
		}

		estrDestroy(&pErrorString);


	}

	pSearchInfo->baseMapDescription.eMapType = eActualType;

	switch (pSearchInfo->baseMapDescription.eMapType)
	{
	// UNSPEC here for test maps in development
	case ZMTYPE_UNSPECIFIED:
	case ZMTYPE_STATIC:
	case ZMTYPE_SHARED:

		if (pSearchInfo->baseMapDescription.containerID && pSearchInfo->baseMapDescription.iPartitionID)
		{
			pSearchInfo->eSearchType = MAPSEARCHTYPE_SPECIFIC_CONTAINER_AND_PARTITION_ID_OR_OTHER;
		}
		else		
			if (pSearchInfo->baseMapDescription.mapInstanceIndex)
		{
			pSearchInfo->eSearchType = MAPSEARCHTYPE_SPECIFIC_PUBLIC_INDEX_OR_OTHER;
		}
		else
		{
			pSearchInfo->eSearchType = MAPSEARCHTYPE_ONE_MAPNAME_ALL_CHOICES;
		}
		return;

	case ZMTYPE_MISSION:
	case ZMTYPE_OWNED:
	case ZMTYPE_PVP:
	case ZMTYPE_QUEUED_PVE:
		{
			bool teamNotRequired;
			bool guildOwned;
			if (pSearchInfo->iUGCProjectID)
			{
				teamNotRequired = false;
				guildOwned = false;
			}
			else
			{
				ZoneMapInfo *zmapInfo = worldGetZoneMapByPublicName(pSearchInfo->baseMapDescription.mapDescription);
				teamNotRequired = zmapInfoGetTeamNotRequired(zmapInfo);
				guildOwned = zmapInfoGetIsGuildOwned(zmapInfo);
			}
				
			//don't overwrite an existing team ID, for instance one from CSR
			if (!pSearchInfo->baseMapDescription.ownerType)
			{
				if (guildOwned)
				{
					// If we get here without a guild ID, then something went terribly wrong
					devassert(pSearchInfo->iGuildID);
					pSearchInfo->baseMapDescription.ownerType = GLOBALTYPE_GUILD;
					pSearchInfo->baseMapDescription.ownerID = pSearchInfo->iGuildID;
				}
				else if (pSearchInfo->teamID && !teamNotRequired)
				{
					pSearchInfo->baseMapDescription.ownerType = GLOBALTYPE_TEAM;
					pSearchInfo->baseMapDescription.ownerID = pSearchInfo->teamID;
				}
				else
				{
					pSearchInfo->baseMapDescription.ownerType = GLOBALTYPE_ENTITYPLAYER;
					pSearchInfo->baseMapDescription.ownerID = pSearchInfo->playerID;
				}
			}

			pSearchInfo->eSearchType = MAPSEARCHTYPE_OWNED_MAP;
			return;
		}

	}

	ParserWriteText(&pMapSearchString, parse_MapSearchInfo, pSearchInfo, 0, 0, 0);
			
	Errorf("Trying to convert map search %s into a new search, type is unsupported",
		pMapSearchString);
	estrDestroy(&pMapSearchString);


		
}

static int siNumOutstandingPreexistingMaps = 0;
static int siTotalPreexistingMaps = 0;

static void GetGameServerDescriptionForNewMapTransferCB(TransactionReturnVal *returnVal, void *userData)
{

	ContainerID iID = (ContainerID)(intptr_t)userData;
	TrackedGameServerExe *pServer;
	
	GSDescription_And_ZoneMapInfo *pDescriptionAndZMInfo;
	GameServerExe_Description *pDescription;
	ZoneMapInfo *pZoneMapInfo;

	enumTransactionOutcome eOutcome;
	bool bNeedToDestroy = false;
	static char *pNeedToDestroyReason = NULL;

	siNumOutstandingPreexistingMaps--;

	if (!(pServer = GetGameServerFromID(iID)))
	{
		log_printf(LOG_LOGIN, "Command %s returned for invalid map id %d", __FUNCTION__, iID);
		return;
	}

	if (pServer->eState != GSSTATE_PREEXISTING_WAITING_FOR_DESCRIPTION)
	{
		log_printf(LOG_LOGIN, "Command %s returned for map %d while in invalid state", __FUNCTION__, iID);
		return;
	}

	estrDestroy(&pNeedToDestroyReason);


	if (isProductionMode() && eMMLState != MML_STATE_SENT_STARTING_REMOTE_COMMANDS)
	{
		bNeedToDestroy = true;
		estrPrintf(&pNeedToDestroyReason, "Mapmanager got GetGameServerDescriptionForNewMapTransferCB in wrong state, presumably because it was too slow. We can't add new maps any more");
	}
	else
	{
		eOutcome = RemoteCommandCheck_gslGetGameServerDescriptionForNewMapTransfer(returnVal, &pDescriptionAndZMInfo);

		switch (eOutcome)
		{
		xcase TRANSACTION_OUTCOME_SUCCESS:
			pDescription = SAFE_MEMBER(pDescriptionAndZMInfo, pDescription);
			pZoneMapInfo = SAFE_MEMBER(pDescriptionAndZMInfo, pZoneMapInfo);

			//a running GS will always have a non-zero port num. So if this is zero, then the GS must not yet be running, so we treat it like a failed transaction
			if (!pDescription || !pZoneMapInfo || !pDescription->iListeningPortNum)
			{
				log_printf(LOG_LOGIN, "Command %s succeeded for map %u, but got back empty information, was the map right in the middle of starting up or something?", __FUNCTION__, iID);
				bNeedToDestroy = true;
				estrPrintf(&pNeedToDestroyReason, "GetGameServerDescriptionForNewMapTransferCB got back empty information");
			}
			else
			{
				//if we don't already know about our zonemapinfo, add it to our resource dictionary
				if (!worldGetZoneMapByPublicName(pZoneMapInfo->map_name))
				{
					//remove from parent so it doesn't get destroyed later
					pDescriptionAndZMInfo->pZoneMapInfo = NULL;
					RefSystem_AddReferent(g_ZoneMapDictionary, (char*)(pZoneMapInfo->map_name), pZoneMapInfo);
				}
				
				NewMapTransfer_RemoveGameServerFromList(pServer);

				StructDeInit(parse_GameServerExe_Description, &pServer->description);
				StructCopy(parse_GameServerExe_Description, pDescription, &pServer->description, 0, 0, TOK_EARRAY);
				NewMapTransfer_AddGameServerToList(pServer);

				FOR_EACH_IN_EARRAY(pDescription->ppPartitions, MapPartitionSummary, pPartition)
				{
					if (!NewMapTransfer_AddPreexistingPartitionToServer(pServer, pPartition, &pNeedToDestroyReason))
					{
						bNeedToDestroy = true;
						break;
					}

				}
				FOR_EACH_END				
			}

			StructDestroySafe(parse_GSDescription_And_ZoneMapInfo, &pDescriptionAndZMInfo);	
	
		xcase TRANSACTION_OUTCOME_FAILURE:
			log_printf(LOG_LOGIN, "Command %s failed for map %u for some reason", __FUNCTION__, iID);
			bNeedToDestroy = true;
			estrPrintf(&pNeedToDestroyReason, "GetGameServerDescriptionForNewMapTransferCB trans failed: %s", GetTransactionFailureString(returnVal));
		}
	}


	if (bNeedToDestroy)
	{
		RemoteCommand_KillServer(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_GAMESERVER, iID, pNeedToDestroyReason ?  pNeedToDestroyReason : "Unknown_GetGameServerDescriptionForNewMapTransferCB");
		RemoveAndDestroyGameServerExe(pServer, pNeedToDestroyReason ?  pNeedToDestroyReason : "Unknown_GetGameServerDescriptionForNewMapTransferCB");
	}
	else
	{
		pServer->eState = GSSTATE_RUNNING;

		//Now that is it loaded and running, init active activities and random active queues
		aslMapManager_MapInitActivities(pServer->iContainerID);
		aslMapManager_MapInitRandomActiveQueues(pServer->iContainerID);
	}

	
}
void NewMapTransfer_AddPreexistingMap(ContainerID iContainerID, char *pMachineName, U32 iIP, U32 iPublicIP, int iPid)
{
	GameServerExe_Description description = {0};
	TrackedGameServerExe *pServer;

	siNumOutstandingPreexistingMaps++;
	siTotalPreexistingMaps++;

	description.pMapDescription = allocAddString(CONST_MAPDESCRIPTION_PREEXISTING);

	description.eServerType = GSTYPE_PREEXISTING;

	pServer = CreateAndLinkTrackedGameServerExe(GSSTATE_PREEXISTING_WAITING_FOR_DESCRIPTION, &description, iContainerID);
	AddGameServerToMachine(pServer, pMachineName, iIP, iPublicIP);
	pServer->pid = iPid;

	RemoteCommand_gslGetGameServerDescriptionForNewMapTransfer( 
			objCreateManagedReturnVal(GetGameServerDescriptionForNewMapTransferCB, (void *)(intptr_t)iContainerID),
			GLOBALTYPE_GAMESERVER, iContainerID);
}

void NewMapTransfer_NonReadyPreexistingGameServerExists(ContainerID iContainerID)
{
	RemoteCommand_KillServer(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_GAMESERVER, 0, "Map that was not in gslRunning existed on MapManager startup (presumably due to mapmanager crash and restart). Killing it because we don't know what to do with it");
}

//after waiting this many seconds during a restart-of-crashed-mapmanager-in-live-shard, if a certain proportion of preexisting gameservers have reported in, just proceed with startup (will cause the rest of the preexisting gameservers to be killed)
static int siSecsToWaitBeforeNormalOperationFirstPass = 5;
AUTO_CMD_INT(siSecsToWaitBeforeNormalOperationFirstPass, SecsToWaitBeforeNormalOperationFirstPass);

//proportion of preexisting gameservers that have to have reported in by SecsToWaitBeforeNormalOperationFirstPass to allow MM to continue on
static float sfCutoffToProceedToNormalOperationFirstPass = 0.9f;
AUTO_CMD_FLOAT(sfCutoffToProceedToNormalOperationFirstPass, CutoffToProceedToNormalOperationFirstPass);

//after this many seconds during a restart-of-crashed-mapmanager-in-live-shard, just proceed no matter how many preexisting gameservers have 
//not been heard back from
static int siSecsToWaitBeforeNormalOperationSecondPass = 30;
AUTO_CMD_INT(siSecsToWaitBeforeNormalOperationSecondPass, SecsToWaitBeforeNormalOperationSecondPass);

bool NewMapTransfer_ReadyForNormalOperation(void)
{
	static U32 siFirstTimeChecked = 0;
	int iTimePassed;

	if (siNumOutstandingPreexistingMaps == 0 || siTotalPreexistingMaps == 0)
	{
		return true;
	}

	if (siFirstTimeChecked == 0)
	{
		siFirstTimeChecked = timeSecondsSince2000();
		return false;
	}

	iTimePassed = timeSecondsSince2000() - siFirstTimeChecked;

	if (iTimePassed > siSecsToWaitBeforeNormalOperationFirstPass && (siTotalPreexistingMaps - siNumOutstandingPreexistingMaps) > (float)siTotalPreexistingMaps * sfCutoffToProceedToNormalOperationFirstPass)
	{
		ErrorOrAlert("SOME_GS_MISSING_1", "After %d seconds MapManager has only heard from %d of %d preexisting running gameservers, but that's more than %f of them so we're proceeding",
			siSecsToWaitBeforeNormalOperationFirstPass, siTotalPreexistingMaps - siNumOutstandingPreexistingMaps, siTotalPreexistingMaps, sfCutoffToProceedToNormalOperationFirstPass);
		return true;
	}

	if (iTimePassed > siSecsToWaitBeforeNormalOperationSecondPass)
	{
		ErrorOrAlert("SOME_GS_MISSING_2", "After %d seconds MapManager has only heard from %d of %d preexisting running gameservers, giving up on the rest",
			siSecsToWaitBeforeNormalOperationSecondPass, siTotalPreexistingMaps - siNumOutstandingPreexistingMaps, siTotalPreexistingMaps);
		return true;
	}

	return false;
}

AUTO_COMMAND_REMOTE;
void PartitionOwnerChanged(ContainerID iServerID, U32 iPartitionID, GlobalType eNewOwnerType, ContainerID iNewOwnerID)
{
	MapPartitionSummary *pSummary = GetPartitionFromIDs(iServerID, iPartitionID, NULL);
	if (pSummary)
	{
		pSummary->eOwnerType = eNewOwnerType;
		pSummary->iOwnerID = iNewOwnerID;
		pSummary->bAssignedOpenInstance = false;
	}
}

void NewMapTransfer_CreatePreloadMap(MapCategoryConfig *pCategory)
{
	GameServerExe_Description description = {0};
	TrackedGameServerExe *pServer;
	char temp[256];
	sprintf(temp, "%s%s", CONST_MAPDESCRIPTION_PREFIX_PRELOAD, pCategory->pCategoryName);

	description.pMapDescription = allocAddString(temp);
	description.eServerType = GSTYPE_PRELOAD;

	pServer = NewMapTransfer_LaunchNewServer(&description, "Preload", NULL, NULL, NULL, true);

	ea32Push(&pCategory->pPreloadMapContainerIDs, pServer->iContainerID);
}

void NewMapTransfer_CheckForPreloadMaps(void)
{
	TrackedGameServerExe *pServer;

	FOR_EACH_IN_STASHTABLE(sCategoriesByCategoryNamePointer, MapCategoryConfig, pCategory)
	{
		if (pCategory->iNumPreloadMaps)
		{
			int i;

			for (i = ea32Size(&pCategory->pPreloadMapContainerIDs) - 1; i >= 0; i--)
			{
				pServer = GetGameServerFromID(pCategory->pPreloadMapContainerIDs[i]);
				if (!pServer || pServer->eState != GSSTATE_PRELOAD_WAITING_FOR_HANDSHAKE && pServer->eState != GSSTATE_PRELOAD && pServer->eState != GSSTATE_LAUNCH_REQUESTED_FROM_CONTROLLER)
				{
					ea32RemoveFast(&pCategory->pPreloadMapContainerIDs, i);
				}
			}

			while (ea32Size(&pCategory->pPreloadMapContainerIDs) < pCategory->iNumPreloadMaps)
			{
				NewMapTransfer_CreatePreloadMap(pCategory);
			}
		}
	}
	FOR_EACH_END;
}

void NewMapTransfer_DoStartingMaps(void)
{
	int i;
	int j;

	GameServerExe_Description desc = {0};

	for (i=0; i < eaSize(&gMapManagerConfig.ppStartingMaps); i++)
	{
		desc.eServerType = GSTYPE_NORMAL;
		desc.pMapDescription = allocAddString(gMapManagerConfig.ppStartingMaps[i]);

		NewMapTransfer_LaunchNewServer(&desc, "Starting Map defined in MapManagerConfig", NULL, NULL, NULL, false);
	}

	for (i=0; i < eaSize(&gMapManagerConfig.ppMinMapCounts); i++)
	{
		desc.eServerType = GSTYPE_NORMAL;
		desc.pMapDescription = allocAddString(gMapManagerConfig.ppMinMapCounts[i]->pMapName);
			
		for (j=0; j < gMapManagerConfig.ppMinMapCounts[i]->iMinCount; j++)
		{
			NewMapTransfer_LaunchNewServer(&desc, "Startup min map counts as defined in MapManagerConfig", NULL, NULL, NULL, false);
		}
	}
}

bool NewMapTransfer_ListWantsNewLaunchDueToMapLaunchCutoff(GameServerList *pList, ContainerID iIDToIgnore)
{
	int i;
	bool bFoundASmallOne = false;

	if (!gMapManagerState.bDoLaunchCutoffs)
	{
		return false;
	}

	if (!pList->pCategoryConfig || !pList->pCategoryConfig->iNewMapLaunchCutoff)
	{
		return false;
	}

	if (pList->eType != LISTTYPE_NORMAL)
	{
		return false;
	}

	for (i = 0; i < eaSize(&pList->ppGameServers); i++)
	{
		TrackedGameServerExe *pServer = pList->ppGameServers[i];
		if (pServer->iContainerID != iIDToIgnore && !pServer->bToldToDie)
		{
			if (NewMapTransfer_UsefulNumPlayers(pServer) < pList->pCategoryConfig->iNewMapLaunchCutoff)
			{
				bFoundASmallOne = true;
				break;
			}
		}
	}

	return !bFoundASmallOne;
}

void NewMapTransfer_CheckForLaunchCutoffs(void)
{
	if (!gMapManagerState.bDoLaunchCutoffs)
	{
		return;
	}

	FOR_EACH_IN_STASHTABLE(sGameServerListsByMapDescription, GameServerList, pList)
	{
		if (pList->eType == LISTTYPE_NORMAL && pList->pCategoryConfig && pList->pCategoryConfig->iNewMapLaunchCutoff)
		{			
			if (gMapManagerState.bLaunchCutoffsIgnoreNonexistant && eaSize(&pList->ppGameServers) == 0)
			{
				continue;
			}

			if (NewMapTransfer_ListWantsNewLaunchDueToMapLaunchCutoff(pList, 0))
			{
				GameServerExe_Description desc = {0};
				char *pComment = NULL;

				estrPrintf(&pComment, "%d %s servers already exist, but all have >= %d players, so NewMapLaunchCutoff triggering",
					eaSize(&pList->ppGameServers), pList->pMapDescription, pList->pCategoryConfig->iNewMapLaunchCutoff); 

				desc.eServerType = GSTYPE_NORMAL;
				desc.pMapDescription = pList->pMapDescription;

				NewMapTransfer_LaunchNewServer(&desc, pComment, NULL, NULL, NULL, false);

				estrDestroy(&pComment);
			}
		}
	}
	FOR_EACH_END;
}


bool NewMapTransfer_ServerCanDieIfItHasTimedOut(TrackedGameServerExe *pServer)
{
	if (!pServer->pList || !pServer->pList->pCategoryConfig)
	{
		return true;
	}

	//if a server is locked, make it stay around forever for debugging or whatnot
	if (pServer->bLocked)
	{
		return false;
	}

	//two reasons why a server might not die... either there's a min map count, or else there's a new launch cutoff
	if (pServer->pList->pMinMapCount && eaSize(&pServer->pList->ppGameServers) <= pServer->pList->pMinMapCount->iMinCount)
	{
		return false;
	}

	if (pServer->pList->pCategoryConfig->iNewMapLaunchCutoff && NewMapTransfer_ListWantsNewLaunchDueToMapLaunchCutoff(pServer->pList, pServer->iContainerID))
	{
		return false;
	}

	return true;
}


void NewMapTransfer_KillGameServerDueToTimeoutIfAppropriate(ContainerID iServerID)
{
	TrackedGameServerExe *pServer = GetGameServerFromID(iServerID);
	if (!pServer)
	{
		return;
	}

	if (NewMapTransfer_ServerCanDieIfItHasTimedOut(pServer))
	{
		RemoteCommand_KillYourselfGracefully(GLOBALTYPE_GAMESERVER, iServerID);
		pServer->bToldToDie = true;
		return;
	}


}


void NewMapTransfer_LogPlayerWasSentToMap(TrackedGameServerExe *pServer, ContainerID iPlayerID)
{
	//iPlayerID is 0 when queued maps are being created due to how the PVP queues work
	if (iPlayerID)
	{
		U32 iCurTime = timeSecondsSince2000();
		PlayerWasSentToMapLog *pLog = StructCreate(parse_PlayerWasSentToMapLog);
		int i;

		//check for a log that already says this guy was sent here earlier, remove it if found
		for (i=eaSize(&pServer->ppPlayersSentHere)-1; i >=0; i--)
		{
			if (pServer->ppPlayersSentHere[i]->iPlayerContainerID == iPlayerID)
			{
				StructDestroy(parse_PlayerWasSentToMapLog, pServer->ppPlayersSentHere[i]);
				eaRemove(&pServer->ppPlayersSentHere, i);
			}
		}
		
		pLog->iPlayerContainerID = iPlayerID;
		pLog->iTime = iCurTime;

		eaPush(&pServer->ppPlayersSentHere, pLog);

		while (pServer->ppPlayersSentHere[0]->iTime < iCurTime - PSTCM_PLAYER_SENT_TO_MAP_INTERVAL)
		{
			StructDestroy(parse_PlayerWasSentToMapLog, pServer->ppPlayersSentHere[0]);
			eaRemove(&pServer->ppPlayersSentHere, 0);
		}
	}
	
}

void NewMapTransfer_DecayAllRecentlyLogginInCounts(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	FOR_EACH_IN_STASHTABLE(sGameServerListsByMapDescription, GameServerList, pList)
	{
		FOR_EACH_IN_EARRAY(pList->ppGameServers, TrackedGameServerExe, pServer)
		{
			if (pServer->eState == GSSTATE_RUNNING)
			{
				if (pServer->iNumPlayersRecentlyLoggingIn)
				{
					pServer->iNumPlayersRecentlyLoggingIn--;
				}
			}
		}
		FOR_EACH_END;

		FOR_EACH_PARTITION_IN_LIST(pList, pPartition, pServer)
		{
			if (pServer->eState == GSSTATE_RUNNING)
			{
				if (pPartition->iNumPlayersRecentlyLoggingIn)
				{
					pPartition->iNumPlayersRecentlyLoggingIn--;
				}
			}
		}
		FOR_EACH_PARTITION_END;
	}
	FOR_EACH_END;
}

AUTO_COMMAND_REMOTE;
void LockGameServer(ContainerID iID, bool bLock)
{
	TrackedGameServerExe *pServer = GetGameServerFromID(iID);
	if (!pServer)
	{
		return;
	}

	pServer->bLocked = bLock;
}

AUTO_COMMAND;
void KillAllUGCPlayMaps(void)
{
	FOR_EACH_IN_STASHTABLE(sGameServerExesByID, TrackedGameServerExe, pServer)
	{
		if (pServer->description.eServerType == GSTYPE_UGC_PLAY)
		{
			RemoteCommand_KillServer(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_GAMESERVER, pServer->iContainerID, "KillAllUGCPlayMaps");
			RemoveAndDestroyGameServerExe(pServer, "KillAllUGCPlayMaps");
		}
	}
	FOR_EACH_END;
}


AUTO_COMMAND;
void KillAllUGCEditMaps(void)
{
	FOR_EACH_IN_STASHTABLE(sGameServerExesByID, TrackedGameServerExe, pServer)
	{
		if (pServer->description.eServerType == GSTYPE_UGC_EDIT)
		{
			RemoteCommand_KillServer(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_GAMESERVER, pServer->iContainerID, "KillAllUGCEditMaps");
			RemoveAndDestroyGameServerExe(pServer, "KillAllUGCEditMaps");
		}
	}
	FOR_EACH_END;
}


AUTO_COMMAND;
void CreateNewPartition(ContainerID iID)
{
	TrackedGameServerExe *pServer;

	if (stashIntFindPointer(sGameServerExesByID, iID, &pServer))
	{
		AddPartitionSummaryInternal(pServer, NULL, 0, 0, 0);


	}
}

//returns a summary string of everything that was done... certain GS types can't be locked, so just kill them
AUTO_COMMAND_REMOTE;
char *LockAllGameserversOnOneMachine(char *pMachineName, int iLock)
{
	static char *spRetString = NULL;
	int iNumLocked = 0;
	int iNumKilled = 0;
	int iPlayersBooted = 0;
	MachineForGameServers *pMachine;
	TrackedGameServerExe **ppGameServers = NULL;

	estrClear(&spRetString);

	if (!stashFindPointer(sGameServerMachinesByMachineName, allocAddString(pMachineName), &pMachine))
	{
		estrPrintf(&spRetString, "MapManager was asked to lock or kill all gameservers on %s, but that machine doesn't have any gameservers",
			pMachineName);
		return spRetString;
	}

	eaCopy(&ppGameServers, &pMachine->ppGameServers);

	FOR_EACH_IN_EARRAY(ppGameServers, TrackedGameServerExe, pServer)
	{
		if (iLock)
		{

			if (MapDescription_MapTypeSupportsLockingWithoutKilling(pServer->description.eMapType))
			{
				iNumLocked++;
				pServer->bLocked = true;
			}
			else
			{
				iNumKilled++;
				iPlayersBooted += NewMapTransfer_UsefulNumPlayers(pServer);
				estrConcatf(&spRetString, "Killing GS %u (%s) (%s), with %d players\n",
					pServer->iContainerID, StaticDefineInt_FastIntToString(ZoneMapTypeEnum, pServer->description.eMapType),
					pServer->description.pMapDescription, NewMapTransfer_UsefulNumPlayers(pServer));
				RemoteCommand_KillServer(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_GAMESERVER, pServer->iContainerID, "LockAllGameserversOnOneMachine");
				RemoveAndDestroyGameServerExe(pServer, "LockAllGameserversOnOneMachine");
			}
		}
		else
		{
			iNumLocked++;
			pServer->bLocked = false;
		}
	}
	FOR_EACH_END;
	eaDestroy(&ppGameServers);

	if (iLock)
	{
		estrInsertf(&spRetString, 0, "LockAllGameserversOnOneMachine called on %s. Locked %d servers. Killed %d servers, booting %d players:\n",
			pMachineName, iNumLocked, iNumKilled, iPlayersBooted);
	}
	else
	{
		estrPrintf(&spRetString, "UNLOCK: LockAllGameserversOnOneMachine called on %s. Unlocked %d servers.\n",
			pMachineName, iNumLocked);
	}

	return spRetString;
}

#include "aslMapManagerNewMapTransfer_Private_h_ast.c"