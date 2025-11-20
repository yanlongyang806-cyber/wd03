/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AppServerLib.h"
#include "aslMapManager.h"
#include "aslMapManager_cmd.h"
#include "aslUGCDataManagerProject.h"
#include "autogen/controller_autogen_remotefuncs.h"
#include "autogen/serverlib_autogen_remotefuncs.h"
#include "../../core/controller/pub/controllerpub.h"
#include "autogen/gameserverlib_autogen_remotefuncs.h"
#include "autogen/aslmapmanager_h_ast.h"
#include "autogen/AppServerLib_autogen_slowfuncs.h"
#include "WorldGrid.h"
#include "logcomm.h"
#include "mechanics_common.h"
#include "serverlib.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "aslMapManagerConfig_h_ast.h"
#include "MapDescription_h_ast.h"
#include "GlobalEnums_h_ast.h"
#include "TimedCallback.h"
#include "AutoStartupSupport.h"
#include "team.h"
#include "earray.h"
#include "Entity.h"
#include "guild.h"
#include "winutil.h"
#include "alerts.h"
#include "HttpXPathSupport.h"
#include "ConsoleDebug.h"
#include "accountNet.h"
#include "Sock.h"
#include "EntityLib.h"
#include "entCritter.h"
#include "AslMapManager_c_ast.h"
#include "uuid.h"
#include "aslPatching.h"
#include "continuousBuilderSupport.h"
#include "AslMapManagerVirtualShard.h"
#include "resourceDBSupport.h"
#include "ugcProjectUtils.h"
#include "UGCCommon.h"
#include "aslMapManagerNewMapTransfer.h"
#include "aslMapMAnagerNewMapTransfer_GetChoices.h"
#include "aslMapManagerNewMapTransfer_InnerStructs.h"
#include "aslMapManagerNewMapTransfer_Private.h"
#include "aslMapManagerNewMapTransfer_Private_h_ast.h"
#include "aslMapManagerActivity.h"
#include "aslMapManagerrandomQueueRelay.h"
#include "../../crossroads/gameserverlib/gslPartition.h"

#include "staticworld/worldGridPrivate.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/mechanics_common_h_ast.h"
#include "AutoGen/svrGlobalInfo_h_ast.h"
#include "AutoGen/wlGenesis_h_ast.h"
#include "AutoGen/WorldGrid_h_ast.h"
#include "AutoGen/WorldGridPrivate_h_ast.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "aslMapManagerNewMapTransfer_GetAddress.h"
#include "AutoGen/ActivityCommon_h_ast.h"

#define	PHYSX_SRC_FOLDER "../../3rdparty"
#include "PhysicsSDK.h"
#include "UGCProjectCommon.h"
#include "UGCProjectCommon_h_ast.h"
#include "aslUGCDataManagerWhatsHot.h"
#include "utilitiesLib.h"
#include "objContainer.h"
#include "ShardVariableCommon.h"
#include "AutoGen/ShardVariableCommon_h_ast.h"
#include "ShardCommon.h"
#include "zutils.h"
#include "../../serverlib/objects/ServerLibPrefStore.h"

void aslMapManagerCheckForLaunchCutoffs(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);
void SendPartitionInfoAndWaitForMapTransfer(TrackedMap *pServer, int iRequestID, ContainerID iEntContainerID, MapPartitionSummary *pInfo);
void aslMapManagerCheckMapInfoCaches(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData);
MapPartitionSummary *GetPartitionSummary(TrackedMap *pMap, U32 uPartitionID);

void GetNextBlockOfIDsComplete(bool bSucceeded, char *pRetVal, void *pUserData);

static bool sbMapManagerSubscribeToPlayers = false;
AUTO_CMD_INT(sbMapManagerSubscribeToPlayers, MapManagerSubscribeToPlayers);

static bool sbMapManagerSubscribeToTeamsAndGuilds = false;
AUTO_CMD_INT(sbMapManagerSubscribeToTeamsAndGuilds, MapManagerSubscribeToTeamsAndGuilds);


typedef struct ShardVariableServerInitContext 
{
	// Indicates whether the map manager acquired the ownership of shard variable containers
	U32 bAcquiredShardVariableContainers : 1;

	// Indicates whether the server asked the ObjectDB to create the shard variable containers
	U32 bAskedObjectDBToCreateContainers : 1;

	// Indicates whether the shard variable container exists in the ObjectDB
	U32 bShardVariableMapRequestedContainerExists : 1;
	U32 bShardVariableBroadcastContainerExists : 1;

} ShardVariableServerInitContext;

static ShardVariableServerInitContext shardVariableServerInitContext = { 0 };

typedef struct EventServerInitContext
{
	// Indicates whether the map manager acquired the ownership of event variable containers
	U32 bAcquiredEventVariableContainers : 1;

	// Indicates whether the server asked the ObjectDB to create the event variable container
	U32 bAskedObjectDBToCreateContainer : 1;

	// Indicates whether the event variable container exists in the ObjectDB
	U32 bEventContainerExists : 1;
}EventServerInitContext;

static EventServerInitContext eventServerInitContext = { 0 };

static int siTimer = 0;
bool bRequestSent;

MapManagerState gMapManagerState = {0};
GlobalMapManagerConfig gMapManagerConfig = {0};

StashTable sCategoriesByCategoryNamePointer = NULL;
static StashTable sTrackedMapListsByName = NULL;
static StashTable sPartitionHandshakesByID = NULL;

bool gbGotBlockOfIDs = false;
bool gbGotServerList = false;

bool gbDebugTransferNotifications = false;
AUTO_CMD_INT(gbDebugTransferNotifications, DebugTransferNotificaitons);


//a list of IDs that the object db thinks are probably OK for game servers. 
U32 *pPotentialGameServerIDs = NULL;

AUTO_CMD_INT(gMapManagerState.bDoStartingMaps, DoStartingMaps) ACMD_COMMANDLINE;
AUTO_CMD_INT(gMapManagerState.bDoLaunchCutoffs, DoLaunchCutoffs) ACMD_COMMANDLINE;
AUTO_CMD_INT(gMapManagerState.bLaunchCutoffsIgnoreNonexistant, LaunchCutoffsIgnoreNonexistant) ACMD_COMMANDLINE;
AUTO_CMD_INT(gMapManagerState.bAllowSkipTutoral, AllowSkipTutorial) ACMD_COMMANDLINE;

// turn on logging of port assignments
bool gLogPorts = false;
AUTO_CMD_INT(gLogPorts, LogPorts) ACMD_COMMANDLINE;


#define DECREMENT_RECENT_LOGIN_COUNT_DELAY (60.0f) 
#define CHECK_FOR_LAUNCH_CUTOFFS_DELAY (15.0f)

#define CHECK_FOR_PRELOAD_DELAY (5.0f)

enumMapManagerLibState eMMLState = MML_STATE_INIT;


void InformMapManagerOfGameServerDeath(ContainerID iContainerID, bool bCrashOrAssert);
MinMapCountConfig *FindMinMapCountConfigForMap(TrackedMap *pMap);


MapCategoryConfig *FindCategoryByNameAndType(const char *pMapName, ZoneMapType eType, char **ppOutSingleCategoryName /*NOT AN ESTRING*/);

void FixupConfigStuffForMap(TrackedMap *pMap);

//every time the map manager starts up, it requests a number of free game server IDs from the object DB, which basically involves
//the object DB incrementing it's "next free game server ID" variable by that amount. Then when fewer than half that many
//are availalble, it asks for more. This will fail only if n/2 game servers are created in the time it takes a message to get
//to the object DB and back, which is very unlikely.
#define NUM_GAMESERVER_IDS_TO_GRAB_AT_ONCE 4000



SingleMapConfig *FindSingleMapConfig(const char *pName)
{
	int i;

	for (i=0; i < eaSize(&gMapManagerConfig.ppSingleMaps); i++)
	{
		if (stricmp(gMapManagerConfig.ppSingleMaps[i]->pPublicName, pName) == 0)
		{
			return gMapManagerConfig.ppSingleMaps[i];
		}
	}

	return NULL;
}

MapCategoryConfig *FindCategoryByName(const char *pName)
{
	MapCategoryConfig *pConfig = NULL;

	stashFindPointer(sCategoriesByCategoryNamePointer, pName, &pConfig);

	return pConfig;
}

typedef struct SingleMachineUnusedPortList
{
	U32 *pUnusedPorts; //ea32
	bool bCleanupArray[MAX_GAMESERVER_PORT - STARTING_GAMESERVER_PORT + 1];
} SingleMachineUnusedPortList;

static StashTable sUnusedPortsByIP = NULL;




SingleMachineUnusedPortList *GetPortListForIP(U32 iIP)
{
	SingleMachineUnusedPortList *pPortList = NULL;

	if (!sUnusedPortsByIP)
	{
		sUnusedPortsByIP = stashTableCreateInt(16);
	}

	if (!stashIntFindPointer(sUnusedPortsByIP, iIP, &pPortList))
	{
		int i;
		pPortList = calloc(sizeof(SingleMachineUnusedPortList), 1);

		for (i=STARTING_GAMESERVER_PORT; i <= MAX_GAMESERVER_PORT; i++)
		{
			ea32Push(&pPortList->pUnusedPorts, i);
		}

		stashIntAddPointer(sUnusedPortsByIP, iIP, pPortList, false);
	}

	return pPortList;
}

static int siMinGameServerID = 0;
static int siGameServerIDRange = 0;

AUTO_COMMAND;
void SetGameServerIDRange(int iMinLegalValue, int iMaxLegalValue)
{
	siMinGameServerID = iMinLegalValue;
	siGameServerIDRange = iMaxLegalValue - iMinLegalValue + 1;
}


ContainerID GetNextGameServerID(void)
{
	U32 iRetVal;

	while (1)
	{
		U32 iNextPotential;

		//TODO make this a queue if performance is an issue
		assertmsg(ea32Size(&pPotentialGameServerIDs), "Map manager ran out of game server IDs... did it lose contact with the object DB in some crazy way?");
		iNextPotential = pPotentialGameServerIDs[0];
		ea32Remove(&pPotentialGameServerIDs, 0);

		if (!NewMapTransfer_IsHandlingServer(iNextPotential))
		{
			iRetVal = iNextPotential;
			break;
		}
	}

	if (ea32Size(&pPotentialGameServerIDs) < NUM_GAMESERVER_IDS_TO_GRAB_AT_ONCE / 2)
	{
		PrefStore_AtomicAddAndGet("NextGameServerID", NUM_GAMESERVER_IDS_TO_GRAB_AT_ONCE,
			GetNextBlockOfIDsComplete, NULL);
	}

	if (siMinGameServerID)
	{
		iRetVal = iRetVal % siGameServerIDRange + siMinGameServerID;
	}

	return iRetVal;
}


int FindUnusedPort(U32 iIP)
{
	int iRetVal;
	SingleMachineUnusedPortList *pPortList = GetPortListForIP(iIP);

	if (!ea32Size(&pPortList->pUnusedPorts))
	{
		return 0;
	}

	iRetVal = pPortList->pUnusedPorts[0];
	ea32Remove(&pPortList->pUnusedPorts, 0);
	return iRetVal;
}


MapCategoryConfig *FindCategoryByNameAndType(const char *pMapName, ZoneMapType eType, char **ppOutSingleCategoryName /*NOT AN ESTRING*/)
{
	MapCategoryConfig *pCategory = NULL;

	SingleMapConfig *pSingleMapConfig = FindSingleMapConfig(pMapName);

	if (pSingleMapConfig)
	{
		if (ppOutSingleCategoryName)
		{
			*ppOutSingleCategoryName = pSingleMapConfig->pCategoryName;
		}
		pCategory = FindCategoryByName(pSingleMapConfig->pCategoryName);
		
	}
	else
	{
		char temp[256];

		if (eType != ZMTYPE_UNSPECIFIED)
		{
			sprintf(temp, "default_%s", StaticDefineIntRevLookup(ZoneMapTypeEnum, eType));
			pCategory = FindCategoryByName(allocAddString(temp));
		}

		if (!pCategory)
		{
			pCategory = FindCategoryByName(allocAddString("default"));
			if (!pCategory)
			{
				ErrorOrAlert("NO_DEFAULT_CATEGORY", "The map manager config file seems to have no default map category. Please fix this.");
			}
		}

	}

	return pCategory;

}






void aslMapManagerCheckForPreloadMaps( TimedCallback *callback, F32 timeSinceLastCallback, UserData userData )
{
	NewMapTransfer_CheckForPreloadMaps();
}

bool DEFAULT_LATELINK_MapNameShouldBeExcludedFromMapNameFileForMCP(const char *pMapName)
{
	return false;
}

