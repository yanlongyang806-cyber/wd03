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

#include "mapdescription_h_ast.h"
#include "aslMapManagerConfig_h_ast.h"
#include "stringCache.h"
#include "alerts.h"
#include "autogen/AppServerLib_autogen_slowfuncs.h"
#include "aslMapManagerNewMapTransfer_GetAddress.h"
#include "autogen/controller_autogen_remotefuncs.h"
#include "ugcProjectUtils.h"

//includes basic management code for the NewMapTransfer structs, adding and removing lists, etc.
StashTable sGameServerExesByID = NULL;
StashTable sGameServerListsByMapDescription = NULL;
GameServerList **s_eaGameServerListByMapDescription = NULL;
StashTable sGameServerMachinesByMachineName = NULL;

StashTable sActiveGameServerListsByMapDescription = NULL; //subset of lists which have at least one active map, for easy servermonitoring
StashTable sPlayersBeingSentByID = NULL;

U32 *g_piDoorDestinationRequestMapIDs = NULL;
PendingDoorDestinationRequest **g_eaPendingDoorDestinationRequestMapIDs = NULL;


AUTO_FIXUPFUNC;
TextParserResult fixupGameServerMachine(MachineForGameServers* pMachine, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:
		pMachine->__SERVERMONONLY__numGameServers = eaSize(&pMachine->ppGameServers);
		break;
	}

	return 1;
}


AUTO_FIXUPFUNC;
TextParserResult fixupGameServerList(GameServerList* pList, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:
		pList->__SERVERMONONLY__numGameServers = eaSize(&pList->ppGameServers);
		break;
	}

	return 1;
}

AUTO_FIXUPFUNC;
TextParserResult fixupMapPartitionSummary(MapPartitionSummary* pPartition, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if (pPartition->sTeamsSentThereByID)
		{
			stashTableDestroy(pPartition->sTeamsSentThereByID);
		}
		break;
	}

	return 1;
}


AUTO_RUN;
void aslMapManagerNewMapTransferInitTables(void)
{
	if (GetAppGlobalType() == GLOBALTYPE_MAPMANAGER)
	{
		sGameServerExesByID = stashTableCreateInt(256);
		resRegisterDictionaryForStashTable("GameServerExes", RESCATEGORY_OTHER, 0, sGameServerExesByID, parse_TrackedGameServerExe);

		sGameServerListsByMapDescription = stashTableCreateWithStringKeys(256, StashDefault);
		resRegisterDictionaryForStashTable("GameServerExeLists",  RESCATEGORY_OTHER, 0, sGameServerListsByMapDescription, parse_GameServerList);

		sActiveGameServerListsByMapDescription = stashTableCreateWithStringKeys(256, StashDefault);
		resRegisterDictionaryForStashTable("ActiveGameServerExeLists",  RESCATEGORY_OTHER, 0, sActiveGameServerListsByMapDescription, parse_GameServerList);

		sGameServerMachinesByMachineName = stashTableCreateWithStringKeys(256, StashDefault);
		resRegisterDictionaryForStashTable("GameServerExeMachines", RESCATEGORY_OTHER, 0, sGameServerMachinesByMachineName, parse_MachineForGameServers);

		sPlayersBeingSentByID = stashTableCreateInt(256);
	}
}



static void GameServerListHadServerAddOrRemoved(GameServerList *pList)
{
	if (eaSize(&pList->ppGameServers) == 1)
	{
		stashAddPointer(sActiveGameServerListsByMapDescription, pList->pMapDescription, pList, false);
	}
	else if (eaSize(&pList->ppGameServers) == 0)
	{
		stashRemovePointer(sActiveGameServerListsByMapDescription, pList->pMapDescription, NULL);
	}

}

//note that this does NOT actually destroy the partition, just removes all side effects it might have (which are not many)
static void NewMapTransfer_CleanupAndUnlinkPartition(TrackedGameServerExe *pGameServer, MapPartitionSummary *pPartition)
{
	if (pGameServer->pList)
	{
		int iIndex = pPartition->iPublicIndex;
		int iIndexWord = (iIndex - 1) / 32;
		int iIndexBit = (iIndex - 1) % 32;

		//possible that this will be out of range, if mapmanager just restarted and is in corner case
		if (iIndexWord < ea32Size(&pGameServer->pList->pUsedPublicIndices))
		{
			pGameServer->pList->pUsedPublicIndices[iIndexWord] &= ~(1 << iIndexBit);
		}
	}
}

