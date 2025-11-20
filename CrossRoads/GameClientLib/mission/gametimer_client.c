/***************************************************************************
*     Copyright (c) 2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gametimer_common.h"
#include "mission_common.h"
#include "NotifyCommon.h"

#include "gclEntity.h"
#include "EString.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static GameTimer* gametimer_ClientTimerFromName(const char *timerName)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(entActivePlayerPtr());
	if (pInfo)
	{
		int i, n = eaSize(&pInfo->clientGameTimers);
		for (i = 0; i < n; i++)
		{
			if (timerName && pInfo->clientGameTimers[i]->name && !stricmp(timerName, pInfo->clientGameTimers[i]->name))
				return pInfo->clientGameTimers[i];
		}
	}
	return NULL;
}


AUTO_COMMAND ACMD_NAME("ClientGameTimerStart") ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gametimer_ClientGameTimerStart(const char *timerName, F32 durationSeconds)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(entActivePlayerPtr());
	if (pInfo)
	{
		GameTimer *timer = gametimer_ClientTimerFromName(timerName);

		if (!timer && timerName && durationSeconds)
		{
			timer = gametimer_Create(timerName, durationSeconds);
			if (timer)
			{
				eaPush(&pInfo->clientGameTimers, timer);
				timerStart(timer->timer);
			}
		}
	}
}

AUTO_COMMAND ACMD_NAME("ClientGameTimerUpdate") ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gametimer_ClientGameTimerUpdate(const char *timerName, F32 durationSeconds)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(entActivePlayerPtr());
	if (pInfo)
	{
		GameTimer *timer = gametimer_ClientTimerFromName(timerName);

		if (!timer && timerName && durationSeconds)
		{
			timer = gametimer_Create(timerName, durationSeconds);
			if (timer)
				eaPush(&pInfo->clientGameTimers, timer);
		}

		if (timer && durationSeconds > -60.0f)
		{
			// Adjust the time to match the server
			// TODO - some more sophisticated way of staying in sync...?
			gametimer_AddTime(timer, durationSeconds - gametimer_GetRemainingSeconds(timer));
		}
		else if (timer && durationSeconds <= -60.0f)
		{
			// If this timer has expired, clean it up
			eaFindAndRemove(&pInfo->clientGameTimers, timer);
			gametimer_Destroy(timer);
		}
	}
}

AUTO_COMMAND ACMD_NAME("ClientGameTimerAddTime") ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gametimer_ClientGameTimerAddTime(const char *timerName, U32 seconds)
{
	GameTimer *timer = gametimer_ClientTimerFromName(timerName);
	if (timer)
	{
		// Send a floater telling the player the good news!
		char *tmpStr = NULL;
		estrStackCreate(&tmpStr);
		estrPrintf(&tmpStr, "+%d", seconds);
		notify_NotifySend(entActivePlayerPtr(), kNotifyType_GameTimerTimeAdded, tmpStr, NULL, NULL);
		estrDestroy(&tmpStr);
		
		gametimer_AddTime(timer, seconds);
	}
}

AUTO_COMMAND ACMD_NAME("ClientGameTimerClearAll") ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gametimer_ClientGameTimerClearAll(void)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(entActivePlayerPtr());
	if (pInfo)
		eaClearEx(&pInfo->clientGameTimers, gametimer_Destroy);
}

AUTO_COMMAND ACMD_NAME("ClientGameTimerClear") ACMD_CLIENTCMD ACMD_PRIVATE ACMD_ACCESSLEVEL(0);
void gametimer_ClientGameTimerClear(const char *timerName)
{
	MissionInfo *pInfo = mission_GetInfoFromPlayer(entActivePlayerPtr());
	if (pInfo)
	{
		int i, n = eaSize(&pInfo->clientGameTimers);
		for (i = n-1; i >= 0; --i)
		{
			if (timerName && pInfo->clientGameTimers[i]->name && !stricmp(timerName, pInfo->clientGameTimers[i]->name))
			{
				GameTimer *timer = eaRemove(&pInfo->clientGameTimers, i);
				gametimer_Destroy(timer);
			}
		}
	}
}

