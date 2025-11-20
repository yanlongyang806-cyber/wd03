/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslTransactions.h"
#include "gslMapTransfer.h"
#include "gslExtern.h"
#include "gslUGC.h"
#include "objSchema.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "gslMapVariable.h"

#include "objPath.h"
#include "EntityLib.h"
#include "GameServerLib.h"
#include "ServerLib.h"
#include "error.h"
#include "earray.h"
#include "EntityIterator.h"
#include "EntityMovementDoor.h"
#include "EntityMovementManager.h"
#include "EntitySavedData.h"
#include "GamePermissionsCommon.h"
#include "gslCommandParse.h"
#include "gslDoorTransition.h"
#include "gslLogSettings.h"
#include "gslSpawnPoint.h"
#include "gslSavedPet.h"
#include "gslInterior.h"
#include "gslHandleMsg.h"
#include "gslQueue.h"
#include "gslSound.h"
#include "NotifyCommon.h"
#include "logging.h"
#include "Player.h"
#include "Quat.h"
#include "queue_common.h"
#include "rand.h"
#include "entCritter.h"
#include "Entity_h_ast.h"
#include "EntitySavedData_h_ast.h"
#include "Player_h_ast.h"
#include "Autogen/AppServerLib_autogen_remotefuncs.h"
#include "Autogen/GameServerLib_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "Autogen/ObjectDB_autogen_remotefuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/MapDescription_h_ast.h"
#include "AutoGen/accountnet_h_ast.h"

#include "file.h"
#include "gslSendToClient.h"
#include "gslEntity.h"
#include "textparser.h"
#include "structNet.h"
#include "sock.h"
#include "InstancedStateMachine.h"
#include "gslEntity.h"
#include "WorldGrid.h"
#include "team.h"
#include "aiAnimList.h"
#include "AnimList_Common.h"
#include "AutoTransDefs.h"
#include "cutscene.h"
#include "cutscene_common.h"
#include "PowerModes.h"
#include "RegionRules.h"
#include "StringCache.h"
#include "EntityMovementManager.h"
#include "gslWorldVariable.h"
#include "encounter_common.h"
#include "GameAccountDataCommon.h"
#include "Guild.h"
#include "inventoryCommon.h"
#include "itemServer.h"
#include "UGCProjectCommon.h"
#include "wlUGC.h"
#include "gslPartition.h"
#include "utils.h"
#include "MapTransferCommon.h"
#include "gslMapTransfer_h_ast.h"
#include "utilitiesLib.h"
#include "LoggedTransactions.h"

#define PAUSE_WHILE_MOVING_PARTITIONS_TIME 1.0f

static float sbDelayBeforeForceSavingOnTransfer = 0.5f;
AUTO_CMD_FLOAT(sbDelayBeforeForceSavingOnTransfer, DelayBeforeForceSavingOnTransfer) ACMD_CMDLINE;

static bool sbFailAllPlayerTransfers = false;
AUTO_CMD_INT(sbFailAllPlayerTransfers, FailAllPlayerTransfers) ACMD_CMDLINE;

// Lifespan (in seconds) of an entity's iNearbyTeamsize value
#define NEARBY_TEAMSIZE_LIFESPAN 120

CharacterTransfer ** gTransferringCharacters;

static bool sbPrintfTransferLogs = false;
AUTO_CMD_INT(sbPrintfTransferLogs, PrintfTransferLogs) ACMD_COMMANDLINE;

static bool sbVerboseTransferLogs = false;
AUTO_CMD_INT(sbVerboseTransferLogs, VerboseTransferLogs);


//tranfserLog all ISM state changes
void MapTransferStateChangeLoggingCB(CharacterTransfer *transfer, char *pStateString);

AUTO_COMMAND;
void LogMapTransferStateChanges(int iSet)
{
	if (iSet)
	{
		ISM_SetNewStateDebugCB(MAP_TRANSFER_STATE_MACHINE, MapTransferStateChangeLoggingCB);
	}
	else
	{
		ISM_SetNewStateDebugCB(MAP_TRANSFER_STATE_MACHINE, NULL);
	}
}

static CharacterTransfer *getCharacterTransferForCookie(U32 searchCookie);




void TransferLog(CharacterTransfer *pTransfer, char *pAction, const char *pString, ...)
{
	va_list ap;
	char *pTempString = NULL;
	estrStackCreate(&pTempString);
	va_start(ap, pString);
	if (!pTransfer)
	{
		estrPrintf(&pTempString, "TransferLog called with NULL transfer:");
		estrConcatfv(&pTempString, pString, ap);
		objLog(LOG_LOGIN, GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, pAction, NULL, "%s", pTempString);
		if (sbPrintfTransferLogs)
		{
			printfColor(COLOR_BRIGHT|COLOR_BLUE|COLOR_GREEN, "TRANSFER: %s\n", pTempString);
		}
	}
	else
	{
		Entity *pEnt = entFromEntityRefAnyPartition(pTransfer->entRef);
		if (pEnt)
		{
			entLog_vprintf(LOG_LOGIN, pEnt, pAction, pString, ap);
			if (sbPrintfTransferLogs)
			{
				estrPrintf(&pTempString, "Ent %u: ", entGetContainerID(pEnt));
				estrConcatfv(&pTempString, pString, ap);
				printfColor(COLOR_BRIGHT|COLOR_BLUE, "TRANSFER: %s\n", pTempString);
			}
		}
		else
		{
			estrPrintf(&pTempString, "TransferLog called with unknown ent (ref %u):", pTransfer->entRef);
			estrConcatfv(&pTempString, pString, ap);
			objLog(LOG_LOGIN, GLOBALTYPE_NONE, 0, 0, NULL, NULL, NULL, pAction, NULL, "%s", pTempString);
			if (sbPrintfTransferLogs)
			{
				printfColor(COLOR_BRIGHT|COLOR_BLUE, "TRANSFER: %s\n", pTempString);
			}

		}
	}

	va_end(ap);
	estrDestroy(&pTempString);
}

void MapTransferStateChangeLoggingCB(CharacterTransfer *transfer, char *pStateString)
{
	TransferLog(transfer, "StateChange", "Now in state %s", pStateString);
}


#define VerboseTransferLog(pTransfer, pAction, pString, ...) {if (sbVerboseTransferLogs) TransferLog(pTransfer, pAction, pString, __VA_ARGS__);}


char *GetSimpleStringForMapChoice(PossibleMapChoice *pChoice)
{
	static char *pRetVal = NULL;
	estrPrintf(&pRetVal, "Type %s. Name %s. ID %u. Index %u. Partition %u. Owner %s[%u]",
		StaticDefineIntRevLookup(MapChoiceTypeEnum, pChoice->eChoiceType), pChoice->baseMapDescription.mapDescription,
		pChoice->baseMapDescription.containerID, pChoice->baseMapDescription.mapInstanceIndex,
		pChoice->baseMapDescription.iPartitionID, GlobalTypeToName(pChoice->baseMapDescription.ownerType),
		pChoice->baseMapDescription.ownerID);

	return pRetVal;
}

static CharacterTransfer *createCharacterTransfer(void)
{
	CharacterTransfer *transfer = calloc(sizeof(CharacterTransfer),1);
	transfer->loginCookie = gslMapTransferGetLoginCookie();	
	ISM_CreateMachine(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_INITIAL);

	return transfer;
}

static void destroyCharacterTransfer(CharacterTransfer *transfer)
{
	if (transfer->pMapSearchInfo)
	{
		StructDestroy(parse_MapSearchInfo, transfer->pMapSearchInfo);
	}

	if (transfer->pChosenMap)
	{
		StructDestroy(parse_PossibleMapChoice, transfer->pChosenMap);
	}

	if (transfer->pReturnedAddress)
	{
		StructDestroy(parse_ReturnedGameServerAddress, transfer->pReturnedAddress);
	}

	if (transfer->pPossibleMapChoices)
	{
		StructDestroy(parse_PossibleMapChoices, transfer->pPossibleMapChoices);
	}

	SAFE_FREE(transfer->pReasonString);

	ISM_DestroyMachine(MAP_TRANSFER_STATE_MACHINE, transfer);
	SAFE_FREE(transfer);

}

void destroyCharacterTransferDeferredCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	U32 iCookie = (U32)((intptr_t)(userData));
	CharacterTransfer *pTransfer = getCharacterTransferForCookie(iCookie);
	if (pTransfer)
	{
		eaFindAndRemove(&gTransferringCharacters,pTransfer);

		destroyCharacterTransfer(pTransfer);
	}
}

void destroyCharacterTransferDeferred(CharacterTransfer *transfer)
{
	TimedCallback_Run(destroyCharacterTransferDeferredCB, (void*)(transfer->loginCookie), 0.0f);
}

CharacterTransfer *gslGetCharacterTransferForEntity(Entity *entity)
{
	int i, size = eaSize(&gTransferringCharacters);
	EntityRef ref = entGetRef(entity);
	for (i = 0; i < size; i++)
	{
		if (gTransferringCharacters[i]->entRef == ref)
		{
			return gTransferringCharacters[i];
		}
	}
	return NULL;
}

bool characterIsTransferring(Entity *entity)
{
	return ( entity->pPlayer->bMapTransferPending || entGetNetLink(entity) == NULL);
}

static CharacterTransfer *getCharacterTransferForContainer(GlobalType containerType, ContainerID containerID)
{
	int i, size = eaSize(&gTransferringCharacters);
	for (i = 0; i < size; i++)
	{
		if (gTransferringCharacters[i]->containerType == containerType &&
			gTransferringCharacters[i]->containerID == containerID)
		{
			return gTransferringCharacters[i];
		}
	}
	return NULL;
}

static CharacterTransfer *getCharacterTransferForLink(ClientLink *link)
{
	int i, size = eaSize(&gTransferringCharacters);
	for (i = 0; i < size; i++)
	{
		if (gTransferringCharacters[i]->clientLink == link)
		{
			return gTransferringCharacters[i];
		}
	}
	return NULL;
}

static CharacterTransfer *getCharacterTransferForCookie(U32 searchCookie)
{
	int i, size = eaSize(&gTransferringCharacters);
	for (i = 0; i < size; i++)
	{
		if (gTransferringCharacters[i]->loginCookie == searchCookie)
		{
			return gTransferringCharacters[i];
		}
	}
	return NULL;
}

void gslMapTransferTick(F32 elapsed)
{
	int i, size = eaSize(&gTransferringCharacters);
	
	PERFINFO_AUTO_START_FUNC();
	
	for (i = 0; i < size; i++)
	{
		CharacterTransfer *transfer = gTransferringCharacters[i];
		ClientLink *link = transfer->clientLink;

		ISM_Tick(MAP_TRANSFER_STATE_MACHINE, transfer, elapsed);

	}
	
	PERFINFO_AUTO_STOP();
}

int gslMapTransferGetLoginCookie(void)
{
	static int nextCookie = 1;
	if (nextCookie >= 65536)
	{
		nextCookie = 1;
	}
	return (65536 * objServerID()) + nextCookie++; //login cookies will be from 0 to 65536
}


void gslMapTransferFail(CharacterTransfer *transfer, bool bCancelled, const char *pUntranslatedErrorString)
{
	Entity *ent = entFromEntityRefAnyPartition(transfer->entRef);
	const char *pTranslatedErrorString = pUntranslatedErrorString;

	PERFINFO_AUTO_START_FUNC();

	if (ent)
	{
		pTranslatedErrorString = langTranslateMessageKeyDefault(entGetLanguage(ent), pUntranslatedErrorString, pUntranslatedErrorString);

		if (ent->pPlayer)
		{
			ent->pPlayer->bMapTransferPending = false;

			if (entCheckFlag(ent, ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS))
			{
				gslEntityClearTransitionSequenceFlags(ent,ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS|ENTITYFLAG_DONOTDRAW,true);
			}
		}
	}

	if (transfer->containerType == GLOBALTYPE_ENTITYPLAYER && transfer->pMapSearchInfo)
	{
		const char* pchMapName = transfer->pMapSearchInfo->baseMapDescription.mapDescription;
		ZoneMapInfo *pZoneMap = worldGetZoneMapByPublicName(pchMapName);

		if (pZoneMap && queue_IsQueueMap(zmapInfoGetMapType(pZoneMap)))
		{
			gslQueue_HandleMapTransferFailure(transfer->containerID);
		}
	}

	if (transfer->eFlags & TRANSFERFLAG_PASSED_POINT_OF_NO_RETURN)
	{
		if (transfer->clientLink)
		{
			if (transfer->clientLink->netLink)
			{

				Packet *pak = pktCreate(transfer->clientLink->netLink, TOCLIENT_GAME_TRANSFER_FAILED);


				pktSendString(pak, pTranslatedErrorString);
				pktSend(&pak);
			}

			if (!transfer->clientLink->disconnected)
			{
				log_printf(LOG_CLIENTSERVERCOMM, "adding netlink %p to disconnect list because map transfer has failed", transfer->clientLink->netLink);
				if (transfer->clientLink->netLink)
				{
					linkFlushAndClose(&transfer->clientLink->netLink, "Map Transfer Failed");
				}
				transfer->clientLink->disconnected = 1;
			}
		}


		if (ent)
		{
			gslLogOutEntity(ent, 0, 0);
		}
	}
	else
	{
		//TODO make this more official
		if (ent && !bCancelled)
		{
			objBroadcastMessage(entGetType(ent), entGetContainerID(ent), "Map Transfer Failure", pTranslatedErrorString);
		}

	}

	ISM_SwitchToSibling(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_FAILED);

	if (!bCancelled)
	{
		TransferLog(transfer, "TransferFailed", pUntranslatedErrorString);
	}

	if (ent && ent->pPlayer && ent->pPlayer->pCSRListener){
		gslSendCSRFeedback(ent, STACK_SPRINTF("Map transfer failed.  Reason: %s", pUntranslatedErrorString));
	}
	
	PERFINFO_AUTO_STOP();
}


