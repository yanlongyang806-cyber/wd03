#include "gslBugReport.h"
#include "gslMission.h"
#include "errornet.h"
#include "EntitySavedData.h"
#include "ticketnet.h"
#include "GameServerLib.h"
#include "estring.h"
#include "utilitiesLib.h"
#include "ServerLib.h"
#include "Powers.h"
#include "Character.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "file.h"
#include "../StaticWorld/ZoneMap.h"
#include "winutil.h"
#include "aiDebug.h"
#include "aiDebugShared.h"
#include "aiMovement.h"
#include "Player.h"
#include "Alerts.h"

#include "Entity_h_ast.h"
#include "mission_common.h"
#include "GameEvent.h"
#include "autogen/aiDebugShared_h_ast.h"
#include "Autogen/mission_enums_h_ast.h"
#include "Autogen/mission_common_h_ast.h"
#include "gslBugReport_h_ast.h"
#include "Autogen/trivia_h_ast.h"

extern ParseTable parse_ErrorData[];
#define TYPE_parse_ErrorData ErrorData
extern ParseTable parse_TicketData[];
#define TYPE_parse_TicketData TicketData
extern ParseTable parse_GameEvent[];
#define TYPE_parse_GameEvent GameEvent
extern ParseTable parse_TriviaData[];
#define TYPE_parse_TriviaData TriviaData
extern ParseTable parse_TriviaList[];
#define TYPE_parse_TriviaList TriviaList
static StashTable sCategoryCustomDataCallbacks;

#define MAX_REASONABLE_IMAGE_SIZE (1024 * 1024)


LATELINK;
void createCBug(Entity *ent, ErrorData *pErrorData, char *pUserDataString, U32 uImageSize, char *pImageBuffer);
LATELINK;
void createTicket(Entity *ent, TicketData *pTicketData, char *pUserDataString, U32 uImageSize, char *pImageBuffer);

void gslTicket_Response(TicketData *ticket, const char *error, U32 uTicketID, int result);

typedef struct TicketEntityData
{
	EntityRef uRef; // Container ID of the player who sent this in

	// Store copy of player entity data as well?
	//char *target; AST(ESTRING) // Copy of target entity data
	EntityRef target;

	// these will be filled in by a callback function and will be pushed into the ticket when it's sent
	void *pStruct;
	ParseTable *pti;
} TicketEntityData;

void EntityTargetClearOldRequests(EntityRef uRef);

MissionTrackingStruct * DumpMissionData(Mission *pMission, MissionDef *pDef)
{
	MissionTrackingStruct *pTracking = StructCreate(parse_MissionTrackingStruct);
	int i;

	pTracking->eState = pMission->state;
	pTracking->startTime = pMission->startTime;
	eaCopyStructs(&((MissionEventContainer **) pMission->eaEventCounts), &pTracking->eaEventCounts, parse_MissionEventContainer);
	eaCopyStructs(&pMission->eaTrackedEvents, &pTracking->trackedEvents, parse_GameEvent);
	pTracking->refKeyString = strdup(pDef->pchRefString);

	for (i=0; i<eaSize(&pMission->children); i++)
	{
		MissionDef *pChildDef = mission_GetDef(pMission->children[i]);
		if (pChildDef)
			eaPush(&pTracking->children, DumpMissionData(pMission->children[i], pChildDef));
	}

	return pTracking;
}
MissionTrackingStruct * DumpCompletedMissionData(CompletedMission *pMission, MissionDef *pDef)
{
	MissionTrackingStruct *pTracking = StructCreate(parse_MissionTrackingStruct);

	pTracking->eState = MissionState_TurnedIn;
	pTracking->startTime = 0; // we no longer track the start time
	pTracking->endTime = completedmission_GetLastCompletedTime(pMission);;
	pTracking->timesCompleted = completedmission_GetNumTimesCompleted(pMission);
	pTracking->refKeyString = strdup(pDef->pchRefString);

	return pTracking;
}