void NewMapTransfer_DestroyPartition(TrackedGameServerExe *pGameServer, MapPartitionSummary *pPartition)
{
	int iIndex = eaFind(&pGameServer->description.ppPartitions, pPartition);
	if (iIndex == -1)
	{
		AssertOrAlert("BAD_DESTROYPARTITION", "Someone calling NewMapTransfer_DestroyPartition on a partition that is not there");
	}
	else
	{
		NewMapTransfer_CleanupAndUnlinkPartition(pGameServer, pPartition);
		eaRemoveFast(&pGameServer->description.ppPartitions, iIndex);
		StructDestroy(parse_MapPartitionSummary, pPartition);
	}
}



void NewMapTransfer_AddGameServerToList(TrackedGameServerExe *pServer)
{
	pServer->pList = NewMapTransfer_FindListForGameServer(pServer);

	if (pServer->pList)
	{
		eaPush(&pServer->pList->ppGameServers, pServer);
		GameServerListHadServerAddOrRemoved(pServer->pList);
	}
}

void NewMapTransfer_RemoveGameServerFromList(TrackedGameServerExe *pGameServer)
{
	int iIndex;

	if (pGameServer->pList)
	{
		iIndex = eaIndexedFindUsingInt(&pGameServer->pList->ppGameServers, pGameServer->iContainerID);
		if (iIndex != -1)
		{
			eaRemove(&pGameServer->pList->ppGameServers, iIndex);

			GameServerListHadServerAddOrRemoved(pGameServer->pList);
			pGameServer->pList = NULL;
		}
	}

}

void RemoveAndDestroyGameServerExe(TrackedGameServerExe *pGameServer, char *pReason)
{
	PendingDoorDestinationRequest *pPendingData;
	int iIndex;
	int i;

	log_printf(LOG_MM_GAMESERVERS, "No longer tracking GS %u because: %s", pGameServer->iContainerID, pReason);

	for (i=0; i < eaSize(&pGameServer->ppPendingRequests); i++)
	{
		NewMapTransfer_RequestNewOrExistGameServerAddress_Fail(pGameServer->ppPendingRequests[i]->iCmdID, "GS shutting down before getting to ready state");
	}

	for (i=0; i < eaSize(&pGameServer->description.ppPartitions); i++)
	{
		NewMapTransfer_CleanupAndUnlinkPartition(pGameServer, pGameServer->description.ppPartitions[i]);
	}

	NewMapTransfer_RemoveGameServerFromList(pGameServer);

	if (pGameServer->pMachine)
	{
		iIndex = eaIndexedFindUsingInt(&pGameServer->pMachine->ppGameServers, pGameServer->iContainerID);

		if (iIndex != -1)
		{
			eaRemove(&pGameServer->pMachine->ppGameServers, iIndex);
		}
	}

	stashIntRemovePointer(sGameServerExesByID, pGameServer->iContainerID, NULL);

	// Remove this map from the list of maps that want destination tracking (if it happens to be there)
	ea32FindAndRemove(&g_piDoorDestinationRequestMapIDs, pGameServer->iContainerID);

	i = eaIndexedFindUsingInt(&g_eaPendingDoorDestinationRequestMapIDs, pGameServer->iContainerID);
	if (pPendingData = eaGet(&g_eaPendingDoorDestinationRequestMapIDs, i))
	{
		eaRemove(&g_eaPendingDoorDestinationRequestMapIDs, i);
		StructDestroySafe(parse_PendingDoorDestinationRequest, &pPendingData);
	}

	StructDestroy(parse_TrackedGameServerExe, pGameServer);
}

void FindMapMapConfigForList(GameServerList *pList)
{
	int i;

	if (pList->eType != LISTTYPE_NORMAL)
	{
		return;
	}

	StructDestroySafe(parse_MinMapCountConfig, &pList->pMinMapCount);

	for (i = 0; i < eaSize(&gMapManagerConfig.ppMinMapCounts); i++)
	{
		if (stricmp(gMapManagerConfig.ppMinMapCounts[i]->pMapName, pList->pMapDescription) == 0)
		{
			pList->pMinMapCount = StructClone(parse_MinMapCountConfig, gMapManagerConfig.ppMinMapCounts[i]);
			return;
		}
	}
}


