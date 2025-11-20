/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "GameClientLib.h"
#include "ActivityCalendar.h"
#include "gclActivity.h"
#include "Entity.h"
#include "Player.h"
#include "timing.h"
#include "time.h"
#include "gclEntity.h"
#include "ActivityCommon.h"

#include "UIGen.h"
#include "Expression.h"

#include "AutoGen/ActivityCalendar_h_ast.h"

#include "autogen/GameServerLib_autogen_ServerCmdWrappers.h"

typedef struct tm TimeStruct;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


////////////////////////////////////////////////////////////////
// Event Clock management

static S32 gsiEventClockDelta_Client=0;
static U32 gsuLastSync=0;
static U32 s_uNowFrame;

AUTO_COMMAND ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gclActivity_SetEventClockDelta(S32 iNewDelta)
{
	gsiEventClockDelta_Client = iNewDelta;
}

void gclActivity_EventClock_Synchronize(void)
{
	if (gGCLState.totalElapsedTimeMs != s_uNowFrame)
	{
		U32 uNow = timeServerSecondsSince2000();

		if (uNow < gsuLastSync || uNow > gsuLastSync + 30)
		{
			// 30 seconds or something big happened
			ServerCmd_gslActivity_EventClock_Synchronize();
			gsuLastSync = uNow;
		}
		s_uNowFrame = gGCLState.totalElapsedTimeMs;
	}
}

U32 gclActivity_EventClock_GetSecondsSince2000()
{
	U32 uNow = timeServerSecondsSince2000();

	gclActivity_EventClock_Synchronize();

	// NOTE: This can overflow. Not really a lot we can do about it other than limit MAX_EVENTCLOCK_DELTA in aslMapManagerActivity.c
	return(uNow + gsiEventClockDelta_Client);
}

AUTO_EXPR_FUNC(util) ACMD_NAME(EventClockSecondsSince2000);
S64 gclActivity_EventClockSecondsSince2000(ExprContext *pContext)
{
	return gclActivity_EventClock_GetSecondsSince2000();
}

// Indicates if there is a reminder set for the given event
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EventReminder_IsSet");
bool exprEventReminder_IsSet(const char *pchEventName)
{
	Entity *pEnt = entActivePlayerPtr();
	
	return pEnt && eaIndexedFindUsingString(&pEnt->pPlayer->pEventInfo->eaSubscribedEvents, pchEventName) >= 0;
}

// Creates a reminder for the given event for the occurrence that starts at uStartTime
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EventReminder_Set");
void exprEventReminder_Set(const char *pchEventName, U32 uStartTime)
{
	// Set a reminder for this event
	ServerCmd_SetEventReminder(pchEventName, uStartTime);
}

// Removes an existing event reminder
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EventReminder_Remove");
void exprEventReminder_Remove(const char *pchEventName)
{
	// Remove the event reminder
	if (exprEventReminder_IsSet(pchEventName))
	{
		ServerCmd_RemoveEventReminder(pchEventName);
	}	
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EventReminder_GetAllReminders");
void exprEventReminder_GetAllReminders(SA_PARAM_NN_VALID UIGen *pGen)
{
	Entity *pEnt = entActivePlayerPtr();

	PlayerSubscribedEvent ***peaSubscribedEventList = ui_GenGetManagedListSafe(pGen, PlayerSubscribedEvent);	

	S32 iCount = 0;

	if (pEnt)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pEnt->pPlayer->pEventInfo->eaSubscribedEvents, PlayerSubscribedEvent, pCurrentSubscribedEvent)
		{
			PlayerSubscribedEvent *pSubscribedEvent = eaGetStruct(peaSubscribedEventList, parse_PlayerSubscribedEvent, iCount++);
			StructCopyAll(parse_PlayerSubscribedEvent, pCurrentSubscribedEvent, pSubscribedEvent);
		}
		FOR_EACH_END
	}

	eaSetSizeStruct(peaSubscribedEventList, parse_PlayerSubscribedEvent, iCount);

	ui_GenSetManagedListSafe(pGen, peaSubscribedEventList, PlayerSubscribedEvent, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Event_GetDisplayName");
const char * exprEvent_GetDisplayName(SA_PARAM_OP_STR const char *pchEventName)
{
	EventDef *pEventDef = EventDef_Find(pchEventName);

	const char *pchReturnValue = NULL;

	if (pEventDef)
	{
		pchReturnValue = TranslateDisplayMessage(pEventDef->msgDisplayName);
	}

	return pchReturnValue ? pchReturnValue : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Event_GetDisplayShortDesc");
const char * exprEvent_GetDisplayShortDesc(SA_PARAM_OP_STR const char *pchEventName)
{
	EventDef *pEventDef = EventDef_Find(pchEventName);

	const char *pchReturnValue = NULL;

	if (pEventDef)
	{
		pchReturnValue = TranslateDisplayMessage(pEventDef->msgDisplayShortDesc);
	}

	return pchReturnValue ? pchReturnValue : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Event_GetDisplayLongDesc");
const char * exprEvent_GetDisplayLongDesc(SA_PARAM_OP_STR const char *pchEventName)
{
	EventDef *pEventDef = EventDef_Find(pchEventName);

	const char *pchReturnValue = NULL;

	if (pEventDef)
	{
		pchReturnValue = TranslateDisplayMessage(pEventDef->msgDisplayLongDesc);
	}

	return pchReturnValue ? pchReturnValue : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("Event_GetIcon");
const char * exprEvent_GetIcon(SA_PARAM_OP_STR const char *pchEventName)
{
	EventDef *pEventDef = EventDef_Find(pchEventName);

	if (pEventDef)
	{
		return pEventDef->pchIcon ? pEventDef->pchIcon : "";
	}
	return "";
}
