		/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslMapManagerActivity.h"

#include "aslMapManagerNewMapTransfer_InnerStructs.h"
#include "timing.h"
#include "ActivityCommon.h"
#include "ResourceInfo.h"

#include "stashTable.h"
#include "StringCache.h"

#include "AutoGen/aslMapManagerActivity_h_ast.h"
#include "autogen/GameServerLib_autogen_RemoteFuncs.h"

#include "autogen/AppServerLib_autotransactions_autogen_wrappers.h"


ActiveActivityEntry **gppUnownedActiveActivities = NULL;		// These are used by internal testing. It's okay that they do not persist across restarts or that they
														//  interfere a little with the events' activities.

ActiveEventEntry **gppActiveEvents = NULL;

typedef struct EventContainerRef
{
	REF_TO(EventContainer) hRef;
}EventContainerRef;

EventContainerRef s_EventContainerRef = {0};


///////////////////////////////////////////////////////////////////////////////////
//  EventClock

// Limit on how big the delta from real time can be for a user-set event clock. Trying to stay under MAXINT.
#define MAX_EVENTCLOCK_DELTA (60*60*24*365*13)	

static S32 gsiEventClockDelta=0;

U32 GetEventClockTime()
{
	U32 uCurrentTime = timeSecondsSince2000();

	// NOTE: This can overflow. Not really a lot we can do about it other than limit MAX_EVENTCLOCK_DELTA
	return(uCurrentTime + gsiEventClockDelta);
}

static void SetEventClockDelta(S32 iNewDelta)
{
	gsiEventClockDelta = iNewDelta;

	//Inform game servers that there is a new delta
	FOR_EACH_IN_STASHTABLE(sGameServerListsByMapDescription, GameServerList, pList)
	{
		if (pList->eType == LISTTYPE_NORMAL)
		{
			int iServerNum;
			for (iServerNum=0; iServerNum < eaSize(&pList->ppGameServers); iServerNum++)
			{
				TrackedGameServerExe *pServer = pList->ppGameServers[iServerNum];
				RemoteCommand_gslActivity_SetEventClockDelta(GLOBALTYPE_GAMESERVER,pServer->iContainerID,gsiEventClockDelta);
			}
		}
	}
	FOR_EACH_END
}


static bool SetEventClockTime(U32 uNewTime)
{
	U32 uCurrentTime = timeSecondsSince2000();
	U32 uDelta;
	int iSign;

	if (uNewTime > uCurrentTime)
	{
		uDelta = uNewTime - uCurrentTime;
		iSign = 1;
	}
	else
	{
		uDelta = uCurrentTime - uNewTime;
		iSign = -1;
	}

	if (uDelta > MAX_EVENTCLOCK_DELTA)
	{
		return(false);
	}
	SetEventClockDelta(uDelta * iSign);
	return(true);
}


AUTO_COMMAND_REMOTE;
char *MapManager_SetEventClockTimeString(const char* pTimeString)
{
	bool bSuccess=false;
	static char *estrResult = NULL;
	U32 uNewTime;

	estrClear(&estrResult);
	
	uNewTime = timeGetSecondsSince2000FromGenericString(pTimeString);
	if (uNewTime>0)
	{
		bSuccess=SetEventClockTime(uNewTime);
	}

	if (bSuccess)
	{
		estrCopy2(&estrResult,"Event Clock Set: ");
		estrConcatf(&estrResult,"PAC%s",timeGetPACDateStringFromSecondsSince2000(uNewTime));
		estrConcatf(&estrResult," UTC%s",timeGetDateStringFromSecondsSince2000(uNewTime));
	}
	else
	{
		estrCopy2(&estrResult,"Event Clock Not Set.");
	}
	return estrResult;
}

AUTO_COMMAND_REMOTE;
char *MapManager_UnsetEventClockTime()
{
	static char *estrResult = NULL;
	estrClear(&estrResult);
	
	SetEventClockDelta(0);

	estrCopy2(&estrResult,"Event Clock reset to current time.");
	return estrResult;
}

AUTO_COMMAND_REMOTE;
char *MapManager_GetEventClockTime()
{
	U32 uCurrentTime = GetEventClockTime();
	static char *estrResult = NULL;
	estrClear(&estrResult);
	
	estrCopy2(&estrResult,"Current Event Clock: ");
	estrPrintf(&estrResult,"PAC%s",timeGetPACDateStringFromSecondsSince2000(uCurrentTime));
	estrConcatf(&estrResult," UTC%s",timeGetDateStringFromSecondsSince2000(uCurrentTime));
	
	return estrResult;
}

///////////////////////////////////////////////////////////////////////////////////
//  New Server Info request

