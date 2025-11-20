#include "ArmamentSwapCommon.h"
#include "earray.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// ------------------------------------------------------------------------------------------------------------
ArmamentActiveItemSwap* findQueuedActiveItemSwap(ArmamentSwapInfo *pArmamentSwap, S32 bagId, S32 index)
{
	FOR_EACH_IN_EARRAY(pArmamentSwap->eaActiveItemSwap, ArmamentActiveItemSwap, pitem)
		if (pitem->iBagID == bagId && pitem->iIndex == index)
			return pitem;
	FOR_EACH_END
	
	return NULL;
}


#include "ArmamentSwapCommon_h_ast.c"