void gslMapTransferFailf(CharacterTransfer *transfer, bool bCancelled, const char *pErrorString, ...)
{
	char *pFullErrorString = NULL;
	estrGetVarArgs(&pFullErrorString, pErrorString);
	gslMapTransferFail(transfer, bCancelled, pFullErrorString);
	estrDestroy(&pFullErrorString);
}

bool gslMapTransferHandleDisconnect(ClientLink *link)
{
	CharacterTransfer *transfer = getCharacterTransferForLink(link);
	bool transferSucceeded = 0;
	if (!transfer)
	{
		return false;
	}
	if (ISM_IsStateActiveOrPending(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_COMPLETE) ||
		ISM_IsStateActiveOrPending(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_FAILED))
	{
		eaFindAndRemove(&gTransferringCharacters,transfer);
		destroyCharacterTransfer(transfer);		
		return true;
	}
	else
	{
		gslMapTransferFail(transfer, false, "ClientDisconnectDuringTransfer");
		eaFindAndRemove(&gTransferringCharacters,transfer);
		destroyCharacterTransfer(transfer);		
		return false;
	}
}

// number of seconds after a team member leaves that they can be followed
#define TEAM_FOLLOW_MAX_TIME 120

// how long we wait on a team transfer before timing out
#define TEAM_TRANSFER_QUEUE_TIMEOUT_DEV 30
#define TEAM_TRANSFER_QUEUE_TIMEOUT_PROD 5
#define TEAM_TRANSFER_QUEUE_TIMEOUT (isProductionMode()?TEAM_TRANSFER_QUEUE_TIMEOUT_PROD:TEAM_TRANSFER_QUEUE_TIMEOUT_DEV)

// how many times we allow retry of team transfer queue before giving up
#define TEAM_TRANSFER_MAX_RETRIES 3

//
// Team transfer functions
//

bool
IsValidTeamTransferDestination(MapDescription *mapDesc)
{
	// If mapDesc is the same as my current map, return false.
	if ( mapDesc != NULL && IsSameMapDescription(mapDesc, &gGSLState.gameServerDescription.baseMapDescription ) )
	{
		return false;
	}

	if ( mapDesc != NULL )
	{
		ZoneMapType destMapType = zmapInfoGetMapTypeByName(mapDesc->mapDescription);

		// Only static and shared maps use this team transfer mechanism.
		// Mission maps have their own mechanism to ensure teams end up on the same map.
		return( ( destMapType == ZMTYPE_STATIC ) || ( destMapType == ZMTYPE_SHARED ) );
	}

	return false;
}

//
// Return the index in the eaMembers array of a member that has a valid
//  team transfer.
// NOTE - This value must be used immediately.  It may not be saved
//  for later processing since the array could be changed.
// NOTE - mapName must be a pooled string!
//
static S32
GetAvailableTeamTransferIndex(Entity *pEnt, MapDescription *mapDesc)
{
	if ( !IsValidTeamTransferDestination(mapDesc) )
	{
		return -1;
	}
	
	if ( team_IsMember(pEnt) && ( mapDesc != NULL ) )
	{
		Team *team = team_GetTeam(pEnt);
		if ( team != NULL )
		{
			int n = eaSize(&team->eaMembers);
			int i;
			int iPartitionIdx = entGetPartitionIdx(pEnt);
			U32 now = timeSecondsSince2000();

			for ( i = 0; i < n; i++ )
			{
				if ( IsSameMapDescription(mapDesc, team->eaMembers[i]->pExitMapDescription ) )
				{
					Entity *followingEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, team->eaMembers[i]->iEntID);

					// This member is only valid to follow if it is still on this map or has a non-zero Map ID, and the exit time is
					//  within the specified limit.
					if ( ( ( followingEnt != NULL ) || ( team->eaMembers[i]->pExitMapDescription->containerID != 0 ) ) && ( now < ( team->eaMembers[i]->iExitMapTime + TEAM_FOLLOW_MAX_TIME ) ) )
					{
						return i;
					}
				}
			}
		}
	}
	return -1;
}

//
// Given the index of the team member we want to follow, get their destination map ID
//
static MapDescription *
GetTeamTransferDestination(Entity *pEnt, int index)
{

	Team *team = team_GetTeam(pEnt);

	if ( ( team != NULL ) && ( index >= 0 ) && ( index < eaSize(&team->eaMembers) ) )
	{
		return team->eaMembers[index]->pExitMapDescription;
	}

	return 0;
}

static void
SetTeamTransferInfo(Entity *pEnt, MapDescription *mapDesc)
{
	if ( team_IsMember(pEnt) && IsValidTeamTransferDestination(mapDesc) )
	{
		Team *team = team_GetTeam(pEnt);
		TeamMember *teamMember;
		
		teamMember = team_FindMember(team, pEnt);

		if ( teamMember != NULL )
		{
			teamMember->iExitMapTime = timeSecondsSince2000();
			if ( teamMember->pExitMapDescription != NULL )
			{
				StructDestroy(parse_MapDescription, teamMember->pExitMapDescription);
			}
			teamMember->pExitMapDescription = StructClone(parse_MapDescription, mapDesc);
		}
	}
}

// update the team transfer info for a player with the map id
static void
UpdateTeamTransferMapID(Entity *pEnt, MapDescription *updateMapDesc, U32 destMapID, U32 iPartitionID)
{
	if ( team_IsMember(pEnt) && IsValidTeamTransferDestination(updateMapDesc) )
	{
		Team *team = team_GetTeam(pEnt);
		TeamMember *teamMember;

		teamMember = team_FindMember(team, pEnt);

		if ( teamMember != NULL )
		{
			if ( IsSameMapDescription(teamMember->pExitMapDescription, updateMapDesc ) )
			{
				teamMember->iExitMapTime = timeSecondsSince2000();
				teamMember->pExitMapDescription->containerID = destMapID;
				teamMember->pExitMapDescription->iPartitionID = iPartitionID;
			}
		}
	}
}

AUTO_STRUCT;
typedef struct QueuedTeamTransfer
{
	U32 followerID;
	U32 followingID;
	U32 queuedTime;
	MapDescription *mapDesc;
} QueuedTeamTransfer;

extern ParseTable parse_QueuedTeamTransfer[];
#define TYPE_parse_QueuedTeamTransfer QueuedTeamTransfer

static EARRAY_OF(QueuedTeamTransfer) sTeamTransferQueue = NULL;

static bool
QueueForTeamTransfer(Entity *pEnt, int memberToFollowIndex, MapDescription *destMap)
{
	devassert(team_IsMember(pEnt));
	if ( team_IsMember(pEnt) )
	{
		Team *team = team_GetTeam(pEnt);
		TeamMember *teamMember;

		if ( team != NULL )
		{
			int iPartitionIdx = entGetPartitionIdx(pEnt);
			devassert( ( memberToFollowIndex >= 0 ) && ( memberToFollowIndex < eaSize(&team->eaMembers) ) );
			if ( ( memberToFollowIndex >= 0 ) && ( memberToFollowIndex < eaSize(&team->eaMembers) ) )
			{
				QueuedTeamTransfer *teamTransfer;
				Entity *entToFollow;

				teamMember = team->eaMembers[memberToFollowIndex];
				entToFollow = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, teamMember->iEntID);
				devassert(teamMember->pExitMapDescription->containerID == 0);
				devassert(IsSameMapDescription(teamMember->pExitMapDescription, destMap));
				devassert(entToFollow != NULL);

				if ( ( teamMember->pExitMapDescription->containerID == 0 ) && IsSameMapDescription(teamMember->pExitMapDescription, destMap) && ( entToFollow != NULL ) )
				{
					teamTransfer = StructCreate(parse_QueuedTeamTransfer);

					teamTransfer->followerID = pEnt->myContainerID;
					teamTransfer->followingID = teamMember->iEntID;
					teamTransfer->queuedTime = timeSecondsSince2000();
					teamTransfer->mapDesc = StructClone(parse_MapDescription, destMap);

					eaPush(&sTeamTransferQueue, teamTransfer);

					return true;
				}
			}
		}
	}

	return false;
}

static void
TeamTransferQueueWakeup(U32 playerID)
{
	CharacterTransfer *transfer = getCharacterTransferForContainer(GLOBALTYPE_ENTITYPLAYER, playerID);

	if ( transfer != NULL )
	{
		// switch back to selecting map state
		ISM_SwitchToSibling(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_SELECTING_MAP);
	}

}

// Notify any players who are waiting for this player to have a destination map ID
static void
TeamTransferDestinationNotify(Entity *pEnt, MapDescription *destMap)
{
	int n = eaSize(&sTeamTransferQueue);
	int i;

	for ( i = n-1; i >= 0; i-- )
	{
		if ( ( sTeamTransferQueue[i]->followingID == pEnt->myContainerID ) && IsSameMapDescription(sTeamTransferQueue[i]->mapDesc, destMap) )
		{
			// ok to remove since we are iterating from end to start
			QueuedTeamTransfer *teamTransfer = eaRemove(&sTeamTransferQueue, i);

			if ( teamTransfer != NULL )
			{
				// wake up the guy waiting in the queue
				TeamTransferQueueWakeup(teamTransfer->followerID);

				// clean up the queue entry
				StructDestroy(parse_QueuedTeamTransfer, teamTransfer);
			}
		}
	}
}

static void
MapTransferWaitingForTeamTransfer_BeginFrame(InstancedMachineHandleOrName pStateMachineHandle, CharacterTransfer *transfer, F32 fElapsed)
{
	static U32 lastSecond = 0;
	U32 now;

	now = timeSecondsSince2000();

	// only scan the list once per second
	if ( lastSecond < now )
	{
		int n = eaSize(&sTeamTransferQueue);
		int i;

		lastSecond = now;

		for ( i = n-1; i >= 0; i-- )
		{
			// wake up any queued transfers that are older than the timeout
			if ( ( sTeamTransferQueue[i]->queuedTime + TEAM_TRANSFER_QUEUE_TIMEOUT ) < now )
			{
				// ok to remove since we are iterating from end to start
				QueuedTeamTransfer *teamTransfer = eaRemove(&sTeamTransferQueue, i);

				if ( teamTransfer != NULL )
				{
					// wake up the guy waiting in the queue
					TeamTransferQueueWakeup(teamTransfer->followerID);

					// clean up the queue entry
					StructDestroy(parse_QueuedTeamTransfer, teamTransfer);
				}
			}
		}
	}
}

static char *GetCurrentAndPendingStateString(CharacterTransfer *transfer)
{
	static char *pCurString = NULL;
	static char *pPendingString = NULL;
	static char *pRetVal = NULL;

	ISM_PutFullStateStackIntoEString(MAP_TRANSFER_STATE_MACHINE, transfer, &pCurString);
	ISM_PutFullNextStateStackIntoEString(MAP_TRANSFER_STATE_MACHINE, transfer, &pPendingString);

	estrPrintf(&pRetVal, "%s:%s", pCurString, pPendingString);

	return pRetVal;
}


// State machine functions

// Functions for TRANSFERSTATE_INITIAL