// Called when a new server starts. Inform the server of any active events and activities and the current event time
void aslMapManager_MapInitActivities(U32 iServerID)
{
	int i;
	U32 uCurrentTime = GetEventClockTime();
	TrackedGameServerExe *pServer = GetGameServerFromID(iServerID);
	RefDictIterator eventIterator;
	EventDef *pEventDef;

	if (pServer==NULL)
	{
		return;
	}

	// Set the EventClockDelta

	RemoteCommand_gslActivity_SetEventClockDelta(GLOBALTYPE_GAMESERVER,iServerID,gsiEventClockDelta);

	// Send the event run modes
	RefSystem_InitRefDictIterator(g_hEventDictionary, &eventIterator);
	while(pEventDef = (EventDef *)RefSystem_GetNextReferentFromIterator(&eventIterator))
	{
		RemoteCommand_Event_SetRunMode(GLOBALTYPE_GAMESERVER,iServerID, pEventDef->pchEventName, pEventDef->iEventRunMode);
	}

	// Send the active events and their activities
	for(i=0;i<eaSize(&gppActiveEvents);i++)
	{
		int iActivity;
		ActiveEventEntry* pActiveEvent = gppActiveEvents[i];
		pEventDef = GET_REF(pActiveEvent->hEvent);
		assertmsg(pEventDef, "An event that existed has stopped existing. This is unrecoverable. Maybe you got latest while map manager was running?");

		RemoteCommand_Event_Start(GLOBALTYPE_GAMESERVER,iServerID, pEventDef->pchEventName);

		// Send each of the events activities
		for(iActivity=0; iActivity<eaSize(&(pActiveEvent->ppActivities)); iActivity++)
		{
			ActiveActivityEntry *pActivity = pActiveEvent->ppActivities[iActivity];

			if (EventDef_MapCheck(pEventDef,pServer->description.pMapDescription))
			{
				RemoteCommand_Activity_Start(GLOBALTYPE_GAMESERVER,iServerID, pActivity->pActivityDef, pActivity->uActivityTimeStart, pActivity->uActivityTimeEnd);
			}
		}
	}

	// Send the unowned activities
	for(i=0;i<eaSize(&gppUnownedActiveActivities);i++)
	{
		RemoteCommand_Activity_Start(GLOBALTYPE_GAMESERVER,iServerID,gppUnownedActiveActivities[i]->pActivityDef,
										 gppUnownedActiveActivities[i]->uActivityTimeStart, gppUnownedActiveActivities[i]->uActivityTimeEnd);
	}
}

///////////////////////////////////////////////////////////////////////////////////
//  Active Event Search

ActiveEventEntry *aslEvent_GetActiveEvent(EventDef *pDef)
{
	int i;

	for(i=0;i<eaSize(&gppActiveEvents);i++)
	{
		EventDef *pEventDef = GET_REF(gppActiveEvents[i]->hEvent);
		if (pEventDef && pEventDef->pchEventName == pDef->pchEventName)
			return gppActiveEvents[i];
	}

	return NULL;
}


///////////////////////////////////////////////////////////////////////////////////
//  General On/Off queries

bool aslEvent_IsOn(EventDef *pDef)
{
	int i;

	for(i=0;i<eaSize(&gppActiveEvents);i++)
	{
		EventDef *pEventDef = GET_REF(gppActiveEvents[i]->hEvent);
		if (pEventDef && pEventDef->pchEventName == pDef->pchEventName)
			return true;
	}

	return false;
}

bool aslActiveEvent_ShouldBeOn(ActiveEventEntry *pEvent)
{
	U32 uCurrentTime = GetEventClockTime();
	EventDef *pEventDef = GET_REF(pEvent->hEvent);

	if(!pEventDef || pEventDef->iEventRunMode == kEventRunMode_ForceOff)
		return false;

	if(pEventDef->iEventRunMode == kEventRunMode_ForceOn)
		return true;
	
	return(ShardEventTiming_EntryShouldBeOn(&(pEventDef->ShardTimingDef),pEvent->iTimingIndex,uCurrentTime));
}


bool aslEventDef_ShouldBeOn(EventDef *pDef, int *iIndexOut)
{
	U32 uCurrentTime = GetEventClockTime();

	if(!pDef || pDef->iEventRunMode==kEventRunMode_ForceOff)
		return false;

	return(ShardEventTiming_DefShouldBeOn(&(pDef->ShardTimingDef),iIndexOut,uCurrentTime));
}

// This only pays attention to the actual def and does not take into consideration the iRunMode of the EventDef. It is up
//  to the calling function to deal appropriately with the different run modes. The monitor display in aslMapManagerActivity pays
//  appropriate attention to the runMode e.g.