//runs once right as normal operations begin
void MapManagerLibStartedNormalOperation(void)
{
	//find all public map names, create a map list for each one
	ResourceIterator iterator;
	char *pMapName;
	ZoneMapInfo *pZoneMap;
	FILE *pMapNameFile = NULL;

	resInitIterator("ZoneMap", &iterator);

	//in dev mode, we always write out a list of maps to localdata, so the MCP can read it and 
	//present the options to designers
	if (isDevelopmentMode())
	{
		char fname[CRYPTIC_MAX_PATH];
		sprintf(fname, "%s/mapname.txt", fileLocalDataDir());
		pMapNameFile = fopen(fname, "wt");
	}


	while (resIteratorGetNext(&iterator, &pMapName, &pZoneMap))
	{
		if (pMapNameFile)
		{
			if (!MapNameShouldBeExcludedFromMapNameFileForMCP(pMapName))
			{
				fprintf(pMapNameFile, "%s\n", pMapName);
			}
		}
	}
	resFreeIterator(&iterator);

	if (pMapNameFile)
	{
		fclose(pMapNameFile);
	}

	aslMapManagerCheckForPreloadMaps(NULL, 0, NULL);
	TimedCallback_Add(aslMapManagerCheckForPreloadMaps, NULL, CHECK_FOR_PRELOAD_DELAY);

	TimedCallback_Add(aslMapManagerCheckMapInfoCaches, NULL, 1.0f);

	TimedCallback_Add(EditQueue_OncePerSecond, NULL, 1.0f);

	TimedCallback_Add(aslMapManagerCheckForLaunchCutoffs, NULL, CHECK_FOR_LAUNCH_CUTOFFS_DELAY);

	TimedCallback_Add(NewMapTransfer_DecayAllRecentlyLogginInCounts, NULL,  DECREMENT_RECENT_LOGIN_COUNT_DELAY);

	aslMapManagerVirtualShards_StartNormalOperation();

	NewMapTransfer_BeginNormalOperation();
}



static void GetServerListComplete(TransactionReturnVal *returnVal, void *userData)
{
	Controller_ServerList *pListFromController;
	enumTransactionOutcome eOutcome;
	static int iFailureCount = 0;

	eOutcome = RemoteCommandCheck_GetServerList(returnVal, &pListFromController);

	if (eOutcome == TRANSACTION_OUTCOME_FAILURE)
	{
		Errorf("GetServerList failed. Error string: %s", GetTransactionFailureString(returnVal));

		iFailureCount++;
		assertmsgf(iFailureCount < 10, "Map manager repeatedly couldn't get server list from controller, most recently because: %s", GetTransactionFailureString(returnVal));
		bRequestSent = false;
		timerStart(siTimer);
	}

	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		int i;

		gbGotServerList = true;
		timerStart(siTimer);
		bRequestSent = false;
		
		for (i=0; i < eaSize(&pListFromController->ppServers); i++)
		{
			if (NewMapTransfer_IsHandlingServer(pListFromController->ppServers[i]->iGlobalID))
			{
				NewMapTransfer_HereIsControllerServerInfo(pListFromController->ppServers[i]);
				continue;
			}

			if (strstr(pListFromController->ppServers[i]->stateString, "gslRunning"))
			{

					log_printf(LOG_LOGIN, "controller informed us of previously unknown map %d\n", pListFromController->ppServers[i]->iGlobalID);

				NewMapTransfer_AddPreexistingMap(
					pListFromController->ppServers[i]->iGlobalID,
					pListFromController->ppServers[i]->machineName,
					pListFromController->ppServers[i]->iIP,
					pListFromController->ppServers[i]->iPublicIP,
					pListFromController->ppServers[i]->pid);
	
			
			}
			else
			{
				
				NewMapTransfer_NonReadyPreexistingGameServerExists(pListFromController->ppServers[i]->iGlobalID);
				
			}
		}

		StructDestroy(parse_Controller_ServerList, pListFromController);

	}
}


void GetNextBlockOfIDsComplete(bool bSucceeded, char *pRetVal, void *pUserData)
{
	U32 iRetVal;
	U32 i;

	if (!bSucceeded || !StringToUint(pRetVal, &iRetVal))
	{
		assertmsg(0, "Map manager couldn't starting game server ID from object DB");
	}



	gbGotBlockOfIDs = true;

	for (i = iRetVal - NUM_GAMESERVER_IDS_TO_GRAB_AT_ONCE; i != iRetVal; i++)
	{
		if (i && i < LOWEST_SPECIAL_CONTAINERID)
		{
			ea32Push(&pPotentialGameServerIDs, i);
		}
		}

}





void aslMapManagerCheckForLaunchCutoffs(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	NewMapTransfer_CheckForLaunchCutoffs();
}

static void MapSendDoorDestinationStatusRequests(TrackedGameServerExe* pServer)
{
	bool bChanged = false;
	int i;
	bool bServerIsAcceptingLogins;

	if (!pServer->pDestMapStatusRequests)
		return;

	bServerIsAcceptingLogins = NewMapTransfer_GameServerIsAcceptingLogins(pServer, false, NULL, NULL);

	// Iterate the maps summary requests for this map
	for (i = eaSize(&pServer->pDestMapStatusRequests->eaList)-1; i >= 0; i--)
	{
		GameServerList *pList;
		MapSummary* pData;
		
		// Clone the data and clear the numbers
		pData = StructClone(parse_MapSummary, pServer->pDestMapStatusRequests->eaList[i]);
		if (!pData)
			continue;
		mechanics_ResetCalculatedMapSummaryData(pData); 

		// Find the list of matching maps and then iterate if any are found
		// to collect the numbers for this map type
		stashFindPointer(sGameServerListsByMapDescription, pData->pchMapName, &pList);
		if (pList)
		{
			FOR_EACH_PARTITION_IN_LIST(pList, pPartition, pCurrServer)
			{
				if (stricmp(pData->pchMapVars, pPartition->pMapVariables) == 0)
				{
					pData->iNumInstances++;
					pData->iNumPlayers += pPartition->iNumPlayers;
					
					if (bServerIsAcceptingLogins && NewMapTransfer_PartitionIsAcceptingLogins(pCurrServer, pPartition, false, NULL, NULL))
					{
						pData->iNumNonFullInstances++;
					}
					if (pCurrServer->description.eMapType == ZMTYPE_MISSION &&
						pCurrServer->globalInfo.bEnableOpenInstancing)
					{
						pData->iNumEnabledOpenInstancing++;
					}
				}
			}
			FOR_EACH_PARTITION_END
		}

		// Check if the data changed this frame
		if (StructCompare(parse_MapSummary, pData, pServer->pDestMapStatusRequests->eaList[i], 0, 0, 0) == 0) 
		{
			StructDestroy(parse_MapSummary, pData);
		} 
		else 
		{
			StructDestroy(parse_MapSummary, pServer->pDestMapStatusRequests->eaList[i]);
			pServer->pDestMapStatusRequests->eaList[i] = pData;
			bChanged = true;
		}
	}

	// If any data changed, send updates to interested game server
	if (bChanged)
	{
		RemoteCommand_SendDoorDestinationStatusRequestsToServer(GLOBALTYPE_GAMESERVER,pServer->iContainerID,pServer->pDestMapStatusRequests);
	}
}

#define SECS_BETWEEN_MAP_REQUEST_UPDATES  5

static void MapManagerProcessDoorDestinationStatusRequests(F32 fElapsed)
{
	static U32 s_uPrevTime = 0;
	U32 uCurrentTime = timeSecondsSince2000();
	int i;

	// Only check again once a second
	if (uCurrentTime == s_uPrevTime)
	{
		return;
	}
	s_uPrevTime = uCurrentTime;

	// Iterate the maps that are interested in updates
	for(i=ea32Size(&g_piDoorDestinationRequestMapIDs)-1; i>=0; --i) 
	{
		TrackedGameServerExe* pServer;
		U32 uiID = g_piDoorDestinationRequestMapIDs[i];

		if (stashIntFindPointer(sGameServerExesByID, uiID, &pServer))
		{
			// If are past sample time, send the status update
			if (pServer->uDestMapSampleTimestamp < uCurrentTime)
			{
				MapSendDoorDestinationStatusRequests(pServer);
				pServer->uDestMapSampleTimestamp = uCurrentTime + SECS_BETWEEN_MAP_REQUEST_UPDATES;
			}
		}
		else
		{
			// Map was not found, so stop checking for it
			ea32FindAndRemove(&g_piDoorDestinationRequestMapIDs, uiID);
		}
	}
}

void MapManagerLibNormalOperation(F32 fElapsed)
{
	NewMapTransfer_NormalOperation();

	aslMapManagerVirtualShards_NormalOperation();

	MapManagerProcessDoorDestinationStatusRequests(fElapsed);

	if (timerElapsed(siTimer) > 1.0f && !bRequestSent && !isProductionMode())
	{
		RemoteCommand_GetServerList( objCreateManagedReturnVal(GetServerListComplete, NULL),
			GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_GAMESERVER);
		bRequestSent = true;
	}

	aslMapManager_PlayableNameSpace_NormalOperation();
}

///////////////////////////////////////////////////////////////////////////////
//  Shard Variable Container set up. There are two. One is used for map-based
//    frequently-changed vars. The other is used for broadcast seldom-changed
//    vars.

// This CB is on acquiring ALL containers
static void MapManagerLibAcquireShardVariableContainersComplete_CB(void)
{
	shardVariableServerInitContext.bAcquiredShardVariableContainers = true;
}

static void MapManagerLibCreateShardVariableContainer_CB(TransactionReturnVal *pReturn, ContainerID* puID)
{
	if (puID!=NULL && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		if (*puID == SHARD_VAR_MAPREQUESTED_CONTAINER_ID)
		{
			shardVariableServerInitContext.bShardVariableMapRequestedContainerExists = true;
		}
		else
		{
			shardVariableServerInitContext.bShardVariableBroadcastContainerExists = true;
		}
		resReRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_SHARDVARIABLE));
	} 
	else 
	{
        TriggerAlert("MAPMANAGER_SHARDVARIABLE_CONTAINER_ERROR", "At launch time, could not find or create a global shard variable container.", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);		
	}

	SAFE_FREE(puID);	
}

// There are no safety checks on this request. Responsibility is on the caller to gate multiple requests.
static void CreateShardVariableContainer(ContainerID containerID)
{
	// Manual transaction to create the shard variable container if it doesn't exist, and to make
	//  sure that it gets the correct ID.
	
	TransactionRequest *request = objCreateTransactionRequest();
	ContainerID* puID = calloc(1, sizeof(ContainerID));
	TransactionReturnVal* pReturn;
	
	*puID = containerID;

	pReturn = objCreateManagedReturnVal(MapManagerLibCreateShardVariableContainer_CB, puID);

	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
		"VerifyContainer containerIDVar %s %d",
		GlobalTypeToName(GLOBALTYPE_SHARDVARIABLE),
		containerID);

	// Move the container to the map server
	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, "containerIDVar", 
		"MoveContainerTo containerVar %s TRVAR_containerIDVar %s %d",
		GlobalTypeToName(GLOBALTYPE_SHARDVARIABLE), GlobalTypeToName(GLOBALTYPE_MAPMANAGER), 0);

	objAddToTransactionRequestf(request, GLOBALTYPE_MAPMANAGER, 0, "containerVar containerIDVar ContainerVarBinary", 
		"ReceiveContainerFrom %s TRVAR_containerIDVar ObjectDB 0 TRVAR_containerVar",
		GlobalTypeToName(GLOBALTYPE_SHARDVARIABLE));

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
		pReturn, "CreateShardVariableContainer", request);
		objDestroyTransactionRequest(request);
}

static void MapManagerLibEnsureShardVariableContainersExist(void)
{
	Container *pContainerMapRequested = objGetContainer(GLOBALTYPE_SHARDVARIABLE, SHARD_VAR_MAPREQUESTED_CONTAINER_ID);
	Container *pContainerBroadcast = objGetContainer(GLOBALTYPE_SHARDVARIABLE, SHARD_VAR_BROADCAST_CONTAINER_ID);
	
	if (pContainerMapRequested)
	{
		shardVariableServerInitContext.bShardVariableMapRequestedContainerExists = true;
	}
	else
	{
		CreateShardVariableContainer(SHARD_VAR_MAPREQUESTED_CONTAINER_ID);
	}
	
	if (pContainerBroadcast)
	{
		shardVariableServerInitContext.bShardVariableBroadcastContainerExists = true;
	}
	else
	{
		CreateShardVariableContainer(SHARD_VAR_BROADCAST_CONTAINER_ID);
	}
}



///////////////////////////////////////////////////////////////////////////////
//  Event Container set up

static void MapManagerLibAcquireEventContainersComplete_CB(void)
{
	eventServerInitContext.bAcquiredEventVariableContainers = true;
}

static void MapManagerLibCreateEventContainer_CB(TransactionReturnVal *pReturn, void *pUnused)
{
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		eventServerInitContext.bEventContainerExists = true;
	} 
	else 
	{
        TriggerAlert("MAPMANAGER_EVENT_CONTAINER_ERROR", "At launch time, could not find or create a global event container. Manually started events will not be tracked across restarts.", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0);
	}
}