static void GetPossibleMapChoices_CB(TransactionReturnVal *returnVal, void *userData)
{	
	U32 loginCookie = (U32)(intptr_t)userData;	
	CharacterTransfer *transfer = getCharacterTransferForCookie(loginCookie);
	Entity *ent;
	int iNumMaps;
	PossibleMapChoice *pOnlyChoice = NULL;
	int i;
	bool bGoingToSendToClientDueToBeingStatic = false;


	if (!transfer)
	{
		TransferLog(transfer, "InvalidTransfer", "Command %s returned for invalid transfer id %d", __FUNCTION__, loginCookie);
		return;
	}

	if (!ISM_IsStateActiveOrPending(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_SELECTING_MAP))
	{
		TransferLog(transfer, "InvalidTransfer", "Command %s returned for transfer %d while in invalid state %s", __FUNCTION__, loginCookie,
			GetCurrentAndPendingStateString(transfer));
		return;
	}

	ent = entFromEntityRefAnyPartition(transfer->entRef);

	if (!ent || !ent->pPlayer)
	{
		gslMapTransferFail(transfer,false,"Missing entity");
		return;
	}

	if (transfer->pPossibleMapChoices)
	{
		StructDestroy(parse_PossibleMapChoices, transfer->pPossibleMapChoices);
		transfer->pPossibleMapChoices = NULL;
	}

	switch(RemoteCommandCheck_GetPossibleMapChoices(returnVal, &transfer->pPossibleMapChoices))
	{
	case TRANSACTION_OUTCOME_FAILURE:
		gslMapTransferFail(transfer, false,"Couldn't get map names from map manager");
		break;
	case TRANSACTION_OUTCOME_SUCCESS:
		iNumMaps = 0;

		// This should not happen but did when there was a Map Manager bug.  Make sure we can recover from it.
		if (!eaSize(&transfer->pPossibleMapChoices->ppChoices)) {
			gslMapTransferFail(transfer, false,"Couldn't get map names from map manager");
			break;
		}
	
		//going to use this twice, so calculate it ahead of time
		bGoingToSendToClientDueToBeingStatic = ((transfer->eFlags & TRANSFERFLAG_FORCE_SEND_OPTIONS_TO_CLIENT_IF_GOING_TO_STATIC)
					&& transfer->pPossibleMapChoices->ppChoices[0] 
					&& transfer->pPossibleMapChoices->ppChoices[0]->baseMapDescription.eMapType == ZMTYPE_STATIC);

		for (i = eaSize(&transfer->pPossibleMapChoices->ppChoices) - 1; i >= 0; i--)
		{
			switch (transfer->pPossibleMapChoices->ppChoices[i]->eChoiceType)
			{
			case MAPCHOICETYPE_SPECIFIED_ONLY:
			case MAPCHOICETYPE_SPECIFIED_OR_BEST_FIT:
				transfer->iNumSpecificChoices++; 
			}
		}
		
		//cull out any choices that our flags tell us we don't care about
		if (transfer->eFlags & TRANSFERFLAG_ONLY_EXISTING_MAPS || bGoingToSendToClientDueToBeingStatic && transfer->iNumSpecificChoices)
		{
			TransferCommon_RemoveNonSpecificChoices(&transfer->pPossibleMapChoices->ppChoices);
		}
		

		for (i = eaSize(&transfer->pPossibleMapChoices->ppChoices) - 1; i >= 0; i--)
		{
			switch (transfer->pPossibleMapChoices->ppChoices[i]->eChoiceType)
			{
			case MAPCHOICETYPE_SPECIFIED_ONLY:
			case MAPCHOICETYPE_SPECIFIED_OR_BEST_FIT:
				if (transfer->pPossibleMapChoices->ppChoices[i]->baseMapDescription.containerID == GetAppGlobalID()
					&& transfer->pPossibleMapChoices->ppChoices[i]->baseMapDescription.iPartitionID == partition_IDFromIdx(entGetPartitionIdx(ent)))
				{
					if (transfer->eFlags & TRANSFERFLAG_IGNORE_CURRENT_MAP)
					{
						StructDestroy(parse_PossibleMapChoice, transfer->pPossibleMapChoices->ppChoices[i]);
						eaRemove(&transfer->pPossibleMapChoices->ppChoices, i);
					}
					else
					{
						transfer->pPossibleMapChoices->ppChoices[i]->bIsCurrent = true;
						transfer->pPossibleMapChoices->ppChoices[i]->bNotALegalChoice = true;
					}
				}
				break;

			default:
				break;
			}
		}

		for (i=0; i < eaSize(&transfer->pPossibleMapChoices->ppChoices); i++)
		{
			if (!transfer->pPossibleMapChoices->ppChoices[i]->bNotALegalChoice)
			{
				iNumMaps++;
				pOnlyChoice = transfer->pPossibleMapChoices->ppChoices[i];
			}
		}

		// send pPossibleMapChoices to client unless there's only 1
	
		if (!iNumMaps)
		{

			if (transfer->eFlags & TRANSFERFLAG_NO_NEW_OWNED_MAP)
			{
				//for now, this is only possible in FC, might need to generalize at some point
				gslMapTransferFail(transfer,false,"Map_HideoutFull");
			}
			else
			{
				gslMapTransferFail(transfer,false,"NoPossibleMaps");
			}
			return;
		}

		TransferLog(transfer, "Selecting Map", "Choosing between %d possible maps. First is %s",
			iNumMaps, GetSimpleStringForMapChoice(transfer->pPossibleMapChoices->ppChoices[0]));

		if (sbVerboseTransferLogs)
		{
			for (i = 1; i < iNumMaps; i++)
			{
				VerboseTransferLog(transfer, "Selecting Map", "Choice %d: %s", i, GetSimpleStringForMapChoice(transfer->pPossibleMapChoices->ppChoices[i]));
			}
		}


		ISM_SwitchToSibling(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_SELECTING_MAP);

		if (entFromEntityRefAnyPartition(transfer->entRef))
		{
			PossibleMapChoice *pBestChoice = NULL;

			if (ent->pPlayer)
			{
				ent->pPlayer->bMapTransferPending = false;
			}
		
			//if there is more than one specific choice, then we want to use our teammates/guildmates/friends logic, otherwise
			//use the generic logic
			if (transfer->iNumSpecificChoices > 1)
			{
				pBestChoice  = TransferCommon_GetBestChoiceBasedOnTeamMembersEtc(transfer->pPossibleMapChoices, ent);
				if (pBestChoice)
				{
					pBestChoice->bGoWhereATeammateRecentlyWentInsteadIfPossible = true;
				}
			}
			else if (transfer->iNumSpecificChoices == 1)
			{
				pBestChoice = TransferCommon_ChooseOnlySpecificChoiceIfTeamIsThere(transfer->pPossibleMapChoices, ent);
				if (!pBestChoice)
				{
					pBestChoice = ChooseBestMapChoice(&transfer->pPossibleMapChoices->ppChoices);
				}
			}
			else
			{
				pBestChoice = ChooseBestMapChoice(&transfer->pPossibleMapChoices->ppChoices);
			}
			

			if (transfer->eFlags & TRANSFERFLAG_FORCE_SEND_OPTIONS_TO_CLIENT 
				|| bGoingToSendToClientDueToBeingStatic)
			{
				if (transfer->eFlags & TRANSFERFLAG_FORCE_SEND_OPTIONS_TO_CLIENT)
				{
					gslSendCSRFeedback(entFromEntityRefAnyPartition(transfer->entRef), "Forced to send options to client by transfer request type");
				}
				else
				{
					gslSendCSRFeedback(entFromEntityRefAnyPartition(transfer->entRef), "Forced to send options to client because cilent has bShowMapChoice turned on");
				}

				ClientCmd_gclDisplayMapChoice(entFromEntityRefAnyPartition(transfer->entRef),transfer->pPossibleMapChoices);
			}
			else
			{
				if ( transfer->doingTeamTransfer )
				{
					// There are 3 possibilities here:
					// 1) Returned map ID is the same, so this is a no-op.
					// 2) Returned map ID is zero, which means the previous team transfer map is full or gone, and we are waiting for a new map to be created.
					//    Future team transfers will be queued until we get a real address again.
					// 3) Returned map ID is different, so just replace the map ID and future team transfers will go there.
					UpdateTeamTransferMapID(ent, &pOnlyChoice->baseMapDescription, pOnlyChoice->baseMapDescription.containerID, pOnlyChoice->baseMapDescription.iPartitionID);
				}
				gslSendCSRFeedback(entFromEntityRefAnyPartition(transfer->entRef), "Picked a best choice automatically.  Transferring player.");
				gslMapTransferChooseAddress(entFromEntityRefAnyPartition(transfer->entRef), pBestChoice);			
			}
		}
		else
		{
			gslMapTransferFail(transfer,false,"Missing entity");		
		}

		break;
	}
}


static void gslMapTransferRequest_MoveOfflinePlayer(Entity *pOfflineEnt, MapDescription *newDesc, CmdContext *pContext)
{
	ZoneMapInfo *pZoneMap = worldGetZoneMapByPublicName(newDesc->mapDescription);

	if (!pZoneMap){
		// Error: Map doesn't exist
		if (pContext && pContext->output_msg){
			estrConcatf(pContext->output_msg, "Error: Map doesn't exist");
		}
		return;
	}

	if (zmapInfoGetMapType(pZoneMap) != ZMTYPE_STATIC){
		//Error: Offline players can only be moved to Static maps
		if (pContext && pContext->output_msg){
			estrConcatf(pContext->output_msg, "Error: Offline players can only be moved to Static maps");
		}
		return;
	}

	AutoTrans_trUpdateMapHistory(LoggedTransactions_CreateManagedReturnVal("MoveOfflinePlayer", NULL, NULL), GetAppGlobalType(), entGetType(pOfflineEnt), entGetContainerID(pOfflineEnt), newDesc);
	entLog(LOG_LOGIN, pOfflineEnt, "CSRTransfer", "Offline player moved to %s via CSR command", newDesc->mapDescription);
}

static void setNearbyTeamSize(Entity* pEnt, const char* pchDestinationMap) 
{
	if(pEnt && pEnt->pTeam)
	{
		Entity** eaTeammates = NULL;
		encounter_getTeammatesInRange(pEnt, &eaTeammates);
		if(eaTeammates) {
			S32 iCount = 0;
			U32 iAveragePartyLevel = encounter_getTeamLevelInRange(pEnt, &iCount, false);
			U32 currentTime = timeSecondsSince2000();
			int i, teamSize = eaSize(&eaTeammates);
			for(i = 0; i < eaSize(&eaTeammates); i++) {
				Entity* pTeammate = eaTeammates[i];
				if(pTeammate->pTeam) {
					pTeammate->pTeam->iNearbyTeamSize = teamSize;
					pTeammate->pTeam->pchDestinationMap = allocAddString(pchDestinationMap);
					pTeammate->pTeam->iTeamSizeTimestamp = currentTime;
					pTeammate->pTeam->iAverageTeamLevel = iAveragePartyLevel;
					pTeammate->pTeam->iAverageTeamLevelTime = currentTime + AVERAGE_PARTY_LEVEL_EXPIRE_TIME_SECONDS;
					entity_SetDirtyBit(pTeammate, parse_PlayerTeam, pTeammate->pTeam, true);
					
				}
			}
			eaDestroy(&eaTeammates);
		}
	}
}