bool aslEvent_GetUsefulTimes(const char* pEventName, U32 *puLastStart, U32 *puEndOfLastStart, U32 *puNextStart)
{
	U32 uCurrentTime = GetEventClockTime();
	EventDef *pEventDef = EventDef_Find(pEventName);

	U32 uLastStart=0;
	U32 uEndOfLastStart=0;
	U32 uNextStart = 0xffffffff;

	if(!pEventDef)
	{
		return false;
	}

	ShardEventTiming_GetUsefulTimes(&(pEventDef->ShardTimingDef), uCurrentTime, puLastStart, puEndOfLastStart, puNextStart);

	return(true);
}


static void aslMapManager_SendStartActivityToMaps(EventDef *pEventDef, ActiveActivityEntry *pActivity)
{
	//Inform game servers that this activity started
	FOR_EACH_IN_STASHTABLE(sGameServerListsByMapDescription, GameServerList, pList)
	{
		if (pList->eType == LISTTYPE_NORMAL)
		{
			int iServerNum;
			for (iServerNum=0; iServerNum < eaSize(&pList->ppGameServers); iServerNum++)
			{
				TrackedGameServerExe *pServer = pList->ppGameServers[iServerNum];

				// Unowned activities always get sent
				if (pEventDef==NULL || EventDef_MapCheck(pEventDef,pServer->description.pMapDescription))
				{
					RemoteCommand_Activity_Start(GLOBALTYPE_GAMESERVER,pServer->iContainerID,pActivity->pActivityDef,pActivity->uActivityTimeStart,pActivity->uActivityTimeEnd);
				}
			}
		}
	}
	FOR_EACH_END
}
	 


///////////////////////////////////////////////////////////////////////////////////
//  Activities

static void aslMapManager_StartActivityEx(ActivityDef *pDef, ActiveEventEntry *pOwningEvent)
{
	ActiveActivityEntry *pActivity = StructCreate(parse_ActiveActivityEntry);
	EventDef *pEventDef = pOwningEvent ? GET_REF(pOwningEvent->hEvent) : NULL;

	pActivity->pActivityDef = pDef;
	pActivity->pchActivityName = pDef->pchActivityName;

	if(pOwningEvent)
	{
		pActivity->uActivityTimeStart = pOwningEvent->uEventTimeStart + pDef->uDefaultDelay;

		if(pDef->uDuration == 0)
			pActivity->uActivityTimeEnd = pOwningEvent->uEventTimeEnd;
		else
			pActivity->uActivityTimeEnd = pActivity->uActivityTimeStart + pDef->uDuration;
	}
	else
	{
		pActivity->uActivityTimeStart = GetEventClockTime();
		pActivity->uActivityTimeEnd = 0xffffffff;			// Unowned activities essentially last forever (until they are manually stopped)
		eaPush(&gppUnownedActiveActivities,pActivity);
	}

	if(!pOwningEvent || pActivity->uActivityTimeStart <= GetEventClockTime())
	{
		if(pOwningEvent)
			eaPush(&pOwningEvent->ppActivities,pActivity);
		aslMapManager_SendStartActivityToMaps(pEventDef,pActivity);
	}
	else if(pOwningEvent)
	{
		eaPush(&pOwningEvent->ppDelayActivities,pActivity);
	}
}

static void aslMapManager_StartActivity(const char *pchActivity, ActiveEventEntry *pOwningEvent)
{
	ActivityDef *pDef = ActivityDef_Find(pchActivity);

	if(pDef)
	{
		aslMapManager_StartActivityEx(pDef,pOwningEvent);
	}
}

static void aslMapManager_StopActivityEx(ActiveActivityEntry **ppActivity, ActiveEventEntry *pOwningEvent)
{
	EventDef *pEventDef;

	if (pOwningEvent==NULL)
	{
		return;
	}
	
	if(!ppActivity || !*ppActivity)
		return;

	pEventDef = GET_REF(pOwningEvent->hEvent);

	eaFindAndRemove(&pOwningEvent->ppActivities,*ppActivity);

	//Inform game servers that this activity stopped
	FOR_EACH_IN_STASHTABLE(sGameServerListsByMapDescription, GameServerList, pList)
	{
		if (pList->eType == LISTTYPE_NORMAL)
		{
			int iServerNum;
			for (iServerNum=0; iServerNum < eaSize(&pList->ppGameServers); iServerNum++)
			{
				TrackedGameServerExe *pServer = pList->ppGameServers[iServerNum];

				if(!EventDef_MapCheck(pEventDef,pServer->description.pMapDescription))
					continue;

				RemoteCommand_Activity_End(GLOBALTYPE_GAMESERVER,pServer->iContainerID,(*ppActivity)->pchActivityName);
			}
		}
	}
	FOR_EACH_END

	StructDestroySafe(parse_ActiveActivityEntry,ppActivity);
}