GameServerList *NewMapTransfer_CreateAndRegisterNewGameServerList(const char *pMapName, ZoneMapInfo *pZoneMap, GameServerListType eListType)
{
	GameServerList *pList = StructCreate(parse_GameServerList);
	MapCategoryConfig *pCategoryConfig;
	pList->pMapDescription = allocAddString(pMapName);
	if (pZoneMap)
	{
		SET_HANDLE_FROM_REFDATA("ZoneMap", pZoneMap->map_name, pList->hZoneMapInfo);
		assertmsgf(GET_REF(pList->hZoneMapInfo), "Zone map %s exists but is not in the ref dictionary", pZoneMap->map_name);
	}

	pList->eType = eListType;

	if (strStartsWith(pMapName, CONST_MAPDESCRIPTION_PREFIX_PRELOAD))
	{
		pCategoryConfig = FindCategoryByName(allocAddString(pMapName + strlen(CONST_MAPDESCRIPTION_PREFIX_PRELOAD)));
		pList->pSpecifiedCategoryName = pCategoryConfig->pCategoryName;
	}
	else
	{
		pCategoryConfig = FindCategoryByNameAndType(pMapName, pZoneMap ? pZoneMap->map_type : ZMTYPE_UNSPECIFIED, 
			&pList->pSpecifiedCategoryName);
	}

	if (pCategoryConfig)
	{
		pList->pCategoryConfig = StructClone(parse_MapCategoryConfig, pCategoryConfig);
	}

	FindMapMapConfigForList(pList);


	if (stashRemovePointer(sGameServerListsByMapDescription, pList->pMapDescription, NULL))
	{
		AssertOrAlert("GSLIST_NAME_DUP", "Two gameserverlists with same name: %s", pList->pMapDescription);
	}
	stashAddPointer(sGameServerListsByMapDescription, pList->pMapDescription, pList, false);

	eaPush(&s_eaGameServerListByMapDescription, pList);

	pList->iUGCContainerIDForPlayingServers = UGCProject_GetProjectContainerIDFromUGCResource(pMapName);

	return pList;
}


void NewMapTransfer_FixupListsForReloadedGlobalConfig(void)
{
	FOR_EACH_IN_STASHTABLE(sGameServerListsByMapDescription, GameServerList, pList)
	{
		MapCategoryConfig *pCategoryConfig;
		StructDestroySafe(parse_MapCategoryConfig, &pList->pCategoryConfig);

		pCategoryConfig = FindCategoryByNameAndType(pList->pMapDescription, GetListZoneMapType(pList), 
			&pList->pSpecifiedCategoryName);

		if (pCategoryConfig)
		{
			pList->pCategoryConfig = StructClone(parse_MapCategoryConfig, pCategoryConfig);
		}

		FindMapMapConfigForList(pList);

		//preload maps may no longer be meaningful at all, just kill them all, the preload system will re-preload them shortly
		if (pList->eType == LISTTYPE_PRELOAD)
		{
			while (eaSize(&pList->ppGameServers))
			{
				RemoteCommand_KillServer(GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_GAMESERVER, pList->ppGameServers[0]->iContainerID, "Killing all preloads during map manager config reload");
				RemoveAndDestroyGameServerExe(pList->ppGameServers[0], "Killing all preloads during map manager config reload");
			}
		}


	}
	FOR_EACH_END
}