static void gslMapTransferRequest_Internal(Entity *entity, MapSearchInfo *pChoiceInfo, char *pReason, TransferFlags eFlags, CmdContext *pContext)
{
	CharacterTransfer *transfer = gslGetCharacterTransferForEntity(entity);
	U32 iGuildID;
	ZoneMapInfo *pZoneMap = zmapInfoGetByPublicName(pChoiceInfo->baseMapDescription.mapDescription);
	CharClassCategorySet *pSet = zmapInfoGetRequiredClassCategorySet(pZoneMap);

	if (!pReason || !pReason[0])
	{
		pReason = "No reason provided";
	}

	// Hack for CSR commands: If this is a CSR command being run on an offline player,
	// do something totally different
	if(gslIsOfflineCSREnt(entGetType(entity), entGetContainerID(entity))){
		gslMapTransferRequest_MoveOfflinePlayer(entity, &pChoiceInfo->baseMapDescription, pContext);
		gslSendCSRFeedback(entity, "Player was offline, and will be transferred on next log-in.");
		return;
	}

	// Check to see if the player has a puppet of the appropriate type
	if (pSet)
	{
		PuppetEntity *pPuppetEntity;
		RegionRules *pRegionRules = getRegionRulesFromZoneMap(pZoneMap);
		if (!entity_ChoosePuppetEx(entity, pRegionRules, pZoneMap, &pPuppetEntity) || !pPuppetEntity)
		{
			notify_NotifySend(entity, kNotifyType_MapTransferFailed_NoPuppet, TranslateMessageKey("MapTransferFailed_NoPuppet"), NULL, NULL);
			ErrorDetailsf("Player name: %s, Map Name: %s", ENTDEBUGNAME(entity), pChoiceInfo->baseMapDescription.mapDescription);
			Errorf("Player attempted to transfer to a map for which they do not have an appropriate puppet");
			return;
		}
	}

	// Safety check to make sure that map transfers do not attempt to create guild-owned maps for players with no guild
	iGuildID = pChoiceInfo->iGuildID ? pChoiceInfo->iGuildID : guild_IsMember(entity) ? guild_GetGuildID(entity) : 0;
	if (!iGuildID) {
		if (pChoiceInfo->eSearchType == MAPSEARCHTYPE_UNSPECIFIED || pChoiceInfo->eSearchType == MAPSEARCHTYPE_OWNED_MAP) {
			if (zmapInfoGetIsGuildOwned(pZoneMap)) {
				ErrorDetailsf("Player name: %s, Map Name: %s", ENTDEBUGNAME(entity), pChoiceInfo->baseMapDescription.mapDescription);
				Errorf("Player attempted to transfer to a guild-owned map, but the player was not in a guild");
				return;
			}
		} else if (pChoiceInfo->eSearchType == MAPSEARCHTYPE_SPECIFIC_CONTAINER_AND_PARTITION_ID_ONLY) {
			if (zmapInfoGetIsGuildOwned(pZoneMap) && !zmapInfoGetGuildNotRequired(pZoneMap)) {
				ErrorDetailsf("Player name: %s, Map Name: %s", ENTDEBUGNAME(entity), pChoiceInfo->baseMapDescription.mapDescription);
				Errorf("Player attempted to transfer to a specific guild-owned map, but the map requires the player to be a guild member");
				return;
			}
		}
	}

	// Hack for Remote XMLRPC: We don't know if they're logged in or not, do both offline and online moves.
	if (pContext)
	{
		if (pContext->eHowCalled == CMD_CONTEXT_HOWCALLED_XMLRPC)
		{
			gslMapTransferRequest_MoveOfflinePlayer(entity, &pChoiceInfo->baseMapDescription, pContext);
		}
	}

	if (transfer)
	{
		if (ISM_IsStateActiveOrPending(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_FAILED))
		{
			eaFindAndRemove(&gTransferringCharacters,transfer);
			destroyCharacterTransfer(transfer);		
		}
		else
		{
			// something is wrong, this is already in progress
			gslSendCSRFeedback(entity, "Player already has a map transfer pending.");
			return;
		}
	}

	//If the player is attempting to using Warp tech ( see gslWarp.c )
	if(eFlags & TRANSFERFLAG_RECRUITWARP)
	{
		//Check to see if their struct is filled in before allowing the map transfer
		if(entity->pPlayer && !entity->pPlayer->pWarp)
		{
			//If it's not valid, clear the transition sequence flags
			if (entCheckFlag(entity, ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS))
			{
				gslEntityClearTransitionSequenceFlags(entity,ENTITYFLAG_DOOR_SEQUENCE_IN_PROGRESS|ENTITYFLAG_DONOTDRAW,true);
			}

			return;
		}
	}

	//Calculate nearby team size for encounter2 spawning logic
	if(entity && entity->pTeam && !(eFlags & TRANSFERFLAG_IGNORE_ENCOUNTER_SPAWN_LOGIC)) {
		// Is the player's iNearbyTeamSize value already set?
		if(entity->pTeam->iNearbyTeamSize && EMPTY_TO_NULL(entity->pTeam->pchDestinationMap)) {

			// Check the timestamp to make sure iNearbyTeamsize isn't an old calculation
			if(timeSecondsSince2000() - entity->pTeam->iTeamSizeTimestamp > NEARBY_TEAMSIZE_LIFESPAN)
			{
				setNearbyTeamSize(entity, pChoiceInfo->baseMapDescription.mapDescription);
			}

			// If not set for the map we're transferring to, recalculate iNearbyTeamSize
			if(stricmp(pChoiceInfo->baseMapDescription.mapDescription, entity->pTeam->pchDestinationMap)) {
				setNearbyTeamSize(entity, pChoiceInfo->baseMapDescription.mapDescription);
			}
		} else {
			setNearbyTeamSize(entity, pChoiceInfo->baseMapDescription.mapDescription);
		}
	}

	if (entity->pPlayer)
	{
		entity->pPlayer->bMapTransferPending = true;
	}

	// we need to validate the choiceInfo here

	transfer = createCharacterTransfer();
	transfer->containerType = entGetType(entity);
	transfer->containerID = entGetContainerID(entity);
	transfer->clientLink = entGetClientLink(entity);
	transfer->entRef = entGetRef(entity);
	transfer->pReasonString = strdup(pReason);
	transfer->eFlags = eFlags;
	transfer->teamTransferQueuedCount = 0;

	if (SAFE_MEMBER4(entity, pPlayer, pUI, pLooseUI, bShowMapChoice))
	{
		transfer->eFlags |= TRANSFERFLAG_FORCE_SEND_OPTIONS_TO_CLIENT_IF_GOING_TO_STATIC;
	}

	pChoiceInfo->teamID = transfer->teamID = team_GetTeamID(entity);
	pChoiceInfo->iGuildID = iGuildID;
	pChoiceInfo->playerType = entGetType(entity);
	pChoiceInfo->playerID = entGetContainerID(entity);
	pChoiceInfo->playerAccountID = entGetAccountID(entity);
	pChoiceInfo->iAccessLevel = entGetAccessLevel(entity);
	pChoiceInfo->bExpectedTrasferFromShardNS = false;


	eaPush(&gTransferringCharacters,transfer);

	if (transfer->pMapSearchInfo)
	{
		StructReset(parse_MapSearchInfo, transfer->pMapSearchInfo);
	}
	else
	{
		transfer->pMapSearchInfo = StructCreate(parse_MapSearchInfo);
	}

	StructCopyFields(parse_MapSearchInfo,pChoiceInfo,transfer->pMapSearchInfo,0,0);

	if (transfer->eFlags & TRANSFERFLAG_SHOW_FULL_MAPS)
	{
		transfer->pMapSearchInfo->bShowFullMapsAsDisabled = true;
	}

	//special case for leaving pvp maps... never give a player a choice to cancel
	if (gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_PVP)
	{
		transfer->pMapSearchInfo->eFlags |= MAPSEARCH_NOCANCELLING;
	}

	//never ever ever let someone try to move to their own map
	transfer->pMapSearchInfo->gameServerIDToExclude = GetAppGlobalID();

	if (entity->pSaved->pPermissionsOnMostRecentLogin)
	{
        transfer->pMapSearchInfo->pAccountPermissions = StructCloneVoid(parse_AccountProxyKeyValueInfoListContainer, entity->pSaved->pPermissionsOnMostRecentLogin);
	}

	TransferLog(transfer, "TransferBegan", "Dest map: %s. spawn point: %s. Reason: %s", pChoiceInfo->baseMapDescription.mapDescription, pChoiceInfo->baseMapDescription.spawnPoint, pReason);

}


// Remote command called from the client to choose a certain address
AUTO_COMMAND ACMD_PRIVATE;
void gslMapTransferRequest(Entity *entity, MapSearchInfo *pChoiceInfo, char *pReason)
{
	gslMapTransferRequest_Internal(entity, pChoiceInfo, pReason, 0, NULL);
}




static void MapTransferInit_Enter(InstancedMachineHandleOrName pStateMachineHandle, CharacterTransfer *transfer, F32 fElapsed)
{
	Entity *entity;

	entity = entFromEntityRefAnyPartition(transfer->entRef);

	if (!entity)
	{
		gslMapTransferFail(transfer,false,"Missing entity");
		return;
	}


	ISM_SwitchToSibling(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_SELECTING_MAP);

}


static void MapTransferSelectingMap_Enter(InstancedMachineHandleOrName pStateMachineHandle, CharacterTransfer *transfer, F32 fElapsed)
{
	Entity *ent;
	AllegianceDef *a;
	S32 teamTransferIndex;
	MapDescription *destMap = &transfer->pMapSearchInfo->baseMapDescription;
	bool clearTeamTransfer = true;

	ent = entFromEntityRefAnyPartition(transfer->entRef);

	if (!ent)
	{
		gslMapTransferFail(transfer,false,"Missing entity");
		return;
	}

	a = GET_REF(ent->hAllegiance);

	teamTransferIndex = GetAvailableTeamTransferIndex(ent, destMap);

	if ( ( teamTransferIndex >= 0 ) && ( transfer->teamTransferQueuedCount < TEAM_TRANSFER_MAX_RETRIES ) )
	{
		MapDescription *teamDestMap = GetTeamTransferDestination(ent, teamTransferIndex);

		devassert(teamDestMap != NULL);

		if ( teamDestMap != NULL )
		{
			if ( teamDestMap->containerID == 0 )
			{
				// We don't have the container ID of the destination server yet, so queue up and switch states
				// Queueing shouldn't fail, but if it does then just fall through and transfer normally
				if ( QueueForTeamTransfer(ent, teamTransferIndex, destMap) )
				{
					// increment number of times queued
					transfer->teamTransferQueuedCount++;

					// put the player in a special state to wait for the map ID to arrive
					ISM_SwitchToSibling(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_WAITING_FOR_TEAM_TRANSFER);

					// Force a loading screen for the player
					ClientCmd_gclLoading_SetForcedLoading(ent); 

					return;
				}
			}
			else
			{
				// We found a previous team member who went to the same map, so try to follow.
				transfer->pMapSearchInfo->baseMapDescription.containerID = teamDestMap->containerID;
				transfer->pMapSearchInfo->baseMapDescription.iPartitionID = teamDestMap->iPartitionID;
				transfer->pMapSearchInfo->overSoftCapOK = true;
				transfer->doingTeamTransfer = true;
			}
			clearTeamTransfer = false;
		}
	}
	
	if ( clearTeamTransfer )
	{
		// If there is not already a team transfer to the destination map, then set team
		//  transfer details for this player.  May do nothing depending on whether player
		//  is on a team, destination map type, etc.
		SetTeamTransferInfo(ent, destMap);
	}

	transfer->pMapSearchInfo->baseMapDescription.iVirtualShardID = entGetVirtualShardID(ent);

	transfer->pMapSearchInfo->pPlayerName = strdup(entGetLocalName(ent));
	transfer->pMapSearchInfo->pAllegiance = strdup(a ? a->pcName : NULL);

	if (transfer->eFlags & TRANSFERFLAG_NO_NEW_OWNED_MAP)
	{
		transfer->pMapSearchInfo->bNoNewOwned = true;
	}

	RemoteCommand_GetPossibleMapChoices(
		objCreateManagedReturnVal(GetPossibleMapChoices_CB, (void *)(intptr_t)transfer->loginCookie),
		GLOBALTYPE_MAPMANAGER, 0, transfer->pMapSearchInfo, NULL, transfer->pReasonString);


	VerboseTransferLog(transfer, "RequestedPossibleChoices", "Requested possible map choices, type %s map %s owner %s[%d]",
		StaticDefineIntRevLookup(MapSearchTypeEnum, transfer->pMapSearchInfo->eSearchType),
		transfer->pMapSearchInfo->baseMapDescription.mapDescription,
		GlobalTypeToName(transfer->pMapSearchInfo->baseMapDescription.ownerType),
		transfer->pMapSearchInfo->baseMapDescription.ownerID);

}


// Functions for TRANSFERSTATE_SELECTING_MAP

static void ReturnDestination_CB(TransactionReturnVal *returnVal, void *userData)
{
	U32 loginCookie = (U32)(intptr_t)userData;	
	CharacterTransfer *transfer = getCharacterTransferForCookie(loginCookie);
	Entity *ent;

	if (!transfer)
	{
		TransferLog(transfer, "InvalidTransfer", "Command %s returned for invalid transfer id %d", __FUNCTION__, loginCookie);
		return;
	}

	if (!ISM_IsStateActive(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_SELECTING_MAP))
	{
		TransferLog(transfer, "InvalidTransfer", "Command %s returned for transfer %d while in invalid state", __FUNCTION__, loginCookie);
		return;
	}

	ent = entFromEntityRefAnyPartition(transfer->entRef);

	if (!ent)
	{
		gslMapTransferFail(transfer,false,"Missing entity");
		return;
	}

	if (transfer->pReturnedAddress)
	{
		StructDestroy(parse_ReturnedGameServerAddress, transfer->pReturnedAddress);
	}

	switch(RemoteCommandCheck_RequestNewOrExistingGameServerAddress(returnVal, &transfer->pReturnedAddress))
	{
	case TRANSACTION_OUTCOME_FAILURE:
		gslMapTransferFail(transfer, false, "Couldn't get game server address from map manager");
		break;

	case TRANSACTION_OUTCOME_SUCCESS:


		transfer->destinationServer = transfer->pReturnedAddress->iContainerID;
		transfer->destinationType = GLOBALTYPE_GAMESERVER;
		transfer->destinationPartitionID = transfer->pReturnedAddress->uPartitionID;

		if (transfer->destinationServer == 0)
		{
			gslMapTransferFail(transfer, false, transfer->pReturnedAddress->errorString);
			break;
		}

		TransferLog(transfer, "TransferringCharacter", "Transferring to %s partition %u", GlobalTypeAndIDToString(transfer->destinationType, transfer->destinationServer), transfer->destinationPartitionID);

		// update transferring player's team transfer info and map info on the player
		UpdateTeamTransferMapID(ent, &transfer->pChosenMap->baseMapDescription, transfer->destinationServer, transfer->destinationPartitionID);
		if (ent->pPlayer)
		{
			ent->pPlayer->pchLastUsedDoorMapName = transfer->pChosenMap->baseMapDescription.mapDescription;
			ent->pPlayer->pchLastUsedDoorSpawnPointName = transfer->pChosenMap->baseMapDescription.spawnPoint;
			ent->pPlayer->uLastUsedDoorMapID = transfer->pReturnedAddress->iContainerID;
			ent->pPlayer->uLastUsedDoorPartitionID = transfer->pReturnedAddress->uPartitionID;
		}

		// wake up any players that were queued waiting for the map ID of this player's transfer
		TeamTransferDestinationNotify(ent, &transfer->pChosenMap->baseMapDescription);

		ISM_SwitchToSibling(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_TRANSFERRING_CHARACTER);

		break;
	}
}