static void aslMapManager_StopUnownedActivity(const char *pchActivityName)
{
	int iIndex=-1;
	int i;

	ActiveActivityEntry *pActivity = NULL;

	for(i=eaSize(&gppUnownedActiveActivities)-1;(i>=0 && iIndex<0);i--)
	{
		if(gppUnownedActiveActivities[i]->pchActivityName == pchActivityName)
		{
			pActivity = gppUnownedActiveActivities[i];
			iIndex=i;
		}
	}

	if(iIndex != -1)
	{
		eaRemove(&gppUnownedActiveActivities,iIndex);
	}

	//Remove from possible active events. This overrides what the event set up and will remain like this until
	//  the event activates again, or the map manager is reset
	
	for(i=eaSize(&gppActiveEvents)-1;i>=0;i--)
	{
		int j;
		for(j=eaSize(&gppActiveEvents[i]->ppActivities)-1;j>=0;j--)
		{
			if (pchActivityName==gppActiveEvents[i]->ppActivities[j]->pchActivityName)
			{
				eaRemove(&gppActiveEvents[i]->ppActivities, j);
			}
		}
	}
	
	//Inform game servers that this activity stopped
	FOR_EACH_IN_STASHTABLE(sGameServerListsByMapDescription, GameServerList, pList)
	{
		if (pList->eType == LISTTYPE_NORMAL)
		{
			int iServerNum;
			for (iServerNum=0; iServerNum < eaSize(&pList->ppGameServers); iServerNum++)
			{
				TrackedGameServerExe *pServer = pList->ppGameServers[iServerNum];

				// We have no eventDef so can't do a MapCheck to limit which servers we send to.
				RemoteCommand_Activity_End(GLOBALTYPE_GAMESERVER,pServer->iContainerID,pchActivityName);
			}
		}
	}
	FOR_EACH_END

	StructDestroy(parse_ActiveActivityEntry,pActivity);
}




///////////////////////////////////////////////////////////////////////////////////
//  Events

static void aslMapManager_StartEventEx(EventDef *pEventDef, int iTimingIndex)
{
	if(pEventDef && pEventDef->iEventRunMode!=kEventRunMode_ForceOff)
	{
		int i;
		ActiveEventEntry *pActiveEvent;
		bool bAlreadyActive=false;
		bool bDBEventNeedsUpdate=true;
		
		pActiveEvent = aslEvent_GetActiveEvent(pEventDef);
		if (pActiveEvent!=NULL)
		{
			// It's already active
			bAlreadyActive=true;
			if (pEventDef->iEventRunMode==kEventRunMode_ForceOn && pActiveEvent->iTimingIndex==-1)
			{
				// We were manually overriden previously and continue to be so, we don't need to update the DB
				bDBEventNeedsUpdate=false;
			}
		}
		else
		{
			// Make a new event
			pActiveEvent = StructCreate(parse_ActiveEventEntry);
			SET_HANDLE_FROM_REFERENT(g_hEventDictionary, pEventDef, pActiveEvent->hEvent);
	
			eaPush(&gppActiveEvents,pActiveEvent);
		}

		// First update our DB data if it needs it.
		if (bDBEventNeedsUpdate)
		{
			if(iTimingIndex == -1)
			{
				pActiveEvent->uEventTimeStart = GetEventClockTime();
				pActiveEvent->uEventTimeEnd = 0xffffffff;		// This is a change to support activity delay/duration stuff. We used to leave it at zero.
			}
			else
			{
				// If EventClockTime is inconsistent with the event REALLY being active, we will get strange start, stop times
				//  The functions that call this one should have dealt with that, though. Theoretically the system will stop the event next frame anyway
				// Long term we should consider removing the dependency on the iTimingIndex at all. It doesn't serve much of a purpose under
				//  the present implementation. We would need to distinguish manually started events from auto-started events still, though

				ShardEventTiming_GetUsefulTimes(&(pEventDef->ShardTimingDef), GetEventClockTime(), &(pActiveEvent->uEventTimeStart), &(pActiveEvent->uEventTimeEnd), NULL);
			}
			pActiveEvent->iTimingIndex = iTimingIndex;

			AutoTrans_aslMapManagerActivity_trans_UpdateEventEntry(NULL,GLOBALTYPE_MAPMANAGER,GLOBALTYPE_EVENTCONTAINER,1,pEventDef->pchEventName,pActiveEvent->uEventTimeStart,iTimingIndex,pEventDef->iEventRunMode);			
		}

		// If we're not already active, inform the correct servers
		if (!bAlreadyActive)
		{
			FOR_EACH_IN_STASHTABLE(sGameServerListsByMapDescription, GameServerList, pList)
			{
				if (pList->eType == LISTTYPE_NORMAL)
				{
					int iServerNum;
					for (iServerNum=0; iServerNum < eaSize(&pList->ppGameServers); iServerNum++)
					{
						TrackedGameServerExe *pServer = pList->ppGameServers[iServerNum];
						RemoteCommand_Event_Start(GLOBALTYPE_GAMESERVER,pServer->iContainerID,pEventDef->pchEventName);
					}
				}
			}
			FOR_EACH_END
	
			for(i=0;i<eaSize(&pEventDef->ppActivities);i++)
			{
				aslMapManager_StartActivity(pEventDef->ppActivities[i]->pchActivityName,pActiveEvent);
			}
		}
	}
}