// There are no safety checks on this request. Responsibility is on the caller to gate multiple requests.
static void CreateGlobalEventContainer()
{
	// Manual transaction to create the event container if it doesn't exist, and to make
	//  sure that it gets the correct ID.
	
	TransactionRequest *request = objCreateTransactionRequest();

	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, NULL, 
		"VerifyContainer containerIDVar %s %d",
		GlobalTypeToName(GLOBALTYPE_EVENTCONTAINER),
		EVENT_VAR_CONTAINER_ID);

	// Move the container to the map server
	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, 0, "containerIDVar", 
		"MoveContainerTo containerVar %s TRVAR_containerIDVar %s %d",
		GlobalTypeToName(GLOBALTYPE_EVENTCONTAINER), GlobalTypeToName(GLOBALTYPE_MAPMANAGER), 0);

	objAddToTransactionRequestf(request, GLOBALTYPE_MAPMANAGER, 0, "containerVar containerIDVar ContainerVarBinary", 
		"ReceiveContainerFrom %s TRVAR_containerIDVar ObjectDB 0 TRVAR_containerVar",
		GlobalTypeToName(GLOBALTYPE_EVENTCONTAINER));

	objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
		objCreateManagedReturnVal(MapManagerLibCreateEventContainer_CB, NULL), "CreateGlobalEventContainer", request);
		objDestroyTransactionRequest(request);
}

static void	MapManagerLibEnsureEventContainerExists()
{
	Container *pContainer = objGetContainer(GLOBALTYPE_EVENTCONTAINER, EVENT_VAR_CONTAINER_ID);
	if (pContainer)
	{
		eventServerInitContext.bEventContainerExists = true;
	}
	else
	{
		CreateGlobalEventContainer();
	}
}

int MapManagerLibOncePerFrame(F32 fElapsed)
{
	switch (eMMLState)
	{
	case MML_STATE_INIT:
		aslMapManager_PlayableNameSpaceCache_Init();

		NewMapTransfer_Init();
		aslMapManagerActivity_InitEventList();
		
		aslAcquireContainerOwnership(GLOBALTYPE_VIRTUALSHARD, NULL);

		// Acquire container ownership for shard variables
		aslAcquireContainerOwnership(GLOBALTYPE_SHARDVARIABLE, MapManagerLibAcquireShardVariableContainersComplete_CB);
		aslAcquireContainerOwnership(GLOBALTYPE_EVENTCONTAINER, MapManagerLibAcquireEventContainersComplete_CB);

		sPartitionHandshakesByID = stashTableCreateInt(16);

		RemoteCommand_GetServerList( objCreateManagedReturnVal(GetServerListComplete, NULL),
			GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_GAMESERVER);


		PrefStore_AtomicAddAndGet("NextGameServerID", NUM_GAMESERVER_IDS_TO_GRAB_AT_ONCE,GetNextBlockOfIDsComplete, NULL);

		/*for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
		{
			if (GlobalTypeParent(i) == GLOBALTYPE_ENTITY)
			{
				resDictSetMaxUnreferencedResources(GlobalTypeToCopyDictionaryName(i), RES_DICT_KEEP_ALL); // We need to keep them around even if not references
				objSubscribeToOnlineContainers(i);
			}
		} */

		//we believe that these subscriptions are all obsolete now, but might as well leave a command line option to turn them
		//back on in case we're missing something dumb

		if (sbMapManagerSubscribeToPlayers)
		{
			resDictSetMaxUnreferencedResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), RES_DICT_KEEP_ALL); // We need to keep them around even if not references
			objSubscribeToOnlineContainers(GLOBALTYPE_ENTITYPLAYER);
		}

		if (sbMapManagerSubscribeToTeamsAndGuilds)
		{
			resDictSetMaxUnreferencedResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_TEAM), RES_DICT_KEEP_ALL); // We need to keep them around even if not references
			objSubscribeToOnlineContainers(GLOBALTYPE_TEAM);

			resDictSetMaxUnreferencedResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GUILD), RES_DICT_KEEP_ALL); // We need to keep them around even if not references
			objSubscribeToOnlineContainers(GLOBALTYPE_GUILD);
		}


		bRequestSent = true;

		eMMLState = MML_STATE_SENT_STARTING_REMOTE_COMMANDS;

		siTimer = timerAlloc();

		break;


	case MML_STATE_SENT_STARTING_REMOTE_COMMANDS:
		{
			if (gbGotBlockOfIDs && gbGotServerList && NewMapTransfer_ReadyForNormalOperation())
			{
				eMMLState = MML_STATE_NORMAL;
				MapManagerLibStartedNormalOperation();
				RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
			}

		}
		break;

	case MML_STATE_NORMAL:
		MapManagerLibNormalOperation(fElapsed);
		aslMapManagerEvent_Tick();		

		if (shardVariableServerInitContext.bAcquiredShardVariableContainers)
		{
			if (!shardVariableServerInitContext.bAskedObjectDBToCreateContainers)
			{
				// Once we've acquired the shard variable containers, make a single attempt to find or create the single container we want
				MapManagerLibEnsureShardVariableContainersExist();
				shardVariableServerInitContext.bAskedObjectDBToCreateContainers = true;
			}
		}

		if (eventServerInitContext.bAcquiredEventVariableContainers &&
			!eventServerInitContext.bAskedObjectDBToCreateContainer)
		{
			// Once we've acquired the event container, make a single attempt to find or create the single container we want
			MapManagerLibEnsureEventContainerExists();
			eventServerInitContext.bAskedObjectDBToCreateContainer=true;
		}

		shardvariable_OncePerFrame();
		break;

	}

	return 1;
}



AUTO_STRUCT;
typedef struct PlayerWasSentToCrashedMapLogEntry
{
	ContainerID iMapContainerID;
	U32 iTransferTime;
	U32 iCrashTime;

} PlayerWasSentToCrashedMapLogEntry;

AUTO_STRUCT;
typedef struct PlayerWasSentToCrashedMapLog
{
	ContainerID iPlayerContainerID;
	PlayerWasSentToCrashedMapLogEntry **ppSentToCrashedMapLog;
} PlayerWasSentToCrashedMapLog;

StashTable sPlayersSentToCrashedMapsByPlayerID = NULL;


//for debugging... try to find people who are frequently sent to maps shortly before the map crashes
void RegisterPlayerWasSentToMapBeforeItCrashed(PlayerWasSentToMapLog *pPlayerLog, U32 iMapContainerID)
{
	PlayerWasSentToCrashedMapLog *pLog;
	PlayerWasSentToCrashedMapLogEntry *pLogEntry = StructCreate(parse_PlayerWasSentToCrashedMapLogEntry);
	pLogEntry->iMapContainerID = iMapContainerID;
	pLogEntry->iTransferTime = pPlayerLog->iTime;
	pLogEntry->iCrashTime = timeSecondsSince2000();

	if (!sPlayersSentToCrashedMapsByPlayerID)
	{
		sPlayersSentToCrashedMapsByPlayerID = stashTableCreateInt(16);
	}

	if (!stashIntFindPointer(sPlayersSentToCrashedMapsByPlayerID, pPlayerLog->iPlayerContainerID, &pLog))
	{
		pLog = StructCreate(parse_PlayerWasSentToCrashedMapLog);
		pLog->iPlayerContainerID = pPlayerLog->iPlayerContainerID;
		eaPush(&pLog->ppSentToCrashedMapLog, pLogEntry);
		stashIntAddPointer(sPlayersSentToCrashedMapsByPlayerID, pPlayerLog->iPlayerContainerID, pLog, false);
		return;
	}

	eaPush(&pLog->ppSentToCrashedMapLog, pLogEntry);

	while (eaSize(&pLog->ppSentToCrashedMapLog) > PSTCM_COUNT)
	{
		StructDestroy(parse_PlayerWasSentToCrashedMapLogEntry, pLog->ppSentToCrashedMapLog[0]);
		eaRemove(&pLog->ppSentToCrashedMapLog, 0);
	}

	if (pLog->ppSentToCrashedMapLog[eaSize(&pLog->ppSentToCrashedMapLog)-1]->iTransferTime 
		- pLog->ppSentToCrashedMapLog[0]->iTransferTime < PSTCM_INTERVAL && eaSize(&pLog->ppSentToCrashedMapLog) == PSTCM_COUNT)
	{
		char *pErrorString = NULL;
		char *pTimeString = NULL;
		int i;

		timeSecondsDurationToPrettyEString(PSTCM_INTERVAL, &pTimeString);

		estrPrintf(&pErrorString, "Player Entity %u was transferred to maps shortly before they crashed %d times in the last %s: ",
			pPlayerLog->iPlayerContainerID, PSTCM_COUNT, pTimeString);

		for (i=0; i < eaSize(&pLog->ppSentToCrashedMapLog); i++)
		{
			PlayerWasSentToCrashedMapLogEntry *pEntry = pLog->ppSentToCrashedMapLog[i];

			timeSecondsDurationToPrettyEString(pEntry->iCrashTime - pEntry->iTransferTime, &pTimeString);

			estrConcatf(&pErrorString, "%s%s before GS %u crashed", i == 0 ? "" : " - ", pTimeString, pEntry->iMapContainerID);
		}

		ErrorOrAlert("CRASHY_PLAYER", "%s", pErrorString);

		estrDestroy(&pErrorString);
		estrDestroy(&pTimeString);


	}


}



AUTO_COMMAND_REMOTE ACMD_IFDEF(CONTROLLER);
void InformMapManagerOfGameServerDeath(ContainerID iContainerID, bool bUnnaturalDeath)
{

	log_printf(LOG_LOGIN, "Map manager being informed of game server %d death\n", iContainerID);

	if (NewMapTransfer_IsHandlingServer(iContainerID))
	{	
		NewMapTransfer_InformMapManagerOfGameServerDeath(iContainerID, bUnnaturalDeath);
		return;
	}
}



// option to disable new code that will send the user to a new static map instance if the one they requested is full
bool gFullMapFailover = true;
AUTO_CMD_INT(gFullMapFailover, FullMapFailover) ACMD_CMDLINE;

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslMapManager_UGCProjectCreate_Return(ReturnedPossibleUGCProject *pReturnedPossibleUGCProject, UGCProjectRequestData *pUGCProjectRequestData)
{
	ReturnedGameServerAddress retVal = {0};

	if(pReturnedPossibleUGCProject->pPossibleUGCProject)
	{
		NewMapTransfer_RequestUGCEditingServer(pReturnedPossibleUGCProject->pPossibleUGCProject, pUGCProjectRequestData->iCmdID, &pUGCProjectRequestData->requesterInfo);
	}
	else
	{
		log_printf(LOG_LOGIN, "Failed to fully create new UGC project: %s", pReturnedPossibleUGCProject->strError);
		sprintf(retVal.errorString, "Failed to fully create new UGC project: %s", pReturnedPossibleUGCProject->strError);

		SlowRemoteCommandReturn_RequestNewOrExistingGameServerAddress(pUGCProjectRequestData->iCmdID, &retVal);
	}
}

void aslMapManager_RequestNewUGCProjectAndGameServer(PossibleUGCProject *pUGCProjectRequest, SlowRemoteCommandID iCmdID,
	NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo)
{
	UGCProjectRequestData ugcProjectRequestData;
	StructInit(parse_UGCProjectRequestData, &ugcProjectRequestData);

	StructCopy(parse_NewOrExistingGameServerAddressRequesterInfo, pRequesterInfo, &ugcProjectRequestData.requesterInfo, 0, 0, 0);

	ugcProjectRequestData.iCmdID = iCmdID;

	RemoteCommand_Intershard_aslUGCDataManager_UGCProjectCreate(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		pUGCProjectRequest, pRequesterInfo, &ugcProjectRequestData);
}