MapPartitionSummary *AddPartitionSummaryInternal(TrackedGameServerExe *pServer, const char *pMapVariables, GlobalType eOwnerType, ContainerID iOwnerID, int iVirtualShardID)
{
	MapPartitionSummary *pSummary = StructCreate(parse_MapPartitionSummary);
	pSummary->pMapVariables = pMapVariables ? strdup(pMapVariables) : NULL;
	pSummary->iVirtualShardID = iVirtualShardID;
	pSummary->eOwnerType = eOwnerType;
	pSummary->iOwnerID = iOwnerID;

	pSummary->uPartitionID = pServer->uNextPartitionID++;
	if (pServer->pList)
	{
		int iWordNum, iBitNum;
		for (iWordNum=0; iWordNum < ea32Size(&pServer->pList->pUsedPublicIndices); iWordNum++)
		{
			for (iBitNum = 0; iBitNum < 32; iBitNum++)
			{
				if (!(pServer->pList->pUsedPublicIndices[iWordNum] & (1 << iBitNum)))
				{
					pServer->pList->pUsedPublicIndices[iWordNum] |= (1 << iBitNum);
					pSummary->iPublicIndex = iWordNum * 32 + iBitNum + 1;
					break;
				}
			}

			if (pSummary->iPublicIndex)
			{
				break;
			}
		}

		if (!pSummary->iPublicIndex)
		{
			ea32Push(&pServer->pList->pUsedPublicIndices, 1);
			pSummary->iPublicIndex = (ea32Size(&pServer->pList->pUsedPublicIndices) - 1) * 32 + 1;
		}

	}
	else
	{
		pSummary->iPublicIndex = 1;
	}

	eaPush(&pServer->description.ppPartitions, pSummary);

	return pSummary;
}

bool PublicIndexIsInUse(GameServerList *pList, int iIndex)
{
	int iBitNum = (iIndex - 1) % 32;
	int iWordNum = (iIndex - 1) / 32;

	if (iWordNum >= ea32Size(&pList->pUsedPublicIndices))
	{
		return false;
	}

	return !!(pList->pUsedPublicIndices[iWordNum] & ( 1 << iBitNum));
}

void MarkPublicIndexInUse(GameServerList *pList, int iIndex)
{
	int iBitNum = (iIndex - 1) % 32;
	int iWordNum = (iIndex - 1) / 32;
	

	while (iWordNum >= ea32Size(&pList->pUsedPublicIndices))
	{
		ea32Push(&pList->pUsedPublicIndices, 0);
	}

	pList->pUsedPublicIndices[iWordNum] |= ( 1 << iBitNum );

}

//returns true if the add went OK, false if there was a conflict (a conflict could occur if a race condition of some sort
//happened during a mapmanager restart)
bool NewMapTransfer_AddPreexistingPartitionToServer(TrackedGameServerExe *pServer, MapPartitionSummary *pSummary, char **ppErrorString)
{
	if (pSummary->uPartitionID == 0)
	{
		estrPrintf(ppErrorString, "Trying to add preexisting partition with ID zero to GS %u. Zero is not allowed.",
			pServer->iContainerID);
		return false;
	}

	if (pSummary->iPublicIndex < 0 || pSummary->iPublicIndex > 100000)
	{
		estrPrintf(ppErrorString, "Trying to add preexisting partition with public index %d to GS %u. This seems like a corrupted value.",
			pSummary->iPublicIndex, pServer->iContainerID);
		return false;
	}


	if (GetPartitionFromServerAndID(pServer, pSummary->uPartitionID))
	{
		estrPrintf(ppErrorString, "Trying to add a preexisting partition with ID %u to GS %u, but one already exists.",
			pSummary->uPartitionID, pServer->iContainerID);
		return false;
	}

	if (pServer->pList)
	{
		if (PublicIndexIsInUse(pServer->pList, pSummary->iPublicIndex))
		{
			estrPrintf(ppErrorString, "Trying to add a preexisting partition with public index %d to GS %u, but that index is not free.",
				pSummary->iPublicIndex, pServer->iContainerID);
			return false;
		}
	}

	if (pSummary->uPartitionID >= pServer->uNextPartitionID)
	{
		pServer->uNextPartitionID = pSummary->uPartitionID + 1;
	}

	if (pServer->pList)
	{
		MarkPublicIndexInUse(pServer->pList, pSummary->iPublicIndex);
	}

	eaPush(&pServer->description.ppPartitions, StructClone(parse_MapPartitionSummary, pSummary));

	return true;
}



int NewMapTransfer_CompareGameServerListEntry(const GameServerList **pLeft, const GameServerList **pRight)
{
	return stricmp((*pLeft)->pMapDescription, (*pRight)->pMapDescription);
}