bool gslBug_MissionInfoCallback(void **ppMissionInfo, ParseTable **ppti, Entity *ent, const char *pDataString, char **estrLabel)
{
	MissionDef *pDef = RefSystem_ReferentFromString(g_MissionDictionary, pDataString);
	CompletedMission *pCompleted = NULL;
	MissionTrackingStruct *pTracking = NULL;
	if (pDef)
	{
		Mission *pMission = mission_FindMissionFromDef(mission_GetInfoFromPlayer(ent), pDef);
		if (pMission)
		{	
			pTracking = DumpMissionData(pMission, pDef);

		}
		else if (pCompleted = mission_GetCompletedMissionByDef(mission_GetInfoFromPlayer(ent), pDef))
		{
			// TODO get the proper data for completed missions
			pTracking = DumpCompletedMissionData(pCompleted, pDef);
		}
	}
	if (pTracking)
	{
		*ppMissionInfo = pTracking;
		*ppti = parse_MissionTrackingStruct;
	}
	return true;
}

bool gslBug_PowersCallback(void **ppPowersBugStruct, ParseTable **ppti, Entity *ent, const char *pDataString, char **estrLabel)
{
	PowerDef *pDef = RefSystem_ReferentFromString(g_hPowerDefDict, pDataString);
	PowersBugStruct *pStruct = NULL;
	if (pDef)
	{
		pStruct = StructCreate(parse_PowersBugStruct);
		pStruct->pchName = strdup(pDef->pchName);
	}

	if (pStruct)
	{
		*ppPowersBugStruct = pStruct;
		*ppti = parse_PowersBugStruct;
	}
	return true;
}

bool gslBug_NPCMobCallback(void **ppNPCBugStruct, ParseTable **ppti, Entity *ent, const char *pDataString, char **estrLabel)
{
	TicketEntityData *target;

	target = FindTicketEntityTargetDone(entGetRef(ent));
	if (target)
	{
		Entity *targetEnt = entFromEntityRefAnyPartition(target->target);

		if (targetEnt)
			estrCopy2(estrLabel, entGetLocalName(targetEnt));
		*ppNPCBugStruct = target->pStruct;
		*ppti = target->pti;
		target->pStruct = NULL;
	}
	else if (target = FindTicketEntityTarget(entGetRef(ent)))
	{
		// Not done yet, return false
		return false;
	}

	EntityTargetClearOldRequests(entGetRef(ent));
	return true;
}

AUTO_RUN;
void gslBug_AddCallbacks(void)
{
	gslBug_AddCustomDataCallback("CBug.Category.Mission", gslBug_MissionInfoCallback, false);
	gslBug_AddCustomDataCallback("CBug.Category.Powers", gslBug_PowersCallback, false);
	gslBug_AddCustomDataCallback(TICKETDATA_ALL_CATEGORY_STRING, gslBug_NPCMobCallback, true);
}


typedef struct UserDataCBStruct
{
	char *category;
	CategoryCustomDataFunc cb;
	bool bIsDelayed; // Marks if the callback may not be able to be completed in this frame
} UserDataCBStruct;

static UserDataCBStruct ** seaUserDataCBList = NULL;
static StashTable stashDelayedCallbacks = NULL;

bool gslBug_AddCustomDataCallback (const char *category, CategoryCustomDataFunc callback, bool bIsDelayed)
{
	UserDataCBStruct *cb = calloc(1, sizeof(UserDataCBStruct));

	cb->category = strdup(category);
	cb->cb = callback;
	cb->bIsDelayed = bIsDelayed;

	eaPush(&seaUserDataCBList, cb);
	return true;
}

bool gslBug_RemoveCustomDataCallback (const char *category, CategoryCustomDataFunc callback)
{
	int i;
	for (i=eaSize(&seaUserDataCBList)-1; i>=0; i--)
	{
		if (stricmp(seaUserDataCBList[i]->category, category) == 0 && seaUserDataCBList[i]->cb == callback)
		{
			eaRemove(&seaUserDataCBList, i);
			return true;
		}
	}
	return false;
}