//has two basic modes of operation... when getting a PossibleMapChoice and when getting a PossibleUGCProject
AUTO_COMMAND_REMOTE_SLOW(ReturnedGameServerAddress*) ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void RequestNewOrExistingGameServerAddress(PossibleMapChoice *pRequest, PossibleUGCProject *pUGCProjectRequest, 
	NewOrExistingGameServerAddressRequesterInfo *pRequesterInfo, SlowRemoteCommandID iCmdID)
{
	ReturnedGameServerAddress retVal = {0};



	if (eMMLState != MML_STATE_NORMAL)
	{
		log_printf(LOG_LOGIN, "Map server not yet ready... request failing");
		sprintf(retVal.errorString, "Map manager not yet ready... request failing");

		SlowRemoteCommandReturn_RequestNewOrExistingGameServerAddress(iCmdID, &retVal);
		return;

	}

	if (pUGCProjectRequest)
	{
		if (pUGCProjectRequest->iEditQueueCookie) //0 means that the login server has verified that player has access level
		{
			if (!CheckEditQueueCookieValidity(pUGCProjectRequest->iEditQueueCookie))
			{
				log_printf(LOG_LOGIN, "Invalid edit login cookie submitted by player %s", pRequesterInfo->pPlayerName);
				strcpy(retVal.errorString, "Invalid edit login cookie");

				SlowRemoteCommandReturn_RequestNewOrExistingGameServerAddress(iCmdID, &retVal);
				return;
			}
		}

		if (pUGCProjectRequest->iID)
		{				
			NewMapTransfer_RequestUGCEditingServer(pUGCProjectRequest, iCmdID, pRequesterInfo);
		}
		else
		{
			char *pErrorString = NULL;
			if (!UGCProject_ValidateNewProjectRequest(pUGCProjectRequest, &pErrorString))
			{
				log_printf(LOG_LOGIN, "Invalid UGC project requested by %s, failing because %s", pRequesterInfo->pPlayerName, pErrorString);
				strcpy(retVal.errorString, pErrorString);

				SlowRemoteCommandReturn_RequestNewOrExistingGameServerAddress(iCmdID, &retVal);
				return;
			}

			aslMapManager_RequestNewUGCProjectAndGameServer(pUGCProjectRequest, iCmdID, pRequesterInfo);
		}
	}
	else
	{
		char *pFullRequest = NULL;
		char *pFullRequesterInfo = NULL;
	
		if (pRequest->eChoiceType != MAPCHOICETYPE_UNSPECIFIED)
		{
			NewMapTransfer_RequestNewOrExistingGameServerAddress(pRequest, pRequesterInfo, iCmdID);
			return;
		}

		ParserWriteText(&pFullRequest, parse_PossibleMapChoice, pRequest, 0, 0, 0);
		ParserWriteText(&pFullRequesterInfo, parse_NewOrExistingGameServerAddressRequesterInfo, pRequesterInfo, 0, 0, 0);

		AssertOrAlert("OLD_MAP_REQUEST", "Got RequestNewOrExistingGameServerAddress with no map choice type. full struct: %s\n\nRequester info: %s\n",
			pFullRequest, pFullRequesterInfo);
		estrDestroy(&pFullRequest);
		estrDestroy(&pFullRequesterInfo);

	}
}


DynamicPatchInfo *CreatePatchInfoForNameSpace(const char *pNameSpace, bool bForPlaying)
{
	DynamicPatchInfo *pPatchInfo  = StructCreate(parse_DynamicPatchInfo);

	if (!(strStartsWith(pNameSpace, "ugc") || strStartsWith(pNameSpace, UGC_GetShardSpecificNSPrefix(NULL))))
	{
		AssertOrAlert("UGC_BAD_NAMESPACE", "Expected namespace %s to start with \"ugc_\" or \"%s\", it does not", pNameSpace, UGC_GetShardSpecificNSPrefix(NULL));
	}
	aslFillInPatchInfo(pPatchInfo, pNameSpace, bForPlaying ? PATCHINFO_FOR_UGC_PLAYING : 0);

	return pPatchInfo;
}

void addPatchInfoForUGCProject(PossibleUGCProject *pPossibleProject, UGCProject *pProject)
{
	pPossibleProject->pPatchInfo = CreatePatchInfoForNameSpace(UGCProject_GetMostRecentNamespace(pProject), false);
}

void addPatchInfoForPossibleMapChoice(PossibleMapChoice *pChoice)
{
	char ns[1024], base[1024]; 
	if(resExtractNameSpace(pChoice->baseMapDescription.mapDescription, ns, base)) 
	{ 
		pChoice->patchInfo = StructCreate(parse_DynamicPatchInfo); 

		aslFillInPatchInfo(pChoice->patchInfo, ns, true);

		if(pChoice->baseMapDescription.bUGC)
		{ 
			if (!(strStartsWith(ns, "ugc") || strStartsWith(ns, UGC_GetShardSpecificNSPrefix(NULL))))
			{
				AssertOrAlert("UGC_BAD_NAMESPACE", "Expected namespace %s to start with \"ugc_\" or \"%s\", it does not", ns, UGC_GetShardSpecificNSPrefix(NULL));
			}
		}
	} 
}



