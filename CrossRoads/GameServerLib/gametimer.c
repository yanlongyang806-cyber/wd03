
#include "gametimer_common.h"
#include "gametimer.h"

#include "expression.h"
#include "entity.h"
#include "entityiterator.h"

#include "earray.h"
#include "timing.h"

#include "../AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

GameTimer** g_GlobalGameTimers = 0;

GameTimer* gametimer_GlobalTimerFromName(const char *timerName)
{
	int i, n = eaSize(&g_GlobalGameTimers);
	for (i = 0; i < n; i++)
	{
		if (timerName && g_GlobalGameTimers[i]->name && !stricmp(timerName, g_GlobalGameTimers[i]->name))
			return g_GlobalGameTimers[i];
	}
	return NULL;
}

void gametimer_RefreshGameTimersForPlayer(Entity *playerEnt)
{
	int i, n = eaSize(&g_GlobalGameTimers);
	for (i = 0; i < n; i++)
	{
		ClientCmd_ClientGameTimerUpdate(playerEnt, g_GlobalGameTimers[i]->name, gametimer_GetRemainingSeconds(g_GlobalGameTimers[i]));
	}
}

void gametimer_ClearAllTimersForPlayer(Entity *playerEnt)
{
	ClientCmd_ClientGameTimerClearAll(playerEnt);
}

// Starts a new Global timer with the specified name and duration.  If the timer already
// exists, it's reset to the specified time.  It starts running immediately.
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME("GameTimerGlobalStart");
void exprFuncGameTimerStart(ACMD_EXPR_PARTITION iPartitionIdx, const char *timerName, F32 durationSeconds)
{
	GameTimer *timer = gametimer_GlobalTimerFromName(timerName);
	Entity* currEnt;
	EntityIterator* iter;

	if (!timer && timerName && durationSeconds)
	{
		timer = gametimer_Create(timerName, durationSeconds);
		if (timer)
		{
			eaPush(&g_GlobalGameTimers, timer);
			timerStart(timer->timer);

			iter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
			while ((currEnt = EntityIteratorGetNext(iter)))
			{
				ClientCmd_ClientGameTimerStart(currEnt, timerName, durationSeconds);
			}
			EntityIteratorRelease(iter);
		}
	}
}

// Destroys the specified timer.  It disappears from all players' UI.
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME("GameTimerGlobalClear");
void exprFuncGameTimerClear(ACMD_EXPR_PARTITION iPartitionIdx, const char *timerName)
{
	GameTimer *timer = gametimer_GlobalTimerFromName(timerName);
	EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
	Entity* currEnt;

	if (timer)
	{
		eaFindAndRemove(&g_GlobalGameTimers, timer);
		gametimer_Destroy(timer);
	}

	while ((currEnt = EntityIteratorGetNext(iter)))
		ClientCmd_ClientGameTimerClear(currEnt, timerName);
	EntityIteratorRelease(iter);
}

// Gets the time remaining on the timer in seconds.  This can be negative, for
// example, -20 means the timer expired 20 seconds ago. If the timer doesn't exist, 
// it will return something less than 0.
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME("GameTimerGlobalGetTime");
F32 exprFuncGameTimerGetRemaining(const char *timerName)
{
	GameTimer *timer = gametimer_GlobalTimerFromName(timerName);
	return gametimer_GetRemainingSeconds(timer);
}

// Adds time to the specified timer.
AUTO_EXPR_FUNC(encounter_action) ACMD_NAME("GameTimerGlobalAddTime");
void exprFuncGameTimerAddTime(ACMD_EXPR_PARTITION iPartitionIdx, const char *timerName, F32 seconds)
{
	EntityIterator* iter = entGetIteratorSingleType(iPartitionIdx, 0, 0, GLOBALTYPE_ENTITYPLAYER);
	Entity* currEnt;
	GameTimer *timer = gametimer_GlobalTimerFromName(timerName);
	if (timer && gametimer_GetRemainingSeconds(timer) >= 0)
	{
		gametimer_AddTime(timer, seconds);
		while ((currEnt = EntityIteratorGetNext(iter)))
			ClientCmd_ClientGameTimerAddTime(currEnt, timerName, seconds);
	}
	EntityIteratorRelease(iter);
}