void NewMapTransfer_InitMapLists(void)
{
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
		const char **ppPrivacy = zmapInfoGetPrivacy(pZoneMap);

		//in production mode, private maps effectively do not exist
		if (isProductionMode() && eaSize(&ppPrivacy))
		{
			continue;
		}

		if (pMapNameFile)
		{
			if (!MapNameShouldBeExcludedFromMapNameFileForMCP(pMapName))
			{
				fprintf(pMapNameFile, "%s\n", pMapName);
			}
		}

		NewMapTransfer_CreateAndRegisterNewGameServerList(pMapName, pZoneMap, LISTTYPE_NORMAL);
	}
	resFreeIterator(&iterator);

	if (pMapNameFile)
	{
		fclose(pMapNameFile);
	}

	eaQSort(s_eaGameServerListByMapDescription, NewMapTransfer_CompareGameServerListEntry);

	NewMapTransfer_CreateAndRegisterNewGameServerList(CONST_MAPDESCRIPTION_UGCEDIT, NULL, LISTTYPE_UGC_EDIT);
	NewMapTransfer_CreateAndRegisterNewGameServerList(CONST_MAPDESCRIPTION_PREEXISTING, NULL, LISTTYPE_PREEXISTING);
}

GameServerList *NewMapTransfer_FindEditingGameServerList(void)
{
	GameServerList *pList = NULL;

	stashFindPointer(sGameServerListsByMapDescription, CONST_MAPDESCRIPTION_UGCEDIT, &pList);

	return pList;
}


GameServerList *NewMapTransfer_FindListForGameServer(TrackedGameServerExe *pServer)
{
	const char *pNameToUse = NULL;
	GameServerList *pList;
	ZoneMapInfo *pZoneMapInfo;

	switch (pServer->description.eServerType)
	{
	xcase GSTYPE_PREEXISTING:
		pNameToUse = CONST_MAPDESCRIPTION_PREEXISTING;
	xcase GSTYPE_UGC_PLAY:
	case GSTYPE_NORMAL:
	case GSTYPE_PRELOAD:
		pNameToUse = pServer->description.pMapDescription;
	xcase GSTYPE_UGC_EDIT:
		pNameToUse = CONST_MAPDESCRIPTION_UGCEDIT;
	xdefault:
		AssertOrAlert("BAD_TRACKED_GS", "Map manager being asked to track a map of unsupported type %s", StaticDefineIntRevLookup(GameServerExeTypeEnum, pServer->description.eServerType));
		return NULL;
	}

	if (stashFindPointer(sGameServerListsByMapDescription, pNameToUse, &pList))
	{
		return pList;
	}

	if (pServer->description.eServerType == GSTYPE_PRELOAD)
	{
		return NewMapTransfer_CreateAndRegisterNewGameServerList(pNameToUse, NULL, LISTTYPE_PRELOAD);
	}

	//this is relevant only in rare cases, such as mapmanager restarting and being contacted by already-running
	//UGC gameserver
	pZoneMapInfo = zmapInfoGetByPublicName(pNameToUse);


	//in production mode, should never see unknown non-UGC gameservers
	if (pServer->description.eServerType != GSTYPE_UGC_PLAY && isProductionMode() && !pZoneMapInfo)
	{
		WARNING_NETOPS_ALERT("UNKNOWN GS NAME", "Map manager being asked to track unknown map %s, not a UGC map", pNameToUse);
	}
	//development mode, or UGC
    // jweinstein 9/1/2011 - temporary fix: do this for all cases so we don't crash when map manager restarts
	return NewMapTransfer_CreateAndRegisterNewGameServerList(pNameToUse, pZoneMapInfo, LISTTYPE_NORMAL);
}