bool AccountPermissionsAllowMapByName(AccountProxyKeyValueInfoList *pPermissions, const char *pMapName, ZoneMapType eMapType)
{
	AccountProxyKeyValueInfoList empty = {0};
	MapCategoryConfig *pCategory;
	int i;

	PERFINFO_AUTO_START_FUNC();


	if (!(gServerLibState.bUseAccountPermissionsForMapTransfer || isProductionMode()))
	{
		PERFINFO_AUTO_STOP();
		return true;
	}

	if (eMapType == ZMTYPE_UNSPECIFIED)
	{
		eMapType = zmapInfoGetMapType(worldGetZoneMapByPublicName(pMapName));
	}

	if (!pPermissions)
	{
		pPermissions = &empty;
	}

	pCategory = FindCategoryByNameAndType(pMapName, eMapType, NULL);

	if (!pCategory)
	{
		PERFINFO_AUTO_STOP();
		return true;
	}

	for (i=0; i < eaSize(&pCategory->ppPermissionTokens_Exclude); i++)
	{
		if (AccountProxyFindValueFromKeyInList(pPermissions,pCategory->ppPermissionTokens_Exclude[i]))
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	for (i=0; i < eaSize(&pCategory->ppPermissionTokens_Require); i++)
	{
		if (!AccountProxyFindValueFromKeyInList(pPermissions,pCategory->ppPermissionTokens_Require[i]))
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	PERFINFO_AUTO_STOP();
	return true;
}


bool aslAddPossibleEditingMapChoices(MapSearchInfo *mapInfo, char *pAccountName, PossibleMapChoices *choices, char *pReason)
{
	PossibleMapChoice *pChoice;
	RefDictIterator zmap_iter;
	ZoneMapInfo *zminfo;
	int highest_slot = 1;
	char newmap_name[64];

	if (!pAccountName || !resNameSpaceGetByName(pAccountName))
		return true;

	//for now, assemble list of all existing maps and all possible maps
	worldGetZoneMapIterator(&zmap_iter);
	while (zminfo = worldGetNextZoneMap(&zmap_iter))
	{
		char nameSpace[RESOURCE_NAME_MAX_SIZE];
		char resourceName[RESOURCE_NAME_MAX_SIZE];
		const char *pMapPublicName = zmapInfoGetPublicName(zminfo);
		if (resExtractNameSpace(pMapPublicName, nameSpace, resourceName) &&
			stricmp(nameSpace, pAccountName) == 0)
		{
			int slot;

			pChoice = StructCreate(parse_PossibleMapChoice);
			pChoice->baseMapDescription.containerID = 0;
			pChoice->bNewMap = true;
			pChoice->bEditMap = true;
			pChoice->baseMapDescription.mapDescription = allocAddString(pMapPublicName);
			pChoice->baseMapDescription.spawnPoint = allocAddString(START_SPAWN);
			pChoice->baseMapDescription.eMapType = ZMTYPE_UNSPECIFIED;//zmapInfoGetMapType(zminfo);

			sprintf(pChoice->newMapReason, "Created from PossibleEditingMapChoices by player %u", mapInfo->playerID);
			
			

			eaPush(&choices->ppChoices, pChoice);

			if (sscanf(resourceName, "Slot_%d", &slot) == 1 )
			{
				if (slot >= highest_slot)
					highest_slot = slot+1;
			}
		}
	}

	pChoice = StructAlloc(parse_PossibleMapChoice);
	pChoice->baseMapDescription.containerID = 0;
	pChoice->bNewMap = true;
	pChoice->bEditMap = true;
	pChoice->bNewMap = true;
	sprintf(newmap_name, "%s:SLOT_%d", pAccountName, highest_slot);
	pChoice->baseMapDescription.mapDescription = allocAddString(newmap_name);
	pChoice->baseMapDescription.spawnPoint = allocAddString(START_SPAWN);
	pChoice->baseMapDescription.eMapType = ZMTYPE_UNSPECIFIED;

	sprintf(pChoice->newMapReason, "Created from PossibleEditingMapChoices by player %u", mapInfo->playerID);
	eaPush(&choices->ppChoices, pChoice);


	return true;
}

bool aslCreateGameServerDescription(MapDescription *map, GameServerDescription *server, ContainerID iContainerID)
{
	if (map->eMapType == ZMTYPE_UNSPECIFIED)
	{
		server->baseMapDescription.eMapType = zmapInfoGetMapType(worldGetZoneMapByPublicName(map->mapDescription));
	}
	else
	{
		server->baseMapDescription.eMapType = map->eMapType;
	}


	server->baseMapDescription.ownerID = map->ownerID;
	server->baseMapDescription.ownerType = map->ownerType;
	server->baseMapDescription.mapDescription = allocAddString(map->mapDescription);
	server->baseMapDescription.mapVariables = allocAddString(map->mapVariables);

	server->baseMapDescription.containerID = iContainerID;
	server->baseMapDescription.bUGC = map->bUGC;
	server->baseMapDescription.bUGCEdit = map->bUGC;
	server->baseMapDescription.iVirtualShardID = map->iVirtualShardID;

	
	return true;
}

static PossibleMapChoice *FindPlayersMapFromChoices(PossibleMapChoices *pChoices, ContainerID iPlayerID)
{
	ContainerRef ref = objGetSubscribedContainerLocation(GLOBALTYPE_ENTITYPLAYER, iPlayerID);

	if (ref.containerType == GLOBALTYPE_GAMESERVER)
	{
		int i;

		for (i=0; i < eaSize(&pChoices->ppChoices); i++)
		{
			if (pChoices->ppChoices[i]->baseMapDescription.containerID == ref.containerID)
			{
				return pChoices->ppChoices[i];
			}
		}
	}

	return NULL;
}



void FixupPossibleMapChoice(MapSearchInfo *mapInfo, PossibleMapChoice *pChoice)
{
	if (mapInfo->iUGCProjectID)
	{
		if (!pChoice->baseMapDescription.bUGC)
		{
			AssertOrAlert("UGC_MM_CORRUPTION", "a possible map choice name %s is not listed as UGC, should be", 
				pChoice->baseMapDescription.mapDescription);
		}
	}
	addPatchInfoForPossibleMapChoice(pChoice);
}


//backupMapSearchInfo is used if mapInfo requests a specific game server and it 
//doesn't exist
//
//NOTE: If you are considering adding new arguments to this function, they should almost certainly be added to MapSearchInfo instead, even though
//yes that means that they will be duplicated between mapInfo and BackupMapInfo
AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER) ACMD_IFDEF(GAMESERVER);
PossibleMapChoices *GetPossibleMapChoices(MapSearchInfo *mapInfo, MapSearchInfo *pBackupMapInfo, char *pReason)
{
	NewMapTransfer_FixupOldMapSearchInfo(mapInfo, pReason);
	NewMapTransfer_FixupOldMapSearchInfo(pBackupMapInfo, pReason);


	return NewMapTransfer_GetPossibleMapChoices(mapInfo, pBackupMapInfo, pReason);
	

}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void MapIsDoneLoading(ContainerID iID)
{
	if (NewMapTransfer_IsHandlingServer(iID))
	{
		NewMapTransfer_MapIsDoneLoading(iID);
		
	}
}



AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void HereIsGSLGlobalInfo_ForMapManager(ContainerID iServerID, GameServerGlobalInfo *pInfo)
{
	if (NewMapTransfer_IsHandlingServer(iServerID))
	{
		NewMapTransfer_HereIsGSLGlobalInfo_ForMapManager(iServerID, pInfo);
		return;
	}
}

#define mapManagerConfigLoadingAssertf(expr, fmt, ...) { if (!(expr)) { estrPrintf(ppErrorString, fmt, __VA_ARGS__); return false; }}

bool VerifyGlobalMapConfig(GlobalMapManagerConfig *pConfig, StashTable categoryTable, char **ppErrorString)
{	
	int i, j;
	bool bHadGSSuffix;

	if (!(pConfig->pMaxEditServers && pConfig->pMaxEditServers[0]))
	{
		estrConcatf(ppErrorString, "MaxEditServers somehow not set, should have a default value. What did you do?");
		return false;
	}


	if (ServerLib_GetIntFromStringPossiblyWithPerGsSuffix(pConfig->pMaxEditServers, &pConfig->iMaxEditServers, &bHadGSSuffix))
	{
		if (!bHadGSSuffix)
		{
			WARNING_NETOPS_ALERT("ABSOLUTE_MAXEDITSERVERS", "Max edit servers is being set as an absolute int (%d), not as nnn_PER_GS_MACHINE", pConfig->iMaxEditServers);
		}
	}
	else
	{
		estrConcatf(ppErrorString, "Couldn't parse MaxEditServers string %s", pConfig->pMaxEditServers);
		return false;
	}

	//for any duplicates, remove the earlier copy of the duplicates (so that a specific file #including a general file
	//will work properly)
	for (i=eaSize(&pConfig->ppCategories) - 1; i >= 0; i--)
	{
		if (stashFindPointer(categoryTable, pConfig->ppCategories[i]->pCategoryName, NULL))
		{
			StructDestroy(parse_MapCategoryConfig, pConfig->ppCategories[i]);
			eaRemove(&pConfig->ppCategories, i);
		}
		else
		{
			stashAddPointer(categoryTable, pConfig->ppCategories[i]->pCategoryName, pConfig->ppCategories[i], false);
		}
	}

	for (i=eaSize(&pConfig->ppSingleMaps) - 2; i >= 0; i--)
	{
		for (j=i+1; j < eaSize(&pConfig->ppSingleMaps); j++)
		{
			if (stricmp(pConfig->ppSingleMaps[i]->pPublicName, pConfig->ppSingleMaps[j]->pPublicName) == 0)
			{
				StructDestroy(parse_SingleMapConfig, pConfig->ppSingleMaps[i]);
				eaRemove(&pConfig->ppSingleMaps, i);
				break;
			}
		}
	}

	//now check that each public name exists, and each category exists
	for (i=eaSize(&pConfig->ppSingleMaps)-1; i >= 0; i--)
	{
		if (!worldGetZoneMapByPublicName(pConfig->ppSingleMaps[i]->pPublicName))
		{
			ErrorOrAlert("BAD_MAPMGRCFG_MAP_NAME", "Unknown public name %s while loading map manager config. This is NON fatal.", pConfig->ppSingleMaps[i]->pPublicName);
			StructDestroy(parse_SingleMapConfig, pConfig->ppSingleMaps[i]);
			eaRemove(&pConfig->ppSingleMaps, i);
		}
		else
		{
			mapManagerConfigLoadingAssertf(stashFindPointer(categoryTable, pConfig->ppSingleMaps[i]->pCategoryName, NULL),
				"Unknown category name %s", pConfig->ppSingleMaps[i]->pCategoryName);
		}
	}

	//now check that each category is well formed
	for (i=0; i < eaSize(&pConfig->ppCategories); i++)
	{
		MapCategoryConfig *pCat = pConfig->ppCategories[i];

		mapManagerConfigLoadingAssertf(pCat->iMaxPartitions > 0 && pCat->iMaxPartitions <= MAX_ACTUAL_PARTITIONS, "category %s has invalid maxPartitions %d", pCat->pCategoryName, pCat->iMaxPartitions);

		mapManagerConfigLoadingAssertf(((!!pCat->iMaxPlayers_Hard) ^ (!!pCat->iMaxPlayers_Soft)) == 0, "category %s must have both or neither of hard and soft max players", pCat->pCategoryName); 
		mapManagerConfigLoadingAssertf(((!!pCat->iMaxPlayers_AcrossPartitions_Hard) ^ (!!pCat->iMaxPlayers_AcrossPartitions_Soft)) == 0, "category %s must have both or neither of hard and soft max players across partitions", pCat->pCategoryName); 

		if (pCat->iMaxPlayers_Soft)
		{
			mapManagerConfigLoadingAssertf(pCat->iMaxPlayers_Soft <= pCat->iMaxPlayers_Hard, "Category %s has soft max > hard max", pCat->pCategoryName);

			if (pCat->iMinPlayers)
			{
				mapManagerConfigLoadingAssertf(pCat->iMinPlayers < pCat->iMaxPlayers_Soft, "Category %s has min players >= soft max", pCat->pCategoryName);
			}
		}

		if (pCat->iMaxPlayers_AcrossPartitions_Soft)
		{
			mapManagerConfigLoadingAssertf(pCat->iMaxPlayers_AcrossPartitions_Soft <= pCat->iMaxPlayers_AcrossPartitions_Hard, "Category %s has soft max across partitions > hard max", pCat->pCategoryName);
			mapManagerConfigLoadingAssertf(pCat->iMaxPartitions > 1, "Category %s has across_partitions soft/hard max set, but doesn't allow partitions", pCat->pCategoryName);
		}
	}

	//now check that all starting map names are real static or shared maps
	for (i=0; i < eaSize(&pConfig->ppStartingMaps); i++)
	{
		ZoneMapType eType = zmapInfoGetMapType(worldGetZoneMapByPublicName(pConfig->ppStartingMaps[i]));
		mapManagerConfigLoadingAssertf( eType == ZMTYPE_STATIC || eType == ZMTYPE_SHARED,
			"Requested starting map %s is either non-existent or non-static/non-shared", pConfig->ppStartingMaps[i]);
	}

	return true;
}


bool CompareCategoriesCB(MapCategoryConfig *pCategory1, MapCategoryConfig *pCategory2)
{
	if (stricmp_safe(pCategory1->pCategoryName, pCategory2->pCategoryName) == 0)
	{
		return true;
	}

	return false;
}

bool CompareSingleMapConfigCB(SingleMapConfig *pConfig1, SingleMapConfig *pConfig2)
{
	if (stricmp_safe(pConfig1->pPublicName, pConfig2->pPublicName) == 0)
	{
		return true;
	}

	return false;
}

bool CompareMinMapCountConfigCB(MinMapCountConfig *pConfig1, MinMapCountConfig *pConfig2)
{
	if (stricmp_safe(pConfig1->pMapName, pConfig2->pMapName) == 0)
	{
		return true;
	}

	return false;
}

bool CompareStringCB(char *pStr1, char *pStr2)
{
	if (stricmp_safe(pStr1, pStr2) == 0)
	{
		return true;
	}

	return false;
}

char *pMarker = "__MARKER__";

//the normal loading behavior does not work for fields like ppNewCharacterMaps where if we specify something in a local file, it should
//totally replace the previous contents. But we don't want to clear the fields between loads in case the later load doesn't specify 
//the field at all. So we push little markers into the earray after each file is loaded, allowing us to go through in a fixup pass
//later and use only the "last group" of strings
void MarkEarrayForLaterFixup(char ***pppEarray)
{
	eaPush(pppEarray, pMarker);
}

void MarkMapEarraysForLaterFixup(GlobalMapManagerConfig *pConfig)
{
	MarkEarrayForLaterFixup(&pConfig->ppStartingMaps);
	MarkEarrayForLaterFixup(&pConfig->ppNewCharacterMaps);
	MarkEarrayForLaterFixup(&pConfig->ppSkipTutorialMaps);
}

void FixupMarkedEarray(char ***pppEArray)
{
	int i = eaSize(pppEArray) - 1;

	//first remove all trailing markers
	while (eaSize(pppEArray) && (*pppEArray)[i] == pMarker)
	{
		eaRemove(pppEArray, i);
		i--;
	}

	if (!eaSize(pppEArray))
	{
		return;
	}

	//now skip past all "real" fields

	while (i >= 0 && (*pppEArray)[i] != pMarker)
	{
		i--;
	}

	//now remove all fields before the "real" fields

	while (i >= 0)
	{
		if ((*pppEArray)[i] != pMarker)
		{
			free((*pppEArray)[i]);
		}
		eaRemove(pppEArray, i);
		i--;
	}
}

void FixupMapEarrays(GlobalMapManagerConfig *pConfig)
{
	FixupMarkedEarray(&pConfig->ppStartingMaps);
	FixupMarkedEarray(&pConfig->ppNewCharacterMaps);
	FixupMarkedEarray(&pConfig->ppSkipTutorialMaps);
}




int ReadMapManagerConfigFromTextFiles(GlobalMapManagerConfig *pConfig, char **ppErrorString, char **ppCommentString)
{
	char fileName[CRYPTIC_MAX_PATH];
	char *pClusterName;
	char located[CRYPTIC_MAX_PATH];
	char *bRet;

	sprintf(fileName, "server/MapManagerConfig.txt");
	bRet = fileLocateRead(fileName, located);
	estrConcatf(ppCommentString, "Loading from %s\n", located);
	if (!ParserReadTextFile(fileName, parse_GlobalMapManagerConfig, pConfig, 0))
	{
		char *pTempString = NULL;
		ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
		ParserReadTextFile(fileName, parse_GlobalMapManagerConfig, pConfig, 0);
		ErrorfPopCallback();
		estrPrintf(ppErrorString, "Text Parser error while reading %s: %s", fileName, pTempString);

		estrDestroy(&pTempString);
		return 0;
	}

	MarkMapEarraysForLaterFixup(pConfig);

	pClusterName = ShardCommon_GetClusterName();
	if (pClusterName && pClusterName[0])
	{

		sprintf(fileName, "server/MapManagerConfig_%s.txt", pClusterName);
		bRet = fileLocateRead(fileName, located);
		if (fileExists(fileName))
		{
			estrConcatf(ppCommentString, "Loading from %s\n", located);
			if (!ParserReadTextFile(fileName, parse_GlobalMapManagerConfig, pConfig, 0))
			{
				char *pTempString = NULL;
				ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
				ParserReadTextFile(fileName, parse_GlobalMapManagerConfig, pConfig, 0);
				ErrorfPopCallback();
				estrPrintf(ppErrorString, "Text Parser error while reading %s: %s", fileName, pTempString);

				estrDestroy(&pTempString);
				return 0;
			}
		}
		else
		{
			estrConcatf(ppCommentString, "NOT loading from %s, it does not exist\n", fileName);
		}

		MarkMapEarraysForLaterFixup(pConfig);
	}

	sprintf(fileName, "server/MapManagerConfig_%s.txt", GetShardNameFromShardInfoString());
	bRet = fileLocateRead(fileName, located);
	if (fileExists(fileName))
	{
		estrConcatf(ppCommentString, "Loading from %s\n", located);
		if (!ParserReadTextFile(fileName, parse_GlobalMapManagerConfig, pConfig, 0))
		{
			char *pTempString = NULL;
			ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
			ParserReadTextFile(fileName, parse_GlobalMapManagerConfig, pConfig, 0);
			ErrorfPopCallback();
			estrPrintf(ppErrorString, "Text Parser error while reading %s: %s", fileName, pTempString);

			estrDestroy(&pTempString);
			return 0;
		}
	}
	else
	{
		estrConcatf(ppCommentString, "NOT loading from %s, it does not exist\n", fileName);
	}

	MarkMapEarraysForLaterFixup(pConfig);

	if (pClusterName && pClusterName[0])
	{

		sprintf(fileName, "server/MapManagerConfig_ClusterLocal.txt");
		bRet = fileLocateRead(fileName, located);
		if (fileExists(fileName))
		{
			estrConcatf(ppCommentString, "Loading from %s\n", located);
			if (!ParserReadTextFile(fileName, parse_GlobalMapManagerConfig, pConfig, 0))
			{
				char *pTempString = NULL;
				ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
				ParserReadTextFile(fileName, parse_GlobalMapManagerConfig, pConfig, 0);
				ErrorfPopCallback();
				estrPrintf(ppErrorString, "Text Parser error while reading %s: %s", fileName, pTempString);

				estrDestroy(&pTempString);
				return 0;
			}
		}
		else
		{
			estrConcatf(ppCommentString, "NOT loading from %s, it does not exist\n", fileName);
		}

		MarkMapEarraysForLaterFixup(pConfig);
	}



	sprintf(fileName, "server/MapManagerConfig_local.txt");
	bRet = fileLocateRead(fileName, located);
	if (fileExists(fileName))
	{
		estrConcatf(ppCommentString, "Loading from %s\n", located);
		if (!ParserReadTextFile(fileName, parse_GlobalMapManagerConfig, pConfig, 0))
		{
			char *pTempString = NULL;
			ErrorfPushCallback(EstringErrorCallback, (void*)(&pTempString));
			ParserReadTextFile(fileName, parse_GlobalMapManagerConfig, pConfig, 0);
			ErrorfPopCallback();
			estrPrintf(ppErrorString, "Text Parser error while reading %s: %s", fileName, pTempString);

			estrDestroy(&pTempString);
			return 0;
		}
	}
	else
	{
		estrConcatf(ppCommentString, "NOT loading from %s, it does not exist\n", fileName);
	}

	FixupMapEarrays(pConfig);

	eaOverrideAndRemoveDuplicates(&pConfig->ppCategories, CompareCategoriesCB, parse_MapCategoryConfig);
	eaRemoveDuplicates(&pConfig->ppSingleMaps, CompareSingleMapConfigCB, parse_SingleMapConfig);
	eaRemoveDuplicates(&pConfig->ppMinMapCounts, CompareMinMapCountConfigCB, parse_MinMapCountConfig);

	eaRemoveDuplicates(&pConfig->ppStartingMaps, CompareStringCB, NULL);
	eaRemoveDuplicates(&pConfig->ppNewCharacterMaps, CompareStringCB, NULL);
	eaRemoveDuplicates(&pConfig->ppSkipTutorialMaps, CompareStringCB, NULL);

	estrCopy(&pConfig->pLoadingComment, ppCommentString);

	return 1;
}

U32 siLastLoadTime = 0;
bool sbLastLoadWasAtStartup = true;

#define LOAD_AND_VERIFY_GLOBAL_MAP_CONFIG_SUCCESS "LOADED SUCCESSFULLY"
char *LoadAndVerifyGlobalMapConfig(void)
{
	static bool bFirstTime = true;
	static char *pReturnString = NULL;
	char *pErrorString = NULL;
	static char *pCommentString = NULL;

	estrClear(&pCommentString);
	estrClear(&pReturnString);



	if (bFirstTime)
	{

		sCategoriesByCategoryNamePointer = stashTableCreateAddress(16);


		bFirstTime = false;

		StructInit(parse_GlobalMapManagerConfig, &gMapManagerConfig);

		if (!ReadMapManagerConfigFromTextFiles(&gMapManagerConfig, &pReturnString, &pCommentString))
		{
			return pReturnString;
		}
		else if (!VerifyGlobalMapConfig(&gMapManagerConfig, sCategoriesByCategoryNamePointer, &pErrorString))
		{
			estrPrintf(&pReturnString, "Failed to load global map manager config file: %s", pErrorString);

			return pReturnString;

		}

		estrInsertf(&pCommentString, 0, "%s -- ", LOAD_AND_VERIFY_GLOBAL_MAP_CONFIG_SUCCESS);

		siLastLoadTime = timeSecondsSince2000();

		estrConcatf(&gMapManagerConfig.pLoadingComment, "\n\nLoaded at %s (MapManager startup)\n", timeGetLocalDateStringFromSecondsSince2000(siLastLoadTime));
		return pCommentString;
	}
	else
	{
		GlobalMapManagerConfig tempConfig = {0};
		StashTable tempCategoryTable = stashTableCreateAddress(16);

		StructInit(parse_GlobalMapManagerConfig, &tempConfig);
		if (!ReadMapManagerConfigFromTextFiles(&tempConfig, &pReturnString, &pCommentString))
		{
			return pReturnString;
		}

		if (VerifyGlobalMapConfig(&tempConfig, tempCategoryTable, &pErrorString))
		{
			MapCategoryConfig **ppTemp = NULL;
			char *pDiffString = NULL;
			
			stashTableDestroy(sCategoriesByCategoryNamePointer);
			sCategoriesByCategoryNamePointer = tempCategoryTable;

			//copy over the loading comment first so StructWriteTextDiff will always ignore it
			estrCopy(&gMapManagerConfig.pLoadingComment, &tempConfig.pLoadingComment);
			StructWriteTextDiff(&pDiffString, parse_GlobalMapManagerConfig, &gMapManagerConfig, &tempConfig, NULL, 0, 0, 0);


			//need to specially copy the categories, as they are referred to by pointer in sCategoriesByCategoryNamePointer
			ppTemp = tempConfig.ppCategories;
			tempConfig.ppCategories = NULL;
			StructCopyAll(parse_GlobalMapManagerConfig, &tempConfig, &gMapManagerConfig);
			StructDeInit(parse_GlobalMapManagerConfig, &tempConfig);

			gMapManagerConfig.ppCategories = ppTemp;

			NewMapTransfer_FixupListsForReloadedGlobalConfig();

			estrConcatf(&gMapManagerConfig.pLoadingComment, "\n\nDIFF FROM PREVIOUS:\n%s", pDiffString);
			estrDestroy(&pDiffString);

			//this return string is explicitly checked for 
			estrInsertf(&pCommentString, 0, "%s -- ", LOAD_AND_VERIFY_GLOBAL_MAP_CONFIG_SUCCESS);

			estrConcatf(&gMapManagerConfig.pLoadingComment, "\n\nLoaded at %s\n", timeGetLocalDateStringFromSecondsSince2000(timeSecondsSince2000()));
			estrConcatf(&gMapManagerConfig.pLoadingComment, "Previous load was at %s%s\n", timeGetLocalDateStringFromSecondsSince2000(siLastLoadTime), 
				sbLastLoadWasAtStartup ? "(MapManager startup)" : "");

			siLastLoadTime = timeSecondsSince2000();
			sbLastLoadWasAtStartup = false;


			return pCommentString;
		}
		else
		{
			estrPrintf(&pReturnString, "FAILED TO LOAD NEW CONFIG: %s", pErrorString);

			stashTableDestroy(tempCategoryTable);
			estrDestroy(&pErrorString);
			StructDeInit(parse_GlobalMapManagerConfig, &tempConfig);
			return pReturnString;
		}
	}



}

AUTO_STARTUP(MapManager) ASTRT_DEPS(InventoryBags, Allegiance,
									Activities, MissionPlayTypes);
void aslMapManagerStartup(void)
{
	// MissionPlayTypes required so we can read in the UGC config file.

}

int MapManagerLibInit(void)
{
	char *pRetVal;
	objLoadAllGenericSchemas();

	AutoStartup_SetTaskIsOn("MapManager", 1);
	AutoStartup_RemoveAllDependenciesOn("WorldLib");

	if (isDevelopmentMode())
		fileLoadAllUserNamespaces(1);

	loadstart_printf("Running Auto Startup...");
	DoAutoStartup();
	loadend_printf(" done.");

	worldLoadZoneMaps();

	resFinishLoading();

	assertmsg(GetAppGlobalType() == GLOBALTYPE_MAPMANAGER, "map manager type not set");

	loadstart_printf("Connecting MapManager to TransactionServer (%s)... ", gServerLibState.transactionServerHost);

	while (!InitObjectTransactionManager(
		GetAppGlobalType(),
		gServerLibState.containerID,
		gServerLibState.transactionServerHost,
		gServerLibState.transactionServerPort,
		gServerLibState.bUseMultiplexerForTransactions, NULL))
	{
		Sleep(1000);
	}
	if (!objLocalManager())
	{
		loadend_printf("failed.");
		return 0;
	}

	loadend_printf("connected.");

	gAppServer->oncePerFrame = MapManagerLibOncePerFrame;

	pRetVal = LoadAndVerifyGlobalMapConfig();

	if (!strStartsWith(pRetVal, LOAD_AND_VERIFY_GLOBAL_MAP_CONFIG_SUCCESS))
	{
		if (isProductionMode())
		{
			CRITICAL_NETOPS_ALERT("BAD_MAPMANAGER_CONFIG", "Mapmanager failed to load mapmanagerconfig.txt. Will kill itself and not come back. Error: %s",
				pRetVal);
			RemoteCommand_ServerIsGoingToDie(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), GetAppGlobalID(), "MapManagerConfig.txt fail", false, false);
			svrExit(-1);
		}
		else
		{
			assertmsgf(0, "Mapmanager failed to load mapmanagerconfig.txt: %s", pRetVal);
		}
	}

	return 1;
}
	


