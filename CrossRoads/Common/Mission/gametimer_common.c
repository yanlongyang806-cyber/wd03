#include "gametimer_common.h"

#include "textparserUtils.h"
#include "timing.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

#define MAX_GAME_TIMERS 10
static int s_iNumGameTimers = 0;

F32 gametimer_GetRemainingSeconds(GameTimer *timer)
{
	if (!timer)
		return -60;
	return (timer->durationSeconds - timerElapsed(timer->timer));
}

void gametimer_AddTime(GameTimer *timer, F32 seconds)
{
	if (timer)
		timerAdd(timer->timer, -seconds); // subtract the time from the timer
}

GameTimer *gametimer_Create(const char *name, F32 durationSeconds)
{
	if (s_iNumGameTimers < MAX_GAME_TIMERS)
	{
		GameTimer *timer = calloc(1, sizeof(GameTimer));
		timer->name = strdup(name);
		timer->visible = true;
		timer->timer = timerAlloc();
		timer->durationSeconds = durationSeconds;
		s_iNumGameTimers++;
		return timer;
	}
	Errorf("Error: Too may GameTimers allocated!");
	return NULL;
}

void gametimer_Destroy(GameTimer *timer)
{
	timerFree(timer->timer);
	free(timer->name);
	free(timer);
	s_iNumGameTimers--;
}

AUTO_FIXUPFUNC;
TextParserResult fixupGameTimer(GameTimer* timer, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
		xcase FIXUPTYPE_DESTRUCTOR:
		{
			timerFree(timer->timer);
			s_iNumGameTimers--;
		}
	}
	
	return 1;
}

#include "AutoGen/gametimer_common_h_ast.c"