bool gbFailAllMapTransfers = false;
AUTO_CMD_INT(gbFailAllMapTransfers, failAllMapTransfers) ACMD_CATEGORY(DEBUG);


void gslMapTransfer_BeginClientSideTransferIfNotAlreadyBegun(Entity *entity)
{
	CharacterTransfer *transfer = gslGetCharacterTransferForEntity(entity);
	Packet *pPak;

	if (!transfer)
	{
		return;
	}

	if (transfer->eFlags & TRANSFERFLAG_BEGAN_CLIENT_SIDE_TRANSFER)
	{
		VerboseTransferLog(transfer, "ClientSideTransferAlreadyBegun", "Not beginning client side transfer again, already begun");
		return;
	}

	if (!SAFE_MEMBER2(transfer, clientLink, netLink))
	{
		VerboseTransferLog(transfer, "LinkDisconnected", "Not beginning client side transfer, client link or its net link not present, character presumably disconnecting");
		return;
	}

	VerboseTransferLog(transfer, "BeginClientSideTransfer", "Beginning client side transfer, %sdoing patching", transfer->pChosenMap->patchInfo ? "" : "NOT ");


	transfer->eFlags |= TRANSFERFLAG_BEGAN_CLIENT_SIDE_TRANSFER;
	pPak = pktCreate(transfer->clientLink->netLink, TOCLIENT_GAME_START_TRANSFER);
	pktSendBool(pPak, transfer->pChosenMap->patchInfo!=NULL);
	if(transfer->pChosenMap->patchInfo)
		ParserSendStruct(parse_DynamicPatchInfo, pPak, transfer->pChosenMap->patchInfo);
	pktSend(&pPak);

	linkSetKeepAliveSeconds(transfer->clientLink->netLink,4); // To avoid the "disconnected" message on client
	gslLeaveMap(entity);
}

//when we send GetNewOrExistingGameServerAddress to the mapmanager, most of the time we get an answer back basically identically,
//in which case we just do BeginClientSideTransfer then. However, if it's going to be slow (ie, waiting for a new GS.exe to start up),
//we get this remote command so we begin the client side transfer early
AUTO_COMMAND_REMOTE;
void AddressReturnWillBeSlow(U32 iCookie)
{
	CharacterTransfer *transfer = getCharacterTransferForCookie(iCookie);
	Entity *pEnt;

	if (!transfer)
	{
		return;
	}

	VerboseTransferLog(transfer, "WillBeSlow", "Return address will be slow");

	pEnt = entFromEntityRefAnyPartition(transfer->entRef);

	if (!pEnt)
	{
		gslMapTransferFail(transfer,false,"Missing entity");
		return;
	}

	gslMapTransfer_BeginClientSideTransferIfNotAlreadyBegun(pEnt);
}


// Remote command called from the client to choose a certain address
AUTO_COMMAND ACMD_PRIVATE ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_IFDEF(GAMECLIENT);
void gslMapTransferChooseAddress(Entity *entity, PossibleMapChoice *pInChoice)
{
	CharacterTransfer *transfer = gslGetCharacterTransferForEntity(entity);
	int i;
	bool bFound = false;
	PossibleMapChoice *pFoundChoice = NULL;
	PossibleMapChoice *pBackupChoice = NULL;
	NewOrExistingGameServerAddressRequesterInfo requesterInfo = {0};

	if (!transfer)
	{
		// Something bad is going on
		return;
	}

	//client has logged out
	if (!transfer->clientLink || !transfer->clientLink->clientLoggedIn || !transfer->clientLink->netLink)
	{
		gslMapTransferFail(transfer,true, "Cancelled");
		return;
	}

	VerboseTransferLog(transfer, "ClientChoseAddress", "Client chose address %s", GetSimpleStringForMapChoice(pInChoice));

	if (pInChoice->bNotALegalChoice)
	{
		gslMapTransferFail(transfer,true, "Cancelled");
		eaFindAndRemove(&gTransferringCharacters,transfer);
		destroyCharacterTransfer(transfer);		
		return;
	}

	if (pInChoice->bDebugLogin)
	{
		if (entGetAccessLevel(entity) >= ACCESS_GM || (transfer->eFlags & TRANSFERFLAG_CSR))
		{
			bFound = true;
		}
		else
		{		
			gslMapTransferFail(transfer,false,"Not allowed to use debug login");	
			// Can't use debug login as access level 0
			return;
		}
	}

	for (i = 0; i < eaSize(&transfer->pPossibleMapChoices->ppChoices); i++)
	{
		pFoundChoice = transfer->pPossibleMapChoices->ppChoices[i];

		//don't compare quats, because quats can't be trusted to compare to zero after being written and read
		if (StructCompare(parse_PossibleMapChoice, pFoundChoice, pInChoice, 0, 0, TOK_USEROPTIONBIT_1) == 0)
		{
			// Try other possibilities before forcing a new map
			if (pFoundChoice->eChoiceType == MAPCHOICETYPE_FORCE_NEW)
			{
				pBackupChoice = pFoundChoice;
				continue;
			}
			bFound = true;
			break;
		}
	}

	// Consider the last resort choice if nothing else was found
	if (!bFound && pBackupChoice)
	{
		pFoundChoice = pBackupChoice;
		bFound = true;
	}

	if (!bFound)
	{
		//do full logging of this, since it should basically never happen.
		char *pErrorString = NULL;
		char *pStructString = NULL;
		estrPrintf(&pErrorString, "Entity requested an invalid map transfer.\n",);
		ParserWriteText(&pStructString, parse_PossibleMapChoice, pInChoice, 0, 0, 0);
		estrConcatf(&pErrorString, "Requested map choice:\n\n%s\n\n", pStructString);

		for (i = 0; i < eaSize(&transfer->pPossibleMapChoices->ppChoices); i++)
		{
			pFoundChoice = transfer->pPossibleMapChoices->ppChoices[i];
			ParserWriteText(&pStructString, parse_PossibleMapChoice, pFoundChoice, 0, 0, 0);
			estrConcatf(&pErrorString, "Possible choice %d:\n\n%s\n\n", i, pStructString);
		}

		entLog(LOG_GSL, entity, "InvalidMapRequested", "%s", pErrorString);

		estrDestroy(&pErrorString);
		estrDestroy(&pStructString);
		

		gslMapTransferFail(transfer,false, "Invalid map requested");
		return;
	}



	if (gbFailAllMapTransfers)
	{
		gslMapTransferFail(transfer,false, "fake failure");
		return;
	}


	if (!ISM_IsStateActiveOrPending(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_SELECTING_MAP))
	{
		gslMapTransferFailf(transfer, false, "Got address request while in wrong state %s",
			GetCurrentAndPendingStateString(transfer));
		return;
	}

	if (transfer->pChosenMap)
	{
		StructReset(parse_PossibleMapChoice, transfer->pChosenMap);
	}
	else
	{
		transfer->pChosenMap = StructCreate(parse_PossibleMapChoice);
	}

	StructCopyAll(parse_PossibleMapChoice, pFoundChoice, transfer->pChosenMap);

	requesterInfo.pcRequestingShardName = GetShardNameFromShardInfoString();
	requesterInfo.eRequestingServerType = GetAppGlobalType();
	requesterInfo.iRequestingServerID = GetAppGlobalID();
	requesterInfo.iEntContainerID = entGetContainerID(entity);
	requesterInfo.iPlayerAccountID = entGetPlayer(entity)->accountID;
	requesterInfo.iPlayerIdentificationCookie = transfer->loginCookie;
	requesterInfo.iPlayerLangID = entGetLanguage(entity);
	requesterInfo.pPlayerAccountName = entGetPlayer(entity)->privateAccountName;
	requesterInfo.pPlayerName = entGetLocalName(entity);
	requesterInfo.iRequestingTeamID = team_GetTeamID(entity);

	VerboseTransferLog(transfer, "RequestingAddress", "calling RemoteCommand_RequestNewOrExistingGameServerAddress");

	RemoteCommand_RequestNewOrExistingGameServerAddress(
		objCreateManagedReturnVal_TransactionMayTakeALongTime(ReturnDestination_CB, (void *)(intptr_t)transfer->loginCookie),
		GLOBALTYPE_MAPMANAGER, 0, 
		transfer->pChosenMap, NULL, &requesterInfo);

	transfer->eFlags |= TRANSFERFLAG_PASSED_POINT_OF_NO_RETURN;

	sndServerMapTransfer(entity);
}


// Functions for TRANSFERSTATE_TRANSFERRING_CHARACTER

static void FinalTransfer_CB(TransactionReturnVal *returnVal, void *userData)
{
	U32 loginCookie = (U32)(intptr_t)userData;	
	CharacterTransfer *transfer = getCharacterTransferForCookie(loginCookie);
	Packet *pPak;
	char *pFailureString;

	if (!transfer)
	{
		TransferLog(transfer, "InvalidTransfer", "Command %s returned for invalid transfer id %d", __FUNCTION__, loginCookie);
		return;
	}

	if (!ISM_IsStateActive(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_TRANSFERRING_CHARACTER))
	{
		TransferLog(transfer, "InvalidTransfer", "Command %s returned for transfer %d while in invalid state", __FUNCTION__, loginCookie);
		return;
	}

	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		pFailureString = GetTransactionFailureString(returnVal);

		if (stricmp(pFailureString, "Unspecified")==0)
		{
			char *pReturnValStr = NULL;
			char *pTransStr = NULL;
			char *pFullFailureString = NULL;
			//we're trying to track down an unspecified failure... need more logging.

			estrStackCreate(&pReturnValStr);
			estrStackCreate(&pTransStr);
			estrStackCreate(&pFullFailureString);

			ParserWriteText(&pTransStr, parse_CharacterTransfer, transfer, 0, 0, 0);
			TransactionReturnVal_VerboseDump(returnVal, &pReturnValStr);

			estrPrintf(&pFullFailureString, "Got mysterious unspecified transfer failure. Trans return struct: %s. Transfer: %s",
				pReturnValStr, pTransStr);

			gslMapTransferFail(transfer, false, "Couldn't transfer character");
			TransferLog(transfer, "TransferFailed", pFullFailureString);

			estrDestroy(&pFullFailureString);
			estrDestroy(&pReturnValStr);
			estrDestroy(&pTransStr);
	
		}
		else
		{

			gslMapTransferFail(transfer, false, "Couldn't transfer character");

			TransferLog(transfer, "TransferFailed", pFailureString);
		}

		break;

	case TRANSACTION_OUTCOME_SUCCESS:
		transfer->pReturnedAddress->iCookie = transfer->loginCookie;

		if (transfer->clientLink->netLink)
		{		
			pPak = pktCreate(transfer->clientLink->netLink, TOCLIENT_GAME_SERVER_ADDRESS);
			pktSendBits(pPak, 1, 1);
			ParserSend(parse_ReturnedGameServerAddress, pPak, NULL, transfer->pReturnedAddress, SENDDIFF_FLAG_FORCEPACKALL, 0, 0, NULL);
			pktSend(&pPak);

			// pass returnedAddress to client

			if (!transfer->clientLink->disconnected)
			{
				log_printf(LOG_CLIENTSERVERCOMM, "adding netlink %p to disconnect list because map transfer has succeeded", transfer->clientLink->netLink);
				linkFlushAndClose(&transfer->clientLink->netLink, "Map Transfer Succeeded");
				transfer->clientLink->disconnected = 1;
			}
		}
	
		TransferLog(transfer, "TransferComplete", "Transfer is complete");
		ISM_SwitchToSibling(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_COMPLETE);

		break;
	}
}