AUTO_RUN;
int MapManagerRegister(void)
{
	aslRegisterApp(GLOBALTYPE_MAPMANAGER,MapManagerLibInit, 0); // loads worldgrids, can't do APPSERVERTYPEFLAG_NOPIGGSINDEVELOPMENT);
	return 1;
}


AUTO_COMMAND_REMOTE;
void GameServerPortWorked(ContainerID iGameServerID, U32 iGameServerLocalIP, int iPortNum)
{
	TrackedMap *pMap = NULL;
	SingleMachineUnusedPortList *pPortList = GetPortListForIP(iGameServerLocalIP);

	if (NewMapTransfer_IsHandlingServer(iGameServerID))
	{
		NewMapTransfer_GameServerPortWorked(iGameServerID, iPortNum);
	}
}



//whenever a player logs in to a game server, that game server calls this function, which lets the map manager
//decrement the number of currently logging in players it has tracked for that map
AUTO_COMMAND_REMOTE;
void GameServerReportsPlayerBeganLogin(ContainerID iGameServerID, U32 uPartitionID)
{
	if (NewMapTransfer_IsHandlingServer(iGameServerID))
	{
		NewMapTransfer_GameServerReportsPlayerBeganLogin(iGameServerID, uPartitionID);
		return;
	}
}

/*

//there are two reasons we might not kill a map which thinks it should die:
//(1) we have a min config saying that we need n of it running, and there are n or fewer running
//(2) every other map of its type has > NewMapLaunchCutoff players
void KillGameServerDueToTimeoutIfAppropriate(TrackedMap *pMap)
{

	MinMapCountConfig *pMinConfig = FindMinMapCountConfigForMap(pMap);
	TrackedMapList *pList = FindMapListForMap(pMap);

	int i;
	int iTotalCount = 0;
	bool bFoundSomeoneElseBelowLaunchCutoff = false;
	int iNewMapLaunchCutoff = 0;
	
	if (!pList)
	{
		RemoteCommand_KillYourselfGracefully(GLOBALTYPE_GAMESERVER, pMap->iContainerID);
		pMap->bToldItToDie = true;
		return;
	}


	if (pList->pCategory)
	{
		iNewMapLaunchCutoff = pList->pCategory->iNewMapLaunchCutoff;
	}

	if (!pMinConfig && !iNewMapLaunchCutoff)
	{
		RemoteCommand_KillYourselfGracefully(GLOBALTYPE_GAMESERVER, pMap->iContainerID);
		pMap->bToldItToDie = true;
		return;
	}

	for (i=0; i < eaSize(&pList->ppMaps); i++)
	{
		if (!pList->ppMaps[i]->bToldItToDie)
		{
			iTotalCount++;

			if (iNewMapLaunchCutoff && pList->ppMaps[i] != pMap && (pList->ppMaps[i]->globalInfo.iNumPlayers + pList->ppMaps[i]->iNumPlayersRecentlyLoggingIn) < iNewMapLaunchCutoff)
			{
				bFoundSomeoneElseBelowLaunchCutoff = true;
			}
		}
	}

	if (pMinConfig && iTotalCount <= pMinConfig->iMinCount)
	{
		return;
	}

	if (gMapManagerState.bDoLaunchCutoffs)
	{
		if (iNewMapLaunchCutoff && !bFoundSomeoneElseBelowLaunchCutoff)
		{
			return;
		}
	}

	RemoteCommand_KillYourselfGracefully(GLOBALTYPE_GAMESERVER, pMap->iContainerID);
	pMap->bToldItToDie = true;
}
*/

AUTO_COMMAND_REMOTE;
void IHaveTimedOutServer(ContainerID iGameServerID)
{
/*	TrackedMap *pMap;

	if (!stashIntFindPointer(gMapManagerState.trackedMaps, iGameServerID, &pMap))
	{
		return;
	}

	KillGameServerDueToTimeoutIfAppropriate(pMap);*/

	NewMapTransfer_KillGameServerDueToTimeoutIfAppropriate(iGameServerID);
}


AUTO_COMMAND;
char *ReloadMapManagerConfig(void)
{
	return LoadAndVerifyGlobalMapConfig();
}

AUTO_COMMAND;
char *OverwriteClusterLocalMapManagerConfigAndReload(char *pEncodedFile)
{
	int iFileSize;
	char *pFileContents = EncodedZippedStringToBuffer(pEncodedFile, &iFileSize);
	FILE *pOutFile;

	if (!pFileContents)
	{
		return "Unable to decode string";
	}

	mkdirtree_const("data/server/MapManagerConfig_ClusterLocal.txt");
	pOutFile = fopen("data/server/MapManagerConfig_ClusterLocal.txt", "wt");
	if (!pOutFile)
	{
		return "Unable to open file on disk for writing";
	}

	fprintf(pOutFile, "%s", pFileContents);
	fclose(pOutFile);
	free(pFileContents);

	return LoadAndVerifyGlobalMapConfig();

}


void OVERRIDE_LATELINK_GetCommandsForGenericServerMonitoringPage(GenericServerInfoForHttpXpath *pInfo)
{
	estrPrintf(&pInfo->pCommand1, "ReloadMapManagerConfig $COMMANDNAME(ReloadMapManagerConfig)");
}



// This function is called by game servers that want to track destination door status
// to register themselves for callbacks.
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslMapManagerUpdateDoorDestinationStatusRequests(ContainerID uMapID, MapList* pInfo)
{
	TrackedGameServerExe* pServer = GetGameServerFromID(uMapID);
	PendingDoorDestinationRequest* pPendingData;
	MapSummaryList* pMapSummaryList;
	S32 iPendingIdx;

	iPendingIdx = eaIndexedFindUsingInt(&g_eaPendingDoorDestinationRequestMapIDs, uMapID);
	pPendingData = eaGet(&g_eaPendingDoorDestinationRequestMapIDs, iPendingIdx);
	pMapSummaryList = mechanics_CreateMapSummaryListFromMapList(pInfo);

	if (pServer)
	{
		// If it already had map request data, destroy it
		if (pServer->pDestMapStatusRequests)
		{
			StructDestroy(parse_MapSummaryList, pServer->pDestMapStatusRequests);
		}
		pServer->pDestMapStatusRequests = pMapSummaryList;

		// If there is pending data, destroy it
		if (pPendingData)
		{
			eaRemove(&g_eaPendingDoorDestinationRequestMapIDs, iPendingIdx);
			StructDestroySafe(parse_PendingDoorDestinationRequest, &pPendingData);
		}

		// If not already tracking, add to the tracking list
		ea32PushUnique(&g_piDoorDestinationRequestMapIDs, uMapID);
	}
	else // If the TrackedGameServerExe doesn't exist yet, create or update pending data
	{
		if (pPendingData)
		{
			pPendingData->uLastUpdateTime = timeSecondsSince2000();
			pPendingData->pMapSummaryList = pMapSummaryList;
		}
		else
		{
			pPendingData = StructCreate(parse_PendingDoorDestinationRequest);
			pPendingData->uMapID = uMapID;
			pPendingData->pMapSummaryList = pMapSummaryList;
			pPendingData->uLastUpdateTime = timeSecondsSince2000();
			eaIndexedEnable(&g_eaPendingDoorDestinationRequestMapIDs, parse_PendingDoorDestinationRequest);
			eaPush(&g_eaPendingDoorDestinationRequestMapIDs, pPendingData);
		}
	}
}