bool gslBug_RunCustomDataCallbacks(TicketData *ticket, const char *category, Entity *pEnt, char *pUserDataString)
{
	int i;
	UserDataCBStruct ** ppDelayedFuncs = NULL;

	if (!pEnt)
		return false; // fails if no entity passed in
	if (!stashDelayedCallbacks)
		stashDelayedCallbacks = stashTableCreateInt(100);
	else
	{
		cEArrayHandle *eaHandle = NULL;
		stashIntRemovePointer(stashDelayedCallbacks, pEnt->myContainerID, (void**) &eaHandle);
		if (eaHandle)
			eaDestroy(eaHandle);
	}

	for (i=eaSize(&seaUserDataCBList)-1; i>=0; i--)
	{
		if (stricmp(seaUserDataCBList[i]->category, category) == 0 || 
			stricmp(seaUserDataCBList[i]->category, TICKETDATA_ALL_CATEGORY_STRING) == 0)
		{
			if (seaUserDataCBList[i]->bIsDelayed)
			{
				eaPush(&ppDelayedFuncs, seaUserDataCBList[i]);
			}
			else
			{
				void *pStruct = NULL;
				ParseTable *pti = NULL;
				seaUserDataCBList[i]->cb(&pStruct, &pti, pEnt, pUserDataString, &ticket->pTicketLabel);

				if (pStruct)
				{
					assertmsg(pti, "No Parse Table set.");
					putUserDataIntoTicket(ticket, pStruct, pti);
					StructDestroyVoid(pti, pStruct);
				}
			}
		}
	}
	if (ppDelayedFuncs)
	{
		cEArrayHandle *eaHandle = calloc(1, sizeof(cEArrayHandle));
		*eaHandle = ppDelayedFuncs;
		devassert(stashIntAddPointer(stashDelayedCallbacks, pEnt->myContainerID, (void*) eaHandle, false));
		return false;
	}
	return true;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0)  ACMD_PRIVATE;
void sendCBug(Entity *ent, ErrorData *errorData, char *pUserDataString)
{
	createCBug(ent, errorData, pUserDataString, 0, NULL);
}

void DEFAULT_LATELINK_createCBug(Entity *ent, ErrorData *pErrorData, char *pUserDataString, U32 uImageSize, char *pImageBuffer)
{
	if (ent->pPlayer && ent->pPlayer->clientLink && ent->pPlayer->clientLink->netLink)
		pErrorData->uIP = linkGetSAddr(ent->pPlayer->clientLink->netLink);

	ParseTableWriteText(&pErrorData->pEntityPTIStr, parse_Entity, "Entity", 0);
	ParserWriteText(&pErrorData->pEntityStr, parse_Entity, ent, 0, 0, 0);

	if (strlen(pErrorData->pEntityStr) > TICKET_MAX_ENTITY_LEN || strlen(pErrorData->pEntityPTIStr) > TICKET_MAX_ENTITY_LEN)
	{
		estrDestroy(&pErrorData->pEntityStr);
		estrDestroy(&pErrorData->pEntityPTIStr);
	}

	// send error TODO
	ClientCmd_ClientShowTrackerResponse(ent, errorTrackerSendError(pErrorData), NULL, 0);

	estrDestroy(&pErrorData->pEntityPTIStr);
	estrDestroy(&pErrorData->pEntityStr);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0)  ACMD_PRIVATE;
void sendTicket(Entity *ent, TicketData *pTicketData, char *pUserDataString)
{
	TicketData *ticketCopy = StructClone(parse_TicketData, pTicketData);
	createTicket(ent, ticketCopy, pUserDataString, 0, NULL);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0)  ACMD_PRIVATE;