static void gslMapTransferRequestMoveToServer(CharacterTransfer *transfer)
{
	Entity *ent = entFromEntityRefAnyPartition(transfer->entRef);

	if (!ent)
	{
		gslMapTransferFail(transfer, false, "Nonexistant ent");
		return;
	}

	if (transfer->destinationServer == GetAppGlobalID())
	{
		VerboseTransferLog(transfer, "SameServer", "Moving to the server we're already on (must be to a different partition)");

		if (!transfer->destinationPartitionID)
		{
			gslMapTransferFail(transfer, false, "Transfer requested to same GS without partition");
			return;
		}

		if (!partition_ExistsByID(transfer->destinationPartitionID))
		{
			gslMapTransferFailf(transfer, false, "Transfer requested to nonexistant partition %u", transfer->destinationPartitionID);
			return;
		}

		if (transfer->destinationPartitionID == partition_IDFromIdx(entGetPartitionIdx(ent)))
		{
			gslMapTransferFailf(transfer, false, "Transfer requested to same partition");
			return;
		}

		ISM_SwitchToSibling(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_MOVING_BETWEEN_PARTITIONS_PRESAVE);

	}
	else
	{

		VerboseTransferLog(transfer, "DifferentServer", "Moving to a different server, requesting container move");

		gslMapTransfer_BeginClientSideTransferIfNotAlreadyBegun(ent);
		objRequestContainerMove(objCreateManagedReturnVal(FinalTransfer_CB, (void *)(intptr_t)transfer->loginCookie),
			transfer->containerType, transfer->containerID, objServerType(), objServerID(), transfer->destinationType, transfer->destinationServer);
	}
}

static void LoginTransferringCharacter_Enter(InstancedMachineHandleOrName pStateMachineHandle, CharacterTransfer *transfer, F32 fElapsed)
{
	Entity *ent;


	ent = entFromEntityRefAnyPartition(transfer->entRef);

	if (!ent)
	{
		gslMapTransferFail(transfer, false, "Missing entity");
		return;
	}

	gslMapTransferRequestMoveToServer(transfer);
}

// Stuff to do when transferring to a different server
enumTransactionOutcome PlayerTransferCB(ATR_ARGS, NOCONST(Entity) *newPlayer, NOCONST(Entity) *backupPlayer, GlobalType locationType, ContainerID locationID)
{
	// Grab the real one, not the transaction copy
	Entity *bEnt = entFromContainerIDAnyPartition(newPlayer->myEntityType,newPlayer->myContainerID);
	CharacterTransfer *transfer = getCharacterTransferForContainer(newPlayer->myEntityType,newPlayer->myContainerID);

	if (sbFailAllPlayerTransfers)
	{
		TRANSACTION_RETURN_LOG_FAILURE("sbFailAllPlayerTransfers set");
	}

	if (!transfer)
	{
		TRANSACTION_RETURN_LOG_FAILURE("No character transfer for container");
	}

	if (!transfer->pChosenMap)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Transfer has no chosen map");
	}

	if (!bEnt)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Invalid Ent");
	}

	newPlayer->pPlayer->loginCookie = transfer->loginCookie;

	//players who never finished logging on shouldn't have their map history modified
	if (bEnt->initPlayerLoginPositionRun)
	{
        // update the player's position so that if they return to this map they will be in the same spot
		gslEntityMapHistoryLeftMap(newPlayer, &transfer->pChosenMap->baseMapDescription);

        // Save the destination map ID and spawn point.  The destination map will use this to determine where to spawn the player.
        newPlayer->pSaved->transferDestinationMapID = transfer->destinationServer;
        newPlayer->pSaved->transferDestinationSpawnPoint = transfer->pChosenMap->baseMapDescription.spawnPoint;
		newPlayer->pSaved->bSpawnPosSkipBeaconCheck = transfer->pChosenMap->baseMapDescription.bSpawnPosSkipBeaconCheck;
		copyVec3(transfer->pChosenMap->baseMapDescription.spawnPos, newPlayer->pSaved->transferDestinationSpawnPos);
		copyVec3(transfer->pChosenMap->baseMapDescription.spawnPYR, newPlayer->pSaved->transferDestinationSpawnPYR);

		//if we're transferring to another copy of the same map, make sure to force use our spawn point, so the code in
		//gslEntityMapHistoryEnteredMap doesn't skip it entirely
		if (IsSameMapDescription(&transfer->pChosenMap->baseMapDescription, partition_GetMapDescription(entGetPartitionIdx(bEnt))))
		{
			newPlayer->pSaved->forceTransferDestinationSpawnPoint = true;
		}
	}

    StructCopyFieldsDeConst(parse_Entity, bEnt->pSaved->pEntityBackup, backupPlayer, TOK_PERSIST, 0);

	return TRANSACTION_OUTCOME_SUCCESS;
}