static MapPartitionSummary* aslFindPartitionWithLeastMembers(GameServerList* pList, 
															 const char* pchFindVars, 
															 S32 iFindDifficulty, 
															 S32 iDifficultyRange,
															 U32* puMapContainerID)
{
	S32 iLeastMembers = -1;
	S32 iBestDiffAbs = -1;
	MapPartitionSummary* pChosenPartition = NULL;
	
	FOR_EACH_PARTITION_IN_LIST(pList, pPartition, pServer)
	{
		S32 iDiffAbs;

		if (!pServer->globalInfo.bEnableOpenInstancing)
			continue;

		if (!pPartition->iNumPlayers)
			continue;

		if (pPartition->bAssignedOpenInstance)
			continue;

		if (stricmp(pchFindVars, pPartition->pMapVariables)==0)
			continue;

		iDiffAbs = ABS(pServer->globalInfo.iDifficulty - iFindDifficulty);
		if (iDifficultyRange >= 0 && iDiffAbs > iDifficultyRange)
		{
			continue;
		}
		
		if (iLeastMembers < 0 || iBestDiffAbs > iDiffAbs 
			|| (iLeastMembers > pPartition->iNumPlayers && iBestDiffAbs == iDiffAbs))
		{
			if (puMapContainerID)
			{
				(*puMapContainerID) = pServer->iContainerID;
			}
			iLeastMembers = pPartition->iNumPlayers;
			pChosenPartition = pPartition;
			iBestDiffAbs = iDiffAbs;
		}
	}
	FOR_EACH_PARTITION_END

	return pChosenPartition;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslMapManagerJoinOpenTeamByMap(ContainerID uServerID, const char* pchMapName, const char* pchMapVars, 
									ContainerID uEntID, const char *pchCreateAllegiance, const char *pchCreateSubAllegiance,
									S32 iFindDifficulty, S32 iDifficultyRange)
{
	GameServerList* pList = NULL;
	GlobalType eOwnerType = 0;
	ContainerID uOwnerID = 0;
	ContainerID uMapContainerID = 0;
	U32 uMapPartitionID = 0;

	stashFindPointer(sGameServerListsByMapDescription, pchMapName, &pList);
	if (pList)
	{
		MapPartitionSummary* pPartition = aslFindPartitionWithLeastMembers(pList, 
																		   pchMapVars, 
																		   iFindDifficulty, 
																		   iDifficultyRange,
																		   &uMapContainerID);
		if (pPartition)
		{
			eOwnerType = pPartition->eOwnerType;
			uOwnerID = pPartition->iOwnerID;
			uMapPartitionID = pPartition->uPartitionID;

			// Setting this only for PLAYER owned maps, because a few lines down we are requesting a team create.
			// This team create will invalidate the copy of the server's description that the mapmanager has. We need to wait for an update.
			if (eOwnerType != GLOBALTYPE_TEAM)
			{
				pPartition->bAssignedOpenInstance = true;
				// We need to mark this as processed so we don't send anyone else, because they're guaranteed to fail
			}
		}
	}

	if (!uOwnerID)
	{
		RemoteCommand_gslTeam_JoinOpenTeamByMap_Error(GLOBALTYPE_GAMESERVER, uServerID, uEntID, pchMapName, pchMapVars);
	}
	else
	{
		if (eOwnerType == GLOBALTYPE_TEAM)
		{
			bool bIsRejoin=false;
			RemoteCommand_aslTeam_JoinWithoutInvite(GLOBALTYPE_TEAMSERVER, 0, uOwnerID, uEntID, pchMapName, pchMapVars, uMapContainerID, uMapPartitionID, bIsRejoin);
		}
		else
		{
			RemoteCommand_aslTeam_Create(GLOBALTYPE_TEAMSERVER, 0, uOwnerID, uEntID, pchMapName, pchMapVars, uMapContainerID, uMapPartitionID, pchCreateAllegiance, pchCreateSubAllegiance);
		}
	}
}


ZoneMapInfoRequest* CreateZoneMapInfoRequestFromZoneMapInfo(ZoneMapInfo *pZoneMapInfo)
{
	ZoneMapInfoRequest* pRequestData = StructCreate(parse_ZoneMapInfoRequest);

	WorldVariableDef*** peaVars = zmapInfoGetVariableDefs(pZoneMapInfo);
	GenesisZoneMapData* pGenesisData = zmapInfoGetGenesisData(pZoneMapInfo);
	GenesisZoneMapInfo* pGenesisInfo = zmapInfoGetGenesisInfo(pZoneMapInfo);
	WorldRegion** eaRegions = (!pGenesisData && !pGenesisInfo) ? zmapInfoGetWorldRegions(pZoneMapInfo) : NULL;
	const char* pchDisplayNameMsgKey = zmapInfoGetDisplayNameMsgKey(pZoneMapInfo);
	
	pRequestData->pchDisplayNameMsgKey = StructAllocString(pchDisplayNameMsgKey);
	pRequestData->eMapType = zmapInfoGetMapType(pZoneMapInfo);
	pRequestData->bConfirmPurchasesOnExit = zmapInfoConfirmPurchasesOnExit(pZoneMapInfo);
	pRequestData->pGenesisData = StructClone(parse_GenesisZoneMapData, pGenesisData);
	pRequestData->pGenesisInfo = StructClone(parse_GenesisZoneMapInfo, pGenesisInfo);
	eaCopyStructs(&eaRegions, &pRequestData->eaRegions, parse_WorldRegion);
	eaCopyStructs(peaVars, &pRequestData->eaVarDefs, parse_WorldVariableDef);

	pRequestData->pRequiresExpr = exprClone(zmapInfoGetRequiresExpr(pZoneMapInfo));
	exprClean(pRequestData->pRequiresExpr);
	
	pRequestData->pPermissionExpr = exprClone(zmapInfoGetPermissionExpr(pZoneMapInfo));
	exprClean(pRequestData->pPermissionExpr);

	return pRequestData;
}


//if we ask the resource DB for a mapinfo and don't get it back within this many seconds
//something must be wrong
#define MAPINFO_GIVE_UP_ON_RESDB_TIME 120

AUTO_STRUCT;
typedef struct ZoneMapInfoRequestCacheLocalCB
{
	U32 iRequestTime;
	ZoneMapInfoRequestCBFunc pCB; NO_AST
	void *pUserData; NO_AST
} ZoneMapInfoRequestCacheLocalCB;

AUTO_STRUCT;
typedef struct ZoneMapInfoRequestCache
{
	const char *pMapName;
	REF_TO(ZoneMapInfo) hZoneMapInfo;
	ZoneMapInfoRequest *pRequestToReturn;
	U32 iInitialRequestTime;
	U32 iMostRecentRequestTime;
	SlowRemoteCommandID *pCmdIDs; NO_AST
	ZoneMapInfoRequestCacheLocalCB **ppLocalCBs;
} ZoneMapInfoRequestCache;

StashTable hZoneMapInfoRequestCachesByMapName = NULL;

ZoneMapInfoRequestCache **ppCachesThatAreWaiting = NULL;
	
void aslMapManagerCheckMapInfoCaches(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	int i, j;
	U32 iCurTime = timeSecondsSince2000();

	for (i=eaSize(&ppCachesThatAreWaiting) - 1; i >= 0; i--)
	{
		ZoneMapInfoRequestCache *pCache = ppCachesThatAreWaiting[i];

		if (GET_REF(pCache->hZoneMapInfo))
		{
			pCache->pRequestToReturn = CreateZoneMapInfoRequestFromZoneMapInfo(GET_REF(pCache->hZoneMapInfo));

			for (j=0 ; j < ea32Size(&pCache->pCmdIDs); j++)
			{
				SlowRemoteCommandReturn_aslMapManagerRequestZoneMapInfoByPublicName(pCache->pCmdIDs[j], pCache->pRequestToReturn);
			}

			for (j = 0; j < eaSize(&pCache->ppLocalCBs); j++)
			{
				pCache->ppLocalCBs[j]->pCB(GET_REF(pCache->hZoneMapInfo), pCache->ppLocalCBs[j]->pUserData);
			}

			eaDestroyStruct(&pCache->ppLocalCBs, parse_ZoneMapInfoRequestCacheLocalCB);
			ea32Destroy(&pCache->pCmdIDs);
			eaRemoveFast(&ppCachesThatAreWaiting, i);
		}
		else if (pCache->iInitialRequestTime < iCurTime - MAPINFO_GIVE_UP_ON_RESDB_TIME)
		{
			ErrorOrAlert("UNKNOWN_NAMESPACE_MAP", "the map manager was asked about map %s, requested it from the resource DB, didn't get it within %d seconds",
				pCache->pMapName, MAPINFO_GIVE_UP_ON_RESDB_TIME);


			for (j=0 ; j < ea32Size(&pCache->pCmdIDs); j++)
			{
				SlowRemoteCommandReturn_aslMapManagerRequestZoneMapInfoByPublicName(pCache->pCmdIDs[j], NULL);
			}

			for (j = 0; j < eaSize(&pCache->ppLocalCBs); j++)
			{
				pCache->ppLocalCBs[j]->pCB(NULL, pCache->ppLocalCBs[j]->pUserData);
			}

			ea32Destroy(&pCache->pCmdIDs);
			eaDestroyStruct(&pCache->ppLocalCBs, parse_ZoneMapInfoRequestCacheLocalCB);
	
			stashRemovePointer(hZoneMapInfoRequestCachesByMapName, pCache->pMapName, NULL);
			StructDestroy(parse_ZoneMapInfoRequestCache, pCache);
			eaRemoveFast(&ppCachesThatAreWaiting, i);

		}
	}
}



AUTO_COMMAND_REMOTE_SLOW(ZoneMapInfoRequest*) ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void aslMapManagerRequestZoneMapInfoByPublicName(SlowRemoteCommandID iCmdID, const char* pchMapName)
{
	ZoneMapInfoRequest* pRequestData = NULL;
	ZoneMapInfo* pZoneMapInfo = worldGetZoneMapByPublicName(pchMapName);
	
	if (pZoneMapInfo)
	{
		pRequestData = CreateZoneMapInfoRequestFromZoneMapInfo(pZoneMapInfo);
		SlowRemoteCommandReturn_aslMapManagerRequestZoneMapInfoByPublicName(iCmdID, pRequestData);
		StructDestroy(parse_ZoneMapInfoRequest, pRequestData);
	}
	else
	{
		ZoneMapInfoRequestCache *pCache;
		char ns[RESOURCE_NAME_MAX_SIZE], base[RESOURCE_NAME_MAX_SIZE];

		if (!resExtractNameSpace(pchMapName, ns, base))
		{
			SlowRemoteCommandReturn_aslMapManagerRequestZoneMapInfoByPublicName(iCmdID, NULL);
			return;
		}

		if (stashFindPointer(hZoneMapInfoRequestCachesByMapName, pchMapName, &pCache))
		{
			pCache->iMostRecentRequestTime = timeSecondsSince2000();
		
			if (pCache->pRequestToReturn)
			{
				SlowRemoteCommandReturn_aslMapManagerRequestZoneMapInfoByPublicName(iCmdID, pCache->pRequestToReturn);
			}
			else
			{
				ea32Push(&pCache->pCmdIDs, iCmdID);
			}
		}
		else
		{
			pCache = StructCreate(parse_ZoneMapInfoRequestCache);
			
			if (!hZoneMapInfoRequestCachesByMapName)
			{
				hZoneMapInfoRequestCachesByMapName = stashTableCreateWithStringKeys( 1000, StashDefault );
			}

			SET_HANDLE_FROM_REFDATA(g_ZoneMapDictionary, pchMapName, pCache->hZoneMapInfo);
			pCache->iInitialRequestTime = pCache->iMostRecentRequestTime = timeSecondsSince2000();
			pCache->pMapName = strdup(pchMapName);
			
			ea32Push(&pCache->pCmdIDs, iCmdID);

			stashAddPointer(hZoneMapInfoRequestCachesByMapName, pCache->pMapName, pCache, false);
			eaPush(&ppCachesThatAreWaiting, pCache);
		}
	}
}

void LocallyRequestZoneMapInfoByPublicName(const char *pchMapName, ZoneMapInfoRequestCBFunc pCB, void *pUserData)
{
	ZoneMapInfoRequest* pRequestData = NULL;
	ZoneMapInfo* pZoneMapInfo = worldGetZoneMapByPublicName(pchMapName);
	
	if (pZoneMapInfo)
	{
		pCB(pZoneMapInfo, pUserData);
	}
	else
	{
		ZoneMapInfoRequestCache *pCache;
		char ns[RESOURCE_NAME_MAX_SIZE], base[RESOURCE_NAME_MAX_SIZE];

		if (!resExtractNameSpace(pchMapName, ns, base))
		{
			pCB(NULL, pUserData);
			return;
		}

		if (stashFindPointer(hZoneMapInfoRequestCachesByMapName, pchMapName, &pCache))
		{
			pCache->iMostRecentRequestTime = timeSecondsSince2000();
		
			if (pCache->pRequestToReturn)
			{
				AssertOrAlert("ZMINFO_REQUEST_CACHE_CORRUPT", "Something weird happened, we have a ZoneMapInfoRequestToReturn but no ZoneMapInfo");
				pCB(NULL, pUserData);
			}
			else
			{
				ZoneMapInfoRequestCacheLocalCB *pLocalCBCache = StructCreate(parse_ZoneMapInfoRequestCacheLocalCB);
				pLocalCBCache->iRequestTime = timeSecondsSince2000();
				pLocalCBCache->pCB = pCB;
				pLocalCBCache->pUserData = pUserData;

				eaPush(&pCache->ppLocalCBs, pLocalCBCache);

			}
		}
		else
		{
			ZoneMapInfoRequestCacheLocalCB *pLocalCBCache = StructCreate(parse_ZoneMapInfoRequestCacheLocalCB);
			pCache = StructCreate(parse_ZoneMapInfoRequestCache);

			if (!hZoneMapInfoRequestCachesByMapName)
			{
				hZoneMapInfoRequestCachesByMapName = stashTableCreateWithStringKeys( 1000, StashDefault );
			}

			SET_HANDLE_FROM_REFDATA(g_ZoneMapDictionary, pchMapName, pCache->hZoneMapInfo);
			pCache->iInitialRequestTime = pCache->iMostRecentRequestTime = timeSecondsSince2000();
			pCache->pMapName = strdup(pchMapName);

			pLocalCBCache->iRequestTime = timeSecondsSince2000();
			pLocalCBCache->pCB = pCB;
			pLocalCBCache->pUserData = pUserData;

			eaPush(&pCache->ppLocalCBs, pLocalCBCache);

			stashAddPointer(hZoneMapInfoRequestCachesByMapName, pCache->pMapName, pCache, false);
			eaPush(&ppCachesThatAreWaiting, pCache);
		}
	}
}




AUTO_STRUCT;
typedef struct MapManagerOverview 
{
	char *pGameServers; AST(ESTRING, FORMATSTRING(HTML=1))
	char *pServerLists; AST(ESTRING, FORMATSTRING(HTML=1))
	char *pEventInfo; AST(ESTRING, FORMATSTRING(HTML=1))
	char *pGenericInfo; AST(ESTRING, FORMATSTRING(HTML=1))
	U32 iUGCEditQueueSize;
	GlobalMapManagerConfig *pCurMapManagerConfig;
	AST_COMMAND("Reload MapManagerConfig.txt", "ReloadMapManagerConfig")
	AST_COMMAND("Kill all UGC Edit Maps", "KillAllUGCEditMaps $CONFIRM(Really kill all UGC edit maps?)")
	AST_COMMAND("Kill all UGC Play Maps", "KillAllUGCPlayMaps $CONFIRM(Really kill all UGC play maps?)")
	AST_COMMAND("Enable UGC Virtual Shard", "EnableUGCVirtualShard $CONFIRM(Really enable UGC Virtual Shard?)")
	AST_COMMAND("Disable UGC Virtual Shard", "DisableUGCVirtualShard $CONFIRM(Really disable UGC Virtual Shard?)")
} MapManagerOverview;

void MapManager_GetCustomServerInfoStructForHttp(UrlArgumentList *pUrl, ParseTable **ppTPI, void **ppStruct)
{
	static MapManagerOverview overview = {0};

	overview.iUGCEditQueueSize = aslMapManager_UGCEditQueue_Size();

	overview.pCurMapManagerConfig = &gMapManagerConfig;

	estrPrintf(&overview.pGenericInfo, "<a href=\"/viewxpath?xpath=%s[%u].generic\">Generic ServerLib info for the %s</a>",
		GlobalTypeToName(GetAppGlobalType()), gServerLibState.containerID,GlobalTypeToName(GetAppGlobalType()));
	estrPrintf(&overview.pGameServers, "<a href=\"%s%sGameServerExes\">GameServerExes</a>",
		LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME);
	estrPrintf(&overview.pServerLists, "<a href=\"%s%sGameServerExeLists\">GameServerExeLists</a>",
		LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME);
	estrPrintf(&overview.pEventInfo, "<a href=\"%s%sEventInfo\">EventInfo</a>",
		LinkToThisServer(), GLOBALOBJECTS_DOMAIN_NAME);

	*ppTPI = parse_MapManagerOverview;
	*ppStruct = &overview;
	
}

#include "AutoGen/aslMapManager_h_ast.c"
#include "AutoGen/aslMapManagerConfig_h_ast.c"
#include "AutoGen/AslMapManager_c_ast.c"

//Alex's test jobmanager code

#include "aslJobManagerPub.h"
#include "aslJobManagerPub_h_ast.h"
#include "RemoteCommandGroup.h"
#include "JobManagerSupport.h"

static void GetStatusReturnCB(TransactionReturnVal *returnVal, char *pJobName)
{
      JobManagerGroupResult *pResult = NULL;
      enumTransactionOutcome eOutcome = RemoteCommandCheck_RequestJobGroupStatus(returnVal, &pResult);

      if (eOutcome == TRANSACTION_OUTCOME_SUCCESS)
      {
            char *pStatusString = NULL;
            ParserWriteText(&pStatusString, parse_JobManagerGroupResult, pResult, 0, 0, 0);
            printf("Status for %s:\n%s\n\n", pJobName, pStatusString);
            estrDestroy(&pStatusString);
            StructDestroy(parse_JobManagerGroupResult, pResult);
      }
}

void GetJobStatusCB(TimedCallback *callback, F32 timeSinceLastCallback, char *pName)
{
      RemoteCommand_RequestJobGroupStatus(objCreateManagedReturnVal(GetStatusReturnCB, pName), 
            GLOBALTYPE_JOBMANAGER, 0, pName);
}


AUTO_COMMAND;
void JobManagerTest(void)
{
	int i;

	for (i=0; i < 50; i++)
	{

		JobManagerJobGroupDef groupDef = {0};

		StructInit(parse_JobManagerJobGroupDef, &groupDef);

		groupDef.pGroupTypeName = allocAddString("TestJobGroup");
		groupDef.pJobGroupName = strdup(GetUniqueJobGroupName("Test Job group"));

		groupDef.pComment = strdup("Test job");

		eaPush(&groupDef.ppJobs, StructCreate(parse_JobManagerJobDef));

		groupDef.ppJobs[0]->eType = JOB_REMOTE_CMD;
		groupDef.ppJobs[0]->pJobName = strdup("RemoteCmdTest");
		groupDef.ppJobs[0]->pRemoteCmdDef = StructCreate(parse_JobManagerRemoteCommandDef);
		groupDef.ppJobs[0]->pRemoteCmdDef->eTypeForCommand = GLOBALTYPE_LOGINSERVER;
		groupDef.ppJobs[0]->pRemoteCmdDef->pCommandString = strdup("TestJobStep 5 \"$JOBNAME$\" 7");
		groupDef.ppJobs[0]->pRemoteCmdDef->bSlow = true;


		groupDef.pWhatToDoOnJobManagerCrash = CreateRCGWithPrintf("The Job manager crashed, so %s failed\n",
			groupDef.pJobGroupName);
		groupDef.pWhatToDoOnFailure = CreateRCGWithPrintf("%s failed\n", groupDef.pJobGroupName);
		groupDef.pWhatToDoOnSuccess = CreateRCGWithPrintf("%s succeeded\n", groupDef.pJobGroupName);

		RemoteCommand_BeginNewJobGroup(NULL, GLOBALTYPE_JOBMANAGER, 0, &groupDef);

		TimedCallback_Add(GetJobStatusCB, strdup(groupDef.pJobGroupName), 60.0f);

		StructDeInit(parse_JobManagerJobGroupDef, &groupDef);
	}
}


//NOTE NOTE NOTE THIS MUST BE KEPT IN SYNC WITH THE ONE IN gslResourceDBSupport.c
AUTO_COMMAND_REMOTE ACMD_IFDEF(NEVER_USED);
void ResourceDB_ReceiveZoneMapInfo(char *pName, ACMD_OWNABLE(ZoneMapInfo) ppZmInfo, char *pComment)
{

	verbose_printf("Receiving ZoneMapInfo %s. Comment: %s\n", pName, pComment);

	if (ResourceDBHandleGetObject(g_ZoneMapDictionary, pName, *ppZmInfo, pComment))
	{
		verbose_printf("Added to resource system\n");
		*ppZmInfo = NULL;
	}
}


AUTO_FIXUPFUNC;
TextParserResult fixupMapCategoryConfig(MapCategoryConfig *pConfig, enumTextParserFixupType eType, void *pExtraData)
{
	int success = true;

	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			if (pConfig->iMaxPartitions == 0)
			{
				pConfig->iMaxPartitions = 1;
			}
		}
	}

	return true;
}

