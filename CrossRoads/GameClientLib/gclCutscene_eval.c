#include "gclCutscene.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

extern ClientCutscene *g_ClientCutscene;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetCutsceneElapsedTimeInSeconds);
F32 exprGetCutsceneElapsedTimeInSeconds(void)
{
	return g_ClientCutscene ? MAX(0, g_ClientCutscene->elapsedTime) : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetCutsceneRemainingTimeInSeconds);
F32 exprGetCutsceneRemainingTimeInSeconds(void)
{
	return g_ClientCutscene ? MAX(0, g_ClientCutscene->runningTime - g_ClientCutscene->elapsedTime) : 0;
}
