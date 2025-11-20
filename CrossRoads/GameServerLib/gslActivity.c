/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslActivity.h"

#include "contact_common.h"
#include "earray.h"
#include "StringCache.h"
#include "timing.h"
#include "ActivityCalendar.h"
#include "ActivityCommon.h"
#include "gslMission.h"
#include "DoorTransitionCommon.h"
#include "Entity.h"
#include "Player.h"
#include "GlobalTypeEnum.h"
#include "gslContact.h"
#include "gslItemAssignments.h"
#include "gslQueue.h"
#include "gslSpawnPoint.h"
#include "gslSendToClient.h"
#include "worldgrid.h"
#include "GameStringFormat.h"
#include "NotifyEnum.h"
#include "qsortG.h"

#include "AutoGen/ActivityCommon_h_ast.h"
#include "AutoGen/gslActivity_h_ast.h"
#include "AutoGen/ActivityCalendar_h_ast.h"
#include "AutoGen/ServerLib_autogen_RemoteFuncs.h"
#include "autogen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#define EVENT_PERIODIC_REQUEST_TIME 10
#define EVENT_MAX_PENDING_REQUESTS 10

ActiveActivites g_Activities = {0};
EventDefs g_ActiveEvents = {0};
NameList *g_ActivityNameList = NULL;

// The global list of event subscriptions for players
PlayerEventSubscriptionList ** g_PlayerEventSubscriptionLists;

static S32 gsiEventClockDelta_Server=0;

U32 gslActivity_GetEventClockSecondsSince2000(void)
{
	return timeSecondsSince2000() + gsiEventClockDelta_Server;
}

int gslActivity_FindIndex(const char *pchActivity)
{
	return eaIndexedFindUsingString(&g_Activities.ppActivities,pchActivity);
}

ActiveActivity *gslActivity_Find(const char *pchActivity)
{
	int iIndex = gslActivity_FindIndex(pchActivity);

	if(iIndex != -1)
		return g_Activities.ppActivities[iIndex];

	return NULL;
}

static void gslActivity_Start(ActivityDef *pDef, U32 uStartTime, U32 uEndTime)
{
	ActiveActivity *pActivity = gslActivity_Find(pDef->pchActivityName);

	if(!pActivity)
	{
		pActivity = StructCreate(parse_ActiveActivity);

		//SET_HANDLE_FROM_REFERENT(g_hActivityDict, pDef, pActivity->hDef);
		pActivity->pActivityDef = StructClone(parse_ActivityDef, pDef);
		pActivity->pchActivityName = allocAddString(pDef->pchActivityName);
		pActivity->uTimeStart = uStartTime;
		pActivity->uTimeEnd = uEndTime;

		gslItemAssignments_NotifyActivityStarted(pActivity->pchActivityName);

		eaIndexedAdd(&g_Activities.ppActivities,pActivity);
	}
	else
	{
		//Change start time
		pActivity->uTimeStart = uStartTime;
		pActivity->uTimeEnd = uEndTime;
	}
}

void gslActivity_End(const char *pchActivity)
{
	int iIndex = gslActivity_FindIndex(pchActivity);
	ActiveActivity *pActivity = iIndex > -1 ? g_Activities.ppActivities[iIndex] : NULL;

	if(pActivity)
	{
		ActivityDef* pDef = ActivityDef_Find(pActivity->pchActivityName);
		if (pDef)
		{
			gslMission_NotifyActivityEnded(pDef);
		}

		eaFindAndRemove(&g_Activities.ppActivities,pActivity);

		StructDestroy(parse_ActiveActivity,pActivity);
	}
}

// Indicates whether the event is active at the moment
bool gslEvent_IsActive(EventDef *pDef)
{
	if(pDef && eaIndexedFindUsingString(&g_ActiveEvents.ppDefs, pDef->pchEventName) != -1)
		return true;

	return false;
}

void gslEvent_Start(const char *pchEvent)
{
	EventDef *pDef = EventDef_Find(pchEvent);

	if(pDef)
	{
		EventDefRef *pRef = StructCreate(parse_EventDefRef);
		SET_HANDLE_FROM_STRING(g_hEventDictionary, pDef->pchEventName, pRef->hEvent);

		eaIndexedAdd(&g_ActiveEvents.ppDefs, pRef);
	}
}

void gslEvent_End(const char *pchEvent)
{
	EventDef *pDef = EventDef_Find(pchEvent);

	if(pDef)
	{
		S32 iIndex = eaIndexedFindUsingString(&g_ActiveEvents.ppDefs, pDef->pchEventName);
		if (iIndex >= 0)
		{
			eaRemove(&g_ActiveEvents.ppDefs, iIndex);
		}
	}
}

void gslEvent_SetRunMode(const char *pchEventName, int iRunMode)
{
	EventDef *pDef = EventDef_Find(pchEventName);

	if(pDef)
		pDef->iEventRunMode=iRunMode;
}


bool gslActivity_IsActive(const char *pchActivity)
{
	return gslActivity_FindIndex(pchActivity) > -1 ? true : false;
}

U32 gslActivity_TimeActive(const char *pchActivity)
{
	ActiveActivity *pActivity = gslActivity_Find(pchActivity);

	if(pActivity)
	{
		return gslActivity_GetEventClockSecondsSince2000() - pActivity->uTimeStart;
	}

	return 0;
}

// This is in EventClock time
U32 gslActivity_EventClockEndingTime(const char *pchActivity)
{
	ActiveActivity *pActivity = gslActivity_Find(pchActivity);

	if(pActivity)
	{
		return pActivity->uTimeEnd;
	}

	return 0;
}


AUTO_COMMAND;
void StartActivity_Local(const char *pchActivity)
{
	ActiveActivity *pActivity = gslActivity_Find(pchActivity);

	if(pActivity)
		return; //Activity already started
}