// Stuff to do when transferring to a different server
enumTransactionOutcome PetTransferCB(ATR_ARGS, NOCONST(Entity) *newPlayer, NOCONST(Entity) *backupPlayer, GlobalType locationType, ContainerID locationID)
{
	// Grab the real one, not the transaction copy
	Entity *bEnt = entFromContainerIDAnyPartition(newPlayer->myEntityType,newPlayer->myContainerID);

	if (!bEnt)
	{
		TRANSACTION_RETURN_LOG_FAILURE("Invalid Ent");
	}

	// This sends the non-transacted persist fields to the DB
	StructCopyFieldsDeConst(parse_Entity, bEnt->pSaved->pEntityBackup, backupPlayer, TOK_PERSIST, 0);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(pMyEnt, ".Psaved.Laststaticmap, .Psaved.Lastnonstaticmap");
enumTransactionOutcome atr_MoveBetweenPartitions(ATR_ARGS, NOCONST(Entity) *pMyEnt, PossibleMapChoice *pChosenMap, ReturnedGameServerAddress *pReturnedAddress)
{
	MapDescription *pDescriptionToUpdate;

	if (pChosenMap->baseMapDescription.eMapType == ZMTYPE_STATIC)
	{
		pDescriptionToUpdate = (MapDescription*)(pMyEnt->pSaved->lastStaticMap);
	}
	else
	{
		pDescriptionToUpdate = (MapDescription*)(pMyEnt->pSaved->lastNonStaticMap);
	}

	pDescriptionToUpdate->iPartitionID = pReturnedAddress->uPartitionID;
	pDescriptionToUpdate->mapInstanceIndex = pReturnedAddress->iInstanceIndex;
	pDescriptionToUpdate->spawnPoint = pChosenMap->baseMapDescription.spawnPoint;

	copyVec3(pChosenMap->baseMapDescription.spawnPos, pDescriptionToUpdate->spawnPos);
	copyVec3(pChosenMap->baseMapDescription.spawnPYR, pDescriptionToUpdate->spawnPYR);


	return TRANSACTION_OUTCOME_SUCCESS;
}

void MoveBetweenPartitionsCB(TransactionReturnVal *returnVal, void *userData)
{
	CharacterTransfer *pTransfer = getCharacterTransferForCookie((U32)((intptr_t)userData));

	if (!pTransfer)
	{
		return;
	}

	if (returnVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		gslMapTransferFailf(pTransfer, false, "atr_MoveBetweenPartitions failed: %s", GetTransactionFailureString(returnVal));
		return;
	}

	pTransfer->eFlags |= TRANSFERFLAG_MOVE_BETWEEN_PARTITIONS_TRANS_COMPLETED;
}

void MapTransferMovingBetweenPartitionsPreSave_Enter(InstancedMachineHandleOrName pStateMachineHandle, CharacterTransfer *transfer, F32 fElapsed)
{
	Entity *ent;


	ent = entFromEntityRefAnyPartition(transfer->entRef);

	if (!ent)
	{
		gslMapTransferFail(transfer, false, "Missing entity");
		return;
	}

	coarseTimerAddInstance(NULL, "MapTransferMovingBetweenPartitionsPreSave_Enter");

	transfer->fTimeInState = 0;

	coarseTimerStopInstance(NULL, "MapTransferMovingBetweenPartitionsPreSave_Enter");
}

void MapTransferMovingBetweenPartitionsPreSave_BeginFrame(InstancedMachineHandleOrName pStateMachineHandle, CharacterTransfer *transfer, F32 fElapsed)
{
	Entity *ent;

	transfer->fTimeInState += fElapsed;

	ent = entFromEntityRefAnyPartition(transfer->entRef);

	if (!ent)
	{
		gslMapTransferFail(transfer, false, "Missing entity");
		return;
	}

	coarseTimerAddInstance(NULL, "MapTransferMovingBetweenPartitionsPreSave_BeginFrame");

	VerboseTransferLog(transfer, "BetweenPartitions", "Beginning move between partitions, calling SetForcedLoading");

	if (gslEntitySafeToSend(ent) || transfer->fTimeInState > sbDelayBeforeForceSavingOnTransfer)
	{
		if(!gslEntitySafeToSend(ent))
			entLog(LOG_CONTAINER, ent, "PushForced", "Non-transact data push forced due to constant locks");

		gslSendEntityToDatabase(ent, true); // Force save of no_transact data fields
		gslPlayerLeftMap(ent, true); // Cause pets to despawn and player otherwise leave
		ClientCmd_gclLoading_SetForcedLoading(ent); // Put up loading screen for player

		AutoTrans_atr_MoveBetweenPartitions(objCreateManagedReturnVal(MoveBetweenPartitionsCB, (void*)(transfer->loginCookie)), GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, entGetContainerID(ent), transfer->pChosenMap, transfer->pReturnedAddress);
		ISM_SwitchToSibling(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_MOVING_BETWEEN_PARTITIONS);
	}

	coarseTimerStopInstance(NULL, "MapTransferMovingBetweenPartitionsPreSave_BeginFrame");
}

void MapTransferMovingBetweenPartitions_Enter(InstancedMachineHandleOrName pStateMachineHandle, CharacterTransfer *transfer, F32 fElapsed)
{
	Entity *ent;


	ent = entFromEntityRefAnyPartition(transfer->entRef);

	if (!ent)
	{
		gslMapTransferFail(transfer, false, "Missing entity");
		return;
	}

	coarseTimerAddInstance(NULL, "MapTransferMovingBetweenPartitions_Enter");

	transfer->fTimeInState = 0;

	coarseTimerStopInstance(NULL, "MapTransferMovingBetweenPartitions_Enter");
}

void MapTransferMovingBetweenPartitions_BeginFrame(InstancedMachineHandleOrName pStateMachineHandle, CharacterTransfer *transfer, F32 fElapsed)
{
	transfer->fTimeInState += fElapsed;
	if (transfer->fTimeInState >= PAUSE_WHILE_MOVING_PARTITIONS_TIME && (transfer->eFlags & TRANSFERFLAG_MOVE_BETWEEN_PARTITIONS_TRANS_COMPLETED))
	{
		Entity *ent = entFromEntityRefAnyPartition(transfer->entRef);

		if (!ent)
		{
			gslMapTransferFail(transfer, false, "Missing entity");
			return;
		}
		ANALYSIS_ASSUME(ent);

		if (!partition_ExistsByID(transfer->destinationPartitionID))
		{
			gslMapTransferFail(transfer, false, "nonexistant partition when ready to transfer");
			return;
		}

		coarseTimerAddInstance(NULL, "MapTransferMovingBetweenPartitions_BeginFrame");

		VerboseTransferLog(transfer, "BetweenPartitionsDone", "Completing between-partition move, switching partition and calling ClearForcedLoading");

		// Added new parameter in order to clear out modarray.ppPowers array. Otherwise powers were freed but earray remained (pointing to freed data)
		gslCleanupEntityEx(entGetPartitionIdx_NoAssert(ent), ent, true, true);

		partition_DebugLogInternal(PARTITION_PLAYER_LEFT, ent->iPartitionIdx_UseAccessor, "%u (%s) moving to another partition",
			ent->myContainerID, ENTDEBUGNAME(ent));

		ent->iPartitionIdx_UseAccessor = partition_IdxFromID(transfer->destinationPartitionID);
		
		partition_DebugLogInternal(PARTITION_PLAYER_ENTERED, ent->iPartitionIdx_UseAccessor, "%u (%s) moved from another partition",
			ent->myContainerID, ENTDEBUGNAME(ent));


		gslInitializeEntity(ent, true);
		
		HandlePlayerLogin_EarlyEntityTasks(ent);
		HandlePlayerLogin_EntityTasks(ent);
		ClientCmd_gclLoading_ClearForcedLoading(ent); // Clear the loading screen
		HandleDoneLoading_Entity(ent);
		mmAttachToClient(ent->mm.movement, entGetClientLink(ent)->movementClient);
		gslQueueFullUpdateForLink(entGetClientLink(ent));

		TransferLog(transfer, "TransferComplete", "Transfer is complete (different partition same GS)");
		ISM_SwitchToSibling(MAP_TRANSFER_STATE_MACHINE, transfer, TRANSFERSTATE_COMPLETE);
		ent->pPlayer->bMapTransferPending = false;

		destroyCharacterTransferDeferred(transfer);

		coarseTimerStopInstance(NULL, "MapTransferMovingBetweenPartitions_BeginFrame");
	}

}



AUTO_RUN;
void RegisterMapTransferStates(void)
{
	objRegisterSpecialTransactionCallback(GLOBALTYPE_ENTITYPLAYER, TRANSACTION_MOVE_CONTAINER_TO, PlayerTransferCB);
	objRegisterSpecialTransactionCallback(GLOBALTYPE_ENTITYSAVEDPET, TRANSACTION_MOVE_CONTAINER_TO, PetTransferCB);

	ISM_AddInstancedState(MAP_TRANSFER_STATE_MACHINE, TRANSFERSTATE_INITIAL, MapTransferInit_Enter, NULL, NULL, NULL);
	ISM_AddInstancedState(MAP_TRANSFER_STATE_MACHINE, TRANSFERSTATE_SELECTING_MAP, MapTransferSelectingMap_Enter, NULL, NULL, NULL);
	ISM_AddInstancedState(MAP_TRANSFER_STATE_MACHINE, TRANSFERSTATE_WAITING_FOR_TEAM_TRANSFER, NULL, MapTransferWaitingForTeamTransfer_BeginFrame, NULL, NULL);
	ISM_AddInstancedState(MAP_TRANSFER_STATE_MACHINE, TRANSFERSTATE_TRANSFERRING_CHARACTER, LoginTransferringCharacter_Enter, NULL, NULL, NULL);
	ISM_AddInstancedState(MAP_TRANSFER_STATE_MACHINE, TRANSFERSTATE_FAILED, NULL, NULL, NULL, NULL);	
	ISM_AddInstancedState(MAP_TRANSFER_STATE_MACHINE, TRANSFERSTATE_COMPLETE, NULL, NULL, NULL, NULL);
	ISM_AddInstancedState(MAP_TRANSFER_STATE_MACHINE, TRANSFERSTATE_MOVING_BETWEEN_PARTITIONS_PRESAVE, MapTransferMovingBetweenPartitionsPreSave_Enter, MapTransferMovingBetweenPartitionsPreSave_BeginFrame, NULL, NULL);
	ISM_AddInstancedState(MAP_TRANSFER_STATE_MACHINE, TRANSFERSTATE_MOVING_BETWEEN_PARTITIONS, MapTransferMovingBetweenPartitions_Enter, MapTransferMovingBetweenPartitions_BeginFrame, NULL, NULL);
}

void MapMoveFromSavedMapDescription(Entity *pEnt, SavedMapDescription *pMap)
{
	MapSearchInfo choiceInfo = {0};

	MapMoveFillMapDescription(	&choiceInfo.baseMapDescription,pMap->mapDescription,pMap->eMapType, pMap->spawnPoint,pMap->mapInstanceIndex,
							  pMap->containerID,0,0,0);

	copyVec3(pMap->spawnPos, choiceInfo.baseMapDescription.spawnPos);
	copyVec3(pMap->spawnPYR, choiceInfo.baseMapDescription.spawnPYR);

	gslMapTransferRequest_Internal(pEnt,&choiceInfo, STACK_SPRINTF("Puppet Failure call on %s",pEnt->debugName), 0, NULL);
}




// Development commands to initiate a transfer

void MapMoveFillMapDescriptionEx(MapDescription *pMapDesc, SA_PARAM_NN_STR const char *zoneName, ZoneMapType eMapType, SA_PARAM_OP_STR const char *spawnName, int iMapIndex, ContainerID iMapContainerID, U32 uPartitionID, GlobalType eOwnerType, ContainerID ownerID, const char* pchMapVars)
{
	if (eMapType == ZMTYPE_UNSPECIFIED)
	{
		ZoneMapInfo* pZoneMap = worldGetZoneMapByPublicName(zoneName);
		if (pZoneMap)
		{
			eMapType = zmapInfoGetMapType(pZoneMap);
		}
	}
	pMapDesc->eMapType = eMapType;
	if (zoneName)
		pMapDesc->mapDescription = allocAddString(zoneName);
	pMapDesc->mapInstanceIndex = iMapIndex;
	pMapDesc->containerID = iMapContainerID;
	pMapDesc->iPartitionID = uPartitionID;
	pMapDesc->ownerID = ownerID;
	pMapDesc->ownerType = eOwnerType;
	pMapDesc->spawnPoint = allocAddString(START_SPAWN);
	
	pMapDesc->mapVariables = allocAddString(pchMapVars);
	if (pchMapVars && !pMapDesc->mapVariables)
	{
		Errorf("Variable values are too long in map move static and are being ignored");
	}

	if(spawnName)
	{
		if(strlen(spawnName) >= 254)
			Errorf("Spawn name is too long in map move static");
		else
			pMapDesc->spawnPoint = allocAddString(spawnName);
	}
}

void MapMoveFillMapDescription(MapDescription *pMapDesc, SA_PARAM_NN_STR const char *zoneName, ZoneMapType eMapType, SA_PARAM_OP_STR const char *spawnName, int iMapIndex, ContainerID iMapContainerID, GlobalType eOwnerType, ContainerID ownerID, WorldVariable **eaVariables)
{
	const char* pchMapVars = worldVariableArrayToString(eaVariables);
	MapMoveFillMapDescriptionEx(pMapDesc,zoneName,eMapType, spawnName,iMapIndex,iMapContainerID,0,eOwnerType,ownerID,pchMapVars);
}

// Request a transfer to a map
void MapMoveStaticEx(Entity *ent, const char *zoneName, const char *spawnName, int iMapIndex, ContainerID iMapContainerID, U32 uPartitionID, GlobalType eOwnerType, ContainerID ownerID, WorldVariable **eaVariables, TransferFlags eFlags, MapSearchType eSearchType, char *pReason, CmdContext *pContext)
{
	MapSearchInfo choiceInfo = {0};
	bool bIgnoreEncounterSpawnLogic = false;

	MapMoveFillMapDescriptionEx(&choiceInfo.baseMapDescription,
								zoneName,
								ZMTYPE_UNSPECIFIED,
								spawnName,
								iMapIndex,
								iMapContainerID,
								uPartitionID,
								eOwnerType,
								ownerID,
								worldVariableArrayToString(eaVariables));

	choiceInfo.eSearchType = eSearchType;

	gslMapTransferRequest_Internal(ent,&choiceInfo, pReason, eFlags, pContext);
}

RegionRules* MapTransferGetRegionRulesFromMapName( const char* pchNextMap )
{
	ZoneMapInfo* pNextZoneMap = worldGetZoneMapByPublicName(pchNextMap);

	return pNextZoneMap ? getRegionRulesFromZoneMap(pNextZoneMap) : NULL;
}

// change to an already created instance of the same map. Only works while not in combat.
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(ChangeInstance) ACMD_ACCESSLEVEL(0);
void ChangeInstance(SA_PARAM_NN_VALID Entity *pEntity, CmdContext *pContext)
{
	if(pEntity && pEntity->pChar)
	{
		// only allow if static or shared map
		ZoneMapInfo* pInfo = zmapInfoGetByPublicName(gGSLState.gameServerDescription.baseMapDescription.mapDescription);
		bool bDisableInstanceChanging = pInfo && zmapInfoGetDisableInstanceChanging(pInfo);

		if( gConf.uSecondsBetweenChangeInstance > 0 )
		{
			if( pEntity->pSaved && pEntity->pSaved->timeEnteredMap )
			{
				bool bTooSoon = (gConf.uSecondsBetweenChangeInstance + pEntity->pSaved->timeEnteredMap > timeServerSecondsSince2000());
				bDisableInstanceChanging = bDisableInstanceChanging || bTooSoon;
			}
		}
		
		if(!bDisableInstanceChanging && !character_HasMode(pEntity->pChar,kPowerMode_Combat) &&
			(gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_STATIC || 
			 gGSLState.gameServerDescription.baseMapDescription.eMapType == ZMTYPE_SHARED
			 || gGSLState.gameServerDescription.bAllowInstanceSwitchingBetweenOwnedMaps && partition_OwnerTypeFromIdx(entGetPartitionIdx(pEntity))
			 ))
		{
			MapSearchInfo choiceInfo = {0};

			choiceInfo.baseMapDescription.eMapType = gGSLState.gameServerDescription.baseMapDescription.eMapType;
			choiceInfo.baseMapDescription.mapDescription = allocAddString(gGSLState.gameServerDescription.baseMapDescription.mapDescription);
			choiceInfo.baseMapDescription.mapVariables = allocAddString(gGSLState.gameServerDescription.baseMapDescription.mapVariables);
			choiceInfo.baseMapDescription.spawnPoint = allocAddString(SPAWN_AT_NEAR_RESPAWN);
			
			if (partition_OwnerTypeFromIdx(entGetPartitionIdx(pEntity)))
			{
				choiceInfo.eSearchType = MAPSEARCHTYPE_ALL_OWNED_MAPS;
				choiceInfo.baseMapDescription.ownerID = partition_OwnerIDFromIdx(entGetPartitionIdx(pEntity));
				choiceInfo.baseMapDescription.ownerType = partition_OwnerTypeFromIdx(entGetPartitionIdx(pEntity));
			}
		
			gslMapTransferRequest_Internal(pEntity, &choiceInfo, "ChangeInstance", 
				TRANSFERFLAG_FORCE_SEND_OPTIONS_TO_CLIENT | TRANSFERFLAG_ONLY_EXISTING_MAPS /*| TRANSFERFLAG_IGNORE_CURRENT_MAP*/ | TRANSFERFLAG_SHOW_FULL_MAPS, pContext);
		}
	}
}

// Move to the named map
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(MapMoveStatic, MapMove) ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(debug, csr);
void MapMoveStatic(Entity *ent, ACMD_NAMELIST("ZoneMap",REFDICTIONARY) char *zoneName, CmdContext *pContext)
{
	MapMoveStaticEx(ent, zoneName, NULL, 0, 0, 0, 0, 0, NULL, 0, 0, STACK_SPRINTF("MapMoveStatic called via %s", GetContextHowString(pContext)), pContext);
}

AUTO_COMMAND ACMD_SERVERCMD;
void MapMoveIndex(Entity *ent, ACMD_NAMELIST("ZoneMap",REFDICTIONARY) char *zoneName, int index, CmdContext *pContext)
{
	MapMoveStaticEx(ent, zoneName, NULL, index, 0, 0, 0, 0, NULL, 0, MAPSEARCHTYPE_SPECIFIC_PUBLIC_INDEX_ONLY, STACK_SPRINTF("MapMoveStatic called via %s", GetContextHowString(pContext)), pContext);
}

AUTO_COMMAND ACMD_SERVERCMD;
void MapMoveContainerAndPartitionID(Entity *ent, ACMD_NAMELIST("ZoneMap",REFDICTIONARY) char *zoneName, ContainerID iContainerID, U32 iPartitionID, CmdContext *pContext)
{
	MapMoveStaticEx(ent, zoneName, NULL, 0, iContainerID, iPartitionID, 0, 0, NULL, 0, MAPSEARCHTYPE_SPECIFIC_CONTAINER_AND_PARTITION_ID_ONLY, STACK_SPRINTF("MapMoveStatic called via %s", GetContextHowString(pContext)), pContext);
}


// Request a transfer to a specific point
AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(MapMoveDebug) ACMD_ACCESSLEVEL(7);
void MapMoveDebug(Entity *ent, char *zoneName, Vec3 vPos, Vec3 vRot, char* mapVariables, CmdContext *pContext)
{
	MapSearchInfo choiceInfo = {0};
	Quat qRot;
	PYRToQuat(vRot, qRot);

	choiceInfo.baseMapDescription.eMapType = ZMTYPE_UNSPECIFIED;
	choiceInfo.baseMapDescription.mapDescription = allocAddString(zoneName);
	if (mapVariables && *mapVariables)
	{
		choiceInfo.baseMapDescription.mapVariables = allocAddString(mapVariables);
	}
	copyVec3(vPos, choiceInfo.baseMapDescription.spawnPos);
	copyVec3(vRot, choiceInfo.baseMapDescription.spawnPYR);
	choiceInfo.debugPosLogin = true;
	choiceInfo.baseMapDescription.ownerID = entGetContainerID(ent);
	choiceInfo.baseMapDescription.spawnPoint = allocAddString(POSITION_SET);
	choiceInfo.baseMapDescription.bSpawnPosSkipBeaconCheck = true;
	choiceInfo.playerAccountID = entGetPlayer(ent)->accountID;

	if (!gslIsOfflineCSREnt(entGetType(ent), entGetContainerID(ent)) && IsSameMapDescription(&choiceInfo.baseMapDescription, partition_GetMapDescription(entGetPartitionIdx(ent))))
	{
		Vec3 vPyr;
		copyVec3(vRot, vPyr);
		entSetPos(ent, vPos, 1, __FUNCTION__);
		entSetRot(ent, qRot, 1, __FUNCTION__);
		vPyr[1] = addAngle(vPyr[1], PI);
		ClientCmd_setGameCamPYR(ent, vPyr);
		gslCacheEntRegion(ent, entity_GetCachedGameAccountDataExtract(ent));
	}
	else
	{
		gslMapTransferRequest_Internal(ent,&choiceInfo, STACK_SPRINTF("MapMoveStatic called via %s", GetContextHowString(pContext)), 0, pContext);
	}
}

AUTO_COMMAND ACMD_NAME(SetDebugPosEx);
void gslSetRequestedDebugPosEx(Entity *pEnt, char *mapName, Vec3 vPos, Vec3 vRot, char* mapVariables, CmdContext *pContext)
{
	vRot[0] = vRot[2] = 0.0; // strip out pitch and roll for now

	if (pEnt)
	{
		MapMoveDebug(pEnt, mapName, vPos, vRot, mapVariables, pContext);
	}
}

AUTO_COMMAND ACMD_NAME(SetDebugPos);
void gslSetRequestedDebugPos(Entity *pEnt, char *mapName, Vec3 vPos, Vec3 vRot, CmdContext *pContext)
{
	gslSetRequestedDebugPosEx(pEnt, mapName, vPos, vRot, "", pContext);
}

AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0);
void MapMoveConfirm(Entity *pEnt, int confirm)
{
	if(pEnt && pEnt->pPlayer && pEnt->pPlayer->pMapMoveConfirm && !pEnt->pPlayer->pMapMoveConfirm->bConfirmed)
	{
		PlayerMapMoveConfirm *pConfirm = pEnt->pPlayer->pMapMoveConfirm;
		if(confirm && 
			(!pConfirm->uiTimeToConfirm || pConfirm->uiTimeStart + pConfirm->uiTimeToConfirm >= timeServerSecondsSince2000()))
		{
			//If this confirmation was from a warp...
			if(pConfirm->eType == kPlayerMapMove_Warp && pConfirm->pWarp)
			{
				StructDestroySafe(parse_PlayerWarpToData, &pEnt->pPlayer->pWarp);
				pEnt->pPlayer->pWarp = StructClone(parse_PlayerWarpToData, pConfirm->pWarp);
			}

			pConfirm->bConfirmed = true;

			//If this confirmation wasn't a warp, wasn't to a specific map ID, or is to a different map, do the mapmove code
			if( !pConfirm->pWarp ||
				!pConfirm->pWarp->iMapID ||
				!pConfirm->pWarp->uPartitionID ||
				pConfirm->pWarp->iMapID != gGSLState.gameServerDescription.baseMapDescription.containerID ||
				pConfirm->pWarp->uPartitionID != partition_IDFromIdx(entGetPartitionIdx(pEnt)))
			{
				// HEY YOU!  If you're changing the parameters to this function, you need to update the related PlayerMapMoveConfirm code
				spawnpoint_MovePlayerToMapAndSpawn(	pEnt,
													pConfirm->pcMapName,
													pConfirm->pcNamedSpawnPoint,
													pConfirm->pcQueueName,
													pConfirm->eOwnerType,
													pConfirm->uOwnerID,
													pConfirm->pWarp ? pConfirm->pWarp->iMapID : 0,
													pConfirm->pWarp ? pConfirm->pWarp->uPartitionID : 0,
													pConfirm->eaVariables,
													pConfirm->pScope,
													NULL,
													pConfirm->eFlags,
													0);
			}
			else
			{
				//Else, move to a point on this map instead
				if(!pConfirm->pWarp->pchSpawn)
				{
					Quat spawnRot;
					entGetRot(pEnt, spawnRot);
					spawnpoint_MovePlayerToLocation(pEnt, pConfirm->pWarp->vecTarget, spawnRot, NULL, true );
				}
				else
				{
					spawnpoint_MovePlayerToNamedSpawn(pEnt, pConfirm->pWarp->pchSpawn, NULL, 0);
				}

				//Charge the item
				if(pConfirm->pWarp->uiItemId)
				{
					Item *pItem = inv_GetItemByID(pEnt, pConfirm->pWarp->uiItemId);
					if(pItem)
					{
						gslItem_ChargeForWarp(pEnt, pItem);
					}
				}

				if(pEnt->pPlayer->pWarp)
				{
					StructDestroySafe(parse_PlayerWarpToData, &pEnt->pPlayer->pWarp);
				}
				StructDestroySafe(parse_PlayerMapMoveConfirm,&pEnt->pPlayer->pMapMoveConfirm);
			}
		}
		else
		{
			//If the player was trying to warp when this occurred, remove the warp data
			if(pEnt->pPlayer->pWarp)
			{
				StructDestroySafe(parse_PlayerWarpToData, &pEnt->pPlayer->pWarp);
			}
			StructDestroySafe(parse_PlayerMapMoveConfirm,&pEnt->pPlayer->pMapMoveConfirm);
		}
	}
}

