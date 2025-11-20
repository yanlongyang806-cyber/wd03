
#include "dynThread.h"


#include "dynAnimInterface.h"
#include "dynFxInterface.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_FXSystem););

//CommandQueue* pDTCmdQueue = NULL;
static bool bDTInitialized = false;

void dtInitSys(void)
{
	//pDTCmdQueue = CommandQueue_Create(1024, false);

	dtFxInitSys();
	dtAnimInitSys();

	bDTInitialized = true;
}


/*
void dtProcessQueue(void)
{
	if (pDTCmdQueue)
		CommandQueue_ExecuteAllCommands(pDTCmdQueue);
}
*/


bool dtInitialized(void)
{
	return bDTInitialized;
}