AUTO_COMMAND;
void StopActivity_Local(const char *pchActivity)
{
	gslActivity_End(pchActivity);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void Activity_Start(ActivityDef *pDef, U32 uStartTime, U32 uEndTime)
{
	// EndTime will be 0xffffffff if we will never end
	
	printf("\tRequest to start activity %s\n",pDef->pchActivityName);
	gslActivity_Start(pDef, uStartTime, uEndTime);
}

AUTO_COMMAND_REMOTE;
void Activity_End(const char *pchActivity)
{
	printf("\tRequest to stop activity %s\n",pchActivity);
	gslActivity_End(pchActivity);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void Event_Start(const char *pchEventName)
{
	printf("Request to start event %s\n",pchEventName);
	gslEvent_Start(pchEventName);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void Event_End(const char *pchEventName)
{
	printf("Request to end event %s\n",pchEventName);
	gslEvent_End(pchEventName);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void Event_SetRunMode(const char *pchEventName, int iRunMode)
{
	printf("Request to set event run mode %s %d\n",pchEventName, iRunMode);
	gslEvent_SetRunMode(pchEventName,iRunMode);
}

/////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void StartActivity(ACMD_NAMELIST(g_ActivityNameList) const char *pchActivity)
{
	
	RemoteCommand_MapManager_StartActivity(GLOBALTYPE_MAPMANAGER,0, pchActivity);
	//Send request to map manager to start a new activity
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void StopActivity(ACMD_NAMELIST(g_ActivityNameList) const char *pchActivity)
{
	
	RemoteCommand_MapManager_StopActivity(GLOBALTYPE_MAPMANAGER,0, pchActivity);
	//Send request to map manager to stop an activity
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void StartCalendarEvent(const char *pchEvent)
{
	RemoteCommand_MapManager_StartEvent(GLOBALTYPE_MAPMANAGER,0, pchEvent);
	//Send request to map manager to start calendar event
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void StopCalendarEvent(const char *pchEvent)
{
	RemoteCommand_MapManager_StopEvent(GLOBALTYPE_MAPMANAGER,0, pchEvent);
	//Send request to map manager to stop a calendar event
}

AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void AutoCalendarEvent(const char *pchEvent)
{
	RemoteCommand_MapManager_AutoEvent(GLOBALTYPE_MAPMANAGER,0, pchEvent);
	//Send request to map manager to let calendar event run off its schedule
}

//////////////////////////////////////////////////////////////////////
// Event Clock commands

// Map Manager will call this when it is distributing a new delta or when a server first comes online
AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void gslActivity_SetEventClockDelta(S32 iNewDelta)
{
	gsiEventClockDelta_Server = iNewDelta;
}


// Clients will call this no more than once every 30 seconds to request the latest delta
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE; 
void gslActivity_EventClock_Synchronize(Entity *pEntity)
{
	ClientCmd_gclActivity_SetEventClockDelta(pEntity, gsiEventClockDelta_Server);
}

// Generic call back for the event clock console commands
void cmdClockSet_Return_CB(TransactionReturnVal *returnVal, EntityRef *pRef)
{
	enumTransactionOutcome eOutcome;
	char *estrBuffer = NULL;
	estrStackCreate(&estrBuffer);
	
	eOutcome = RemoteCommandCheck_MapManager_SetEventClockTimeString(returnVal, &estrBuffer);
	if (eOutcome == TRANSACTION_OUTCOME_SUCCESS && estrBuffer && estrBuffer[0])
	{
		Entity *pEntity = entFromEntityRefAnyPartition(*pRef);
		if (pEntity)
		{
			gslSendPrintf(pEntity, "%s",estrBuffer);	
		}
	}
	estrDestroy(&estrBuffer);
	free(pRef);
}

// Set the event clock (used by the event system) to the given time. Format is ZZZYYYY-MM-DD HH:MM:SS where ZZZ is the time format: either UTC or PAC
AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void EventClockSet(Entity *pCaller, const char *clockTime)
{
	EntityRef *pRef = malloc(sizeof(EntityRef));
	*pRef = entGetRef(pCaller);	
	
	RemoteCommand_MapManager_SetEventClockTimeString(objCreateManagedReturnVal(cmdClockSet_Return_CB, pRef), GLOBALTYPE_MAPMANAGER,0, clockTime);
}

// Restore the event clock to normal current time
AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void EventClockUnset(Entity *pCaller)
{
	EntityRef *pRef = malloc(sizeof(EntityRef));
	*pRef = entGetRef(pCaller);	
	
	RemoteCommand_MapManager_UnsetEventClockTime(objCreateManagedReturnVal(cmdClockSet_Return_CB, pRef), GLOBALTYPE_MAPMANAGER,0);
}

// Print out the current event clock time
AUTO_COMMAND ACMD_ACCESSLEVEL(7);
void EventClockGet(Entity *pCaller)
{
	EntityRef *pRef = malloc(sizeof(EntityRef));
	*pRef = entGetRef(pCaller);	
	
	RemoteCommand_MapManager_GetEventClockTime(objCreateManagedReturnVal(cmdClockSet_Return_CB, pRef), GLOBALTYPE_MAPMANAGER,0);
}


//////////////////////////////////////////////////////////////////////
// Auto-run

static ActivityDef** gslActivity_GetDefs(void)
{
	return g_ActivityDefs.ppDefs;
}

AUTO_RUN;
void gslActivity_AutoRun(void)
{
	eaIndexedEnable(&g_Activities.ppActivities,parse_ActiveActivity);
	eaIndexedEnable(&g_ActiveEvents.ppDefs, parse_EventDefRef);
	eaIndexedEnable(&g_PlayerEventSubscriptionLists, parse_PlayerEventSubscriptionList);

	g_ActivityNameList = CreateNameList_StructArray(parse_ActivityDef, ".ActivityName", gslActivity_GetDefs);
}

AUTO_EXPR_FUNC(mission, gameutil, encounter_action, reward, ItemAssignments) ACMD_NAME(Activity_IsActive);
bool AcitivityExpr_IsActive(const char *pchActivityName)
{
	return gslActivity_IsActive(pchActivityName);
}

AUTO_EXPR_FUNC(mission, gameutil, encounter_action, reward, ItemAssignments) ACMD_NAME(Activity_TimeRunning);
U32 ActivityExpr_TimeRunning(const char *pchActivityName)
{
	return gslActivity_TimeActive(pchActivityName);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(SpawnAtEvent) ACMD_HIDE;
void gslActivity_SpawnAtEvent(Entity *pEntity, const char *pchEventName, bool bIncludeTeammates)
{
	EventDef *pDef = EventDef_Find(pchEventName);
	EventWarpDef *pWarpDef = EventDef_GetWarpForAllegiance(pDef, SAFE_GET_REF(pEntity, hAllegiance));

	if(pDef && gslEvent_IsActive(pDef) && pWarpDef && pEntity && Event_CanPlayerUseWarp(pWarpDef, pEntity, true) && !gslQueue_IsQueueMap())
	{
		DoorTransitionSequenceDef* pTransOverride = GET_REF(pWarpDef->hTransOverride);

		spawnpoint_MovePlayerToMapAndSpawn(pEntity,pWarpDef->pchSpawnMap,pWarpDef->pchSpawnPoint,NULL,0,0,0,0,NULL,NULL,pTransOverride,0,bIncludeTeammates);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(EventInteractWithContact) ACMD_HIDE;
void gslActivity_InteractWithContactForEvent(Entity *pEntity, const char *pchEventName)
{
	EventDef *pDef = EventDef_Find(pchEventName);
	EventContactDef *pEventContactDef = EventDef_GetContactForAllegiance(pDef, SAFE_GET_REF(pEntity, hAllegiance));
	
	if (pDef && gslEvent_IsActive(pDef) && pEventContactDef)
	{
		ContactDef* pContactDef = contact_DefFromName(pEventContactDef->pchContactDef);
		if (pContactDef)
		{
			contact_InteractBegin(pEntity, NULL, pContactDef, pEventContactDef->pchDialogName, NULL);
		}
	}
}

static void gslActivity_FillCalendarEvent(CalendarEvent* pCalendarEvent, EventDef* pEventDef)
{
	pCalendarEvent->pchEventName = pEventDef->pchEventName;
	pCalendarEvent->pchQueue = allocAddString(REF_STRING_FROM_HANDLE(pEventDef->hLinkedQueue));
	ea32Copy(&pCalendarEvent->uDisplayTags,&pEventDef->uDisplayTags);
	StructCopyAll(parse_DisplayMessage,&pEventDef->msgDisplayName,&pCalendarEvent->msgDisplayName);
	StructCopyAll(parse_DisplayMessage,&pEventDef->msgDisplayShortDesc,&pCalendarEvent->msgDisplayDescShort);
	StructCopyAll(parse_DisplayMessage,&pEventDef->msgDisplayLongDesc,&pCalendarEvent->msgDisplayDescLong);
	pCalendarEvent->pchIcon = pEventDef->pchIcon;
	pCalendarEvent->pchBackground = pEventDef->pchBackground;
	pCalendarEvent->pchParent = pEventDef->pchParentEvent;
}

static bool gslActivity_UpdateCalendarEvent(CalendarEvent* pCalendarEvent, EventDef* pEventDef, EventWarpDef* pWarpDef, EventContactDef* pEventContact, Entity* pEntity)
{
	bool bEventActiveOnServer = gslEvent_IsActive(pEventDef);
	bool bEventMapMove = pWarpDef && Event_CanPlayerUseWarp(pWarpDef, pEntity, false);
	bool bEventContact = !!pEventContact;
	bool bEventAffectsCurrentMap = EventDef_MapCheck(pEventDef, zmapGetName(NULL));

	if (pCalendarEvent->bEventActiveOnServer != (U32)bEventActiveOnServer ||
		pCalendarEvent->bEventMapMove != (U32)bEventMapMove ||
		pCalendarEvent->bEventContact != (U32)bEventContact ||
		pCalendarEvent->bEventAffectsCurrentMap != (U32)bEventAffectsCurrentMap)
	{
		pCalendarEvent->bEventActiveOnServer = bEventActiveOnServer;
		pCalendarEvent->bEventMapMove = bEventMapMove;
		pCalendarEvent->bEventContact = bEventContact;
		pCalendarEvent->bEventAffectsCurrentMap = bEventAffectsCurrentMap;
		return true;
	}
	return false;
}

static CalendarTiming* gslActivity_GetOrCreateCalendarTimingEntry(CalendarEvent* pCalendarEvent, U32 uStartTime, U32 uEndTime)
{
	CalendarTiming *pCalendarTiming = NULL;
	S32 iCalendarTiming;
	for (iCalendarTiming = eaSize(&pCalendarEvent->eaTiming)-1; iCalendarTiming >= 0; iCalendarTiming--)
	{
		pCalendarTiming = pCalendarEvent->eaTiming[iCalendarTiming];
		if (pCalendarTiming->uStartDate == uStartTime)
			break;
	}
	if (iCalendarTiming < 0)
	{
		pCalendarTiming = StructCreate(parse_CalendarTiming);
		pCalendarTiming->uStartDate = uStartTime;
		eaPush(&pCalendarEvent->eaTiming, pCalendarTiming);
	}
	ANALYSIS_ASSUME(pCalendarTiming);
	pCalendarTiming->uEndDate = uEndTime;
	pCalendarTiming->bDirty = true;
	return pCalendarTiming;
}


static bool gslActivity_ShouldHideEvent(EventDef* pEventDef)
{
	if (pEventDef->bHideEventFromClient)
	{
		return true;
	}
	if (pEventDef->bHideFromExcludedMaps && !EventDef_MapCheck(pEventDef, zmapGetName(NULL)))
	{
		return true;
	}
	return false;
}

static void gslActivity_AddCalendarEventsForTimeRange(Entity* pEntity, 
													  EventDef* pEventDef, 
													  EventWarpDef* pWarpDef,
													  EventContactDef* pEventContact,
													  PendingCalendarRequest* pRequest,
													  CalendarEvent*** peaCalendarEvents)
{
	int iTimerEntry;
	bool bUpdatedEvent = false;

	PERFINFO_AUTO_START_FUNC();
	
	if (ActivityCalendarFilterByTag(pEventDef->uDisplayTags, pRequest->piTagsInclude, pRequest->piTagsExclude))
	{
		PERFINFO_AUTO_STOP();// FUNC
		return;
	}

	// This should be moved at least in part into ShardEventTimingCommon. Perhaps a function that calls
	//   a callback on every valid event occurrence within a range.
	for(iTimerEntry=0;iTimerEntry<eaSize(&pEventDef->ShardTimingDef.ppTimingEntries);iTimerEntry++)
	{
		ShardEventTimingEntry *pShardTimingEntry = pEventDef->ShardTimingDef.ppTimingEntries[iTimerEntry];
		U32 uFirstIteration;
		U32 uLastIteration;
		U32 u;

		if (pShardTimingEntry->uDateStart >= pRequest->uEndDate || (pShardTimingEntry->uTimeEnd && pShardTimingEntry->uTimeEnd <= pRequest->uStartDate))
			continue;

		if (pShardTimingEntry->uTimeRepeat)
		{
			U32 uFirstTime, uLastTime;
			if (pRequest->uStartDate >= pShardTimingEntry->uDateStart)
			{
				uFirstIteration = (pRequest->uStartDate - pShardTimingEntry->uDateStart) / pShardTimingEntry->uTimeRepeat;
			}
			else
			{
				uFirstIteration = 0;
			}
			// -1 so we don't count the interval that starts at our end date
			uLastIteration = ((MIN(pRequest->uEndDate, pShardTimingEntry->uTimeEnd) - pShardTimingEntry->uDateStart)-1) / pShardTimingEntry->uTimeRepeat;

			// Get the starting times of the first and last iterations for some comparison checks
			uFirstTime = ShardEventTiming_GetTimingDefStartTimeFromCycle(&(pEventDef->ShardTimingDef), iTimerEntry, uFirstIteration);
			uLastTime = ShardEventTiming_GetTimingDefStartTimeFromCycle(&(pEventDef->ShardTimingDef), iTimerEntry, uLastIteration);
				
			// If the StartDate is in the inactive portion of the FirstIteration cycle, then increment to the next.
			if (pRequest->uStartDate > uFirstTime + pShardTimingEntry->uTimeDuration)
			{
				uFirstIteration++;
			}

			// And if the last iteration starts after the requested end date, decrement the end
			if (pRequest->uEndDate < uLastTime)
			{
				uLastIteration--;
			}
		}
		else
		{
			uFirstIteration = uLastIteration = 0;
		}

		for(u=uFirstIteration;u<=uLastIteration;u++)
		{
			CalendarEvent *pCalendarEvent = eaIndexedGetUsingString(peaCalendarEvents, pEventDef->pchEventName);
			U32 uStartTime;
			U32 uEndTime;

			if (!pCalendarEvent)
			{
				pCalendarEvent = StructCreate(parse_CalendarEvent);
				gslActivity_FillCalendarEvent(pCalendarEvent, pEventDef);
				eaIndexedEnable(peaCalendarEvents, parse_CalendarEvent);
				eaPush(peaCalendarEvents, pCalendarEvent);
			}
			if (!bUpdatedEvent)
			{
				gslActivity_UpdateCalendarEvent(pCalendarEvent, pEventDef, pWarpDef, pEventContact, pEntity);
				pCalendarEvent->bDirty = true;
				bUpdatedEvent = true;
			}

			if(pShardTimingEntry->uTimeRepeat)
			{
				uStartTime = ShardEventTiming_GetTimingDefStartTimeFromCycle(&(pEventDef->ShardTimingDef), iTimerEntry, u);
				// MIN is required here because the TimeEnd may be specified in the middle of the active part of the cycle
				uEndTime = MIN(uStartTime + pShardTimingEntry->uTimeDuration, pShardTimingEntry->uTimeEnd);
			}
			else
			{
				uStartTime = pShardTimingEntry->uDateStart;
				uEndTime = pShardTimingEntry->uTimeEnd;
			}

			gslActivity_GetOrCreateCalendarTimingEntry(pCalendarEvent, uStartTime, uEndTime);
		}
	}

	PERFINFO_AUTO_STOP();// FUNC
}

static void gslActivity_GetTagsFromStrings(const char *pchTagsToInclude, const char *pchTagsToExclude, U32 **ppiTagsInclude, U32 **ppiTagsExclude)
{
	char *pchFind = NULL;
	char *pchData = NULL;
	char *pchContext;
	if(pchTagsToInclude && pchTagsToInclude[0])
	{
		strdup_alloca(pchData,pchTagsToInclude);

		while(pchFind = strtok_r(pchFind ? NULL : pchData,",",&pchContext))
		{
			if(pchFind[0])
			{
				U32 uFind = StaticDefineIntGetInt(ActivityDisplayTagsEnum, pchFind);

				if(uFind != -1)
					ea32Push(ppiTagsInclude,uFind);
			}
		}
	}

	if(pchTagsToExclude && pchTagsToExclude[0])
	{
		strdup_alloca(pchData,pchTagsToExclude);

		pchFind = NULL;
		while(pchFind = strtok_r(pchFind ? NULL : pchData,",",&pchContext))
		{
			if(pchFind[0])
			{
				U32 uFind = StaticDefineIntGetInt(ActivityDisplayTagsEnum, pchFind);

				if(uFind != -1)
					ea32Push(ppiTagsExclude,uFind);
			}
		}
	}
}

static void gslActivity_FillActivityCalendarEx(Entity *pEntity, PendingCalendarRequest** eaRequests, CalendarEvent ***peaCalendarEvents)
{
	RefDictIterator eventIterator;
	EventDef *pEventDef;
	EventWarpDef *pWarpDef;
	EventContactDef *pEventContact;
	int i;

	PERFINFO_AUTO_START_FUNC();

	// WOLF[22Feb12]  Currently the eventDefs carry the RunMode of the events indicating
	//  whether the event is Manually turned on, Manually turned off, or running it's normal schedule.
	//  We could theoretically change the data we are passing back to the client based on this,
	//  but without a clear design of how this information should be displayed we would only
	//  be complicating things without cause.

	RefSystem_InitRefDictIterator(g_hEventDictionary, &eventIterator);
	while(pEventDef = (EventDef *)RefSystem_GetNextReferentFromIterator(&eventIterator))
	{
		if (gslActivity_ShouldHideEvent(pEventDef))
			continue;

		pWarpDef = EventDef_GetWarpForAllegiance(pEventDef, SAFE_GET_REF(pEntity, hAllegiance));
		pEventContact = EventDef_GetContactForAllegiance(pEventDef, SAFE_GET_REF(pEntity, hAllegiance));

		for (i = eaSize(&eaRequests)-1; i >= 0; i--)
		{
			PendingCalendarRequest* pRequest = eaRequests[i];

			gslActivity_AddCalendarEventsForTimeRange(pEntity, pEventDef, pWarpDef, pEventContact, pRequest, peaCalendarEvents);
		}
	}

	PERFINFO_AUTO_STOP();// FUNC
}

void gslActivity_UpdateServerCalendar(Entity *pEntity)
{
	CalendarRequest testCalendar = {0};
	PendingCalendarRequest pendingRequest = {0};

	RefDictIterator eventIterator;
	EventDef *pEventDef;
	EventWarpDef *pWarpDef;
	EventContactDef *pEventContact;

	if(g_EventConfig.uServerCalendarTime == 0)
		return;

	PERFINFO_AUTO_START_FUNC();

	pendingRequest.uStartDate = gslActivity_GetEventClockSecondsSince2000();
	pendingRequest.uEndDate = gslActivity_GetEventClockSecondsSince2000() + g_EventConfig.uServerCalendarTime;
	pendingRequest.piTagsExclude = g_EventConfig.piServerTagsExclude;
	pendingRequest.piTagsInclude = g_EventConfig.piServerTagsInclude;


	RefSystem_InitRefDictIterator(g_hEventDictionary, &eventIterator);
	while(pEventDef = (EventDef *)RefSystem_GetNextReferentFromIterator(&eventIterator))
	{
		if (gslActivity_ShouldHideEvent(pEventDef))
			continue;

		pWarpDef = EventDef_GetWarpForAllegiance(pEventDef, SAFE_GET_REF(pEntity, hAllegiance));
		pEventContact = EventDef_GetContactForAllegiance(pEventDef, SAFE_GET_REF(pEntity, hAllegiance));

		gslActivity_AddCalendarEventsForTimeRange(pEntity, pEventDef, pWarpDef, pEventContact, &pendingRequest, &testCalendar.eaEvents);
	}

	if(StructCompare(parse_CalendarRequest,pEntity->pPlayer->pEventInfo->pServerCalendar,&testCalendar,COMPAREFLAG_NULLISDEFAULT,0,0) != 0)
	{
        // Events have changed.

        if ( pEntity->pPlayer->pEventInfo->pServerCalendar )
        {
            // There are old events.  There may or may not be new events.

            // Clear out the old events.
            eaDestroyStruct(&pEntity->pPlayer->pEventInfo->pServerCalendar->eaEvents, parse_CalendarEvent);

            // Swap in the new events.
            pEntity->pPlayer->pEventInfo->pServerCalendar->eaEvents = testCalendar.eaEvents;

            // Set the dirty bit so they get sent to the client.
            entity_SetDirtyBit(pEntity,parse_PlayerEventInfo,pEntity->pPlayer->pEventInfo,false);
        }
        else if ( testCalendar.eaEvents ) 
        {
            // There are no old events, but new events were found.

            // No old events exist, so create a new struct to hold the new ones.
            pEntity->pPlayer->pEventInfo->pServerCalendar = StructCreate(parse_CalendarRequest);

            // Swap in the new events.
            pEntity->pPlayer->pEventInfo->pServerCalendar->eaEvents = testCalendar.eaEvents;

            // Set the dirty bit so they get sent to the client.
            entity_SetDirtyBit(pEntity,parse_PlayerEventInfo,pEntity->pPlayer->pEventInfo,false);
        }
        else
        {
            devassertmsgf(false, "%s: No old events or new events, but somehow we think events changed.", __FUNCTION__);
        }
	}
    else
    {
        // Events have not changed, so free the new events.
        eaDestroyStruct(&testCalendar.eaEvents, parse_CalendarEvent);
    }

	PERFINFO_AUTO_STOP(); // FUNC
}

static void gslActivity_UpdateActiveEvents(Entity* pEntity)
{
	if (pEntity->pPlayer->pEventInfo)
	{
		U32 uCurrentTime = gslActivity_GetEventClockSecondsSince2000();
		PendingCalendarRequest Request = {0};
		bool bDirty = false;
		int i, j;
		
		PERFINFO_AUTO_START_FUNC();

		Request.uStartDate = uCurrentTime;
		Request.uEndDate = uCurrentTime;

		// Clear dirty flags
		for (i = eaSize(&pEntity->pPlayer->pEventInfo->eaActiveEvents)-1; i >= 0; i--)
		{
			pEntity->pPlayer->pEventInfo->eaActiveEvents[i]->bDirty = false;
			for (j = eaSize(&pEntity->pPlayer->pEventInfo->eaActiveEvents[i]->eaTiming)-1; j >= 0; j--)
			{
				pEntity->pPlayer->pEventInfo->eaActiveEvents[i]->eaTiming[j]->bDirty = false;
			}
		}
		for (i = 0; i < eaSize(&g_ActiveEvents.ppDefs); i++)
		{
			EventDef* pEventDef = GET_REF(g_ActiveEvents.ppDefs[i]->hEvent);
			if (!pEventDef) 
			{
				// Something is horribly wrong, just exit the function now
				ErrorDetailsf("EventDef %s", REF_STRING_FROM_HANDLE(g_ActiveEvents.ppDefs[i]->hEvent));
				Errorf("Couldn't find EventDef when updating active events for player!");
				PERFINFO_AUTO_STOP();// FUNC
				return;
			}
			else if (!gslActivity_ShouldHideEvent(pEventDef))
			{
				EventWarpDef *pWarpDef = EventDef_GetWarpForAllegiance(pEventDef, SAFE_GET_REF(pEntity, hAllegiance));
				EventContactDef *pEventContact = EventDef_GetContactForAllegiance(pEventDef, SAFE_GET_REF(pEntity, hAllegiance));

				gslActivity_AddCalendarEventsForTimeRange(pEntity,pEventDef,pWarpDef,pEventContact,&Request,&pEntity->pPlayer->pEventInfo->eaActiveEvents);
			}
		}

		// Destroy any data that wasn't marked as dirty
		for (i = eaSize(&pEntity->pPlayer->pEventInfo->eaActiveEvents)-1; i >= 0; i--)
		{
			CalendarEvent* pCalendarEvent = pEntity->pPlayer->pEventInfo->eaActiveEvents[i];

			if (!pCalendarEvent->bDirty)
			{
				StructDestroy(parse_CalendarEvent, eaRemove(&pEntity->pPlayer->pEventInfo->eaActiveEvents, i));
				bDirty = true;
			}
			else
			{
				for (j = eaSize(&pCalendarEvent->eaTiming)-1; j >= 0; j--)
				{
					CalendarTiming* pTiming = pCalendarEvent->eaTiming[j];
					if (!pTiming->bDirty)
					{
						StructDestroy(parse_CalendarTiming, eaRemove(&pCalendarEvent->eaTiming, j));
						bDirty = true;
					}
				}
			}
		}

		if (bDirty)
		{
			entity_SetDirtyBit(pEntity, parse_PlayerEventInfo, pEntity->pPlayer->pEventInfo, true);
			entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
		}

		PERFINFO_AUTO_STOP();// FUNC
	}
}

static void gslActivity_UpdatePendingRequests(Entity* pEntity)
{
	PlayerEventInfo* pEventInfo = pEntity->pPlayer->pEventInfo;

	if (pEventInfo && eaSize(&pEventInfo->eaPendingRequests))
	{
		int i;
		
		PERFINFO_AUTO_START_FUNC();

		for (i = eaSize(&pEventInfo->eaPendingRequests)-1; i >= 0; i--)
		{
			PendingCalendarRequest* pPendingRequest = pEventInfo->eaPendingRequests[i];
			if (!pEventInfo->uRequestStartDate)
			{
				pEventInfo->uRequestStartDate = pPendingRequest->uStartDate;
			}
			else
			{
				MIN1(pEventInfo->uRequestStartDate, pPendingRequest->uStartDate);
			}
			MAX1(pEventInfo->uRequestEndDate, pPendingRequest->uEndDate);
		}

		// Fill in calendar details for all existing requests
		gslActivity_FillActivityCalendarEx(pEntity,pEventInfo->eaPendingRequests,&pEventInfo->eaRequestedEvents);

		// Clear the list of pending requests
		eaClearStruct(&pEventInfo->eaPendingRequests,parse_PendingCalendarRequest);
		
		// Set dirty bits
		entity_SetDirtyBit(pEntity, parse_PlayerEventInfo, pEntity->pPlayer->pEventInfo, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);
		
		PERFINFO_AUTO_STOP();// FUNC
	}
}

// Update active and pending requested events for the player
void gslActivity_UpdateEvents(Entity *pEntity)
{
	// Update active events
	gslActivity_UpdateActiveEvents(pEntity);

	// Update pending event requests
	gslActivity_UpdatePendingRequests(pEntity);

	// Update active calendar
	gslActivity_UpdateServerCalendar(pEntity);
}

static PendingCalendarRequest* gslActivity_GetSimilarRequest(Entity* pEntity, U32 uStartDate, U32 uEndDate, U32* piTagsInclude, U32* piTagsExclude)
{
	int i, j, iPendingCount = eaSize(&pEntity->pPlayer->pEventInfo->eaPendingRequests);
	for (i = iPendingCount-1; i >= 0; i--)
	{
		PendingCalendarRequest* pPendingRequest = pEntity->pPlayer->pEventInfo->eaPendingRequests[i];
		
		// Consider this a match if the input list is a subset of the existing list
		for (j = eaiSize(&piTagsInclude)-1; j >= 0; j--)
		{
			int iIndex = (int)eaiBFind(pPendingRequest->piTagsInclude,piTagsInclude[i]);
			if (iIndex >= eaiSize(&pPendingRequest->piTagsInclude) || 
				pPendingRequest->piTagsInclude[iIndex] != piTagsInclude[i])
			{
				break;
			}
		}
		if (j >= 0)
		{
			continue;
		}

		// The exclude tags must match
		if (eaiCompare(&pPendingRequest->piTagsExclude, &piTagsExclude)!=0)
		{
			continue;
		}

		// Check to see that the time range is a subset of the existing time range or roughly the same
		if (uStartDate >= pPendingRequest->uStartDate - EVENT_PERIODIC_REQUEST_TIME &&
			uStartDate < pPendingRequest->uEndDate &&
			uEndDate <= pPendingRequest->uEndDate + EVENT_PERIODIC_REQUEST_TIME &&
			uEndDate > pPendingRequest->uStartDate)
		{
			return pPendingRequest;
		}
	}
	return NULL;
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE; 
void gslActivity_FillActivityCalendar(Entity *pEntity, U32 uStartDate, U32 uEndDate, const char *pchTagsToInclude, const char *pchTagsToExclude)
{
	PERFINFO_AUTO_START_FUNC();

	if (pEntity->pPlayer && pEntity->pPlayer->pEventInfo)
	{
		PendingCalendarRequest* pRequest;
		U32 uCurrentTime = timeSecondsSince2000();
		U32* piTagsInclude = NULL;
		U32* piTagsExclude = NULL;
		
		gslActivity_GetTagsFromStrings(pchTagsToInclude, pchTagsToExclude, &piTagsInclude, &piTagsExclude);

		eaiQSort(piTagsInclude, intCmp);
		eaiQSort(piTagsExclude, intCmp);

		pRequest = gslActivity_GetSimilarRequest(pEntity, uStartDate, uEndDate, piTagsInclude, piTagsExclude);
		if (pRequest)
		{
			MIN1(pRequest->uStartDate, uStartDate);
			MAX1(pRequest->uEndDate, uEndDate);
		}
		else if (eaSize(&pEntity->pPlayer->pEventInfo->eaPendingRequests) < EVENT_MAX_PENDING_REQUESTS)
		{
			PendingCalendarRequest* pPendingRequest = StructCreate(parse_PendingCalendarRequest);
			pPendingRequest->uStartDate = uStartDate;
			pPendingRequest->uEndDate = uEndDate;
			eaiCopy(&pPendingRequest->piTagsInclude, &piTagsInclude);
			eaiCopy(&pPendingRequest->piTagsExclude, &piTagsExclude);
			eaPush(&pEntity->pPlayer->pEventInfo->eaPendingRequests, pPendingRequest);
		}

		// If the player hasn't requested events in a while, handle the requests now
		if (pEntity->pPlayer->pEventInfo->uLastRequestTime + EVENT_PERIODIC_REQUEST_TIME <= uCurrentTime)
		{
			gslActivity_UpdatePendingRequests(pEntity);
		}

		// Set the last request time
		pEntity->pPlayer->pEventInfo->uLastRequestTime = uCurrentTime;

		// Cleanup
		eaiDestroy(&piTagsInclude);
		eaiDestroy(&piTagsExclude);
	}

	PERFINFO_AUTO_STOP();// FUNC
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(7) ACMD_PRIVATE; 
void gslActivity_FillActivityCalendarForPrint(Entity *pEntity, U32 uStartDate, U32 uEndDate, const char *pchFileName)
{
	if (pEntity->pPlayer)
	{
		CalendarRequest* pRequest = StructCreate(parse_CalendarRequest);
		PendingCalendarRequest** eaPendingRequests = NULL;
		PendingCalendarRequest* pPendingRequest = StructCreate(parse_PendingCalendarRequest);

		pPendingRequest->uStartDate = uStartDate;
		pPendingRequest->uEndDate = uEndDate;
		eaPush(&eaPendingRequests, pPendingRequest);

		gslActivity_FillActivityCalendarEx(pEntity,eaPendingRequests,&pRequest->eaEvents);

		ClientCmd_gclPrintCalendarInfo(pEntity, pRequest, pchFileName);

		StructDestroy(parse_CalendarRequest, pRequest);
		eaDestroyStruct(&eaPendingRequests, parse_PendingCalendarRequest);
	}
}

// Adds a player subscription to a specific event on the server
static void gslEvent_AddPlayerSubscription(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_OP_VALID EventDef *pEventDef)
{
	PlayerEventSubscriptionList *pList;
	EntityRef erPlayer = entGetRef(pEntity);
	S32 iPlayerIndex = -1;
	S32 iIndex;

	if (pEventDef == NULL)
	{
		return;
	}

	iIndex = eaIndexedFindUsingString(&g_PlayerEventSubscriptionLists, pEventDef->pchEventName);

	if (iIndex >= 0)
	{
		pList = g_PlayerEventSubscriptionLists[iIndex];
	}
	else
	{
		pList = StructCreate(parse_PlayerEventSubscriptionList);
		pList->pchEventName = allocAddString(pEventDef->pchEventName);
		eaIndexedAdd(&g_PlayerEventSubscriptionLists, pList);
	}
	
	if (!ea32SortedFindIntOrPlace(&pList->perSubscribedPlayers, erPlayer, &iPlayerIndex))
	{
		ea32Insert(&pList->perSubscribedPlayers, erPlayer, iPlayerIndex);
	}
}

// Removes a player subscription to a specific event on the server
static void gslEvent_RemovePlayerSubscription(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_OP_VALID EventDef *pEventDef)
{
	S32 iIndex;
	if (pEventDef == NULL)
	{
		return;
	}
	
	iIndex = eaIndexedFindUsingString(&g_PlayerEventSubscriptionLists, pEventDef->pchEventName);
	if (iIndex >= 0)
	{
		S32 iPlayerIndex = -1;
		EntityRef erPlayer = entGetRef(pEntity);
		PlayerEventSubscriptionList *pList = g_PlayerEventSubscriptionLists[iIndex];

		if (ea32SortedFindIntOrPlace(&pList->perSubscribedPlayers, erPlayer, &iPlayerIndex))
		{
			ea32Remove(&pList->perSubscribedPlayers, iPlayerIndex);
		}

		if (ea32Size(&pList->perSubscribedPlayers) == 0)
		{
			// Remove since there are no players subscribed to this event
			eaRemove(&g_PlayerEventSubscriptionLists, iIndex);
		}
	}
}

// Adds all player event subscriptions to the server list
void gslEvent_AddAllPlayerSubscriptions(SA_PARAM_NN_VALID Entity *pEntity)
{
	devassert(pEntity->pPlayer);

	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pEntity->pPlayer->pEventInfo->eaSubscribedEvents, PlayerSubscribedEvent, pSubscribedEvent)
	{
		gslEvent_AddPlayerSubscription(pEntity, GET_REF(pSubscribedEvent->hEvent));
	}
	FOR_EACH_END
}

// Removes all player event subscriptions from the server list
void gslEvent_RemoveAllPlayerSubscriptions(SA_PARAM_NN_VALID Entity *pEntity)
{
	devassert(pEntity->pPlayer);

	FOR_EACH_IN_CONST_EARRAY_FORWARDS(pEntity->pPlayer->pEventInfo->eaSubscribedEvents, PlayerSubscribedEvent, pSubscribedEvent)
	{
		gslEvent_RemovePlayerSubscription(pEntity, GET_REF(pSubscribedEvent->hEvent));
	}
	FOR_EACH_END
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(SetEventReminder) ACMD_PRIVATE; 
void gslEvent_Subscribe(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char * pchEventName, U32 uTargetTime)
{
	// Make sure this is a valid event
	EventDef *pEventDef = EventDef_Find(pchEventName);
	
	if (!pEventDef || pEventDef->bHideEventFromClient)
	{
		return;
	}

	if (eaSize(&pEntity->pPlayer->pEventInfo->eaSubscribedEvents) >= g_EventConfig.iMaxEventReminders)
	{
		// Reached the maximum number of event reminders
		char *estrBuffer = NULL;
		entFormatGameMessageKey(pEntity, &estrBuffer, "EventReminders.ReachedMaximum",
			STRFMT_INT("NumberOfEventRemindersAllowed", g_EventConfig.iMaxEventReminders),
			STRFMT_STRING("EventName", entTranslateDisplayMessage(pEntity, pEventDef->msgDisplayName)),
			STRFMT_END);
		ClientCmd_NotifySend(pEntity, kNotifyType_Failed, estrBuffer, pEventDef->pchEventName, pEventDef->pchIcon);
		estrDestroy(&estrBuffer);

		return;
	}

	// Make sure the player is not already subscribed to the event
	if (eaIndexedFindUsingString(&pEntity->pPlayer->pEventInfo->eaSubscribedEvents, pchEventName) >= 0)
	{
		// Already subscribed
		char *estrBuffer = NULL;
		entFormatGameMessageKey(pEntity, &estrBuffer, "EventReminders.ReminderIsAlreadySet",
			STRFMT_STRING("EventName", entTranslateDisplayMessage(pEntity, pEventDef->msgDisplayName)),
			STRFMT_END);
		ClientCmd_NotifySend(pEntity, kNotifyType_Failed, estrBuffer, pEventDef->pchEventName, pEventDef->pchIcon);
		estrDestroy(&estrBuffer);

		return;
	}

	{
		U32 uLastStart = 0;
		U32 uEndOfLastStart = 0;
		U32 uNextStart = 0;
		U32 uEventClockTime = gslActivity_GetEventClockSecondsSince2000();

		// This only pays attention to the actual def and does not take into consideration the iRunMode of the EventDef:
		ShardEventTiming_GetUsefulTimes(&(pEventDef->ShardTimingDef), uTargetTime, &uLastStart, &uEndOfLastStart, &uNextStart);

		// Make sure it's something we can subscribe to (it's something in the future)
		if (uEventClockTime < uLastStart)
		{
			PlayerSubscribedEvent *pSubscribedEvent;

			pSubscribedEvent = StructCreate(parse_PlayerSubscribedEvent);
			SET_HANDLE_FROM_REFERENT(g_hEventDictionary, pEventDef, pSubscribedEvent->hEvent);

			// Set the start/end times
			pSubscribedEvent->uEventStartTime=uLastStart;
			pSubscribedEvent->uEventEndTime=uEndOfLastStart;

			// Add to the subscription list
			eaIndexedAdd(&pEntity->pPlayer->pEventInfo->eaSubscribedEvents, pSubscribedEvent);

			entity_SetDirtyBit(pEntity, parse_PlayerEventInfo, pEntity->pPlayer->pEventInfo, true);
			entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);

			// Add the server subscription
			gslEvent_AddPlayerSubscription(pEntity, pEventDef);
		}
	}
}

AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(RemoveEventReminder) ACMD_PRIVATE; 
void gslEvent_Unsubscribe(SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char * pchEventName)
{
	S32 iIndex = eaIndexedFindUsingString(&pEntity->pPlayer->pEventInfo->eaSubscribedEvents, pchEventName);

	if (iIndex >= 0)
	{
		eaRemove(&pEntity->pPlayer->pEventInfo->eaSubscribedEvents, iIndex);

		entity_SetDirtyBit(pEntity, parse_PlayerEventInfo, pEntity->pPlayer->pEventInfo, true);
		entity_SetDirtyBit(pEntity, parse_Player, pEntity->pPlayer, false);

		// Remove the server subscription
		gslEvent_RemovePlayerSubscription(pEntity, EventDef_Find(pchEventName));
	}
}

// The event system tick function
void gslEvent_Tick(F32 fTimeStep)
{
	S32 i;
	U32 uEventClockTime = gslActivity_GetEventClockSecondsSince2000();

	for (i = eaSize(&g_PlayerEventSubscriptionLists) - 1; i >= 0; i--)
	{
		PlayerEventSubscriptionList *pList = g_PlayerEventSubscriptionLists[i];
		EventDef *pEventDef = EventDef_Find(pList->pchEventName);
		S32 iPlayerIndex;

		if (pEventDef==NULL)
		{
			// The eventDef went away somehow. 
			eaRemove(&g_PlayerEventSubscriptionLists, i);
			continue;
		}

		// Handle all player subscriptions
		for (iPlayerIndex = ea32Size(&pList->perSubscribedPlayers) - 1; iPlayerIndex >= 0; iPlayerIndex--)
		{
			Entity *pEnt = entFromEntityRefAnyPartition(pList->perSubscribedPlayers[iPlayerIndex]);
			S32 iEventSubscriptionIndex;

			if (pEnt == NULL)
			{
				// Player is no longer in this server
				ea32Remove(&pList->perSubscribedPlayers, iPlayerIndex);
				continue;
			}

			if (entCheckFlag(pEnt, ENTITYFLAG_IGNORE))
			{
				continue;
			}

			iEventSubscriptionIndex = eaIndexedFindUsingString(&pEnt->pPlayer->pEventInfo->eaSubscribedEvents, pEventDef->pchEventName);

			if (iEventSubscriptionIndex >= 0)
			{
				PlayerSubscribedEvent *pSubscribedEvent = pEnt->pPlayer->pEventInfo->eaSubscribedEvents[iEventSubscriptionIndex];

				// Run our stored eventStartTime and EventEndTime against the event def as validation. Perhaps this player
				//  just logged in and has a subscription to something that is no longer a valid slot if the data changed since
				//  the subscription was originally made.
				{
					U32 uLastStart = 0;
					U32 uEndOfLastStart = 0;
					U32 uNextStart = 0;

					// This only pays attention to the actual def and does not take into consideration the iRunMode of the EventDef:
					ShardEventTiming_GetUsefulTimes(&(pEventDef->ShardTimingDef), pSubscribedEvent->uEventStartTime, &uLastStart, &uEndOfLastStart, &uNextStart);

					if (pSubscribedEvent->uEventStartTime!=uLastStart || pSubscribedEvent->uEventEndTime!=uEndOfLastStart)
					{

						// Something changed. Let's consider this an invalid subscription and just remove it
						eaRemove(&pEnt->pPlayer->pEventInfo->eaSubscribedEvents, iEventSubscriptionIndex);
						entity_SetDirtyBit(pEnt, parse_PlayerEventInfo, pEnt->pPlayer->pEventInfo, true);
						entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
						ea32Remove(&pList->perSubscribedPlayers, iPlayerIndex);
					}
				}

				if (uEventClockTime >= pSubscribedEvent->uEventEndTime)
				{
					// Missed the event
					char *estrBuffer = NULL;
					entFormatGameMessageKey(pEnt, &estrBuffer, "EventReminders.Missed",
						STRFMT_STRING("EventName", entTranslateDisplayMessage(pEnt, pEventDef->msgDisplayName)),
						STRFMT_END);
					ClientCmd_NotifySend(pEnt, kNotifyType_EventMissed, estrBuffer, pEventDef->pchEventName, pEventDef->pchIcon);
					estrDestroy(&estrBuffer);

					// Player missed the event. Send the notification and remove from the list
					eaRemove(&pEnt->pPlayer->pEventInfo->eaSubscribedEvents, iEventSubscriptionIndex);
					entity_SetDirtyBit(pEnt, parse_PlayerEventInfo, pEnt->pPlayer->pEventInfo, true);
					entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
					ea32Remove(&pList->perSubscribedPlayers, iPlayerIndex);
				}
				else if (uEventClockTime >= pSubscribedEvent->uEventStartTime)
				{
					// Send event started notification
					char *estrBuffer = NULL;
					entFormatGameMessageKey(pEnt, &estrBuffer, "EventReminders.Started",
						STRFMT_STRING("EventName", entTranslateDisplayMessage(pEnt, pEventDef->msgDisplayName)),
						STRFMT_END);
					ClientCmd_NotifySend(pEnt, kNotifyType_EventStarted, estrBuffer, pEventDef->pchEventName, pEventDef->pchIcon);
					estrDestroy(&estrBuffer);

					// We're done with the player. Remove from the list
					eaRemove(&pEnt->pPlayer->pEventInfo->eaSubscribedEvents, iEventSubscriptionIndex);
					entity_SetDirtyBit(pEnt, parse_PlayerEventInfo, pEnt->pPlayer->pEventInfo, true);
					entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
					ea32Remove(&pList->perSubscribedPlayers, iPlayerIndex);
				}
				else if (!pSubscribedEvent->bPreEventNotificationSent && 
						(uEventClockTime >= pSubscribedEvent->uEventStartTime - (g_EventConfig.uPreEventReminderTimeInMinutes * SECONDS_PER_MINUTE)))
				{
					// Send event about to start notification
					char *estrBuffer = NULL;
					entFormatGameMessageKey(pEnt, &estrBuffer, "EventReminders.AboutToStart",
						STRFMT_STRING("EventName", entTranslateDisplayMessage(pEnt, pEventDef->msgDisplayName)),
						STRFMT_END);
					ClientCmd_NotifySend(pEnt, kNotifyType_EventAboutToStart, estrBuffer, pEventDef->pchEventName, pEventDef->pchIcon);
					estrDestroy(&estrBuffer);

					pSubscribedEvent->bPreEventNotificationSent = true;
				}
			}
			else
			{
				// Player is no longer subscribed to this event
				ea32Remove(&pList->perSubscribedPlayers, iPlayerIndex);
			}
		}

		if (ea32Size(&pList->perSubscribedPlayers) == 0)
		{
			// No more subscribed players
			eaRemove(&g_PlayerEventSubscriptionLists, i);
			continue;
		}
	}
}

#include "AutoGen/gslActivity_h_ast.c"