static void aslMapManager_StopEventEx(EventDef *pEventDef)
{
	int x;

	if(pEventDef)
	{
		ActiveEventEntry* pActiveEvent = aslEvent_GetActiveEvent(pEventDef);

		if (pActiveEvent!=NULL)
		{
			eaFindAndRemove(&gppActiveEvents,pActiveEvent);

			// Stop the event on each server
			FOR_EACH_IN_STASHTABLE(sGameServerListsByMapDescription, GameServerList, pList)
			{
				if (pList->eType == LISTTYPE_NORMAL)
				{
					int iServerNum;
					for (iServerNum=0; iServerNum < eaSize(&pList->ppGameServers); iServerNum++)
					{
						TrackedGameServerExe *pServer = pList->ppGameServers[iServerNum];
						RemoteCommand_Event_End(GLOBALTYPE_GAMESERVER,pServer->iContainerID,pEventDef->pchEventName);
					}
				}
			}
			FOR_EACH_END

			// Stop each of the events activities
			for(x=eaSize(&pActiveEvent->ppActivities)-1;x>=0;x--)
			{
				ActiveActivityEntry *pActivity = pActiveEvent->ppActivities[x];
				aslMapManager_StopActivityEx(&pActivity,pActiveEvent);
			}


			StructDestroy(parse_ActiveEventEntry,pActiveEvent);
		}

		// Update the DB regardless of if we were actually active or not.
		AutoTrans_aslMapManagerActivity_trans_UpdateEventEntry(NULL,GLOBALTYPE_MAPMANAGER,GLOBALTYPE_EVENTCONTAINER,1,pEventDef->pchEventName,0,0,
																			    pEventDef->iEventRunMode);
	}
}



///////////////////////////////////////////////////////////////////////////////////
//  Event Run Mode

static void aslMapManager_SetEventRunMode(EventDef *pEventDef, int iRunMode)
{
	if (pEventDef->iEventRunMode!=iRunMode)
	{
		pEventDef->iEventRunMode = iRunMode;

		FOR_EACH_IN_STASHTABLE(sGameServerListsByMapDescription, GameServerList, pList)
		{
			if (pList->eType == LISTTYPE_NORMAL)
			{
				int iServerNum;
				for (iServerNum=0; iServerNum < eaSize(&pList->ppGameServers); iServerNum++)
				{
					TrackedGameServerExe *pServer = pList->ppGameServers[iServerNum];
					RemoteCommand_Event_SetRunMode(GLOBALTYPE_GAMESERVER,pServer->iContainerID, pEventDef->pchEventName, pEventDef->iEventRunMode);
				}
			}
		}
		FOR_EACH_END
	}
}

AUTO_COMMAND;
void aslMapManager_SetEventOn(const char *pchEvent)
{
	EventDef *pEventDef = EventDef_Find(pchEvent);
	
	if (pEventDef!=NULL && pEventDef->iEventRunMode != kEventRunMode_ForceOn)
	{
		aslMapManager_SetEventRunMode(pEventDef, kEventRunMode_ForceOn);
		aslMapManager_StartEventEx(pEventDef, -1);
	}
}

AUTO_COMMAND;
void aslMapManager_SetEventOff(const char *pchEvent)
{
	EventDef *pEventDef = EventDef_Find(pchEvent);

	if (pEventDef!=NULL && pEventDef->iEventRunMode != kEventRunMode_ForceOff)
	{
		aslMapManager_SetEventRunMode(pEventDef, kEventRunMode_ForceOff);
		aslMapManager_StopEventEx(pEventDef);
	}
}

AUTO_COMMAND;
void aslMapManager_SetEventAuto(const char *pchEvent)
{
	int iIndexOut=-1;
	EventDef *pEventDef = EventDef_Find(pchEvent);

	if (pEventDef!=NULL && pEventDef->iEventRunMode != kEventRunMode_Auto)		
	{
		aslMapManager_SetEventRunMode(pEventDef, kEventRunMode_Auto);
		
		if (aslEventDef_ShouldBeOn(pEventDef, &iIndexOut))
		{
			// If it should be on, just call start. This will do the right thing whether it was running before or not
			aslMapManager_StartEventEx(pEventDef, iIndexOut);
		}
		else
		{
			aslMapManager_StopEventEx(pEventDef);
		}
	}
}


///////////////////////////////////////////////////////////////////////////////////
//  Start/Tick/Update

