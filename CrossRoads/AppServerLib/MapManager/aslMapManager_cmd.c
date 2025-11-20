/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslMapManager_cmd.h"

#include "UGCProjectCommon.h"

#include "Alerts.h"
#include "file.h"
#include "ServerLib.h"
#include "utilitiesLib.h"
#include "logging.h"
#include "aslMapManagerNewMapTransfer_InnerStructs.h"
#include "aslMapManagerNewMapTransfer_Private.h"

#include "autogen/AppServerLib_autogen_slowfuncs.h"
#include "autogen/AppServerLib_autogen_RemoteFuncs.h"
#include "autogen/GameServerLib_autogen_RemoteFuncs.h"

static StashTable s_PlayableNameSpaceStashByNameSpace = NULL;

static void EnsurePlayableNameSpaceStash()
{
	if(!s_PlayableNameSpaceStashByNameSpace)
		s_PlayableNameSpaceStashByNameSpace = stashTableCreateWithStringKeys(1024 * 1024, StashDeepCopyKeys | StashCaseInsensitive);
}

static void ClearPlayableNameSpaceStash()
{
	EnsurePlayableNameSpaceStash();

	stashTableClear(s_PlayableNameSpaceStashByNameSpace);
}

static void AddPlayableNameSpaceStash(UGCPlayableNameSpaceData *pUGCPlayableNameSpaceData)
{
	if(pUGCPlayableNameSpaceData)
	{
		EnsurePlayableNameSpaceStash();

		stashAddPointer(s_PlayableNameSpaceStashByNameSpace, pUGCPlayableNameSpaceData->strNameSpace, StructClone(parse_UGCPlayableNameSpaceData, pUGCPlayableNameSpaceData), true);
	}
}

static void RemovePlayableNameSpaceStash(UGCPlayableNameSpaceData *pUGCPlayableNameSpaceData)
{
	if(pUGCPlayableNameSpaceData)
	{
		UGCPlayableNameSpaceData *pUGCPlayableNameSpaceDataToDestroy = NULL;

		EnsurePlayableNameSpaceStash();

		stashRemovePointer(s_PlayableNameSpaceStashByNameSpace, pUGCPlayableNameSpaceData->strNameSpace, &pUGCPlayableNameSpaceDataToDestroy);
		if(pUGCPlayableNameSpaceDataToDestroy)
			StructDestroy(parse_UGCPlayableNameSpaceData, pUGCPlayableNameSpaceDataToDestroy);
	}
}

static UGCPlayableNameSpaceData *FindPlayableNameSpaceStashTableByNameSpace(const char *strNameSpace)
{
	if(strNameSpace)
	{
		UGCPlayableNameSpaceData *pUGCPlayableNameSpaceData = NULL;

		EnsurePlayableNameSpaceStash();

		stashFindPointer(s_PlayableNameSpaceStashByNameSpace, strNameSpace, &pUGCPlayableNameSpaceData);

		return pUGCPlayableNameSpaceData;
	}

	return NULL;
}

static U32 s_LastTimePlayableNameSpacesRecd = 0;

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslMapManager_UpdateNameSpacesPlayable(UGCPlayableNameSpaces *pUGCPlayableNameSpaces, bool bReplace)
{
	if(bReplace)
		ClearPlayableNameSpaceStash();

	s_LastTimePlayableNameSpacesRecd = timeSecondsSince2000();

	FOR_EACH_IN_EARRAY_FORWARDS(pUGCPlayableNameSpaces->eaUGCPlayableNameSpaceData, UGCPlayableNameSpaceData, pUGCPlayableNameSpaceData)
	{
		if(pUGCPlayableNameSpaceData->bPlayable)
			AddPlayableNameSpaceStash(pUGCPlayableNameSpaceData);
		else
			RemovePlayableNameSpaceStash(pUGCPlayableNameSpaceData);
	}
	FOR_EACH_END;
}

void aslMapManager_PlayableNameSpace_NormalOperation(void)
{
	if(gConf.bUserContent && (isProductionMode() || ugc_DevMode))
	{
		static U32 startedChecking = 0;

		if(0 == startedChecking)
			startedChecking = timeSecondsSince2000();

		if(timeSecondsSince2000() > startedChecking + UGC_MAP_MANAGER_NOT_RECEIVING_PLAYABLE_NAMESPACES_ALERT_DELAY) // start caring
		{
			static U32 lastTimeTriggered = 0;

			if(s_LastTimePlayableNameSpacesRecd == 0 || timeSecondsSince2000() > s_LastTimePlayableNameSpacesRecd + UGC_MAP_MANAGER_NOT_RECEIVING_PLAYABLE_NAMESPACES_ALERT_PERIOD)
			{
				if(lastTimeTriggered == 0 || timeSecondsSince2000() > lastTimeTriggered + UGC_MAP_MANAGER_NOT_RECEIVING_PLAYABLE_NAMESPACES_ALERT_PERIOD)
				{
					lastTimeTriggered = timeSecondsSince2000();
					TriggerAutoGroupingAlert("UGC_MAP_MANAGER_NOT_RECEIVING_PLAYABLE_NAMESPACES", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 12*60*60,
						"The MapManager on shard %s is not receiving playable namespaces from the UGCDataManager. Some UGC projects may be unplayable until this is resolved.", GetShardNameFromShardInfoString());
				}
			}
			else
				lastTimeTriggered = 0;
		}
	}
}