//the alert string should look like this:
//"Map %s thought to have type %s, someone requesting type %s"
//(defined in MapCheckRequestedType() in MapDescription.c)

#define KEY_STRING_1 " thought to have type "
#define KEY_STRING_1_LEN 22

#define KEY_STRING_2 ", someone requesting type "
#define KEY_STRING_2_LEN 26

//called locally, extracts the map name and returns true if this alert is whitelisted
bool MapManager_SpecialMapTypeCorruptionAlertHandling(const char *pKey, const char *pString)
{
	char *pKey1 = strstri(pString, KEY_STRING_1);
	char *pKey2 = strstri(pString, KEY_STRING_2);

	char *pMapName = NULL;
	char *pTypeName1 = NULL;
	char *pTypeName2 = NULL;
	char *pTemp;
	MapTypeCorruptionWhiteListEntry *pEntry;
	bool bShouldSuppress = false;

	if (!pKey1 || !pKey2)
	{
		return false;
	}

	estrStackCreate(&pMapName);
	estrStackCreate(&pTypeName1);
	estrStackCreate(&pTypeName2);

	estrConcat(&pTypeName1, pKey1 + KEY_STRING_1_LEN, pKey2 - pKey1 - KEY_STRING_1_LEN);
	estrTrimLeadingAndTrailingWhitespace(&pTypeName1);

	estrCopy2(&pTypeName2, pKey2 + KEY_STRING_2_LEN);
	estrTrimLeadingAndTrailingWhitespace(&pTypeName2);

	//typename2 may have dangling stuff still in it... truncate it at first non-alphanum
	pTemp = pTypeName2;
	while (isalnum(*pTemp))
	{
		pTemp++;
	}
	estrSetSize(&pTypeName2, pTemp - pTypeName2);

	estrConcat(&pMapName, pString, pKey1 - pString);
	estrRemoveUpToLastOccurrence(&pMapName, ' ');

	pEntry = eaIndexedGetUsingString(&gMapManagerConfig.ppMapTypeCorruptionWhiteList, pMapName);

	if (pEntry)
	{
		//if this entry doesn't specify types, then it always matches
		if (pEntry->eType1 == ZMTYPE_UNSPECIFIED && pEntry->eType2 == ZMTYPE_UNSPECIFIED)
		{
			bShouldSuppress = true;
		}
		else
		{
			ZoneMapType eFoundType1 = StaticDefineInt_FastStringToInt(ZoneMapTypeEnum, pTypeName1, 0);
			ZoneMapType eFoundType2 = StaticDefineInt_FastStringToInt(ZoneMapTypeEnum, pTypeName2, 0);

			if (eFoundType1 == pEntry->eType1 && eFoundType2 == pEntry->eType2
				|| eFoundType1 == pEntry->eType2 && eFoundType2 == pEntry->eType1)
			{
				bShouldSuppress = true;
			}
		}
	}

	estrDestroy(&pMapName);
	estrDestroy(&pTypeName1);
	estrDestroy(&pTypeName2);
	return bShouldSuppress;
}
AUTO_COMMAND_REMOTE;
void HandleMapTypeCorruptionAlert(char *pKey, char *pString)
{
	WARNING_NETOPS_ALERT(pKey, "%s", pString);
}
