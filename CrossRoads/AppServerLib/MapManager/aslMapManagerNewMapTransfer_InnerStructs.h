#pragma once
#include "stashTable.h"
#include "aslMapManagerNewMapTransfer_Private.h"
#include "staticworld/worldGridPrivate.h"

extern StashTable sGameServerExesByID;
extern StashTable sGameServerListsByMapDescription;
extern GameServerList **s_eaGameServerListByMapDescription;
extern StashTable sGameServerMachinesByMachineName;

extern StashTable sActiveGameServerListsByMapDescription; //subset of lists which have at least one active map, for easy servermonitoring
extern StashTable sPlayersBeingSentByID;

extern U32 *g_piDoorDestinationRequestMapIDs;
extern PendingDoorDestinationRequest **g_eaPendingDoorDestinationRequestMapIDs;


static __forceinline TrackedGameServerExe *GetGameServerFromID(ContainerID iID)
{
	TrackedGameServerExe *pRetVal;

	if (stashIntFindPointer(sGameServerExesByID, iID, &pRetVal))
	{
		return pRetVal;
	}

	return NULL;
}

static __forceinline MapPartitionSummary *GetPartitionFromServerAndID(TrackedGameServerExe *pServer, U32 uPartitionID)
{
	int i;

	for (i=0; i < eaSize(&pServer->description.ppPartitions); i++)
	{
		if (pServer->description.ppPartitions[i]->uPartitionID == uPartitionID)
		{
			return pServer->description.ppPartitions[i];
		}
	}

	return NULL;
}

static __forceinline MapPartitionSummary *GetPartitionFromIDs(ContainerID iGameServerID, U32 uPartitionID, TrackedGameServerExe **ppGameServer)
{
	TrackedGameServerExe *pGameServer = GetGameServerFromID(iGameServerID);

	if (pGameServer)
	{
		MapPartitionSummary *pRetVal = GetPartitionFromServerAndID(pGameServer, uPartitionID);

		if (pRetVal)
		{
			if (ppGameServer)
			{
				*ppGameServer = pGameServer;
			}

			return pRetVal;
		}

	}

	if (ppGameServer)
	{
		*ppGameServer = NULL;
	}

	return NULL;
}


void NewMapTransfer_InitMapLists(void);
void RemoveAndDestroyGameServerExe(TrackedGameServerExe *pGameServer, char *pReason);
void AddGameServerToMachine(TrackedGameServerExe *pGameServer, const char *pMachineName, U32 iIP, U32 iPublicIP);
TrackedGameServerExe *CreateAndLinkTrackedGameServerExe(GameServerExeState eState, GameServerExe_Description *pDescription, ContainerID iContainerID);
MapPartitionSummary *AddPartitionSummaryInternal(TrackedGameServerExe *pServer, const char *pMapVariables, GlobalType eOwnerType, ContainerID iOwnerID, int iVirtualShardID);
int NewMapTransfer_UsefulNumPlayers(TrackedGameServerExe *pServer);
void NewMapTransfer_AddExistingPartitionSummary(TrackedGameServerExe *pServer, MapPartitionSummary *pSummary);
void NewMapTransfer_DestroyPartition(TrackedGameServerExe *pServer, MapPartitionSummary *pSummary);
void NewMapTransfer_RemoveGameServerFromList(TrackedGameServerExe *pGameServer);
void NewMapTransfer_AddGameServerToList(TrackedGameServerExe *pGameServer);
GameServerList *NewMapTransfer_FindListForGameServer(TrackedGameServerExe *pServer);

GameServerList *NewMapTransfer_FindEditingGameServerList(void);

GameServerList *NewMapTransfer_CreateAndRegisterNewGameServerList(const char *pMapName, ZoneMapInfo *pZoneMap, GameServerListType eListType);

void NewMapTransfer_FixupListsForReloadedGlobalConfig(void);


//returns true if the add went OK, false if there was a conflict (a conflict could occur if a race condition of some sort
//happened during a mapmanager restart)
bool NewMapTransfer_AddPreexistingPartitionToServer(TrackedGameServerExe *pServer, MapPartitionSummary *pSummary, char **ppErrorString);

#define FOR_EACH_PARTITION_IN_LIST(pList, pPartitionVar, pServerVar) {										\
	int _iServerNum; int _iPartitionNum;																	\
	TrackedGameServerExe *pServerVar; MapPartitionSummary *pPartitionVar;									\
	for (_iServerNum = 0; _iServerNum < eaSize(&pList->ppGameServers); _iServerNum++) {					\
		pServerVar = pList->ppGameServers[_iServerNum];														\
			for (_iPartitionNum = 0; _iPartitionNum < eaSize(&pServerVar->description.ppPartitions); _iPartitionNum++) {\
				pPartitionVar = pServerVar->description.ppPartitions[_iPartitionNum];

#define FOR_EACH_PARTITION_END } } }

static __forceinline ZoneMapType GetListZoneMapType(GameServerList *pList)
{
	ZoneMapInfo *pZoneMap;

	if (!pList)
	{
		return ZMTYPE_UNSPECIFIED;
	}

	pZoneMap = GET_REF(pList->hZoneMapInfo);

	if (pZoneMap)
	{
		return pZoneMap->map_type;
	}

	return ZMTYPE_UNSPECIFIED;
}