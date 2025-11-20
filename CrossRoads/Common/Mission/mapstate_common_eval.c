#include "Entity.h"
#include "Expression.h"
#include "mapstate_common.h"
#include "WorldVariable.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


// ----------------------------------------------------------------------------------
// Map State Expression Functions
// ----------------------------------------------------------------------------------

AUTO_EXPR_FUNC(gameutil,UIGen) ACMD_NAME(PlayerMapValueGet);
ExprFuncReturnVal exprFuncPlayerMapValueGet(ACMD_EXPR_INT_OUT piValOut, SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *pcValueName, ACMD_EXPR_ERRSTRING errString)
{
	MapState *pState = mapState_FromEnt(pEntity);
	MultiVal *pmvMapValue = mapState_GetPlayerValue(pState, pEntity, pcValueName);

	if (pmvMapValue)
	{
		ANALYSIS_ASSUME(pmvMapValue != NULL);
		if (MultiValIsNumber(pmvMapValue))
		{
			(*piValOut) = MultiValGetInt(pmvMapValue, NULL);
		}
		else
		{
			(*piValOut) = 0;
		}
	}
	else
	{
		(*piValOut) = 0;
	}

	return ExprFuncReturnFinished;
}


AUTO_EXPR_FUNC(gameutil,UIGen) ACMD_NAME(PlayerMapStringGet);
ExprFuncReturnVal exprFuncPlayerMapStringGet(ACMD_EXPR_STRING_OUT ppcValOut, SA_PARAM_NN_VALID Entity *pEntity, SA_PARAM_NN_STR const char *pcValueName, ACMD_EXPR_ERRSTRING errString)
{
	MapState *pState = mapState_FromEnt(pEntity);
	MultiVal *pmvMapValue = mapState_GetPlayerValue(pState, pEntity, pcValueName);

	if (pmvMapValue)
	{
		ANALYSIS_ASSUME(pmvMapValue != NULL);
		if (MultiValIsString(pmvMapValue))
		{
			(*ppcValOut) = MultiValGetString(pmvMapValue, NULL);
		}
		else
		{
			(*ppcValOut) = 0;
		}
	}
	else
	{
		(*ppcValOut) = 0;
	}

	return ExprFuncReturnFinished;
}