static void aslMapManagerEvent_StartEventSystem()
{
	// First time here since starting up the Map Manager. Either the whole shard is starting,
	//   or just the map manager is starting. Restart any events that were running last time
	//   we were up. Some of these events may immediately get shut down, but we need to
	//   have the global lists set up properly to do so.

	int i;
	EventContainer *pContainer = GET_REF(s_EventContainerRef.hRef);

	if (!pContainer)
	{
		return;
	}

	// Reset the event clock
	SetEventClockDelta(0);

	// Restart are all events which were running the last time the Map Manager was up.
	for(i=0;i<eaSize(&pContainer->ppEntries);i++)
	{
		EventEntry *pEntry = pContainer->ppEntries[i];
		EventDef *pEventDef = EventDef_Find(pEntry->pchEventName);

		// If the data changed since the last time we saved data to the container, we could have a couple of
		// problems: the event in the container may longer exist, or its timing data may have changed.
		// the stored iTimingIndex may no longer refer to the correct TimingIndex, and, in fact, may
		// refer to a now non-existant TimingIndex.  Just do the best we can, and let the actual
		// event tick clean stuff up for us. Don't forget this code also is handling the case where
		// the MapManager went down by itself in a shard and is now restarting. It's important that
		// we at least try to restore the running events so they can be properly shut down and any
		// still running GameServers be informed.

		if (pEntry!=NULL && pEventDef!=NULL)
		{
			if (pEntry->iEventRunMode!=kEventRunMode_Auto)
			{
				aslMapManager_SetEventRunMode(pEventDef, pEntry->iEventRunMode);
			}
			if (pEntry->uLastTimeStarted > 0)
			{
				if (pEntry->iTimingIndex < eaSize(&pEventDef->ShardTimingDef.ppTimingEntries))
				{
					aslMapManager_StartEventEx(pEventDef, pEntry->iTimingIndex);
				}
				else
				{
					// The TimingIndex no longer exists. Data files must have changed, so update this entry in the container and don't start it.
					AutoTrans_aslMapManagerActivity_trans_UpdateEventEntry(NULL,GLOBALTYPE_MAPMANAGER,GLOBALTYPE_EVENTCONTAINER,1,pEventDef->pchEventName,0,0,
																			    pEventDef->iEventRunMode);
				}
			}
		}
	}
}

static bool aslMapManager_ResolveEventContainer()
{
	static U32 s_iShardVarTickCount = 0;
	
	if(!IS_HANDLE_ACTIVE(s_EventContainerRef.hRef))
	{
		char idBuf[128];
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_EVENTCONTAINER), ContainerIDToString(EVENT_VAR_CONTAINER_ID, idBuf),s_EventContainerRef.hRef);
		return(false);
	}
	else if(!GET_REF(s_EventContainerRef.hRef))
	{
		s_iShardVarTickCount++;
		if ((s_iShardVarTickCount % 180) == 0)
		{
			// Re-request missing resources so  reference gets filled
			resReRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_EVENTCONTAINER));
		}
		return(false);
	}
	return(true);
}


void aslMapManagerEvent_Tick()
{
	U32 uTimeNow = GetEventClockTime();
	static U32 uTimeLast = 0;
	int iIndex;
	RefDictIterator eventIterator;
	EventDef *pEventDef;

	// If we have events and we haven't yet resolved our container reference, wait until it is resolved.
	if(RefSystem_GetDictionaryNumberOfReferents(g_hEventDictionary) && uTimeLast == 0)
	{
		if (!aslMapManager_ResolveEventContainer())
		{
			return;
		}
	}

	if(uTimeLast == uTimeNow)
		return;

	if (uTimeLast==0)
	{
		aslMapManagerEvent_StartEventSystem();
	}

	// Tick-to-tick management of the events.
	RefSystem_InitRefDictIterator(g_hEventDictionary, &eventIterator);
	while(pEventDef = (EventDef *)RefSystem_GetNextReferentFromIterator(&eventIterator))
	{
		ActiveEventEntry *pEvent = aslEvent_GetActiveEvent(pEventDef);

		if(pEvent)
		{
			int i;

			for(i=eaSize(&pEvent->ppDelayActivities)-1;i>=0;i--)
			{
				ActiveActivityEntry *pActivity = pEvent->ppDelayActivities[i];

				if(pActivity->uActivityTimeStart <= uTimeNow)
				{
					eaRemove(&pEvent->ppDelayActivities,i);
					eaPush(&pEvent->ppActivities,pActivity);

					aslMapManager_SendStartActivityToMaps(pEventDef,pActivity);	
				}
			}

			if(!aslActiveEvent_ShouldBeOn(pEvent))
			{
				// Our last timerIndex timing event finished. Check to see if we should still be running anyway possibly on another index
				if(aslEventDef_ShouldBeOn(pEventDef, &iIndex))
				{
					// Start with the new index. Update the DB, etc.
					aslMapManager_StartEventEx(pEventDef, iIndex);
				}
				else
				{
					aslMapManager_StopEventEx(pEventDef);
				}
			}
			else
			{
				// Turn off any activities that have a duration that has expired
				for(i=eaSize(&pEvent->ppActivities)-1;i>=0;i--)
				{
					ActiveActivityEntry *pActivity = pEvent->ppActivities[i];

					if(pActivity->uActivityTimeEnd <= uTimeNow)
					{
						aslMapManager_StopActivityEx(&pActivity,pEvent);
					}
				}
			}
		}
		// WOLF[14Feb12] We used to check aslEventDef_ShouldBeStarted here. This is insufficient to deal with the case
		//   where the map manager went down before an event was supposed to start, and comes up while it is supposed
		//   to be runnning after that. Now just check if it should be on. If we need to detect the actual start time,
		//   we can switch to ShouldBeStarted again, and turn on ShouldBeOn events in _StartEventSystem above.
		//		else if(aslEventDef_ShouldBeStarted(pDef,uTimeLast,uTimeNow,&iIndex))
		else if(aslEventDef_ShouldBeOn(pEventDef, &iIndex))
		{
			aslMapManager_StartEventEx(pEventDef, iIndex);
		}
	}

	uTimeLast = uTimeNow;
}