void MapMoveWithDescriptionAndPosRot(SA_PARAM_NN_VALID Entity *ent, SA_PARAM_NN_VALID const MapDescription *pMapDesc, Vec3 vPos, Quat qRot3, char *pReason, bool bCSR)
{
	MapSearchInfo choiceInfo = {0};

	StructCopy(parse_MapDescription, pMapDesc, &choiceInfo.baseMapDescription, 0, 0, 0);
	choiceInfo.baseMapDescription.spawnPoint = allocAddString(POSITION_SET);
	copyVec3(vPos, choiceInfo.baseMapDescription.spawnPos);
	quatToPYR(qRot3, choiceInfo.baseMapDescription.spawnPYR);
	choiceInfo.debugPosLogin = true;
	

	//if they want to go to this map, or an unspecified container ID map which matches this map, just teleport.
	if ((pMapDesc->containerID == gServerLibState.containerID && (pMapDesc->iPartitionID == 0 || pMapDesc->iPartitionID == partition_IDFromIdx(entGetPartitionIdx(ent))))
		|| (pMapDesc->containerID == 0 && IsSameMapDescription(&choiceInfo.baseMapDescription, partition_GetMapDescription(entGetPartitionIdx(ent)))))
	{
		Vec3 vPyr;
		entSetPos(ent, vPos, 1, __FUNCTION__);
		entSetRot(ent, qRot3, 1, __FUNCTION__);
		quatToPYR(qRot3, vPyr);
		vPyr[1] = addAngle(vPyr[1], PI);
		ClientCmd_setGameCamPYR(ent, vPyr);
		gslCacheEntRegion(ent, entity_GetCachedGameAccountDataExtract(ent));
	}
	else
	{
		if (pMapDesc->containerID && pMapDesc->iPartitionID)
		{
			choiceInfo.eSearchType = MAPSEARCHTYPE_SPECIFIC_CONTAINER_AND_PARTITION_ID_ONLY;
		}

		gslMapTransferRequest_Internal(ent,&choiceInfo, pReason, bCSR ? TRANSFERFLAG_CSR : 0, NULL);	
	}
}




void MapMoveWithDescription(SA_PARAM_NN_VALID Entity *ent, SA_PARAM_NN_VALID const MapDescription *pMapDesc, char *pReason, TransferFlags eFlags)
{
	MapSearchInfo choiceInfo = {0};

	if (isProductionEditMode())
	{
		gslUGC_TransferToMapWithDelay(ent, pMapDesc->mapDescription, pMapDesc->spawnPoint, pMapDesc->spawnPos, pMapDesc->spawnPYR, NULL, 0);

		// set variable overrides before we load the map so the variables get inited correctly
		eaClearStruct( &g_eaMapVariableOverrides, parse_WorldVariable );
		worldVariableStringToArray( pMapDesc->mapVariables, &g_eaMapVariableOverrides );
	}
	else
	{
		StructCopy(parse_MapDescription, pMapDesc, &choiceInfo.baseMapDescription, 0, 0, 0);
		if (pMapDesc->containerID && pMapDesc->iPartitionID)
		{
			if (eFlags & TRANSFERFLAG_SPECIFIC_MAP_ONLY)
			{
				choiceInfo.eSearchType = MAPSEARCHTYPE_SPECIFIC_CONTAINER_AND_PARTITION_ID_ONLY;
			}
			else
			{
				choiceInfo.eSearchType = MAPSEARCHTYPE_SPECIFIC_CONTAINER_AND_PARTITION_ID_OR_OTHER;
			}
		}
		else if (pMapDesc->mapInstanceIndex)
		{
			if (eFlags & TRANSFERFLAG_SPECIFIC_MAP_ONLY)
			{
				choiceInfo.eSearchType = MAPSEARCHTYPE_SPECIFIC_PUBLIC_INDEX_ONLY;
			}
			else
			{
				choiceInfo.eSearchType = MAPSEARCHTYPE_SPECIFIC_PUBLIC_INDEX_OR_OTHER;
			}
		}

		if (pMapDesc->ownerType == GLOBALTYPE_GUILD)
		{
			choiceInfo.iGuildID = pMapDesc->ownerID;
		}

		gslMapTransferRequest_Internal(ent, &choiceInfo, pReason, eFlags, NULL);	
	}
}

//Map move or move to another spawn within the same map
void MapMoveOrSpawnWithDescription(SA_PARAM_NN_VALID Entity *ent, SA_PARAM_NN_VALID MapDescription *pMapDesc, char *pReason, TransferFlags eFlags)
{
	if (pMapDesc->mapDescription && pMapDesc->mapDescription[0] && 
		!IsSameMapDescription(pMapDesc, partition_GetMapDescription(entGetPartitionIdx(ent))))
	{
		MapMoveWithDescription(ent, pMapDesc, pReason, eFlags);
	}
	else
	{
		spawnpoint_MovePlayerToNamedSpawn(ent, pMapDesc->spawnPoint, NULL, true);
	}
}

void OVERRIDE_LATELINK_SendDebugTransferMessageToClient(U32 iCookie, char *pMessage)
{
	CharacterTransfer *pTransfer = getCharacterTransferForCookie(iCookie);

	if (pTransfer && pTransfer->clientLink && pTransfer->clientLink->netLink)
	{
		Packet *pak = pktCreate(pTransfer->clientLink->netLink, TOCLIENT_GAME_TRANSFER_FAILED);	
		Packet *pPack = pktCreate(pTransfer->clientLink->netLink, TO_CLIENT_DEBUG_MESSAGE);

		pktSendString(pPack, pMessage);
		pktSend(&pPack);	
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void SetAlwaysShowMapTransfer(Entity *pEntity, bool bVal)
{
	if(pEntity->pPlayer)
	{
		pEntity->pPlayer->pUI->pLooseUI->bShowMapChoice = bVal;
		entity_SetDirtyBit(pEntity, parse_PlayerUI, pEntity->pPlayer->pUI, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
	}
}

char *GetVerboseMapMoveComment(Entity *ent, const char *pFmt, ...)
{
	static char *pRetVal = NULL;
	SavedMapDescription *pLastStatic = entity_GetLastStaticMap(ent);
	SavedMapDescription *pLastNonStatic = entity_GetLastNonStaticMap(ent);
	
	estrClear(&pRetVal);
	estrGetVarArgs(&pRetVal, pFmt);

	if (pLastStatic)
	{
		estrConcatf(&pRetVal, ". LastStatic: %s(%s).", 
			pLastStatic->mapDescription, StaticDefineIntRevLookup(ZoneMapTypeEnum, pLastStatic->eMapType));
	}
	else
	{
		estrConcatf(&pRetVal, ". No LastStatic.");
	}

	if (pLastNonStatic)
	{
		estrConcatf(&pRetVal, " LastNonStatic: %s(%s).", 
			pLastNonStatic->mapDescription, StaticDefineIntRevLookup(ZoneMapTypeEnum, pLastNonStatic->eMapType));
	}
	else
	{
		estrConcatf(&pRetVal, " No LastNonStatic.");
	}

	estrConcatf(&pRetVal, " Current GS: %s(%s)", gGSLState.gameServerDescription.baseMapDescription.mapDescription, StaticDefineIntRevLookup(ZoneMapTypeEnum, gGSLState.gameServerDescription.baseMapDescription.eMapType));

	return pRetVal;
}



#include "AutoGen/gslMapTransfer_c_ast.c"
#include "gslMapTransfer_h_ast.c"