void aslMapManager_PlayableNameSpaceCache_Init(void)
{
	// This command will not actually return anything if the UGCDataManager is also starting up. This is here solely to get playable namespaces again if this MapManager was down for any reason
	// while the UGCDataManager remained up.
	RemoteCommand_Intershard_aslUGCDataManager_GetNameSpacesPlayable(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, GetShardNameFromShardInfoString());
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslMapManagerManager_IsNameSpacePlayable_Return(UGCPlayableNameSpaceDataReturn *pUGCPlayableNameSpaceDataReturn)
{
	SlowRemoteCommandReturn_aslMapManager_IsNameSpacePlayable(pUGCPlayableNameSpaceDataReturn->iCmdID, pUGCPlayableNameSpaceDataReturn->ugcPlayableNameSpaceData.bPlayable);
}

bool aslMapManager_IsNameSpacePlayableHelper(char *pNameSpace)
{
	UGCPlayableNameSpaceData *pUGCPlayableNameSpaceData = FindPlayableNameSpaceStashTableByNameSpace(pNameSpace);

	return !!(pUGCPlayableNameSpaceData && pUGCPlayableNameSpaceData->bPlayable);
}

char* estrTransferFromShardName = NULL;
AUTO_CMD_ESTRING(estrTransferFromShardName, TransferFromShardName) ACMD_AUTO_SETTING(Misc, MAPMANAGER);

//void aslMapManager_IsNameSpacePlayable(SlowRemoteCommandID cmdID, char *pNameSpace, const char* strReason, bool bExpectedTrasferFromShardNS)
AUTO_COMMAND_REMOTE_SLOW(bool) ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void aslMapManager_IsNameSpacePlayable(SlowRemoteCommandID cmdID, char *pNameSpace, const char* strReason)
{
	servLog(LOG_UGC, "IsNameSpacePlayable", "%s", strReason);

	/*U32 iContainerID = UGCProject_GetContainerIDFromUGCNamespace(pNameSpace);
	if(!iContainerID)
	{
		if(!bExpectedTrasferFromShardNS || (estrTransferFromShardName && !namespaceIsUGCOtherShard( pNameSpace, estrTransferFromShardName )))
		{
			ErrorOrAlert("UGC_BAD_NAMESPACE_IN_ISPUBLISHED", "A corrupted namespace has made it into IsUGCNameSpacePublished, this is bad (%s).  Namespace checked because: %s", pNameSpace, strReason);
		}
		return false;
	}*/

	SlowRemoteCommandReturn_aslMapManager_IsNameSpacePlayable(cmdID, aslMapManager_IsNameSpacePlayableHelper(pNameSpace));
}

AUTO_COMMAND_REMOTE_SLOW(UGCPlayableNameSpaceData *);
void aslMapManager_RequestUGCDataForMission(SlowRemoteCommandID cmdID, const char *strNameSpace)
{
	UGCPlayableNameSpaceData *pUGCPlayableNameSpaceData = StructClone(parse_UGCPlayableNameSpaceData, FindPlayableNameSpaceStashTableByNameSpace(strNameSpace));

	pUGCPlayableNameSpaceData->bProjectIsFeatured = UGCProject_IsFeaturedWindow(pUGCPlayableNameSpaceData->iFeaturedStartTimestamp, pUGCPlayableNameSpaceData->iFeaturedEndTimestamp,
		false, false);
	pUGCPlayableNameSpaceData->bProjectWasFeatured = UGCProject_IsFeaturedWindow(pUGCPlayableNameSpaceData->iFeaturedStartTimestamp, pUGCPlayableNameSpaceData->iFeaturedEndTimestamp,
		true, false);

	SlowRemoteCommandReturn_aslMapManager_RequestUGCDataForMission(cmdID, pUGCPlayableNameSpaceData);

	StructDestroy(parse_UGCPlayableNameSpaceData, pUGCPlayableNameSpaceData);
}

static U32 PlayerCountForAllInstancesOfMap(const char *mapName)
{
    U32 count = 0;

    GameServerList *gameServerList;
    if (stashFindPointer(sGameServerListsByMapDescription, mapName, &gameServerList))
    {
        if ( gameServerList ) 
        {
            FOR_EACH_PARTITION_IN_LIST(gameServerList, partitionInfo, serverInfo)
            {
                count += (U32)partitionInfo->iNumPlayers;
            }
        }
        FOR_EACH_PARTITION_END
    }

    return count;
}

AUTO_COMMAND_REMOTE;
void aslMapManager_GetPopulationForMapList(GlobalType returnServerType, ContainerID returnServerID, MapNameAndPopulationList *mapList)
{
    int i;

    if ( mapList == NULL )
    {
        return;
    }

    // Update the population numbers in place so that we don't have to do extra allocations or worry about freeing the list.
    for ( i = eaSize(&mapList->eaMapList) - 1; i >= 0; i-- )
    {
        MapNameAndPopulation *mapNameAndPopulation = mapList->eaMapList[i];

        if ( mapNameAndPopulation )
        {
            mapNameAndPopulation->uPlayerCount = PlayerCountForAllInstancesOfMap(mapNameAndPopulation->mapName);
        }
    }

    // Return the list with updated population numbers to the calling server.
    RemoteCommand_GetPopulationForMapList_Return(returnServerType, returnServerID, mapList);
}