///////////////////////////////////////////////////////////////////////////////////
//  Remote commands Activities.
//    These should only be used by internal testing and not on a live shard.

AUTO_COMMAND_REMOTE;
void MapManager_StartActivity(const char *pchActivity)
{
	aslMapManager_StartActivity(pchActivity, NULL);
}

AUTO_COMMAND_REMOTE;
void MapManager_StopActivity(const char *pchActivity)
{
	aslMapManager_StopUnownedActivity(pchActivity);
}

///////////////////////////////////////////////////////////////////////////////////
//  Remote commands Events

AUTO_COMMAND_REMOTE;
void MapManager_StartEvent(const char *pchEvent)
{
	aslMapManager_SetEventOn(pchEvent);
}

AUTO_COMMAND_REMOTE;
void MapManager_StopEvent(const char *pchEvent)
{
	aslMapManager_SetEventOff(pchEvent);
}

AUTO_COMMAND_REMOTE;
void MapManager_AutoEvent(const char *pchEvent)
{
	aslMapManager_SetEventAuto(pchEvent);
}

///////////////////////////////////////////////////////////////////////////////////
//  Monitor info

static StashTable sEventListForMonitor = NULL;


static void _doTimerPrint(char **eOut, int iDelta, const char *Message)
{
	int iMinutes = abs(iDelta)/60 + 1;
	if (iMinutes < 60)
	{
		estrPrintf(eOut,"%s %dm",Message,iMinutes);
	}
	else
	{
		int iHours = iMinutes/60;
		iMinutes = iMinutes%60;
		if (iHours < 24)
		{
			estrPrintf(eOut,"%s %dh%dm",Message,iHours,iMinutes);
		}
		else
		{
			int iDays = iHours/24;
			iHours = iHours%24;
			estrPrintf(eOut,"%s %dd%dh%dm",Message,iDays,iHours,iMinutes);
		}
	}
}