void sendTicketEdit(Entity *ent, TicketData *pTicketData)
{
	TicketData *ticketCopy = StructClone(parse_TicketData, pTicketData);
	bool bLinkCreated;

	if (ticketCopy)
	{
		if (ent->pPlayer && ent->pPlayer->clientLink && ent->pPlayer->clientLink->netLink)
			ticketCopy->uIP = linkGetSAddr(ent->pPlayer->clientLink->netLink);
		ticketCopy->entRef = entGetRef(ent);

		bLinkCreated = ticketTrackerSendTicketEditAsync(ticketCopy, gslTicket_Response);
		if (!bLinkCreated)
		{
			gslTicket_Response(ticketCopy, "CTicket.Failure", 0, TICKETFLAGS_CONNECTION_ERROR);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0)  ACMD_PRIVATE;
void sendTicketClose(Entity *ent, TicketData *pTicketData)
{
	TicketData *ticketCopy = StructClone(parse_TicketData, pTicketData);
	bool bLinkCreated;

	if (ticketCopy)
	{
		if (ent->pPlayer && ent->pPlayer->clientLink && ent->pPlayer->clientLink->netLink)
			ticketCopy->uIP = linkGetSAddr(ent->pPlayer->clientLink->netLink);
		ticketCopy->entRef = entGetRef(ent);

		bLinkCreated = ticketTrackerSendTicketCloseAsync(ticketCopy, gslTicket_Response);
		if (!bLinkCreated)
		{
			gslTicket_Response(ticketCopy, "CTicket.Failure", 0, TICKETFLAGS_CONNECTION_ERROR);
		}
	}
}

void gslTicket_Response(TicketData *ticket, const char *msg, U32 uTicketID, int result)
{
	Entity *ent = entFromEntityRefAnyPartition(ticket->entRef);

	if (result == TICKETFLAGS_CONNECTION_ERROR && msg && *msg)
	{
		TriggerAlertf("GS_TICKET_SEND_FAIL", ALERTLEVEL_WARNING, ALERTCATEGORY_NETOPS, 0, 0, 0, 0, 0, NULL, 0, 
			"Ticket Creation failed with error: %s", msg);
	}

	// Destroy ticket data after sending response
	ClientCmd_ClientShowTrackerResponse(ent, result, result == TICKETFLAGS_CONNECTION_ERROR ? "CTicket.Failure" : msg, uTicketID);

	ticket->pShardInfoString = NULL;

	if (ticket->imageBuffer)
		free(ticket->imageBuffer);
	if  (ticket->pServerDataString)
		free(ticket->pServerDataString);
	StructDestroy(parse_TicketData, ticket);
}

static void gsl_sendTicket(TicketData *pTicketData)
{
	bool bLinkCreated = false;
	// send ticket
	if (pTicketData->uImageSize)
	{
		bLinkCreated = ticketTrackerSendTicketPlusScreenshotAsync(pTicketData, pTicketData->imageBuffer, gslTicket_Response);
	}
	else
	{
		bLinkCreated = ticketTrackerSendTicketAsync(pTicketData, gslTicket_Response);
	}

	if (!bLinkCreated)
	{
		gslTicket_Response(pTicketData, "CTicket.Failure", 0, TICKETFLAGS_CONNECTION_ERROR);
	}
}

void gslTicket_DelayedSend(TimedCallback *callback, F32 timeSinceLastCallback, TicketData *ticket)
{
	int i, size;
	cEArrayHandle *eaDelayedList = NULL;
	Entity *ent = entFromEntityRefAnyPartition(ticket->entRef);

	if (!ent)
		return;
	stashIntFindPointer(stashDelayedCallbacks, ent->myContainerID, (void**) &eaDelayedList);

	if (eaDelayedList)
	{
		size = eaSize(eaDelayedList);

		for (i=size-1; i>=0; i--)
		{
			void *pStruct = NULL;
			ParseTable *pti = NULL;
			bool bCompleted = false;

			bCompleted = ((UserDataCBStruct**) (*eaDelayedList))[i]->cb(&pStruct, &pti, ent, ticket->pServerDataString, &ticket->pTicketLabel);

			if (bCompleted)
			{
				if (pStruct)
				{
					assertmsg(pti, "No Parse Table set.");
					putUserDataIntoTicket(ticket, pStruct, pti);
					StructDestroyVoid(pti, pStruct);
				}
				eaRemoveFast(eaDelayedList, i);
				size--;
			}
		}

		if (size > 0)
		{
			TimedCallback_Run(gslTicket_DelayedSend, ticket, 0.5f);
		}
		else
		{
			stashIntRemovePointer(stashDelayedCallbacks, ent->myContainerID, (void**) &eaDelayedList);
			eaDestroy(eaDelayedList);
			gsl_sendTicket(ticket);
		}
	}
	else
		gsl_sendTicket(ticket);
}

void DEFAULT_LATELINK_createTicket(Entity *ent, TicketData *pTicketData, char *pUserDataString, U32 uImageSize, char *pImageBuffer)
{
	if (ent->pPlayer && ent->pPlayer->clientLink && ent->pPlayer->clientLink->netLink)
		pTicketData->uIP = linkGetSAddr(ent->pPlayer->clientLink->netLink);

	ParseTableWriteText(&pTicketData->pEntityPTIStr, parse_Entity, "Entity", 0);
	ParserWriteText(&pTicketData->pEntityStr, parse_Entity, ent, 0, 0, 0);

	if (pTicketData->pShardInfoString)
		free(pTicketData->pShardInfoString);

	pTicketData->pShardInfoString = GetShardInfoString();
	pTicketData->iServerContainerID = gServerLibState.containerID;
	pTicketData->uImageSize = uImageSize;
	pTicketData->imageBuffer = pImageBuffer;
	pTicketData->entRef = entGetRef(ent);
	pTicketData->pServerDataString = pUserDataString ? strdup(pUserDataString) : NULL;

	if (zmapInfoGetUGCProjectID(gGSLState.gameServerDescription.baseMapDescription.pZoneMapInfo))
	{
		NOCONST(TriviaData) *pUGCData = StructCreateNoConst(parse_TriviaData);
		estrCopy2(&pUGCData->pKey, "ugcProjectID");
		estrPrintf(&pUGCData->pVal, "%d", zmapInfoGetUGCProjectID(gGSLState.gameServerDescription.baseMapDescription.pZoneMapInfo));
		eaPush((TriviaData***) &pTicketData->pTriviaList->triviaDatas, (TriviaData*) pUGCData);
	}
	if (gslBug_RunCustomDataCallbacks(pTicketData, pTicketData->pCategory, ent, pUserDataString))
		gsl_sendTicket(pTicketData);
	else
		gslTicket_DelayedSend(NULL, 0, pTicketData);
}

void gslHandleBugOrTicket(Packet *pak, Entity *ent)
{
	U32 uType = pktGetU32(pak);
	char *pBTData = pktGetStringTemp(pak);
	char *pUserData = pktGetStringTemp(pak);
	U32 uImageSize = pktGetU32(pak);
	char *pImageBuffer;

	if (pktIsNotTrustworthy(pak))
	{
		if (uImageSize > MAX_REASONABLE_IMAGE_SIZE)
		{
			//we are getting corrupt data, forget about bothering to try to read the rest of the packet
			pktSetErrorOccurred(pak, "Corrupt data received.");
			return;
		}
	}

	pImageBuffer = malloc(uImageSize);

	pktGetBytes(pak, uImageSize, pImageBuffer);

	if (uType) // it's a ticket
	{
		TicketData *pTicketData = StructCreate(parse_TicketData);
		ParserReadText(pBTData, parse_TicketData, pTicketData, 0);

		// Create ticket is responsible for destroying the ticket data struct
		createTicket(ent, pTicketData, pUserData, uImageSize, pImageBuffer);
	}
	else // it's a bug
	{
		ErrorData *pErrorData = StructCreate(parse_ErrorData);
		ParserReadText(pBTData, parse_ErrorData, pErrorData, 0);

		createCBug(ent, pErrorData, pUserData, uImageSize, pImageBuffer);

		StructDestroy(parse_ErrorData, pErrorData);
		free(pImageBuffer);
	}
}

/////////////////////////////////////////
// Entity Target information for Tickets
/////////////////////////////////////////
static CRITICAL_SECTION sTicketEntityAccess;
static CRITICAL_SECTION sTicketEntityDoneAccess;
static bool sbTicketEntityInitialized = false;
static TicketEntityData ** sppTicketEntities = NULL;
static TicketEntityData ** sppTicketEntitiesDone = NULL;

#define TIME_BETWEEN_PARSING 0.5f // in seconds

AUTO_RUN;
void gslTicket_InitializeCriticalSections(void)
{
	if (!sbTicketEntityInitialized)
	{
		InitializeCriticalSection(&sTicketEntityAccess);
		InitializeCriticalSection(&sTicketEntityDoneAccess);
		sbTicketEntityInitialized = true;
	}
}

// Returns if the processing finished or not.
bool gslTicket_EntityProcessing(void **ppStruct, ParseTable** ppti, EntityRef playerRef, EntityRef targetRef)
{
	// TODO stuff with sppTicketEntities[i] here
	// Should set ppStruct with the pointer to the struct data and ppti to the struct's parse table
	Entity *player = entFromEntityRefAnyPartition(playerRef);
	Entity *target = entFromEntityRefAnyPartition(targetRef);
	AIBugTargetStruct *bts = (AIBugTargetStruct*)*ppStruct;

	// Ent died or was destroyed while trying to get info
	if(!target || !player)
	{
		if(bts)
			StructDestroy(parse_AIBugTargetStruct, bts);
		*ppStruct = NULL;
		*ppti = NULL;

		return 1;
	}
	
	if(!bts)
	{
		bts = *ppStruct = StructCreate(parse_AIBugTargetStruct);
		*ppti = parse_AIBugTargetStruct;

		bts->debugInfo = StructCreate(parse_AIDebug);
		bts->debugInfo->settings.updateSelected = 0;
		bts->debugInfo->settings.flags = AI_DEBUG_FLAGS_ALL;

		bts->requestTime = ABS_TIME;

		aiMovementSetDebugFlag(target, true);

		aiDebugFillStructEntity(player, target, target->aibase, bts->debugInfo, 1);
	}

	if(bts->requestTime < target->aibase->timeDebugCurPathUpdated)
	{
		bts->debugInfo->settings.flags = AI_DEBUG_FLAG_MOVEMENT;
		aiDebugFillStructEntity(player, target, target->aibase, bts->debugInfo, 0);

		aiMovementSetDebugFlag(target, false);

		return 1;
	}

	return 0;
}

void EntityTargetParsing(TimedCallback *callback, F32 timeSinceLastCallback, void *userData)
{
	int i, size;

	EnterCriticalSection(&sTicketEntityAccess);
	size = eaSize(&sppTicketEntities);
	for (i=size-1; i>=0; i--)
	{
		bool bCompleted = gslTicket_EntityProcessing(&sppTicketEntities[i]->pStruct, &sppTicketEntities[i]->pti, 
			sppTicketEntities[i]->uRef, sppTicketEntities[i]->target);

		if (bCompleted)
		{
			size--;

			EnterCriticalSection(&sTicketEntityDoneAccess);
			eaPush(&sppTicketEntitiesDone, sppTicketEntities[i]);
			LeaveCriticalSection(&sTicketEntityDoneAccess);

			eaRemove(&sppTicketEntities, i);
		}
	}
	LeaveCriticalSection(&sTicketEntityAccess);

	if (size > 0)
		TimedCallback_Run(EntityTargetParsing, NULL, TIME_BETWEEN_PARSING);
}

TicketEntityData * FindTicketEntityTarget(EntityRef uRef)
{
	int i;
	int size;

	EnterCriticalSection(&sTicketEntityAccess);
	size = eaSize(&sppTicketEntities);
	for (i=0; i<size; i++)
	{
		if (uRef == sppTicketEntities[i]->uRef)
		{
			LeaveCriticalSection(&sTicketEntityAccess);
			return sppTicketEntities[i];
		}
	}
	LeaveCriticalSection(&sTicketEntityAccess);
	return NULL;
}

TicketEntityData * FindTicketEntityTargetDone(EntityRef uRef)
{
	int i;
	int size;

	EnterCriticalSection(&sTicketEntityDoneAccess);
	size = eaSize(&sppTicketEntitiesDone);
	for (i=0; i<size; i++)
	{
		if (uRef == sppTicketEntitiesDone[i]->uRef)
		{
			LeaveCriticalSection(&sTicketEntityDoneAccess);
			return sppTicketEntitiesDone[i];
		}
	}
	LeaveCriticalSection(&sTicketEntityDoneAccess);
	return NULL;
}

U32 gslTicket_EntityTarget(U32 entRef)
{
	TicketEntityData * target = FindTicketEntityTarget(entRef);

	if (target)
	{
		return target->target;
	}
	target = FindTicketEntityTargetDone(entRef);
	if (target)
	{
		return target->target;
	}
	return 0;
}

void EntityTargetClearOldRequests(EntityRef uRef)
{
	int i;
	int size;

	EnterCriticalSection(&sTicketEntityAccess);
	size = eaSize(&sppTicketEntities);
	for (i=0; i<size; i++)
	{
		if (uRef == sppTicketEntities[i]->uRef)
		{
			free(sppTicketEntities[i]);
			eaRemove(&sppTicketEntities, i);
			break;
		}
	}
	LeaveCriticalSection(&sTicketEntityAccess);

	EnterCriticalSection(&sTicketEntityDoneAccess);
	size = eaSize(&sppTicketEntitiesDone);
	for (i=0; i<size; i++)
	{
		if (uRef == sppTicketEntitiesDone[i]->uRef)
		{
			if (sppTicketEntitiesDone[i]->pStruct)
				StructDestroyVoid(sppTicketEntitiesDone[i]->pti, sppTicketEntitiesDone[i]->pStruct);
			free(sppTicketEntitiesDone[i]);
			eaRemove(&sppTicketEntitiesDone, i);
			break;
		}
	}
	LeaveCriticalSection(&sTicketEntityDoneAccess);
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gslGrabEntityTarget (Entity *ent)
{
	TicketEntityData * data;
	int size;
	Entity *targetEnt = NULL;

	EntityTargetClearOldRequests(entGetRef(ent));
	data = calloc(1, sizeof(TicketEntityData));
	data->uRef = entGetRef(ent);
	data->target = ent->pChar->currentTargetRef; // the hard target, is not always set for interactable nodes
	//ent->pChar->currentTargetKey
	if (data->target)
		targetEnt = entFromEntityRefAnyPartition(data->target);

	EnterCriticalSection(&sTicketEntityAccess);
	size = eaSize(&sppTicketEntities);
	eaPush(&sppTicketEntities, data);
	LeaveCriticalSection(&sTicketEntityAccess);
	if (size == 0) // this is the size before the current one was added
	{
		EntityTargetParsing(NULL, 0, NULL);
	}
	ClientCmd_setTicketTargetName(ent, ent->pChar->currentTargetRef, 
		targetEnt ? entGetLocalName(targetEnt) : "");
}

void SubmitTicketFromServerCode(Entity *pEnt, char *pMainCategory, char *pCategory, char *pSummary, char *pDescription, char *pTicketLabel)
{
	TicketData *pTicketData = StructCreate(parse_TicketData);

	pTicketData->pTriviaList = StructCreate(parse_TriviaList);
	if (pEnt)
	{
		Vec3 Pos;
		NOCONST(TriviaData) *pTriviaData = StructCreateNoConst(parse_TriviaData);

		entGetPos(pEnt, Pos);
		estrPrintf( &pTriviaData->pKey, "playerPos" );
		estrPrintf( &pTriviaData->pVal, "SetDebugPos \"%s\" %f %f %f 0 0 0", zmapInfoGetPublicName(NULL), vecParamsXYZ(Pos) );
		eaPush((TriviaData***) &pTicketData->pTriviaList->triviaDatas, (TriviaData*)pTriviaData);
	}
	else
	{
		NOCONST(TriviaData) *pTriviaData = StructCreateNoConst(parse_TriviaData);

		estrPrintf( &pTriviaData->pKey, "NoEntity" );
		estrPrintf( &pTriviaData->pVal, "no valid entity" );
		eaPush((TriviaData***) &pTicketData->pTriviaList->triviaDatas, (TriviaData*)pTriviaData);
	}

	pTicketData->pPlatformName = strdup(PLATFORM_NAME);
	pTicketData->pProductName = strdup(GetProductName());
	pTicketData->pVersionString = strdup(GetUsefulVersionString());

	if (pEnt && pEnt->pPlayer)
	{
		pTicketData->pAccountName = strdup(pEnt->pPlayer->privateAccountName);
		pTicketData->pCharacterName = strdup(pEnt->pSaved->savedName);
	}
	else
		pTicketData->pAccountName = strdup(getUserName());

	estrCopy2(&pTicketData->pMainCategory, pMainCategory ? pMainCategory : "CBug.CategoryMain.GM");
	estrCopy2(&pTicketData->pCategory, pCategory ? pCategory : "CBug.Category.GM.Stuck");
	pTicketData->pSummary = pSummary ? strdup(pSummary) : "None";
	pTicketData->pUserDescription = pDescription ? strdup(pDescription) : "None";
	if (pTicketData->pShardInfoString)
		free(pTicketData->pShardInfoString);
	pTicketData->pShardInfoString = strdup(GetShardInfoString());
	estrCopy2(&pTicketData->pTicketLabel, pTicketLabel ? pTicketLabel : "stuck_killme_cmd");	 

	pTicketData->iProductionMode = isProductionMode();
	pTicketData->iMergeID = 0;
	pTicketData->imagePath = NULL;
	pTicketData->eVisibility = TICKETVISIBLE_HIDDEN;

	pTicketData->uIsInternal = true;

	if (pEnt)
	{
		ParserWriteText(&pTicketData->pEntityStr, parse_Entity, pEnt, 0, 0, 0);
		ParseTableWriteText(&pTicketData->pEntityPTIStr, parse_Entity, ParserGetTableName(parse_Entity), 0);
	}

	ticketTrackerSendTicketAsync(pTicketData, NULL);
}


#include "gslBugReport_h_ast.c"