#define PENDING_DOOR_DEST_REQUEST_TIMEOUT 60
static void NewMapTransfer_UpdateDoorDestinationRequestData(TrackedGameServerExe *pServer)
{
	PendingDoorDestinationRequest *pPendingData;
	U32 uCurrentTime = timeSecondsSince2000();
	S32 i, iFoundIdx = -1;

	for (i = eaSize(&g_eaPendingDoorDestinationRequestMapIDs)-1; i >= 0; i--)
	{
		pPendingData = g_eaPendingDoorDestinationRequestMapIDs[i];
		
		if (pPendingData->uMapID == pServer->iContainerID)
		{
			iFoundIdx = i;
		}
		else if (uCurrentTime > pPendingData->uLastUpdateTime + PENDING_DOOR_DEST_REQUEST_TIMEOUT)
		{
			eaRemove(&g_eaPendingDoorDestinationRequestMapIDs, i);
			StructDestroy(parse_PendingDoorDestinationRequest, pPendingData);
		}
	}

	if (pPendingData = eaGet(&g_eaPendingDoorDestinationRequestMapIDs, iFoundIdx))
	{
		pServer->pDestMapStatusRequests = pPendingData->pMapSummaryList;
		pPendingData->pMapSummaryList = NULL;
		eaRemove(&g_eaPendingDoorDestinationRequestMapIDs, iFoundIdx);
		StructDestroySafe(parse_PendingDoorDestinationRequest, &pPendingData);
		
		// If not already tracking, add to the tracking list
		ea32PushUnique(&g_piDoorDestinationRequestMapIDs, pServer->iContainerID);
	}
}

TrackedGameServerExe *CreateAndLinkTrackedGameServerExe(GameServerExeState eState, GameServerExe_Description *pDescription, ContainerID iContainerID)
{
	TrackedGameServerExe *pServer = StructCreate(parse_TrackedGameServerExe);

	char *pCheckMapTypeErrorString = NULL;
	char *pCheckMapTypeAlert;


	pServer->iContainerID = iContainerID ? iContainerID : GetNextGameServerID();

	if (stashIntRemovePointer(sGameServerExesByID, pServer->iContainerID, NULL))
	{
		AssertOrAlert("DUP_GS_ID", "Two tracked game servers somehow both had ID %u", pServer->iContainerID);
	}
	stashIntAddPointer(sGameServerExesByID, pServer->iContainerID, pServer, false);

	pServer->eState = eState;
	StructCopy(parse_GameServerExe_Description, pDescription, &pServer->description, 0, 0, 0);

	if (pDescription->eServerType != GSTYPE_PRELOAD && stricmp(pServer->description.pMapDescription, CONST_MAPDESCRIPTION_PREEXISTING) != 0)
	{

		pServer->description.eMapType = 
			MapCheckRequestedType(pServer->description.pMapDescription, pServer->description.eMapType,
			&pCheckMapTypeErrorString, &pCheckMapTypeAlert);

		if (estrLength(&pCheckMapTypeErrorString))
		{
			ErrorOrAlert(allocAddString(pCheckMapTypeAlert), "CreateAndLinkTrackedGameServerExe got an error from MapCheckRequestedType. Error: %s",
				pCheckMapTypeErrorString);
			estrDestroy(&pCheckMapTypeErrorString);
		}
	}

	NewMapTransfer_UpdateDoorDestinationRequestData(pServer);
	NewMapTransfer_AddGameServerToList(pServer);

	return pServer;
}


void AddGameServerToMachine(TrackedGameServerExe *pGameServer, const char *pMachineName, U32 iIP, U32 iPublicIP)
{
	MachineForGameServers *pMachine;

	if (pGameServer->pMachine)
	{
		AssertOrAlert("MM_GS_MACHINE_CORRUPTION", "MapManager being told to put gameserver %u onto machine %s, but it's already on a machine (%s)",
			pGameServer->iContainerID, pMachineName, pGameServer->pMachine->pMachineName);
		return;
	}


	if (!stashFindPointer(sGameServerMachinesByMachineName, pMachineName, &pMachine))
	{
		pMachine = StructCreate(parse_MachineForGameServers);
		pMachine->pMachineName = allocAddString(pMachineName);
		pMachine->iIP = iIP;
		pMachine->iPublicIP = iPublicIP;

		stashAddPointer(sGameServerMachinesByMachineName, pMachine->pMachineName, pMachine, false);
	}

	pGameServer->pMachine = pMachine;
	eaPush(&pMachine->ppGameServers, pGameServer);
	NewMapTransfer_MaybeFulfillPendingDebugTransferNotifications(pGameServer);
}

int NewMapTransfer_UsefulNumPlayers(TrackedGameServerExe *pServer)
{
	//TODO: eaSize(&pServer->ppPendingRequests) may not be valid if there are non-player requests
	return pServer->globalInfo.iNumPlayers + pServer->iNumPlayersRecentlyLoggingIn + eaSize(&pServer->ppPendingRequests);
}