AUTO_FIXUPFUNC;
TextParserResult fixupEventList(EventList* pList, enumTextParserFixupType eType, void *pExtraData)
{
	int i;
	U32 uLastStart,uEndOfLastStart,uNextStart;
	bool bIsRunning=false;
	bool bIsManual=false;
	U32 uCurrentTime = GetEventClockTime();
	EventDef *pEventDef = EventDef_Find(pList->pEventName);
				
	switch (eType)
	{
	case FIXUPTYPE_ABOUT_TO_BE_SERVERMONITORED:

		if (pEventDef->iEventRunMode==kEventRunMode_ForceOn)
		{
			pList->pStatus = allocAddString("Manual On");
			bIsRunning=true;
		}
		else if (pEventDef->iEventRunMode==kEventRunMode_ForceOff)
		{
			pList->pStatus = allocAddString("Manual Off");
		}
		else
		{
			pList->pStatus = allocAddString("Stopped");

			for(i=0;i<eaSize(&gppActiveEvents);i++)
			{
				EventDef *pCurrentEventDef = GET_REF(gppActiveEvents[i]->hEvent);
				if(pCurrentEventDef && pCurrentEventDef->pchEventName == pList->pEventName)
				{
					bIsRunning=true;
					pList->pStatus = allocAddString("Running");
					break;
				}
			}
		}
		
		if (aslEvent_GetUsefulTimes(pList->pEventName, &uLastStart, &uEndOfLastStart, &uNextStart))
		{
			//////////////////////////////
			///   Start/End dates
			
			if (uLastStart>0)
			{
				estrPrintf(&pList->pLastStartDatePAC,"PAC%s",timeGetPACDateStringFromSecondsSince2000(uLastStart));
				estrPrintf(&pList->pLastStartDateUTC,"UTC%s",timeGetDateStringFromSecondsSince2000(uLastStart));
			}
			else
			{
				estrCopy2(&pList->pLastStartDatePAC,"---");
				estrCopy2(&pList->pLastStartDateUTC,"---");
			}
			
			if (uEndOfLastStart>0 && uEndOfLastStart<0xffffffff)
			{
				estrPrintf(&pList->pEndDatePAC,"PAC%s",timeGetPACDateStringFromSecondsSince2000(uEndOfLastStart));
				estrPrintf(&pList->pEndDateUTC,"UTC%s",timeGetDateStringFromSecondsSince2000(uEndOfLastStart));
			}
			else
			{
				estrCopy2(&pList->pEndDatePAC,"---");
				estrCopy2(&pList->pEndDateUTC,"---");
			}

			if (uNextStart<0xffffffff && uNextStart > 0)
			{
				estrPrintf(&pList->pNextStartDatePAC,"PAC%s",timeGetPACDateStringFromSecondsSince2000(uNextStart));
				estrPrintf(&pList->pNextStartDateUTC,"UTC%s",timeGetDateStringFromSecondsSince2000(uNextStart));
			}
			else
			{
				estrCopy2(&pList->pNextStartDatePAC,"---");
				estrCopy2(&pList->pNextStartDateUTC,"---");
			}
			
			//////////////////////////////
			///   NextHappening
			if (bIsRunning)
			{
				if (pEventDef->iEventRunMode==kEventRunMode_ForceOn || uEndOfLastStart==0 || uEndOfLastStart==0xffffffff)
				{
					estrPrintf(&pList->pNextHappeningStatus,"Will End: Never");
				}
				else
				{
					_doTimerPrint(&pList->pNextHappeningStatus,uEndOfLastStart-uCurrentTime,"Will End:");
				}
			}
			else
			{
				if (pEventDef->iEventRunMode==kEventRunMode_ForceOff || uNextStart==0 || uNextStart==0xffffffff)
				{
					estrPrintf(&pList->pNextHappeningStatus,"Will Start: Never");
				}
				else
				{
					_doTimerPrint(&pList->pNextHappeningStatus,uNextStart-uCurrentTime,"Will Start:");
				}
			}
		}
		else
		{
			estrCopy2(&pList->pLastStartDatePAC,"Unknown");		
			estrCopy2(&pList->pEndDatePAC,"Unknown");		
			estrCopy2(&pList->pNextStartDatePAC,"Unknown");
			estrCopy2(&pList->pLastStartDateUTC,"Unknown");		
			estrCopy2(&pList->pEndDateUTC,"Unknown");		
			estrCopy2(&pList->pNextStartDateUTC,"Unknown");
		}

		
		break;
	}

	return 1;
}


void aslMapManagerActivity_InitEventList(void)
{
	EventDef *pEventDef;
	RefDictIterator eventIterator;

	RefSystem_InitRefDictIterator(g_hEventDictionary, &eventIterator);
	while(pEventDef = (EventDef *)RefSystem_GetNextReferentFromIterator(&eventIterator))
	{
		EventList *pEventList = StructCreate(parse_EventList);
		pEventList->pEventName = allocAddString(pEventDef->pchEventName);

		pEventList->pStatus = allocAddString("Stopped");

		estrCreate(&pEventList->pNextHappeningStatus);

		estrCreate(&pEventList->pLastStartDatePAC);
		estrCreate(&pEventList->pEndDatePAC);
		estrCreate(&pEventList->pNextStartDatePAC);
		estrCreate(&pEventList->pLastStartDateUTC);
		estrCreate(&pEventList->pEndDateUTC);
		estrCreate(&pEventList->pNextStartDateUTC);

		if (stashRemovePointer(sEventListForMonitor, pEventList->pEventName, NULL))
		{
			Errorf("Duplicate Event Name Found: %s", pEventList->pEventName);
		}
		stashAddPointer(sEventListForMonitor, pEventList->pEventName, pEventList, false);
	}
}

AUTO_RUN;
void aslMapManagerActivityInitEventStashTable(void)
{
	if (GetAppGlobalType() == GLOBALTYPE_MAPMANAGER)
	{
		sEventListForMonitor = stashTableCreateWithStringKeys(256, StashDefault);
		resRegisterDictionaryForStashTable("EventInfo",  RESCATEGORY_OTHER, 0, sEventListForMonitor, parse_EventList);
	}
}



#include "AutoGen/aslMapManagerActivity_h_ast.c"
